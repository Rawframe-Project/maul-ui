// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes the access tree with updates a host builds by hand, the input
// muiAccessTree_Apply and every adapter's Apply take. The input is a run
// of updates applied to one tree of TREE_NODES nodes, each read as:
//
// - a byte of flags: bit 0 names the first node sent as the root, bit 1
//   states the children array's length (childCount), bit 2 names a focus;
// - the number of nodes, a byte mod MAX_NODES + 1;
// - the children array, MAX_CHILDREN ids, a byte each;
// - each node: a byte that makes it NULL when it is 255; its id, role,
//   flags, actions and bounds; its first child and child count; its
//   links, with their array left out when a bit says so; its label and
//   value texts, picked from a pool by their true lengths; its marks:
//   the selection, line starts, words, line boxes and clusters, every
//   offset and count from the input;
// - the focus's id, a byte.
//
// Ids are a byte mod 24, so 0, repeats and ids no node holds come too.
// The host's side of the contract is kept: every count stays within the
// array it names, and with no length stated a node's children stay
// within the children array, as the header leaves to the caller. The
// values in those ranges are anything the bytes give.
//
// An update is applied whole or not at all: a refused one leaves the tree
// as its text (muiAccessTree_Write) was. After each update every query
// is asked of every id, and the change callbacks ask the tree too while it
// applies. Nothing is read or written past a buffer, and the library's
// assertions hold.

#include "maul-ui/access.h"
#include "maul-ui/access_tree.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

enum
{
    TREE_NODES = 16,
    MAX_NODES = 12,
    MAX_CHILDREN = 32,
    MAX_LINKS = 4,
    MAX_MARKS = 8,
    MAX_UPDATES = 16,
    IDS = 24,
    WRITTEN = 1 << 16
};

typedef struct Reader
{
    const uint8_t* data;
    size_t size;
} Reader;

// The next count bytes into out, zeros past the input.
static void Read(Reader* reader, void* out, size_t count)
{
    size_t taken = count < reader->size ? count : reader->size;
    memset(out, 0, count);
    if (taken > 0)
    {
        memcpy(out, reader->data, taken);
    }
    reader->data += taken;
    reader->size -= taken;
}

static uint8_t Byte(Reader* reader)
{
    uint8_t value = 0;
    Read(reader, &value, 1);
    return value;
}

static float Float(Reader* reader)
{
    float value = 0.0f;
    Read(reader, &value, sizeof value);
    return value;
}

// Texts a node may carry: empty, ASCII, accented, a combining mark, an
// emoji, a line break and ill-formed UTF-8.
static const char* const s_texts[] = {
    "",
    "abc",
    "h\xC3\xA9llo w\xC3\xB6rld",
    "a\xCC\x81\x62",
    "\xF0\x9F\x98\x80 x",
    "line one\nline two",
    "\xC3(",
    "  two  words ",
};

enum
{
    TEXTS = sizeof s_texts / sizeof s_texts[0]
};

// A node sent and the arrays its marks and links point at.
typedef struct Sent
{
    muiAccessNode node;
    muiAccessLink links[MAX_LINKS];
    size_t lineStarts[MAX_MARKS];
    muiAccessWord words[MAX_MARKS];
    muiAccessLineBox lineBoxes[MAX_MARKS];
    muiAccessCluster clusters[MAX_MARKS];
} Sent;

// One of the pool's texts or none, by a byte, with its true length.
static void PickText(Reader* reader, muiAccessNode* node, muiAccessTextKind kind)
{
    uint8_t pick = Byte(reader);
    if (pick % (TEXTS + 1) == TEXTS)
    {
        return;
    }
    node->text[kind] = s_texts[pick % (TEXTS + 1)];
    node->textLength[kind] = strlen(s_texts[pick % (TEXTS + 1)]);
}

