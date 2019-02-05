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
//  telnet.h
//

// This file contains definitions and structures for using the telnet protocol
// in Visopsys.

#if !defined(_TELNET_H)

// Telnet commands
#define TELNET_COMMAND_SE		240	// End of subnegotiation parameters.
#define TELNET_COMMAND_NOP		241	// No operation
#define TELNET_COMMAND_DM		242	// Data mark.
#define TELNET_COMMAND_BRK		243	// Break.
#define TELNET_COMMAND_IP		244	// Suspend, interrupt or abort the process
#define TELNET_COMMAND_AO		245	// Abort output.
#define TELNET_COMMAND_AYT		246	// Are you there.
#define TELNET_COMMAND_EC		247	// Erase character.
#define TELNET_COMMAND_EL		248	// Erase line.
#define TELNET_COMMAND_GA		249	// Go ahead.
#define TELNET_COMMAND_GSB		250	// Subnegotiation of the indicated option
#define TELNET_COMMAND_WILL		251	// Indicates the desire to begin performing,
									// or confirmation that you are now
									// performing, the indicated option.
#define TELNET_COMMAND_WONT		252	// Indicates the refusal to perform, or
									// continue performing, the indicated option
#define TELNET_COMMAND_DO		253	// Indicates the request that the other
									// party perform, or confirmation that you
									// are expecting the other party to perform,
									// the indicated option.
#define TELNET_COMMAND_DONT		254	// Indicates the demand that the other party
									// stop performing, or confirmation that you
									// are no longer expecting the other party
									// to perform, the indicated option.
#define TELNET_COMMAND_IAC		255	// Interpret as command

// Telnet option codes
#define TELNET_OPTION_ECHO		1	// echo: RFC 857
#define TELNET_OPTION_SUPGA		3	// suppress go ahead: RFC 858
#define TELNET_OPTION_STATUS	5	// status: RFC 859
#define TELNET_OPTION_TMARK		6	// timing mark: RFC 860
#define TELNET_OPTION_TTYPE		24	// terminal type: RFC 1091
#define TELNET_OPTION_WINSZ		31	// window size: RFC 1073
#define TELNET_OPTION_TSPEED	32	// terminal speed: RFC 1079
#define TELNET_OPTION_REMFC		33	// remote flow control: RFC 1372
#define TELNET_OPTION_LMODE		34	// linemode: RFC 1184
#define TELNET_OPTION_ENVAR		36	// environment variables: RFC 1408
#define TELNET_OPTION_ENVOPT	39	// environment variables: RFC 1572

#define _TELNET_H
#endif

