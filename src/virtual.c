// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Virtualization (record mui-0007): the table of virtual lists, their
// items' extents in Fenwick trees, the windows reported after layout,
// and bound items measured and placed.

#include "maul-ui/virtual.h"

#include "context.h"
#include "layout_node.h"
#include "notify.h"
#include "scroll_store.h"
#include "tree.h"
#include "virtual.h"
#include "virtual_store.h"

#include <math.h>

static bool IsLength(float value)
{
    return isfinite(value) && value >= 0.0f;
}

static bool IsValid(const muiVirtualList* list)
{
    return list->axis <= mui_listHorizontal && isfinite(list->extent) && list->extent > 0.0f &&
           IsLength(list->gap) && IsLength(list->overscan);
}

muiVirtualList muiDefaultVirtualList(void)
{
    return (muiVirtualList){
        .axis = mui_listVertical, .extent = 40.0f, .gap = 0.0f, .overscan = 200.0f};
}

// The entry of the list at slot; NULL for a node that is no list.
static muiVirtualEntry* EntryOf(const muiContext* context, uint32_t slot)
{
    const muiVirtualStore* store = &context->lists;
    for (uint32_t i = 0; i < store->count; i++)
    {
        muiVirtualEntry* entry = &store->entries[i];
        if (entry->node.index1 == slot && muiTreeResolve(&context->tree, entry->node) == slot)
        {
            return entry;
        }
    }
    return nullptr;
}

// The live entry of a node; NULL for a node that is no list.
static muiVirtualEntry* Find(const muiContext* context, muiNodeId nodeId, muiResult* statusOut)
{
    if (nodeId.index1 == 0)
    {
        *statusOut = mui_errorInvalid;
        return nullptr;
    }
    uint32_t slot = muiTreeResolve(&context->tree, nodeId);
    muiVirtualEntry* entry = slot != 0 ? EntryOf(context, slot) : nullptr;
    *statusOut = slot == 0 ? mui_errorStale : entry == nullptr ? mui_empty : mui_success;
    return entry;
}

// Takes out the entries of nodes destroyed since.
static void Purge(muiContext* context)
{
    muiVirtualStore* store = &context->lists;
    for (uint32_t i = store->count; i > 0; i--)
    {
        if (muiTreeResolve(&context->tree, store->entries[i - 1].node) == 0)
        {
            store->entries[i - 1] = store->entries[--store->count];
        }
    }
}

// Whether an entry holds item storage.
static bool Stores(const muiVirtualEntry* entry)
{
    return !entry->list.fixed && entry->list.count != 0;
}

// The first start from which count items fit in the item storage, past
// every other entry's that overlaps; the capacity when none does.
static uint32_t Room(const muiVirtualStore* store, const muiVirtualEntry* self, uint32_t count)
{
    // Candidates: the start, and the end of each other entry's items.
    for (uint32_t c = 0; c <= store->count; c++)
    {
        uint32_t start = 0;
        if (c < store->count)
        {
            const muiVirtualEntry* after = &store->entries[c];
            if (!Stores(after))
            {
                continue;
            }
            start = after->base + after->list.count;
        }
        // Every entry's items lie within the capacity, so start does too.
        bool fits = store->itemCapacity - start >= count;
        for (uint32_t i = 0; fits && i < store->count; i++)
        {
            const muiVirtualEntry* other = &store->entries[i];
            fits = other == self || !Stores(other) || other->base >= start + count ||
                   other->base + other->list.count <= start;
        }
        if (fits)
        {
            return start;
        }
    }
    return store->itemCapacity;
}

// Item i's place in its entry's Fenwick tree, 1-based.
static double* SumsOf(const muiVirtualStore* store, const muiVirtualEntry* entry)
{
    return store->sums + entry->base - 1;
}

// Sets every item of an estimated entry to the estimate and builds its
// tree in O(count).
static void Build(muiVirtualStore* store, const muiVirtualEntry* entry)
{
    uint32_t count = entry->list.count;
    float* sizes = store->sizes + entry->base;
    double* sums = SumsOf(store, entry);
    for (uint32_t k = 1; k <= count; k++)
    {
        sizes[k - 1] = entry->list.extent;
        sums[k] = 0.0;
    }
    for (uint32_t k = 1; k <= count; k++)
    {
        sums[k] += (double)entry->list.extent + (double)entry->list.gap;
        uint32_t up = k + (k & (0u - k));
        if (up <= count)
        {
            sums[up] += sums[k];
        }
    }
}

