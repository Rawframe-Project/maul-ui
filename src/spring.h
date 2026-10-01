// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A damped spring pulling a value to its target, solved in closed form
// from a start offset and velocity, as Flutter's SpringSimulation, with
// the library's own exponential and trigonometry.

#ifndef MAUL_UI_SRC_SPRING_H
#define MAUL_UI_SRC_SPRING_H

// The spring's motion from one start: its offset from the target at time
// t is c1 e^(r1 t) + c2 e^(r2 t) for an overdamped spring,
// e^(r1 t) (c1 + c2 t) for a critical one, and
// e^(r1 t) (c1 cos(w t) + c2 sin(w t)) for an underdamped one.
typedef struct muiSpring
{
    double r1;
    double r2;
    double w;
    double c1;
    double c2;
    // 0 overdamped, 1 critical, 2 underdamped.
    int kind;
} muiSpring;

// A spring of frequency hertz and damping ratio above 0 that starts
// offset from its target with velocity, both in value units (per second).
muiSpring muiMakeSpring(double frequency, double dampingRatio, double offset, double velocity);

// The offset from the target and the velocity at seconds after the start.
void muiSpringAt(const muiSpring* spring, double seconds, double* offsetOut, double* velocityOut);

#endif // MAUL_UI_SRC_SPRING_H
