//This Code is without ANY license. You can do with it whatever you want, for example modify it, 
//license it under any style of license, incorporate it in your programs or anything else.
//There is absolutely no warranty on it, don't blame anybody if it is evil.
//You have the right to remove these lines. It would be nice if you gave me credits when you use this code, 
//but you don't have to, I claim no copyright or mental ownership on the code. 
//
//	2005	Jonas Zaddach

// These routines allow access to PCI configuration space.

#include "kernelBusPCI.h"
#include "kernelProcessorX86.h"
#include "kernelLog.h"
#include "kernelMiscFunctions.h"
#include "kernelMalloc.h"
#include "kernelFileStream.h"
#include <stdlib.h>
#include <sys/errors.h>

#define PCI_DEBUG

#define hexStringToInt(x)	xtoi(x)

pciSubclassCode subclass_old[] = 
{
	{0x00, "other"},
	{0x01, "VGA"},
	{-1, ""}
};

pciSubclassCode subclass_disk[] = 
{
	{0x00, "SCSI"},
	{0x01, "IDE"},
	{0x02, "floppy"},
	{0x03, "IPI"},
	{0x04, "RAID"},
	{-1, ""}
};

pciSubclassCode subclass_net[] = 
{
	{0x00, "Ethernet"},
	{0x01, "Token Ring"},
	{0x02, "FDDI"},
	{0x03, "ATM"},
	{-1, ""}
};

pciSubclassCode subclass_graphics[] = 
{
	{0x00, "VGA"},
	{0x01, "SuperVGA"},
	{0x02, "XGA"},
	{-1, ""}
};

pciSubclassCode subclass_mma[] = 
{
	{0x00, "video"},
	{0x01, "audio"},
	{-1, ""}
};

pciSubclassCode subclass_mem[] =
{
	{0x00, "RAM"},
	{0x01, "Flash"},
	{-1, ""}
};

pciSubclassCode subclass_bridge[] = 
{
	{0x00, "CPU/PCI"},
	{0x01, "PCI/ISA"},
	{0x02, "PCI/EISA"},
	{0x03, "PCI/MCA"},
	{0x04, "PCI/PCI"},
	{0x05, "PCI/PCMCIA"},
	{0x06, "PCI/NuBus"},
	{0x07, "PCI/cardbus"},
	{-1, ""}
};

pciSubclassCode subclass_comm[] = 
{
	{ 0x00, "serial" },
	{ 0x01, "parallel" },
	{-1, ""}
};

pciSubclassCode subclass_sys[] = 
{
	{ 0x00, "PIC" },
	{ 0x01, "DMAC" },
	{ 0x02, "timer" },
	{ 0x03, "RTC" },
	{-1, ""}
};

pciSubclassCode subclass_hid[] = 
{
	{ 0x00, "keyboard" },
	{ 0x01, "digitizer" },
	{ 0x02, "mouse" },
	{-1, ""}
};

pciSubclassCode subclass_dock[] = 
{
	{ 0x00, "generic" },
	{-1, ""}
};

pciSubclassCode subclass_cpu[] = 
{
	{ 0x00, "386" },
	{ 0x01, "486" },
	{ 0x02, "Pentium" },
	{ 0x03, "P6" },
	{ 0x10, "Alpha" },
	{ 0x40, "Coprocessor" },
	{-1, ""}
};

pciSubclassCode subclass_serial[] = 
{
	{ 0x00, "Firewire" },
	{ 0x01, "ACCESS.bus" },
	{ 0x02, "SSA" },
	{ 0x03, "USB" },
	{ 0x04, "Fiber Channel" },
	{-1, ""}
};
	
pciClassCode kernelBusPCIClassNames[] = 
{
	{0x00, "before PCI 2.0", subclass_old},
	{0x01, "disk controller", subclass_disk},
	{0x02, "network interface", subclass_net},
	{0x03, "graphics adapter", subclass_graphics},
	{0x04, "multimedia adapter", subclass_mma},
	{0x05, "memory", subclass_mem},
	{0x06, "bridge", subclass_bridge},
	{0x07, "communication", subclass_comm},
	{0x08, "system peripheral", subclass_sys},
	{0x09, "HID", subclass_hid},
	{0x0a, "docking station", subclass_dock},
	{0x0b, "CPU", subclass_cpu},
	{0x0c, "serial bus", subclass_serial},
	{-1, "", 0}
};

