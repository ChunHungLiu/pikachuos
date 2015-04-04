#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <coremap.h>
// TODO: coremap code goes here

#define CM_TO_PADDR(i) (paddr_t)PAGE_SIZE * (i + cm_base)
#define PADDR_TO_CM(paddr)  (paddr / PAGE_SIZE) - cm_base

static struct cm_entry *coremap;
static struct spinlock busy_lock = SPINLOCK_INITIALIZER;

static int cm_entries;
static int cm_base;

void cm_bootstrap(void) {
	// int i;
    paddr_t lo, hi;
    uint32_t npages, cm_size;

    // get size of memory
    ram_getsize(&lo, &hi);

    // assert alignment 
    KASSERT((lo & PAGE_FRAME) == lo);
    KASSERT((hi & PAGE_FRAME) == hi);

    // # of pages we need
    npages = (hi - lo) / PAGE_SIZE;

    // total size of cm
	cm_size = npages * sizeof(struct cm_entry);
	cm_size = ROUNDUP(cm_size, PAGE_SIZE);
	KASSERT((cm_size & PAGE_FRAME) == cm_size);
	cm_entries = 

	// this is kinda strange, we may end up having unused cormap space.
	ram_stealmem(cm_size / PAGE_SIZE);
	ram_getsize(&lo, &hi);

	cm_entries = (hi - lo) / PAGE_SIZE;
	cm_base = lo / PAGE_SIZE;

	for (i=0; i<(int)cm_entries; i++) {
        coremap[i].vaddr_base = 0;
        coremap[i].busy = 0;
        coremap[i].pid = -1;
        coremap[i].iskernel = 0;
        coremap[i].allocated = 0;
        coremap[i].has_next = 0;
    }
}