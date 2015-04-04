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
struct pt_entry*** pagetable_create() {
	// Code goes here
	return kmalloc(PT_LEVEL_SIZE * sizeof(struct pt_entry*));
}

/* Create a new page. This will only be called when in a page fault, the faulting address is in valid region but no page is allocated for it. Calls cm_allocuser()/cm_allockernel() to find an empty physical page, and put the new entry into the associated 2-level page table. */
struct pt_entry* page_create() {
	// Code goes here
	return kmalloc(sizeof(struct pt_entry));
}