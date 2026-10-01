// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Resolution in the fixed layers of record mui-0004: the defaults, every
// class's base values in order, the state variants (states weakest
// first, classes in order within each), the conditions that hold, then
// the direct writes, which the node's resolved values already hold.

#include "restyle.h"

#include "condition.h"
#include "layout_node.h"
#include "notify.h"
#include "pool.h"
#include "property.h"
#include "style_store.h"
#include "tree.h"

#include "maul-ui/layout.h"

// The live classes of a node, its type's then its own, as class slots.
typedef struct Classes
{
    uint32_t slots[2 * MUI_MAX_CLASSES];
    uint32_t count;
} Classes;

static void AddLive(const muiStyleStore* store, const muiClassList* list, Classes* classes)
{
    for (uint32_t i = 0; i < list->count; i++)
    {
        muiStyleId id = list->classes[i];
        uint32_t slot = muiPoolResolve(&store->classPool, id.index1, id.generation);
        if (slot != 0)
        {
            classes->slots[classes->count++] = slot;
        }
    }
}

static Classes ClassesOf(const muiStyleStore* store, const muiNodeStyle* node)
{
    Classes classes = {.count = 0};
    uint32_t type = muiPoolResolve(&store->typePool, node->type.index1, node->type.generation);
    if (type != 0)
    {
        AddLive(store, &store->types[type - 1], &classes);
    }
    AddLive(store, &node->classes, &classes);
    return classes;
}

// Applies one variant of every class, in class order, to the properties
// free names.
static void ApplyVariant(const muiStyleStore* store, const Classes* classes, muiVariant variant,
                         muiPropertyMask free, muiLayoutStyle* values)
{
    for (uint32_t i = 0; i < classes->count; i++)
    {
        uint32_t set = store->classes[classes->slots[i] - 1].sets[variant];
        if (set != 0)
        {
            const muiPropertySet* source = &store->sets[set - 1];
            muiApplyProperties(values, &source->values, source->mask & free);
        }
    }
}

// Applies the conditional values that hold, in class order and then
// condition order, sampling the node's last layout; records what was
// read and returns the run: which held, as one bit per condition with
// values in that order (folded past 64), and the sample.
static muiConditionRun ApplyConditions(muiContext* context, uint32_t slot, const Classes* classes,
                                       muiPropertyMask free, muiLayoutStyle* values)
{
    const muiStyleStore* store = &context->style;
    muiLayoutNode* layout = &context->layout[slot - 1];
    const muiConditionSample sample = {
        .width = layout->rect.width,
        .height = layout->rect.height,
        .rtl = layout->rtl,
        .environment = &context->environment,
    };
    muiConditionRun run = {.width = sample.width, .height = sample.height, .rtl = sample.rtl};
    muiConditionReads reads = 0;
    uint32_t position = 0;
    for (uint32_t i = 0; i < classes->count; i++)
    {
        const muiStyleClass* class = &store->classes[classes->slots[i] - 1];
        for (uint32_t k = 0; k < class->conditionCount; k++)
        {
            uint32_t set = class->sets[mui_variantCondition0 + k];
            if (set == 0)
            {
                continue;
            }
            reads |= muiConditionReadsOf(&class->conditions[k]);
            if (muiConditionHolds(&class->conditions[k], &sample))
            {
                const muiPropertySet* source = &store->sets[set - 1];
                muiApplyProperties(values, &source->values, source->mask & free);
                run.outcome |= (uint64_t)1 << (position % 64);
            }
            position++;
        }
    }
    layout->conditionReads = reads;
    layout->conditionSize = (muiSize){sample.width, sample.height};
    layout->conditionRtl = sample.rtl;
    return run;
}

static bool IsSameSample(const muiConditionRun* a, const muiConditionRun* b)
{
    return a->width == b->width && a->height == b->height && a->rtl == b->rtl;
}

