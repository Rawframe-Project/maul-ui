// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes sequences of public calls, the API's own untrusted input: any
// bytes are a list of calls (nodes made, destroyed, moved and detached,
// layout and visual values written raw, so enumerators out of range and
// lengths not finite reach the checks, classes and states, layouts, draw
// lists, scrolling, pointers, focus and hit tests), with ids that may be
// stale, null or made up. The calls go to two contexts alike, and each must
// answer both the same, with a status its contract allows. Now and then
// every node of the second is marked changed, so it is solved from
// nothing, and both are laid out: the first's bounded layout must give
// every node the same rectangle, and both the same draw list, byte for
// byte. The library runs with its assertions on, so a call that reaches a
// broken invariant traps.

#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/focus.h"
#include "maul-ui/interaction.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/pointer.h"
#include "maul-ui/scroll.h"
#include "maul-ui/style.h"
#include "maul-ui/visual.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

enum
{
    // Ids the calls choose among, live, destroyed or never made.
    MAX_IDS = 48,
    MAX_STYLES = 4,
    MAX_CALLS = 256,
    // The kinds of call an input's bytes choose among.
    CALL_KINDS = 17
};

static void Expect(bool condition)
{
    if (!condition)
    {
        abort();
    }
}

// Whether a result is one of the statuses the library answers with.
static bool IsStatus(muiResult result)
{
    return result >= mui_errorState && result <= mui_empty;
}

typedef struct Reader
{
    const uint8_t* at;
    size_t left;
} Reader;

static uint8_t Byte(Reader* reader)
{
    if (reader->left == 0)
    {
        return 0;
    }
    reader->left--;
    return *reader->at++;
}

static uint32_t Word(Reader* reader)
{
    uint32_t value = 0;
    for (int i = 0; i < 4; i++)
    {
        value = value << 8 | Byte(reader);
    }
    return value;
}

// A float: mostly a small length, sometimes any bits, NaN and the
// infinities among them.
static float Number(Reader* reader)
{
    uint8_t pick = Byte(reader);
    if (pick < 192)
    {
        return (float)(pick % 64) * 4.0f - 16.0f;
    }
    uint32_t bits = Word(reader);
    float value = 0.0f;
    memcpy(&value, &bits, sizeof value);
    return value;
}

typedef struct World
{
    muiContext* contexts[2];
    muiNodeId ids[MAX_IDS];
    uint32_t idCount;
    muiStyleId styles[MAX_STYLES];
    uint32_t styleCount;
    uint64_t now;
} World;

static muiNodeId PickNode(const World* world, Reader* reader)
{
    uint8_t pick = Byte(reader);
    if (pick < 16 || world->idCount == 0)
    {
        // The null id, or one never handed out.
        return pick < 8 ? (muiNodeId){0, 0} : (muiNodeId){Word(reader), pick};
    }
    return world->ids[pick % world->idCount];
}

static muiStyleId PickStyle(const World* world, Reader* reader)
{
    uint8_t pick = Byte(reader);
    if (pick < 16 || world->styleCount == 0)
    {
        return (muiStyleId){pick, pick};
    }
    return world->styles[pick % world->styleCount];
}

// Host content: a size from its key, whatever the axes ask.
static muiSize Measure(void* user, muiNodeId nodeId, uint64_t hostKey, muiMeasureAxis width,
                       muiMeasureAxis height)
{
    (void)user;
    (void)nodeId;
    (void)width;
    (void)height;
    return (muiSize){(float)(hostKey % 97), (float)(hostKey % 31) + 1.0f};
}

// Both contexts answer a call the same, with a status.
static muiResult Both(muiResult a, muiResult b)
{
    Expect(IsStatus(a) && a == b);
    return a;
}

static void LayOut(World* world, muiNodeId root, const muiLayoutInput* input)
{
    (void)Both(muiComputeLayout(world->contexts[0], root, input),
               muiComputeLayout(world->contexts[1], root, input));
}

