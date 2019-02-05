#!/bin/bash
##
##  Visopsys
##  Copyright (C) 1998-2018 J. Andrew McLaughlin
##
##  langstrings.sh
##

# Automate the updating of language string template files and translations

GETTXTCMD="xgettext -Lc -k_ -d-"
LANGS="de es ru tr"

# Do libntfs

pushd ../ports/ntfsprogs
TEMPLATE=libntfs.pot
rm -f $TEMPLATE
$GETTXTCMD libntfs/*.c >> $TEMPLATE
$GETTXTCMD ntfsprogs/*.c >> $TEMPLATE
for DIR in $LANGS ; do
	../../utils/mergepot.sh $TEMPLATE $DIR
done
popd

# Do libwindow

pushd ../src/lib/libwindow
TEMPLATE=libwindow.pot
$GETTXTCMD *.c > $TEMPLATE
for DIR in $LANGS ; do
	../../../utils/mergepot.sh $TEMPLATE $DIR
done
popd

# Do the programs

pushd ../src/programs
for FILE in *.c ; do
	TEMPLATE=`basename $FILE .c`.pot
	$GETTXTCMD $FILE > $TEMPLATE
	if [ ! -s $TEMPLATE ] ; then
		rm $TEMPLATE
		continue
	fi
	for DIR in $LANGS ; do
		../../utils/mergepot.sh $TEMPLATE $DIR
	done
done

cd fdisk
TEMPLATE=fdisk.pot
$GETTXTCMD *.c > $TEMPLATE
for DIR in $LANGS ; do
	../../../utils/mergepot.sh $TEMPLATE $DIR
done
popd

# Do the kernel

pushd ../src/kernel
TEMPLATE=kernel.pot
$GETTXTCMD *.c > $TEMPLATE
for DIR in $LANGS ; do
	../../utils/mergepot.sh $TEMPLATE $DIR
done
popd


exit 0

