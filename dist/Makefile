##
##  Visopsys
##  Copyright (C) 1998-2016 J. Andrew McLaughlin
##
##  Makefile
##

BUILDDIR = ../build

all:
	mkdir -p ${BUILDDIR}/temp
	mkdir -p ${BUILDDIR}/users
	${MAKE} -C programs
	${MAKE} -C system

clean:
	${MAKE} -C programs clean
	${MAKE} -C system clean
	rm -f *~ core
	rm -Rf ${BUILDDIR}/temp
	rm -Rf ${BUILDDIR}/users

