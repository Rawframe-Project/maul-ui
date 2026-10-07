// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A slider, a progress bar and a scrollbar, composed over Maul UI's
// range values (maul-ui/range.h, record mui-0007) with the roles and
// keys of the WAI-ARIA Authoring Practices (record mui-0005). The library
// keeps each value and moves it by ARIA's slider keys, by presses on the
// track and by drags of the thumb; the host shows it: each frame it
// reads the changes input made (mui_notificationRangeChanged) and sizes
// the fill and places the thumb from the value. The progress bar shows
// the slider's value, set by code; the scrollbar is a range over a
// horizontally scrolling list's offset, both ways: a drag of its thumb
// scrolls the list, and the thumb's place and length come from the list
// (muiNode_GetScrollThumb). Headless, the slider is moved by End and the
// scrollbar by a drag and a press on its track, the pixels and the
// accessibility tree's values checked.

#include "app.h"

#include "maul-ui/access.h"
#include "maul-ui/access_tree.h"
#include "maul-ui/interaction.h"
#include "maul-ui/range.h"
#include "maul-ui/scroll.h"
#include "maul-ui/style.h"

#include <math.h>
#include <string.h>

#define CELLS 8

// The slider and the bars, in logical units.
#define TRACK 200.0f
#define THUMB 16.0f

// Colours, sRGB bytes.
static const uint8_t s_background[3] = {24, 32, 40};
static const uint8_t s_text[3] = {242, 242, 242};
static const uint8_t s_track[3] = {64, 72, 80};
static const uint8_t s_fill[3] = {48, 112, 224};
static const uint8_t s_progress[3] = {48, 176, 96};
static const uint8_t s_thumb[3] = {250, 250, 250};
static const uint8_t s_cells[CELLS][3] = {
    {200, 60, 60},  {60, 200, 60},  {60, 60, 200},  {200, 200, 60},
    {60, 200, 200}, {200, 60, 200}, {200, 120, 60}, {120, 60, 200},
};

typedef struct Ranges
{
    muiNodeId slider;
    muiNodeId sliderFill;
    muiNodeId sliderThumb;
    muiNodeId progress;
    muiNodeId progressFill;
    muiNodeId list;
    muiNodeId scrollbar;
    muiNodeId scrollGap;
    muiNodeId scrollThumb;
} Ranges;

static muiValueRange RangeOf(const SampleApp* app, muiNodeId node)
{
    muiValueRange range = muiDefaultValueRange();
    (void)muiNode_GetValueRange(app->context, node, &range);
    return range;
}

// A node's width, set alone.
static void Width(SampleApp* app, muiNodeId node, float width)
{
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.sizing.width = SampleLength(width);
    SampleSetLayout(app, node, &layout, MUI_PROPERTY_BIT(mui_propertyWidth));
}

// A bar: a row of a fixed size on the track's colour, below a caption.
static muiNodeId Bar(SampleApp* app, const char* caption, float height)
{
    muiNodeId label = SampleLabel(app, app->root, caption, 14.0f, s_text);
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.margin.top = 10.0f;
    SampleSetLayout(app, label, &layout, MUI_PROPERTY_BIT(mui_propertyMarginTop));
    muiNodeId bar = SampleNode(app, app->root, TRACK, height);
    layout = muiDefaultLayoutStyle();
    layout.container.direction = mui_flexRow;
    layout.margin.top = 4.0f;
    SampleSetLayout(app, bar, &layout,
                    MUI_PROPERTY_BIT(mui_propertyFlexDirection) |
                        MUI_PROPERTY_BIT(mui_propertyMarginTop));
    SampleFill(app, bar, s_track);
    SampleRound(app, bar, height / 2.0f);
    return bar;
}

// Makes a node a focusable widget of a role that takes drags.
static void Draggable(SampleApp* app, muiNodeId node, muiRole role, const char* name)
{
    muiInteractionStyle interaction = muiDefaultInteractionStyle();
    interaction.focusMode = mui_focusAll;
    interaction.drags = true;
    SampleAppCheck(
        app,
        muiNode_SetAccessRole(app->context, node, role) == mui_success &&
            muiNode_SetAccessText(app->context, node, mui_accessLabel, name, strlen(name)) ==
                mui_success &&
            muiNode_SetInteractionValues(app->context, node, &interaction,
                                         MUI_PROPERTY_BIT(mui_propertyFocusMode) |
                                             MUI_PROPERTY_BIT(mui_propertyDrags)) == mui_success,
        "a widget");
}

