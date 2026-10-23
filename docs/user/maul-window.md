# Maul Window

`maul-ui-window` (`MAUL_UI_WINDOW`) connects a context to a [Maul Window](https://github.com/Rawframe-Project/maul-window) window: its input, its text input and clipboard, its gamepads and its accessibility. It is a static library of its own, `maul-ui-window::maul-ui-window`, built against an installed Maul Window or one fetched at its release tag. A program on another window library does the same translation itself, and this page lists what that takes.

## Input

A glue (`muiCreateWindowGlue`) belongs to one window and one context root. Each record the program drains from Maul Window goes to `muiWindowGlue_HandleEvent`, which feeds it to the context and says whether the UI used it. The program hands the rest to its game:

- keys and typed text pass as they come, since Maul UI's keys are Maul Window's;
- the cursor, touches and the pen become pointers, and a press the UI took keeps the pointer the UI's until it is released;
- the wheel turns at the cursor's last place;
- a lost focus or an input reset cancels every pointer holding a button, so nothing stays pressed.

`muiWindowGlue_Tick` keeps time between records for repeats, so call it once a frame.

## Gamepads

One glue per Maul Window context can take the gamepads (`gamepads` in its def), turning their records into navigation for the player a function of the program's names for each pad: the d-pad and the left stick past a threshold are the four directions, the south face button activates and the east cancels (the other way with `confirmEast`, where the platform's convention puts confirm east), and the shoulders move to the previous and next control. A held direction repeats after 400 ms and then every 100 ms unless the def says otherwise.

## Text fields

- `muiWindowGlue_SetCaret` tells the window that a node takes text and where its caret is, so the platform places its candidate window there and shows its input method; the null id stops text input. `muiWindowGlue_SetTextCaret` does the same at a position of a node's text.
- `muiWindowCompose` shows the window's preedit in an editing field, and `muiWindowSetComposition` sets one into any text block.
- `muiWindowGlue_WriteClipboard` is the text editor's clipboard writer. A paste the editor asks for is requested with `muiWindowGlue_RequestPaste`, and `muiWindowGlue_Paste` takes the window's answer into the block when it arrives, since Maul Window reads the clipboard by request.
- `muiWindowGlue_RequestKeyboard` shows the on-screen keyboard for a field's input purpose, or hides it.

## Accessibility

A `muiWindowAccess` (`muiCreateWindowAccess`) makes the adapter for the window's platform over Maul Window's native handles, enables the glue's root, and hands the window the adapter's root whenever it changes. Each frame, `muiWindowAccess_Update` sends the update and performs what assistive technology asked, unless the program takes a request itself. `muiWindowAccess_HandleEvent` follows the window's scale and accessibility records.

On Maul Window's test backend there is no adapter (`muiWindowAccess_HasAdapter` is false), and `muiWindowAccess_GetTree` gives the tree a screen reader would read, which tests can check.

## The samples

The samples in `samples/` are programs on Maul Window with the reference renderer: a tour, controls, ranges, text, pickers, tabs and menus, dialogs, collections and color. Build them with `-DMAUL_UI_WINDOW=ON -DMAUL_UI_RHI=ON`. Each runs in a window, or headless with `--headless`, where it checks its own frames, which is how the tests run them.
