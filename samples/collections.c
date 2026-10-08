// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A list, a tree and a table of 100,000 rows, composed over Maul UI's
// virtual lists (maul-ui/virtual.h) with the listbox, tree and grid
// patterns of the WAI-ARIA Authoring Practices (record mui-0005). Only
// the rows near each viewport exist: after each layout the host reads
// the window of items the library wants, binds pooled row nodes to the
// items in it and lets go of the rest. The focus stays on each
// collection while its active row, named as the active descendant,
// moves by arrows, Page Up and Down, Home and End, the host scrolling it
// into view from the item's offset, which the library keeps even for
// rows that do not exist. The tree is a list whose folders insert and
// remove their files' items as they open and close (Right and Left, or
// Enter). Headless, each is used as a person would and the rows, the
// nodes that exist, the pixels and the accessibility tree checked.

#include "app.h"

#include "maul-ui/access.h"
#include "maul-ui/access_tree.h"
#include "maul-ui/event.h"
#include "maul-ui/focus.h"
#include "maul-ui/interaction.h"
#include "maul-ui/pointer.h"
#include "maul-ui/scroll.h"
#include "maul-ui/style.h"
#include "maul-ui/virtual.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define ROWS    100000u
#define FOLDERS 1000u
#define FILES   100u
#define POOL    64
#define CELLS   3
#define ROW     22.0f

static const char* const s_kinds[CELLS] = {"Text", "Image", "Audio"};
static const float s_columns[CELLS] = {200.0f, 120.0f, 112.0f};

// Colours, sRGB bytes.
static const uint8_t s_background[3] = {24, 32, 40};
static const uint8_t s_text[3] = {242, 242, 242};
static const uint8_t s_field[3] = {40, 48, 56};
static const uint8_t s_header[3] = {56, 64, 76};
static const uint8_t s_active[3] = {48, 112, 224};

typedef enum Kind
{
    kind_list,
    kind_tree,
    kind_table,
} Kind;

// A collection: its virtual list, its rows' pool, its active row.
typedef struct Collection
{
    Kind kind;
    muiNodeId list;
    uint32_t count;
    uint32_t active;
    // The pooled rows, their cells' blocks and the items they show.
    muiNodeId rows[POOL];
    muiTextBlockId blocks[POOL][CELLS];
    uint32_t bound[POOL];
    int rowCount;
    // Whether bound rows must be filled again: the data or the active
    // row changed.
    bool dirty;
} Collection;

typedef struct Collections
{
    Collection list;
    Collection tree;
    Collection table;
    bool expanded[FOLDERS];
    muiStyleId rowStyle;
    SampleApp* app;
} Collections;

// The folder and file a tree row shows; file is FILES for a folder.
static void TreeItemAt(const Collections* collections, uint32_t index, uint32_t* folderOut,
                       uint32_t* fileOut)
{
    for (uint32_t folder = 0; folder < FOLDERS; folder++)
    {
        if (index == 0)
        {
            *folderOut = folder;
            *fileOut = FILES;
            return;
        }
        index--;
        if (collections->expanded[folder])
        {
            if (index < FILES)
            {
                *folderOut = folder;
                *fileOut = index;
                return;
            }
            index -= FILES;
        }
    }
    *folderOut = FOLDERS;
    *fileOut = FILES;
}

// A tree folder's row index.
static uint32_t FolderRow(const Collections* collections, uint32_t folder)
{
    uint32_t index = 0;
    for (uint32_t i = 0; i < folder; i++)
    {
        index += 1 + (collections->expanded[i] ? FILES : 0);
    }
    return index;
}

static void SetText(SampleApp* app, muiNodeId node, muiTextBlockId block, const char* text)
{
    SampleAppCheck(app,
                   muiTextBlock_SetText(app->text, block, text, strlen(text)) == mui_success &&
                       muiNode_MarkContentChanged(app->context, node) == mui_success,
                   "a row's text");
}

