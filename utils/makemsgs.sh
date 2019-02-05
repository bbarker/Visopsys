#!/bin/sh
##
##  Visopsys
##  Copyright (C) 1998-2018 J. Andrew McLaughlin
##
##  makemsgs.sh
##

# Given the name of a directory that corresponds with a language code,
# and containing message files named using the standard [package].po, invoke
# msgfmt to compile them into binary message object files in the destination
# directory

if [ $# -ne 2 ] ; then
	echo Usage: $0 srcdir destdir
	exit 1
fi

SRCDIR=$1
DESTDIR=$2

LANG=`basename $SRCDIR`

for FILE in `find $SRCDIR -name '*.po'` ; do
	PACKAGE=`basename $FILE .po`
	DIR=$DESTDIR/$LANG/LC_MESSAGES
	OBJ=$DIR/$PACKAGE.mo
	mkdir -p $DIR
	if [ ! -e $OBJ -o $SRCDIR/$PACKAGE.po -nt $OBJ ] ; then
		echo Create message object $OBJ
		msgfmt -o $OBJ $SRCDIR/$PACKAGE.po
		RET=$?
		if [ $RET -ne 0 ] ; then
			exit $RET
		fi
	fi
done

exit 0