const char * invalidDevice = "invalid device";

const char * otherDevice = "other";

int kernelBusPCIGetClassName(int classcode, int subclasscode, char ** classname, char ** subclassname)
{
//returns name of the class and the subclass in human readable format. Buffers classname and subclassname have to provide  
	int status = 0;
	
	int i;
	
	for(i = 0; i < 256; i++)
	{	
		//If no more classcodes are in the list
		if(kernelBusPCIClassNames[i].classcode == -1)
		{
			*classname = (char *) invalidDevice;
			return (status = INVALID_CLASSCODE);
		}
		
		//if valid classcode is found
		if(kernelBusPCIClassNames[i].classcode == classcode)
		{
			*classname = (char *) kernelBusPCIClassNames[i].name;
			
			break;
		}
	}
	
	//Subclasscode 0x80 is always other
	if(subclasscode == 0x80)
	{
		*subclassname = (char *) otherDevice;
		
		return (status);
	}
	
	pciSubclassCode * subclass = kernelBusPCIClassNames[classcode].subclass;
	
	for(i = 0; i < 256; i++)
	{
		if(subclass[i].subclasscode == -1)
		{
			*subclassname = (char *) invalidDevice;
			
			return (status = INVALID_SUBCLASSCODE);
		}
		
//		kernelLog("subclasscode: %x, expected: %x\n", subclass[i].subclasscode, subclasscode);
		
		if(subclass[i].subclasscode == subclasscode)
		{
			*subclassname = (char *) subclass[i].name;
			
			break;
		}
	}
	
	return (status);
}

int kernelBusPCIFindController(void)
{
//Checks for a configuration mechanism #1 able PCI controller
//returns 0 if controller is found, otherwise -1

	DWORD reply = 0;
	
	int status = 0;
	
	kernelProcessorOutPort32(CONFIG_PORT, 0x80000000L);
	
	kernelProcessorInPort32(CONFIG_PORT, reply);
	
	if(reply != 0x80000000L)
	{
	   return (status = -1);
	}
	
	return (status);
}  

int kernelBusPCIReadConfig8(int bus, int device, int function, int reg, BYTE *data)
{
//Reads 1 byte of the PCI configuration header of the requested device. bus, device and function identify the device,
//register is which byte you want to read. The data will be written into data.
	int status = 0;
	
	DWORD address;
	
	address = 0x80000000L | 
		(((DWORD) (bus & 0xff) << 16) | ((device & 0x1f)  << 11) | ((function & 0x07) << 8) | (reg & 0xfc));
	
	kernelProcessorOutPort32(CONFIG_PORT, address) ;
		
	kernelProcessorInPort8(DATA_PORT + (reg & 3), *data) ;
	
	return (status = 0) ;
}

int kernelBusPCIWriteConfig8(int bus, int device, int function, int reg, BYTE data)
{
//Writes 1 byte of the PCI configuration header of the requested device. bus, device and function identify the device,
//register is which byte you want to write. data is the byte you want to write.
	int status = 0;
	
	DWORD address;
	
	address = 0x80000000L | 
		(((DWORD) (bus & 0xff) << 16) | ((device & 0x1f)  << 11) | ((function & 0x07) << 8) | (reg & 0xfc));
	
	kernelProcessorOutPort32(CONFIG_PORT, address) ;
		
	kernelProcessorOutPort8(DATA_PORT + (reg & 3), data) ;
	
	return (status = 0) ;
}

int kernelBusPCIReadConfig16(int bus, int device, int function, int reg, WORD *data)
{
//Reads configuration word
	int status = 0;
	
	DWORD address;
	
	address = 0x80000000L | 
		(((DWORD) (bus & 0xff) << 16) | ((device & 0x1f)  << 11) | ((function & 0x07) << 8) | (reg & 0xfc));
	
	kernelProcessorOutPort32(CONFIG_PORT, address) ;
		
	kernelProcessorInPort16(DATA_PORT + (reg & 2), *data) ;
	
	return (status = 0) ;
}

