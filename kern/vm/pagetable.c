#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <setjmp.h>
#include <thread.h>
#include <current.h>
#include <vm.h>
#include <copyinout.h>
#include <pagetable.h>
#include <coremap.h>

/* Create a new page table. The new page table will be associated with an addrspace. */
struct pt_entry** pagetable_create() {
	// Code goes here
	return kmalloc(PT_LEVEL_SIZE * sizeof(struct pt_entry*));
}

/* 
 * Creates a pagetable entry given a virtual address. The resulting 
 * pt_entry<->cm_entry pairing is guaranteed to be consistent (map correctly
 * to each other) and in memory.
 */
struct pt_entry* pt_alloc_page(struct addrspace *as, vaddr_t v_addr) {
	uint32_t index_hi = v_addr >> 22;
	uint32_t index_lo = v_addr >> 12 & 0x000003FF;

	// Check that the L2 pagetable exists, creating if it doesn't
	if (as->pagetable[index_hi] == NULL) {
		as->pagetable[index_hi] = kmalloc(PT_LEVEL_SIZE * sizeof(struct pt_entry));
		memset(as->pagetable[index_hi], 0, PT_LEVEL_SIZE * sizeof(struct pt_entry));
	}

	struct pt_entry *entry = &as->pagetable[index_hi][index_lo];
	entry->p_addr = cm_alloc_page(as, v_addr);
	entry->store_index = 0;
	entry->in_memory = true;
	entry->allocated = true;
	entry->lk = lock_create("pt");

	return entry;
}

void pt_dealloc_page(struct addrspace *as, vaddr_t v_addr) {
	uint32_t index_hi = v_addr >> 22;
	uint32_t index_lo = v_addr >> 12 & 0x000003FF;

	// The L2 pagetable should exists
	KASSERT(as->pagetable[index_hi] != NULL);

	struct pt_entry *entry = &as->pagetable[index_hi][index_lo];

	lock_acquire(entry->lk);

	panic("WTF?! You haven't written this yet?! It's due in like n days!!!\n");

	lock_release(entry->lk);
}

/*
 * Returns the page table entry from the current process's page table with the
 * specified virtual address
 */
struct pt_entry* pt_get_entry(struct addrspace *as, vaddr_t v_addr) {
	uint32_t index_hi = v_addr >> 22;
	uint32_t index_lo = v_addr >> 12 & 0x000003FF;

	// Check to make sure the second level pagetable exists
	if (as->pagetable[index_hi] == NULL) {
		return NULL;
	}

	

	return &as->pagetable[index_hi][index_lo];
}

/* Destroy all entries in the page table. 
 * This will free coremap as well
 */
void pt_destroy(struct addrspace *as, struct pt_entry** pagetable) {
    int i, j;
    struct pt_entry entry;
    for (i = 0; i < PT_LEVEL_SIZE; i ++) {
        if (pagetable[i] != NULL) {
            for (j = 0; j < PT_LEVEL_SIZE; j ++){
                entry = pagetable[i][j];
                if (entry.allocated) {
                    if (entry.in_memory) {
                        cm_dealloc_page(as, entry.p_addr);
                    } 
                    else {
                        bs_dealloc_index(entry.store_index);
                    }
                    lock_destroy(entry.lk);
                }
            }
        }
        kfree(pagetable[i]);
    }
    kfree(pagetable);
}

/* 
 * Sets the page table entry for the page pointed to by 'vaddr' to the provided
 * page table entry
 */
// void pt_set_entry(vaddr_t vaddr, struct pt_entry* ptentry) {
// 	uint32_t index_hi = v_addr >> 22;
// 	uint32_t index_lo = v_addr >> 12 & 0x000003FF;

// 	if (curproc->p_addrspace->pagetable[index_hi] == NULL) {

// 	}
// }