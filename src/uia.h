// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The UI Automation adapter (record mui-0008): the adapter, its provider
// objects, and the UI Automation functions it loads.

#ifndef MAUL_UI_SRC_UIA_H
#define MAUL_UI_SRC_UIA_H

#include "id_map.h"

#include "maul-ui/access_uia.h"

#define WIN32_LEAN_AND_MEAN
// Interfaces point at const tables of their functions.
#define CONST_VTABLE
#include <windows.h>
// COM's declarations, which lean Windows headers leave out, before UI
// Automation's.
#include "uia_ids.h"

#include <ole2.h>
#include <uiautomationcore.h>

// UI Automation's errors, as HRESULTs: UIA_E_ELEMENTNOTAVAILABLE and
// UIA_E_NOTSUPPORTED.
#define ELEMENT_GONE  ((HRESULT)0x80040201)
#define NOT_SUPPORTED ((HRESULT)0x80040204)

// The functions of uiautomationcore.dll the adapter calls, loaded when it
// is made, as MinGW has no import library of them.
typedef struct muiUiaFunctions
{
    HMODULE module;
    HRESULT(WINAPI* hostProviderFromHwnd)(HWND, IRawElementProviderSimple**);
    LRESULT(WINAPI* returnRawElementProvider)(HWND, WPARAM, LPARAM, IRawElementProviderSimple*);
    HRESULT(WINAPI* disconnectProvider)(IRawElementProviderSimple*);
} muiUiaFunctions;

// A provider object: one per node UI Automation asked for, and the root,
// which stands for whichever node is the tree's root. Once detached, it
// answers ELEMENT_GONE until its last reference goes.
typedef struct muiUiaNode
{
    IRawElementProviderSimple simple;
    IRawElementProviderFragment fragment;
    IRawElementProviderFragmentRoot fragmentRoot;
    LONG references;
    // NULL once detached.
    muiUiaAdapter* adapter;
    // The node's id; 0 for the root.
    uint64_t id;
} muiUiaNode;

struct muiUiaAdapter
{
    muiAllocator allocator;
    size_t blockSize;
    muiAccessTree* tree;
    HWND window;
    float scale;
    muiUiaActionFunction action;
    void* user;
    muiUiaFunctions uia;
    muiUiaNode* root;
    // Provider objects by node id, each holding a reference.
    muiIdMap objects;
    // Room for any node's shown children.
    uint64_t* scratch;
    uint32_t nodes;
};

// A new provider object with one reference, or NULL when the process
// heap is out of memory.
muiUiaNode* muiUiaMakeNode(muiUiaAdapter* adapter, uint64_t id);

// Lets go of a reference.
void muiUiaRelease(muiUiaNode* node);

// The provider object of a node held, made when first asked for; the
// root's for the tree's root. NULL for a node not held or no memory. No
// reference is added.
muiUiaNode* muiUiaNodeOf(muiUiaAdapter* adapter, uint64_t id);

// The node an object stands for, or NULL when detached or gone.
const muiAccessNode* muiUiaNodeFor(const muiUiaNode* node);

// A property's value for a node held.
HRESULT muiUiaPropertyValue(muiUiaAdapter* adapter, const muiAccessNode* node, PROPERTYID property,
                            VARIANT* out);

// A node's bounds in screen pixels.
struct UiaRect muiUiaScreenRect(const muiUiaAdapter* adapter, uint64_t id);

// The top left of the window's client area on the screen, in pixels.
POINT muiUiaClientOrigin(const muiUiaAdapter* adapter);

// Asks the host to perform an action on a node.
HRESULT muiUiaPerform(muiUiaAdapter* adapter, muiAccessAction action, uint64_t target);

#endif // MAUL_UI_SRC_UIA_H