int kernelBusPCIWriteConfig16(int bus, int device, int function, int reg, WORD data)
{
//writes configuration word
	int status = 0;
	
	DWORD address;
	
	address = 0x80000000L | 
		(((DWORD) (bus & 0xff) << 16) | ((device & 0x1f)  << 11) | ((function & 0x07) << 8) | (reg & 0xfc));
	
	kernelProcessorOutPort32(CONFIG_PORT, address) ;
		
	kernelProcessorOutPort16(DATA_PORT + (reg & 2), data) ;
	
	return (status = 0) ;
}

int kernelBusPCIReadConfig32(int bus, int device, int function, int reg, DWORD *data)
{
//reads configuration dword
	int status = 0;
	
	DWORD address;
	
	address = 0x80000000L | 
		(((DWORD) (bus & 0xff) << 16) | ((device & 0x1f)  << 11) | ((function & 0x07) << 8) | (reg & 0xfc));
	
	kernelProcessorOutPort32(CONFIG_PORT, address) ;
		
	kernelProcessorInPort32(DATA_PORT, *data) ;
	
	return (status = 0) ;
}

int kernelBusPCIWriteConfig32(int bus, int device, int function, int reg, DWORD data)
{
//writes configuration dword
	int status = 0;
	
	DWORD address;
	
	address = 0x80000000L | 
		(((DWORD) (bus & 0xff) << 16) | ((device & 0x1f)  << 11) | ((function & 0x07) << 8) | (reg & 0xfc));
	
	kernelProcessorOutPort32(CONFIG_PORT, address) ;
		
	kernelProcessorOutPort32(DATA_PORT, data) ;
	
	return (status = 0) ;
}

int kernelBusPCIGetBaseAddress(kernelBusPCIDevice * pciDevice, int baseAddressRegister, unsigned long * address, unsigned long * length, int * type)
{
	int status = 0;
	
	DWORD previousValue;
	
	//Check if any of the given parameters is NULL	
	if(!pciDevice)
	{
#if defined(PCI_DEBUG)
		kernelLog("kernelBusPCIGetBaseAddress: pciDevice == NULL\n");
#endif
		return (status = ERR_NULLPARAMETER);
	}
	
	if(!address)
	{
#if defined(PCI_DEBUG)
		kernelLog("kernelBusPCIGetBaseAddress: address == NULL\n");
#endif
		return (status = ERR_NULLPARAMETER);
	}
	
	if(!length)
	{
#if defined(PCI_DEBUG)
		kernelLog("kernelBusPCIGetBaseAddress: length == NULL\n");
#endif
		return (status = ERR_NULLPARAMETER);
	}
	
	if(!type)
	{
#if defined(PCI_DEBUG)
		kernelLog("kernelBusPCIGetBaseAddress: type == NULL\n");
#endif
		return (status = ERR_NULLPARAMETER);
	}
	
	//Check if requested base address register is valid
	if(baseAddressRegister < 0 || baseAddressRegister >= 256)
	{
#if defined(PCI_DEBUG)
		kernelLog("kernelBusPCIGetBaseAddress: invalid baseAddressRegister %d requested\n", baseAddressRegister);
#endif
		
		return (status = ERR_BOUNDS);
	}
	
	
	//Zero all values
	*address = 0;
	*length = 0;
	*type = -1;

	//Check if this device header is type non-bridge
	if(((*pciDevice).device.header_type & 0x7f) != 0)
	{
		//no, it is not. We cannot determine base address registers.
		return (status = -1);
	}
	
	//Check if requested base address register is valid
	if(baseAddressRegister < 0 || baseAddressRegister > 5)
	{
		return (status = -2);
	}
	
	*address = (*pciDevice).device.nonbridge.base_address[baseAddressRegister]; 
	
	if(*address == 0)
	{
		return(status = -3);
	}
	
	//type is lowest bit of base address
	*type = (int) (*address & 1);
	
	if(type == PCI_MEMORY_ADDRESS)
	{
		*type = (int) (*address & 0x0f);
		
		*address &= 0xfffffff0;
	}
	else
	{
		*address &= 0xfffffffe;
	}
	
	//Backup the BAR register value
	kernelBusPCIReadConfig32((*pciDevice).device.bus_nr, (*pciDevice).device.device_nr, (*pciDevice).device.function_nr, 0x10 + baseAddressRegister * sizeof(DWORD), &previousValue);
	
	//Okay, that's done. Now determine the length of the region by writing all bits 1 
	//to this address field and reading the result out. The io/memory bit 1 is preserved!
	kernelBusPCIWriteConfig32((*pciDevice).device.bus_nr, (*pciDevice).device.device_nr, (*pciDevice).device.function_nr, 0x10 + baseAddressRegister * sizeof(DWORD), 0xfffffffe | (*type & 1));
	
	kernelBusPCIReadConfig32((*pciDevice).device.bus_nr, (*pciDevice).device.device_nr, (*pciDevice).device.function_nr, 0x10 + baseAddressRegister * sizeof(DWORD), length);
	
	if((*type & 1) == PCI_IO_ADDRESS)
		*length &= 0xfffffffc;
	else
		*length &= 0xfffffff0;
	
	kernelBusPCIWriteConfig32((*pciDevice).device.bus_nr, (*pciDevice).device.device_nr, (*pciDevice).device.function_nr, 0x10 + baseAddressRegister * sizeof(DWORD), previousValue);
	
	return (status = 0);
}

