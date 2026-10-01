// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The library's own exponential and trigonometry against the C
// library's, the easing curves, and the spring's closed form against its
// differential equation.

#include "easing.h"
#include "motion_math.h"
#include "spring.h"
#include "test_harness.h"

#include <float.h>
#include <math.h>

static double RelativeError(double value, double reference)
{
    return fabs(value - reference) / fabs(reference);
}

static void TestExpIsAccurate(void)
{
    double worst = 0.0;
    for (int i = 0; i <= 140000; i++)
    {
        double x = -700.0 + (double)i * 0.01;
        double error = RelativeError(muiExp(x), exp(x));
        worst = error > worst ? error : worst;
    }
    CHECK(worst <= 4.0 * DBL_EPSILON, "within a few units in the last place");
    CHECK(muiExp(0.0) == 1.0, "e^0 is 1 exactly");
    CHECK(RelativeError(muiExp(-744.0), exp(-744.0)) < 1e-3, "into the subnormals");
    CHECK(RelativeError(muiExp(-720.0), exp(-720.0)) < 1e-9, "near them");
    CHECK(RelativeError(muiExp(709.0), exp(709.0)) <= 4.0 * DBL_EPSILON, "near the top");
    CHECK(RelativeError(muiExp(709.78), exp(709.78)) <= 4.0 * DBL_EPSILON,
          "up to the largest double");
    CHECK(muiExp(-746.0) == 0.0 && isinf(muiExp(709.79)), "past the ends");
    CHECK(isinf(muiExp(800.0)) && isinf(muiExp(1e6)), "far past the top");
    CHECK(isnan(muiExp((double)NAN)), "NaN stays NaN");
}

static void TestSinCosAreAccurate(void)
{
    double worst = 0.0;
    double worstUnit = 0.0;
    for (int i = 0; i <= 200000; i++)
    {
        double x = -2000.0 + (double)i * 0.02;
        double sine = 0.0;
        double cosine = 0.0;
        muiSinCos(x, &sine, &cosine);
        double error = fmax(fabs(sine - sin(x)), fabs(cosine - cos(x)));
        worst = error > worst ? error : worst;
        double unit = fabs(sine * sine + cosine * cosine - 1.0);
        worstUnit = unit > worstUnit ? unit : worstUnit;
    }
    CHECK(worst <= 1e-14, "within 1e-14 up to 2000");
    CHECK(worstUnit <= 4.0 * DBL_EPSILON, "on the unit circle");
    double sine = 1.0;
    double cosine = 0.0;
    muiSinCos(0.0, &sine, &cosine);
    CHECK(sine == 0.0 && cosine == 1.0, "exact at 0");
    // Each quadrant, and negative arguments.
    const double points[] = {0.5, 2.0, 3.5, 5.0, -0.5, -2.0, -3.5, -5.0};
    for (int i = 0; i < 8; i++)
    {
        muiSinCos(points[i], &sine, &cosine);
        CHECK(fabs(sine - sin(points[i])) < 1e-15 && fabs(cosine - cos(points[i])) < 1e-15,
              "every quadrant");
    }
}

// y at x by bisection to 1e-12, as a reference.
static double ReferenceEase(double x1, double y1, double x2, double y2, double x)
{
    double low = 0.0;
    double high = 1.0;
    double t = 0.5;
    for (int i = 0; i < 200; i++)
    {
        t = (low + high) * 0.5;
        double u = 1.0 - t;
        double sampled = 3.0 * u * u * t * x1 + 3.0 * u * t * t * x2 + t * t * t;
        if (fabs(sampled - x) < 1e-12)
        {
            break;
        }
        if (sampled < x)
        {
            low = t;
        }
        else
        {
            high = t;
        }
    }
    double u = 1.0 - t;
    return 3.0 * u * u * t * y1 + 3.0 * u * t * t * y2 + t * t * t;
}

