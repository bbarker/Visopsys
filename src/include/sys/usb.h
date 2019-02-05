//
//	Visopsys
//	Copyright (C) 1998-2018 J. Andrew McLaughlin
//
//	This library is free software; you can redistribute it and/or modify it
//	under the terms of the GNU Lesser General Public License as published by
//	the Free Software Foundation; either version 2.1 of the License, or (at
//	your option) any later version.
//
//	This library is distributed in the hope that it will be useful, but
//	WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the GNU Lesser
//	General Public License for more details.
//
//	You should have received a copy of the GNU Lesser General Public License
//	along with this library; if not, write to the Free Software Foundation,
//	Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
//	usb.h
//

#if !defined(_USB_H)

#define USB_MAX_INTERFACES				8
#define USB_MAX_ENDPOINTS				16

// USB descriptor types
#define USB_DESCTYPE_DEVICE				1
#define USB_DESCTYPE_CONFIG				2
#define USB_DESCTYPE_STRING				3
#define USB_DESCTYPE_INTERFACE			4
#define USB_DESCTYPE_ENDPOINT			5
// USB 2.0+
#define USB_DESCTYPE_DEVICEQUAL			6
#define USB_DESCTYPE_OTHERSPEED			7
#define USB_DESCTYPE_INTERPOWER			8
// USB 3.0+
#define USB_DESCTYPE_OTG				9
#define USB_DESCTYPE_DEBUG				10
#define USB_DESCTYPE_INTERASSOC			11
#define USB_DESCTYPE_BOS				15
#define USB_DESCTYPE_DEVCAP				16
#define USB_DESCTYPE_SSENDPCOMP			48
// Class-specific
#define USB_DESCTYPE_HID				33
#define USB_DESCTYPE_HIDREPORT			34
#define USB_DESCTYPE_HIDPHYSDESC		35
#define USB_DESCTYPE_HUB				41
#define USB_DESCTYPE_SSHUB				42

// Endpoint attributes
#define USB_ENDP_ATTR_MASK				0x03
#define USB_ENDP_ATTR_CONTROL			0x00
#define USB_ENDP_ATTR_ISOCHRONOUS		0x01
#define USB_ENDP_ATTR_BULK				0x02
#define USB_ENDP_ATTR_INTERRUPT			0x03

// USB commands (control transfer types).  Things with _V2 suffixes are
// obsolete in USB 3.0
#define USB_GET_STATUS					0
#define USB_CLEAR_FEATURE				1
#define USB_GET_STATE					2
#define USB_SET_FEATURE					3
#define USB_SET_ADDRESS					5
#define USB_GET_DESCRIPTOR				6
#define USB_SET_DESCRIPTOR				7
#define USB_GET_CONFIGURATION			8
#define USB_SET_CONFIGURATION			9
#define USB_GET_INTERFACE				10
#define USB_SET_INTERFACE				11
#define USB_SYNCH_FRAME					12
// Class-specific
#define USB_HID_GET_REPORT				1
#define USB_HID_GET_IDLE				2
#define USB_HID_GET_PROTOCOL			3
#define USB_HID_SET_REPORT				9
#define USB_HID_SET_IDLE				10
#define USB_HID_SET_PROTOCOL			11
#define USB_HUB_CLEAR_TT_BUFFER_V2		8
#define USB_HUB_RESET_TT_V2				9
#define USB_HUB_GET_TT_STATE_V2			10
#define USB_HUB_STOP_TT_V2				11
#define USB_HUB_SET_HUB_DEPTH			12
#define USB_MASSSTORAGE_RESET			0xFF

// USB device request types
#define USB_DEVREQTYPE_HOST2DEV			0x00
#define USB_DEVREQTYPE_DEV2HOST			0x80
#define USB_DEVREQTYPE_STANDARD			0x00
#define USB_DEVREQTYPE_CLASS			0x20
#define USB_DEVREQTYPE_VENDOR			0x40
#define USB_DEVREQTYPE_RESERVED			0x60
#define USB_DEVREQTYPE_DEVICE			0x00
#define USB_DEVREQTYPE_INTERFACE		0x01
#define USB_DEVREQTYPE_ENDPOINT			0x02
#define USB_DEVREQTYPE_OTHER			0x03

// USB features (for set/clear commands)
#define USB_FEATURE_ENDPOINTHALT		0x00
#define USB_FEATURE_REMOTEWAKEUP		0x01
#define USB_FEATURE_TESTMODE			0x02

#define USB_INVALID_CLASSCODE			-1
#define USB_INVALID_SUBCLASSCODE		-2

