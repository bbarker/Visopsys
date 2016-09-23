
#define NET_MAX_INTERFACE_NUMBER 20

#define MAX_ADDRESS_LENGTH 6
#define ETHERNET_ADDRESS_LENGTH 6
#define ETHERNET_PREAMBLE_LENGTH 14

//structure holding the statistics of a device
typedef struct
{
} kernelNetworkStatistics;

typedef volatile struct
{
	//name of the network interface (p. ex. eth0, amd_pcnet)
	char * name;
	//The memory io address of this device -> not mandatory
	void * memoryIOAddress;
	//the physical memory io address from the pci BAR or hardset for ISA 
	//-> has to be mapped to a kernel address
	void * physicalMemoryIOAddress;
	//length of the Memory IO area 
	unsigned long memoryIOLength;
	//the port io address of this devie -> not mandatory
	unsigned long portIOAddress;
	//the length of addresses in this type of network
	unsigned long addressLength;
	//the address this network interface has -> NIC receives packets destined for this address
	unsigned char myAddress[MAX_ADDRESS_LENGTH];
	//the broadcast address -> all NICs receive packets with the broadcast destination address
	unsigned char broadcastAddress[MAX_ADDRESS_LENGTH];
	//number of bytes that are sent before the header 
	unsigned short headerPreambleLength;
	//the interrupt used by this device
	unsigned char irq;
	//the driver of this device contains initialize/destroy/transmit routines
	//this should really be of type kernelNetworkDriver, but then there is a circular dependency
	//between this structure and the kernelNetworkDriver structure. HELP !!!
	void * driver;
	//Will hold a pointer to the data given by the bus (mostly the kernelBusPCIDevice structure)
	void * busData;
	//This holds a pointer to a device-specific private data-structure
	void * privateData;
	//this is the current link status of this network device
	unsigned short linkStatus;
	//the version of this network card
	unsigned long version;
	//the lock of this NIC
	lock interfaceLock;
} kernelNetworkInterface ;

//int kernelNetworkGetInterfacePCI(kernelBusPCIDevice * netDevice, kernelNetworkInterface ** nic);
int kernelNetworkRegisterInterface(kernelNetworkInterface * nic);
int kernelNetworkUnregisterInterface(kernelNetworkInterface * nic);
int kernelNetworkGetInterfacePCI(kernelBusPCIDevice * netDevice, kernelNetworkInterface ** nic);
//int kernelNetworkGetInterface(int number, kernelNetworkInterface ** nic);

#include "kernelNetworkDriver.h"


