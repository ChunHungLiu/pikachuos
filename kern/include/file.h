#include <types.h>
#include <limits.h>
#include <synch.h>
#include <uio.h>

#ifndef _FILE_H_
#define _FILE_H_

// everything should operate under curthread, we don't pass in any filetable pointer
struct file_obj{
	struct vnode *file_node;
	struct lock *file_lock;
	off_t pos;
	int file_refcount;
	int file_mode;
};

struct file_obj* file_obj_create(struct vnode *vn, int flags);
int console_open(int flags, int *retval);

struct filetable
{
	// Arbitrary number for now
	struct file_obj *filetable_files[OPEN_MAX];
	struct lock *filetable_lock;
};

// init con: for all stds
int filetable_init(void);
int filetable_add(struct file_obj *file_ptr, int *retval);
int filetable_remove(int fd);
int filetable_destroy(void);
int filetable_get(struct filetable *filetable, int fd, struct file_obj** retval);
int filetable_put(struct filetable *filetable, int fd, struct file_obj* file);
// make a shallow copy for the filetable. refcounts should be increased
int filetable_copy(struct filetable *dest_ft);

/* set up a uio with the buffer, its size, and the current offset */
void uio_uinit(struct iovec *iov, struct uio *useruio, void *buf, size_t buflen, off_t offset, enum uio_rw uio_rw);

#endif