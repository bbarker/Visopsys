##
##  Visopsys
##  Copyright (C) 1998-2016 J. Andrew McLaughlin
##
##  Makefile
##

BUILDDIR = ../../build/system/headers

all:
	mkdir -p ${BUILDDIR}
	cp *.h ${BUILDDIR}
	${MAKE} -C sys

clean:
	${MAKE} -C sys clean
	rm -f *~
	rm -Rf ${BUILDDIR}

