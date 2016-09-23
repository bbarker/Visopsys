//
//  Visopsys
//  Copyright (C) 1998-2016 J. Andrew McLaughlin
//
//  This library is free software; you can redistribute it and/or modify it
//  under the terms of the GNU Lesser General Public License as published by
//  the Free Software Foundation; either version 2.1 of the License, or (at
//  your option) any later version.
//
//  This library is distributed in the hope that it will be useful, but
//  WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
//  General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public License
//  along with this library; if not, write to the Free Software Foundation,
//  Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
//  locale.h
//

// This is the Visopsys version of the standard header file locale.h

#if !defined(_LOCALE_H)

#define LOCALE_MAX_NAMELEN	16

struct lconv {
    char *currency_symbol;
    char *decimal_point;
    char *grouping;
    char *int_curr_symbol;
    char *mon_decimal_point;
    char *mon_grouping;
    char *mon_thousands_sep;
    char *negative_sign;
    char *positive_sign;
    char *thousands_sep;
    char frac_digits;
    char int_frac_digits;
    char n_cs_precedes;
    char n_sep_by_space;
    char n_sign_posn;
    char p_cs_precedes;
    char p_sep_by_space;
    char p_sign_posn;
};

#define LC_COLLATE  0x01
#define LC_CTYPE    0x02
#define LC_MESSAGES 0x04
#define LC_MONETARY 0x08
#define LC_NUMERIC  0x10
#define LC_TIME     0x20
#define LC_ALL      (LC_COLLATE | LC_CTYPE | LC_MESSAGES | LC_MONETARY | \
					LC_NUMERIC | LC_TIME)

char *setlocale(int, const char *);

#define _LOCALE_H
#endif

