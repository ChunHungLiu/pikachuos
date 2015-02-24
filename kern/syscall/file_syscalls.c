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
