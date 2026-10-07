# Maul UI API reference

Generated from the public headers by `tools/gen_api.py`. The headers
are the source of truth; this file mirrors them.

## `base.h`

The base of the Maul UI API: the library version, the export and attribute macros, and the result codes every fallible function returns.

```c
muiVersion muiGetVersion(void);
```
Returns the version of the library that was linked, which may differ from the MUI_VERSION macros a program was compiled with.  @return The library version. @par Thread safety Safe from any thread.

```c
const char* muiResultName(muiResult result);
```
Returns the name of a result code, for diagnostics.  @param result  Any value; an unknown one is named as such. @return A static, NUL-terminated string such as "mui_errorCapacity". @par Thread safety Safe from any thread.

## `access.h`

Accessibility (record mui-0008): the platform-neutral accessibility tree. Every node of a root's tree is a node of it, with a role, text, flags and actions; most come from what the library already holds (rectangles, scrolling, focus, states, value ranges, virtual lists), the rest from the host. The library builds updates in the shape AccessKit uses: the nodes that changed, each sent whole, the focus with every update, the root with the first. The host hands them to the adapters, which keep their own copies, and applies on its thread the requests the adapters queued. Nothing is built until the host enables a root, as when Maul Window reports that an assistive technology asked for one.

```c
uint64_t muiAccessIdOf(muiNodeId nodeId);
```
The accessibility id of a node.  @param nodeId  The node. @return Its id. @par Thread safety Safe from any thread.

```c
muiNodeId muiNodeIdOfAccess(uint64_t id);
```
The node of an accessibility id.  @param id  The id. @return Its node. @par Thread safety Safe from any thread.

