# Text editing

Editing is built in two layers. The editing primitives answer questions about laid-out text: where a point falls, where a caret goes, what a move or a deletion reaches. The editor keeps a field's state on top of them: a selection, the rules of the field and an undo history. Focus, the clipboard and drawing the caret and selection stay the host's, since their look and their platform integration are the host's.

## Contents

- [Positions](#positions)
- [Hit testing, carets and moves](#hit-testing-carets-and-moves)
- [Deletion](#deletion)
- [Input method compositions](#input-method-compositions)
- [The editor](#the-editor)
- [Keeping the caret in view](#keeping-the-caret-in-view)
- [Passwords](#passwords)

## Positions

The primitives work on a node's text laid out as painting lays it out, at the content width the caller gives ([text_hit.c](../../src/text_hit.c)). A position is a byte offset between grapheme clusters and an affinity, downstream or upstream. The affinity picks the place of an offset that has two: the end of a wrapped line or the start of the next, and either side of a change of direction.

Each line is read as boxes of grapheme clusters left to right, from the glyphs painting draws ([text_boxes.c](../../src/text_boxes.c)). The letter spacing after each cluster is included, and a glyph several clusters share, such as a ligature, is cut into equal parts.

## Hit testing, carets and moves

- `muiTextHitTest` finds the line at a point's y and the nearer edge of the box at its x.
- `muiTextGetCaret` gives the edge of a position's box: leading for downstream, trailing for upstream.
- `muiTextGetRangeRects` gives a rectangle for each stretch of boxes in a range, so a range across a change of direction is several rectangles.
- `muiTextMove` moves a position ([text_move.c](../../src/text_move.c)): to the next or previous grapheme cluster boundary in the text; a cluster left or right on screen, crossing to the next line or the one before as the paragraph's direction has it; to the start or end of a word (a UAX #29 word segment with a letter or a number); to its line's start or end; up or down a line at an x the caller keeps; or to the text's start or end.

## Deletion

`muiTextBlock_FindDeletion` gives what a deletion removes ([text_delete.c](../../src/text_delete.c)). Forward it is the next grapheme cluster. Backward it is one code point, as Blink and Android delete, so a user can take back an accent without its letter, except where that would leave a broken cluster: a cluster with an emoji, a regional indicator or a keycap goes whole, a variation selector goes with the code point before it, and CR goes with its LF.

## Input method compositions

Compositions live in the block's text, as browsers and platform controls keep them, so they shape, wrap and reorder with the text around them. `muiTextBlock_SetComposition` replaces the composition, or inserts one at an offset, with the method's text and its styled segments; an empty text removes it. `muiTextBlock_EndComposition` keeps its text as typed. A replacement before or after a composition moves it, and one over it ends it.

Painting underlines a composition after the glyphs, where an underline goes: thin for underlined and converted segments, twice as thick for the target, none for plain, and the whole composition thin when it has no segments. The host maps its window library's compositions to segments and gives the platform the caret rectangle from `muiTextGetCaret` for the candidate window.

## The editor

A block opted into editing with a field's rules (`muiTextBlock_SetEditing`: multi-line, read-only, password, an integer or decimal filter, a maximum length, an undo limit) keeps a selection, the x that vertical moves keep, and an undo history ([text_editor.c](../../src/text_editor.c)).

- **Rules.** Typing, pasting and deleting go through the field's rules ([text_rules.c](../../src/text_rules.c)). Control characters other than tab and line breaks are dropped. On a single line each line break becomes a space, as Firefox's paste does. A number filter keeps a sign at the start and then digits, with at most one point for a decimal, dropping what would break it. A maximum length counts grapheme clusters, as Flutter's does, cutting what goes in.
- **Undo** takes typing back a word at a time, a run of backspaces or of forward deletions whole, as AppKit does, and anything else alone ([text_history.c](../../src/text_history.c)). An edit after undoing drops what was undone, the oldest past the limit goes, and text changed outside the editor empties the history.
- **Selection by pointer.** One, two and three clicks select an edge, a word and a paragraph, and a drag extends by that unit. A move without Shift over a selection collapses it toward the edge it goes.
- **Events.** `muiTextEditEvent` takes a listener's events: typed text types; keys act by a keymap (`mui_keymapPc` after Windows and the Linux toolkits, `mui_keymapMac` after AppKit's standard bindings, shortcuts wanting exactly their modifier so AltGr still types); and a first-button press places the selection by its click count ([text_keys.c](../../src/text_keys.c)).
- **Clipboard.** Copies and cuts go to a host function, and a password copies nothing. A paste is asked of the host, whose clipboard may answer later, and arrives as `muiTextBlock_Paste`.
- **Compositions** go through `muiTextBlock_Compose`: shown at the caret, replacing a selection as an edit, taken out by empty text with the commit arriving as typing. Undo waits until a composition ends, and an edit while one shows takes it out first.
- **On-screen keyboards.** A field's rules name what it takes (`muiInputPurpose`: text, email, URL and so on), a password's and a number filter's own purposes first, with the values of Maul Window's.

## Keeping the caret in view

An editing block's text scrolls in its content box: from where it was, just far enough that the caret shows on its line, a unit wide, then no further than the text reaches, so a shortened text comes back, as browsers' inputs and text areas do. The scroll folds into where lines start and the lines' tops, so painting, hits, carets, rectangles and vertical moves all work in points as drawn. Measuring and the first baseline stay unscrolled, since layout must not move with the caret. A node that clips keeps the scrolled text inside its box.

## Passwords

An editing block with the password rule is laid out, painted, hit and read by accessibility as its mask, a bullet (U+2022, as Chrome and AppKit draw it) per grapheme cluster, made again when its text changes ([text_mask.c](../../src/text_mask.c)). Carets, hits, moves and selection rectangles map offsets by cluster. Copying gives nothing, moves by word and double clicks take the text as one word so its words stay unseen, and compositions are refused, as platforms turn input methods off there. Showing the last typed character for a moment, as Android does, is refused: it is timed and shows the password.
