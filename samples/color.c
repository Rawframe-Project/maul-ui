// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A colour picker, composed over Maul UI's public capabilities (record
// mui-0005): a saturation and brightness area drawn by layered gradients
// (maul-ui/visual.h), the pure hue under a white one across it and a
// black one down it, with a thumb the host places, a press or a drag
// setting both and arrows stepping them; a hue slider and an opacity
// slider that are ranges (maul-ui/range.h) the library moves by ARIA's
// slider keys and drags, the hue's track three gradient segments; a hex
// field whose Enter sets the colour; and a swatch showing the result.
// The host keeps the colour as hue, saturation, brightness and opacity
// and shows it each frame. Headless, each part is used as a person would
// and the pixels, the values and the accessibility tree checked.

#include "app.h"

#include "maul-ui/access.h"
#include "maul-ui/access_tree.h"
#include "maul-ui/event.h"
#include "maul-ui/focus.h"
#include "maul-ui/interaction.h"
#include "maul-ui/pointer.h"
#include "maul-ui/range.h"
#include "maul-ui/style.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The area and the sliders, in logical units.
#define AREA_WIDTH  160.0f
#define AREA_HEIGHT 120.0f
#define TRACK       200.0f
#define THUMB       14.0f

// Colours, sRGB bytes.
static const uint8_t s_background[3] = {24, 32, 40};
static const uint8_t s_text[3] = {242, 242, 242};
static const uint8_t s_field[3] = {48, 56, 64};
static const uint8_t s_white[3] = {255, 255, 255};
static const uint8_t s_black[3] = {0, 0, 0};

// The hue strip's segments: red to yellow to green, green to cyan to
// blue, blue to magenta to red.
static const uint8_t s_hues[7][3] = {{255, 0, 0}, {255, 255, 0}, {0, 255, 0}, {0, 255, 255},
                                     {0, 0, 255}, {255, 0, 255}, {255, 0, 0}};

// A slider: the range node, the gap before its thumb and the thumb.
typedef struct Slider
{
    muiNodeId node;
    muiNodeId gap;
    muiNodeId thumb;
    muiNodeId paint;
} Slider;

typedef struct Picker
{
    // The colour: hue in degrees, the rest from 0 to 1.
    float hue;
    float saturation;
    float brightness;
    float opacity;
    muiNodeId area;
    muiNodeId areaThumb;
    muiNodeId areaDot;
    Slider hueSlider;
    Slider opacitySlider;
    muiNodeId hex;
    muiTextBlockId hexBlock;
    muiNodeId preview;
    SampleApp* app;
} Picker;

// HSV to sRGB, each from 0 to 1.
static void ToRgb(float hue, float saturation, float brightness, float rgb[3])
{
    float h = fmodf(hue, 360.0f) / 60.0f;
    float c = brightness * saturation;
    float x = c * (1.0f - fabsf(fmodf(h, 2.0f) - 1.0f));
    float m = brightness - c;
    int sector = (int)h;
    const float parts[6][3] = {{c, x, 0}, {x, c, 0}, {0, c, x}, {0, x, c}, {x, 0, c}, {c, 0, x}};
    for (int i = 0; i < 3; i++)
    {
        rgb[i] = parts[sector % 6][i] + m;
    }
}

static void ToBytes(const float rgb[3], uint8_t bytes[3])
{
    for (int i = 0; i < 3; i++)
    {
        bytes[i] = (uint8_t)lroundf(fminf(fmaxf(rgb[i], 0.0f), 1.0f) * 255.0f);
    }
}

// sRGB bytes to HSV.
static void FromBytes(const uint8_t bytes[3], float* hueOut, float* saturationOut,
                      float* brightnessOut)
{
    float r = bytes[0] / 255.0f;
    float g = bytes[1] / 255.0f;
    float b = bytes[2] / 255.0f;
    float high = fmaxf(r, fmaxf(g, b));
    float low = fminf(r, fminf(g, b));
    float delta = high - low;
    float hue = 0.0f;
    if (delta > 0.0f)
    {
        hue = high == r   ? 60.0f * fmodf((g - b) / delta, 6.0f)
              : high == g ? 60.0f * ((b - r) / delta + 2.0f)
                          : 60.0f * ((r - g) / delta + 4.0f);
    }
    *hueOut = hue < 0.0f ? hue + 360.0f : hue;
    *saturationOut = high > 0.0f ? delta / high : 0.0f;
    *brightnessOut = high;
}

