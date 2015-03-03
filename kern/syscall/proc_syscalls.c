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
#include <copyinout.h>

// TODO: this is getting pretty sketchy
static void run_forked_proc(void *tf, unsigned long junk) 
{
	(void) junk;
	struct trapframe kern_tf;

	kern_tf = *(struct trapframe *)tf;

	// TODO: any sync issue with tf?
	kfree(tf);
	enter_forked_process(&kern_tf);
}

int sys_fork(struct trapframe *tf, pid_t *retval) {
	int result;
	char* name;
	(void) result;
	(void) tf;
	(void) retval;
	struct trapframe *temp_tf = kmalloc(sizeof(*temp_tf));
	*temp_tf = *tf;

	struct proc *newproc = proc_create("forked_process");

	// TODO: concurrency issue?
	as_copy(curproc->p_addrspace, &newproc->p_addrspace);

	newproc->p_filetable = kmalloc(sizeof(newproc->p_filetable));
	newproc->p_filetable->filetable_lock = lock_create("filetable_lock");

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

	*retval = newproc->pid;

	thread_fork(name, newproc ,run_forked_proc, (void *)temp_tf, 0);

	return 0;
}

void sys__exit(int exitcode) {
	(void) exitcode;
	// lock_acquire(proc_table_lock);

	// // orphan all children, should be done no matter what
	// for (int i = 0; i < PID_MAX; i++) {
	// 	// Found a children
	// 	if (proc_table[i] != NULL && proc_table[i]->parent_pid == curproc->pid) {
	// 		proc_table[i]->parent_pid = INVALID_PID;
	// 		// weird case, parent doesn't wait, but exited later
	// 		if (proc_table[i]->exited) {
	// 			proc_destroy(proc_table[i]);
	// 		}
	// 	}
	// }

	// // set exitcode stuff is probably unnecessary for orphan
	// if (curproc->parent_pid == INVALID_PID) {
	// 	// Parent is dead, suicide
	// 	proc_destroy(curproc);
	// } else {
	// 	// do fancy shit and don't destroy itself
	// 	// it should be destroyed by parent in waitpid

	// 	// TODO: This is probably not a good design
	// 	curproc->exited = true;
	// 	curproc->exitcode = exitcode;
	// 	// Wake up parent
	// 	cv_signal(curproc->waitpid_cv, proc_table_lock);
	// }

	// lock_release(proc_table_lock);
	// kfree(curproc);
	thread_exit();
}

int sys_waitpid(pid_t pid, userptr_t returncode, int flags, pid_t *retval) {
	(void) pid;
	(void) returncode;
	(void) flags;
	(void) retval;

	// TODO: a lot of sanity checks for this one.
	
	// TODO: what's the _MKWAIT_EXIT stuff?
	lock_acquire(proc_table_lock);
	struct proc *child = proc_table[pid];

	if (!child->exited) {
		// We always wait on the child's cv
		cv_wait(child->waitpid_cv, proc_table_lock);
		KASSERT(child->exited);
	}

	if (returncode != NULL) {
		// Give back returncode
		copyout(&child->exitcode,returncode, sizeof(int));
	}
	*retval = pid;
	proc_destroy(child);
	lock_release(proc_table_lock);
	return 0;
}

int sys_getpid(pid_t *retval) {
	*retval = curproc->pid;
	return 0;
}