static void ReadMarks(Reader* reader, Sent* sent)
{
    muiAccessTextMarks* marks = &sent->node.marks;
    uint8_t flags = Byte(reader);
    marks->selected = (flags & 1) != 0;
    marks->anchor = Byte(reader);
    marks->focus = Byte(reader);
    marks->lineCount = Byte(reader) % (MAX_MARKS + 1);
    for (uint32_t i = 0; i < marks->lineCount; i++)
    {
        sent->lineStarts[i] = Byte(reader);
        sent->lineBoxes[i] = (muiAccessLineBox){
            Float(reader), Float(reader), Byte(reader) % (MAX_MARKS + 2), (Byte(reader) & 1) != 0};
    }
    marks->lineStarts = (flags & 2) != 0 ? nullptr : sent->lineStarts;
    marks->lineBoxes = (flags & 4) != 0 ? sent->lineBoxes : nullptr;
    marks->wordCount = Byte(reader) % (MAX_MARKS + 1);
    for (uint32_t i = 0; i < marks->wordCount; i++)
    {
        sent->words[i] = (muiAccessWord){Byte(reader), Byte(reader)};
    }
    marks->words = (flags & 8) != 0 ? nullptr : sent->words;
    marks->clusterCount = Byte(reader) % (MAX_MARKS + 1);
    for (uint32_t i = 0; i < marks->clusterCount; i++)
    {
        sent->clusters[i] =
            (muiAccessCluster){Byte(reader), Byte(reader), Float(reader), Float(reader)};
    }
    marks->clusters = (flags & 16) != 0 ? nullptr : sent->clusters;
}

static void ReadNode(Reader* reader, Sent* sent, bool stated)
{
    muiAccessNode* node = &sent->node;
    *node = (muiAccessNode){0};
    node->id = Byte(reader) % IDS;
    node->role = (muiRole)Byte(reader);
    Read(reader, &node->flags, sizeof node->flags);
    Read(reader, &node->actions, sizeof node->actions);
    node->bounds = (muiRect){Float(reader), Float(reader), Float(reader), Float(reader)};
    node->value = Float(reader);
    node->firstChild = Byte(reader);
    node->childCount = Byte(reader) % 5;
    if (!stated)
    {
        // With no length stated, the caller keeps the children in range.
        node->firstChild %= MAX_CHILDREN;
        node->childCount =
            node->childCount <= MAX_CHILDREN - node->firstChild ? node->childCount : 0;
    }
    uint8_t links = Byte(reader);
    node->linkCount = links % (MAX_LINKS + 1);
    for (uint32_t i = 0; i < node->linkCount; i++)
    {
        sent->links[i] = (muiAccessLink){Byte(reader) % IDS, (muiAccessRelation)Byte(reader)};
    }
    node->links = (links & 0x80) != 0 ? nullptr : sent->links;
    PickText(reader, node, mui_accessLabel);
    PickText(reader, node, mui_accessValue);
    ReadMarks(reader, sent);
}

// The change callbacks ask the tree about what changed while it applies.
static void Added(void* user, const muiAccessTree* tree, uint64_t id)
{
    (void)user;
    char name[64];
    size_t length = 0;
    (void)muiAccessTree_GetName(tree, id, name, sizeof name, &length);
    (void)muiAccessTree_Find(tree, id);
}

static void Updated(void* user, const muiAccessTree* tree, const muiAccessNode* old)
{
    (void)user;
    (void)muiAccessTree_Find(tree, old->id);
    (void)muiAccessTree_GetParent(tree, old->id);
}

static void Removed(void* user, const muiAccessTree* tree, const muiAccessNode* old)
{
    (void)user;
    (void)muiAccessTree_Find(tree, old->id);
}

static void FocusMoved(void* user, const muiAccessTree* tree, uint64_t old, uint64_t focus)
{
    (void)user;
    (void)old;
    (void)muiAccessTree_IsShown(tree, focus);
}

