//
//  Visopsys
//  Copyright (C) 1998-2018 J. Andrew McLaughlin
//
//  This library is free software; you can redistribute it and/or modify it
//  under the terms of the GNU Lesser General Public License as published by
//  the Free Software Foundation; either version 2.1 of the License, or (at
//  your option) any later version.
//
//  This library is distributed in the hope that it will be useful, but
//  WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
//  General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public License
//  along with this library; if not, write to the Free Software Foundation,
//  Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
//  ext.h
//

// This file contains definitions and structures for using and manipulating
// EXT2 and EXT3 filesystems in Visopsys.  The reference material for this
// header can be found at: http://www.nongnu.org/ext2-doc/ext2.html

#if !defined(_EXT_H)

/*
  The organisation of an ext2 file system on a floppy:

  offset # of blocks description
  -------- ----------- -----------
  0                  1 boot record
  -- block group 0 --
  (1024 bytes)       1 superblock
  2                  1 group descriptors
  3                  1 block bitmap
  4                  1 inode bitmap
  5                 23 inode table
  28              1412 data blocks

  The organisation of a 20MB ext2 file system:

  offset # of blocks description
  -------- ----------- -----------
  0                  1 boot record
  -- block group 0 --
  (1024 bytes)       1 superblock
  2                  1 group descriptors
  3                  1 block bitmap
  4                  1 inode bitmap
  5                214 inode table
  219             7974 data blocks
  -- block group 1 --
  8193               1 superblock backup
  8194               1 group descriptors backup
  8195               1 block bitmap
  8196               1 inode bitmap
  8197             214 inode table
  8408            7974 data blocks
  -- block group 2 --
  16385              1 block bitmap
  16386              1 inode bitmap
  16387            214 inode table
  16601           3879 data blocks
*/

// Definitions

// Superblock-related constants
#define EXT_SUPERBLOCK_OFFSET		1024
#define EXT_SUPERBLOCK_MAGIC		0xEF53

// For extents
#define EXT_EXTENT_MAGIC			0xF30A

// EXT_ERRORS values for the 'errors' field in the superblock
#define EXT_ERRORS_CONTINUE			1  // Continue as if nothing happened
#define EXT_ERRORS_RO				2  // Remount read-only
#define EXT_ERRORS_PANIC			3  // Cause a kernel panic
#define EXT_ERRORS_DEFAULT			EXT_ERRORS_CONTINUE

// EXT_OS: 32-bit identifier of the OS that created the file system for
// the 'creator_os' field in the superblock
#define EXT_OS_LINUX				0			// Linux
#define EXT_OS_HURD					1			// Hurd
#define EXT_OS_MASIX				2			// MASIX
#define EXT_OS_FREEBSD				3			// FreeBSD
#define EXT_OS_LITES4				4			// Lites
#define EXT_OS_VISOPSYS				0xA600D05	// Visopsys

// 32-bit revision level value for the 'rev_level' field in the superblock
#define EXT_GOOD_OLD_REV			0  // Original format
#define EXT_DYNAMIC_REV				1  // V2 format with dynamic inode sizes

// Superblock read-write compatibility flags
#define EXT_COMPAT_SPARSESUPER2		0x200  // Sparse superblocks, V2
#define EXT_COMPAT_EXCLBITMAP		0x100  // Exclude bitmap
#define EXT_COMPAT_EXCLINODE		0x080  // Exclude inode
#define EXT_COMPAT_LAZYBG			0x040  // Lazy block groups
#define EXT_COMPAT_DIRINDEX			0x020  // Has directory indices
#define EXT_COMPAT_RESIZEINODE		0x010  // Has reserved GDT blocks
#define EXT_COMPAT_EXTATTR			0x008  // Supports extended attributes
#define EXT_COMPAT_HASJOURNAL		0x004  // Has a journal
#define EXT_COMPAT_IMAGICINODES		0x002  // "imagic inodes"
#define EXT_COMPAT_DIRPREALLOC		0x001  // Directory preallocation

// Superblock incompatibility flags
#define EXT_INCOMPAT_INLINEDATA		0x8000  // Data in inodes
#define EXT_INCOMPAT_LARGEDIR		0x4000  // Large directories
#define EXT_INCOMPAT_USEMETACSUM	0x2000 	// Never used
#define EXT_INCOMPAT_DIRDATA		0x1000  // Data in directory entries
#define EXT_INCOMPAT_EAINODE		0x0400 	// Extended attributes in inodes
#define EXT_INCOMPAT_FLEXBG			0x0200  // Flexible block groups
#define EXT_INCOMPAT_MMP			0x0100  // Multiple mount protection
#define EXT_INCOMPAT_64BIT			0x0080  // Enable 2^64 blocks
#define EXT_INCOMPAT_EXTENTS		0x0040  // Files use extents
#define EXT_INCOMPAT_METABG			0x0010  // Meta block groups
#define EXT_INCOMPAT_JOURNALDEV		0x0008  // Separate journal device
#define EXT_INCOMPAT_RECOVER		0x0004  // Filesystem needs recovery
#define EXT_INCOMPAT_FILETYPE		0x0002  // Directory entries have file type
#define EXT_INCOMPAT_COMPRESSION	0x0001  // Compression

