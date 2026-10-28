// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes text, the bytes content, users, the clipboard and input methods
// hand the library: Maul UI's own code splits it into paragraphs, runs
// it through bidi, breaks it into lines, shapes its runs, hit tests it
// and edits it by clusters and words. Any bytes are a width, flags, a
// text and edits: the block is laid out and painted in Liberation Sans,
// then each edit (typing, pasting, erasing, selecting, moving, pressing,
// composing, undoing) is made and the block laid out again; each edit
// succeeds or is refused, the selection stays within the text, and
// nothing touches memory it does not own. The text edited is then hit
// tested as a pointer and an input method ask it: points to positions,
// offsets to carets and ranges to rectangles. The input also chooses one of
// the service's allocations to fail, or none: every call then fails
// cleanly or succeeds, and every block comes back.

#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/font.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/text.h"
#include "maul-ui/text_block.h"
#include "maul-ui/text_edit.h"
#include "maul-ui/text_editor.h"
#include "maul-ui/text_style.h"

#include <math.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "liberation_sans.inc"

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

enum
{
    HEADER = 6,
    MAX_EDITS = 24,
    // The kinds of edit an input's bytes choose among.
    EDIT_KINDS = 12,
    // The offsets hit testing asks carets and short ranges of, from 0.
    MAX_PROBED = 48,
    // The rectangles a range's are read into.
    MAX_RECTS = 8
};

static void Expect(bool condition)
{
    if (!condition)
    {
        abort();
    }
}

// Whether an edit's result is one its contract allows for any input.
static bool IsAllowed(muiResult result)
{
    return result == mui_success || result == mui_errorInvalid || result == mui_errorCapacity ||
           result == mui_errorState || result == mui_empty;
}

// The service's memory: allocation failAt fails (counting from 1, 0 for
// none), and what is live is counted.
typedef struct Memory
{
    uint32_t allocations;
    uint32_t failAt;
    size_t liveBytes;
} Memory;

static void* Allocate(size_t size, size_t alignment, void* context)
{
    Memory* memory = context;
    if (++memory->allocations == memory->failAt)
    {
        return nullptr;
    }
    size_t rounded = (size + alignment - 1) / alignment * alignment;
    void* block =
        alignment <= alignof(max_align_t) ? malloc(size) : aligned_alloc(alignment, rounded);
    memory->liveBytes += block != nullptr ? size : 0;
    return block;
}

static void Release(void* block, size_t size, size_t alignment, void* context)
{
    (void)alignment;
    Memory* memory = context;
    memory->liveBytes -= size;
    free(block);
}

// The input as it is read: bytes taken from the front.
typedef struct Reader
{
    const uint8_t* data;
    size_t size;
} Reader;

static uint8_t Byte(Reader* reader)
{
    if (reader->size == 0)
    {
        return 0;
    }
    reader->size--;
    return *reader->data++;
}

// Up to count bytes as text; fewer when the input runs out.
static const char* Take(Reader* reader, size_t count, size_t* lengthOut)
{
    size_t length = count < reader->size ? count : reader->size;
    const char* text = (const char*)reader->data;
    reader->data += length;
    reader->size -= length;
    *lengthOut = length;
    return text;
}

typedef struct Scene
{
    muiTextService* service;
    muiContext* context;
    muiTextBlockId block;
    muiNodeId node;
    muiTextHost host;
    float width;
} Scene;

// Whether the context and the service are left busy by no call that
// returned: a call each refuses for its argument, which changes nothing,
// is not refused as busy (Maul conventions, Threads).
static bool IsIdle(const Scene* scene)
{
    const muiTextSelection none = {0, {0, false}};
    return muiNextNotification(scene->context, NULL) == mui_errorInvalid &&
           muiTextBlock_Select(scene->service, (muiTextBlockId){0, 0}, none) == mui_errorInvalid;
}

