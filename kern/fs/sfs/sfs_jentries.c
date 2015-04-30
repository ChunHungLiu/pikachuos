#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <synch.h>
#include <buf.h>
#include <sfs.h>
#include "sfsprivate.h"

/* RUN 'sfs_jentries.py' after modifying any of the structs below */


// struct block_alloc_args {
// 	unsigned code;
// 	daddr_t disk_addr;
// 	daddr_t ref_addr;
// 	size_t offset_addr;
// };

// struct inode_link_args {
// 	unsigned code;
// 	daddr_t disk_addr;
// 	uint16_t old_linkcount;
// 	uint16_t new_linkcount;
// };

// struct meta_update_args {
// 	unsigned code;
// 	daddr_t disk_addr;
// 	size_t offset_addr;
// 	void *old_data;
// 	void *new_data;
// };

// struct block_dealloc_args {
// 	unsigned code;
// 	daddr_t disk_addr;
// };

// struct truncate_args {
// 	unsigned code;
// 	daddr_t inode_addr;
// 	daddr_t start_block;
// 	daddr_t end_block;
// };

/* Generally won't need to modify anything below this */

#undef sfs_jphys_write_wrapper

sfs_lsn_t sfs_jphys_write_wrapper_debug(const char* file, int line,
		struct sfs_fs *sfs, struct sfs_jphys_writecontext *ctx, void *rec) {

	sfs_lsn_t ret = sfs_jphys_write_wrapper(sfs, ctx, rec);
	kprintf(" %s:%d\n", file, line);
	return ret;
}

sfs_lsn_t sfs_jphys_write_wrapper(struct sfs_fs *sfs,
		struct sfs_jphys_writecontext *ctx,	void *rec) {

	unsigned code = *(int *)rec;
	size_t len;

	// Debugging
	kprintf("jentry: ");

	// Tianyu, don't waste time writing debugger code. I have a system for generating this.

	switch (code) {
/* Autogenerate cases: sfs_jentries.py */
		case TRUNCATE:
			len = sizeof(struct truncate_args);
			kprintf("TRUNCATE(code=%d, inode_addr=%d, start_block=%d, end_block=%d)",
				((struct truncate_args*)rec)->code,
				((struct truncate_args*)rec)->inode_addr,
				((struct truncate_args*)rec)->start_block,
				((struct truncate_args*)rec)->end_block);
			break;
		case BLOCK_ALLOC:
			len = sizeof(struct block_alloc_args);
			kprintf("BLOCK_ALLOC(code=%d, disk_addr=%d, ref_addr=%d, offset_addr=%d)",
				((struct block_alloc_args*)rec)->code,
				((struct block_alloc_args*)rec)->disk_addr,
				((struct block_alloc_args*)rec)->ref_addr,
				((struct block_alloc_args*)rec)->offset_addr);
			break;
		case META_UPDATE:
			len = sizeof(struct meta_update_args);
			kprintf("META_UPDATE(code=%d, disk_addr=%d, offset_addr=%d, old_data=%p, new_data=%p)",
				((struct meta_update_args*)rec)->code,
				((struct meta_update_args*)rec)->disk_addr,
				((struct meta_update_args*)rec)->offset_addr,
				((struct meta_update_args*)rec)->old_data,
				((struct meta_update_args*)rec)->new_data);
			break;
		case INODE_LINK:
			len = sizeof(struct inode_link_args);
			kprintf("INODE_LINK(code=%d, disk_addr=%d, old_linkcount=%d, new_linkcount=%d)",
				((struct inode_link_args*)rec)->code,
				((struct inode_link_args*)rec)->disk_addr,
				((struct inode_link_args*)rec)->old_linkcount,
				((struct inode_link_args*)rec)->new_linkcount);
			break;
		case BLOCK_DEALLOC:
			len = sizeof(struct block_dealloc_args);
			kprintf("BLOCK_DEALLOC(code=%d, disk_addr=%d)",
				((struct block_dealloc_args*)rec)->code,
				((struct block_dealloc_args*)rec)->disk_addr);
			break;
/* End autogenerate */
	}

	sfs_lsn_t lsn = sfs_jphys_write(sfs, /*callback*/ NULL, ctx, code, rec, len);

	kfree(rec);

	return lsn;
}

#define sfs_jphys_write_wrapper(args...) sfs_jphys_write_wrapper_debug(__FILE__, __LINE__, args)

/* Autogenerate functions: sfs_jentries.py */
void *jentry_truncate(daddr_t inode_addr, daddr_t start_block, daddr_t end_block)
{
	struct truncate_args *record;

	record = kmalloc(sizeof(struct truncate_args));
	record->code = TRUNCATE;
	record->inode_addr = inode_addr;
	record->start_block = start_block;
	record->end_block = end_block;

	return (void *)record;
}

void *jentry_block_alloc(daddr_t disk_addr, daddr_t ref_addr, size_t offset_addr)
{
	struct block_alloc_args *record;

	record = kmalloc(sizeof(struct block_alloc_args));
	record->code = BLOCK_ALLOC;
	record->disk_addr = disk_addr;
	record->ref_addr = ref_addr;
	record->offset_addr = offset_addr;

	return (void *)record;
}

void *jentry_meta_update(daddr_t disk_addr, size_t offset_addr, void * old_data, void * new_data)
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

void *jentry_inode_link(daddr_t disk_addr, uint16_t old_linkcount, uint16_t new_linkcount)
{
	struct inode_link_args *record;

	record = kmalloc(sizeof(struct inode_link_args));
	record->code = INODE_LINK;
	record->disk_addr = disk_addr;
	record->old_linkcount = old_linkcount;
	record->new_linkcount = new_linkcount;

	return (void *)record;
}

void *jentry_block_dealloc(daddr_t disk_addr)
{
	struct block_dealloc_args *record;

	record = kmalloc(sizeof(struct block_dealloc_args));
	record->code = BLOCK_DEALLOC;
	record->disk_addr = disk_addr;

	return (void *)record;
}

/* End autogenerate */
