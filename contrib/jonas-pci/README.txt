Changes made to version 0.54:

* added four macros to kernelProcessorX86.h:
	+ kernelProcessorInPort32(port, data)
	+ kernelProcessorOutPort32(port, data)
	+ kernelProcessorRepInPort32(port, buffer, reads)
	+ kernelProcessorRepOutPort32(port, buffer, reads)

* added new include lines, new function and some code to function kernelHardwareEnumerate() in file kernelHardwareEnumeration.c:
	+ #include "kernelBusPCI.h"
	+ #include "kernelMemoryManager.h"
	+ enumeratePCIDevices()
	+ in function kernelHardwareEnumerate(): 
		status = enumeratePCIDevices();
		if(status < 0)
    		return (status);
		
questions: Why can't the union kernelBusPCIDevice in function enumeratePCIDevices() not be static? Whenever I wrote on it, there was a kernel stack fault. Now it allocates memory dynamically.

Why can't visopsys boot from the iso image? I tried on my Laptop and on my father's computer, and both have problems with the visopsys floppy driver. (Could be improper floppy emulation). Sadly I don't have a floppy drive, I can only boot cd or hdd. hdd installation fails with me too, grub should be chainloading but nothing happens. I will try the Windows boot manager, but I don't expect much. VMWare works with the iso image.

When pressing STRC+C while displaying text with more:
	PANIC:more:kernelMultitasker.c:kernelMultitaskerTerminate(2914):
	Cannot terminate() inside interrupt handler
	SYSTEM HALTED
 
Seems like pressing the STRG- (or c-) key causes an interrupt while the program handles the terminate-signal.

I will continue the development of the PCI driver. I plan to write some functions which give vendor names, device names, class and subclass names from the numbers. Also, I want to write a driver for my Realtek network card, and perhaps do some of the framework (a simple TCP/IP stack). Internet would be nice with visopsys. Do you have any plans what network support for visopsys should look like? Hope you integrate my drivers (not yet, as they don't do anything.)
Bye, jonas