// The colour as sRGB bytes.
static void Bytes(const Picker* picker, uint8_t bytes[3])
{
    float rgb[3];
    ToRgb(picker->hue, picker->saturation, picker->brightness, rgb);
    ToBytes(rgb, bytes);
}

// Reads #RRGGBB or RRGGBB.
static bool ParseHex(const char* text, size_t length, uint8_t bytes[3])
{
    if (length > 0 && text[0] == '#')
    {
        text++;
        length--;
    }
    if (length != 6)
    {
        return false;
    }
    for (int i = 0; i < 3; i++)
    {
        char pair[3] = {text[i * 2], text[i * 2 + 1], 0};
        char* end = NULL;
        long value = strtol(pair, &end, 16);
        if (end != pair + 2)
        {
            return false;
        }
        bytes[i] = (uint8_t)value;
    }
    return true;
}

static void Fill(SampleApp* app, muiNodeId node, muiColor color)
{
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.background = color;
    SampleAppCheck(app,
                   muiNode_SetVisualValues(app->context, node, &visual,
                                           MUI_PROPERTY_BIT(mui_propertyBackground)) == mui_success,
                   "a fill");
}

// Paints a linear gradient of up to three colours over a node.
static void Gradient(SampleApp* app, muiNodeId node, float angle, const muiColor* colors, int count)
{
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.gradient.kind = mui_gradientLinear;
    visual.gradient.angle = angle;
    visual.gradient.stopCount = (uint8_t)count;
    for (int i = 0; i < count; i++)
    {
        visual.gradient.stops[i] = (muiGradientStop){colors[i], (float)i / (float)(count - 1)};
    }
    SampleAppCheck(app,
                   muiNode_SetVisualValues(app->context, node, &visual,
                                           MUI_PROPERTY_BIT(mui_propertyGradient)) == mui_success,
                   "a gradient");
}

// A node filling its parent, out of the flow.
static muiNodeId Cover(SampleApp* app, muiNodeId parent, float width, float height)
{
    muiNodeId node = SampleNode(app, parent, width, height);
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.placement.position = mui_positionAbsolute;
    layout.placement.inset.start = SampleLength(0.0f);
    layout.placement.inset.top = SampleLength(0.0f);
    SampleSetLayout(app, node, &layout,
                    MUI_PROPERTY_BIT(mui_propertyPosition) |
                        MUI_PROPERTY_BIT(mui_propertyInsetStart) |
                        MUI_PROPERTY_BIT(mui_propertyInsetTop));
    return node;
}

static void Width(SampleApp* app, muiNodeId node, float width)
{
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.sizing.width = SampleLength(width);
    SampleSetLayout(app, node, &layout, MUI_PROPERTY_BIT(mui_propertyWidth));
}

static muiNodeId Caption(SampleApp* app, const char* text)
{
    muiNodeId label = SampleLabel(app, app->root, text, 13.0f, s_text);
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.margin.top = 8.0f;
    SampleSetLayout(app, label, &layout, MUI_PROPERTY_BIT(mui_propertyMarginTop));
    return label;
}

static void Labelled(SampleApp* app, muiNodeId node, muiRole role, const char* name)
{
    muiInteractionStyle interaction = muiDefaultInteractionStyle();
    interaction.focusMode = mui_focusAll;
    interaction.drags = role != mui_roleTextInput;
    SampleAppCheck(
        app,
        muiNode_SetAccessRole(app->context, node, role) == mui_success &&
            muiNode_SetAccessText(app->context, node, mui_accessLabel, name, strlen(name)) ==
                mui_success &&
            muiNode_SetInteractionValues(app->context, node, &interaction,
                                         MUI_PROPERTY_BIT(mui_propertyFocusMode) |
                                             MUI_PROPERTY_BIT(mui_propertyDrags)) == mui_success,
        "a part");
}

