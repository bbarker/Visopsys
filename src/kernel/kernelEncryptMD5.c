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
//  kernelEncryptMD5.c
//

// This file contains an implementation of the MD5 one-way hashing algorithm,
// useful for passwords and whatnot.
// Ref: RFC 1321  http://www.freesoft.org/CIE/RFC/1321/

#include "kernelEncrypt.h"
#include "kernelMalloc.h"
#include <string.h>


static unsigned T[64] = {
	0xD76AA478, /* 1 */   0xE8C7B756, /* 2 */
	0x242070DB, /* 3 */   0xC1BDCEEE, /* 4 */
	0xF57C0FAF, /* 5 */   0x4787C62A, /* 6 */
	0xA8304613, /* 7 */   0xFD469501, /* 8 */
	0x698098D8, /* 9 */   0x8B44F7AF, /* 10 */
	0xFFFF5BB1, /* 11 */  0x895CD7BE, /* 12 */
	0x6B901122, /* 13 */  0xFD987193, /* 14 */
	0xA679438E, /* 15 */  0x49B40821, /* 16 */

	0xF61E2562, /* 17 */  0xC040B340, /* 18 */
	0x265E5A51, /* 19 */  0xE9B6C7AA, /* 20 */
	0xD62F105D, /* 21 */  0x02441453, /* 22 */
	0xD8A1E681, /* 23 */  0xE7D3FBC8, /* 24 */
	0x21E1CDE6, /* 25 */  0xC33707D6, /* 26 */
	0xF4D50D87, /* 27 */  0x455A14ED, /* 28 */
	0xA9E3E905, /* 29 */  0xFCEFA3F8, /* 30 */
	0x676F02D9, /* 31 */  0x8D2A4C8A, /* 32 */

	0xFFFA3942, /* 33 */  0x8771F681, /* 34 */
	0x6D9D6122, /* 35 */  0xFDE5380C, /* 36 */
	0xA4BEEA44, /* 37 */  0x4BDECFA9, /* 38 */
	0xF6BB4B60, /* 39 */  0xBEBFBC70, /* 40 */
	0x289B7EC6, /* 41 */  0xEAA127FA, /* 42 */
	0xD4EF3085, /* 43 */  0x04881D05, /* 44 */
	0xD9D4D039, /* 45 */  0xE6DB99E5, /* 46 */
	0x1FA27CF8, /* 47 */  0xC4AC5665, /* 48 */

	0xF4292244, /* 49 */  0x432AFF97, /* 50 */
	0xAB9423A7, /* 51 */  0xFC93A039, /* 52 */
	0x655B59C3, /* 53 */  0x8F0CCC92, /* 54 */
	0xFFEFF47D, /* 55 */  0x85845DD1, /* 56 */
	0x6FA87E4F, /* 57 */  0xFE2CE6E0, /* 58 */
	0xA3014314, /* 59 */  0x4E0811A1, /* 60 */
	0xF7537E82, /* 61 */  0xBD3AF235, /* 62 */
	0x2AD7D2BB, /* 63 */  0xEB86D391, /* 64 */
};

#define F(X, Y, Z) (((X) & (Y)) | ((~X) & (Z)))
#define G(X, Y, Z) (((X) & (Z)) | ((Y) & (~Z)))
#define H(X, Y, Z) ((X) ^ (Y) ^ (Z))
#define I(X, Y, Z) ((Y) ^ ((X) | (~Z)))


static inline unsigned rol(unsigned x, unsigned n)
{
	return ((x << n) | (x >> (32 - n)));
}


/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
//
//  Below here, the functions are exported for external use
//
/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////

