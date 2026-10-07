// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Single and multi-line text inputs, composed over Maul UI's text
// editing primitives (maul-ui/text_edit.h) with the roles and keys of
// the WAI-ARIA Authoring Practices (record mui-0005). Each field is one
// node: its box, its text block as host content, the text input role and
// focus. The host keeps the caret and the selection: a press places the
// caret (muiTextHitTest), typed text replaces the selection
// (muiTextBlock_Replace), Backspace and Delete remove a cluster
// (muiTextBlock_FindDeletion) or the selection, arrows, Home and End
// move by clusters and lines (muiTextMove), Shift extending the
// selection, Enter breaking a line in the multi-line field; its paint
// function draws the selection's rectangles and the caret over the
// text. An input method's preedit goes into the focused field's block
// (maul-ui-window/composition.h) and the window's candidate box follows
// the caret (muiWindowGlue_SetTextCaret). Headless, a person's typing,
// editing, selecting and composing is posted and the texts, the caret's
// pixels, the window's caret and the accessibility tree checked.

#include "app.h"

#include "maul-ui-window/composition.h"
#include "maul-ui/access.h"
#include "maul-ui/access_tree.h"
#include "maul-ui/event.h"
#include "maul-ui/focus.h"
#include "maul-ui/interaction.h"
#include "maul-ui/pointer.h"
#include "maul-ui/style.h"
#include "maul-ui/text_edit.h"
#include "maul-ui/text_style.h"
#include "maul-window/test.h"

#include <math.h>
#include <string.h>

#define FIELDS 2

// Colours, sRGB bytes.
static const uint8_t s_background[3] = {24, 32, 40};
static const uint8_t s_text[3] = {242, 242, 242};
static const uint8_t s_field[3] = {48, 56, 64};
static const uint8_t s_caret[3] = {255, 200, 64};

typedef struct Field
{
    muiNodeId node;
    muiTextBlockId block;
    bool multiline;
    // The selection runs from the anchor to the caret; empty when they
    // meet.
    uint32_t anchor;
    muiTextPosition caret;
    // The x lines keep moving up and down to, taken from the caret at
    // the first such move; -1 until then.
    float preferredX;
} Field;

typedef struct Texts
{
    Field fields[FIELDS];
    SampleApp* app;
    // The field the focus is in, or -1, and the caret the window was
    // last given.
    int focused;
    muiTextPosition placedCaret;
    mwinRect placed;
} Texts;

static int FieldOf(const Texts* texts, muiNodeId node)
{
    for (int i = 0; i < FIELDS; i++)
    {
        if (SampleSame(texts->fields[i].node, node))
        {
            return i;
        }
    }
    return -1;
}

static float Width(const SampleApp* app, const Field* field)
{
    return muiNode_GetContentRect(app->context, field->node).width;
}

static uint32_t Start(const Field* field)
{
    return field->anchor < field->caret.offset ? field->anchor : field->caret.offset;
}

static uint32_t End(const Field* field)
{
    return field->anchor > field->caret.offset ? field->anchor : field->caret.offset;
}

// Replaces a range of a field's text, the caret after what went in.
static void Replace(SampleApp* app, Field* field, uint32_t start, uint32_t end, const char* text,
                    size_t length)
{
    SampleAppCheck(app,
                   muiTextBlock_Replace(app->text, field->block, start, end, text, length) ==
                           mui_success &&
                       muiNode_MarkContentChanged(app->context, field->node) == mui_success,
                   "an edit");
    field->caret = (muiTextPosition){start + (uint32_t)length, mui_affinityDownstream};
    field->anchor = field->caret.offset;
    field->preferredX = -1.0f;
}

// Typed text replaces the selection.
static void Insert(SampleApp* app, Field* field, const char* text, size_t length)
{
    Replace(app, field, Start(field), End(field), text, length);
}

// Backspace or Delete: the selection, or a cluster beside the caret.
static void Delete(SampleApp* app, Field* field, muiTextDeletion deletion)
{
    uint32_t start = Start(field);
    uint32_t end = End(field);
    if (start == end && muiTextBlock_FindDeletion(app->text, field->block, field->caret.offset,
                                                  deletion, &start, &end) != mui_success)
    {
        return;
    }
    Replace(app, field, start, end, NULL, 0);
}

