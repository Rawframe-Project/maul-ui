// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The public headers compile together with <windows.h>, whose winnls.h
// defines macros that start with MUI_: a clash is a redefinition, which
// warnings as errors refuse, and the version macros keep their meaning.

#include "test_harness.h"

#include "maul-ui/base.h"
#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/style.h"
#include "maul-ui/theme.h"
#include "maul-ui/token.h"
#include "maul-ui/transition.h"
#include "maul-ui/visual.h"

#include <windows.h>

static void TestMacrosSurviveWindowsHeaders(void)
{
    muiVersion version = muiGetVersion();
    CHECK(version.major == MUI_VERSION_MAJOR, "major version");
    CHECK(version.minor == MUI_VERSION_MINOR, "minor version");
    CHECK(version.patch == MUI_VERSION_PATCH, "patch version");
}

int main(void)
{
    TestMacrosSurviveWindowsHeaders();
    return s_failures == 0 ? 0 : 1;
}
