#!/bin/sh
##
##  Visopsys
##  Copyright (C) 1998-2018 J. Andrew McLaughlin
##
##  image-usb.sh
##

# Installs the Visopsys system into a zipped USB image file

BLANKUSB=./blankusb.gz
INSTSCRIPT=./install.sh
MOUNTDIR=./tmp_mnt
ZIPLOG=./zip.log

echo ""
echo "Making Visopsys USB IMAGE file"

while [ "$1" != "" ] ; do
	# Are we doing a release version?  If the argument is "-r" then we use
	# the release number in the destination directory name.  Otherwise, we
	# assume an interim package and use the date instead
	if [ "$1" = "-r" ] ; then
		# What is the current release version?
		RELEASE=`./release.sh`
		echo " - doing RELEASE version $RELEASE"
	fi

	shift
done

# Check for things we need to be in the current directory
for FILE in $BLANKUSB $INSTSCRIPT ; do
	if [ ! -f $FILE ] ; then
		echo ""
		echo "Required file $FILE not found.  Terminating"
		echo ""
		exit 1
	fi
done

if [ "$RELEASE" = "" ] ; then
	# What is the date?
	RELEASE=`date +%Y-%m-%d`
	echo " - doing INTERIM version $RELEASE (use -r flag for RELEASES)"
fi

NAME=visopsys-"$RELEASE"
IMAGEFILE="$NAME"-usb.img
ZIPFILE="$NAME"-usb-img.zip
rm -f $IMAGEFILE
cp $BLANKUSB "$IMAGEFILE".gz
gunzip "$IMAGEFILE".gz

# Determine the starting offset of the first partition in the image.
# Assumptions:
#  1. We are installing in the first partition
#  2. The first partition has been made active
#  3. The image was created using 512-byte sectors.
STARTSEC=$(/sbin/fdisk -lu -b512 $IMAGEFILE 2> /dev/null | \
	grep ^"$IMAGEFILE"1 | tr -s ' ' | cut -d' ' -f3)
STARTOFF=$(($STARTSEC * 512))

# Connect the virtual partition to a loop device
LOOPDEV=$(/sbin/losetup -f)
/sbin/losetup -o $STARTOFF $LOOPDEV "$IMAGEFILE"

# Run the installation script
$INSTSCRIPT $LOOPDEV
STATUS=$?

# Disconnect the loop device
/sbin/losetup -d $LOOPDEV

if [ $STATUS -ne 0 ] ; then
	echo ""
	echo "Install failure.  Terminating"
	echo ""
	exit 1
fi

echo -n "Archiving... "
echo "Visopsys $RELEASE USB Image Release" > /tmp/comment
echo "Copyright (C) 1998-2018 J. Andrew McLaughlin" >> /tmp/comment
rm -f $ZIPFILE
zip -9 -z -r $ZIPFILE $IMAGEFILE < /tmp/comment > $ZIPLOG 2>&1
if [ $? -ne 0 ] ; then
	echo ""
	echo -n "Not able to create zip file $ZIPFILE.  "
	echo "See $ZIPLOG.  Terminating."
	echo ""
	exit 1
fi
rm -f /tmp/comment $IMAGEFILE $ZIPLOG
echo Done

echo ""
echo "File is: $ZIPFILE"
echo ""

exit 0

