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
//  kernelAtaDriver.h
//

// This header file contains definitions for the ATA family of disk drivers

#if !defined(_KERNELATADRIVER_H)

#include <string.h>

#define ATAPI_SECTORSIZE		2048

// ATA commands
#define ATA_NOP					0x00
#define ATA_ATAPIRESET			0x08
//#define ATA_RECALIBRATE		0x10	// Obsolete
#define ATA_READSECTS			0x20
//#define ATA_READECC			0x22	// Obsolete
#define ATA_READSECTS_EXT		0x24
#define ATA_READDMA_EXT			0x25
#define ATA_READMULTI_EXT		0x29
#define ATA_WRITESECTS			0x30
//#define ATA_WRITEECC			0x32	// Obsolete
#define ATA_WRITESECTS_EXT		0x34
#define ATA_WRITEDMA_EXT		0x35
#define ATA_WRITEMULTI_EXT		0x39
#define ATA_VERIFYMULTI			0x40
//#define ATA_FORMATTRACK		0x50	// Obsolete
//#define ATA_SEEK				0x70	// Obsolete
#define ATA_DIAG				0x90
//#define ATA_INITPARAMS		0x91	// Reserved
#define ATA_ATAPIPACKET			0xA0
#define ATA_ATAPIIDENTIFY		0xA1
#define ATA_ATAPISERVICE		0xA2
#define ATA_READMULTI			0xC4
#define ATA_WRITEMULTI			0xC5
#define ATA_SETMULTIMODE		0xC6
#define ATA_READDMA				0xC8
#define ATA_WRITEDMA			0xCA
#define ATA_MEDIASTATUS			0xDA	// Obsolete from ATA 8
#define ATA_FLUSHCACHE			0xE7
#define ATA_FLUSHCACHE_EXT		0xEA
#define ATA_IDENTIFY			0xEC
#define ATA_SETFEATURES			0xEF

// ATAPI commands
#define ATAPI_TESTREADY			0x00
#define ATAPI_REQUESTSENSE		0x03
#define ATAPI_INQUIRY			0x12
#define ATAPI_STARTSTOP			0x1B
#define ATAPI_PERMITREMOVAL		0x1E
#define ATAPI_READCAPACITY		0x25
#define ATAPI_READ10			0x28
#define ATAPI_SEEK				0x2B
#define ATAPI_READSUBCHAN		0x42
#define ATAPI_READTOC			0x43
#define ATAPI_READHEADER		0x44
#define ATAPI_PLAYAUDIO			0x45
#define ATAPI_PLAYAUDIOMSF		0x47
#define ATAPI_PAUSERESUME		0x4B
#define ATAPI_STOPPLAYSCAN		0x4E
#define ATAPI_MODESELECT		0x55
#define ATAPI_MODESENSE			0x5A
#define ATAPI_LOADUNLOAD		0xA6
#define ATAPI_READ12			0xA8
#define ATAPI_SCAN				0xBA
#define ATAPI_SETCDSPEED		0xBB
#define ATAPI_PLAYCD			0xBC
#define ATAPI_MECHSTATUS		0xBD
#define ATAPI_READCD			0xBE
#define ATAPI_READCDMSF			0xB9

// Status register bits
#define ATA_STAT_BSY			0x80	// Busy
#define ATA_STAT_DRDY			0x40	// Device ready
#define ATA_STAT_DF				0x20	// Device fault
#define ATA_STAT_DSC			0x10	// Device seek complete
#define ATA_STAT_DRQ			0x08	// Data request
#define ATA_STAT_CORR			0x04	// Corrected data
#define ATA_STAT_IDX			0x02	// Index (vendor specific)
#define ATA_STAT_ERR			0x01	// Error

// Error register bits
#define ATA_ERR_UNC				0x40	// Uncorrectable data error
#define ATA_ERR_MC				0x20	// Media changed
#define ATA_ERR_IDNF			0x10	// ID not found
#define ATA_ERR_MCR				0x08	// Media change requested
#define ATA_ERR_ABRT			0x04	// Aborted command
#define ATA_ERR_TKNONF			0x02	// Track 0 not found
#define ATA_ERR_AMNF			0x01	// Address mark not found

