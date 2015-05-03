#!/usr/bin/python

import subprocess
import signal
import sys
from termcolor import colored
 
def run(command):
	print colored(command, 'red')
	subprocess.call(command.split(' '))

def frack(params):
	print "Testing: %s" % params
	for doom in xrange(20):
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
    print 'got SIGTERM'
    sys.exit(0)
 
signal.signal(signal.SIGTERM, signal_term_handler)
if __name__ == "__main__":
	main()

