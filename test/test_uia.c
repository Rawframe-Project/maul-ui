// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The UI Automation adapter, read by a UI Automation client on a second
// thread while the window's thread pumps its messages:
// - the root's children as shown: a generic node flattened away;
// - names, control types, a heading's level and a landmark;
// - bounds in screen pixels at a scale of 1.5, and the node under a
//   point;
// - the focus, and focusing asked of the host;
// - a node removed answering UIA_E_ELEMENTNOTAVAILABLE.

#include "test_harness.h"

#include "maul-ui/access_uia.h"

#include <stddef.h>
#include <string.h>
#include <wchar.h>

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include "uia_com.h"
#include "uia_ids.h"

#include <ole2.h>
#include <uiautomationclient.h>
#include <uiautomationcore.h>
#include <uiautomationcoreapi.h>
#include <windows.h>

// The adapter's own declarations of UI Automation (src/uia_com.h,
// src/uia_ids.h) against the headers: the same layouts and values.
static_assert(sizeof(muiUiaSimpleTable) == sizeof(IRawElementProviderSimpleVtbl),
              "IRawElementProviderSimpleVtbl");
static_assert(offsetof(muiUiaSimpleTable, QueryInterface) ==
                  offsetof(IRawElementProviderSimpleVtbl, QueryInterface),
              "IRawElementProviderSimpleVtbl QueryInterface");
static_assert(offsetof(muiUiaSimpleTable, AddRef) ==
                  offsetof(IRawElementProviderSimpleVtbl, AddRef),
              "IRawElementProviderSimpleVtbl AddRef");
static_assert(offsetof(muiUiaSimpleTable, Release) ==
                  offsetof(IRawElementProviderSimpleVtbl, Release),
              "IRawElementProviderSimpleVtbl Release");
static_assert(offsetof(muiUiaSimpleTable, get_ProviderOptions) ==
                  offsetof(IRawElementProviderSimpleVtbl, get_ProviderOptions),
              "IRawElementProviderSimpleVtbl get_ProviderOptions");
static_assert(offsetof(muiUiaSimpleTable, GetPatternProvider) ==
                  offsetof(IRawElementProviderSimpleVtbl, GetPatternProvider),
              "IRawElementProviderSimpleVtbl GetPatternProvider");
static_assert(offsetof(muiUiaSimpleTable, GetPropertyValue) ==
                  offsetof(IRawElementProviderSimpleVtbl, GetPropertyValue),
              "IRawElementProviderSimpleVtbl GetPropertyValue");
static_assert(offsetof(muiUiaSimpleTable, get_HostRawElementProvider) ==
                  offsetof(IRawElementProviderSimpleVtbl, get_HostRawElementProvider),
              "IRawElementProviderSimpleVtbl get_HostRawElementProvider");
static_assert(sizeof(muiUiaFragmentTable) == sizeof(IRawElementProviderFragmentVtbl),
              "IRawElementProviderFragmentVtbl");
static_assert(offsetof(muiUiaFragmentTable, QueryInterface) ==
                  offsetof(IRawElementProviderFragmentVtbl, QueryInterface),
              "IRawElementProviderFragmentVtbl QueryInterface");
static_assert(offsetof(muiUiaFragmentTable, AddRef) ==
                  offsetof(IRawElementProviderFragmentVtbl, AddRef),
              "IRawElementProviderFragmentVtbl AddRef");
static_assert(offsetof(muiUiaFragmentTable, Release) ==
                  offsetof(IRawElementProviderFragmentVtbl, Release),
              "IRawElementProviderFragmentVtbl Release");
static_assert(offsetof(muiUiaFragmentTable, Navigate) ==
                  offsetof(IRawElementProviderFragmentVtbl, Navigate),
              "IRawElementProviderFragmentVtbl Navigate");
static_assert(offsetof(muiUiaFragmentTable, GetRuntimeId) ==
                  offsetof(IRawElementProviderFragmentVtbl, GetRuntimeId),
              "IRawElementProviderFragmentVtbl GetRuntimeId");
static_assert(offsetof(muiUiaFragmentTable, get_BoundingRectangle) ==
                  offsetof(IRawElementProviderFragmentVtbl, get_BoundingRectangle),
              "IRawElementProviderFragmentVtbl get_BoundingRectangle");
