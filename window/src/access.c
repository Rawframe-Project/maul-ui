// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Accessibility through the Maul Window glue (record mui-0008). The
// adapters this build has are named by MUI_WINDOW_UIA, MUI_WINDOW_NS and
// MUI_WINDOW_ATSPI, as Maul UI was built with them; a window takes the
// one for its platform, or keeps a tree alone. NSAccessibility's root
// is the tree root's object and changes with it, so after each update
// the root is asked for again and handed over when it changed.

#include "maul-ui-window/access.h"

#include "allocator.h"
#include "glue_state.h"

#include "maul-window/accessibility.h"
#include "maul-window/native.h"

#if MUI_WINDOW_UIA
#include "maul-ui/access_uia.h"
#endif
#if MUI_WINDOW_NS
#include "maul-ui/access_ns.h"
#endif
#if MUI_WINDOW_ATSPI
#include "maul-ui/access_atspi.h"
#endif

#include <math.h>
#include <stdalign.h>

#define DEF_COOKIE 0x6D757761u // "muwa"

// The adapter an access made.
typedef enum Kind
{
    kind_tree,
    kind_uia,
    kind_ns,
    kind_atspi,
} Kind;

struct muiWindowAccess
{
    muiAllocator allocator;
    muiWindowGlue* glue;
    Kind kind;
    // The adapter, of its kind, or the tree alone.
    void* adapter;
    // The root handed to the window, NULL before any.
    void* root;
    // Whether the window is X11's, whose place AT-SPI is told.
    bool placed;
    muiWindowAccessActionFunction action;
    void* user;
};

muiWindowAccessDef muiDefaultWindowAccessDef(void)
{
    return (muiWindowAccessDef){
        .cookie = DEF_COOKIE,
        .nodes = 4096,
    };
}

// What a client asks, done by the host's function or the context.
static bool Act(void* user, const muiAccessRequest* request)
{
    const muiWindowAccess* access = user;
    if (access->action != nullptr)
    {
        return access->action(access->user, request);
    }
    bool handled = false;
    return muiPerformAccessAction(access->glue->context, request, &handled) == mui_success &&
           handled;
}

// The window's scale and state, which a window with no surface lacks.
static float ScaleOf(const muiWindowGlue* glue)
{
    mwinWindowState state;
    return mwinGetWindowState(glue->windows, glue->window, &state) == mwin_success &&
                   state.scale > 0.0f
               ? state.scale
               : 1.0f;
}

#if MUI_WINDOW_ATSPI
// Tells AT-SPI where an X11 window's client area is, in pixels.
static void Place(const muiWindowAccess* access)
{
    mwinWindowState state;
    const muiWindowGlue* glue = access->glue;
    if (access->kind == kind_atspi && access->placed &&
        mwinGetWindowState(glue->windows, glue->window, &state) == mwin_success)
    {
        muiAtspiAdapter_SetPlace(access->adapter, (int32_t)lroundf(state.position.x * state.scale),
                                 (int32_t)lroundf(state.position.y * state.scale));
    }
}
#endif