static void TestCurvesMatchTheirDefinition(void)
{
    // ease, ease-in, ease-out, ease-in-out, and one that overshoots.
    const double curves[][4] = {
        {0.25, 0.1, 0.25, 1.0}, {0.42, 0.0, 1.0, 1.0},     {0.0, 0.0, 0.58, 1.0},
        {0.42, 0.0, 0.58, 1.0}, {0.68, -0.55, 0.27, 1.55}, {1.0, 0.0, 0.0, 1.0},
    };
    for (int c = 0; c < 6; c++)
    {
        const double* p = curves[c];
        muiCurve curve = muiMakeCurve(p[0], p[1], p[2], p[3]);
        double worst = 0.0;
        for (int i = 0; i <= 1000; i++)
        {
            double x = (double)i / 1000.0;
            double error = fabs(muiEase(&curve, x) - ReferenceEase(p[0], p[1], p[2], p[3], x));
            worst = error > worst ? error : worst;
        }
        // The last curve's x has a flat point at the middle, where t, and so
        // y, is known only to the cube root of a double's rounding.
        CHECK(worst < (c == 5 ? 1e-5 : 1e-9), "y on the curve");
        CHECK(muiEase(&curve, 0.0) == 0.0 && muiEase(&curve, 1.0) == 1.0, "fixed ends");
        CHECK(muiEase(&curve, -1.0) == 0.0 && muiEase(&curve, 2.0) == 1.0, "clamped outside");
    }
    muiCurve linear = muiMakeCurve(0.0, 0.0, 1.0, 1.0);
    muiCurve inOut = muiMakeCurve(0.42, 0.0, 0.58, 1.0);
    for (int i = 1; i < 100; i++)
    {
        double x = (double)i / 100.0;
        CHECK(fabs(muiEase(&linear, x) - x) < 1e-7, "the straight curve is the identity");
        CHECK(fabs(muiEase(&inOut, x) + muiEase(&inOut, 1.0 - x) - 1.0) < 1e-6,
              "ease-in-out is symmetric");
    }
}

static void TestSpringsFollowTheirEquation(void)
{
    const double ratios[] = {0.2, 0.7, 1.0, 1.5, 0.999999, 1.000001};
    for (int c = 0; c < 6; c++)
    {
        const double frequency = 2.0;
        const double w0 = 6.28318530717958647692 * frequency;
        muiSpring spring = muiMakeSpring(frequency, ratios[c], 40.0, -100.0);
        double offset = 0.0;
        double velocity = 0.0;
        muiSpringAt(&spring, 0.0, &offset, &velocity);
        CHECK(fabs(offset - 40.0) < 1e-9 && fabs(velocity + 100.0) < 1e-9, "starts as given");
        for (int i = 1; i < 200; i++)
        {
            double t = (double)i * 0.01;
            const double h = 1e-5;
            double before = 0.0;
            double after = 0.0;
            double v = 0.0;
            muiSpringAt(&spring, t - h, &before, &v);
            muiSpringAt(&spring, t + h, &after, &v);
            muiSpringAt(&spring, t, &offset, &velocity);
            double slope = (after - before) / (2.0 * h);
            double curvature = (after - 2.0 * offset + before) / (h * h);
            CHECK(fabs(slope - velocity) < 1e-4 * fmax(1.0, fabs(velocity)),
                  "the velocity is the offset's derivative");
            double residual = curvature + 2.0 * ratios[c] * w0 * velocity + w0 * w0 * offset;
            CHECK(fabs(residual) < 1e-2 * w0 * w0 * 40.0, "the equation of motion holds");
        }
        muiSpringAt(&spring, 10.0, &offset, &velocity);
        CHECK(fabs(offset) < 1e-6 && fabs(velocity) < 1e-4, "comes to rest");
    }
    // Only an underdamped spring released at rest crosses its target.
    muiSpring under = muiMakeSpring(1.0, 0.3, 1.0, 0.0);
    muiSpring critical = muiMakeSpring(1.0, 1.0, 1.0, 0.0);
    muiSpring over = muiMakeSpring(1.0, 2.0, 1.0, 0.0);
    bool crossed = false;
    bool others = false;
    for (int i = 1; i < 500; i++)
    {
        double offset = 0.0;
        double velocity = 0.0;
        muiSpringAt(&under, (double)i * 0.01, &offset, &velocity);
        crossed = crossed || offset < 0.0;
        muiSpringAt(&critical, (double)i * 0.01, &offset, &velocity);
        others = others || offset < 0.0;
        muiSpringAt(&over, (double)i * 0.01, &offset, &velocity);
        others = others || offset < 0.0;
    }
    CHECK(crossed && !others, "overshoot only when underdamped");
}

int main(void)
{
    TestExpIsAccurate();
    TestSinCosAreAccurate();
    TestCurvesMatchTheirDefinition();
    TestSpringsFollowTheirEquation();
    return s_failures == 0 ? 0 : 1;
}
