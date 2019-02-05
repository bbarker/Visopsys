//
//  Visopsys
//  Copyright (C) 1998-2018 J. Andrew McLaughlin
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
//  gpt.h
//

// This is the header for the handling of GPT disk labels

#if !defined(_GPT_H)

#include <sys/guid.h>
#include <sys/types.h>

#define GPT_SIG					"EFI PART"
#define GPT_HEADERBYTES			92

// GPT entry attribute flags
#define GPT_ENTRYATTR_RES		0xFFFF000000000000
#define GPT_ENTRYATTR_UNDEF		0x0000FFFFFFFFFFF8
#define GPT_ENTRYATTR_LEGBOOT	0x0000000000000004
#define GPT_ENTRYATTR_NOBLKIO	0x0000000000000002
#define GPT_ENTRYATTR_REQ		0x0000000000000001

// EFI GPT filesystem GUIDs

#define GUID_MBRPART      ((guid){ 0x024DEE41, 0x33E7, 0x11D3, 0x9D, 0x69, \
	{ 0x00, 0x08, 0xC7, 0x81, 0xF3, 0x9F } })
#define GUID_MBRPART_DESC "MBR partition scheme"

#define GUID_EFISYS       ((guid){ 0xC12A7328, 0xF81F, 0x11D2, 0xBA, 0x4B, \
	{ 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B } })
#define GUID_EFISYS_DESC "EFI System partition"

#define GUID_BIOSBOOT     ((guid){ 0x21686148, 0x6449, 0x6E6F, 0x74, 0x4E,\
	{ 0x65, 0x65, 0x64, 0x45, 0x46, 0x49 } })
#define GUID_BIOSBOOT_DESC "BIOS Boot partition"

#define GUID_MSRES        ((guid){ 0xE3C9E316, 0x0B5C, 0x4DB8, 0x81, 0x7D, \
	{ 0xF9, 0x2D, 0xF0, 0x02, 0x15, 0xAE } })
#define GUID_MSRES_DESC "Microsoft Reserved"

#define GUID_WINDATA      ((guid){ 0xEBD0A0A2, 0xB9E5, 0x4433, 0x87, 0xC0, \
	{ 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7 } })
#define GUID_WINDATA_DESC "Windows data"

#define GUID_WINLDMMETA   ((guid){ 0x5808C8AA, 0x7E8F, 0x42E0, 0x85, 0xD2, \
	{ 0xE1, 0xE9, 0x04, 0x34, 0xCF, 0xB3 } })
#define GUID_WINLDMMETA_DESC "Windows LDM metadata"

#define GUID_WINLDMDATA   ((guid){ 0xAF9B60A0, 0x1431, 0x4F62, 0xBC, 0x68, \
	{ 0x33, 0x11, 0x71, 0x4A, 0x69, 0xAD } })
#define GUID_WINLDMDATA_DESC "Windows LDM data"

#define GUID_WINRECOVER   ((guid){ 0xDE94BBA4, 0x06D1, 0x4D40, 0xA1, 0x6A, \
	{ 0xBF, 0xD5, 0x01, 0x79, 0xD6, 0xAC } })
#define GUID_WINRECOVER_DESC "Windows recovery"

#define GUID_IMBGPFS      ((guid){ 0x37AFFC90, 0xEF7D, 0x4E96, 0x91, 0xC3, \
	{ 0x2D, 0x7A, 0xE0, 0x55, 0xB1, 0x74 } })
#define GUID_IMBGPFS_DESC "IBM GPFS"

#define GUID_HPUXDATA     ((guid){ 0x75894C1E, 0x3AEB, 0x11D3, 0xB7, 0xC1, \
	{ 0x7B, 0x03, 0xA0, 0x00, 0x00, 0x00 } })
#define GUID_HPUXDATA_DESC "HP-UX data"

#define GUID_HPUXSERV     ((guid){ 0xE2A1E728, 0x32E3, 0x11D6, 0xA6, 0x82, \
	{ 0x7B, 0x03, 0xA0, 0x00, 0x00, 0x00 } })
#define GUID_HPUXSERV_DESC "HP-UX service"

#define GUID_LINUXDATA    ((guid){ 0x0FC63DAF, 0x8483, 0x4772, 0x8E, 0x79, \
	{ 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4 } })
#define GUID_LINUXDATA_DESC "Linux data"

