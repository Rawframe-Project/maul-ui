# mui-0008. Accessibility

Status: Accepted

## Context

Screen readers and other assistive technology read a UI through the
platform's accessibility API: UI Automation on Windows, AT-SPI on
Linux, NSAccessibility on macOS, UIAccessibility on iOS, Android's
accessibility nodes, and the browser's accessibility tree on the web.
Each wants the same things of every element: what it is, its name and
value, its states, where it is, its children, which one has the focus,
and what it can be asked to do. AccessKit gives a toolkit one schema
for all of them: nodes sent whole in updates, the focus with every
update, and requests coming back. Its platforms call in on threads of
their own (UI Automation does), while a Maul UI context is used by one
thread at a time (family record 0017). Maul UI already holds most of
what the schema asks for: rectangles, scrolling, focus, states, value
ranges and virtual lists.

## Decision

- **The core produces, adapters keep** (`maul-ui/access.h`). The
  library builds updates in AccessKit's shape: the nodes that changed,
  each whole, the focus with every update and the root with the first.
  It keeps no platform state. Adapters keep their own copies under
  their own locks and answer their platforms from them; the host hands
  them each update, and applies on its thread the requests they queue.
- **Every node is a node.** Its id is its handle packed in 64 bits,
  generation high and index low, so a reused slot is new to the
  platform. Its role is `mui_roleGeneric` until the host gives one; an
  adapter leaves generic nodes out and shows their children in their
  place, so layout-only nodes cost the reader nothing.
- **Roles** are a closed, append-only enum: AccessKit's roles less the
  browser's document internals (text runs, layout tables, frames, web
  areas, PDF and graphics roles, DPUB, inline formatting, HTML-only
  parts). Every role left is one every platform expresses.
- **The host's data per node:** the role; seven texts (label,
  description, value, placeholder, keyboard shortcut, role description,
  state description), well-formed UTF-8 without NUL, copied; and flags
  (hidden, read only, required, multiselectable, busy, checkable,
  mixed, selectable, expandable, expanded, clickable); relations to
  other nodes (labelled by, described by, controls, details, flows to,
  and the single active descendant, error message and popup it is for),
  sent as links in order of kind; and typed values (level, position
  and set size, table rows and columns with indices and spans, live,
  has popup, orientation, sort, invalid, current). Nodes with data are
  bounded by `limits.accessNodes`; texts and links are allocated when
  set and freed when replaced, cleared or their node is destroyed. A
  node named that is destroyed later stays named, as adapters pass over
  ids they do not hold.
- **Host content reads as its text** through a function the host sets
  (`muiSetAccessTextFunction`), as layout measures through one: a node
  whose content is the host's and whose value the host did not set
  takes the text the function gives as its value, and the label role
  when the host gave it none. The text component gives one for text
  blocks (`muiAccessTextOf`), so text in the UI is named without the
  host repeating it, and a button's name comes from the label inside
  it by the consumer's rule for names. The function may not edit the
  context; its text must be well-formed UTF-8, and is left out
  otherwise. Its text is compared by a 64-bit fingerprint, kept per
  node slot, since it is not the library's to keep.