// A cell: a text node of a width, or filling its row for none, padded,
// its block handed back.
static muiNodeId Cell(SampleApp* app, muiNodeId parent, float width, muiTextBlockId* blockOut)
{
    muiNodeId cell = SampleTextNode(app, parent, "", 14.0f, s_text, blockOut);
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    muiPropertyMask mask =
        MUI_PROPERTY_BIT(mui_propertyPaddingStart) | MUI_PROPERTY_BIT(mui_propertyPaddingEnd) |
        MUI_PROPERTY_BIT(mui_propertyPaddingTop) | MUI_PROPERTY_BIT(mui_propertyPaddingBottom);
    if (width > 0.0f)
    {
        layout.sizing.width = SampleLength(width);
        mask |= MUI_PROPERTY_BIT(mui_propertyWidth);
    }
    else
    {
        layout.item.grow = 1.0f;
        mask |= MUI_PROPERTY_BIT(mui_propertyGrow);
    }
    layout.padding = (muiEdges){8.0f, 8.0f, 3.0f, 3.0f};
    SampleSetLayout(app, cell, &layout, mask);
    return cell;
}

// A new row of a collection, in its pool's next slot.
static void MakeRow(Collections* collections, Collection* collection, int slot)
{
    SampleApp* app = collections->app;
    muiNodeId row = SampleNode(app, collection->list, 0.0f, ROW);
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.container.direction = mui_flexRow;
    SampleSetLayout(app, row, &layout, MUI_PROPERTY_BIT(mui_propertyFlexDirection));
    int cells = collection->kind == kind_table ? CELLS : 1;
    for (int i = 0; i < cells; i++)
    {
        muiNodeId cell = Cell(app, row, collection->kind == kind_table ? s_columns[i] : 0.0f,
                              &collection->blocks[slot][i]);
        if (collection->kind == kind_table)
        {
            SampleAppCheck(
                app, muiNode_SetAccessRole(app->context, cell, mui_roleGridCell) == mui_success,
                "a cell");
        }
    }
    static const muiRole roles[] = {mui_roleListBoxOption, mui_roleTreeItem, mui_roleRow};
    SampleAppCheck(
        app,
        muiNode_SetClasses(app->context, row, &collections->rowStyle, 1) == mui_success &&
            muiNode_SetAccessRole(app->context, row, roles[collection->kind]) == mui_success,
        "a row");
    collection->rows[slot] = row;
}

// Fills a tree row: the folder or the file, indented, and its level,
// place and state.
static void FillTree(Collections* collections, muiNodeId row, muiTextBlockId block, uint32_t index)
{
    SampleApp* app = collections->app;
    uint32_t folder = 0;
    uint32_t file = 0;
    TreeItemAt(collections, index, &folder, &file);
    char text[48];
    bool isFolder = file == FILES;
    if (isFolder)
    {
        snprintf(text, sizeof text, "%s Folder %u", collections->expanded[folder] ? "-" : "+",
                 folder);
    }
    else
    {
        snprintf(text, sizeof text, "File %u.%u", folder, file);
    }
    muiNodeId cell = muiNode_GetFirstChild(app->context, row);
    SetText(app, cell, block, text);
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.padding.start = isFolder ? 8.0f : 28.0f;
    SampleSetLayout(app, cell, &layout, MUI_PROPERTY_BIT(mui_propertyPaddingStart));
    muiAccessValues values = muiDefaultAccessValues();
    values.level = isFolder ? 1 : 2;
    values.setPosition = (isFolder ? folder : file) + 1;
    values.setSize = isFolder ? FOLDERS : FILES;
    muiAccessFlags flags = isFolder ? mui_accessExpandable : 0;
    flags |= isFolder && collections->expanded[folder] ? mui_accessExpanded : 0;
    SampleAppCheck(app,
                   muiNode_SetAccessValues(app->context, row, &values) == mui_success &&
                       muiNode_SetAccessFlags(app->context, row, flags) == mui_success,
                   "a tree item");
}

// Fills a row with what its item shows, and marks it if active.
static void Fill(Collections* collections, Collection* collection, int slot, uint32_t index)
{
    SampleApp* app = collections->app;
    muiNodeId row = collection->rows[slot];
    muiNodeId cell = muiNode_GetFirstChild(app->context, row);
    char text[48];
    switch (collection->kind)
    {
    case kind_list:
        snprintf(text, sizeof text, "Item %u", index);
        SetText(app, cell, collection->blocks[slot][0], text);
        break;
    case kind_tree:
        FillTree(collections, row, collection->blocks[slot][0], index);
        break;
    case kind_table:
    {
        snprintf(text, sizeof text, "Row %u", index);
        SetText(app, cell, collection->blocks[slot][0], text);
        cell = muiNode_GetNextSibling(app->context, cell);
        snprintf(text, sizeof text, "%u KB", index * 37u % 1000u + 1u);
        SetText(app, cell, collection->blocks[slot][1], text);
        cell = muiNode_GetNextSibling(app->context, cell);
        SetText(app, cell, collection->blocks[slot][2], s_kinds[index % CELLS]);
        muiAccessValues values = muiDefaultAccessValues();
        values.rowIndex = index + 2;
        SampleAppCheck(app, muiNode_SetAccessValues(app->context, row, &values) == mui_success,
                       "a row's place");
        break;
    }
    }
    SampleAppCheck(
        app,
        muiNode_SetStates(app->context, row, index == collection->active ? mui_stateSelected : 0) ==
            mui_success,
        "a row's state");
}

