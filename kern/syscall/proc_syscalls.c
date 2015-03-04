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

	newproc->p_filetable = kmalloc(sizeof(struct filetable));
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

	lock_acquire(proc_table_lock);

	// orphan all children, should be done no matter what
	for (int i = 0; i < PID_MAX; i++) {
		// Found a children
		if (proc_table[i] != NULL && proc_table[i]->parent_pid == curproc->pid) {
			proc_table[i]->parent_pid = INVALID_PID;
			// weird case, parent doesn't wait, but exited later
			if (proc_table[i]->exited) {
				proc_destroy(proc_table[i]);
			}
		}
	}

	// set exitcode stuff is probably unnecessary for orphan
	if (curproc->parent_pid == INVALID_PID) {
		// Parent is dead, suicide
		proc_destroy(curproc);
	} else {
		// do fancy shit and don't destroy itself
		// it should be destroyed by parent in waitpid

		// TODO: This is probably not a good design
		curproc->exited = true;
		curproc->exitcode = exitcode;
		// Wake up parent
		cv_signal(curproc->waitpid_cv, proc_table_lock);
	}

	lock_release(proc_table_lock);
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
		copyout(&child->exitcode, returncode, sizeof(int));
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

int sys_execv(char* progname, char** args, int *retval) {
	int result;
	int *args_len;
	size_t len;
	size_t total_len = 0;
	char *temp;
	char **kern_args;
	int nargs = 0;
	userptr_t *usr_argv = NULL;
	char *kern_progname = kmalloc(PATH_MAX);
	args_len = kmalloc(sizeof(int) * 1024);
	temp = kmalloc(sizeof(char) * ARG_MAX);

	if (args == NULL) {
		*retval = -1;
        return EFAULT;
    }

   	result = copyinstr((userptr_t)args, temp, ARG_MAX, &len);
   	if (result) {
		*retval = -1;
   		return result;
   	}

	// Figure out nargs, and the length for each arg string
	while(args[nargs] != NULL) {
		result = copyinstr((userptr_t)args[nargs], temp, ARG_MAX, &len);
		if (result) {
			*retval = -1;
			return result;
		}
		args_len[nargs] = len;
		total_len += len;
        nargs += 1;
    }
    kfree(temp);
    
    if (total_len > ARG_MAX) {
		*retval = -1;
    	return E2BIG;
    }
    
    kern_args = kmalloc(sizeof(char*) * nargs);

	// Go through args and copy everything over to kern_args using copyinstr
	for (int i = 0; i < nargs; i++) {
		// This is causing issue
		kern_args[i] = kmalloc(sizeof(char) * args_len[i]);
		if (kern_args[i] == NULL) {
			*retval = -1;
			return ENOMEM;
		}
        copyinstr((userptr_t)args[i], kern_args[i], ARG_MAX, NULL);
	}

	// This is from runprogram 
	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;

	result = copyinstr((userptr_t)progname, kern_progname, PATH_MAX, NULL);
	if (*kern_progname == 0) {
		*retval = -1;
		return ENOENT;
	}
	if (result) {
		kfree(kern_progname);
		*retval = -1;
		return result;
	}

	/* Open the file. */
	result = vfs_open(kern_progname, O_RDONLY, 0, &v);
	if (result) {
		*retval = -1;
		return result;
	}

	// Blow up the current addrspace
	as_destroy(curproc->p_addrspace);
    curproc->p_addrspace = NULL;

	/* We should be a new process. */
	KASSERT(proc_getas() == NULL);

	/* Create a new address space. */
	as = as_create();
	if (as == NULL) {
		vfs_close(v);
		*retval = -1;
		return ENOMEM;
	}

	/* Switch to it and activate it. */
	proc_setas(as);
	as_activate();

	/* Intialize file table, not needed for execv */
	// filetable_init();

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		vfs_close(v);
		*retval = -1;
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		*retval = -1;
		return result;
	}

	// Clear space for arg pointers +1 for NULL terminator
    stackptr -= (sizeof(char*)) * (nargs + 1);

    // Convenience method for indexing
    usr_argv = (userptr_t*)stackptr;

    for(int i = 0; i < nargs; i++ ) {
    	// Clear out space for an arg string
        stackptr -= sizeof(char) * (strlen(kern_args[i]) + 1);
        // Assign the string's pointer to usr_argv
        usr_argv[i] = (userptr_t)stackptr;
        // Copy over string
        copyout(kern_args[i], usr_argv[i],
        	sizeof(char) * (strlen(kern_args[i]) + 1));
    }

    // NULL terminate usr_argv
    usr_argv[nargs] = NULL;

    // Free memory
    for(int i = 0; i < nargs; i++) {
        kfree(kern_args[i]);
    }
    kfree(kern_args);

	/* Warp to user mode. */
	enter_new_process(nargs /*argc*/, (userptr_t)usr_argv /*userspace addr of argv*/,
			  NULL /*userspace addr of environment*/,
			  stackptr, entrypoint);

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
}
