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
#include <stat.h>
#include <endian.h>

/**
 * Opens the file/device/kernel object named by the pathname 'filename'. 'flags' specifies how to open the file.
 * @param  user_filename path to the file to be opened
 * @param  flags         flags for how to open the file
 * @param  mode          currently unused
 * @param  retval        pointer to store the file descriptor for the new file
 * @return               0 if successful, otherwise the error
 */
int sys_open(const char* user_filename, int flags, int mode, int *retval) {
	struct vnode *vn;
	struct file_obj *file;
	char* filename;
	int err = 0;
	int fd;
	int result;
	char *test = kmalloc(PATH_MAX);

	// TODO: Error checking

	// Copy file name - vfs_* may change name
	// TODO: Should this use copyinstr? 
	if (user_filename == NULL) {
		return EFAULT;
	}
	result = copyin((userptr_t)user_filename, test, PATH_MAX);
	if (result) {
		*retval = 0;
		return result;
	}
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


/**
 * Reads 'size' bytes from the file represented by 'filehandle' to 'buf'.
 * Stores the number of bytes read in 'retval'
 * @param  filehandle file descriptor to read from
 * @param  buf        buffer to read to
 * @param  size       number of bytes to read
 * @param  retval     pointer to the place to store the number of bytes read
 * @return            0 if successful, otherwise the error code
 */
int sys_read(int filehandle, void *buf, size_t size, int *retval)
{
	struct file_obj **ft = curproc->p_filetable->filetable_files;
	struct file_obj *file;
	int err = 0;

	if (filehandle < 0 || filehandle >= OPEN_MAX || ft[filehandle] == NULL)
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

	err = VOP_READ(file->file_node, &read_uio);

	// Still update stuff in case we manage to write some of the stuff before
	// the error
	*retval = size - read_uio.uio_resid;
	file->pos += *retval;

	lock_release(file->file_lock);

	return err;
}

/**
 * Writes 'size' bytes from 'buf' to the file represented by 'filehandle'.
 * Stores the number of bytes written in 'retval'
 * @param  filehandle file descriptor to write to
 * @param  buf        buffer to write from
 * @param  size       number of bytes to write
 * @param  retval     pointer to the place to store the number of bytes written
 * @return            0 if successful, otherwise the error code
 */
int sys_write(int filehandle, const void *buf, size_t size, int *retval)
{
	struct file_obj **ft = curproc->p_filetable->filetable_files;
	struct file_obj *file;
	int err = 0;

	//lock_acquire(curproc->p_filetable->filetable_lock);

	if (filehandle < 0 || filehandle >= OPEN_MAX || ft[filehandle] == NULL)
		return EBADF;
	file = ft[filehandle];
	KASSERT(file->file_lock != (void *) 0xdeadbeef);

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

	err = VOP_WRITE(file->file_node, &write_uio);

	// Still update stuff in case we manage to write some of the stuff before
	// the error
	*retval = size - write_uio.uio_resid;
	file->pos += *retval;

	lock_release(file->file_lock);

	return err;
}

/**
 * Wrapper for lseek when only 32-bit values can be passed in (e.g. in 
 * syscall.c) and sent back
 * @param  fd      file descriptor to seek
 * @param  pos1    upper (lower) 32 bits of the offset
 * @param  pos2    lower (upper) 32 bits of the offset
 * @param  whence  position to seek from
 * @param  retval  pointer to the place to store the new position (upper 32 bits)
 * @param  retval2 pointer to the place to store the new position (lower 32 bits)
 * @return         0 if successful, otherwise the error code
 */
int sys_lseek_32(int fd, int pos1, int pos2, int whence, uint32_t *retval, uint32_t *retval2) {
	uint64_t pos;
	off_t ret;
	join32to64(pos1, pos2, &pos);
	int err;
	err = sys_lseek(fd, pos, whence, &ret);
	split64to32(ret, retval, retval2);
	return err;
}


/**
 * Seek 'fd' to the position specified by 'pos' from position specified by
 * 'whence'
 * @param  fd     file descriptor to seek
 * @param  pos    number of bytes to shift the position
 * @param  whence position to seek from
 * @param  retval pointer to the place to store the new position
 * @return        0 if successful, otherwise the error code
 */
int sys_lseek(int fd, off_t pos, int whence, off_t *retval) {
	struct file_obj **ft = curproc->p_filetable->filetable_files;
	struct file_obj *file;
	struct stat f_stat;

	if (fd < 0 || fd >= OPEN_MAX || ft[fd] == NULL)
		return EBADF;
	file = ft[fd];

	lock_acquire(file->file_lock);

	if (!VOP_ISSEEKABLE(file->file_node)) {
		lock_release(file->file_lock);
		return ESPIPE;
	}

	switch(whence) {
		case SEEK_SET:
			file->pos = pos;
			break;
		case SEEK_CUR:
			file->pos += pos;
			break;
		case SEEK_END:
			// Get file stats
			VOP_STAT(file->file_node, &f_stat);

			file->pos = f_stat.st_size + pos;
			break;
		default:
			lock_release(file->file_lock);
			return EINVAL;
	}

	if (file->pos < 0) {
		lock_release(file->file_lock);
		return EINVAL;
	}

	*retval = file->pos;

	lock_release(file->file_lock);

	return 0;
}

/**
 * Clones 'oldfd' onto 'newfd'. If 'newfd' is already open file, that file is
 * closed. Stores return value in retval
 * @param  oldfd file descriptor that is to be cloned (i.e. 'newfd' will refer
 *               to the same file as oldfd after this is done)
 * @param  newfd file descriptor pointing to the file that will be replaced
 *               by the file for 'oldfd'
 * @param retval pointer to the place to store the return value (either newfd
 *               or -1)
 * @return       0 if successful, otherwise the error code
 */
int sys_dup2(int oldfd, int newfd, int *retval) {
	(void) oldfd;
	(void) newfd;

	struct file_obj **ft = curproc->p_filetable->filetable_files;
	struct file_obj *old_file;

	// Check that all file descriptors are in range and the old one is active
	if (oldfd < 0 || oldfd >= OPEN_MAX || ft[oldfd] == NULL ||
		newfd < 0 || newfd >= OPEN_MAX) {
		*retval = -1;
		return EBADF;
	}
	old_file = ft[oldfd];

	if (ft[newfd] != NULL)
		sys_close(newfd, NULL);

	lock_acquire(curproc->p_filetable->filetable_lock);

	ft[newfd] = old_file;

	lock_release(curproc->p_filetable->filetable_lock);

	*retval = newfd;
	return 0;
}


/**
 * Closes the file specified by 'filehandle'
 * @param  filehandle file descriptor for the file to be closed
 * @param  retval     pointer to the location to store the return value
 * @return            should always return 0
 */
int sys_close(int filehandle, int *retval) {
	struct file_obj *file = curproc->p_filetable->filetable_files[filehandle];

	KASSERT(file != NULL);

	// Decrease refcounts, and close & remove if 0
	lock_acquire(file->file_lock);
	KASSERT(file->file_refcount > 0);
	if (file->file_refcount > 1) {
		file->file_refcount--;
		lock_release(file->file_lock);
	} else {
		lock_release(file->file_lock);
		filetable_remove(filehandle);
	}

	if (retval)		// Handle retval = NULL
		*retval = 0;
	return 0;
}

/**
 * Sets the process' current working directory to the path specified by
 * pathname, if it exists
 * @param  pathname path to change to 
 * @param  retval   pointer to the int to store the return value
 * @return          0 if successful, otherwise the error code
 */
int sys_chdir(const char *pathname, int *retval) {
	unsigned int path_len = 0;
	unsigned int ret = 0;
	char* kpath;
	int err = 0;

	// Copy the pathname into the kernel
	path_len = strlen(pathname);
	kpath = kmalloc(path_len + 1);
	if (kpath == NULL) {
		*retval = -1;
		return ENOMEM;
	}
	copyinstr((const_userptr_t)pathname, kpath, path_len, &ret);
	KASSERT(ret == path_len);
	if (ret != path_len) {
		kfree(kpath);
		*retval = -1;
		return ENAMETOOLONG;
	}

	// Do the chdir
	err = vfs_chdir(kpath);
	kfree(kpath);
	if (err) {
		*retval = -1;
		return err;
	}

	*retval = 0;
	return 0;
}

/**
 * Stores up to 'buflen-1' characters of the current working directory into
 * 'buf', returning the number of bytes stored
 * @param  buf    buffer to store the current working directory
 * @param  buflen max length of 'buf'
 * @param  retval pointer to store the result
 * @return        0 if successful, otherwise the error
 */
int sys___getcwd(char *buf, size_t buflen, int *retval) {
	int err = 0;

	// Create uio to copy path from kernel to 'buf'
	struct uio cwd_uio;
	struct iovec iov;

	iov.iov_ubase = (void *) buf;
	iov.iov_len = buflen;
	cwd_uio.uio_iov = &iov;
	cwd_uio.uio_iovcnt = 1;
	cwd_uio.uio_offset = 0;
	cwd_uio.uio_resid = buflen;
	cwd_uio.uio_segflg = UIO_USERSPACE;	// User process data
	cwd_uio.uio_rw = UIO_READ;			// From kernel to uio_seg
	cwd_uio.uio_space = proc_getas();

	err = vfs_getcwd(&cwd_uio);
	if (err) {
		*retval = -1;
		return err;
	}

	// uio_offset will hold the number of bytes read
	*retval = cwd_uio.uio_offset;
	return 0;
}