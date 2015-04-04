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

/* Wrapper to synchronize access to coremap entries. If it is currently busy, 
 * returns NULL. Should be matched with a CM_UNSET_BUSY once done. */
cm_entry_t* get_cm_entry(uint index) {
    // KASSERT(index <= coremap.size);
    spinlock_acquire(busy_lock);
    cm_entry_t cm_entry = coremap[index];
    if (CM_IS_BUSY(cm_entry)) {
        return NULL;
    }
    CM_SET_BUSY(cm_entry);
    return &cm_entry;
    spinlock_release(busy_lock);
}

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

// NOT COMPLETE
/* Evict the "next" page from memory. This will be dependent on the eviction policy that we choose (clock, random, etc.). This is where we will switch out different eviction policies */
// Consider returning the page we evicted
#ifdef PAGE_RANDOM
void page_evict_any() {
    // Code goes here
    uint index = random();

}
#elif PAGE_CLOCK
static uint evict_index = 0;
void page_evict_any() {
    //KASSERT(lock_do_i_hold(coremap_lock)); // Should not be true
    while (true) {
        cm_entry_t *cm_entry = get_cm_entry(evict_index);
        if (cm_entry.used_recently) {
            cm_entry.used_recently = false;
            CM_UNSET_BUSY(cm_entry);
            continue
        } else {
            pt_entry_t *pt_entry = get
            page_evict(pt_entry of cm_entry)
            return;// cm_entry;
        }
    }
}
#endif

/* Evict page from memory. This function will update coremap, write to backstore and update the backing_index entry; */
void page_evict(pt_entry_t* page) {
    // Code goes here
}

/* Load page from back store to memory. May call page_evict_any if there’s no more physical memory. See Paging for more details. */
void page_load(pt_entry_t* page) {
    // Code goes here
}

/* Load page from the backing store into a specific page of physical memory (used as a helper function for page_load) */
void page_load_into(pt_entry_t* page, cm_entry_t c_page) {
    // Code goes here
}
