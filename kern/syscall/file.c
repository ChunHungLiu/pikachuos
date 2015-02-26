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

struct file_obj* file_open(char* filename, int flags, int mode) {
	struct vnode *vn;
	struct file_obj *file;
	vfs_open(filename, flags, mode, &vn);

	file = kmalloc(sizeof(struct file_obj));
	file->file_node = vn;
	file->file_lock = lock_create("file lock");
	file->pos = 0;
	file->file_refcount = flags & O_ACCMODE;
	file->file_mode = 0;
	return file;
}

int file_close(int fd) {
	(void) fd;
	return 0;
}

// init con: for all stds
int filetable_init() {
	// kprintf("filetable_init called");
	// TODO: sanity checks
	curproc->p_filetable = kmalloc(sizeof(struct filetable));
	for (int fd = 0; fd < OPEN_MAX; fd ++) {
		curproc->p_filetable->filetable_files[fd] = NULL;
	}
	curproc->p_filetable->filetable_lock = lock_create("filetable_lock");
	// open and init stdin stdout and stderr
	char *console = (char *)"con:";
	struct file_obj* stdin_ptr = file_open(console, O_RDONLY, 0);
	struct file_obj* stdout_ptr = file_open(console, O_WRONLY, 0);
	struct file_obj* stderr_ptr = file_open(console, O_WRONLY, 0);
	// TODO: sanity check for stdin/out/err fd
	filetable_add(stdin_ptr);
	filetable_add(stdout_ptr);
	filetable_add(stderr_ptr);
	return 0;
}

// returns the file descriptor 
int filetable_add(struct file_obj *file_ptr) {
	lock_acquire(curproc->p_filetable->filetable_lock);
	for (int fd = 0; fd < OPEN_MAX; fd ++) {
		if (curproc->p_filetable->filetable_files[fd] == NULL){
			curproc->p_filetable->filetable_files[fd] = file_ptr;
			lock_release(curproc->p_filetable->filetable_lock);
			return fd;
		}
	}
	lock_release(curproc->p_filetable->filetable_lock);
	return EMFILE;
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

int filetable_copy() {
	return 0;
}
