// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A value text's marks (record mui-0008, I123): the selection, lines and
// words the host's text function gives, read for a node the update
// sends, kept where they fit the text; the tree copying them, refusing
// those that do not fit, and writing them.

#include "test_harness.h"

#include "maul-ui/access.h"
#include "maul-ui/access_tree.h"
#include "maul-ui/context.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"

#include <string.h>

static const muiNodeId s_nullNode = {0, 0};

// What the reader gives: "héllo wörld" with its selection, two lines and
// two words; how often it was read, and how often for boundaries.
static const char s_text[] = "h\xC3\xA9llo w\xC3\xB6rld";
static uint32_t s_lines[2] = {0, 7};
static muiAccessWord s_words[2] = {{0, 6}, {7, 13}};
static muiAccessTextMarks s_marks = {1, 1, true, s_lines, 2, s_words, 2};
static uint32_t s_reads;
static uint32_t s_boundaryReads;

static bool ReadMarks(void* user, muiNodeId nodeId, uint64_t hostKey, bool boundaries,
                      muiAccessContent* contentOut)
{
    (void)user;
    (void)nodeId;
    (void)hostKey;
    s_reads++;
    s_boundaryReads += boundaries ? 1u : 0u;
    *contentOut = (muiAccessContent){s_text, sizeof s_text - 1, s_marks};
    return true;
}

static muiNodeId Content(muiContext* context, muiNodeId parent)
{
    muiNodeDef def = muiDefaultNodeDef();
    def.hostKey = 1;
    muiNodeId node = s_nullNode;
    CHECK(muiCreateNode(context, &def, &node) == mui_success, "node");
    if (parent.index1 != 0)
    {
        CHECK(muiNode_InsertChild(context, parent, node, s_nullNode) == mui_success, "insert");
    }
    muiLayoutStyle style = muiDefaultLayoutStyle();
    style.content = mui_contentHost;
    CHECK(muiNode_SetLayoutValues(context, node, &style, MUI_PROPERTY_BIT(mui_propertyContent)) ==
              mui_success,
          "content");
    return node;
}

static const muiAccessNode* Sent(const muiAccessUpdate* update, muiNodeId node)
{
    for (uint32_t i = 0; i < update->nodeCount; i++)
    {
        if (update->nodes[i]->id == muiAccessIdOf(node))
        {
            return update->nodes[i];
        }
    }
    return NULL;
}

static muiAccessUpdate Build(muiContext* context, muiNodeId root)
{
    muiAccessUpdate update;
    CHECK(muiBuildAccessUpdate(context, root, &update) == mui_success, "build");
    return update;
}

static void TestRead(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    muiNodeId root = Content(context, s_nullNode);
    CHECK(muiSetAccessTextFunction(context, ReadMarks, NULL) == mui_success &&
              muiAccess_Enable(context, root) == mui_success,
          "reading");
    muiAccessUpdate update = Build(context, root);
    const muiAccessNode* node = Sent(&update, root);
    CHECK(node != NULL && node->marks.selected && node->marks.anchor == 1 &&
              node->marks.focus == 1 && node->marks.lineCount == 2 &&
              node->marks.lineStarts[1] == 7 && node->marks.wordCount == 2 &&
              node->marks.words[1].end == 13 && s_reads == 2 && s_boundaryReads == 1,
          "the selection, lines and words, boundaries read once it is sent");
    // Visited again, the same: read without boundaries, not sent.
    CHECK(muiNode_MarkAccessChanged(context, root) == mui_success, "marked");
    update = Build(context, root);
    CHECK(update.nodeCount == 0 && s_reads == 3 && s_boundaryReads == 1, "the same");
    // The caret moved: sent, with boundaries.
    s_marks.focus = 3;
    CHECK(muiNode_MarkAccessChanged(context, root) == mui_success, "the caret moved");
    update = Build(context, root);
    node = Sent(&update, root);
    CHECK(node != NULL && node->marks.anchor == 1 && node->marks.focus == 3 &&
              node->marks.lineCount == 2 && s_boundaryReads == 2,
          "the caret told");
    // Not editing any more: no selection, sent.
    s_marks.selected = false;
    CHECK(muiNode_MarkAccessChanged(context, root) == mui_success, "unselected");
    update = Build(context, root);
    node = Sent(&update, root);
    CHECK(node != NULL && !node->marks.selected, "no selection");
    CHECK(muiNode_MarkAccessChanged(NULL, root) == mui_errorInvalid &&
              muiNode_MarkAccessChanged(context, s_nullNode) == mui_errorInvalid,
          "refused");
    muiNodeId gone = Content(context, s_nullNode);
    CHECK(muiDestroyNode(context, gone) == mui_success &&
              muiNode_MarkAccessChanged(context, gone) == mui_errorStale,
          "a node gone");
    s_marks.selected = true;
    s_marks.focus = 1;
    muiDestroyContext(context);
}

