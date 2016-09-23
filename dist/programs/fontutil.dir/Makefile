##
##  Visopsys
##  Copyright (C) 1998-2016 J. Andrew McLaughlin
##
##  Makefile
##

BUILDDIR = ../../../build/programs/fontutil.dir

FONTDIRS = $(addprefix ${BUILDDIR}/, ${DIRNAMES})

all:
	mkdir -p ${BUILDDIR}
	cp *.txt ${BUILDDIR}

clean:
	rm -f *~ core
	rm -Rf ${BUILDDIR}