int kernelBusPCIEnable(kernelBusPCIDevice * pciDevice)
{
//	WORD powerSettings;
	WORD commandRegister;
	
	int status;
	
	if(!pciDevice)
	{
#if defined(PCI_DEBUG)
		kernelLog("kernelBusPCIEnable: pciDevice == NULL\n");
#endif
		return (status = ERR_NULLPARAMETER);
	}	
	
	
	//TODO: Activate the device if it was in power-save mode
/*	kernelBusPCIReadConfig16((*pciDevice).device.bus_nr, (*pciDevice).device.device_nr, (*pciDevice).device.function_nr, 2, &powerSettings);
	
	powerSettings |= 0x800;
	
	kernelBusPCIWriteConfig16((*pciDevice).device.bus_nr, (*pciDevice).device.device_nr, (*pciDevice).device.function_nr, 2, powerSettings); */
	
	//Activate IO and Memory IO
	kernelBusPCIReadConfig16((*pciDevice).device.bus_nr, (*pciDevice).device.device_nr, (*pciDevice).device.function_nr, 3, &commandRegister);
	
	commandRegister |= 3;
	
	kernelBusPCIWriteConfig16((*pciDevice).device.bus_nr, (*pciDevice).device.device_nr, (*pciDevice).device.function_nr, 3, commandRegister);
	
	return (status = 0);
}

int kernelBusPCIDisable(kernelBusPCIDevice * pciDevice)
{
//	WORD powerSettings;
	WORD commandRegister;
	
	int status;
	
	if(!pciDevice)
	{
#if defined(PCI_DEBUG)
		kernelLog("kernelBusPCIDisable: pciDevice == NULL\n");
#endif
		return (status = ERR_NULLPARAMETER);
	}
	
	//TODO: Activate the device if it was in power-save mode
/*	kernelBusPCIReadConfig16((*pciDevice).device.bus_nr, (*pciDevice).device.device_nr, (*pciDevice).device.function_nr, 2, &powerSettings);
	
	powerSettings |= 0x800;
	
	kernelBusPCIWriteConfig16((*pciDevice).device.bus_nr, (*pciDevice).device.device_nr, (*pciDevice).device.function_nr, 2, powerSettings); */
	
	//Activate IO and Memory IO
	kernelBusPCIReadConfig16((*pciDevice).device.bus_nr, (*pciDevice).device.device_nr, (*pciDevice).device.function_nr, 3, &commandRegister);
	
	commandRegister &= 0xfffc;
	
	kernelBusPCIWriteConfig16((*pciDevice).device.bus_nr, (*pciDevice).device.device_nr, (*pciDevice).device.function_nr, 3, commandRegister);
	
	return (status = 0);
}

