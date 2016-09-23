##
##  Visopsys
##  Copyright (C) 1998-2016 J. Andrew McLaughlin
##
##  Makefile
##

ifdef DEBUG
	override DEBUG = -DDEBUG
else
	STRIP = strip -s ${SHAREDLIB}
endif

ROOT = ../../..
BUILDDIR = ${ROOT}/build/system/libraries
INCDIR = ../../include

include ${ROOT}/Makefile.include

ifdef DEBUG
	override DEBUG = -DDEBUG
else
	STRIP = strip -s ${SHAREDLIB}
endif

STDDEPS = ${ROOT}/Makefile.include Makefile ../shared/* ${INCDIR}/*.h \
	${INCDIR}/sys/*.h
INCLUDE = -nostdinc -I${INCDIR}
CFLAGS = ${OPT} ${ARCH} ${CCODEGEN} ${CWARN} ${INCLUDE} ${DEBUG}

CDEFNAMES = _dbl2str \
	_digits \
	_flt2str \
	_fmtinpt \
	_kernapi \
	_ldigits \
	_lnum2str \
	_num2str \
	_str2num \
	_xpndfmt

CTYPENAMES = isalnum \
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

FCNTLNAMES = open

LIBGENNAMES = basename \
	dirname

LOCALENAMES = setlocale

MATHNAMES = ceil \
	cos \
	cosf \
	fabs \
	fabsf \
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

SIGNALNAMES = signal

STDIONAMES = fclose \
	fflush \
	fgetpos \
	fgets \
	fopen \
	fprintf \
	fread \
	fscanf \
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
	sscanf \
	strtol \
	strtoll \
	strtoul \
	strtoull \
	vfprintf \
	vfscanf \
	vprintf \
	vscanf \
	vsnprintf \
	vsprintf \
	vsscanf

STDLIBNAMES = abort \
	abs \
	calloc \
	exit \
	getenv \
	labs \
	malloc \
	mbtowc \
	mbstowcs \
	rand \
	realloc \
	realpath \
	setenv \
	srand \
	system \
	wctomb

STRINGNAMES = bcopy \
	bzero \
	ffs \
	fls \
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
	strnlen \
	strrchr \
	strspn \
	strstr \
	strtok

TIMENAMES = asctime \
	clock \
	ctime \
	difftime \
	gmtime \
	mktime \
	time

UNISTDNAMES = close \
	ftruncate \
	getopt \
	lseek \
	read \
	sleep \
	stat \
	swab \
	truncate \
	write

MISCNAMES = readline

ALLNAMES = ${CDEFNAMES} ${CTYPENAMES} ${FCNTLNAMES} ${LIBGENNAMES} \
	${LOCALENAMES} ${MATHNAMES} ${SIGNALNAMES} ${STDIONAMES} ${STDLIBNAMES} \
	${STRINGNAMES} ${TIMENAMES} ${UNISTDNAMES} ${MISCNAMES}

OBJDIR = obj
PICOBJDIR = picobj
OBJS = $(addprefix ${OBJDIR}/, $(addsuffix .o, ${ALLNAMES}))
PICOBJS = $(addprefix ${PICOBJDIR}/, $(addsuffix .o, ${ALLNAMES}))
CRT0 = ${BUILDDIR}/crt0.o
LIBRARY = ${BUILDDIR}/libc.a
SONAME = libc.so
SHAREDLIB = ${BUILDDIR}/${SONAME}

# Targets

all: target-dirs ${CRT0} ${LIBRARY} ${SHAREDLIB} strip

target-dirs:
	mkdir -p ${OBJDIR} ${PICOBJDIR} ${BUILDDIR}

${CRT0}: crt0.c ${STDDEPS}
	${CC} ${CFLAGS} -c $< -o $@

${LIBRARY}: ${OBJS}
	${AR} -rs $@ ${OBJS}

${SHAREDLIB}: ${PICOBJS}
	${CC} ${ARCH} -shared -nostdlib -Wl,--soname=${SONAME},-z,norelro \
		${PICOBJS} -static -lgcc -o $@

strip: ${SHAREDLIB}
	${STRIP}

${OBJDIR}/%.o: %.c ${STDDEPS}
	${CC} ${CFLAGS} -c $< -o $@

${PICOBJDIR}/%.o: %.c ${STDDEPS}
	${CC} ${CFLAGS} -fpic -c $< -o $@

clean:
	rm -Rf *~ *.o core ${OBJDIR} ${PICOBJDIR} ${CRT0} ${LIBRARY} ${SHAREDLIB}

