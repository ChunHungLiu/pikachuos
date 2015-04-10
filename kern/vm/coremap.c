#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <vm.h>
#include <coremap.h>
#include <bitmap.h>
#include <uio.h>
#include <vnode.h>
#include <vfs.h>
#include <kern/fcntl.h>
#include <stat.h>

#include <cpu.h>

//#define DEBUG_CM
//#define DEBUG_BS

#ifdef DEBUG_CM
#define CM_DEBUG(message...) kprintf("cm: ");kprintf(message);
#define CM_DONE kprintf("done\n");
#else
#define CM_DEBUG(message...) ;
#define CM_DONE ;
#endif

#ifdef DEBUG_BS
#define BS_DEBUG(message...) kprintf("bs: ");kprintf(message);
#define BS_DONE kprintf("done\n");
#else
#define BS_DEBUG(message...) ;
#define BS_DONE (void)0;
#endif

#define PAGE_RANDOM

#define CM_TO_PADDR(i) ((paddr_t)PAGE_SIZE * (i + cm_base))
#define PADDR_TO_CM(paddr)  ((paddr / PAGE_SIZE) - cm_base)

static struct cm_entry *coremap;
static struct spinlock busy_lock = SPINLOCK_INITIALIZER;

static unsigned cm_entries;
static unsigned cm_base;

static unsigned cm_used;
static struct spinlock cm_used_lock = SPINLOCK_INITIALIZER;

static unsigned evict_hand = 0;

static unsigned mem_free;
static struct lock *mem_free_lock;

static int cm_do_evict(int cm_index, struct addrspace* as, vaddr_t va);

struct vnode *bs_file;
struct bitmap *bs_map;
struct lock *bs_map_lock;
struct semaphore *tlb_sem;

void cm_used_change(int amount) {
    spinlock_acquire(&cm_used_lock);
    cm_used += amount;
    spinlock_release(&cm_used_lock);
}

void cm_mem_change(int amount) {
    lock_acquire(mem_free_lock);
    mem_free += amount;
    lock_release(mem_free_lock);
}

unsigned cm_mem_free() {
    lock_acquire(mem_free_lock);
    int ret = mem_free;
    lock_release(mem_free_lock);
    return ret;
}

int cm_alloc_entry(struct addrspace *as, vaddr_t vaddr, bool busy);

void cm_bootstrap(void) {
	int i;
    paddr_t mem_start, mem_end;
    uint32_t npages, cm_size;
    (void) evict_hand;

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

    coremap = (struct cm_entry *) PADDR_TO_KVADDR(mem_start);

	// We may end up having unused cormap space.
	ram_stealmem(cm_size / PAGE_SIZE);
    mem_start += cm_size;

	cm_entries = (mem_end - mem_start) / PAGE_SIZE;
	cm_base = mem_start / PAGE_SIZE;
    cm_used = 0;

    // TODO: Can be replaced with memset
	for (i = 0; i < (int)cm_entries; i++) {
        coremap[i].vm_addr = 0;
        coremap[i].busy = 0;
        coremap[i].is_kernel = 0;
        coremap[i].allocated = 0;
        coremap[i].has_next = 0;
        coremap[i].dirty = 0;
        coremap[i].used_recently = 0;
        coremap[i].as = NULL;    
    }

    tlb_sem = sem_create("Shootdown", 0);
}

/**
 * @brief Allocates a coremap entry to the given address space with the provided
 *        virtual address
 * @details This will get a free coremap entry (evicting if necessary) and mark 
 *          that entry as used
 * 
 * @param addrspace Address space to assign this entry to
 * @param vaddr Reverse mapping back to the page table entry refrencing this
 * @param busy Final busy state of the allocated page
 * 
 * @return Returns the index of the entry we just allocated
 */
int cm_alloc_entry(struct addrspace *as, vaddr_t vaddr, bool busy) {
    KASSERT(vaddr != 0);

    int cm_index;
    paddr_t pa;

    // Get the index of a free page, or -1 if none are free
    cm_index = cm_get_free_page();
    
    // We don't have any free page any more, needs to evict.
    if (cm_index < 0) {
        // Do page eviction
        cm_index = cm_evict_page(as, vaddr);
    }

    // cm_index should be a valid page index at this point

    // Either cm_get_free_page or cm_evict_page should have set this entry to busy
    KASSERT(coremap[cm_index].busy == true);

    // If alread free, this should not be allocated. If evicted, should not be either
    KASSERT(coremap[cm_index].allocated == 0);

    // Mark the entry as allocated
    CM_DEBUG("allocating (cm_entry) %d to (addrspace) %p...", cm_index, as);
    coremap[cm_index].allocated = 1;

    // Assignment
    coremap[cm_index].vm_addr = vaddr;
    coremap[cm_index].as = as;
    coremap[cm_index].is_kernel = (as == NULL);
    coremap[cm_index].busy = busy;
    pa = CM_TO_PADDR(cm_index);

    bzero((void *)PADDR_TO_KVADDR(pa), PAGE_SIZE);

    // Track the number of used coremap entries
    cm_used_change(1);

    CM_DONE;

    return cm_index;
}

