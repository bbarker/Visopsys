#!/bin/sh
##
##  Visopsys
##  Copyright (C) 1998-2016 J. Andrew McLaughlin
##
##  mergepot.sh
##

# Given the name of a .pot template file and a language subdirectory, invoke
# msgmerge to extract strings
# into a corresponding .pot message template file

if [ $# -ne 2 ] ; then
	echo Usage: $0 template.pot language_dir
	exit 1
fi

TEMPLATE=$1
LANGDIR=$2
MSGFILE=$LANGDIR/`basename $TEMPLATE .pot`.po
echo -n Merge $TEMPLATE into message file $MSGFILE
msgmerge -o $MSGFILE $MSGFILE $TEMPLATE

exit 0
