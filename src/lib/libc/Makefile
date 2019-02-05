##
##  Visopsys
##  Copyright (C) 1998-2018 J. Andrew McLaughlin
##
##  Makefile
##

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

CDEFNAMES = \
	_dbl2str \
	_digits \
	_fdesc \
	_flt2str \
	_fmtinpt \
	_kernapi \
	_ldigits \
	_lnum2str \
	_num2str \
	_str2num \
	_xpndfmt

CTYPENAMES = \
	isalnum \
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

DIRENTNAMES = closedir \
	opendir \
	readdir \
	readdir_r \
	rewinddir

FCNTLNAMES = open

LIBGENNAMES = \
	basename \
	dirname

LOCALENAMES = setlocale

MATHNAMES = \
	ceil \
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

NETNAMES = \
	accept \
	bind \
	connect \
	getprotobyname \
	getprotobynumber \
	getservbyname \
	getservbyport \
	inet_ntop \
	inet_pton \
	listen \
	poll \
	recv \
	send \
	shutdown \
	socket

SIGNALNAMES = signal

STATNAMES = \
	mkdir \
	stat

STDIONAMES = \
	fclose \
	fdopen \
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
	tmpfile \
	vfprintf \
	vfscanf \
	vprintf \
	vscanf \
	vsnprintf \
	vsprintf \
	vsscanf

STDLIBNAMES = \
	abort \
	abs \
	calloc \
	exit \
	getenv \
	labs \
	malloc \
	mbstowcs \
	mbtowc \
	mkdtemp \
	mkstemp \
	rand \
	realloc \
	realpath \
	setenv \
	srand \
	system \
	wctomb

STRINGNAMES = \
	bcopy \
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
	strtok \
	strtok_r

TIMENAMES = \
	asctime \
	clock \
	ctime \
	difftime \
	gmtime \
	mktime \
	time

UNISTDNAMES = \
	chdir \
	close \
	ftruncate \
	getcwd \
	getopt \
	lseek \
	read \
	rmdir \
	sleep \
	swab \
	truncate \
	unlink \
	write

MISCNAMES = readline \
	sched_yield \
	uname

ALLNAMES = ${CDEFNAMES} ${CTYPENAMES} ${DIRENTNAMES} ${FCNTLNAMES} \
	${LIBGENNAMES} ${LOCALENAMES} ${MATHNAMES} ${NETNAMES} ${SIGNALNAMES} \
	${STATNAMES} ${STDIONAMES} ${STDLIBNAMES} ${STRINGNAMES} ${TIMENAMES} \
	${UNISTDNAMES} ${MISCNAMES}

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

