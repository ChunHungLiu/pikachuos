#include <pagetable.h>
#include <addrspace.h>

// TODO: coremap interface goes here

#ifndef _COREMAP_H_
#define _COREMAP_H_

/* Macros for access to the coremap entry */
//#define CM_IS_KERNEL(CE) (1 & (CE).vm_addr)
//#define CM_ALLOCATED(CE) (2 & (CE).vm_addr)
//#define CM_HAS_NEXT(CE)  (4 & (CE).vm_addr)
//#define CM_VM_ADDR(CE)   (0xFFFFF000 & (CE).vm_addr)
//
//#define CM_SET_IS_KERNEL(CE, value) ((CE).vm_addr = (CE).vm_addr & 0xFFFFFFFE | value)
//#define CM_SET_ALLOCATED(CE, value) ((CE).vm_addr = (CE).vm_addr & 0xFFFFFFFD | (value << 1))
//#define CM_SET_HAS_NEXT(CE, value)  ((CE).vm_addr = (CE).vm_addr & 0xFFFFFFFB | (value << 2))
//#define CM_SET_VM_ADDR(CE, value)   ((CE).vm_addr = (CE).vm_addr & 0x00000FFF | (value << 12))

#define CM_IS_BUSY(CE)		((CE).busy)
#define CM_SET_BUSY(CE)		((CE).busy = true)
#define CM_UNSET_BUSY(CE)	((CE).busy = false)

struct cm_entry {
	vaddr_t vm_addr;		// The vm translation of the physical address. Only upper 20 bits get used
	bool is_kernel;		// Note if this is a kernel page or not
	bool allocated;		// Note if the physical address is allocated or not
	bool has_next;		// Indicating that we have a cross-page allocation. Only used for the kernel???
	bool busy;
	bool used_recently;
	bool dirty;
	int pid;			// The process who owns this memory
	struct addrspace *as;
};

struct vnode *back_store;
struct bitmap *disk_map;

int find_free_page(void);

void cm_bootstrap(void);

/* 
 * Evict the "next" page from memory. This will be dependent on the eviction 
 * policy that we choose (clock, random, etc.). This is where we will switch 
 * out different eviction policies 
 */
// Consider returning the page we evicted
int cm_choose_evict_page(void);

/* 
 * Evict page from memory. This function will update coremap, write to 
 * backstore and update the backing_index entry; 
 */
int cm_evict_page(void);

/*
 * Allocate a page of memory, pointing back to the virtual address in the
 * address space that references it
 */
paddr_t cm_alloc_page(struct addrspace *as, vaddr_t va);

/* Find a contiguous npages of memory */
paddr_t cm_alloc_npages(unsigned npages);

/* 
 * Deallocates a page of memory specified by the physical address
 */
void cm_dealloc_page(struct addrspace *as, paddr_t paddr);

/* Load page from the backing store into a specific page of physical memory (used as a helper function for page_load) */
paddr_t cm_load_page(struct addrspace *as, vaddr_t va);

/* Blocks until a coremap entry can be set as dirty */
void cm_set_dirty(paddr_t paddr);

int cm_get_free_page(void);

void bs_bootstrap(void);
int bs_write_out(int cm_index);
int bs_read_in(struct addrspace *as, vaddr_t va, int cm_index);
unsigned bs_alloc_index(void);
void bs_dealloc_index(unsigned index);
#endif