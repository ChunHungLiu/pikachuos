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
	int i;
    paddr_t mem_start, mem_end;
    uint32_t npages, cm_size;
    (void) &busy_lock;

    // get size of memory
    mem_end = ram_getsize();
    mem_start = ram_getfirstfree();

    // assert alignment 
    KASSERT((mem_end & PAGE_FRAME) == mem_end);
    KASSERT((mem_start & PAGE_FRAME) == mem_start);

    // # of pages we need
    npages = (mem_end - mem_start) / PAGE_SIZE;

    // total size of cm
	cm_size = npages * sizeof(struct cm_entry);
	cm_size = ROUNDUP(cm_size, PAGE_SIZE);
	KASSERT((cm_size & PAGE_FRAME) == cm_size);

	// this is kinda strange, we may end up having unused cormap space.
	ram_stealmem(cm_size / PAGE_SIZE);
    mem_start += cm_size / PAGE_SIZE;

	cm_entries = (mem_end - mem_start) / PAGE_SIZE;
	cm_base = mem_start / PAGE_SIZE;

	for (i=0; i<(int)cm_entries; i++) {
        coremap[i].vm_addr = 0;
        coremap[i].busy = 0;
        coremap[i].pid = -1;
        coremap[i].is_kernel = 0;
        coremap[i].allocated = 0;
        coremap[i].has_next = 0;
        coremap[i].as = NULL;    
    }
}

/* Load page from back store to memory. May call page_evict_any if there’s no more physical memory. See Paging for more details. */
paddr_t cm_load_page(void) {
    // Code goes here
    // Basically do cm_alloc_page and then load
    return 0;
}

paddr_t cm_alloc_page(vaddr_t va) {
    int cm_index;
    // Try to find a free page. If we have one, it's easy. We probably
    // want to keep a global cm_free veriable to boost performance
    cm_index = cm_get_free_page();
    
    // We don't have any free page any more, needs to evict.
    if (cm_index < 0) {
        // Do page eviction
        cm_evict_page();
    }
    // cm_index should be a valid page index at this point
    KASSERT(coremap[cm_index].busy);
    mark_allocated(cm_index);

    // If not kernel, update as and vaddr_base
    // What do we do with kernel vs. user?
    KASSERT(va != 0);
    coremap[cm_index].as = curproc->p_addrspace;
    coremap[cm_index].vm_addr = va >> 12;
    return CM_TO_PADDR(cm_index);
}

// Returns a index where a page is free
int cm_get_free_page(void) {
    int i;
    // Do we want to use busy_lock here?
    for (i = 0; i < cm_entries; i++){
        spinlock_acquire(&busy_lock);
        if (!coremap[i].allocated) {
            spinlock_release(&busy_lock);
            return i;
        }
        spinlock_release(&busy_lock);
    }
    return -1;
}

/* Evict page from memory. This function will update coremap, write to backstore and update the backing_index entry; */
// Need to sync 2 addrspaces
// Simply update the pte related to paddr
void cm_evict_page(){
    // Use our eviction policy to choose a page to evict
    // coremap[cm_index] should be busy when this returns
    int cm_index;

    cm_index = cm_choose_evict_page();

    // Shoot down other CPUs
    vm_tlbshootdown_all();

    // Write to backing storage no matter what
    // swapout(COREMAP_TO_PADDR(cm_index));

    // Need to find the pt entry and mark it as not in memory anymore
    struct pt_entry *pte = get_pt_entry(coremap[cm_index].as,coremap[cm_index].vm_addr<<12);
    KASSERT(pte != NULL);

    pte->in_memory = 0;
    coremap[cm_index].allocated = 0;
}

// NOT COMPLETE
/* Evict the "next" page from memory. This will be dependent on the 
eviction policy that we choose (clock, random, etc.). This is 
where we will switch out different eviction policies */
// Consider returning the page we evicted
#ifdef PAGE_LINEAR
int cm_choose_evict_page() {
    int i = 0;
    struct cm_entry *cm_entry;
    while (true) {
        cm_entry = coremap[i];
        spinlock_acquire(&busy_lock);
        if (cm_entry.busy){
            spinlock_release(&busy_lock);
            i = (i + 1) % cm_entries;
            continue;
        } else {
            CM_SET_BUSY(cm_entry);
            spinlock_release(&busy_lock);
            return i;
        }
    }
}
#elif PAGE_CLOCK

static uint evict_index = 0;
int page_evict_any() {
    //KASSERT(lock_do_i_hold(coremap_lock)); // Should not be true
    while (true) {
        struct cm_entry *cm_entry = get_cm_entry(evict_index);
        if (cm_entry.used_recently) {
            cm_entry.used_recently = false;
            CM_UNSET_BUSY(cm_entry);
            continue
        } else {
            struct pt_entry *pt_entry = get
            page_evict(pt_entry of cm_entry)
            return;// cm_entry;
        }
    }
}
#endif