// Makes the adapter for a platform, where this build has it.
static muiResult MakeAdapter(muiWindowAccess* access, const muiWindowAccessDef* def,
                             const mwinNativeHandles* handles)
{
    float scale = ScaleOf(access->glue);
    (void)scale;
    switch (handles->platform)
    {
#if MUI_WINDOW_UIA
    case mwin_platformWin32:
    {
        muiUiaAdapterDef uia = muiDefaultUiaAdapterDef();
        uia.allocator = def->allocator;
        uia.nodes = def->nodes;
        uia.window = handles->handles.win32.hwnd;
        uia.scale = scale;
        uia.action = Act;
        uia.user = access;
        access->kind = kind_uia;
        return muiCreateUiaAdapter(&uia, (muiUiaAdapter**)&access->adapter);
    }
#endif
#if MUI_WINDOW_NS
    case mwin_platformMacOS:
    {
        // A view's points are Maul Window's logical units.
        muiNsAdapterDef ns = muiDefaultNsAdapterDef();
        ns.allocator = def->allocator;
        ns.nodes = def->nodes;
        ns.view = handles->handles.apple.view;
        ns.action = Act;
        ns.user = access;
        access->kind = kind_ns;
        return muiCreateNsAdapter(&ns, (muiNsAdapter**)&access->adapter);
    }
#endif
#if MUI_WINDOW_ATSPI
    case mwin_platformX11:
    case mwin_platformWayland:
    {
        if (def->atspiApp == nullptr)
        {
            break;
        }
        muiAtspiAdapterDef atspi = muiDefaultAtspiAdapterDef();
        atspi.nodes = def->nodes;
        atspi.scale = scale;
        atspi.action = Act;
        atspi.user = access;
        access->kind = kind_atspi;
        // Wayland does not say where a window is: AT-SPI's screen is
        // then the window's own.
        access->placed = handles->platform == mwin_platformX11;
        return muiCreateAtspiAdapter(def->atspiApp, &atspi, (muiAtspiAdapter**)&access->adapter);
    }
#endif
    default:
        break;
    }
    muiAccessTreeDef tree = muiDefaultAccessTreeDef();
    tree.allocator = def->allocator;
    tree.nodes = def->nodes;
    access->kind = kind_tree;
    return muiCreateAccessTree(&tree, (muiAccessTree**)&access->adapter);
}

static void DestroyAdapter(muiWindowAccess* access)
{
    switch (access->kind)
    {
#if MUI_WINDOW_UIA
    case kind_uia:
        muiDestroyUiaAdapter(access->adapter);
        break;
#endif
#if MUI_WINDOW_NS
    case kind_ns:
        muiDestroyNsAdapter(access->adapter);
        break;
#endif
#if MUI_WINDOW_ATSPI
    case kind_atspi:
        muiDestroyAtspiAdapter(access->adapter);
        break;
#endif
    default:
        muiDestroyAccessTree(access->adapter);
        break;
    }
    access->adapter = nullptr;
}

muiResult muiCreateWindowAccess(const muiWindowAccessDef* def, muiWindowAccess** accessOut)
{
    if (accessOut != nullptr)
    {
        *accessOut = nullptr;
    }
    if (def == nullptr || accessOut == nullptr || def->cookie != DEF_COOKIE ||
        !muiWindowIsAllocatorValid(&def->allocator) || def->glue == nullptr || def->nodes == 0)
    {
        return mui_errorInvalid;
    }
    const muiWindowGlue* glue = def->glue;
    mwinNativeHandles handles;
    if (mwinGetNativeHandles(glue->windows, glue->window, &handles) != mwin_success)
    {
        return mui_errorInvalid;
    }
    muiWindowAccess* access =
        muiWindowAllocate(&def->allocator, sizeof(muiWindowAccess), alignof(muiWindowAccess));
    if (access == nullptr)
    {
        return mui_errorCapacity;
    }
    *access = (muiWindowAccess){
        .allocator = def->allocator,
        .glue = def->glue,
        .action = def->action,
        .user = def->user,
    };
    muiResult status = muiAccess_Enable(glue->context, glue->root);
    if (status != mui_success)
    {
        muiWindowRelease(&def->allocator, access, sizeof(muiWindowAccess),
                         alignof(muiWindowAccess));
        return status;
    }
    status = MakeAdapter(access, def, &handles);
    if (status != mui_success)
    {
        (void)muiAccess_Disable(glue->context, glue->root);
        muiWindowRelease(&def->allocator, access, sizeof(muiWindowAccess),
                         alignof(muiWindowAccess));
        return status;
    }
#if MUI_WINDOW_ATSPI
    Place(access);
#endif
    *accessOut = access;
    return mui_success;
}