#define GUID_LINUXRAID    ((guid){ 0xA19D880F, 0x05FC, 0x4D3B, 0xA0, 0x06, \
	{ 0x74, 0x3F, 0x0F, 0x84, 0x91, 0x1E } })
#define GUID_LINUXRAID_DESC "Linux RAID"

#define GUID_LINUXSWAP    ((guid){ 0x0657FD6D, 0xA4AB, 0x43C4, 0x84, 0xE5, \
	{ 0x09, 0x33, 0xC8, 0x4B, 0x4F, 0x4F } })
#define GUID_LINUXSWAP_DESC "Linux swap"

#define GUID_LINUXLVM     ((guid){ 0xE6D6D379, 0xF507, 0x44C2, 0xA2, 0x3C, \
	{ 0x23, 0x8F, 0x2A, 0x3D, 0xF9, 0x28 } })
#define GUID_LINUXLVM_DESC "Linux LVM"

#define GUID_LINUXRES     ((guid){ 0x8DA63339, 0x0007, 0x60C0, 0xC4, 0x36, \
	{ 0x08, 0x3A, 0xC8, 0x23, 0x09, 0x08 } })
#define GUID_LINUXRES_DESC "Linux reserved"

#define GUID_FREEBSDBOOT  ((guid){ 0x83BD6B9D, 0x7F41, 0x11DC, 0xBE, 0x0B, \
	{ 0x00, 0x15, 0x60, 0xB8, 0x4F, 0x0F } })
#define GUID_FREEBSDBOOT_DESC "FreeBSD boot"

#define GUID_FREEBSDDATA  ((guid){ 0x516E7CB4, 0x6ECF, 0x11D6, 0x8F, 0xF8, \
	{ 0x00, 0x02, 0x2D, 0x09, 0x71, 0x2B } })
#define GUID_FREEBSDDATA_DESC "FreeBSD data"

#define GUID_FREEBSDSWAP  ((guid){ 0x516E7CB5, 0x6ECF, 0x11D6, 0x8F, 0xF8, \
	{ 0x00, 0x02, 0x2D, 0x09, 0x71, 0x2B } })
#define GUID_FREEBSDSWAP_DESC "FreeBSD swap"

#define GUID_FREEBSDUFS   ((guid){ 0x516E7CB6, 0x6ECF, 0x11D6, 0x8F, 0xF8, \
	{ 0x00, 0x02, 0x2D, 0x09, 0x71, 0x2B } })
#define GUID_FREEBSDUFS_DESC "FreeBSD Unix UFS"

#define GUID_FREEBSDVIN   ((guid){ 0x516E7CB8, 0x6ECF, 0x11D6, 0x8F, 0xF8, \
	{ 0x00, 0x02, 0x2D, 0x09, 0x71, 0x2B } })
#define GUID_FREEBSDVIN_DESC "FreeBSD Vinum"

#define GUID_FREEBSDZFS   ((guid){ 0x516E7CBA, 0x6ECF, 0x11D6, 0x8F, 0xF8, \
	{ 0x00, 0x02, 0x2D, 0x09, 0x71, 0x2B } })
#define GUID_FREEBSDZFS_DESC "FreeBSD ZFS"

#define GUID_MACOSXHFS    ((guid){ 0x48465300, 0x0000, 0x11AA, 0xAA, 0x11, \
	{ 0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC } })
#define GUID_MACOSXHFS_DESC "MacOS X HFS+"

#define GUID_APPLEUFS     ((guid){ 0x55465300, 0x0000, 0x11AA, 0xAA, 0x11, \
	{ 0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC } })
#define GUID_APPLEUFS_DESC "Apple UFS"

#define GUID_APPLERAID    ((guid){ 0x52414944, 0x0000, 0x11AA, 0xAA, 0x11, \
	{ 0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC } })
#define GUID_APPLERAID_DESC "Apple RAID"

#define GUID_APPLERDOFFL  ((guid){ 0x52414944, 0x5F4F, 0x11AA, 0xAA, 0x11, \
	{ 0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC } })
#define GUID_APPLERDOFFL_DESC "Apple RAID offline"

#define GUID_APPLEBOOT    ((guid){ 0x426F6F74, 0x0000, 0x11AA, 0xAA, 0x11, \
	{ 0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC } })
