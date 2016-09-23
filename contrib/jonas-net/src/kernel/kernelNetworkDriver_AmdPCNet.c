#include "kernelBusPCI.h"
#include "kernelLock.h"
#include "kernelNetwork.h"
#include "kernelProcessorX86.h"
#include "kernelLog.h"
#include "kernelMalloc.h"
#include "kernelMemoryManager.h"
#include "kernelPageManager.h"
#include "kernelParameters.h"
#include "kernelMiscFunctions.h"
#include "kernelInterrupt.h"
#include "kernelError.h"
#include "kernelNetworkDriver_AmdPCNetRegisters.h"
#include <errno.h>
#include <stddef.h>


/*
#define NET_TRANSMIT_BUFFER_NUMBER 4
#define NET_TRANSMIT_BUFFER_SIZE 1536
#define NET_TOTAL_TRANSMIT_BUFFER_SIZE NET_TRANSMIT_BUFFER_NUMBER * NET_TRANSMIT_BUFFER_SIZE 
#define NET_RECEIVE_BUFFER_NUMBER 4
#define NET_RECEIVE_BUFFER_SIZE 1536
#define NET_TOTAL_RECEIVE_BUFFER_SIZE NET_RECEIVE_BUFFER_NUMBER * NET_RECEIVE_BUFFER_SIZE
*/
#define PCNET32_LOG_TX_BUFFERS 2
#define PCNET32_LOG_RX_BUFFERS 2

#define TX_RING_SIZE		(1 << (PCNET32_LOG_TX_BUFFERS))
#define TX_RING_MOD_MASK	(TX_RING_SIZE - 1)
#define TX_RING_LEN_BITS	((PCNET32_LOG_TX_BUFFERS) << 12)

#define RX_RING_SIZE		(1 << (PCNET32_LOG_RX_BUFFERS))
#define RX_RING_MOD_MASK	(RX_RING_SIZE - 1)
#define RX_RING_LEN_BITS	((PCNET32_LOG_RX_BUFFERS) << 4)

#define NET_TRANSMIT_BUFFER_NUMBER TX_RING_SIZE
#define NET_TRANSMIT_BUFFER_SIZE 1536
#define NET_TOTAL_TRANSMIT_BUFFER_SIZE NET_TRANSMIT_BUFFER_NUMBER * NET_TRANSMIT_BUFFER_SIZE 
#define NET_RECEIVE_BUFFER_NUMBER RX_RING_SIZE
#define NET_RECEIVE_BUFFER_SIZE 1536
#define NET_TOTAL_RECEIVE_BUFFER_SIZE NET_RECEIVE_BUFFER_NUMBER * NET_RECEIVE_BUFFER_SIZE

#define NET_DEBUG

static int kernelNetworkDriverInitialize_AmdPCNet(kernelNetworkInterface * nic);
static int kernelNetworkDriverDestroy_AmdPCNet(kernelNetworkInterface * nic);
static int kernelNetworkDriverHandleInterrupt_AmdPCNet(void * data);
static int kernelNetworkDriverTransmit_AmdPCNet(kernelNetworkInterface * nic, void * data, int length);

//one element of the transmit ring
typedef struct
{
	unsigned long bufferBaseAddress;
	
	signed short bufferLength;
	
	signed short statusBits;
	
	unsigned long miscBits;
	
	unsigned long reserved;
} amdPCNetTransmitDescriptor;

//One element of the receive ring
typedef struct 
{
	//the physical address of the buffer this descriptor points to
	unsigned long bufferBaseAddress;
	//the two's complement of the buffer length
	signed short bufferLength;
	//some bits to set extra options
	signed short statusBits;
	//length of the received message
	unsigned long messageLength;
	
	unsigned long reserved;
} amdPCNetReceiveDescriptor;

typedef struct
{
	struct
	{
		unsigned short mode;
		unsigned char receiveBufferLength;
		unsigned char transmitBufferLength;
		unsigned char physicalAddress[6];
		unsigned short reserved;
		unsigned long filter[2];
		unsigned long receiveRingPointer;
		unsigned long transmitRingPointer;
	} initBlock;
	
	amdPCNetTransmitDescriptor transmitDescriptor[NET_TRANSMIT_BUFFER_NUMBER];
	
	amdPCNetReceiveDescriptor receiveDescriptor[NET_RECEIVE_BUFFER_NUMBER];
	
	unsigned char transmitBuffer[NET_TRANSMIT_BUFFER_NUMBER][NET_TRANSMIT_BUFFER_SIZE];
	
	unsigned char receiveBuffer[NET_RECEIVE_BUFFER_NUMBER][NET_RECEIVE_BUFFER_SIZE];
	
	int currentTransmitBuffer;
	
	int currentReceiveBuffer;
	
	void * physicalAddress;	//physical address of this structure	
} amdPCNetPrivate;