// A slider: a track whose paint lies under a gap and a round thumb.
static void MakeSlider(Picker* picker, Slider* slider, const char* name, float maximum, float value)
{
    SampleApp* app = picker->app;
    Caption(app, name);
    slider->node = SampleNode(app, app->root, TRACK, THUMB);
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.container.direction = mui_flexRow;
    SampleSetLayout(app, slider->node, &layout, MUI_PROPERTY_BIT(mui_propertyFlexDirection));
    slider->paint = Cover(app, slider->node, TRACK, THUMB);
    SampleRound(app, slider->paint, THUMB / 2.0f);
    slider->gap = SampleNode(app, slider->node, 1.0f, THUMB);
    slider->thumb = SampleNode(app, slider->node, THUMB, THUMB);
    SampleFill(app, slider->thumb, s_white);
    SampleRound(app, slider->thumb, THUMB / 2.0f);
    Labelled(app, slider->node, mui_roleSlider, name);
    muiValueRange range = muiDefaultValueRange();
    range.maximum = maximum;
    range.value = value;
    range.thumb = slider->thumb;
    SampleAppCheck(app, muiNode_SetValueRange(app->context, slider->node, &range) == mui_success,
                   "a slider's range");
}

static void MakeArea(Picker* picker)
{
    SampleApp* app = picker->app;
    picker->area = SampleNode(app, app->root, AREA_WIDTH, AREA_HEIGHT);
    Labelled(app, picker->area, mui_roleGroup, "Saturation and brightness");
    // White across, then black down, over the pure hue.
    muiNodeId across = Cover(app, picker->area, AREA_WIDTH, AREA_HEIGHT);
    muiColor white = SampleColor(s_white);
    muiColor clear = white;
    clear.a = 0.0f;
    Gradient(app, across, 90.0f, (const muiColor[]){white, clear}, 2);
    muiNodeId down = Cover(app, picker->area, AREA_WIDTH, AREA_HEIGHT);
    muiColor black = SampleColor(s_black);
    muiColor none = black;
    none.a = 0.0f;
    Gradient(app, down, 180.0f, (const muiColor[]){none, black}, 2);
    picker->areaThumb = Cover(app, picker->area, THUMB, THUMB);
    SampleFill(app, picker->areaThumb, s_white);
    SampleRound(app, picker->areaThumb, THUMB / 2.0f);
    picker->areaDot = SampleNode(app, picker->areaThumb, THUMB - 4.0f, THUMB - 4.0f);
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.margin = (muiEdges){2.0f, 0.0f, 2.0f, 0.0f};
    SampleSetLayout(app, picker->areaDot, &layout,
                    MUI_PROPERTY_BIT(mui_propertyMarginStart) |
                        MUI_PROPERTY_BIT(mui_propertyMarginTop));
    SampleRound(app, picker->areaDot, THUMB / 2.0f - 2.0f);
}

// The hue strip: three segments of three stops each.
static void PaintHues(Picker* picker)
{
    SampleApp* app = picker->app;
    muiNodeId paint = picker->hueSlider.paint;
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.container.direction = mui_flexRow;
    SampleSetLayout(app, paint, &layout, MUI_PROPERTY_BIT(mui_propertyFlexDirection));
    for (int i = 0; i < 3; i++)
    {
        muiNodeId segment = SampleNode(app, paint, TRACK / 3.0f, THUMB);
        const muiColor colors[3] = {SampleColor(s_hues[i * 2]), SampleColor(s_hues[i * 2 + 1]),
                                    SampleColor(s_hues[i * 2 + 2])};
        Gradient(app, segment, 90.0f, colors, 3);
    }
}

