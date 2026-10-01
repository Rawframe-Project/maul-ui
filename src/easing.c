// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// x(t) = ((ax t + bx) t + cx) t, and y likewise. x is monotonic since
// the control points' x lie in [0, 1], so x(t) = x has one solution: a
// guess from eleven samples, at most four Newton steps, then bisection,
// as Chromium's CubicBezier::SolveCurveX, with a bound on the bisection.

#include "easing.h"

#include <math.h>

enum
{
    SAMPLES = 11,
    NEWTON_STEPS = 4,
    // Halving [0, 1] 64 times leaves less than any double's spacing.
    BISECTION_STEPS = 64,
};

#define EPSILON 1e-7

muiCurve muiMakeCurve(double x1, double y1, double x2, double y2)
{
    muiCurve curve;
    curve.cx = 3.0 * x1;
    curve.bx = 3.0 * (x2 - x1) - curve.cx;
    curve.ax = 1.0 - curve.cx - curve.bx;
    curve.cy = 3.0 * y1;
    curve.by = 3.0 * (y2 - y1) - curve.cy;
    curve.ay = 1.0 - curve.cy - curve.by;
    return curve;
}

static double SampleX(const muiCurve* curve, double t)
{
    return ((curve->ax * t + curve->bx) * t + curve->cx) * t;
}

static double SampleY(const muiCurve* curve, double t)
{
    return ((curve->ay * t + curve->by) * t + curve->cy) * t;
}

static double SlopeX(const muiCurve* curve, double t)
{
    return (3.0 * curve->ax * t + 2.0 * curve->bx) * t + curve->cx;
}

// Bisection of [low, high] for x(t) = x.
static double Bisect(const muiCurve* curve, double x, double low, double high)
{
    double t = (low + high) * 0.5;
    for (int i = 0; i < BISECTION_STEPS; i++)
    {
        double sampled = SampleX(curve, t);
        if (fabs(sampled - x) < EPSILON)
        {
            break;
        }
        if (x > sampled)
        {
            low = t;
        }
        else
        {
            high = t;
        }
        t = (low + high) * 0.5;
    }
    return t;
}

// The t with x(t) = x.
static double SolveX(const muiCurve* curve, double x)
{
    // The interval between samples that holds x, and a linear guess in it.
    double low = 0.0;
    double high = 1.0;
    double t = x;
    double previous = 0.0;
    for (int i = 1; i < SAMPLES; i++)
    {
        double at = (double)i / (SAMPLES - 1);
        double sampled = SampleX(curve, at);
        if (x <= sampled)
        {
            low = (double)(i - 1) / (SAMPLES - 1);
            high = at;
            t = sampled > previous ? low + (high - low) * (x - previous) / (sampled - previous)
                                   : low;
            break;
        }
        previous = sampled;
    }
    for (int i = 0; i < NEWTON_STEPS; i++)
    {
        double error = SampleX(curve, t) - x;
        if (fabs(error) < EPSILON)
        {
            return t;
        }
        double slope = SlopeX(curve, t);
        if (fabs(slope) < EPSILON)
        {
            break;
        }
        t -= error / slope;
    }
    if (t >= low && t <= high && fabs(SampleX(curve, t) - x) < EPSILON)
    {
        return t;
    }
    return Bisect(curve, x, low, high);
}

double muiEase(const muiCurve* curve, double x)
{
    if (x <= 0.0)
    {
        return 0.0;
    }
    if (x >= 1.0)
    {
        return 1.0;
    }
    return SampleY(curve, SolveX(curve, x));
}
