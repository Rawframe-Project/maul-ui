// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The UI Automation interfaces the adapter implements (record mui-0008),
// declared here rather than taken from uiautomationcore.h: the Windows
// SDK's UI Automation headers define const variables, which in C every
// file including them defines again, so a program including them too
// could not link. Their names are the adapter's own; their layouts and
// values are UI Automation's, which test_uia.c checks against the
// headers at compile time.

#ifndef MAUL_UI_SRC_UIA_COM_H
#define MAUL_UI_SRC_UIA_COM_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// IUnknown, VARIANT, BSTR and SAFEARRAY.
#include <ole2.h>

// ProviderOptions.
enum
{
    UIA_OPTION_SERVER_SIDE = 0x2,
    UIA_OPTION_COM_THREADING = 0x20,
};

// NavigateDirection.
enum
{
    NAVIGATE_PARENT = 0,
    NAVIGATE_NEXT = 1,
    NAVIGATE_PREVIOUS = 2,
    NAVIGATE_FIRST = 3,
    NAVIGATE_LAST = 4,
};

// UiaRect: a rectangle on the screen, in pixels.
typedef struct muiUiaRect
{
    double left;
    double top;
    double width;
    double height;
} muiUiaRect;

typedef struct muiUiaSimple muiUiaSimple;
typedef struct muiUiaFragment muiUiaFragment;
typedef struct muiUiaFragmentRoot muiUiaFragmentRoot;

// IRawElementProviderSimple.
typedef struct muiUiaSimpleTable
{
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(muiUiaSimple* self, REFIID id, void** out);
    ULONG(STDMETHODCALLTYPE* AddRef)(muiUiaSimple* self);
    ULONG(STDMETHODCALLTYPE* Release)(muiUiaSimple* self);
    HRESULT(STDMETHODCALLTYPE* get_ProviderOptions)(muiUiaSimple* self, int* out);
    HRESULT(STDMETHODCALLTYPE* GetPatternProvider)(muiUiaSimple* self, int pattern, IUnknown** out);
    HRESULT(STDMETHODCALLTYPE* GetPropertyValue)(muiUiaSimple* self, int property, VARIANT* out);
    HRESULT(STDMETHODCALLTYPE* get_HostRawElementProvider)(muiUiaSimple* self, muiUiaSimple** out);
} muiUiaSimpleTable;

struct muiUiaSimple
{
    const muiUiaSimpleTable* lpVtbl;
};

// IRawElementProviderFragment.
typedef struct muiUiaFragmentTable
{
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(muiUiaFragment* self, REFIID id, void** out);
    ULONG(STDMETHODCALLTYPE* AddRef)(muiUiaFragment* self);
    ULONG(STDMETHODCALLTYPE* Release)(muiUiaFragment* self);
    HRESULT(STDMETHODCALLTYPE* Navigate)(muiUiaFragment* self, int direction, muiUiaFragment** out);
    HRESULT(STDMETHODCALLTYPE* GetRuntimeId)(muiUiaFragment* self, SAFEARRAY** out);
    HRESULT(STDMETHODCALLTYPE* get_BoundingRectangle)(muiUiaFragment* self, muiUiaRect* out);
    HRESULT(STDMETHODCALLTYPE* GetEmbeddedFragmentRoots)(muiUiaFragment* self, SAFEARRAY** out);
    HRESULT(STDMETHODCALLTYPE* SetFocus)(muiUiaFragment* self);
    HRESULT(STDMETHODCALLTYPE* get_FragmentRoot)(muiUiaFragment* self, muiUiaFragmentRoot** out);
} muiUiaFragmentTable;

struct muiUiaFragment
{
    const muiUiaFragmentTable* lpVtbl;
};

// IRawElementProviderFragmentRoot.
typedef struct muiUiaFragmentRootTable
{
    HRESULT(STDMETHODCALLTYPE* QueryInterface)(muiUiaFragmentRoot* self, REFIID id, void** out);
    ULONG(STDMETHODCALLTYPE* AddRef)(muiUiaFragmentRoot* self);
    ULONG(STDMETHODCALLTYPE* Release)(muiUiaFragmentRoot* self);
    HRESULT(STDMETHODCALLTYPE* ElementProviderFromPoint)(muiUiaFragmentRoot* self, double x,
                                                         double y, muiUiaFragment** out);
    HRESULT(STDMETHODCALLTYPE* GetFocus)(muiUiaFragmentRoot* self, muiUiaFragment** out);
} muiUiaFragmentRootTable;

struct muiUiaFragmentRoot
{
    const muiUiaFragmentRootTable* lpVtbl;
};

#endif // MAUL_UI_SRC_UIA_COM_H
