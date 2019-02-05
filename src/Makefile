##
##  Visopsys
##  Copyright (C) 1998-2018 J. Andrew McLaughlin
##
##  Makefile
##

# This is the top-level source directories Makefile.

all:
	${MAKE} -C osloader
	${MAKE} -C include
	${MAKE} -C lib DEBUG=${DEBUG}
	${MAKE} -C kernel DEBUG=${DEBUG}
	${MAKE} -C programs DEBUG=${DEBUG}

debug:
	${MAKE} all DEBUG=1

clean:
	${MAKE} -C osloader clean
	${MAKE} -C include clean
	${MAKE} -C lib clean
	${MAKE} -C kernel clean
	${MAKE} -C programs clean
	rm -Rf *~ core

