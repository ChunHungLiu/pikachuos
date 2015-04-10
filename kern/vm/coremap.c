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

//#define CM_DEBUG(message...) kprintf("cm: ");kprintf(message);
#define CM_DEBUG(message...) ;
#define CM_DONE (void)0;

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

struct vnode *bs_file;
struct bitmap *bs_map;
struct lock *bs_map_lock;

void cm_bootstrap(void) {
	int i;
    paddr_t mem_start, mem_end;
    uint32_t npages, cm_size;
    (void) &busy_lock;
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

	// this is kinda strange, we may end up having unused cormap space.
	ram_stealmem(cm_size / PAGE_SIZE);
    mem_start += cm_size;

	cm_entries = (mem_end - mem_start) / PAGE_SIZE;
	cm_base = mem_start / PAGE_SIZE;
    cm_used = 0;

    // TODO: Can be replaced with memset
	for (i=0; i<(int)cm_entries; i++) {
        coremap[i].vm_addr = 0;
        coremap[i].busy = 0;
        coremap[i].is_kernel = 0;
        coremap[i].allocated = 0;
        coremap[i].has_next = 0;
        coremap[i].dirty = 0;
        coremap[i].used_recently = 0;
        coremap[i].as = NULL;    
    }

    // Initialize lock on the number of used entries
    //cm_used_lock = lock_create("Used coremap lock");
}

/* Load page from back store to memory. May call page_evict_any if there’s no more physical memory. See Paging for more details. */
paddr_t cm_load_page(struct addrspace *as, vaddr_t va) {
    // Code goes here
    // Basically do cm_alloc_page and then load
    // TODO: bs_read_in should happen here
    (void) as;
    (void) va;
    // paddr_t pa;
    // pa = cm_alloc_page(as, va);
    // bs_read_in();

    return 0;
}

/* 
 * Notes:
 *  a NULL address space indicates that this is a kernel page
 */
paddr_t cm_alloc_page(struct addrspace *as, vaddr_t va) {
    KASSERT(va != 0);
    // TODO: design choice here, everything needs to be passed down
    int cm_index;
    // Try to find a free page. If we have one, it's easy. We probably
    // want to keep a global cm_free veriable to boost performance
    cm_index = cm_get_free_page();
    
    // We don't have any free page any more, needs to evict.
    if (cm_index < 0) {
        // Do page eviction
        cm_index = cm_evict_page();
    }
    // cm_index should be a valid page index at this point
    KASSERT(coremap[cm_index].busy == true);
    // If alread free, this should not be allocated. If evicted, should not be either
    KASSERT(coremap[cm_index].allocated == 0);
    CM_DEBUG("allocating (cm_entry) %d to (addrspace) %p...", cm_index, as);
    coremap[cm_index].allocated = 1;

    if (as != NULL) {
        struct pt_entry *pte = pt_get_entry(as, va);
        if (!pte->in_memory && pte->allocated) {
            bs_read_in(as, va, cm_index);
        }
    }
    // KASSERT(va != 0);
    coremap[cm_index].vm_addr = va;
    coremap[cm_index].as = as;
    coremap[cm_index].is_kernel = (as == NULL);
    coremap[cm_index].busy = false;

    //if (cm_used_lock) {
        spinlock_acquire(&cm_used_lock);
        cm_used++;
        //kprintf("cm: %d of %d pages used\n", cm_used, cm_entries);
        spinlock_release(&cm_used_lock);
    //}

    CM_DONE;
    return CM_TO_PADDR(cm_index);
}

/* Linear probing to find free contiguous pages. We reserve memory and try to
 * grow it. If we encounter a kernel page (can't be moved), then we unreserve
 * the previous pages we had and start over after it. */
