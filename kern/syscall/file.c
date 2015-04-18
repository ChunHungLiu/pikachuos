
#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <current.h>
#include <vm.h>
#include <vfs.h>
#include <syscall.h>
#include <test.h>
#include <proc.h>
#include <synch.h>
#include <uio.h>

/**
 * Allocates and initializes a file_obj to point to the supplied vnode, and
 * with the correct flags (which are assumed to be valid for the vnode)
 * 
 * Returns NULL on failure (out of memory)
 */
struct file_obj* file_obj_create(struct vnode *vn, int flags) {
	struct file_obj *file;

	// Allocate & check
	file = kmalloc(sizeof(struct file_obj));
	if (!file)
		return NULL;

	// Initial values
	file->file_node = vn;
	file->file_lock = lock_create("file lock");
	file->pos = 0;
	file->file_refcount = 1;
	file->file_mode = flags & O_ACCMODE;

	// No lock? Assume we're out of memory.
	if (!file->file_lock) {
		kfree(file);
		return NULL;
	}

	return file;	
}

int console_open(int flags, int *retval) {
	struct vnode *vn;
	struct file_obj *file;
	char* filename;
	int err = 0;
	int fd;
	filename = kmalloc(5);
	if (!filename)
		return ENOMEM;
	strcpy(filename, "con:");

	// Open file
	err = vfs_open(filename, flags, 0, &vn);
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

// init con: for all stds
int filetable_init() {
	int err = 0;

	// kprintf("filetable_init called");
	curproc->p_filetable = kmalloc(sizeof(struct filetable));
	for (int fd = 0; fd < OPEN_MAX; fd ++) {
		curproc->p_filetable->filetable_files[fd] = NULL;
	}
	curproc->p_filetable->filetable_lock = lock_create("filetable_lock");
	// open and init stdin stdout and stderr
	int stdin, stdout, stderr;
	// We should do something with error values
	err = console_open(O_RDONLY, &stdin);
	err = console_open(O_WRONLY, &stdout);
	err = console_open(O_WRONLY, &stderr);
	// Sanity check for stdin/out/err fd.
	// There shouldn't be any other open file descriptors when this is called
	KASSERT(stdin == 0);
	KASSERT(stdout == 1);
	KASSERT(stderr == 2);
	return err;
}

/**
 * Adds the given file object to the process's file table, using the first
 * empty slot available.
 * 
 * @param	file_ptr	file object to add
 * @param	retval	pointer to the int to store the file descriptor
 * @return	Error value, if any
 */
int filetable_add(struct file_obj *file_ptr, int *retval) {
	lock_acquire(curproc->p_filetable->filetable_lock);
	// Find the first unused file descriptor
	for (int fd = 0; fd < OPEN_MAX; fd ++) {
		if (curproc->p_filetable->filetable_files[fd] == NULL){
			curproc->p_filetable->filetable_files[fd] = file_ptr;
			lock_release(curproc->p_filetable->filetable_lock);
			*retval = fd;
			return 0;
		}
	}
	lock_release(curproc->p_filetable->filetable_lock);

	// Error if no file descriptors are available
	return EMFILE;
}

/* copies ft of curproc to dest_fd
 * this is a shallow copy, we just increment the refcount
 *
*/
// TODO: the logic here is kinda weird... how deos it play out with init?
// TODO: should we model the interface on as_copy?
int filetable_copy(struct filetable *dest_ft) {
	struct filetable *curft = curproc->p_filetable;
	
	for (int fd = 0; fd < OPEN_MAX; fd ++) {
		if (curft->filetable_files[fd] != NULL){
			lock_acquire(curft->filetable_files[fd]->file_lock);
			curft->filetable_files[fd]->file_refcount++;
			lock_release(curft->filetable_files[fd]->file_lock);
			dest_ft->filetable_files[fd] = curft->filetable_files[fd];
		} else {
			dest_ft->filetable_files[fd] = NULL;
		}
	}
	return 0;
}

int filetable_remove(int fd) {
	// TODO: better sync?
	// TODO: sanity check
	lock_acquire(curproc->p_filetable->filetable_lock);
	
	struct file_obj *file_ptr = curproc->p_filetable->filetable_files[fd];
	vfs_close(file_ptr->file_node);
	lock_destroy(file_ptr->file_lock);
	kfree(file_ptr);
	curproc->p_filetable->filetable_files[fd] = NULL;
	lock_release(curproc->p_filetable->filetable_lock);
	return 0;
}

int filetable_destroy() {
	for (int fd = 0; fd < OPEN_MAX; fd ++) {
		if (curproc->p_filetable->filetable_files[fd] != NULL){
			filetable_remove(fd);
		}
	}
	kfree(curproc->p_filetable);
	return 0;
}

int filetable_get(struct filetable *filetable, int fd, struct file_obj** retval) {
	if (fd < 0 || fd > OPEN_MAX)
		return EBADF;
	lock_acquire(filetable->filetable_lock);
	*retval = filetable->filetable_files[fd];
	lock_release(filetable->filetable_lock);
	if (retval == NULL)
		return EBADF;
	return 0;
}

int filetable_put(struct filetable *filetable, int fd, struct file_obj* file) {
	if (fd < 0 || fd > OPEN_MAX)
		return EBADF;
	lock_acquire(filetable->filetable_lock);
	filetable->filetable_files[fd] = file;
	lock_release(filetable->filetable_lock);
	return 0;
}

void uio_uinit(struct iovec *iov, struct uio *useruio, void *buf, size_t buflen, off_t offset, enum uio_rw uio_rw) {
	iov->iov_ubase = buf;
	iov->iov_len = buflen;
	useruio->uio_iov = iov;
	useruio->uio_iovcnt = 1;
	useruio->uio_offset = offset;
	useruio->uio_resid = buflen;
	useruio->uio_segflg = UIO_USERSPACE;
	useruio->uio_rw = uio_rw;
	useruio->uio_space = proc_getas();
}