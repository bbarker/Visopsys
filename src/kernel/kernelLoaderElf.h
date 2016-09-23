//
//  Visopsys
//  Copyright (C) 1998-2016 J. Andrew McLaughlin
//
//  This program is free software; you can redistribute it and/or modify it
//  under the terms of the GNU General Public License as published by the Free
//  Software Foundation; either version 2 of the License, or (at your option)
//  any later version.
//
//  This program is distributed in the hope that it will be useful, but
//  WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
//  or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
//  for more details.
//
//  You should have received a copy of the GNU General Public License along
//  with this program; if not, write to the Free Software Foundation, Inc.,
//  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
//  kernelLoaderElf.h
//

// This file defines things used by kernelLoaderElf.c for dealing with ELF
// format executables and object files.  Much of the information here comes
// straight from the Tools Interface Standard (TIS) Portable Formats
// Specification, version 1.1

#if !defined(_KERNELLOADERELF_H)

// ELF file types
#define ELFTYPE_RELOC		1
#define ELFTYPE_EXEC		2
#define ELFTYPE_SHARED		3
#define ELFTYPE_CORE		4

// ELF section types
#define ELFSHT_NULL			0
#define ELFSHT_PROGBITS		1
#define ELFSHT_SYMTAB		2
#define ELFSHT_STRTAB		3
#define ELFSHT_RELA			4
#define ELFSHT_HASH			5
#define ELFSHT_DYNAMIC		6
#define ELFSHT_NOTE			7
#define ELFSHT_NOBITS		8
#define ELFSHT_REL			9
#define ELFSHT_SHLIB		10
#define ELFSHT_DYNSYM		11
#define ELFSHT_LOPROC		0x70000000
#define ELFSHT_HIPROC		0x7FFFFFFF
#define ELFSHT_LOUSER		0x80000000
#define ELFSHT_HIUSER		0xFFFFFFFF

// ELF special section indexes
#define ELFSHN_UNDEF		0
#define ELFSHN_LORESERVE	0xFF00
#define ELFSHN_LOPROC		0xFF00
#define ELFSHN_HIPROC		0xFF1F
#define ELFSHN_ABS			0xFFF1
#define ELFSHN_COMMON		0xFFF2
#define ELFSHN_HIRESERVE	0xFFFF

// Program header segment types
#define ELFPT_NULL			0
#define ELFPT_LOAD			1
#define ELFPT_DYNAMIC		2
#define ELFPT_INTERP		3
#define ELFPT_NOTE			4
#define ELFPT_SHLIB			5
#define ELFPT_PHDR			6
#define ELFPT_LOPROC		0x70000000
#define ELFPT_HIPROC		0x7FFFFFFF

// Program header segment flags
#define ELFPF_R				0x4
#define ELFPF_W				0x2
#define ELFPF_X				0x1

// ELF symbol binding types
#define ELFSTB_LOCAL		0
#define ELFSTB_GLOBAL		1
#define ELFSTB_WEAK			2
#define ELFSTB_LOPROC		13
#define ELFSTB_HIPROC		15

// Flag values from the 'info' field of an ELF symbol structure
#define ELFSTT_NOTYPE		0
#define ELFSTT_OBJECT		1
#define ELFSTT_FUNC			2
#define ELFSTT_SECTION		3
#define ELFSTT_FILE			4
#define ELFSTT_LOPROC		13
#define ELFSTT_HIPROC		15

// ELF 'dynamic' section tag values
#define ELFDT_NULL			0
#define ELFDT_NEEDED		1
#define ELFDT_PLTRELSZ		2
#define ELFDT_PLTGOT		3
#define ELFDT_HASH			4
#define ELFDT_STRTAB		5
#define ELFDT_SYMTAB		6
#define ELFDT_RELA			7
#define ELFDT_RELASZ		8
#define ELFDT_RELAENT		9
#define ELFDT_STRSZ			10
#define ELFDT_SYMENT		11
#define ELFDT_INIT			12
#define ELFDT_FINI			13
#define ELFDT_SONAME		14
#define ELFDT_RPATH			15
#define ELFDT_SYMBOLIC		16
#define ELFDT_REL			17
#define ELFDT_RELSZ			18
#define ELFDT_RELENT		19
#define ELFDT_PLTREL		20
#define ELFDT_DEBUG			21
#define ELFDT_TEXTREL		22
#define ELFDT_JMPREL		23
#define ELFDT_LOPROC		0x70000000
#define ELFDT_HIPROC		0x7FFFFFFF

