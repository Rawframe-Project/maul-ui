// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// x'' + 2 z w0 x' + w0^2 x = 0 with w0 = 2 pi f. The three cases of the
// damping ratio z have the solutions below; their constants come from
// x(0) = offset and x'(0) = velocity.

#include "spring.h"

#include "motion_math.h"

#include <math.h>

#define TWO_PI 6.28318530717958647692

enum
{
    kindOverdamped = 0,
    kindCritical = 1,
    kindUnderdamped = 2,
};

muiSpring muiMakeSpring(double frequency, double dampingRatio, double offset, double velocity)
{
    double w0 = TWO_PI * frequency;
    muiSpring spring = {0};
    if (dampingRatio < 1.0)
    {
        spring.kind = kindUnderdamped;
        spring.r1 = -dampingRatio * w0;
        spring.w = w0 * sqrt(1.0 - dampingRatio * dampingRatio);
        spring.c1 = offset;
        spring.c2 = (velocity - spring.r1 * offset) / spring.w;
    }
    else if (dampingRatio == 1.0)
    {
        spring.kind = kindCritical;
        spring.r1 = -w0;
        spring.c1 = offset;
        spring.c2 = velocity + w0 * offset;
    }
    else
    {
        double root = sqrt(dampingRatio * dampingRatio - 1.0);
        spring.kind = kindOverdamped;
        spring.r1 = -w0 * (dampingRatio - root);
        spring.r2 = -w0 * (dampingRatio + root);
        spring.c2 = (velocity - spring.r1 * offset) / (spring.r2 - spring.r1);
        spring.c1 = offset - spring.c2;
    }
    return spring;
}

void muiSpringAt(const muiSpring* spring, double seconds, double* offsetOut, double* velocityOut)
{
    double decay = muiExp(spring->r1 * seconds);
    switch (spring->kind)
    {
    case kindUnderdamped:
    {
        double sine = 0.0;
        double cosine = 0.0;
        muiSinCos(spring->w * seconds, &sine, &cosine);
        double wave = spring->c1 * cosine + spring->c2 * sine;
        *offsetOut = decay * wave;
        *velocityOut =
            decay * (spring->r1 * wave + spring->w * (spring->c2 * cosine - spring->c1 * sine));
        break;
    }
    case kindCritical:
        *offsetOut = decay * (spring->c1 + spring->c2 * seconds);
        *velocityOut = decay * (spring->r1 * (spring->c1 + spring->c2 * seconds) + spring->c2);
        break;
    default:
    {
        double second = muiExp(spring->r2 * seconds);
        *offsetOut = spring->c1 * decay + spring->c2 * second;
        *velocityOut = spring->c1 * spring->r1 * decay + spring->c2 * spring->r2 * second;
        break;
    }
    }
}