// Adds delta to item i's extent in its entry's tree.
static void Add(const muiVirtualStore* store, const muiVirtualEntry* entry, uint32_t i,
                double delta)
{
    double* sums = SumsOf(store, entry);
    for (uint32_t k = i + 1; k <= entry->list.count; k += k & (0u - k))
    {
        sums[k] += delta;
    }
}

// Where item i begins: the extents and gaps of the items before it.
static double OffsetOf(const muiVirtualStore* store, const muiVirtualEntry* entry, uint32_t i)
{
    const muiVirtualList* list = &entry->list;
    if (list->fixed)
    {
        return (double)i * ((double)list->extent + (double)list->gap);
    }
    const double* sums = SumsOf(store, entry);
    double sum = 0.0;
    for (uint32_t k = i; k > 0; k -= k & (0u - k))
    {
        sum += sums[k];
    }
    return sum;
}

static float ExtentOf(const muiVirtualStore* store, const muiVirtualEntry* entry, uint32_t i)
{
    return entry->list.fixed ? entry->list.extent : store->sizes[entry->base + i];
}

// All the items' extents and the gaps between them.
static double TotalOf(const muiVirtualStore* store, const muiVirtualEntry* entry)
{
    uint32_t count = entry->list.count;
    return count == 0 ? 0.0 : OffsetOf(store, entry, count) - (double)entry->list.gap;
}

// The item whose place, its extent and the gap after it, holds offset;
// the count past the last.
static uint32_t IndexAt(const muiVirtualStore* store, const muiVirtualEntry* entry, double offset)
{
    const muiVirtualList* list = &entry->list;
    if (offset < 0.0)
    {
        return 0;
    }
    if (list->fixed)
    {
        double index = floor(offset / ((double)list->extent + (double)list->gap));
        return index >= (double)list->count ? list->count : (uint32_t)index;
    }
    // Descends the tree: the most items whose places end at or before it.
    const double* sums = SumsOf(store, entry);
    uint32_t at = 0;
    uint32_t step = 1;
    while (step <= list->count / 2)
    {
        step <<= 1;
    }
    for (; step != 0; step >>= 1)
    {
        if (at + step <= list->count && sums[at + step] <= offset)
        {
            at += step;
            offset -= sums[at];
        }
    }
    return at;
}

// Sets the content the list's items need on its scroll state.
static void SetLength(muiContext* context, uint32_t slot, const muiVirtualEntry* entry)
{
    muiScrollState* scroll = &context->scrolls[slot - 1];
    float total = entry != nullptr ? (float)TotalOf(&context->lists, entry) : 0.0f;
    bool horizontal = entry != nullptr && entry->list.axis == mui_listHorizontal;
    scroll->listX = horizontal ? total : 0.0f;
    scroll->listY = entry != nullptr && !horizontal ? total : 0.0f;
}

muiResult muiNode_SetVirtualList(muiContext* context, muiNodeId nodeId, const muiVirtualList* list)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (list == nullptr || !IsValid(list))
    {
        return muiRefuse(context);
    }
    muiResult status = mui_success;
    uint32_t slot = muiResolveEdit(context, nodeId, &status);
    if (slot == 0)
    {
        return status;
    }
    muiVirtualStore* store = &context->lists;
    muiVirtualEntry* entry = EntryOf(context, slot);
    if (entry == nullptr && store->count == store->capacity)
    {
        Purge(context);
    }
    if (entry == nullptr && store->count == store->capacity)
    {
        return mui_errorCapacity;
    }
    uint32_t base = 0;
    if (!list->fixed && list->count != 0)
    {
        base = Room(store, entry, list->count);
        if (base == store->itemCapacity)
        {
            Purge(context);
            entry = EntryOf(context, slot);
            base = Room(store, entry, list->count);
        }
        if (base == store->itemCapacity)
        {
            return mui_errorCapacity;
        }
    }
    if (entry == nullptr)
    {
        entry = &store->entries[store->count++];
    }
    *entry =
        (muiVirtualEntry){.node = muiTreeIdOf(&context->tree, slot), .list = *list, .base = base};
    if (Stores(entry))
    {
        Build(store, entry);
    }
    SetLength(context, slot, entry);
    muiTreeMarkLayout(&context->tree, slot);
    return mui_success;
}

