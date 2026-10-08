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
// entry, with those of a block given the whole new text.

#include "test_harness.h"
#include "text_block.h"
#include "text_blocks.h"

#include "maul-ui/text.h"
#include "maul-ui/text_block.h"

#include <string.h>

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
        CHECK(muiCreateTextBlock(service, text, length, &edited) == mui_success &&
                  muiCreateTextBlock(service, "", 0, &whole) == mui_success,
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

int main(void)
{
    TestEdits();
    return s_failures == 0 ? 0 : 1;
}
