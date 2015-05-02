/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009, 2014
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * SFS filesystem
 *
 * Filesystem-level interface routines.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <array.h>
#include <bitmap.h>
#include <synch.h>
#include <uio.h>
#include <vfs.h>
#include <buf.h>
#include <device.h>
#include <sfs.h>
#include "sfsprivate.h"

#define rdebug kprintf

/* Shortcuts for the size macros in kern/sfs.h */
#define SFS_FS_NBLOCKS(sfs)        ((sfs)->sfs_sb.sb_nblocks)
#define SFS_FS_FREEMAPBITS(sfs)    SFS_FREEMAPBITS(SFS_FS_NBLOCKS(sfs))
#define SFS_FS_FREEMAPBLOCKS(sfs)  SFS_FREEMAPBLOCKS(SFS_FS_NBLOCKS(sfs))

/*
 * Routine for doing I/O (reads or writes) on the free block bitmap.
 * We always do the whole bitmap at once; writing individual sectors
 * might or might not be a worthwhile optimization. Similarly, storing
 * the freemap in the buffer cache might or might not be a worthwhile
 * optimization. (But that would require a total rewrite of the way
 * it's handled, so not now.)
 *
 * The free block bitmap consists of SFS_FREEMAPBLOCKS 512-byte
 * sectors of bits, one bit for each sector on the filesystem. The
 * number of blocks in the bitmap is thus rounded up to the nearest
 * multiple of 512*8 = 4096. (This rounded number is SFS_FREEMAPBITS.)
 * This means that the bitmap will (in general) contain space for some
 * number of invalid sectors that are actually beyond the end of the
 * disk device. This is ok. These sectors are supposed to be marked
 * "in use" by mksfs and never get marked "free".
 *
 * The sectors used by the superblock and the bitmap itself are
 * likewise marked in use by mksfs.
 */
static
int
sfs_freemapio(struct sfs_fs *sfs, enum uio_rw rw)
{
	uint32_t j, freemapblocks;
	char *freemapdata;
	int result;

	KASSERT(lock_do_i_hold(sfs->sfs_freemaplock));

	/* Number of blocks in the free block bitmap. */
	freemapblocks = SFS_FS_FREEMAPBLOCKS(sfs);

	/* Pointer to our freemap data in memory. */
	freemapdata = bitmap_getdata(sfs->sfs_freemap);

	/* For each block in the free block bitmap... */
	for (j=0; j<freemapblocks; j++) {

		/* Get a pointer to its data */
		void *ptr = freemapdata + j*SFS_BLOCKSIZE;

		/* and read or write it. The freemap starts at sector 2. */
		if (rw == UIO_READ) {
			result = sfs_readblock(&sfs->sfs_absfs,
					       SFS_FREEMAP_START + j,
					       ptr, SFS_BLOCKSIZE);
		}
		else {
			sfs->newest_freemap_lsn = 0;
			sfs->oldest_freemap_lsn = 0;
			sfs_jphys_flush(sfs, sfs->newest_freemap_lsn);
			result = sfs_writeblock(&sfs->sfs_absfs,
						SFS_FREEMAP_START + j, NULL,
						ptr, SFS_BLOCKSIZE);
		}

		/* If we failed, stop. */
		if (result) {
			return result;
		}
	}
	return 0;
}

/*
 * Sync routine. This is what gets invoked if you do FS_SYNC on the
 * sfs filesystem structure.
 */
static
int
sfs_sync(struct fs *fs)
{
	struct sfs_fs *sfs;
	int result;


	/*
	 * Get the sfs_fs from the generic abstract fs.
	 *
	 * Note that the abstract struct fs, which is all the VFS
	 * layer knows about, is actually a member of struct sfs_fs.
	 * The pointer in the struct fs points back to the top of the
	 * struct sfs_fs - essentially the same object. This can be a
	 * little confusing at first.
	 *
	 * The following diagram may help:
	 *
	 *     struct sfs_fs        <-------------\
         *           :                            |
         *           :   sfs_absfs (struct fs)    |   <------\
         *           :      :                     |          |
         *           :      :  various members    |          |
         *           :      :                     |          |
         *           :      :  fs_data  ----------/          |
         *           :      :                             ...|...
         *           :                                   .  VFS  .
         *           :                                   . layer .
         *           :   other members                    .......
         *           :
         *           :
	 *
	 * This construct is repeated with vnodes and devices and other
	 * similar things all over the place in OS/161, so taking the
	 * time to straighten it out in your mind is worthwhile.
	 */

	sfs = fs->fs_data;

	/* Sync the buffer cache */
	result = sync_fs_buffers(fs);
	if (result) {
		return result;
	}

	lock_acquire(sfs->sfs_freemaplock);

	/* If the free block map needs to be written, write it. */
	if (sfs->sfs_freemapdirty) {
		result = sfs_freemapio(sfs, UIO_WRITE);
		if (result) {
			lock_release(sfs->sfs_freemaplock);
			return result;
		}
		sfs->sfs_freemapdirty = false;
	}

	/* If the superblock needs to be written, write it. */
	if (sfs->sfs_superdirty) {
		result = sfs_writeblock(&sfs->sfs_absfs, SFS_SUPER_BLOCK,
					NULL,
					&sfs->sfs_sb, sizeof(sfs->sfs_sb));
		if (result) {
			lock_release(sfs->sfs_freemaplock);
			return result;
		}
		sfs->sfs_superdirty = false;
	}

	lock_release(sfs->sfs_freemaplock);

	result = sfs_jphys_flushall(sfs);
	if (result) {
		return result;
	}

	return 0;
}