#define GUID_APPLEBOOT_DESC "Apple Boot"

#define GUID_APPLELABEL   ((guid){ 0x4C616265, 0x6C00, 0x11AA, 0xAA, 0x11, \
	{ 0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC } })
#define GUID_APPLELABEL_DESC "Apple label"

#define GUID_APPLETVRECV  ((guid){ 0x5265636F, 0x7665, 0x11AA, 0xAA, 0x11, \
	{ 0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC } })
#define GUID_APPLETVRECV_DESC "Apple TV recovery"

#define GUID_APPLECOREST  ((guid){ 0x53746F72, 0x6167, 0x11AA, 0xAA, 0x11, \
	{ 0x00, 0x30, 0x65, 0x43, 0xEC, 0xAC } })
#define GUID_APPLECOREST_DESC "Apple core storage"

#define GUID_SOLBOOT      ((guid){ 0x6A82CB45, 0x1DD2, 0x11B2, 0x99, 0xA6, \
	{ 0x08, 0x00, 0x20, 0x73, 0x66, 0x31 } })
#define GUID_SOLBOOT_DESC "Solaris boot"

#define GUID_SOLROOT      ((guid){ 0x6A85CF4D, 0x1DD2, 0x11B2, 0x99, 0xA6, \
	{ 0x08, 0x00, 0x20, 0x73, 0x66, 0x31 } })
#define GUID_SOLROOT_DESC "Solaris root"

#define GUID_SOLSWAP      ((guid){ 0x6A87C46F, 0x1DD2, 0x11B2, 0x99, 0xA6, \
	{ 0x08, 0x00, 0x20, 0x73, 0x66, 0x31 } })
#define GUID_SOLSWAP_DESC "Solaris swap"

#define GUID_SOLBACKUP    ((guid){ 0x6A8B642B, 0x1DD2, 0x11B2, 0x99, 0xA6, \
	{ 0x08, 0x00, 0x20, 0x73, 0x66, 0x31 } })
#define GUID_SOLBACKUP_DESC "Solaris backup"

#define GUID_SOLUSR       ((guid){ 0x6A898CC3, 0x1DD2, 0x11B2, 0x99, 0xA6, \
	{ 0x08, 0x00, 0x20, 0x73, 0x66, 0x31 } })
#define GUID_SOLUSR_DESC "Solaris /usr"

#define GUID_SOLVAR       ((guid){ 0x6A8EF2E9, 0x1DD2, 0x11B2, 0x99, 0xA6, \
	{ 0x08, 0x00, 0x20, 0x73, 0x66, 0x31 } })
#define GUID_SOLVAR_DESC "Solaris /var"

#define GUID_SOLHOME      ((guid){ 0x6A90BA39, 0x1DD2, 0x11B2, 0x99, 0xA6, \
	{ 0x08, 0x00, 0x20, 0x73, 0x66, 0x31 } })
#define GUID_SOLHOME_DESC "Solaris /home"

#define GUID_SOLALTSECT   ((guid){ 0x6A9283A5, 0x1DD2, 0x11B2, 0x99, 0xA6, \
	{ 0x08, 0x00, 0x20, 0x73, 0x66, 0x31 } })
#define GUID_SOLALTSECT_DESC "Solaris alternate sector"

#define GUID_SOLRES1      ((guid){ 0x6A945A3B, 0x1DD2, 0x11B2, 0x99, 0xA6, \
	{ 0x08, 0x00, 0x20, 0x73, 0x66, 0x31 } })
#define GUID_SOLRES1_DESC "Solaris reserved"

#define GUID_SOLRES2      ((guid){ 0x6A9630D1, 0x1DD2, 0x11B2, 0x99, 0xA6, \
	{ 0x08, 0x00, 0x20, 0x73, 0x66, 0x31 } })
#define GUID_SOLRES2_DESC "Solaris reserved"

#define GUID_SOLRES3      ((guid){ 0x6A980767, 0x1DD2, 0x11B2, 0x99, 0xA6, \
	{ 0x08, 0x00, 0x20, 0x73, 0x66, 0x31 } })
#define GUID_SOLRES3_DESC "Solaris reserved"

#define GUID_SOLRES4      ((guid){ 0x6A96237F, 0x1DD2, 0x11B2, 0x99, 0xA6, \
	{ 0x08, 0x00, 0x20, 0x73, 0x66, 0x31 } })
