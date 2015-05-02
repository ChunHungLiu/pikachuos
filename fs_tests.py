#!/usr/bin/python

import subprocess
import signal
import sys
import os
import re
from termcolor import colored

VERSION_RE = re.compile("Established ([0-9]+) versions")

BAD_TEXT = ["panic", "assert", "fail"]

def bad(line):
	return any([term in line.lower() for term in BAD_TEXT])
 
def run(command, print_start="unmatchable", print_end="unmatchable"):
	print colored("Running " + command, "green", attrs=["bold"])
	result = subprocess.Popen(command.split(" "), 
		stdout=subprocess.PIPE, stderr=subprocess.PIPE)
	print_output(result, print_start, print_end)

def print_output(result, print_start="unmatchable", print_end="unmatchable"):
	do_print = False
	bad_print = False
	for line in result.stderr:
		bad_print |= bad(line)
		if bad_print:
			print colored(line[:-1], "black", "on_red")
		if "warn" in line.lower():
			print colored(line[:-1], "yellow", attrs=["bold"])
	for line in result.stdout:
		bad_print |= bad(line)
		if bad_print:
			print colored(line[:-1], "black", "on_red")
		if "warn" in line.lower():
			print colored(line[:-1], "yellow", attrs=["bold"])
			continue

		do_print |= print_start in line
		if do_print:
			print line[:-1]
		if print_end in line:
			do_print = False
	if bad_print:
		os.system('stty sane')
		sys.exit(0)

def frack(params, max_doom=5):
	print "Testing: %s" % params
	for doom in xrange(1, max_doom):
		run('hostbin/host-poisondisk LHD1.img')
		run('hostbin/host-mksfs LHD1.img test')

		run('sys161 -D %d kernel mount sfs lhd1:; cd lhd1:;'
			'p /testbin/frack do %s; q' % (doom, params))

		run('sys161 kernel mount sfs lhd1:; cd lhd1:; '
			'p /testbin/frack check %s; q' % (params),
			print_start="OS/161 kernel:  p /testbin/frack check",
			print_end="Done.")

		run('hostbin/host-sfsck LHD1.img',
			print_start='', print_end='')

def dirconc(seed, max_doom=5):
	print "Testing dirconc (%d)" % seed
	for doom in xrange(1, max_doom):
		run('hostbin/host-poisondisk LHD1.img')
		run('hostbin/host-mksfs LHD1.img test')

		run('sys161 -D %d kernel mount sfs lhd1:; cd lhd1:;'
			'p /testbin/dirconc lhd1: %d; q' % (doom, seed))

		run('hostbin/host-sfsck LHD1.img',
			print_start='', print_end='')

def bigfile(max_doom=5):
	print "Testing bigfile"
	for doom in xrange(1, max_doom):
		run('hostbin/host-poisondisk LHD1.img')
		run('hostbin/host-mksfs LHD1.img test')

		run('sys161 -D %d kernel mount sfs lhd1:; cd lhd1:;'
			'p /testbin/bigfile test 813498; q' % (doom))

		run('hostbin/host-sfsck LHD1.img',
			print_start='', print_end='')


def test(command):
	pass

def main():
	size = "small"
	seed = 1337

	# More tests go here

	print "dirseek"
	print "dirtest"
	print "f_test"
	print "filetest"
	print "rmdirtest"
	print "rmtest"
	print "sparsefile"

	return # Tests below this are all passing

	# Full test suite


	bigfile(20)
	dirconc(seed, 20)

	# Frack tests
	frack("createwrite %s" % size)
	frack("rewrite %s" % size)
	frack("randupdate %s" % size)
	frack("truncwrite %s" % size)
	frack("makehole %s" % size)
	frack("fillhole %s" % size)
	frack("truncfill %s" % size)
	frack("append %s" % size)
	frack("trunczero %s" % size)
	frack("trunconeblock %s" % size)
	frack("truncsmallersize %s" % size)
	frack("trunclargersize %s" % size)
	frack("appendandtrunczero %s" % size)
	frack("appendandtruncpartly %s" % size)
	frack("mkfile")
	frack("mkdir")
	frack("mkmanyfile")
	frack("mkmanydir")
	frack("mktree")
	frack("mkrandtree %d" % seed, max_doom=20)
	frack("rmfile")
	frack("rmdir")
	frack("rmfiledelayed")
	frack("rmfiledelayedappend")
	frack("rmdirdelayed")
	frack("rmmanyfile")
	frack("rmmanyfiledelayed")
	frack("rmmanyfiledelayedandappend")
	frack("rmmanydir")
	frack("rmmanydirdelayed")
	frack("rmtree")
	frack("rmrandtree %d" % seed)
	frack("linkfile")
	frack("linkmanyfile")
	frack("unlinkfile")
	frack("unlinkmanyfile")
	frack("linkunlinkfile")
	frack("renamefile")
	frack("renamedir")
	frack("renamesubtree")
	frack("renamexdfile")
	frack("renamexddir")
	frack("renamexdsubtree")
	frack("renamemanyfile")
	frack("renamemanydir")
	frack("renamemanysubtree")
	frack("copyandrename")
	frack("untar")
	frack("compile")
	frack("cvsupdate")
	frack("writefileseq %d" % seed)
	frack("writetruncseq %d" % seed)
	frack("mkrmseq %d" % seed)
	frack("linkunlinkseq %d" % seed)
	frack("renameseq %d" % seed)
	frack("diropseq %d" % seed)
	frack("genseq %d" % seed)

def signal_term_handler(signal, frame):
	print 'got SIGTERM or SIGINT'
	os.system('stty sane')
	sys.exit(0)
 
if __name__ == "__main__":
	signal.signal(signal.SIGTERM, signal_term_handler)
	signal.signal(signal.SIGINT, signal_term_handler)
	main()

