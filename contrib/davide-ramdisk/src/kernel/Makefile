##
##  Visopsys
##  Copyright (C) 1998-2006 J. Andrew McLaughlin
## 
##  Makefile
##

##  This file is the Makefile for the Visopsys kernel itself.

ROOT=../..
UTILSDIR=${ROOT}/utils
BUILDDIR=${ROOT}/build
INCDIR=../include
LIBDIR=${BUILDDIR}/system/libraries

include ${ROOT}/Makefile.include

ifeq (${RELEASE},)
	RELEASE = $(shell ${UTILSDIR}/release.sh)
endif

STDDEPS		= ${ROOT}/Makefile.include Makefile *.h ${INCDIR}/sys/*.h \
		${INCDIR}/*.h ${LIBDIR}/libc.a
INCLUDE		= -nostdinc -I. -I${INCDIR}
DEBUG		= # -DDEBUG # Uncomment to turn on debugging
CFLAGS		= ${OPT} ${ARCH} ${CODEGEN} ${WARN} ${INCLUDE} -DKERNEL \
		${DEBUG}
LFLAGS		= -L${LIBDIR} -nostdlib -nodefaultlibs -nostartfiles -static \
		-Wl,-warn-common,-X,--oformat,elf32-i386,-e,kernelMain,-Ttext,0xC0000000

# kernelMain needs to go first
NAMES =	kernelMain \
	kernelApi \
	kernelBus \
	kernelDebug \
	kernelDescriptor \
	kernelDevice \
	kernelDisk \
	kernelDma \
	kernelDriver \
	kernelEncryptMD5 \
	kernelEnvironment \
	kernelError \
	kernelFile \
	kernelFileStream \
	kernelFilesystem \
	kernelFilesystemExt \
	kernelFilesystemFat \
	kernelFilesystemIso \
	kernelFilesystemLinuxSwap \
	kernelFilesystemNtfs \
	kernelFont \
	kernelGraphic \
	kernelImage \
	kernelImageBmp \
	kernelImageIco \
	kernelImageJpg \
	kernelInitialize \
	kernelInterrupt \
	kernelKeyboard \
	kernelLoader \
	kernelLoaderClass \
	kernelLoaderElf \
	kernelLock \
	kernelLog \
	kernelMalloc \
	kernelMemory \
	kernelMouse \
	kernelMisc \
	kernelMultitasker \
	kernelNetwork \
	kernelNetworkDevice \
	kernelNetworkStream \
	kernelPage \
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
	kernelWindowComponent \
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
	kernelWindowSlider \
	kernelWindowSysContainer \
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
	kernelLanceDriver \
	kernelPciDriver \
	kernelPicDriver \
	kernelPS2MouseDriver \
	kernelRtcDriver \
	kernelScsiDiskDriver \
	kernelSysTimerDriver \
	kernelSystemDriver \
	kernelTextConsoleDriver \
	kernelUsbDriver \
	kernelUsbEhciDriver \
	kernelUsbHidDriver \
	kernelUsbUhciDriver \
	kernelRamDiskDriver
# previous one by Davide Airaghi

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
	${UTILSDIR}/kernel-symbols.sh ${KERNEL} ${SYMBOLS}

strip: ${KERNEL}
	strip -s ${KERNEL}

obj/%.o: %.c ${STDDEPS}
	${CC} ${CFLAGS} -c $< -o $@

clean:
	rm -Rf *~ *.o core obj ${KERNEL} ${SYMBOLS}

# Object files with explicit rules

obj/kernelApi.o: kernelApi.c ${STDDEPS}
	${CC} ${CFLAGS} -O0 -Wno-strict-prototypes -c $< -o $@

obj/kernelMisc.o: kernelMisc.c ${STDDEPS}
	${CC} ${CFLAGS} -D_KVERSION_=\"${RELEASE}\" -c $< -o $@
