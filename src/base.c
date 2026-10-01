// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The version and result names.

#include "maul-ui/base.h"

muiVersion muiGetVersion(void)
{
    return (muiVersion){MUI_VERSION_MAJOR, MUI_VERSION_MINOR, MUI_VERSION_PATCH};
}

const char* muiResultName(muiResult result)
{
    switch (result)
    {
    case mui_success:
        return "mui_success";
    case mui_errorInvalid:
        return "mui_errorInvalid";
    case mui_errorCapacity:
        return "mui_errorCapacity";
    case mui_errorStale:
        return "mui_errorStale";
    default:
        return "unknown result";
    }
}