static_assert(offsetof(muiUiaFragmentTable, GetEmbeddedFragmentRoots) ==
                  offsetof(IRawElementProviderFragmentVtbl, GetEmbeddedFragmentRoots),
              "IRawElementProviderFragmentVtbl GetEmbeddedFragmentRoots");
static_assert(offsetof(muiUiaFragmentTable, SetFocus) ==
                  offsetof(IRawElementProviderFragmentVtbl, SetFocus),
              "IRawElementProviderFragmentVtbl SetFocus");
static_assert(offsetof(muiUiaFragmentTable, get_FragmentRoot) ==
                  offsetof(IRawElementProviderFragmentVtbl, get_FragmentRoot),
              "IRawElementProviderFragmentVtbl get_FragmentRoot");
static_assert(sizeof(muiUiaFragmentRootTable) == sizeof(IRawElementProviderFragmentRootVtbl),
              "IRawElementProviderFragmentRootVtbl");
static_assert(offsetof(muiUiaFragmentRootTable, QueryInterface) ==
                  offsetof(IRawElementProviderFragmentRootVtbl, QueryInterface),
              "IRawElementProviderFragmentRootVtbl QueryInterface");
static_assert(offsetof(muiUiaFragmentRootTable, AddRef) ==
                  offsetof(IRawElementProviderFragmentRootVtbl, AddRef),
              "IRawElementProviderFragmentRootVtbl AddRef");
static_assert(offsetof(muiUiaFragmentRootTable, Release) ==
                  offsetof(IRawElementProviderFragmentRootVtbl, Release),
              "IRawElementProviderFragmentRootVtbl Release");
static_assert(offsetof(muiUiaFragmentRootTable, ElementProviderFromPoint) ==
                  offsetof(IRawElementProviderFragmentRootVtbl, ElementProviderFromPoint),
              "IRawElementProviderFragmentRootVtbl ElementProviderFromPoint");
static_assert(offsetof(muiUiaFragmentRootTable, GetFocus) ==
                  offsetof(IRawElementProviderFragmentRootVtbl, GetFocus),
              "IRawElementProviderFragmentRootVtbl GetFocus");
static_assert(UIA_OPTION_SERVER_SIDE == ProviderOptions_ServerSideProvider,
              "ProviderOptions_ServerSideProvider");
static_assert(UIA_OPTION_COM_THREADING == ProviderOptions_UseComThreading,
              "ProviderOptions_UseComThreading");
static_assert(NAVIGATE_PARENT == NavigateDirection_Parent, "NavigateDirection_Parent");
static_assert(NAVIGATE_NEXT == NavigateDirection_NextSibling, "NavigateDirection_NextSibling");
static_assert(NAVIGATE_PREVIOUS == NavigateDirection_PreviousSibling,
              "NavigateDirection_PreviousSibling");
static_assert(NAVIGATE_FIRST == NavigateDirection_FirstChild, "NavigateDirection_FirstChild");
static_assert(NAVIGATE_LAST == NavigateDirection_LastChild, "NavigateDirection_LastChild");
static_assert(sizeof(muiUiaRect) == sizeof(struct UiaRect) &&
                  offsetof(muiUiaRect, height) == offsetof(struct UiaRect, height),
              "UiaRect");
static_assert(APPEND_RUNTIME_ID == UiaAppendRuntimeId && ROOT_OBJECT_ID == UiaRootObjectId,
              "runtime and root ids");