// ATA feature flags.  These don't represent all possible features; just the
// ones we [plan to] support.
#define ATA_FEATURE_48BIT		0x80
#define ATA_FEATURE_MEDSTAT		0x40
#define ATA_FEATURE_WCACHE		0x20
#define ATA_FEATURE_RCACHE		0x10
#define ATA_FEATURE_SMART		0x08
#define ATA_FEATURE_DMA			(ATA_FEATURE_UDMA | ATA_FEATURE_MWDMA)
#define ATA_FEATURE_UDMA		0x04
#define ATA_FEATURE_MWDMA		0x02
#define ATA_FEATURE_MULTI		0x01

// ATA transfer modes.
#define ATA_TRANSMODE_UDMA6		0x46
#define ATA_TRANSMODE_UDMA5		0x45
#define ATA_TRANSMODE_UDMA4		0x44
#define ATA_TRANSMODE_UDMA3		0x43
#define ATA_TRANSMODE_UDMA2		0x42
#define ATA_TRANSMODE_UDMA1		0x41
#define ATA_TRANSMODE_UDMA0		0x40
#define ATA_TRANSMODE_DMA2		0x22
#define ATA_TRANSMODE_DMA1		0x21
#define ATA_TRANSMODE_DMA0		0x20
#define ATA_TRANSMODE_PIO		0x00

// 'Identify device' data as of ATA-ATAPI 7
typedef union {
	struct {
		unsigned short generalConfig;	// 0x00		| word 0
		unsigned short cylinders;		// 0x02		| word 1		| < ATA 6
		unsigned short specificConfig;	// 0x04		| word 2
		unsigned short heads;			// 0x06		| word 3		| < ATA 6
		unsigned short retired1[2];		// 0x08		| word 4-5
		unsigned short sectsPerCyl;		// 0x0C		| word 6		| < ATA 6
		unsigned short reserved1[2];	// 0x0E		| word 7-8
		unsigned short retired2;		// 0x12		| word 9
		unsigned short serial[10];		// 0x14		| word 10-19
		unsigned short retired3[2];		// 0x28		| word 20-21
		unsigned short obsolete1;		// 0x2C		| word 22
		unsigned short firmwareRev[4];	// 0x2E		| word 23-26
		unsigned short modelNum[20];	// 0x36		| word 27-46
		unsigned short maxMulti;		// 0x5E		| word 47
		unsigned short reserved2;		// 0x60		| word 48
		unsigned short capabilities1;	// 0x62		| word 49
		unsigned short capabilities2;	// 0x64		| word 50
		unsigned short obsolete2[2];	// 0x66		| word 51-52
		unsigned short validFields;		// 0x6A		| word 53
		unsigned short obsolete3[5];	// 0x6C		| word 54-58
		unsigned short multiSector;		// 0x76		| word 59
		unsigned totalSectors;			// 0x78		| word 60-61
		unsigned short obsolete4;		// 0x7C		| word 62
		unsigned short multiDmaModes;	// 0x7E		| word 63
		unsigned short pioModes;		// 0x80		| word 64
		unsigned short minDmaTime;		// 0x82		| word 65
		unsigned short recDmaTime;		// 0x84		| word 66
		unsigned short minPioTimeNoFlo;	// 0x86		| word 67
		unsigned short minPioTimeIordy;	// 0x88		| word 68
		unsigned short reserved3[2];	// 0x8A		| word 69-70
		unsigned short reserved4[4];	// 0x8E		| word 71-74
		unsigned short queueDepth;		// 0x96		| word 75		| SATA
		unsigned short sataCaps;		// 0x98		| word 76		| SATA
		unsigned short reserved5;		// 0x9A		| word 77		| SATA
		unsigned short sataFeatSupp;	// 0x9C		| word 78		| SATA
		unsigned short sataFeatEnab;	// 0x9E		| word 79		| SATA
		unsigned short majorVersion;	// 0xA0		| word 80
		unsigned short minorVersion;	// 0xA2		| word 81
		unsigned short cmdSetSupp1;		// 0xA4		| word 82
		unsigned short cmdSetSupp2;		// 0xA6		| word 83
		unsigned short cmdFeatSetSupp;	// 0xA8		| word 84
		unsigned short cmdFeatSetEnab1;	// 0xAA		| word 85
		unsigned short cmdFeatSetEnab2;	// 0xAC		| word 86
		unsigned short cmdFeatSetDef;	// 0xAE		| word 87
		unsigned short udmaModes;		// 0xB0		| word 88
		unsigned short secEraseTime;	// 0xB2		| word 89
		unsigned short enSecEraseTime;	// 0xB4		| word 90
		unsigned short currApmValue;	// 0xB6		| word 91
		unsigned short masterPassRev;	// 0xB8		| word 92
		unsigned short hardResetResult;	// 0xBA		| word 93
		unsigned short acoustMgmtValue;	// 0xBC		| word 94
		unsigned short strmMinReqSz;	// 0xBE		| word 95
		unsigned short strmXferTimeDma;	// 0xC0		| word 96
		unsigned short strmAccLatency;	// 0xC2		| word 97
		unsigned strmPerfGran;			// 0xC4		| word 98-99
		unsigned long long maxLba48;	// 0xC8		| word 100-103
		unsigned short strmXferTimePio;	// 0xD0		| word 104		| ATA 7
		unsigned short reserved6;		// 0xD2		| word 105		| ATA 7
		unsigned short physLogSectSize;	// 0xD4		| word 106		| ATA 7
		unsigned short intrSeekDelay;	// 0xD6		| word 107		| ATA 7
		unsigned short naaIeeeHi;		// 0xD8		| word 108		| ATA 7
		unsigned short ieeeLoUniqIdHi;	// 0xDA		| word 109		| ATA 7
		unsigned short uniqIdMid;		// 0xDC		| word 110		| ATA 7
		unsigned short uniqIdHi;		// 0xDE		| word 111		| ATA 7
		unsigned short reserved7[4];	// 0xE0		| word 112-115	| ATA 7
		unsigned short reserved8;		// 0xE8		| word 116		| ATA 7
		unsigned wordsPerLogSect;		// 0xEA		| word 117-118	| ATA 7
		unsigned short reserved9[8];	// 0xEE		| word 119-126	| ATA 7
		unsigned short remNotFeatSupp;	// 0xFE		| word 127
		unsigned short securityStatus;	// 0x100	| word 128
		unsigned short vendorSpec[31];	// 0x102	| word 129-159
		unsigned short cfaPowerMode1;	// 0x140	| word 160
		unsigned short reserved10[15];	// 0x142	| word 161-175
		unsigned short mediaSerial[30];	// 0x160	| word 176-205
		unsigned short reserved11[49];	// 0x19C	| word 206-254
		unsigned short integrity;		// 0x1FE	| word 255

	} __attribute__((packed)) field;
	unsigned short word[256];

} __attribute__((packed)) ataIdentifyData;