/*
 * Code called when buffers are attached to and detached from the fs.
 * This can allocate and destroy fs-specific buffer data. We don't
 * currently have any, but later changes might want to.
 */
static
int
sfs_attachbuf(struct fs *fs, daddr_t diskblock, struct buf *buf)
{
	struct sfs_fs *sfs = fs->fs_data;
	void *olddata;
	struct b_fsdata *newdata = kmalloc(sizeof(struct b_fsdata));

	newdata->sfs = sfs;
	newdata->diskblock = diskblock;
	newdata->oldest_lsn = 0;
	newdata->newest_lsn = 0;
	newdata->buf = buf;

	olddata = buffer_set_fsdata(buf, (void*)newdata);
	KASSERT(olddata == NULL);

	return 0;
}

static
void
sfs_detachbuf(struct fs *fs, daddr_t diskblock, struct buf *buf)
{
	struct sfs_fs *sfs = fs->fs_data;
	void *bufdata;

	(void)sfs;
	(void)diskblock;

	bufdata = buffer_set_fsdata(buf, NULL);
	kfree(bufdata);
}

/*
 * Routine to retrieve the volume name. Filesystems can be referred
 * to by their volume name followed by a colon as well as the name
 * of the device they're mounted on.
 */
static
const char *
sfs_getvolname(struct fs *fs)
{
	struct sfs_fs *sfs = fs->fs_data;

	/*
	 * VFS only uses the volume name transiently, and its
	 * synchronization guarantees that we will not disappear while
	 * it's using the name. Furthermore, we don't permit the volume
	 * name to change on the fly (this is also a restriction in VFS)
	 * so there's no need to synchronize.
	 */

	return sfs->sfs_sb.sb_volname;
}

/*
 * Destructor for struct sfs_fs.
 */
static
void
sfs_fs_destroy(struct sfs_fs *sfs)
{
	sfs_jphys_destroy(sfs->sfs_jphys);
	lock_destroy(sfs->sfs_renamelock);
	lock_destroy(sfs->sfs_freemaplock);
	lock_destroy(sfs->sfs_vnlock);
	if (sfs->sfs_freemap != NULL) {
		bitmap_destroy(sfs->sfs_freemap);
	}
	vnodearray_destroy(sfs->sfs_vnodes);
	KASSERT(sfs->sfs_device == NULL);
	kfree(sfs);
}

/*
 * Unmount code.
 *
 * VFS calls FS_SYNC on the filesystem prior to unmounting it.
 */
static
int
sfs_unmount(struct fs *fs)
{
	struct sfs_fs *sfs = fs->fs_data;


	lock_acquire(sfs->sfs_vnlock);
	lock_acquire(sfs->sfs_freemaplock);

	/* Do we have any files open? If so, can't unmount. */
	if (vnodearray_num(sfs->sfs_vnodes) > 0) {
		lock_release(sfs->sfs_freemaplock);
		lock_release(sfs->sfs_vnlock);
		return EBUSY;
	}

	sfs_jphys_stopwriting(sfs);

	unreserve_fsmanaged_buffers(2, SFS_BLOCKSIZE);

	/* We should have just had sfs_sync called. */
	KASSERT(sfs->sfs_superdirty == false);
	KASSERT(sfs->sfs_freemapdirty == false);

	/* All buffers should be clean; invalidate them. */
	drop_fs_buffers(fs);

	/* The vfs layer takes care of the device for us */
	sfs->sfs_device = NULL;

	/* Release the locks. VFS guarantees we can do this safely. */
	lock_release(sfs->sfs_vnlock);
	lock_release(sfs->sfs_freemaplock);

	/* Destroy the fs object; once we start nuking stuff we can't fail. */
	sfs_fs_destroy(sfs);

	/* nothing else to do */
	return 0;
}

