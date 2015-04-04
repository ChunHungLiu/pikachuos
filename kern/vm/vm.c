

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <mips/vm.h>
#include <addrspace.h>
#include <vm.h>
#include <spinlock.h>
#include <mainbus.h>

static // ???
pt_entry
cm_entry* coremap;

static
struct spinlock coremap_lock;


void vm_bootstrap(void)
{
	// If this isn't 8, we're wasting space
	KASSERT(sizeof(pt_entry
cm_entry) == 8);

	// Calculate how many cm_entries we need and nicely steal it
	// Slightly inefficient since firstpaddr might not be 0, but whatevs
	uint32_t page_count = mainbus_ramsize() / PAGE_SIZE;
	coremap = (pt_entry
cm_entry*) firstpaddr;
	firstpaddr += page_count * sizeof(pt_entry
cm_entry);

	(void) coremap;
	(void) coremap_lock;

	// Initialize the lock for the coremap
	spinlock_init(&coremap_lock);
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t alloc_kpages(unsigned npages)
{
	(void) npages;

	return 0;
}

void free_kpages(vaddr_t addr)
{
	/* nothing - leak the memory. */

	(void)addr;
}

void vm_tlbshootdown_all(void)
{
	panic("You lazy bum. Why haven't you written me?!\n");
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("Get off your ass and start coding me!!!\n");
}

static pt_entry
pt_entry* get_pt_entry(vaddr_t v_addr) {
	(void) v_addr;
	return NULL;
}

static
void tlb_replacer(uint32_t tlbhi, uint32_t tlblo, int faulttype, vaddr_t faultaddress) {
	tlb_random(tlbhi, tlblo);
	(void)tlbhi;
	(void)tlblo;
	(void)faulttype;
	(void)faultaddress;
	return;
}

int vm_fault(int faulttype, vaddr_t faultaddress)
{
	struct pt_entry *page;
	uint32_t tlbhi, tlblo;
	switch (faulttype) {
		case VM_FAULT_READ:
			// This occurs when the page is not in the TLB
		case VM_FAULT_WRITE:
			// This occurs when the page is not in the TLB
			// Get page
			page = get_pt_entry(faultaddress);
			// Permissions check should go here

			// calculate what should go in the TLB
			tlbhi = faultaddress & PAGE_MASK;
			tlblo = (page->p_addr & PAGE_MASK) | VALID;
			// Random replacement
			tlb_replacer(tlbhi, tlblo, faulttype, faultaddress);
			break;
		case VM_FAULT_READONLY:
			// This occurs when the user tries to write to a clean page
			// Get page
			page = get_pt_entry(faultaddress);
			// Check permissions to see if write is allowed

			// Set the pagetable entry to now be dirty
			PT_SET_DIRTY(*page);

			// calculate what should go in the TLB
			tlbhi = faultaddress & PAGE_MASK;
			tlblo = (page->p_addr & PAGE_MASK) | DIRTY | VALID;

			// Replace the faulting entry with the writable one
			int index = tlb_probe(faultaddress & PAGE_MASK, 0);
			tlb_write(tlbhi, tlblo, index);
	}

	return 0;
}