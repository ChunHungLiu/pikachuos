#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <synch.h>
#include <buf.h>
#include <sfs.h>
#include "sfsprivate.h"

struct block_alloc_args {
	daddr_t disk_addr;
	daddr_t ref_addr;
	size_t offset_addr;
};
