// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The undo history's sizes: a history whose bytes pass 4 GiB, as a text
// of MUI_MAX_TEXT_BYTES and its replacement can make, is reserved for in
// full, never at a size wrapped to 32 bits. No bytes are copied, so no
// such buffer is made: the allocator only records what it was asked.

#include "test_harness.h"
#include "text_history.h"

#include <stdint.h>

static size_t s_asked;

static void* Refuse(size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    s_asked = size;
    return NULL;
}

static void Free(void* memory, size_t size, size_t alignment, void* context)
{
    (void)memory;
    (void)size;
    (void)alignment;
    (void)context;
}

// A history of one done edit whose bytes end at kept, with room for two
// edits' records and a small byte buffer.
static void MakeHistory(muiTextEditing* editing, muiTextEdit edits[2], char bytes[64], size_t kept)
{
    edits[0] = (muiTextEdit){.removed = 32, .bytes = kept - 32, .kind = MUI_EDIT_OTHER};
    *editing = (muiTextEditing){
        .on = true,
        .entries = {edits, 2 * sizeof(muiTextEdit)},
        .entryCount = 1,
        .done = 1,
        .bytes = {bytes, 64},
        .byteCount = kept,
    };
    editing->rules.undoLimit = 100;
}

static void TestPastFourGiB(void)
{
#if SIZE_MAX > UINT32_MAX
    const muiAllocator allocator = {Refuse, Free, NULL};
    muiTextEdit edits[2];
    char bytes[64];
    muiTextEditing editing;
    // Kept bytes 2^32 + 16: wrapped to 32 bits, 16, which the buffer holds.
    const size_t kept = ((size_t)1 << 32) + 16;
    MakeHistory(&editing, edits, bytes, kept);
    s_asked = 0;
    CHECK(!muiReserveEdit(&allocator, &editing, 8, 8), "refused by the allocator");
    CHECK(s_asked == kept + 16, "asked for every byte the history keeps and the edit's");
    CHECK(editing.bytes.data == bytes && editing.byteCount == kept, "the history kept");
#endif
}

static void TestPastSizeMax(void)
{
    const muiAllocator allocator = {Refuse, Free, NULL};
    muiTextEdit edits[2];
    char bytes[64];
    muiTextEditing editing;
    // Kept bytes 16 short of SIZE_MAX, which only a 32-bit size_t can come
    // near: the edit's 32 more fit no buffer.
    MakeHistory(&editing, edits, bytes, SIZE_MAX - 16);
    s_asked = 0;
    CHECK(!muiReserveEdit(&allocator, &editing, 16, 16), "refused");
    CHECK(s_asked == 0, "without asking the allocator");
}

int main(void)
{
    TestPastFourGiB();
    TestPastSizeMax();
    return s_failures == 0 ? 0 : 1;
}
