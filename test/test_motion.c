// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The library's own exponential, trigonometry, logarithm, power and cube
// root against the C library's, and colors through Oklab. The easing
// curves and springs are Maul Tween's, tested there.

#include "color.h"
#include "motion_math.h"
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

// The angle of a point, around the circle at radii from 1e-6 to 1e9; the
// axes exact, and the origin 0, as a sweep gradient takes its centre.
static void TestAtan2IsAccurate(void)
{
    const double pi = 3.14159265358979323846;
    double worst = 0.0;
    for (int i = 0; i <= 2000; i++)
    {
        double angle = -pi + 2.0 * pi * (double)i / 2000.0;
        for (int m = 0; m < 6; m++)
        {
            double radius = pow(10.0, (double)(m * 3 - 6));
            double y = radius * sin(angle);
            double x = radius * cos(angle);
            worst = fmax(worst, fabs(muiAtan2(y, x) - atan2(y, x)));
        }
    }
    CHECK(worst <= 1e-15, "within 1e-15 around the circle");
    CHECK(muiAtan2(0.0, 1.0) == 0.0 && muiAtan2(1.0, 0.0) == atan2(1.0, 0.0) &&
              muiAtan2(0.0, -1.0) == atan2(0.0, -1.0) && muiAtan2(-1.0, 0.0) == atan2(-1.0, 0.0),
          "the axes");
    CHECK(muiAtan2(0.0, 0.0) == 0.0, "the origin");
}

// How far muiCbrt is from the cube root in long double, in units in the
// last place: the C library's cbrt is itself off by more than one.
static double UlpsFromCbrt(double x)
{
    long double root = cbrtl((long double)x);
    double magnitude = fabs((double)root);
    double ulp = nextafter(magnitude, (double)INFINITY) - magnitude;
    return (double)(fabsl((long double)muiCbrt(x) - root) / (long double)ulp);
}

static void TestLogPowCbrtAreAccurate(void)
{
    double worstLog = 0.0;
    double worstCbrt = 0.0;
    for (int i = -400; i <= 400; i++)
    {
        double x = pow(1.37, (double)i) * (1.0 + 1e-3 * (double)(i % 7));
        if (fabs(log(x)) > 1e-3)
        {
            worstLog = fmax(worstLog, RelativeError(muiLog(x), log(x)));
        }
        worstCbrt = fmax(worstCbrt, UlpsFromCbrt(x));
        worstCbrt = fmax(worstCbrt, UlpsFromCbrt(-x));
    }
    CHECK(worstLog < 4.5e-16, "ln within two units in the last place");
    // Where long double is double, the reference is the C library's cbrt.
    const double allowed = LDBL_MANT_DIG > DBL_MANT_DIG ? 1.0 : 3.5;
    CHECK(worstCbrt < allowed, "cube root within a unit in the last place");
    CHECK(muiLog(1.0) == 0.0 && RelativeError(muiLog(1.0 + 1e-12), log(1.0 + 1e-12)) < 2.3e-16,
          "ln near 1");
    CHECK(RelativeError(muiLog(2.0), 0.69314718055994530942) < 2.3e-16, "ln 2");
    CHECK(RelativeError(muiLog(5e-324), log(5e-324)) < 2.3e-16, "a subnormal");
    CHECK(RelativeError(muiLog(DBL_MAX), log(DBL_MAX)) < 2.3e-16, "the largest");
    CHECK(muiLog(0.0) == -(double)INFINITY && isnan(muiLog(-1.0)) && isnan(muiLog((double)NAN)) &&
              muiLog((double)INFINITY) == (double)INFINITY,
          "the edges");
    CHECK(muiCbrt(0.0) == 0.0 && muiCbrt(-27.0) == -3.0 && muiCbrt(8.0) == 2.0,
          "exact cubes are exact");
    double worstPow = 0.0;
    // Over the ranges the sRGB transfer function raises.
    for (int i = 0; i <= 1000; i++)
    {
        double toLinear = 0.0899 + 0.9101 * (double)i / 1000.0;
        double fromLinear = 0.0031308 + 0.9968692 * (double)i / 1000.0;
        worstPow = fmax(worstPow, RelativeError(muiPow(toLinear, 2.4), pow(toLinear, 2.4)));
        worstPow = fmax(worstPow,
                        RelativeError(muiPow(fromLinear, 1.0 / 2.4), pow(fromLinear, 1.0 / 2.4)));
    }
    CHECK(worstPow < 2e-15, "the sRGB powers");
}

