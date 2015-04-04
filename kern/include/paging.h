// TODO: paging interface goes here

#ifndef _VM_H_
#define _VM_H_

#include <vm.h>

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
pt_entry*** pagetable_create();

/* Create a new page. This will only be called when in a page fault, the faulting address is in valid region but no page is allocated for it. Calls cm_allocuser()/cm_allockernel() to find an empty physical page, and put the new entry into the associated 2-level page table. */
pt_entry* page_create();

/* Evict the "next" page from memory. This will be dependent on the eviction policy that we choose (clock, random, etc.). This is where we will switch out different eviction policies */
// Consider returning the page we evicted
void page_evict_any();

/* Evict page from memory. This function will update coremap, write to backstore and update the backing_index entry; */
void page_evict(pt_entry* page);

/* Load page from back store to memory. May call page_evict_any if there’s no more physical memory. See Paging for more details. */
void page_load(pt_entry* page);

/* Load page from the backing store into a specific page of physical memory (used as a helper function for page_load) */
void page_load_into(pt_entry* page, cm_entry c_page);

#endif