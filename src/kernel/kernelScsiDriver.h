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
//  kernelScsiDriver.h
//

#if !defined(_KERNELSCSIDRIVER_H)

// SCSI command codes
#define SCSI_CMD_CHANGEDEF			0x40
#define SCSI_CMD_COMPARE			0x39
#define SCSI_CMD_COPY				0x18
#define SCSI_CMD_COPYANDVERIFY		0x3A
#define SCSI_CMD_INQUIRY			0x12
#define SCSI_CMD_LOGSELECT			0x4C
#define SCSI_CMD_LOGSENSE			0x4D
#define SCSI_CMD_MODESELECT6		0x15
#define SCSI_CMD_MODESELECT10		0x55
#define SCSI_CMD_MODESENSE6			0x1A
#define SCSI_CMD_MODESENSE10		0x5A
#define SCSI_CMD_RCVDIAGRESULTS		0x1C
#define SCSI_CMD_READ6				0x08
#define SCSI_CMD_READ10				0x28
#define SCSI_CMD_READBUFFER			0x3C
#define SCSI_CMD_READCAPACITY		0x25
#define SCSI_CMD_REQUESTSENSE		0x03
#define SCSI_CMD_SENDDIAGNOSTIC		0x1D
#define SCSI_CMD_STARTSTOPUNIT		0x1B
#define SCSI_CMD_TESTUNITREADY		0x00
#define SCSI_CMD_WRITE6				0x0A
#define SCSI_CMD_WRITE10			0x2A
#define SCSI_CMD_WRITEBUFFER		0x3B

// SCSI status codes
#define SCSI_STAT_MASK				0x3E
#define SCSI_STAT_GOOD				0x00
#define SCSI_STAT_CHECKCOND			0x02
#define SCSI_STAT_CONDMET			0x04
#define SCSI_STAT_BUSY				0x08
#define SCSI_STAT_INTERMED			0x10
#define SCSI_STAT_INTERCONDMET		0x14
#define SCSI_STAT_RESERCONF			0x18
#define SCSI_STAT_COMMANDTERM		0x21
#define SCSI_STAT_QUEUEFULL			0x28

// SCSI sense keys
#define SCSI_SENSE_NOSENSE			0x00
#define SCSI_SENSE_RECOVEREDERROR	0x01
#define SCSI_SENSE_NOTREADY			0x02
#define SCSI_SENSE_MEDIUMERROR		0x03
#define SCSI_SENSE_HARWAREERROR		0x04
#define SCSI_SENSE_ILLEGALREQUEST	0x05
#define SCSI_SENSE_UNITATTENTION	0x06
#define SCSI_SENSE_DATAPROTECT		0x07
#define SCSI_SENSE_BLANKCHECK		0x08
#define SCSI_SENSE_VENDORSPECIFIC	0x09
#define SCSI_SENSE_COPYABORTED		0x0A
#define SCSI_SENSE_ABORTEDCOMMAND	0x0B
#define SCSI_SENSE_VOLUMEOVERFLOW	0x0D
#define SCSI_SENSE_MISCOMPARE		0x0E
#define SCSI_SENSE_COMPLETED		0x0F