// Values for the 'packet ID' field of USB transfer descriptors
#define USB_PID_IN						0x69
#define USB_PID_OUT						0xE1
#define USB_PID_SETUP					0x2D

// Hub characteristics, from the hub descriptor's hubChars field.  Things with
// _V suffixes indicate the USB versions they're compatible with.
#define USB_HUBCHARS_PORTIND_V2			0x80
#define USB_HUBCHARS_TTT_V2				0x60
#define USB_HUBCHARS_TTT_32_V2			0x60
#define USB_HUBCHARS_TTT_24_V2			0x40
#define USB_HUBCHARS_TTT_16_V2			0x20
#define USB_HUBCHARS_TTT_8_V2			0x00
#define USB_HUBCHARS_OVERCURR			0x18
#define USB_HUBCHARS_COMPOUND			0x04
#define USB_HUBCHARS_POWERSWITCH		0x03

// Hub status/change bits
#define USB_HUBSTAT_LOCPOWER			0x0001
#define USB_HUBSTAT_OVERCURR			0x0002

// Port status/change bits.  Things with _V suffixes indicate the USB versions
// they're compatible with.
#define USB_HUBPORTSTAT_SPEED_V3		0x1C00
#define USB_HUBPORTSTAT_PORTINDCONT_V2	0x1000
#define USB_HUBPORTSTAT_PORTTEST_V2		0x0800
#define USB_HUBPORTSTAT_HIGHSPEED_V2	0x0400
#define USB_HUBPORTSTAT_LOWSPEED_V12	0x0200
#define USB_HUBPORTSTAT_POWER_V3		0x0200
#define USB_HUBPORTSTAT_LINKSTATE_V3	0x01E0
#define USB_HUBPORTSTAT_POWER_V12		0x0100
#define USB_HUBPORTSTAT_RESET			0x0010
#define USB_HUBPORTSTAT_OVERCURR		0x0008
#define USB_HUBPORTSTAT_SUSPEND_V12		0x0004
#define USB_HUBPORTSTAT_ENABLE			0x0002
#define USB_HUBPORTSTAT_CONN			0x0001

// Additional port change bits.  Before USB 3.0 the port status and change
// bits were one-to-one.  _V suffixes indicate the USB versions they're
// compatible with
#define USB_HUBPORTCHANGE_CONFERROR_V3	0x0080
#define USB_HUBPORTCHANGE_LINKSTATE_V3	0x0040
#define USB_HUBPORTCHANGE_BHRESET_V3	0x0020

// Hub features
#define USB_HUBFEAT_HUBLOCPOWER_CH		0
#define USB_HUBFEAT_HUBOVERCURR_CH		1

// Hub port features.  Things with _V suffixes indicate the USB versions
// they're compatible with.
#define USB_HUBFEAT_PORTCONN			0
#define USB_HUBFEAT_PORTENABLE_V12		1
#define USB_HUBFEAT_PORTSUSPEND_V12		2
#define USB_HUBFEAT_PORTOVERCURR		3
#define USB_HUBFEAT_PORTRESET			4
#define USB_HUBFEAT_PORTLINKSTATE_V3	5
#define USB_HUBFEAT_PORTPOWER			8
#define USB_HUBFEAT_PORTLOWSPEED_V12	9
#define USB_HUBFEAT_PORTCONN_CH			16
#define USB_HUBFEAT_PORTENABLE_CH_V12	17
#define USB_HUBFEAT_PORTSUSPEND_CH_V12	18
#define USB_HUBFEAT_PORTOVERCURR_CH		19
#define USB_HUBFEAT_PORTRESET_CH		20
#define USB_HUBFEAT_PORTTEST_V2			21
#define USB_HUBFEAT_PORTINDICATOR_V2	22
#define USB_HUBFEAT_PORTU1TIMEOUT_V3	23
#define USB_HUBFEAT_PORTU2TIMEOUT_V3	24
#define USB_HUBFEAT_PORTLINKSTATE_CH_V3	25
#define USB_HUBFEAT_PORTCONFERR_CH_V3	26
#define USB_HUBFEAT_PORTREMWAKEMASK_V3	27
#define USB_HUBFEAT_PORTBHRESET_V3		28
#define USB_HUBFEAT_PORTBHRESET_CH_V3	29
#define USB_HUBFEAT_PORTFRCELNKPMACC_V3	30

// USB mass storage command and status block signatures
#define USB_CMDBLOCKWRAPPER_SIG			0x43425355
#define USB_CMDSTATUSWRAPPER_SIG		0x53425355

