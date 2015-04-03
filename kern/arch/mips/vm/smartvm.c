

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

void vm_bootstrap(void)
{
	/* Do nothing. */
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

int vm_fault(int faulttype, vaddr_t faultaddress)
{
	(void) faulttype;
	(void) faultaddress;

	return 0;
}