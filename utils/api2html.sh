#!/bin/sh
##
##  Visopsys
##  Copyright (C) 1998-2016 J. Andrew McLaughlin
##
##  api2html.sh
##

# This is run against the C library's kernel API code, as well as the VSH
# and window library code, to generate a simple formatted HTML listing of
# the function declarations with their descriptions

if [ $# -ne 1 ] ; then
	echo "Usage: $0 <filename>"
	exit 1
fi

sed -n -e 's/[ 	]*_X_ /<p><font face="Courier New">/p ; /<font/a</font></p>' -e 's/[ ]*\/\/[ ]*Desc[ ]*: /<blockquote>\n  <p>/p ; /<block/a</p></blockquote>' $1
