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
#include <copyinout.h>

/*
 * Open:
 */
int sys_open(const char* user_filename, int flags, int mode, int *retval) {
	kprintf("Opening %s with flags %x\n", user_filename, flags);

	struct vnode *vn;
	struct file_obj *file;
	char* filename;
	int err = 0;
	int fd;

	// TODO: Error checking

	// Copy file name - vfs_* may change name
	// TODO: Should this use copyinstr? 
	int len = strlen(user_filename);
	filename = kmalloc(len + 1);
	if (!filename)
		return ENOMEM;
	strcpy(filename, user_filename);

	// Open file
	err = vfs_open(filename, flags, mode, &vn);
	kfree(filename);
	if (err)
		return err;

	// Create file_obj
	file = file_obj_create(vn, flags);
	if (!file)
		return ENOMEM;

	// Add to the proc's filetable
	err = filetable_add(file, &fd);
	if (err)
		return err;
	*retval = fd;
	return 0;
}

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

	// Decrease refcounts, and close if 0
	lock_acquire(file->file_lock);
	KASSERT(file->file_refcount > 0);
	if (file->file_refcount > 1) {
		file->file_refcount--;
	} else {
		vfs_close(file->file_node);
		return 0;
	}
	lock_release(file->file_lock);

	return 0;
}