// The row node bound to an item, or the null id when it does not exist.
static muiNodeId RowOf(const Collection* collection, uint32_t index)
{
    for (int slot = 0; slot < collection->rowCount; slot++)
    {
        if (collection->bound[slot] == index)
        {
            return collection->rows[slot];
        }
    }
    return (muiNodeId){0, 0};
}

// Binds the pool to the window the library wants: rows whose items left
// it are rebound to items that entered, made when the pool is short,
// and let go when left over. Whether anything changed.
static bool Realize(Collections* collections, Collection* collection)
{
    SampleApp* app = collections->app;
    uint32_t first = 0;
    uint32_t end = 0;
    if (muiNode_GetVirtualWindow(app->context, collection->list, &first, &end) != mui_success)
    {
        return false;
    }
    bool changed = collection->dirty;
    // Bound rows follow their items through insertions and removals.
    bool keep[POOL] = {false};
    for (int slot = 0; slot < collection->rowCount; slot++)
    {
        uint32_t index = 0;
        bool bound = muiNode_GetItem(app->context, collection->rows[slot], &index) == mui_success;
        collection->bound[slot] = bound ? index : UINT32_MAX;
        keep[slot] = bound && index >= first && index < end;
    }
    for (uint32_t index = first; index < end; index++)
    {
        if (RowOf(collection, index).index1 != 0)
        {
            continue;
        }
        int slot = 0;
        while (slot < collection->rowCount && keep[slot])
        {
            slot++;
        }
        if (slot == collection->rowCount)
        {
            if (slot == POOL)
            {
                break;
            }
            MakeRow(collections, collection, slot);
            collection->rowCount++;
        }
        keep[slot] = true;
        collection->bound[slot] = index;
        SampleAppCheck(app,
                       muiNode_SetItem(app->context, collection->rows[slot], index) == mui_success,
                       "a row bound");
        Fill(collections, collection, slot, index);
        changed = true;
    }
    // Rows left over go, the pool kept dense.
    for (int slot = collection->rowCount - 1; slot >= 0; slot--)
    {
        if (keep[slot])
        {
            if (collection->dirty)
            {
                Fill(collections, collection, slot, collection->bound[slot]);
            }
            continue;
        }
        SampleAppCheck(app, muiDestroyNode(app->context, collection->rows[slot]) == mui_success,
                       "a row let go");
        int last = --collection->rowCount;
        collection->rows[slot] = collection->rows[last];
        collection->bound[slot] = collection->bound[last];
        memcpy(collection->blocks[slot], collection->blocks[last], sizeof collection->blocks[0]);
        keep[slot] = keep[last];
        changed = true;
    }
    collection->dirty = false;
    muiNodeId active = RowOf(collection, collection->active);
    SampleAppCheck(app,
                   muiNode_SetAccessRelation(app->context, collection->list,
                                             mui_relationActiveDescendant, &active,
                                             active.index1 != 0 ? 1u : 0u) == mui_success,
                   "the active row named");
    return changed;
}

static bool Settle(void* user, SampleApp* app)
{
    Collections* collections = user;
    muiNotification notification;
    while (muiNextNotification(app->context, &notification) == mui_success)
    {
    }
    bool list = Realize(collections, &collections->list);
    bool tree = Realize(collections, &collections->tree);
    bool table = Realize(collections, &collections->table);
    return list || tree || table;
}

