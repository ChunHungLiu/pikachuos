/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include <coremap.h>

struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

	/*
	 * Initialize as needed.
	 */
	as->pt_locks = kmalloc(PT_LEVEL_SIZE * sizeof(struct lock*));
	for (int i = 0; i < PT_LEVEL_SIZE; i++)
		as->pt_locks[i] = NULL;
	as->pagetable = pagetable_create();
	
	KASSERT(as->pt_locks);
	KASSERT(as->pagetable);

	as->as_regions = array_create();
	as->heap_start = 0;
	as->heap_end = 0;

	return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;
	int i,j, errno, retval, offset, region_len;
	struct region *old_region, *new_region;
	void *buffer;
	struct pt_entry *old_entry, *new_entry;

	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

	buffer = kmalloc(PAGE_SIZE);

	newas->heap_start = old->heap_start;
	newas->heap_end = old->heap_end;

	// Copy regions
	region_len = array_num(old->as_regions);
	for (i = 0; i < region_len; i++) {
		old_region = array_get(old->as_regions, i);
		new_region = kmalloc(sizeof(struct region));
		if (old_region == NULL || new_region == NULL) {
			goto err0;
		}

		new_region->base = old_region->base;
		new_region->size = old_region->size;
		new_region->permission = old_region->permission;

		errno = array_add(newas->as_regions, new_region, NULL);
		if (errno) {
			goto err0;
		}
	}

	// Copy page table
	for (i = 0; i < PT_LEVEL_SIZE; i++){
		if (old->pagetable[i] != NULL){
			newas->pagetable[i] = kmalloc(PT_LEVEL_SIZE * sizeof(struct pt_entry));
			memset(newas->pagetable[i], 0, PT_LEVEL_SIZE * sizeof(struct pt_entry));
			// For each page table entry
			// Lock the entire L2 pagetable
			lock_acquire(old->pt_locks[i]);
			for (j = 0; j < PT_LEVEL_SIZE; j++){
				// We're about to copy this page. Make sure it can't be written to
				vm_tlbflush((i << 22) & (j << 12));

				old_entry = &old->pagetable[i][j];
				new_entry = &newas->pagetable[i][j];

				// For every valid pagetable entry...
				if (old_entry->allocated){
					offset = bs_alloc_index();
					if (old_entry->in_memory){
						// Write it to disk if it's in memory
						retval = bs_write_page((void *) PADDR_TO_KVADDR(old_entry->p_addr), offset);
						KASSERT(retval == 0);					
					} else {
						// Or copy it to the new disk spot if not
						retval = bs_read_page(buffer, old_entry->store_index);
						KASSERT(retval == 0);

						retval = bs_write_page(buffer, offset);
						KASSERT(retval == 0);
					}
					// Update entry
					// TODO: this will be caught by coremap
					new_entry->p_addr = 0;
					new_entry->store_index = offset;
					new_entry->in_memory = false;
					new_entry->allocated = true;
				}
			}
			lock_release(old->pt_locks[i]);
		}
	}
	kfree(buffer);

	*ret = newas;

	return 0;
	
	err0:
	for (i=0; i < (int)region_len; i++){
		new_region = (struct region *)array_get(newas->as_regions,i);
		if (new_region != NULL)
			kfree(new_region);
	}
	return ENOMEM;
}

void
as_destroy(struct addrspace *as)
{
    /*
     * Clean up as needed.
     */
    unsigned len, i;
    pt_destroy(as, as->pagetable);
    struct region *region_ptr;

    for (i = 0; i < PT_LEVEL_SIZE; i++) {
    	if (as->pt_locks[i])
	    	lock_destroy(as->pt_locks[i]);
    }
    
    len = array_num(as->as_regions);
    // for (i = len - 1; i > 0; i--){
    //  region_ptr = array_get(as->as_regions, i);
    //  kfree(region_ptr);
    //  array_remove(as->as_regions, i);
    // }

    i = len - 1;
    while (len > 0){
        region_ptr = array_get(as->as_regions, i);
        kfree(region_ptr);
        array_remove(as->as_regions, i);
        if (i == 0)
            break;
        i--;
    }
    array_destroy(as->as_regions);
    kfree(as);
}

void
as_activate(void)
{
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}

	// Context switch happens, shootdown everything
	// ipi_tlbshootdown_allcpus(&(const struct tlbshootdown){vaddr, sem_create("Shootdown", 0)});
	vm_tlbflush_all();
}

void
as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
	 vm_tlbflush_all();
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{

	int err;
	struct region *region;

	// TODO: This is from dumbvm. How does this alignment work?
	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

	// Find the last region and put the heap tight next to it
	if (as->heap_start < vaddr + sz) {
		as->heap_start = vaddr + sz;
		as->heap_end = as->heap_start;
	}

	// TODO: Alighment and heap? How do we know sth is a heap?
	// Record region (to be used in vm_fault)
	region = kmalloc(sizeof(struct region));
	if (region == NULL)
		return ENOMEM;

	region->base = vaddr;
	region->size = sz;
	region->permission = readable + writeable + executable;

	cm_mem_change(-sz);
	
	err = array_add(as->as_regions, region, NULL);
	if (err)
		return err;

	return 0;
}

int
as_prepare_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	(void)as;
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	(void)as;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/*
	 * Write this.
	 */

	(void)as;

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;

	return 0;
}

int
as_check_region(struct addrspace *as, vaddr_t va)
{
	int i;
	struct region *region;
	int len = array_num(as->as_regions);

	for (i = 0; i < len; i++){
		region = array_get(as->as_regions, i);
		if (va >= region->base && va < (region->base + region->size)){
			return region->permission;
		}
	}
	// Can't find the addr in region, this is a segfault
	return -1;
}