int kernelBusPCISetMaster(kernelBusPCIDevice * pciDevice)
{
	//Sets the device to be busmaster -> transfers are quicker
	WORD commandRegister;
	
	BYTE latency;
	
	int status;
	
	if(!pciDevice)
	{
#if defined(PCI_DEBUG)
		kernelLog("kernelBusPCISetMaster: pciDevice == NULL\n");
#endif
		return (status = ERR_NULLPARAMETER);
	}
	
	//Read current settings from command register
	kernelBusPCIReadConfig16((*pciDevice).device.bus_nr, (*pciDevice).device.device_nr, (*pciDevice).device.function_nr, 2, &commandRegister);
	
	//toggle busmaster-Bit on
	commandRegister |= 4;
	
	//write config back
	kernelBusPCIWriteConfig16((*pciDevice).device.bus_nr, (*pciDevice).device.device_nr, (*pciDevice).device.function_nr, 2, commandRegister);
	
	//Check latency timer
	kernelBusPCIReadConfig8((*pciDevice).device.bus_nr, (*pciDevice).device.device_nr, (*pciDevice).device.function_nr, 13, &latency);
	
	if(latency < 0x10) latency = 0x40;
	
	kernelBusPCIWriteConfig8((*pciDevice).device.bus_nr, (*pciDevice).device.device_nr, (*pciDevice).device.function_nr, 13, latency);
	
	return (status = 0);
}
	
	 
	
	

/*
int kernelBusPCIGetDeviceName(WORD vendorID, WORD deviceID, int vendorLength, char * vendorName, int deviceLength, char * deviceName)
{
//Gets a human readable name for a device id and vendor id
   
	int status;
	
	fileStream pciIDList;
	
	char * tmpString;
	
	char idString[5];
	
	WORD id;
	
	//open the file with the Info(path is can be changed)
	status = kernelFileStreamOpen("/system/pcilist.txt", OPENMODE_READ, &pciIDList); 
   
	if(status < 0) 
		return (status);
		
	tmpString = (char *) kernelMalloc(MAX_CONFIG_LINE_LENGTH);
	
	if(tmpString == NULL)
		return (status = -2);
	
	//find vendor ID
	while(1)
	{
		status = kernelFileStreamReadLine(&pciIDList, MAX_CONFIG_LINE_LENGTH, tmpString);
	
		if(status < 0) 
			return (status);
		
		//ignore comment lines and device lines
		if(tmpString[0] != ';' && tmpString[0] != '\t')
		{
			idString[0] = tmpString[0];
			idString[1] = tmpString[1];
			idString[2] = tmpString[2];
			idString[3] = tmpString[3];
			idString[4] = '\0';
			
			id = hexStringToInt(idString);
			
			if(id == vendorID)
			{	
				kernelLog(tmpString);
				//status still holds the line length 
				if(vendorLength > status - 5) vendorLength = status - 5;
				
				//copy the vendorname string to its destination
				kernelMemCopy((void *) &tmpString[5], (void *) vendorName, vendorLength - 1);
				
				//terminate the string porperly
				vendorName[vendorLength] = '\0';
				
				break;
			}
		}
	}
	
	while(1)
	{
		status = kernelFileStreamReadLine(&pciIDList, MAX_CONFIG_LINE_LENGTH, tmpString);
	
		if(status < 0) 
			return (status);
		
		//ignore comment lines
		if(deviceName[0] != ';')
		{
			//if next vendor id is there device ID is not found
			if(deviceName[0] != 't')
			{
				return (status = -1);
			}
			
			//Copy device id into seperate string
			idString[0] = tmpString[1];
			idString[1] = tmpString[2];
			idString[2] = tmpString[3];
			idString[3] = tmpString[4];
			idString[4] = '\0';
			
			id = hexStringToInt(idString);
			
			if(id == deviceID)
			{	
				//status still holds the line length 
				if(deviceLength > status - 6) deviceLength = status - 6;
				
				//copy the vendorname string to its destination
				kernelMemCopy((void *) &tmpString[6], (void *) deviceName, deviceLength - 1);
				
				//terminate the string porperly
				deviceName[deviceLength] = '\0';
				
				break;
			}
		}
	}
	
	kernelFree(tmpString);
	
	return (status = 0);
}	
*/
