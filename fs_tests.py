#!/usr/bin/python

import subprocess

def run(command):
	print command
	subprocess.call(command.split(' '))

def frack(params):
	print "Testing: %s" % params
	for doom in xrange(20):
		run('sys161 -D %d kernel "mount sfs lhd1:; cd lhd1:;'
			'p /testbin/frack do %s; q"' % (doom, params))
		run('sys161 kernel "mount sfs lhd1:; cd lhd1:;'
			'p /testbin/frack check %s; q"' % (params))

def test(command):
	pass

def main():
	size = 1024 * 50
	seed = 1337

	# More tests go here

	# Frack tests
	frack("createwrite %d" % size)
	frack("rewrite %d" % size)
	frack("randupdate %d" % size)
	frack("truncwrite %d" % size)
	frack("makehole %d" % size)
	frack("fillhole %d" % size)
	frack("truncfill %d" % size)
	frack("append %d" % size)
	frack("trunczero %d" % size)
	frack("trunconeblock %d" % size)
	frack("truncsmallersize %d" % size)
	frack("trunclargersize %d" % size)
	frack("appendandtrunczero %d" % size)
	frack("appendandtruncpartly %d" % size)
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

if __name__ == "__main__":
	main()