typedef struct {
	unsigned blockNumber;
	unsigned blockLength;

} __attribute__((packed)) atapiCapacityData;

typedef struct {
	unsigned short length;
	unsigned char firstSession;
	unsigned char lastSession;
	unsigned char res1;
	unsigned char adrControl;
	unsigned char firstTrackLastSession;
	unsigned char res2;
	unsigned lastSessionLba;

}  __attribute__((packed)) atapiTocData; // With format field = 01b


typedef struct {
	unsigned char error;
	unsigned char segNum;
	unsigned char senseKey;
	unsigned info;
	unsigned char addlLength;
	unsigned commandSpecInfo;
	unsigned char addlSenseCode;
	unsigned char addlSenseCodeQual;
	unsigned char unitCode;
	unsigned char senseKeySpec[3];
	unsigned char addlSenseBytes[];

} __attribute__((packed)) atapiSenseData;

typedef enum {
	ata_unknown, ata_nondata, ata_pio, ata_dma, ata_pio_or_dma

} ataCommandType;

typedef struct {
	char *name;
	unsigned char identWord;
	unsigned short suppMask;
	unsigned char featureCode;
	unsigned char enabledWord;
	unsigned short enabledMask;
	int featureFlag;

} ataFeature;

typedef struct {
	char *name;
	unsigned char val;
	unsigned char identWord;
	unsigned short suppMask;
	unsigned short enabledMask;
	int featureFlag;

} ataDmaMode;

// Some predefined ATAPI packets
#define ATAPI_PACKET_UNLOCK \
	((unsigned char[]){ ATAPI_PERMITREMOVAL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } )
#define ATAPI_PACKET_LOCK \
	((unsigned char[]){ ATAPI_PERMITREMOVAL, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 } )
#define ATAPI_PACKET_STOP \
	((unsigned char[]){ ATAPI_STARTSTOP, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } )
