// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The context and the one block of memory it reserves at creation.

#include "context.h"

#include "allocator.h"
#include "invariant.h"

#include "maul-ui/style.h"

#include <stdint.h>
#include <string.h>

#define CONTEXT_DEF_COOKIE 0x6D756378u // "mucx"

// Slots are 1-based uint32_t values with room for a parent link count.
#define MAX_SLOTS 0x7FFFFFFFu

muiContextDef muiDefaultContextDef(void)
{
    return (muiContextDef){
        .cookie = CONTEXT_DEF_COOKIE,
        .limits = {.nodes = 4096,
                   .styles = 256,
                   .nodeTypes = 64,
                   .propertySets = 1024,
                   .notifications = 64,
                   .transitions = 64,
                   .animations = 256,
                   .tokens = 256,
                   .tokenNames = 1024,
                   .themes = 16,
                   .themeOverrides = 512},
    };
}

static bool AreLimitsValid(const muiLimits* limits)
{
    return limits->nodes != 0 && limits->nodes <= MAX_SLOTS && limits->styles <= MAX_SLOTS &&
           limits->nodeTypes <= MAX_SLOTS && limits->propertySets <= MAX_SLOTS &&
           limits->notifications <= MAX_SLOTS && limits->transitions <= MAX_SLOTS &&
           limits->animations <= MAX_SLOTS && limits->tokens <= MAX_SLOTS &&
           limits->tokenNames <= MAX_SLOTS && limits->themes <= MAX_SLOTS &&
           limits->themeOverrides <= MAX_SLOTS;
}

// Where each part of the context's block starts.
typedef struct Parts
{
    size_t nodes;
    size_t layout;
    size_t visual;
    size_t nodeStyles;
    size_t classSlots;
    size_t classes;
    size_t typeSlots;
    size_t types;
    size_t setSlots;
    size_t sets;
    size_t notifications;
    size_t specSlots;
    size_t specs;
    size_t recordSlots;
    size_t records;
    size_t tokenSlots;
    size_t tokens;
    size_t nameSlots;
    size_t names;
    size_t themeSlots;
    size_t themeTables;
    size_t overrideSlots;
    size_t overrides;
} Parts;

// A table per theme, an entry per token slot; a count past size_t marks
// the layout as overflowing.
static size_t ThemeTableEntries(muiLayout* layout, const muiLimits* limits)
{
    uint64_t entries = (uint64_t)limits->themes * limits->tokens;
    if (entries > SIZE_MAX)
    {
        layout->overflow = true;
        return 0;
    }
    return (size_t)entries;
}

static Parts LayOut(muiLayout* layout, const muiLimits* limits)
{
    // The context opens the block, so the block is freed through it.
    size_t contextOffset = muiLayoutAdd(layout, 1, sizeof(muiContext), alignof(muiContext));
    MUI_ASSERT(contextOffset == 0);
    (void)contextOffset;
    return (Parts){
        .nodes = muiLayoutAdd(layout, limits->nodes, sizeof(muiTreeNode), alignof(muiTreeNode)),
        .layout =
            muiLayoutAdd(layout, limits->nodes, sizeof(muiLayoutNode), alignof(muiLayoutNode)),
        .visual =
            muiLayoutAdd(layout, limits->nodes, sizeof(muiVisualStyle), alignof(muiVisualStyle)),
        .nodeStyles =
            muiLayoutAdd(layout, limits->nodes, sizeof(muiNodeStyle), alignof(muiNodeStyle)),
        .classSlots =
            muiLayoutAdd(layout, limits->styles, sizeof(muiPoolSlot), alignof(muiPoolSlot)),
        .classes =
            muiLayoutAdd(layout, limits->styles, sizeof(muiStyleClass), alignof(muiStyleClass)),
        .typeSlots =
            muiLayoutAdd(layout, limits->nodeTypes, sizeof(muiPoolSlot), alignof(muiPoolSlot)),
        .types =
            muiLayoutAdd(layout, limits->nodeTypes, sizeof(muiClassList), alignof(muiClassList)),
        .setSlots =
            muiLayoutAdd(layout, limits->propertySets, sizeof(muiPoolSlot), alignof(muiPoolSlot)),
        .sets = muiLayoutAdd(layout, limits->propertySets, sizeof(muiPropertySet),
                             alignof(muiPropertySet)),
        .notifications = muiLayoutAdd(layout, limits->notifications, sizeof(muiNotification),
                                      alignof(muiNotification)),
        .specSlots =
            muiLayoutAdd(layout, limits->transitions, sizeof(muiPoolSlot), alignof(muiPoolSlot)),
        .specs = muiLayoutAdd(layout, limits->transitions, sizeof(muiTransitionSpec),
                              alignof(muiTransitionSpec)),
        .recordSlots =
            muiLayoutAdd(layout, limits->animations, sizeof(muiPoolSlot), alignof(muiPoolSlot)),
        .records =
            muiLayoutAdd(layout, limits->animations, sizeof(muiAnimation), alignof(muiAnimation)),
        .tokenSlots =
            muiLayoutAdd(layout, limits->tokens, sizeof(muiPoolSlot), alignof(muiPoolSlot)),
        .tokens = muiLayoutAdd(layout, limits->tokens, sizeof(muiToken), alignof(muiToken)),
        .nameSlots =
            muiLayoutAdd(layout, limits->tokenNames, sizeof(muiPoolSlot), alignof(muiPoolSlot)),
        .names =
            muiLayoutAdd(layout, limits->tokenNames, sizeof(muiTokenName), alignof(muiTokenName)),
        .themeSlots =
            muiLayoutAdd(layout, limits->themes, sizeof(muiPoolSlot), alignof(muiPoolSlot)),
        .themeTables = muiLayoutAdd(layout, ThemeTableEntries(layout, limits), sizeof(uint32_t),
                                    alignof(uint32_t)),
        .overrideSlots =
            muiLayoutAdd(layout, limits->themeOverrides, sizeof(muiPoolSlot), alignof(muiPoolSlot)),
        .overrides = muiLayoutAdd(layout, limits->themeOverrides, sizeof(muiThemeOverride),
                                  alignof(muiThemeOverride)),
    };
}

