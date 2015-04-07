

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <tlb.h>
#include <mips/vm.h>
#include <addrspace.h>
#include <vm.h>
#include <spinlock.h>
#include <mainbus.h>
#include <coremap.h>

// static // ???
// struct cm_entry* coremap;
// 
// static
// struct spinlock coremap_lock;
// 
// extern paddr_t firstpaddr;

void vm_bootstrap(void)
{
	cm_bootstrap();
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t alloc_kpages(unsigned npages)
{
	(void) npages;
	if (npages > 1)
		panic("alloc_kpages: don't support multi-page alloc");
	paddr_t pa = cm_alloc_page(NULL, 0);
	if (pa == 0)
		panic("Fuck everything is broken");
	return PADDR_TO_KVADDR(pa);
}

// Free kernel pages
void free_kpages(vaddr_t addr)
{
	// For debugging purposes. Try not to forget to delete this
    memset((void*)addr, 0x47, 4096);

	paddr_t paddr = KVADDR_TO_PADDR(addr);
	cm_dealloc_page(NULL, paddr);
}

void vm_tlbshootdown_all(void)
{
	int spl;
	int i;

	spl = splhigh();
	for (i = 0; i < NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}
	splx(spl);
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	int spl;
	int i;

	spl = splhigh();
	i = tlb_probe(ts->target, 0);
	tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);	
	splx(spl);
}

int vm_fault(int faulttype, vaddr_t faultaddress)
{
	struct pt_entry *pt_entry;
	uint32_t tlbhi, tlblo;
	// TODO: implement regions
	// struct as_region *region;

	// // Address space checks
	// region = as_get_region(faultaddress);


	// // Check that the faulting address is in the address space
	// if (region == NULL) {
	// 	return EFAULT;
	// }

	// // Process lacks permissions for accessing this address
	// if (faulttype == VM_FAULT_READ     && !region->readable ||	// Result of read
	// 	faulttype == VM_FAULT_WRITE    && !region->writable ||	// Result of write
	// 	faulttype == VM_FAULT_READONLY && !region->writable) {	// Result of write
	// 	return EFAULT;
	// }

	// If we have reached this point, the process has access to the faulting address

	pt_entry = pt_get_entry(curproc->p_addrspace, faultaddress);

	// Check if the page containing the address has been allocated.
	// If not, we will allocate the page and let the switch block handle tlb loading
	if (!pt_entry || !pt_entry->allocated) {
		pt_entry = pt_alloc_page(curproc->p_addrspace, faultaddress & PAGE_MASK);
	}

	lock_acquire(pt_entry->lk);

	// The page has been allocated. Check if it is in physical memory.
	if (pt_entry->p_addr == 0) {
		KASSERT(pt_entry->store_index != 0);
		KASSERT(faulttype != VM_FAULT_READONLY);
		cm_load_page(curproc->p_addrspace, faultaddress & PAGE_MASK);
	}

	// All the above checks *should* mean it's safe to just load it in
	tlbhi = faultaddress & PAGE_MASK;
	tlblo = (pt_entry->p_addr & PAGE_MASK) | VALID;

	paddr_t paddr = pt_entry->p_addr;

	lock_release(pt_entry->lk);

	switch (faulttype) {
		case VM_FAULT_READ:
			// This occurs when reading from a page not in the TLB
		case VM_FAULT_WRITE:
			// This occurs when writing to a page not in the TLB
			// Random replacement
			tlb_random(tlbhi, tlblo);
			break;
		case VM_FAULT_READONLY:
			// This occurs when the user tries to write to a clean page

			// Set the pagetable entry to now be dirty
			cm_set_dirty(paddr);

			tlblo |= WRITABLE;

			// Replace the faulting entry with the writable one
			int index = tlb_probe(faultaddress & PAGE_MASK, 0);
			tlb_write(tlbhi, tlblo, index);
	}


	return 0;
}