void muiDestroyWindowAccess(muiWindowAccess* access)
{
    if (access == nullptr)
    {
        return;
    }
    const muiWindowGlue* glue = access->glue;
    if (access->root != nullptr)
    {
        (void)mwinRequestAccessibilityRoot(glue->windows, glue->window, nullptr, nullptr);
    }
    DestroyAdapter(access);
    (void)muiAccess_Disable(glue->context, glue->root);
    muiAllocator allocator = access->allocator;
    muiWindowRelease(&allocator, access, sizeof(muiWindowAccess), alignof(muiWindowAccess));
}

// Applies an update to the adapter: the root it then has, or NULL.
static muiResult Apply(muiWindowAccess* access, const muiAccessUpdate* update, void** rootOut)
{
    *rootOut = nullptr;
    switch (access->kind)
    {
#if MUI_WINDOW_UIA
    case kind_uia:
        *rootOut = muiUiaAdapter_GetRoot(access->adapter);
        return muiUiaAdapter_Apply(access->adapter, update);
#endif
#if MUI_WINDOW_NS
    case kind_ns:
    {
        muiResult status = muiNsAdapter_Apply(access->adapter, update);
        *rootOut = muiNsAdapter_GetRoot(access->adapter);
        return status;
    }
#endif
#if MUI_WINDOW_ATSPI
    case kind_atspi:
        return muiAtspiAdapter_Apply(access->adapter, update);
#endif
    default:
        return muiAccessTree_Apply(access->adapter, update, nullptr);
    }
}

muiResult muiWindowAccess_Update(muiWindowAccess* access)
{
    if (access == nullptr)
    {
        return mui_errorInvalid;
    }
    const muiWindowGlue* glue = access->glue;
    muiAccessUpdate update;
    muiResult status = muiBuildAccessUpdate(glue->context, glue->root, &update);
    void* root = nullptr;
    status = status == mui_success ? Apply(access, &update, &root) : status;
    if (status != mui_success || root == access->root)
    {
        return status;
    }
    if (mwinRequestAccessibilityRoot(glue->windows, glue->window, root, nullptr) != mwin_success)
    {
        return mui_errorInvalid;
    }
    access->root = root;
    return mui_success;
}

muiResult muiWindowAccess_HandleEvent(muiWindowAccess* access, const mwinEvent* event)
{
    if (access == nullptr || event == nullptr)
    {
        return mui_errorInvalid;
    }
    const muiWindowGlue* glue = access->glue;
    if (event->window.index1 != glue->window.index1 ||
        event->window.generation != glue->window.generation)
    {
        return mui_success;
    }
    float scale = event->type == mwin_eventScaleChanged ? event->data.scale.scale : 0.0f;
    (void)scale;
    switch (access->kind)
    {
#if MUI_WINDOW_UIA
    case kind_uia:
        return scale > 0.0f ? muiUiaAdapter_SetScale(access->adapter, scale) : mui_success;
#endif
#if MUI_WINDOW_ATSPI
    case kind_atspi:
        if (event->type == mwin_eventScaleChanged || event->type == mwin_eventMoved)
        {
            Place(access);
        }
        return scale > 0.0f ? muiAtspiAdapter_SetScale(access->adapter, scale) : mui_success;
#endif
    default:
        return mui_success;
    }
}

const muiAccessTree* muiWindowAccess_GetTree(const muiWindowAccess* access)
{
    if (access == nullptr)
    {
        return nullptr;
    }
    switch (access->kind)
    {
#if MUI_WINDOW_UIA
    case kind_uia:
        return muiUiaAdapter_GetTree(access->adapter);
#endif
#if MUI_WINDOW_NS
    case kind_ns:
        return muiNsAdapter_GetTree(access->adapter);
#endif
#if MUI_WINDOW_ATSPI
    case kind_atspi:
        return muiAtspiAdapter_GetTree(access->adapter);
#endif
    default:
        return access->adapter;
    }
}

bool muiWindowAccess_HasAdapter(const muiWindowAccess* access)
{
    return access != nullptr && access->kind != kind_tree;
}