// The ids, which the Windows SDK gives as const variables rather than
// constants.
static void TestIds(void)
{
    CHECK(
        PROPERTY_CONTROL_TYPE == UIA_ControlTypePropertyId &&
            PROPERTY_LOCALIZED_CONTROL_TYPE == UIA_LocalizedControlTypePropertyId &&
            PROPERTY_NAME == UIA_NamePropertyId &&
            PROPERTY_ACCELERATOR_KEY == UIA_AcceleratorKeyPropertyId &&
            PROPERTY_HAS_KEYBOARD_FOCUS == UIA_HasKeyboardFocusPropertyId &&
            PROPERTY_IS_KEYBOARD_FOCUSABLE == UIA_IsKeyboardFocusablePropertyId &&
            PROPERTY_IS_ENABLED == UIA_IsEnabledPropertyId &&
            PROPERTY_HELP_TEXT == UIA_HelpTextPropertyId &&
            PROPERTY_IS_PASSWORD == UIA_IsPasswordPropertyId &&
            PROPERTY_IS_OFFSCREEN == UIA_IsOffscreenPropertyId &&
            PROPERTY_ORIENTATION == UIA_OrientationPropertyId &&
            PROPERTY_IS_REQUIRED_FOR_FORM == UIA_IsRequiredForFormPropertyId &&
            PROPERTY_ITEM_STATUS == UIA_ItemStatusPropertyId &&
            PROPERTY_LIVE_SETTING == UIA_LiveSettingPropertyId &&
            PROPERTY_POSITION_IN_SET == UIA_PositionInSetPropertyId &&
            PROPERTY_SIZE_OF_SET == UIA_SizeOfSetPropertyId &&
            PROPERTY_LEVEL == UIA_LevelPropertyId &&
            PROPERTY_LANDMARK_TYPE == UIA_LandmarkTypePropertyId &&
            PROPERTY_LOCALIZED_LANDMARK_TYPE == UIA_LocalizedLandmarkTypePropertyId &&
            PROPERTY_FULL_DESCRIPTION == UIA_FullDescriptionPropertyId &&
            PROPERTY_HEADING_LEVEL == UIA_HeadingLevelPropertyId &&
            PROPERTY_IS_DIALOG == UIA_IsDialogPropertyId &&
            CONTROL_BUTTON == UIA_ButtonControlTypeId &&
            CONTROL_CHECK_BOX == UIA_CheckBoxControlTypeId &&
            CONTROL_COMBO_BOX == UIA_ComboBoxControlTypeId &&
            CONTROL_EDIT == UIA_EditControlTypeId &&
            CONTROL_HYPERLINK == UIA_HyperlinkControlTypeId &&
            CONTROL_IMAGE == UIA_ImageControlTypeId &&
            CONTROL_LIST_ITEM == UIA_ListItemControlTypeId &&
            CONTROL_LIST == UIA_ListControlTypeId && CONTROL_MENU == UIA_MenuControlTypeId &&
            CONTROL_MENU_BAR == UIA_MenuBarControlTypeId &&
            CONTROL_MENU_ITEM == UIA_MenuItemControlTypeId &&
            CONTROL_PROGRESS_BAR == UIA_ProgressBarControlTypeId &&
            CONTROL_RADIO_BUTTON == UIA_RadioButtonControlTypeId &&
            CONTROL_SCROLL_BAR == UIA_ScrollBarControlTypeId &&
            CONTROL_SLIDER == UIA_SliderControlTypeId &&
            CONTROL_SPINNER == UIA_SpinnerControlTypeId &&
            CONTROL_STATUS_BAR == UIA_StatusBarControlTypeId &&
            CONTROL_TAB == UIA_TabControlTypeId && CONTROL_TAB_ITEM == UIA_TabItemControlTypeId &&
            CONTROL_TEXT == UIA_TextControlTypeId && CONTROL_TOOL_BAR == UIA_ToolBarControlTypeId &&
            CONTROL_TOOL_TIP == UIA_ToolTipControlTypeId && CONTROL_TREE == UIA_TreeControlTypeId &&
            CONTROL_TREE_ITEM == UIA_TreeItemControlTypeId &&
            CONTROL_GROUP == UIA_GroupControlTypeId &&
            CONTROL_DATA_GRID == UIA_DataGridControlTypeId &&
            CONTROL_DATA_ITEM == UIA_DataItemControlTypeId &&
            CONTROL_DOCUMENT == UIA_DocumentControlTypeId &&
            CONTROL_WINDOW == UIA_WindowControlTypeId && CONTROL_PANE == UIA_PaneControlTypeId &&
            CONTROL_HEADER_ITEM == UIA_HeaderItemControlTypeId &&
            CONTROL_TABLE == UIA_TableControlTypeId &&
            CONTROL_TITLE_BAR == UIA_TitleBarControlTypeId &&
            CONTROL_SEPARATOR == UIA_SeparatorControlTypeId,
        "the ids");
}

#define DEADLINE_MS 20000u
#define SCALE       1.5f

typedef struct Built
{
    muiAccessNode nodes[8];
    const muiAccessNode* sent[8];
    uint64_t children[8];
    uint32_t nodeCount;
    uint32_t childCount;
} Built;

