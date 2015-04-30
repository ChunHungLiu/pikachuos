#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <synch.h>
#include <buf.h>
#include <sfs.h>
#include "sfsprivate.h"

uint32_t checksum(struct buf *input) {
	(void)input;
	return 0;
}

/* Generally won't need to modify anything below this */

#undef sfs_jphys_write_wrapper

sfs_lsn_t sfs_jphys_write_wrapper_debug(const char* file, int line,
		struct sfs_fs *sfs, struct sfs_jphys_writecontext *ctx, void *rec) {

	sfs_lsn_t ret = sfs_jphys_write_wrapper(sfs, ctx, rec);
	kprintf(" %s:%d\n", file, line);
	return ret;
}

sfs_lsn_t sfs_jphys_write_wrapper(struct sfs_fs *sfs,
		struct sfs_jphys_writecontext *ctx,	void *recptr) {

	unsigned code = *(int *)recptr;
	size_t reclen;

	// Debugging
	kprintf("jentry: ");

	switch (code) {
/* Autogenerate: cases */
	}

	sfs_lsn_t lsn = sfs_jphys_write(sfs, /*callback*/ NULL, ctx, code, recptr, reclen);

	kfree(recptr);

	return lsn;
}

#define sfs_jphys_write_wrapper(args...) sfs_jphys_write_wrapper_debug(__FILE__, __LINE__, args)

/* Autogenerate: functions */