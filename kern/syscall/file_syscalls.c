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

int sys_read(int filehandle, void *buf, size_t size, int *retval)
{
	kprintf("Hello sys_read");
	(void)buf;
	(void)size;
	(void)filehandle;
	*retval = 0;
	return 0;
}

int sys_write(int filehandle, const void *buf, size_t size, int *retval)
{
	kprintf("Hello sys_write");
	(void)buf;
	(void)size;
	(void)filehandle;
	*retval = 0;
	return 0;
}

int sys_close(int filehandle) {
	struct file_obj *file = curproc->p_filetable->filetable_files[filehandle];

	KASSERT(file != NULL);

	bool destroy;

	lock_acquire(file->file_lock);
	KASSERT(file->file_refcount > 0);
	if (file->file_refcount > 1) {
		file->file_refcount--;
		destroy = false;
	}
	else {
		destroy = true;
	}
	lock_release(file->file_lock);

	if (destroy) {
		vfs_close(file->file_node);
		return 0;
	}

	return 0;
}