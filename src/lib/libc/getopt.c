//
//  Visopsys
//  Copyright (C) 1998-2018 J. Andrew McLaughlin
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
//  getopt.c
//

// This is the standard "getopt" function, as found in standard C libraries

#include <unistd.h>
#include <string.h>

char *optarg = NULL;
int optind = 0;
int opterr = 1;
int optopt = 0;


int getopt(int argc, char *const argv[], const char *optstring)
{
	/*  From the GNU man page:

	The getopt() function parses the command line arguments.  Its arguments
	argc and argv are the argument count and array as passed to the main()
	function on program invocation.  An element of argv that starts with
	'-' (and is not exactly "-" or "--") is an option element.  The charac-
	ters of this element (aside from the initial '-') are option charac-
	ters.  If getopt() is called repeatedly, it returns successively each
	of the option characters from each of the option elements.

	If getopt() finds another option character, it returns that character,
	updating the external variable optind and a static variable nextchar so
	that the next call to getopt() can resume the scan with the following
	option character or argv-element.

	If there are no more option characters, getopt() returns -1.  Then
	optind is the index in argv of the first argv-element that is not an
	option.

	optstring is a string containing the legitimate option characters.  If
	such a character is followed by a colon, the option requires an argu-
	ment, so getopt places a pointer to the following text in the same
	argv-element, or the text of the following argv-element, in optarg.
	Two colons mean an option takes an optional arg; if there is text in
	the current argv-element, it is returned in optarg, otherwise optarg is
	set to zero.  This is a GNU extension.  If optstring contains W fol-
	lowed by a semicolon, then -W foo is treated as the long option --foo.
	(The -W option is reserved by POSIX.2 for implementation extensions.)
	This behaviour is a GNU extension, not available with libraries before
	GNU libc 2.

	By default, getopt() permutes the contents of argv as it scans, so that
	eventually all the non-options are at the end.  Two other modes are
	also implemented.  If the first character of optstring is '+' or the
	environment variable POSIXLY_CORRECT is set, then option processing
	stops as soon as a non-option argument is encountered.  If the first
	character of optstring is '-', then each non-option argv-element is
	handled as if it were the argument of an option with character code 1.
	(This is used by programs that were written to expect options and other
	argv-elements in any order and that care about the ordering of the
	two.)  The special argument '--' forces an end of option-scanning
	regardless of the scanning mode.

	If getopt() does not recognize an option character, it prints an error
	message to stderr, stores the character in optopt, and returns '?'.
	The calling program may prevent the error message by setting opterr to
	0.

	If getopt() finds an option character in argv that was not included in
	optstring, or if it detects a missing option argument, it returns '?'
	and sets the external variable optopt to the actual option character.
	If the first character of optstring is a colon (':'), then getopt()
	returns ':' instead of '?' to indicate a missing option argument.  If
	an error was detected, and the first character of optstring is not a
	colon, and the external variable opterr is nonzero (which is the
	default), getopt() prints an error message.
	*/

	// So we do that, approximately.  More like the POSIXLY one though.

	static int nextchar = 0;
	int optstrlength = 0;
	char option = '\0';
	int count;

	if (!optind)
		// We don't want argv[0]
		optind += 1;

	while (1)
	{
		if (optind >= argc)
		{
			nextchar = 0;
			return (-1);
		}

		// Stop as soon as a non-option argument is encountered.
		if (argv[optind][0] != '-')
		{
			nextchar = 0;
			return (-1);
		}

		// Not exactly "-" or "--"
		if (argv[optind][1] == '\0')
		{
			nextchar = 0;
			return ((int) '?');
		}
		if (!strcmp(argv[optind], "--"))
		{
			nextchar = 0;
			return (-1);
		}

		// Skip the '-'
		if (!nextchar)
			nextchar += 1;

		// Looks like we have an argument.  What is it then?
		option = argv[optind][nextchar];
		if (option == '\0')
		{
			nextchar = 0;
			optind += 1;
			continue;
		}

		break;
	}

	// Is it in our list of acceptable options?
	optstrlength = strlen(optstring);
	for (count = 0; count < optstrlength; count ++)
	{
		if (optstring[count] == option)
		{
			// Okay, this is a good option

			// Are we expecting an argument?
			if ((count < (optstrlength - 1)) && (optstring[count + 1] == ':'))
			{
				// Is there more text in the current argument?
				if (argv[optind][nextchar + 1] != '\0')
					// That's the option
					optarg = (argv[optind] + nextchar + 1);

				// Otherwise, if the option is optional, there's no option.
				// Optional options need to occur in the same argument string.
				else if ((count < (optstrlength - 2)) &&
					(optstring[count + 2] == ':'))
				{
					optarg = NULL;
				}

				// Otherwise if there's a next argument, that's the argument.
				else
				{
					optind += 1;

					if ((optind >= argc) || (argv[optind][0] == '-'))
					{
						// There is no next argument.
						nextchar = 0;
						optarg = NULL;
						return ((int) ':');
					}

					optarg = argv[optind];
				}

				nextchar = 0;
				optind += 1;
			}

			nextchar += 1;
			return ((int) option);
		}
	}

	// If we fall through, the option was invalid
	nextchar = 0;
	optopt = (int) option;
	return ((int) '?');
}