static void LayOut(Scene* scene, bool paint)
{
    const muiLayoutInput layoutInput = {scene->width, 1000.0f,     muiMeasureText, &scene->host, 0,
                                        nullptr,      {0, 0, 0, 0}};
    // mui_errorCapacity exactly when the service ran out of memory.
    uint64_t failures = muiGetTextServiceFailures(scene->service);
    muiResult laid = muiComputeLayout(scene->context, scene->node, &layoutInput);
    Expect(laid == (muiGetTextServiceFailures(scene->service) != failures ? mui_errorCapacity
                                                                          : mui_success));
    if (paint)
    {
        const muiDrawInput drawInput = {1, 1.0f, muiPaintText, &scene->host};
        muiDrawList list = {0};
        failures = muiGetTextServiceFailures(scene->service);
        muiResult painted = muiBuildDrawList(scene->context, scene->node, &drawInput);
        Expect(painted == (muiGetTextServiceFailures(scene->service) != failures ? mui_errorCapacity
                                                                                 : mui_success) &&
               muiGetDrawList(scene->context, &list) == mui_success);
    }
    // The selection stays within the text.
    const char* text = nullptr;
    size_t length = 0;
    muiTextSelection selection = {0, {0, mui_affinityDownstream}};
    Expect(muiTextBlock_GetText(scene->service, scene->block, &text, &length) == mui_success &&
           muiTextBlock_GetSelection(scene->service, scene->block, &selection) == mui_success &&
           selection.anchor <= length && selection.caret.offset <= length);
}

// An offset in the text, or one past it, from two bytes.
static uint32_t Offset(const Scene* scene, uint8_t low, uint8_t high)
{
    const char* text = nullptr;
    size_t length = 0;
    Expect(muiTextBlock_GetText(scene->service, scene->block, &text, &length) == mui_success);
    return (uint32_t)(((size_t)low | (size_t)high << 8) % (length + 2));
}

static void Edit(Scene* scene, Reader* reader)
{
    muiTextService* service = scene->service;
    muiTextBlockId block = scene->block;
    uint8_t kind = Byte(reader) % EDIT_KINDS;
    uint8_t a = Byte(reader);
    uint8_t b = Byte(reader);
    bool changed = false;
    size_t length = 0;
    const char* text = nullptr;
    muiResult result = mui_success;
    switch (kind)
    {
    case 0:
        text = Take(reader, a % 9, &length);
        result = muiTextBlock_Type(service, block, text, length, &changed);
        break;
    case 1:
        text = Take(reader, a % 33, &length);
        result = muiTextBlock_Paste(service, block, text, length, &changed);
        break;
    case 2:
        result = muiTextBlock_Erase(service, block, (muiTextDeletion)(a % 3), &changed);
        break;
    case 3:
    {
        const muiTextSelection selection = {Offset(scene, a, b),
                                            {Offset(scene, b, a), (muiTextAffinity)(a >> 7)}};
        result = muiTextBlock_Select(service, block, selection);
        break;
    }
    case 4:
        result =
            muiTextEditMove(&scene->host, scene->node, (muiTextMovement)(a % 16), (b & 1) != 0);
        break;
    case 5:
        result = muiTextEditPress(&scene->host, scene->node, (float)a - 8.0f, (float)b - 8.0f,
                                  1u + b % 4, (a & 1) != 0);
        break;
    case 6:
        result = muiTextEditDrag(&scene->host, scene->node, (float)a, (float)b);
        break;
    case 7:
    {
        text = Take(reader, a % 13, &length);
        const muiCompositionSegment segments[2] = {
            {0, (uint32_t)(length / 2), (muiCompositionStyle)(b % 3)},
            {(uint32_t)(length / 2), (uint32_t)(length - length / 2),
             (muiCompositionStyle)(b % 2)}};
        result = muiTextBlock_Compose(service, block, text, length, b % (uint32_t)(length + 2),
                                      segments, b % 3, &changed);
        break;
    }
    case 8:
        result = muiTextBlock_EndComposition(service, block);
        break;
    case 9:
        result = (a & 1) != 0 ? muiTextBlock_Undo(service, block, &changed)
                              : muiTextBlock_Redo(service, block, &changed);
        break;
    case 10:
        text = Take(reader, b % 9, &length);
        result = muiTextBlock_Replace(service, block, Offset(scene, a, 0), Offset(scene, a, b),
                                      text, length);
        break;
    default:
    {
        muiTextEditRules def = muiDefaultTextEditRules();
        def.flags = (muiTextEditFlags)(a & 7);
        def.filter = (muiTextFilter)(b % 4);
        def.maxLength = b >> 4;
        def.undoLimit = a >> 4;
        result = muiTextBlock_SetEditing(service, block, &def);
        break;
    }
    }
    Expect(IsAllowed(result));
}

