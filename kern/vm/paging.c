#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <setjmp.h>
#include <thread.h>
#include <current.h>
#include <vm.h>
#include <copyinout.h>
#include <paging.h>

#define PAGE_RANDOM

/* Create a new page table. The new page table will be associated with an addrspace. */
pt_entry*** pagetable_create() {
	// Code goes here
	return kmalloc(PT_LEVEL_SIZE * sizeof(pt_entry_t*));
}

/* Create a new page. This will only be called when in a page fault, the faulting address is in valid region but no page is allocated for it. Calls cm_allocuser()/cm_allockernel() to find an empty physical page, and put the new entry into the associated 2-level page table. */
pt_entry* page_create() {
	// Code goes here
	return kmalloc(sizeof(pt_entry_t));
}

/* Evict the "next" page from memory. This will be dependent on the eviction policy that we choose (clock, random, etc.). This is where we will switch out different eviction policies */
// Consider returning the page we evicted
#ifdef PAGE_RANDOM
void page_evict_any() {
	// Code goes here
	spinlock_acquire(coremap_lock);
	
	spinlock_release(coremap_lock);
}
#elif PAGE_CLOCK
void page_evict_any() {

}
#endif

/* Evict page from memory. This function will update coremap, write to backstore and update the backing_index entry; */
void page_evict(pt_entry* page) {
	// Code goes here
}

/* Load page from back store to memory. May call page_evict_any if there’s no more physical memory. See Paging for more details. */
void page_load(pt_entry* page) {
	// Code goes here
}

/* Load page from the backing store into a specific page of physical memory (used as a helper function for page_load) */
void page_load_into(pt_entry* page, cm_entry c_page) {
	// Code goes here
}