// Every query of every id, with places and ranges from the input.
static void Ask(Reader* reader, const muiAccessTree* tree)
{
    for (uint64_t id = 0; id < IDS; id++)
    {
        uint32_t count = 0;
        (void)muiAccessTree_Find(tree, id);
        (void)muiAccessTree_GetParent(tree, id);
        (void)muiAccessTree_GetShownParent(tree, id);
        (void)muiAccessTree_IsShown(tree, id);
        const uint64_t* children = muiAccessTree_GetChildren(tree, id, &count);
        for (uint32_t i = 0; children != nullptr && i < count; i++)
        {
            (void)muiAccessTree_Find(tree, children[i]);
        }
        uint64_t shown[MAX_CHILDREN];
        (void)muiAccessTree_GetShownChildren(tree, id, shown, Byte(reader) % (MAX_CHILDREN + 1),
                                             &count);
        char name[64];
        size_t length = 0;
        (void)muiAccessTree_GetName(tree, id, name, Byte(reader) % sizeof name, &length);
        muiRect bounds;
        (void)muiAccessTree_GetBounds(tree, id, &bounds);
        muiRect rects[MAX_MARKS];
        size_t start = Byte(reader);
        size_t end = Byte(reader);
        (void)muiAccessTree_GetTextRects(tree, id, start, end, rects,
                                         Byte(reader) % (MAX_MARKS + 1), &count);
        size_t offset = 0;
        (void)muiAccessTree_GetTextOffsetAt(tree, id, Float(reader), Float(reader), &offset);
    }
}

static char s_before[WRITTEN];
static char s_after[WRITTEN];

// The tree's text into a buffer; false when it does not fit.
static bool Written(const muiAccessTree* tree, char* buffer, size_t* lengthOut)
{
    return muiAccessTree_Write(tree, buffer, WRITTEN, lengthOut) == mui_success;
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    Reader reader = {data, size};
    muiAccessTreeDef def = muiDefaultAccessTreeDef();
    def.nodes = TREE_NODES;
    muiAccessTree* tree = nullptr;
    if (muiCreateAccessTree(&def, &tree) != mui_success)
    {
        __builtin_trap();
    }
    const muiAccessChanges changes = {nullptr, Added, Updated, Removed, FocusMoved, nullptr};
    static Sent s_sent[MAX_NODES];
    for (int u = 0; u < MAX_UPDATES && reader.size > 0; u++)
    {
        uint8_t flags = Byte(&reader);
        bool stated = (flags & 2) != 0;
        uint32_t count = Byte(&reader) % (MAX_NODES + 1);
        uint64_t children[MAX_CHILDREN];
        for (uint32_t i = 0; i < MAX_CHILDREN; i++)
        {
            children[i] = Byte(&reader) % IDS;
        }
        const muiAccessNode* nodes[MAX_NODES];
        for (uint32_t i = 0; i < count; i++)
        {
            bool none = Byte(&reader) == 255;
            ReadNode(&reader, &s_sent[i], stated);
            nodes[i] = none ? nullptr : &s_sent[i].node;
        }
        uint64_t focus = Byte(&reader) % IDS;
        muiAccessUpdate update = {
            .nodes = count != 0 ? nodes : nullptr,
            .nodeCount = count,
            .children = children,
            .root = (flags & 1) != 0 && count != 0 && nodes[0] != nullptr ? nodes[0]->id : 0,
            .focus = (flags & 4) != 0 ? focus : 0,
            .childCount = stated ? MAX_CHILDREN : 0,
        };
        size_t before = 0;
        bool wrote = Written(tree, s_before, &before);
        muiResult applied = muiAccessTree_Apply(tree, &update, (u & 1) != 0 ? &changes : nullptr);
        if (applied != mui_success && applied != mui_errorInvalid && applied != mui_errorCapacity &&
            applied != mui_errorLimit)
        {
            __builtin_trap();
        }
        size_t after = 0;
        if (applied != mui_success && wrote &&
            (!Written(tree, s_after, &after) || after != before ||
             memcmp(s_before, s_after, before) != 0))
        {
            // A refused update changed the tree.
            __builtin_trap();
        }
        if (muiAccessTree_GetNodeCount(tree) > TREE_NODES)
        {
            __builtin_trap();
        }
        Ask(&reader, tree);
    }
    muiDestroyAccessTree(tree);
    return 0;
}
