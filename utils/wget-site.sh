#!/bin/sh
##
##  Visopsys
##  Copyright (C) 1998-2018 J. Andrew McLaughlin
##
##  wget-site.sh
##

# Retrieves the relevant parts of the visopsys.org website that we include
# in the 'docs' directory.

SITE=visopsys.org
TMP1=$SITE.tmp1
TMP2=$SITE.tmp2

if [ -d "$SITE" ] ; then
	for FILE in `find $SITE -type f | grep -v CVS` ; do
		rm $FILE
	done
fi

wget --recursive --level=99 --page-requisites --convert-links --restrict-file-names=windows -X /forums --reject zip --domains $SITE $SITE

rm -Rf $SITE/feed $SITE/comments
rm -f $SITE/index.html\@*

cvs update $SITE > $TMP1 2>&1
grep "^U " $TMP1 > $TMP2
for FILE in `cut -d' ' -f2 $TMP2` ; do
	rm $FILE ; cvs rm $FILE
done
rm -f $TMP1 $TMP2

exit 0