muiResult muiNode_ClearVirtualList(muiContext* context, muiNodeId nodeId)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    if (muiIsInHostCall(context))
    {
        return muiRefuse(context);
    }
    muiResult status = mui_success;
    muiVirtualEntry* entry = Find(context, nodeId, &status);
    if (status == mui_errorInvalid)
    {
        return muiRefuse(context);
    }
    if (entry != nullptr)
    {
        uint32_t slot = muiTreeResolve(&context->tree, nodeId);
        *entry = context->lists.entries[--context->lists.count];
        SetLength(context, slot, nullptr);
        muiTreeMarkLayout(&context->tree, slot);
    }
    return status == mui_empty ? mui_success : status;
}

muiResult muiNode_GetVirtualWindow(const muiContext* context, muiNodeId nodeId, uint32_t* firstOut,
                                   uint32_t* endOut)
{
    if (context == nullptr || firstOut == nullptr || endOut == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    const muiVirtualEntry* entry = Find(context, nodeId, &status);
    if (entry == nullptr || !entry->windowed)
    {
        return entry == nullptr ? status : mui_empty;
    }
    *firstOut = entry->first;
    *endOut = entry->end;
    return mui_success;
}

muiResult muiNode_GetVirtualItem(const muiContext* context, muiNodeId nodeId, uint32_t index,
                                 float* offsetOut, float* extentOut)
{
    if (context == nullptr || offsetOut == nullptr || extentOut == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    const muiVirtualEntry* entry = Find(context, nodeId, &status);
    if (entry == nullptr)
    {
        return status;
    }
    if (index >= entry->list.count)
    {
        return mui_errorInvalid;
    }
    *offsetOut = (float)OffsetOf(&context->lists, entry, index);
    *extentOut = ExtentOf(&context->lists, entry, index);
    return mui_success;
}

// Binds or unbinds the node at slot, in or out of its parent's flow.
static void Bind(muiContext* context, uint32_t slot, uint32_t item)
{
    muiLayoutNode* layout = &context->layout[slot - 1];
    context->lists.items[slot - 1] = item;
    if (layout->listed != (item != 0))
    {
        layout->listed = item != 0;
        muiSyncLayoutNode(layout);
    }
    muiTreeMarkLayout(&context->tree, slot);
}

muiResult muiNode_SetItem(muiContext* context, muiNodeId nodeId, uint32_t index)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    uint32_t slot = muiResolveEdit(context, nodeId, &status);
    if (slot == 0)
    {
        return status;
    }
    uint32_t parent = muiTreeAt(&context->tree, slot)->links.parent;
    const muiVirtualEntry* entry = parent != 0 ? EntryOf(context, parent) : nullptr;
    if (entry == nullptr || index >= entry->list.count)
    {
        return muiRefuse(context);
    }
    Bind(context, slot, index + 1);
    return mui_success;
}

muiResult muiNode_ClearItem(muiContext* context, muiNodeId nodeId)
{
    if (context == nullptr)
    {
        return mui_errorInvalid;
    }
    muiResult status = mui_success;
    uint32_t slot = muiResolveEdit(context, nodeId, &status);
    if (slot != 0 && context->lists.items[slot - 1] != 0)
    {
        Bind(context, slot, 0);
    }
    return status;
}

// Measures, places and sizes one list at slot.
static void PlaceList(muiContext* context, uint32_t slot, const muiVirtualEntry* entry)
{
    muiVirtualStore* store = &context->lists;
    const muiTree* tree = &context->tree;
    const muiLayoutNode* list = &context->layout[slot - 1];
    bool horizontal = entry->list.axis == mui_listHorizontal;
    // Measured first, so that every place reads every extent.
    for (uint32_t c = muiTreeAt(tree, slot)->links.firstChild; c != 0 && Stores(entry);
         c = muiTreeAt(tree, c)->links.next)
    {
        uint32_t item = store->items[c - 1];
        if (item == 0 || item > entry->list.count)
        {
            continue;
        }
        const muiRect* rect = &context->layout[c - 1].rect;
        float size = horizontal ? rect->width : rect->height;
        float* known = &store->sizes[entry->base + item - 1];
        if (size != *known)
        {
            Add(store, entry, item - 1, (double)size - (double)*known);
            *known = size;
        }
    }
    const muiLayoutStyle* style = &list->style;
    for (uint32_t c = muiTreeAt(tree, slot)->links.firstChild; c != 0;
         c = muiTreeAt(tree, c)->links.next)
    {
        uint32_t item = store->items[c - 1];
        if (item == 0 || item > entry->list.count)
        {
            continue;
        }
        muiRect* rect = &context->layout[c - 1].rect;
        float offset = (float)OffsetOf(store, entry, item - 1);
        if (!horizontal)
        {
            rect->y = style->border.top + style->padding.top + offset;
        }
        else if (list->rtl)
        {
            rect->x = list->rect.width - style->border.start - style->padding.start - offset -
                      rect->width;
        }
        else
        {
            rect->x = style->border.start + style->padding.start + offset;
        }
        if (!muiIsSameRect(*rect, context->draw.states[c - 1].rect))
        {
            muiTreeMark(&context->tree, c, mui_stagePaint);
        }
    }
    SetLength(context, slot, entry);
    muiScrollState* scroll = &context->scrolls[slot - 1];
    if (horizontal)
    {
        scroll->extentWidth =
            fmaxf(scroll->extentWidth, style->padding.start + scroll->listX + style->padding.end);
    }
    else
    {
        scroll->extentHeight =
            fmaxf(scroll->extentHeight, style->padding.top + scroll->listY + style->padding.bottom);
    }
}

void muiVirtualPlace(muiContext* context, uint32_t root)
{
    const muiVirtualStore* store = &context->lists;
    for (uint32_t i = 0; i < store->count; i++)
    {
        const muiVirtualEntry* entry = &store->entries[i];
        uint32_t slot = muiTreeResolve(&context->tree, entry->node);
        if (slot != 0 && muiTreeIsAncestor(&context->tree, root, slot))
        {
            PlaceList(context, slot, entry);
        }
    }
}

void muiVirtualWindows(muiContext* context, uint32_t root)
{
    muiVirtualStore* store = &context->lists;
    for (uint32_t i = 0; i < store->count; i++)
    {
        muiVirtualEntry* entry = &store->entries[i];
        uint32_t slot = muiTreeResolve(&context->tree, entry->node);
        if (slot == 0 || !muiTreeIsAncestor(&context->tree, root, slot))
        {
            continue;
        }
        // The viewport in the items' coordinates, from the content box's
        // start, widened by the overscan. It ends at 0 or later: a box is
        // no smaller than its padding.
        const muiLayoutNode* list = &context->layout[slot - 1];
        const muiScrollState* scroll = &context->scrolls[slot - 1];
        bool horizontal = entry->list.axis == mui_listHorizontal;
        const muiLayoutStyle* style = &list->style;
        double start = horizontal ? (double)scroll->x - (double)style->padding.start
                                  : (double)scroll->y - (double)style->padding.top;
        double port = horizontal ? (double)list->rect.width - (double)style->border.start -
                                       (double)style->border.end
                                 : (double)list->rect.height - (double)style->border.top -
                                       (double)style->border.bottom;
        double overscan = (double)entry->list.overscan;
        double low = start - overscan;
        double high = start + port + overscan;
        uint32_t count = entry->list.count;
        uint32_t end = count == 0 ? 0 : IndexAt(store, entry, high) + 1;
        end = end > count ? count : end;
        // At most end: low is below high, and an empty window starts at 0.
        uint32_t first = IndexAt(store, entry, low);
        if (!entry->windowed || first != entry->first || end != entry->end)
        {
            entry->first = first;
            entry->end = end;
            entry->windowed = true;
            const muiNotification record = {mui_notificationWindowChanged, entry->node, 0};
            muiNotifyPost(&context->notifications, &record);
        }
    }
}