// Whether a hit test's rectangle is a box: finite, not negative in size.
static bool IsBox(muiRect rect)
{
    return isfinite(rect.x) && isfinite(rect.y) && rect.width >= 0.0f && rect.height >= 0.0f &&
           isfinite(rect.width) && isfinite(rect.height);
}

// Reads a range's rectangles: they number the count, those that fit are
// boxes, and a range not after its start has none. Memory may run out.
static void ProbeRange(Scene* scene, float width, uint32_t start, uint32_t end)
{
    muiRect rects[MAX_RECTS];
    uint32_t count = 0;
    muiResult result = muiTextGetRangeRects(&scene->host, scene->node, width, start, end, rects,
                                            MAX_RECTS, &count);
    Expect(result == mui_success || result == mui_errorCapacity);
    Expect(result != mui_success || count <= MAX_RECTS);
    Expect(end > start || count == 0);
    for (uint32_t i = 0; i < count && i < MAX_RECTS; i++)
    {
        Expect(IsBox(rects[i]));
    }
}

// Hit tests the text: points over and around the content box find
// positions within it, offsets in it and past it have finite carets, and
// ranges have boxes. Each call answers or runs out of memory.
static void Probe(Scene* scene)
{
    const char* text = nullptr;
    size_t length = 0;
    Expect(muiTextBlock_GetText(scene->service, scene->block, &text, &length) == mui_success);
    const muiRect box = muiNode_GetContentRect(scene->context, scene->node);
    for (int row = 0; row < 6; row++)
    {
        for (int column = 0; column < 6; column++)
        {
            float x = -8.0f + (box.width + 16.0f) * (float)column / 5.0f;
            float y = -8.0f + (box.height + 16.0f) * (float)row / 5.0f;
            muiTextPosition at = {0, mui_affinityDownstream};
            muiResult hit = muiTextHitTest(&scene->host, scene->node, box.width, x, y, &at);
            Expect(hit == mui_success || hit == mui_errorCapacity);
            Expect(hit != mui_success || at.offset <= length);
        }
    }
    for (uint32_t offset = 0; offset <= length + 1 && offset < MAX_PROBED; offset++)
    {
        for (int affinity = 0; affinity < 2; affinity++)
        {
            const muiTextPosition position = {offset, (muiTextAffinity)affinity};
            muiTextCaret caret;
            muiResult got = muiTextGetCaret(&scene->host, scene->node, box.width, position, &caret);
            Expect(got == mui_success || got == mui_errorCapacity);
            Expect(got != mui_success || (isfinite(caret.x) && isfinite(caret.y) &&
                                          isfinite(caret.height) && caret.height >= 0.0f));
        }
    }
    const uint32_t end = (uint32_t)length;
    ProbeRange(scene, box.width, 0, end);
    ProbeRange(scene, box.width, end / 3, end - end / 3);
    ProbeRange(scene, box.width, end, 0);
    for (uint32_t start = 0; start < end && start < MAX_PROBED; start += 3)
    {
        ProbeRange(scene, box.width, start, start + 3);
    }
}

// Whether a creation's result is one its contract allows, memory
// running out included; false when it ran out.
static bool Made(muiResult result)
{
    Expect(result == mui_success || result == mui_errorCapacity);
    return result == mui_success;
}

