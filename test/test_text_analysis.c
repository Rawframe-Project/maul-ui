// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A block's analysis after edits (record mui-0006): a replace finds the
// line breaks and script runs of the paragraphs it reaches again and
// moves the rest, which must be what analyzing the whole new text finds.
// Seeded random texts of paragraphs, every mandatory break among their
// separators (LF, CR, CR LF, VT, FF, NEL, LS, PS), several scripts,
// digits, spaces and brackets, take random replaces (inserting,
// removing and replacing, separators and the halves of CR LF among
// them); after each, the edited block's tables are compared, entry by
// entry, with those of a block given the whole new text. Then the same
// for shaping: both blocks laid out in Liberation Sans with a fallback
// font after each edit, the edited one shaping again only its stale
// paragraphs, and every shaped table compared: levels, fonts, unsafe
// marks, items, glyphs and the sums of advances and clusters.

#include "line_break.h"
#include "test_harness.h"
#include "text_block.h"
#include "text_blocks.h"

#include "maul-ui/context.h"
#include "maul-ui/font.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/text.h"
#include "maul-ui/text_block.h"

#include <string.h>

#include "coverage.inc"
#include "liberation_sans.inc"

enum
{
    ROUNDS = 60,
    EDITS = 120,
    LIMIT = 4096
};

static const char* const s_pieces[] = {
    "word",
    " ",
    "  ",
    "\n",
    "\r",
    "\r\n",
    "\v",
    "\f",
    "\xC2\x85",
    "\xE2\x80\xA8",
    "\xE2\x80\xA9",
    "\xD0\x9F\xD1\x80\xD0\xB8",
    "\xD8\xB3\xD9\x84",
    "\xE4\xB8\xAD",
    "123",
    "(",
    ")",
    "-",
    ".",
    "\xCC\x81",
};

