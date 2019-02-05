##
##  Visopsys
##  Copyright (C) 1998-2018 J. Andrew McLaughlin
##
##  Makefile
##

##  This file is the Makefile for the Visopsys kernel itself.

ROOT = ../..
UTILSDIR = ${ROOT}/utils
BUILDDIR = ${ROOT}/build
INCDIR = ../include
LIBDIR = ${BUILDDIR}/system/libraries

include ${ROOT}/Makefile.include

STDDEPS = ${ROOT}/Makefile.include Makefile *.h ${INCDIR}/sys/*.h \
	${INCDIR}/*.h ${LIBDIR}/libc.a
INCLUDE = -nostdinc -I. -I${INCDIR}
CFLAGS = ${OPT} ${ARCH} ${CCODEGEN} ${CWARN} ${INCLUDE} -DKERNEL ${DEBUG}
LFLAGS = -L${LIBDIR} -nostdlib -static \
	-Wl,-warn-common,-X,--oformat,elf32-i386,-e,kernelMain,-Ttext,0xC0000000
RELEASE = $(shell ${UTILSDIR}/release.sh)

ifdef DEBUG
	override DEBUG = -DDEBUG
else
	STRIP = strip -s ${KERNEL}
endif

# kernelMain needs to go first
NAMES = kernelMain \
	kernelApi \
	kernelBus \
	kernelCharset \
	kernelCpu \
	kernelDebug \
	kernelDescriptor \
	kernelDevice \
	kernelDisk \
	kernelDma \
	kernelDriver \
	kernelCryptHashMd5 \
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
	kernelFilesystemUdf \
	kernelFont \
	kernelFontTtf \
	kernelFontVbf \
	kernelGraphic \
	kernelImage \
	kernelImageBmp \
	kernelImageIco \
	kernelImageJpg \
	kernelImagePpm \
	kernelInitialize \
	kernelInterrupt \
	kernelKeyboard \
	kernelLinkedList \
	kernelLoader \
	kernelLoaderClass \
	kernelLoaderElf \
	kernelLocale \
	kernelLock \
	kernelLog \
	kernelMalloc \
	kernelMemory \
	kernelMisc \
	kernelMouse \
	kernelMultitasker \
	kernelNetwork \
	kernelNetworkArp \
	kernelNetworkDevice \
	kernelNetworkDhcp \
	kernelNetworkEthernet \
	kernelNetworkIcmp \
	kernelNetworkIp4 \
	kernelNetworkStream \
	kernelNetworkUdp \
	kernelPage \
	kernelPic \
	kernelPower \
	kernelRandom \
	kernelRtc \
	kernelShutdown \
	kernelStream \
	kernelSysTimer \
	kernelText \
	kernelTouch \
	kernelUser \
	kernelVariableList \
	kernelWindow \
	kernelWindowBorder \
	kernelWindowButton \
	kernelWindowCanvas \
	kernelWindowCheckbox \
	kernelWindowComponent \
	kernelWindowContainer \
	kernelWindowDivider \
	kernelWindowEventStream \
	kernelWindowIcon \
	kernelWindowImage \
	kernelWindowList \
	kernelWindowListItem \
	kernelWindowMenu \
	kernelWindowMenuBar \
	kernelWindowMenuBarIcon \
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
	kernelWindowTitleBar \
	kernelWindowTree

DRIVERNAMES = kernelAcpiDriver \
	kernelApicDriver \
	kernelAtaDriver \
	kernelDmaDriver \
	kernelFloppyDriver \
	kernelFramebufferGraphicDriver \
	kernelGraphicConsoleDriver \
	kernelIdeDriver \
	kernelNetworkLoopDriver \
	kernelNetworkPcNetDriver \
	kernelPciDriver \
	kernelPicDriver \
	kernelPs2KeyboardDriver \
	kernelPs2MouseDriver \
	kernelRamDiskDriver \
	kernelRtcDriver \
	kernelSataAhciDriver \
	kernelScsiDiskDriver \
	kernelSysTimerDriver \
	kernelSystemDriver \
	kernelTextConsoleDriver \
	kernelUsbAtapiDriver \
	kernelUsbDriver \
	kernelUsbEhciDriver \
	kernelUsbGenericDriver \
	kernelUsbKeyboardDriver \
	kernelUsbMouseDriver \
	kernelUsbHubDriver \
	kernelUsbOhciDriver \
	kernelUsbTouchscreenDriver \
	kernelUsbUhciDriver \
	kernelUsbXhciDriver

ALLNAMES = ${NAMES} ${DRIVERNAMES}
OBJS = $(addprefix obj/, $(addsuffix .o, ${ALLNAMES}))
KERNEL = ${BUILDDIR}/visopsys

# Targets

all: target-dirs ${KERNEL} strip
	${MAKE} -C de
	${MAKE} -C es
	${MAKE} -C ru
	${MAKE} -C tr

target-dirs:
	mkdir -p obj
	mkdir -p ${BUILDDIR}

${KERNEL}: ${OBJS}
	${CC} ${CFLAGS} ${LFLAGS} ${OBJS} -lc -lgcc -o $@

strip: ${KERNEL}
	${STRIP}

obj/%.o: %.c ${STDDEPS}
	${CC} ${CFLAGS} -c $< -o $@

# Object files with explicit rules

obj/kernelApi.o: kernelApi.c ${STDDEPS}
	${CC} ${CFLAGS} -O0 -Wno-strict-prototypes -c $< -o $@

obj/kernelMisc.o: kernelMisc.c ${STDDEPS}
	${CC} ${CFLAGS} -D_KVERSION_=\"${RELEASE}\" -c $< -o $@

clean:
	${MAKE} -C de clean
	${MAKE} -C es clean
	${MAKE} -C ru clean
	${MAKE} -C tr clean
	rm -Rf *~ *.o core obj ${KERNEL}