//The PCNet chip can be accessed in two ways: first in word mode, and second in doubleword mode. In DW mode only the lower 2 byte contain data, but we use only word mode, which is mandytorily supported by all devices

static inline void amdPCNetReadControlStatusRegister(unsigned long baseIOAddress, int registerIndex, unsigned short * result)
{
	kernelProcessorOutPort16(baseIOAddress + 0x12, registerIndex);
	kernelProcessorInPort16(baseIOAddress + 0x10, *result);
}

static inline void amdPCNetWriteControlStatusRegister(unsigned long baseIOAddress, int registerIndex, unsigned short value)
{
	kernelProcessorOutPort16(baseIOAddress + 0x12, registerIndex);
	kernelProcessorOutPort16(baseIOAddress + 0x10, value);
}

static void amdPCNetReadBusControlRegister(unsigned long baseIOAddress, int registerIndex, unsigned short * result)
{
	kernelProcessorOutPort16(baseIOAddress + 0x12, registerIndex);
	kernelProcessorInPort16(baseIOAddress + 0x16, *result);
}

static inline void amdPCNetWriteBusControlRegister(unsigned long baseIOAddress, int registerIndex, unsigned short value)
{
	kernelProcessorOutPort16(baseIOAddress + 0x12, registerIndex);
	kernelProcessorOutPort16(baseIOAddress + 0x16, value);
}


static void amdPCNetReadRegisterAddressPort(unsigned long baseIOAddress, unsigned short * result)
{
	kernelProcessorInPort16(baseIOAddress + 0x12, *result);
}

static void amdPCNetWriteRegisterAddressPort(unsigned long baseIOAddress, unsigned short value)
{
	kernelProcessorOutPort16(baseIOAddress + 0x12, value);
}

static inline void amdPCNetReset(unsigned long baseIOAddress) 
{
	unsigned short tmp;
	
	//first 32bit-reset
	kernelProcessorOutPort16(baseIOAddress + 0x18, 0x0000);
	
	kernelProcessorInPort16(baseIOAddress + 0x18, tmp);
	//then 16bit-reset so chip is afterwards reset and in 16bit mode
	kernelProcessorOutPort16(baseIOAddress + 0x14, 0x0000);
	
	kernelProcessorInPort16(baseIOAddress + 0x14, tmp);
}

#if defined(NET_DEBUG)
static void debugDumpRegisters(kernelNetworkInterface * nic)
{
	unsigned short tmp;
	unsigned short tmp2;
	unsigned short tmp3;

	//the CSR0 register
	amdPCNetReadControlStatusRegister(nic->portIOAddress, 0, &tmp);
	kernelLog("%s->CSR0: %04x", nic->name, tmp); 
	//the CSR3 register
	amdPCNetReadControlStatusRegister(nic->portIOAddress, 3, &tmp);
	kernelLog("%s->CSR3: %04x", nic->name, tmp); 
	//the CSR4 register
	amdPCNetReadControlStatusRegister(nic->portIOAddress, 4, &tmp);
	kernelLog("%s->CSR4: %04x", nic->name, tmp);
	//the CSR5 register
	amdPCNetReadControlStatusRegister(nic->portIOAddress, 5, &tmp);
	kernelLog("%s->CSR5: %04x", nic->name, tmp);  
	//the CSR6 register
	amdPCNetReadControlStatusRegister(nic->portIOAddress, 6, &tmp);
	kernelLog("%s->CSR6: %04x", nic->name, tmp); 
	//the CSR6 register
	//tell chip to stop first or we cannot read address
	amdPCNetReadControlStatusRegister(nic->portIOAddress, 0, &tmp);
	amdPCNetWriteControlStatusRegister(nic->portIOAddress, 0, tmp | CSR0_STOP);
	amdPCNetReadControlStatusRegister(nic->portIOAddress, 12, &tmp);
	amdPCNetReadControlStatusRegister(nic->portIOAddress, 13, &tmp2);
	amdPCNetReadControlStatusRegister(nic->portIOAddress, 14, &tmp3);
	kernelLog("%s->physicalAddress: %02x:%02x:%02x:%02x:%02x:%02x", nic->name, tmp & 0xff, tmp >> 8, tmp2 & 0xff, tmp2 >> 8, tmp3 & 0xff, tmp3 >> 8); 
	amdPCNetReadControlStatusRegister(nic->portIOAddress, 0, &tmp);
	amdPCNetWriteControlStatusRegister(nic->portIOAddress, 0, tmp | CSR0_START);
	
}
#endif

