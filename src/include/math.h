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
//  math.h
//

// This is the Visopsys version of the standard header file math.h

#if !defined(_MATH_H)

#define M_E         2.7182818284590452354  // e
#define M_LOG2E     1.4426950408889634074  // log_2 e
#define M_LOG10E    0.43429448190325182765 // log_10 e
#define M_LN2       0.69314718055994530942 // log_e 2
#define M_LN10      2.30258509299404568402 // log_e 10
#define M_PI        3.14159265358979323846 // pi
#define M_PI_2      1.57079632679489661923 // pi/2
#define M_PI_4      0.78539816339744830962 // pi/4
#define M_1_PI      0.31830988618379067154 // 1/pi
#define M_2_PI      0.63661977236758134308 // 2/pi
#define M_2_SQRTPI  1.12837916709551257390 // 2/sqrt(pi)
#define M_SQRT2     1.41421356237309504880 // sqrt(2)
#define M_SQRT1_2   0.70710678118654752440 // 1/sqrt(2)

double ceil(double);
double cos(double);
float cosf(float);
double fabs(double);
float fabsf(float);
double floor(double);
float floorf(float);
double fmod(double, double);
double modf(double, double *);
double pow(double, double);
double sin(double);
float sinf(float);
double sqrt(double);
double tan(double);
float tanf(float);

#define _MATH_H
#endif