int kernelEncryptMD5(const char *input, char *output)
{
	unsigned bits = 0;
	unsigned padBits = 0;
	unsigned bytes = 0;
	unsigned blocks = 0;
	void *buff = NULL;
	char *cBuff = NULL;
	unsigned *uBuff = NULL;
	unsigned A = 0x67452301;
	unsigned B = 0xEFCDAB89;
	unsigned C = 0x98BADCFE;
	unsigned D = 0x10325476;
	unsigned AA, BB, CC, DD;
	unsigned blockCount, bufcnt, count;

	// Calculate the number of bits and padding bits
	bits = (strlen(input) * 8);
	if ((bits % 512) >= 448)
		padBits = (960 - (bits % 512));
	else
		padBits = (448 - (bits % 512));

	// Calculate the length needed for the padding and the length quad-word.
	bytes = ((bits + padBits + 64) / 8);
	blocks = (bytes / 64);

	// Get a work area
	buff = kernelMalloc(bytes);
	if (!buff)
		return (-1);

	cBuff = buff;
	uBuff = (unsigned *) buff;

	// Copy the input, and add the padding bits
	strcpy(cBuff, input);
	memset((cBuff + strlen(input)), 0, (bytes - strlen(input)));
	cBuff[strlen(input)] = 0x80;

	// Append 64-bit length value.  Really only 32-bits in a 64-bit field.
	uBuff[(bytes / 4) - 2] = bits;

	for (blockCount = 0; blockCount < blocks; blockCount ++)
	{
		cBuff += (blockCount * 64);
		uBuff = (unsigned *) cBuff;

		AA = A;
		BB = B;
		CC = C;
		DD = D;

		// Don't spend too much time trying to analyze this bit.  It's a
		// somewhat optimized version of the already-convoluted algorithm
		// described in the RFC.  The RFC's description is a lot more readable.

		// Round 1
		for (count = 0; count < 16; count += 4)
		{
			A = B + rol((A + F(B, C, D) + uBuff[count] + T[count]), 7);
			D = A + rol((D + F(A, B, C) + uBuff[count + 1] + T[count + 1]), 12);
			C = D + rol((C + F(D, A, B) + uBuff[count + 2] + T[count + 2]), 17);
			B = C + rol((B + F(C, D, A) + uBuff[count + 3] + T[count + 3]), 22);
		}

		// Round 2
		for (bufcnt = 1; count < 32; bufcnt = ((bufcnt + 20) % 16))
		{
			A = B + rol((A + G(B, C, D) + uBuff[bufcnt] + T[count++]), 5);
			D = A + rol((D + G(A, B, C) + uBuff[(bufcnt + 5) % 16] +
				T[count++]), 9);
			C = D + rol((C + G(D, A, B) + uBuff[(bufcnt + 10) % 16] +
				T[count++]), 14);
			B = C + rol((B + G(C, D, A) + uBuff[(bufcnt + 15) % 16] +
				T[count++]), 20);
		}

		// Round 3
		for (bufcnt = 5; count < 48; bufcnt = ((bufcnt + 12) % 16))
		{
			A = B + rol((A + H(B, C, D) + uBuff[bufcnt] + T[count++]), 4);
			D = A + rol((D + H(A, B, C) + uBuff[(bufcnt + 3) % 16] +
				T[count++]), 11);
			C = D + rol((C + H(D, A, B) + uBuff[(bufcnt + 6) % 16] +
				T[count++]), 16);
			B = C + rol((B + H(C, D, A) + uBuff[(bufcnt + 9) % 16] +
				T[count++]), 23);
		}

		// Round 4
		for (bufcnt = 0; count < 64; bufcnt = ((bufcnt + 28) % 16))
		{
			A = B + rol((A + I(B, C, D) + uBuff[bufcnt] + T[count++]), 6);
			D = A + rol((D + I(A, B, C) + uBuff[(bufcnt + 7) % 16] +
				T[count++]), 10);
			C = D + rol((C + I(D, A, B) + uBuff[(bufcnt + 14) % 16] +
				T[count++]), 15);
			B = C + rol((B + I(C, D, A) + uBuff[(bufcnt + 21) % 16] +
				T[count++]), 21);
		}

		uBuff[0] = A = (A + AA);
		uBuff[1] = B = (B + BB);
		uBuff[2] = C = (C + CC);
		uBuff[3] = D = (D + DD);

		memcpy((output + (blockCount * 16)), cBuff, 16);
		output[(blockCount * 16) + 17] = '\0';
	}

	kernelFree(buff);
	return (blocks * 16);
}

