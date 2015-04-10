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

#define VM_STACKPAGES 18


// TODO: sbrk code goes here
int
sys_sbrk(int amount, int *retval) {
	(void) amount;
	(void) retval;
	struct addrspace *as = curproc->p_addrspace;

    if (amount <= 0) {
    	*retval = as->heap_end;
    	return 0;
    }

    if (as->heap_end + amount < USERSTACK - VM_STACKPAGES * PAGE_SIZE) {
        *retval = as->heap_end;
        as->heap_end += amount;
        return 0;
    }

    return ENOMEM;
}
