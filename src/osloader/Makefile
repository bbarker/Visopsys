##
##  Visopsys
##  Copyright (C) 1998-2018 J. Andrew McLaughlin
##
##  Makefile
##

# Builds all the MBRs, boot sectors, and the OS loader.

ROOT = ../..
LOADBUILDDIR = ${ROOT}/build
BOOTBUILDDIR = ${LOADBUILDDIR}/system/boot

include ${ROOT}/Makefile.include

WARN = -w+orphan-labels
BOOTOPTS = -f bin ${WARN}
LOADOPTS = -f elf32 ${WARN}
LINKOPTS = -Wl,-warn-common,-X,--oformat,binary,-e,loaderMain,-Ttext,0x00000000
STDDEPS = loader.h Makefile

BOOTNAMES = mbr.simple \
	mbr.bootmenu \
	bootmenu \
	bootsect.fat \
	bootsect.fat32 \
	bootsect.fatnoboot \
	bootsect.fatnoboot32

BOOTSECTS = $(addprefix ${BOOTBUILDDIR}/, ${BOOTNAMES})

LOADNAMES = loaderMain \
	loaderA20 \
	loaderVideo \
	loaderDetectHardware \
	loaderLoadFile \
	loaderLoadKernel \
	loaderDiskError \
	loaderPrintRoutines

LOADOBJS = $(addprefix obj/, $(addsuffix .o, ${LOADNAMES}))

all: target-dirs bootsectors ${LOADBUILDDIR}/vloader

target-dirs:
	mkdir -p obj ${BOOTBUILDDIR} ${LOADBUILDDIR}

bootsectors: ${BOOTSECTS}

#
# MBRs and Boot sectors
#

${BOOTBUILDDIR}/mbr.simple: mbr-simple.s ${STDDEPS}
	${NASM} ${BOOTOPTS} $< -o $@

${BOOTBUILDDIR}/mbr.bootmenu: mbr-bootmenu.s ${STDDEPS}
	${NASM} ${BOOTOPTS} $< -o $@

${BOOTBUILDDIR}/bootmenu: bootmenu.s ${STDDEPS}
	${NASM} ${BOOTOPTS} $< -o $@

${BOOTBUILDDIR}/bootsect.fat: bootsect-fat.s ${STDDEPS}
	${NASM} ${BOOTOPTS} $< -o $@

${BOOTBUILDDIR}/bootsect.fat32: bootsect-fat.s ${STDDEPS}
	${NASM} ${BOOTOPTS} -DFAT32 $< -o $@

${BOOTBUILDDIR}/bootsect.fatnoboot: bootsect-fatnoboot.s ${STDDEPS}
	${NASM} ${BOOTOPTS} $< -o $@

${BOOTBUILDDIR}/bootsect.fatnoboot32: bootsect-fatnoboot.s ${STDDEPS}
	${NASM} ${BOOTOPTS} -DFAT32 $< -o $@

#
# Loader files
#

${LOADBUILDDIR}/vloader: ${LOADOBJS} ${STDDEPS}
	${CC} ${ARCH} -nodefaultlibs -nostartfiles ${LINKOPTS} ${LOADOBJS} -o $@

obj/%.o: %.s ${STDDEPS}
	${NASM} ${LOADOPTS} $< -o $@

#
# Miscellany
#

clean:
	rm -Rf *~ *.o core obj
	rm -Rf ${BOOTBUILDDIR} ${LOADBUILDDIR}/vloader