// USB mass storage CSW status codes
#define USB_CMDSTATUS_GOOD				0x00
#define USB_CMDSTATUS_FAILED			0x01
#define USB_CMDSTATUS_PHASEERROR		0x02

// USB HID report types
#define USB_HID_REPORT_INPUT			1
#define USB_HID_REPORT_OUTPUT			2
#define USB_HID_REPORT_FEATURE			3

// USB HID item types
#define USB_HID_ITEMTYPE_MAIN			0x00
#define USB_HID_ITEMTYPE_GLOBAL			0x01
#define USB_HID_ITEMTYPE_LOCAL			0x02
#define USB_HID_ITEMTYPE_RES			0x03

// USB HID main items
#define USB_HID_ITEMTAG_INPUT			0x08  // 1000
#define USB_HID_ITEMTAG_OUTPUT			0x09  // 1001
#define USB_HID_ITEMTAG_COLL			0x0A  // 1010
#define USB_HID_ITEMTAG_FEATURE			0x0B  // 1011
#define USB_HID_ITEMTAG_ENDCOLL			0x0C  // 1100
#define USB_HID_ITEMTAG_LONG			0x0F  // 1111

// USB HID global items
#define USB_HID_ITEMTAG_USAGEPG			0x00  // 0000
#define USB_HID_ITEMTAG_LOGIMIN			0x01  // 0001
#define USB_HID_ITEMTAG_LOGIMAX			0x02  // 0010
#define USB_HID_ITEMTAG_PHYSMIN			0x03  // 0011
#define USB_HID_ITEMTAG_PHYSMAX			0x04  // 0100
#define USB_HID_ITEMTAG_UNITEXP			0x05  // 0101
#define USB_HID_ITEMTAG_UNIT			0x06  // 0110
#define USB_HID_ITEMTAG_REPSIZE			0x07  // 0111
#define USB_HID_ITEMTAG_REPID			0x08  // 1000
#define USB_HID_ITEMTAG_REPCNT			0x09  // 1001
#define USB_HID_ITEMTAG_PUSH			0x0A  // 1010
#define USB_HID_ITEMTAG_POP				0x0B  // 1011

// USB HID local items
#define USB_HID_ITEMTAG_USAGE			0x00  // 0000
#define USB_HID_ITEMTAG_USGMIN			0x01  // 0001
#define USB_HID_ITEMTAG_USGMAX			0x02  // 0010
#define USB_HID_ITEMTAG_DESGIDX			0x03  // 0011
#define USB_HID_ITEMTAG_DESGMIN			0x04  // 0100
#define USB_HID_ITEMTAG_DESGMAX			0x05  // 0101
#define USB_HID_ITEMTAG_STRIDX			0x07  // 0111
#define USB_HID_ITEMTAG_STRMIN			0x08  // 1000
#define USB_HID_ITEMTAG_STRMAX			0x09  // 1001
#define USB_HID_ITEMTAG_DELIMTR			0x0A  // 1010

// USB HID main item parts
#define USB_HID_MAININPUT_CONST			0x001
#define USB_HID_MAININPUT_VAR			0x002
#define USB_HID_MAININPUT_REL			0x004
#define USB_HID_MAININPUT_WRAP			0x008
#define USB_HID_MAININPUT_NONLIN		0x010
#define USB_HID_MAININPUT_NOPREF		0x020
#define USB_HID_MAININPUT_NULLST		0x040
#define USB_HID_MAININPUT_RES			0x080
#define USB_HID_MAININPUT_BUFFB			0x100

#define USB_HID_MAINOUTPUT_CONST		0x001
#define USB_HID_MAINOUTPUT_VAR			0x002
#define USB_HID_MAINOUTPUT_REL			0x004
#define USB_HID_MAINOUTPUT_WRAP			0x008
#define USB_HID_MAINOUTPUT_NONLIN		0x010
#define USB_HID_MAINOUTPUT_NOPREF		0x020
#define USB_HID_MAINOUTPUT_NULLST		0x040
#define USB_HID_MAINOUTPUT_VOL			0x080
#define USB_HID_MAINOUTPUT_BUFFB		0x100

#define USB_HID_MAINFEATURE_CONST		0x001
#define USB_HID_MAINFEATURE_VAR			0x002
#define USB_HID_MAINFEATURE_REL			0x004
#define USB_HID_MAINFEATURE_WRAP		0x008
#define USB_HID_MAINFEATURE_NONLIN		0x010
#define USB_HID_MAINFEATURE_NOPREF		0x020
#define USB_HID_MAINFEATURE_NULLST		0x040
#define USB_HID_MAINFEATURE_VOL			0x080
#define USB_HID_MAINFEATURE_BUFFB		0x100