// Sets the colour from sRGB bytes, the sliders following.
static void SetColor(Picker* picker, const uint8_t bytes[3])
{
    SampleApp* app = picker->app;
    FromBytes(bytes, &picker->hue, &picker->saturation, &picker->brightness);
    SampleAppCheck(app,
                   muiNode_SetRangeValue(app->context, picker->hueSlider.node,
                                         roundf(picker->hue)) == mui_success,
                   "the hue set");
}

// The area takes a press or a drag as both values at the point, and
// arrows as a step of either.
static bool AreaHears(Picker* picker, const muiEvent* event)
{
    if (event->kind == mui_eventPointer)
    {
        const muiPointerRecord* record = event->pointer;
        if (record->kind != mui_pointerRecordPress && record->kind != mui_pointerRecordDragMove &&
            record->kind != mui_pointerRecordDragStart)
        {
            return false;
        }
        picker->saturation = fminf(fmaxf(record->x / AREA_WIDTH, 0.0f), 1.0f);
        picker->brightness = 1.0f - fminf(fmaxf(record->y / AREA_HEIGHT, 0.0f), 1.0f);
        return true;
    }
    if (event->kind != mui_eventKeyDown)
    {
        return false;
    }
    float* value =
        event->code == mui_codeArrowLeft || event->code == mui_codeArrowRight ? &picker->saturation
        : event->code == mui_codeArrowUp || event->code == mui_codeArrowDown  ? &picker->brightness
                                                                              : NULL;
    if (value == NULL)
    {
        return false;
    }
    float step =
        event->code == mui_codeArrowRight || event->code == mui_codeArrowUp ? 0.01f : -0.01f;
    *value = fminf(fmaxf(*value + step, 0.0f), 1.0f);
    return true;
}

// The hex field: typing and Backspace at its end, Enter applying it.
static bool HexHears(Picker* picker, const muiEvent* event)
{
    SampleApp* app = picker->app;
    const char* text = "";
    size_t length = 0;
    (void)muiTextBlock_GetText(app->text, picker->hexBlock, &text, &length);
    if (event->kind == mui_eventText && event->length > 0 && (unsigned char)event->text[0] >= 0x20)
    {
        SampleAppCheck(app,
                       muiTextBlock_Replace(app->text, picker->hexBlock, (uint32_t)length,
                                            (uint32_t)length, event->text,
                                            event->length) == mui_success &&
                           muiNode_MarkContentChanged(app->context, picker->hex) == mui_success,
                       "typed");
        return true;
    }
    if (event->kind != mui_eventKeyDown)
    {
        return false;
    }
    if (event->code == mui_codeBackspace && length > 0)
    {
        SampleAppCheck(app,
                       muiTextBlock_Replace(app->text, picker->hexBlock, (uint32_t)length - 1,
                                            (uint32_t)length, NULL, 0) == mui_success &&
                           muiNode_MarkContentChanged(app->context, picker->hex) == mui_success,
                       "deleted");
        return true;
    }
    uint8_t bytes[3];
    if (event->code == mui_codeEnter && ParseHex(text, length, bytes))
    {
        SetColor(picker, bytes);
        return true;
    }
    return event->code == mui_codeBackspace || event->code == mui_codeEnter;
}

static bool Hear(void* user, muiNodeId nodeId, muiPhase phase, const muiEvent* event)
{
    Picker* picker = user;
    if (phase != mui_phaseBubble)
    {
        return false;
    }
    if (SampleSame(nodeId, picker->area))
    {
        return AreaHears(picker, event);
    }
    if (SampleSame(nodeId, picker->hex))
    {
        return HexHears(picker, event);
    }
    return false;
}

static float ValueOf(const SampleApp* app, muiNodeId node)
{
    muiValueRange range = muiDefaultValueRange();
    (void)muiNode_GetValueRange(app->context, node, &range);
    return range.value;
}

// Places a slider's thumb from its value.
static void PlaceThumb(SampleApp* app, const Slider* slider)
{
    muiValueRange range = muiDefaultValueRange();
    (void)muiNode_GetValueRange(app->context, slider->node, &range);
    Width(app, slider->gap, fmaxf((TRACK - THUMB) * range.value / range.maximum, 0.0f));
}

