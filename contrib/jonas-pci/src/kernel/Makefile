##
##  Visopsys
##  Copyright (C) 1998-2005 J. Andrew McLaughlin
## 
##  Makefile
##

##  This file is the Makefile for the Visopsys kernel itself.

BUILDDIR=../../build
LIBDIR=../../build/system/libraries

CC		= gcc
RELEASE		= $(shell ../../utils/release.sh)
CPU		= $(shell ../../utils/cpu.sh)
STDDEPS		= *.h ../include/sys/*.h ${LIBDIR}/libc.a Makefile
CODEGEN		= -O2 ${CPU} -fno-strength-reduce -fno-strict-aliasing \
		-ffreestanding
WARN		= -Wall -W -Wshadow -Wcast-align -Wmissing-prototypes \
		-Wstrict-prototypes -Wmissing-declarations -Wredundant-decls \
		-Werror
INCLUDE		= -nostdinc -I. -I../include
CFLAGS		= -pipe ${CODEGEN} ${WARN} ${INCLUDE} -DKERNEL
LFLAGS		= -L${LIBDIR} -nostdlib -nodefaultlibs -nostartfiles -static -Wl,-warn-common,-X,--oformat,elf32-i386,-e,kernelMain,-Ttext,0xC0000000

# kernelMain needs to go first
NAMES =	kernelMain \
	kernelApi \
	kernelBusPCI \
	kernelDescriptor \
	kernelDisk \
	kernelDma \
	kernelDriverManagement \
	kernelEncryptMD5 \
	kernelEnvironment \
	kernelError \
	kernelFile \
	kernelFileStream \
	kernelFilesystem \
	kernelFilesystemExt \
	kernelFilesystemFat \
	kernelFilesystemIso \
	kernelFont \
	kernelGraphic \
	kernelHardwareEnumeration \
	kernelImage \
	kernelInitialize \
	kernelInterrupt \
	kernelKeyboard \
	kernelLoader \
	kernelLock \
	kernelLog \
	kernelMalloc \
	kernelMemoryManager \
	kernelMouse \
	kernelMiscFunctions \
	kernelMultitasker \
	kernelPageManager \
	kernelPic \
	kernelRandom \
	kernelRtc \
	kernelShutdown \
	kernelStream \
	kernelSysTimer \
	kernelText \
	kernelUser \
	kernelVariableList \
	kernelWindow \
	kernelWindowBorder \
	kernelWindowButton \
	kernelWindowCanvas \
	kernelWindowCheckbox \
	kernelWindowContainer \
	kernelWindowEventStream \
	kernelWindowIcon \
	kernelWindowImage \
	kernelWindowList \
	kernelWindowListItem \
	kernelWindowMenu \
	kernelWindowMenuBar \
	kernelWindowMenuItem \
	kernelWindowPasswordField \
	kernelWindowProgressBar \
	kernelWindowRadioButton \
	kernelWindowScrollBar \
	kernelWindowShell \
	kernelWindowTextArea \
	kernelWindowTextField \
	kernelWindowTextLabel \
	kernelWindowTitleBar

DRIVERNAMES = kernelDmaDriver \
	kernelFloppyDriver \
	kernelFramebufferGraphicDriver \
	kernelGraphicConsoleDriver \
	kernelIdeDriver \
	kernelKeyboardDriver \
	kernelPicDriver \
	kernelPS2MouseDriver \
	kernelRtcDriver \
	kernelSysTimerDriver \
	kernelTextConsoleDriver

ALLNAMES = ${NAMES} ${DRIVERNAMES}

OBJS = $(addprefix obj/, $(addsuffix .o, ${ALLNAMES}))
KERNEL = ${BUILDDIR}/visopsys
SYMBOLS = ${BUILDDIR}/system/kernelSymbols.txt

# Targets

all: target-dirs ${KERNEL} strip

target-dirs:
	mkdir -p obj
	mkdir -p ${BUILDDIR}

${KERNEL}: ${OBJS}
	${CC} ${CFLAGS} ${LFLAGS} ${OBJS} -lc -o $@
	../../utils/kernel-symbols.sh ${KERNEL} ${SYMBOLS}

strip: ${KERNEL}
	strip -s ${KERNEL}

obj/%.o: %.c ${STDDEPS}
	${CC} ${CFLAGS} -c $< -o $@

clean:
	rm -Rf *~ *.o core obj ${KERNEL} ${SYMBOLS}

# Object files with explicit rules

obj/kernelApi.o: kernelApi.c ${STDDEPS}
	${CC} ${CFLAGS} -Wno-strict-prototypes -c $< -o $@

obj/kernelMiscFunctions.o: kernelMiscFunctions.c ${STDDEPS}
	${CC} ${CFLAGS} -D_KVERSION_=\"${RELEASE}\" -c $< -o $@
