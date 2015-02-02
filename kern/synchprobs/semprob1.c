/*
 * Copyright (c) 2001, 2002, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Driver code for binary semaphore problem 1.
 */
#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

#define NTHREADS    10
#define NHITS 20

/* Print lock so we can read the output. */
struct semaphore *print_lock;
int hit_count = 1;

/* Ping threads will only hit the ball on odd numbered hits. */
static
void
ping(void *p, unsigned long which)
{
	int n = 0;

	/* Add proper synchronization. */
	(void)p;

	P(print_lock);
	kprintf("Ping (%ld) starting\n", which);
	V(print_lock);

	/* We will do a fixed number of hits. */
	while (n < NHITS) {
		if (hit_count % 2 == 1) {
			n++;
			P(print_lock);
			kprintf("Ping! (%ld) hit number %d\n", which, n);
			V(print_lock);
			hit_count++;
		}
	}
}

/* Pong threads will only hit the ball on even numbered hits. */
static
void
pong(void *p, unsigned long which)
{
	int n = 0;

	/* Add proper synchronization. */
	(void)p;

	P(print_lock);
	kprintf("Pong (%ld) starting\n", which);
	V(print_lock);

	/* We will do a fixed number of hits. */
	while (n < NHITS) {
		if (hit_count % 2 == 0) {
			n++;
			P(print_lock);
			kprintf("Pong! (%ld) hit number %d\n", which, n);
			V(print_lock);
			hit_count++;
		}
	}
}

int
pingpong(int nargs, char **args)
{

	int err=0;
	unsigned long i;

	/* Avoids compiler warnings that this argument is not used. */
	(void)nargs;
	(void)args;

	/* Creating print_lock. */
	print_lock = sem_create("print lock", 1);

	/* Fork off NTHREADS/2 ping thread and one pong thread. */
	for (i = 0; i < NTHREADS / 2; i++ ) {
		err = thread_fork("Ping thread", NULL, ping, NULL, i);
		if (err != 0)
			panic("ping: thread_fork failed: %s)\n", strerror(err));

		err = thread_fork("Pong thread", NULL, pong, NULL, i);
		if (err != 0)
			panic("ping: thread_fork failed: %s)\n", strerror(err));
	}
	return 0;
}
