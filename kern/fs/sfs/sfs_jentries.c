#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <synch.h>
#include <buf.h>
#include <sfs.h>
#include "sfsprivate.h"

#define block_alloc	1
//#define

struct block_alloc_args {
	int type;	// 1
	daddr_t disk_addr;
	daddr_t ref_addr;
	size_t offset_addr;
};
