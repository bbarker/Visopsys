##
##  Visopsys
##  Copyright (C) 1998-2018 J. Andrew McLaughlin
##
##  Makefile
##

# Top-level Makefile.

BUILDDIR = build

all:
	mkdir -p ${BUILDDIR}/system
	cp COPYING.txt ${BUILDDIR}/system/
	${MAKE} -C dist
	${MAKE} -C src DEBUG=${DEBUG}
	${MAKE} -C utils DEBUG=${DEBUG}

debug:
	${MAKE} all DEBUG=1

clean:
	${MAKE} -C dist clean
	${MAKE} -C src clean
	${MAKE} -C utils clean
	rm -f *~ core
	rm -Rf ${BUILDDIR}
	find -name '*.rej' -exec rm {} \;
	find -name '*.orig' -exec rm {} \;
	find . -type f -a ! -name '*.sh' -exec chmod -x {} \;

