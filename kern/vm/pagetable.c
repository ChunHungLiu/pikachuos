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
	int i;
	struct pt_entry** pt = kmalloc(PT_LEVEL_SIZE * sizeof(struct pt_entry*));
	for (i = 0; i < PT_LEVEL_SIZE; i++) {
		pt[i] = NULL;
	}
	return pt;
}

/* 
 * Creates a pagetable entry given a virtual address. The resulting 
 * pt_entry<->cm_entry pairing is guaranteed to be consistent (map correctly
 * to each other) and in memory.
 */
struct pt_entry* pt_alloc_page(struct addrspace *as, vaddr_t v_addr) {
	kprintf("pt: Allocating %x...", v_addr);
	uint32_t index_hi = v_addr >> 22;

	// Check that the L2 pagetable exists, creating if it doesn't
	lock_acquire(as->pt_locks[index_hi]);
	if (as->pagetable[index_hi] == NULL) {
		as->pagetable[index_hi] = kmalloc(PT_LEVEL_SIZE * sizeof(struct pt_entry));
		memset(as->pagetable[index_hi], 0, PT_LEVEL_SIZE * sizeof(struct pt_entry));
	}

	struct pt_entry *entry = pt_get_entry(as, v_addr);

	entry->store_index = bs_alloc_index();
	entry->in_memory = true;
	entry->allocated = true;
	entry->lk = as->pt_locks[index_hi];
	entry->p_addr = cm_alloc_page(as, v_addr);
	lock_release(as->pt_locks[index_hi]);

	kprintf("done\n");

	return entry;
}

/* Removes a page from the pagetable */
void pt_dealloc_page(struct addrspace *as, vaddr_t v_addr) {
	struct pt_entry *entry = pt_get_entry(as, v_addr);

	lock_acquire(entry->lk);

	cm_dealloc_page(NULL, entry->p_addr);

	entry->p_addr = 0;
	entry->store_index = 0;
	entry->in_memory = 0;
	entry->allocated = 0;

	lock_release(entry->lk);
}

/*
 * Returns the page table entry from the current process's page table with the
 * specified virtual address
 */
struct pt_entry* pt_get_entry(struct addrspace *as, vaddr_t v_addr) {
	uint32_t index_hi = v_addr >> 22;
	uint32_t index_lo = v_addr >> 12 & 0x000003FF;
	int i_hold;

	// Check if we already hold the lock. In some cases, caller needs to have
	//  the lock. In other cases they shouldn't. We don't care here, we just
	//  need to make sure it's synchronized.
	i_hold = lock_do_i_hold(as->pt_locks[index_hi]);

	// Check to make sure the second level pagetable exists
	if (!i_hold)
		lock_acquire(as->pt_locks[index_hi]);
	if (as->pagetable[index_hi] == NULL) {
		lock_release(as->pt_locks[index_hi]);
		return NULL;
	}
	struct pt_entry *entry = &as->pagetable[index_hi][index_lo];
	if (!i_hold)
		lock_release(as->pt_locks[index_hi]);
	return entry;
}

/* Destroy all entries in the page table. 
 * This will free coremap as well
 */
void pt_destroy(struct addrspace *as, struct pt_entry** pagetable) {
	// kprintf("pt: pt_destroy (addrspace) %p\n", as);
    int i, j;
    struct pt_entry entry;
    for (i = 0; i < PT_LEVEL_SIZE; i ++) {
        if (pagetable[i] != NULL) {
            for (j = 0; j < PT_LEVEL_SIZE; j ++){
                entry = pagetable[i][j];
                if (entry.allocated) {
                	// kprintf("pt: dealloc {paddr: %x, store_index: %x, in_memory: %d, allocated: %d, lock: %p}\n",
                		// entry.p_addr, entry.store_index, entry.in_memory, entry.allocated, entry.lk);
                    pt_dealloc_page(as, (i << 22) | (j << 12));
                    bs_dealloc_index(entry.store_index);
                }
            }
        }
        kfree(pagetable[i]);
    }
    kfree(pagetable);
    // kprintf("pt: pt_destroy complete\n");
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