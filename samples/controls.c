// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Buttons and the checkable family, composed over Maul UI's public
// capabilities with the roles, states and keys of the WAI-ARIA Authoring
// Practices (record mui-0005): a button with an icon and a label, a
// checkbox, a switch, and a radio group whose arrows move the choice.
// Each widget is a node with a role, a focus mode and classes whose
// variants answer its states; the host's event function activates it on
// a click, Enter or Space (only Space for the checkable ones, as ARIA's
// patterns say) and the navigation's activate, which accessibility
// clients use too, and keeps its checked state. Headless, each is used
// as a person would and the pixels and the accessibility tree checked.

#include "app.h"

#include "maul-rhi/resources.h"
#include "maul-ui/access.h"
#include "maul-ui/access_tree.h"
#include "maul-ui/event.h"
#include "maul-ui/focus.h"
#include "maul-ui/interaction.h"
#include "maul-ui/pointer.h"
#include "maul-ui/style.h"

#include <string.h>

// The icon's key, which the renderer's image function knows.
#define ICON 1u

#define RADIOS 3

// Colours, sRGB bytes.
static const uint8_t s_background[3] = {24, 32, 40};
static const uint8_t s_text[3] = {242, 242, 242};
static const uint8_t s_button[3] = {48, 112, 224};
static const uint8_t s_pressed[3] = {224, 112, 48};
static const uint8_t s_off[3] = {96, 104, 112};
static const uint8_t s_on[3] = {48, 176, 96};
static const uint8_t s_knob[3] = {250, 250, 250};

typedef struct Controls
{
    muiNodeId button;
    muiNodeId icon;
    muiNodeId checkbox;
    muiNodeId box;
    muiNodeId toggle;
    muiNodeId track;
    muiNodeId knob;
    muiNodeId radios[RADIOS];
    muiNodeId dots[RADIOS];
    mrhiTextureId iconTexture;
    int plays;
    bool checked;
    bool on;
    int choice;
    SampleApp* app;
} Controls;

static const muiPropertyMask s_background_bit = MUI_PROPERTY_BIT(mui_propertyBackground);

// A class: a base background and one for a state's variant.
static muiStyleId Class(SampleApp* app, const uint8_t base[3], muiVariant variant,
                        const uint8_t varied[3])
{
    muiStyleId style = {0};
    muiVisualStyle values = muiDefaultVisualStyle();
    values.background = SampleColor(base);
    muiVisualStyle other = muiDefaultVisualStyle();
    other.background = SampleColor(varied);
    SampleAppCheck(app,
                   muiCreateStyle(app->context, &style) == mui_success &&
                       muiStyle_SetVisualValues(app->context, style, mui_variantBase, &values,
                                                s_background_bit) == mui_success &&
                       muiStyle_SetVisualValues(app->context, style, variant, &other,
                                                s_background_bit) == mui_success,
                   "a class");
    return style;
}

// Rounds a node's corners.
static void Round(SampleApp* app, muiNodeId node, float radius)
{
    muiVisualStyle visual = muiDefaultVisualStyle();
    muiDimension r = SampleLength(radius);
    visual.radius = (muiCornerRadii){r, r, r, r};
    SampleAppCheck(app,
                   muiNode_SetVisualValues(app->context, node, &visual,
                                           MUI_PROPERTY_BIT(mui_propertyRadiusTopStart) |
                                               MUI_PROPERTY_BIT(mui_propertyRadiusTopEnd) |
                                               MUI_PROPERTY_BIT(mui_propertyRadiusBottomEnd) |
                                               MUI_PROPERTY_BIT(mui_propertyRadiusBottomStart)) ==
                       mui_success,
                   "corners");
}

static void SetClass(SampleApp* app, muiNodeId node, muiStyleId style)
{
    SampleAppCheck(app, muiNode_SetClasses(app->context, node, &style, 1) == mui_success,
                   "a node's class");
}

// A row of a fixed height, its children spaced by padding.
static muiNodeId Row(SampleApp* app, muiNodeId parent, float width, float height)
{
    muiNodeId row = SampleNode(app, parent, width, height);
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.container.direction = mui_flexRow;
    layout.margin.top = 12.0f;
    SampleSetLayout(app, row, &layout,
                    MUI_PROPERTY_BIT(mui_propertyFlexDirection) |
                        MUI_PROPERTY_BIT(mui_propertyMarginTop));
    return row;
}

