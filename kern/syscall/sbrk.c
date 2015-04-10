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
#include <coremap.h>

#define VM_STACKPAGES 18

int
sys_sbrk(int amount, int *retval) {
    struct addrspace *as = curproc->p_addrspace;
    kprintf("sbrk: amount = %d, free = %d\n", amount, cm_mem_free());

    *retval = as->heap_end;

    if (amount > (int) cm_mem_free()) {
        kprintf("sbrk: no memory\n");
        return ENOMEM;
    }

    if (as->heap_end + amount < as->heap_start) {
        kprintf("sbrk: negative heap size\n");
        return EINVAL;
    }

    kprintf("sbrk: new heap_end = %x, stack top = %x\n", as->heap_end + amount, USERSTACK - VM_STACKPAGES * PAGE_SIZE);
    if (as->heap_end + amount < USERSTACK - VM_STACKPAGES * PAGE_SIZE) {
        cm_mem_change(-amount);
        as->heap_end += amount;
        return 0;
    }

    panic("la de da de da");
}