// Superblock read-only compatibility flags
#define EXT_ROCOMPAT_METADATACSUM	0x400  // Metadata checksumming
#define EXT_ROCOMPAT_BIGALLOC		0x200  // Extents are units of blocks
#define EXT_ROCOMPAT_QUOTA			0x100  // Quotas
#define EXT_ROCOMPAT_HASSNAPSHOT	0x080  // Has a snapshot
#define EXT_ROCOMPAT_EXTRAISIZE		0x040  // Large inodes
#define EXT_ROCOMPAT_DIRNLINK		0x020  // No 32K subdirectory limit
#define EXT_ROCOMPAT_GDTCSUM		0x010  // Group descriptors have checksums
#define EXT_ROCOMPAT_HUGEFILE		0x008  // File sizes in logical blocks
#define EXT_ROCOMPAT_BTREEDIR		0x004  // B-tree directories
#define EXT_ROCOMPAT_LARGEFILE		0x002  // Has a file greater than 2GiB
#define EXT_ROCOMPAT_SPARSESUPER	0x001  // Sparse superblocks

// If the revision level (above) is EXT_GOOD_OLD_REV, here are a coupla
// fixed values
#define EXT_GOOD_OLD_FIRST_INODE	11
#define EXT_GOOD_OLD_INODE_SIZE		128

// File system states
#define EXT_VALID_FS				1  // Unmounted cleanly
#define EXT_ERROR_FS				2  // Errors detected

// Reserved inode numbers for the inode table
#define EXT_BAD_INO					1  // Bad blocks inode
#define EXT_ROOT_INO				2  // Root directory inode
#define EXT_ACL_IDX_INO				3  // ACL index inode
#define EXT_ACL_DATA_INO			4  // ACL data inode
#define EXT_BOOT_LOADER_INO			5  // Boot loader inode
#define EXT_UNDEL_DIR_INO			6  // Undelete directory inode

// File types for the file_type field in extDirEntry
#define EXT_FT_UNKNOWN				0
#define EXT_FT_REG_FILE				1
#define EXT_FT_DIR					2
#define EXT_FT_CHRDEV				3
#define EXT_FT_BLKDEV				4
#define EXT_FT_FIFO					5
#define EXT_FT_SOCK					6
#define EXT_FT_SYMLINK				7
#define EXT_FT_MAX					8

// EXT_S_: 16-bit value used to indicate the format of the described file
// and the access rights for the i_mode field in extInode
//                 -- file format --
#define EXT_S_IFMT					0xF000  // Format mask
#define EXT_S_IFSOCK				0xC000  // Socket
#define EXT_S_IFLNK					0xA000  // Symbolic link
#define EXT_S_IFREG					0x8000  // Regular file
#define EXT_S_IFBLK					0x6000  // Block device
#define EXT_S_IFDIR					0x4000  // Directory
#define EXT_S_IFCHR					0x2000  // Character device
#define EXT_S_IFIFO					0x1000  // Fifo
//                 -- access rights --
#define EXT_S_ISUID					0x0800  // SUID
#define EXT_S_ISGID					0x0400  // SGID
#define EXT_S_ISVTX					0x0200  // Sticky bit
#define EXT_S_IRWXU					0x01C0  // User access rights mask
#define EXT_S_IRUSR					0x0100  // Read
#define EXT_S_IWUSR					0x0080  // Write
#define EXT_S_IXUSR					0x0040  // Execute
#define EXT_S_IRWXG					0x0038  // Group access rights mask
#define EXT_S_IRGRP					0x0020  // Read
#define EXT_S_IWGRP					0x0010  // Write
#define EXT_S_IXGRP					0x0008  // Execute
#define EXT_S_IRWXO					0x0007  // Others access rights mask
#define EXT_S_IROTH					0x0004  // Read
#define EXT_S_IWOTH					0x0002  // Write
#define EXT_S_IXOTH					0x0001  // Execute