/**
 *	Initializes the chip with its basic data.
 *	Sets up mode, etc and gives the NIC an init block from which it reads the configuration values.
 */	
static void amdPCNetInitChip(kernelNetworkInterface * nic)
{
	amdPCNetPrivate * privateVirtual = (amdPCNetPrivate *) nic->privateData;
	amdPCNetPrivate * privatePhysical = (amdPCNetPrivate *) privateVirtual->physicalAddress;
	unsigned short tmp;
	int i;
	
	//Setup the init block
//	privateVirtual->initBlock.mode = 0x0000;
//	privateVirtual->initBlock.receiveBufferLength = 2; //real length = 2^2 = 4
//	privateVirtual->initBlock.transmitBufferLength = 2; //real length = 2^2 = 4 
/*
	privateVirtual->initBlock.physicalAddress[0] = 0x09;
	privateVirtual->initBlock.physicalAddress[1] = 0x18;
	privateVirtual->initBlock.physicalAddress[2] = 0x27;
	privateVirtual->initBlock.physicalAddress[3] = 0x36;
	privateVirtual->initBlock.physicalAddress[4] = 0x45;
	privateVirtual->initBlock.physicalAddress[5] = 0x54;
	i = 0;
*/
/*	for(i = 0;  i< 6; i++)
		privateVirtual->initBlock.physicalAddress[i] = nic->myAddress[i];

	privateVirtual->initBlock.reserved = 0x0000;
	privateVirtual->initBlock.filter[0] = 0x00000000;
	privateVirtual->initBlock.filter[1] = 0x00000000;
	privateVirtual->initBlock.receiveRingPointer = (unsigned long) &privatePhysical->receiveDescriptor[0];
	privateVirtual->initBlock.transmitRingPointer = (unsigned long) &privatePhysical->transmitDescriptor[0];
	
	if((unsigned long) &privatePhysical->initBlock.mode & 0x00000003)
		kernelError(kernel_error, "%s: physical address of init block is not 32bit-aligned", nic->name); */
//	if(privateVirtual->initBlock.receiveRingPointer & 0x00000003)
//		kernelError(kernel_error, "%s: physical address of RX descriptor is not 32bit-aligned", nic->name);	
//	if(privateVirtual->initBlock.transmitRingPointer & 0x00000003)
//		kernelError(kernel_error, "%s: physical address of TX descriptor is not 32bit-aligned", nic->name);
	
	//reset the chip
	amdPCNetReset(nic->portIOAddress);
	
	//stop the chip
	amdPCNetWriteControlStatusRegister(nic->portIOAddress, CONTROLLER_STATUS, CSR0_STOP);
	//wait a short while to give the chip time to reset
	kernelProcessorDelay();
	//Tell the chip we want 32bit mode (this method is different from the one described in the specs)
	amdPCNetReadControlStatusRegister(nic->portIOAddress, SOFTWARE_MODE, &tmp);
	amdPCNetWriteControlStatusRegister(nic->portIOAddress, SOFTWARE_MODE, tmp | CSR58_SOFTWARE_32BIT);
	//Set some options
	amdPCNetReadControlStatusRegister(nic->portIOAddress, TEST_FEATURES_CONTROL, &tmp);
	amdPCNetWriteControlStatusRegister(nic->portIOAddress, TEST_FEATURES_CONTROL, tmp | CSR4_NO_TRANSMIT_INTERRUPT | CSR4_AUTO_PAD_TRANSMIT | CSR4_DISABLE_TX_POLLING | CSR4_DMAPLUS);
	//Emable burst read and write
	amdPCNetReadBusControlRegister(nic->portIOAddress, BURST_CONTROL, &tmp);
	amdPCNetWriteBusControlRegister(nic->portIOAddress, BURST_CONTROL, tmp | BCR18_BURST_READ_ENABLED | BCR18_BURST_WRITE_ENABLED);
/*	//write address of init block to chip
	amdPCNetWriteControlStatusRegister(nic->portIOAddress, INIT_BLOCK_ADDRESS_LOW, (unsigned long) &privatePhysical->initBlock & 0xffff);
	amdPCNetWriteControlStatusRegister(nic->portIOAddress, INIT_BLOCK_ADDRESS_HIGH, ((unsigned long) &privatePhysical->initBlock >> 16) & 0xffff); */
	//set mode 0 (default)
	amdPCNetWriteControlStatusRegister(nic->portIOAddress, MODE,  0x0000);
	amdPCNetWriteControlStatusRegister(nic->portIOAddress, 76, -NET_RECEIVE_BUFFER_NUMBER & 0xffff);
	amdPCNetWriteControlStatusRegister(nic->portIOAddress, 78, -NET_TRANSMIT_BUFFER_NUMBER & 0xffff);
	amdPCNetWriteControlStatusRegister(nic->portIOAddress, 24, (unsigned long) &privatePhysical->receiveDescriptor[0] & 0xffff);
	amdPCNetWriteControlStatusRegister(nic->portIOAddress, 25, ((unsigned long) &privatePhysical->receiveDescriptor[0] >> 16) & 0xffff);
	amdPCNetWriteControlStatusRegister(nic->portIOAddress, 30, (unsigned long) &privatePhysical->transmitDescriptor[0] & 0xffff);
//	amdPCNetWriteControlStatusRegister(nic->portIOAddress, 31, ((unsigned long) &privatePhysical->transmitDescriptor[0] >> 16) & 0xffff);
	for(i = 0; i < 4; i++)
		amdPCNetWriteControlStatusRegister(nic->portIOAddress, i + 8, 0x0000);
	for(i = 0; i < 3; i++)
		amdPCNetWriteControlStatusRegister(nic->portIOAddress, i + 12, nic->myAddress[i * 2] | (nic->myAddress[i * 2 + 1] << 8));
//	amdPCNetWriteControlStatusRegister(nic->portIOAddress, 25, ((unsigned long) &privatePhysical->receiveDescriptor[0] >> 16) & 0xffff);
	//start chip
	amdPCNetWriteControlStatusRegister(nic->portIOAddress, CONTROLLER_STATUS, CSR0_START | CSR0_INTERRUPT_ENABLE);
	//Get link status
	amdPCNetReadBusControlRegister(nic->portIOAddress, LINK_STATUS, (unsigned short *) &nic->linkStatus);
#if defined(NET_DEBUG)
	kernelLog("%s: Link status is %x", nic->name, nic->linkStatus);
#endif  
}

