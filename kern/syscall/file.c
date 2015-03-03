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

	// No lock? Assume we're out of memory. TODO: what dbelse could happen?
	if (!file->file_lock) {
		kfree(file);
		return NULL;
	}

	return file;	
}

// init con: for all stds
int filetable_init() {
	int err = 0;

	// kprintf("filetable_init called");
	// TODO: sanity checks
	curproc->p_filetable = kmalloc(sizeof(struct filetable));
	for (int fd = 0; fd < OPEN_MAX; fd ++) {
		curproc->p_filetable->filetable_files[fd] = NULL;
	}
	curproc->p_filetable->filetable_lock = lock_create("filetable_lock");
	// open and init stdin stdout and stderr
	const char *console = "con:";
	int stdin, stdout, stderr;
	// We should do something with error values
	err = sys_open(console, O_RDONLY, 0, &stdin);
	err = sys_open(console, O_WRONLY, 0, &stdout);
	err = sys_open(console, O_WRONLY, 0, &stderr);
	// Sanity check for stdin/out/err fd. TODO: More checks?
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
	(void) curft;
	(void) dest_ft;
	// struct filetable *new_ft; 
	// dest_ft = kmalloc(sizeof(struct filetable));
	// // Not sure if this a good idea or not
	// dest_ft->filetable_lock = lock_create("filetable_lock");

	// for (int fd = 0; fd < OPEN_MAX; fd ++) {
	// 	if (curft->filetable_files[fd] != NULL){
	// 		lock_acquire(curft->filetable_files[fd]->file_lock);
	// 		curft->filetable_files[fd]->file_refcount++;
	// 		lock_release(curft->filetable_files[fd]->file_lock);
	// 		dest_ft->filetable_files[fd] = curft->filetable_files[fd];
	// 	} else {
	// 		dest_ft->filetable_files[fd] = NULL;
	// 	}
	// }
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
