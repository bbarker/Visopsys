//
//  Visopsys
//  Copyright (C) 1998-2016 J. Andrew McLaughlin
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
//  kernelCharset.c
//

#include "kernelCharset.h"
#include "kernelError.h"
#include <string.h>

charset sets[] = {
	{
		CHARSET_NAME_ISO_8859_5, // Latin/Cyrillic (Slavic, Russian)
		{
			{ 0x80, 0x0080 }, // <control>
			{ 0x81, 0x0081 }, // <control>
			{ 0x82, 0x0082 }, // <control>
			{ 0x83, 0x0083 }, // <control>
			{ 0x84, 0x0084 }, // <control>
			{ 0x85, 0x0085 }, // <control>
			{ 0x86, 0x0086 }, // <control>
			{ 0x87, 0x0087 }, // <control>
			{ 0x88, 0x0088 }, // <control>
			{ 0x89, 0x0089 }, // <control>
			{ 0x8A, 0x008A }, // <control>
			{ 0x8B, 0x008B }, // <control>
			{ 0x8C, 0x008C }, // <control>
			{ 0x8D, 0x008D }, // <control>
			{ 0x8E, 0x008E }, // <control>
			{ 0x8F, 0x008F }, // <control>
			{ 0x90, 0x0090 }, // <control>
			{ 0x91, 0x0091 }, // <control>
			{ 0x92, 0x0092 }, // <control>
			{ 0x93, 0x0093 }, // <control>
			{ 0x94, 0x0094 }, // <control>
			{ 0x95, 0x0095 }, // <control>
			{ 0x96, 0x0096 }, // <control>
			{ 0x97, 0x0097 }, // <control>
			{ 0x98, 0x0098 }, // <control>
			{ 0x99, 0x0099 }, // <control>
			{ 0x9A, 0x009A }, // <control>
			{ 0x9B, 0x009B }, // <control>
			{ 0x9C, 0x009C }, // <control>
			{ 0x9D, 0x009D }, // <control>
			{ 0x9E, 0x009E }, // <control>
			{ 0x9F, 0x009F }, // <control>
			{ 0xA0, 0x00A0 }, // no-break space
			{ 0xA1, 0x0401 }, // cyrillic capital letter IO
			{ 0xA2, 0x0402 }, // cyrillic capital letter DJE
			{ 0xA3, 0x0403 }, // cyrillic capital letter GJE
			{ 0xA4, 0x0404 }, // cyrillic capital letter ukrainian IE
			{ 0xA5, 0x0405 }, // cyrillic capital letter DZE
			{ 0xA6, 0x0406 }, // cyrillic capital letter byelorussian-ukrainian I
			{ 0xA7, 0x0407 }, // cyrillic capital letter YI
			{ 0xA8, 0x0408 }, // cyrillic capital letter JE
			{ 0xA9, 0x0409 }, // cyrillic capital letter LJE
			{ 0xAA, 0x040A }, // cyrillic capital letter NJE
			{ 0xAB, 0x040B }, // cyrillic capital letter TSHE
			{ 0xAC, 0x040C }, // cyrillic capital letter KJE
			{ 0xAD, 0x00AD }, // soft hyphen
			{ 0xAE, 0x040E }, // cyrillic capital letter short U
			{ 0xAF, 0x040F }, // cyrillic capital letter DZHE
			{ 0xB0, 0x0410 }, // cyrillic capital letter A
			{ 0xB1, 0x0411 }, // cyrillic capital letter BE
			{ 0xB2, 0x0412 }, // cyrillic capital letter VE
			{ 0xB3, 0x0413 }, // cyrillic capital letter GHE
			{ 0xB4, 0x0414 }, // cyrillic capital letter DE
			{ 0xB5, 0x0415 }, // cyrillic capital letter IE
			{ 0xB6, 0x0416 }, // cyrillic capital letter ZHE
			{ 0xB7, 0x0417 }, // cyrillic capital letter ZE
			{ 0xB8, 0x0418 }, // cyrillic capital letter I
			{ 0xB9, 0x0419 }, // cyrillic capital letter short I
			{ 0xBA, 0x041A }, // cyrillic capital letter KA
			{ 0xBB, 0x041B }, // cyrillic capital letter EL
			{ 0xBC, 0x041C }, // cyrillic capital letter EM
			{ 0xBD, 0x041D }, // cyrillic capital letter EN
			{ 0xBE, 0x041E }, // cyrillic capital letter O
			{ 0xBF, 0x041F }, // cyrillic capital letter PE
			{ 0xC0, 0x0420 }, // cyrillic capital letter ER
			{ 0xC1, 0x0421 }, // cyrillic capital letter ES
			{ 0xC2, 0x0422 }, // cyrillic capital letter TE
			{ 0xC3, 0x0423 }, // cyrillic capital letter U
			{ 0xC4, 0x0424 }, // cyrillic capital letter EF
			{ 0xC5, 0x0425 }, // cyrillic capital letter HA
			{ 0xC6, 0x0426 }, // cyrillic capital letter TSE
			{ 0xC7, 0x0427 }, // cyrillic capital letter CHE
			{ 0xC8, 0x0428 }, // cyrillic capital letter SHA
			{ 0xC9, 0x0429 }, // cyrillic capital letter SHCHA
			{ 0xCA, 0x042A }, // cyrillic capital letter hard sign
			{ 0xCB, 0x042B }, // cyrillic capital letter YERU
			{ 0xCC, 0x042C }, // cyrillic capital letter soft sign
			{ 0xCD, 0x042D }, // cyrillic capital letter E
			{ 0xCE, 0x042E }, // cyrillic capital letter YU
			{ 0xCF, 0x042F }, // cyrillic capital letter YA
			{ 0xD0, 0x0430 }, // cyrillic small letter A
			{ 0xD1, 0x0431 }, // cyrillic small letter BE
			{ 0xD2, 0x0432 }, // cyrillic small letter VE
			{ 0xD3, 0x0433 }, // cyrillic small letter GHE
			{ 0xD4, 0x0434 }, // cyrillic small letter DE
			{ 0xD5, 0x0435 }, // cyrillic small letter IE
			{ 0xD6, 0x0436 }, // cyrillic small letter ZHE
			{ 0xD7, 0x0437 }, // cyrillic small letter ZE
			{ 0xD8, 0x0438 }, // cyrillic small letter I
			{ 0xD9, 0x0439 }, // cyrillic small letter short I
			{ 0xDA, 0x043A }, // cyrillic small letter KA
			{ 0xDB, 0x043B }, // cyrillic small letter EL
			{ 0xDC, 0x043C }, // cyrillic small letter EM
			{ 0xDD, 0x043D }, // cyrillic small letter EN
			{ 0xDE, 0x043E }, // cyrillic small letter O
			{ 0xDF, 0x043F }, // cyrillic small letter PE
			{ 0xE0, 0x0440 }, // cyrillic small letter ER
			{ 0xE1, 0x0441 }, // cyrillic small letter ES
			{ 0xE2, 0x0442 }, // cyrillic small letter TE
			{ 0xE3, 0x0443 }, // cyrillic small letter U
			{ 0xE4, 0x0444 }, // cyrillic small letter EF
			{ 0xE5, 0x0445 }, // cyrillic small letter HA
			{ 0xE6, 0x0446 }, // cyrillic small letter TSE
			{ 0xE7, 0x0447 }, // cyrillic small letter CHE
			{ 0xE8, 0x0448 }, // cyrillic small letter SHA
			{ 0xE9, 0x0449 }, // cyrillic small letter SHCHA
			{ 0xEA, 0x044A }, // cyrillic small letter hard sign
			{ 0xEB, 0x044B }, // cyrillic small letter YERU
			{ 0xEC, 0x044C }, // cyrillic small letter soft sign
			{ 0xED, 0x044D }, // cyrillic small letter E
			{ 0xEE, 0x044E }, // cyrillic small letter YU
			{ 0xEF, 0x044F }, // cyrillic small letter YA
			{ 0xF0, 0x2116 }, // numero sign
			{ 0xF1, 0x0451 }, // cyrillic small letter IO
			{ 0xF2, 0x0452 }, // cyrillic small letter DJE
			{ 0xF3, 0x0453 }, // cyrillic small letter GJE
			{ 0xF4, 0x0454 }, // cyrillic small letter ukrainian IE
			{ 0xF5, 0x0455 }, // cyrillic small letter DZE
			{ 0xF6, 0x0456 }, // cyrillic small letter byelorussian-ukrainian I
			{ 0xF7, 0x0457 }, // cyrillic small letter YI
			{ 0xF8, 0x0458 }, // cyrillic small letter JE
			{ 0xF9, 0x0459 }, // cyrillic small letter LJE
			{ 0xFA, 0x045A }, // cyrillic small letter NJE
			{ 0xFB, 0x045B }, // cyrillic small letter TSHE
			{ 0xFC, 0x045C }, // cyrillic small letter KJE
			{ 0xFD, 0x00A7 }, // section sign
			{ 0xFE, 0x045E }, // cyrillic small letter short U
			{ 0xFF, 0x045F }  // cyrillic small letter DZHE
		}
	},
	{
		CHARSET_NAME_ISO_8859_9, // Latin-5 (Turkish)
		{
			{ 0x80, 0x0080 }, // <control>
			{ 0x81, 0x0081 }, // <control>
			{ 0x82, 0x0082 }, // <control>
			{ 0x83, 0x0083 }, // <control>
			{ 0x84, 0x0084 }, // <control>
			{ 0x85, 0x0085 }, // <control>
			{ 0x86, 0x0086 }, // <control>
			{ 0x87, 0x0087 }, // <control>
			{ 0x88, 0x0088 }, // <control>
			{ 0x89, 0x0089 }, // <control>
			{ 0x8A, 0x008A }, // <control>
			{ 0x8B, 0x008B }, // <control>
			{ 0x8C, 0x008C }, // <control>
			{ 0x8D, 0x008D }, // <control>
			{ 0x8E, 0x008E }, // <control>
			{ 0x8F, 0x008F }, // <control>
			{ 0x90, 0x0090 }, // <control>
			{ 0x91, 0x0091 }, // <control>
			{ 0x92, 0x0092 }, // <control>
			{ 0x93, 0x0093 }, // <control>
			{ 0x94, 0x0094 }, // <control>
			{ 0x95, 0x0095 }, // <control>
			{ 0x96, 0x0096 }, // <control>
			{ 0x97, 0x0097 }, // <control>
			{ 0x98, 0x0098 }, // <control>
			{ 0x99, 0x0099 }, // <control>
			{ 0x9A, 0x009A }, // <control>
			{ 0x9B, 0x009B }, // <control>
			{ 0x9C, 0x009C }, // <control>
			{ 0x9D, 0x009D }, // <control>
			{ 0x9E, 0x009E }, // <control>
			{ 0x9F, 0x009F }, // <control>
			{ 0xA0, 0x00A0 }, // no-break space
			{ 0xA1, 0x00A1 }, // inverted exclamation mark
			{ 0xA2, 0x00A2 }, // cent sign
			{ 0xA3, 0x00A3 }, // pound sign
			{ 0xA4, 0x00A4 }, // currency sign
			{ 0xA5, 0x00A5 }, // yen sign
			{ 0xA6, 0x00A6 }, // broken bar
			{ 0xA7, 0x00A7 }, // section sign
			{ 0xA8, 0x00A8 }, // diaeresis
			{ 0xA9, 0x00A9 }, // copyright sign
			{ 0xAA, 0x00AA }, // feminine ordinal indicator
			{ 0xAB, 0x00AB }, // left-pointing double angle quotation mark
			{ 0xAC, 0x00AC }, // not sign
			{ 0xAD, 0x00AD }, // soft hyphen
			{ 0xAE, 0x00AE }, // registered sign
			{ 0xAF, 0x00AF }, // macron
			{ 0xB0, 0x00B0 }, // degree sign
			{ 0xB1, 0x00B1 }, // plus-minus sign
			{ 0xB2, 0x00B2 }, // superscript two
			{ 0xB3, 0x00B3 }, // superscript three
			{ 0xB4, 0x00B4 }, // acute accent
			{ 0xB5, 0x00B5 }, // micro sign
			{ 0xB6, 0x00B6 }, // pilcrow sign
			{ 0xB7, 0x00B7 }, // middle dot
			{ 0xB8, 0x00B8 }, // cedilla
			{ 0xB9, 0x00B9 }, // superscript one
			{ 0xBA, 0x00BA }, // masculine ordinal indicator
			{ 0xBB, 0x00BB }, // right-pointing double angle quotation mark
			{ 0xBC, 0x00BC }, // vulgar fraction one quarter
			{ 0xBD, 0x00BD }, // vulgar fraction one half
			{ 0xBE, 0x00BE }, // vulgar fraction three quarters
			{ 0xBF, 0x00BF }, // inverted question mark
			{ 0xC0, 0x00C0 }, // latin capital letter A with grave
			{ 0xC1, 0x00C1 }, // latin capital letter A with acute
			{ 0xC2, 0x00C2 }, // latin capital letter A with circumflex
			{ 0xC3, 0x00C3 }, // latin capital letter A with tilde
			{ 0xC4, 0x00C4 }, // latin capital letter A with diaeresis
			{ 0xC5, 0x00C5 }, // latin capital letter A with ring above
			{ 0xC6, 0x00C6 }, // latin capital letter AE
			{ 0xC7, 0x00C7 }, // latin capital letter C with cedilla
			{ 0xC8, 0x00C8 }, // latin capital letter E with grave
			{ 0xC9, 0x00C9 }, // latin capital letter E with acute
			{ 0xCA, 0x00CA }, // latin capital letter E with circumflex
			{ 0xCB, 0x00CB }, // latin capital letter E with diaeresis
			{ 0xCC, 0x00CC }, // latin capital letter I with grave
			{ 0xCD, 0x00CD }, // latin capital letter I with acute
			{ 0xCE, 0x00CE }, // latin capital letter I with circumflex
			{ 0xCF, 0x00CF }, // latin capital letter I with diaeresis
			{ 0xD0, 0x011E }, // latin capital letter G with breve
			{ 0xD1, 0x00D1 }, // latin capital letter N with tilde
			{ 0xD2, 0x00D2 }, // latin capital letter O with grave
			{ 0xD3, 0x00D3 }, // latin capital letter O with acute
			{ 0xD4, 0x00D4 }, // latin capital letter O with circumflex
			{ 0xD5, 0x00D5 }, // latin capital letter O with tilde
			{ 0xD6, 0x00D6 }, // latin capital letter O with diaeresis
			{ 0xD7, 0x00D7 }, // multiplication sign
			{ 0xD8, 0x00D8 }, // latin capital letter O with stroke
			{ 0xD9, 0x00D9 }, // latin capital letter U with grave
			{ 0xDA, 0x00DA }, // latin capital letter U with acute
			{ 0xDB, 0x00DB }, // latin capital letter U with circumflex
			{ 0xDC, 0x00DC }, // latin capital letter U with diaeresis
			{ 0xDD, 0x0130 }, // latin capital letter I with dot above
			{ 0xDE, 0x015E }, // latin capital letter S with cedilla
			{ 0xDF, 0x00DF }, // latin small letter sharp S
			{ 0xE0, 0x00E0 }, // latin small letter A with grave
			{ 0xE1, 0x00E1 }, // latin small letter A with acute
			{ 0xE2, 0x00E2 }, // latin small letter A with circumflex
			{ 0xE3, 0x00E3 }, // latin small letter A with tilde
			{ 0xE4, 0x00E4 }, // latin small letter A with diaeresis
			{ 0xE5, 0x00E5 }, // latin small letter A with ring above
			{ 0xE6, 0x00E6 }, // latin small letter AE
			{ 0xE7, 0x00E7 }, // latin small letter C with cedilla
			{ 0xE8, 0x00E8 }, // latin small letter E with grave
			{ 0xE9, 0x00E9 }, // latin small letter E with acute
			{ 0xEA, 0x00EA }, // latin small letter E with circumflex
			{ 0xEB, 0x00EB }, // latin small letter E with diaeresis
			{ 0xEC, 0x00EC }, // latin small letter I with grave
			{ 0xED, 0x00ED }, // latin small letter I with acute
			{ 0xEE, 0x00EE }, // latin small letter I with circumflex
			{ 0xEF, 0x00EF }, // latin small letter I with diaeresis
			{ 0xF0, 0x011F }, // latin small letter G with breve
			{ 0xF1, 0x00F1 }, // latin small letter N with tilde
			{ 0xF2, 0x00F2 }, // latin small letter O with grave
			{ 0xF3, 0x00F3 }, // latin small letter O with acute
			{ 0xF4, 0x00F4 }, // latin small letter O with circumflex
			{ 0xF5, 0x00F5 }, // latin small letter O with tilde
			{ 0xF6, 0x00F6 }, // latin small letter O with diaeresis
			{ 0xF7, 0x00F7 }, // division sign
			{ 0xF8, 0x00F8 }, // latin small letter O with stroke
			{ 0xF9, 0x00F9 }, // latin small letter U with grave
			{ 0xFA, 0x00FA }, // latin small letter U with acute
			{ 0xFB, 0x00FB }, // latin small letter U with circumflex
			{ 0xFC, 0x00FC }, // latin small letter U with diaeresis
			{ 0xFD, 0x0131 }, // latin small letter dotless I
			{ 0xFE, 0x015F }, // latin small letter S with cedilla
			{ 0xFF, 0x00FF }  // latin small letter Y with diaeresis
		}
	},
	{
		CHARSET_NAME_ISO_8859_15, // Latin-9 (Western Europe)
		{
			{ 0x80, 0x0080 }, // <control>
			{ 0x81, 0x0081 }, // <control>
			{ 0x82, 0x0082 }, // <control>
			{ 0x83, 0x0083 }, // <control>
			{ 0x84, 0x0084 }, // <control>
			{ 0x85, 0x0085 }, // <control>
			{ 0x86, 0x0086 }, // <control>
			{ 0x87, 0x0087 }, // <control>
			{ 0x88, 0x0088 }, // <control>
			{ 0x89, 0x0089 }, // <control>
			{ 0x8A, 0x008A }, // <control>
			{ 0x8B, 0x008B }, // <control>
			{ 0x8C, 0x008C }, // <control>
			{ 0x8D, 0x008D }, // <control>
			{ 0x8E, 0x008E }, // <control>
			{ 0x8F, 0x008F }, // <control>
			{ 0x90, 0x0090 }, // <control>
			{ 0x91, 0x0091 }, // <control>
			{ 0x92, 0x0092 }, // <control>
			{ 0x93, 0x0093 }, // <control>
			{ 0x94, 0x0094 }, // <control>
			{ 0x95, 0x0095 }, // <control>
			{ 0x96, 0x0096 }, // <control>
			{ 0x97, 0x0097 }, // <control>
			{ 0x98, 0x0098 }, // <control>
			{ 0x99, 0x0099 }, // <control>
			{ 0x9A, 0x009A }, // <control>
			{ 0x9B, 0x009B }, // <control>
			{ 0x9C, 0x009C }, // <control>
			{ 0x9D, 0x009D }, // <control>
			{ 0x9E, 0x009E }, // <control>
			{ 0x9F, 0x009F }, // <control>
			{ 0xA0, 0x00A0 }, // no-break space
			{ 0xA1, 0x00A1 }, // inverted exclamation mark
			{ 0xA2, 0x00A2 }, // cent sign
			{ 0xA3, 0x00A3 }, // pound sign
			{ 0xA4, 0x20AC }, // euro sign
			{ 0xA5, 0x00A5 }, // yen sign
			{ 0xA6, 0x0160 }, // latin capital letter S with caron
			{ 0xA7, 0x00A7 }, // section sign
			{ 0xA8, 0x0161 }, // latin small letter S with caron
			{ 0xA9, 0x00A9 }, // copyright sign
			{ 0xAA, 0x00AA }, // feminine ordinal indicator
			{ 0xAB, 0x00AB }, // left-pointing double angle quotation mark
			{ 0xAC, 0x00AC }, // not sign
			{ 0xAD, 0x00AD }, // soft hyphen
			{ 0xAE, 0x00AE }, // registered sign
			{ 0xAF, 0x00AF }, // macron
			{ 0xB0, 0x00B0 }, // degree sign
			{ 0xB1, 0x00B1 }, // plus-minus sign
			{ 0xB2, 0x00B2 }, // superscript 2
			{ 0xB3, 0x00B3 }, // superscript 3
			{ 0xB4, 0x017D }, // latin capital letter Z with caron
			{ 0xB5, 0x00B5 }, // micro sign
			{ 0xB6, 0x00B6 }, // pilcrow sign
			{ 0xB7, 0x00B7 }, // middle dot
			{ 0xB8, 0x017E }, // latin small letter Z with caron
			{ 0xB9, 0x00B9 }, // superscript 1
			{ 0xBA, 0x00BA }, // masculine ordinal indicator
			{ 0xBB, 0x00BB }, // right-pointing double angle quotation mark
			{ 0xBC, 0x0152 }, // latin capital ligature OE
			{ 0xBD, 0x0153 }, // latin small ligature OE
			{ 0xBE, 0x0178 }, // latin capital letter Y with diaeresis
			{ 0xBF, 0x00BF }, // inverted question mark
			{ 0xC0, 0x00C0 }, // latin capital letter A with grave
			{ 0xC1, 0x00C1 }, // latin capital letter A with acute
			{ 0xC2, 0x00C2 }, // latin capital letter A with circumflex
			{ 0xC3, 0x00C3 }, // latin capital letter A with tilde
			{ 0xC4, 0x00C4 }, // latin capital letter A with diaeresis
			{ 0xC5, 0x00C5 }, // latin capital letter A with ring above
			{ 0xC6, 0x00C6 }, // latin capital letter AE
			{ 0xC7, 0x00C7 }, // latin capital letter C with cedilla
			{ 0xC8, 0x00C8 }, // latin capital letter E with grave
			{ 0xC9, 0x00C9 }, // latin capital letter E with acute
			{ 0xCA, 0x00CA }, // latin capital letter E with circumflex
			{ 0xCB, 0x00CB }, // latin capital letter E with diaeresis
			{ 0xCC, 0x00CC }, // latin capital letter I with grave
			{ 0xCD, 0x00CD }, // latin capital letter I with acute
			{ 0xCE, 0x00CE }, // latin capital letter I with circumflex
			{ 0xCF, 0x00CF }, // latin capital letter I with diaeresis
			{ 0xD0, 0x00D0 }, // latin capital letter ETH
			{ 0xD1, 0x00D1 }, // latin capital letter N with tilde
			{ 0xD2, 0x00D2 }, // latin capital letter O with grave
			{ 0xD3, 0x00D3 }, // latin capital letter O with acute
			{ 0xD4, 0x00D4 }, // latin capital letter O with circumflex
			{ 0xD5, 0x00D5 }, // latin capital letter O with tilde
			{ 0xD6, 0x00D6 }, // latin capital letter O with diaeresis
			{ 0xD7, 0x00D7 }, // multiplication sign
			{ 0xD8, 0x00D8 }, // latin capital letter O with stroke
			{ 0xD9, 0x00D9 }, // latin capital letter U with grave
			{ 0xDA, 0x00DA }, // latin capital letter U with acute
			{ 0xDB, 0x00DB }, // latin capital letter U with circumflex
			{ 0xDC, 0x00DC }, // latin capital letter U with diaeresis
			{ 0xDD, 0x00DD }, // latin capital letter Y with acute
			{ 0xDE, 0x00DE }, // latin capital letter thorn
			{ 0xDF, 0x00DF }, // latin small letter sharp S
			{ 0xE0, 0x00E0 }, // latin small letter A with grave
			{ 0xE1, 0x00E1 }, // latin small letter A with acute
			{ 0xE2, 0x00E2 }, // latin small letter A with circumflex
			{ 0xE3, 0x00E3 }, // latin small letter A with tilde
			{ 0xE4, 0x00E4 }, // latin small letter A with diaeresis
			{ 0xE5, 0x00E5 }, // latin small letter A with ring above
			{ 0xE6, 0x00E6 }, // latin small letter AE
			{ 0xE7, 0x00E7 }, // latin small letter C with cedilla
			{ 0xE8, 0x00E8 }, // latin small letter E with grave
			{ 0xE9, 0x00E9 }, // latin small letter E with acute
			{ 0xEA, 0x00EA }, // latin small letter E with circumflex
			{ 0xEB, 0x00EB }, // latin small letter E with diaeresis
			{ 0xEC, 0x00EC }, // latin small letter I with grave
			{ 0xED, 0x00ED }, // latin small letter I with acute
			{ 0xEE, 0x00EE }, // latin small letter I with circumflex
			{ 0xEF, 0x00EF }, // latin small letter I with diaeresis
			{ 0xF0, 0x00F0 }, // latin small letter eth
			{ 0xF1, 0x00F1 }, // latin small letter N with tilde
			{ 0xF2, 0x00F2 }, // latin small letter O with grave
			{ 0xF3, 0x00F3 }, // latin small letter O with acute
			{ 0xF4, 0x00F4 }, // latin small letter O with circumflex
			{ 0xF5, 0x00F5 }, // latin small letter O with tilde
			{ 0xF6, 0x00F6 }, // latin small letter O with diaeresis
			{ 0xF7, 0x00F7 }, // division sign
			{ 0xF8, 0x00F8 }, // latin small letter O with stroke
			{ 0xF9, 0x00F9 }, // latin small letter U with grave
			{ 0xFA, 0x00FA }, // latin small letter U with acute
			{ 0xFB, 0x00FB }, // latin small letter U with circumflex
			{ 0xFC, 0x00FC }, // latin small letter U with diaeresis
			{ 0xFD, 0x00FD }, // latin small letter Y with acute
			{ 0xFE, 0x00FE }, // latin small letter thorn
			{ 0xFF, 0x00FF }  // latin small letter Y with diaeresis
		}
	},
	{
		CHARSET_NAME_ISO_8859_16, // Latin-10 (Southeastern Europe)
		{
			{ 0x80, 0x0080 }, // <control>
			{ 0x81, 0x0081 }, // <control>
			{ 0x82, 0x0082 }, // <control>
			{ 0x83, 0x0083 }, // <control>
			{ 0x84, 0x0084 }, // <control>
			{ 0x85, 0x0085 }, // <control>
			{ 0x86, 0x0086 }, // <control>
			{ 0x87, 0x0087 }, // <control>
			{ 0x88, 0x0088 }, // <control>
			{ 0x89, 0x0089 }, // <control>
			{ 0x8A, 0x008A }, // <control>
			{ 0x8B, 0x008B }, // <control>
			{ 0x8C, 0x008C }, // <control>
			{ 0x8D, 0x008D }, // <control>
			{ 0x8E, 0x008E }, // <control>
			{ 0x8F, 0x008F }, // <control>
			{ 0x90, 0x0090 }, // <control>
			{ 0x91, 0x0091 }, // <control>
			{ 0x92, 0x0092 }, // <control>
			{ 0x93, 0x0093 }, // <control>
			{ 0x94, 0x0094 }, // <control>
			{ 0x95, 0x0095 }, // <control>
			{ 0x96, 0x0096 }, // <control>
			{ 0x97, 0x0097 }, // <control>
			{ 0x98, 0x0098 }, // <control>
			{ 0x99, 0x0099 }, // <control>
			{ 0x9A, 0x009A }, // <control>
			{ 0x9B, 0x009B }, // <control>
			{ 0x9C, 0x009C }, // <control>
			{ 0x9D, 0x009D }, // <control>
			{ 0x9E, 0x009E }, // <control>
			{ 0x9F, 0x009F }, // <control>
			{ 0xA0, 0x00A0 }, // no-break space
			{ 0xA1, 0x0104 }, // latin capital letter A with ogonek
			{ 0xA2, 0x0105 }, // latin small letter A with ogonek
			{ 0xA3, 0x0141 }, // latin capital letter L with stroke
			{ 0xA4, 0x20AC }, // euro sign
			{ 0xA5, 0x201E }, // double low-9 quotation mark
			{ 0xA6, 0x0160 }, // latin capital letter S with caron
			{ 0xA7, 0x00A7 }, // section sign
			{ 0xA8, 0x0161 }, // latin small letter S with caron
			{ 0xA9, 0x00A9 }, // copyright sign
			{ 0xAA, 0x0218 }, // latin capital letter S with comma below
			{ 0xAB, 0x00AB }, // left-pointing double angle quotation mark
			{ 0xAC, 0x0179 }, // latin capital letter Z with acute
			{ 0xAD, 0x00AD }, // soft hyphen
			{ 0xAE, 0x017A }, // latin small letter Z with acute
			{ 0xAF, 0x017B }, // latin capital letter Z with dot above
			{ 0xB0, 0x00B0 }, // degree sign
			{ 0xB1, 0x00B1 }, // plus-minus sign
			{ 0xB2, 0x010C }, // latin capital letter C with caron
			{ 0xB3, 0x0142 }, // latin small letter L with stroke
			{ 0xB4, 0x017D }, // latin capital letter Z with caron
			{ 0xB5, 0x201D }, // right double quotation mark
			{ 0xB6, 0x00B6 }, // pilcrow sign
			{ 0xB7, 0x00B7 }, // middle dot
			{ 0xB8, 0x017E }, // latin small letter Z with caron
			{ 0xB9, 0x010D }, // latin small letter C with caron
			{ 0xBA, 0x0219 }, // latin small letter S with comma below
			{ 0xBB, 0x00BB }, // right-pointing double angle quotation mark
			{ 0xBC, 0x0152 }, // latin capital ligature OE
			{ 0xBD, 0x0153 }, // latin small ligature OE
			{ 0xBE, 0x0178 }, // latin capital letter Y with diaeresis
			{ 0xBF, 0x017C }, // latin small letter Z with dot above
			{ 0xC0, 0x00C0 }, // latin capital letter A with grave
			{ 0xC1, 0x00C1 }, // latin capital letter A with acute
			{ 0xC2, 0x00C2 }, // latin capital letter A with circumflex
			{ 0xC3, 0x0102 }, // latin capital letter A with breve
			{ 0xC4, 0x00C4 }, // latin capital letter A with diaeresis
			{ 0xC5, 0x0106 }, // latin capital letter C with acute
			{ 0xC6, 0x00C6 }, // latin capital letter AE
			{ 0xC7, 0x00C7 }, // latin capital letter C with cedilla
			{ 0xC8, 0x00C8 }, // latin capital letter E with grave
			{ 0xC9, 0x00C9 }, // latin capital letter E with acute
			{ 0xCA, 0x00CA }, // latin capital letter E with circumflex
			{ 0xCB, 0x00CB }, // latin capital letter E with diaeresis
			{ 0xCC, 0x00CC }, // latin capital letter I with grave
			{ 0xCD, 0x00CD }, // latin capital letter I with acute
			{ 0xCE, 0x00CE }, // latin capital letter I with circumflex
			{ 0xCF, 0x00CF }, // latin capital letter I with diaeresis
			{ 0xD0, 0x0110 }, // latin capital letter D with stroke
			{ 0xD1, 0x0143 }, // latin capital letter N with acute
			{ 0xD2, 0x00D2 }, // latin capital letter O with grave
			{ 0xD3, 0x00D3 }, // latin capital letter O with acute
			{ 0xD4, 0x00D4 }, // latin capital letter O with circumflex
			{ 0xD5, 0x0150 }, // latin capital letter O with double acute
			{ 0xD6, 0x00D6 }, // latin capital letter O with diaeresis
			{ 0xD7, 0x015A }, // latin capital letter S with acute
			{ 0xD8, 0x0170 }, // latin capital letter U with double acute
			{ 0xD9, 0x00D9 }, // latin capital letter U with grave
			{ 0xDA, 0x00DA }, // latin capital letter U with acute
			{ 0xDB, 0x00DB }, // latin capital letter U with circumflex
			{ 0xDC, 0x00DC }, // latin capital letter U with diaeresis
			{ 0xDD, 0x0118 }, // latin capital letter E with ogonek
			{ 0xDE, 0x021A }, // latin capital letter T with comma below
			{ 0xDF, 0x00DF }, // latin small letter sharp s
			{ 0xE0, 0x00E0 }, // latin small letter A with grave
			{ 0xE1, 0x00E1 }, // latin small letter A with acute
			{ 0xE2, 0x00E2 }, // latin small letter A with circumflex
			{ 0xE3, 0x0103 }, // latin small letter A with breve
			{ 0xE4, 0x00E4 }, // latin small letter A with diaeresis
			{ 0xE5, 0x0107 }, // latin small letter C with acute
			{ 0xE6, 0x00E6 }, // latin small letter AE
			{ 0xE7, 0x00E7 }, // latin small letter C with cedilla
			{ 0xE8, 0x00E8 }, // latin small letter E with grave
			{ 0xE9, 0x00E9 }, // latin small letter E with acute
			{ 0xEA, 0x00EA }, // latin small letter E with circumflex
			{ 0xEB, 0x00EB }, // latin small letter E with diaeresis
			{ 0xEC, 0x00EC }, // latin small letter I with grave
			{ 0xED, 0x00ED }, // latin small letter I with acute
			{ 0xEE, 0x00EE }, // latin small letter I with circumflex
			{ 0xEF, 0x00EF }, // latin small letter I with diaeresis
			{ 0xF0, 0x0111 }, // latin small letter D with stroke
			{ 0xF1, 0x0144 }, // latin small letter N with acute
			{ 0xF2, 0x00F2 }, // latin small letter O with grave
			{ 0xF3, 0x00F3 }, // latin small letter O with acute
			{ 0xF4, 0x00F4 }, // latin small letter O with circumflex
			{ 0xF5, 0x0151 }, // latin small letter O with double acute
			{ 0xF6, 0x00F6 }, // latin small letter O with diaeresis
			{ 0xF7, 0x015B }, // latin small letter S with acute
			{ 0xF8, 0x0171 }, // latin small letter U with double acute
			{ 0xF9, 0x00F9 }, // latin small letter U with grave
			{ 0xFA, 0x00FA }, // latin small letter U with acute
			{ 0xFB, 0x00FB }, // latin small letter U with circumflex
			{ 0xFC, 0x00FC }, // latin small letter U with diaeresis
			{ 0xFD, 0x0119 }, // latin small letter E with ogonek
			{ 0xFE, 0x021B }, // latin small letter T with comma below
			{ 0xFF, 0x00FF }  // latin small letter Y with diaeresis
		}
	}
};


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
// Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