// Each frame: the sliders' values into the colour, then the colour shown.
static void Update(void* user, SampleApp* app)
{
    Picker* picker = user;
    muiNotification notification;
    while (muiNextNotification(app->context, &notification) == mui_success)
    {
    }
    picker->hue = ValueOf(app, picker->hueSlider.node);
    picker->opacity = ValueOf(app, picker->opacitySlider.node) / 100.0f;
    float pure[3];
    ToRgb(picker->hue, 1.0f, 1.0f, pure);
    Fill(app, picker->area, (muiColor){pure[0], pure[1], pure[2], 1.0f});
    uint8_t bytes[3];
    Bytes(picker, bytes);
    muiColor color = SampleColor(bytes);
    Fill(app, picker->areaDot, color);
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.placement.inset.start = SampleLength(picker->saturation * AREA_WIDTH - THUMB / 2.0f);
    layout.placement.inset.top =
        SampleLength((1.0f - picker->brightness) * AREA_HEIGHT - THUMB / 2.0f);
    SampleSetLayout(app, picker->areaThumb, &layout,
                    MUI_PROPERTY_BIT(mui_propertyInsetStart) |
                        MUI_PROPERTY_BIT(mui_propertyInsetTop));
    PlaceThumb(app, &picker->hueSlider);
    PlaceThumb(app, &picker->opacitySlider);
    muiColor clear = color;
    clear.a = 0.0f;
    Gradient(app, picker->opacitySlider.paint, 90.0f, (const muiColor[]){clear, color}, 2);
    muiColor shown = color;
    shown.a = picker->opacity;
    Fill(app, picker->preview, shown);
    char value[64];
    snprintf(value, sizeof value, "Saturation %d%%, brightness %d%%",
             (int)lroundf(picker->saturation * 100.0f), (int)lroundf(picker->brightness * 100.0f));
    SampleAppCheck(app,
                   muiNode_SetAccessText(app->context, picker->area, mui_accessValue, value,
                                         strlen(value)) == mui_success,
                   "the area's value");
    // The field shows the colour unless it is being edited.
    if (!SampleSame(muiFocus_Get(app->context, 0), picker->hex))
    {
        char hex[8];
        snprintf(hex, sizeof hex, "#%02X%02X%02X", bytes[0], bytes[1], bytes[2]);
        SampleAppCheck(app,
                       muiTextBlock_SetText(app->text, picker->hexBlock, hex, 7) == mui_success &&
                           muiNode_MarkContentChanged(app->context, picker->hex) == mui_success,
                       "the hex shown");
    }
}

static void Build(void* user, SampleApp* app)
{
    Picker* picker = user;
    picker->app = app;
    picker->saturation = 0.75f;
    picker->brightness = 0.9f;
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.container.direction = mui_flexColumn;
    layout.padding = (muiEdges){16.0f, 16.0f, 8.0f, 16.0f};
    SampleSetLayout(
        app, app->root, &layout,
        MUI_PROPERTY_BIT(mui_propertyFlexDirection) | MUI_PROPERTY_BIT(mui_propertyPaddingStart) |
            MUI_PROPERTY_BIT(mui_propertyPaddingEnd) | MUI_PROPERTY_BIT(mui_propertyPaddingTop) |
            MUI_PROPERTY_BIT(mui_propertyPaddingBottom));
    SampleFill(app, app->root, s_background);
    muiNodeId top = SampleNode(app, app->root, 0.0f, 0.0f);
    layout = muiDefaultLayoutStyle();
    layout.container.direction = mui_flexRow;
    layout.container.columnGap = 16.0f;
    SampleSetLayout(app, top, &layout,
                    MUI_PROPERTY_BIT(mui_propertyFlexDirection) |
                        MUI_PROPERTY_BIT(mui_propertyColumnGap));
    muiNodeId root = app->root;
    app->root = top;
    MakeArea(picker);
    picker->preview = SampleNode(app, top, 64.0f, 64.0f);
    SampleRound(app, picker->preview, 6.0f);
    app->root = root;
    MakeSlider(picker, &picker->hueSlider, "Hue", 360.0f, 210.0f);
    PaintHues(picker);
    MakeSlider(picker, &picker->opacitySlider, "Opacity", 100.0f, 100.0f);
    Caption(app, "Hex");
    picker->hex = SampleTextNode(app, app->root, "", 15.0f, s_text, &picker->hexBlock);
    layout = muiDefaultLayoutStyle();
    layout.sizing.width = SampleLength(100.0f);
    layout.padding = (muiEdges){8.0f, 8.0f, 4.0f, 4.0f};
    SampleSetLayout(
        app, picker->hex, &layout,
        MUI_PROPERTY_BIT(mui_propertyWidth) | MUI_PROPERTY_BIT(mui_propertyPaddingStart) |
            MUI_PROPERTY_BIT(mui_propertyPaddingEnd) | MUI_PROPERTY_BIT(mui_propertyPaddingTop) |
            MUI_PROPERTY_BIT(mui_propertyPaddingBottom));
    SampleFill(app, picker->hex, s_field);
    Labelled(app, picker->hex, mui_roleTextInput, "Hex");
    SampleAppCheck(app,
                   muiSetEventFunction(app->context, Hear, picker) == mui_success &&
                       muiSetAccessTextFunction(app->context, muiAccessTextOf, &app->host) ==
                           mui_success,
                   "the event and access text functions");
}

