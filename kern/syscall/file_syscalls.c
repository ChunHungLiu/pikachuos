#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/seek.h>
#include <lib.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <syscall.h>
#include <test.h>
#include <copyinout.h>
#include <uio.h>
#include <vnode.h>

/*
 * Open:
 */
int sys_open(const char* user_filename, int flags, int mode, int *retval) {
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
	struct file_obj **ft = curproc->p_filetable->filetable_files;
	struct file_obj *file;
	int bytes_read = 0;

	if (filehandle < 0 || filehandle > OPEN_MAX || ft[filehandle] == NULL)
		return EBADF;
	file = ft[filehandle];

	lock_acquire(file->file_lock);

	// Get info
	if (file->file_mode != O_RDONLY && file->file_mode != O_RDWR) {
		return EPERM;
	}

	// Create uio object for handing reading
	struct uio read_uio;
	struct iovec iov;

	iov.iov_ubase = buf;
	iov.iov_len = size;
	read_uio.uio_iov = &iov;
	read_uio.uio_iovcnt = 1;
	read_uio.uio_offset = file->pos;
	read_uio.uio_resid = size;
	read_uio.uio_segflg = UIO_USERSPACE;
	read_uio.uio_rw = UIO_READ;
	read_uio.uio_space = proc_getas();

	*retval = VOP_READ(file->file_node, &read_uio);

	bytes_read = size - read_uio.uio_resid;
	file->pos += bytes_read;

	lock_release(file->file_lock);

	return bytes_read;
}

int sys_write(int filehandle, const void *buf, size_t size, int *retval)
{
	struct file_obj **ft = curproc->p_filetable->filetable_files;
	struct file_obj *file;
	int bytes_written;

	//lock_acquire(curproc->p_filetable->filetable_lock);

	if (filehandle < 0 || filehandle > OPEN_MAX || ft[filehandle] == NULL)
		return EBADF;
	file = ft[filehandle];

	//lock_release(curproc->p_filetable->filetable_lock);

	lock_acquire(file->file_lock);

	// Get info
	if (file->file_mode != O_WRONLY && file->file_mode != O_RDWR) {
		return EPERM;
	}

	// Create uio object for handling reading
	struct uio write_uio;
	struct iovec iov;

	iov.iov_ubase = (void *) buf;
	iov.iov_len = size;
	write_uio.uio_iov = &iov;
	write_uio.uio_iovcnt = 1;
	write_uio.uio_offset = file->pos;
	write_uio.uio_resid = size;
	write_uio.uio_segflg = UIO_USERSPACE;
	write_uio.uio_rw = UIO_WRITE;
	write_uio.uio_space = proc_getas();

	*retval = VOP_WRITE(file->file_node, &write_uio);
	bytes_written = size - write_uio.uio_resid;
	file->pos += bytes_written;

	lock_release(file->file_lock);

	return bytes_written;
}

off_t sys_lseek(int fd, off_t pos, int whence) {
	struct file_obj **ft = curproc->p_filetable->filetable_files;
	struct file_obj *file;
	off_t new_pos;

	if (fd < 0 || fd > OPEN_MAX || ft[fd] == NULL)
		return EBADF;
	file = ft[fd];

	lock_acquire(file->file_lock);

	if (!VOP_ISSEEKABLE(file->file_node))
		return ESPIPE;

	switch(whence) {
		case SEEK_SET:
			file->pos = pos;
			break;
		case SEEK_CUR:
			file->pos += pos;
			break;
		case SEEK_END:
			panic("SEEK_END: Unimplemememented");
			break;
		default:
			return EINVAL;
	}

	new_pos = file->pos;

	lock_release(file->file_lock);

	return new_pos;

}

int sys_dup2(int oldfd, int newfd) {
	(void) oldfd;
	(void) newfd;

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