paddr_t cm_alloc_page(struct addrspace *as, vaddr_t vaddr) {
    int cm_index = cm_alloc_entry(as, vaddr, false);
    return CM_TO_PADDR(cm_index);
}

paddr_t cm_load_page(struct addrspace *as, vaddr_t vaddr) {
    int cm_index = cm_alloc_entry(as, vaddr, true);

    KASSERT(coremap[cm_index].busy);
    KASSERT(coremap[cm_index].allocated);

    // Should be locked. Get the pagetable entry and read in
    KASSERT(pte_locked(as, vaddr));
    struct pt_entry *pt_entry = pt_get_entry(as, vaddr);
    if (!pt_entry->in_memory && pt_entry->allocated) {
        bs_read_in(as, vaddr, cm_index);
    }

    coremap[cm_index].busy = false;

    return CM_TO_PADDR(cm_index);
}

/**
 * @brief Linear probing to find free contiguous pages. NOTE: Only used by kernel
 * @details We reserve memory and try to grow it. If we encounter a kernel page
 *          (can't be moved), then we unreserve the previous pages we had and 
 *          start over after it.
 * 
 * @param npages Numer of pages to allocate
 * @return A physical address to the start of the first page, or 0 if there is
 *         no contiguous region of the requested size
 */
paddr_t cm_alloc_npages(unsigned npages) {
    // This should be the kernel calling this. Can we check this
    unsigned start_index = 0, end_index = 0;
    //spinlock_acquire(&busy_lock);
    for (end_index = 0; end_index < cm_entries; end_index++) {
        spinlock_acquire(&busy_lock);
        if (coremap[end_index].busy || coremap[end_index].is_kernel) {
            // Entry busy or can't be moved. Give up and restart the contiguous region
            // Set the pages we had reserved to not busy
            for (; start_index < end_index; start_index++) {
                KASSERT(coremap[start_index].busy);
                coremap[start_index].busy = false;
            }
            spinlock_release(&busy_lock);
            KASSERT(start_index == end_index);
            // start_index should point to the start of a potentially free region
            start_index++;
            continue;
        } else {
            // This page is free!!! Reserve it
            KASSERT(!coremap[end_index].busy);
            coremap[end_index].busy = true;
            spinlock_release(&busy_lock);
            // Check if we are done
            KASSERT(end_index - start_index < npages);
            if (end_index - start_index == npages - 1) {
                // Take ownership of all the reserved ones
                for (unsigned i = start_index; i <= end_index; i++) {
                    // We may need to page out user pages to make space for our kernel
                    if (coremap[i].allocated) {
                        KASSERT(coremap[i].busy);
                        cm_do_evict(i, NULL, coremap[i].vm_addr);
                        KASSERT(!coremap[i].allocated);
                    }
                    spinlock_acquire(&busy_lock);
                    CM_DEBUG("allocating (cm_entry) %d to kernel...", i);
                    coremap[i].vm_addr = CM_TO_PADDR(i);  // TODO TEMP: for debugging. Should get overridden anyway
                    coremap[i].is_kernel = true;
                    coremap[i].allocated = true;
                    // Can't be set after we set busy to false, so we need some dirty logic here
                    if (i < end_index)
                        coremap[i].has_next = true;
                    KASSERT(coremap[i].busy);
                    coremap[i].busy = false;
                    spinlock_release(&busy_lock);
                    CM_DONE;
                }

                cm_used_change(npages);

                //spinlock_release(&busy_lock);
                return CM_TO_PADDR(start_index);
            }
        }
        // TEST
        //KASSERT(!spinlock_do_i_hold(&busy_lock)); // Seems to be hanging on this
    }
    //spinlock_release(&busy_lock);
    return 0;
}