// The slider: a fill and a thumb in a row on the track, the fill's width
// the thumb's place, 0 to 100 by 1, at 50.
static void MakeSlider(Ranges* ranges, SampleApp* app)
{
    ranges->slider = Bar(app, "Volume", THUMB);
    ranges->sliderFill = SampleNode(app, ranges->slider, 1.0f, THUMB);
    SampleFill(app, ranges->sliderFill, s_fill);
    SampleRound(app, ranges->sliderFill, THUMB / 2.0f);
    ranges->sliderThumb = SampleNode(app, ranges->slider, THUMB, THUMB);
    SampleFill(app, ranges->sliderThumb, s_thumb);
    SampleRound(app, ranges->sliderThumb, THUMB / 2.0f);
    Draggable(app, ranges->slider, mui_roleSlider, "Volume");
    muiValueRange range = muiDefaultValueRange();
    range.value = 50.0f;
    range.thumb = ranges->sliderThumb;
    SampleAppCheck(app, muiNode_SetValueRange(app->context, ranges->slider, &range) == mui_success,
                   "the slider's range");
}

// The progress bar: a fill on the track, a range with no input.
static void MakeProgress(Ranges* ranges, SampleApp* app)
{
    ranges->progress = Bar(app, "Loading", 8.0f);
    ranges->progressFill = SampleNode(app, ranges->progress, 1.0f, 8.0f);
    SampleFill(app, ranges->progressFill, s_progress);
    SampleRound(app, ranges->progressFill, 4.0f);
    muiValueRange range = muiDefaultValueRange();
    range.value = 50.0f;
    SampleAppCheck(app,
                   muiNode_SetAccessRole(app->context, ranges->progress,
                                         mui_roleProgressIndicator) == mui_success &&
                       muiNode_SetAccessText(app->context, ranges->progress, mui_accessLabel,
                                             "Loading", 7) == mui_success &&
                       muiNode_SetValueRange(app->context, ranges->progress, &range) == mui_success,
                   "the progress bar");
}

// The list: a row of cells 50 wide scrolling horizontally in 200; the
// scrollbar below it a range over its offset, a gap then the thumb.
static void MakeScroller(Ranges* ranges, SampleApp* app)
{
    ranges->list = SampleNode(app, app->root, TRACK, 40.0f);
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.container.direction = mui_flexRow;
    layout.scrollAxes = mui_scrollHorizontal;
    layout.margin.top = 16.0f;
    SampleSetLayout(app, ranges->list, &layout,
                    MUI_PROPERTY_BIT(mui_propertyFlexDirection) |
                        MUI_PROPERTY_BIT(mui_propertyScrollAxes) |
                        MUI_PROPERTY_BIT(mui_propertyMarginTop));
    for (int i = 0; i < CELLS; i++)
    {
        muiNodeId cell = SampleNode(app, ranges->list, 50.0f, 40.0f);
        SampleFill(app, cell, s_cells[i]);
    }
    ranges->scrollbar = Bar(app, "Scroll", 10.0f);
    ranges->scrollGap = SampleNode(app, ranges->scrollbar, 1.0f, 10.0f);
    ranges->scrollThumb = SampleNode(app, ranges->scrollbar, 1.0f, 10.0f);
    SampleFill(app, ranges->scrollThumb, s_thumb);
    SampleRound(app, ranges->scrollThumb, 5.0f);
    Draggable(app, ranges->scrollbar, mui_roleScrollBar, "Scroll");
    muiValueRange range = muiDefaultValueRange();
    range.maximum = 0.0f;
    range.step = 10.0f;
    range.page = TRACK;
    range.thumb = ranges->scrollThumb;
    SampleAppCheck(app,
                   muiNode_SetValueRange(app->context, ranges->scrollbar, &range) == mui_success,
                   "the scrollbar's range");
}

static void Build(void* user, SampleApp* app)
{
    Ranges* ranges = user;
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.container.direction = mui_flexColumn;
    layout.padding = (muiEdges){16.0f, 16.0f, 6.0f, 16.0f};
    SampleSetLayout(app, app->root, &layout,
                    MUI_PROPERTY_BIT(mui_propertyFlexDirection) |
                        MUI_PROPERTY_BIT(mui_propertyPaddingStart) |
                        MUI_PROPERTY_BIT(mui_propertyPaddingTop));
    SampleFill(app, app->root, s_background);
    MakeSlider(ranges, app);
    MakeProgress(ranges, app);
    MakeScroller(ranges, app);
}