// Moves the caret, extending the selection with Shift; lines keep the x
// the first of them started from, other moves forget it.
static void Move(SampleApp* app, Field* field, muiTextMovement movement, bool extend)
{
    bool vertical = movement == mui_moveLineUp || movement == mui_moveLineDown;
    float width = Width(app, field);
    muiTextCaret caret;
    if (vertical && field->preferredX < 0.0f &&
        muiTextGetCaret(&app->host, field->node, width, field->caret, &caret) == mui_success)
    {
        field->preferredX = caret.x;
    }
    muiTextPosition moved = field->caret;
    if (muiTextMove(&app->host, field->node, width, field->caret, movement,
                    fmaxf(field->preferredX, 0.0f), &moved) != mui_success)
    {
        return;
    }
    field->caret = moved;
    if (!extend)
    {
        field->anchor = moved.offset;
    }
    if (!vertical)
    {
        field->preferredX = -1.0f;
    }
}

// A press places the caret where it is, in the content box.
static void Press(SampleApp* app, Field* field, const muiPointerRecord* record, bool extend)
{
    muiRect content = muiNode_GetContentRect(app->context, field->node);
    muiTextPosition position = field->caret;
    if (muiTextHitTest(&app->host, field->node, content.width, record->x - content.x,
                       record->y - content.y, &position) == mui_success)
    {
        field->caret = position;
        field->preferredX = -1.0f;
        if (!extend)
        {
            field->anchor = position.offset;
        }
    }
}

// The keys a field takes: editing, moving, and Enter where lines break.
static bool Key(SampleApp* app, Field* field, const muiEvent* event)
{
    bool extend = (event->modifiers & mui_modShift) != 0;
    switch (event->code)
    {
    case mui_codeBackspace:
        Delete(app, field, mui_deleteBackward);
        return true;
    case mui_codeDelete:
        Delete(app, field, mui_deleteForward);
        return true;
    case mui_codeArrowLeft:
        Move(app, field, mui_moveLeft, extend);
        return true;
    case mui_codeArrowRight:
        Move(app, field, mui_moveRight, extend);
        return true;
    case mui_codeArrowUp:
        Move(app, field, field->multiline ? mui_moveLineUp : mui_moveTextStart, extend);
        return true;
    case mui_codeArrowDown:
        Move(app, field, field->multiline ? mui_moveLineDown : mui_moveTextEnd, extend);
        return true;
    case mui_codeHome:
        Move(app, field, mui_moveLineStart, extend);
        return true;
    case mui_codeEnd:
        Move(app, field, mui_moveLineEnd, extend);
        return true;
    case mui_codeEnter:
        if (field->multiline)
        {
            Insert(app, field, "\n", 1);
        }
        return field->multiline;
    default:
        return false;
    }
}

static bool Hear(void* user, muiNodeId nodeId, muiPhase phase, const muiEvent* event)
{
    Texts* texts = user;
    int index = FieldOf(texts, nodeId);
    if (phase != mui_phaseBubble || index < 0)
    {
        return false;
    }
    Field* field = &texts->fields[index];
    SampleApp* app = texts->app;
    switch (event->kind)
    {
    case mui_eventPointer:
        if (event->pointer->kind == mui_pointerRecordPress)
        {
            Press(app, field, event->pointer, (event->modifiers & mui_modShift) != 0);
            return true;
        }
        return false;
    case mui_eventText:
        // Control characters are the keys' to handle.
        if (event->length > 0 && (unsigned char)event->text[0] >= 0x20)
        {
            Insert(app, field, event->text, event->length);
        }
        return true;
    case mui_eventKeyDown:
        return Key(app, field, event);
    default:
        return false;
    }
}

// An input method's preedit goes into the focused field at its caret;
// an empty one ends the composition, the caret back where it began, the
// committed text coming after it as text.
static void Record(void* user, SampleApp* app, const mwinEvent* event)
{
    Texts* texts = user;
    if (event->type != mwin_eventImePreedit || texts->focused < 0)
    {
        return;
    }
    Field* field = &texts->fields[texts->focused];
    uint32_t start = field->caret.offset;
    uint32_t length = 0;
    (void)muiTextBlock_GetComposition(app->text, field->block, &start, &length);
    if (length == 0)
    {
        start = Start(field);
    }
    int32_t caret = -1;
    SampleAppCheck(app,
                   muiWindowSetComposition(app->text, field->block, start, &event->data.preedit,
                                           &caret) == mui_success &&
                       muiNode_MarkContentChanged(app->context, field->node) == mui_success,
                   "a composition");
    field->caret.offset = caret >= 0 ? (uint32_t)caret : start;
    field->anchor = field->caret.offset;
}