/*static void cm_wait_for(int cm_index) {
    for (unsigned i = 0; i < 1000; i++) {
        spinlock_acquire(&busy_lock);
        if (!coremap[cm_index].busy) {
            coremap[cm_index].busy = true;
            spinlock_release(&busy_lock);
            return;
        } else {
            spinlock_release(&busy_lock);
            CM_DEBUG("waiting on (cm_entry) %d\n", cm_index);
            thread_yield();
        }
    }
    coremap[cm_index].busy = true;
    CM_DEBUG("!!!!!!!!!! Fuck it. Page (cm_entry) %d is still busy. I give up !!!!!!!!!!\n", cm_index);
}*/

/**
 * @brief Dellocates a page with a given physical address from the address space
 * @details Free any bitmap indices if the given page is in the backing store,
 *          update the pagetable entry, zero out the coremap entry, repeat on
 *          the next coremap entry if this was part of a multi page chain
 * 
 * @param addrspace Target address space
 * @param paddr The physical address to deallocate
 */
bool cm_dealloc_page(struct addrspace *as, paddr_t paddr) {
    int cm_index;
    int bs_index;
    bool has_next = true;

    cm_index = PADDR_TO_CM(paddr);

    // Loop until all pages in a multipage chain are deallocated
    while (has_next) {

        // Lock the coremap entry, or give up if we can't
        spinlock_acquire(&busy_lock);
        if (coremap[cm_index].busy) {
            spinlock_release(&busy_lock);
            return false;
        }
        coremap[cm_index].busy = true;
        spinlock_release(&busy_lock);

        //
        KASSERT(coremap[cm_index].allocated);

        // Check if we should continue, unlock this entry
        has_next = coremap[cm_index].has_next;
        if (has_next) {
            CM_DEBUG("continuing to next contiguous page\n");
        }

        KASSERT(coremap[cm_index].busy);

        // If this is not the kernel, set this to 'free' in the backing store
        if (as != NULL) {
            pte_lock(as, coremap[cm_index].vm_addr);
            struct pt_entry* pt_entry = pt_get_entry(as, coremap[cm_index].vm_addr);
            bs_index = pt_entry->store_index;
            bs_dealloc_index(bs_index);
            pt_entry->store_index = 0;
            pte_unlock(as, coremap[cm_index].vm_addr);
        }

        KASSERT(coremap[cm_index].busy);

        // Fill with something recognizable for debugging purposes
        if (as == NULL) {
            for (unsigned i = 0; i < 1024; i++) {
                ((unsigned*)PADDR_TO_KVADDR(CM_TO_PADDR(cm_index)))[i] = 0xDEA110C1;
            }
        }

        KASSERT(coremap[cm_index].busy);

        bzero((void *)PADDR_TO_KVADDR(paddr), PAGE_SIZE);
        CM_DEBUG("deallocating (cm_entry) %d from (addrspace) %p...", cm_index, as);
        coremap[cm_index].allocated     = 0;
        coremap[cm_index].vm_addr       = 0;
        coremap[cm_index].is_kernel     = 0;
        coremap[cm_index].has_next      = 0;
        coremap[cm_index].used_recently = 0;
        coremap[cm_index].dirty         = 0;
        coremap[cm_index].as            = 0;

        cm_used_change(-1);

        CM_DONE;

        coremap[cm_index].busy = false;
        cm_index++;
    }
    return true;
}

/**
 * @brief Gets the index of an unused coremap entry, if one exists
 * @details Linearly probes the coremap until it finds an unused entry, sets it
 *          to busy, and returns it
 * @return The index of an unsed coremap entry, or -1 if none exist
 */
int cm_get_free_page(void) {
    int i;

    // We should not be using more page table entries than exist
    KASSERT(cm_entries >= cm_used);

    // Short circuit if we know there are no more entries
    if (cm_entries == cm_used) {
        return -1;
    }

    // Linearly probe for a free entry and mark it as busy
    // Start from end to reduce kernel/user contention
    //for (i = cm_entries - 1; i > 0; i--) {
    for (i = 0; i < (int)cm_entries; i++) {
        spinlock_acquire(&busy_lock);
        if (!coremap[i].allocated && !coremap[i].busy) {
            coremap[i].busy = true;
            spinlock_release(&busy_lock);
            return i;
        }
        spinlock_release(&busy_lock);
    }

    // Either cm_used = cm_entries, or there was a free space
    //KASSERT(false);
    // The previous assertion is not correct. The state of the coremap could
    //  have changed between our (cm_entries == cm_used) check and our linear
    //  probe. A page may have been taken by another processor
    return -1;
}