// The service, its font, the context and the block, editing; false when
// memory ran out on the way.
static bool Make(Scene* scene, Memory* memory, uint8_t flags, const char* text, size_t length)
{
    muiTextServiceDef serviceDef = muiDefaultTextServiceDef();
    serviceDef.allocator = (muiAllocator){Allocate, Release, memory};
    if (!Made(muiCreateTextService(&serviceDef, &scene->service)))
    {
        return false;
    }
    muiFontDef fontDef = muiDefaultFontDef();
    fontDef.data = s_liberationSans;
    fontDef.size = sizeof s_liberationSans;
    fontDef.dataMode = mui_fontDataBorrow;
    muiFontId font = {0, 0};
    if (!Made(muiCreateFont(scene->service, &fontDef, &font)))
    {
        return false;
    }
    Expect(muiSetDefaultFont(scene->service, font) == mui_success);
    muiContextDef contextDef = muiDefaultContextDef();
    Expect(muiCreateContext(&contextDef, &scene->context) == mui_success);
    scene->host = (muiTextHost){scene->service, scene->context};
    muiTextBlockDef blockDef = muiDefaultTextBlockDef();
    blockDef.text = text;
    blockDef.length = length;
    muiTextEditRules editDef = muiDefaultTextEditRules();
    editDef.flags = (muiTextEditFlags)(flags & 7);
    editDef.undoLimit = 8;
    return Made(muiCreateTextBlock(scene->service, &blockDef, &scene->block)) &&
           Made(muiTextBlock_SetEditing(scene->service, scene->block, &editDef));
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < HEADER)
    {
        return 0;
    }
    Reader reader = {data, size};
    Scene scene = {.width = (float)Byte(&reader) * 4.0f};
    uint8_t flags = Byte(&reader);
    size_t textLength = (size_t)Byte(&reader) | (size_t)Byte(&reader) << 8;
    Memory memory = {.failAt = (uint32_t)Byte(&reader) | (uint32_t)Byte(&reader) << 8};
    size_t length = 0;
    const char* text = Take(&reader, textLength, &length);
    if (!Make(&scene, &memory, flags, text, length))
    {
        muiDestroyContext(scene.context);
        muiDestroyTextService(scene.service);
        Expect(memory.liveBytes == 0);
        return 0;
    }
    muiNodeDef nodeDef = muiDefaultNodeDef();
    nodeDef.hostKey = muiTextBlock_GetKey(scene.block);
    Expect(muiCreateNode(scene.context, &nodeDef, &scene.node) == mui_success);
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.content = mui_contentHost;
    layout.textDirection = (flags & 8) != 0 ? mui_inlineRightToLeft : mui_inlineLeftToRight;
    muiTextStyle style = muiDefaultTextStyle();
    style.size = (muiDimension){0.0f, (flags & 16) != 0 ? 24.0f : 13.0f, mui_dimensionValue};
    style.wrap = (flags & 32) != 0 ? mui_textNoWrap : mui_textWrap;
    style.letterSpacing = (muiDimension){0.0f, (flags & 64) != 0 ? 1.5f : 0.0f, mui_dimensionValue};
    Expect(muiNode_SetLayoutValues(scene.context, scene.node, &layout,
                                   MUI_PROPERTY_BIT(mui_propertyContent) |
                                       MUI_PROPERTY_BIT(mui_propertyTextDirection)) ==
               mui_success &&
           muiNode_SetTextValues(scene.context, scene.node, &style,
                                 MUI_PROPERTY_BIT(mui_propertyFontSize) |
                                     MUI_PROPERTY_BIT(mui_propertyTextWrap) |
                                     MUI_PROPERTY_BIT(mui_propertyLetterSpacing)) == mui_success);
    LayOut(&scene, true);
    for (uint32_t i = 0; i < MAX_EDITS && reader.size != 0; i++)
    {
        Edit(&scene, &reader);
        LayOut(&scene, false);
        Expect(IsIdle(&scene));
    }
    LayOut(&scene, true);
    Probe(&scene);
    Expect(IsIdle(&scene));
    muiDestroyContext(scene.context);
    muiDestroyTextService(scene.service);
    Expect(memory.liveBytes == 0);
    return 0;
}
