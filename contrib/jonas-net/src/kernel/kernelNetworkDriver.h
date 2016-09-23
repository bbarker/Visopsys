#define VENDOR_AMD	0x1022

#define DEVICE_NET_PCNET_LANCE_PCI	0x2000

int initialize(kernelNetworkInterface * nic);
int destroy(kernelNetworkInterface * nic);
int transmit(kernelNetworkInterface * nic, void * data, int length);
int getStatistics(kernelNetworkInterface * nic, kernelNetworkStatistics * statistics);

typedef volatile struct
{
	//initializes the network card, registers its interrupt 
	int (*initialize) (kernelNetworkInterface * nic);	
	//unregisters the network card's interrupt, frees memory used by this structure
	int (*destroy) (kernelNetworkInterface * nic);
	//transmits a packet over the cable
	int (*transmit) (kernelNetworkInterface * nic, void * data, int length);	
	//Gets the statistics for this device
	int (*getStatistics) (kernelNetworkInterface * nic, kernelNetworkStatistics * statistics);
} kernelNetworkDriver;

//Must be at the end, because a function in kernelNetworkDriver_AmdPCNet.h relies on the structures above.
#include "kernelNetworkDriver_AmdPCNet.h"


