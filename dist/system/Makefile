##
##  Visopsys
##  Copyright (C) 1998-2018 J. Andrew McLaughlin
##
##  Makefile
##

BUILDDIR = ../../build/system
FILES = visopsys.jpg install-files.* password.blank
DESTFILES = $(addprefix ${BUILDDIR}/, ${FILES})

all:
	mkdir -p ${BUILDDIR}
	cp ${FILES} ${BUILDDIR}
	${MAKE} -C config
	${MAKE} -C fonts
	${MAKE} -C icons
	${MAKE} -C keymaps
	${MAKE} -C locale
	${MAKE} -C mouse
	${MAKE} -C wallpaper


clean:
	${MAKE} -C config clean
	${MAKE} -C fonts clean
	${MAKE} -C icons clean
	${MAKE} -C keymaps clean
	${MAKE} -C locale clean
	${MAKE} -C mouse clean
	${MAKE} -C wallpaper clean
	rm -f *~ core
	rm -f ${DESTFILES}