```c
MUI_NODISCARD MUI_API muiResult muiSetAccessTextFunction(muiContext* context, muiAccessTextFunction function, void* user);
```
Sets the function host content's text comes from: a node whose content is the host's (maul-ui/layout.h's mui_contentHost) and whose value text the host did not set reads as what it returns, and as a label when the host gave it no role. NULL reads nothing.  @param context   The context. @param function  The function, or NULL. @param user      Passed to it. @return `mui_success`; `mui_errorInvalid` for a NULL context or a call from a measure or paint function. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiAccess_Enable(muiContext* context, muiNodeId rootId);
```
Builds updates for a root's tree from now on, the next one whole. Enabling an enabled root makes its next update whole again, as when an adapter starts over. The first root enabled allocates a copy of each node as last sent and the update's buffers.  @param context  The context. @param rootId   A node without a parent. @return `mui_success`; `mui_errorCapacity` when `limits.accessRoots` roots are enabled or memory runs out; `mui_errorInvalid` for a NULL context, the null id, a node with a parent, or a call from a measure or paint function; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiAccess_Disable(muiContext* context, muiNodeId rootId);
```
Stops building updates for a root; the last root disabled frees what enabling allocated.  @param context  The context. @param rootId   The root. @return `mui_success`; `mui_empty` for a root not enabled; `mui_errorInvalid` for a NULL context, the null id or a call from a measure or paint function. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiBuildAccessUpdate(muiContext* context, muiNodeId rootId, muiAccessUpdate* updateOut);
```
Builds the update for an enabled root after its layout: the nodes whose role, texts, flags, actions, bounds, transform, values or children changed since the last update, each whole.  @param context    The context. @param rootId     The root. @param updateOut  Receives the update. @return `mui_success`; `mui_empty` for a root not enabled; `mui_errorInvalid` for a NULL argument, the null id, or a call from a measure or paint function; `mui_errorStale` for a root that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiPerformAccessAction(muiContext* context, const muiAccessRequest* request, bool* handledOut);
```
Applies a request to a node that takes its action.  @param context     The context. @param request     The request. @param handledOut  Receives whether it did anything: a click a widget handled, a focus or value or offset that moved, a request posted. May be NULL. @return `mui_success`; `mui_empty` for a node that does not take the action; `mui_errorInvalid` for a NULL context or request, an unknown action, a value that is not finite, or a call from a measure, paint or event function; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_SetAccessRole(muiContext* context, muiNodeId nodeId, muiRole role);
```
Sets a node's role.  @param context  The context. @param nodeId   The node. @param role     The role, at most MUI_ROLE_LAST. @return `mui_success`; `mui_errorCapacity` when `limits.accessNodes` nodes have accessibility data; `mui_errorInvalid` for a NULL context, the null id, an unknown role, or a call from a measure or paint function; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_GetAccessRole(const muiContext* context, muiNodeId nodeId, muiRole* roleOut);
```
Reads a node's role: mui_roleGeneric for one the host gave none.  @param context  The context. @param nodeId   The node. @param roleOut  Receives the role. @return `mui_success`; `mui_errorInvalid` for a NULL argument or the null id; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_SetAccessText(muiContext* context, muiNodeId nodeId, muiAccessTextKind kind, const char* text, size_t length);
```
Sets one of a node's texts; a length of 0 clears it. The text is copied.  @param context  The context. @param nodeId   The node. @param kind     Which text. @param text     UTF-8, non-NULL when length is not 0, without NUL. @param length   Its length in bytes, below 2^31. @return `mui_success`; `mui_errorCapacity` when `limits.accessNodes` nodes have accessibility data or memory runs out; `mui_errorInvalid` for a NULL context, the null id, an unknown kind, text that is not UTF-8 or holds a NUL, or a call from a measure or paint function; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_GetAccessText(const muiContext* context, muiNodeId nodeId, muiAccessTextKind kind, const char** textOut, size_t* lengthOut);
```
Reads one of a node's texts.  @param context    The context. @param nodeId     The node. @param kind       Which text. @param textOut    Receives it, NUL-terminated, valid until it is set again or the node is destroyed. @param lengthOut  Receives its length. @return `mui_success`; `mui_empty` for none; `mui_errorInvalid` for a NULL argument, the null id or an unknown kind; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_SetAccessFlags(muiContext* context, muiNodeId nodeId, muiAccessFlags flags);
```
Sets a node's flags, those the host sets.  @param context  The context. @param nodeId   The node. @param flags    Within MUI_ACCESS_HOST_FLAGS. @return `mui_success`; `mui_errorCapacity` when `limits.accessNodes` nodes have accessibility data; `mui_errorInvalid` for a NULL context, the null id, a flag outside those, or a call from a measure or paint function; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_GetAccessFlags(const muiContext* context, muiNodeId nodeId, muiAccessFlags* flagsOut);
```
Reads the flags the host set on a node.  @param context   The context. @param nodeId    The node. @param flagsOut  Receives them, 0 for none. @return `mui_success`; `mui_errorInvalid` for a NULL argument or the null id; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_SetAccessRelation(muiContext* context, muiNodeId nodeId, muiAccessRelation kind, const muiNodeId* targets, uint32_t count);
```
Names other nodes from a node by one relation, replacing those it named by it; a count of 0 clears it. The ids are copied; a node named that is destroyed later stays named, and adapters pass over ids they do not hold.  @param context  The context. @param nodeId   The node. @param kind     The relation. @param targets  The nodes, non-NULL when count is not 0. @param count    How many, below 2^16. @return `mui_success`; `mui_errorCapacity` when `limits.accessNodes` nodes have accessibility data or memory runs out; `mui_errorInvalid` for a NULL context, the null id as the node or a target, an unknown kind, or a call from a measure or paint function; `mui_errorStale` for a node or a target that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_GetAccessRelation(const muiContext* context, muiNodeId nodeId, muiAccessRelation kind, muiNodeId* targetsOut, uint32_t capacity, uint32_t* countOut);
```
Reads the nodes a node names by one relation.  @param context     The context. @param nodeId      The node. @param kind        The relation. @param targetsOut  Receives the nodes, up to capacity; may be NULL when capacity is 0. @param capacity    Room in targetsOut. @param countOut    Receives how many it names, whatever the room. @return `mui_success`; `mui_errorCapacity` when they do not fit, those that fit written; `mui_errorInvalid` for a NULL argument, the null id or an unknown kind; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
muiAccessValues muiDefaultAccessValues(void);
```
The default values: none of them.  @return The values. @par Thread safety Safe from any thread.

```c
MUI_NODISCARD MUI_API muiResult muiNode_SetAccessValues(muiContext* context, muiNodeId nodeId, const muiAccessValues* values);
```
Sets a node's typed values.  @param context  The context. @param nodeId   The node. @param values   The values, each enum within its own. @return `mui_success`; `mui_errorCapacity` when `limits.accessNodes` nodes have accessibility data; `mui_errorInvalid` for a NULL argument, the null id, an enum outside its values, or a call from a measure or paint function; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_GetAccessValues(const muiContext* context, muiNodeId nodeId, muiAccessValues* valuesOut);
```
Reads the typed values the host set on a node.  @param context    The context. @param nodeId     The node. @param valuesOut  Receives them; the defaults for none. @return `mui_success`; `mui_errorInvalid` for a NULL argument or the null id; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

## `access_android.h`

The Android accessibility adapter (record mui-0008), the component MAUL_UI_ANDROID_ACCESSIBILITY builds on Android: the accessibility tree's consumer shown to Android through maul.ui.AccessProvider (java/maul/ui/AccessProvider.java, which the host builds into its application), a provider whose virtual views are the shown nodes. The provider is the host view's (Maul Window's mwinRequestAccessibilityRoot gives it); clients' actions come back through a function of the host's. The header is C: the JNI's types pass as void*.

```c
muiAndroidAdapterDef muiDefaultAndroidAdapterDef(void);
```
The default def: the C library's allocation, 4096 nodes, no JNIEnv, no view, a scale of 1, no action function.  @return The def. @par Thread safety Safe from any thread.

```c
MUI_NODISCARD MUI_API muiResult muiCreateAndroidAdapter(const muiAndroidAdapterDef* def, muiAndroidAdapter** adapterOut);
```
Makes an adapter with an empty tree, and its provider: the class maul.ui.AccessProvider is found through the view's class loader, so that any thread the JNIEnv belongs to may make it.  @param def         The def, from muiDefaultAndroidAdapterDef. @param adapterOut  Receives the adapter; NULL on failure. @return `mui_success`; `mui_errorInvalid` for a NULL argument, a def not from muiDefaultAndroidAdapterDef, a half-set allocator, no nodes, no JNIEnv, no view, no action function or a scale not above 0; `mui_errorCapacity` when memory runs out; `mui_errorPlatform` when the provider's class is not in the application or Java fails. @par Thread safety Main thread only.

```c
void muiDestroyAndroidAdapter(muiAndroidAdapter* adapter);
```
Lets go of the provider, which answers nothing from then on, and destroys the adapter; NULL is ignored. Take the provider from the view first.  @param adapter  The adapter. @par Thread safety Main thread only.

```c
MUI_NODISCARD MUI_API muiResult muiAndroidAdapter_Apply(muiAndroidAdapter* adapter, const muiAccessUpdate* update);
```
Applies an update to the adapter's tree (muiAccessTree_Apply).  @param adapter  The adapter. @param update   The update. @return As muiAccessTree_Apply; `mui_errorInvalid` for a NULL adapter. @par Thread safety Main thread only.

```c
const muiAccessTree* muiAndroidAdapter_GetTree(const muiAndroidAdapter* adapter);
```
The adapter's tree.  @param adapter  The adapter. @return The tree; NULL for a NULL adapter. @par Thread safety Main thread only.

```c
MUI_NODISCARD MUI_API muiResult muiAndroidAdapter_SetScale(muiAndroidAdapter* adapter, float scale);
```
Sets the pixels per unit, as the host scales its UI.  @param adapter  The adapter. @param scale    The scale, above 0. @return `mui_success`; `mui_errorInvalid` for a NULL adapter or a scale not above 0. @par Thread safety Main thread only.

```c
void* muiAndroidAdapter_GetRoot(muiAndroidAdapter* adapter);
```
The provider (a global reference to a maul.ui.AccessProvider), for the view to give as its accessibility node provider (mwinRequestAccessibilityRoot); the adapter keeps it until destroyed.  @param adapter  The adapter. @return The provider (a jobject), or NULL for a NULL adapter. @par Thread safety Main thread only.

## `access_aria.h`

The ARIA adapter (record mui-0008), the component MAUL_UI_ARIA builds for Emscripten: the accessibility tree's consumer mirrored into elements of the page, which the browser gives its accessibility clients. The elements are built in an element of the host's over the canvas (Maul Window's accessibility host, say), each placed over what it names and invisible; clients' actions come back through a function of the host's. A page cannot tell whether a screen reader runs, and the elements cost every user, so by default nothing is built until the program enables the adapter or a screen reader user presses the visually hidden button the adapter puts in the host. Every function here is used on the page's main thread.

```c
muiAriaAdapterDef muiDefaultAriaAdapterDef(void);
```
The default def: the C library's allocation, 4096 nodes, no host, a scale of 1, building deferred behind a button labelled "Enable accessibility", no action function.  @return The def. @par Thread safety Safe from any thread.

```c
MUI_NODISCARD MUI_API muiResult muiCreateAriaAdapter(const muiAriaAdapterDef* def, muiAriaAdapter** adapterOut);
```
Makes an adapter with an empty tree in the host element, with the enabling button when building is deferred.  @param def         The def, from muiDefaultAriaAdapterDef. @param adapterOut  Receives the adapter; NULL on failure. @return `mui_success`; `mui_errorInvalid` for a NULL argument, a def not from muiDefaultAriaAdapterDef, a half-set allocator, no nodes, no host, no label, no action function or a scale not above 0; `mui_errorPlatform` when no element matches the host, or there is no page; `mui_errorCapacity` when memory runs out. @par Thread safety Main thread only.

```c
void muiDestroyAriaAdapter(muiAriaAdapter* adapter);
```
Takes the adapter's elements out of the page and destroys it; NULL is ignored.  @param adapter  The adapter. @par Thread safety Main thread only.

```c
MUI_NODISCARD MUI_API muiResult muiAriaAdapter_Apply(muiAriaAdapter* adapter, const muiAccessUpdate* update);
```
Applies an update to the adapter's tree (muiAccessTree_Apply) and, once enabled, brings the elements to it.  @param adapter  The adapter. @param update   The update. @return As muiAccessTree_Apply; `mui_errorInvalid` for a NULL adapter. @par Thread safety Main thread only.

```c
const muiAccessTree* muiAriaAdapter_GetTree(const muiAriaAdapter* adapter);
```
The adapter's tree.  @param adapter  The adapter. @return The tree; NULL for a NULL adapter. @par Thread safety Main thread only.

```c
MUI_NODISCARD MUI_API muiResult muiAriaAdapter_SetScale(muiAriaAdapter* adapter, float scale);
```
Sets the CSS pixels per unit, as the host scales its UI, and places the elements again.  @param adapter  The adapter. @param scale    The scale, above 0. @return `mui_success`; `mui_errorInvalid` for a NULL adapter or a scale not above 0. @par Thread safety Main thread only.

```c
void muiAriaAdapter_Enable(muiAriaAdapter* adapter);
```
Builds the elements, if building was deferred and has not begun, and takes the enabling button away. The button does the same.  @param adapter  The adapter; NULL is ignored. @par Thread safety Main thread only.

```c
bool muiAriaAdapter_IsEnabled(const muiAriaAdapter* adapter);
```
Whether the elements are built.  @param adapter  The adapter. @return Whether they are; false for NULL. @par Thread safety Main thread only.

## `access_atspi.h`

The AT-SPI adapter (record mui-0008), the component MAUL_UI_ATSPI builds on Linux: the accessibility tree's consumer shown to AT-SPI, the accessibility service of Linux desktops. An application joins the accessibility bus and registers its root, whose children are its windows; each window is an adapter owning a consumer tree, applying the core's updates. Clients' actions come back through a function of the host's. Nothing here starts a thread or waits on the bus after the application is made: the host polls the application's descriptor for reading, with its other sources, and pumps it when it is readable and once a frame. Every function here is used on the thread that made the application, which "main thread" below means.

```c
muiAtspiAppDef muiDefaultAtspiAppDef(void);
```
The default def: the C library's allocation, no name and 16 windows.  @return The def. @par Thread safety Safe from any thread.

```c
MUI_NODISCARD MUI_API muiResult muiCreateAtspiApp(const muiAtspiAppDef* def, muiAtspiApp** appOut);
```
Joins the accessibility bus (the address AT_SPI_BUS_ADDRESS names, else the one the session bus's org.a11y.Bus gives) and serves the application's root, asking the registry to embed it; the answer comes at a later pump. Waits for the buses' first answers, which a local socket gives at once.  @param def     The def, from muiDefaultAtspiAppDef. @param appOut  Receives the application; NULL on failure. @return `mui_success`; `mui_errorInvalid` for a NULL argument, a def not from muiDefaultAtspiAppDef, a half-set allocator, no name or no windows; `mui_errorPlatform` when libdbus-1 or the accessibility bus cannot be reached; `mui_errorCapacity` when memory runs out. @par Thread safety Main thread only.

```c
void muiDestroyAtspiApp(muiAtspiApp* app);
```
Leaves the accessibility bus; NULL is ignored. Destroy its adapters first.  @param app  The application. @par Thread safety Main thread only.

```c
int muiAtspiApp_GetDescriptor(const muiAtspiApp* app);
```
The descriptor to poll for reading.  @param app  The application. @return The descriptor; -1 for a NULL application. @par Thread safety Main thread only.

```c
void muiAtspiApp_Pump(muiAtspiApp* app);
```
Reads what the bus has, answers clients' calls, and writes what it can, without waiting. Call it when the descriptor is readable and once a frame, as answers wait to be written.  @param app  The application; NULL is ignored. @par Thread safety Main thread only.

```c
bool muiAtspiApp_IsRegistered(const muiAtspiApp* app);
```
Whether the registry has embedded the application's root.  @param app  The application. @return Whether it has; false for NULL. @par Thread safety Main thread only.

```c
muiAtspiAdapterDef muiDefaultAtspiAdapterDef(void);
```
The default adapter def: 4096 nodes, a scale of 1, no action function.  @return The def. @par Thread safety Safe from any thread.

```c
MUI_NODISCARD MUI_API muiResult muiCreateAtspiAdapter(muiAtspiApp* app, const muiAtspiAdapterDef* def, muiAtspiAdapter** adapterOut);
```
Adds a window to the application: an adapter with an empty tree, whose root becomes the application root's last child.  @param app         The application. @param def         The def, from muiDefaultAtspiAdapterDef. @param adapterOut  Receives the adapter; NULL on failure. @return `mui_success`; `mui_errorInvalid` for a NULL argument, a def not from muiDefaultAtspiAdapterDef, no nodes, no action function or a scale not above 0; `mui_errorCapacity` when the application has its windows or memory runs out. @par Thread safety Main thread only.

```c
void muiDestroyAtspiAdapter(muiAtspiAdapter* adapter);
```
Takes a window out of the application; NULL is ignored. Its nodes answer as unknown objects from then on.  @param adapter  The adapter. @par Thread safety Main thread only.

```c
MUI_NODISCARD MUI_API muiResult muiAtspiAdapter_Apply(muiAtspiAdapter* adapter, const muiAccessUpdate* update);
```
Applies an update to the adapter's tree (muiAccessTree_Apply).  @param adapter  The adapter. @param update   The update. @return As muiAccessTree_Apply; `mui_errorInvalid` for a NULL adapter. @par Thread safety Main thread only.

```c
const muiAccessTree* muiAtspiAdapter_GetTree(const muiAtspiAdapter* adapter);
```
The adapter's tree.  @param adapter  The adapter. @return The tree; NULL for a NULL adapter. @par Thread safety Main thread only.

```c
MUI_NODISCARD MUI_API muiResult muiAtspiAdapter_SetScale(muiAtspiAdapter* adapter, float scale);
```
Sets the pixels per unit, as the window's scale changes.  @param adapter  The adapter. @param scale    The scale, above 0. @return `mui_success`; `mui_errorInvalid` for a NULL adapter or a scale not above 0. @par Thread safety Main thread only.

```c
void muiAtspiAdapter_SetPlace(muiAtspiAdapter* adapter, int32_t x, int32_t y);
```
Sets where the window's client area is on the screen, in pixels, for clients asking in screen coordinates. Where the window system does not tell it (Wayland), leave it at 0, 0: screen coordinates are then the window's own, as GTK gives them.  @param adapter  The adapter; NULL is ignored. @param x        The left edge. @param y        The top edge. @par Thread safety Main thread only.

## `access_ns.h`

The NSAccessibility adapter (record mui-0008), the component MAUL_UI_NSACCESSIBILITY builds on macOS: the accessibility tree's consumer shown to AppKit's accessibility, one object a shown node that answers the NSAccessibility protocol. The window root's object is given to the view the tree lies in (Maul Window's mwinRequestAccessibilityRoot does that), whose child it is; clients' actions come back through a function of the host's. The header is C: AppKit's objects pass as void*.

```c
muiNsAdapterDef muiDefaultNsAdapterDef(void);
```
The default def: the C library's allocation, 4096 nodes, no view, a scale of 1, no action function.  @return The def. @par Thread safety Safe from any thread.

```c
MUI_NODISCARD MUI_API muiResult muiCreateNsAdapter(const muiNsAdapterDef* def, muiNsAdapter** adapterOut);
```
Makes an adapter with an empty tree.  @param def         The def, from muiDefaultNsAdapterDef. @param adapterOut  Receives the adapter; NULL on failure. @return `mui_success`; `mui_errorInvalid` for a NULL argument, a def not from muiDefaultNsAdapterDef, a half-set allocator, no nodes, no view, no action function or a scale not above 0; `mui_errorCapacity` when memory runs out. @par Thread safety Main thread only.

```c
void muiDestroyNsAdapter(muiNsAdapter* adapter);
```
Lets go of the adapter's objects, which answer nothing from then on, and destroys it; NULL is ignored. Take its root from the view first.  @param adapter  The adapter. @par Thread safety Main thread only.

```c
MUI_NODISCARD MUI_API muiResult muiNsAdapter_Apply(muiNsAdapter* adapter, const muiAccessUpdate* update);
```
Applies an update to the adapter's tree (muiAccessTree_Apply).  @param adapter  The adapter. @param update   The update. @return As muiAccessTree_Apply; `mui_errorInvalid` for a NULL adapter. @par Thread safety Main thread only.

```c
const muiAccessTree* muiNsAdapter_GetTree(const muiNsAdapter* adapter);
```
The adapter's tree.  @param adapter  The adapter. @return The tree; NULL for a NULL adapter. @par Thread safety Main thread only.

```c
MUI_NODISCARD MUI_API muiResult muiNsAdapter_SetScale(muiNsAdapter* adapter, float scale);
```
Sets the points per unit, as the host scales its UI.  @param adapter  The adapter. @param scale    The scale, above 0. @return `mui_success`; `mui_errorInvalid` for a NULL adapter or a scale not above 0. @par Thread safety Main thread only.

```c
void* muiNsAdapter_GetRoot(muiNsAdapter* adapter);
```
The window root's object, for the view to give as its child (mwinRequestAccessibilityRoot); the adapter keeps it while the root is the tree's.  @param adapter  The adapter. @return The object (an NSAccessibilityElement), or NULL for an empty tree or a NULL adapter. @par Thread safety Main thread only.

## `access_tree.h`

The accessibility tree's consumer (record mui-0008), the component MAUL_UI_ACCESS_TREE builds: the copy of a root's accessibility tree that platform adapters read, kept from the updates muiBuildAccessUpdate gives (maul-ui/access.h). Applying an update reports what it changed, so an adapter can raise its platform's events. A tree is used on one thread, the one its platform calls on: the window's, with UI Automation's COM threading on an STA thread.

```c
muiAccessTreeDef muiDefaultAccessTreeDef(void);
```
The default def: the C library's allocation, 4096 nodes, as many as a default context's.  @return The def. @par Thread safety Safe from any thread.

```c
MUI_NODISCARD MUI_API muiResult muiCreateAccessTree(const muiAccessTreeDef* def, muiAccessTree** treeOut);
```
Creates an empty tree.  @param def      The def, from muiDefaultAccessTreeDef. @param treeOut  Receives the tree; NULL on failure. @return `mui_success`; `mui_errorInvalid` for a NULL argument, a def not from muiDefaultAccessTreeDef, a half-set allocator or no nodes; `mui_errorCapacity` when memory runs out. @par Thread safety Safe from any thread.

```c
void muiDestroyAccessTree(muiAccessTree* tree);
```
Destroys a tree and all it holds; NULL is ignored.  @param tree  The tree. @par Thread safety Safe from any thread; the tree is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiAccessTree_Apply(muiAccessTree* tree, const muiAccessUpdate* update, const muiAccessChanges* changes);
```
Applies an update whole, or nothing of it: the nodes sent replace those held, new ones join, and a node a parent sent no longer lists, which no other node sent lists, leaves with its subtree, as does a node sent with no parent that is not the root (told only as removed). The first update a tree takes, and any naming a new root, must be whole.  @param tree     The tree. @param update   The update. @param changes  Told what changed; may be NULL. @return `mui_success`; `mui_errorCapacity` when the nodes would not fit or memory runs out, which changes nothing; `mui_errorInvalid` for a NULL tree or update, or an update that does not fit the tree: no root for an empty tree, a node with the id 0 or sent twice, a child neither held nor sent, a root, or focus, that is neither, or lists that do not leave a tree: a child listed twice, or by a node sent while a node not sent lists it, the root listed, or a node under itself. @par Thread safety Safe from any thread; the tree is used by one thread at a time.

```c
uint64_t muiAccessTree_GetRoot(const muiAccessTree* tree);
```
The root's id, or 0 for an empty tree.  @param tree  The tree. @return The id. @par Thread safety Safe from any thread; the tree is used by one thread at a time.

```c
uint64_t muiAccessTree_GetFocus(const muiAccessTree* tree);
```
The focused node's id, or 0 for an empty tree.  @param tree  The tree. @return The id. @par Thread safety Safe from any thread; the tree is used by one thread at a time.

```c
uint32_t muiAccessTree_Count(const muiAccessTree* tree);
```
How many nodes the tree holds.  @param tree  The tree. @return The count; 0 for NULL. @par Thread safety Safe from any thread; the tree is used by one thread at a time.

```c
const muiAccessNode* muiAccessTree_Find(const muiAccessTree* tree, uint64_t id);
```
A node the tree holds: its texts and links are the tree's copies, valid until an update replaces or removes it; its firstChild is 0, and its children come from muiAccessTree_GetChildren.  @param tree  The tree. @param id    The node's id. @return The node; NULL for one not held or a NULL tree. @par Thread safety Safe from any thread; the tree is used by one thread at a time.

```c
uint64_t muiAccessTree_GetParent(const muiAccessTree* tree, uint64_t id);
```
A node's parent.  @param tree  The tree. @param id    The node's id. @return The parent's id; 0 for the root, a node not held, or a NULL tree. @par Thread safety Safe from any thread; the tree is used by one thread at a time.

```c
const uint64_t* muiAccessTree_GetChildren(const muiAccessTree* tree, uint64_t id, uint32_t* countOut);
```
A node's children, in order, valid until an update replaces it.  @param tree      The tree. @param id        The node's id. @param countOut  Receives how many; 0 for a node not held. @return Their ids; NULL for none. @par Thread safety Safe from any thread; the tree is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiAccessTree_GetShownChildren(const muiAccessTree* tree, uint64_t id, uint64_t* childrenOut, uint32_t capacity, uint32_t* countOut);
```
A node's children as platforms see them, in order: generic children with no label replaced by their own, hidden ones left out with their subtrees, and under a node that clips its children, those wholly outside it left out with their subtrees unless a neighbour among them is not, so the first one past each edge can still be scrolled to. The focus is not left out for being hidden (nor what is in it), generic or clipped, though a hidden ancestor hides it; the root is always shown.  @param tree        The tree. @param id          A node shown. @param childrenOut Receives the ids, up to capacity; may be NULL when capacity is 0. @param capacity    Room in childrenOut. @param countOut    Receives how many there are, whatever the room. @return `mui_success`; `mui_errorCapacity` when they do not fit, those that fit written; `mui_empty` for a node not held; `mui_errorInvalid` for a NULL tree or count. @par Thread safety Safe from any thread; the tree is used by one thread at a time.

```c
uint64_t muiAccessTree_GetShownParent(const muiAccessTree* tree, uint64_t id);
```
The nearest ancestor of a node that platforms see.  @param tree  The tree. @param id    The node's id. @return The ancestor's id; 0 for the root, a node not held, or a NULL tree. @par Thread safety Safe from any thread; the tree is used by one thread at a time.

```c
bool muiAccessTree_IsShown(const muiAccessTree* tree, uint64_t id);
```
Whether platforms see a node: it is the root, or among its shown parent's shown children.  @param tree  The tree. @param id    The node's id. @return Whether it is shown; false for a node not held. @par Thread safety Safe from any thread; the tree is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiAccessTree_GetName(const muiAccessTree* tree, uint64_t id, char* buffer, size_t capacity, size_t* lengthOut);
```
A node's name: its label; else the texts of the nodes that label it (a label node's value, another's label), joined by spaces; else, for buttons, checkboxes, radio buttons, switches, links, menu items and tabs, those of the labels and images inside it, hidden subtrees left out.  @param tree       The tree. @param id         The node's id. @param buffer     Receives the name, NUL-terminated; may be NULL when capacity is 0. @param capacity   Its size in bytes. @param lengthOut  Receives the name's length. @return `mui_success`; `mui_empty` for no name or a node not held; `mui_errorCapacity` when it does not fit, as much written as fits on a whole character; `mui_errorInvalid` for a NULL tree or length, or a NULL buffer with room. @par Thread safety Safe from any thread; the tree is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiAccessTree_GetBounds(const muiAccessTree* tree, uint64_t id, muiRect* boundsOut);
```
A node's bounds where the root is placed (the window's client area, for a root laid out in it): its bounds carried through its own transform and each ancestor's, the root's included, as the box around them.  @param tree       The tree. @param id         The node's id. @param boundsOut  Receives the bounds. @return `mui_success`; `mui_empty` for a node not held; `mui_errorInvalid` for a NULL argument. @par Thread safety Safe from any thread; the tree is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiAccessTree_Write(const muiAccessTree* tree, char* buffer, size_t capacity, size_t* lengthOut);
```
Writes the tree as text, a node a line in tree order, indented by depth: its role's name, its id's index, and its flags, actions, size, place and texts where it has them. Tests compare it.  @param tree       The tree. @param buffer     Receives the text, NUL-terminated; may be NULL when capacity is 0. @param capacity   Its size in bytes. @param lengthOut  Receives the text's length without its NUL. @return `mui_success`; `mui_errorCapacity` when it does not fit, as much written as fits; `mui_errorInvalid` for a NULL tree or length, or a NULL buffer with room. @par Thread safety Safe from any thread; the tree is used by one thread at a time.

```c
const char* muiAccessRoleName(muiRole role);
```
A role's name, as its constant's after mui_role with a lower-case first letter: "button" for mui_roleButton.  @param role  The role. @return The name; "unknown" for a role past MUI_ROLE_LAST. @par Thread safety Safe from any thread.

## `access_uia.h`

The UI Automation adapter (record mui-0008), the component MAUL_UI_UIA builds on Windows: the accessibility tree's consumer shown to UI Automation through a provider object per node. The host hands the adapter's root to its window (mwinRequestAccessibilityRoot of Maul Window, or its own WM_GETOBJECT through muiUiaAdapter_HandleGetObject) and applies the core's updates through the adapter. Clients' actions come back through a function of the host's. An adapter lives on its window's thread, which must be in a COM single-threaded apartment (OleInitialize or CoInitializeEx with COINIT_APARTMENTTHREADED): UI Automation calls its providers there, as its window's messages are dispatched. "Main thread" below means that thread.

```c
muiUiaAdapterDef muiDefaultUiaAdapterDef(void);
```
The default def: the C library's allocation, 4096 nodes, a scale of 1, and no window or action function.  @return The def. @par Thread safety Safe from any thread.

```c
MUI_NODISCARD MUI_API muiResult muiCreateUiaAdapter(const muiUiaAdapterDef* def, muiUiaAdapter** adapterOut);
```
Creates an adapter with an empty tree.  @param def         The def, from muiDefaultUiaAdapterDef. @param adapterOut  Receives the adapter; NULL on failure. @return `mui_success`; `mui_errorInvalid` for a NULL argument, a def not from muiDefaultUiaAdapterDef, a half-set allocator, no nodes, no window, no action function or a scale not above 0; `mui_errorPlatform` when the thread is not in a single-threaded apartment or UI Automation cannot be loaded; `mui_errorCapacity` when memory runs out. @par Thread safety Main thread only.

```c
void muiDestroyUiaAdapter(muiUiaAdapter* adapter);
```
Destroys an adapter; NULL is ignored. Every provider object it gave out answers UIA_E_ELEMENTNOTAVAILABLE from then on, and UI Automation is told to let go of each. Take the root from the window first.  @param adapter  The adapter. @par Thread safety Main thread only.

```c
MUI_NODISCARD MUI_API muiResult muiUiaAdapter_Apply(muiUiaAdapter* adapter, const muiAccessUpdate* update);
```
Applies an update to the adapter's tree (muiAccessTree_Apply); the provider objects of nodes it removes answer UIA_E_ELEMENTNOTAVAILABLE from then on.  @param adapter  The adapter. @param update   The update. @return As muiAccessTree_Apply; `mui_errorInvalid` for a NULL adapter. @par Thread safety Main thread only.

```c
const muiAccessTree* muiUiaAdapter_GetTree(const muiUiaAdapter* adapter);
```
The adapter's tree.  @param adapter  The adapter. @return The tree; NULL for a NULL adapter. @par Thread safety Main thread only.

```c
void* muiUiaAdapter_GetRoot(muiUiaAdapter* adapter);
```
The root's provider, an IRawElementProviderSimple*, for mwinRequestAccessibilityRoot; it stands for whatever node is the tree's root, and lives while the adapter does. No reference is added for the caller.  @param adapter  The adapter. @return The provider; NULL for a NULL adapter. @par Thread safety Main thread only.

```c
bool muiUiaAdapter_HandleGetObject(muiUiaAdapter* adapter, uintptr_t wParam, intptr_t lParam, intptr_t* resultOut);
```
Answers WM_GETOBJECT for a window procedure of the host's own: for UiaRootObjectId, gives UI Automation the root.  @param adapter    The adapter. @param wParam     The message's WPARAM. @param lParam     The message's LPARAM. @param resultOut  Receives the LRESULT to return when answered. @return Whether it answered; when not, the message goes on to DefWindowProc. @par Thread safety Main thread only.

```c
MUI_NODISCARD MUI_API muiResult muiUiaAdapter_SetScale(muiUiaAdapter* adapter, float scale);
```
Sets the pixels per unit, as the window's DPI changes.  @param adapter  The adapter. @param scale    The scale, above 0. @return `mui_success`; `mui_errorInvalid` for a NULL adapter or a scale not above 0. @par Thread safety Main thread only.

## `access_uikit.h`

The UIAccessibility adapter (record mui-0008), the component MAUL_UI_UIACCESSIBILITY builds on iOS: the accessibility tree's consumer shown to UIKit's accessibility, an element object a shown node, and a container object a shown node with shown children whose elements are its node's element and then its children. The root's object is given to the view the tree lies in (Maul Window's mwinRequestAccessibilityRoot does that), its container; clients' actions come back through a function of the host's. The header is C: UIKit's objects pass as void*.

```c
muiUikitAdapterDef muiDefaultUikitAdapterDef(void);
```
The default def: the C library's allocation, 4096 nodes, no view, a scale of 1, no action function.  @return The def. @par Thread safety Safe from any thread.

```c
MUI_NODISCARD MUI_API muiResult muiCreateUikitAdapter(const muiUikitAdapterDef* def, muiUikitAdapter** adapterOut);
```
Makes an adapter with an empty tree.  @param def         The def, from muiDefaultUikitAdapterDef. @param adapterOut  Receives the adapter; NULL on failure. @return `mui_success`; `mui_errorInvalid` for a NULL argument, a def not from muiDefaultUikitAdapterDef, a half-set allocator, no nodes, no view, no action function or a scale not above 0; `mui_errorCapacity` when memory runs out. @par Thread safety Main thread only.

```c
void muiDestroyUikitAdapter(muiUikitAdapter* adapter);
```
Lets go of the adapter's objects, which answer nothing from then on, and destroys it; NULL is ignored. Take its root from the view first.  @param adapter  The adapter. @par Thread safety Main thread only.

```c
MUI_NODISCARD MUI_API muiResult muiUikitAdapter_Apply(muiUikitAdapter* adapter, const muiAccessUpdate* update);
```
Applies an update to the adapter's tree (muiAccessTree_Apply).  @param adapter  The adapter. @param update   The update. @return As muiAccessTree_Apply; `mui_errorInvalid` for a NULL adapter. @par Thread safety Main thread only.

```c
const muiAccessTree* muiUikitAdapter_GetTree(const muiUikitAdapter* adapter);
```
The adapter's tree.  @param adapter  The adapter. @return The tree; NULL for a NULL adapter. @par Thread safety Main thread only.

```c
MUI_NODISCARD MUI_API muiResult muiUikitAdapter_SetScale(muiUikitAdapter* adapter, float scale);
```
Sets the points per unit, as the host scales its UI.  @param adapter  The adapter. @param scale    The scale, above 0. @return `mui_success`; `mui_errorInvalid` for a NULL adapter or a scale not above 0. @par Thread safety Main thread only.

```c
void* muiUikitAdapter_GetRoot(muiUikitAdapter* adapter);
```
The root's object, for the view to give as its element (mwinRequestAccessibilityRoot): its container when it has shown children, else its element. The adapter keeps it while it is the root's object; ask again after an update.  @param adapter  The adapter. @return The object (a UIAccessibilityElement), or NULL for an empty tree or a NULL adapter. @par Thread safety Main thread only.

## `context.h`

The context: the root object that owns a tree of nodes and every result computed over it.

```c
muiContextDef muiDefaultContextDef(void);
```
Returns the default context def: 4,096 nodes, 256 styles, 64 node types, 1,024 property sets, 64 notifications, 64 transitions, 256 running transitions, 256 tokens, 1,024 token names, 16 themes, 512 theme overrides, draw lists of 8,192 commands, 256 clips, 256 gradients and 16,384 glyphs, 64 layers, 16 popups, 64 exits, 8 virtual lists of 16,384 estimated items together, and the C library's allocator.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MUI_NODISCARD MUI_API muiResult muiCreateContext(const muiContextDef* def, muiContext** contextOut);
```
Creates a context and reserves the memory its limits name.  @param def         The context: a valid cookie, an allocator with both functions or neither, a node limit from 1 to 2^31 - 1 and other limits from 0 to 2^31 - 1. @param contextOut  Receives the context; set to NULL on failure. @return `mui_success`; `mui_errorInvalid` for a NULL argument, a bad cookie, a half-set allocator or a limit out of range; `mui_errorCapacity` when the allocator cannot give the memory. @par Thread safety Safe from any thread.

```c
void muiDestroyContext(muiContext* context);
```
Destroys a context, every node in it and its memory. Every id it gave out becomes meaningless.  @param context  The context, or NULL for nothing. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNextNotification(muiContext* context, muiNotification* notificationOut);
```
Takes the oldest notification.  @param context          The context. @param notificationOut  Receives it. @return `mui_success`; `mui_empty` when none is waiting; `mui_errorInvalid` for a NULL argument. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
uint64_t muiGetContextMisuse(const muiContext* context);
```
Returns how many calls the context has refused as invalid input (`mui_errorInvalid`): a count release builds can watch to catch a host's bugs. Stale ids are not misuse.  @param context  The context. @return The count; 0 for a NULL context. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
muiWorkCounts muiGetWorkCounts(const muiContext* context);
```
Returns the work a context has done since it was made.  @param context  The context. @return The counts; all 0 for a NULL context. @par Thread safety Safe from any thread; the context is used by one thread at a time.

## `draw.h`

The draw-command list (record mui-0005): what a renderer draws for a subtree, as fixed-size records in paint order, each with an index into a clip table and a transform table, so that a renderer evaluates clips per command and batches across them. Coordinates are logical units; colors are linear light with premultiplied alpha. Identical trees give byte-identical lists.

```c
MUI_NODISCARD MUI_API muiResult muiDrawSink_AddGlyphRun(muiDrawSink* sink, const muiGlyphRun* run, const muiGlyph* glyphs, uint32_t glyphCount);
```
Adds a run of glyphs to the node being painted, after what it added before, in the clip its children are drawn in. Its color is converted to linear light and multiplied by the node's opacity; at the identity transform its baseline snaps to a device pixel.  @param sink        The sink the paint function was given. @param run         The run. @param glyphs      Its glyphs, at finite positions. @param glyphCount  How many, above 0. @return `mui_success`; `mui_errorInvalid` for a NULL argument, no glyphs, a size that is not a finite number above 0, a color outside 0 to 1, or an origin or a position that is not finite; `mui_errorCapacity` when the list needs more commands or glyphs than the context's limits, which fails the build. @par Thread safety Safe from any thread; the sink is used by one thread at a time, and only during the call of the paint function given it.

```c
MUI_NODISCARD MUI_API muiResult muiDrawSink_AddRect(muiDrawSink* sink, muiRect rect, muiColor color);
```
Adds a filled rectangle to the node being painted, after what it added before, in the clip its children are drawn in: a box command with no radii, borders or gradient. At the identity transform its edges snap to device pixels, and a side that was not empty keeps one, so a thin line never vanishes. Its color is converted as a glyph run's.  @param sink   The sink the paint function was given. @param rect   The rectangle, relative to the content box's top left. @param color  Its color, sRGB-encoded with straight alpha. @return `mui_success`; `mui_errorInvalid` for a NULL sink, a rectangle not finite or of a negative size, or a color outside 0 to 1; `mui_errorCapacity` when the list needs more commands than the context's limits, which fails the build. @par Thread safety Safe from any thread; the sink is used by one thread at a time, and only during the call of the paint function given it.

```c
MUI_NODISCARD MUI_API muiResult muiBuildDrawList(muiContext* context, muiNodeId rootId, const muiDrawInput* input);
```
Paints a root's subtree, as its last muiComputeLayout left it, into the context's list, and clears the subtree's paint requests. When nothing below the root asked for paint since the last build of the same root, surface and scale, the list stays as it is, generation and all; otherwise subtrees nothing asked to repaint, at the origin and opacity they were painted at, copy their commands from the last list, which gives the bytes a build from nothing would. Per node, in paint order: its outer shadow, its box, its inner shadow, its image and what the paint function adds for host content, then its children, depth first; a node that clips draws its host content and its children inside its rounded border box. Opacity multiplies down the subtree into every command's colors. At the identity transform, box and image edges and clips snap to device pixels, and border widths to whole device pixels, at least one.  @param context  The context. @param rootId   The root. @param input    The surface, the scale and the paint function. @return `mui_success`; `mui_errorInvalid` for a NULL argument, the null id, a scale that is not a finite number above 0, or a call from a measure or paint function; `mui_errorStale` for a root that is gone; `mui_errorCapacity` when the list needs more commands, clips, gradients or glyphs than the context's limits, which leaves the list empty. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiGetDrawList(const muiContext* context, muiDrawList* listOut);
```
Shows the context's last list.  @param context  The context. @param listOut  Receives the list; empty before any build. @return `mui_success`; `mui_errorInvalid` for a NULL argument. @par Thread safety Safe from any thread; the context is used by one thread at a time.

## `event.h`

Routed input (record mui-0007): keys, text and navigation go to a player's focus, pointer records to their node, each along the target's ancestors, top down and then back up, through one function of the host's that says whether it handled the event. What the UI leaves unhandled is the game's.

```c
MUI_NODISCARD MUI_API muiResult muiSetEventFunction(muiContext* context, muiEventFunction function, void* user);
```
Sets the function routed events go to; NULL routes nothing, so every input is unhandled but for the library's defaults.  @param context   The context. @param function  The function, or NULL. @param user      Passed to the function. @return `mui_success`; `mui_errorInvalid` for a NULL context or a call from a measure, paint or event function. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiKeyInput(muiContext* context, muiNodeId rootId, const muiKeyEvent* event, bool* handledOut);
```
Routes a key to the player's focus under the root (a focus a modal layer covers: the top modal layer under the root; none: the root). Unhandled, a key down does what the library does by default: Tab, with Shift or not and no other modifier, moves the focus as muiFocus_Move does. Escape cancels every drag going (maul-ui/pointer.h). On a focused range (maul-ui/range.h), the unmodified keys of ARIA's slider pattern change its value: arrows along its axis a step, Page Up and Down a page, Home and End the ends. Else an arrow without modifiers, when the focus is in a scroll container along its axis (maul-ui/scroll.h), moves the focus to the candidate directional navigation finds inside it if that lies within half a scrollport of the visible part (or a link leads out), else steps the container a line while it can move, as Android's ScrollView does; otherwise it moves the focus as muiFocus_MoveToward does. Page Up and Down, Home and End without modifiers, and Space with Shift or not and no other modifier, step the vertical scroll container holding the focus a page or to an end. What moves is handled. A key down makes the player's next focus by code shown.  @param context     The context. @param rootId      The root of the subtree the player's input is for. @param event       The key: a player below MUI_MAX_PLAYERS. @param handledOut  Receives whether the UI handled it; what it did not is the game's. @return `mui_success`; `mui_errorInvalid` for a NULL argument, the null id, an event outside the above, or a call from a measure, paint or event function; `mui_errorStale` for a root that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiTextInput(muiContext* context, muiNodeId rootId, const muiTextEvent* event, bool* handledOut);
```
Routes text to the player's focus under the root, as muiKeyInput routes a key; text has no default.  @param context     The context. @param rootId      The root. @param event       The text: non-NULL when its length is not 0, a player below MUI_MAX_PLAYERS. @param handledOut  Receives whether the UI handled it. @return As muiKeyInput's. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNavigationInput(muiContext* context, muiNodeId rootId, const muiNavigationEvent* event, bool* handledOut);
```
Routes a navigation action to the player's focus under the root, as muiKeyInput routes a key. Unhandled, the four directions do what arrows do, a focused range's included, next and previous move the focus as muiFocus_Move does; a move is handled. Activate and cancel have no default. It makes the player's next focus by code shown.  @param context     The context. @param rootId      The root. @param event       The action: a known one, a player below MUI_MAX_PLAYERS. @param handledOut  Receives whether the UI handled it. @return As muiKeyInput's. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiWheelInput(muiContext* context, muiNodeId rootId, const muiWheelEvent* event, bool* handledOut);
```
Routes a wheel turn to the node under its point, as muiHitTest finds it; a point over nothing is routed nowhere and not handled. Unhandled, it scrolls by the scroll rule's step a detent (maul-ui/scroll.h): the scroll container it scrolled last, while turns keep coming within the rule's latch time and the point stays over that container; otherwise the nearest scroll container from the node up that can move that way, not past the root of the node's layer. Shift turns a vertical-only turn horizontal, as on Windows. A turn a scroll container takes is handled, even at its end while latched.  @param context     The context. @param rootId      The root. @param event       The turn: a finite point and deltas, a player below MUI_MAX_PLAYERS. @param handledOut  Receives whether the UI handled it. @return As muiKeyInput's. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiDispatchPointerRecord(muiContext* context, const muiPointerRecord* record, bool* handledOut);
```
Routes a pointer record (muiNextPointerRecord) to its node; a record with no node, or one gone, is routed nowhere and not handled. Unhandled, a press on a range or inside it, and a drag of one or of a node inside it, change its value (maul-ui/range.h), and are handled. The host hands what is not handled and passes through to the game.  @param context     The context. @param record      The record. @param handledOut  Receives whether the UI handled it. @return `mui_success`; `mui_errorInvalid` for a NULL argument or a call from a measure, paint or event function. @par Thread safety Safe from any thread; the context is used by one thread at a time.

## `exit.h`

Exit transitions (record mui-0007): a node the host removes plays its way out before it goes. Beginning an exit gives the node the exiting state, so its classes' exiting variants and their transitions apply; the node and its subtree leave hit testing, focus and navigation at once, and focus inside it is given up. When no transition runs in the subtree any more, at a layout, mui_notificationExitFinished reports it, once; the library never destroys the node. The host destroys it then, or earlier, which ends the exit, or cancels the exit to bring it back.

```c
MUI_NODISCARD MUI_API muiResult muiNode_BeginExit(muiContext* context, muiNodeId nodeId);
```
Begins a node's exit; nothing for a node already exiting.  @param context  The context. @param nodeId   The node. @return `mui_success`; `mui_errorCapacity` when the context holds its limit of exits; `mui_errorInvalid` for a NULL context, the null id or a call from a measure or paint function; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_CancelExit(muiContext* context, muiNodeId nodeId);
```
Cancels a node's exit: the exiting state goes, and the node and its subtree take input again; nothing for a node not exiting.  @param context  The context. @param nodeId   The node. @return `mui_success`; `mui_errorInvalid` for a NULL context, the null id or a call from a measure or paint function; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

## `focus.h`

Focus (record mui-0007): the node each player's keys and gamepad go to, one per player slot for local multiplayer, moved by code, pointer presses and sequential navigation, and shown as the input that moved it calls for.

```c
MUI_NODISCARD MUI_API muiResult muiFocus_Set(muiContext* context, uint8_t player, muiNodeId nodeId, muiFocusCause cause);
```
Moves a player's focus to a node, or takes it away. A node takes focus when its focus mode (as its last style resolution or direct write left it) is not mui_focusNone, it is neither disabled nor exiting, and no modal layer covers it. The node gets mui_stateFocused, and mui_stateFocusVisible when the focus is shown; a focus that moves posts mui_notificationFocusLost for the node it leaves and mui_notificationFocusGained for the one it reaches, with the player as the count.  @param context  The context. @param player   The player, below MUI_MAX_PLAYERS. @param nodeId   The node; the null id takes the focus away. @param cause    What moved it. @return `mui_success`; `mui_errorInvalid` for a NULL context, a player or cause outside the above, a node that takes no focus or a call from a measure or paint function, which changes nothing; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
muiNodeId muiFocus_Get(const muiContext* context, uint8_t player);
```
Returns the node a player focuses.  @param context  The context. @param player   The player. @return The node; the null id for none, a NULL context or a player past MUI_MAX_PLAYERS. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiFocus_Move(muiContext* context, muiNodeId rootId, uint8_t player, bool backward);
```
Moves a player's focus to the next or previous node in sequential order (Tab and Shift+Tab), shown. The order runs over the nodes whose focus mode is mui_focusAll in the layer that holds the focus: a layer's subtree, or the root's subtree, without the layers in it; those with a tab order of 1 to 255 first, ascending, then the rest, ties in tree order; it wraps. With no focus under the root, or one a modal layer covers, it starts in the top modal layer under the root, or else the root's own content.  @param context   The context. @param rootId    The root of the subtree the player navigates. @param player    The player. @param backward  Whether to go to the previous node. @return `mui_success`; `mui_empty` when no node there takes focus, which leaves it; `mui_errorInvalid` for a NULL context, the null id, a player past MUI_MAX_PLAYERS or a call from a measure or paint function; `mui_errorStale` for a root that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiFocus_MoveToward(muiContext* context, muiNodeId rootId, uint8_t player, muiDirection direction);
```
Moves a player's focus toward a direction on the screen (arrow keys, a gamepad's pad or stick), shown. A link the focused node has for the direction (muiNode_SetNeighbor) to a node that takes focus wins; a link to the node itself stops the move. Otherwise the nearest node in the direction is found as Android's focus search finds it, among the nodes sequential navigation reaches in the same layer: nodes overlapping the focus across the direction first, then the least of 13 times the square of the gap along the direction plus the square of the distance between centers across it, ties to the earlier in tree order. Boxes are as the last muiComputeLayout left them. With no focus under the root, or one a modal layer covers, it moves as muiFocus_Move does forward.  @param context    The context. @param rootId     The root of the subtree the player navigates. @param player     The player. @param direction  The direction. @return `mui_success`; `mui_empty` when nothing lies that way, which leaves the focus; `mui_errorInvalid` for a NULL context, the null id, a player past MUI_MAX_PLAYERS, an unknown direction or a call from a measure or paint function; `mui_errorStale` for a root that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_SetNeighbor(muiContext* context, muiNodeId nodeId, muiDirection direction, muiNodeId targetId);
```
Links a node to the node directional navigation moves to from it in a direction, over what geometry would find; the node itself stops movement that way. Links to nodes that do not take focus when the move is made leave it to geometry.  @param context    The context. @param nodeId     The node. @param direction  The direction. @param targetId   The node to move to; the null id removes the link. @return `mui_success`; `mui_errorInvalid` for a NULL context, the null id as the node, an unknown direction or a call from a measure or paint function; `mui_errorStale` for a node or target that is gone; `mui_errorCapacity` past the context's neighbors limit. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
muiNodeId muiNode_GetNeighbor(const muiContext* context, muiNodeId nodeId, muiDirection direction);
```
Returns the node a node links to in a direction.  @param context    The context. @param nodeId     The node. @param direction  The direction. @return The target, which may be gone since; the null id for no link, a NULL context, a node that is gone or an unknown direction. @par Thread safety Safe from any thread; the context is used by one thread at a time.

## `font.h`

Fonts of a text service (record mui-0006): TrueType and OpenType fonts and collections, from memory. Font files are hostile input: a font is validated when it is created, and compressed web fonts (WOFF, WOFF2) are refused.

```c
muiFontDef muiDefaultFontDef(void);
```
Returns the default font def: no data, face 0, copied.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MUI_NODISCARD MUI_API muiResult muiCountFontFaces(const void* data, size_t size, uint32_t* countOut);
```
Counts the faces of a font file without reading them: 1 for a single font, the number a collection declares for a collection. Allocates nothing.  @param data        The file's bytes. @param size        Their count. @param countOut    Receives the count; 0 on failure. @return `mui_success`; `mui_errorInvalid` for a NULL argument; `mui_errorFormat` for data that is not a TrueType or OpenType font or collection, or a collection whose header does not fit in size. @par Thread safety Safe from any thread.

```c
MUI_NODISCARD MUI_API muiResult muiCreateFont(muiTextService* service, const muiFontDef* def, muiFontId* fontOut);
```
Creates a font from a face of a font file, validating it.  @param service  The service. @param def      The font: a valid cookie, data and a size from 12 to 2^31 - 1, and a data mode above. @param fontOut  Receives the font; the null id on failure. @return `mui_success`; `mui_errorInvalid` for a NULL argument, a bad cookie, a size out of range or an unknown data mode; `mui_errorFormat` for data that is not a font the service reads, a face index past the file's faces, or a font that fails validation; `mui_errorCapacity` past the font limit or when memory runs out. @par Thread safety Safe from any thread; the service is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiDestroyFont(muiTextService* service, muiFontId fontId);
```
Destroys a font, which releases a copy of its bytes or ends the borrow of the caller's.  @param service  The service. @param fontId   The font. @return `mui_success`; `mui_errorInvalid` for a NULL service or the null id; `mui_errorStale` for a font that is gone. @par Thread safety Safe from any thread; the service is used by one thread at a time.

```c
bool muiFont_IsValid(const muiTextService* service, muiFontId fontId);
```
Tells whether an id names a font of the service that still exists.  @param service  The service, or NULL. @param fontId   The id. @return true for a live font; false otherwise. @par Thread safety Safe from any thread; the service is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiFont_GetMetrics(const muiTextService* service, muiFontId fontId, muiFontMetrics* metricsOut);
```
Reads a font's metrics.  @param service     The service. @param fontId      The font. @param metricsOut  Receives the metrics; unchanged on failure. @return `mui_success`; `mui_errorInvalid` for a NULL argument or the null id; `mui_errorStale` for a font that is gone. @par Thread safety Safe from any thread; the service is used by one thread at a time.

```c
muiFontFamilyDef muiDefaultFontFamilyDef(void);
```
Returns the default font family def: no faces and no fallbacks.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MUI_NODISCARD MUI_API muiResult muiCreateFontFamily(muiTextService* service, const muiFontFamilyDef* def, muiFontFamilyId* familyOut);
```
Creates a family of fonts, which a text style names by its key (muiFontFamily_GetKey): text is laid out in the face the style's weight and slant choose, as CSS matches faces (width nearest to normal, then italic, oblique and normal faces in CSS's order for the slant, then the weight in CSS's order), then in the instance of it they make. A face destroyed later is passed over.  @param service    The service. @param def        The family: a valid cookie, from 1 to 256 faces and up to 8 fallbacks, each the key of a font or a family. @param familyOut  Receives the family; the null id on failure. @return `mui_success`; `mui_errorInvalid` for a NULL argument, a bad cookie, NULL faces or fallbacks with a count, a count out of range, or a fallback that is no font's or family's key; `mui_errorStale` for a face or fallback that is gone, or the null id; `mui_errorCapacity` past the family limit or when memory runs out. @par Thread safety Safe from any thread; the service is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiDestroyFontFamily(muiTextService* service, muiFontFamilyId familyId);
```
Destroys a family; its faces stay.  @param service   The service. @param familyId  The family. @return `mui_success`; `mui_errorInvalid` for a NULL service or the null id; `mui_errorStale` for a family that is gone. @par Thread safety Safe from any thread; the service is used by one thread at a time.

```c
bool muiFontFamily_IsValid(const muiTextService* service, muiFontFamilyId familyId);
```
Tells whether an id names a family of the service that still exists.  @param service   The service, or NULL. @param familyId  The id. @return true for a live family; false otherwise. @par Thread safety Safe from any thread; the service is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiFontFamily_MatchFace(const muiTextService* service, muiFontFamilyId familyId, float weight, uint8_t slant, muiFontId* faceOut);
```
Finds the face of a family a weight and slant choose, as text laid out in the family is.  @param service   The service. @param familyId  The family. @param weight    From 1 to 1000. @param slant     A muiFontSlant (maul-ui/text_style.h). @param faceOut   Receives the face; unchanged on failure. @return `mui_success`; `mui_errorInvalid` for a NULL argument, the null id, or a weight or slant out of range; `mui_errorStale` for a family that is gone or whose faces all are. @par Thread safety Safe from any thread; the service is used by one thread at a time.

## `glyph_atlas.h`

Glyph atlases (record mui-0006): glyph images of a text service's fonts, rendered as muiRenderGlyph and muiRenderGlyphField render them and packed into pages of 8-bit pixels a renderer uploads as textures; a page may hold coverage and distance fields both. Pages are split into plots; when no plot has room, the least recently used plot that the current frame has not used is emptied and packed again. The atlas keeps the pages' pixels and tells which rectangles changed; it uses no graphics API.

```c
muiGlyphAtlasDef muiDefaultGlyphAtlasDef(void);
```
Returns the default atlas def: pages of 1,024 by 1,024 in plots of 256 by 256, at most 4 pages.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MUI_NODISCARD MUI_API muiResult muiCreateGlyphAtlas(muiTextService* service, const muiGlyphAtlasDef* def, muiGlyphAtlas** atlasOut);
```
Creates an atlas of a text service's glyphs, in the service's memory. It is destroyed before the service.  @param service   The service. @param def       The atlas: a valid cookie; pages from 64 to 16,384 pixels a side; plots from 16 to 4,096 pixels a side that divide the pages, at most 4,096 of them a page; from 1 to 64 pages. @param atlasOut  Receives the atlas; set to NULL on failure. @return `mui_success`; `mui_errorInvalid` for a NULL argument, a bad cookie or a size out of range; `mui_errorCapacity` when memory runs out. @par Thread safety Safe from any thread; the service is used by one thread at a time.

```c
void muiDestroyGlyphAtlas(muiGlyphAtlas* atlas);
```
Destroys an atlas and its pages.  @param atlas  The atlas, or NULL for nothing. @par Thread safety Safe from any thread; the atlas is used by one thread at a time.

```c
void muiGlyphAtlas_NextFrame(muiGlyphAtlas* atlas);
```
Starts a frame: glyphs got before it may be evicted to make room.  @param atlas  The atlas, or NULL for nothing. @par Thread safety Safe from any thread; the atlas is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiGlyphAtlas_Get(muiGlyphAtlas* atlas, uint64_t font, uint32_t glyph, float pixelSize, float penX, float baselineY, muiAtlasGlyph* glyphOut);
```
Gets a glyph's image for a pen in device pixels, rendering and packing it the first time. The pen's x is taken to the nearest quarter pixel and its baseline to the nearest pixel; the font key 0 is the default font's. The glyph's plot is kept until a later frame.  @param atlas      The atlas. @param font       A font key, as a glyph run carries. @param glyph      A glyph id of the font. @param pixelSize  The em in device pixels, as muiRenderGlyph takes. @param penX       The pen, in device pixels, within 2^24 of 0. @param baselineY  The baseline, likewise. @param glyphOut   Receives the image; when the image is larger than a plot, its width and height only. @return `mui_success`; `mui_errorCapacity` when the image is larger than a plot, every plot is in use this frame, or memory runs out; `mui_errorInvalid` for a NULL atlas or glyphOut, a size or position out of range, or a glyph id the font lacks; `mui_errorStale` for a key that names no font; `mui_errorFormat` for a glyph that cannot be rendered. @par Thread safety Safe from any thread; the atlas is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiGlyphAtlas_GetField(muiGlyphAtlas* atlas, uint64_t font, uint32_t glyph, float pixelSize, uint32_t spread, muiAtlasGlyph* glyphOut);
```
Gets a glyph's distance field, rendering and packing it the first time, as muiRenderGlyphField renders it; the font key 0 is the default font's. A renderer draws it at any size s by scaling the image and its place, x and y from the pen and baseline, by s / pixelSize. The glyph's plot is kept until a later frame.  @param atlas      The atlas. @param font       A font key, as a glyph run carries. @param glyph      A glyph id of the font. @param pixelSize  The em in pixels of the field, as muiRenderGlyphField takes. @param spread     How far the field reaches past the outline, from MUI_MIN_FIELD_SPREAD to MUI_MAX_FIELD_SPREAD pixels. @param glyphOut   Receives the image; when the image is larger than a plot, its width and height only. @return As muiGlyphAtlas_Get, with a spread out of range invalid. @par Thread safety Safe from any thread; the atlas is used by one thread at a time.

```c
uint32_t muiGlyphAtlas_GetPageCount(const muiGlyphAtlas* atlas);
```
Returns how many pages the atlas has made.  @param atlas  The atlas, or NULL for 0. @return The page count; pages are numbered from 0. @par Thread safety Safe from any thread; the atlas is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiGlyphAtlas_GetPage(const muiGlyphAtlas* atlas, uint32_t page, muiAtlasPage* pageOut);
```
Reads a page's pixels, which stay valid until the atlas is destroyed and change only in calls to muiGlyphAtlas_Get.  @param atlas    The atlas. @param page     A page the atlas has made. @param pageOut  Receives the page; unchanged on failure. @return `mui_success`; `mui_errorInvalid` for a NULL argument or a page not made. @par Thread safety Safe from any thread; the atlas is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiGlyphAtlas_TakeUpdates(muiGlyphAtlas* atlas, muiAtlasUpdate* updates, uint32_t capacity, uint32_t* countOut);
```
Takes the rectangles of pages that changed since the last call, a plot's changes as one rectangle, for the renderer to copy from the pages' pixels.  @param atlas     The atlas. @param updates   Receives the rectangles; may be NULL when capacity is 0. @param capacity  How many updates holds. @param countOut  Receives how many rectangles there are, also when updates is too small. @return `mui_success`, taking them; `mui_errorCapacity` when updates holds fewer, taking none; `mui_errorInvalid` for a NULL atlas or countOut, or NULL updates with a capacity. @par Thread safety Safe from any thread; the atlas is used by one thread at a time.

## `glyph_image.h`

Glyph images (record mui-0006): a glyph of a text service's font rendered for a renderer to draw, as 8-bit coverage or as a signed distance field that scales. Outlines are rendered unhinted, as text is laid out, so an image sits where its glyph run places it at every size; images are the same on every platform.

```c
MUI_NODISCARD MUI_API muiResult muiRenderGlyph(muiTextService* service, uint64_t font, uint32_t glyph, float pixelSize, float offsetX, muiGlyphImage* imageOut, unsigned char* pixels, size_t capacity);
```
Renders a glyph as coverage: a byte per pixel, rows from the top, 0 outside the outline to 255 inside, linear in the area covered (a renderer applies any gamma). A glyph with no outline, such as a space, has an empty image. A glyph run's glyph at (x, y) from its origin, drawn at a scale, has its pen at (originX + x) * scale and its baseline at (originY + y) * scale, y rounded to a pixel; its em is the run's size times the scale.  @param service    The service. @param font       A font key, as a glyph run carries; 0 for the default font. @param glyph      A glyph id of the font. @param pixelSize  The em in device pixels, from 1/64 to MUI_MAX_GLYPH_PIXEL_SIZE. @param offsetX    How far the pen is right of a pixel boundary, from 0 up to 1; the image's left is counted from that boundary. @param imageOut   Receives the image's place and size, also when pixels hold too few bytes. @param pixels     Receives width * height bytes; may be NULL when capacity is 0. @param capacity   How many bytes pixels holds. @return `mui_success`; `mui_errorCapacity` when pixels hold fewer bytes than imageOut asks for, or memory runs out; `mui_errorInvalid` for a NULL service or imageOut, NULL pixels with a capacity, a size or offset outside the above, or a glyph id the font does not have; `mui_errorStale` for a key that names no font; `mui_errorFormat` for a glyph whose outline cannot be read or is too large to render. @par Thread safety Safe from any thread; the service is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiRenderGlyphField(muiTextService* service, uint64_t font, uint32_t glyph, float pixelSize, uint32_t spread, muiGlyphImage* imageOut, unsigned char* pixels, size_t capacity);
```
Renders a glyph as a signed distance field, which a renderer scales to any size: a byte per pixel, rows from the top, 128 at the outline, and 128 / spread more for each pixel inside and less for each pixel outside, up to 255 and down to 0. The image reaches spread pixels past the outline on every side; drawn at a scale, it covers where a sample is 128 or more. Glyphs whose contours overlap, as variable fonts' and composite glyphs' may, are rendered as their union.  @param service    The service. @param font       A font key, as a glyph run carries; 0 for the default font. @param glyph      A glyph id of the font. @param pixelSize  The em in pixels of the image, from 1/64 to MUI_MAX_GLYPH_PIXEL_SIZE. @param spread     How far the field reaches past the outline, from MUI_MIN_FIELD_SPREAD to MUI_MAX_FIELD_SPREAD pixels. @param imageOut   Receives the image's place and size, from the pen and baseline at pixelSize, also when pixels hold too few bytes. @param pixels     Receives width * height bytes; may be NULL when capacity is 0. @param capacity   How many bytes pixels holds. @return `mui_success`; `mui_errorCapacity` when pixels hold fewer bytes than imageOut asks for, or memory runs out; `mui_errorInvalid` for a NULL service or imageOut, NULL pixels with a capacity, a size or spread outside the above, or a glyph id the font does not have; `mui_errorStale` for a key that names no font; `mui_errorFormat` for a glyph whose outline cannot be read or is too large to render. @par Thread safety Safe from any thread; the service is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiRenderGlyphMultiField( muiTextService* service, uint64_t font, uint32_t glyph, float pixelSize, uint32_t spread, muiGlyphImage* imageOut, unsigned char* pixels, size_t capacity);
```
Renders a glyph's multi-channel signed distance field (MTSDF) for a renderer that scales it: four bytes a pixel. Red, green and blue are three distances, each to the outline's edges of one colour, and their median is the distance with the outline's corners kept sharp at any scale (Chlumský's method, as msdfgen's); alpha is the field muiRenderGlyphField renders, byte for byte, for outlines, glows and shadows. Each channel is 128 at its distance's 0 and 128 / spread more for each pixel inside, less outside, held within 0 and 255. The image is placed and sized as muiRenderGlyphField places it, and overlapping contours are their union.  @param service    The service. @param font       A font key, as a glyph run carries; 0 for the default font. @param glyph      A glyph id of the font. @param pixelSize  The em in pixels of the image, from 1/64 to MUI_MAX_GLYPH_PIXEL_SIZE. @param spread     How far the field reaches past the outline, from MUI_MIN_FIELD_SPREAD to MUI_MAX_FIELD_SPREAD pixels. @param imageOut   Receives the image's place and size, also when pixels hold too few bytes. @param pixels     Receives width * height * 4 bytes, red, green, blue and alpha a pixel; may be NULL when capacity is 0. @param capacity   How many bytes pixels holds. @return As muiRenderGlyphField. @par Thread safety Safe from any thread; the service is used by one thread at a time.

## `interaction.h`

Interaction properties and hit testing (record mui-0007): whether a node and its children are hit by a point, whether input it leaves unused passes through to what lies behind the UI, whether it roots a layer, and which node is topmost at a point, in reverse paint order.

```c
muiInteractionStyle muiDefaultInteractionStyle(void);
```
Returns the default interaction values: hit in full, blocking, no layer, taking no focus.  @return The values. @par Thread safety Safe from any thread.

```c
MUI_NODISCARD MUI_API muiResult muiStyle_SetInteractionValues(muiContext* context, muiStyleId styleId, muiVariant variant, const muiInteractionStyle* values, muiPropertyMask mask);
```
Sets interaction values of one variant of a class, as muiStyle_SetLayoutValues does layout ones.  @param context  The context. @param styleId  The class. @param variant  The variant. @param values   The values; only the fields mask names are read: a known hit mode, layer kind and focus mode. @param mask     The properties, within MUI_INTERACTION_PROPERTIES. @return `mui_success`; `mui_errorInvalid` for a NULL argument, the null id, an unknown variant or property bit, a value outside the above or a call from a measure or paint function, which changes nothing; `mui_errorStale` for an id whose class is gone; `mui_errorCapacity` when the variant had no values and the context's limit of property sets is reached. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiStyle_GetInteractionValues(const muiContext* context, muiStyleId styleId, muiVariant variant, muiInteractionStyle* valuesOut, muiPropertyMask* maskOut);
```
Reads the interaction values one variant of a class sets.  @param context    The context. @param styleId    The class. @param variant    The variant. @param valuesOut  Receives the set values, and muiDefaultInteractionStyle's for the rest. @param maskOut    Receives which interaction properties are set. @return `mui_success`; `mui_errorInvalid` for a NULL argument, the null id or an unknown variant; `mui_errorStale` for an id whose class is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_SetInteractionValues(muiContext* context, muiNodeId nodeId, const muiInteractionStyle* values, muiPropertyMask mask);
```
Writes interaction properties of a node directly, as muiNode_SetLayoutValues does layout ones; neither its layout nor its paint is redone.  @param context  The context. @param nodeId   The node. @param values   The values, as muiStyle_SetInteractionValues takes them. @param mask     The properties, within MUI_INTERACTION_PROPERTIES. @return `mui_success`; `mui_errorInvalid` for a NULL argument, the null id, an unknown property bit, a value outside the above or a call from a measure or paint function, which changes nothing; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_GetInteractionStyle(const muiContext* context, muiNodeId nodeId, muiInteractionStyle* valuesOut);
```
Reads a node's resolved interaction values: its direct writes, and for the other properties what its classes and states gave at the last muiComputeLayout that reached it.  @param context    The context. @param nodeId     The node. @param valuesOut  Receives the values. @return `mui_success`; `mui_errorInvalid` for a NULL argument or the null id; `mui_errorStale` for an id whose node is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiHitTest(const muiContext* context, muiNodeId rootId, float x, float y, muiHit* hitOut);
```
Finds the topmost node of a root's subtree at a point, as its last muiComputeLayout left it: the last in paint order whose rounded border box holds the point, inside the rounded clips of every ancestor that clips and of no node whose hit mode leaves it out. Layers are tried from the top down, then the content they are not in; a point a modal layer's subtree misses hits the modal layer's root, blocked, and nothing below it. Opacity does not matter, as in CSS. Positions are those painting gives, the root at its own rectangle, through local scales: a node scaled to nothing on an axis is hit nowhere.  @param context  The context. @param rootId   The root of the subtree. @param x        The point, in the space the root's rectangle is in. @param y        Likewise. @param hitOut   Receives what the point hits; unchanged on failure. @return `mui_success`; `mui_errorInvalid` for a NULL argument, the null id or a point not finite; `mui_errorStale` for a root that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_RaiseLayer(muiContext* context, muiNodeId nodeId);
```
Raises a layer above the others of its band, as activating a window brings it to the front: it becomes the latest activated.  @param context  The context. @param nodeId   A node that roots a layer, as its last style resolution or direct write left it. @return `mui_success`; `mui_errorInvalid` for a NULL context, the null id, a node that roots no layer or a call from a measure or paint function; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

## `layout.h`

Layout: the authored values that size and place a node, the solver that computes rectangles from them (CSS Flexbox, record mui-0003), and the rectangles it publishes. Lengths are logical units; a node's rectangle is relative to its parent's border box, and a root's rectangle starts at 0, 0.

```c
muiLayoutStyle muiDefaultLayoutStyle(void);
```
Returns the default layout style: CSS's initial values (row, one line, no grow, shrink 1, automatic basis and sizes, stretched items and lines, start), no margins, borders or padding, and no content.  @return The style. @par Thread safety Safe from any thread.

```c
MUI_NODISCARD MUI_API muiResult muiNode_SetLayoutStyle(muiContext* context, muiNodeId nodeId, const muiLayoutStyle* style);
```
Writes every layout property of a node directly, so that they win over its style classes until reset (muiNode_SetLayoutValues in maul-ui/style.h writes some). The node and its parent are laid out again at the next muiComputeLayout.  @param context  The context. @param nodeId   The node. @param style    The values: finite numbers, grow and shrink, padding, border and gaps at least 0, known enumerators and edge bits, alignItems not mui_alignAuto, and anchors from 0 to 1, and an aspect ratio of 0 or more. @return `mui_success`; `mui_errorInvalid` for a NULL argument, the null id, a value outside the above, or a call from a measure function; `mui_errorStale` for an id whose node is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_GetLayoutStyle(const muiContext* context, muiNodeId nodeId, muiLayoutStyle* styleOut);
```
Reads a node's resolved layout values: its direct writes, and for the other properties what its classes and states gave at the last muiComputeLayout that reached it.  @param context   The context. @param nodeId    The node. @param styleOut  Receives the values. @return `mui_success`; `mui_errorInvalid` for a NULL argument or the null id; `mui_errorStale` for an id whose node is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
bool muiNode_IsRightToLeft(const muiContext* context, muiNodeId nodeId);
```
Whether a node's content runs right to left: the direction its own resolved layout values give, else the nearest ancestor's that gives one, else left to right. It reads the values as they are at the call, so a measure function sees the direction layout uses.  @param context  The context. @param nodeId   The node. @return true for right to left; false for left to right, a NULL context, the null id or a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_MarkContentChanged(muiContext* context, muiNodeId nodeId);
```
Tells the solver a node's host content changed size, so it is measured again at the next muiComputeLayout.  @param context  The context. @param nodeId   The node. @return `mui_success`; `mui_errorInvalid` for a NULL context, the null id or a call from a measure or paint function; `mui_errorStale` for an id whose node is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiComputeLayout(muiContext* context, muiNodeId rootId, const muiLayoutInput* input);
```
Lays out a root and its subtree in the given space. Subtrees that did not change since the last call are not visited.  @param context  The context. @param rootId   A root: a node without a parent. @param input    The space and the measure function. @return `mui_success`; `mui_errorInvalid` for a NULL argument, the null id, a node with a parent, a negative or non-finite space, or a call from a measure or paint function; `mui_errorStale` for an id whose node is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
bool muiIsUpdatePending(const muiContext* context, muiNodeId rootId);
```
Returns whether muiComputeLayout on a root has work to do: an edit below it since its last run, a node whose conditions read a size or direction that run changed, which a following run styles again, a transition running below it, or a scroll step easing below it (maul-ui/scroll.h).  @param context  The context. @param rootId   The root. @return Whether work is pending; false for a stale id or a NULL context. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
muiRect muiNode_GetRect(const muiContext* context, muiNodeId nodeId);
```
Returns a node's border box from the last muiComputeLayout that reached it, relative to its parent's border box.  @param context  The context. @param nodeId   The node. @return The rectangle; all zero before any layout, for a stale id or a NULL context. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
muiRect muiNode_GetContentRect(const muiContext* context, muiNodeId nodeId);
```
Returns a node's content box from the last muiComputeLayout that reached it, relative to its border box: inside its border and padding, the start's on the right in a right-to-left node, as its paint function and its text's carets are given it.  @param context  The context. @param nodeId   The node. @return The rectangle; all zero before any layout, for a stale id or a NULL context. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_MapToRoot(const muiContext* context, muiNodeId nodeId, float x, float y, float* xOut, float* yOut);
```
Carries a point of a node's border box into the space its topmost ancestor's rectangle is in, where pointer events are given, through its own and its ancestors' places, local scales and scroll containers' offsets as the last muiComputeLayout, styling and scrolling left them: so a host places a window's candidate box at a caret, or its own popup beside a node.  @param context  The context. @param nodeId   The node. @param x        The point, from the border box's top left. @param y        Likewise. @param xOut     Receives the point's x there; unchanged on failure. @param yOut     Likewise its y. @return `mui_success`; `mui_errorInvalid` for a NULL argument or a point not finite; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

## `node.h`

Nodes: the retained tree a host builds and edits through ids. A node without a parent is a root; each root is a separate tree.

```c
muiNodeDef muiDefaultNodeDef(void);
```
Returns the default node def: host key 0.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MUI_NODISCARD MUI_API muiResult muiCreateNode(muiContext* context, const muiNodeDef* def, muiNodeId* nodeIdOut);
```
Creates a node, a root until it is inserted into a parent.  @param context    The context. @param def        The node: a valid cookie. @param nodeIdOut  Receives the node's id; the null id on failure. @return `mui_success`; `mui_errorInvalid` for a NULL argument or a bad cookie; `mui_errorCapacity` when the context's node limit is reached. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiDestroyNode(muiContext* context, muiNodeId nodeId);
```
Destroys a node and its whole subtree, detaching it from its parent first. The ids of every destroyed node become stale.  @param context  The context. @param nodeId   The node. @return `mui_success`; `mui_errorInvalid` for a NULL context or the null id; `mui_errorStale` for an id whose node is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
bool muiNode_IsValid(const muiContext* context, muiNodeId nodeId);
```
Returns whether an id names a live node. A stale id is not misuse.  @param context  The context. @param nodeId   Any id. @return True for a live node; false otherwise and for a NULL context. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_InsertChild(muiContext* context, muiNodeId parentId, muiNodeId childId, muiNodeId beforeId);
```
Inserts a root into a parent, before one of the parent's children or last.  @param context   The context. @param parentId  The parent. @param childId   The child: a root that is not the parent or one of its ancestors. @param beforeId  A child of the parent to insert before, or the null id to append. @return `mui_success`; `mui_errorInvalid` for a NULL context, a null parent or child, a child that has a parent, a child that is the parent or one of its ancestors, or a before id that is not the parent's child; `mui_errorStale` for an id whose node is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_Detach(muiContext* context, muiNodeId nodeId);
```
Detaches a node from its parent, making it a root with its subtree. A root stays as it is.  @param context  The context. @param nodeId   The node. @return `mui_success`; `mui_errorInvalid` for a NULL context or the null id; `mui_errorStale` for an id whose node is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
muiNodeId muiNode_GetParent(const muiContext* context, muiNodeId nodeId);
```
Returns a node's parent.  @param context  The context. @param nodeId   The node. @return The parent; the null id for a root, a stale id or a NULL context. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
muiNodeId muiNode_GetFirstChild(const muiContext* context, muiNodeId nodeId);
```
Returns a node's first child.  @param context  The context. @param nodeId   The node. @return The first child; the null id for none, a stale id or a NULL context. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
muiNodeId muiNode_GetLastChild(const muiContext* context, muiNodeId nodeId);
```
Returns a node's last child.  @param context  The context. @param nodeId   The node. @return The last child; the null id for none, a stale id or a NULL context. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
muiNodeId muiNode_GetNextSibling(const muiContext* context, muiNodeId nodeId);
```
Returns the child after a node in its parent's order.  @param context  The context. @param nodeId   The node. @return The next sibling; the null id for the last child, a root, a stale id or a NULL context. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
muiNodeId muiNode_GetPreviousSibling(const muiContext* context, muiNodeId nodeId);
```
Returns the child before a node in its parent's order.  @param context  The context. @param nodeId   The node. @return The previous sibling; the null id for the first child, a root, a stale id or a NULL context. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
uint32_t muiNode_GetChildCount(const muiContext* context, muiNodeId nodeId);
```
Returns how many children a node has.  @param context  The context. @param nodeId   The node. @return The count; 0 for a stale id or a NULL context. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
uint64_t muiNode_GetHostKey(const muiContext* context, muiNodeId nodeId);
```
Returns the host key a node was created with.  @param context  The context. @param nodeId   The node. @return The key; 0 for a stale id or a NULL context. @par Thread safety Safe from any thread; the context is used by one thread at a time.

## `pointer.h`

Pointer input (record mui-0007): the host's pointer events become hover and press states for styling, pointer capture, and records of presses, releases, clicks and cancels the host takes after each call.

```c
MUI_NODISCARD MUI_API muiResult muiPointerInput(muiContext* context, muiNodeId rootId, const muiPointerEvent* event);
```
Takes a pointer event. Hit testing finds the node at the point, as muiHitTest does, unless the pointer is captured; then the pointer's hover is updated, and so is its press and capture, and records are posted for muiNextPointerRecord.  A node is in mui_stateHovered while a pointer's topmost node is it or one of its descendants, as CSS's :hover, and in mui_statePressed while a pointer that pressed it or a descendant holds a button, wherever the pointer went since, as :active; both join the states the host sets, and the node is styled again at the next muiComputeLayout. A touch hovers only in contact. Hover reads the last layout: after one, a move to the same point updates it.  @param context  The context. @param rootId   The root of the subtree under the pointer. @param event    The event: a known kind and action, a button below 8, a point that is finite, a player below MUI_MAX_PLAYERS. @return `mui_success`; `mui_errorInvalid` for a NULL argument, the null id, an event outside the above or a call from a measure, paint or event function, which changes nothing; `mui_errorStale` for a root that is gone; `mui_errorCapacity` for a new pointer past the context's pointers limit. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNextPointerRecord(muiContext* context, muiPointerRecord* recordOut);
```
Takes the oldest pointer record.  @param context    The context. @param recordOut  Receives the record. @return `mui_success`; `mui_empty` when none is waiting; `mui_errorInvalid` for a NULL argument. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiPointer_SetCapture(muiContext* context, uint32_t pointer, muiNodeId nodeId);
```
Sends every later event of a pointer that holds a button to a node, wherever it is, until its last button is released, it is cancelled, the capture is released or the node is destroyed. It hovers the node's chain from the next event on. A node that had the capture gets a capture-lost record.  @param context  The context. @param pointer  The host's id of the pointer. @param nodeId   The node. @return `mui_success`; `mui_errorInvalid` for a NULL context, the null id, a pointer the context does not know or that holds no button, or a call from a measure or paint function; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiPointer_ReleaseCapture(muiContext* context, uint32_t pointer);
```
Releases a pointer's capture, with a capture-lost record; its next event hit tests again.  @param context  The context. @param pointer  The host's id of the pointer. @return `mui_success`, also when it was not captured; `mui_errorInvalid` for a NULL context, a pointer the context does not know, or a call from a measure or paint function. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiPointer_GetState(const muiContext* context, uint32_t pointer, muiPointerState* stateOut);
```
Reads a pointer's state.  @param context   The context. @param pointer   The host's id of the pointer. @param stateOut  Receives the state. @return `mui_success`; `mui_empty` for a pointer the context does not know (it left, lifted or was cancelled); `mui_errorInvalid` for a NULL argument. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiSetClickRule(muiContext* context, uint64_t intervalNs, float distance);
```
Sets when presses count as one series (a double click): a press of the same button within intervalNs of the last and within distance of it on each axis. The defaults are 500 ms and 2, Windows's; hosts pass the platform's setting.  @param context     The context. @param intervalNs  The time between presses, in nanoseconds. @param distance    The distance on each axis, finite and at least 0. @return `mui_success`; `mui_errorInvalid` for a NULL context, a distance outside the above, or a call from a measure or paint function. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiPointer_Offer(muiContext* context, uint32_t pointer, uint32_t kind, uint64_t key);
```
Offers a thing to drop for a dragging pointer, from its drag start on: a kind, one or more of the application's bits, and the host's key for it. From then the drag looks for a target under the pointer, the nearest node from the one hit up whose accepts mask (maul-ui/interaction.h) shares a bit with the kind, and posts drop enter and leave records as it changes, and a drop record when the drag ends over one; a cancelled drag only leaves. A second offer replaces the first.  @param context  The context. @param pointer  The host's id of the pointer. @param kind     The kind: not 0. @param key      The host's key for the thing. @return `mui_success`; `mui_errorInvalid` for a NULL context, a pointer not dragging, a kind of 0, or a call from a measure or paint function. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiSetDragThreshold(muiContext* context, float mouse, float touch);
```
Sets how far a press moves before it becomes a drag, on either axis: for a mouse, and for touch and pens. The defaults are 4, Windows's, and 8, Android's touch slop; hosts pass the platform's.  @param context  The context. @param mouse    The distance for a mouse, finite and at least 0. @param touch    The distance for touch and pens, likewise. @return `mui_success`; `mui_errorInvalid` for a NULL context, a distance outside the above, or a call from a measure or paint function. @par Thread safety Safe from any thread; the context is used by one thread at a time.

## `popup.h`

Popups (record mui-0007): a node placed beside an anchor node after each layout. Its layer kind (maul-ui/interaction.h) says how it paints, usually mui_layerOverlay; this says where. From the anchor's border box on the surface, through scrolling, the popup's border box goes to the side asked, aligned along it, then on each axis flips to the opposite side when it overflows the root's box and the other side has more room, and is clamped into the root's box, its start edge kept when it cannot fit. Layout gives its size; an absolutely placed popup takes no room where it sits in the tree. A popup record stands for an open popup: the host sets it when it opens one and clears it, or destroys the node, when it closes. Light dismissal, as HTML's popovers have it, reports when one should close (mui_notificationPopupDismissed, the reason in its count); the host closes it, with whatever exit it likes. A popup anchored inside another nests under it. A pointer press dismisses every popup that neither holds the pressed node nor has its anchor holding it, nested ones first, and keeps the popups those nest under; an Escape no handler takes dismisses the popup set last; focus moved by code or navigation to a node outside a popup and its anchor dismisses it. Each is reported once, until the popup is set anew.

```c
muiPopup muiDefaultPopup(void);
```
The default popup: below the null anchor, start edges aligned, no gap or margin, dismissed lightly.  @return The popup. @par Thread safety Safe from any thread.

```c
MUI_NODISCARD MUI_API muiResult muiNode_SetPopup(muiContext* context, muiNodeId nodeId, const muiPopup* popup);
```
Makes a node a popup, or sets its popup anew; placed at the next layout, and dismissible again.  @param context  The context. @param nodeId   The node. @param popup    The popup: a live anchor other than the node, a known side and alignment, gap and margin as above. @return `mui_success`; `mui_errorCapacity` when the context holds its limit of popups; `mui_errorInvalid` for a NULL argument, the null id, a popup outside the above, or a call from a measure or paint function; `mui_errorStale` for a node or anchor that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_GetPopup(const muiContext* context, muiNodeId nodeId, muiPopup* popupOut);
```
Reads a node's popup.  @param context   The context. @param nodeId    The node. @param popupOut  Receives the popup. @return `mui_success`; `mui_empty` for a node that is not a popup; `mui_errorInvalid` for a NULL argument or the null id; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_GetPopupSide(const muiContext* context, muiNodeId nodeId, muiPopupSide* sideOut);
```
Reads the side a popup went to at the last layout that placed it, after flipping, so an arrow can point at the anchor.  @param context  The context. @param nodeId   The node. @param sideOut  Receives the side. @return `mui_success`; `mui_empty` for a node that is not a popup or has not been placed since it was set; `mui_errorInvalid` for a NULL argument or the null id; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_ClearPopup(muiContext* context, muiNodeId nodeId);
```
Makes a node no longer a popup; it keeps its last place until layout places it again.  @param context  The context. @param nodeId   The node. @return `mui_success`, whether or not it was one; `mui_errorInvalid` for a NULL context, the null id or a call from a measure or paint function; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

## `range.h`

Range values (record mui-0007): a value between a minimum and a maximum on a node, as a slider, a scrollbar or a volume control holds one. Unhandled input changes it by default: keys by ARIA's slider pattern along the range's axis (muiKeyInput, muiNavigationInput), and pointer records a host dispatches (muiDispatchPointerRecord): a press on the track pages toward the point, and a drag of the range (its node takes drags, maul-ui/interaction.h) moves the value with the pointer, keeping where the thumb was grabbed. A change made so is reported by mui_notificationRangeChanged; a change by code is not.

```c
muiValueRange muiDefaultValueRange(void);
```
The default range: 0 to 100 by steps of 1, a page of 10, horizontal, at 0, without a thumb.  @return The range. @par Thread safety Safe from any thread.

```c
MUI_NODISCARD MUI_API muiResult muiNode_SetValueRange(muiContext* context, muiNodeId nodeId, const muiValueRange* range);
```
Makes a node a range, or sets its range anew; the value is kept within the minimum and maximum and on a step.  @param context  The context. @param nodeId   The node. @param range    The range: finite numbers as described above. @return `mui_success`; `mui_errorCapacity` when the context holds its limit of ranges; `mui_errorInvalid` for a NULL argument, the null id, a range outside the above, or a call from a measure or paint function; `mui_errorStale` for a node or thumb that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_GetValueRange(const muiContext* context, muiNodeId nodeId, muiValueRange* rangeOut);
```
Reads a node's range.  @param context   The context. @param nodeId    The node. @param rangeOut  Receives the range. @return `mui_success`; `mui_empty` for a node that is not a range; `mui_errorInvalid` for a NULL argument or the null id; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_SetRangeValue(muiContext* context, muiNodeId nodeId, float value);
```
Sets a range's value, kept within its minimum and maximum and on a step.  @param context  The context. @param nodeId   The node. @param value    The value, finite. @return `mui_success`; `mui_empty` for a node that is not a range; `mui_errorInvalid` for a NULL context, the null id, a value not finite or a call from a measure or paint function; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_ClearValueRange(muiContext* context, muiNodeId nodeId);
```
Makes a node no longer a range.  @param context  The context. @param nodeId   The node. @return `mui_success`, whether or not it was one; `mui_errorInvalid` for a NULL context, the null id or a call from a measure or paint function; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

## `scroll.h`

Scrolling (record mui-0007): a scroll container's offset (layout.h's scrollAxes) and the extent its children reach. Offsets are logical: x runs from the inline start, so under right to left it grows leftward. Painting moves the children by the offset through a transform, and hit testing and navigation follow them.

```c
MUI_NODISCARD MUI_API muiResult muiNode_SetScroll(muiContext* context, muiNodeId nodeId, float x, float y);
```
Scrolls a node to an offset at once, stopping any step easing it: within 0 and its extent less its padding box along each axis it scrolls (0 along any other), as its last muiComputeLayout measured them; layout keeps it within them as sizes change. The next draw list moves the children.  @param context  The context. @param nodeId   The node. @param x        The offset from the inline start, finite. @param y        The offset from the top, finite. @return `mui_success`; `mui_errorInvalid` for a NULL context, the null id, an offset not finite or a call from a measure or paint function; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_GetScroll(const muiContext* context, muiNodeId nodeId, float* xOut, float* yOut);
```
Reads a node's scroll offset; 0 along an axis it does not scroll, as a node that stops scrolling along one drops its offset there.  @param context  The context. @param nodeId   The node. @param xOut     Receives the offset from the inline start. @param yOut     Receives the offset from the top. @return `mui_success`; `mui_errorInvalid` for a NULL argument or the null id; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_GetScrollExtent(const muiContext* context, muiNodeId nodeId, muiSize* extentOut);
```
Reads the extent a scroll container's children reach, as its last muiComputeLayout measured it: from its padding box's start to the furthest end of its children's margin boxes plus its end padding, at least its padding box; for a scrollbar, the padding box over the extent is the thumb's share.  @param context    The context. @param nodeId     The node. @param extentOut  Receives the extent; 0 by 0 for a node that does not scroll or was not laid out since it does. @return `mui_success`; `mui_errorInvalid` for a NULL argument or the null id; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_ScrollIntoView(muiContext* context, muiNodeId nodeId);
```
Scrolls each scrolling ancestor of a node at once, the nearest first, stopping steps easing them: the least that brings the node's border box into its padding box, as CSSOM View's scrollIntoView with "nearest" does per axis. A node already inside stays; one past the start edge and no larger than the box aligns its start, one past the end its end; a larger one past either edge aligns the other, and one past both stays. Directional and sequential navigation does this to the node it focuses.  @param context  The context. @param nodeId   The node. @return `mui_success`; `mui_errorInvalid` for a NULL context, the null id or a call from a measure or paint function; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
muiScrollRule muiDefaultScrollRule(void);
```
The default scroll rule: 100 a detent, Chrome's on Windows; 40 a line and 0.875 of the scrollport a page, Chrome's; a latch of 500 ms; steps easing out over 150 ms; flings keeping 0.998 of their speed a millisecond, iOS's normal rate; no overscroll. Hosts pass the platform's steps where it has them.  @return The rule. @par Thread safety Safe from any thread.

```c
MUI_NODISCARD MUI_API muiResult muiSetScrollRule(muiContext* context, const muiScrollRule* rule);
```
Sets the context's scroll rule.  @param context  The context. @param rule     The rule, as described above. @return `mui_success`; `mui_errorInvalid` for a NULL argument, a rule outside the above, or a call from a measure or paint function. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_GetScrollThumb(const muiContext* context, muiNodeId nodeId, bool horizontal, float track, float minimum, muiScrollThumb* thumbOut);
```
Places a scrollbar thumb for a node along an axis, as its last muiComputeLayout and its offset leave it: the track times the padding box over the extent, at least minimum (at most the track), and placed as the offset is between 0 and its limit. A node that cannot scroll that way fills the track.  @param context     The context. @param nodeId      The node. @param horizontal  The axis: true for x. @param track       The track's length, finite and at least 0. @param minimum     The shortest thumb, finite and at least 0. @param thumbOut    Receives the thumb. @return `mui_success`; `mui_errorInvalid` for a NULL argument, the null id or a length outside the above; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

## `style.h`

Style: classes of typed property values, the node types and node states that pick them, and a node's direct writes (record mui-0004). A node's values resolve in fixed layers, each later one winning: the defaults, every class's base values in order, the state variants (the states in the order of muiState, weakest first, and the classes in order within each), the conditions that hold (classes in order, then each class's conditions in order), and the node's direct writes. A node's classes are its type's, then its own.

```c
muiCondition muiDefaultCondition(void);
```
Returns the condition that always holds.  @return The condition. @par Thread safety Safe from any thread.

```c
muiEnvironment muiDefaultEnvironment(void);
```
Returns the environment of a new context: a medium viewport, a pointer, a text scale of 1 and full motion.  @return The environment. @par Thread safety Safe from any thread.

```c
MUI_NODISCARD MUI_API muiResult muiSetContextEnvironment(muiContext* context, const muiEnvironment* environment);
```
Sets the environment conditions read. Every node is styled again at the next muiComputeLayout when it changes.  @param context      The context. @param environment  One viewport class bit, one input modality bit and a finite text scale above 0. @return `mui_success`; `mui_errorInvalid` for a NULL argument, a value outside the above or a call from a measure or paint function. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
muiEnvironment muiGetContextEnvironment(const muiContext* context);
```
Returns the environment conditions read.  @param context  The context. @return The environment; muiDefaultEnvironment's for a NULL context. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiCreateStyle(muiContext* context, muiStyleId* styleIdOut);
```
Creates a style class with no values set.  @param context     The context. @param styleIdOut  Receives the class; set to the null id on failure. @return `mui_success`; `mui_errorInvalid` for a NULL argument or a call from a measure or paint function; `mui_errorCapacity` when the context's style limit is reached. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiDestroyStyle(muiContext* context, muiStyleId styleId);
```
Destroys a style class. Nodes and node types that list it skip it, and every node is styled again at the next muiComputeLayout.  @param context  The context. @param styleId  The class. @return `mui_success`; `mui_errorInvalid` for a NULL context, the null id or a call from a measure or paint function; `mui_errorStale` for an id whose class is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiStyle_SetLayoutValues(muiContext* context, muiStyleId styleId, muiVariant variant, const muiLayoutStyle* values, muiPropertyMask mask);
```
Sets layout properties in one variant of a class from the fields of values. Every node is styled again at the next muiComputeLayout.  @param context  The context. @param styleId  The class. @param variant  The variant. @param values   The values; only the fields mask names are read, and each must be one muiNode_SetLayoutStyle allows. @param mask     The properties, within MUI_LAYOUT_PROPERTIES. @return `mui_success`; `mui_errorInvalid` for a NULL argument, the null id, an unknown variant or property bit, a value outside the above, a property the variant's condition reads (its axis's sizes and limits, the aspect ratio, the text direction) or a call from a measure or paint function, which changes nothing; `mui_errorStale` for an id whose class is gone; `mui_errorCapacity` when the variant had no values and the context's limit of property sets is reached. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiStyle_ResetProperties(muiContext* context, muiStyleId styleId, muiVariant variant, muiPropertyGroup group, muiPropertyMask mask);
```
Unsets properties in one variant of a class: their values and the tokens named for them. Every node is styled again at the next muiComputeLayout.  @param context  The context. @param styleId  The class. @param variant  The variant. @param group    The properties' group. @param mask     The properties, within the group's. @return `mui_success`; `mui_errorInvalid` for a NULL context, the null id, an unknown variant, group or property bit or a call from a measure or paint function; `mui_errorStale` for an id whose class is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiStyle_GetLayoutValues(const muiContext* context, muiStyleId styleId, muiVariant variant, muiLayoutStyle* valuesOut, muiPropertyMask* maskOut);
```
Reads the values one variant of a class sets.  @param context    The context. @param styleId    The class. @param variant    The variant. @param valuesOut  Receives the set values, and muiDefaultLayoutStyle's for the rest. @param maskOut    Receives which properties are set. @return `mui_success`; `mui_errorInvalid` for a NULL argument, the null id or an unknown variant; `mui_errorStale` for an id whose class is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiStyle_AddCondition(muiContext* context, muiStyleId styleId, const muiCondition* condition, muiVariant* variantOut);
```
Adds a condition to a class, after its others. Its values are then set through the variant it gives. Every node is styled again at the next muiComputeLayout.  @param context     The context. @param styleId     The class. @param condition   Ranges with a finite minimum of 0 or more and a maximum not below it, known bits and choices. @param variantOut  Receives the condition's variant; set to mui_variantBase on failure. @return `mui_success`; `mui_errorInvalid` for a NULL argument, the null id, a condition outside the above or a call from a measure or paint function; `mui_errorStale` for an id whose class is gone; `mui_errorCapacity` when the class has MUI_MAX_CONDITIONS. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiStyle_SetCondition(muiContext* context, muiStyleId styleId, muiVariant variant, const muiCondition* condition);
```
Replaces one of a class's conditions, keeping its values. Every node is styled again at the next muiComputeLayout.  @param context    The context. @param styleId    The class. @param variant    The condition's variant. @param condition  As muiStyle_AddCondition takes it, reading nothing the condition's values set. @return `mui_success`; `mui_errorInvalid` for a NULL argument, the null id, a variant that is not one of the class's conditions, a condition outside the above or a call from a measure or paint function; `mui_errorStale` for an id whose class is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiStyle_GetCondition(const muiContext* context, muiStyleId styleId, muiVariant variant, muiCondition* conditionOut);
```
Reads one of a class's conditions.  @param context       The context. @param styleId       The class. @param variant       The condition's variant. @param conditionOut  Receives the condition. @return `mui_success`; `mui_errorInvalid` for a NULL argument, the null id or a variant that is not one of the class's conditions; `mui_errorStale` for an id whose class is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiStyle_ClearConditions(muiContext* context, muiStyleId styleId);
```
Removes every condition of a class and its values. Every node is styled again at the next muiComputeLayout.  @param context  The context. @param styleId  The class. @return `mui_success`; `mui_errorInvalid` for a NULL context, the null id or a call from a measure or paint function; `mui_errorStale` for an id whose class is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiCreateNodeType(muiContext* context, const muiStyleId* classes, uint32_t count, muiNodeTypeId* typeIdOut);
```
Creates a node type with an ordered list of classes.  @param context    The context. @param classes    count classes, kept as given; a class destroyed later is skipped. NULL when count is 0. @param count      At most MUI_MAX_CLASSES. @param typeIdOut  Receives the type; set to the null id on failure. @return `mui_success`; `mui_errorInvalid` for a NULL argument, a count over the limit or a call from a measure or paint function; `mui_errorCapacity` when the context's node type limit is reached. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiDestroyNodeType(muiContext* context, muiNodeTypeId typeId);
```
Destroys a node type. Its nodes are left with no type, and every node is styled again at the next muiComputeLayout.  @param context  The context. @param typeId   The type. @return `mui_success`; `mui_errorInvalid` for a NULL context, the null id or a call from a measure or paint function; `mui_errorStale` for an id whose type is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNodeType_SetClasses(muiContext* context, muiNodeTypeId typeId, const muiStyleId* classes, uint32_t count);
```
Replaces a node type's classes. Every node is styled again at the next muiComputeLayout.  @param context  The context. @param typeId   The type. @param classes  count classes, as muiCreateNodeType takes them. @param count    At most MUI_MAX_CLASSES. @return `mui_success`; `mui_errorInvalid` for a NULL context, the null id, NULL classes with a count, a count over the limit or a call from a measure or paint function; `mui_errorStale` for an id whose type is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_SetType(muiContext* context, muiNodeId nodeId, muiNodeTypeId typeId);
```
Sets a node's type. The node is styled again at the next muiComputeLayout.  @param context  The context. @param nodeId   The node. @param typeId   The type; the null id for none. @return `mui_success`; `mui_errorInvalid` for a NULL context, the null node id or a call from a measure or paint function; `mui_errorStale` for a node or a type that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_SetClasses(muiContext* context, muiNodeId nodeId, const muiStyleId* classes, uint32_t count);
```
Replaces a node's own classes, which follow its type's. The node is styled again at the next muiComputeLayout.  @param context  The context. @param nodeId   The node. @param classes  count classes, kept as given; a class destroyed later is skipped. NULL when count is 0. @param count    At most MUI_MAX_CLASSES. @return `mui_success`; `mui_errorInvalid` for a NULL context, the null id, NULL classes with a count, a count over the limit or a call from a measure or paint function; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_SetStates(muiContext* context, muiNodeId nodeId, muiState states);
```
Sets the states a node is in. The node is styled again at the next muiComputeLayout when they change. Pointer input's hover and press, and the players' focus, join them, apart: setting states leaves those. The exiting bit is the exits' (maul-ui/exit.h): setting states keeps it as it is.  @param context  The context. @param nodeId   The node. @param states   muiState bits. @return `mui_success`; `mui_errorInvalid` for a NULL context, the null id or a call from a measure or paint function; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
muiState muiNode_GetStates(const muiContext* context, muiNodeId nodeId);
```
Returns the states a node is in: those the host set, hover and press from pointer input (muiPointerInput), and focus and its showing from the players' focus (muiFocus_Set).  @param context  The context. @param nodeId   The node. @return Its muiState bits; 0 for a stale id or a NULL context. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_SetLayoutValues(muiContext* context, muiNodeId nodeId, const muiLayoutStyle* values, muiPropertyMask mask);
```
Writes layout properties of a node directly from the fields of values: they win over every class until reset, at once. The node and its parent are laid out again at the next muiComputeLayout. muiNode_SetLayoutStyle writes every layout property.  @param context  The context. @param nodeId   The node. @param values   The values; only the fields mask names are read, and each must be one muiNode_SetLayoutStyle allows. @param mask     The properties, within MUI_LAYOUT_PROPERTIES. @return `mui_success`; `mui_errorInvalid` for a NULL argument, the null id, an unknown property bit, a value outside the above or a call from a measure or paint function, which changes nothing; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_ResetProperties(muiContext* context, muiNodeId nodeId, muiPropertyGroup group, muiPropertyMask mask);
```
Ends direct writes of a node's properties: they take their classes' values again at the next muiComputeLayout.  @param context  The context. @param nodeId   The node. @param group    The properties' group. @param mask     The properties, within the group's. @return `mui_success`; `mui_errorInvalid` for a NULL context, the null id, an unknown group or property bit or a call from a measure or paint function; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
muiPropertyMask muiNode_GetDirectProperties(const muiContext* context, muiNodeId nodeId, muiPropertyGroup group);
```
Returns which properties of a group a node writes directly.  @param context  The context. @param nodeId   The node. @param group    The group. @return The properties; 0 for a stale id, a NULL context or an unknown group. @par Thread safety Safe from any thread; the context is used by one thread at a time.

## `text.h`

The text service (record mui-0006): fonts, shaping and paragraphs, in the library when it is built with its text component (MAUL_UI_TEXT). The core reaches text only through host content, so a host may use another text stack instead.

```c
muiTextServiceDef muiDefaultTextServiceDef(void);
```
Returns the default service def: 64 fonts, 1,024 text blocks, 16 font families and the C library's allocator.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MUI_NODISCARD MUI_API muiResult muiCreateTextService(const muiTextServiceDef* def, muiTextService** serviceOut);
```
Creates a text service.  @param def         The service: a valid cookie, an allocator with both functions or neither, a font limit from 1 to 65,536 and a family limit up to 65,536. @param serviceOut  Receives the service; set to NULL on failure. @return `mui_success`; `mui_errorInvalid` for a NULL argument, a bad cookie, a half-set allocator or a limit out of range; `mui_errorCapacity` when memory runs out. @par Thread safety Safe from any thread.

```c
void muiDestroyTextService(muiTextService* service);
```
Destroys a service and every font in it. Every id it gave out becomes meaningless.  @param service  The service, or NULL for nothing. @par Thread safety Safe from any thread; the service is used by one thread at a time.

## `text_block.h`

Text blocks (record mui-0006): UTF-8 text the text service lays out as a node's host content. A node whose host key is a block's key, and whose content is the host's, is measured by muiMeasureText and painted by muiPaintText in its computed text style: lines broken where Unicode allows (UAX #14), runs ordered by the Unicode bidirectional algorithm (UAX #9), glyphs shaped by HarfBuzz. White space is kept as written, line breaks in the text end lines, and spaces ending a wrapped line hang past it.

```c
MUI_NODISCARD MUI_API muiResult muiCreateTextBlock(muiTextService* service, const char* text, size_t length, muiTextBlockId* blockOut);
```
Creates a text block holding a copy of UTF-8 text. Ill-formed sequences are laid out as U+FFFD.  @param service  The service. @param text     The text; may be NULL when length is 0. @param length   Its length in bytes, below 2^31. @param blockOut Receives the block's id; the null id on failure. @return `mui_success`; `mui_errorInvalid` for a NULL argument or a length of 2^31 or more; `mui_errorCapacity` when the service's limit of blocks is reached or memory runs out. @par Thread safety Safe from any thread; the service is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiDestroyTextBlock(muiTextService* service, muiTextBlockId blockId);
```
Destroys a text block.  @param service  The service. @param blockId  The block. @return `mui_success`; `mui_errorInvalid` for a NULL service or the null id; `mui_errorStale` for a block that is gone. @par Thread safety Safe from any thread; the service is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiTextBlock_SetText(muiTextService* service, muiTextBlockId blockId, const char* text, size_t length);
```
Replaces a block's text with a copy of other UTF-8 text. Nodes that show it are measured again once the host calls muiNode_MarkContentChanged for them.  @param service  The service. @param blockId  The block. @param text     The text; may be NULL when length is 0. @param length   Its length in bytes, below 2^31. @return `mui_success`; `mui_errorInvalid` for a NULL service, the null id, a NULL text with a length or a length of 2^31 or more; `mui_errorStale` for a block that is gone; `mui_errorCapacity` when memory runs out, which keeps the old text. @par Thread safety Safe from any thread; the service is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiTextBlock_Replace(muiTextService* service, muiTextBlockId blockId, uint32_t start, uint32_t end, const char* text, size_t length);
```
Replaces the bytes of a block's text from start up to end with a text, as editing does; its nodes are measured and painted anew once marked changed. Offsets inside a UTF-8 sequence leave bytes that read as U+FFFD; muiTextBlock_FindDeletion and muiTextMove give offsets on grapheme cluster boundaries.  @param service  The service. @param blockId  The block. @param start    The first byte replaced. @param end      The byte after the last; start for an insertion. @param text     The text put in its place, UTF-8. May be NULL when length is 0, and may be part of the block's text. @param length   Its length in bytes. @return `mui_success`; `mui_errorInvalid` for a NULL service, the null id, a NULL text with a length, start after end, end past the text, or a result of 2^31 bytes or more; `mui_errorStale` for a block that is gone; `mui_errorCapacity` when memory runs out, which keeps the old text. @par Thread safety Safe from any thread; the service is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiTextBlock_SetSpans(muiTextService* service, muiTextBlockId blockId, const muiTextSpan* spans, uint32_t count);
```
Sets the spans of a block's text, copied, replacing the ones it had; later ones win where they overlap, as a stack of styles does. Spans set the text color, the decoration and its color, and the font, size, weight and slant their runs are shaped in: a size is against the node's, as a child's text is against its parent's, and a line is as tall as the runs on it reach (record mui-0006). A block takes up to 31 run styles apart from the node's; a span making more is shaped in the style under it. Setting the text drops them; replacing a range moves those after it, a span growing with text put strictly inside it; a span start inside the range goes to the new text's end, a span end inside it to the range's start, and a span left empty is dropped. Mark the nodes showing the block changed (muiNode_MarkContentChanged).  @param service  The service. @param blockId  The block. @param spans    The spans; NULL when count is 0. @param count    How many, at most MUI_MAX_TEXT_SPANS; 0 clears them. @return `mui_success`; `mui_errorInvalid` for a NULL service, the null id, NULL spans with a count, too many, a span empty, past the text or with an edge inside a UTF-8 sequence, a mask naming another property, or a value not valid for its property; `mui_errorStale` for a block that is gone; `mui_errorCapacity` when memory runs out, which keeps the old spans. @par Thread safety Safe from any thread; the service is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiTextBlock_GetSpans(const muiTextService* service, muiTextBlockId blockId, const muiTextSpan** spansOut, uint32_t* countOut);
```
Reads a block's spans, as moved by the edits since they were set: what a host saves of a block's rich text.  @param service    The service. @param blockId    The block. @param spansOut   Receives them, valid until its spans or text change or the block is destroyed; NULL for none. @param countOut   Receives how many. @return `mui_success`; `mui_errorInvalid` for a NULL argument or the null id; `mui_errorStale` for a block that is gone. Nothing is written on failure. @par Thread safety Safe from any thread; the service is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiTextBlock_GetText(const muiTextService* service, muiTextBlockId blockId, const char** textOut, size_t* lengthOut);
```
Reads a block's text.  @param service    The service. @param blockId    The block. @param textOut    Receives its bytes, valid until its text is set, replaced or the block destroyed; never NULL. @param lengthOut  Receives its length in bytes. @return `mui_success`; `mui_errorInvalid` for a NULL argument or the null id; `mui_errorStale` for a block that is gone. Nothing is written on failure. @par Thread safety Safe from any thread; the service is used by one thread at a time.

```c
uint64_t muiTextBlock_GetKey(muiTextBlockId blockId);
```
Returns a block's key, for a node's host key: never 0.  @param blockId  The block. @return The key. @par Thread safety Safe from any thread.

```c
uint64_t muiFont_GetKey(muiFontId fontId);
```
Returns a font's key, for a text style's font: never 0, which names the service's default font. Text laid out in it draws glyph runs whose keys name an instance of the font as well: the axes and the bold or oblique the style's weight, slant and size make of it, which glyph images and atlases rebuild from the key. A null id's key is 0.  @param fontId  The font. @return The key. @par Thread safety Safe from any thread.

```c
uint64_t muiFontFamily_GetKey(muiFontFamilyId familyId);
```
Returns a font family's key, for a text style's font: never 0, and apart from every font's key.  @param familyId  The family. @return The key; 0 for the null id. @par Thread safety Safe from any thread.

```c
MUI_NODISCARD MUI_API muiResult muiSetDefaultFont(muiTextService* service, muiFontId fontId);
```
Sets the font a text style's font key 0 names; the null id sets none, and text in font 0 then draws nothing.  @param service  The service. @param fontId   The font, or the null id. @return `mui_success`; `mui_errorInvalid` for a NULL service; `mui_errorStale` for a font that is gone. @par Thread safety Safe from any thread; the service is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiSetFallbackFonts(muiTextService* service, const uint64_t* keys, uint32_t count);
```
Sets the fonts tried, in order, for characters a style's font or family, and its family's fallbacks, lack: each grapheme cluster is drawn in the first that has all its characters, characters of no one script staying in the font before them when it has them. Lines keep the metrics of the style's own font.  @param service  The service. @param keys     Keys of fonts and families; may be NULL when count is 0. @param count    Up to 8; 0 for none. @return `mui_success`; `mui_errorInvalid` for a NULL service, NULL keys with a count, a count past 8 or a key that is no font's or family's; `mui_errorStale` for one that is gone. On failure the fallbacks stay as they were. @par Thread safety Safe from any thread; the service is used by one thread at a time.

```c
uint64_t muiGetTextServiceFailures(const muiTextService* service);
```
Counts the times a block could not be laid out for want of memory, and so measured as empty and painted nothing.  @param service  The service; NULL gives 0. @return The count. @par Thread safety Safe from any thread; the service is used by one thread at a time.

```c
muiSize muiMeasureText(void* user, muiNodeId nodeId, uint64_t hostKey, muiMeasureAxis width, muiMeasureAxis height);
```
Measures a text block's lines, as a muiMeasureFunction: user is a muiTextHost, and hostKey a block's key. The width breaks lines: exact and at-most sizes wrap them to it, max-content keeps only the text's own line breaks, min-content breaks at every opportunity. The height is the lines' heights. A key that names no block, a font key that names no font, or a node whose text does not wrap measure as their lines allow; none of them is an error.  @param user     A muiTextHost. @param nodeId   The node, whose text style is read. @param hostKey  The block's key. @param width    The width request. @param height   The height request, which the text does not use. @return The content box size. @par Thread safety Safe from any thread; the service and context are used by one thread at a time.

```c
void muiPaintText(void* user, muiNodeId nodeId, uint64_t hostKey, float width, float height, muiDrawSink* sink);
```
Paints a text block's lines into a draw list, as a muiPaintFunction: user is a muiTextHost, and hostKey a block's key. Lines break to the content box's width, are ordered for display and aligned by the text style, and are drawn as glyph runs.  @param user     A muiTextHost. @param nodeId   The node, whose text style is read. @param hostKey  The block's key. @param width    The content box's width. @param height   The content box's height. @param sink     Where the runs go. @par Thread safety Safe from any thread; the service and context are used by one thread at a time.

```c
bool muiAccessTextOf(void* user, muiNodeId nodeId, uint64_t hostKey, const char** textOut, size_t* lengthOut);
```
Reads a text block's text for accessibility, as a maul-ui/access.h's muiAccessTextFunction: user is a muiTextHost, and hostKey a block's key. The block's text, which the record leaves out when it is not well-formed UTF-8; for an editing password (maul-ui/text_editor.h), a bullet per character, as it is shown.  @param user       A muiTextHost. @param nodeId     The node. @param hostKey    The block's key. @param textOut    Receives the text, valid until the block is edited or destroyed. @param lengthOut  Receives its length. @return Whether the key names a block; false, too, when memory for a password's bullets runs out. @par Thread safety Safe from any thread; the service and context are used by one thread at a time.

```c
float muiTextBaseline(void* user, muiNodeId nodeId, uint64_t hostKey, float width, float height);
```
muiBaselineFunction: user is a muiTextHost, and hostKey a block's key. Lines are a line height apart from the content box's top, so the first baseline does not depend on the width.  @param user     A muiTextHost. @param nodeId   The node, whose text style is read. @param hostKey  The block's key. @param width    The content box's width, which the baseline does not use. @param height   The content box's height, which it does not use. @return The baseline's distance down from the content box's top; NaN for empty text, which has no lines, and where muiMeasureText measures nothing. @par Thread safety Safe from any thread; the service and context are used by one thread at a time.

## `text_edit.h`

Editing primitives over laid-out text (record mui-0006): positions in a node's text, from points, to carets and moved by cluster, word or line, the rectangles a range of it covers, the text laid out as muiPaintText paints it, what a deletion removes, and an input method's composition held in a block. maul-ui/text_editor.h builds selection and undo on them.

```c
MUI_NODISCARD MUI_API muiResult muiTextHitTest(const muiTextHost* host, muiNodeId nodeId, float width, float x, float y, muiTextPosition* positionOut);
```
Finds the position nearest a point of a node's text: the line at the point's y (the first above the text, the last below it), then the edge of the grapheme cluster nearer the point's x (the right one at the middle), a cluster several clusters share a glyph with taking an equal share of it; past a line's ends, that end, before any white space hanging past it. The position keeps to the cluster the point is on. Negative letter spacing can draw a cluster over the one before it; the point then finds the first, left to right.  @param host         The text host the node's text is laid out with. @param nodeId       A node whose host key is a block's. @param width        The node's content box width, as painting is given. @param x            The point, from the content box's top left. @param y            Likewise. @param positionOut  Receives the position; unchanged on failure. @return `mui_success`; `mui_errorInvalid` for a NULL argument or a point not finite; `mui_errorStale` for a node, block or font that is gone; `mui_errorCapacity` when memory runs out. @par Thread safety Safe from any thread; the host's context and service are used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiTextGetCaret(const muiTextHost* host, muiNodeId nodeId, float width, muiTextPosition position, muiTextCaret* caretOut);
```
Finds where the caret of a position is drawn: at the leading edge of the cluster after it for downstream, the trailing edge of the cluster before it for upstream, on the line the affinity picks where a line wraps; an offset in white space hanging past a line's end sits at that end, and an offset inside a cluster at its start.  @param host      The text host. @param nodeId    A node whose host key is a block's. @param width     The node's content box width. @param position  The position; an offset past the text is its end. @param caretOut  Receives the caret; unchanged on failure. @return As muiTextHitTest. @par Thread safety Safe from any thread; the host's context and service are used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiTextGetRangeRects(const muiTextHost* host, muiNodeId nodeId, float width, uint32_t start, uint32_t end, muiRect* rects, uint32_t capacity, uint32_t* countOut);
```
Finds the rectangles a range of a node's text covers: on each line, one for each stretch of side by side clusters in the range, left to right, lines from the top; clusters on both sides of the range's ends are not covered.  @param host      The text host. @param nodeId    A node whose host key is a block's. @param width     The node's content box width. @param start     The range's first byte. @param end       The byte after it; no rectangles when not after start. @param rects     Receives the rectangles, from the content box's top left; may be NULL when capacity is 0. @param capacity  How many rects holds. @param countOut  Receives how many rectangles there are, also when rects holds fewer. @return `mui_success`; `mui_errorCapacity` when rects holds fewer, writing those that fit, or memory runs out; otherwise as muiTextHitTest. @par Thread safety Safe from any thread; the host's context and service are used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiTextMove(const muiTextHost* host, muiNodeId nodeId, float width, muiTextPosition from, muiTextMovement movement, float preferredX, muiTextPosition* positionOut);
```
Moves a position through a node's text, laid out as muiPaintText paints it. Positions moved to are on grapheme cluster boundaries; one that cannot move (the text's start moving back) stays.  @param host         The text host. @param nodeId       A node whose host key is a block's. @param width        The node's content box width. @param from         The position; an offset past the text is its end. @param movement     Where to. @param preferredX   For moving up and down a line, the x to keep, as the caret had before the first vertical move; NaN for from's own caret x. Not read otherwise. @param positionOut  Receives the position; unchanged on failure. @return `mui_success`; `mui_errorInvalid` for a NULL argument or a movement out of range; `mui_errorStale` for a node, block or font that is gone; `mui_errorCapacity` when memory runs out. @par Thread safety Safe from any thread; the host's context and service are used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiTextBlock_FindDeletion(const muiTextService* service, muiTextBlockId blockId, uint32_t offset, muiTextDeletion deletion, uint32_t* startOut, uint32_t* endOut);
```
Finds the bytes a deletion from an offset of a block's text removes, for muiTextBlock_Replace; words are deleted by moving with muiTextMove and replacing what lies between. Nothing at the text's start going back or its end going forward.  @param service   The service. @param blockId   The block. @param offset    The offset; past the text is its end. @param deletion  Which way. @param startOut  Receives the first byte to remove. @param endOut    Receives the byte after the last; startOut's value when there is nothing. @return `mui_success`; `mui_errorInvalid` for a NULL argument, the null id or a deletion out of range; `mui_errorStale` for a block that is gone. Nothing is written on failure. @par Thread safety Safe from any thread; the service is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiTextBlock_SetComposition( muiTextService* service, muiTextBlockId blockId, uint32_t offset, const char* text, size_t length, const muiCompositionSegment* segments, uint32_t segmentCount);
```
Sets a block's input method composition, the text being composed before it is committed: the text replaces the composition there is, or goes in at offset when there is none, and painting underlines it by its segments (all of it thin when there are none). An empty text removes the composition and ends it. While it lasts, muiTextBlock_Replace before or after it moves it, and one that overlaps it, or muiTextBlock_SetText, ends it.  @param service       The service. @param blockId       The block. @param offset        Where a new composition goes; past the text is its end. Not read while one lasts. @param text          The composition, UTF-8. May be NULL when length is 0. @param length        Its length in bytes. @param segments      Its styled parts, within it. May be NULL when segmentCount is 0. @param segmentCount  How many, at most MUI_MAX_COMPOSITION_SEGMENTS. @return `mui_success`; `mui_errorInvalid` for a NULL service, the null id, a NULL text or segments with a count, too many segments, one past the text or of an unknown style, or a text of 2^31 bytes or more; `mui_errorStale` for a block that is gone; `mui_errorCapacity` when memory runs out, which keeps the old text and composition. @par Thread safety Safe from any thread; the service is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiTextBlock_EndComposition(muiTextService* service, muiTextBlockId blockId);
```
Ends a block's composition, keeping its text as typed text: what an input method that commits the composition as it is asks. Nothing without one.  @param service  The service. @param blockId  The block. @return `mui_success`; `mui_errorInvalid` for a NULL service or the null id; `mui_errorStale` for a block that is gone. @par Thread safety Safe from any thread; the service is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiTextBlock_GetComposition(const muiTextService* service, muiTextBlockId blockId, uint32_t* startOut, uint32_t* lengthOut);
```
Reads where a block's composition is.  @param service    The service. @param blockId    The block. @param startOut   Receives its first byte in the text. @param lengthOut  Receives its length; 0 when there is none. @return `mui_success`; `mui_errorInvalid` for a NULL argument or the null id; `mui_errorStale` for a block that is gone. Nothing is written on failure. @par Thread safety Safe from any thread; the service is used by one thread at a time.