// Makes a node a widget: its role, its name, its flags and focus by
// pointer and by Tab.
static void Widget(SampleApp* app, muiNodeId node, muiRole role, const char* name,
                   muiAccessFlags flags)
{
    muiInteractionStyle interaction = muiDefaultInteractionStyle();
    interaction.focusMode = mui_focusAll;
    SampleAppCheck(
        app,
        muiNode_SetAccessRole(app->context, node, role) == mui_success &&
            muiNode_SetAccessText(app->context, node, mui_accessLabel, name, strlen(name)) ==
                mui_success &&
            muiNode_SetAccessFlags(app->context, node, flags) == mui_success &&
            muiNode_SetInteractionValues(app->context, node, &interaction,
                                         MUI_PROPERTY_BIT(mui_propertyFocusMode)) == mui_success,
        "a widget");
}

// A label beside a widget's mark, spaced from it.
static void Beside(SampleApp* app, muiNodeId parent, const char* text)
{
    muiNodeId label = SampleLabel(app, parent, text, 16.0f, s_text);
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.margin.start = 8.0f;
    SampleSetLayout(app, label, &layout, MUI_PROPERTY_BIT(mui_propertyMarginStart));
}

// The icon: a white play triangle on the button's blue, 16 by 16.
static void MakeIcon(Controls* controls, SampleApp* app)
{
    uint8_t texels[16 * 16 * 4];
    for (int y = 0; y < 16; y++)
    {
        for (int x = 0; x < 16; x++)
        {
            int half = y < 8 ? y : 15 - y;
            bool inside = x >= 3 && x - 3 <= half * 2;
            const uint8_t* rgb = inside ? s_knob : s_button;
            uint8_t* texel = &texels[(y * 16 + x) * 4];
            memcpy(texel, rgb, 3);
            texel[3] = 255;
        }
    }
    SampleAppCheck(app, SampleTexture(&app->sample, 16, 16, texels, &controls->iconTexture),
                   "the icon");
}

// The button: the icon and "Play" on blue, orange while pressed.
static void MakeButton(Controls* controls, SampleApp* app)
{
    controls->button = Row(app, app->root, 112.0f, 36.0f);
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.padding = (muiEdges){10.0f, 10.0f, 10.0f, 10.0f};
    SampleSetLayout(app, controls->button, &layout,
                    MUI_PROPERTY_BIT(mui_propertyPaddingStart) |
                        MUI_PROPERTY_BIT(mui_propertyPaddingTop));
    SetClass(app, controls->button, Class(app, s_button, mui_variantPressed, s_pressed));
    Widget(app, controls->button, mui_roleButton, "Play", 0);
    Round(app, controls->button, 6.0f);
    MakeIcon(controls, app);
    controls->icon = SampleNode(app, controls->button, 16.0f, 16.0f);
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.image = ICON;
    SampleAppCheck(app,
                   muiNode_SetVisualValues(app->context, controls->icon, &visual,
                                           MUI_PROPERTY_BIT(mui_propertyImage)) == mui_success,
                   "the icon's image");
    Beside(app, controls->button, "Play");
}

// The checkbox: a box blue while checked, and its label.
static void MakeCheckbox(Controls* controls, SampleApp* app)
{
    controls->checkbox = Row(app, app->root, 200.0f, 24.0f);
    Widget(app, controls->checkbox, mui_roleCheckBox, "Subtitles", mui_accessCheckable);
    controls->box = SampleNode(app, controls->checkbox, 20.0f, 20.0f);
    SetClass(app, controls->box, Class(app, s_off, mui_variantChecked, s_button));
    Round(app, controls->box, 4.0f);
    Beside(app, controls->checkbox, "Subtitles");
}