unsigned kernelCharsetToUnicode(const char *set, unsigned value)
{
	int setCount;

	// Check params
	if (!set)
	{
		kernelError(kernel_error, "Character set name is NULL");
		return (0);
	}

	if (value >= (CHARSET_IDENT_CODES + CHARSET_NUM_CODES))
	{
		kernelError(kernel_error, "Charset value %u is invalid", value);
		return (0);
	}

	if (value < CHARSET_IDENT_CODES)
		return (value);

	for (setCount = 0; setCount < (int)(sizeof(sets) / sizeof(charset));
		setCount ++)
	{
		if (!strcmp(set, sets[setCount].name))
			return (sets[setCount].codes[value - CHARSET_IDENT_CODES].unicode);
	}

	// Not found
	return (0);
}


unsigned kernelCharsetFromUnicode(const char *set, unsigned value)
{
	int setCount, codeCount;

	// Check params
	if (!set)
	{
		kernelError(kernel_error, "Character set name is NULL");
		return (0);
	}

	if (value < CHARSET_IDENT_CODES)
		return (value);

	for (setCount = 0; setCount < (int)(sizeof(sets) / sizeof(charset));
		setCount ++)
	{
		if (!strcmp(set, sets[setCount].name))
		{
			for (codeCount = 0; codeCount < CHARSET_NUM_CODES; codeCount ++)
			{
				if (sets[setCount].codes[codeCount].unicode == value)
					return (sets[setCount].codes[codeCount].code);
			}
		}
	}

	// Not found
	return (0);
}

