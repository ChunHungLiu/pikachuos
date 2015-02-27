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
#include <vnode.h>

// TODO: this is getting pretty sketchy
static void run_forked_proc(void *tf, unsigned long junk) 
{
	(void) junk;

	// TODO: any catch with tf?
	enter_forked_process((struct trapframe *)tf);
}

int sys_fork(struct trapframe *tf, pid_t *retval) {
	int result;
	char* name;
	(void) result;
	(void) tf;
	(void) retval;

	struct proc *newproc = proc_create("forked_process");

	// TODO: concurrency issue?
	as_copy(curproc->p_addrspace, &newproc->p_addrspace);

	filetable_copy(newproc->p_filetable);
	// copied from proc.c init p_cwd
	spinlock_acquire(&curproc->p_lock);
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		newproc->p_cwd = curproc->p_cwd;
	}
	spinlock_release(&curproc->p_lock);

	newproc->parent_pid = curproc->pid;

	name = kstrdup(curproc->p_name);

	thread_fork(name, newproc ,run_forked_proc, (void *)tf, 0);

	return 0;
}
