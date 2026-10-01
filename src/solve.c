// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The cache answers a sizing query when a stored result provably answers
// it the same; everything else is computed and stored.

#include "solve.h"

#include "absolute.h"
#include "flex.h"
#include "invariant.h"
#include "sizing.h"

#include <math.h>

static bool IsSized(muiMeasureMode mode)
{
    return mode == mui_measureExact || mode == mui_measureAtMost;
}

// Whether a size computed under an old constraint answers a new one, by
// the rules Yoga's cache uses: the same constraint; an exact size equal to
// what an unshrunk sizing gave; a max-content size that fits the new
// space; or a smaller space the old size still fits.
static bool AxisAnswers(muiMeasureAxis next, muiMeasureAxis old, float result)
{
    if (next.mode == old.mode && (!IsSized(next.mode) || next.size == old.size))
    {
        return true;
    }
    if (next.mode == mui_measureExact)
    {
        return old.mode != mui_measureMinContent && next.size == result;
    }
    if (next.mode == mui_measureAtMost)
    {
        bool fitsMaxContent = old.mode == mui_measureMaxContent;
        bool stricter = old.mode == mui_measureAtMost && next.size < old.size;
        return (fitsMaxContent || stricter) && result <= next.size;
    }
    return false;
}

static bool IsScaled(muiDimension dimension)
{
    return dimension.kind == mui_dimensionValue && dimension.scale != 0.0f;
}

// Whether a node's own sizing reads its parent's extents: only its scaled
// limits do, as the parent resolves the node's size itself.
static bool ReadsParentExtent(const muiSizing* sizing)
{
    return IsScaled(sizing->minWidth) || IsScaled(sizing->maxWidth) ||
           IsScaled(sizing->minHeight) || IsScaled(sizing->maxHeight);
}

static const muiCacheEntry* FindCached(const muiLayoutCache* cache, const muiSizingInput* input,
                                       bool keyExtents)
{
    for (int i = 0; i < MUI_CACHE_ENTRIES; i++)
    {
        const muiCacheEntry* entry = &cache->entries[i];
        bool sameExtents = !keyExtents || (entry->input.parentWidth == input->parentWidth &&
                                           entry->input.parentHeight == input->parentHeight);
        if (entry->valid && sameExtents && entry->input.rtl == input->rtl &&
            AxisAnswers(input->width, entry->input.width, entry->size.width) &&
            AxisAnswers(input->height, entry->input.height, entry->size.height))
        {
            return entry;
        }
    }
    return nullptr;
}

static void StoreCached(muiLayoutCache* cache, const muiSizingInput* input, muiSize size)
{
    cache->entries[cache->next] = (muiCacheEntry){.input = *input, .size = size, .valid = true};
    cache->next = (uint8_t)((cache->next + 1) % MUI_CACHE_ENTRIES);
}

// The content-box constraint for host content from a border-box one.
static muiMeasureAxis ContentAxis(muiMeasureAxis axis, float box)
{
    if (IsSized(axis.mode))
    {
        return (muiMeasureAxis){fmaxf(axis.size - box, 0.0f), axis.mode};
    }
    return axis;
}

static float SaneLength(float value)
{
    return isfinite(value) && value > 0.0f ? value : 0.0f;
}

static muiSize SizeLeaf(const muiSolver* solver, uint32_t node, const muiSizingInput* input)
{
    const muiLayoutStyle* style = &solver->nodes[node - 1].style;
    float boxWidth = muiBoxSum(style, true);
    float boxHeight = muiBoxSum(style, false);
    muiSize content = {0.0f, 0.0f};
    if (style->content == mui_contentHost && solver->measure != nullptr)
    {
        muiNodeId id = muiTreeIdOf(solver->tree, node);
        uint64_t hostKey = muiTreeAt(solver->tree, node)->hostKey;
        content =
            solver->measure(solver->measureUser, id, hostKey, ContentAxis(input->width, boxWidth),
                            ContentAxis(input->height, boxHeight));
        content.width = SaneLength(content.width);
        content.height = SaneLength(content.height);
    }
    muiAxisSizing width = muiResolveAxis(&style->sizing, true, input->parentWidth);
    muiAxisSizing height = muiResolveAxis(&style->sizing, false, input->parentHeight);
    muiSize size;
    size.width =
        input->width.mode == mui_measureExact
            ? input->width.size
            : muiClampSize(content.width + boxWidth, width.minimum, width.maximum, boxWidth);
    size.height =
        input->height.mode == mui_measureExact
            ? input->height.size
            : muiClampSize(content.height + boxHeight, height.minimum, height.maximum, boxHeight);
    return size;
}

