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

#undef sfs_jphys_write_wrapper

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
/* Autogenerate: cases */
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

#define sfs_jphys_write_wrapper(args...) sfs_jphys_write_wrapper_debug(__FILE__, __LINE__, __FUNCTION__, args)

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
/* Autogenerate: print */
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

/* Autogenerate: functions */