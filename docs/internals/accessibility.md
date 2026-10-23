# Accessibility

Screen readers read a UI through the platform's accessibility API: UI Automation on Windows, AT-SPI on Linux, NSAccessibility on macOS, UIAccessibility on iOS, Android's accessibility nodes, and the browser's accessibility tree on the web. Each wants the same things of every element: what it is, its name and value, its states, where it is, its children, which one has the focus, and what it can be asked to do. AccessKit gives a toolkit one schema for all of them: nodes sent whole in updates, the focus with every update, and requests coming back. Maul UI uses AccessKit's shape. It already holds most of what the schema asks for: rectangles, scrolling, focus, states, value ranges and virtual lists.

## Contents

- [Producer and consumer](#producer-and-consumer)
- [What a node says](#what-a-node-says)
- [Updates](#updates)
- [Requests](#requests)
- [Text](#text)
- [The consumer tree](#the-consumer-tree)
- [The adapters](#the-adapters)
- [Heard by screen readers](#heard-by-screen-readers)

## Producer and consumer

Platforms call their accessibility interfaces on threads and at times of their own, while a context is used by one thread at a time. So the core produces and the adapters keep. The context builds updates and keeps no platform state ([access_build.c](../../src/access_build.c)). Each adapter keeps its own copy of the tree, answers its platform from it, and queues the requests its platform makes. The host hands each update to the adapter and applies the queued requests on its own thread.

Nothing is built until the host enables a root (`muiAccess_Enable`), up to `limits.accessRoots` roots, one per window. Enabling the first allocates, for every node slot, the copy last sent and the update's buffers; disabling the last frees them. A page cannot tell that a screen reader runs, and most programs never meet one, so a context with no root enabled pays only its table of host data, reserved at creation, and a bit per mark.

## What a node says

Every node is a node of the accessibility tree. Its id is its handle packed in 64 bits, generation high, so a reused slot is new to the platform. Its role is `mui_roleGeneric` until the host gives one, and the consumer flattens generic nodes, so layout-only nodes cost a reader nothing.

Roles are a closed, append-only enum: AccessKit's roles less the browser's document internals (text runs, layout tables, frames, PDF and graphics roles, DPUB, HTML-only parts). Every role left is one every platform can express.

**From the host:** the role; seven texts (label, description, value, placeholder, keyboard shortcut, role description, state description), well-formed UTF-8 without NUL, copied; flags (hidden, read only, required, multiselectable, busy, checkable, mixed, selectable, expandable, expanded, clickable); relations (labelled by, described by, controls, details, flows to, active descendant, error message, popup for); and typed values (level, position and set size, table rows and columns, live, has popup, orientation, sort, invalid, current). Texts and links are allocated when set and freed when replaced, cleared or their node is destroyed.

**Host content reads as its text** through a function the host sets (`muiSetAccessTextFunction`), as layout measures through one. A node whose content is the host's and whose value the host did not set takes that text as its value, and the label role when it has no role. The text component gives the function for text blocks (`muiAccessTextOf`), so text in the UI is named without the host repeating it, and a button takes its name from the label inside it. The text is not the library's to keep, so it is compared by a 64-bit FNV-1a fingerprint per node slot.

**What the library derives**, read when an update is built ([access_record.c](../../src/access_record.c)):

- children, a virtual list's in item order;
- bounds, the border box in the node's own space, with a transform to its parent's carrying its place, local scale and the parent's scroll;
- disabled from the state and hidden for an exiting node; checked and selected from the states on nodes that can have them;
- focusable, the focus and blur actions, and player 0's focus;
- modal for a modal layer's root;
- scroll values, clipping and the scroll actions for scroll containers;
- the value, limits and step of a range, with increment, decrement and set value unless read only, and its axis as orientation;
- position in set and set size for bound items of a virtual list;
- click for the roles that take one or a node flagged clickable, and expand or collapse for expandable nodes.

## Updates

Every mark of another stage also marks the node for accessibility, as do the host's accessibility edits, a scroll offset and a range's value ([Node tree](node-tree.md#dirty-tracking)). `muiBuildAccessUpdate` visits marked subtrees only, derives each marked node and compares it with the copy last sent. An equal node is not sent, so a color animating says nothing to assistive technology.

- A child its parent lists anew, after a list that left it out, is sent with its subtree, since adapters let a node go with the last list holding it.
- A change to the layers under the root (one opening, closing, raised, changing kind or exiting) makes the build compare every node, since it changes which nodes a modal layer covers and so which can take focus. A fingerprint of the layers in order finds it.
- Enabling an enabled root makes its next update whole again, for an adapter starting over.

With a root enabled, memory is a copy of a node record and its place for each node slot, and a frame costs deriving the nodes marked since the last update.

## Requests

`muiPerformAccessAction` performs what the library owns: focus and blur for player 0, scrolling a node into view, a container a page each way or to an offset, and a range's value a step or to a value, reported as input's changes are ([access_action.c](../../src/access_action.c)). A click is routed to the node as `mui_navigateActivate`, so a widget answers assistive technology as it answers Enter, and a widget that heeds only pointer clicks is one a screen reader cannot press. Expand and collapse go to the host as `mui_notificationAccessAction`. A request a node does not take now is refused as empty. One made from an event function is misuse, as other input is.

## Text

A node's value text carries marks (`muiAccessTextMarks`), in byte offsets at characters' starts ([access_text.c](../../src/access_text.c)): the selection, with its caret at the focus, while the text is being edited; where each line starts, as painted; its words; and, where it is shown, a box per line and the grapheme clusters line by line with their left and right edges. The text function gives marks only for a node the update sends, told the part of the node its scrolling ancestors leave shown. It may leave out the clusters of lines far from that part, and the text component gives at most 16,384 clusters, nearest lines first, keeping a block's words between reads.

Every adapter reads text through one module ([access_view.c](../../src/access_view.c)): which nodes have text being edited, the boundaries of characters, words, lines and paragraphs, offsets in code points and UTF-16, and a range's rectangles and the character at a point. Two requests change text and are applied by the host, because the context does not hold text: `mui_actionSetSelection` and `mui_actionReplaceText`. The text component applies them (`muiTextPerformAccessAction`) as a paste, under the field's rules and undone alone. Each adapter tells its platform's text and selection events only for text being edited, so a list that reuses rows does not flood a reader.

## The consumer tree

`muiAccessTree` (`maul-ui/access_tree.h`, the component `MAUL_UI_ACCESS_TREE`, on by default) keeps the copy adapters read ([access_tree.c](../../src/access_tree.c)). It applies an update whole or not at all: checked, copied into memory it owns, then put in place. It lets a node no node lists go with its subtree, refuses lists that would not leave a tree, and tells the adapter what was added, updated (with the old record) and removed, and where the focus moved. It also writes itself as text for tests.

It answers what platforms see as AccessKit's consumer does ([access_apply.c](../../src/access_apply.c)): hidden subtrees and children clipped wholly out of view are left out (but the first past each edge stays, to scroll to); generic nodes are flattened unless labelled; the focus is never left out; names come from the label, the labelling nodes, or for button-like roles the labels and images inside; bounds go through every transform.

It reports `shownChanged` when an update may change what a reader is shown: the root or the focus moved, a node's children changed, or a record changed what the rules read. Adapters walk the shown tree only then, so a value or a name changing costs its own event and no walk.

The consumer is used on one thread, the one the platform calls on: the family allows no lock or wait on a platform's thread, and every platform here calls on the window's thread.

## The adapters

Each adapter is its own component, on by default on its platform, and each owns a consumer tree. Its header is `maul-ui/access_<platform>.h`.

- **UI Automation** ([uia_adapter.c](../../src/uia_adapter.c)) gives UI Automation a provider object per node, made when first asked for and kept by id. Its objects come from the process heap, not the host's allocator, since clients set their lives through COM reference counts and may outlive the adapter. It loads `uiautomationcore.dll` at run time and refuses a thread outside a single-threaded apartment. It includes no UI Automation header: the Windows SDK's define const variables, which in C every file including them defines again, so a program including them as well could not link. It declares the interfaces it implements itself, and its test checks their layouts and values against the SDK's. Its patterns follow the node's flags and actions (Toggle, SelectionItem, ExpandCollapse, RangeValue, Value, Text with ITextProvider2, Scroll, ScrollItem), and it raises UI Automation's events from the consumer's changes while a client listens.
- **AT-SPI** ([atspi_app.c](../../src/atspi_app.c)) follows AT-SPI's two levels: an application joins the accessibility bus, serves the root and asks the registry to embed it, and each window owns a consumer tree under it. It starts no thread: libdbus-1, opened at run time, reads and writes the socket the host polls at each pump, without waiting ([dbus_api.c](../../src/dbus_api.c)). It answers the Accessible, Component, Action, Value, Text, EditableText and Application interfaces, and sends `ChildrenChanged` from one walk of the shown tree after an update, compared with what clients were last told, removals before additions, the topmost node of a change only.
- **ARIA** ([aria_adapter.c](../../src/aria_adapter.c)) mirrors the shown tree into elements of the page, inside the element over the canvas the host names, in a container made invisible as Flutter makes its semantics (`filter: opacity(0%)`). Roles come from a table and states and values are ARIA attributes. A range is an `input type=range`, which touch screen readers adjust. A text input is a real `input` or `textarea`, since only those give screen readers a browser's editing, and its text stays the program's: typed text is kept from the field and asked for as a replacement of the selection. Nothing is built until the program enables it, or a screen reader user presses a visually hidden button. A live node's new name is said through `ariaNotify`, else a live region. [The web](web.md) covers how its JavaScript is built.
- **NSAccessibility** ([ns_adapter.m](../../src/ns_adapter.m)) is Objective-C with manual reference counting, as Maul Window's macOS code is. One `NSAccessibilityElement` subclass object per shown node, cached by id, looks its node up at each call, so an object whose node or adapter is gone answers nothing. Roles and subroles come from a table, as AccessKit maps them, and `isAccessibilitySelectorAllowed:` answers per node so clients offer only what a node has.
- **UIAccessibility** ([uikit_adapter.m](../../src/uikit_adapter.m)) is Objective-C too. VoiceOver never looks inside an element, so, as Flutter does, a shown node with shown children is also a container object, never an element, whose elements are the node's own and then its children's. A node is an element when it says something: a name, a value, an action other than scrolling, being focusable, or a role with a trait. UIKit has no notification for a name or a value changing, so the adapter posts a new screen, a layout change or an announcement.
- **Android** ([android_adapter.c](../../src/android_adapter.c)) shows the tree through a Java class of its own, `maul.ui.AccessProvider` ([AccessProvider.java](../../java/maul/ui/AccessProvider.java)), which the host builds into its application with [proguard-rules.pro](../../java/proguard-rules.pro) among its rules, since a shrinker renames what only native code reaches. The adapter finds the class through the view's class loader and binds its native methods with `RegisterNatives`, so no JNI name is exported. Java asks native code for a node's packed numbers and texts and fills `AccessibilityNodeInfo` itself, a few JNI crossings a node. Texts cross as UTF-16, since the JNI's modified UTF-8 breaks on characters past the Basic Multilingual Plane. Android throws for an event sent while accessibility is off, so the provider sends only while it is on.

Through the [Window glue](window-glue.md), one call makes the adapter for the window's platform over Maul Window's native handles.

## Heard by screen readers

Tests check each adapter against its platform's reading, but only a screen reader shows what a user hears. The samples, in a window through the glue, are walked by screen readers from steps files (`tools/screen_reader/`): Orca locally ([screen_reader_walk.sh](../../tools/screen_reader_walk.sh)), VoiceOver on a macOS runner and NVDA on a Windows runner through Guidepup, and NVDA over the ARIA adapter in Chrome. Each walk checks the phrases its file expects: the controls' names and a state, a field's name and the words typed into it. Their timing depends on the runners, so they run nightly ([Test environments](../operations/test-environments.md#screen-readers)).

A window should open with a control focused, as platform toolkits' windows do. Orca 46 announces nothing for the first focus of a window that opened with none.
