/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009, 2014
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

#ifndef _KERN_SFS_H_
#define _KERN_SFS_H_


/*
 * SFS definitions visible to userspace. This covers the on-disk format
 * and is used by tools that work on SFS volumes, such as mksfs.
 */

#define SFS_MAGIC         0xabadf001    /* magic number identifying us */
#define SFS_BLOCKSIZE     512           /* size of our blocks */
#define SFS_VOLNAME_SIZE  32            /* max length of volume name */
#define SFS_NDIRECT       15            /* # of direct blocks in inode */
#define SFS_NINDIRECT     1             /* # of indirect blocks in inode */
#define SFS_NDINDIRECT    1             /* # of 2x indirect blocks in inode */
#define SFS_NTINDIRECT    1             /* # of 3x indirect blocks in inode */
#define SFS_DBPERIDB      128           /* # direct blks per indirect blk */
#define SFS_NAMELEN       60            /* max length of filename */
#define SFS_SUPER_BLOCK   0             /* block the superblock lives in */
#define SFS_FREEMAP_START 2             /* 1st block of the freemap */
#define SFS_NOINO         0             /* inode # for free dir entry */
#define SFS_ROOTDIR_INO   1             /* loc'n of the root dir inode */

/* Number of bits in a block */
#define SFS_BITSPERBLOCK (SFS_BLOCKSIZE * CHAR_BIT)

/* Utility macro */
#define SFS_ROUNDUP(a,b)       ((((a)+(b)-1)/(b))*b)

/* Size of free block bitmap (in bits) */
#define SFS_FREEMAPBITS(nblocks) SFS_ROUNDUP(nblocks, SFS_BITSPERBLOCK)

/* Size of free block bitmap (in blocks) */
#define SFS_FREEMAPBLOCKS(nblocks)  (SFS_FREEMAPBITS(nblocks)/SFS_BITSPERBLOCK)

/* File types for sfi_type */
#define SFS_TYPE_INVAL    0       /* Should not appear on disk */
#define SFS_TYPE_FILE     1
#define SFS_TYPE_DIR      2

// We start from 3 because 0, 1, 2 are reserved

#define BLOCK_ALLOC	3
#define	INODE_LINK  4
#define META_UPDATE 5
#define BLOCK_DEALLOC 6
#define TRUNCATE	7
#define BLOCK_WRITE 8
#define INODE_UPDATE_TYPE 9
#define TRANS_BEGIN 10
#define TRANS_COMMIT 11
#define RESIZE 12

/*
 * On-disk superblock
 */
struct sfs_superblock {
	uint32_t sb_magic;		/* Magic number; should be SFS_MAGIC */
	uint32_t sb_nblocks;			/* Number of blocks in fs */
	char sb_volname[SFS_VOLNAME_SIZE];	/* Name of this volume */
	uint32_t sb_journalstart;		/* First block in journal */
	uint32_t sb_journalblocks;		/* # of blocks in journal */
	uint32_t reserved[116];			/* unused, set to 0 */
};

/*
 * On-disk inode
 */
struct sfs_dinode {
	uint32_t sfi_size;			/* Size of this file (bytes) */
	uint16_t sfi_type;			/* One of SFS_TYPE_* above */
	uint16_t sfi_linkcount;			/* # hard links to this file */
	uint32_t sfi_direct[SFS_NDIRECT];	/* Direct blocks */
	uint32_t sfi_indirect;			/* Indirect block */
	uint32_t sfi_dindirect;   /* Double indirect block */
	uint32_t sfi_tindirect;   /* Triple indirect block */
	uint32_t sfi_waste[128-5-SFS_NDIRECT];	/* unused space, set to 0 */
};

/*
 * On-disk directory entry
 */
struct sfs_direntry {
	uint32_t sfd_ino;			/* Inode number */
	char sfd_name[SFS_NAMELEN];		/* Filename */
};

/*
 * Buffer metadata
 */
struct b_fsdata {
	struct sfs_fs *sfs;
	daddr_t diskblock;
	uint64_t oldest_lsn;
};

/*
 * On-disk journal container types and constants
 */

/*
 * On-disk bit-packed type for use in record headers; contains the
 * container-level information for a journal record, namely:
 *     48-bit LSN
 *     8-bit length, in 2-octet units
 *     7-bit type code
 *     1-bit type code class
 *
 * The type code class is either SFS_JPHYS_CONTAINER, for container-
 * level records, or SFS_JPHYS_CLIENT, for records defined by higher-
 * level code.
 *
 * The length is stored in 2-octet units so we only need 8 bits for a
 * record of up to one whole block.
 *
 * The length includes the header. (struct sfs_jphys_header)
 *
 * Note that a coninfo whose integer value is 0 is not valid; this
 * prevents us from getting confused by still-zeroed journal blocks.
 */
#define SFS_CONINFO_CLASS(ci)	((ci) >> 63)  /* client vs. container record */
#define SFS_CONINFO_TYPE(ci)	(((ci) >> 56) & 0x7f)	/* record type */
#define SFS_CONINFO_LEN(ci)	((((ci) >> 48) & 0xff)*2) /* record length */
#define SFS_CONINFO_LSN(ci)	((ci) & 0xffffffffffff)	/* log sequence no. */
#define SFS_MKCONINFO(cl, ty, len, lsn) \
	(						\
		((uint64_t)(cl) << 63) |		\
		((uint64_t)(ty) << 56) |		\
		((uint64_t)((len + 1) / 2) << 48) |	\
		(lsn)					\
	)

/* symbolic names for the type code classes */
#define SFS_JPHYS_CONTAINER	0
#define SFS_JPHYS_CLIENT	1

/* container-level record types (allowable range 0-127) */
#define SFS_JPHYS_INVALID	0		/* No record here */
#define SFS_JPHYS_PAD		1		/* Padding */
#define SFS_JPHYS_TRIM		2		/* Log trim record */

/* The record header */
struct sfs_jphys_header {
	uint64_t jh_coninfo;			/* Container info */
};

/* Contents for SFS_JPHYS_TRIM */
struct sfs_jphys_trim {
	uint64_t jt_taillsn;			/* Tail LSN */
};

struct block_alloc_args {
	unsigned code;
	daddr_t disk_addr;
	daddr_t ref_addr;
	size_t offset_addr;
};

struct inode_link_args {
	unsigned code;
	daddr_t disk_addr;
	uint16_t old_linkcount;
	uint16_t new_linkcount;
};

struct meta_update_args {
	unsigned code;
	daddr_t disk_addr;
	size_t offset_addr;
	size_t data_len;
	void *old_data;
	void *new_data;
};

struct block_dealloc_args {
	unsigned code;
	daddr_t disk_addr;
};

struct truncate_args {
	unsigned code;
	daddr_t inode_addr;
	daddr_t start_block;
	daddr_t end_block;
};

struct resize_args {
	unsigned code;
	daddr_t inode_addr;
	size_t old_size;
	size_t new_size;
};

struct block_write_args {
	unsigned code;
	daddr_t written_addr;
	uint32_t new_checksum;
	bool new_alloc;
};

struct inode_update_type_args {
	unsigned code;
	daddr_t inode_addr;
	int old_type;
	int new_type;
};

struct trans_begin_args {
	unsigned code;
	int trans_type;
	int id;
};

struct trans_commit_args {
	unsigned code;
	int trans_type;
	int id;
};


#endif /* _KERN_SFS_H_ */