// Usage pages
#define USB_HID_USAGEPAGE_UNDEFINED		0x00
#define USB_HID_USAGEPAGE_GENDESK		0x01
#define USB_HID_USAGEPAGE_SIMCTRLS		0x02
#define USB_HID_USAGEPAGE_VRCTRLS		0x03
#define USB_HID_USAGEPAGE_SPRTCTRLS		0x04
#define USB_HID_USAGEPAGE_GAMECTRLS		0x05
#define USB_HID_USAGEPAGE_GENDEVCTL		0x06
#define USB_HID_USAGEPAGE_KEYBRDPAD		0x07
#define USB_HID_USAGEPAGE_LEDS			0x08
#define USB_HID_USAGEPAGE_BUTTON		0x09
#define USB_HID_USAGEPAGE_ORDINAL		0x0A
#define USB_HID_USAGEPAGE_TELEPHONY		0x0B
#define USB_HID_USAGEPAGE_CONSUMER		0x0C
#define USB_HID_USAGEPAGE_DIGITIZER		0x0D
#define USB_HID_USAGEPAGE_PIDPAGE		0x0F
#define USB_HID_USAGEPAGE_UNICODE		0x10
#define USB_HID_USAGEPAGE_ALPHANUM		0x14
#define USB_HID_USAGEPAGE_MEDICAL		0x40
#define USB_HID_USAGEPAGE_MONPAGES1		0x80
#define USB_HID_USAGEPAGE_MONPAGES2		0x81
#define USB_HID_USAGEPAGE_MONPAGES3		0x82
#define USB_HID_USAGEPAGE_MONPAGES4		0x83
#define USB_HID_USAGEPAGE_POWPAGES1		0x84
#define USB_HID_USAGEPAGE_POWPAGES2		0x85
#define USB_HID_USAGEPAGE_POWPAGES3		0x86
#define USB_HID_USAGEPAGE_POWPAGES4		0x87
#define USB_HID_USAGEPAGE_BARCODE		0x8C
#define USB_HID_USAGEPAGE_SCALEPAGE		0x8D
#define USB_HID_USAGEPAGE_MAGSTRDEV		0x8E
#define USB_HID_USAGEPAGE_RESPOS		0x8F
#define USB_HID_USAGEPAGE_CAMERACTL		0x90
#define USB_HID_USAGEPAGE_ARCADE		0x91

// Control endpoint device request
typedef struct {
	unsigned char requestType;
	unsigned char request;
	unsigned short value;
	unsigned short index;
	unsigned short length;

} __attribute__((packed)) usbDeviceRequest;

// USB device descriptor
typedef struct {
	unsigned char descLength;		// Number of bytes in this descriptor
	unsigned char descType;			// Type, DEVICE descriptor type
	unsigned short usbVersion;		// BCD USB version supported
	unsigned char deviceClass;		// Major device class
	unsigned char deviceSubClass;	// Minor device class
	unsigned char deviceProtocol;	// Device protocol
	unsigned char maxPacketSize0;	// Max packet size (8/16/32/64) for endpt 0
	unsigned short vendorId;		// Vendor ID
	unsigned short productId;		// Product ID
	unsigned short deviceVersion;	// BCD device version
	unsigned char manuStringIdx;	// Index of manufacturer string
	unsigned char prodStringIdx;	// Index of product string
	unsigned char serStringIdx;		// Index of serial number string
	unsigned char numConfigs;		// Number of possible configurations

} __attribute__((packed)) usbDeviceDesc;

// USB device qualifier descriptor
typedef struct {
	unsigned char descLength;		// Number of bytes in this descriptor
	unsigned char descType;			// Type, DEVICEQUAL descriptor type
	unsigned short usbVersion;		// BCD USB version supported
	unsigned char deviceClass;		// Major device class
	unsigned char deviceSubClass;	// Minor device class
	unsigned char deviceProtocol;	// Device protocol
	unsigned char maxPacketSize0;	// Max packet size (8/16/32/64) for endpt 0
	unsigned char numConfigs;		// Number of possible configurations
	unsigned char res;				// Reserved, must be zero

} __attribute__((packed)) usbDevQualDesc;

// USB configuration descriptor
typedef struct {
	unsigned char descLength;		// Number of bytes in this descriptor
	unsigned char descType;			// Type, CONFIGURATION descriptor type
	unsigned short totalLength;		// Total length returned for this config
	unsigned char numInterfaces;	// Number of interfaces in this config
	unsigned char confValue;		// Value for 'set config' requests
	unsigned char confStringIdx;	// Index of config descriptor string
	unsigned char attributes;		// Bitmap of attributes
	unsigned char maxPower;			// Max consumption in this config

} __attribute__((packed)) usbConfigDesc;