#define ATAPI_PACKET_START \
	((unsigned char[]){ ATAPI_STARTSTOP, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 } )
#define ATAPI_PACKET_EJECT \
	((unsigned char[]){ ATAPI_STARTSTOP, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0 } )
#define ATAPI_PACKET_CLOSE \
	((unsigned char[]){ ATAPI_STARTSTOP, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0 } )
#define ATAPI_PACKET_READCAPACITY \
	((unsigned char[]){ ATAPI_READCAPACITY, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } )
#define ATAPI_PACKET_READTOC \
	((unsigned char[]){ ATAPI_READTOC, 0, 1, 0, 0, 0, 0, 0, 12, 0x40, 0, 0 } )

static inline void ataError2String(int error, char *string)
{
	string[0] = '\0';

	if (error & ATA_ERR_UNC)
	{
		strcat(string, "uncorrectable data error");
	}
	if (error & ATA_ERR_MC)
	{
		if (strlen(string))
			strcat(string, ", ");
		strcat(string, "media changed");
	}
	if (error & ATA_ERR_IDNF)
	{
		if (strlen(string))
			strcat(string, ", ");
		strcat(string, "ID or target sector not found");
	}
	if (error & ATA_ERR_MCR)
	{
		if (strlen(string))
			strcat(string, ", ");
		strcat(string, "media change requested");
	}
	if (error & ATA_ERR_ABRT)
	{
		if (strlen(string))
			strcat(string, ", ");
		strcat(string, "command aborted - invalid command");
	}
	if (error & ATA_ERR_TKNONF)
	{
		if (strlen(string))
			strcat(string, ", ");
		strcat(string, "track 0 not found");
	}
	if (error & ATA_ERR_AMNF)
	{
		if (strlen(string))
			strcat(string, ", ");
		strcat(string, "address mark not found");
	}
	if (!strlen(string))
		strcat(string, "unknown error");
}

static inline const char *atapiCommand2String(int command)
{
	switch (command)
	{
		case ATAPI_TESTREADY: return "ATAPI_TESTREADY";
		case ATAPI_REQUESTSENSE: return "ATAPI_REQUESTSENSE";
		case ATAPI_INQUIRY: return "ATAPI_INQUIRY";
		case ATAPI_STARTSTOP: return "ATAPI_STARTSTOP";
		case ATAPI_PERMITREMOVAL: return "ATAPI_PERMITREMOVAL";
		case ATAPI_READCAPACITY: return "ATAPI_READCAPACITY";
		case ATAPI_READ10: return "ATAPI_READ10";
		case ATAPI_SEEK: return "ATAPI_SEEK";
		case ATAPI_READSUBCHAN: return "ATAPI_READSUBCHAN";
		case ATAPI_READTOC: return "ATAPI_READTOC";
		case ATAPI_READHEADER: return "ATAPI_READHEADER";
		case ATAPI_PLAYAUDIO: return "ATAPI_PLAYAUDIO";
		case ATAPI_PLAYAUDIOMSF: return "ATAPI_PLAYAUDIOMSF";
		case ATAPI_PAUSERESUME: return "ATAPI_PAUSERESUME";
		case ATAPI_STOPPLAYSCAN: return "ATAPI_STOPPLAYSCAN";
		case ATAPI_MODESELECT: return "ATAPI_MODESELECT";
		case ATAPI_MODESENSE: return "ATAPI_MODESENSE";
		case ATAPI_LOADUNLOAD: return "ATAPI_LOADUNLOAD";
		case ATAPI_READ12: return "ATAPI_READ12";
		case ATAPI_SCAN: return "ATAPI_SCAN";
		case ATAPI_SETCDSPEED: return "ATAPI_SETCDSPEED";
		case ATAPI_PLAYCD: return "ATAPI_PLAYCD";
		case ATAPI_MECHSTATUS: return "ATAPI_MECHSTATUS";
		case ATAPI_READCD: return "ATAPI_READCD";
		case ATAPI_READCDMSF: return "ATAPI_READCDMSF";
		default: return "unknown command"; break;
	}
}

// Functions exported from kernelAtaDriver.c
int kernelAtaCommandIsAtapi(unsigned char);
ataCommandType kernelAtaCommandType(unsigned char);
ataFeature *kernelAtaGetFeatures(void);
ataDmaMode *kernelAtaGetDmaModes(void);

#define _KERNELATADRIVER_H
#endif

