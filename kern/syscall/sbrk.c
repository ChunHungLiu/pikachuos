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

//#define SBRK_DEBUG(message...) kprintf("sbrk: ");kprintf(message);
#define SBRK_DEBUG(message...) ;
#define SBRK_DONE (void)0;

int
sys_sbrk(int amount, int *retval) {
    struct addrspace *as = curproc->p_addrspace;
    SBRK_DEBUG("amount = %d, free = %d\n", amount, cm_mem_free());

    *retval = as->heap_end;

    if (amount > (int) cm_mem_free()) {
        SBRK_DEBUG("no memory\n");
        return ENOMEM;
    }

    if (as->heap_end + amount < as->heap_start) {
        SBRK_DEBUG("negative heap size\n");
        return EINVAL;
    }

    SBRK_DEBUG("new heap_end = %x, stack top = %x\n", as->heap_end + amount, USERSTACK - VM_STACKPAGES * PAGE_SIZE);
    if (as->heap_end + amount < USERSTACK - VM_STACKPAGES * PAGE_SIZE) {
        cm_mem_change(-amount);
        as->heap_end += amount;
        return 0;
    }

    panic("sbrk not handled");
}