// The window's caret follows the focused field's; none without one.
static void Update(void* user, SampleApp* app)
{
    Texts* texts = user;
    int focused = FieldOf(texts, muiFocus_Get(app->context, 0));
    if (focused >= 0)
    {
        Field* field = &texts->fields[focused];
        bool moved = focused != texts->focused || field->caret.offset != texts->placedCaret.offset;
        if (moved && muiWindowGlue_SetTextCaret(app->glue, &app->host, field->node, field->caret,
                                                &texts->placed) == mui_success)
        {
            texts->placedCaret = field->caret;
        }
    }
    else if (texts->focused >= 0)
    {
        SampleAppCheck(app,
                       muiWindowGlue_SetCaret(app->glue, (muiNodeId){0, 0}, (muiRect){0},
                                              &texts->placed) == mui_success,
                       "text input stopped");
    }
    texts->focused = focused;
}

// Text, then for the focused field its selection and its caret.
static void Paint(void* user, muiNodeId nodeId, uint64_t hostKey, float width, float height,
                  muiDrawSink* sink)
{
    Texts* texts = user;
    SampleApp* app = texts->app;
    muiPaintText(&app->host, nodeId, hostKey, width, height, sink);
    int index = FieldOf(texts, nodeId);
    if (index < 0 || index != texts->focused)
    {
        return;
    }
    const Field* field = &texts->fields[index];
    muiRect rects[8];
    uint32_t count = 0;
    if (Start(field) != End(field) &&
        muiTextGetRangeRects(&app->host, nodeId, width, Start(field), End(field), rects, 8,
                             &count) == mui_success)
    {
        for (uint32_t i = 0; i < count && i < 8; i++)
        {
            (void)muiDrawSink_AddRect(sink, rects[i], (muiColor){0.2f, 0.45f, 0.9f, 0.5f});
        }
    }
    muiTextCaret caret;
    if (muiTextGetCaret(&app->host, nodeId, width, field->caret, &caret) == mui_success)
    {
        const muiRect bar = {caret.x, caret.y, 2.0f, caret.height};
        (void)muiDrawSink_AddRect(sink, bar, SampleColor(s_caret));
    }
}

// A field: a box with padding, its block as host content, at 16.
static void MakeField(Texts* texts, SampleApp* app, int index, const char* name, const char* text,
                      float height, bool multiline)
{
    Field* field = &texts->fields[index];
    field->multiline = multiline;
    muiNodeId label = SampleLabel(app, app->root, name, 14.0f, s_text);
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.margin.top = 10.0f;
    SampleSetLayout(app, label, &layout, MUI_PROPERTY_BIT(mui_propertyMarginTop));
    field->node = SampleTextNode(app, app->root, text, 16.0f, s_text, &field->block);
    layout = muiDefaultLayoutStyle();
    layout.sizing.width = SampleLength(240.0f);
    layout.sizing.height = SampleLength(height);
    layout.item.shrink = 0.0f;
    layout.margin.top = 4.0f;
    layout.padding = (muiEdges){8.0f, 8.0f, 6.0f, 6.0f};
    SampleSetLayout(
        app, field->node, &layout,
        MUI_PROPERTY_BIT(mui_propertyWidth) | MUI_PROPERTY_BIT(mui_propertyHeight) |
            MUI_PROPERTY_BIT(mui_propertyShrink) | MUI_PROPERTY_BIT(mui_propertyMarginTop) |
            MUI_PROPERTY_BIT(mui_propertyPaddingStart) | MUI_PROPERTY_BIT(mui_propertyPaddingEnd) |
            MUI_PROPERTY_BIT(mui_propertyPaddingTop) | MUI_PROPERTY_BIT(mui_propertyPaddingBottom));
    SampleFill(app, field->node, s_field);
    muiTextStyle style = muiDefaultTextStyle();
    style.wrap = multiline ? mui_textWrap : mui_textNoWrap;
    muiInteractionStyle interaction = muiDefaultInteractionStyle();
    interaction.focusMode = mui_focusAll;
    SampleAppCheck(
        app,
        muiNode_SetTextValues(app->context, field->node, &style,
                              MUI_PROPERTY_BIT(mui_propertyTextWrap)) == mui_success &&
            muiNode_SetInteractionValues(app->context, field->node, &interaction,
                                         MUI_PROPERTY_BIT(mui_propertyFocusMode)) == mui_success &&
            muiNode_SetAccessRole(app->context, field->node,
                                  multiline ? mui_roleMultilineTextInput : mui_roleTextInput) ==
                mui_success &&
            muiNode_SetAccessText(app->context, field->node, mui_accessLabel, name, strlen(name)) ==
                mui_success,
        "a field");
    field->caret = (muiTextPosition){(uint32_t)strlen(text), mui_affinityDownstream};
    field->anchor = field->caret.offset;
    field->preferredX = -1.0f;
}

