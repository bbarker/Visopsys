#!/bin/sh
##
##  Visopsys
##  Copyright (C) 1998-2018 J. Andrew McLaughlin
##
##  check-tree.sh
##

# Check the files in the build tree and make sure they are all listed in
# one of the install files.

# Then check that all of the items in the install files are in the build
# tree

BUILD_DIR=../build/
BUILD_FILES=buildfiles.$$
INST_FILES="../dist/system/install-files.basic ../dist/system/install-files.full"

if [ ! -d $BUILD_DIR ] ; then
	echo ERROR: No build directory
	exit 1
fi

# Get the list of all files in the build directory
(cd $BUILD_DIR; find *) > $BUILD_FILES

# Make sure each one is in an installation file
for FILE in `cat $BUILD_FILES` ; do
	FOUND=0
	for INSTFILE in $INST_FILES ; do
		GREP=`grep $FILE $INSTFILE | cut -f1 -d=`
		if [ "$GREP" != "" ] ; then
			FOUND=1
			break
		fi
	done

	if [ $FOUND -ne 1 ] ; then
		echo WARNING: File $FILE not in installation files
	fi
done

# Make sure each item listed in the install files is in the build tree
for INSTFILE in $INST_FILES ; do
	for FILE in `cat $INSTFILE | grep -v ^# | cut -f1 -d=` ; do
		(cd $BUILD_DIR ; ls .$FILE) > /dev/null 2>&1
		if [ $? -ne 0 ] ; then
			echo WARNING: File $FILE not in the build tree
		fi
	done
done

rm -f $BUILD_FILES