## `text_editor.h`

Text editing (record mui-0006): a block opted into editing keeps a selection, an undo history and its field's rules, and takes typing, pastes, deletions, undo and redo; moves, presses and drags place the selection through a node's laid-out text. muiTextEditEvent maps a platform's keys, typed text and the pointer onto these; the clipboard and focus stay the host's. An editing block's text scrolls in its node's content box to keep the caret in view, back as far as the text allows; muiPaintText draws it scrolled (a node that clips keeps it inside), and the editing primitives (maul-ui/text_edit.h) take and give points as drawn.

```c
muiTextEditDef muiDefaultTextEditDef(void);
```
Returns a single-line field's rules: no filter, plain text, no limit, and 100 edits to undo.  @return The rules. @par Thread safety Safe from any thread.

```c
MUI_NODISCARD MUI_API muiResult muiTextBlock_SetEditing(muiTextService* service, muiTextBlockId blockId, const muiTextEditDef* def);
```
Opts a block into editing with a field's rules, or out with NULL. Either way the history empties and the caret goes to the text's end. Changing the text otherwise (muiTextBlock_SetText, muiTextBlock_Replace, a composition) empties the history too, the selection kept within the text.  @param service  The service. @param blockId  The block. @param def      The rules, or NULL. @return `mui_success`; `mui_errorInvalid` for a NULL service, the null id, or a flag, filter or purpose out of range; `mui_errorStale` for a block that is gone. @par Thread safety Safe from any thread; the service is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiTextBlock_GetSelection(const muiTextService* service, muiTextBlockId blockId, muiTextSelection* selectionOut);
```
Reads an editing block's selection.  @param service       The service. @param blockId       The block. @param selectionOut  Receives the selection. @return `mui_success`; `mui_errorInvalid` for a NULL argument, the null id or a block not editing; `mui_errorStale` for a block that is gone. Nothing is written on failure. @par Thread safety Safe from any thread; the service is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiTextBlock_Select(muiTextService* service, muiTextBlockId blockId, muiTextSelection selection);
```
Sets an editing block's selection, which ends a run of typing undone together.  @param service    The service. @param blockId    The block. @param selection  The selection: offsets within the text, at the starts of characters. @return `mui_success`; `mui_errorInvalid` for a NULL service, the null id, a block not editing or an offset out of place; `mui_errorStale` for a block that is gone. @par Thread safety Safe from any thread; the service is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiTextBlock_Type(muiTextService* service, muiTextBlockId blockId, const char* text, size_t length, bool* changedOut);
```
Types text over an editing block's selection, under its rules, the caret after it. Typing undoes word by word: a run of it continuing where the last ended is one edit until a word starts after white space.  @param service     The service. @param blockId     The block. @param text        UTF-8 text; may be NULL when length is 0. @param length      Its length in bytes. @param changedOut  Receives whether the text changed; may be NULL. @return `mui_success`, changed or not (read-only, filtered out, at the limit); `mui_errorInvalid` for a NULL service or text, the null id, a block not editing or text past the block's limit; `mui_errorStale` for a block that is gone; `mui_errorCapacity` when memory runs out, which changes nothing. @par Thread safety Safe from any thread; the service is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiTextBlock_Paste(muiTextService* service, muiTextBlockId blockId, const char* text, size_t length, bool* changedOut);
```
Pastes text over an editing block's selection, as muiTextBlock_Type but undone alone.  @param service     The service. @param blockId     The block. @param text        UTF-8 text; may be NULL when length is 0. @param length      Its length in bytes. @param changedOut  Receives whether the text changed; may be NULL. @return As muiTextBlock_Type. @par Thread safety Safe from any thread; the service is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiTextBlock_Erase(muiTextService* service, muiTextBlockId blockId, muiTextDeletion deletion, bool* changedOut);
```
Deletes an editing block's selection, or with none, what the deletion removes beside the caret (muiTextBlock_FindDeletion). A run of deletions one way, each where the last left the caret, is undone together.  @param service     The service. @param blockId     The block. @param deletion    Which way. @param changedOut  Receives whether the text changed; may be NULL. @return As muiTextBlock_Type; `mui_errorInvalid` for a deletion out of range. @par Thread safety Safe from any thread; the service is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiTextBlock_EraseTo(muiTextService* service, muiTextBlockId blockId, uint32_t offset, bool* changedOut);
```
Deletes an editing block's selection, or with none, the text from the caret to an offset: a word or a line, found with muiTextMove.  @param service     The service. @param blockId     The block. @param offset      The other end, at the start of a character; past the text is its end. @param changedOut  Receives whether the text changed; may be NULL. @return As muiTextBlock_Type; `mui_errorInvalid` for an offset inside a character. @par Thread safety Safe from any thread; the service is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiTextBlock_GetSelectedText(const muiTextService* service, muiTextBlockId blockId, const char** textOut, size_t* lengthOut);
```
Reads the selected text of an editing block, for the host's clipboard: a cut is this, then muiTextBlock_Erase. A password's is empty.  @param service    The service. @param blockId    The block. @param textOut    Receives the text, valid until the block changes. @param lengthOut  Receives its length in bytes. @return `mui_success`; `mui_errorInvalid` for a NULL argument, the null id or a block not editing; `mui_errorStale` for a block that is gone. Nothing is written on failure. @par Thread safety Safe from any thread; the service is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiTextBlock_Undo(muiTextService* service, muiTextBlockId blockId, bool* changedOut);
```
Undoes an editing block's last edit, the selection back as it was before it; nothing on a read-only block or during a composition.  @param service     The service. @param blockId     The block. @param changedOut  Receives whether the text changed; may be NULL. @return As muiTextBlock_Type. @par Thread safety Safe from any thread; the service is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiTextBlock_Redo(muiTextService* service, muiTextBlockId blockId, bool* changedOut);
```
Redoes an editing block's last undone edit, the selection as it was after it; as muiTextBlock_Undo otherwise. An edit made after an undo drops what was undone.  @param service     The service. @param blockId     The block. @param changedOut  Receives whether the text changed; may be NULL. @return As muiTextBlock_Type. @par Thread safety Safe from any thread; the service is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiTextBlock_GetInputPurpose(const muiTextService* service, muiTextBlockId blockId, muiInputPurpose* purposeOut);
```
Reads what an editing block's field takes, for an on-screen keyboard: a password's purpose for a password, a number's for a number filter, else the one its rules give.  @param service     The service. @param blockId     The block. @param purposeOut  Receives the purpose. @return As muiTextBlock_GetSelection. @par Thread safety Safe from any thread; the service is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiTextBlock_GetUndoState(const muiTextService* service, muiTextBlockId blockId, bool* undoOut, bool* redoOut);
```
Reads whether an editing block has an edit to undo and one to redo.  @param service  The service. @param blockId  The block. @param undoOut  Receives whether muiTextBlock_Undo would undo one. @param redoOut  Receives whether muiTextBlock_Redo would redo one. @return As muiTextBlock_GetSelection. @par Thread safety Safe from any thread; the service is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiTextBlock_Compose(muiTextService* service, muiTextBlockId blockId, const char* text, size_t length, uint32_t caret, const muiCompositionSegment* segments, uint32_t segmentCount, bool* changedOut);
```
Shows an input method's composition in an editing block at its caret, replacing the one shown; one starting over a selection deletes it first, as an edit undo takes back. Empty text takes the composition out, the caret where it began: what the method commits comes after as typing (muiTextBlock_Type), which also takes out a composition still shown. Undo waits until it ends.  @param service       The service. @param blockId       The block. @param text          The composition's UTF-8 text; may be NULL when length is 0. @param length        Its length in bytes. @param caret         Where the caret is in it, at a character's start or its end. @param segments      Its styled parts (muiTextBlock_SetComposition); may be NULL when segmentCount is 0. @param segmentCount  How many. @param changedOut    Receives whether the text changed; may be NULL. @return As muiTextBlock_Type; `mui_errorInvalid` for a caret out of place or segments out of the text. @par Thread safety Safe from any thread; the service is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiTextEditEvent(const muiTextHost* host, muiNodeId nodeId, const muiEvent* event, const muiTextEditInput* input, muiTextEditOutcome* outcomeOut);
```
Takes an event for a node's editing block, as its listener would: typed text is typed; keys move, select, delete, undo and copy by the keymap's shortcuts; a press of the first button places the selection by its click count, and a drag's records extend it (the node takes drags, maul-ui/interaction.h).  The shortcuts, with Shift extending each move: arrows by cluster and line, up and down on a single line to its ends; words with Control (PC) or Option (Mac); line ends with Home and End (PC) or Command and the arrows (Mac), the text's ends with Control and Home and End (PC) or Command and up and down (Mac); Backspace and Delete by cluster, a word with Control (PC) or Option (Mac), back to the line's start with Command (Mac); Enter's line break on many lines; select all, copy, cut, paste, undo and redo with Control or Command and A, C, X, V, Z and Shift and Z, also Y (PC), Control and Insert and Shift and Insert (PC). Letters are read from the key's meaning under the layout, the rest from the physical key.  @param host        The text host. @param nodeId      A node whose host key is an editing block's. @param event       The event. @param input       The keymap and the clipboard writer. @param outcomeOut  Receives what the event did. @return As muiTextEditMove. @par Thread safety Safe from any thread; the host's context and service are used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiTextEditMove(const muiTextHost* host, muiNodeId nodeId, muiTextMovement movement, bool extend);
```
Moves the caret of a node's editing block through its laid-out text (muiTextMove), extending the selection or collapsing it there; lines up and down keep the x the first of them started from.  @param host      The text host. @param nodeId    A node whose host key is an editing block's. @param movement  The movement. @param extend    Whether the anchor stays. @return `mui_success`; `mui_errorInvalid` for a NULL argument, a movement out of range or a block not editing; `mui_errorStale` for a node, block or font that is gone; `mui_errorCapacity` when memory runs out. @par Thread safety Safe from any thread; the host's context and service are used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiTextEditPress(const muiTextHost* host, muiNodeId nodeId, float x, float y, uint32_t clickCount, bool extend);
```
Places the caret of a node's editing block where a press is: one click at the point, two selecting the word there, three the paragraph; with extend, the selection grows to it from the anchor. Drags then extend by the same unit.  @param host        The text host. @param nodeId      A node whose host key is an editing block's. @param x           The point, in the node's border box, as pointer records give it. @param y           The point's y. @param clickCount  The press's place in a quick series: 1, 2, 3; a fourth goes round to 1, as on every platform. @param extend      Whether the anchor stays. @return As muiTextEditMove; `mui_errorInvalid` for a point not finite or a click count of 0. @par Thread safety Safe from any thread; the host's context and service are used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiTextEditDrag(const muiTextHost* host, muiNodeId nodeId, float x, float y);
```
Extends the selection of a node's editing block to where a drag is, by the unit its press selected, keeping what the press selected.  @param host    The text host. @param nodeId  A node whose host key is an editing block's. @param x       The point, in the node's border box. @param y       The point's y. @return As muiTextEditPress. @par Thread safety Safe from any thread; the host's context and service are used by one thread at a time.