static void Build(void* user, SampleApp* app)
{
    Texts* texts = user;
    texts->app = app;
    texts->focused = -1;
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.container.direction = mui_flexColumn;
    layout.padding = (muiEdges){16.0f, 16.0f, 4.0f, 16.0f};
    SampleSetLayout(app, app->root, &layout,
                    MUI_PROPERTY_BIT(mui_propertyFlexDirection) |
                        MUI_PROPERTY_BIT(mui_propertyPaddingStart) |
                        MUI_PROPERTY_BIT(mui_propertyPaddingTop));
    SampleFill(app, app->root, s_background);
    MakeField(texts, app, 0, "Name", "Maul", 32.0f, false);
    MakeField(texts, app, 1, "Notes", "", 80.0f, true);
    SampleAppCheck(app,
                   muiSetEventFunction(app->context, Hear, texts) == mui_success &&
                       muiSetAccessTextFunction(app->context, muiAccessTextOf, &app->host) ==
                           mui_success,
                   "the event and access text functions");
}

static const char* TextOf(const SampleApp* app, const Field* field, size_t* lengthOut)
{
    const char* text = "";
    *lengthOut = 0;
    (void)muiTextBlock_GetText(app->text, field->block, &text, lengthOut);
    return text;
}

static bool Holds(const SampleApp* app, const Field* field, const char* expected)
{
    size_t length = 0;
    const char* text = TextOf(app, field, &length);
    return length == strlen(expected) && memcmp(text, expected, length) == 0;
}

// The rows of a field's content box that hold ink, counted in runs: its
// lines of text.
static int Lines(const SampleApp* app, const Field* field)
{
    muiRect content = muiNode_GetContentRect(app->context, field->node);
    int lines = 0;
    bool inked = false;
    for (float y = content.y; y < content.y + content.height; y += 1.0f)
    {
        bool row = false;
        for (float x = content.x; x < content.x + content.width && !row; x += 1.0f)
        {
            row = SamplePixelOf(app, field->node, x, y)[1] > 160;
        }
        lines += row && !inked ? 1 : 0;
        inked = row;
    }
    return lines;
}

// Whether a field shows the caret's colour anywhere in its box.
static bool ShowsCaret(const SampleApp* app, const Field* field)
{
    muiRect rect = muiNode_GetRect(app->context, field->node);
    for (float y = 0.0f; y < rect.height; y += 1.0f)
    {
        for (float x = 0.0f; x < rect.width; x += 1.0f)
        {
            if (SampleNear(SamplePixelOf(app, field->node, x, y), s_caret))
            {
                return true;
            }
        }
    }
    return false;
}

// A field's value as the accessibility tree reports it.
static bool Reports(const SampleApp* app, const Field* field, const char* expected)
{
    const muiAccessNode* found =
        muiAccessTree_Find(muiWindowAccess_GetTree(app->access), muiAccessIdOf(field->node));
    size_t length = strlen(expected);
    return found != NULL && found->textLength[mui_accessValue] == length &&
           (length == 0 || memcmp(found->text[mui_accessValue], expected, length) == 0);
}

static void PostText(SampleApp* app, const char* text)
{
    mwinEvent typed = {.type = mwin_eventTextInput, .window = app->window};
    typed.data.text = (mwinTextEvent){text, (uint32_t)strlen(text)};
    SampleAppCheck(app, mwinTestPost(app->windows, &typed) == mwin_success, "text posted");
}

static void PostNamed(SampleApp* app, mwinKeyCode code, mwinModifiers modifiers)
{
    mwinEvent down = {.type = mwin_eventKeyDown, .window = app->window};
    down.data.key =
        (mwinKeyEvent){.code = code, .modifiers = modifiers, .key = MWIN_KEY_NAMED | code};
    mwinEvent up = down;
    up.type = mwin_eventKeyUp;
    SampleAppCheck(app,
                   mwinTestPost(app->windows, &down) == mwin_success &&
                       mwinTestPost(app->windows, &up) == mwin_success,
                   "a key posted");
}

