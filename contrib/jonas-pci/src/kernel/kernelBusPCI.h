//This Code is without ANY license. You can do with it whatever you want, for example modify it, 
//license it under any style of license or anything else.
//There is absolutely no warranty on it, don't blame anybody if it is evil.
//You have the right to remove these lines. It would be nice if you gave me credits when you use this code, 
//but you don't have to, I claim no copyright or mental ownership on the code. 
//
//	2005	Jonas Zaddach

#define CONFIG_PORT 0xcf8
#define DATA_PORT 0xcfc

//The true maximum value is 255, but searching all buses slows the starting process down,
//and there won't be many people with mor than 10 PCI buses (none thatI can think of)
#define BUS_PCI_MAX_BUSES 10
#define BUS_PCI_MAX_DEVICES 32
#define BUS_PCI_MAX_FUNCTIONS 8

//return codes for function kernelBusPCIGetClassName()
#define INVALID_CLASSCODE -1
#define INVALID_SUBCLASSCODE -2

//Maximum line length when reading from the PCI device/vendor name list
//used in function kernelBusPCIGetDeviceName()
#define MAX_CONFIG_LINE_LENGTH 1024

//PCI class code constants
#define PCI_CLASS_DISK		0x01
#define PCI_CLASS_NETWORK 	0x02
#define PCI_CLASS_GRAPHICS	0x03
#define PCI_CLASS_MULTIMEDIA 	0x04

#define PCI_CLASS_SERIALBUS	0x0c

//type constants for BAR type
#define PCI_MEMORY_ADDRESS 0
#define PCI_IO_ADDRESS 1
#define PCI_MEMORY_ADDRESS_32 0
#define PCI_MEMORY_ADDRESS_24 2
#define PCI_MEMORY_ADDRESS_64 4
#define PCI_MEMORY_ADDRESS_32_PREFETCHABLE 8
#define PCI_MEMORY_ADDRESS_24_PREFETCHABLE 10
#define PCI_MEMORY_ADDRESS_64_PREFETCHABLE 12
#define PCI_NO_ADDRESS -1

//struct kernelBusPCIDevice.device is from Ralf Brown's CPI configuration data dumper
//If you are in doubt about something, check his source code.

typedef unsigned char BYTE ;
typedef unsigned short WORD ;
typedef unsigned long DWORD ; 

//structure containing the full PCI configuration header of a device (256 byte)
typedef volatile union
{
	struct 
	{
		BYTE	 bus_nr;
		BYTE	 device_nr;
		BYTE	 function_nr;
		WORD	 vendorID ;
		WORD	 deviceID ;
		WORD	 command_reg ;
		WORD	 status_reg ;
		BYTE	 revisionID ;
		BYTE	 progIF ;
		BYTE	 subclass_code ;
		BYTE	 class_code ;
		BYTE	 cacheline_size ;
		BYTE	 latency ;
		BYTE	 header_type ;
		BYTE	 BIST ;
		union
		{
			struct
			{
				DWORD base_address[6] ;
				DWORD CardBus_CIS ;
				WORD  subsystem_vendorID ;
				WORD  subsystem_deviceID ;
				DWORD expansion_ROM ;
				BYTE  cap_ptr ;
				BYTE  reserved1[3] ;
				DWORD reserved2[1] ;
				BYTE  interrupt_line ;
				BYTE  interrupt_pin ;
				BYTE  min_grant ;
				BYTE  max_latency ;
				DWORD device_specific[48] ;
			} nonbridge ;
			struct
			{
				DWORD base_address[2] ;
				BYTE  primary_bus ;
				BYTE  secondary_bus ;
				BYTE  subordinate_bus ;
				BYTE  secondary_latency ;
				BYTE  IO_base_low ;
				BYTE  IO_limit_low ;
				WORD  secondary_status ;
				WORD  memory_base_low ;
				WORD  memory_limit_low ;
				WORD  prefetch_base_low ;
				WORD  prefetch_limit_low ;
				DWORD prefetch_base_high ;
				DWORD prefetch_limit_high ;
				WORD  IO_base_high ;
				WORD  IO_limit_high ;
				DWORD reserved2[1] ;
				DWORD expansion_ROM ;
				BYTE  interrupt_line ;
				BYTE  interrupt_pin ;
				WORD  bridge_control ;
				DWORD device_specific[48] ;
			} bridge ;
			struct
			{
				DWORD ExCa_base ;
				BYTE  cap_ptr ;
				BYTE  reserved05 ;
				WORD  secondary_status ;
				BYTE  PCI_bus ;
				BYTE  CardBus_bus ;
				BYTE  subordinate_bus ;
				BYTE  latency_timer ;
				DWORD memory_base0 ;
				DWORD memory_limit0 ;
				DWORD memory_base1 ;
				DWORD memory_limit1 ;
				WORD  IObase_0low ;
				WORD  IObase_0high ;
				WORD  IOlimit_0low ;
				WORD  IOlimit_0high ;
				WORD  IObase_1low ;
				WORD  IObase_1high ;
				WORD  IOlimit_1low ;
				WORD  IOlimit_1high ;
				BYTE  interrupt_line ;
				BYTE  interrupt_pin ;
				WORD  bridge_control ;
				WORD  subsystem_vendorID ;
				WORD  subsystem_deviceID ;
				DWORD legacy_baseaddr ;
				DWORD cardbus_reserved[14] ;
				DWORD vendor_specific[32] ;
			} cardbus ;
		} ;
	} device ;
	struct
	{
		BYTE  dummy[3];
		DWORD header[64];
	} header;
} kernelBusPCIDevice ;

/*
//same structure as above, but containing only fields neccessary to indetify a device.
typedef volatile union
{
	struct 
	{
		WORD	 bus_nr;
		WORD	 device_nr;
		WORD	 function_nr;
		WORD	 vendorID ;
		WORD	 deviceID ;
	} device;
	struct
	{
		BYTE  address[3];
		DWORD header[1];
	} header;
} kernelBusPCIDeviceLight ;
*/	

typedef volatile struct
{
	int subclasscode;
	const char name[32];
} pciSubclassCode ;

typedef volatile struct
{
	 int classcode;
	 const char name[32];
	 pciSubclassCode * subclass;
	 
} pciClassCode;



int kernelBusPCIGetClassName(int classcode, int subclasscode, char ** classname, char ** subclassname) ;

int kernelBusPCIFindController(void) ;

int kernelBusPCIReadConfig8(int bus, int device, int function, int reg, BYTE *data) ;

int kernelBusPCIWriteConfig8(int bus, int device, int function, int reg, BYTE data) ;

int kernelBusPCIReadConfig16(int bus, int device, int function, int reg, WORD *data) ;

int kernelBusPCIWriteConfig16(int bus, int device, int function, int reg, WORD data) ;

int kernelBusPCIReadConfig32(int bus, int device, int function, int reg, DWORD *data) ;

int kernelBusPCIWriteConfig32(int bus, int device, int function, int reg, DWORD data) ;

int kernelBusPCIGetBaseAddress(kernelBusPCIDevice * pciDevice, int baseAddressRegister, unsigned long * address, unsigned long * length, int * type);

int kernelBusPCIEnable(kernelBusPCIDevice * pciDevice);

int kernelBusPCISetMaster(kernelBusPCIDevice * pciDevice);

int kernelBusPCIDisable(kernelBusPCIDevice * pciDevice);