/**
	Function for the NIC driver. Opens the device and initializes all associated data structures.
	Can be called several times, previously used memory is freed.

*/

static int kernelNetworkDriverInitialize_AmdPCNet(kernelNetworkInterface * nic)
{	
	
	int status = 0;
	int i;
	unsigned short tmp;
	amdPCNetPrivate * privateVirtual;
	amdPCNetPrivate * privatePhysical;
	
	//Check parameters
	if(!nic)
	{
#if defined(NET_DEBUG)
		kernelLog("kernelNetworkDriverInitialize_AmdPCNet: nic == NULL\n");
#endif
		return (status = ERR_NULLPARAMETER);
	}
	
	if(nic->portIOAddress == 0xffffffff)
	{
#if defined(NET_DEBUG)		
		//no valid port address
		kernelError(kernel_error, "kernelNetworkDriverInitialize_AmdPCNet: Device port is not valid.\n");
#endif		
		return (status = ERR_IO);
	} 
	
	if(nic->privateData)
	{
#if defined(NET_DEBUG)		
		kernelLog("kernelNetworkDriverInitialize_AmdPCNet: nic->privateData is not NULL. trying to free it.\n");
#endif			
		kernelFree(nic->privateData);
	}
	
	if(nic->privateData)
	{
#if defined(NET_DEBUG)		
		kernelLog("kernelNetworkDriverInitialize_AmdPCNet: nic->privateData is not NULL. trying to free it.\n");
#endif		
		kernelPageUnmap(KERNELPROCID, nic->privateData, sizeof(amdPCNetPrivate));
		kernelFree(nic->privateData);
	}
	
	//reset the NIC's chip
	amdPCNetReset(nic->portIOAddress);
	
	//read the physical adress from PROM
	for(i = 0; i < 6; i++)
		kernelProcessorInPort8(nic->portIOAddress + i, nic->myAddress[i]);
		
	//Get chip version and name
	amdPCNetReadControlStatusRegister(nic->portIOAddress, 89, &tmp);
	nic->version = (unsigned long) tmp << 16;
	amdPCNetReadControlStatusRegister(nic->portIOAddress, 88, &tmp);
	nic->version = ((nic->version | tmp) >> 12) & 0xffff;
	switch(nic->version)
	{
		case 0x2420:
			nic->name = "PCnet/PCI 79C970"; // PCI 
			break;
		case 0x2621:
			nic->name = "PCnet/PCI II 79C970A"; // PCI 
			break;
		case 0x2623:
			nic->name = "PCnet/FAST 79C971"; // PCI 
			break;
		case 0x2624:
			nic->name = "PCnet/FAST+ 79C972"; // PCI 
			break;
		case 0x2625:
			nic->name = "PCnet/FAST III 79C973"; // PCI 
			break;
		case 0x2626:
			nic->name = "PCnet/Home 79C978"; // PCI 
			break;
		case 0x2627:
			nic->name = "PCnet/FAST III 79C975"; // PCI 
			break;
		case 0x2628:
			nic->name = "PCnet/PRO 79C976";
			break;
	}
#if defined(NET_DEBUG)
	kernelLog("%s: My address is: %x:%x:%x:%x:%x:%x\n", nic->name, nic->myAddress[0], nic->myAddress[1], nic->myAddress[2], nic->myAddress[3], nic->myAddress[4], nic->myAddress[5]);
#endif
	
	//Allocate physical space for the NIC's private structure
	privatePhysical = (amdPCNetPrivate *) kernelMemoryGetPhysical(sizeof(amdPCNetPrivate), MEMORY_PAGE_SIZE, "AMD PCNet buffers");
	if(!privatePhysical)
	{
		kernelError(kernel_error, "kernelNetworkDriverInitialize_AmdPCNet: Can't allocate memory for buffers");
		((kernelNetworkDriver * ) nic->driver)->destroy(nic);
		return(status = ERR_MEMORY);
	}
	
	//map the physical address from above to a kernel virtual address
	status = kernelPageMapToFree(KERNELPROCID, privatePhysical, (void **) &privateVirtual, sizeof(amdPCNetPrivate));
	if(status < 0)
	{
		kernelError(kernel_error, "kernelNetworkDriverInitialize_AmdPCNet: Can't map physical address of buffers to virtual address");
		((kernelNetworkDriver * ) nic->driver)->destroy(nic);
		return(status);
	}
	
	//Save pointers in the NIC structure
	privateVirtual->physicalAddress = (void *) privatePhysical;
	nic->privateData = (void *) privateVirtual;
	//Clear the memory
	kernelMemClear(privateVirtual, sizeof(amdPCNetPrivate));
	//Hook the card's interrupt BEFORE card can issue any interrupts
	kernelInterruptHookShared(nic->irq, kernelNetworkDriverHandleInterrupt_AmdPCNet, (void *) nic);
	//Set up some registers, setup and load init block and start card
	amdPCNetInitChip(nic);
	
	
//	debugInterruptDo(10);
	
/*	 
	//Get and save the MAC address and the broadcast address
	for(i = 0; i < 3; i++)
	{
		amdPCNetReadControlStatusRegister(nic->portIOAddress, i + 0x0c, &tmp);
		 
		nic->myAddress[i * 2] = (unsigned char) (tmp & 0xff);
		
		nic->myAddress[i * 2 + 1] = (unsigned char) ((tmp >> 8) & 0xff);
		
		nic->broadcastAddress[i * 2] = 0xff;
		
		nic->broadcastAddress[i * 2 + 1] = 0xff;
	}
	
	kernelLog("My MAC address: %x:%x:%x:%x:%x:%x\n", nic->myAddress[0], nic->myAddress[1], nic->myAddress[2], nic->myAddress[3], nic->myAddress[4], nic->myAddress[5]); 
	
	//Get chip version
	amdPCNetReadControlStatusRegister(nic->portIOAddress, 89, &tmp);
	chipVersion = (unsigned long) tmp << 16;
	amdPCNetReadControlStatusRegister(nic->portIOAddress, 88, &tmp);
	chipVersion |= tmp;
	chipVersion = (chipVersion >> 12) & 0xffff;
	
	switch(chipVersion)
	{
		case 0x2420:
			chipName = "PCnet/PCI 79C970"; // PCI 
			break;
		case 0x2430:
			chipName = "PCnet/32 79C965"; // 486/VL bus 
			break;
		case 0x2621:
			chipName = "PCnet/PCI II 79C970A"; // PCI 
			break;
		case 0x2623:
			chipName = "PCnet/FAST 79C971"; // PCI 
			break;
		case 0x2624:
			chipName = "PCnet/FAST+ 79C972"; // PCI 
			break;
		case 0x2625:
			chipName = "PCnet/FAST III 79C973"; // PCI 
			break;
		case 0x2626:
			chipName = "PCnet/Home 79C978"; // PCI 
			break;
		case 0x2627:
			chipName = "PCnet/FAST III 79C975"; // PCI 
			break;
		case 0x2628:
			chipName = "PCnet/PRO 79C976";
			break;
	}
	//TODO: Register interrupt
	
	if((chipVersion != 0x2420 && chipVersion < 0x2621 && chipVersion > 0x2628) || chipVersion == 0x2622)
	{
		//unsupported chip
		//free resources and exit
#if defined(NET_DEBUG)
		kernelLog("Chip version \"%s\" of the AMD PCNet32 NIC is not supported!\n", chipName);
#endif
		((kernelNetworkDriver * ) nic->driver)->destroy(nic);
		
		return(status = ERR_INVALID);
	}
		
#if defined(NET_DEBUG)	
	kernelLog("Chip Name %s recognized (ID: %x)\n", chipName, chipVersion);	
#endif

	//Save the name of this network device
	//TODO: What if there are two identical NICs in a computer?
	nic->name = chipName;
	
	//reset the chip again
	amdPCNetReset(nic->portIOAddress);
	
	//Clear logical address filter (since we want to receive packets from all MAC addresses)
	for(i = 0; i < 4; i++)
	{
		amdPCNetWriteControlStatusRegister(nic->portIOAddress, i + 8, 0x0000);
	} 
	
	//program number of descriptors in transmit ring
	//number to be set is the two's complement of the real number
	amdPCNetWriteControlStatusRegister(nic->portIOAddress, 78, -NET_TRANSMIT_BUFFER_NUMBER);
	
	//program number of descriptors in receive ring
	amdPCNetWriteControlStatusRegister(nic->portIOAddress, 76, -NET_RECEIVE_BUFFER_NUMBER);
	
	//allocate space for the device private structure 
	//(the transmit and receive ring structures, the transmit and the receive buffer)
	//memory must be 32bit-aligned
	//TODO: Memory allocation is suboptimal since whole block is used. What to do?
	privatePhysicalAddress = (amdPCNetPrivate *) kernelMemoryGetPhysical(sizeof(amdPCNetPrivate), MEMORY_PAGE_SIZE, "AMD PCNet buffers");
	
	if(!privatePhysicalAddress)
	{
#if defined(NET_DEBUG)
		kernelLog("Can't allocate memory for buffers\n");
#endif
		((kernelNetworkDriver * ) nic->driver)->destroy(nic);
		
		return(status = ERR_MEMORY);
	}
	
	//map the physical address from above to a kernel virtual address
	status = kernelPageMapToFree(KERNELPROCID, privatePhysicalAddress, (void **) &nic->privateData, sizeof(amdPCNetPrivate));

	if(status < 0)
	{
#if defined(NET_DEBUG)
		kernelLog("Can't map physical address of transmit buffer to virtual address\n");
#endif
		((kernelNetworkDriver * ) nic->driver)->destroy(nic);
		
		return(status);
	}
	
	//save the physical address of the NIC's private structure
	((amdPCNetPrivate *) nic->privateData)->thisStructurePhysicalAddress = (void *) privatePhysicalAddress;
	
	//clear the NIC's private structure
	kernelMemClear(nic->privateData, sizeof(amdPCNetPrivate));
	
	//Setup the init block used to intialize the NIC
	//Save pointers to transmit/receive ring into init block
	((amdPCNetPrivate *) nic->privateData)->initBlock.receiveRingPointer = (unsigned long) &privatePhysicalAddress->receiveDescriptor[0];
	((amdPCNetPrivate *) nic->privateData)->initBlock.transmitRingPointer = (unsigned long) &privatePhysicalAddress->transmitDescriptor[0];
	//disable transmit and receive
	((amdPCNetPrivate *) nic->privateData)->initBlock.mode = 0x0003;
	//set receive and transmit ring size
	((amdPCNetPrivate *) nic->privateData)->initBlock.receiveBufferLength_transmitBufferLength = TX_RING_LEN_BITS | RX_RING_LEN_BITS;
	//set physical address
    	for (i = 0; i < 6; i++)
		((amdPCNetPrivate *) nic->privateData)->initBlock.physicalAddress[i] = nic->myAddress[i];
	//Clear filters
	((amdPCNetPrivate *) nic->privateData)->initBlock.filter[0] = 0x00000000;
	((amdPCNetPrivate *) nic->privateData)->initBlock.filter[1] = 0x00000000; 
	
	//Switch the NIC to 32 bit mode
	amdPCNetWriteBaseControlRegister(nic->portIOAddress, 20, 2);
	
	//give the NIC the address of the initialisation block
	amdPCNetWriteBaseControlRegister(nic->portIOAddress, 1, (unsigned short) ((unsigned long) &privatePhysicalAddress->initBlock &0xffff));
	amdPCNetWriteBaseControlRegister(nic->portIOAddress, 2, (unsigned short) ((unsigned long) &privatePhysicalAddress->initBlock >> 16)); 
	
	//Clear both logical address filters
	for(i = 0; i < 4; i++)
		amdPCNetWriteControlStatusRegister(nic->portIOAddress, i + 8, 0x0000);
	
	//store length of receive ring (value is the negative of the real length)
	amdPCNetWriteControlStatusRegister(nic->portIOAddress, 76, -RX_RING_SIZE);
	//store length of transmit ring (value is the negative of the real length)
	amdPCNetWriteControlStatusRegister(nic->portIOAddress, 78, -TX_RING_SIZE);
	
	//store physical address of first transmit descriptor in the NIC's registers
	amdPCNetWriteControlStatusRegister(nic->portIOAddress, 31, (unsigned short) ((unsigned long) &privatePhysicalAddress->transmitDescriptor[0] & 0xffff));
	
	amdPCNetWriteControlStatusRegister(nic->portIOAddress, 31, (unsigned short) ((unsigned long) &privatePhysicalAddress->transmitDescriptor[0] >> 16));
	
	//store physical address of first receive descriptor in the NIC's registers
	amdPCNetWriteControlStatusRegister(nic->portIOAddress, 25, (unsigned short) ((unsigned long) &privatePhysicalAddress->receiveDescriptor[0] & 0xffff));
	
	amdPCNetWriteControlStatusRegister(nic->portIOAddress, 25, (unsigned short) ((unsigned long) &privatePhysicalAddress->receiveDescriptor[0] >> 16));
	
	//Set default values in the mode register
	amdPCNetWriteControlStatusRegister(nic->portIOAddress, 15, 0x0000);

	//enable the card's leds
	amdPCNetWriteBusControlRegister(nic->portIOAddress, 2, 0x1002);
	
	//Hook the card's interrupt
	kernelInterruptHookShared(nic->irq, kernelNetworkDriverHandleInterrupt_AmdPCNet, (void *) nic);
	//start the *** thing
//	amdPCNetWriteControlStatusRegister(nic->portIOAddress, 4, 0x9150);
	amdPCNetWriteControlStatusRegister(nic->portIOAddress, 0, 0x0042);
	
	amdPCNetReadControlStatusRegister(nic->portIOAddress, 0, &tmp);
	
	kernelError(kernel_error, "CSR0: %x", tmp); */
	
/*	//wait until it is initialized
	for(i = 0; i < 100; i++)
	{
		amdPCNetReadControlStatusRegister(nic->portIOAddress, 0, &tmp);
		if(tmp & 0x0100) break;
 	}
	
	kernelError(kernel_error, "device has private struct at %x", privatePhysicalAddress);
	
	amdPCNetWriteControlStatusRegister(nic->portIOAddress, 0, 0x0042);
*/	
	return (status = 0);
}
/***************************************************************************************************************/
/**
 *	Releases all memory associated with the network device.
 *	Frees the pci information structure nic->busData, the NIC's private structure nic->privateData, 
 *	and at last the NIC's structure intself (nic). Unhooks the cards interrupt before it does so.
 *	NOTE: If you just want to reinitialize the pcnet, call kernelNetworkDriverInitialize again, it will 
 *	free memory previously used.
 */