static void PostPreedit(SampleApp* app, const char* text, int32_t caret)
{
    mwinEvent preedit = {.type = mwin_eventImePreedit, .window = app->window};
    preedit.data.preedit =
        (mwinPreeditEvent){.text = text, .length = (uint32_t)strlen(text), .caret = caret};
    SampleAppCheck(app, mwinTestPost(app->windows, &preedit) == mwin_success, "a preedit posted");
}

static void Click(SampleApp* app, muiNodeId node)
{
    SamplePost(app, mwin_eventCursorMoved, node, 0);
    SamplePost(app, mwin_eventButtonDown, node, 1);
    SamplePost(app, mwin_eventButtonUp, node, 0);
}

// The first frame: both fields unfocused, Name holding "Maul".
static void Still(void* user, SampleApp* app)
{
    const Texts* texts = user;
    SampleAppCheck(app, Lines(app, &texts->fields[0]) == 1 && !ShowsCaret(app, &texts->fields[0]),
                   "Name's text, no caret");
    SampleAppCheck(app, Reports(app, &texts->fields[0], "Maul"), "Name's value reported");
}

static bool Script(void* user, SampleApp* app, int frame)
{
    Texts* texts = user;
    Field* name = &texts->fields[0];
    Field* notes = &texts->fields[1];
    switch (frame)
    {
    case 0:
        Still(texts, app);
        // A click past the text puts the caret at its end.
        Click(app, name->node);
        PostText(app, "!");
        return true;
    case 1:
        SampleAppCheck(app, Holds(app, name, "Maul!") && ShowsCaret(app, name),
                       "typed at the end, the caret shown");
        PostNamed(app, mwin_codeBackspace, 0);
        PostNamed(app, mwin_codeArrowLeft, 0);
        PostNamed(app, mwin_codeArrowLeft, 0);
        PostText(app, "X");
        return true;
    case 2:
        SampleAppCheck(app, Holds(app, name, "MaXul"), "Backspace, two lefts and a typed X");
        PostNamed(app, mwin_codeEnd, mwin_modShift);
        PostText(app, "Y");
        return true;
    case 3:
        SampleAppCheck(app, Holds(app, name, "MaXY") && Reports(app, name, "MaXY"),
                       "Shift+End selected the rest, which Y replaced");
        Click(app, notes->node);
        PostText(app, "one");
        PostNamed(app, mwin_codeEnter, 0);
        PostText(app, "two");
        return true;
    case 4:
        SampleAppCheck(app, Holds(app, notes, "one\ntwo") && Lines(app, notes) == 2,
                       "Enter broke the notes' line");
        PostNamed(app, mwin_codeArrowUp, 0);
        PostPreedit(app, "e", 1);
        return true;
    case 5:
    {
        uint32_t start = 0;
        uint32_t length = 0;
        muiRect field = muiNode_GetRect(app->context, notes->node);
        float x = 0.0f;
        float y = 0.0f;
        SampleAppCheck(app,
                       muiTextBlock_GetComposition(app->text, notes->block, &start, &length) ==
                               mui_success &&
                           start == 3 && length == 1 && Holds(app, notes, "onee\ntwo"),
                       "Up moved to the first line's end, where the preedit composes");
        SampleAppCheck(app,
                       muiNode_MapToRoot(app->context, notes->node, 0.0f, 0.0f, &x, &y) ==
                               mui_success &&
                           texts->placed.x > x && texts->placed.x < x + field.width &&
                           texts->placed.y >= y && texts->placed.y < y + field.height,
                       "the window's caret inside the notes");
        PostPreedit(app, "", -1);
        PostText(app, "\xc3\xa9");
        return true;
    }
    default:
        SampleAppCheck(
            app, Holds(app, notes, "one\xc3\xa9\ntwo") && Reports(app, notes, "one\xc3\xa9\ntwo"),
            "the composition committed as \xc3\xa9");
        return false;
    }
}

int main(int count, char** arguments)
{
    static Texts texts;
    const SampleAppDef def = {
        .width = 320,
        .height = 240,
        .build = Build,
        .update = Update,
        .record = Record,
        .paint = Paint,
        .script = Script,
        .still = Still,
        .user = &texts,
    };
    return SampleRunApp(&def, count, arguments);
}
