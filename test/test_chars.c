// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Numbers the library writes without printf (src/chars.c): held to the C
// library's own output, "%g" to six and nine digits over random doubles
// of every exponent, values of few digits and near halfway cases, and
// special values to each precision up to 15; integers and hex; and text
// cut where it does not fit.

#include "chars.h"
#include "test_harness.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint64_t Next(uint64_t* state)
{
    *state ^= *state << 13;
    *state ^= *state >> 7;
    *state ^= *state << 17;
    return *state;
}

// Whether muiPutGeneral writes a value as snprintf's "%.*g" does.
static bool SameGeneral(double value, uint32_t digits)
{
    char ours[64];
    char theirs[64];
    muiChars chars = muiCharsIn(ours, sizeof ours);
    muiPutGeneral(&chars, value, digits);
    (void)snprintf(theirs, sizeof theirs, "%.*g", (int)digits, value);
    if (strcmp(ours, theirs) != 0)
    {
        printf("%.17g to %u digits: %s, printf %s\n", value, digits, ours, theirs);
        return false;
    }
    return true;
}

static void TestGeneral(void)
{
    const double special[] = {0.0,
                              -0.0,
                              1.0,
                              10.0,
                              100000.0,
                              1000000.0,
                              123456,
                              1234567,
                              0.1,
                              0.0001,
                              0.00001,
                              1.5,
                              2.5,
                              123456.5,
                              -3.25,
                              99999.95,
                              999999.5,
                              1e-300,
                              1e300,
                              5e-324,
                              1.7976931348623157e308,
                              0.30000000000000004,
                              (double)INFINITY,
                              -(double)INFINITY,
                              (double)NAN};
    bool same = true;
    // Each precision up to 15 within 10^17 either way, where the scaling
    // is exact; beyond, the precisions the library writes, six and nine,
    // as more digits may differ in the last (src/chars.c).
    for (uint32_t digits = 1; digits <= 15; digits++)
    {
        for (size_t i = 0; i < sizeof special / sizeof special[0]; i++)
        {
            double size = fabs(special[i]);
            bool near = size == 0.0 || !isfinite(size) || (size > 1e-17 && size < 1e17);
            if (near || digits == 6 || digits == 9)
            {
                same = SameGeneral(special[i], digits) && same;
            }
        }
    }
    CHECK(same, "special values to every precision");
    uint64_t state = 88172645463325252u;
    same = true;
    for (uint32_t n = 0; n < 200000 && same; n++)
    {
        uint64_t bits = Next(&state);
        double value = 0.0;
        switch (n % 4)
        {
        case 0:
            memcpy(&value, &bits, sizeof value);
            if (!isfinite(value))
            {
                continue;
            }
            break;
        case 1:
            // Logical units in 64ths, as float.
            value = (double)(float)((double)(int64_t)(bits % 2000001u) - 1000000.0) / 64.0;
            break;
        case 2:
            // Thousandths, many lying near halfway at six digits.
            value = (double)((int64_t)(bits % 20000000001u) - 10000000000) / 1000.0;
            break;
        default:
            value = (double)(float)((double)(bits >> 11) * 0x1p-53 * 1e4);
            break;
        }
        same = SameGeneral(value, 6) && SameGeneral(value, 9);
    }
    CHECK(same, "random values as printf writes them");
}

static void TestIntegersAndText(void)
{
    char buffer[64];
    muiChars chars = muiCharsIn(buffer, sizeof buffer);
    muiPutText(&chars, "n");
    muiPutUnsigned(&chars, 0);
    muiPutText(&chars, " ");
    muiPutUnsigned(&chars, UINT64_MAX);
    muiPutText(&chars, " ");
    muiPutHex(&chars, 0xABCDEFu, 1, false);
    muiPutText(&chars, " ");
    muiPutHex(&chars, 7, 2, true);
    muiPutText(&chars, " ");
    muiPutHex(&chars, UINT64_MAX, 1, true);
    char expected[64];
    (void)snprintf(expected, sizeof expected, "n0 %" PRIu64 " %" PRIx64 " %02X %" PRIX64,
                   UINT64_MAX, (uint64_t)0xABCDEFu, 7u, UINT64_MAX);
    CHECK(strcmp(buffer, expected) == 0 && chars.length == strlen(expected),
          "integers and hex as printf writes them");
    // Cut where it does not fit, as snprintf cuts, NUL after.
    char small[6];
    chars = muiCharsIn(small, sizeof small);
    muiPutText(&chars, "abc");
    muiPutUnsigned(&chars, 12345);
    CHECK(strcmp(small, "abc12") == 0 && chars.length == 5, "cut at the buffer's end");
    muiPutText(&chars, "more");
    CHECK(strcmp(small, "abc12") == 0 && chars.length == 5, "nothing past it");
    chars = muiCharsIn(NULL, 0);
    muiPutText(&chars, "none");
    CHECK(chars.length == 0, "no room at all");
}

int main(void)
{
    TestGeneral();
    TestIntegersAndText();
    return s_failures == 0 ? 0 : 1;
}