// Marks every node of the second context changed, lays both out, and
// holds the first's bounded answers to the second's from nothing.
static void Compare(World* world, muiNodeId root, const muiLayoutInput* input)
{
    if (!muiNode_IsValid(world->contexts[0], root) ||
        muiNode_GetParent(world->contexts[0], root).index1 != 0)
    {
        return;
    }
    for (uint32_t i = 0; i < world->idCount; i++)
    {
        (void)muiNode_MarkContentChanged(world->contexts[1], world->ids[i]);
    }
    muiResult result = Both(muiComputeLayout(world->contexts[0], root, input),
                            muiComputeLayout(world->contexts[1], root, input));
    if (result != mui_success)
    {
        return;
    }
    for (uint32_t i = 0; i < world->idCount; i++)
    {
        muiRect a = muiNode_GetRect(world->contexts[0], world->ids[i]);
        muiRect b = muiNode_GetRect(world->contexts[1], world->ids[i]);
        Expect(memcmp(&a, &b, sizeof a) == 0);
    }
    // Equal trees give equal draw lists, byte for byte, but for the
    // generation: a build with nothing to repaint keeps the last list,
    // generation and all, and only the second was marked.
    const muiDrawInput draw = {1, 1.0f, nullptr, nullptr};
    if (Both(muiBuildDrawList(world->contexts[0], root, &draw),
             muiBuildDrawList(world->contexts[1], root, &draw)) != mui_success)
    {
        return;
    }
    muiDrawList lists[2];
    for (int i = 0; i < 2; i++)
    {
        Expect(muiGetDrawList(world->contexts[i], &lists[i]) == mui_success);
    }
    Expect(lists[0].commandCount == lists[1].commandCount &&
           lists[0].clipCount == lists[1].clipCount &&
           lists[0].transformCount == lists[1].transformCount &&
           lists[0].gradientCount == lists[1].gradientCount &&
           lists[0].glyphCount == lists[1].glyphCount &&
           lists[0].header.surface == lists[1].header.surface &&
           memcmp(&lists[0].header.width, &lists[1].header.width, 3 * sizeof(float)) == 0 &&
           memcmp(lists[0].commands, lists[1].commands,
                  lists[0].commandCount * sizeof(muiDrawCommand)) == 0 &&
           memcmp(lists[0].clips, lists[1].clips, lists[0].clipCount * sizeof(muiDrawClip)) == 0 &&
           memcmp(lists[0].transforms, lists[1].transforms,
                  lists[0].transformCount * sizeof(muiDrawTransform)) == 0);
}