// What the host does each frame: the slider's changes go to the progress
// bar, the scrollbar's to the list's offset; then the values are shown,
// and the scrollbar's range follows the list as it was laid out.
static void Update(void* user, SampleApp* app)
{
    Ranges* ranges = user;
    muiNotification notification;
    while (muiNextNotification(app->context, &notification) == mui_success)
    {
        if (notification.kind != mui_notificationRangeChanged)
        {
            continue;
        }
        if (SampleSame(notification.nodeId, ranges->slider))
        {
            SampleAppCheck(app,
                           muiNode_SetRangeValue(app->context, ranges->progress,
                                                 RangeOf(app, ranges->slider).value) == mui_success,
                           "the progress set");
        }
        else if (SampleSame(notification.nodeId, ranges->scrollbar))
        {
            SampleAppCheck(app,
                           muiNode_SetScroll(app->context, ranges->list,
                                             RangeOf(app, ranges->scrollbar).value,
                                             0.0f) == mui_success,
                           "the list scrolled by its bar");
        }
    }
    muiValueRange slider = RangeOf(app, ranges->slider);
    Width(app, ranges->sliderFill, fmaxf((TRACK - THUMB) * slider.value / slider.maximum, 0.0f));
    muiValueRange progress = RangeOf(app, ranges->progress);
    Width(app, ranges->progressFill, TRACK * progress.value / progress.maximum);
    // The scrollbar: the list's limit and offset, and the thumb's place.
    muiSize extent = {0.0f, 0.0f};
    float scrollX = 0.0f;
    float scrollY = 0.0f;
    muiScrollThumb thumb = {0.0f, TRACK};
    SampleAppCheck(
        app,
        muiNode_GetScrollExtent(app->context, ranges->list, &extent) == mui_success &&
            muiNode_GetScroll(app->context, ranges->list, &scrollX, &scrollY) == mui_success &&
            muiNode_GetScrollThumb(app->context, ranges->list, true, TRACK, 20.0f, &thumb) ==
                mui_success,
        "the list's scrolling");
    // Set only when the list changed, so a drag going on is left alone.
    muiValueRange bar = RangeOf(app, ranges->scrollbar);
    float limit = fmaxf(extent.width - TRACK, 0.0f);
    if (bar.maximum != limit || bar.value != scrollX)
    {
        bar.maximum = limit;
        bar.value = scrollX;
        SampleAppCheck(app,
                       muiNode_SetValueRange(app->context, ranges->scrollbar, &bar) == mui_success,
                       "the scrollbar follows the list");
    }
    Width(app, ranges->scrollGap, thumb.start);
    Width(app, ranges->scrollThumb, thumb.length);
}

static bool Shows(const SampleApp* app, muiNodeId node, float x, float y, const uint8_t rgb[3])
{
    return SampleNear(SamplePixelOf(app, node, x, y), rgb);
}

// A range's value as the accessibility tree reports it.
static float Reported(const SampleApp* app, muiNodeId node)
{
    const muiAccessNode* found =
        muiAccessTree_Find(muiWindowAccess_GetTree(app->access), muiAccessIdOf(node));
    return found != NULL && (found->flags & mui_accessNumeric) != 0 ? found->value : -1.0f;
}

// After the first frames: the slider half way, the progress bar half
// full, the list and its bar at the start.
static void Still(void* user, SampleApp* app)
{
    const Ranges* ranges = user;
    SampleAppCheck(app,
                   Shows(app, ranges->slider, 30.0f, 8.0f, s_fill) &&
                       Shows(app, ranges->slider, 100.0f, 8.0f, s_thumb) &&
                       Shows(app, ranges->slider, 150.0f, 8.0f, s_track),
                   "the slider half way");
    SampleAppCheck(app,
                   Shows(app, ranges->progress, 90.0f, 4.0f, s_progress) &&
                       Shows(app, ranges->progress, 110.0f, 4.0f, s_track),
                   "the progress bar half full");
    SampleAppCheck(app,
                   Shows(app, ranges->list, 25.0f, 20.0f, s_cells[0]) &&
                       Shows(app, ranges->scrollbar, 20.0f, 5.0f, s_thumb) &&
                       Shows(app, ranges->scrollbar, 150.0f, 5.0f, s_track),
                   "the list and its bar at the start");
    SampleAppCheck(app,
                   Reported(app, ranges->slider) == 50.0f &&
                       Reported(app, ranges->progress) == 50.0f &&
                       Reported(app, ranges->scrollbar) == 0.0f,
                   "the values reported");
}