// Adds a styling to the node's history and reports an oscillation: the
// last four stylings, none the host's but perhaps the first, read two
// samples in turn and their outcomes flipped with them. Its conditions
// are then held at this outcome (record mui-0004).
static void Watch(muiContext* context, uint32_t slot, const muiConditionRun* run)
{
    muiNodeStyle* node = &context->style.nodes[slot - 1];
    muiConditionRun* history = node->history;
    if (node->historyCount == MUI_CONDITION_HISTORY && IsSameSample(run, &history[1]) &&
        IsSameSample(&history[0], &history[2]) && !IsSameSample(run, &history[0]) &&
        run->outcome != history[0].outcome)
    {
        muiLayoutNode* layout = &context->layout[slot - 1];
        layout->held = true;
        layout->heldSizes[0] = (muiSize){run->width, run->height};
        layout->heldSizes[1] = (muiSize){history[0].width, history[0].height};
        node->historyCount = 0;
        const muiNotification record = {
            .kind = mui_notificationOscillation,
            .nodeId = muiTreeIdOf(&context->tree, slot),
        };
        muiNotifyPost(&context->notifications, &record);
        return;
    }
    for (uint32_t i = MUI_CONDITION_HISTORY - 1; i > 0; i--)
    {
        history[i] = history[i - 1];
    }
    history[0] = *run;
    if (node->historyCount < MUI_CONDITION_HISTORY)
    {
        node->historyCount++;
    }
}

// Starts the node's history again when the host caused this styling,
// rather than its own layout: it edited the node, or a class, a node type
// or the environment. That also releases a hold.
static void NoteHostEdit(muiContext* context, uint32_t slot)
{
    muiNodeStyle* node = &context->style.nodes[slot - 1];
    bool edited = node->edited || node->editsSeen != context->styleEdits;
    node->edited = false;
    node->editsSeen = context->styleEdits;
    if (edited)
    {
        node->historyCount = 0;
        context->layout[slot - 1].held = false;
    }
}

// Resolves the properties a node does not write directly; the direct
// ones already hold their values, which are carried over.
static void Resolve(muiContext* context, uint32_t slot)
{
    NoteHostEdit(context, slot);
    const muiStyleStore* store = &context->style;
    const muiNodeStyle* node = &store->nodes[slot - 1];
    muiPropertyMask free = MUI_LAYOUT_PROPERTIES & ~node->direct;
    muiLayoutNode* layout = &context->layout[slot - 1];
    layout->conditionReads = 0;
    if (free == 0)
    {
        return;
    }
    muiLayoutStyle values = *muiLayoutDefaults();
    muiApplyProperties(&values, &layout->style, node->direct);
    Classes classes = ClassesOf(store, node);
    ApplyVariant(store, &classes, mui_variantBase, free, &values);
    for (uint32_t v = mui_variantChecked; v < mui_variantCondition0; v++)
    {
        // Variant v belongs to state bit v - 1.
        if ((node->states & (1u << (v - 1))) != 0)
        {
            ApplyVariant(store, &classes, (muiVariant)v, free, &values);
        }
    }
    muiConditionRun run = ApplyConditions(context, slot, &classes, free, &values);
    if (layout->conditionReads != 0)
    {
        Watch(context, slot, &run);
    }
    if (muiDoPropertiesDiffer(&values, &layout->style, free))
    {
        layout->style = values;
        muiSyncLayoutNode(layout);
        muiTreeMarkLayout(&context->tree, slot);
    }
}

void muiRestyle(muiContext* context, uint32_t root)
{
    muiTree* tree = &context->tree;
    for (uint32_t at = muiTreeNextOwing(tree, root, 0, mui_stageStyle); at != 0;
         at = muiTreeNextOwing(tree, root, at, mui_stageStyle))
    {
        if ((muiTreeAt(tree, at)->dirty.request & mui_stageStyle) != 0)
        {
            Resolve(context, at);
        }
    }
    muiTreeSweep(tree, root, mui_stageStyle);
}