paddr_t cm_alloc_npages(unsigned npages) {
    // This should be the kernel calling this. Can we check this
    unsigned start_index = 0, end_index = 0;
    for (end_index = 0; end_index < cm_entries; end_index++) {
        spinlock_acquire(&busy_lock);
        if (coremap[end_index].busy ||
            coremap[end_index].allocated) {
            spinlock_release(&busy_lock);
            // Entry busy or can't be moved. Give up and restart the contiguous region
            // Set the pages we had reserved to not busy
            for (; start_index < end_index; start_index++)
                coremap[start_index].busy = false;
            KASSERT(start_index == end_index);
            // start_index should point to the start of a potentially free region
            start_index++;
            continue;
        } else {
            // This page is free!!! Reserve it
            coremap[end_index].busy = true;
            spinlock_release(&busy_lock);
            // Check if we are done
            KASSERT(end_index - start_index < npages);
            if (end_index - start_index == npages - 1) {
                // Take ownership of all the reserved ones
                for (unsigned i = start_index; i <= end_index; i++) {
                    CM_DEBUG("allocating (cm_entry) %d to kernel...", i);
                    coremap[i].vm_addr = CM_TO_PADDR(i);  // TODO TEMP: for debugging. Should get overridden anyway
                    coremap[i].is_kernel = true;
                    coremap[i].allocated = true;
                    // Can't be set after we set busy to false
                    if (i < end_index)
                        coremap[i].has_next = true;
                    coremap[i].busy = false;
                    CM_DONE;
                }

                //if (&cm_used_lock) {
                    spinlock_acquire(&cm_used_lock);
                    cm_used += npages;
                    spinlock_release(&cm_used_lock);
                //}

                return CM_TO_PADDR(start_index);
            }
        }
        //KASSERT(!spinlock_do_i_hold(&busy_lock)); // Seems to be hanging on this
    }
    return 0;
}

void cm_dealloc_page(struct addrspace *as, paddr_t paddr) {
    int cm_index;
    // int bs_index;
    bool has_next = true;

    cm_index = PADDR_TO_CM(paddr);

    // Loop until all pages in a multipage chain are deallocated
    while (has_next) {

        // Lock the coremap entry
        spinlock_acquire(&busy_lock);
        coremap[cm_index].busy = true;
        spinlock_release(&busy_lock);

        // Check if we should continue, unlock this entry
        has_next = coremap[cm_index].has_next;
        if (has_next) {
            CM_DEBUG("continuing to next contiguous page\n");
        }

        // If this is not the kernel, set this to 'free' in the backing store
        // TODO: where should we unset bitmap??
        if (as != NULL) {
            struct pt_entry *pt_entry = pt_get_entry(as, coremap[cm_index].vm_addr);
            lock_acquire(pt_entry->lk);
            // bs_index = pt_entry->store_index;
            lock_release(pt_entry->lk);
            // bs_dealloc_index(bs_index);
        }

        CM_DEBUG("deallocating (cm_entry) %d from (addrspace) %p...", cm_index, as);
        coremap[cm_index].allocated     = 0;
        coremap[cm_index].vm_addr       = 0;
        coremap[cm_index].is_kernel     = 0;
        coremap[cm_index].allocated     = 0;
        coremap[cm_index].has_next      = 0;
        coremap[cm_index].used_recently = 0;
        coremap[cm_index].dirty         = 0;
        coremap[cm_index].as            = 0;


        //if (&cm_used_lock) {
            spinlock_acquire(&cm_used_lock);
            cm_used--;
            spinlock_release(&cm_used_lock);
        //}

        CM_DONE;

        // The pagetable entry should be gone...nevermind, we need the backing store index
        if (as != NULL) {
           struct pt_entry *pt_entry = pt_get_entry(as, coremap[cm_index].vm_addr);
           KASSERT(pt_entry != NULL);
           bs_dealloc_index(pt_entry->store_index);
        }

        coremap[cm_index].busy = false;
        cm_index++;
    }
}

// Returns a index where a page is free
int cm_get_free_page(void) {
    unsigned i;
    KASSERT(cm_entries >= cm_used);
    if (cm_entries == cm_used) {
        return -1;
    }
    // Do we want to use busy_lock here?
    for (i = 0; i < cm_entries; i++){
        spinlock_acquire(&busy_lock);
        if (!coremap[i].allocated) {
            coremap[i].busy = true;
            spinlock_release(&busy_lock);
            return i;
        }
        spinlock_release(&busy_lock);
    }

    // Either cm_used = cm_entries, or there was a free space
    int index = bs_alloc_index();
    kprintf("%d\n", index);
    KASSERT(false);
}

