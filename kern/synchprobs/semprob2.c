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
 * Driver code for counting semaphore problem 2.
 */
#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

#define N_TFS 4
#define N_STUDENTS 10
#define N_QUESTIONS 5

int tf_status[N_TFS];

/* Using a semaphore so that you can see the output easily. */
struct semaphore *print_lock;

/* 
 * The student thread should try to obtain a TF resource, print a message
 * indicating that it has a TF and then release the TF resource and try
 * again (up to N_QUESTIONS times)..
 */
static
void
student(void *p, unsigned long which)
{
	int i, n = 0;

	/* Add proper synchronization. */
	(void)p;

	P(print_lock);
	kprintf("Student %ld starting\n", which);
	V(print_lock);

	/* We will ask a fixed number of questions. */
	while (n < N_QUESTIONS) {
        
		/* Acquire a TF. */
		for (i = 0; i < N_TFS; i++) {
			if (tf_status[i] == 0) {
				tf_status[i] = 1;
				break;
			}
		}

		/* What does it mean if i is not < N_TFS? */
		if (i < N_TFS) {
			n++;
			P(print_lock);
			kprintf("Student %ld asking TF %d a question\n", which, i);
			V(print_lock);

			thread_yield();

			/* Now release the TF. */
			P(print_lock);
			kprintf("Student %ld done with TF %d\n", which, i);
			V(print_lock);

			/* In theory; should protect this in practice, it's atomic */
			tf_status[i] = 0;

			thread_yield();
		}
	}
}


int
tfs(int nargs, char **args)
{

	int err=0;
	unsigned long s;

	print_lock = sem_create("print", 1);

	/* Avoids compiler warnings that this argument is not used. */
	(void)nargs;
	(void)args;

	kprintf("Starting up TF and student problem\n");
	/* Initialize all the TFs: 0 is available; 1 is taken. */
	for (s = 0; s < N_TFS; s++)
		tf_status[s] = 0;

	/* Fork off N_STUDENTS threads. */
	for (s = 0; s < N_STUDENTS; s++) {
		err = thread_fork("Student thread", NULL, student, NULL, s);
		if (err != 0)
			panic("student: thread_fork failed: %s)\n", strerror(err));
	}

	return 0;
}