muiResult muiCreateContext(const muiContextDef* def, muiContext** contextOut)
{
    if (contextOut != nullptr)
    {
        *contextOut = nullptr;
    }
    if (def == nullptr || contextOut == nullptr || def->cookie != CONTEXT_DEF_COOKIE ||
        !muiIsAllocatorValid(&def->allocator) || !AreLimitsValid(&def->limits))
    {
        return mui_errorInvalid;
    }
    muiLayout layout = {0};
    Parts parts = LayOut(&layout, &def->limits);
    if (layout.overflow)
    {
        return mui_errorCapacity;
    }
    unsigned char* block = muiAllocate(&def->allocator, layout.size, alignof(max_align_t));
    if (block == nullptr)
    {
        return mui_errorCapacity;
    }
    memset(block, 0, layout.size);
    muiContext* context = (muiContext*)block;
    context->allocator = def->allocator;
    context->blockSize = layout.size;
    muiTreeInit(&context->tree, (muiTreeNode*)(block + parts.nodes), def->limits.nodes);
    context->layout = (muiLayoutNode*)(block + parts.layout);
    context->visual = (muiVisualStyle*)(block + parts.visual);
    context->environment = muiDefaultEnvironment();
    muiStyleStore* style = &context->style;
    style->nodes = (muiNodeStyle*)(block + parts.nodeStyles);
    muiPoolInit(&style->classPool, (muiPoolSlot*)(block + parts.classSlots), def->limits.styles);
    style->classes = (muiStyleClass*)(block + parts.classes);
    muiPoolInit(&style->typePool, (muiPoolSlot*)(block + parts.typeSlots), def->limits.nodeTypes);
    style->types = (muiClassList*)(block + parts.types);
    muiPoolInit(&style->setPool, (muiPoolSlot*)(block + parts.setSlots), def->limits.propertySets);
    style->sets = (muiPropertySet*)(block + parts.sets);
    muiNotifyInit(&context->notifications, (muiNotification*)(block + parts.notifications),
                  def->limits.notifications);
    muiAnimationStore* animations = &context->animations;
    muiPoolInit(&animations->specPool, (muiPoolSlot*)(block + parts.specSlots),
                def->limits.transitions);
    animations->specs = (muiTransitionSpec*)(block + parts.specs);
    muiPoolInit(&animations->pool, (muiPoolSlot*)(block + parts.recordSlots),
                def->limits.animations);
    animations->records = (muiAnimation*)(block + parts.records);
    muiPoolInit(&context->tokens.pool, (muiPoolSlot*)(block + parts.tokenSlots),
                def->limits.tokens);
    context->tokens.tokens = (muiToken*)(block + parts.tokens);
    muiPoolInit(&style->namePool, (muiPoolSlot*)(block + parts.nameSlots), def->limits.tokenNames);
    style->names = (muiTokenName*)(block + parts.names);
    muiThemeStore* themes = &context->themes;
    muiPoolInit(&themes->pool, (muiPoolSlot*)(block + parts.themeSlots), def->limits.themes);
    themes->tables = (uint32_t*)(block + parts.themeTables);
    themes->tokenCapacity = def->limits.tokens;
    muiPoolInit(&themes->overridePool, (muiPoolSlot*)(block + parts.overrideSlots),
                def->limits.themeOverrides);
    themes->overrides = (muiThemeOverride*)(block + parts.overrides);
    *contextOut = context;
    return mui_success;
}

void muiDestroyContext(muiContext* context)
{
    if (context == nullptr)
    {
        return;
    }
    const muiAllocator allocator = context->allocator;
    muiRelease(&allocator, context, context->blockSize, alignof(max_align_t));
}

uint64_t muiGetContextMisuse(const muiContext* context)
{
    return context != nullptr ? context->misuse : 0;
}

muiResult muiRefuse(muiContext* context)
{
    context->misuse++;
    return mui_errorInvalid;
}

bool muiIsMeasuring(const muiContext* context)
{
    return context->measuring;
}

uint32_t muiResolveEdit(muiContext* context, muiNodeId nodeId, muiResult* statusOut)
{
    if (nodeId.index1 == 0 || muiIsMeasuring(context))
    {
        *statusOut = muiRefuse(context);
        return 0;
    }
    uint32_t slot = muiTreeResolve(&context->tree, nodeId);
    *statusOut = slot != 0 ? mui_success : mui_errorStale;
    return slot;
}

muiResult muiNextNotification(muiContext* context, muiNotification* notificationOut)
{
    if (context == nullptr || notificationOut == nullptr)
    {
        return mui_errorInvalid;
    }
    return muiNotifyTake(&context->notifications, notificationOut) ? mui_success : mui_empty;
}
