#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/wait.h>
#include <lib.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <syscall.h>
#include <test.h>
#include <vnode.h>
#include <copyinout.h>


// TODO: sbrk code goes here
/*void *
sys_sbrk(intptr_t amount) {
	int i;
	struct addrspace *as = curproc->p_addrspace;
	if (amount <= 0) {
		return(current_break);
	}

	if (heap > max_heap || heap overlaps stack) {
		return(-1);
	}

	for (i = 0; i < as->num_regions; i++) {
		if (as->region.start == heap_start) {
			as_heap = region;
		}
	}
	as_heap->amount += amount
	return(as_heap->amount - amount)
}*/