// Within a tolerance, for gradients: half a pixel in from a stop holds a
// trace of the next colour, which sRGB magnifies near black.
static bool Close(const uint8_t* pixel, const uint8_t rgb[3], int tolerance)
{
    for (int i = 0; i < 3; i++)
    {
        if (abs((int)pixel[i] - (int)rgb[i]) > tolerance)
        {
            return false;
        }
    }
    return true;
}

static bool Holds(const SampleApp* app, muiTextBlockId block, const char* expected)
{
    const char* text = "";
    size_t length = 0;
    (void)muiTextBlock_GetText(app->text, block, &text, &length);
    return length == strlen(expected) && memcmp(text, expected, length) == 0;
}

static const muiAccessNode* Found(const SampleApp* app, muiNodeId node)
{
    return muiAccessTree_Find(muiWindowAccess_GetTree(app->access), muiAccessIdOf(node));
}

// Whether the swatch, the hex field, the area's thumb and its corners
// show the colour the picker holds.
static bool Shows(const Picker* picker, const SampleApp* app)
{
    uint8_t bytes[3];
    Bytes(picker, bytes);
    char hex[8];
    snprintf(hex, sizeof hex, "#%02X%02X%02X", bytes[0], bytes[1], bytes[2]);
    float pure[3];
    uint8_t hue[3];
    ToRgb(picker->hue, 1.0f, 1.0f, pure);
    ToBytes(pure, hue);
    float thumbX = picker->saturation * AREA_WIDTH;
    float thumbY = (1.0f - picker->brightness) * AREA_HEIGHT;
    return SampleNear(SamplePixelOf(app, picker->preview, 32.0f, 32.0f), bytes) &&
           SampleNear(SamplePixelOf(app, picker->area, thumbX, thumbY), bytes) &&
           Holds(app, picker->hexBlock, hex) &&
           Close(SamplePixelOf(app, picker->area, AREA_WIDTH - 0.5f, 0.5f), hue, 16) &&
           Close(SamplePixelOf(app, picker->area, 0.5f, 0.5f), s_white, 16) &&
           Close(SamplePixelOf(app, picker->area, 0.5f, AREA_HEIGHT - 0.5f), s_black, 16);
}

// The first frame: hue 210, saturation 75%, brightness 90%, opaque.
static void Still(void* user, SampleApp* app)
{
    const Picker* picker = user;
    const muiAccessNode* hue = Found(app, picker->hueSlider.node);
    SampleAppCheck(app, Shows(picker, app) && picker->hue == 210.0f,
                   "the colour shown in the swatch, the field and the area");
    SampleAppCheck(
        app,
        hue != NULL && hue->value == 210.0f && (hue->flags & mui_accessNumeric) != 0 &&
            // The first and last segments' middle stops, yellow and
            // magenta, a sixth of the track from each end.
            Close(SamplePixelOf(app, picker->hueSlider.paint, TRACK / 6.0f, 7.0f), s_hues[1], 12) &&
            Close(SamplePixelOf(app, picker->hueSlider.paint, TRACK * 5.0f / 6.0f, 7.0f), s_hues[5],
                  12),
        "the hue slider's value and strip");
}

