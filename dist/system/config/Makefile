##
##  Visopsys
##  Copyright (C) 1998-2018 J. Andrew McLaughlin
##
##  Makefile
##

BUILDDIR = ../../../build/system/config

all:
	mkdir -p ${BUILDDIR}
	cp *.conf ${BUILDDIR}
	mkdir -p ${BUILDDIR}/de
	cp de/*.conf ${BUILDDIR}/de
	mkdir -p ${BUILDDIR}/es
	cp es/*.conf ${BUILDDIR}/es
	mkdir -p ${BUILDDIR}/ru
	cp ru/*.conf ${BUILDDIR}/ru
	mkdir -p ${BUILDDIR}/tr
	cp tr/*.conf ${BUILDDIR}/tr

clean:
	rm -f *~ core
	rm -Rf ${BUILDDIR}