// Scrolls a collection so its active row shows whole.
static void Reveal(SampleApp* app, Collection* collection)
{
    float offset = 0.0f;
    float extent = 0.0f;
    float scrollX = 0.0f;
    float scrollY = 0.0f;
    muiRect content = muiNode_GetContentRect(app->context, collection->list);
    if (muiNode_GetVirtualItem(app->context, collection->list, collection->active, &offset,
                               &extent) != mui_success ||
        muiNode_GetScroll(app->context, collection->list, &scrollX, &scrollY) != mui_success)
    {
        return;
    }
    float view = content.height;
    float target = offset < scrollY                   ? offset
                   : offset + extent > scrollY + view ? offset + extent - view
                                                      : scrollY;
    SampleAppCheck(app,
                   muiNode_SetScroll(app->context, collection->list, 0.0f, target) == mui_success,
                   "the active row revealed");
}

static void Activate(SampleApp* app, Collection* collection, uint32_t index)
{
    collection->active = index < collection->count ? index : collection->count - 1;
    collection->dirty = true;
    Reveal(app, collection);
}

// Opens or closes a folder: its files' items go in after it or out.
static void Toggle(Collections* collections, uint32_t folder)
{
    SampleApp* app = collections->app;
    Collection* tree = &collections->tree;
    uint32_t row = FolderRow(collections, folder);
    bool open = !collections->expanded[folder];
    SampleAppCheck(app,
                   (open ? muiNode_InsertVirtualItems(app->context, tree->list, row + 1, FILES)
                         : muiNode_RemoveVirtualItems(app->context, tree->list, row + 1, FILES)) ==
                       mui_success,
                   "a folder's files");
    collections->expanded[folder] = open;
    tree->count = open ? tree->count + FILES : tree->count - FILES;
    if (!open && tree->active > row && tree->active <= row + FILES)
    {
        tree->active = row;
    }
    else if (!open && tree->active > row + FILES)
    {
        tree->active -= FILES;
    }
    else if (open && tree->active > row)
    {
        tree->active += FILES;
    }
    tree->dirty = true;
}

// Right opens a folder, or moves into it when open; Left closes it, or
// moves from a file to its folder; Enter toggles.
static bool TreeKey(Collections* collections, const muiEvent* event)
{
    Collection* tree = &collections->tree;
    uint32_t folder = 0;
    uint32_t file = 0;
    TreeItemAt(collections, tree->active, &folder, &file);
    bool isFolder = file == FILES;
    switch (event->code)
    {
    case mui_codeArrowRight:
        if (isFolder && !collections->expanded[folder])
        {
            Toggle(collections, folder);
        }
        else if (isFolder)
        {
            Activate(collections->app, tree, tree->active + 1);
        }
        return true;
    case mui_codeArrowLeft:
        if (isFolder && collections->expanded[folder])
        {
            Toggle(collections, folder);
        }
        else if (!isFolder)
        {
            Activate(collections->app, tree, FolderRow(collections, folder));
        }
        return true;
    case mui_codeEnter:
        if (isFolder)
        {
            Toggle(collections, folder);
        }
        return true;
    default:
        return false;
    }
}

// The keys every collection takes: the active row by one, a page, or to
// an end.
static bool Key(Collections* collections, Collection* collection, const muiEvent* event)
{
    SampleApp* app = collections->app;
    uint32_t page = (uint32_t)fmaxf(
        floorf(muiNode_GetContentRect(app->context, collection->list).height / ROW) - 1.0f, 1.0f);
    uint32_t active = collection->active;
    switch (event->code)
    {
    case mui_codeArrowDown:
        Activate(app, collection, active + 1);
        return true;
    case mui_codeArrowUp:
        Activate(app, collection, active > 0 ? active - 1 : 0);
        return true;
    case mui_codePageDown:
        Activate(app, collection, active + page);
        return true;
    case mui_codePageUp:
        Activate(app, collection, active > page ? active - page : 0);
        return true;
    case mui_codeHome:
        Activate(app, collection, 0);
        return true;
    case mui_codeEnd:
        Activate(app, collection, collection->count - 1);
        return true;
    default:
        return collection->kind == kind_tree && TreeKey(collections, event);
    }
}

static bool Hear(void* user, muiNodeId nodeId, muiPhase phase, const muiEvent* event)
{
    Collections* collections = user;
    if (phase != mui_phaseBubble)
    {
        return false;
    }
    Collection* all[3] = {&collections->list, &collections->tree, &collections->table};
    for (int c = 0; c < 3; c++)
    {
        Collection* collection = all[c];
        for (int slot = 0; slot < collection->rowCount; slot++)
        {
            if (SampleSame(nodeId, collection->rows[slot]) && event->kind == mui_eventPointer &&
                event->pointer->kind == mui_pointerRecordClick)
            {
                Activate(collections->app, collection, collection->bound[slot]);
                return true;
            }
        }
        if (SampleSame(nodeId, collection->list) && event->kind == mui_eventKeyDown)
        {
            return Key(collections, collection, event);
        }
    }
    return false;
}

