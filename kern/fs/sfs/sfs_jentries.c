#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <synch.h>
#include <buf.h>
#include <sfs.h>
#include "sfsprivate.h"

#define BLOCK_ALLOC	1
#define	INODE_LINK  2
#define META_UPDATE 3

struct block_alloc_args {
	unsigned code;
	daddr_t disk_addr;
	daddr_t ref_addr;
	size_t offset_addr;
};

struct inode_link_args {
	unsigned code;
	daddr_t disk_addr;
	size_t offset_addr;
	uint16_t old_linkcount;
	uint16_t new_linkcount;
};

struct meta_update_args {
	unsigned code;
	daddr_t disk_addr;
	size_t offset_addr;
	void *old_data;
	void *new_data;
};

sfs_lsn_t sfs_jphys_write_wrapper(struct sfs_fs *sfs,
		struct sfs_jphys_writecontext *ctx,
		void *rec) {

	unsigned code = *(int *)rec;
	size_t len;
	switch (code) {
		case BLOCK_ALLOC:
			len = sizeof(struct block_alloc_args);
			break;
		case INODE_LINK:
			len = sizeof(struct inode_link_args);
			break;
		case META_UPDATE:
			len = sizeof(struct meta_update_args);
			break;
	}
	sfs_lsn_t lsn = sfs_jphys_write(sfs, /*callback*/ NULL, ctx, code, rec, len);

	// TODO free the struct afterwards
	kfree(rec);

	return lsn;
}

void *jentry_block_alloc(daddr_t disk_addr, 
	daddr_t ref_addr, size_t offset_addr)
{
	struct block_alloc_args *record;

	record = kmalloc(sizeof(struct block_alloc_args));
	record->code = BLOCK_ALLOC;
	record->disk_addr = disk_addr;
	record->ref_addr = ref_addr;
	record->offset_addr = offset_addr;

	return (void *)record;
}

void *jentry_inode_link(daddr_t disk_addr, 
	uint16_t old_linkcount, uint16_t new_linkcount)
{
	struct inode_link_args *record;

	record = kmalloc(sizeof(struct inode_link_args));
	record->code = INODE_LINK;
	record->disk_addr = disk_addr;
	record->old_linkcount = old_linkcount;
	record->new_linkcount = new_linkcount;

	return (void *)record;
}

void *jentry_meta_update(daddr_t disk_addr, size_t offset_addr, 
	void *old_data, void *new_data) 
{
	struct meta_update_args *record;

	record = kmalloc(sizeof(struct meta_update_args));
	record->code = META_UPDATE;
	record->disk_addr = disk_addr;
	record->offset_addr = offset_addr;
	record->old_data = old_data;
	record->new_data = new_data;

	return (void *)record;
}