// Drags a node's thumb by a distance, as a person does.
static void Drag(SampleApp* app, muiNodeId thumb, float distance)
{
    float x = 0.0f;
    float y = 0.0f;
    muiRect rect = muiNode_GetRect(app->context, thumb);
    SampleAppCheck(app,
                   muiNode_MapToRoot(app->context, thumb, rect.width / 2.0f, rect.height / 2.0f, &x,
                                     &y) == mui_success,
                   "the thumb's place");
    SamplePostAt(app, mwin_eventCursorMoved, x, y, 0);
    SamplePostAt(app, mwin_eventButtonDown, x, y, 1);
    SamplePostAt(app, mwin_eventCursorMoved, x + distance / 2.0f, y, 1);
    SamplePostAt(app, mwin_eventCursorMoved, x + distance, y, 1);
    SamplePostAt(app, mwin_eventButtonUp, x + distance, y, 0);
}

static bool Script(void* user, SampleApp* app, int frame)
{
    Ranges* ranges = user;
    switch (frame)
    {
    case 0:
        // The scrollbar learns the list's extent from the first layout.
        return true;
    case 1:
        Still(ranges, app);
        // A click on the thumb focuses the slider without moving it.
        SamplePost(app, mwin_eventCursorMoved, ranges->sliderThumb, 0);
        SamplePost(app, mwin_eventButtonDown, ranges->sliderThumb, 1);
        SamplePost(app, mwin_eventButtonUp, ranges->sliderThumb, 0);
        SamplePostKey(app, mwin_codeEnd, MWIN_KEY_NAMED | mwin_codeEnd, NULL);
        return true;
    case 2:
        SampleAppCheck(app,
                       Shows(app, ranges->slider, 190.0f, 8.0f, s_thumb) &&
                           Shows(app, ranges->progress, 196.0f, 4.0f, s_progress) &&
                           Reported(app, ranges->slider) == 100.0f &&
                           Reported(app, ranges->progress) == 100.0f,
                       "End moved the slider to its maximum, the progress bar with it");
        // The thumb is half the track: dragging it 30 scrolls 60, where
        // a range without a thumb would follow the pointer to 80.
        Drag(app, ranges->scrollThumb, 30.0f);
        return true;
    case 3:
        SampleAppCheck(app,
                       Reported(app, ranges->scrollbar) == 60.0f &&
                           Shows(app, ranges->list, 25.0f, 20.0f, s_cells[1]) &&
                           Shows(app, ranges->scrollbar, 70.0f, 5.0f, s_thumb) &&
                           Shows(app, ranges->scrollbar, 20.0f, 5.0f, s_track),
                       "the thumb's drag scrolled the list by 60");
        // A press on the track before the thumb pages back to the start.
        SamplePostAt(app, mwin_eventCursorMoved, 0.0f, 0.0f, 0);
        {
            float x = 0.0f;
            float y = 0.0f;
            SampleAppCheck(app,
                           muiNode_MapToRoot(app->context, ranges->scrollbar, 10.0f, 5.0f, &x,
                                             &y) == mui_success,
                           "the track's place");
            SamplePostAt(app, mwin_eventButtonDown, x, y, 1);
            SamplePostAt(app, mwin_eventButtonUp, x, y, 0);
        }
        return true;
    default:
        SampleAppCheck(app,
                       Reported(app, ranges->scrollbar) == 0.0f &&
                           Shows(app, ranges->list, 25.0f, 20.0f, s_cells[0]) &&
                           Shows(app, ranges->scrollbar, 20.0f, 5.0f, s_thumb),
                       "a press on the track paged back to the start");
        return false;
    }
}

int main(int count, char** arguments)
{
    static Ranges ranges;
    const SampleAppDef def = {
        .width = 320,
        .height = 240,
        .build = Build,
        .update = Update,
        .script = Script,
        .still = Still,
        .user = &ranges,
    };
    return SampleRunApp(&def, count, arguments);
}
