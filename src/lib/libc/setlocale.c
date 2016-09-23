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
//  setlocale.c
//

// This is the standard "setlocale" function, as found in standard C libraries

#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>

char *_getLocaleCategory(int);

// This is the default 'C' locale
static const char *_c_locale_name = "C";
struct lconv _c_locale = {
	"",			// currency_symbol
	".",		// decimal_point
	"",			// grouping
	"",			// int_curr_symbol
	"",			// mon_decimal_point
	"",			// mon_grouping
	"",			// mon_thousands_sep
	"",			// negative_sign
	"",			// positive_sign
	"",			// thousands_sep
	CHAR_MAX,	// frac_digits
	CHAR_MAX,	// int_frac_digits
	CHAR_MAX,	// n_cs_precedes
	CHAR_MAX,	// n_sep_by_space
	CHAR_MAX,	// n_sign_posn
	CHAR_MAX,	// p_cs_precedes
	CHAR_MAX,	// p_sep_by_space
	CHAR_MAX,	// p_sign_posn
};

// Locale categories
char _lc_all[LOCALE_MAX_NAMELEN + 1];
char _lc_collate[LOCALE_MAX_NAMELEN + 1];
char _lc_ctype[LOCALE_MAX_NAMELEN + 1];
char _lc_messages[LOCALE_MAX_NAMELEN + 1];
char _lc_monetary[LOCALE_MAX_NAMELEN + 1];
char _lc_numeric[LOCALE_MAX_NAMELEN + 1];
char _lc_time[LOCALE_MAX_NAMELEN + 1];


char *_getLocaleCategory(int category)
{
	switch (category)
	{
		default:
		case LC_ALL:
			return (_lc_all);
		case LC_COLLATE:
			return (_lc_collate);
		case LC_CTYPE:
			return (_lc_ctype);
		case LC_MESSAGES:
			return (_lc_messages);
		case LC_MONETARY:
			return (_lc_monetary);
		case LC_NUMERIC:
			return (_lc_numeric);
		case LC_TIME:
			return (_lc_time);
	}
}


static char *setCategory(const char *name, char *category, const char *locale)
{
	category[0] = '\0';

	if (!strcmp(locale, ""))
	{
		// If locale is "", the locale is modified according to environment
		// variables.
		locale = getenv(name);
		if (locale)
		{
			strncpy(category, locale, LOCALE_MAX_NAMELEN);
			free((void *) locale);
		}
		else
			strcpy(category, _c_locale_name);
	}
	else
		strncpy(category, locale, LOCALE_MAX_NAMELEN);

	category[LOCALE_MAX_NAMELEN] = '\0';
	return (category);
}


char *setlocale(int category, const char *locale)
{
	char *returnLocale = NULL;

	if (!locale)
	{
		errno = ERR_NULLPARAMETER;
		return (returnLocale = NULL);
	}

	// Note that these calls are not redundant since each one of these
	// flags can result in copying into different categories

	if ((category & LC_ALL) == LC_ALL)
		returnLocale = setCategory("LC_ALL", _lc_all, locale);
	else
		strcpy(_lc_all, _c_locale_name);

	if (category & LC_COLLATE)
		returnLocale = setCategory("LC_COLLATE", _lc_collate, locale);
	else
		strcpy(_lc_collate, _c_locale_name);

	if (category & LC_CTYPE)
		returnLocale = setCategory("LC_CTYPE", _lc_ctype, locale);
	else
		strcpy(_lc_ctype, _c_locale_name);

	if (category & LC_MESSAGES)
		returnLocale = setCategory("LC_MESSAGES", _lc_messages, locale);
	else
		strcpy(_lc_messages, _c_locale_name);

	if (category & LC_MONETARY)
		returnLocale = setCategory("LC_MONETARY", _lc_monetary, locale);
	else
		strcpy(_lc_monetary, _c_locale_name);

	if (category & LC_NUMERIC)
		returnLocale = setCategory("LC_NUMERIC", _lc_numeric, locale);
	else
		strcpy(_lc_numeric, _c_locale_name);

	if (category & LC_TIME)
		returnLocale = setCategory("LC_TIME", _lc_time, locale);
	else
		strcpy(_lc_time, _c_locale_name);

	return (returnLocale);
}