// Marks that do not fit the text are left out, each part alone.
static void TestUnfit(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    muiNodeId root = Content(context, s_nullNode);
    CHECK(muiSetAccessTextFunction(context, ReadMarks, NULL) == mui_success &&
              muiAccess_Enable(context, root) == mui_success,
          "reading");
    // Inside é, a line not from 0, words overlapping.
    s_marks.focus = 2;
    s_lines[0] = 1;
    s_words[1].start = 5;
    muiAccessUpdate update = Build(context, root);
    const muiAccessNode* node = Sent(&update, root);
    CHECK(node != NULL && !node->marks.selected && node->marks.lineCount == 0 &&
              node->marks.lineStarts == NULL && node->marks.wordCount == 0,
          "all left out");
    // Past the text; lines not ascending; a word empty.
    s_marks.focus = 14;
    s_lines[0] = 0;
    s_lines[1] = 0;
    s_words[1] = (muiAccessWord){7, 7};
    CHECK(muiNode_MarkAccessChanged(context, root) == mui_success, "marked");
    update = Build(context, root);
    CHECK(update.nodeCount == 0, "left out again: nothing to tell");
    // The selection fits again, the rest not: the selection alone.
    s_marks.focus = 13;
    CHECK(muiNode_MarkAccessChanged(context, root) == mui_success, "marked again");
    update = Build(context, root);
    node = Sent(&update, root);
    CHECK(node != NULL && node->marks.selected && node->marks.focus == 13 &&
              node->marks.lineCount == 0 && node->marks.wordCount == 0,
          "the selection kept alone");
    s_marks.focus = 1;
    s_lines[1] = 7;
    s_words[1] = (muiAccessWord){7, 13};
    muiDestroyContext(context);
}

static muiAccessUpdate One(const muiAccessNode* const* node)
{
    return (muiAccessUpdate){node, 1, NULL, 1, 1};
}

// The tree copies marks that fit, refuses an update with marks that do
// not, and writes them.
static void TestTree(void)
{
    muiAccessTreeDef def = muiDefaultAccessTreeDef();
    def.nodes = 4;
    muiAccessTree* tree = NULL;
    CHECK(muiCreateAccessTree(&def, &tree) == mui_success, "tree");
    uint32_t lines[2] = {0, 7};
    muiAccessWord words[2] = {{0, 6}, {7, 13}};
    muiAccessNode node = {.id = 1, .role = mui_roleTextInput};
    node.text[mui_accessValue] = s_text;
    node.textLength[mui_accessValue] = sizeof s_text - 1;
    node.marks = (muiAccessTextMarks){3, 1, true, lines, 2, words, 2};
    const muiAccessNode* sent[1] = {&node};
    muiAccessUpdate update = One(sent);
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_success, "applied");
    lines[1] = 8;
    words[0].end = 5;
    const muiAccessNode* held = muiAccessTree_Find(tree, 1);
    CHECK(held != NULL && held->marks.lineStarts != lines && held->marks.lineStarts[1] == 7 &&
              held->marks.words[0].end == 6 && held->marks.anchor == 3,
          "copied");
    char text[256];
    size_t length = 0;
    CHECK(muiAccessTree_Write(tree, text, sizeof text, &length) == mui_success &&
              strstr(text, " selection=3-1 lines=2 words=2\n") != NULL,
          "written");
    // Each part that does not fit refuses the update, the tree kept.
    muiAccessNode bad = node;
    bad.marks.focus = 2;
    sent[0] = &bad;
    update = One(sent);
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_errorInvalid, "inside a character");
    bad = node;
    bad.marks.lineStarts = NULL;
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_errorInvalid, "no line starts");
    bad = node;
    lines[1] = 14;
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_errorInvalid, "a line past the end");
    lines[1] = 7;
    bad = node;
    words[0].end = 8;
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_errorInvalid, "words overlapping");
    words[0].end = 6;
    bad = node;
    bad.marks.words = NULL;
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_errorInvalid, "no words");
    bad = node;
    bad.text[mui_accessValue] = NULL;
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_errorInvalid, "no text");
    held = muiAccessTree_Find(tree, 1);
    CHECK(held->marks.focus == 1 && held->marks.lineStarts[1] == 7, "kept");
    // Marks gone: freed, written without them.
    node.marks = (muiAccessTextMarks){0};
    sent[0] = &node;
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_success &&
              muiAccessTree_Write(tree, text, sizeof text, &length) == mui_success &&
              strstr(text, "selection") == NULL && strstr(text, "lines") == NULL,
          "gone");
    muiDestroyAccessTree(tree);
}

int main(void)
{
    TestRead();
    TestUnfit();
    TestTree();
    return s_failures == 0 ? 0 : 1;
}