// A collection: a framed virtual list of fixed rows that takes focus.
static void MakeCollection(Collections* collections, Collection* collection, Kind kind,
                           muiNodeId parent, float width, float height, uint32_t count,
                           muiRole role, const char* name)
{
    SampleApp* app = collections->app;
    collection->kind = kind;
    collection->count = count;
    collection->list = SampleNode(app, parent, width, height);
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.scrollAxes = mui_scrollVertical;
    SampleSetLayout(app, collection->list, &layout, MUI_PROPERTY_BIT(mui_propertyScrollAxes));
    SampleFill(app, collection->list, s_field);
    muiVirtualList list = muiDefaultVirtualList();
    list.count = count;
    list.extent = ROW;
    list.fixed = true;
    list.overscan = 2.0f * ROW;
    muiInteractionStyle interaction = muiDefaultInteractionStyle();
    interaction.focusMode = mui_focusAll;
    SampleAppCheck(app,
                   muiNode_SetVirtualList(app->context, collection->list, &list) == mui_success &&
                       muiNode_SetInteractionValues(app->context, collection->list, &interaction,
                                                    MUI_PROPERTY_BIT(mui_propertyFocusMode)) ==
                           mui_success &&
                       muiNode_SetAccessRole(app->context, collection->list, role) == mui_success &&
                       muiNode_SetAccessText(app->context, collection->list, mui_accessLabel, name,
                                             strlen(name)) == mui_success,
                   "a collection");
}

// The table: a header row of column names over its rows.
static void MakeTable(Collections* collections, muiNodeId parent)
{
    SampleApp* app = collections->app;
    static const char* const names[CELLS] = {"Name", "Size", "Kind"};
    muiNodeId grid = SampleNode(app, parent, 0.0f, 0.0f);
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.container.direction = mui_flexColumn;
    SampleSetLayout(app, grid, &layout, MUI_PROPERTY_BIT(mui_propertyFlexDirection));
    muiNodeId header = SampleNode(app, grid, 0.0f, 0.0f);
    layout.container.direction = mui_flexRow;
    SampleSetLayout(app, header, &layout, MUI_PROPERTY_BIT(mui_propertyFlexDirection));
    SampleFill(app, header, s_header);
    for (int i = 0; i < CELLS; i++)
    {
        muiTextBlockId block = {0};
        muiNodeId cell = Cell(app, header, s_columns[i], &block);
        SetText(app, cell, block, names[i]);
        SampleAppCheck(
            app, muiNode_SetAccessRole(app->context, cell, mui_roleColumnHeader) == mui_success,
            "a column header");
    }
    MakeCollection(collections, &collections->table, kind_table, grid, 432.0f, 110.0f, ROWS,
                   mui_roleGrid, "Files");
    muiAccessValues values = muiDefaultAccessValues();
    values.rowCount = ROWS + 1;
    values.columnCount = CELLS;
    SampleAppCheck(app,
                   muiNode_SetAccessRole(app->context, header, mui_roleRow) == mui_success &&
                       muiNode_SetAccessValues(app->context, collections->table.list, &values) ==
                           mui_success,
                   "the table");
}