// Mirrors the children's horizontal positions in a container of width.
static void Mirror(const muiSolver* solver, uint32_t node, float width)
{
    for (uint32_t c = muiTreeAt(solver->tree, node)->links.firstChild; c != 0;
         c = muiTreeAt(solver->tree, c)->links.next)
    {
        muiRect* rect = &solver->nodes[c - 1].rect;
        rect->x = width - rect->x - rect->width;
    }
}

// Lays a container out in logical coordinates, start on the left, and
// mirrors it when its direction is right to left (record mui-0003).
static muiSize SizeContainer(const muiSolver* solver, uint32_t node, const muiSizingInput* input,
                             bool perform)
{
    muiSize size = muiLayoutFlex(solver, node, input, perform);
    if (perform)
    {
        muiPlaceAbsolute(solver, node, size, input->rtl);
        if (input->rtl)
        {
            Mirror(solver, node, size.width);
        }
    }
    return size;
}

// The input with the node's own direction in place of the inherited one.
static muiSizingInput OwnDirection(const muiLayoutStyle* style, const muiSizingInput* input)
{
    muiSizingInput own = *input;
    if (style->textDirection != mui_textInherit)
    {
        own.rtl = style->textDirection == mui_textRightToLeft;
    }
    return own;
}

muiSize muiSolveNode(const muiSolver* solver, uint32_t node, const muiSizingInput* input,
                     bool perform)
{
    muiLayoutCache* cache = &solver->nodes[node - 1].cache;
    if (perform)
    {
        MUI_ASSERT(input->width.mode == mui_measureExact && input->height.mode == mui_measureExact);
        if (cache->finalValid && cache->finalRtl == input->rtl &&
            cache->finalSize.width == input->width.size &&
            cache->finalSize.height == input->height.size)
        {
            return cache->finalSize;
        }
    }
    else
    {
        // The parent gave both sizes: nothing below can change them.
        if (input->width.mode == mui_measureExact && input->height.mode == mui_measureExact)
        {
            return (muiSize){input->width.size, input->height.size};
        }
        const muiCacheEntry* hit =
            FindCached(cache, input, ReadsParentExtent(&solver->nodes[node - 1].style.sizing));
        if (hit != nullptr)
        {
            return hit->size;
        }
    }
    muiSizingInput own = OwnDirection(&solver->nodes[node - 1].style, input);
    muiSize size = muiTreeAt(solver->tree, node)->links.firstChild == 0
                       ? SizeLeaf(solver, node, &own)
                       : SizeContainer(solver, node, &own, perform);
    if (perform)
    {
        cache->finalValid = true;
        cache->finalRtl = input->rtl;
        cache->finalSize = size;
    }
    else
    {
        StoreCached(cache, input, size);
    }
    return size;
}

muiSizingInput muiRootInput(const muiLayoutStyle* style, float availableWidth,
                            float availableHeight)
{
    muiSizingInput input = {
        .width = {availableWidth, mui_measureAtMost},
        .height = {availableHeight, mui_measureAtMost},
        .parentWidth = availableWidth,
        .parentHeight = availableHeight,
    };
    muiAxisSizing width = muiResolveAxis(&style->sizing, true, availableWidth);
    muiAxisSizing height = muiResolveAxis(&style->sizing, false, availableHeight);
    if (width.definite)
    {
        input.width = muiExact(
            muiClampSize(width.size, width.minimum, width.maximum, muiBoxSum(style, true)));
    }
    if (height.definite)
    {
        input.height = muiExact(
            muiClampSize(height.size, height.minimum, height.maximum, muiBoxSum(style, false)));
    }
    return input;
}