static uint32_t Next(uint32_t* state)
{
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

// Appends random pieces to a text, at most limit bytes in all.
static uint32_t Pieces(char* out, uint32_t count, uint32_t limit, uint32_t* state)
{
    uint32_t length = 0;
    for (uint32_t i = 0; i < count; i++)
    {
        const char* piece = s_pieces[Next(state) % (sizeof s_pieces / sizeof s_pieces[0])];
        uint32_t size = (uint32_t)strlen(piece);
        if (length + size > limit)
        {
            break;
        }
        memcpy(out + length, piece, size);
        length += size;
    }
    return length;
}

// A random offset at the start of a character of a text.
static uint32_t CharacterStart(const char* text, uint32_t length, uint32_t* state)
{
    uint32_t at = length == 0 ? 0 : Next(state) % (length + 1);
    while (at < length && ((unsigned char)text[at] & 0xC0) == 0x80)
    {
        at++;
    }
    return at;
}

static bool SameTables(const muiTextBlock* a, const muiTextBlock* b)
{
    return a->length == b->length && a->breakCount == b->breakCount &&
           a->scriptCount == b->scriptCount &&
           (a->breakCount == 0 ||
            memcmp(a->breaks.data, b->breaks.data, a->breakCount * sizeof(muiTextBreak)) == 0) &&
           (a->scriptCount == 0 ||
            memcmp(a->scripts.data, b->scripts.data, a->scriptCount * sizeof(muiTextScript)) == 0);
}

static void TestEdits(void)
{
    muiTextServiceDef def = muiDefaultTextServiceDef();
    muiTextService* service = NULL;
    CHECK(muiCreateTextService(&def, &service) == mui_success, "a text service");
    static char text[LIMIT + 64];
    static char inserted[64];
    uint32_t state = 0x1B873593u;
    uint32_t compared = 0;
    bool same = true;
    for (uint32_t round = 0; round < ROUNDS && service != NULL; round++)
    {
        uint32_t length = Pieces(text, 1 + Next(&state) % 200, LIMIT, &state);
        muiTextBlockId edited = {0, 0};
        muiTextBlockId whole = {0, 0};
        muiTextBlockDef blockDef = muiDefaultTextBlockDef();
        blockDef.text = text;
        blockDef.length = length;
        muiTextBlockDef blockDef2 = muiDefaultTextBlockDef();
        blockDef2.text = "";
        blockDef2.length = 0;
        CHECK(muiCreateTextBlock(service, &blockDef, &edited) == mui_success &&
                  muiCreateTextBlock(service, &blockDef2, &whole) == mui_success,
              "blocks");
        for (uint32_t edit = 0; edit < EDITS; edit++)
        {
            uint32_t start = CharacterStart(text, length, &state);
            uint32_t end = CharacterStart(text, length, &state);
            if (end < start)
            {
                uint32_t swap = start;
                start = end;
                end = swap;
            }
            end = Next(&state) % 3 == 0 ? start : end;
            uint32_t size =
                Next(&state) % 4 == 0 ? 0 : Pieces(inserted, 1 + Next(&state) % 3, 32, &state);
            if (length - (end - start) + size > LIMIT ||
                muiTextBlock_Replace(service, edited, start, end, inserted, size) != mui_success)
            {
                continue;
            }
            memmove(text + start + size, text + end, length - end);
            memcpy(text + start, inserted, size);
            length = length - (end - start) + size;
            CHECK(muiTextBlock_SetText(service, whole, text, length) == mui_success, "the whole");
            const muiTextBlock* a = muiResolveTextBlock(service, edited);
            const muiTextBlock* b = muiResolveTextBlock(service, whole);
            same = same && a != NULL && b != NULL && SameTables(a, b) &&
                   memcmp(a->text.data, text, length) == 0;
            compared++;
        }
        CHECK(muiDestroyTextBlock(service, edited) == mui_success &&
                  muiDestroyTextBlock(service, whole) == mui_success,
              "destroyed");
    }
    CHECK(same && compared > ROUNDS * EDITS / 2, "edited tables are the whole text's");
    muiDestroyTextService(service);
}

static bool SameBytes(const muiBuffer* a, const muiBuffer* b, size_t bytes)
{
    return bytes == 0 || memcmp(a->data, b->data, bytes) == 0;
}

static bool SameShaping(const muiTextBlock* a, const muiTextBlock* b)
{
    size_t entries = (size_t)a->length + 1u;
    // Bytes a paragraph's sums leave alone are never read, so the sums
    // are compared whole: both blocks write every entry.
    return a->shaped && b->shaped && a->length == b->length && a->itemCount == b->itemCount &&
           a->glyphCount == b->glyphCount &&
           SameBytes(&a->items, &b->items, a->itemCount * sizeof(muiTextItem)) &&
           SameBytes(&a->glyphs, &b->glyphs, a->glyphCount * sizeof(muiShapedGlyph)) &&
           SameBytes(&a->levels, &b->levels, a->length) &&
           SameBytes(&a->faces, &b->faces, a->length) &&
           SameBytes(&a->unsafe, &b->unsafe, a->length) &&
           SameBytes(&a->advances, &b->advances, entries * sizeof(double)) &&
           SameBytes(&a->clusters, &b->clusters, entries * sizeof(uint32_t));
}

// Whether two blocks keep the same lines for each break mode both have
// lines for: the edited block's broken again where edits reached, the
// other's whole. Returns how many modes were compared.
static uint32_t SameLines(const muiTextBlock* a, const muiTextBlock* b, bool* same)
{
    uint32_t compared = 0;
    for (int i = 0; i < MUI_LINE_CACHES; i++)
    {
        const muiLineCache* x = &a->lineCaches[i];
        const muiLineCache* y = &b->lineCaches[i];
        if (x->shaping == 0 || y->shaping == 0 || x->stale.on || y->stale.on)
        {
            continue;
        }
        compared++;
        *same = *same && x->count == y->count && x->widest == y->widest && x->length == y->length &&
                SameBytes(&x->lines, &y->lines, x->count * sizeof(muiTextLine)) &&
                SameBytes(&x->widths, &y->widths, x->count * sizeof(float));
    }
    return compared;
}

typedef struct Shaping
{
    muiTextService* service;
    muiContext* context;
    muiNodeId root;
    muiNodeId nodes[2];
    muiTextBlockId blocks[2];
} Shaping;

static muiNodeId TextNode(muiContext* context, muiNodeId root, muiTextBlockId block)
{
    muiNodeDef def = muiDefaultNodeDef();
    def.hostKey = muiTextBlock_GetKey(block);
    muiNodeId node = {0, 0};
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.content = mui_contentHost;
    CHECK(muiCreateNode(context, &def, &node) == mui_success &&
              muiNode_InsertChild(context, root, node, (muiNodeId){0, 0}) == mui_success &&
              muiNode_SetLayoutValues(context, node, &layout,
                                      MUI_PROPERTY_BIT(mui_propertyContent)) == mui_success,
          "a text node");
    return node;
}

static bool LayOut(Shaping* shaping)
{
    muiTextHost host = {shaping->service, shaping->context};
    const muiLayoutInput input = {300.0f, 1e9f, muiMeasureText, &host, 0, NULL, {0, 0, 0, 0}};
    return muiNode_MarkContentChanged(shaping->context, shaping->nodes[0]) == mui_success &&
           muiNode_MarkContentChanged(shaping->context, shaping->nodes[1]) == mui_success &&
           muiComputeLayout(shaping->context, shaping->root, &input) == mui_success;
}

static Shaping MakeShaping(void)
{
    Shaping shaping = {0};
    muiTextServiceDef def = muiDefaultTextServiceDef();
    muiContextDef contextDef = muiDefaultContextDef();
    muiFontDef font = muiDefaultFontDef();
    font.dataMode = mui_fontDataBorrow;
    muiFontId liberation = {0, 0};
    muiFontId coverage = {0, 0};
    CHECK(muiCreateTextService(&def, &shaping.service) == mui_success &&
              muiCreateContext(&contextDef, &shaping.context) == mui_success,
          "a service and a context");
    font.data = s_liberationSans;
    font.size = sizeof s_liberationSans;
    CHECK(muiCreateFont(shaping.service, &font, &liberation) == mui_success, "Liberation Sans");
    font.data = s_coverage;
    font.size = sizeof s_coverage;
    CHECK(muiCreateFont(shaping.service, &font, &coverage) == mui_success, "the fallback");
    uint64_t fallback = muiFont_GetKey(coverage);
    muiNodeDef nodeDef = muiDefaultNodeDef();
    CHECK(muiSetDefaultFont(shaping.service, liberation) == mui_success &&
              muiSetFallbackFonts(shaping.service, &fallback, 1) == mui_success &&
              muiCreateNode(shaping.context, &nodeDef, &shaping.root) == mui_success,
          "fonts and a root");
    return shaping;
}

static void TestShaping(void)
{
    Shaping shaping = MakeShaping();
    static char text[LIMIT + 64];
    static char inserted[64];
    uint32_t state = 0x85EBCA6Bu;
    uint32_t compared = 0;
    uint32_t modes = 0;
    bool same = true;
    for (uint32_t round = 0; round < ROUNDS / 2 && shaping.root.index1 != 0; round++)
    {
        uint32_t length = Pieces(text, 1 + Next(&state) % 120, LIMIT, &state);
        muiTextBlockDef blockDef = muiDefaultTextBlockDef();
        blockDef.text = text;
        blockDef.length = length;
        muiTextBlockDef blockDef2 = muiDefaultTextBlockDef();
        blockDef2.text = "";
        blockDef2.length = 0;
        CHECK(muiCreateTextBlock(shaping.service, &blockDef, &shaping.blocks[0]) == mui_success &&
                  muiCreateTextBlock(shaping.service, &blockDef2, &shaping.blocks[1]) ==
                      mui_success,
              "blocks");
        shaping.nodes[0] = TextNode(shaping.context, shaping.root, shaping.blocks[0]);
        shaping.nodes[1] = TextNode(shaping.context, shaping.root, shaping.blocks[1]);
        CHECK(LayOut(&shaping), "shaped first");
        for (uint32_t edit = 0; edit < EDITS / 2; edit++)
        {
            uint32_t start = CharacterStart(text, length, &state);
            uint32_t end = CharacterStart(text, length, &state);
            uint32_t low = start < end ? start : end;
            uint32_t high = start < end ? end : start;
            uint32_t size =
                Next(&state) % 4 == 0 ? 0 : Pieces(inserted, 1 + Next(&state) % 3, 32, &state);
            if (length - (high - low) + size > LIMIT ||
                muiTextBlock_Replace(shaping.service, shaping.blocks[0], low, high, inserted,
                                     size) != mui_success)
            {
                continue;
            }
            memmove(text + low + size, text + high, length - high);
            memcpy(text + low, inserted, size);
            length = length - (high - low) + size;
            // Now and then two edits before the next layout.
            if (Next(&state) % 3 == 0)
            {
                continue;
            }
            CHECK(muiTextBlock_SetText(shaping.service, shaping.blocks[1], text, length) ==
                          mui_success &&
                      LayOut(&shaping),
                  "laid out");
            const muiTextBlock* edited = muiResolveTextBlock(shaping.service, shaping.blocks[0]);
            const muiTextBlock* whole = muiResolveTextBlock(shaping.service, shaping.blocks[1]);
            same = same && SameShaping(edited, whole);
            modes += SameLines(edited, whole, &same);
            compared++;
        }
        CHECK(muiDestroyNode(shaping.context, shaping.nodes[0]) == mui_success &&
                  muiDestroyNode(shaping.context, shaping.nodes[1]) == mui_success &&
                  muiDestroyTextBlock(shaping.service, shaping.blocks[0]) == mui_success &&
                  muiDestroyTextBlock(shaping.service, shaping.blocks[1]) == mui_success,
              "destroyed");
    }
    CHECK(same && compared > ROUNDS * EDITS / 8 && modes >= compared,
          "edited shaping and lines are the whole text's");
    muiDestroyContext(shaping.context);
    muiDestroyTextService(shaping.service);
}

int main(void)
{
    TestEdits();
    TestShaping();
    return s_failures == 0 ? 0 : 1;
}
