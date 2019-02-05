//
//  Visopsys
//  Copyright (C) 1998-2018 J. Andrew McLaughlin
//
//  This program is free software; you can redistribute it and/or modify it
//  under the terms of the GNU General Public License as published by the Free
//  Software Foundation; either version 2 of the License, or (at your option)
//  any later version.
//
//  This program is distributed in the hope that it will be useful, but
//  WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
//  or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
//  for more details.
//
//  You should have received a copy of the GNU General Public License along
//  with this program; if not, write to the Free Software Foundation, Inc.,
//  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
//  help.c
//

// This is like the UNIX-style 'man' command for showing documentation

/* This is the text that appears when a user requests help about this program
<help>

 -- List of commands (type 'help <command>' for specific help) --

adduser           Add a user account to the system
bootmenu          Install or edit the boot loader menu
cal               Display the days of the current calendar month
cat (or type)     Print a file's contents on the screen
cd                Change the current directory
cdrom             Control of the CD-ROM device, such as opening and closing
chkdisk           Check a filesystem for errors
copy-boot         Write a Visopsys boot sector
copy-mbr          Write a Visopsys MBR sector
cp (or copy)      Copy a file
date              Show the date
defrag            Defragment a filesystem
deluser           Delete a user account from the system
disks             Show the disk volumes in the system
domainname        Print or set the system's network domain name
fdisk             Manage hard disks (must be user "admin")
file              Show the type of a file
find              Traverse directory hierarchies
fontutil          Edit and convert Visopsys fonts
format            Create new, empty filesystems
help              Show this summary of help entries
hexdump           View files as hexadecimal listings
hostname          Print or set the system's network host name
ifconfig          Network device information and control
imgboot           The program launched at first system boot
install           Install Visopsys (must be user "admin")
keymap            View or change the current keyboard mapping
kill              Kill a running process
login             Start a new login process
logout (or exit)  End the current session
ls (or dir)       Show the files in a directory
lsdev             Display devices
md5               Calculate and print an md5 digest
mem               Show system memory usage
mkdir             Create one or more new directories
more              Display file's contents, one screenfull at a time
mount             Mount a filesystem
mv (or move)      Move a file (ren or rename have the same effect)
nm                Show symbol information for a dynamic program or library
passwd            Set the password on a user account
ping              'Ping' a host on the network
ps                Show list of current processes
pwd               Show the current directory
ramdisk           Create or destroy RAM disks
reboot            Reboot the computer
renice            Change the priority of a running process
rm (or del)       Delete a file
rmdir             Remove a directory
shutdown          Stop the computer
snake             A 'snake' game like the one found on mobile phones
sync              Synchronize all filesystems on disk
sysdiag           Perform system diagnostics
tar               Create or manage archives using the TAR format
touch             Update a file or create a new (empty) file
umount            Unmount a filesystem
uname             Print system information
unzip             Decompress and extract files from a compressed archive file
uptime            Time since last boot
vsh               Start a new command shell
zip               Compress and archive files

 -- Additional (graphics mode only) --

archman           A graphical program for managing archive files
calc              A calculator program
clock             Show a simple clock in the taskbar menu
cmdwin            Open a new command window
computer          Navigate the resources of the computer
confedit          Edit Visopsys configuration files
console           Show the console window
disprops          View or change the display settings
edit              Simple text editor
filebrowse        Navigate the file system
filesys           Set mount points and other filesystem properties
iconwin           A program for displaying custom icon windows
imgedit           Simple image editor
keyboard          Display a virtual keyboard
mines             A mine sweeper game
progman           View and manage programs and processes
screenshot        Take a screenshot
users             User manager for creating/deleting user accounts
view              Display a file in a new window
wallpaper         Load a new background wallpaper image

</help>
*/

#include <errno.h>
#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/api.h>
#include <sys/env.h>
#include <sys/paths.h>

#define _(string) gettext(string)


int main(int argc, char *argv[])
{
	int status = 0;
	char command[MAX_PATH_NAME_LENGTH];
	int count;

	setlocale(LC_ALL, getenv(ENV_LANG));
	textdomain("help");

	if (argc < 2)
	{
		// If there are no arguments, print the general help file
		status = system(PATH_PROGRAMS "/more " PATH_PROGRAMS_HELPFILES
			"/help.txt");
	}
	else
	{
		for (count = 1; count < argc; count ++)
		{
			// See if there is a help file for the argument
			sprintf(command, "%s/%s.txt", PATH_PROGRAMS_HELPFILES,
				argv[count]);
			status = fileFind(command, NULL);
			if (status < 0)
			{
				// No help file
				printf(_("There is no help available for \"%s\"\n"),
					argv[count]);
				return (status = ERR_NOSUCHFILE);
			}

			// For each argument, look for a help file whose name matches
			sprintf(command, PATH_PROGRAMS "/more %s/%s.txt",
				PATH_PROGRAMS_HELPFILES, argv[count]);

			// Search
			status = system(command);
			if (status < 0)
				break;
		}
	}

	return (status);
}