// Values for the 'flags' field in extInode
#define EXT_RESERVED_FL				0x80000000 	// Reserved for ext4 library
#define EXT_INLINE_DATA_FL			0x10000000 	// Inode has inline data
#define EXT_SNAPFILE_SHRUNK_FL		0x08000000 	// Snapshot shrink completed
#define EXT_SNAPFILE_DELETED_FL		0x04000000 	// Snapshot being deleted
#define EXT_SNAPFILE_FL				0x01000000 	// Inode is a snapshot
#define EXT_EOFBLOCKS_FL			0x00400000 	// Blocks allocated past EOF
#define EXT_EA_INODE_FL				0x00200000 	// Extended attribute
#define EXT_EXTENTS_FL				0x00080000 	// Inode uses extents
#define EXT_HUGE_FILE_FL			0x00040000 	// Huge file
#define EXT_TOPDIR_FL				0x00020000 	// Top of dir hierarchy
#define EXT_DIRSYNC_FL				0x00010000 	// Write synchronously
#define EXT_NOTAIL_FL				0x00008000 	// Tail should not be merged
#define EXT_JOURNAL_DATA_FL			0x00004000  // Journal file data
#define EXT_IMAGIC_FL				0x00002000  // AFS directory
#define EXT_INDEX_FL				0x00001000  // Hash indexed directory
#define EXT_BTREE_FL				0x00001000  // B-tree format directory
#define EXT_ECOMPR_FL				0x00000800  // Compression error
#define EXT_NOCOMPR_FL				0x00000400  // Access raw compressed data
#define EXT_COMPRBLK_FL				0x00000200  // Compressed blocks
#define EXT_DIRTY_FL				0x00000100  // Dirty (file is in use?)
#define EXT_NOATIME_FL				0x00000080  // Do not update .i_atime
#define EXT_NODUMP_FL				0x00000040  // Do not dump/delete file
#define EXT_APPEND_FL				0x00000020  // Append only
#define EXT_IMMUTABLE_FL			0x00000010  // Immutable file
#define EXT_SYNC_FL					0x00000008  // Synchronous updates
#define EXT_COMPR_FL				0x00000004  // Compressed file
#define EXT_UNRM_FL					0x00000002  // Record for undelete
#define EXT_SECRM_FL				0x00000001  // Secure deletion

typedef struct {
	unsigned inodes_count;				// 0x000
	unsigned blocks_count;				// 0x004
	unsigned r_blocks_count;			// 0x008
	unsigned free_blocks_count;			// 0x00C
	unsigned free_inodes_count;			// 0x010
	unsigned first_data_block;			// 0x014
	unsigned log_block_size;			// 0x018
	unsigned log_cluster_size;			// 0x01C
	unsigned blocks_per_group;			// 0x020
	unsigned clusters_per_group;		// 0x024
	unsigned inodes_per_group;			// 0x028
	unsigned mtime;						// 0x02C
	unsigned wtime;						// 0x030
	unsigned short mnt_count;			// 0x034
	unsigned short max_mnt_count;		// 0x036
	unsigned short magic;				// 0x038
	unsigned short state;				// 0x03A
	unsigned short errors;				// 0x03C
	unsigned short minor_rev_level;		// 0x03E
	unsigned lastcheck;					// 0x040
	unsigned checkinterval;				// 0x044
	unsigned creator_os;				// 0x048
	unsigned rev_level;					// 0x04C
	unsigned short def_resuid;			// 0x050
	unsigned short def_resgid;			// 0x052
	// EXT2_DYNAMIC_REV specific
	unsigned first_ino;					// 0x054
	unsigned short inode_size;			// 0x058
	unsigned short block_group_nr;		// 0x05A
	unsigned feature_compat;			// 0x05C
	unsigned feature_incompat;			// 0x060
	unsigned feature_ro_compat;			// 0x064
	unsigned char uuid[16];				// 0x068
	char volume_name[16];				// 0x078
	unsigned char last_mounted[64];		// 0x088
	unsigned algo_bitmap;				// 0x0C8
	// Performance Hints
	unsigned char prealloc_blocks;		// 0x0CC
	unsigned char prealloc_dir_blocks;	// 0x0CD
	unsigned short alignment;			// 0x0CE
	// Journaling Support
	unsigned char journal_uuid[16];		// 0x0D0
	unsigned journal_inum;				// 0x0E0
	unsigned journal_dev;				// 0x0E4
	unsigned last_orphan;				// 0x0E8
	unsigned hash_seed[4];				// 0x0EC
	unsigned char def_hash_version;		// 0x0FC
	unsigned char jnl_backup_type;		// 0x0FD
	unsigned short desc_size;			// 0x0FE
	unsigned default_mount_opts;		// 0x100
	unsigned first_meta_bg;				// 0x104
	unsigned mkfs_time;					// 0x108
	unsigned jnl_blocks[17];			// 0x10C
	// 64-bit support
	unsigned blocks_count_hi;			// 0x150
	unsigned r_blocks_count_hi;			// 0x154
	unsigned free_blocks_count_hi;		// 0x158
	unsigned short min_extra_isize;		// 0x15C
	unsigned short want_extra_isize;	// 0x15E
	unsigned flags;						// 0x160
	unsigned short raid_stride;			// 0x164
	unsigned short mmp_interval;		// 0x166
	uquad_t mmp_block;					// 0x168
	unsigned raid_stripe_width;			// 0x170
	unsigned char log_groups_per_flex;	// 0x174
	unsigned char checksum_type;		// 0x175
	unsigned short reserved_pad;		// 0x176
	uquad_t kbytes_written;				// 0x178
	unsigned snapshot_inum;				// 0x180
	unsigned snapshot_id;				// 0x184
	uquad_t snapshot_r_blocks_count;	// 0x188
	unsigned snapshot_list;				// 0x190
	unsigned error_count;				// 0x194
	unsigned first_error_time;			// 0x198
	unsigned first_error_ino;			// 0x19C
	uquad_t first_error_block;			// 0x1A0
	unsigned char first_error_func[32];	// 0x1A8
	unsigned first_error_line;			// 0x1C8
	unsigned last_error_time;			// 0x1CC
	unsigned last_error_ino;			// 0x1D0
	unsigned last_error_line;			// 0x1D4
	uquad_t last_error_block;			// 0x1D8
	unsigned char last_error_func[32];	// 0x1E0
	unsigned char mount_opts[64];		// 0x200
	unsigned usr_quota_inum;			// 0x240
	unsigned grp_quota_inum;			// 0x244
	unsigned overhead_blocks;			// 0x248
	unsigned backup_bgs[2];				// 0x24C
	unsigned reserved[106];				// 0x24E
	unsigned checksum;					// 0x3FC

} __attribute__((packed)) extSuperblock;

