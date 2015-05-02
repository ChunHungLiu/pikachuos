/* This is an autogenerated file. Modify sfs_jentries_template.c instead */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <synch.h>
#include <buf.h>
#include <sfs.h>
#include <current.h>
#include <proc.h>
#include "sfsprivate.h"

#define MOD_ADLER 65521

uint32_t checksum(unsigned char *data) {
    uint32_t a = 1, b = 0;
    size_t index;
 
    /* Process each byte of the data in order */
    for (index = 0; index < SFS_BLOCKSIZE; ++index) {
        a = (a + data[index]) % MOD_ADLER;
        b = (b + a) % MOD_ADLER;
    }
 
    return (b << 16) | a;
}

/* Generally won't need to modify anything below this */

// #undef sfs_jphys_write_wrapper

sfs_lsn_t sfs_jphys_write_wrapper_debug(const char* file, int line, const char* func,
		struct sfs_fs *sfs, struct sfs_jphys_writecontext *ctx, void *rec) {

	if (!sfs_jphys_iswriting(sfs)) {
		kprintf("Not writing\n");
		kfree(rec);
		return 0;
	}

	sfs_lsn_t ret = sfs_jphys_write_wrapper(sfs, ctx, rec);
	kprintf("at %s:%d:%s\n", file, line, func);
	return ret;
}

sfs_lsn_t sfs_jphys_write_wrapper(struct sfs_fs *sfs,
		struct sfs_jphys_writecontext *ctx,	void *recptr) {

	unsigned code = *(int *)recptr;
	size_t reclen;
	uint32_t odometer;
	sfs_lsn_t lsn;
	struct buf *recbuf;
	int block;

	if (!sfs_jphys_iswriting(sfs)) {
		kprintf("Not writing\n");
		kfree(recptr);
		return 0;
	}

	// Debugging
	kprintf("jentry: ");
	jentry_print(recptr);

	switch (code) {
		// Special case. There is extra data after the struct
		case META_UPDATE:
			reclen = sizeof(struct meta_update_args);
			reclen += 2 * ((struct meta_update_args *)recptr)->data_len;
			break;
		case BLOCK_ALLOC:
			reclen = sizeof(struct block_alloc_args);
			break;
		case INODE_UPDATE_TYPE:
			reclen = sizeof(struct inode_update_type_args);
			break;
		case TRUNCATE:
			reclen = sizeof(struct truncate_args);
			break;
		case INODE_LINK:
			reclen = sizeof(struct inode_link_args);
			break;
		case TRANS_COMMIT:
			reclen = sizeof(struct trans_commit_args);
			break;
		case BLOCK_DEALLOC:
			reclen = sizeof(struct block_dealloc_args);
			break;
		case TRANS_BEGIN:
			reclen = sizeof(struct trans_begin_args);
			break;
		case BLOCK_WRITE:
			reclen = sizeof(struct block_write_args);
			break;
		case RESIZE:
			reclen = sizeof(struct resize_args);
			break;
	}

	kprintf(" reclen=%d, ", reclen);
	if (ctx == NULL) {
		lsn = sfs_jphys_write(sfs, /*callback*/ NULL, ctx, code, recptr, reclen);
	} else {
		lsn = sfs_jphys_write(sfs, sfs_trans_callback, ctx, code, recptr, reclen);
	}
	kprintf("lsn=%lld, ", lsn);

	// If the journal entry is for something that modified a buffer, 
	//  update that buffer's metadata to refer to this journal entry
	if (code != BLOCK_DEALLOC && code != TRANS_BEGIN && code != TRANS_COMMIT) {
		block = ((int*)recptr)[2];
		recbuf = buffer_find(&sfs->sfs_absfs, (daddr_t)block);
		KASSERT(recbuf != NULL);
		kprintf("buffer=%p, ", recbuf);

		// get the old data, and update the oldest_lsn field only if it's the 
		// first operation that modifies it
		struct b_fsdata *buf_metadata;
		buf_metadata = (struct b_fsdata *)buffer_get_fsdata(recbuf);
		if (buf_metadata->oldest_lsn == 0) {
			buf_metadata->oldest_lsn = lsn;
		}
		if (buf_metadata->newest_lsn < lsn) {
			buf_metadata->newest_lsn = lsn;
		}
		buffer_set_fsdata(recbuf, (void*)buf_metadata);
	}

	// We need to track the lsn of the newest action to modify the freemap
	//  in order to enforce WAL. We need to track the lsn of the oldest action
	//  to modify the freemap so that we know where to trim to.
	if (code == BLOCK_ALLOC || code == BLOCK_DEALLOC) {
		if (sfs->oldest_freemap_lsn == 0) {
			sfs->oldest_freemap_lsn = lsn;
		}
		if (sfs->newest_freemap_lsn < lsn) {
			sfs->newest_freemap_lsn = lsn;
		}
	}

	// do checkpoint here.
	odometer = sfs_jphys_getodometer(sfs->sfs_jphys);
	if (odometer > 1) {
		sfs_checkpoint(sfs);
	}

	kfree(recptr);

	return lsn;
}