static void PostKey(SampleApp* app, mwinKeyCode code)
{
    SamplePostKey(app, code, MWIN_KEY_NAMED | code, NULL);
}

static void ClickAt(SampleApp* app, muiNodeId node, float x, float y)
{
    float rootX = 0.0f;
    float rootY = 0.0f;
    SampleAppCheck(app, muiNode_MapToRoot(app->context, node, x, y, &rootX, &rootY) == mui_success,
                   "a point");
    SamplePostAt(app, mwin_eventCursorMoved, rootX, rootY, 0);
    SamplePostAt(app, mwin_eventButtonDown, rootX, rootY, 1);
    SamplePostAt(app, mwin_eventButtonUp, rootX, rootY, 0);
}

static void Type(SampleApp* app, const char* text)
{
    for (const char* at = text; *at != 0; at++)
    {
        char one[2] = {*at, 0};
        SamplePostKey(app, mwin_codeKeyA, (mwinKey)(unsigned char)*at, one);
    }
}

static bool Script(void* user, SampleApp* app, int frame)
{
    Picker* picker = user;
    switch (frame)
    {
    case 0:
        Still(picker, app);
        ClickAt(app, picker->area, 120.0f, 30.0f);
        PostKey(app, mwin_codeArrowUp);
        return true;
    case 1:
        SampleAppCheck(app,
                       picker->saturation == 0.75f && fabsf(picker->brightness - 0.76f) < 1e-5f &&
                           Shows(picker, app),
                       "a press set both values, Up stepped the brightness");
        ClickAt(app, picker->hueSlider.thumb, THUMB / 2.0f, THUMB / 2.0f);
        PostKey(app, mwin_codeHome);
        return true;
    case 2:
        SampleAppCheck(app, picker->hue == 0.0f && Shows(picker, app),
                       "Home moved the hue to red, the area and the swatch with it");
        ClickAt(app, picker->hex, 10.0f, 10.0f);
        for (int i = 0; i < 7; i++)
        {
            PostKey(app, mwin_codeBackspace);
        }
        Type(app, "#00ff00");
        PostKey(app, mwin_codeEnter);
        return true;
    case 3:
    {
        const muiAccessNode* hue = Found(app, picker->hueSlider.node);
        SampleAppCheck(app,
                       picker->hue == 120.0f && picker->saturation == 1.0f &&
                           picker->brightness == 1.0f && hue != NULL && hue->value == 120.0f,
                       "the hex field set pure green, the hue slider following");
        ClickAt(app, picker->opacitySlider.thumb, THUMB / 2.0f, THUMB / 2.0f);
        PostKey(app, mwin_codeHome);
        return true;
    }
    default:
    {
        const muiAccessNode* opacity = Found(app, picker->opacitySlider.node);
        SampleAppCheck(
            app,
            opacity != NULL && opacity->value == 0.0f && Holds(app, picker->hexBlock, "#00FF00") &&
                SampleNear(SamplePixelOf(app, picker->area, AREA_WIDTH - 1.0f, 1.0f),
                           (const uint8_t[]){0, 255, 0}) &&
                SampleNear(SamplePixelOf(app, picker->preview, 32.0f, 32.0f), s_background),
            "Home made the swatch clear, the colour kept");
        return false;
    }
    }
}

int main(int count, char** arguments)
{
    static Picker picker;
    const SampleAppDef def = {
        .width = 300,
        .height = 330,
        .build = Build,
        .update = Update,
        .script = Script,
        .still = Still,
        .user = &picker,
    };
    return SampleRunApp(&def, count, arguments);
}