/*
 * File system operations table.
 */
static const struct fs_ops sfs_fsops = {
	.fsop_sync = sfs_sync,
	.fsop_getvolname = sfs_getvolname,
	.fsop_getroot = sfs_getroot,
	.fsop_unmount = sfs_unmount,
	.fsop_readblock = sfs_readblock,
	.fsop_writeblock = sfs_writeblock,
	.fsop_attachbuf = sfs_attachbuf,
	.fsop_detachbuf = sfs_detachbuf,
};

/*
 * Basic constructor for struct sfs_fs. This initializes all fields
 * but skips stuff that requires reading the volume, like allocating
 * the freemap.
 */
static
struct sfs_fs *
sfs_fs_create(void)
{
	struct sfs_fs *sfs;

	/*
	 * Make sure our on-disk structures aren't messed up
	 */
	COMPILE_ASSERT(sizeof(struct sfs_superblock)==SFS_BLOCKSIZE);
	COMPILE_ASSERT(sizeof(struct sfs_dinode)==SFS_BLOCKSIZE);
	COMPILE_ASSERT(SFS_BLOCKSIZE % sizeof(struct sfs_direntry) == 0);

	/* Allocate object */
	sfs = kmalloc(sizeof(struct sfs_fs));
	if (sfs==NULL) {
		goto fail;
	}

	/*
	 * Fill in fields
	 */

	/* abstract vfs-level fs */
	sfs->sfs_absfs.fs_data = sfs;
	sfs->sfs_absfs.fs_ops = &sfs_fsops;

	/* superblock */
	/* (ignore sfs_super, we'll read in over it shortly) */
	sfs->sfs_superdirty = false;

	/* device we mount on */
	sfs->sfs_device = NULL;

	/* vnode table */
	sfs->sfs_vnodes = vnodearray_create();
	if (sfs->sfs_vnodes == NULL) {
		goto cleanup_object;
	}

	sfs->sfs_transactions = array_create();
	if (sfs->sfs_transactions == NULL) {
		goto cleanup_trans;
	}

	/* freemap */
	sfs->sfs_freemap = NULL;
	sfs->sfs_freemapdirty = false;
	sfs->newest_freemap_lsn = 0;
	sfs->oldest_freemap_lsn = 0;

	/* locks */
	sfs->trans_lock = lock_create("trans_lock");
	if (sfs->trans_lock == NULL) {
		goto cleanup_translock;
	}

	sfs->sfs_vnlock = lock_create("sfs_vnlock");
	if (sfs->sfs_vnlock == NULL) {
		goto cleanup_vnodes;
	}
	sfs->sfs_freemaplock = lock_create("sfs_freemaplock");
	if (sfs->sfs_freemaplock == NULL) {
		goto cleanup_vnlock;
	}
	sfs->sfs_renamelock = lock_create("sfs_renamelock");
	if (sfs->sfs_renamelock == NULL) {
		goto cleanup_freemaplock;
	}

	/* journal */
	sfs->sfs_jphys = sfs_jphys_create();
	if (sfs->sfs_jphys == NULL) {
		goto cleanup_renamelock;
	}

	return sfs;

cleanup_renamelock:
	lock_destroy(sfs->sfs_renamelock);
cleanup_freemaplock:
	lock_destroy(sfs->sfs_freemaplock);
cleanup_vnlock:
	lock_destroy(sfs->sfs_vnlock);
cleanup_translock:
	lock_destroy(sfs->trans_lock);
cleanup_vnodes:
	vnodearray_destroy(sfs->sfs_vnodes);
cleanup_trans:
	array_destroy(sfs->sfs_transactions);
cleanup_object:
	kfree(sfs);
fail:
	return NULL;
}


static
bool
operation_aborted(struct array* abort_list, sfs_lsn_t lsn)
{
	unsigned num, i;
	sfs_lsn_t *curlsn;
	num = array_num(abort_list);
	for (i = 0; i < num; i++) {
		curlsn = (sfs_lsn_t *) array_get(abort_list, i);
		if (*curlsn == lsn) {
			return true;
		}
	}
	return false;
}

