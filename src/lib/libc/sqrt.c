//
//  This is the standard "sqrt" function, as found in standard C libraries.
//
//  Copyright (C) Paul Hsieh, 1996-2004.
//
//  Paul's square root page is here:
//    http://www.azillionmonkeys.com/qed/sqroot.html
//
//  sqrt.c
//

#include <math.h>

#define itable ((double *) xtable)

static int xtable[16] = {
  0x540bcb0d, 0x3fe56936, 0x415a86d3, 0x3fe35800, 0xd9ac3519, 0x3fe1c80d,
  0x34f91569, 0x3fe08be9, 0x8f3386d8, 0x3fee4794, 0x9ea02719, 0x3feb5b28,
  0xe4ff9edc, 0x3fe92589, 0x1c52539d, 0x3fe76672
};


static int norm2(double *t)
{
	unsigned e, f, g;

	f = ((((unsigned *) t)[1]) >> 1);
	e = ((unsigned *) t)[1];
	f += 0x1FF80000;
	g = (e & 0x000FFFFF);
	f &= 0xFFF00000;
	((int *) t)[1] =  g + 0x40000000 - (e & 0x00100000);

	return (f);
}


double sqrt(double y)
{
	double a;
	int e, c;

	e = norm2(&y);
	c = (((int *) &y)[1]) >> (18) & (7);
	a = itable[c];

	for (c = 0; c < 6; c++)
		a = 0.5 * a * (3.0 - y * a * a);
	a *= y;

	((int *) &a)[1] &= 0x000FFFFF;
	((int *) &a)[1] |= e;

	return (a);
}

