#!/bin/sh
##
##  Visopsys
##  Copyright (C) 1998-2016 J. Andrew McLaughlin
##
##  image-cd.sh
##

# Installs the Visopsys system into a zipped CD-ROM ISO image file

IMAGEFLOPPYLOG=./image-floppy.log
MKISOFSLOG=./mkisofs.log
ZIPLOG=./zip.log

echo ""
echo "Making Visopsys CD-ROM IMAGE file"

# Are we doing a release version?  If the argument is "-r" then we use
# the release number in the destination directory name.  Otherwise, we
# assume an interim package and use the date instead
if [ "$1" = "-r" ] ; then
	# What is the current release version?
	RELEASE=`./release.sh`
	echo " - doing RELEASE version $RELEASE"
	RELFLAG=-r
else
	# What is the date?
	RELEASE=`date +%Y-%m-%d`
	echo " - doing INTERIM version $RELEASE (use -r flag for RELEASES)"
fi
echo ""

BUILDDIR=../build
ISOBOOT=-isoboot
NAME=visopsys-$RELEASE
FLOPPYZIP="$NAME""$ISOBOOT"-img.zip
FLOPPYIMAGE="$NAME""$ISOBOOT".img
ISOIMAGE=$NAME.iso
ZIPFILE=$NAME-iso.zip

TMPDIR=/tmp/iso$$.tmp
rm -Rf $TMPDIR
mkdir -p $TMPDIR

echo -n "Making/copying boot floppy image... "
./image-floppy.sh $RELFLAG $ISOBOOT > $IMAGEFLOPPYLOG 2>&1
if [ $? -ne 0 ] ; then
	echo ""
	echo -n "Not able to create floppy image $FLOPPYZIP.  "
	echo "See $IMAGEFLOPPYLOG.  Terminating."
	echo ""
	exit 1
fi
rm -f $IMAGEFLOPPYLOG
unzip $FLOPPYZIP > /dev/null 2>&1
rm -f $FLOPPYZIP
cp $FLOPPYIMAGE $TMPDIR
echo Done

# Copy all of the files from the build directory
echo -n "Copying build files... "
(cd $BUILDDIR ; tar cf - * ) | (cd $TMPDIR; tar xf - )
echo Done

echo -n "Copying doc files... "
(cd .. ; tar cf - docs ) | (cd $TMPDIR; tar xf - )
find $TMPDIR/docs -name CVS -exec rm -R {} \; > /dev/null 2>&1
echo Done

echo -n "Creating ISO image... "
rm -f $ISOIMAGE
mkisofs -U -D -b $FLOPPYIMAGE -c boot.catalog -hide $FLOPPYIMAGE -hide boot.catalog -V "Visopsys $RELEASE" -iso-level 3 -allow-leading-dots -o $ISOIMAGE $TMPDIR > $MKISOFSLOG 2>&1
if [ $? -ne 0 ] ; then
	echo ""
	echo -n "Not able to create ISO image $ISOIMAGE.  "
	echo "See $MKISOFSLOG.  Terminating."
	echo ""
	exit 1
fi
rm -f $MKISOFSLOG
echo Done

echo -n "Archiving... "
echo "Visopsys $RELEASE CD-ROM Release" > /tmp/comment
echo "Copyright (C) 1998-2016 J. Andrew McLaughlin" >> /tmp/comment
rm -f $ZIPFILE
zip -9 -z -r $ZIPFILE $ISOIMAGE < /tmp/comment > $ZIPLOG 2>&1
if [ $? -ne 0 ] ; then
	echo ""
	echo -n "Not able to create zip file $ZIPFILE.  "
	echo "See $ZIPLOG.  Terminating."
	echo ""
	exit 1
fi
rm -f /tmp/comment $ZIPLOG
echo Done

rm -f $FLOPPYIMAGE
rm -f $ISOIMAGE
rm -Rf $TMPDIR

echo ""
echo "File is: $ZIPFILE"
echo ""

exit 0

