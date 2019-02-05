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
//  kernelAtaDriver.c
//

// Driver for standard ATA functionality

#include "kernelAtaDriver.h"
#include <stdlib.h>

// Miscellaneous ATA features
static ataFeature features[] = {
	// name					identWord | suppMask | featCode | enabByte
	//						                            enabMask | featFlag
	{ "SMART",				82, 0x0001, 0, 0, 0, ATA_FEATURE_SMART },
	{ "write caching",		82, 0x0020, 0x02, 85, 0x0020, ATA_FEATURE_WCACHE },
	{ "read caching",		82, 0x0040, 0xAA, 85, 0x0040, ATA_FEATURE_RCACHE },
	{ "media status",		83, 0x0010, 0x95, 86, 0x0010, ATA_FEATURE_MEDSTAT },
	{ "48-bit addressing",	83, 0x0400, 0, 0, 0, ATA_FEATURE_48BIT },
	{ NULL, 0, 0, 0, 0, 0, 0 }
};

// List of supported DMA modes
static ataDmaMode dmaModes[] = {
	// name		val | identWord | suppMask | enabMask | featFlag;
	{ "UDMA6",	ATA_TRANSMODE_UDMA6, 88, 0x0040, 0x4000, ATA_FEATURE_UDMA },
	{ "UDMA5",	ATA_TRANSMODE_UDMA5, 88, 0x0020, 0x2000, ATA_FEATURE_UDMA },
	{ "UDMA4",	ATA_TRANSMODE_UDMA4, 88, 0x0010, 0x1000, ATA_FEATURE_UDMA },
	{ "UDMA3",	ATA_TRANSMODE_UDMA3, 88, 0x0008, 0x0800, ATA_FEATURE_UDMA },
	{ "UDMA2",	ATA_TRANSMODE_UDMA2, 88, 0x0004, 0x0400, ATA_FEATURE_UDMA },
	{ "UDMA1",	ATA_TRANSMODE_UDMA1, 88, 0x0002, 0x0200, ATA_FEATURE_UDMA },
	{ "UDMA0",	ATA_TRANSMODE_UDMA0, 88, 0x0001, 0x0100, ATA_FEATURE_UDMA },
	{ "DMA2",	ATA_TRANSMODE_DMA2, 63, 0x0004, 0x0040, ATA_FEATURE_MWDMA },
	{ "DMA1",	ATA_TRANSMODE_DMA1, 63, 0x0002, 0x0020, ATA_FEATURE_MWDMA },
	{ "DMA0",	ATA_TRANSMODE_DMA0, 63, 0x0001, 0x0010, ATA_FEATURE_MWDMA },
	{ NULL, 0, 0, 0, 0, 0 }
};


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////


int kernelAtaCommandIsAtapi(unsigned char ataCommand)
{
	// returns 1 if the command is for ATAPI

	switch (ataCommand)
	{
		case ATA_ATAPIRESET:
		case ATA_ATAPIPACKET:
		case ATA_ATAPISERVICE:
			return (1);

		default:
			return (0);
	}
}


ataCommandType kernelAtaCommandType(unsigned char ataCommand)
{
	// Returns the ATA command type

	switch (ataCommand)
	{
		case ATA_NOP:
		case ATA_ATAPIRESET:
		case ATA_VERIFYMULTI:
		case ATA_DIAG:
		case ATA_SETMULTIMODE:
		case ATA_FLUSHCACHE:
		case ATA_FLUSHCACHE_EXT:
		case ATA_SETFEATURES:
			return (ata_nondata);

		case ATA_READSECTS:
		case ATA_READSECTS_EXT:
		case ATA_READMULTI_EXT:
		case ATA_WRITESECTS:
		case ATA_WRITESECTS_EXT:
		case ATA_WRITEMULTI_EXT:
		case ATA_ATAPIIDENTIFY:
		case ATA_READMULTI:
		case ATA_WRITEMULTI:
		case ATA_IDENTIFY:
			return (ata_pio);

		case ATA_READDMA_EXT:
		case ATA_WRITEDMA_EXT:
		case ATA_READDMA:
		case ATA_WRITEDMA:
			return (ata_dma);

		case ATA_ATAPIPACKET:
			return (ata_pio_or_dma);

		case ATA_ATAPISERVICE:
		default:
			return (ata_unknown);
	}
}


ataFeature *kernelAtaGetFeatures(void)
{
	return (features);
}


ataDmaMode *kernelAtaGetDmaModes(void)
{
	return (dmaModes);
}

