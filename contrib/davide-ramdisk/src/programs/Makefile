##
##  Visopsys
##  Copyright (C) 1998-2006 J. Andrew McLaughlin
## 
##  Makefile
##

ROOT=../..
UTILSDIR=${ROOT}/utils
BUILDDIR=${ROOT}/build/programs
INCDIR=../include
LIBDIR=${ROOT}/build/system/libraries

include ${ROOT}/Makefile.include

STDDEPS		= ${ROOT}/Makefile.include Makefile ${INCDIR}/*.h \
		${INCDIR}/sys/*.h ${LIBDIR}/crt0.o
INCLUDE         = -nostdinc -I${INCDIR}
CFLAGS		= ${OPT} ${ARCH} ${CODEGEN} ${WARN} ${INCLUDE}
LFLAGS		= -L${LIBDIR} -nostdlib -nodefaultlibs -nostartfiles \
		-Wl,${LIBDIR}/crt0.o,--warn-common,-X,--oformat,elf32-i386
OPTS		= ${CFLAGS} ${LFLAGS}

NAMES	= adduser \
	bootmenu \
	cal \
	cat \
	cdrom \
	chkdisk \
	clock \
	computer \
	confedit \
	console \
	copy-boot \
	copy-mbr \
	cp \
	date \
	defrag \
	disks \
	disprops \
	domainname \
	edit \
	fdisk \
	file \
	filebrowse \
	find \
	format \
	help \
	hostname \
	iconwin \
	ifconfig \
	imgboot \
	install \
	keymap \
	kill \
	loadfont \
	login \
	logout \
	ls \
	lsdev \
	md5 \
	mem \
	mines \
	mkdir \
	more \
	mount \
	mv \
	nm \
	passwd \
	ping \
	progman \
	ps \
	reboot \
	renice \
	rm \
	rmdir \
	screenshot \
	shutdown \
	snake \
	sync \
	telnet \
	test \
	touch \
	umount \
	uname \
	uptime \
	users \
	view \
	vsh \
	wallpaper \
	window \
	ramdisk \
	disk2file \
	file2disk \
	disk2disk 
# previous four added by Davide Airaghi

OBJS = $(addprefix ${BUILDDIR}/, ${NAMES})

# Targets

all: target-dirs ${OBJS}
	strip -s ${OBJS}

target-dirs:
	mkdir -p ${BUILDDIR}

${BUILDDIR}/%: %.c ${STDDEPS}
	${CC} ${OPTS} $< -lwindow -lvsh -lc -o $@

clean:
	rm -Rf *~ *.o core ${OBJS}

# Object files with explicit rules

${BUILDDIR}/copy-boot: copy-boot.c ${STDDEPS}
	${CC} ${OPTS} -DVISOPSYS $< -lvsh -lc -o $@

${BUILDDIR}/fdisk: fdisk.c fdisk.h ${STDDEPS}
	${CC} ${OPTS} ${PARTLOGIC} $< -lntfs -lwindow -lvsh -lc -o $@
