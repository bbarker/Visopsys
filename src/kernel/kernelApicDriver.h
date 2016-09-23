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
//  kernelApicDriver.h
//

#if !defined(_KERNELAPICDRIVER_H)

#define APIC_LOCALREG_APICID		0x20
#define APIC_LOCALREG_VERSION		0x30
#define APIC_LOCALREG_TASKPRI		0x80
#define APIC_LOCALREG_ARBPRI		0x90
#define APIC_LOCALREG_PROCPRI		0xA0
#define APIC_LOCALREG_EOI			0xB0
#define APIC_LOCALREG_LOGDEST		0xD0
#define APIC_LOCALREG_DESTFMT		0xE0
#define APIC_LOCALREG_SPURINT		0xF0
#define APIC_LOCALREG_ISR			0x100
#define APIC_LOCALREG_TMR			0x180
#define APIC_LOCALREG_IRR			0x200
#define APIC_LOCALREG_ERRSTAT		0x280
#define APIC_LOCALREG_INTCMDLO		0x300
#define APIC_LOCALREG_INTCMDHI		0x310
#define APIC_LOCALREG_LOCVECTBL		0x320
#define APIC_LOCALREG_PERFCNT		0x340
#define APIC_LOCALREG_LINT0			0x350
#define APIC_LOCALREG_LINT1			0x360
#define APIC_LOCALREG_ERROR			0x370
#define APIC_LOCALREG_TIMERCNT		0x380

typedef struct {
	unsigned char id;
	volatile unsigned *regs;

} kernelIoApic;

void kernelApicDebug(void);

#define _KERNELAPICDRIVER_H
#endif