static muiAccessNode* Add(Built* built, uint64_t id, muiRole role, const char* label, float x,
                          float y, float width, float height)
{
    muiAccessNode* node = &built->nodes[built->nodeCount];
    *node = (muiAccessNode){.id = id,
                            .role = role,
                            .bounds = {0.0f, 0.0f, width, height},
                            .transform = {1.0f, 0.0f, 0.0f, 1.0f, x, y}};
    if (label != NULL)
    {
        node->text[role == mui_roleLabel ? mui_accessValue : mui_accessLabel] = label;
        node->textLength[role == mui_roleLabel ? mui_accessValue : mui_accessLabel] =
            (uint32_t)strlen(label);
    }
    built->sent[built->nodeCount++] = node;
    return node;
}

static void List(Built* built, muiAccessNode* parent, const uint64_t* ids, uint32_t count)
{
    parent->firstChild = built->childCount;
    parent->childCount = count;
    memcpy(&built->children[built->childCount], ids, count * sizeof(uint64_t));
    built->childCount += count;
}

// The window 1: a button 2; a generic 3 around a label 4; a heading 5 of
// level 2; a navigation landmark 6; a text input 7, focused.
static muiAccessUpdate Build(Built* built)
{
    *built = (Built){0};
    muiAccessNode* root = Add(built, 1, mui_roleWindow, "Main", 0, 0, 400, 300);
    Add(built, 2, mui_roleButton, "OK", 10, 10, 100, 40)->flags = mui_accessFocusable;
    muiAccessNode* generic = Add(built, 3, mui_roleGeneric, NULL, 10, 60, 100, 20);
    (void)Add(built, 4, mui_roleLabel, "Hello", 0, 0, 100, 20);
    Add(built, 5, mui_roleHeading, "Title", 10, 90, 100, 20)->values.level = 2;
    (void)Add(built, 6, mui_roleNavigation, "Links", 10, 120, 100, 20);
    Add(built, 7, mui_roleTextInput, "Name", 10, 150, 200, 24)->flags = mui_accessFocusable;
    List(built, root, (const uint64_t[]){2, 3, 5, 6, 7}, 5);
    List(built, generic, (const uint64_t[]){4}, 1);
    return (muiAccessUpdate){built->sent, built->nodeCount, built->children, 1, 7};
}

typedef struct Program
{
    HWND window;
    muiUiaAdapter* adapter;
    // Set by the client: removal wanted, and done.
    HANDLE removal;
    HANDLE removed;
    HANDLE done;
    // What the host was asked, by the window's thread.
    volatile LONG focusAsked;
} Program;

static Program s_program;

static bool Act(void* user, const muiAccessRequest* request)
{
    Program* program = user;
    if (request->action == mui_actionFocus)
    {
        InterlockedExchange(&program->focusAsked, (LONG)request->target);
    }
    return true;
}

