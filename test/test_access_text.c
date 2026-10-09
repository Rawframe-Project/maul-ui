// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A value text's marks (record mui-0008, I123): the selection, lines,
// words and clusters the host's text function gives, read for a node the
// update sends, kept where they fit the text; the tree copying them,
// refusing those that do not fit, writing them, and answering where a
// range is and what lies at a point.

#include "access_text.h"
#include "test_harness.h"

#include "maul-ui/access.h"
#include "maul-ui/access_tree.h"
#include "maul-ui/context.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/style.h"

#include <math.h>
#include <string.h>

static const muiNodeId s_nullNode = {0, 0};

// What the reader gives: "héllo wörld" with its selection, two lines and
// two words; how often it was read, and how often for boundaries.
static const char s_text[] = "h\xC3\xA9llo w\xC3\xB6rld";
static uint32_t s_lines[2] = {0, 7};
static muiAccessWord s_words[2] = {{0, 6}, {7, 13}};
static muiAccessTextMarks s_marks = {1, 1, true, s_lines, 2, s_words, 2, NULL, NULL, 0};
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

static bool Takes(const muiAccessNode* node, muiAccessAction action)
{
    return node != NULL && (node->actions & (1u << action)) != 0;
}

// Text being edited takes its selection set and its text replaced; read
// only, its selection alone; disabled or not edited, neither. An empty
// field is a value. The context leaves both to the host.
static void TestActions(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    muiNodeId root = Content(context, s_nullNode);
    CHECK(muiSetAccessTextFunction(context, ReadMarks, NULL) == mui_success &&
              muiAccess_Enable(context, root) == mui_success,
          "reading");
    muiAccessUpdate update = Build(context, root);
    CHECK(Takes(Sent(&update, root), mui_actionSetSelection) &&
              Takes(Sent(&update, root), mui_actionReplaceText),
          "both taken");
    bool handled = true;
    muiAccessRequest request = {.action = mui_actionReplaceText, .target = muiAccessIdOf(root)};
    CHECK(muiPerformAccessAction(context, &request, &handled) == mui_empty && !handled,
          "the host's, not the context's");
    request.action = mui_actionSetSelection;
    CHECK(muiPerformAccessAction(context, &request, &handled) == mui_empty && !handled,
          "a selection too");
    CHECK(muiNode_SetAccessFlags(context, root, mui_accessReadOnly) == mui_success, "read only");
    update = Build(context, root);
    CHECK(Takes(Sent(&update, root), mui_actionSetSelection) &&
              !Takes(Sent(&update, root), mui_actionReplaceText),
          "read only: the selection alone");
    CHECK(muiNode_SetAccessFlags(context, root, 0) == mui_success &&
              muiNode_SetStates(context, root, mui_stateDisabled) == mui_success,
          "disabled");
    update = Build(context, root);
    CHECK(!Takes(Sent(&update, root), mui_actionSetSelection) &&
              !Takes(Sent(&update, root), mui_actionReplaceText),
          "disabled: neither");
    CHECK(muiNode_SetStates(context, root, 0) == mui_success, "enabled");
    s_marks.selected = false;
    update = Build(context, root);
    CHECK(!Takes(Sent(&update, root), mui_actionSetSelection), "not edited: neither");
    muiDestroyContext(context);
    s_marks.selected = true;
}

// An empty text being edited is an empty value with its caret; not
// edited, none.
static const char* s_emptyText = "";
static bool ReadEmpty(void* user, muiNodeId nodeId, uint64_t hostKey, bool boundaries,
                      muiAccessContent* contentOut)
{
    (void)user;
    (void)nodeId;
    (void)hostKey;
    (void)boundaries;
    *contentOut = (muiAccessContent){s_emptyText, 0, {.selected = s_marks.selected}};
    return true;
}

