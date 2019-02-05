##
##  Visopsys
##  Copyright (C) 1998-2018 J. Andrew McLaughlin
##
##  Makefile
##

BUILDDIR = ../../build/system/libraries

all:
	mkdir -p ${BUILDDIR}
	${MAKE} -C libc DEBUG=${DEBUG}
	${MAKE} -C libcompress DEBUG=${DEBUG}
	${MAKE} -C libdl DEBUG=${DEBUG}
	${MAKE} -C libintl DEBUG=${DEBUG}
	${MAKE} -C ../../ports/ntfsprogs -f Makefile.visopsys DEBUG=${DEBUG}
	${MAKE} -C libtimer DEBUG=${DEBUG}
	${MAKE} -C libvsh DEBUG=${DEBUG}
	${MAKE} -C libwindow DEBUG=${DEBUG}

debug:
	${MAKE} all DEBUG=1

clean:
	${MAKE} -C libc clean
	${MAKE} -C libcompress clean
	${MAKE} -C libdl clean
	${MAKE} -C libintl clean
	${MAKE} -C ../../ports/ntfsprogs -f Makefile.visopsys clean
	${MAKE} -C libtimer clean
	${MAKE} -C libvsh clean
	${MAKE} -C libwindow clean
	rm -f *~ *.o core
	rm -Rf ${BUILDDIR}

