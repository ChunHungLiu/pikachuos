#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <synch.h>
#include <buf.h>
#include <sfs.h>
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
		kfree(rec);
		return 0;
	}

	sfs_lsn_t ret = sfs_jphys_write_wrapper(sfs, ctx, rec);
	kprintf(" %s:%d:%s\n", file, line, func);
	return ret;
}

sfs_lsn_t sfs_jphys_write_wrapper(struct sfs_fs *sfs,
		struct sfs_jphys_writecontext *ctx,	void *recptr) {

	unsigned code = *(int *)recptr;
	size_t reclen;
	uint32_t odometer;

	// do checkpoint here.
	odometer = sfs_jphys_getodometer(sfs->sfs_jphys);
	if (odometer > 10) {
		sfs_checkpoint(sfs);
	}
	// Debugging
	kprintf("jentry: ");

	switch (code) {
/* Autogenerate: cases */
	}

	sfs_lsn_t lsn = sfs_jphys_write(sfs, /*callback*/ NULL, ctx, code, recptr, reclen);

	kfree(recptr);

	return lsn;
}

#define sfs_jphys_write_wrapper(args...) sfs_jphys_write_wrapper_debug(__FILE__, __LINE__, __FUNCTION__, args)

/* Autogenerate: functions */