/*
  A few useful useful SCSI commands

INQUIRY command
+=====-========-========-========-========-========-========-========-========+
|  Bit|   7    |   6    |   5    |   4    |   3    |   2    |   1    |   0    |
|Byte |        |        |        |        |        |        |        |        |
|=====+=======================================================================|
| 0   |                           Operation code (12h)                        |
|-----+-----------------------------------------------------------------------|
| 1   | Logical unit number      |                  Reserved         |  EVPD  |
|-----+-----------------------------------------------------------------------|
| 2   |                           Page code                                   |
|-----+-----------------------------------------------------------------------|
| 3   |                           Reserved                                    |
|-----+-----------------------------------------------------------------------|
| 4   |                           Allocation length                           |
|-----+-----------------------------------------------------------------------|
| 5   |                           Control                                     |
+=============================================================================+

MODE SENSE(6) command
+=====-========-========-========-========-========-========-========-========+
|  Bit|   7    |   6    |   5    |   4    |   3    |   2    |   1    |   0    |
|Byte |        |        |        |        |        |        |        |        |
|=====+=======================================================================|
| 0   |                           Operation code (1Ah)                        |
|-----+-----------------------------------------------------------------------|
| 1   | Logical unit number      |Reserved|   DBD  |         Reserved         |
|-----+-----------------------------------------------------------------------|
| 2   |       PC        |                   Page code                         |
|-----+-----------------------------------------------------------------------|
| 3   |                           Reserved                                    |
|-----+-----------------------------------------------------------------------|
| 4   |                           Allocation length                           |
|-----+-----------------------------------------------------------------------|
| 5   |                           Control                                     |
+=============================================================================+

READ(10) command
+=====-========-========-========-========-========-========-========-========+
|  Bit|   7    |   6    |   5    |   4    |   3    |   2    |   1    |   0    |
|Byte |        |        |        |        |        |        |        |        |
|=====+=======================================================================|
| 0   |                           Operation code (28h)                        |
|-----+-----------------------------------------------------------------------|
| 1   |   Logical unit number    |   DPO  |   FUA  |     Reserved    | RelAdr |
|-----+-----------------------------------------------------------------------|
| 2   | (MSB)                                                                 |
|-----+---                                                                 ---|
| 3   |                                                                       |
|-----+---                        Logical block address                    ---|
| 4   |                                                                       |
|-----+---                                                                 ---|
| 5   |                                                                 (LSB) |
|-----+-----------------------------------------------------------------------|
| 6   |                           Reserved                                    |
|-----+-----------------------------------------------------------------------|
| 7   | (MSB)                                                                 |
|-----+---                        Transfer length                             |
| 8   |                                                                 (LSB) |
|-----+-----------------------------------------------------------------------|
| 9   |                           Control                                     |
+=============================================================================+

READ CAPACITY command
+=====-========-========-========-========-========-========-========-========+
|  Bit|   7    |   6    |   5    |   4    |   3    |   2    |   1    |   0    |
|Byte |        |        |        |        |        |        |        |        |
|=====+=======================================================================|
| 0   |                           Operation code (25h)                        |
|-----+-----------------------------------------------------------------------|
| 1   | Logical unit number      |             Reserved              | RelAdr |
|-----+-----------------------------------------------------------------------|
| 2   | (MSB)                                                                 |
|-----+---                                                                 ---|
| 3   |                                                                       |
|-----+---                        Logical block address                    ---|
| 4   |                                                                       |
|-----+---                                                                 ---|
| 5   |                                                                 (LSB) |
|-----+-----------------------------------------------------------------------|
| 6   |                           Reserved                                    |
|-----+-----------------------------------------------------------------------|
| 7   |                           Reserved                                    |
|-----+-----------------------------------------------------------------------|
| 8   |                           Reserved                           |  PMI   |
|-----+-----------------------------------------------------------------------|
| 9   |                           Control                                     |
+=============================================================================+

START STOP UNIT command
+=====-========-========-========-========-========-========-========-========+
|  Bit|   7    |   6    |   5    |   4    |   3    |   2    |   1    |   0    |
|Byte |        |        |        |        |        |        |        |        |
|=====+=======================================================================|
| 0   |                           Operation code (1Bh)                        |
|-----+-----------------------------------------------------------------------|
| 1   | Logical unit number      |                  Reserved         | Immed  |
|-----+-----------------------------------------------------------------------|
| 2   |                           Reserved                                    |
|-----+-----------------------------------------------------------------------|
| 3   |                           Reserved                                    |
|-----+-----------------------------------------------------------------------|
| 4   |                           Reserved                  |  LoEj  |  Start |
|-----+-----------------------------------------------------------------------|
| 5   |                           Control                                     |
+=============================================================================+

TEST UNIT READY command
+=====-========-========-========-========-========-========-========-========+
|  Bit|   7    |   6    |   5    |   4    |   3    |   2    |   1    |   0    |
|Byte |        |        |        |        |        |        |        |        |
|=====+=======================================================================|
| 0   |                           Operation code (00h)                        |
|-----+-----------------------------------------------------------------------|
| 1   | Logical unit number      |                  Reserved                  |
|-----+-----------------------------------------------------------------------|
| 2   |                           Reserved                                    |
|-----+-----------------------------------------------------------------------|
| 3   |                           Reserved                                    |
|-----+-----------------------------------------------------------------------|
| 4   |                           Reserved                                    |
|-----+-----------------------------------------------------------------------|
| 5   |                           Control                                     |
+=============================================================================+

WRITE(10) command
+=====-========-========-========-========-========-========-========-========+
|  Bit|   7    |   6    |   5    |   4    |   3    |   2    |   1    |   0    |
|Byte |        |        |        |        |        |        |        |        |
|=====+=======================================================================|
| 0   |                           Operation code (2Ah)                        |
|-----+-----------------------------------------------------------------------|
| 1   | Logical unit number      |   DPO  |   FUA  |Reserved|Reserved| RelAdr |
|-----+-----------------------------------------------------------------------|
| 2   | (MSB)                                                                 |
|-----+---                                                                 ---|
| 3   |                                                                       |
|-----+---                        Logical block address                    ---|
| 4   |                                                                       |
|-----+---                                                                 ---|
| 5   |                                                                 (LSB) |
|-----+-----------------------------------------------------------------------|
| 6   |                           Reserved                                    |
|-----+-----------------------------------------------------------------------|
| 7   | (MSB)                                                                 |
|-----+---                        Transfer length                             |
| 8   |                                                                 (LSB) |
|-----+-----------------------------------------------------------------------|
| 9   |                           Control                                     |
+=============================================================================+
*/

