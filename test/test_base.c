// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The version and result names.

#include "test_harness.h"

#include "maul-ui/base.h"

#include <string.h>

static void TestVersionMatchesHeader(void)
{
    muiVersion version = muiGetVersion();
    CHECK(version.major == MUI_VERSION_MAJOR, "major version");
    CHECK(version.minor == MUI_VERSION_MINOR, "minor version");
    CHECK(version.patch == MUI_VERSION_PATCH, "patch version");
}

static void TestResultNames(void)
{
    CHECK(strcmp(muiResultName(mui_success), "mui_success") == 0, "success name");
    CHECK(strcmp(muiResultName(mui_errorInvalid), "mui_errorInvalid") == 0, "invalid name");
    CHECK(strcmp(muiResultName(mui_errorCapacity), "mui_errorCapacity") == 0, "capacity name");
    CHECK(strcmp(muiResultName(mui_errorStale), "mui_errorStale") == 0, "stale name");
    CHECK(strcmp(muiResultName(mui_errorFormat), "mui_errorFormat") == 0, "format name");
    CHECK(strcmp(muiResultName(mui_errorPlatform), "mui_errorPlatform") == 0, "platform name");
    CHECK(strcmp(muiResultName(12345), "unknown result") == 0, "unknown name");
}

int main(void)
{
    TestVersionMatchesHeader();
    TestResultNames();
    return s_failures == 0 ? 0 : 1;
}