static void TestEmpty(void)
{
    muiContextDef def = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&def, &context) == mui_success, "context");
    muiNodeId root = Content(context, s_nullNode);
    CHECK(muiSetAccessTextFunction(context, ReadEmpty, NULL) == mui_success &&
              muiAccess_Enable(context, root) == mui_success,
          "reading");
    muiAccessUpdate update = Build(context, root);
    const muiAccessNode* node = Sent(&update, root);
    CHECK(node != NULL && node->text[mui_accessValue] != NULL &&
              node->textLength[mui_accessValue] == 0 && node->marks.selected &&
              node->marks.focus == 0 && Takes(node, mui_actionReplaceText),
          "an empty field: an empty value, the caret at 0");
    s_marks.selected = false;
    CHECK(muiNode_MarkAccessChanged(context, root) == mui_success, "marked");
    update = Build(context, root);
    node = Sent(&update, root);
    CHECK(node != NULL && node->text[mui_accessValue] == NULL && !node->marks.selected,
          "not edited: none");
    s_marks.selected = true;
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
    node.marks = (muiAccessTextMarks){3, 1, true, lines, 2, words, 2, NULL, NULL, 0};
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

// Where a text is shown: "héllo wörld" on two lines, 10 high, the second
// from 7, its node at 100, 50 at twice the size: rectangles of a range
// and the character at a point, through the transform; geometry that
// does not fit refused.
static void TestGeometry(void)
{
    static const uint32_t s_starts[2] = {0, 7};
    static const muiAccessLineBox s_boxes[2] = {{0.0f, 10.0f, 0}, {10.0f, 20.0f, 6}};
    static const muiAccessCluster s_clusters[11] = {
        {0, 1, 0.0f, 5.0f},     {1, 3, 5.0f, 10.0f},    {3, 4, 10.0f, 13.0f},  {4, 5, 13.0f, 16.0f},
        {5, 6, 16.0f, 22.0f},   {6, 7, 22.0f, 25.0f},   {7, 8, 0.0f, 8.0f},    {8, 10, 8.0f, 14.0f},
        {10, 11, 14.0f, 18.0f}, {11, 12, 18.0f, 21.0f}, {12, 13, 21.0f, 27.0f}};
    muiAccessTreeDef def = muiDefaultAccessTreeDef();
    def.nodes = 4;
    muiAccessTree* tree = NULL;
    CHECK(muiCreateAccessTree(&def, &tree) == mui_success, "tree");
    muiAccessNode node = {.id = 1,
                          .role = mui_roleTextInput,
                          .bounds = {0.0f, 0.0f, 40.0f, 20.0f},
                          .transform = {2.0f, 0.0f, 0.0f, 2.0f, 100.0f, 50.0f}};
    node.text[mui_accessValue] = s_text;
    node.textLength[mui_accessValue] = sizeof s_text - 1;
    node.marks = (muiAccessTextMarks){.lineStarts = s_starts,
                                      .lineCount = 2,
                                      .lineBoxes = s_boxes,
                                      .clusters = s_clusters,
                                      .clusterCount = 11};
    const muiAccessNode* sent[1] = {&node};
    muiAccessUpdate update = One(sent);
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_success, "applied");
    muiRect rects[2];
    uint32_t count = 0;
    CHECK(muiAccessTree_GetTextRects(tree, 1, 0, 13, rects, 2, &count) == mui_success &&
              count == 2 && rects[0].x == 100.0f && rects[0].y == 50.0f &&
              rects[0].width == 50.0f && rects[0].height == 20.0f && rects[1].y == 70.0f &&
              rects[1].width == 54.0f,
          "a range over two lines: a rectangle each, through the transform");
    CHECK(muiAccessTree_GetTextRects(tree, 1, 1, 3, rects, 2, &count) == mui_success &&
              count == 1 && rects[0].x == 110.0f && rects[0].width == 10.0f &&
              muiAccessTree_GetTextRects(tree, 1, 3, 3, rects, 2, &count) == mui_success &&
              count == 0,
          "a character; an empty range, none");
    CHECK(muiAccessTree_GetTextRects(tree, 1, 0, 13, rects, 1, &count) == mui_errorCapacity &&
              count == 2 &&
              muiAccessTree_GetTextRects(tree, 1, 0, 13, NULL, 0, &count) == mui_errorCapacity &&
              count == 2,
          "counted past the capacity");
    uint32_t offset = 99;
    CHECK(muiAccessTree_GetTextOffsetAt(tree, 1, 124.0f, 60.0f, &offset) == mui_success &&
              offset == 3 &&
              muiAccessTree_GetTextOffsetAt(tree, 1, 1000.0f, 75.0f, &offset) == mui_success &&
              offset == 12 &&
              muiAccessTree_GetTextOffsetAt(tree, 1, 104.0f, 0.0f, &offset) == mui_success &&
              offset == 0,
          "the character at a point, or the nearest");
    // UTF-16 from é: é, l, l, o, the space, w on the second line, then
    // past the end.
    const muiAllocator allocator = {0};
    muiRect units[12];
    CHECK(muiAccessUnitRects(tree, 1, 1, 12, &allocator, units) && units[0].x == 110.0f &&
              units[0].width == 10.0f && units[5].x == 100.0f && units[5].y == 70.0f &&
              units[9].width == 12.0f && units[10].width == -1.0f && units[11].width == -1.0f,
          "a rectangle each UTF-16 unit, none past the text");
    char text[256];
    size_t length = 0;
    CHECK(muiAccessTree_Write(tree, text, sizeof text, &length) == mui_success &&
              strstr(text, " lines=2 clusters=11\n") != NULL,
          "written");
    muiAccessCluster bad[11];
    memcpy(bad, s_clusters, sizeof bad);
    muiAccessLineBox badBoxes[2] = {s_boxes[0], s_boxes[1]};
    muiAccessNode wrong = node;
    wrong.marks.clusters = bad;
    wrong.marks.lineBoxes = badBoxes;
    sent[0] = &wrong;
    bad[1].start = 2;
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_errorInvalid, "inside a character");
    bad[1].start = 1;
    bad[1].left = NAN;
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_errorInvalid, "an edge not finite");
    bad[1].left = 5.0f;
    badBoxes[1].firstCluster = 12;
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_errorInvalid, "past the clusters");
    badBoxes[1].firstCluster = 6;
    wrong.marks.lineBoxes = NULL;
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_errorInvalid, "clusters without boxes");
    // Without geometry: nothing to answer by.
    node.marks = (muiAccessTextMarks){0};
    sent[0] = &node;
    CHECK(muiAccessTree_Apply(tree, &update, NULL) == mui_success &&
              muiAccessTree_GetTextRects(tree, 1, 0, 3, rects, 2, &count) == mui_empty &&
              count == 0 &&
              muiAccessTree_GetTextOffsetAt(tree, 1, 0.0f, 0.0f, &offset) == mui_empty &&
              muiAccessTree_GetTextRects(tree, 9, 0, 3, rects, 2, &count) == mui_empty,
          "no clusters; a node not held");
    CHECK(muiAccessTree_GetTextRects(NULL, 1, 0, 3, rects, 2, &count) == mui_errorInvalid &&
              muiAccessTree_GetTextRects(tree, 1, 0, 3, NULL, 2, &count) == mui_errorInvalid &&
              muiAccessTree_GetTextOffsetAt(tree, 1, NAN, 0.0f, &offset) == mui_errorInvalid &&
              muiAccessTree_GetTextOffsetAt(tree, 1, 0.0f, 0.0f, NULL) == mui_errorInvalid,
          "refused");
    muiDestroyAccessTree(tree);
}