static int kernelNetworkDriverDestroy_AmdPCNet(kernelNetworkInterface * nic)
{
	int status;
	
	void * privatePhysical = NULL; 
	//TODO: unregister interrupt
	//get physical address of the private structure
	if(!nic || !nic->privateData) privatePhysical = ((amdPCNetPrivate *) nic->privateData)->physicalAddress;
	//disable the device (routine just returns errorcode if nic->busData is NULL)
	if(!nic) kernelBusPCIDisable((kernelBusPCIDevice *) (nic->busData));
	//free memory used by bus data structure (p. ex. kernelBusPCIDevice)
	if(!nic || !nic->busData) kernelFree(nic->busData);
	//free memory used by device private structure
	if(!nic || !nic->privateData) kernelPageUnmap(KERNELPROCID, nic->privateData, sizeof(amdPCNetPrivate));
	//release physical memory used by private structure
	if(!privatePhysical) kernelMemoryReleasePhysical(privatePhysical);
	//free memory used by driver structure
	if(!nic || !nic->driver) kernelFree(nic->driver);
	//free memory used by NIC structure
	if(!nic) kernelFree((void *) nic);
	
	return (status = 0);
}
/***************************************************************************************************************/
static int kernelNetworkDriverHandleInterrupt_AmdPCNet(void * data)
{
	int status;
	kernelNetworkInterface * nic = (kernelNetworkInterface *) data;
	unsigned short oldRapValue; //the value of the RegisterAddressPort before we modify it
	unsigned short tmp;
	
	kernelError(kernel_error, "Jippee! %s is handling an Interrupt!", nic->name);
	//read value of register address port
	amdPCNetReadRegisterAddressPort(nic->portIOAddress, &oldRapValue);
	amdPCNetReadControlStatusRegister(nic->portIOAddress, CONTROLLER_STATUS, &tmp);
	if(!(tmp & CSR0_INTERRUPT_OCCURED))
	{
		//this device did not cause the interrupt
		//restore RAP and exit
		amdPCNetWriteRegisterAddressPort(nic->portIOAddress, oldRapValue);
		return (status = -1);
	}
	//disable interrupts
	amdPCNetWriteControlStatusRegister(nic->portIOAddress, CONTROLLER_STATUS, 0);
	
	debugDumpRegisters(nic);
	//Restore RAP
	amdPCNetWriteRegisterAddressPort(nic->portIOAddress, oldRapValue);
	
	return (status = 0);
}
/***************************************************************************************************************/
static int kernelNetworkDriverTransmit_AmdPCNet(kernelNetworkInterface * nic, void * data, int length)
{
	int status;
	
	amdPCNetPrivate * privateVirtual = (amdPCNetPrivate *) nic->privateData;
	
	amdPCNetPrivate * privatePhysical = (amdPCNetPrivate *) privateVirtual->physicalAddress;
	
	debugDumpRegisters(nic);
	
	if(length >= NET_TRANSMIT_BUFFER_SIZE) 
	{
#if defined(NET_DEBUG)
		kernelLog("%s: Packet length %u is too long. Max packet length is %u\n", nic->name, length, NET_TRANSMIT_BUFFER_SIZE);
#endif
		return (status = ERR_BOUNDS);
	}
	
	if((*privateVirtual).transmitDescriptor[privateVirtual->currentTransmitBuffer].statusBits & 0x8000)
	{
#if defined(NET_DEBUG)
		kernelLog("%s: still sending packet in slot %u\n", nic->name, privateVirtual->currentTransmitBuffer);
#endif
		return (status = ERR_BUSY);
//		while((*privateData).transmitDescriptor[privateData->currentTransmitBuffer].statusBits & 0x8000);
	}
#if defined(NET_DEBUG)
		kernelLog("%s: sending packet of length %u in slot %u", nic->name, length, privateVirtual->currentTransmitBuffer);
#endif
	//get the lock on the NIC
	kernelLockGet(&nic->interfaceLock);
	//copy the data to the dma accessible memory
	kernelMemCopy(data, (void *) &privateVirtual->transmitBuffer[privateVirtual->currentTransmitBuffer], length);
	//Set the buffer base address in the descriptor
	(*privateVirtual).transmitDescriptor[privateVirtual->currentTransmitBuffer].bufferBaseAddress = (unsigned long) &privatePhysical->transmitBuffer[privateVirtual->currentTransmitBuffer];
	//set the negative message length in the descriptor
	(*privateVirtual).transmitDescriptor[privateVirtual->currentTransmitBuffer].bufferLength = 0xf000 | -length;
	//set the own bit, so the NIC owns this descriptor now
	(*privateVirtual).transmitDescriptor[privateVirtual->currentTransmitBuffer].statusBits = TD_START_OF_PACKET | TD_END_OF_PACKET | TD_OWNED_BY_NIC;
	//Tell the NIC to send the packet
	amdPCNetWriteControlStatusRegister(nic->portIOAddress, CONTROLLER_STATUS, CSR0_INTERRUPT_ENABLE | CSR0_TRANSMIT_DEMAND);
	//use next transmit buffer next time
	privateVirtual->currentTransmitBuffer = (privateVirtual->currentTransmitBuffer + 1) % NET_TRANSMIT_BUFFER_NUMBER;
	//free lock
	kernelLockRelease(&nic->interfaceLock);
	
	return (status = 0);
}


/******************************************************************************/
//		FUNCTION EXPORTED FOR EXTERNAL USE			      //
/******************************************************************************/

void kernelNetworkGetDriver_AmdPCNet(kernelNetworkInterface * nic)
{
	kernelNetworkDriver * driver = (kernelNetworkDriver *) kernelMalloc(sizeof(kernelNetworkDriver));
	
	nic->addressLength = ETHERNET_ADDRESS_LENGTH;
	
	//netDriver->myAddress must be initialized by driver
	
	nic->headerPreambleLength = ETHERNET_PREAMBLE_LENGTH;
	
	driver->initialize = kernelNetworkDriverInitialize_AmdPCNet;
	
	driver->destroy = kernelNetworkDriverDestroy_AmdPCNet;
	
	driver->transmit = kernelNetworkDriverTransmit_AmdPCNet;
	
	nic->driver = (void *) driver;
	
	nic->privateData = (void *) 0;
}

