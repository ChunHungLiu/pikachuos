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
#include <elf.h>

#define TLB_DEBUG(message...) kprintf("sbrk: ");kprintf(message);
#define TLB_DONE kprintf("done\n");
//#define TLB_DEBUG(message...) ;
//#define TLB_DONE (void)0;

static struct lock *tlb_lock;

void vm_bootstrap(void)
{
    cm_bootstrap();
    tlb_lock = lock_create("TLB");
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t alloc_kpages(unsigned npages)
{
    paddr_t pa = cm_alloc_npages(npages);
    if (pa == 0)
        panic("Fuck everything is broken");
    return PADDR_TO_KVADDR(pa);
}

// Free kernel pages
void free_kpages(vaddr_t addr)
{
    KASSERT(addr & 0x80000000); // Check that this is actually a kernel page

    paddr_t paddr = KVADDR_TO_PADDR(addr);
    cm_dealloc_page(NULL, paddr);
}

void vm_tlbshootdown_all(void)
{
    panic("HELP!!! vm_tlbshootdown_all called");
}

void vm_tlbflush_all(void)
{
    int spl;
    int i;

    spl = splhigh();
    for (i = 0; i < NUM_TLB; i++) {
        tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
    }
    splx(spl);
}

void vm_tlbflush(vaddr_t target) {
    int spl;
    int i;

    spl = splhigh();
    i = tlb_probe(target & PAGE_MASK, 0);
    if (i != -1)
        tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
    splx(spl);
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
    vm_tlbflush(ts->target);
    V(ts->sem);
}

int vm_fault(int faulttype, vaddr_t faultaddress)
{
    if (faultaddress == 0)
        panic("NULL pointer exception");

    struct pt_entry *pt_entry;
    uint32_t tlbhi, tlblo;
    int spl, perms;

    struct addrspace* as = curproc->p_addrspace;

    //KASSERT(faultaddress != 0);

    // Address space checks
    perms = as_check_region(as, faultaddress);
    if (perms < 0 && 
        (faultaddress < USERSTACK - VM_STACKPAGES * PAGE_SIZE || faultaddress > USERSTACK) &&
        (faultaddress < as->heap_start || faultaddress > as->heap_end)) {
        return EFAULT;
    }

    // Process lacks permissions for accessing this address
    //if ((faulttype == VM_FAULT_READ     && !(perms & PF_R)) ||   // Result of read
    //    (faulttype == VM_FAULT_WRITE    && !(perms & PF_W)) ||  // Result of write
    //    (faulttype == VM_FAULT_READONLY && !(perms & PF_W))) {  // Result of write
    //    return EFAULT;
    //}

    // If we have reached this point, the process has access to the faulting address

    // Create L2 lock if necessary
    int index_hi = faultaddress >> 22;
    if (as->pt_locks[index_hi] == NULL) {
        as->pt_locks[index_hi] = lock_create("pt");
    }
    if (as->pagetable[index_hi] == NULL) {
        as->pagetable[index_hi] = kmalloc(PT_LEVEL_SIZE * sizeof(struct pt_entry));
        memset(as->pagetable[index_hi], 0, PT_LEVEL_SIZE * sizeof(struct pt_entry));
    }

    // Lock pagetable entry
    pte_lock(as, faultaddress);
    pt_entry = pt_get_entry(as, faultaddress);

    // Check if the page containing the address has been allocated.
    // If not, we will allocate the page and let the switch block handle tlb loading
    if (!pt_entry || !pt_entry->allocated) {
        pt_entry = pt_alloc_page(as, faultaddress & PAGE_MASK);
    }

    // The page has been allocated. Check if it is in physical memory.
    if (!pt_entry->in_memory) {
        // KASSERT(faulttype != VM_FAULT_READONLY);
        cm_load_page(as, faultaddress & PAGE_MASK);
    }

    // All the above checks *should* mean it's safe to just load it in
    tlbhi = faultaddress & PAGE_MASK;
    tlblo = (pt_entry->p_addr & PAGE_MASK) | VALID;

    paddr_t paddr = pt_entry->p_addr;

    pte_unlock(as, faultaddress);

    spl = splhigh();

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

    splx(spl);

    return 0;
}