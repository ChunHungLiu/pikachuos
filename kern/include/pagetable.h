// TODO: pagetable interface goes here

#ifndef _PAGETABLE_H_
#define _PAGETABLE_H_

#include <vm.h>
#include <addrspace.h>
#include <synch.h>

#define PT_LEVEL_SIZE 1024

/* Macros for access to the pagetable entry */
#define PT_SET_DIRTY(PE) ((PE).p_addr |= DIRTY)
#define PT_SET_CLEAN(PE) ((PE).p_addr &= !DIRTY)

// Forward declare.
struct addrspace;

struct pt_entry {
    paddr_t p_addr;			// The corresponding translation of the physical address. It will include both the 20 bit physical address and 10 bits of TLB flags 
    short store_index;		// Where the entry is located in backing storage. Negative if not on disk.
    bool in_memory;			// true if the page is in memory
    bool allocated;			// true if the page has been allocated
    struct lock *lk;		// Get rid of this at some point
};

/* Create a new page table. The new page table will be associated with an addrspace. */
struct pt_entry** pagetable_create(void);

/* 
 * Creates a pagetable entry given a virtual address. The resulting 
 * pt_entry<->cm_entry pairing is guaranteed to be consistent (map correctly
 * to each other) and in memory.
 */
struct pt_entry* pt_alloc_page(struct addrspace *as, vaddr_t v_addr);

/*
 * Returns the page table entry from the current process's page table with the
 * specified virtual address
 */
struct pt_entry* pt_get_entry(struct addrspace *as, vaddr_t v_addr);

#endif