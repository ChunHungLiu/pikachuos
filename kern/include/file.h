#include <types.h>
#include <limits.h>
#include <synch.h>

// everything should operate under curthread, we don't pass in any filetable pointer
struct file_obj{
	struct vnode *file_node;
	struct lock *file_lock;
	off_t pos;
	int file_refcount;
	int file_mode;
};

struct file_obj* file_open(char* filename, int flags, int mode);
int file_close(int fd);

struct filetable
{
	// Arbitrary number for now
	struct file_obj *filetable_files[OPEN_MAX];
	struct lock *filetable_lock;
};

// init con: for all stds
int filetable_init(void);
int filetable_add(struct file_obj *file_ptr);
int filetable_remove(int fd);
int filetable_destroy(void);
// make a shallow copy for the filetable. refcounts should be increased
int filetable_copy(struct filetable *dest_fd);