static
int
sfs_recover_operation(struct sfs_fs *sfs, bool redo, void* recptr, struct bitmap *userdata, struct bitmap *garbage) {
	int result = 0;
	int type = *((int*)recptr);
	const char *operation[] = {"Undo", "Redo"};
	(void)garbage;	// Throw away the garbage. Don't need it here

	// data will contain the data for a block on disk. dinode will treat that data as an inode
	void *data = kmalloc(SFS_BLOCKSIZE);
	struct sfs_dinode *dinode = (struct sfs_dinode *)data;

	// Go ahead and load the data for changes that will need access to a block
	if (type == INODE_LINK || type == META_UPDATE || type == RESIZE ||
		type == INODE_UPDATE_TYPE || type == BLOCK_WRITE) {

		// Check if this is untouchable user data, and this operation was not
		//  the last to touch it. If so, do nothing
		int block = ((int*)recptr)[2];
		if (bitmap_isset(userdata, block) && type != BLOCK_WRITE) {
			return 0;
		} else if (type == BLOCK_WRITE) {
			// Even if this is untouchable, we still want to check that the
			//  very last user write completed successfully
			struct block_write_args *jentry = (struct block_write_args*)recptr;
			if (!jentry->last_write) {
				return 0;
			}
			// else, this was the last write to touch this block. Continue
		}

		// Load in the vnode and dinode stuff. Disk address is the 3rd member in all of these
		result = sfs_readblock(&sfs->sfs_absfs, block, data, SFS_BLOCKSIZE);
	}
	kprintf("We have a recovery record of type: %d", type);

	kprintf("    %s: ", operation[redo]);
	jentry_print(recptr);
	kprintf("\n");

	// Ugly braces inside each case. But it makes it easier to use the journal entries
	switch (type) {
		case BLOCK_ALLOC:
		{
			struct block_alloc_args *jentry = (struct block_alloc_args *)recptr;
			lock_acquire(sfs->sfs_freemaplock);
			if (redo) {
				if (!bitmap_isset(sfs->sfs_freemap, jentry->disk_addr))
					bitmap_mark(sfs->sfs_freemap, jentry->disk_addr);
			} else {
				if (bitmap_isset(sfs->sfs_freemap, jentry->disk_addr))
					bitmap_unmark(sfs->sfs_freemap, jentry->disk_addr);
			}
			lock_release(sfs->sfs_freemaplock);
		}
		break;
		case BLOCK_DEALLOC:
		{
			struct block_dealloc_args *jentry = (struct block_dealloc_args *)recptr;
			lock_acquire(sfs->sfs_freemaplock);
			if (redo) {
				if (bitmap_isset(sfs->sfs_freemap, jentry->disk_addr))
					bitmap_unmark(sfs->sfs_freemap, jentry->disk_addr);
			} else {
				if (!bitmap_isset(sfs->sfs_freemap, jentry->disk_addr))
					bitmap_mark(sfs->sfs_freemap, jentry->disk_addr);
			}
			lock_release(sfs->sfs_freemaplock);
		}
		break;
		case INODE_LINK:
		{
			struct inode_link_args *jentry = (struct inode_link_args *)recptr;
			int old, new;

			// Get what the change should be
			if (redo) {
				old = jentry->old_linkcount;
				new = jentry->new_linkcount;
			} else {
				old = jentry->new_linkcount;
				new = jentry->old_linkcount;
			}

			// Do the change, if necessary
			if (dinode->sfi_linkcount == old) {
				// Set the new data
				dinode->sfi_linkcount = new;

				// Write the entire block to disk
				result = sfs_writeblock(&sfs->sfs_absfs, jentry->disk_addr, 
					NULL, data, SFS_BLOCKSIZE);

				rdebug("Setting linkcount of %d from %d to %d\n",
					jentry->disk_addr, old, new);
			} else {
				rdebug("Linkcount of %d is %d; not changing from %d to %d\n",
					jentry->disk_addr, dinode->sfi_linkcount, old, new);
			}
		}
		break;
		case META_UPDATE:
		{
			struct meta_update_args *jentry = (struct meta_update_args *)recptr;
			unsigned char *old;
			unsigned char *new;

			unsigned char* old_data = (unsigned char*)jentry + sizeof(*jentry);
			unsigned char* new_data = old_data + jentry->data_len;

			// Compute the necessary change
			if (redo) {
				old = old_data;
				new = new_data;
			} else {
				old = new_data;
				new = old_data;
			}

			(void) old;
			// If this is the same as the old data, replace it
			//if (memcmp(data, old, jentry->data_len) == 0) {
				// Overwrite a portion with new data
				memcpy(data + jentry->offset_addr, new, jentry->data_len);

				// Write the entire block to disk
				result = sfs_writeblock(&sfs->sfs_absfs, jentry->disk_addr, 
					NULL, data, SFS_BLOCKSIZE);
			//}
		}
		break;
		case RESIZE:
		{
			struct resize_args *jentry = (struct resize_args *)recptr;
			size_t old, new;

			// Get what the change should be
			if (redo) {
				old = jentry->old_size;
				new = jentry->new_size;
			} else {
				old = jentry->new_size;
				new = jentry->old_size;
			}

			// Do the change, if necessary
			if (dinode->sfi_size == old) {
				// Set the new size
				dinode->sfi_size = new;

				// Write the entire block to disk
				result = sfs_writeblock(&sfs->sfs_absfs, jentry->inode_addr, 
					NULL, data, SFS_BLOCKSIZE);

				rdebug("Setting size of %d from %d to %d\n",
					jentry->inode_addr, old, new);
			} else {
				rdebug("size of %d is %d; not changing from %d to %d\n",
					jentry->inode_addr, dinode->sfi_size, old, new);
			}
		}
		break;
		case BLOCK_WRITE:
		{
			struct block_write_args *jentry = (struct block_write_args *)recptr;
			unsigned block_checksum;

			kprintf("  Data looks like:");
			for (int b = 0; b < 8; b++)
				kprintf(" %02x", (unsigned char)((unsigned char*)data)[b]);
			kprintf("\n");

			// Compute checksum
			block_checksum = checksum(data);

			// If may be garbage, zero out
			bool new_alloc = jentry->new_alloc;
			if (block_checksum != jentry->new_checksum) {
				kprintf("  Checksum: got %d instead of %d\n", block_checksum, jentry->new_checksum);
				kprintf("  Failed write in block %d detected. Data may be corrupted\n",
					jentry->written_addr);
				if (new_alloc) {
					kprintf("  Block was newly allocated and may contain garbage data. Clearning\n");
					bzero(data, SFS_BLOCKSIZE);
					result = sfs_writeblock(&sfs->sfs_absfs, jentry->written_addr, NULL, data,
						SFS_BLOCKSIZE);
					if (result)
						panic("stay calm and debug");
				}
			}

		}
		break;
		case INODE_UPDATE_TYPE:
		{
			struct inode_update_type_args *jentry = (struct inode_update_type_args *)recptr;
			int old, new;

			// Get what the change should be
			if (redo) {
				old = jentry->old_type;
				new = jentry->new_type;
			} else {
				old = jentry->new_type;
				new = jentry->old_type;
			}

			// Do the change, if necessary
			if (dinode->sfi_type == old) {
				// Update the new type
				dinode->sfi_type = new;

				// Write the entire block to disk
				result = sfs_writeblock(&sfs->sfs_absfs, jentry->inode_addr, 
					NULL, data, SFS_BLOCKSIZE);

				rdebug("Setting type of %d from %d to %d\n",
					jentry->inode_addr, old, new);
			} else {
				rdebug("type of %d is %d; not changing from %d to %d\n",
					jentry->inode_addr, dinode->sfi_type, old, new);
			}
		}
		break;
		case TRANS_BEGIN:
			// BEGIN and COMMIT will still remain here, but 
			break;
		case TRANS_COMMIT:
			break;
		default:
		panic("Invalid record type (%d)", type);
	}

	return 0;
}

