##
##  Visopsys
##  Copyright (C) 1998-2005 J. Andrew McLaughlin
## 
##  Makefile
##

BUILDDIR=../../build/programs
INCDIR=../include
LIBDIR=../../build/system/libraries

CC		= gcc
RELEASE		= $(shell ../../utils/release.sh)
CPU		= $(shell ../../utils/cpu.sh)
STDDEPS		= Makefile ${INCDIR}/*.h ${INCDIR}/sys/*.h ${LIBDIR}/crt0.o
CODEGEN		= -O2 ${CPU} -fno-strength-reduce -fno-strict-aliasing
WARN		= -Wall -W -Wshadow -Wmissing-prototypes -Wstrict-prototypes \
		-Wmissing-declarations -Wredundant-decls -Werror
INCLUDE         = -nostdinc -I${INCDIR}
CFLAGS		= -pipe ${CODEGEN} ${WARN} ${INCLUDE}
LFLAGS		= -L${LIBDIR} -nostdlib -nodefaultlibs -nostartfiles -Wl,${LIBDIR}/crt0.o,--warn-common,-X,--oformat,elf32-i386
OPTS		= ${CFLAGS} ${LFLAGS}

NAMES	= adduser \
	bootmenu \
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
	fdisk \
	file \
	filebrowse \
	find \
	format \
	help \
	iconwin \
	ifconfig \
	imgboot \
	install \
	keymap \
	kill \
	ldd \
	loadfont \
	login \
	logout \
	ls \
	lsdev \
	md5 \
	mem \
	mkdir \
	more \
	mount \
	mv \
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
	test_port_io \
	su 
# prev 2 added by Davide Airaghi	

OBJS = $(addprefix ${BUILDDIR}/, ${NAMES})

# Targets

all: target-dirs ${OBJS}
	strip -s ${OBJS}
	#make -C mines

target-dirs:
	mkdir -p ${BUILDDIR}

${BUILDDIR}/%: %.c ${STDDEPS}
	${CC} ${OPTS} $< -lwindow -lvsh -lc -o $@

clean:
	rm -Rf *~ *.o core ${OBJS}
	make -C mines clean

# Object files with explicit rules

${BUILDDIR}/copy-boot: copy-boot.c ${STDDEPS}
	${CC} ${OPTS} -DVISOPSYS $< -lvsh -lc -o $@

${BUILDDIR}/fdisk: fdisk.c fdisk.h ${STDDEPS}
	${CC} ${OPTS} ${PARTLOGIC} $< -lwindow -lvsh -lc -o $@
