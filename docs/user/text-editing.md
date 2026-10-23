# Text editing

A block opted into editing keeps a selection, an undo history and its field's rules: one line or many, read only, a password shown as bullets, a filter taking integers or decimals only, a maximum number of characters, and an input purpose for on-screen keyboards. It takes typing, pastes, deletions, undo and redo, and its text scrolls inside the node to keep the caret in view.

```c
// Makes a label's block an editable field: one line, at most 64
// characters, undo keeping the last 100 edits.
static muiResult MakeField(muiTextService* service, muiTextBlockId block)
{
    muiTextEditDef def = muiDefaultTextEditDef();
    def.maxLength = 64;
    def.undoLimit = 100;
    return muiTextBlock_SetEditing(service, block, &def);
}
```

## Contents

- [Feeding events](#feeding-events)
- [Drawing the caret and selection](#drawing-the-caret-and-selection)
- [Input methods](#input-methods)
- [Things to know](#things-to-know)

## Feeding events

`muiTextEditEvent` maps a platform's events onto the field: keys under the PC's or the Mac's shortcuts, typed text, and the pointer's presses and drags placing the selection. The clipboard and focus stay the program's. Copying and cutting write through the input's function, and a paste is asked for, the program answering it once its clipboard is read.

```c
// What the program's clipboard holds, as the editor copies and cuts.
static char s_clipboard[256];

static void WriteClipboard(void* user, const char* text, size_t length)
{
    (void)user;
    size_t kept = length < sizeof s_clipboard - 1 ? length : sizeof s_clipboard - 1;
    memcpy(s_clipboard, text, kept);
    s_clipboard[kept] = '\0';
}

// Hands an event to the focused field: keys, typed text and the
// pointer; a paste it asks for is answered from the clipboard. Whether
// the field took the event.
static bool FieldEvent(muiContext* context, muiTextService* service, muiNodeId field,
                       muiTextBlockId block, const muiEvent* event)
{
    muiTextHost host = {service, context};
    const muiTextEditInput input = {mui_keymapPc, WriteClipboard, NULL};
    muiTextEditOutcome outcome = {false, false, false, false};
    if (muiTextEditEvent(&host, field, event, &input, &outcome) != mui_success)
    {
        return false;
    }
    if (outcome.paste)
    {
        bool pasted = false;
        if (muiTextBlock_Paste(service, block, s_clipboard, strlen(s_clipboard), &pasted) ==
            mui_success)
        {
            outcome.changed = outcome.changed || pasted;
        }
    }
    // A moved caret changes only what assistive technology reads.
    if (outcome.changed)
    {
        (void)muiNode_MarkContentChanged(context, field);
    }
    else if (outcome.selected)
    {
        (void)muiNode_MarkAccessChanged(context, field);
    }
    return outcome.handled;
}
```

A field fed "hello" and then Backspace holds "hell". Control and A, then C, copies it; two pastes give "hellhell", and Control and Z takes the second back. Shift with a key extends the selection, and Control moves and deletes by word. A moved caret changes what assistive technology reads, so the node is marked for accessibility.

The keymaps follow the platforms: `mui_keymapPc` follows Windows and the Linux toolkits, and `mui_keymapMac` follows AppKit's standard bindings. A shortcut needs exactly its modifiers, so AltGr still types.

## Drawing the caret and selection

The library paints the text, not the caret or the selection, whose look is the program's. The editing primitives give their places in the node's content box as drawn: `muiTextGetCaret` the caret, and `muiTextGetRangeRects` a range's rectangles, one for each stretch of it side by side on a line, since mixed directions split a range.

```c
// Where to draw the caret, in the field's content box as laid out.
static bool CaretOf(muiContext* context, muiTextService* service, muiNodeId field,
                    muiTextBlockId block, muiTextCaret* caretOut)
{
    muiTextHost host = {service, context};
    muiTextSelection selection;
    float width = muiNode_GetContentRect(context, field).width;
    return muiTextBlock_GetSelection(service, block, &selection) == mui_success &&
           muiTextGetCaret(&host, field, width, selection.caret, caretOut) == mui_success;
}
```

`muiTextHitTest` finds the position under a point, and `muiTextMove` moves one by cluster, word or line, as the keys do.

## Input methods

An input method's composition is shown in the field with `muiTextBlock_Compose` as the platform sends it, and committed as typing. Undo waits until it ends. The candidate window belongs near the caret: give the platform the caret rectangle, mapped into the window with `muiNode_MapToRoot`. The [Maul Window](maul-window.md) glue does all of this for a program.

## Things to know

- A password field is laid out, painted and read by accessibility as a bullet per character. Copying it gives nothing, and input methods are refused in it, as platforms turn them off there.
- Backspace deletes one code point, as Android and browsers do, so an accent can be taken back alone, but a whole emoji or flag goes at once.
- A maximum length counts what the user sees as characters (grapheme clusters), not bytes.
- On a single line, a pasted line break becomes a space.
- Text changed by the program outside the editor empties the undo history.
- A screen reader's requests to move the selection or change the text go to `muiTextPerformAccessAction`, which applies them as a paste under the field's rules ([Accessibility](accessibility.md)).
