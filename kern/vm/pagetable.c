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

//#define DEBUG_PT

#ifdef DEBUG_PT
#define PT_DEBUG(message...) kprintf("pt: ");kprintf(message);
#define PT_DONE kprintf("done\n");
#else
#define PT_DEBUG(message...) ;
#define PT_DONE ;
#endif

/**
 * @brief Lock and return a pagetable entry
 * @details Locks a pagetable entry given the address space and virtual address
 *          Returns NULL if the second level pagetable is not allocated.
 *          Otherwise, returns the pagetable entry associated with the provided
 *          virtual address
 * 
 * @param addrspace Address space the virtual address resides in
 * @param vaddr Virtual address to look up
 * 
 * @return NULL if there is no second level page table. Otherwise, the pageable
 *         entry associated with the virtual address
 */
inline struct pt_entry* pte_lock(struct addrspace *as, vaddr_t vaddr) {
    int index_hi = vaddr >> 22;
    lock_acquire(as->pt_locks[index_hi]);
    return pt_get_entry(as, vaddr);
}

inline struct pt_entry* pte_unlock(struct addrspace *as, vaddr_t vaddr) {
    int index_hi = vaddr >> 22;
    struct pt_entry *pt_entry = pt_get_entry(as, vaddr);
    lock_release(as->pt_locks[index_hi]);
    return pt_entry;
}

inline bool pte_locked(struct addrspace *as, vaddr_t vaddr) {
    int index_hi = vaddr >> 22;
	return lock_do_i_hold(as->pt_locks[index_hi]);
}

/* Create a new page table. The new page table will be associated with an addrspace. */
struct pt_entry** pagetable_create() {
	int i;
	struct pt_entry** pt = kmalloc(PT_LEVEL_SIZE * sizeof(struct pt_entry*));
	for (i = 0; i < PT_LEVEL_SIZE; i++) {
		pt[i] = NULL;
	}
	return pt;
}

/**
 * @brief Creates a pagetable entry given a virtual address and address space.
 * @details Allocates a L2 pagetable if necessary, gets the pagetable entry,
 *          sets that pagetable entry to be in memory and allocated with a
 *          correct backing store index and a pointer to a valid physical page.
 *          The resulting pt_entry<->cm_entry pairing is guaranteed to be 
 *          consistent (map correctly to each other) and in memory.
 * 
 * @param addrspace Address space under which to create this
 * @param vaddr The virtual address that this page will have in the address space
 * 
 * @return The new pagetable entry that was just allocated
 */
struct pt_entry* pt_alloc_page(struct addrspace *as, vaddr_t vaddr) {
	PT_DEBUG("Allocating %x...", vaddr);
	uint32_t index_hi = vaddr >> 22;

	//pte_lock(as, vaddr);
	KASSERT(pte_locked(as, vaddr));

	// Check that the L2 pagetable exists, creating if it doesn't
	if (as->pagetable[index_hi] == NULL) {
		as->pagetable[index_hi] = kmalloc(PT_LEVEL_SIZE * sizeof(struct pt_entry));
		memset(as->pagetable[index_hi], 0, PT_LEVEL_SIZE * sizeof(struct pt_entry));
	}

	struct pt_entry *pt_entry = pt_get_entry(as, vaddr);

	pt_entry->store_index = bs_alloc_index();
	pt_entry->in_memory = true;
	pt_entry->allocated = true;
	pt_entry->p_addr = cm_alloc_page(as, vaddr);

	//pte_unlock(as, vaddr);

	PT_DONE;

	return pt_entry;
}

/**
 * @brief Removes a page from the pagetable
 * @details [long description]
 * 
 * @param addrspace [description]
 * @param vaddr [description]
 */
void pt_dealloc_page(struct addrspace *as, vaddr_t vaddr) {
	pte_lock(as, vaddr);

	struct pt_entry *pt_entry = pt_get_entry(as, vaddr);

	cm_dealloc_page(NULL, pt_entry->p_addr);

	pt_entry->p_addr = 0;
	pt_entry->store_index = 0;
	pt_entry->in_memory = 0;
	pt_entry->allocated = 0;

	pte_unlock(as, vaddr);
}

/**
 * @brief Returns the page table entry from the current process's page table with the
 *        specified virtual address
 * @details Not synchronized. Assumes that the entry is already locked
 * 
 * @param addrspace [description]
 * @param vaddr [description]
 * 
 * @return [description]
 */
struct pt_entry* pt_get_entry(struct addrspace *as, vaddr_t vaddr) {
	uint32_t index_hi = vaddr >> 22;
	uint32_t index_lo = vaddr >> 12 & 0x000003FF;

	// Check to make sure the second level pagetable exists
	KASSERT(pte_locked(as, vaddr));
	if (as->pagetable[index_hi] == NULL) {
		return NULL;
	}
	struct pt_entry *entry = &as->pagetable[index_hi][index_lo];

	return entry;
}

/* Destroy all entries in the page table. 
 * This will free coremap as well
 */
/**
 * @brief Destroy all entries in the given pagetable
 * @details Iterates through all extant L2 pagetables, iterates over all
 *          pagetable entries in there, and deallocates that page
 * 
 * @param addrspace Address space whose pagetable will be destroyed
 * @param pt_entry the pagetable to destroy
 */
void pt_destroy(struct addrspace *as, struct pt_entry** pagetable) {
	PT_DEBUG("destroy (addrspace) %p\n", as);
    int i, j;
    struct pt_entry *pt_entry;
    for (i = 0; i < PT_LEVEL_SIZE; i ++) {
        if (pagetable[i] != NULL) {
            for (j = 0; j < PT_LEVEL_SIZE; j++) {
                pt_entry = &pagetable[i][j];
                if (pt_entry->allocated) {
                	PT_DEBUG("dealloc {paddr: %x, store_index: %x, in_memory: %d, allocated: %d}\n",
                		pt_entry->p_addr, pt_entry->store_index, pt_entry->in_memory, pt_entry->allocated);
                    pt_dealloc_page(as, (i << 22) | (j << 12));
                }
            }
       		kfree(pagetable[i]);
        }
    }
    kfree(pagetable);
    PT_DONE;
}