static void Call(World* world, Reader* reader)
{
    muiContext** contexts = world->contexts;
    switch (Byte(reader) % CALL_KINDS)
    {
    case 0:
    {
        muiNodeDef def = muiDefaultNodeDef();
        def.hostKey = Byte(reader);
        if (Byte(reader) < 8)
        {
            def.cookie = Word(reader);
        }
        muiNodeId ids[2] = {{0, 0}, {0, 0}};
        muiResult result = Both(muiCreateNode(contexts[0], &def, &ids[0]),
                                muiCreateNode(contexts[1], &def, &ids[1]));
        Expect(memcmp(&ids[0], &ids[1], sizeof ids[0]) == 0);
        if (result == mui_success && world->idCount < MAX_IDS)
        {
            world->ids[world->idCount++] = ids[0];
        }
        break;
    }
    case 1:
    {
        muiNodeId node = PickNode(world, reader);
        (void)Both(muiDestroyNode(contexts[0], node), muiDestroyNode(contexts[1], node));
        break;
    }
    case 2:
    {
        muiNodeId parent = PickNode(world, reader);
        muiNodeId child = PickNode(world, reader);
        muiNodeId before = Byte(reader) < 128 ? (muiNodeId){0, 0} : PickNode(world, reader);
        (void)Both(muiNode_InsertChild(contexts[0], parent, child, before),
                   muiNode_InsertChild(contexts[1], parent, child, before));
        break;
    }
    case 3:
    {
        muiNodeId node = PickNode(world, reader);
        (void)Both(muiNode_Detach(contexts[0], node), muiNode_Detach(contexts[1], node));
        break;
    }
    case 4:
    case 5:
    {
        // Layout values written raw over the defaults: a few bytes at a
        // place the input picks, and a mask of any bits.
        muiNodeId node = PickNode(world, reader);
        muiLayoutStyle style = muiDefaultLayoutStyle();
        size_t at = Byte(reader) % sizeof style;
        size_t length = 1 + Byte(reader) % 8;
        for (size_t i = at; i < at + length && i < sizeof style; i++)
        {
            ((unsigned char*)&style)[i] = Byte(reader);
        }
        muiPropertyMask mask = (muiPropertyMask)Word(reader) << 32 | Word(reader);
        (void)Both(muiNode_SetLayoutValues(contexts[0], node, &style, mask),
                   muiNode_SetLayoutValues(contexts[1], node, &style, mask));
        break;
    }
    case 6:
    {
        muiNodeId node = PickNode(world, reader);
        (void)Both(muiNode_MarkContentChanged(contexts[0], node),
                   muiNode_MarkContentChanged(contexts[1], node));
        break;
    }
    case 7:
    case 8:
    {
        muiNodeId root = PickNode(world, reader);
        world->now += Byte(reader) * 1000000ull;
        muiLayoutInput input = {Number(reader), Number(reader), Measure,     nullptr,
                                world->now,     nullptr,        {0, 0, 0, 0}};
        if (Byte(reader) < 16)
        {
            input.safeArea =
                (muiSides){Number(reader), Number(reader), Number(reader), Number(reader)};
        }
        if (Byte(reader) < 64)
        {
            Compare(world, root, &input);
        }
        else
        {
            LayOut(world, root, &input);
        }
        break;
    }
    case 9:
    {
        muiNodeId root = PickNode(world, reader);
        const muiDrawInput input = {Byte(reader), Number(reader), nullptr, nullptr};
        (void)Both(muiBuildDrawList(contexts[0], root, &input),
                   muiBuildDrawList(contexts[1], root, &input));
        break;
    }
    case 10:
    {
        muiNodeId node = PickNode(world, reader);
        float x = Number(reader);
        float y = Number(reader);
        (void)Both(muiNode_SetScroll(contexts[0], node, x, y),
                   muiNode_SetScroll(contexts[1], node, x, y));
        break;
    }
    case 11:
    {
        muiNodeId root = PickNode(world, reader);
        world->now += Byte(reader) * 1000000ull;
        muiPointerEvent event = {0};
        event.timeNs = world->now;
        event.pointer = Byte(reader) % 4;
        event.kind = (muiPointerKind)(Byte(reader) % 5);
        event.action = (muiPointerAction)(Byte(reader) % 6);
        event.button = Byte(reader) % 10;
        event.buttons = (muiPointerButtons)Byte(reader);
        event.x = Number(reader);
        event.y = Number(reader);
        event.player = Byte(reader) % 10;
        (void)Both(muiPointerInput(contexts[0], root, &event),
                   muiPointerInput(contexts[1], root, &event));
        break;
    }
    case 12:
    {
        muiNodeId root = PickNode(world, reader);
        uint8_t player = Byte(reader) % 10;
        bool backward = Byte(reader) & 1;
        (void)Both(muiFocus_Move(contexts[0], root, player, backward),
                   muiFocus_Move(contexts[1], root, player, backward));
        break;
    }
    case 13:
    {
        muiNodeId root = PickNode(world, reader);
        float x = Number(reader);
        float y = Number(reader);
        muiHit hits[2] = {{0}, {0}};
        (void)Both(muiHitTest(contexts[0], root, x, y, &hits[0]),
                   muiHitTest(contexts[1], root, x, y, &hits[1]));
        Expect(memcmp(&hits[0], &hits[1], sizeof hits[0]) == 0);
        break;
    }
    case 14:
    {
        // A class: made, given raw values for a variant, or listed on a
        // node.
        uint8_t what = Byte(reader) % 3;
        if (what == 0 && world->styleCount < MAX_STYLES)
        {
            const muiStyleDef def = muiDefaultStyleDef();
            muiStyleId ids[2] = {{0, 0}, {0, 0}};
            if (Both(muiCreateStyle(contexts[0], &def, &ids[0]),
                     muiCreateStyle(contexts[1], &def, &ids[1])) == mui_success)
            {
                Expect(memcmp(&ids[0], &ids[1], sizeof ids[0]) == 0);
                world->styles[world->styleCount++] = ids[0];
            }
        }
        else if (what == 1)
        {
            muiStyleId style = PickStyle(world, reader);
            muiVariant variant = (muiVariant)Byte(reader);
            muiLayoutStyle values = muiDefaultLayoutStyle();
            size_t at = Byte(reader) % sizeof values;
            ((unsigned char*)&values)[at] = Byte(reader);
            muiPropertyMask mask = (muiPropertyMask)Word(reader) << 32 | Word(reader);
            (void)Both(muiStyle_SetLayoutValues(contexts[0], style, variant, &values, mask),
                       muiStyle_SetLayoutValues(contexts[1], style, variant, &values, mask));
        }
        else
        {
            muiNodeId node = PickNode(world, reader);
            muiStyleId classes[3];
            uint32_t count = Byte(reader) % 4;
            for (uint32_t i = 0; i < count; i++)
            {
                classes[i] = PickStyle(world, reader);
            }
            (void)Both(
                muiNode_SetClasses(contexts[0], node, count != 0 ? classes : nullptr, count),
                muiNode_SetClasses(contexts[1], node, count != 0 ? classes : nullptr, count));
        }
        break;
    }
    case 15:
    {
        muiNodeId node = PickNode(world, reader);
        muiState states = (muiState)Byte(reader);
        (void)Both(muiNode_SetStates(contexts[0], node, states),
                   muiNode_SetStates(contexts[1], node, states));
        break;
    }
    default:
    {
        // Visual values written raw, as the layout ones are, so the draw
        // lists compared have something in them; the struct is longer than
        // a byte reaches.
        muiNodeId node = PickNode(world, reader);
        muiVisualStyle style = muiDefaultVisualStyle();
        size_t at = (size_t)(Byte(reader) << 8 | Byte(reader)) % sizeof style;
        size_t length = 1 + Byte(reader) % 8;
        for (size_t i = at; i < at + length && i < sizeof style; i++)
        {
            ((unsigned char*)&style)[i] = Byte(reader);
        }
        muiPropertyMask mask = (muiPropertyMask)Word(reader) << 32 | Word(reader);
        (void)Both(muiNode_SetVisualValues(contexts[0], node, &style, mask),
                   muiNode_SetVisualValues(contexts[1], node, &style, mask));
        break;
    }
    }
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    Reader reader = {data, size};
    muiContextDef def = muiDefaultContextDef();
    // Small limits, so the inputs reach them and a context is quick to
    // make.
    def.limits = (muiLimits){.nodes = 40,
                             .styles = MAX_STYLES,
                             .nodeTypes = 2,
                             .propertySets = 16,
                             .notifications = 16,
                             .transitions = 4,
                             .animations = 4,
                             .tokens = 4,
                             .tokenNames = 4,
                             .themes = 1,
                             .themeOverrides = 4,
                             .drawCommands = 256,
                             .drawClips = 16,
                             .drawGradients = 4,
                             .drawGlyphs = 16,
                             .layers = 4,
                             .pointers = 4,
                             .pointerRecords = 16,
                             .neighbors = 16,
                             .drawTransforms = 8,
                             .ranges = 4,
                             .popups = 4,
                             .exits = 4,
                             .virtualLists = 2,
                             .virtualItems = 64,
                             .accessNodes = 40,
                             .accessRoots = 2};
    World world = {0};
    for (int i = 0; i < 2; i++)
    {
        Expect(muiCreateContext(&def, &world.contexts[i]) == mui_success);
    }
    for (uint32_t call = 0; call < MAX_CALLS && reader.left != 0; call++)
    {
        Call(&world, &reader);
    }
    Expect(muiGetContextMisuse(world.contexts[0]) == muiGetContextMisuse(world.contexts[1]));
    for (int i = 0; i < 2; i++)
    {
        muiDestroyContext(world.contexts[i]);
    }
    return 0;
}
