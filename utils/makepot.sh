#!/bin/sh
##
##  Visopsys
##  Copyright (C) 1998-2016 J. Andrew McLaughlin
##
##  makepot.sh
##

# Given the name of a .c source file, invoke xgettext to extract strings
# into a corresponding .pot message template file

if [ $# -ne 1 ] ; then
	echo Usage: $0 srcfile.c
	exit 1
fi

SRCFILE=$1
TEMPLATE=`basename $SRCFILE .c`.pot
echo Create message template $TEMPLATE
xgettext -Lc -k_ -d- $SRCFILE > $TEMPLATE

exit 0
