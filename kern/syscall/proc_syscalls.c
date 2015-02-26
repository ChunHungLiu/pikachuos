#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <syscall.h>
#include <test.h>

// for fork, we could create a new proc, and then thread fork into the new proc


int sys_fork(struct trapframe *tf, pid_t *retval) {
	(void)tf;
	(void)retval;
	return 0;
}