static void Build(void* user, SampleApp* app)
{
    Collections* collections = user;
    collections->app = app;
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.container.direction = mui_flexColumn;
    layout.container.rowGap = 12.0f;
    layout.padding = (muiEdges){16.0f, 16.0f, 16.0f, 16.0f};
    SampleSetLayout(
        app, app->root, &layout,
        MUI_PROPERTY_BIT(mui_propertyFlexDirection) | MUI_PROPERTY_BIT(mui_propertyRowGap) |
            MUI_PROPERTY_BIT(mui_propertyPaddingStart) | MUI_PROPERTY_BIT(mui_propertyPaddingEnd) |
            MUI_PROPERTY_BIT(mui_propertyPaddingTop) | MUI_PROPERTY_BIT(mui_propertyPaddingBottom));
    SampleFill(app, app->root, s_background);
    muiVisualStyle selected = muiDefaultVisualStyle();
    selected.background = SampleColor(s_active);
    const muiStyleDef styleDef = muiDefaultStyleDef();
    SampleAppCheck(app,
                   muiCreateStyle(app->context, &styleDef, &collections->rowStyle) == mui_success &&
                       muiStyle_SetVisualValues(
                           app->context, collections->rowStyle, mui_variantSelected, &selected,
                           MUI_PROPERTY_BIT(mui_propertyBackground)) == mui_success,
                   "the rows' class");
    muiNodeId top = SampleNode(app, app->root, 0.0f, 0.0f);
    layout = muiDefaultLayoutStyle();
    layout.container.direction = mui_flexRow;
    layout.container.columnGap = 12.0f;
    SampleSetLayout(app, top, &layout,
                    MUI_PROPERTY_BIT(mui_propertyFlexDirection) |
                        MUI_PROPERTY_BIT(mui_propertyColumnGap));
    MakeCollection(collections, &collections->list, kind_list, top, 210.0f, 132.0f, ROWS,
                   mui_roleListBox, "Items");
    MakeCollection(collections, &collections->tree, kind_tree, top, 210.0f, 132.0f, FOLDERS,
                   mui_roleTree, "Folders");
    MakeTable(collections, app->root);
    SampleAppCheck(app,
                   muiSetEventFunction(app->context, Hear, collections) == mui_success &&
                       muiSetAccessTextFunction(app->context, muiAccessTextOf, &app->host) ==
                           mui_success,
                   "the event and access text functions");
}

static const muiAccessNode* Found(const SampleApp* app, muiNodeId node)
{
    return muiAccessTree_Find(muiWindowAccess_GetTree(app->access), muiAccessIdOf(node));
}

// Whether a collection's row for an item exists, shows a text, lies whole
// inside the viewport and, when active, is marked and named.
static bool Shows(const SampleApp* app, const Collection* collection, uint32_t index,
                  const char* text)
{
    muiNodeId row = RowOf(collection, index);
    if (row.index1 == 0)
    {
        return false;
    }
    muiNodeId cell = muiNode_GetFirstChild(app->context, row);
    const muiAccessNode* found = Found(app, cell);
    float rowY = 0.0f;
    float listY = 0.0f;
    float x = 0.0f;
    muiRect list = muiNode_GetRect(app->context, collection->list);
    bool inside =
        muiNode_MapToRoot(app->context, row, 0.0f, 0.0f, &x, &rowY) == mui_success &&
        muiNode_MapToRoot(app->context, collection->list, 0.0f, 0.0f, &x, &listY) == mui_success &&
        rowY >= listY && rowY + ROW <= listY + list.height;
    // Host text is reported as the value, the cell a label without a
    // role of its own.
    size_t length = strlen(text);
    bool named = found != NULL && found->textLength[mui_accessValue] == length &&
                 memcmp(found->text[mui_accessValue], text, length) == 0;
    bool active = index == collection->active;
    muiNodeId descendant = {0, 0};
    uint32_t count = 0;
    bool marked = !active || (SampleNear(SamplePixelOf(app, row, 2.0f, ROW / 2.0f), s_active) &&
                              muiNode_GetAccessRelation(app->context, collection->list,
                                                        mui_relationActiveDescendant, &descendant,
                                                        1, &count) == mui_success &&
                              count == 1 && SampleSame(descendant, row));
    return inside && named && marked;
}

// Only the rows near the viewport exist: one for each item of the
// window the library wants, a handful of 100,000.
static bool Virtual(const SampleApp* app, const Collection* collection)
{
    uint32_t first = 0;
    uint32_t end = 0;
    uint32_t children = muiNode_GetChildCount(app->context, collection->list);
    return muiNode_GetVirtualWindow(app->context, collection->list, &first, &end) == mui_success &&
           children == end - first && children <= 16 && (int)children == collection->rowCount;
}

// The first frame: each collection at its start, its first rows made.
static void Still(void* user, SampleApp* app)
{
    const Collections* collections = user;
    const muiAccessNode* row = Found(app, RowOf(&collections->list, 0));
    SampleAppCheck(app,
                   Shows(app, &collections->list, 0, "Item 0") &&
                       Shows(app, &collections->list, 4, "Item 4") &&
                       Virtual(app, &collections->list) && row != NULL &&
                       row->values.setPosition == 1 && row->values.setSize == ROWS,
                   "the list's first rows, alone, placed in 100,000");
    SampleAppCheck(
        app, Shows(app, &collections->tree, 0, "+ Folder 0") && Virtual(app, &collections->tree),
        "the tree's first folders");
    SampleAppCheck(app,
                   Shows(app, &collections->table, 0, "Row 0") && Virtual(app, &collections->table),
                   "the table's first rows");
}