typedef struct {
	unsigned block_bitmap;				// 0x00
	unsigned inode_bitmap;				// 0x04
	unsigned inode_table;				// 0x08
	unsigned short free_blocks_count;	// 0x0C
	unsigned short free_inodes_count;	// 0x0E
	unsigned short used_dirs_count;		// 0x10
	unsigned short flags;				// 0x12
	unsigned exclude_bitmap;			// 0x14
	unsigned short block_bitmap_csum;	// 0x18
	unsigned short inode_bitmap_csum;	// 0x1A
	unsigned short itable_unused;		// 0x1C
	unsigned short checksum;			// 0x1E

} __attribute__((packed)) extGroupDesc;

typedef struct {
	unsigned short magic;				// 0x0
	unsigned short entries;				// 0x2
	unsigned short max;					// 0x4
	unsigned short depth;				// 0x6
	unsigned generation;				// 0x8

} __attribute__((packed)) extExtentHeader;

typedef struct {
	unsigned block;						// 0x00
	unsigned leaf_lo;					// 0x04
	unsigned short leaf_hi;				// 0x08
	unsigned short unused;				// 0x0A

} __attribute__((packed)) extExtentIdx;

typedef struct {
	unsigned block;						// 0x00
	unsigned short len;					// 0x04
	unsigned short start_hi;			// 0x06
	unsigned start_lo;					// 0x08

} __attribute__((packed)) extExtentLeaf;

typedef struct {
	extExtentHeader header;
	union {
		extExtentIdx idx;
		extExtentLeaf leaf;
	} node[];

} __attribute__((packed)) extExtent;

typedef struct {
	unsigned short mode;				// 0x00
	unsigned short uid;					// 0x02
	unsigned size;						// 0x04
	unsigned atime;						// 0x08
	unsigned ctime;						// 0x0C
	unsigned mtime;						// 0x10
	unsigned dtime;						// 0x14
	unsigned short gid;					// 0x18
	unsigned short links_count;			// 0x1A
	unsigned blocks512;					// 0x1C
	unsigned flags;						// 0x20
	unsigned osd1;						// 0x24
	union {								// 0x28
		// When the superblock EXT_INCOMPAT_EXTENTS and inode EXT_EXTENTS_FL
		// flags are set, an extent tree replaces the normal blocks list.
		unsigned block[15];
		extExtent extent;
	} u;
	unsigned generation;				// 0x64
	unsigned file_acl;					// 0x68
	unsigned dir_acl;					// 0x6C
	unsigned faddr;						// 0x70
	unsigned char osd2[12];				// 0x74

} __attribute__((packed)) extInode;

typedef struct {
	unsigned inode;
	unsigned short rec_len;
	union {
		// When the superblock EXT_INCOMPAT_FILETYPE flag is set, half of the
		// name_len field is re-purposed as a file type indicator.
		unsigned short name_len;
		struct {
			unsigned char name_len;
			unsigned char file_type;
		} lenType;
	} u;
	char name[256];

} __attribute__((packed)) extDirEntry;

#define _EXT_H
#endif