/*
 * Mount routine.
 *
 * The way mount works is that you call vfs_mount and pass it a
 * filesystem-specific mount routine. Said routine takes a device and
 * hands back a pointer to an abstract filesystem. You can also pass
 * a void pointer through.
 *
 * This organization makes cleanup on error easier. Hint: it may also
 * be easier to synchronize correctly; it is important not to get two
 * filesystems with the same name mounted at once, or two filesystems
 * mounted on the same device at once.
 */
static
int
sfs_domount(void *options, struct device *dev, struct fs **ret)
{
	int result;
	struct sfs_fs *sfs;

	/* We don't pass any options through mount */
	(void)options;

	/*
	 * We can't mount on devices with the wrong sector size.
	 *
	 * (Note: for all intents and purposes here, "sector" and
	 * "block" are interchangeable terms. Technically a filesystem
	 * block may be composed of several hardware sectors, but we
	 * don't do that in sfs.)
	 */
	if (dev->d_blocksize != SFS_BLOCKSIZE) {
		kprintf("sfs: Cannot mount on device with blocksize %zu\n",
			dev->d_blocksize);
		return ENXIO;
	}

	sfs = sfs_fs_create();
	if (sfs == NULL) {
		return ENOMEM;
	}

	/* Set the device so we can use sfs_readblock() */
	sfs->sfs_device = dev;

	/* Acquire the locks so various stuff works right */
	lock_acquire(sfs->sfs_vnlock);
	lock_acquire(sfs->sfs_freemaplock);

	/* Load superblock */
	result = sfs_readblock(&sfs->sfs_absfs, SFS_SUPER_BLOCK,
			       &sfs->sfs_sb, sizeof(sfs->sfs_sb));
	if (result) {
		lock_release(sfs->sfs_vnlock);
		lock_release(sfs->sfs_freemaplock);
		sfs->sfs_device = NULL;
		sfs_fs_destroy(sfs);
		return result;
	}

	/* Make some simple sanity checks */

	if (sfs->sfs_sb.sb_magic != SFS_MAGIC) {
		kprintf("sfs: Wrong magic number in superblock "
			"(0x%x, should be 0x%x)\n",
			sfs->sfs_sb.sb_magic,
			SFS_MAGIC);
		lock_release(sfs->sfs_vnlock);
		lock_release(sfs->sfs_freemaplock);
		sfs->sfs_device = NULL;
		sfs_fs_destroy(sfs);
		return EINVAL;
	}

	if (sfs->sfs_sb.sb_journalblocks >= sfs->sfs_sb.sb_nblocks) {
		kprintf("sfs: warning - journal takes up whole volume\n");
	}

	if (sfs->sfs_sb.sb_nblocks > dev->d_blocks) {
		kprintf("sfs: warning - fs has %u blocks, device has %u\n",
			sfs->sfs_sb.sb_nblocks, dev->d_blocks);
	}

	/* Ensure null termination of the volume name */
	sfs->sfs_sb.sb_volname[sizeof(sfs->sfs_sb.sb_volname)-1] = 0;

	/* Load free block bitmap */
	sfs->sfs_freemap = bitmap_create(SFS_FS_FREEMAPBITS(sfs));
	if (sfs->sfs_freemap == NULL) {
		lock_release(sfs->sfs_vnlock);
		lock_release(sfs->sfs_freemaplock);
		sfs->sfs_device = NULL;
		sfs_fs_destroy(sfs);
		return ENOMEM;
	}
	result = sfs_freemapio(sfs, UIO_READ);
	if (result) {
		lock_release(sfs->sfs_vnlock);
		lock_release(sfs->sfs_freemaplock);
		sfs->sfs_device = NULL;
		sfs_fs_destroy(sfs);
		return result;
	}

	lock_release(sfs->sfs_vnlock);
	lock_release(sfs->sfs_freemaplock);

	reserve_fsmanaged_buffers(2, SFS_BLOCKSIZE);

	/*
	 * Load up the journal container. (basically, recover it)
	 */

	SAY("*** Loading up the jphys container ***\n");
	result = sfs_jphys_loadup(sfs);
	if (result) {
		unreserve_fsmanaged_buffers(2, SFS_BLOCKSIZE);
		drop_fs_buffers(&sfs->sfs_absfs);
		sfs->sfs_device = NULL;
		sfs_fs_destroy(sfs);
		return result;
	}

	/*
	 * High-level recovery.
	 */

	/* Enable container-level scanning */
	sfs_jphys_startreading(sfs);

	/********************************/
	/* Call your recovery code here */
	/********************************/

	struct sfs_jiter *ji;
	unsigned type;
	void *recptr;
	size_t reclen;
	reserve_buffers(SFS_BLOCKSIZE);
	sfs_lsn_t curlsn;
	unsigned num, i;

	// Iterate backwards through the journal once to
	// 1) Figure out the last WRITE to touch each block
	//    mark that write operation accordingly
	struct bitmap *written_later = bitmap_create(SFS_FS_NBLOCKS(sfs));
	result = sfs_jiter_revcreate(sfs, &ji);
	if (result)
		panic("Oh no. Not again.");
	while (!sfs_jiter_done(ji)) {
		type = sfs_jiter_type(ji);
		recptr = sfs_jiter_rec(ji, &reclen);

		// Ok, we got a write entry
		if (type == BLOCK_WRITE) {
			struct block_write_args *jentry = (struct block_write_args*)recptr;
			daddr_t block = jentry->written_addr;

			// If the bitmap is not set, then no write to `block` happens after
			//  this write. Mark that this block is written to later, and mark
			//  the journal entry as well.
			if (!bitmap_isset(written_later, block)) {
				bitmap_mark(written_later, block);
				jentry->last_write = true;
			} else {
				jentry->last_write = false;
			}
		}
		result = sfs_jiter_next(sfs, ji);
		if (result)
			panic("Ha. And you thought you'd be able to sleep tonight");
	}
	// Free up our iterator
	sfs_jiter_destroy(ji);

	// Iterate forwards through the journal once to
	// 1) Build transaction objects
	// 2) Find untouchable user data
	// 3) Find alloc-write pairs (potential garbage)
	struct bitmap *userdata = bitmap_create(SFS_FS_NBLOCKS(sfs));
	struct bitmap *garbage  = bitmap_create(SFS_FS_NBLOCKS(sfs));

	// All newly seen transactions will go into the `transaction_active` list
	//  first. When we see a `TRANS_COMMIT`, we move it into the
	//  `transaction_list`. If we see a `TRANS_BEGIN` with the same id as an
	//  active transaction, we treat the active transaction as aborted, and we
	//  move the aborted transaction into `transaction_list` with `committed`
	//  set to `false`

	struct array *abort_list = array_create();
	struct array *transaction_active = array_create();
	array_init(abort_list);
	// Iterate
	result = sfs_jiter_fwdcreate(sfs, &ji);
	if (result)
		panic("Fuck everything is broken");
	while (!sfs_jiter_done(ji)) {
		type = sfs_jiter_type(ji);
		curlsn = sfs_jiter_lsn(ji);
		recptr = sfs_jiter_rec(ji, &reclen);
		kprintf("We have a record of type %u\n", type);
		switch (type) {
			case TRANS_BEGIN:
			{
				// Convenience
				// struct recovery_transaction *old_trans;
				struct trans_begin_args *new_trans = (struct trans_begin_args *)recptr;

				// Create the transaction record
				struct recovery_transaction rec_trans;
				rec_trans.operations = array_create();
				rec_trans.committed = false;
				rec_trans.id = new_trans->id;
				array_init(rec_trans.operations);

				// Add the newly created transaction to the list of active transactions
				result = array_add(transaction_active, &rec_trans, NULL);
			}
			break;
			case TRANS_COMMIT:
			{
				// Convenience
				struct recovery_transaction *active_trans;
				struct trans_commit_args *commit_record = (struct trans_commit_args *)recptr;

				// Iterate through the active transactions and find the one that was just committed
				num = array_num(transaction_active);
				int num_found = 0;
				for (i = 0; i < num; i++) {
					active_trans = array_get(transaction_active, i);
					// Add the committed one to the list of completed transactions
					if (active_trans->id == commit_record->id) {
						num_found++;
						array_remove(transaction_active, i);
						active_trans->committed = true;
					}
				}

				// There should not be more than one active transaction with the same id
				// There should be at least one active transaction, otherwise the commit should not have been logged
				//KASSERT(num_found == 1);
			}
			break;
			case BLOCK_ALLOC:
			{
				// Convenience
				struct block_alloc_args *jentry = (struct block_alloc_args *)recptr;

				// Mark as potential garbage
				if (!bitmap_isset(garbage, jentry->disk_addr))
					bitmap_mark(garbage, jentry->disk_addr);
			}
			break;
			case BLOCK_WRITE:
			{
				// Convenience
				struct block_write_args *jentry = (struct block_write_args *)recptr;

				// If this was a new alloc, record it as such in the block_write_args
				if (bitmap_isset(garbage, jentry->written_addr))
					jentry->new_alloc = true;

				// Mark as no longer garbage
				if (bitmap_isset(garbage, jentry->written_addr))
					bitmap_unmark(garbage, jentry->written_addr);

				// Mark as untouchable user data
				if (!bitmap_isset(userdata, jentry->written_addr))
					bitmap_mark(userdata, jentry->written_addr);
			}
			break;
			case BLOCK_DEALLOC:
			{
				// Convenience
				struct block_dealloc_args *jentry = (struct block_dealloc_args *)recptr;

				// Mark as no longer garbage
				if (bitmap_isset(garbage, jentry->disk_addr))
					bitmap_unmark(garbage, jentry->disk_addr);

				// Mark as no longer untouchable
				if (bitmap_isset(userdata, jentry->disk_addr))
					bitmap_unmark(userdata, jentry->disk_addr);
			}
		}

		// Add all log records to it transaction's list of operations
		if (type != TRANS_COMMIT && type != TRANS_BEGIN) {
			// All log records have their transaction id as the second member
			int id = ((int*)recptr)[1];

			KASSERT(type != TRANS_COMMIT);
			KASSERT(type != TRANS_BEGIN);

			// Convenience
			struct recovery_transaction *active_trans;

			// Iterate through the active transactions and find the one that this record belongs to
			num = array_num(transaction_active);
			int num_found = 0;
			for (i = 0; i < num; i++) {
				active_trans = array_get(transaction_active, i);
				// Add this record to the transaction's list of operations
				if (active_trans->id == id) {
					num_found++;
					kprintf("Adding operation type %d to transaction %d at index %d\n", type, id, i);
					result = array_add(active_trans->operations, &curlsn, NULL);
				}
			}

			//KASSERT(num_found == 1);
		}

		// Advance to the next journal entry
		result = sfs_jiter_next(sfs, ji);
		if (result)
			panic("Fuck everything is broken");
	}

	kprintf("Done with first read of journal\n");
	void *lsnptr;

	// Build the abosrt_list from active_transactions list
	num = array_num(transaction_active);
	struct recovery_transaction *rec_trans;
	for (i = 0; i < num; i++) {
		rec_trans = array_get(transaction_active, i);
		rec_trans->committed = false;
		unsigned num_ops = array_num(rec_trans->operations);
		unsigned j;
		for (j = 0; j < num_ops; j++) {
			lsnptr = array_get(rec_trans->operations, j);
			result = array_add(abort_list, lsnptr, NULL);
			if (result)
				panic("stay calm and debug");
		}
	}
	// Destroy the active transactions list
	while (array_num(transaction_active)) {
		array_remove(transaction_active, 0);
	}
	array_destroy(transaction_active);

	// Free up our iterator
	sfs_jiter_destroy(ji);
	unreserve_buffers(SFS_BLOCKSIZE);

	reserve_buffers(SFS_BLOCKSIZE);
	result = sfs_jiter_fwdcreate(sfs, &ji);

	if (result)
		panic("Fuck everything is broken");
	bool redo;
	// At this point all aborted operations are in `abort_list`
	while (!sfs_jiter_done(ji)) {
		type = sfs_jiter_type(ji);
		curlsn = sfs_jiter_lsn(ji);
		recptr = sfs_jiter_rec(ji, &reclen);
		redo = !operation_aborted(abort_list, curlsn);
		result = sfs_recover_operation(sfs, redo, recptr, userdata, garbage);
		if (result)
			panic("stay calm and debug");
		result = sfs_jiter_next(sfs, ji);
		if (result)
			panic("Fuck everything is broken");
	}

	sfs_jiter_destroy(ji);
	unreserve_buffers(SFS_BLOCKSIZE);

	/* Done with container-level scanning */
	sfs_jphys_stopreading(sfs);

	kprintf("Done recovering\n");

	/* Spin up the journal. */
	SAY("*** Starting up ***\n");
	result = sfs_jphys_startwriting(sfs);
	if (result) {
		unreserve_fsmanaged_buffers(2, SFS_BLOCKSIZE);
		drop_fs_buffers(&sfs->sfs_absfs);
		sfs->sfs_device = NULL;
		sfs_fs_destroy(sfs);
		return result;
	}

	// Trim after initial recovery
	sfs_jphys_trim(sfs, sfs_jphys_peeknextlsn(sfs));

	/**************************************/
	/* Maybe call more recovery code here */
	/**************************************/

	// TODO: recovery from graveyard here.

	struct sfs_direntry tsd;
	int nentries;
	// int i, result;
	struct sfs_vnode *grave_node;
	struct sfs_vnode *ptr;

	result = sfs_getgraveyard(&sfs->sfs_absfs, &grave_node);
	if (result) {
		panic("Graveyard is fucked up");
	}
	reserve_buffers(SFS_BLOCKSIZE);
	lock_acquire(grave_node->sv_lock);

	result = sfs_dir_nentries(grave_node, &nentries);
	if (result) {
		return result;
	}

	/* For each slot... */
	for (i=0; i<(unsigned)nentries; i++) {

		/* Read the entry from that slot */
		result = sfs_readdir(grave_node, i, &tsd);
		if (result) {
			panic("Error in Gravyard recovery");
		}
		sfs_loadvnode(sfs, tsd.sfd_ino, SFS_TYPE_INVAL, &ptr);
		lock_release(grave_node->sv_lock);
		sfs_reclaim(&ptr->sv_absvn);
		lock_acquire(grave_node->sv_lock);
	}
	unreserve_buffers(SFS_BLOCKSIZE);
	lock_release(grave_node->sv_lock);

	// Done!!! Yay!!! Nothing is broken!!! 
	sfs_jphys_trim(sfs, sfs_jphys_peeknextlsn(sfs));

	/* Hand back the abstract fs */
	*ret = &sfs->sfs_absfs;
	return 0;
}

/*
 * Actual function called from high-level code to mount an sfs.
 */
int
sfs_mount(const char *device)
{
	return vfs_mount(device, NULL, sfs_domount);
}
