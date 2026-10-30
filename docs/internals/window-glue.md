# Window glue

`maul-ui-window` (the option `MAUL_UI_WINDOW`, off by default) connects a context to a Maul Window window. It is a static library of its own beside the core, in `window/`, with Maul Window found installed or fetched at its release tag ([Window.cmake](../../cmake/Window.cmake)). Maul Window names the Maul Unicode release it is built and tested with, and configuring stops when that differs from Maul UI's own pin ([Unicode.cmake](../../cmake/Unicode.cmake)): with the text component, the fetched Maul Window builds against Maul UI's Maul Unicode, and without it, against its own. The core and the text component never see it. A host that uses another window library writes the same translation itself; the glue is the reference for it.

## Input

A glue per window takes each record the host drains from Maul Window and feeds it to the context, reporting whether the UI handled it, so the host passes the rest to its game ([glue.c](../../window/src/glue.c)). Maul UI's input was shaped after Maul Window's, so most of it passes as it comes:

- Keys (codes, meanings, modifiers, repeat) and typed text pass unchanged.
- The cursor is a mouse pointer. Maul Window's button less one is Maul UI's button index, and the held masks agree.
- A touch is a touch pointer, its 64-bit id given one of ten slots while it lasts.
- The pen is a pen pointer: its tip in contact is the primary button, or button 5 while it erases, and its barrel the secondary, as the W3C's Pointer Events number them.
- The wheel turns at the cursor's last place, which the glue keeps, with the modifiers last reported.
- A reset or a lost focus cancels every pointer holding a button, since the platform will not report the releases that happen elsewhere.

Each event's records are dispatched at once. A pointer's input is the UI's when a record is handled or lands on a node that does not pass input through, and a press that was the UI's holds the pointer until its release. This is the rule a host follows for what it hands its game.

## Gamepads

A glue that takes gamepads makes their records navigation for the player a host function names ([gamepads.c](../../window/src/gamepads.c)): the d-pad and the left stick past a threshold are the four directions, let go below seven tenths of the threshold so a stick at the edge does not flicker; the south face button activates and the east cancels, or the other way where the platform's convention puts confirm east; the shoulders are previous and next. A held direction repeats after a delay and then at an interval (400 and 100 ms by default), timed by the records' clock, which `muiWindowGlue_Tick` continues between records. Repeat stays out of the core, which keeps no clock; hosts with their own bindings repeat as their games do.

## Text input

`muiNode_SetWindowCaret` asks the window to accept text with its caret at a rectangle of a node, carried into the window by `muiNode_MapToRoot`, and hands that rectangle back for a game that draws its own candidates. The null id stops text input.

With the text component:

- `maul-ui-window/composition.h` sets a preedit into a text block and places the caret at a position of a node's text ([composition.c](../../window/src/composition.c)). Maul Window's preedit segments and styles are Maul UI's composition's, member for member and value for value. Preedit records are not the UI's, since the core has no composition record and the host knows which of its fields edits. For the same reason a reset, after which Maul Window shows no composition (maul-window#194), is the host's to pass to its field as an empty preedit: the glue cancels the pointers at a reset, but it cannot reach a field's composition. `muiNode_RequestWindowKeyboard`, beside them, shows the on-screen keyboard for an editing field's purpose, or hides it.
- `maul-ui-window/clipboard.h` gives the text editor the window's clipboard: a writer for copies and cuts, a request for a paste, and the window's answer taken into the block, as Maul Window reads the clipboard by request ([clipboard.c](../../window/src/clipboard.c)).

## Accessibility

With the consumer tree, one `muiWindowAccess` makes the adapter built for the window's platform over Maul Window's native handles ([access.c](../../window/src/access.c)): UI Automation over the HWND, NSAccessibility and UIAccessibility over the view, Android's over the activity's view with the activity's JNIEnv, ARIA in the element Maul Window keeps over the canvas, and AT-SPI for X11 and Wayland in the program's application. It enables the glue's root, sends its updates each frame, has the context perform what clients ask unless the host takes a request, rescales on the window's scale changes, and hands the window the adapter's root (`mwinWindow_RequestAccessibilityRoot`) whenever it changes.

A window with no adapter, Maul Window's test backend's among them, keeps the tree alone, which tests read. The glue's tests run on that headless backend, with the text component and without it.