static void PostKey(SampleApp* app, mwinKeyCode code)
{
    SamplePostKey(app, code, MWIN_KEY_NAMED | code, NULL);
}

static void Click(SampleApp* app, muiNodeId node)
{
    SamplePost(app, mwin_eventCursorMoved, node, 0);
    SamplePost(app, mwin_eventButtonDown, node, 1);
    SamplePost(app, mwin_eventButtonUp, node, 0);
}

static bool Script(void* user, SampleApp* app, int frame)
{
    Collections* collections = user;
    Collection* list = &collections->list;
    Collection* tree = &collections->tree;
    Collection* table = &collections->table;
    switch (frame)
    {
    case 0:
        Still(collections, app);
        Click(app, RowOf(list, 2));
        return true;
    case 1:
        SampleAppCheck(app, list->active == 2 && Shows(app, list, 2, "Item 2"),
                       "a click made Item 2 active");
        PostKey(app, mwin_codeEnd);
        return true;
    case 2:
    {
        SampleAppCheck(app, Shows(app, list, ROWS - 1, "Item 99999") && Virtual(app, list),
                       "End reached Item 99999, the rows near it alone made");
        const muiAccessNode* row = Found(app, RowOf(list, ROWS - 1));
        SampleAppCheck(app, row != NULL && row->values.setPosition == ROWS,
                       "the last row placed 100,000th");
        PostKey(app, mwin_codePageUp);
        return true;
    }
    case 3:
        SampleAppCheck(app, list->active == ROWS - 6 && Shows(app, list, ROWS - 6, "Item 99994"),
                       "Page Up moved a page less a row");
        SamplePostWheel(app, table->list, -10.0f);
        return true;
    case 4:
    {
        uint32_t first = 0;
        uint32_t end = 0;
        SampleAppCheck(app,
                       muiNode_GetVirtualWindow(app->context, table->list, &first, &end) ==
                               mui_success &&
                           first > 0 && Virtual(app, table),
                       "the wheel scrolled the table, its rows made anew");
        // Back up, its window shrinks: rows left over go.
        SamplePostWheel(app, table->list, 10.0f);
        Click(app, RowOf(tree, 1));
        PostKey(app, mwin_codeArrowRight);
        return true;
    }
    case 5:
    {
        const muiAccessNode* folder = Found(app, RowOf(tree, 1));
        SampleAppCheck(app,
                       tree->count == FOLDERS + FILES && Shows(app, tree, 1, "- Folder 1") &&
                           Shows(app, tree, 2, "File 1.0") && folder != NULL &&
                           (folder->flags & mui_accessExpanded) != 0,
                       "Right opened Folder 1, its files after it");
        SampleAppCheck(app, Shows(app, table, 0, "Row 0") && Virtual(app, table),
                       "the table back at its start, the rows left over let go");
        PostKey(app, mwin_codeArrowDown);
        PostKey(app, mwin_codeArrowDown);
        PostKey(app, mwin_codeArrowLeft);
        return true;
    }
    case 6:
        SampleAppCheck(app, tree->active == 1 && Shows(app, tree, 1, "- Folder 1"),
                       "Left moved from File 1.1 to its folder");
        PostKey(app, mwin_codeArrowLeft);
        return true;
    default:
    {
        const muiAccessNode* folder = Found(app, RowOf(tree, 1));
        SampleAppCheck(app,
                       tree->count == FOLDERS && Shows(app, tree, 1, "+ Folder 1") &&
                           Shows(app, tree, 2, "+ Folder 2") && Virtual(app, tree) &&
                           folder != NULL && (folder->flags & mui_accessExpanded) == 0,
                       "Left closed Folder 1, its files' rows let go");
        return false;
    }
    }
}

int main(int count, char** arguments)
{
    static Collections collections;
    const SampleAppDef def = {
        .width = 464,
        .height = 330,
        .build = Build,
        .settle = Settle,
        .script = Script,
        .still = Still,
        .user = &collections,
    };
    return SampleRunApp(&def, count, arguments);
}
