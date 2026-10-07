// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Text blocks (record mui-0006): a block's text, what was found in it
// when it was set (line break opportunities and script runs), and its
// shaping for one font chain and direction, kept until either changes.

#ifndef MAUL_UI_SRC_TEXT_BLOCK_H
#define MAUL_UI_SRC_TEXT_BLOCK_H

#include "pool.h"

#include "maul-ui/base.h"
#include "maul-ui/text_editor.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Memory from an allocator that grows to what is asked, its old contents
// dropped.
typedef struct muiBuffer
{
    void* data;
    size_t bytes;
} muiBuffer;

// Whether buffer holds at least bytes; false, keeping it, when memory
// runs out. Its first kept bytes stay as they were; the rest are
// undefined.
bool muiReserveKeeping(const muiAllocator* allocator, muiBuffer* buffer, size_t bytes, size_t kept);

// As muiReserveKeeping, keeping nothing.
bool muiReserve(const muiAllocator* allocator, muiBuffer* buffer, size_t bytes);

void muiFreeBuffer(const muiAllocator* allocator, muiBuffer* buffer);

// A line break opportunity before the byte at offset.
typedef struct muiTextBreak
{
    uint32_t offset;
    uint32_t mandatory;
} muiTextBreak;

// A script run ending before the byte at end; script is an ISO 15924
// tag, as HarfBuzz's.
typedef struct muiTextScript
{
    uint32_t end;
    uint32_t script;
} muiTextScript;

// A glyph as HarfBuzz shaped it, in its font's units: its id, the byte
// offset of its cluster, its advance and its offset, y up.
typedef struct muiShapedGlyph
{
    uint32_t id;
    uint32_t cluster;
    int32_t advance;
    int32_t offsetX;
    int32_t offsetY;
} muiShapedGlyph;

// Bytes of one bidi level, one script and one font, and their glyphs, in
// visual order; face is the font's place in the chain they were shaped
// with, and units its units per em.
typedef struct muiTextItem
{
    uint32_t start;
    uint32_t end;
    uint32_t firstGlyph;
    uint32_t glyphCount;
    uint32_t level;
    uint32_t script;
    uint32_t face;
    uint32_t units;
    // Its run style (src/text_runs.h), 0 the node's, and that style's
    // size over the node's.
    uint32_t style;
    float scale;
} muiTextItem;

// An edit undo keeps: the bytes from start, removed long, became the
// inserted bytes; both are in the history's bytes from bytes, removed
// first. The selections before and after it, and what made it.
typedef struct muiTextEdit
{
    uint32_t start;
    uint32_t removed;
    uint32_t inserted;
    uint32_t bytes;
    muiTextSelection before;
    muiTextSelection after;
    uint8_t kind;
} muiTextEdit;

// A block's editing state: its rules, its selection, the x vertical
// moves keep (below 0 for none), the unit and the range a press selected
// for drags, and its history: entryCount muiTextEdit, done of them not
// undone, over byteCount bytes; the kind of the last edit while typing
// or deleting may still join it, else 0; the text's revision it last
// saw.
typedef struct muiTextEditing
{
    bool on;
    muiTextEditDef def;
    muiTextSelection selection;
    float preferredX;
    uint8_t grain;
    uint32_t pressStart;
    uint32_t pressEnd;
    muiBuffer entries;
    uint32_t entryCount;
    uint32_t done;
    muiBuffer bytes;
    uint32_t byteCount;
    uint8_t open;
    uint64_t revision;
} muiTextEditing;

typedef struct muiTextBlock
{
    // The text, length bytes.
    muiBuffer text;
    uint32_t length;
    // muiTextBreak, the end of the text included when it is not empty.
    muiBuffer breaks;
    uint32_t breakCount;
    // muiTextScript.
    muiBuffer scripts;
    uint32_t scriptCount;
    // The shaping, valid when shaped, for the font chain of identity
    // shapedChain and shapedRtl.
    bool shaped;
    bool shapedRtl;
    uint64_t shapedChain;
    // A byte per byte: the place in the chain of the font it is drawn in.
    muiBuffer faces;
    // A bidi level per byte.
    muiBuffer levels;
    // muiTextItem, in logical order.
    muiBuffer items;
    uint32_t itemCount;
    // muiShapedGlyph.
    muiBuffer glyphs;
    uint32_t glyphCount;
    // A byte per byte: 1 where HarfBuzz marks the cluster starting there
    // unsafe to break.
    muiBuffer unsafe;
    // length + 1 sums from the start of the text: of advances in ems
    // (double), as glyphs of fonts of different units per em add, and of
    // clusters (uint32_t).
    muiBuffer advances;
    muiBuffer clusters;
    // An input method's composition, while compositionLength is not 0:
    // its bytes from compositionStart, and segmentCount
    // muiCompositionSegment in segments.
    uint32_t compositionStart;
    uint32_t compositionLength;
    muiBuffer segments;
    uint32_t segmentCount;
    // spanCount muiTextSpan, in the order given.
    muiBuffer spans;
    uint32_t spanCount;
    // With spans that shape: runStyleCount muiRunStyle in runStyles, and a
    // byte per byte, its run style, in runs; for the node's style and the
    // spans runKey stands for (src/text_runs.h). runKey is 0 for none.
    muiBuffer runStyles;
    uint32_t runStyleCount;
    muiBuffer runs;
    uint64_t runKey;
    // Counts the changes of the text, so an editor sees those it did not
    // make.
    uint64_t revision;
    // Editing, while editing.on (src/text_editing.h).
    muiTextEditing editing;
} muiTextBlock;

typedef struct muiTextBlockStore
{
    muiPool pool;
    // Block i is blocks[i - 1].
    muiTextBlock* blocks;
} muiTextBlockStore;

// Frees what a block holds and zeroes it.
void muiReleaseTextBlock(const muiAllocator* allocator, muiTextBlock* block);

#endif // MAUL_UI_SRC_TEXT_BLOCK_H
