## 
##  Visopsys
##  Copyright (C) 1998-2006 J. Andrew McLaughlin
##  
##  Makefile
##

ROOT=../../..
UTILSDIR=${ROOT}/utils
BUILDDIR=${ROOT}/build/system/libraries
INCDIR=../../include

include ${ROOT}/Makefile.include

STDDEPS		= ${ROOT}/Makefile.include Makefile ../shared/* ${INCDIR}/*.h \
		${INCDIR}/sys/*.h
INCLUDE         = -nostdinc -I${INCDIR}
CFLAGS		= ${OPT} ${ARCH} ${CODEGEN} ${WARN} ${INCLUDE}
LFLAGS		= -nostdlib -nodefaultlibs -nostartfiles
OPTS		= ${CFLAGS} ${LFLAGS}

CDEFNAMES	= _div64 \
		_divdi3 \
		_fmtinpt \
		_moddi3 \
		_syscall \
		_udivdi3 \
		_umoddi3 \
		_xpndfmt \
		readline		

CTYPENAMES	= isalnum \
		isalpha \
		isascii \
		iscntrl \
		isdigit \
		isgraph \
		islower \
		isprint \
		ispunct \
		isspace \
		isupper \
		isxdigit \
		tolower \
		toupper

FCNTLNAMES	= open

LOCALENAMES	= setlocale

MATHNAMES	= ceil \
		cos \
		cosf \
		fabs \
		floor \
		floorf \
		fmod \
		modf \
		pow \
		sin \
		sinf \
		sqrt \
		tan \
		tanf

SIGNALNAMES	= signal

STDIONAMES	= fclose \
		fflush \
		fgetpos \
		fgets \
		fopen \
		fprintf \
		fread \
		fseek \
		fsetpos \
		ftell \
		fwrite \
		getc \
		getchar \
		gets \
		perror \
		printf \
		putc \
		putchar \
		puts \
		remove \
		rename \
		rewind \
		scanf \
		snprintf \
		sprintf \
		vfprintf \
		vprintf \
		vsnprintf \
		vsprintf

STDLIBNAMES	= abort \
		abs \
		atoi \
		calloc \
		exit \
		itoa \
		itob \
		itox \
		labs \
		lltoa \
		lltob \
		lltox \
		malloc \
		mbtowc \
		mbstowcs \
		rand \
		realloc \
		srand \
		system \
		ulltoa \
		utoa \
		wctomb \
		xtoi \
		atou
# previous one added by Davide Airaghi

STRINGNAMES	= bcopy \
		bzero \
		ffs \
		memcmp \
		memcpy \
		memmove \
		memset \
		strcasecmp \
		strcasestr \
		strcat \
		strchr \
		strcmp \
		strcpy \
		strdup \
		strerror \
		strlen \
		strncasecmp \
		strncat \
		strncmp \
		strncpy \
		strrchr \
		strspn \
		strstr 


TIMENAMES	= asctime \
		clock \
		difftime \
		time

UNISTDNAMES	= close \
		getopt \
		lseek \
		read \
		stat \
		swab \
		write

ALLNAMES = ${CDEFNAMES} ${CTYPENAMES} ${FCNTLNAMES} ${LOCALENAMES} \
	${MATHNAMES} ${SIGNALNAMES} ${STDIONAMES} ${STDLIBNAMES} \
	${STRINGNAMES} ${TIMENAMES} ${UNISTDNAMES}
OBJDIR = obj
PICOBJDIR = picobj
OBJS = $(addprefix ${OBJDIR}/, $(addsuffix .o, ${ALLNAMES}))
PICOBJS = $(addprefix ${PICOBJDIR}/, $(addsuffix .o, ${ALLNAMES}))
CRT0 = ${BUILDDIR}/crt0.o
LIBRARY = ${BUILDDIR}/libc.a
SONAME = libc.so
SHAREDLIB = ${BUILDDIR}/${SONAME}

# Targets

all: target-dirs ${CRT0} ${LIBRARY} ${SHAREDLIB}

target-dirs:
	mkdir -p ${OBJDIR} ${PICOBJDIR} ${BUILDDIR}

${CRT0}: crt0.c ${STDDEPS}
	${CC} ${OPTS} -c $< -o $@

${LIBRARY}: ${OBJS}
	${AR} -rs $@ ${OBJS}

${SHAREDLIB}: ${PICOBJS}
	${LD} -shared -soname=${SONAME} ${PICOBJS} -o $@

${OBJDIR}/%.o: %.c ${STDDEPS}
	${CC} ${CFLAGS} -c $< -o $@

${PICOBJDIR}/%.o: %.c ${STDDEPS}
	${CC} ${CFLAGS} -fpic -c $< -o $@

clean:
	rm -Rf *~ *.o core ${OBJDIR} ${PICOBJDIR} ${CRT0} ${LIBRARY} ${SHAREDLIB}
