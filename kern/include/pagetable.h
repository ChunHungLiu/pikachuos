// TODO: pagetable interface goes here

#ifndef _PAGETABLE_H_
#define _PAGETABLE_H_

#include <vm.h>
#include <coremap.h>

#define PT_LEVEL_SIZE 1024

/* Macros for access to the pagetable entry */
#define PT_SET_DIRTY(PE) ((PE).p_addr |= DIRTY)
#define PT_SET_CLEAN(PE) ((PE).p_addr &= !DIRTY)

typedef struct {
    paddr_t p_addr;			// The corresponding translation of the physical address. It will include both the 20 bit physical address and 10 bits of TLB flags 
    short store_index;		// Where the entry is located in backing storage. Negative if not on disk.
    bool in_memory;			// true if the page is in memory
} pt_entry_t;

/* Create a new page table. The new page table will be associated with an addrspace. */
pt_entry_t*** pagetable_create();

/* Create a new page. This will only be called when in a page fault, the faulting address is in valid region but no page is allocated for it. Calls cm_allocuser()/cm_allockernel() to find an empty physical page, and put the new entry into the associated 2-level page table. */
pt_entry_t* page_create();

#endif