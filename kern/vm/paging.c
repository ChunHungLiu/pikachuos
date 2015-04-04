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
pt_entry_t*** pagetable_create() {
	// Code goes here
	return kmalloc(PT_LEVEL_SIZE * sizeof(pt_entry_t*));
}

/* Create a new page. This will only be called when in a page fault, the faulting address is in valid region but no page is allocated for it. Calls cm_allocuser()/cm_allockernel() to find an empty physical page, and put the new entry into the associated 2-level page table. */
pt_entry_t* page_create() {
	// Code goes here
	return kmalloc(sizeof(pt_entry_t));
}