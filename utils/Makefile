##
##  Visopsys
##  Copyright (C) 1998-2018 J. Andrew McLaughlin
##
##  Makefile
##

##  Makefile for the Visopsys utilities.

ROOT = ..
SRCDIR = ${ROOT}/src/programs

include ${ROOT}/Makefile.include

ifdef DEBUG
	override DEBUG = -DDEBUG
endif

CFLAGS = ${OPT} -pipe ${WARN} ${DEBUG}

all: copy-boot

debug:
	${MAKE} all DEBUG=1

copy-boot: ${SRCDIR}/copy-boot.c Makefile
	${CC} ${CFLAGS} -DPORTABLE ${SRCDIR}/copy-boot.c -o $@

clean:
	rm -f *~ core *.zip *.iso *.img copy-boot *.log