/* Evict page from memory. This function will update coremap, write to backstore and update the backing_index entry; */
// Need to sync 2 addrspaces
// Simply update the pte related to paddr
int cm_evict_page(){
    // Use our eviction policy to choose a page to evict
    // coremap[cm_index] should be busy when this returns
    // TODO: There's no synchronization at all. 
    int cm_index;

    cm_index = cm_choose_evict_page();

    // Write to backing storage no matter what -- Not anymore! We now have dirty bits!
    KASSERT(coremap[cm_index].busy);
    if (coremap[cm_index].dirty) {
        bs_write_out(cm_index);
        coremap[cm_index].dirty = 0;
    }

    // Need to find the pt entry and mark it as not in memory anymore
    struct pt_entry *pte = pt_get_entry(coremap[cm_index].as, coremap[cm_index].vm_addr);
    KASSERT(pte != NULL);

    // TODO: possible deadlock bug
    lock_acquire(pte->lk);
    pte->in_memory = 0;
    // Shoot down this entry
    //vm_tlbshootdown_all();
    lock_release(pte->lk);

    coremap[cm_index].allocated = 0;
    
    spinlock_acquire(&cm_used_lock);
    cm_used--;
    spinlock_release(&cm_used_lock);

    return cm_index;
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

// NOT COMPLETE
/* Evict the "next" page from memory. This will be dependent on the 
eviction policy that we choose (clock, random, etc.). This is 
where we will switch out different eviction policies */
#ifdef PAGE_LINEAR
int cm_choose_evict_page() {
    int i = 0;
    while (true) {
        spinlock_acquire(&busy_lock);
        if (coremap[i].busy || coremap[i].is_kernel){
            spinlock_release(&busy_lock);
            i = (i + 1) % cm_entries;
            continue;
        } else {
            CM_SET_BUSY(coremap[i]);
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
        if (coremap[i].busy || coremap[i].is_kernel){
            spinlock_release(&busy_lock);
            i = (i + 1) % cm_entries;
            continue;
        } else {
            CM_SET_BUSY(coremap[i]);
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
        if (coremap[i].busy || coremap[i].is_kernel || coremap[i].used_recently){
            spinlock_release(&busy_lock);
            if (coremap[i].used_recently) {
                coremap[i].used_recently = false;
            }
            evict_hand = (evict_hand + 1) % cm_entries;
            continue;
        } else {
            CM_SET_BUSY(coremap[i]);
            spinlock_release(&busy_lock);
            return i;
        }
    }
}
#endif

/* Blocks until a coremap entry can be set as dirty */
void cm_set_dirty(paddr_t paddr) {
    // Don't worry about synchronization until we combine the bits with the vm_addr
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
    lock_acquire(mem_free_lock);
    mem_free--;
    lock_release(mem_free_lock);
    
    return;
}

int bs_write_out(int cm_index) {
    int err, offset;
    paddr_t paddr = CM_TO_PADDR(cm_index);
    struct addrspace *as = coremap[cm_index].as;
    vaddr_t va = coremap[cm_index].vm_addr;
    struct pt_entry *pte = pt_get_entry(as, va);

    // TODO: error checking
    offset = pte->store_index;
    err = bs_write_page((void *) PADDR_TO_KVADDR(paddr), offset);

    // TODO: This will relate to dirty page management

    return err;
}

// Put stuff in dest.
int bs_read_in(struct addrspace *as, vaddr_t va, int cm_index) {
    int err;
    unsigned offset;
    paddr_t paddr = CM_TO_PADDR(cm_index);
    struct pt_entry *pte = pt_get_entry(as, va);

    // TODO: error checking
    offset = pte->store_index;
    err = bs_read_page((void *) PADDR_TO_KVADDR(paddr), offset);
    if (!err){
        pte->in_memory = 1;
        pte->p_addr = paddr;
    }

    return err;
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
