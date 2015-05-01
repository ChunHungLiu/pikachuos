#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <wchan.h>
#include <synch.h>
#include <proc.h>
#include <current.h>
#include <buf.h>
#include <sfs.h>
#include "sfsprivate.h"

int sfs_trans_begin(struct sfs_fs* sfs) {
	// create trans, add it to table
	sfs_lsn_t cur_lsn;
	// int err;

	struct trans* new_trans = kmalloc(sizeof(struct trans));

	cur_lsn = sfs_jphys_write_wrapper(sfs, NULL, jentry_trans_begin(1, curproc->pid));
	new_trans->id = curproc->pid;
	new_trans->first_lsn = cur_lsn;

	array_add(sfs->sfs_transactions, new_trans, NULL);

	// if (err)
	// 	return err;
	return new_trans->id;

}

int sfs_trans_commit(struct sfs_fs* sfs) {
	// find trans, remove it from table, destroy it.
	(void) sfs;
	unsigned len, i;
	struct trans* trans_ptr;

	len = array_num(sfs->sfs_transactions);
	for (i = 0; i < len; i++) {
		trans_ptr = array_get(sfs->sfs_transactions, i);
		if (trans_ptr->id == curproc->pid) {
			array_remove(sfs->sfs_transactions, i);
			break;
		}
	}

	sfs_jphys_write_wrapper(sfs, NULL, jentry_trans_commit(1, curproc->pid));
	return 0;
}