// #define sfs_jphys_write_wrapper(args...) sfs_jphys_write_wrapper_debug(__FILE__, __LINE__, __FUNCTION__, args)

void jentry_print(void* recptr) {
	int code = *((int*)recptr);
	unsigned i;

	switch (code) {
		case META_UPDATE:
		{
			struct meta_update_args* jentry = (struct meta_update_args*)recptr;
			kprintf("META_UPDATE(code=%d, id=%d, disk_addr=%d, offset_addr=%d, data_len=%d, old_data=\"",
				jentry->code,
				jentry->id,
				jentry->disk_addr,
				jentry->offset_addr,
				jentry->data_len);

			// The data for the entry starts at the end of the struct, so
			//  get the address of the struct and add the struct's size
			//  to get to the end
			unsigned char* old_data = (unsigned char*)jentry + sizeof(*jentry);
			unsigned char* new_data = old_data + jentry->data_len;

			// print out the old data as hex
			for (i = 0; i < jentry->data_len; i++) {
				if (i)
					kprintf(" ");
				kprintf("%02x", old_data[i]);
			}

			kprintf("\", new_data=\"");

			// print out the new data as hex
			for (i = 0; i < jentry->data_len; i++) {
				if (i)
					kprintf(" ");
				kprintf("%02x", new_data[i]);
			}

			kprintf("\")"); 
		}
		break;
		case BLOCK_ALLOC:
			kprintf("BLOCK_ALLOC(code=%d, id=%d, disk_addr=%d, ref_addr=%d, offset_addr=%d)",
				((struct block_alloc_args*)recptr)->code,
				((struct block_alloc_args*)recptr)->id,
				((struct block_alloc_args*)recptr)->disk_addr,
				((struct block_alloc_args*)recptr)->ref_addr,
				((struct block_alloc_args*)recptr)->offset_addr);
			break;
		case INODE_UPDATE_TYPE:
			kprintf("INODE_UPDATE_TYPE(code=%d, id=%d, inode_addr=%d, old_type=%d, new_type=%d)",
				((struct inode_update_type_args*)recptr)->code,
				((struct inode_update_type_args*)recptr)->id,
				((struct inode_update_type_args*)recptr)->inode_addr,
				((struct inode_update_type_args*)recptr)->old_type,
				((struct inode_update_type_args*)recptr)->new_type);
			break;
		case TRUNCATE:
			kprintf("TRUNCATE(code=%d, id=%d, inode_addr=%d, start_block=%d, end_block=%d)",
				((struct truncate_args*)recptr)->code,
				((struct truncate_args*)recptr)->id,
				((struct truncate_args*)recptr)->inode_addr,
				((struct truncate_args*)recptr)->start_block,
				((struct truncate_args*)recptr)->end_block);
			break;
		case INODE_LINK:
			kprintf("INODE_LINK(code=%d, id=%d, disk_addr=%d, old_linkcount=%d, new_linkcount=%d)",
				((struct inode_link_args*)recptr)->code,
				((struct inode_link_args*)recptr)->id,
				((struct inode_link_args*)recptr)->disk_addr,
				((struct inode_link_args*)recptr)->old_linkcount,
				((struct inode_link_args*)recptr)->new_linkcount);
			break;
		case TRANS_COMMIT:
			kprintf("TRANS_COMMIT(code=%d, id=%d, trans_type=%d)",
				((struct trans_commit_args*)recptr)->code,
				((struct trans_commit_args*)recptr)->id,
				((struct trans_commit_args*)recptr)->trans_type);
			break;
		case BLOCK_DEALLOC:
			kprintf("BLOCK_DEALLOC(code=%d, id=%d, disk_addr=%d)",
				((struct block_dealloc_args*)recptr)->code,
				((struct block_dealloc_args*)recptr)->id,
				((struct block_dealloc_args*)recptr)->disk_addr);
			break;
		case TRANS_BEGIN:
			kprintf("TRANS_BEGIN(code=%d, id=%d, trans_type=%d)",
				((struct trans_begin_args*)recptr)->code,
				((struct trans_begin_args*)recptr)->id,
				((struct trans_begin_args*)recptr)->trans_type);
			break;
		case BLOCK_WRITE:
			kprintf("BLOCK_WRITE(code=%d, id=%d, written_addr=%d, new_checksum=%d, new_alloc=%d)",
				((struct block_write_args*)recptr)->code,
				((struct block_write_args*)recptr)->id,
				((struct block_write_args*)recptr)->written_addr,
				((struct block_write_args*)recptr)->new_checksum,
				((struct block_write_args*)recptr)->new_alloc);
			break;
		case RESIZE:
			kprintf("RESIZE(code=%d, id=%d, inode_addr=%d, old_size=%d, new_size=%d)",
				((struct resize_args*)recptr)->code,
				((struct resize_args*)recptr)->id,
				((struct resize_args*)recptr)->inode_addr,
				((struct resize_args*)recptr)->old_size,
				((struct resize_args*)recptr)->new_size);
			break;
	}
}

