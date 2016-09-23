#include "kernelMalloc.h"
#include "kernelLog.h"
#include "kernelBusPCI.h"
#include "kernelMiscFunctions.h"
#include "kernelNetwork.h"
#include <errno.h>

#define NET_DEBUG 

//pointers to all kernelNetworkInterface structures
static kernelNetworkInterface * networkInterfaces[NET_MAX_INTERFACE_NUMBER];

static int interfaceCount = 0;

int kernelNetworkRegisterInterface(kernelNetworkInterface * nic)
{
	int status;
	
	if(!nic)
	{
#if defined(NET_DEBUG)
		kernelLog("kernelNetworkRegisterInterface: nic == NULL\n");
#endif
		return (status = ERR_NULLPARAMETER);
	}
	
	if(interfaceCount == NET_MAX_INTERFACE_NUMBER)
	{
#if defined(NET_DEBUG)
		kernelLog("kernelNetworkRegisterInterface: Maximum NIC number %u reached\n", NET_MAX_INTERFACE_NUMBER);
#endif
		return (status = ERR_NOFREE);				
	}
	
	//store pointer to the NIC and increase number of total NICs
	networkInterfaces[interfaceCount++] = nic;
	
	return (status = 0);
}

int kernelNetworkUnregisterInterface(kernelNetworkInterface * nic)
{
	int status, i;
	
	//check parameters
	if(!nic)
	{
#if defined(NET_DEBUG)
		kernelLog("kernelNetworkRegisterInterface: nic == NULL\n");
#endif
		return (status = ERR_NULLPARAMETER);
	}
	
	//find the device number corresponding to this device
	for(i = 0; i < interfaceCount; i++)
	{
		if(networkInterfaces[i] == nic)
		{
			//move all pointers which are above the free slot one down so the free slot is filled
			networkInterfaces[i] = networkInterfaces[i + 1];
	
			//decrease number of installed NICs
			interfaceCount--;
			
			return (status = 0);
		}
	}
	
	return (status = ERR_NOSUCHENTRY);
}

/**
 *	gets the network interface card structure corresponding to the pci device structure
 *	and fills in basic information that it gets from the pci structure
 *	(io port, io memory, irq); enables pci devices 
 */

int kernelNetworkGetInterfacePCI(kernelBusPCIDevice * netDevice, kernelNetworkInterface ** nic)
{	
	unsigned long address;
	
	unsigned long length;
	
	int type, i;
	
	int status = 0;
	
	if(!netDevice)
	{
#if defined(NET_DEBUG)		
		kernelLog("kernelNetworkGetInterfacePCI: netDevice == NULL\n");
#endif		
		return (status = ERR_NULLPARAMETER);
	}
	
	if(!nic)
	{
#if defined(NET_DEBUG)		
		kernelLog("kernelNetworkGetInterfacePCI: nic == NULL\n");
#endif		
		return (status = ERR_NULLPARAMETER);
	}

	//allocate space for the driver structure
	*nic = kernelMalloc(sizeof(kernelNetworkInterface));
	
	if(!nic)
	{
		//We got no memory -> complain and exit with error		
#if defined(NET_DEBUG)		
		kernelLog("kernelNetworkGetInterfacePCI couln't allocate memory for device structure\n");
#endif		
		return (status = ERR_MEMORY);
	} 
	
	//Clear out the device structure
	kernelMemClear((void *) *nic, sizeof(kernelNetworkInterface));
	
	
	//set some name
	//TODO: enumeration routine
	//ISSUE: Should the name be given here or device specific?
	(**nic).name = "net0";
	
	(**nic).portIOAddress = 0xffffffff;
	
	(**nic).physicalMemoryIOAddress = (void *) 0;
	
	//Save pointer to the pci structure
	(**nic).busData = (void *) netDevice;
	
	//Enable the device and set it busmaster
	kernelBusPCIEnable(netDevice);
	
	kernelBusPCISetMaster(netDevice);
	
	//Check the first two BARs for port and memory io addresses
	for(i = 0; i < 2; i++)
	{
		kernelBusPCIGetBaseAddress(netDevice, i, &address, &length, &type);
		
		if(type == PCI_NO_ADDRESS)
		{
			//end of bars, since this is empty
			//If no address was found we have a problem
			if(i == 0)
			{
#if defined(NET_DEBUG)
				kernelLog("kernelNetworkGetInterfacePCI: Card has no resources.\n");
#endif				
				return (status = -ERR_IO);
			}
			
			break;
		}
		
		if((type & 1) == PCI_MEMORY_ADDRESS)
		{
			(**nic).physicalMemoryIOAddress = (void *) address;
			
			(**nic).memoryIOLength = length;
#if defined(NET_DEBUG)			
			kernelLog("kernelNetworkGetInterfacePCI: Card has memory resource %x - %x.\n", (**nic).physicalMemoryIOAddress, (**nic).physicalMemoryIOAddress + (**nic).memoryIOLength);
#endif
		}
		else
		{
			(**nic).portIOAddress = address;
#if defined(NET_DEBUG)
			kernelLog("kernelNetworkGetInterfacePCI: Card has port resource %x.\n", (**nic).portIOAddress);
#endif
		}
	}
	
	(**nic).irq = (*netDevice).device.nonbridge.interrupt_line;
	
	//chooses a network driver for the PCI NIC according to its vendor and device id
	switch((*netDevice).device.vendorID)
	{
		case VENDOR_AMD:
			switch((*netDevice).device.deviceID)
			{
				case DEVICE_NET_PCNET_LANCE_PCI:
#if defined(NET_DEBUG)
					kernelLog("Configuring AMD PCNet32 card.\n");
#endif
					kernelNetworkGetDriver_AmdPCNet(*nic);
					break;	
			}
			break;
		
	}
	
	
	
	return (status);

}; 

/**
 *	Returns the NIC corresponding to this number.
 *	May be used only kernel internally. Interfaces cannot be identified by their number, only by their 
 *	interface structure, as numbering may change at any time.
 */

 
/* 
int kernelNetworkGetInterface(int number, kernelNetworkInterface ** nic)
{
	int status = 0;
		
	//check parameters
	//See if pointer is not NULL
	if(!nic)
	{
#if defined(NET_DEBUG)
		kernelLog("kernelNetworkGetInterface: nic == null\n");
#endif
		return(status = ERR_NULLPARAMETER);
	}
	
	//Se if number is valid value (between 0 and interfaceCount - 1)
	if(number < 0)
	{
#if defined(NET_DEBUG)
		kernelLog("kernelNetworkGetInterface: Negative device number (%d) \n", number);
#endif
		return(status = ERR_INVALID);		
	}
	
	if(number >= interfaceCount)
	{
#if defined(NET_DEBUG)
		kernelLog("kernelNetworkGetInterface: device (%d) is not registered \n", number);
#endif
		return(status = ERR_BOUNDS);		
	}	
		
	*nic = networkInterfaces[number];
	
	return (status = 0);
}

*/




