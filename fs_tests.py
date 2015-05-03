#!/usr/bin/python

import subprocess
import signal
import sys
import os
from termcolor import colored
 
def run(command):
	print colored(command, "red")
	result = subprocess.Popen(command.split(" "), 
		stdout=subprocess.PIPE, stderr=subprocess.PIPE)
	do_print = False
	for line in result.stdout:
		if "warn" in line.lower():
			print colored(line[:-1], "yellow")
			continue
		if "OS/161 kernel: p /testbin/frack check" in line:
			do_print = True
		if do_print:
			print line[:-1]
		if "Done." in line:
			do_print = False

def frack(params):
	print "Testing: %s" % params
	for doom in xrange(1,2):
		run('hostbin/host-mksfs LHD1.img test')
		run('sys161 -D %d kernel mount sfs lhd1:; cd lhd1:;'
			'p /testbin/frack do %s; q' % (doom, params))
		run('sys161 kernel mount sfs lhd1:; cd lhd1:;'
			'p /testbin/frack check %s; q' % (params))

def test(command):
	pass

def main():
	size = "small"
	seed = 1337

	# More tests go here

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
	frack("mkrandtree %d" % seed)
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

