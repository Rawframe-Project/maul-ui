// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The exponential, sine and cosine that springs need, written here so
// that they give the same bits on every platform (record mui-0001): only
// arithmetic, floor and the exact scaling of a double by a power of two.

#ifndef MAUL_UI_SRC_MOTION_MATH_H
#define MAUL_UI_SRC_MOTION_MATH_H

// e to the x, within about two units in the last place. Underflows to 0
// below -745 and overflows to infinity above 709.
double muiExp(double x);

// The sine and cosine of x, within about two units in the last place for
// |x| up to 2^20; larger arguments lose accuracy. A spring's argument
// stays far below.
void muiSinCos(double x, double* sineOut, double* cosineOut);

#endif // MAUL_UI_SRC_MOTION_MATH_H