#define GUID_SOLRES4_DESC "Solaris reserved"

#define GUID_SOLRES5      ((guid){ 0x6A8D2AC7, 0x1DD2, 0x11B2, 0x99, 0xA6, \
	{ 0x08, 0x00, 0x20, 0x73, 0x66, 0x31 } })
#define GUID_SOLRES5_DESC "Solaris reserved"

#define GUID_NETBSDSWAP   ((guid){ 0x49F48D32, 0xB10E, 0x11DC, 0xB9, 0x9B, \
	{ 0x00, 0x19, 0xD1, 0x87, 0x96, 0x48 } })
#define GUID_NETBSDSWAP_DESC "NetBSD swap"

#define GUID_NETBSDFFS    ((guid){ 0x49F48D5A, 0xB10E, 0x11DC, 0xB9, 0x9B, \
	{ 0x00, 0x19, 0xD1, 0x87, 0x96, 0x48 } })
#define GUID_NETBSDFFS_DESC "NetBSD FFS"

#define GUID_NETBSDLFS    ((guid){ 0x49F48D82, 0xB10E, 0x11DC, 0xB9, 0x9B, \
	{ 0x00, 0x19, 0xD1, 0x87, 0x96, 0x48 } })
#define GUID_NETBSDLFS_DESC "NetBSD LFS"

#define GUID_NETBSDRAID   ((guid){ 0x49F48DAA, 0xB10E, 0x11DC, 0xB9, 0x9B, \
	{ 0x00, 0x19, 0xD1, 0x87, 0x96, 0x48 } })
#define GUID_NETBSDRAID_DESC "NetBSD RAID"

#define GUID_NETBSDCONCT  ((guid){ 0x2DB519C4, 0xB10F, 0x11DC, 0xB9, 0x9B, \
	{ 0x00, 0x19, 0xD1, 0x87, 0x96, 0x48 } })
#define GUID_NETBSDCONCT_DESC "NetBSD concatenated"

#define GUID_NETBSDENCR   ((guid){ 0x2DB519EC, 0xB10F, 0x11DC, 0xB9, 0x9B, \
	{ 0x00, 0x19, 0xD1, 0x87, 0x96, 0x48 } })
#define GUID_NETBSDENCR_DESC "NetBSD encrypted"

#define GUID_CHROMEKERN   ((guid){ 0xFE3A2A5D, 0x4F32, 0x41A7, 0xB7, 0x25, \
	{ 0xAC, 0xCC, 0x32, 0x85, 0xA3, 0x09 } })
#define GUID_CHROMEKERN_DESC "ChromeOS kernel"

#define GUID_CHROMEROOT   ((guid){ 0x3CB8E202, 0x3B7E, 0x47DD, 0x8A, 0x3C, \
	{ 0x7F, 0xF2, 0xA1, 0x3C, 0xFC, 0xEC } })
#define GUID_CHROMEROOT_DESC "ChromeOS rootfs"

#define GUID_CHROMEFUT    ((guid){ 0x2E0A753D, 0x9E48, 0x43B0, 0x83, 0x37, \
	{ 0xB1, 0x51, 0x92, 0xCB, 0x1B, 0x5E } })
#define GUID_CHROMEFUT_DESC "ChromeOS future use"

#define GUID_UNUSED       ((guid){ 0, 0, 0, 0, 0, { 0, 0, 0, 0, 0, 0 } })
#define GUID_UNUSED_DESC "unused"

// The header for the disk label
typedef struct {
	char signature[8];
	unsigned revision;
	unsigned headerBytes;
	unsigned headerCRC32;
	unsigned reserved1;
	uquad_t myLBA;
	uquad_t altLBA;
	uquad_t firstUsableLBA;
	uquad_t lastUsableLBA;
	guid diskGUID;
	uquad_t partEntriesLBA;
	unsigned numPartEntries;
	unsigned partEntryBytes;
	unsigned partEntriesCRC32;
	char reserved2[512 - GPT_HEADERBYTES];

} __attribute__((packed)) gptHeader;

// An individual partition entry
typedef struct {
	guid typeGuid;
	guid partGuid;
	uquad_t startingLBA;
	uquad_t endingLBA;
	uquad_t attributes;
	char partName[72];

} __attribute__((packed)) gptEntry;

#define _GPT_H
#endif