// USB interface descriptor
typedef struct {
	unsigned char descLength;		// Number of bytes in this descriptor
	unsigned char descType;			// Type, INTERFACE descriptor type
	unsigned char interNum;			// Number of interface
	unsigned char altSetting;		// Alternate setting for interface
	unsigned char numEndpoints;		// Endpoints that use this interface
	unsigned char interClass;		// Interface class
	unsigned char interSubClass;	// Interface subclass
	unsigned char interProtocol;	// Interface protocol code
	unsigned char interStringIdx;	// Index of interface descriptor string

} __attribute__((packed)) usbInterDesc;

// USB superspeed endpoint companion descriptor
typedef struct {
	unsigned char descLength;		// Number of bytes in this descriptor
	unsigned char descType;			// Type, SSENDPCOMP descriptor type
	unsigned char maxBurst;			// Max packets in a burst (0-based)

} __attribute__((packed)) usbSuperEndpCompDesc;

// USB endpoint descriptor
typedef struct {
	unsigned char descLength;		// Number of bytes in this descriptor
	unsigned char descType;			// Type, ENDPOINT descriptor type
	unsigned char endpntAddress;	// Endpoint address
	unsigned char attributes;		// Bitmap of attributes
	unsigned short maxPacketSize;	// Max packet size for this endpoint
	unsigned char interval;			// ms interval for enpoint data polling
	// USB 3.0 superspeed devices
	usbSuperEndpCompDesc superComp;

} __attribute__((packed)) usbEndpointDesc;

typedef struct {
	unsigned char descLength;		// Number of bytes in this descriptor
	unsigned char descType;			// Type, STRING descriptor type
	unsigned char string[];			// String data, not NULL-terminated

} __attribute__((packed)) usbStringDesc;

// USB hub class descriptor
typedef struct {
	unsigned char descLength;		// Number of bytes in this descriptor
	unsigned char descType;			// Type, HUB descriptor type
	unsigned char numPorts;			// Number of ports on the hub
	unsigned short hubChars;		// Bitmap of hub characteristics
	unsigned char pwrOn2PwrGood;	// 2ms intervals until port power stable
	unsigned char maxPower;			// Max consumption of the controller
	union {
		unsigned char padTo8;			// Pad structure to at least 8 bytes
		#if 0	// There are other fields depending on the version, but we
				// don't currently use them
		struct {
			unsigned char devRemovable[]; // Bitmap of ports w/ removable devs
			// unsigned char portPwrCtlMap[]; // Obsolete
		} v2;
		struct {
			// For USB3 superspeed hubs
			unsigned char hubHdrDecLat;	// Hub packet header decode latency
			unsigned short hubDelay;	// Average ns delay introduced by hub
			unsigned short devRemovable; // Bitmap of ports w/ removable devs
		} v3;
		#endif
	} ver;

} __attribute__((packed)) usbHubDesc;

// USB hub status
typedef struct {
	unsigned short status;
	unsigned short change;

} __attribute__((packed)) usbHubStatus;

// USB hub port status
typedef struct {
	unsigned short status;
	unsigned short change;

} __attribute__((packed)) usbHubPortStatus;

// Mass storage command block wrapper
typedef struct {
	unsigned signature;
	unsigned tag;
	unsigned dataLength;
	unsigned char flags;
	unsigned char lun;
	unsigned char cmdLength;
	unsigned char cmd[16];

} __attribute__((packed)) usbCmdBlockWrapper;

// Mass storage status wrapper
typedef struct {
	unsigned signature;
	unsigned tag;
	unsigned dataResidue;
	unsigned char status;

} __attribute__((packed)) usbCmdStatusWrapper;

// Human Interface Device class-specific optional descriptor
typedef struct {
	unsigned char descType;
	unsigned short descLength;

} __attribute__((packed)) usbHidOptDesc;

// Human Interface Device class-specific descriptor
typedef struct {
	unsigned char descLength;		// Number of bytes in this descriptor
	unsigned char descType;			// Type, HID descriptor type
	unsigned short hidVersion;		// BCD version of HID spec
	unsigned char countryCode;		// Hardware target country
	unsigned char numDescriptors;	// Number of HID class descs to follow
	unsigned char repDescType;		// Report descriptor type
	unsigned short repDescLength;	// Report descriptor total length
	usbHidOptDesc optDesc[];		// Array of optional descriptors

} __attribute__((packed)) usbHidDesc;

#define _USB_H
#endif

