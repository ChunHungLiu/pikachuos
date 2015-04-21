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
	int type;
	daddr_t disk_addr;
	daddr_t ref_addr;
	size_t offset_addr;
};

struct inode_link_args {
	int type;
	daddr_t disk_addr;
	size_t offset_addr;
	uint16_t old_linkcount;
	uint16_t new_linkcount;
};

struct meta_update_args {
	int type;
	daddr_t disk_addr;
	size_t offset_addr;
	void *old_data;
	void *new_data;
};

void *jentry_block_alloc(daddr_t disk_addr, 
	daddr_t ref_addr, size_t offset_addr)
{
	struct block_alloc_args *record;

	record = kmalloc(sizeof(struct block_alloc_args));
	record->type = BLOCK_ALLOC;
	record->disk_addr = disk_addr;
	record->ref_addr = ref_addr;
	record->offset_addr = offset_addr;

	return (void *)record;
}

void *jentry_inode_link(disk_addr, size_t offset_addr, 
	uint16_t old_linkcount, uint16_t new_linkcount)
{
	struct inode_link_args *record;

	record = kmalloc(sizeof(struct inode_link_args));
	record->type = INODE_LINK;
	record->disk_addr = disk_addr;
	record->offset_addr = offset_addr;
	record->old_linkcount = old_linkcount;
	record->new_linkcount = new_linkcount;

	return (void *)record;
}

void *jentry_meta_update(daddr_t disk_addr, size_t offset_addr, 
	void *old_data, void *new_data) 
{
	struct meta_update_args *record;

	record = kmalloc(sizeof(struct meta_update_args));
	record->type = META_UPDATE;
	record->disk_addr = disk_addr;
	record->offset_addr = offset_addr;
	record->old_data = old_data;
	record->new_data = new_data;

	return (void *)record;
}
