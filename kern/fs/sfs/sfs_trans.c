#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <wchan.h>
#include <synch.h>
#include <proc.h>
#include <current.h>
#include <buf.h>
#include <sfs.h>
#include <array.h>
#include "sfsprivate.h"

int sfs_trans_begin(struct sfs_fs* sfs, int trans_type) {
	// create trans, add it to table
	sfs_jphys_write_wrapper(sfs, 
		(struct sfs_jphys_writecontext *)0xdeadbeef, jentry_trans_begin(trans_type));

	return 0;
}
void sfs_trans_callback(struct sfs_fs *sfs, sfs_lsn_t newlsn,
	struct sfs_jphys_writecontext *ctx) {
	(void) ctx;
	struct trans* new_trans = kmalloc(sizeof(struct trans));

	new_trans->id = curproc->pid;
	new_trans->first_lsn = newlsn;

	lock_acquire(sfs->trans_lock);
	array_add(sfs->sfs_transactions, new_trans, NULL);
	lock_release(sfs->trans_lock);
}

int sfs_trans_commit(struct sfs_fs* sfs, int trans_type) {
	// find trans, remove it from table, destroy it.
	(void) sfs;
	unsigned len, i;
	struct trans* trans_ptr;

	lock_acquire(sfs->trans_lock);
	len = array_num(sfs->sfs_transactions);
	for (i = 0; i < len; i++) {
		trans_ptr = array_get(sfs->sfs_transactions, i);
		if (trans_ptr->id == curproc->pid) {
			array_remove(sfs->sfs_transactions, i);
			break;
		}
	}
	lock_release(sfs->trans_lock);

	sfs_jphys_write_wrapper(sfs, NULL, jentry_trans_commit(trans_type));
	return 0;
}

int sfs_checkpoint(struct sfs_fs* sfs) {
	unsigned len, i;
	struct trans* trans_ptr;
	struct buf *buffer_ptr;
	unsigned oldest_lsn = 0xFFFFFFFF;
	struct lock *buffer_lock = buffer_get_lock();
	struct array *dirty_buffers = buffer_get_dirty_array();
	struct b_fsdata *data_ptr;

	// Find the earliest active_trans
	lock_acquire(sfs->trans_lock);
	len = array_num(sfs->sfs_transactions);
	for (i = 0; i < len; i++) {
		trans_ptr = array_get(sfs->sfs_transactions, i);
		if (oldest_lsn > trans_ptr->first_lsn)
			oldest_lsn = trans_ptr->first_lsn;
	}
	lock_release(sfs->trans_lock);

	// Find the earlist dirty buffer
	lock_acquire(buffer_lock);
	len = array_num(dirty_buffers);
	for (i = 0; i < len; i++) {
		buffer_ptr = array_get(dirty_buffers, i);
		if (buffer_ptr == NULL)
			continue;
		data_ptr = (struct b_fsdata *)buffer_get_fsdata(buffer_ptr);
		if (sfs_block_is_journal(sfs, data_ptr->diskblock)) {
			continue;
		}
		if (data_ptr->oldest_lsn < oldest_lsn)
			oldest_lsn = data_ptr->oldest_lsn;
	}
	lock_release(buffer_lock);

	// Trim!
	if (oldest_lsn == 0xFFFFFFFF) {
		oldest_lsn = sfs_jphys_peeknextlsn(sfs);
	}

	kprintf("Trim to %d\n", oldest_lsn);
	sfs_jphys_trim(sfs, oldest_lsn);

	// We sucessfully took a checkpoint! Clear the odometer.
	sfs_jphys_clearodometer(sfs->sfs_jphys);
	return 0;
}