static bool Near(float a, double b, double tolerance)
{
    return fabs((double)a - b) <= tolerance;
}

static void TestColorsThroughOklab(void)
{
    float channels[4];
    muiColorToChannels((muiColor){1.0f, 0.0f, 0.0f, 1.0f}, channels);
    // Ottosson's Oklab of sRGB red.
    CHECK(Near(channels[0], 0.6279554, 1e-6) && Near(channels[1], 0.2248631, 1e-6) &&
              Near(channels[2], 0.1258463, 1e-6) && channels[3] == 1.0f,
          "red");
    muiColorToChannels((muiColor){1.0f, 1.0f, 1.0f, 1.0f}, channels);
    CHECK(Near(channels[0], 1.0, 1e-7) && Near(channels[1], 0.0, 1e-7) &&
              Near(channels[2], 0.0, 1e-7),
          "white has no hue");
    muiColorToChannels((muiColor){0.2f, 0.4f, 0.6f, 0.5f}, channels);
    float opaque[4];
    muiColorToChannels((muiColor){0.2f, 0.4f, 0.6f, 1.0f}, opaque);
    CHECK(channels[0] == opaque[0] * 0.5f && channels[1] == opaque[1] * 0.5f && channels[3] == 0.5f,
          "premultiplied by alpha");

    double worst = 0.0;
    for (int r = 0; r <= 16; r++)
    {
        for (int g = 0; g <= 16; g++)
        {
            for (int b = 0; b <= 16; b++)
            {
                const muiColor color = {(float)r / 16.0f, (float)g / 16.0f, (float)b / 16.0f,
                                        (r + g) % 2 == 0 ? 1.0f : 0.25f};
                muiColorToChannels(color, channels);
                muiColor back = muiColorFromChannels(channels);
                worst = fmax(worst, fabs((double)back.r - (double)color.r));
                worst = fmax(worst, fabs((double)back.g - (double)color.g));
                worst = fmax(worst, fabs((double)back.b - (double)color.b));
                CHECK(back.a == color.a, "alpha is carried as is");
            }
        }
    }
    CHECK(worst < 2e-5, "there and back within a 50,000th");

    // A dark gray takes the transfer function's linear part both ways.
    muiColorToChannels((muiColor){0.02f, 0.02f, 0.02f, 1.0f}, channels);
    CHECK(Near(channels[0], 0.1156793, 1e-6), "a dark gray's lightness");
    muiColor dark = muiColorFromChannels(channels);
    CHECK(Near(dark.r, 0.02, 1e-6) && Near(dark.b, 0.02, 1e-6), "and back");
    muiColorToChannels((muiColor){0.002f, 0.0f, 0.0f, 1.0f}, channels);
    CHECK(Near(muiColorFromChannels(channels).r, 0.002, 1e-6), "darker still");

    const float clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    muiColor none = muiColorFromChannels(clear);
    CHECK(none.r == 0.0f && none.g == 0.0f && none.b == 0.0f && none.a == 0.0f, "clear");
    const float bright[4] = {1.5f, 0.3f, -0.2f, 1.2f};
    muiColor held = muiColorFromChannels(bright);
    CHECK(held.r >= 0.0f && held.r <= 1.0f && held.g >= 0.0f && held.g <= 1.0f && held.b >= 0.0f &&
              held.b <= 1.0f && held.a == 1.0f,
          "past the gamut, held to it");
}

int main(void)
{
    TestExpIsAccurate();
    TestSinCosAreAccurate();
    TestAtan2IsAccurate();
    TestLogPowCbrtAreAccurate();
    TestColorsThroughOklab();
    return s_failures == 0 ? 0 : 1;
}