// The boundaries every adapter reads a text by, and its offsets in code
// points and UTF-16: "héllo wörld\r\nnext 𝄞" on lines from 7 and 14,
// its words at 0, 7 and 15.
static void TestBoundaries(void)
{
    static const char s_value[] = "h\xC3\xA9llo w\xC3\xB6rld\r\nnext \xF0\x9D\x84\x9E";
    static const uint32_t s_starts[3] = {0, 7, 15};
    static const muiAccessWord s_three[3] = {{0, 6}, {7, 13}, {15, 19}};
    muiAccessNode node = {.id = 1};
    node.text[mui_accessValue] = s_value;
    node.textLength[mui_accessValue] = sizeof s_value - 1;
    node.marks = (muiAccessTextMarks){
        .lineStarts = s_starts, .lineCount = 3, .words = s_three, .wordCount = 3};
    muiAccessText text = muiAccessValueOf(&node);
    CHECK(text.length == 24 && muiAccessBoundaryAfter(&text, mui_unitCharacter, 1) == 3 &&
              muiAccessBoundaryBefore(&text, mui_unitCharacter, 3) == 1 &&
              muiAccessBoundaryAfter(&text, mui_unitCharacter, 20) == 24 &&
              muiAccessBoundaryBefore(&text, mui_unitCharacter, 24) == 20,
          "characters: code points");
    CHECK(muiAccessBoundaryAfter(&text, mui_unitWord, 0) == 7 &&
              muiAccessBoundaryAfter(&text, mui_unitWord, 15) == 24 &&
              muiAccessBoundaryBefore(&text, mui_unitWord, 7) == 0 &&
              muiAccessBoundaryBefore(&text, mui_unitWord, 9) == 7,
          "words from start to start, the text's ends");
    CHECK(muiAccessBoundaryAfter(&text, mui_unitLine, 3) == 7 &&
              muiAccessBoundaryAfter(&text, mui_unitLine, 14) == 15 &&
              muiAccessBoundaryBefore(&text, mui_unitLine, 15) == 7,
          "lines from their starts");
    CHECK(muiAccessBoundaryAfter(&text, mui_unitParagraph, 0) == 15 &&
              muiAccessBoundaryAfter(&text, mui_unitParagraph, 13) == 15 &&
              muiAccessBoundaryAfter(&text, mui_unitParagraph, 15) == 24 &&
              muiAccessBoundaryBefore(&text, mui_unitParagraph, 20) == 15 &&
              muiAccessBoundaryBefore(&text, mui_unitParagraph, 15) == 0,
          "paragraphs after CR LF, kept whole");
    CHECK(muiAccessBoundaryAfter(&text, mui_unitDocument, 5) == 24 &&
              muiAccessBoundaryBefore(&text, mui_unitDocument, 5) == 0 &&
              muiAccessBoundaryAfter(&text, mui_unitWord, 24) == 24 &&
              muiAccessBoundaryBefore(&text, mui_unitLine, 0) == 0,
          "the document; nothing past the ends");
    CHECK(muiAccessPointsBefore(s_value, 24) == 19 && muiAccessUtf16Before(s_value, 24) == 20 &&
              muiAccessUtf16Before(s_value, 3) == 2 && muiAccessByteOfPoints(&text, 2) == 3 &&
              muiAccessByteOfPoints(&text, 99) == 24 && muiAccessByteOfUtf16(&text, 18) == 20 &&
              muiAccessByteOfUtf16(&text, 19) == 20 && muiAccessByteOfUtf16(&text, 20) == 24,
          "code points and UTF-16 units, a surrogate pair kept whole");
    node.marks = (muiAccessTextMarks){0};
    node.text[mui_accessValue] = NULL;
    text = muiAccessValueOf(&node);
    CHECK(text.length == 0 && muiAccessBoundaryAfter(&text, mui_unitLine, 0) == 0 &&
              muiAccessByteOfUtf16(&text, 3) == 0,
          "no text");
}

int main(void)
{
    TestBoundaries();
    TestGeometry();
    TestRead();
    TestUnfit();
    TestActions();
    TestEmpty();
    TestTree();
    return s_failures == 0 ? 0 : 1;
}