// The switch: a track green while on, its knob moved to the right end.
static void MakeSwitch(Controls* controls, SampleApp* app)
{
    controls->toggle = Row(app, app->root, 200.0f, 24.0f);
    Widget(app, controls->toggle, mui_roleSwitch, "Sound", mui_accessCheckable);
    controls->track = SampleNode(app, controls->toggle, 40.0f, 20.0f);
    SetClass(app, controls->track, Class(app, s_off, mui_variantChecked, s_on));
    Round(app, controls->track, 10.0f);
    controls->knob = SampleNode(app, controls->track, 16.0f, 16.0f);
    muiStyleId knob = Class(app, s_knob, mui_variantChecked, s_knob);
    muiLayoutStyle left = muiDefaultLayoutStyle();
    left.margin.start = 2.0f;
    left.margin.top = 2.0f;
    muiLayoutStyle right = muiDefaultLayoutStyle();
    right.margin.start = 22.0f;
    const muiPropertyMask start = MUI_PROPERTY_BIT(mui_propertyMarginStart);
    SampleAppCheck(
        app,
        muiStyle_SetLayoutValues(app->context, knob, mui_variantBase, &left,
                                 start | MUI_PROPERTY_BIT(mui_propertyMarginTop)) == mui_success &&
            muiStyle_SetLayoutValues(app->context, knob, mui_variantChecked, &right, start) ==
                mui_success,
        "the knob's places");
    SetClass(app, controls->knob, knob);
    Round(app, controls->knob, 8.0f);
    Beside(app, controls->toggle, "Sound");
}

// The radio group: Low, Mid and High, each a dot blue while checked.
static void MakeRadios(Controls* controls, SampleApp* app)
{
    static const char* const names[RADIOS] = {"Low", "Mid", "High"};
    muiNodeId group = Row(app, app->root, 300.0f, 24.0f);
    Widget(app, group, mui_roleRadioGroup, "Quality", 0);
    SampleAppCheck(app,
                   muiNode_SetInteractionValues(app->context, group, &(muiInteractionStyle){0},
                                                MUI_PROPERTY_BIT(mui_propertyFocusMode)) ==
                       mui_success,
                   "the group not focused itself");
    muiStyleId dot = Class(app, s_off, mui_variantChecked, s_button);
    for (int i = 0; i < RADIOS; i++)
    {
        controls->radios[i] = Row(app, group, 88.0f, 24.0f);
        muiLayoutStyle flush = muiDefaultLayoutStyle();
        SampleSetLayout(app, controls->radios[i], &flush, MUI_PROPERTY_BIT(mui_propertyMarginTop));
        Widget(app, controls->radios[i], mui_roleRadioButton, names[i], mui_accessCheckable);
        controls->dots[i] = SampleNode(app, controls->radios[i], 16.0f, 16.0f);
        SetClass(app, controls->dots[i], dot);
        Round(app, controls->dots[i], 8.0f);
        Beside(app, controls->radios[i], names[i]);
    }
}

// Sets or clears the checked state on nodes, keeping their other states.
static void Check(SampleApp* app, const muiNodeId* nodes, int count, bool checked)
{
    for (int i = 0; i < count; i++)
    {
        muiState host = muiNode_GetStates(app->context, nodes[i]) &
                        (mui_stateChecked | mui_stateSelected | mui_stateDisabled);
        host = checked ? host | mui_stateChecked : host & (muiState)~mui_stateChecked;
        SampleAppCheck(app, muiNode_SetStates(app->context, nodes[i], host) == mui_success,
                       "a state");
    }
}

static void Choose(Controls* controls, int choice)
{
    controls->choice = choice;
    for (int i = 0; i < RADIOS; i++)
    {
        const muiNodeId nodes[2] = {controls->radios[i], controls->dots[i]};
        Check(controls->app, nodes, 2, i == choice);
    }
}