static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    intptr_t result = 0;
    if (message == WM_GETOBJECT && s_program.adapter != NULL &&
        muiUiaAdapter_HandleGetObject(s_program.adapter, wParam, lParam, &result))
    {
        return result;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

static bool NameIs(IUIAutomationElement* element, const wchar_t* expected)
{
    BSTR name = NULL;
    bool same = SUCCEEDED(IUIAutomationElement_get_CurrentName(element, &name)) && name != NULL &&
                wcscmp(name, expected) == 0;
    SysFreeString(name);
    return same;
}

static bool TypeIs(IUIAutomationElement* element, CONTROLTYPEID expected)
{
    CONTROLTYPEID type = 0;
    return SUCCEEDED(IUIAutomationElement_get_CurrentControlType(element, &type)) &&
           type == expected;
}

static bool IntPropertyIs(IUIAutomationElement* element, PROPERTYID property, int expected)
{
    VARIANT value;
    VariantInit(&value);
    bool same =
        SUCCEEDED(IUIAutomationElement_GetCurrentPropertyValue(element, property, &value)) &&
        value.vt == VT_I4 && value.lVal == expected;
    VariantClear(&value);
    return same;
}

// The root's children as the client walks them, in order; the caller
// releases them.
static uint32_t Children(IUIAutomation* automation, IUIAutomationElement* root,
                         IUIAutomationElement** out, uint32_t capacity)
{
    IUIAutomationTreeWalker* walker = NULL;
    uint32_t count = 0;
    if (FAILED(IUIAutomation_get_RawViewWalker(automation, &walker)))
    {
        return 0;
    }
    IUIAutomationElement* at = NULL;
    (void)IUIAutomationTreeWalker_GetFirstChildElement(walker, root, &at);
    while (at != NULL && count < capacity)
    {
        out[count++] = at;
        IUIAutomationElement* next = NULL;
        (void)IUIAutomationTreeWalker_GetNextSiblingElement(walker, at, &next);
        at = next;
    }
    IUIAutomationTreeWalker_Release(walker);
    return count;
}

static void CheckChildren(IUIAutomationElement** children, uint32_t count)
{
    CHECK(count == 5, "five children, the generic flattened");
    if (count != 5)
    {
        return;
    }
    CHECK(NameIs(children[0], L"OK") && TypeIs(children[0], UIA_ButtonControlTypeId), "a button");
    CHECK(NameIs(children[1], L"Hello") && TypeIs(children[1], UIA_TextControlTypeId),
          "a label, named by its value");
    // HeadingLevel2 and the navigation landmark of UI Automation.
    CHECK(NameIs(children[2], L"Title") && IntPropertyIs(children[2], 30173, 80052),
          "a heading of level 2");
    CHECK(NameIs(children[3], L"Links") && IntPropertyIs(children[3], 30157, 80003),
          "a navigation landmark");
    BOOL focused = FALSE;
    CHECK(NameIs(children[4], L"Name") && TypeIs(children[4], UIA_EditControlTypeId) &&
              SUCCEEDED(IUIAutomationElement_get_CurrentHasKeyboardFocus(children[4], &focused)) &&
              focused,
          "a text input, focused");
}

static void CheckPlaces(IUIAutomation* automation, IUIAutomationElement* button)
{
    POINT origin = {0, 0};
    (void)ClientToScreen(s_program.window, &origin);
    RECT rect = {0};
    CHECK(SUCCEEDED(IUIAutomationElement_get_CurrentBoundingRectangle(button, &rect)) &&
              rect.left == origin.x + 15 && rect.top == origin.y + 15 &&
              rect.right == origin.x + 165 && rect.bottom == origin.y + 75,
          "bounds in screen pixels");
    POINT middle = {origin.x + 90, origin.y + 45};
    IUIAutomationElement* under = NULL;
    CHECK(SUCCEEDED(IUIAutomation_ElementFromPoint(automation, middle, &under)) && under != NULL &&
              NameIs(under, L"OK"),
          "the node under a point");
    if (under != NULL)
    {
        IUIAutomationElement_Release(under);
    }
    CHECK(SUCCEEDED(IUIAutomationElement_SetFocus(button)) && s_program.focusAsked == 2,
          "focusing asked of the host");
}

static void Inspect(IUIAutomation* automation)
{
    IUIAutomationElement* root = NULL;
    CHECK(SUCCEEDED(IUIAutomation_ElementFromHandle(automation, s_program.window, &root)) &&
              root != NULL,
          "the window's element");
    if (root == NULL)
    {
        return;
    }
    IUIAutomationElement* children[8] = {NULL};
    uint32_t count = Children(automation, root, children, 8);
    CheckChildren(children, count);
    if (count != 0)
    {
        CheckPlaces(automation, children[0]);
        SetEvent(s_program.removal);
        (void)WaitForSingleObject(s_program.removed, DEADLINE_MS);
        BSTR name = NULL;
        CHECK(IUIAutomationElement_get_CurrentName(children[0], &name) ==
                  (HRESULT)UIA_E_ELEMENTNOTAVAILABLE,
              "a node removed is no longer available");
        SysFreeString(name);
    }
    for (uint32_t i = 0; i < count; i++)
    {
        IUIAutomationElement_Release(children[i]);
    }
    IUIAutomationElement_Release(root);
}

// The client, which may not run on the window's thread.
static DWORD WINAPI Client(void* user)
{
    (void)user;
    IUIAutomation* automation = NULL;
    if (SUCCEEDED(CoInitializeEx(NULL, COINIT_MULTITHREADED)))
    {
        CHECK(SUCCEEDED(CoCreateInstance(&CLSID_CUIAutomation, NULL, CLSCTX_INPROC_SERVER,
                                         &IID_IUIAutomation, (void**)&automation)),
              "a client");
        if (automation != NULL)
        {
            Inspect(automation);
            IUIAutomation_Release(automation);
        }
        CoUninitialize();
    }
    SetEvent(s_program.done);
    return 0;
}

static HWND MakeWindow(void)
{
    WNDCLASSW type = {.lpfnWndProc = WindowProcedure,
                      .hInstance = GetModuleHandleW(NULL),
                      .lpszClassName = L"maul-ui uia test"};
    (void)RegisterClassW(&type);
    HWND window =
        CreateWindowExW(WS_EX_TOPMOST, type.lpszClassName, L"Maul UI", WS_OVERLAPPEDWINDOW, 100,
                        100, 640, 480, NULL, NULL, type.hInstance, NULL);
    if (window != NULL)
    {
        ShowWindow(window, SW_SHOWNOACTIVATE);
        UpdateWindow(window);
    }
    return window;
}

// Pumps the window's messages until the client is done, removing the
// button when the client asks.
static void Pump(Built* built)
{
    const HANDLE waits[2] = {s_program.done, s_program.removal};
    ULONGLONG start = GetTickCount64();
    while (GetTickCount64() - start < DEADLINE_MS)
    {
        DWORD woke = MsgWaitForMultipleObjects(2, waits, FALSE, 100, QS_ALLINPUT);
        if (woke == WAIT_OBJECT_0)
        {
            return;
        }
        if (woke == WAIT_OBJECT_0 + 1)
        {
            ResetEvent(s_program.removal);
            muiAccessNode root = built->nodes[0];
            root.firstChild = 1;
            root.childCount = 4;
            const muiAccessNode* sent[1] = {&root};
            const muiAccessUpdate update = {sent, 1, built->children, 0, 0};
            CHECK(muiUiaAdapter_Apply(s_program.adapter, &update) == mui_success, "removed");
            SetEvent(s_program.removed);
        }
        MSG message;
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    CHECK(false, "the client finished in time");
}

static void TestContract(void)
{
    muiUiaAdapterDef def = muiDefaultUiaAdapterDef();
    muiUiaAdapter* adapter = NULL;
    CHECK(muiCreateUiaAdapter(&def, &adapter) == mui_errorInvalid && adapter == NULL,
          "no window or action");
    def.window = s_program.window;
    def.action = Act;
    def.scale = 0.0f;
    CHECK(muiCreateUiaAdapter(&def, &adapter) == mui_errorInvalid, "no scale");
    CHECK(muiUiaAdapter_Apply(NULL, NULL) == mui_errorInvalid &&
              muiUiaAdapter_GetTree(NULL) == NULL && muiUiaAdapter_GetRoot(NULL) == NULL &&
              muiUiaAdapter_SetScale(NULL, 1.0f) == mui_errorInvalid,
          "NULL adapters");
    muiDestroyUiaAdapter(NULL);
}

int main(void)
{
    CHECK(SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED)), "an STA");
    s_program.window = MakeWindow();
    CHECK(s_program.window != NULL, "a window");
    TestIds();
    TestContract();
    muiUiaAdapterDef def = muiDefaultUiaAdapterDef();
    def.window = s_program.window;
    def.scale = SCALE;
    def.action = Act;
    def.user = &s_program;
    CHECK(muiCreateUiaAdapter(&def, &s_program.adapter) == mui_success, "an adapter");
    CHECK(muiUiaAdapter_SetScale(s_program.adapter, 0.0f) == mui_errorInvalid &&
              muiUiaAdapter_GetRoot(s_program.adapter) != NULL,
          "the adapter's contract");
    static Built s_built;
    muiAccessUpdate update = Build(&s_built);
    CHECK(muiUiaAdapter_Apply(s_program.adapter, &update) == mui_success &&
              muiAccessTree_Count(muiUiaAdapter_GetTree(s_program.adapter)) == 7,
          "the tree");
    s_program.removal = CreateEventW(NULL, TRUE, FALSE, NULL);
    s_program.removed = CreateEventW(NULL, TRUE, FALSE, NULL);
    s_program.done = CreateEventW(NULL, TRUE, FALSE, NULL);
    HANDLE client = CreateThread(NULL, 0, Client, NULL, 0, NULL);
    CHECK(client != NULL, "the client's thread");
    if (client != NULL)
    {
        Pump(&s_built);
        (void)WaitForSingleObject(client, DEADLINE_MS);
        CloseHandle(client);
    }
    muiUiaAdapter* adapter = s_program.adapter;
    s_program.adapter = NULL;
    muiDestroyUiaAdapter(adapter);
    DestroyWindow(s_program.window);
    CoUninitialize();
    return s_failures == 0 ? 0 : 1;
}
