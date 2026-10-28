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
    CHECK(strcmp(muiResultName(mui_empty), "mui_empty") == 0, "empty name");
    CHECK(strcmp(muiResultName(mui_errorInvalid), "mui_errorInvalid") == 0, "invalid name");
    CHECK(strcmp(muiResultName(mui_errorCapacity), "mui_errorCapacity") == 0, "capacity name");
    CHECK(strcmp(muiResultName(mui_errorStale), "mui_errorStale") == 0, "stale name");
    CHECK(strcmp(muiResultName(mui_errorUnsupported), "mui_errorUnsupported") == 0,
          "unsupported name");
    CHECK(strcmp(muiResultName(mui_errorPlatform), "mui_errorPlatform") == 0, "platform name");
    CHECK(strcmp(muiResultName(mui_errorVersion), "mui_errorVersion") == 0, "version name");
    CHECK(strcmp(muiResultName(mui_errorFormat), "mui_errorFormat") == 0, "format name");
    CHECK(strcmp(muiResultName(mui_errorLimit), "mui_errorLimit") == 0, "limit name");
    CHECK(strcmp(muiResultName(mui_errorBusy), "mui_errorBusy") == 0, "busy name");
    CHECK(strcmp(muiResultName(-6), "unknown result") == 0 &&
              strcmp(muiResultName(12345), "unknown result") == 0,
          "unknown names");
}

// The values the Maul family gives the errors it shares, so a program
// linking several libraries reads a logged value one way.
static void TestResultValues(void)
{
    CHECK(mui_errorInvalid == -1 && mui_errorCapacity == -2 && mui_errorStale == -3 &&
              mui_errorUnsupported == -4 && mui_errorPlatform == -5 && mui_errorVersion == -7 &&
              mui_errorFormat == -8 && mui_errorLimit == -9 && mui_errorBusy == -10,
          "the family's values");
}

int main(void)
{
    TestVersionMatchesHeader();
    TestResultNames();
    TestResultValues();
    return s_failures == 0 ? 0 : 1;
}