- **What the library derives**, read when an update is built: the
  children (a virtual list's in item order); bounds, the border box in
  the node's own space, and a transform to its parent's, the node's
  place moved by the parent's scroll as painting moves it; disabled
  from the state, and hidden for a node exiting; checked and selected
  from the states on nodes that can have them; focusable, and the
  focus and blur actions, from the focus rules and player 0's focus;
  modal for a modal layer's root; scrolling values, clipping and the
  scroll actions for scroll containers; the value, limits and step,
  with increment, decrement and set value unless read only, for
  ranges, and their axis as orientation unless the host gives one;
  position in set and set size for items bound in a virtual list,
  unless the host gives them; click for the roles that take one or a node flagged
  clickable; expand or collapse for expandable nodes.
- **Updates from marks.** Every mark of another stage also marks the
  node for accessibility, as do the host's accessibility edits, a
  scroll container's offset (with its children) and a range's value. A
  build visits marked subtrees only, derives each marked node and
  compares it with the copy last sent: an equal node is not sent, so a
  color animating is nothing to assistive technology. A child its
  parent lists anew, after a list that left it out, is sent with its
  subtree, since adapters let a node go with the last list holding it. A change to the layers under the root (one opening, closing,
  raised, changing kind or exiting) makes the build compare every node,
  since it changes which nodes a modal layer covers and so which can
  take focus; a fingerprint of the layers in order finds it.
- **Lazily, per root** (`muiAccess_Enable`, `muiAccess_Disable`): up to
  `limits.accessRoots` roots, one per window. Enabling the first
  allocates, for every node slot, the copy last sent and the update's
  buffers; the first update for a root sends every node; disabling the
  last frees them. Enabling an enabled root makes its next update whole
  again, for an adapter starting over.
- **Requests** (`muiPerformAccessAction`) the library performs where it
  owns what they change: focus and blur for player 0, scrolling a node
  into view, a container a page each way or to an offset, and a
  range's value a step or to a value, reported as input's are. A click
  is routed to the node as a navigation event, `mui_navigateActivate`,
  so a widget answers assistive technology as it answers Enter. Expand
  and collapse are posted to the host as
  `mui_notificationAccessAction`. A request a node does not take now
  is refused as empty; one from an event function is misuse, as other
  input is.

- **The consumer** (`maul-ui/access_tree.h`, the component
  `MAUL_UI_ACCESS_TREE`, on by default) keeps the copy adapters read:
  a `muiAccessTree` applies updates whole or not at all (checked, then
  copied into memory it owns, then put in place), lets a node no node
  lists go with its subtree, refuses lists that would not leave a tree
  (a child listed twice, the root listed, a node under itself), tells
  the adapter what was added, updated (with the old record), removed
  and where the focus moved, and writes itself as text for tests. It
  answers what platforms see as AccessKit's consumer does: hidden
  subtrees and children clipped wholly out of view left out (but the
  first past each edge, to scroll to), generic nodes flattened unless
  labelled (the core makes every node generic until the host gives a
  role, so a labelled one must stay to be heard), the focus never left
  out for itself; names from the label, the labelling nodes, or for
  button-like roles the labels and images inside; bounds through every
  transform, the root's included. It is used on one thread, the one
  the platform calls on: family record 0017 allows no lock or wait on
  a platform's thread, and with UI Automation's COM threading on an STA
  thread every platform calls on the window's thread.
- **The UI Automation adapter** (`maul-ui/access_uia.h`, the component
  `MAUL_UI_UIA`, on by default on Windows) owns a consumer tree and
  gives UI Automation a provider object per node, made when first
  asked for and kept by id, and a root that stands for whichever node
  is the tree's root, for Maul Window's `mwinRequestAccessibilityRoot`
  or the host's own `WM_GETOBJECT`. Its objects come from the process
  heap, not the host's allocator: clients set their lives through COM
  reference counts, so they may outlive the adapter (the one exception
  to record 0010 here). It loads `uiautomationcore.dll` at run time,
  as Maul Window does, and refuses a thread outside a single-threaded
  apartment with `mui_errorPlatform`. It includes no UI Automation header:
  the Windows SDK's define const variables, which in C every file
  including them defines again, so a program including them as well
  could not link. It declares the interfaces it implements and writes
  the ids it uses, and its test checks their layouts and values
  against the headers. Its patterns follow the node's flags and
  actions: Toggle for a checkable node and SelectionItem for a
  selectable one (both clicked, as on the screen, and never also
  Invoke), ExpandCollapse, RangeValue for a numeric node, Value for a
  value text (read only until text editing brings setting text),
  Scroll for a scrolling container (a small step scrolls a page, as the
  tree knows no line height), and ScrollItem. Applying an update raises
  UI Automation's events from the changes the consumer reports, while
  a client listens (`UiaClientsAreListening`): the focus moving, a
  property change for each compared property whose value differs
  between a node's old record and its new one, and a live region's
  text changing. They are raised as the changes are reported, as the
  tree then holds the whole update and nothing else holds the thread;
  AccessKit queues them only to leave its lock first. A name is
  compared by the node's own text, so a name drawn from other nodes
  changes without an event.
- **The AT-SPI adapter** (`maul-ui/access_atspi.h`, the component
  `MAUL_UI_ATSPI`, on by default on Linux) follows AT-SPI's two levels:
  an application (`muiAtspiApp`) joins the accessibility bus
  (`AT_SPI_BUS_ADDRESS`, else the session bus's `org.a11y.Bus`), serves
  the root and asks the registry to embed it; each window
  (`muiAtspiAdapter`) owns a consumer tree whose root is a child of
  the application's. It starts no thread: libdbus-1, opened at run
  time with the ABI it uses declared as Maul Window declares it, reads
  and writes the socket the host polls, at each pump, without
  waiting. Nodes are paths `/org/a11y/atspi/accessible/w<window>n<id>`,
  answered by one filter: the Accessible interface (children as
  shown, roles from a table, names, states, parents, indexes), the
  Component interface (extents in pixels, the node under a point,
  focusing and scrolling asked of the host), the Action interface
  (the node's actions by AT-SPI's names, done through the host), the
  Value interface on numeric nodes (its current value set through the
  host's set-value action, within the range), relations (to nodes the
  window holds) and attributes (level, place in a set, live), and the
  Application interface on the root. An object answers only the
  interfaces it lists. Events are `org.a11y.atspi.Event.Object`
  signals: states, names, descriptions, values, roles and
  announcements from comparing each replaced record with its new one;
  `ChildrenChanged` from one walk of the shown tree after each update,
  compared with a record of what clients were last told (each shown
  node's shown parent and index), removals before additions and the
  topmost node of a change only, as AT-SPI clients cache children;
  the focus last. The walk runs only when an update may change what
  is shown, which the consumer tells (`shownChanged`, from the view's
  own rules: the root or the focus moved, a node's children changed,
  or a record changed what the rules read: hidden, clipping, a generic
  node's role or label, a box where it or its parent clips). A value or
  a name changing costs its signal and no walk.
- **The ARIA adapter** (`maul-ui/access_aria.h`, the component
  `MAUL_UI_ARIA`, on by default for Emscripten) mirrors the shown tree
  into elements of the page, the web's only accessibility interface:
  in an element over the canvas the host names by a CSS selector (Maul
  Window's accessibility host), a container of its own made invisible
  as Flutter makes its semantics (`filter: opacity(0%)`), then one
  element a shown node, nested as the shown tree is and placed at the
  node's box relative to its parent's. Roles come from a table; names
  are `aria-label`, or the element's text where ARIA names from
  content; states and values are ARIA attributes; a range the host
  sets is an `input type=range`, which touch screen readers adjust.
  The same `shownChanged` signal starts one walk that removes, makes
  and places elements; `updated` writes the attributes that changed
  and places again the boxes that moved. Nothing is built until the
  program enables it or a screen reader user presses a visually hidden
  button, as a page cannot tell that a screen reader runs. A client's
  click is the node's click action, or expands or collapses what does
  that without one; a DOM focus the adapter did not give is the focus
  action; a range's input is the set-value action. The program's focus
  takes the DOM focus only from within the adapter's elements, or from
  the enabling button, as the canvas needs it for the keys otherwise.
  Relations are ARIA id references; a live node's new name is said
  through `ariaNotify`, else a live region emptied after 300 ms; a
  scroll of the host is put back. The test runs in headless Chrome,
  comparing the browser's accessibility tree.
- **The NSAccessibility adapter** (`maul-ui/access_ns.h`, the
  component `MAUL_UI_NSACCESSIBILITY`, on by default on macOS) is
  Objective-C with manual reference counting, as Maul Window's macOS
  code is. One `NSAccessibilityElement` subclass object a shown node,
  made when first asked for and cached by id, looks its node up at each
  call, so that an object whose node or adapter is gone answers
  nothing. The root's parent is the view the host names; frames go
  from the view (flipped or not) to the window and to the screen.
  Roles and subroles come from a table, as AccessKit maps them;
  `isAccessibilitySelectorAllowed:` answers per node, so that clients
  offer only what a node has. Notifications go as AccessKit posts
  them: a title or value changed on the node's object (static text's
  name as its value), the focused element changed, an element
  destroyed when a node a client saw goes, the layout changed when
  the consumer says the shown tree may have, and an announcement on
  the window for a live node's new name, high priority when
  assertive. The test asks the objects as the accessibility server
  does, since the client API needs a trusted process, and records the
  notifications in place of AppKit's.
- **The UIAccessibility adapter** (`maul-ui/access_uikit.h`, the
  component `MAUL_UI_UIACCESSIBILITY`, on by default on iOS) is
  Objective-C with manual reference counting, as Maul Window's iOS
  code is. VoiceOver never looks inside an element, so, as Flutter
  does, a shown node is an element object and a shown node with shown
  children also a container object, never an element, whose elements
  are the node's own and then its children's objects; both are cached
  by id and look their node up at each call. A node is an element
  when it says something (a name, a value, a description, an action
  other than scrolling, being focusable, or a role with a trait).
  Traits come from the role and flags; a container's type from its
  role; a container's frame is the view's, so that a child outside its
  parent stays reachable by touch. Activation clicks, or expands and
  collapses; a range without a click takes it without acting.
  VoiceOver's scroll directions name what comes into view vertically
  and the finger's way across. The escape gesture has no action.
  UIKit has no notification for a name or a value changing, so the
  adapter posts what it has: a new screen when the root changes or a
  node turns modal, at the element VoiceOver should move to; else the
  layout, with the focused element when the focus moved; and a live
  node's new name announced, queued behind current speech when
  polite. The test is an application in the iOS simulator, asking the
  objects as VoiceOver does and recording the notifications.
- **The Android accessibility adapter** (`maul-ui/access_android.h`,
  the component `MAUL_UI_ANDROID_ACCESSIBILITY`, on by default on
  Android) shows the tree through a Java class of Maul UI's own,
  `maul.ui.AccessProvider` (`java/maul/ui/AccessProvider.java`), which
  the host builds into its application; the adapter finds it through
  the view's class loader and binds its native methods with
  `RegisterNatives`, so that no JNI name is exported. Java asks native
  code for a node's packed numbers (class, states, actions, box,
  parent, children, range, live setting) and its texts and fills the
  `AccessibilityNodeInfo` itself, a few JNI crossings a node. Virtual
  ids are given as clients first see nodes and freed when they go, the
  oldest freed reused first. Class names come from the role, as
  AccessKit's; a text view's text is its name and a text field's its
  value (the name its hint); any other node's name is its content
  description and its value text its state description. Texts cross as
  UTF-16, as the JNI's modified UTF-8 breaks on characters past the
  Basic Multilingual Plane. The provider keeps the screen reader's
  cursor itself and answers `virtualViewAt` for touch exploration
  without depending on Maul Window. The test is an application in the
  Android emulator, built without Gradle, whose Java asks the provider
  as clients do.

## Consequences

Hosts write accessibility once, for every platform, and most of it is
not written at all: layout, scrolling, focus, states, ranges and lists
are read where they already are. A context with no root enabled pays
its table of host data, reserved at creation (168 bytes an entry and
4 a node slot), and a bit per mark. With one enabled, a frame's cost is
deriving the nodes marked since the last update, and memory is a copy
of a node record and its place for each node slot, 308 bytes each.
The adapters come next.