static bool Same(muiNodeId a, muiNodeId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

// Whether an event activates its node: a click, the navigation's
// activate, or Space, and Enter for a button.
static bool Activates(const muiEvent* event, bool button)
{
    switch (event->kind)
    {
    case mui_eventPointer:
        return event->pointer->kind == mui_pointerRecordClick;
    case mui_eventNavigation:
        return event->navigation == mui_navigateActivate;
    case mui_eventKeyDown:
        return !event->repeat &&
               (event->code == mui_codeSpace || (button && event->code == mui_codeEnter));
    default:
        return false;
    }
}

// Arrows in the radio group move the choice and the focus with it,
// wrapping, as ARIA's radio group does.
static bool Arrow(Controls* controls, SampleApp* app, int radio, const muiEvent* event)
{
    if (event->kind != mui_eventKeyDown)
    {
        return false;
    }
    int step = event->code == mui_codeArrowRight || event->code == mui_codeArrowDown ? 1
               : event->code == mui_codeArrowLeft || event->code == mui_codeArrowUp  ? RADIOS - 1
                                                                                     : 0;
    if (step == 0)
    {
        return false;
    }
    int next = (radio + step) % RADIOS;
    Choose(controls, next);
    SampleAppCheck(app,
                   muiFocus_Set(app->context, event->player, controls->radios[next],
                                mui_focusByNavigation) == mui_success,
                   "the focus moved to the choice");
    return true;
}

static bool Hear(void* user, muiNodeId nodeId, muiPhase phase, const muiEvent* event)
{
    Controls* controls = user;
    SampleApp* app = controls->app;
    if (phase != mui_phaseBubble)
    {
        return false;
    }
    if (Same(nodeId, controls->button) && Activates(event, true))
    {
        controls->plays++;
        return true;
    }
    if (Same(nodeId, controls->checkbox) && Activates(event, false))
    {
        controls->checked = !controls->checked;
        const muiNodeId nodes[2] = {controls->checkbox, controls->box};
        Check(app, nodes, 2, controls->checked);
        return true;
    }
    if (Same(nodeId, controls->toggle) && Activates(event, false))
    {
        controls->on = !controls->on;
        const muiNodeId nodes[3] = {controls->toggle, controls->track, controls->knob};
        Check(app, nodes, 3, controls->on);
        return true;
    }
    for (int i = 0; i < RADIOS; i++)
    {
        if (Same(nodeId, controls->radios[i]))
        {
            if (Activates(event, false))
            {
                Choose(controls, i);
                return true;
            }
            return Arrow(controls, app, i, event);
        }
    }
    return false;
}

static void Build(void* user, SampleApp* app)
{
    Controls* controls = user;
    controls->app = app;
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.container.direction = mui_flexColumn;
    layout.padding = (muiEdges){16.0f, 16.0f, 4.0f, 16.0f};
    SampleSetLayout(app, app->root, &layout,
                    MUI_PROPERTY_BIT(mui_propertyFlexDirection) |
                        MUI_PROPERTY_BIT(mui_propertyPaddingStart) |
                        MUI_PROPERTY_BIT(mui_propertyPaddingTop));
    SampleFill(app, app->root, s_background);
    MakeButton(controls, app);
    MakeCheckbox(controls, app);
    MakeSwitch(controls, app);
    MakeRadios(controls, app);
    Choose(controls, 1);
    SampleAppCheck(app, muiSetEventFunction(app->context, Hear, controls) == mui_success,
                   "the event function");
}

static bool FindImage(void* user, uint64_t key, muiRhiImage* imageOut)
{
    const Controls* controls = user;
    if (key != ICON)
    {
        return false;
    }
    *imageOut = (muiRhiImage){controls->iconTexture, 16, 16};
    return true;
}

static void Finish(void* user, SampleApp* app)
{
    Controls* controls = user;
    if (controls->iconTexture.index1 != 0)
    {
        SampleAppCheck(
            app, mrhiDestroyTexture(app->sample.device, controls->iconTexture) == mrhi_success,
            "the icon let go");
    }
}

// Whether a node is reported checked in the accessibility tree.
static bool Reported(const SampleApp* app, muiNodeId node)
{
    const muiAccessNode* found =
        muiAccessTree_Find(muiWindowAccess_GetTree(app->access), muiAccessIdOf(node));
    return found != NULL && (found->flags & mui_accessChecked) != 0;
}

static bool Shows(const SampleApp* app, muiNodeId node, float x, float y, const uint8_t rgb[3])
{
    return SampleNear(SamplePixelOf(app, node, x, y), rgb);
}

// The first frame: the button blue with its icon, the checkbox and the
// switch off, Mid chosen.
static void Still(void* user, SampleApp* app)
{
    const Controls* controls = user;
    SampleAppCheck(app,
                   Shows(app, controls->button, 100.0f, 4.0f, s_button) &&
                       Shows(app, controls->icon, 5.0f, 8.0f, s_knob) &&
                       Shows(app, controls->icon, 14.0f, 2.0f, s_button),
                   "the button and its icon");
    SampleAppCheck(app, Shows(app, controls->box, 10.0f, 10.0f, s_off), "the checkbox off");
    SampleAppCheck(app,
                   Shows(app, controls->track, 10.0f, 10.0f, s_knob) &&
                       Shows(app, controls->track, 30.0f, 10.0f, s_off),
                   "the switch off, its knob at the left");
    SampleAppCheck(app,
                   Shows(app, controls->dots[0], 8.0f, 8.0f, s_off) &&
                       Shows(app, controls->dots[1], 8.0f, 8.0f, s_button) &&
                       Shows(app, controls->dots[2], 8.0f, 8.0f, s_off),
                   "Mid chosen");
    SampleAppCheck(app,
                   !Reported(app, controls->checkbox) && !Reported(app, controls->toggle) &&
                       Reported(app, controls->radios[1]) && !Reported(app, controls->radios[2]),
                   "the checked states reported");
}

static bool Script(void* user, SampleApp* app, int frame)
{
    Controls* controls = user;
    switch (frame)
    {
    case 0:
        Still(controls, app);
        SamplePost(app, mwin_eventCursorMoved, controls->button, 0);
        SamplePost(app, mwin_eventButtonDown, controls->button, 1);
        SamplePost(app, mwin_eventButtonUp, controls->button, 0);
        SamplePost(app, mwin_eventCursorMoved, controls->checkbox, 0);
        SamplePost(app, mwin_eventButtonDown, controls->checkbox, 1);
        SamplePost(app, mwin_eventButtonUp, controls->checkbox, 0);
        return true;
    case 1:
        SampleAppCheck(app, controls->plays == 1, "the button clicked once");
        SampleAppCheck(app,
                       Shows(app, controls->box, 10.0f, 10.0f, s_button) &&
                           Reported(app, controls->checkbox),
                       "the checkbox checked by a click");
        // The click focused the checkbox: Space unchecks it.
        SamplePostKey(app, mwin_codeSpace, ' ', " ");
        SamplePost(app, mwin_eventCursorMoved, controls->toggle, 0);
        SamplePost(app, mwin_eventButtonDown, controls->toggle, 1);
        SamplePost(app, mwin_eventButtonUp, controls->toggle, 0);
        return true;
    case 2:
        SampleAppCheck(app,
                       Shows(app, controls->box, 10.0f, 10.0f, s_off) &&
                           !Reported(app, controls->checkbox),
                       "the checkbox unchecked by Space");
        SampleAppCheck(app,
                       Shows(app, controls->track, 30.0f, 10.0f, s_knob) &&
                           Shows(app, controls->track, 6.0f, 10.0f, s_on) &&
                           Reported(app, controls->toggle),
                       "the switch on, its knob at the right");
        SamplePost(app, mwin_eventCursorMoved, controls->radios[1], 0);
        SamplePost(app, mwin_eventButtonDown, controls->radios[1], 1);
        SamplePost(app, mwin_eventButtonUp, controls->radios[1], 0);
        SamplePostKey(app, mwin_codeArrowRight, MWIN_KEY_NAMED | mwin_codeArrowRight, NULL);
        return true;
    default:
        SampleAppCheck(app,
                       Shows(app, controls->dots[2], 8.0f, 8.0f, s_button) &&
                           Shows(app, controls->dots[1], 8.0f, 8.0f, s_off) &&
                           Reported(app, controls->radios[2]) &&
                           !Reported(app, controls->radios[1]),
                       "the arrow moved the choice to High");
        SampleAppCheck(app, Same(muiFocus_Get(app->context, 0), controls->radios[2]),
                       "the focus moved with the choice");
        return false;
    }
}

int main(int count, char** arguments)
{
    static Controls controls;
    const SampleAppDef def = {
        .width = 360,
        .height = 240,
        .build = Build,
        .script = Script,
        .still = Still,
        .image = FindImage,
        .finish = Finish,
        .user = &controls,
    };
    return SampleRunApp(&def, count, arguments);
}
