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

typedef struct {
	vaddr_t vm_addr;		// The vm translation of the physical address. Only upper 20 bits get used
	bool is_kernel;		// Note if this is a kernel page or not
	bool allocated;		// Note if the physical address is allocated or not
	bool has_next;		// Indicating that we have a cross-page allocation. Only used for the kernel???
	int pid;			// The process who owns this memory
} cm_entry_t;

struct vnode *back_store;
struct bitmap *disk_map;

int find_free_page(void);

void cm_bootstrap(void);

#endif