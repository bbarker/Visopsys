#!/bin/sh
##
##  Visopsys
##  Copyright (C) 1998-2016 J. Andrew McLaughlin
##
##  wget-site.sh
##

# Retrieves the relevant parts of the visopsys.org website that we include
# in the 'docs' directory.

wget --recursive --level=99 --page-requisites --convert-links --restrict-file-names=windows -X /forums --reject zip --domains visopsys.org visopsys.org

rm -Rf visopsys.org/feed visopsys.org/comments

exit 0