// Special case, since we need to memcpy old and new data
void *jentry_meta_update(daddr_t disk_addr, size_t offset_addr, size_t data_len, void * old_data, void * new_data)
{
	struct meta_update_args *record;

	record = kmalloc(sizeof(struct meta_update_args) + 2 * data_len);

	// The data for the entry starts at the end of the struct, so
	//  get the address of the struct and add the struct's size
	//  to get to the end
	unsigned char* record_old_data = (unsigned char*)record + sizeof(*record);
	unsigned char* record_new_data = record_old_data + data_len;

	record->code = META_UPDATE;
	record->id = curproc->pid;
	record->disk_addr = disk_addr;
	record->offset_addr = offset_addr;
	record->data_len = data_len;

	// Either zero or copy in old data
	if (old_data == NULL) {
		bzero(record_old_data, data_len);
	} else {
		memcpy(record_old_data, old_data, data_len);
	}

	// Either zero or copy in new data
	if (new_data == NULL) {
		bzero(record_new_data, data_len);
	} else {
		memcpy(record_new_data, new_data, data_len);
	}

	return (void *)record;
}

void *jentry_block_alloc(daddr_t disk_addr, daddr_t ref_addr, size_t offset_addr)
{
	struct block_alloc_args *record;

	record = kmalloc(sizeof(struct block_alloc_args));
	record->code = BLOCK_ALLOC;
	record->id = curproc->pid;
	record->disk_addr = disk_addr;
	record->ref_addr = ref_addr;
	record->offset_addr = offset_addr;

	return (void *)record;
}

void *jentry_inode_update_type(daddr_t inode_addr, int old_type, int new_type)
{
	struct inode_update_type_args *record;

	record = kmalloc(sizeof(struct inode_update_type_args));
	record->code = INODE_UPDATE_TYPE;
	record->id = curproc->pid;
	record->inode_addr = inode_addr;
	record->old_type = old_type;
	record->new_type = new_type;

	return (void *)record;
}

void *jentry_truncate(daddr_t inode_addr, daddr_t start_block, daddr_t end_block)
{
	struct truncate_args *record;

	record = kmalloc(sizeof(struct truncate_args));
	record->code = TRUNCATE;
	record->id = curproc->pid;
	record->inode_addr = inode_addr;
	record->start_block = start_block;
	record->end_block = end_block;

	return (void *)record;
}

void *jentry_inode_link(daddr_t disk_addr, uint16_t old_linkcount, uint16_t new_linkcount)
{
	struct inode_link_args *record;

	record = kmalloc(sizeof(struct inode_link_args));
	record->code = INODE_LINK;
	record->id = curproc->pid;
	record->disk_addr = disk_addr;
	record->old_linkcount = old_linkcount;
	record->new_linkcount = new_linkcount;

	return (void *)record;
}

void *jentry_trans_commit(int trans_type)
{
	struct trans_commit_args *record;

	record = kmalloc(sizeof(struct trans_commit_args));
	record->code = TRANS_COMMIT;
	record->id = curproc->pid;
	record->trans_type = trans_type;

	return (void *)record;
}

void *jentry_block_dealloc(daddr_t disk_addr)
{
	struct block_dealloc_args *record;

	record = kmalloc(sizeof(struct block_dealloc_args));
	record->code = BLOCK_DEALLOC;
	record->id = curproc->pid;
	record->disk_addr = disk_addr;

	return (void *)record;
}

void *jentry_trans_begin(int trans_type)
{
	struct trans_begin_args *record;

	record = kmalloc(sizeof(struct trans_begin_args));
	record->code = TRANS_BEGIN;
	record->id = curproc->pid;
	record->trans_type = trans_type;

	return (void *)record;
}

void *jentry_block_write(daddr_t written_addr, uint32_t new_checksum, bool new_alloc)
{
	struct block_write_args *record;

	record = kmalloc(sizeof(struct block_write_args));
	record->code = BLOCK_WRITE;
	record->id = curproc->pid;
	record->written_addr = written_addr;
	record->new_checksum = new_checksum;
	record->new_alloc = new_alloc;

	return (void *)record;
}

void *jentry_resize(daddr_t inode_addr, size_t old_size, size_t new_size)
{
	struct resize_args *record;

	record = kmalloc(sizeof(struct resize_args));
	record->code = RESIZE;
	record->id = curproc->pid;
	record->inode_addr = inode_addr;
	record->old_size = old_size;
	record->new_size = new_size;

	return (void *)record;
}