// ELF relocation types
#define ELFR_386_NONE		0
#define ELFR_386_32			1
#define ELFR_386_PC32		2
#define ELFR_386_GOT32		3
#define ELFR_386_PLT32		4
#define ELFR_386_COPY		5
#define ELFR_386_GLOB_DAT	6
#define ELFR_386_JMP_SLOT	7
#define ELFR_386_RELATIVE	8
#define ELFR_386_GOTOFF		9
#define ELFR_386_GOTPC		10

// Macros for the 'info' field of an ELF symbol structure
#define ELF32_ST_BIND(i)	((i) >> 4)
#define ELF32_ST_TYPE(i)	((i) & 0x0F)

// Macros for the 'info' field of an ELF relocation structure
#define ELF32_R_SYM(i)		((i) >> 8)
#define ELF32_R_TYPE(i)		((i) & 0xFF)

// Types
typedef void *			Elf32_Addr;
typedef short			Elf32_Half;
typedef unsigned char	Elf32_Byte;
typedef unsigned		Elf32_Off;
typedef int				Elf32_Sword;
typedef int				Elf32_Word;

// The ELF header
typedef struct {
	Elf32_Byte e_magic[4];
	Elf32_Byte e_class;
	Elf32_Byte e_byteorder;
	Elf32_Byte e_hversion;
	Elf32_Byte e_pad[9];
	Elf32_Half e_type;
	Elf32_Half e_machine;
	Elf32_Word e_version;
	Elf32_Addr e_entry;
	Elf32_Off e_phoff;
	Elf32_Off e_shoff;
	Elf32_Word e_flags;
	Elf32_Half e_ehsize;
	Elf32_Half e_phentsize;
	Elf32_Half e_phnum;
	Elf32_Half e_shentsize;
	Elf32_Half e_shnum;
	Elf32_Half e_shstrndx;

} __attribute__((packed)) Elf32Header;

// ELF section header
typedef struct {
	Elf32_Word sh_name;
	Elf32_Word sh_type;
	Elf32_Word sh_flags;
	Elf32_Addr sh_addr;
	Elf32_Off sh_offset;
	Elf32_Word sh_size;
	Elf32_Word sh_link;
	Elf32_Word sh_info;
	Elf32_Word sh_addralign;
	Elf32_Word sh_entsize;

} __attribute__((packed)) Elf32SectionHeader;

// ELF symbol table entry
typedef struct {
	Elf32_Word st_name;
	Elf32_Addr st_value;
	Elf32_Word st_size;
	Elf32_Byte st_info;
	Elf32_Byte st_other;
	Elf32_Half st_shndx;

} __attribute__((packed)) Elf32Symbol;

// ELF relocation entry
typedef struct {
	Elf32_Addr r_offset;
	Elf32_Word r_info;

} __attribute__((packed)) Elf32Rel;

// ELF relocation entry with explicit addend
typedef struct {
	Elf32_Addr r_offset;
	Elf32_Word r_info;
	Elf32_Sword r_addend;

} __attribute__((packed)) Elf32Rela;

// ELF dynamic entry
typedef struct {
	Elf32_Sword d_tag;
	union {
		Elf32_Word d_val;
		Elf32_Addr d_ptr;
	} d_un;

} __attribute__((packed)) Elf32Dyn;

// ELF program header
typedef struct {
	Elf32_Word p_type;
	Elf32_Off p_offset;
	Elf32_Addr p_vaddr;
	Elf32_Addr p_paddr;
	Elf32_Word p_filesz;
	Elf32_Word p_memsz;
	Elf32_Word p_flags;
	Elf32_Word p_align;

} __attribute__((packed)) Elf32ProgramHeader;

// To keep arrays of dynamic library dependencies that we can modify
typedef struct {
	int numLibraries;
	kernelDynamicLibrary *libraries;

} elfLibraryArray;

#define _KERNELLOADERELF_H
#endif