/**
 * @brief Evict a specific page of memory
 * @details This function writes out the page associated with the coremap if dirty,
 *          sets the assocated pagetable entry to not be in memory, shoots down
 *          the associated TLB entry, and sets the coremap to be unused
 * 
 * @param cm_index The index of the coremap entry to evict
 * @return The index of the page that was evicted
 */
static int cm_do_evict(int cm_index, struct addrspace *old_as, vaddr_t old_va) {
    // The entry we are evicting should already be set busy by the caller
    KASSERT(coremap[cm_index].busy);
    KASSERT(coremap[cm_index].allocated);

    struct addrspace* as = coremap[cm_index].as;
    vaddr_t vaddr = coremap[cm_index].vm_addr;
    int err = 0;
    bool locked;

    locked = pte_locked(as, vaddr);

    // If we are already holding a lock, then make sure it's the same as the one that belongs to the pagetable entry we're about to evict
    if (locked)
        KASSERT(lock_do_i_hold(as->pt_locks[vaddr >> 22]));

    if (!locked) {
        // pte_lock(as, vaddr);
        // To avoid deadlock, acquire AS locks in order of raw pointer value
        if (old_as != NULL) {
            // user
            if ((int)old_as < (int)as){
                pte_unlock(old_as, old_va);
                pte_lock(as, vaddr);
                pte_lock(old_as, old_va);
            } else {
                pte_lock(as, vaddr);
            }
        } else {
            // kernel
            pte_lock(as, vaddr);
        }
    }
        

    // Pagetable entries can dissappear between the call to cm_do_evict and now. In that case, we don't have to do any work
    struct pt_entry *pt_entry = pt_get_entry(as, vaddr);
    if (pt_entry == NULL || !pt_entry->allocated)
        return cm_index;


    // We invalidate the virtual address on all cpus before we touch the pagetable entry
    ipi_tlbshootdown_allcpus(&(const struct tlbshootdown){vaddr, tlb_sem});

    CM_DEBUG("paging out (cm_entry) %d from (addrspace) %p...", cm_index, as);

    // If dirty, write the page to disk and set it to clean
    if (coremap[cm_index].dirty) {
        err = bs_write_out(cm_index);
        KASSERT(!err);
        coremap[cm_index].dirty = 0;
    }

    // Set the pagetable entry to not be in memory
    KASSERT(pt_entry != NULL);
    pt_entry->in_memory = 0;

    if (!locked) pte_unlock(as, vaddr);

    // Set this coremap entry to be unused
    coremap[cm_index].allocated = 0;

    CM_DONE;
    
    // Track number of used coremap entries
    cm_used_change(-1);

    KASSERT(coremap[cm_index].busy);
    KASSERT(!coremap[cm_index].allocated);

    return cm_index;
}

/**
 * @brief Evict some page of memory
 * @details The function guarantees that it will return the index of an unused
 *          coremap entry with its busy bit set. This function may have to
 *          evict a page to ensure this
 * @return An unused coremap entry
 */
int cm_evict_page(struct addrspace *as, vaddr_t va){
    int cm_index;

    cm_index = cm_choose_evict_page();

    return cm_do_evict(cm_index, as, va);
}

// NOT COMPLETE
/* Evict the "next" page from memory. This will be dependent on the 
eviction policy that we choose (clock, random, etc.). This is 
where we will switch out different eviction policies. The resulting
page should not be busy, a kernel page, or unallocated */
#ifdef PAGE_LINEAR
int cm_choose_evict_page() {
    int i = 0;
    while (true) {
        spinlock_acquire(&busy_lock);
        if (coremap[i].busy || coremap[i].is_kernel || !coremap[i].allocated){
            spinlock_release(&busy_lock);
            i = (i + 1) % cm_entries;
            continue;
        } else {
            coremap[i].busy = true;
            KASSERT(coremap[i].busy);
            spinlock_release(&busy_lock);
            return i;
        }
    }
}
#elif defined(PAGE_RANDOM)
int cm_choose_evict_page() {
    int i = random() % cm_entries;
    while (true) {
        spinlock_acquire(&busy_lock);
        if (coremap[i].busy || coremap[i].is_kernel || !coremap[i].allocated){
            spinlock_release(&busy_lock);
            i = (i + 1) % cm_entries;
            continue;
        } else {
            coremap[i].busy = true;
            KASSERT(coremap[i].busy);
            spinlock_release(&busy_lock);
            return i;
        }
    }
}
#elif defined(PAGE_CLOCK)
int cm_choose_evict_page() {
    while (true) {
        spinlock_acquire(&busy_lock);
        if (coremap[i].busy || coremap[i].is_kernel || !coremap[i].allocated || coremap[i].used_recently){
            spinlock_release(&busy_lock);
            if (coremap[i].used_recently) {
                coremap[i].used_recently = false;
            }
            evict_hand = (evict_hand + 1) % cm_entries;
            continue;
        } else {
            coremap[i].busy = true;
            spinlock_release(&busy_lock);
            return i;
        }
    }
}
#endif

