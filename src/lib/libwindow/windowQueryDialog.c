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
//  windowQueryDialog.c
//

// This contains functions for user programs to operate GUI components.

#include <libintl.h>
#include <string.h>
#include <sys/errors.h>
#include <sys/window.h>

#define _(string) gettext(string)

extern int libwindow_initialized;
extern void libwindowInitialize(void);


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

_X_ int windowNewQueryDialog(objectKey parentWindow, const char *title, const char *message)
{
	// Desc: Create a 'query' dialog box, with the parent window 'parentWindow', and the given titlebar text and main message.  The dialog will have an 'OK' button and a 'CANCEL' button.  If the user presses OK, the function returns the value 1.  Otherwise it returns 0.  If 'parentWindow' is NULL, the dialog box is actually created as an independent window that looks the same as a dialog.  This is a blocking call that returns when the user closes the dialog window (i.e. the dialog is 'modal').

	int status = 0;

	if (!libwindow_initialized)
		libwindowInitialize();

	// Check params.  It's okay for parentWindow to be NULL.
	if (!title || !message)
		return (status = ERR_NULLPARAMETER);

	if (!windowNewChoiceDialog(parentWindow, title, message,
		(char *[]){ _("OK"), _("Cancel") }, 2, 0))
	{
		// OK button
		return (status = 1);
	}
	else
	{
		// Cancel button or error or whatever
		return (status = 0);
	}
}

