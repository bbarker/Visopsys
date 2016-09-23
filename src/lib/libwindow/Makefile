##
##  Visopsys
##  Copyright (C) 1998-2016 J. Andrew McLaughlin
##
##  Makefile
##

ROOT = ../../..
BUILDDIR = ${ROOT}/build/system/libraries
INCDIR = ../../include

include ${ROOT}/Makefile.include

STDDEPS = ${ROOT}/Makefile.include Makefile ../shared/* ${INCDIR}/*.h \
	${INCDIR}/sys/*.h
INCLUDE = -nostdinc -I${INCDIR}
CFLAGS = ${OPT} ${ARCH} ${CCODEGEN} ${CWARN} ${INCLUDE} ${DEBUG}
LFLAGS = -nostdlib

ifdef DEBUG
	override DEBUG = -DDEBUG
else
	STRIP = strip -s ${SHAREDLIB}
endif

NAMES = windowArchiveList \
	windowBannerDialog \
	windowCenterDialog \
	windowChoiceDialog \
	windowColorDialog \
	windowFileDialog \
	windowFileList \
	windowKeyboard \
	windowLanguageDialog \
	windowMain \
	windowNumberDialog \
	windowOkDialog \
	windowPixelEditor \
	windowProgressDialog \
	windowPromptDialog \
	windowQueryDialog \
	windowRadioDialog \
	windowThumbImage

OBJDIR = obj
OBJS = $(addprefix ${OBJDIR}/, $(addsuffix .o, ${NAMES}))
PICOBJDIR = picobj
PICOBJS = $(addprefix ${PICOBJDIR}/, $(addsuffix .o, ${NAMES}))
LIBRARY = ${BUILDDIR}/libwindow.a
SONAME = libwindow.so
SHAREDLIB = ${BUILDDIR}/${SONAME}

# Targets

all: target-dirs ${LIBRARY} ${SHAREDLIB} strip
	${MAKE} -C de
	${MAKE} -C es
	${MAKE} -C ru
	${MAKE} -C tr

target-dirs:
	mkdir -p ${OBJDIR} ${PICOBJDIR} ${BUILDDIR}

${LIBRARY}: ${OBJS}
	${AR} -rs $@ ${OBJS}

${SHAREDLIB}: ${PICOBJS}
	${CC} ${ARCH} -shared -nostdlib -Wl,--soname=${SONAME},-z,norelro \
		${PICOBJS} -o $@

strip: ${SHAREDLIB}
	${STRIP}

${OBJDIR}/%.o: %.c ${STDDEPS}
	${CC} ${CFLAGS} ${LFLAGS} -c $< -o $@

${PICOBJDIR}/%.o: %.c ${STDDEPS}
	${CC} ${CFLAGS} ${LFLAGS} -fpic -c $< -o $@

clean:
	${MAKE} -C de clean
	${MAKE} -C es clean
	${MAKE} -C ru clean
	${MAKE} -C tr clean
	rm -Rf *~ *.o core ${OBJDIR} ${PICOBJDIR} ${LIBRARY} ${SHAREDLIB}