typedef struct {
	unsigned char byte[6];

} __attribute__((packed)) scsiCmd6;

typedef struct {
	unsigned char byte[10];

} __attribute__((packed)) scsiCmd10;

typedef struct {
	unsigned char byte[12];

} __attribute__((packed)) scsiCmd12;

typedef struct {
	union {
		unsigned char periQual;		// 7-5 |
		unsigned char periDevType;	//     | 4-0
	} byte0;
	union {
		unsigned char removable;	//  7  |
		unsigned char devTypeMod;	//     | 6-0
	} byte1;
	union {
		unsigned char isoVersion;	// 7-6 |     |
		unsigned char ecmaVersion;	//     | 5-3 |
		unsigned char ansiVersion;	//     |     | 2-0
	} byte2;
	union {
		unsigned char aenc;			//  7  |     |
		unsigned char trmIop;		//     |  6  |
		unsigned char dataFormat;	//     |     | 3-0
	} byte3;
	union {
		unsigned char addlLength;	// 7-0
	} byte4;
	unsigned char byte5;			// 7-0
	unsigned char byte6;			// 7-0
	union {
		unsigned char relAdr;		//  7  |     |     |     |     |     |
		unsigned char wBus32;		//     |  6  |     |     |     |     |
		unsigned char wBus16;		//     |     |  5  |     |     |     |
		unsigned char sync;			//     |     |     |  4  |     |     |
		unsigned char linked;		//     |     |     |     |  3  |     |
		unsigned char cmdQue;		//     |     |     |     |     |  1  |
		unsigned char sftRe;		//     |     |     |     |     |     |  0
	} byte7;
	char vendorId[8];
	char productId[16];
	char productRev[4];

} __attribute__((packed)) scsiInquiryData;

typedef struct {
	unsigned char dataLength;
	unsigned char mediaType;
	unsigned char devSpec;
	unsigned char blockDescLen;

} __attribute__((packed)) scsiModeParamHeader;

typedef struct {
	unsigned char density;
	unsigned char numBlocks0;
	unsigned char numBlocks1;
	unsigned char numBlocks2;
	unsigned char res;
	unsigned char blockLength0;
	unsigned char blockLength1;
	unsigned char blockLength2;

} __attribute__((packed)) scsiBlockDescriptor;

typedef struct {
	unsigned blockNumber;
	unsigned blockLength;

} __attribute__((packed)) scsiCapacityData;

typedef struct {
	unsigned char validErrCode;
	unsigned char segment;
	unsigned char flagsKey;
	unsigned info;
	unsigned char addlLength;
	unsigned cmdSpecific;
	unsigned char addlCode;
	unsigned char addlCodeQual;
	unsigned res;

} __attribute__((packed)) scsiSenseData;

#define _KERNELSCSIDRIVER_H
#endif