## `text_style.h`

Text style (record mui-0004): the values a text service reads to lay out and draw a node's text, set through the same classes, variants, conditions, tokens, themes, transitions and direct writes as every property. They are inherited as CSS inherits them: a property no layer gives a node takes its parent's value, and a root the defaults. The core draws no text; a change marks a node with host content to be measured or painted again.

```c
muiTextStyle muiDefaultTextStyle(void);
```
Returns the default text style: opaque black, font 0, 16 logical units, the font's own line height, no letter spacing, weight 400, upright, at the start, wrapping, with no decoration, whose color is the text's. A root takes these for what no layer gives it.  @return The style. @par Thread safety Safe from any thread.

```c
MUI_NODISCARD MUI_API muiResult muiStyle_SetTextValues(muiContext* context, muiStyleId styleId, muiVariant variant, const muiTextStyle* values, muiPropertyMask mask);
```
Sets text values in one variant of a class, as muiStyle_SetLayoutValues sets layout ones.  @param context  The context. @param styleId  The class. @param variant  The variant. @param values   The values; those mask names must be as muiTextStyle describes, and finite. @param mask     The properties, within MUI_TEXT_PROPERTIES. @return `mui_success`; `mui_errorInvalid` for a NULL argument, the null id, an unknown variant or property bit, a value outside the above or a call from a measure or paint function, which changes nothing; `mui_errorStale` for an id whose class is gone; `mui_errorCapacity` when the variant had no values and the context's limit of property sets is reached. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiStyle_GetTextValues(const muiContext* context, muiStyleId styleId, muiVariant variant, muiTextStyle* valuesOut, muiPropertyMask* maskOut);
```
Reads the text values one variant of a class sets.  @param context    The context. @param styleId    The class. @param variant    The variant. @param valuesOut  Receives the set values, and muiDefaultTextStyle's for the rest. @param maskOut    Receives which properties are set. @return `mui_success`; `mui_errorInvalid` for a NULL argument, the null id or an unknown variant; `mui_errorStale` for an id whose class is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_SetTextValues(muiContext* context, muiNodeId nodeId, const muiTextStyle* values, muiPropertyMask mask);
```
Writes text values to a node directly, over its classes, as muiNode_SetLayoutValues writes layout ones. Its children inherit them at the next muiComputeLayout.  @param context  The context. @param nodeId   The node. @param values   The values; those mask names must be as muiTextStyle describes, and finite. @param mask     The properties, within MUI_TEXT_PROPERTIES. @return `mui_success`; `mui_errorInvalid` for a NULL argument, the null id, an unknown property bit, a value outside the above or a call from a measure or paint function, which changes nothing; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_GetTextStyle(const muiContext* context, muiNodeId nodeId, muiComputedTextStyle* styleOut);
```
Reads a node's computed text style, as of its last muiComputeLayout; a measure or paint function may read it.  @param context   The context. @param nodeId    The node. @param styleOut  Receives the computed values. @return `mui_success`; `mui_errorInvalid` for a NULL argument or the null id; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

## `theme.h`

Themes: sets of token values that override the context's tokens for the subtree a theme is set on (record mui-0004). A node reads a token from the nearest theme above it, itself included, that overrides it, then the next one out, then the context; an alias read in a subtree is read there too. Editing a theme restyles every node; setting one on a node restyles the nodes whose themes it changes.

```c
MUI_NODISCARD MUI_API muiResult muiCreateTheme(muiContext* context, muiThemeId* themeIdOut);
```
Creates a theme that overrides no token.  @param context     The context. @param themeIdOut  Receives the theme; set to the null id on failure. @return `mui_success`; `mui_errorInvalid` for a NULL argument or a call from a measure or paint function; `mui_errorCapacity` when the context's theme limit is reached. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiDestroyTheme(muiContext* context, muiThemeId themeId);
```
Destroys a theme and its overrides. Nodes it was set on read through no theme of their own, and every node is restyled.  @param context  The context. @param themeId  The theme. @return `mui_success`; `mui_errorInvalid` for a NULL context, the null id or a call from a measure or paint function; `mui_errorStale` for an id whose theme is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiTheme_SetTokenValue(muiContext* context, muiThemeId themeId, muiTokenId tokenId, const muiTokenValue* value);
```
Overrides a token in a theme with a literal value.  @param context  The context. @param themeId  The theme. @param tokenId  The token. @param value    The value, of the token's type, as muiCreateToken takes it. @return `mui_success`; `mui_errorInvalid` for a NULL argument, a null id, a value of another type or outside muiCreateToken's or a call from a measure or paint function; `mui_errorStale` for a theme or a token that is gone; `mui_errorCapacity` when the token was not overridden and the context's limit of theme overrides is reached. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiTheme_SetTokenAlias(muiContext* context, muiThemeId themeId, muiTokenId tokenId, muiTokenId target);
```
Overrides a token in a theme with an alias of another token of its type, read in the subtree that reads the theme.  @param context  The context. @param themeId  The theme. @param tokenId  The token. @param target   The token it gives the value of. @return `mui_success`; `mui_errorInvalid` for a NULL context, a null id, a target of another type, an alias that would lead back to the token through this theme and the context, or a call from a measure or paint function; `mui_errorStale` for a theme, token or target that is gone; `mui_errorCapacity` as muiTheme_SetTokenValue. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiTheme_ResetToken(muiContext* context, muiThemeId themeId, muiTokenId tokenId);
```
Ends a theme's override of a token.  @param context  The context. @param themeId  The theme. @param tokenId  The token. @return `mui_success`, also when the theme did not override it; `mui_errorInvalid` for a NULL context, a null id or a call from a measure or paint function; `mui_errorStale` for a theme or a token that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiTheme_GetToken(const muiContext* context, muiThemeId themeId, muiTokenId tokenId, muiTokenValue* valueOut, muiTokenId* aliasOut);
```
Reads a theme's override of a token.  @param context   The context. @param themeId   The theme. @param tokenId   The token. @param valueOut  Receives the literal, with the token's type; its member is zero for an alias or no override. @param aliasOut  Receives the token aliased, or the null id; may be NULL. @return `mui_success`; `mui_empty` when the theme does not override the token; `mui_errorInvalid` for a NULL context or value, or a null id; `mui_errorStale` for a theme or a token that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_SetTheme(muiContext* context, muiNodeId nodeId, muiThemeId themeId);
```
Sets the theme a node and its subtree read; the null id sets none.  @param context  The context. @param nodeId   The node. @param themeId  The theme, or the null id. @return `mui_success`; `mui_errorInvalid` for a NULL context, the null node id or a call from a measure or paint function; `mui_errorStale` for a node or a theme that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_GetTheme(const muiContext* context, muiNodeId nodeId, muiThemeId* themeIdOut);
```
Reads the theme set on a node.  @param context     The context. @param nodeId      The node. @param themeIdOut  Receives the theme set, or the null id. @return `mui_success`; `mui_errorInvalid` for a NULL argument or the null id; `mui_errorStale` for an id whose node is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_GetTokenValue(const muiContext* context, muiNodeId nodeId, muiTokenId tokenId, muiTokenValue* valueOut);
```
Reads a token's value as a node reads it, through the themes above it as of the last muiComputeLayout that styled it.  @param context   The context. @param nodeId    The node. @param tokenId   The token. @param valueOut  Receives the value, with the token's type; its member is zero when the result is `mui_empty`. @return `mui_success`; `mui_empty` when the token gives the node no value (an alias to a token that is gone, or a cycle that only nested themes make); `mui_errorInvalid` for a NULL context or value, or a null id; `mui_errorStale` for a node or a token that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

## `token.h`

Tokens: typed values that class variants name in place of a property's value, so that a theme is a change of token values (record mui-0004). A token holds a literal of its type, or an alias to another token of that type; aliases never form a cycle. Changing a token restyles every node, as a class edit does, and named transitions move the change.

```c
MUI_NODISCARD MUI_API muiResult muiCreateToken(muiContext* context, const muiTokenValue* value, muiTokenId* tokenIdOut);
```
Creates a token holding a value; its type is the value's, for good.  @param context     The context. @param value       The value: a known type and a member valid for it (components of a color from 0 to 1, a finite number, a dimension of a known kind with finite parts, a shadow and a gradient as muiStyle_SetVisualValues takes them). @param tokenIdOut  Receives the token; set to the null id on failure. @return `mui_success`; `mui_errorInvalid` for a NULL argument, a value outside the above or a call from a measure or paint function; `mui_errorCapacity` when the context's token limit is reached. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiDestroyToken(muiContext* context, muiTokenId tokenId);
```
Destroys a token. Variants that name it, and tokens that alias it, then give no value through it, and every node is restyled.  @param context  The context. @param tokenId  The token. @return `mui_success`; `mui_errorInvalid` for a NULL context, the null id or a call from a measure or paint function; `mui_errorStale` for an id whose token is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiSetTokenValue(muiContext* context, muiTokenId tokenId, const muiTokenValue* value);
```
Gives a token a literal value, ending any alias, and restyles every node.  @param context  The context. @param tokenId  The token. @param value    The value, of the token's type, as muiCreateToken takes it. @return `mui_success`; `mui_errorInvalid` for a NULL argument, the null id, a value of another type or outside muiCreateToken's or a call from a measure or paint function; `mui_errorStale` for an id whose token is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiSetTokenAlias(muiContext* context, muiTokenId tokenId, muiTokenId target);
```
Makes a token an alias of another of its type, so that it gives that token's value, and restyles every node.  @param context  The context. @param tokenId  The token. @param target   The token it gives the value of. @return `mui_success`; `mui_errorInvalid` for a NULL context, a null id, a target of another type, an alias that would lead back to the token or a call from a measure or paint function; `mui_errorStale` for a token or a target that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiGetTokenValue(const muiContext* context, muiTokenId tokenId, muiTokenValue* valueOut, muiTokenId* aliasOut);
```
Reads a token's value, through its aliases, and what it aliases.  @param context     The context. @param tokenId     The token. @param valueOut    Receives the value, with the token's type; its member is zero when the result is `mui_empty`. @param aliasOut    Receives the token it aliases, or the null id for a literal; may be NULL. @return `mui_success`; `mui_empty` when an alias leads to a token that is gone; `mui_errorInvalid` for a NULL context or value, or the null id; `mui_errorStale` for an id whose token is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiStyle_SetToken(muiContext* context, muiStyleId styleId, muiVariant variant, muiProperty property, muiTokenId tokenId);
```
Names a token for a property in one variant of a class, in place of a value: resolution reads the token there. A value the property does not allow, or no value, leaves that layer silent for the property. Setting a value for the property with muiStyle_SetLayoutValues or muiStyle_SetVisualValues ends the name, and this ends the value; the null id ends the name alone.  @param context   The context. @param styleId   The class. @param variant   The variant. @param property  The property; one that takes a token of a type above, which a condition of the variant does not read. @param tokenId   The token, of the property's type, or the null id. @return `mui_success`; `mui_errorInvalid` for a NULL context, the null class id, an unknown variant or property, a property of no token type or another type than the token's, one the variant's condition reads, or a call from a measure function; `mui_errorStale` for a class or a token that is gone; `mui_errorCapacity` when the context's limit of token names, or of property sets for a variant with nothing set, is reached. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiStyle_GetToken(const muiContext* context, muiStyleId styleId, muiVariant variant, muiProperty property, muiTokenId* tokenIdOut);
```
Reads the token one variant of a class names for a property.  @param context     The context. @param styleId     The class. @param variant     The variant. @param property    The property. @param tokenIdOut  Receives the token; the null id for none. @return `mui_success`; `mui_errorInvalid` for a NULL argument, the null id, an unknown variant or property; `mui_errorStale` for an id whose class is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

## `transition.h`

Transitions: specs of how a property moves to a new value (timed with an easing, or a spring), shared by the classes whose variants name them, and run against the time the host passes to muiComputeLayout (record mui-0004). The spec for a change is resolved through the same layers as values, from the state after the change. Numbers, insets, dimensions and radii that are Scale+Offset on both sides, colors (in premultiplied Oklab) and shadows move, ending exactly on their target; enumerators, flags, image keys, gradients, changes to or from automatic, direct writes, and every change under reduced motion apply at once.

```c
muiTransitionDef muiDefaultTransitionDef(void);
```
Returns the default transition def: timed, 250 ms, ease, no delay; as a spring, 2 Hz and critically damped.  @return The def, with a valid cookie. @par Thread safety Safe from any thread.

```c
MUI_NODISCARD MUI_API muiResult muiCreateTransition(muiContext* context, const muiTransitionDef* def, muiTransitionId* transitionIdOut);
```
Creates a transition spec.  @param context          The context. @param def              The spec: a valid cookie, a known kind and easing, bezier x from 0 to 1 and finite y, and a finite frequency and damping ratio above 0. @param transitionIdOut  Receives the spec; set to the null id on failure. @return `mui_success`; `mui_errorInvalid` for a NULL argument, a def outside the above or a call from a measure or paint function; `mui_errorCapacity` when the context's transition limit is reached. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiDestroyTransition(muiContext* context, muiTransitionId transitionId);
```
Destroys a transition spec. Variants that name it name none, and transitions it started run to their end.  @param context       The context. @param transitionId  The spec. @return `mui_success`; `mui_errorInvalid` for a NULL context, the null id or a call from a measure or paint function; `mui_errorStale` for an id whose spec is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiStyle_SetTransition(muiContext* context, muiStyleId styleId, muiVariant variant, muiTransitionId transitionId, muiPropertyGroup group, muiPropertyMask mask);
```
Names the spec one variant of a class gives properties; the null id takes the variant's spec away from them.  @param context       The context. @param styleId       The class. @param variant       The variant. @param transitionId  The spec, or the null id. @param group         The properties' group. @param mask          The properties, within the group's. @return `mui_success`; `mui_errorInvalid` for a NULL context, the null class id, an unknown variant, group or property bit or a call from a measure or paint function; `mui_errorStale` for a class or a spec that is gone; `mui_errorCapacity` when the variant already names MUI_MAX_VARIANT_TRANSITIONS specs, or has no values yet and the context's limit of property sets is reached. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiStyle_GetTransition(const muiContext* context, muiStyleId styleId, muiVariant variant, muiProperty property, muiTransitionId* transitionIdOut);
```
Reads the spec one variant of a class gives a property.  @param context          The context. @param styleId          The class. @param variant          The variant. @param property         The property. @param transitionIdOut  Receives the spec; the null id for none. @return `mui_success`; `mui_errorInvalid` for a NULL argument, the null id, an unknown variant or property; `mui_errorStale` for an id whose class is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
bool muiNode_IsTransitioning(const muiContext* context, muiNodeId nodeId, muiProperty property);
```
Returns whether a property of a node is moving.  @param context   The context. @param nodeId    The node. @param property  The property. @return Whether a transition of it runs; false for a stale id or a NULL context. @par Thread safety Safe from any thread; the context is used by one thread at a time.

## `virtual.h`

Virtualization (record mui-0007): a scroll container that stands for many items of which only those near its viewport exist as nodes. The library keeps every item's extent, fixed or estimated until measured, works out after each layout the window of items that should exist (the viewport plus overscan on each side), and reports it by mui_notificationWindowChanged when it changes. The host realizes the window: it creates or reuses a child node of the list for each index in it, binds it (muiNode_SetItem), and destroys or pools the rest, resetting a reused node so nothing of one key outlives it. The library places bound items at their offsets along the axis, out of the flex flow and stretched across the list's content box, sizes the content to all the items so the scroll limits hold, and measures bound items into their extents.

```c
muiVirtualList muiDefaultVirtualList(void);
```
The default list: no items, vertical, estimated at 40 a piece, no gap, an overscan of 200.  @return The list. @par Thread safety Safe from any thread.

```c
MUI_NODISCARD MUI_API muiResult muiNode_SetVirtualList(muiContext* context, muiNodeId nodeId, const muiVirtualList* list);
```
Makes a node a virtual list, or sets it anew, every estimated extent back to the estimate; its window is reported at the next layout. Set anew, as after its data was replaced, it keeps the item it showed first where it was, if the host binds that item's node again before the next layout, wherever the item now is.  @param context  The context. @param nodeId   The node. @param list     The list, as described above. @return `mui_success`; `mui_errorCapacity` when the context holds its limit of lists, or the items of an estimated list do not fit what its limit of items leaves; `mui_errorInvalid` for a NULL argument, the null id, a list outside the above, or a call from a measure or paint function; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_ClearVirtualList(muiContext* context, muiNodeId nodeId);
```
Makes a node no longer a virtual list; its items stay bound.  @param context  The context. @param nodeId   The node. @return `mui_success`, whether or not it was one; `mui_errorInvalid` for a NULL context, the null id or a call from a measure or paint function; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_GetVirtualWindow(const muiContext* context, muiNodeId nodeId, uint32_t* firstOut, uint32_t* endOut);
```
Reads the window of a list's items that should exist, as the last layout found it: indices from first up to end, end excluded.  @param context   The context. @param nodeId    The list. @param firstOut  Receives the first index. @param endOut    Receives the index past the last. @return `mui_success`; `mui_empty` for a node that is not a list or has not been laid out since it was set; `mui_errorInvalid` for a NULL argument or the null id; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_GetVirtualItem(const muiContext* context, muiNodeId nodeId, uint32_t index, float* offsetOut, float* extentOut);
```
Reads where an item of a list begins along its axis, from the start of the list's content box, and its extent, as last known.  @param context    The context. @param nodeId     The list. @param index      The item, below the list's count. @param offsetOut  Receives its offset. @param extentOut  Receives its extent. @return `mui_success`; `mui_empty` for a node that is not a list; `mui_errorInvalid` for a NULL argument, the null id or an index past the count; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_SetItem(muiContext* context, muiNodeId nodeId, uint32_t index);
```
Binds a child of a list to an item: the next layouts place it at the item's offset and measure it into the item's extent.  @param context  The context. @param nodeId   The node, a child of a list. @param index    Its item. @return `mui_success`; `mui_errorInvalid` for a NULL context, the null id, a node whose parent is not a list, an index past the list's count, or a call from a measure or paint function; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_ClearItem(muiContext* context, muiNodeId nodeId);
```
Unbinds a node from its item; it joins its parent's flow again.  @param context  The context. @param nodeId   The node. @return `mui_success`, whether or not it was bound; `mui_errorInvalid` for a NULL context, the null id or a call from a measure or paint function; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_GetItem(const muiContext* context, muiNodeId nodeId, uint32_t* indexOut);
```
Reads the item a node is bound to.  @param context   The context. @param nodeId    The node. @param indexOut  Receives the index. @return `mui_success`; `mui_empty` for a node bound to none, or to an item since removed; `mui_errorInvalid` for a NULL argument or the null id; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_InsertVirtualItems(muiContext* context, muiNodeId nodeId, uint32_t index, uint32_t count);
```
Inserts items into a list before index, estimated; the items after keep their extents, and nodes bound to them follow them. Inserted above the viewport, they move the offset along, so what is shown stays put.  @param context  The context. @param nodeId   The list. @param index    Where, up to the count. @param count    How many, at least 1. @return `mui_success`; `mui_empty` for a node that is not a list; `mui_errorCapacity` when an estimated list's items no longer fit its limit; `mui_errorInvalid` for a NULL context, the null id, an index past the count, no items or more than 2^32 - 1 in all, or a call from a measure or paint function; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_RemoveVirtualItems(muiContext* context, muiNodeId nodeId, uint32_t index, uint32_t count);
```
Removes items from a list; nodes bound to them stay out of the flow, placed nowhere, until the host destroys or binds them (muiNode_GetItem reports them empty), and the rest follow their items. Removed above the viewport, they move the offset back.  @param context  The context. @param nodeId   The list. @param index    The first. @param count    How many, at least 1, ending by the count. @return `mui_success`; `mui_empty` for a node that is not a list; `mui_errorInvalid` for a NULL context, the null id, a range outside the list or empty, or a call from a measure or paint function; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_MoveVirtualItem(muiContext* context, muiNodeId nodeId, uint32_t from, uint32_t to);
```
Moves an item of a list to another index, its extent with it; its node, bound, follows it, as do those between. What is shown stays put when the item crosses the viewport's start.  @param context  The context. @param nodeId   The list. @param from     The item, below the count. @param to       Its new index, below the count. @return `mui_success`; `mui_empty` for a node that is not a list; `mui_errorInvalid` for a NULL context, the null id, an index past the count, or a call from a measure or paint function; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

