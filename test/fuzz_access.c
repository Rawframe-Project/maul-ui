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
// Every field is read into a local in the order above before it goes into
// a compound literal or a call: the order C gives the expressions of an
// initializer list or the arguments of a call is unspecified, and a seed
// must decode the same with every compiler.
//
// An update is applied whole or not at all: a refused one leaves the tree
// as its text (muiAccessTree_Write) was, and tells nothing. The change
// callbacks check what Apply promises while it reports: an added node is
// held, a removed one is gone, no node is both updated and removed, and
// the focus moves from the tree's focus before the update to its focus
// after, told once when it moved. After each update every query is asked
// of every id. Nothing is read or written past a buffer, and the
// library's assertions hold.

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
        float top = Float(reader);
        float bottom = Float(reader);
        uint8_t first = Byte(reader);
        uint8_t last = Byte(reader);
        sent->lineBoxes[i] =
            (muiAccessLineBox){top, bottom, first % (MAX_MARKS + 2), (last & 1) != 0};
    }
    marks->lineStarts = (flags & 2) != 0 ? nullptr : sent->lineStarts;
    marks->lineBoxes = (flags & 4) != 0 ? sent->lineBoxes : nullptr;
    marks->wordCount = Byte(reader) % (MAX_MARKS + 1);
    for (uint32_t i = 0; i < marks->wordCount; i++)
    {
        uint8_t start = Byte(reader);
        uint8_t end = Byte(reader);
        sent->words[i] = (muiAccessWord){start, end};
    }
    marks->words = (flags & 8) != 0 ? nullptr : sent->words;
    marks->clusterCount = Byte(reader) % (MAX_MARKS + 1);
    for (uint32_t i = 0; i < marks->clusterCount; i++)
    {
        uint8_t start = Byte(reader);
        uint8_t end = Byte(reader);
        float left = Float(reader);
        float right = Float(reader);
        sent->clusters[i] = (muiAccessCluster){start, end, left, right};
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
    float x = Float(reader);
    float y = Float(reader);
    float width = Float(reader);
    float height = Float(reader);
    node->bounds = (muiRect){x, y, width, height};
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
        uint8_t target = Byte(reader);
        uint8_t relation = Byte(reader);
        sent->links[i] = (muiAccessLink){target % IDS, (muiAccessRelation)relation};
    }
    node->links = (links & 0x80) != 0 ? nullptr : sent->links;
    PickText(reader, node, mui_accessLabel);
    PickText(reader, node, mui_accessValue);
    ReadMarks(reader, sent);
}

// What one apply told: the focus before it, the ids updated and removed,
// and every call.
typedef struct Told
{
    uint64_t focus;
    bool updated[IDS];
    bool removed[IDS];
    uint32_t focusMoves;
    uint32_t calls;
} Told;

// The change callbacks check what Apply promises while it reports, and
// ask the tree about what changed. Every id the tree holds came from the
// input, so it is below IDS.
static void Added(void* user, const muiAccessTree* tree, uint64_t id)
{
    Told* told = user;
    told->calls++;
    char name[64];
    size_t length = 0;
    (void)muiAccessTree_GetName(tree, id, name, sizeof name, &length);
    if (id == 0 || id >= IDS || muiAccessTree_Find(tree, id) == nullptr)
    {
        // An added node the tree does not hold.
        __builtin_trap();
    }
}

static void Updated(void* user, const muiAccessTree* tree, const muiAccessNode* old)
{
    Told* told = user;
    told->calls++;
    (void)muiAccessTree_GetParent(tree, old->id);
    if (old->id == 0 || old->id >= IDS || told->removed[old->id] ||
        muiAccessTree_Find(tree, old->id) == nullptr)
    {
        // An updated node the tree does not hold, or one also removed.
        __builtin_trap();
    }
    told->updated[old->id] = true;
}

static void Removed(void* user, const muiAccessTree* tree, const muiAccessNode* old)
{
    Told* told = user;
    told->calls++;
    if (old->id == 0 || old->id >= IDS || told->updated[old->id] ||
        muiAccessTree_Find(tree, old->id) != nullptr)
    {
        // A removed node the tree still holds, or one also updated.
        __builtin_trap();
    }
    told->removed[old->id] = true;
}

static void FocusMoved(void* user, const muiAccessTree* tree, uint64_t old, uint64_t focus)
{
    Told* told = user;
    told->calls++;
    told->focusMoves++;
    (void)muiAccessTree_IsShown(tree, focus);
    if (old != told->focus || focus != muiAccessTree_GetFocus(tree) || old == focus)
    {
        // A move from another focus than the tree had, or to another than
        // it has.
        __builtin_trap();
    }
}

// Every query of every id, with places and ranges from the input.
static void Ask(Reader* reader, const muiAccessTree* tree)
{
    for (uint64_t id = 0; id < IDS; id++)
    {
        uint32_t count = 0;
        uint8_t capacity = Byte(reader);
        uint8_t nameCapacity = Byte(reader);
        size_t start = Byte(reader);
        size_t end = Byte(reader);
        uint8_t rectCapacity = Byte(reader);
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
        (void)muiAccessTree_GetShownChildren(tree, id, shown, capacity % (MAX_CHILDREN + 1),
                                             &count);
        char name[64];
        size_t length = 0;
        (void)muiAccessTree_GetName(tree, id, name, nameCapacity % sizeof name, &length);
        muiRect bounds;
        (void)muiAccessTree_GetBounds(tree, id, &bounds);
        muiRect rects[MAX_MARKS];
        (void)muiAccessTree_GetTextRects(tree, id, start, end, rects,
                                         rectCapacity % (MAX_MARKS + 1), &count);
        size_t offset = 0;
        float x = Float(reader);
        float y = Float(reader);
        (void)muiAccessTree_GetTextOffsetAt(tree, id, x, y, &offset);
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
    static Told s_told;
    const muiAccessChanges changes = {&s_told, Added, Updated, Removed, FocusMoved, nullptr};
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
        s_told = (Told){.focus = muiAccessTree_GetFocus(tree)};
        bool telling = (u & 1) != 0;
        muiResult applied = muiAccessTree_Apply(tree, &update, telling ? &changes : nullptr);
        if (applied != mui_success && applied != mui_errorInvalid && applied != mui_errorCapacity &&
            applied != mui_errorLimit)
        {
            __builtin_trap();
        }
        bool moved = applied == mui_success && muiAccessTree_GetFocus(tree) != s_told.focus;
        if ((applied != mui_success && s_told.calls != 0) ||
            s_told.focusMoves != (telling && moved ? 1u : 0u))
        {
            // A refused update told something, or a focus that moved was
            // not told once.
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