/**
 * @brief Set a page to dirty, ignoring synchronization
 * @details The only other logic touching the dirty bit should be the evictor and writer
 * 
 * @param paddr Physical address to mark as dirty
 */
inline void cm_set_dirty(paddr_t paddr) {
    int cm_index = PADDR_TO_CM(paddr);
    coremap[cm_index].dirty = true;
}


// Code for backing storage, could be moved to somewhere else.

void bs_bootstrap() {
    struct stat f_stat;

    // open file, bitmap and lock
    char *path = kstrdup("lhd0raw:");
    if (path == NULL)
        panic("bs_bootstrap: couldn't open disk");

    int err = vfs_open(path, O_RDWR, 0, &bs_file);
    if (err)
        panic("bs_bootstrap: couldn't open disk");

    // Get swap space
    VOP_STAT(bs_file, &f_stat);
    mem_free = f_stat.st_size;

    bs_map = bitmap_create(mem_free / PAGE_SIZE);
    if (bs_map == NULL)
        panic("bs_bootstrap: couldn't create disk map");

    bs_map_lock = lock_create("disk map lock");
    if (bs_map_lock == NULL)
        panic("bs_bootstrap: couldn't create disk map lock");
    
    mem_free_lock = lock_create("Free memory lock");
    if (mem_free_lock == NULL)
        panic("bs_bootstrap: couldn't create free memory tracker lock");

    bs_alloc_index();
    
    cm_mem_change(-1);
    
    return;
}
/**
 * @brief Writes the page referenced by cm_index to disk
 * @details Assumes that the pagetable entry associated with the coremap entry
 *          has already been locked. Does not do any updating..
 * 
 * @param cm_index Index of the coremap entry to evict
 * @return Error, if nany
 */
int bs_write_out(int cm_index) {
    int err, offset;
    paddr_t paddr = CM_TO_PADDR(cm_index);
    struct addrspace *as = coremap[cm_index].as;
    vaddr_t vaddr = coremap[cm_index].vm_addr;

    KASSERT(pte_locked(as, vaddr));
    struct pt_entry *pt_entry = pt_get_entry(as, vaddr);

    KASSERT(pt_entry->store_index);
    offset = pt_entry->store_index;

    BS_DEBUG("writing page (paddr) %x to disk at (offset) %d...", paddr, offset);
    err = bs_write_page((void *) PADDR_TO_KVADDR(paddr), offset);
    BS_DONE;

    return err;
}

// Put stuff in dest.
// NOTE: assert that we alread have locked the virtual address
int bs_read_in(struct addrspace *as, vaddr_t vaddr, int cm_index) {
    int err;
    unsigned offset;
    paddr_t paddr = CM_TO_PADDR(cm_index);

    KASSERT(pte_locked(as, vaddr));
    struct pt_entry *pte = pt_get_entry(as, vaddr);

    KASSERT(pte->store_index);
    offset = pte->store_index;

    err = bs_read_page((void *) PADDR_TO_KVADDR(paddr), offset);
    if (err)
        return err;

    pte->in_memory = 1;
    pte->p_addr = paddr;
    return 0;
}


int bs_write_page(void *vaddr, unsigned offset) {
    struct iovec iov;
    struct uio u;
    uio_kinit(&iov, &u, vaddr, PAGE_SIZE, 
        offset*PAGE_SIZE, UIO_WRITE);

    return VOP_WRITE(bs_file,&u);
}

int bs_read_page(void *vaddr, unsigned offset) {
    struct iovec iov;
    struct uio u;
    uio_kinit(&iov, &u, vaddr, PAGE_SIZE, 
        offset*PAGE_SIZE, UIO_READ);

    return VOP_READ(bs_file,&u);
}

unsigned bs_alloc_index() {
    unsigned index;

    lock_acquire(bs_map_lock);
    if (bitmap_alloc(bs_map, &index))
        panic("no space on disk");
    lock_release(bs_map_lock);
    return index;
}

void bs_dealloc_index(unsigned index) {
    lock_acquire(bs_map_lock);

    KASSERT(bitmap_isset(bs_map, index));
    bitmap_unmark(bs_map, index);

    lock_release(bs_map_lock);
    return;
}
