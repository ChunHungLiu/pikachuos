/* This is an autogenerated file. Modify sfs_jentries_template.c instead */

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
		case BLOCK_ALLOC:
			reclen = sizeof(struct block_alloc_args);
			kprintf("BLOCK_ALLOC(code=%d, disk_addr=%d, ref_addr=%d, offset_addr=%d)",
				((struct block_alloc_args*)recptr)->code,
				((struct block_alloc_args*)recptr)->disk_addr,
				((struct block_alloc_args*)recptr)->ref_addr,
				((struct block_alloc_args*)recptr)->offset_addr);
			break;
		case INODE_UPDATE_TYPE:
			reclen = sizeof(struct inode_update_type_args);
			kprintf("INODE_UPDATE_TYPE(code=%d, inode_addr=%d, old_type=%d, new_type=%d)",
				((struct inode_update_type_args*)recptr)->code,
				((struct inode_update_type_args*)recptr)->inode_addr,
				((struct inode_update_type_args*)recptr)->old_type,
				((struct inode_update_type_args*)recptr)->new_type);
			break;
		case TRUNCATE:
			reclen = sizeof(struct truncate_args);
			kprintf("TRUNCATE(code=%d, inode_addr=%d, start_block=%d, end_block=%d)",
				((struct truncate_args*)recptr)->code,
				((struct truncate_args*)recptr)->inode_addr,
				((struct truncate_args*)recptr)->start_block,
				((struct truncate_args*)recptr)->end_block);
			break;
		case INODE_LINK:
			reclen = sizeof(struct inode_link_args);
			kprintf("INODE_LINK(code=%d, disk_addr=%d, old_linkcount=%d, new_linkcount=%d)",
				((struct inode_link_args*)recptr)->code,
				((struct inode_link_args*)recptr)->disk_addr,
				((struct inode_link_args*)recptr)->old_linkcount,
				((struct inode_link_args*)recptr)->new_linkcount);
			break;
		case META_UPDATE:
			reclen = sizeof(struct meta_update_args);
			kprintf("META_UPDATE(code=%d, disk_addr=%d, offset_addr=%d, data_len=%d, old_data=%p, new_data=%p)",
				((struct meta_update_args*)recptr)->code,
				((struct meta_update_args*)recptr)->disk_addr,
				((struct meta_update_args*)recptr)->offset_addr,
				((struct meta_update_args*)recptr)->data_len,
				((struct meta_update_args*)recptr)->old_data,
				((struct meta_update_args*)recptr)->new_data);
			break;
		case BLOCK_DEALLOC:
			reclen = sizeof(struct block_dealloc_args);
			kprintf("BLOCK_DEALLOC(code=%d, disk_addr=%d)",
				((struct block_dealloc_args*)recptr)->code,
				((struct block_dealloc_args*)recptr)->disk_addr);
			break;
		case BLOCK_WRITE:
			reclen = sizeof(struct block_write_args);
			kprintf("BLOCK_WRITE(code=%d, written_addr=%d, new_checksum=%d)",
				((struct block_write_args*)recptr)->code,
				((struct block_write_args*)recptr)->written_addr,
				((struct block_write_args*)recptr)->new_checksum);
			break;
	}

	sfs_lsn_t lsn = sfs_jphys_write(sfs, /*callback*/ NULL, ctx, code, recptr, reclen);

	kfree(recptr);

	return lsn;
}

#define sfs_jphys_write_wrapper(args...) sfs_jphys_write_wrapper_debug(__FILE__, __LINE__, args)

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

void *jentry_inode_update_type(daddr_t inode_addr, int old_type, int new_type)
{
	struct inode_update_type_args *record;

	record = kmalloc(sizeof(struct inode_update_type_args));
	record->code = INODE_UPDATE_TYPE;
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
	record->disk_addr = disk_addr;
	record->old_linkcount = old_linkcount;
	record->new_linkcount = new_linkcount;

	return (void *)record;
}

void *jentry_meta_update(daddr_t disk_addr, size_t offset_addr, size_t data_len, void * old_data, void * new_data)
{
	struct meta_update_args *record;

	record = kmalloc(sizeof(struct meta_update_args));
	record->code = META_UPDATE;
	record->disk_addr = disk_addr;
	record->offset_addr = offset_addr;
	record->data_len = data_len;
	record->old_data = old_data;
	record->new_data = new_data;

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

void *jentry_block_write(daddr_t written_addr, uint32_t new_checksum)
{
	struct block_write_args *record;

	record = kmalloc(sizeof(struct block_write_args));
	record->code = BLOCK_WRITE;
	record->written_addr = written_addr;
	record->new_checksum = new_checksum;

	return (void *)record;
}