## `visual.h`

Visual values: what the draw-command list paints for a node, set through the same classes, variants, conditions, transitions and direct writes as layout values (record mui-0004). A change to one marks the node's paint, never its layout.

```c
muiVisualStyle muiDefaultVisualStyle(void);
```
Returns the default visual style: clear background, no gradient, square corners, opaque black border colors, no shadows or image, a white tint that does not mirror, opacity 1, no clipping and a scale of 1 about the centre.  @return The values. @par Thread safety Safe from any thread.

```c
MUI_NODISCARD MUI_API muiResult muiStyle_SetVisualValues(muiContext* context, muiStyleId styleId, muiVariant variant, const muiVisualStyle* values, muiPropertyMask mask);
```
Sets visual properties in one variant of a class from the fields of values, as muiStyle_SetLayoutValues does layout ones.  @param context  The context. @param styleId  The class. @param variant  The variant. @param values   The values; only the fields mask names are read: colors with components from 0 to 1, a gradient of a known kind with 2 to MUI_MAX_GRADIENT_STOPS stops in order from 0 to 1 (or none with 0), Scale+Offset radii and slice insets of 0 or more, shadows with finite offsets and spread and a blur of 0 or more, opacity from 0 to 1. @param mask     The properties, within MUI_VISUAL_PROPERTIES. @return `mui_success`; `mui_errorInvalid` for a NULL argument, the null id, an unknown variant or property bit, a value outside the above or a call from a measure or paint function, which changes nothing; `mui_errorStale` for an id whose class is gone; `mui_errorCapacity` when the variant had no values and the context's limit of property sets is reached. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiStyle_GetVisualValues(const muiContext* context, muiStyleId styleId, muiVariant variant, muiVisualStyle* valuesOut, muiPropertyMask* maskOut);
```
Reads the visual values one variant of a class sets.  @param context    The context. @param styleId    The class. @param variant    The variant. @param valuesOut  Receives the set values, and muiDefaultVisualStyle's for the rest. @param maskOut    Receives which visual properties are set. @return `mui_success`; `mui_errorInvalid` for a NULL argument, the null id or an unknown variant; `mui_errorStale` for an id whose class is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_SetVisualValues(muiContext* context, muiNodeId nodeId, const muiVisualStyle* values, muiPropertyMask mask);
```
Writes visual properties of a node directly, as muiNode_SetLayoutValues does layout ones; the node's paint, not its layout, is redone.  @param context  The context. @param nodeId   The node. @param values   The values, as muiStyle_SetVisualValues takes them. @param mask     The properties, within MUI_VISUAL_PROPERTIES. @return `mui_success`; `mui_errorInvalid` for a NULL argument, the null id, an unknown property bit, a value outside the above or a call from a measure or paint function, which changes nothing; `mui_errorStale` for a node that is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

```c
MUI_NODISCARD MUI_API muiResult muiNode_GetVisualStyle(const muiContext* context, muiNodeId nodeId, muiVisualStyle* valuesOut);
```
Reads a node's resolved visual values: its direct writes, and for the other properties what its classes and states gave at the last muiComputeLayout that reached it, where its transitions have them.  @param context    The context. @param nodeId     The node. @param valuesOut  Receives the values. @return `mui_success`; `mui_errorInvalid` for a NULL argument or the null id; `mui_errorStale` for an id whose node is gone. @par Thread safety Safe from any thread; the context is used by one thread at a time.

---

301 functions across 35 headers.
