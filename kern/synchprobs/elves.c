/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
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
 * SYNCHRONIZATION PROBLEM 1: KEEBLER ELVES
 *
 * The Keebler Cookie Factory is staffed by one supervisor and many
 * elves. Each elf must complete some series of tasks before it can
 * leave for the day (implemented in the `work` function). Whenever
 * an elf completes one of its tasks, it announces what it just did
 * (implemented in the `kprintf` in the `work` function). When an elf
 * has completed all of its work the supervisor dismisses the elf by
 * saying "Thanks for your work, Elf N!" where N corresponds to the N-th elf.
 *
 * At the beginning of the day, the supervisor (a supervisor thread)
 * opens the factory and lets the elves inside (starts their threads).
 * At any given moment, there is a single supervisor and possibly
 * multiple elves working. The supervisor is not allowed to dismiss an
 * elf until that elf has finished working. Your solution CANNOT wait
 * for ALL the elves to finish before starting to dismiss them.
 */

/* Synchronization method:
 1. A semaphore is used the supervisor thread so that it will wait for 
 all elves to finish;

 2. Before we spawn any elf thread, we will run P on the semaphore and V
 when we're done. This is similar to the scheme used for clean exit on
 a thread;

 3. We use another semaphore for printing "thank you mesaage". An elf thread 
 will acquire the lock before calling V and save the elf's number to a
 global variable;

 4. The supervisor thread will then refer to the global varible, say
 thank you, and release the lock. In this way, we make sure that supervisor
 must wait for an elf to finish to say thank you.
*/

#include <types.h>
#include <lib.h>
#include <wchan.h>
#include <thread.h>
#include <synch.h>
#include <test.h>
#include <kern/errno.h>
#include "common.h"

#define NUM_TASKS 16

static const char *tasks[NUM_TASKS] = {
	"Stirred the marshmallow mush",
	"Melted the dark chocolate",
	"Salted the caramel",
	"Fluffed the meringue",
	"Counted the butterscotch chips",
	"Chopped up the mint",
	"Chopped up the sprinkles",
	"Whipped up the cream",
	"Tasted the toffee",
	"Cooled the fudge",
	"Mixed the molasses",
	"Froze the frosting",
	"Sliced the sugar cookies",
	"Baked the apples",
	"Melted the candy coating",
	"Perfected the plum sauce",
};

struct semaphore *print_lock;
struct semaphore *elf_lock;
struct semaphore *complete_lock;
unsigned elf_complete;
/*
 * Do not modify this!
 */
static
void
work(unsigned elf_num)
{
	int r;

	r = random() % NUM_TASKS;
	while (r != 0) {
		kprintf("Elf %3u: %s\n", elf_num, tasks[r]);
		r = random() % NUM_TASKS;
		thread_yield(); // cause some interleaving!
	}
}

// One of these structs should be passed from the main driver thread
// to the supervisor thread.
struct supervisor_args {
	// Add stuff as necessary
};

// One of these structs should be passed from the supervisor thread
// to each of the elf threads.
struct elf_args {
	// Add stuff as necessary
};

static
void
elf(void *args, unsigned long elf_num)
{
	struct elf_args *eargs = (struct elf_args *) args;

	(void) elf_num; // suppress unused warnings
	(void) eargs; // suppress unused warnings
	// TODO
	work(elf_num);

	P(complete_lock);
	elf_complete = elf_num;
	V(elf_lock);
}

static
void
supervisor(void *args, unsigned long num_elves)
{
	struct supervisor_args *sargs = (struct supervisor_args *) args;
	unsigned long e;
	int err=0;

	(void) num_elves; // suppress unused warnings
	(void) sargs; // suppress unused warnings

	for (e = 0; e < num_elves; e++) {
		err = thread_fork("Elf thread", NULL, elf, NULL, e);
		if (err != 0)
			panic("elf: thread_fork failed: %s)\n", strerror(err));
		P(elf_lock);
	}

	for (e = 0; e < num_elves; e++) {
		P(elf_lock);
		P(print_lock);
		kprintf("Thank you for your work, Elf %d.\n", elf_complete);
		V(print_lock);
		V(complete_lock);
	}
}

int
elves(int nargs, char **args)
{
	unsigned num_elves;

	// if an argument is passed, use that as the number of elves
	num_elves = 10;
	if (nargs == 2) {
		num_elves = atoi(args[1]);
	}

	// Suppress unused warnings. Remove these when finished.
	(void) work;
	(void) supervisor;
	(void) elf;
    (void) num_elves;
	// TODO

	print_lock = sem_create("print", 1);
	elf_lock = sem_create("elf", num_elves);
	complete_lock = sem_create("complete_lk", 1);
	elf_complete = -1;

	kprintf("Starting up Keebler Factory!\n");
	supervisor(NULL, num_elves);

	return 0;
}
