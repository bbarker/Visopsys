##
##  Visopsys
##  Copyright (C) 1998-2018 J. Andrew McLaughlin
##
##  Makefile
##

ROOT = ../../..
BUILDDIR = ${ROOT}/build/programs
INCDIR = ../../include
LIBDIR = ${ROOT}/build/system/libraries

include ${ROOT}/Makefile.include

STDDEPS = ${ROOT}/Makefile.include Makefile ${INCDIR}/*.h ${INCDIR}/sys/*.h \
	*.h ${LIBDIR}/crt0.o
INCLUDE = -nostdinc -I${INCDIR}
CFLAGS = ${OPT} ${ARCH} ${CCODEGEN} ${CWARN} ${INCLUDE} ${DEBUG} ${PARTLOGIC}
LFLAGS = -L${LIBDIR} -nostdlib \
	-Wl,${LIBDIR}/crt0.o,--warn-common,-X,--oformat,elf32-i386

ifdef DEBUG
	override DEBUG = -DDEBUG
else
	STRIP = strip -s ${FDISK}
endif

NAMES = fdisk \
	gpt \
	msdos

OBJS = $(addprefix obj/, $(addsuffix .o, ${NAMES}))
FDISK = ${BUILDDIR}/fdisk

# Targets

all: target-dirs ${FDISK} strip
	${MAKE} -C de

target-dirs:
	mkdir -p obj
	mkdir -p ${BUILDDIR}

${FDISK}: ${OBJS}
	${CC} ${CFLAGS} ${LFLAGS} ${OBJS} -lintl -lwindow -lvsh -ldl -lc -lgcc -o $@

strip: ${FDISK}
	${STRIP}

obj/%.o: %.c ${STDDEPS}
	${CC} ${CFLAGS} -c $< -o $@

clean:
	${MAKE} -C de clean
	rm -Rf *~ *.o core obj ${FDISK}

