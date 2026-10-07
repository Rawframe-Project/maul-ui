// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A tour of Maul UI: a tree using its parts together, on the samples'
// application frame (app.h), and what they did checked (record
// mui-0005). A title in Liberation Sans, a generated picture, a button
// whose class colours it while pressed, named for the accessibility
// tree, and a scroll container of rows. Headless, records posted as a
// platform reports them: checked, the background, the picture's
// corners, the title's ink, the button hovered and not pressed,
// pressed and let go, the rows cut at the container's edge and moved
// by a wheel, and the button named in the accessibility tree; windowed
// with --frames, the first of those on the last frame presented.

#include "app.h"

#include "maul-rhi/resources.h"
#include "maul-ui/access.h"
#include "maul-ui/access_tree.h"
#include "maul-ui/scroll.h"
#include "maul-ui/style.h"

#include <string.h>

// The picture's key, which the renderer's image function knows.
#define PICTURE 1u

#define ROWS 6

// Colours, sRGB bytes.
static const uint8_t s_background[3] = {24, 32, 40};
static const uint8_t s_title[3] = {242, 242, 242};
static const uint8_t s_button[3] = {48, 112, 224};
static const uint8_t s_pressed[3] = {224, 112, 48};
static const uint8_t s_rows[ROWS][3] = {
    {200, 60, 60}, {60, 200, 60}, {60, 60, 200}, {200, 200, 60}, {60, 200, 200}, {200, 60, 200},
};
// The picture's quarters: red, green, blue and white.
static const uint8_t s_quarters[4][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}};

typedef struct Tour
{
    muiNodeId title;
    muiNodeId picture;
    muiNodeId button;
    muiNodeId list;
    mrhiTextureId pictureTexture;
} Tour;

static void MarginTop(SampleApp* app, muiNodeId node, float margin)
{
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.margin.top = margin;
    SampleSetLayout(app, node, &layout, MUI_PROPERTY_BIT(mui_propertyMarginTop));
}

// The picture, 8 by 8 in quarters.
static void MakePicture(Tour* tour, SampleApp* app)
{
    uint8_t texels[8 * 8 * 4];
    for (int y = 0; y < 8; y++)
    {
        for (int x = 0; x < 8; x++)
        {
            const uint8_t* quarter = s_quarters[(y / 4) * 2 + x / 4];
            uint8_t* texel = &texels[(y * 8 + x) * 4];
            memcpy(texel, quarter, 3);
            texel[3] = 255;
        }
    }
    SampleAppCheck(app, SampleTexture(&app->sample, 8, 8, texels, &tour->pictureTexture),
                   "the picture");
}

// A row holding the picture and the button, whose class colours it
// while pressed; the button is named for the accessibility tree.
static void MakeRow(Tour* tour, SampleApp* app)
{
    muiNodeId row = SampleNode(app, app->root, 0.0f, 64.0f);
    MarginTop(app, row, 12.0f);
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.container.direction = mui_flexRow;
    SampleSetLayout(app, row, &layout, MUI_PROPERTY_BIT(mui_propertyFlexDirection));
    tour->picture = SampleNode(app, row, 64.0f, 64.0f);
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.image = PICTURE;
    SampleAppCheck(app,
                   muiNode_SetVisualValues(app->context, tour->picture, &visual,
                                           MUI_PROPERTY_BIT(mui_propertyImage)) == mui_success,
                   "the picture");
    tour->button = SampleNode(app, row, 96.0f, 40.0f);
    layout = muiDefaultLayoutStyle();
    layout.margin.start = 16.0f;
    SampleSetLayout(app, tour->button, &layout, MUI_PROPERTY_BIT(mui_propertyMarginStart));
    muiStyleId pressable = {0};
    visual = muiDefaultVisualStyle();
    visual.background = SampleColor(s_button);
    muiVisualStyle pressed = muiDefaultVisualStyle();
    pressed.background = SampleColor(s_pressed);
    const muiPropertyMask background = MUI_PROPERTY_BIT(mui_propertyBackground);
    SampleAppCheck(app,
                   muiCreateStyle(app->context, &pressable) == mui_success &&
                       muiStyle_SetVisualValues(app->context, pressable, mui_variantBase, &visual,
                                                background) == mui_success &&
                       muiStyle_SetVisualValues(app->context, pressable, mui_variantPressed,
                                                &pressed, background) == mui_success &&
                       muiNode_SetClasses(app->context, tour->button, &pressable, 1) == mui_success,
                   "the button's class");
    SampleAppCheck(
        app,
        muiNode_SetAccessRole(app->context, tour->button, mui_roleButton) == mui_success &&
            muiNode_SetAccessText(app->context, tour->button, mui_accessLabel, "Play", 4) ==
                mui_success,
        "the button's role and name");
}

// A column scrolling vertically, 200 by 80, of rows 30 tall.
static void MakeList(Tour* tour, SampleApp* app)
{
    tour->list = SampleNode(app, app->root, 200.0f, 80.0f);
    MarginTop(app, tour->list, 12.0f);
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.container.direction = mui_flexColumn;
    layout.scrollAxes = mui_scrollVertical;
    SampleSetLayout(app, tour->list, &layout,
                    MUI_PROPERTY_BIT(mui_propertyFlexDirection) |
                        MUI_PROPERTY_BIT(mui_propertyScrollAxes));
    for (int i = 0; i < ROWS; i++)
    {
        muiNodeId row = SampleNode(app, tour->list, 200.0f, 30.0f);
        SampleFill(app, row, s_rows[i]);
    }
}

static void Build(void* user, SampleApp* app)
{
    Tour* tour = user;
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.container.direction = mui_flexColumn;
    layout.padding = (muiEdges){16.0f, 16.0f, 16.0f, 16.0f};
    SampleSetLayout(
        app, app->root, &layout,
        MUI_PROPERTY_BIT(mui_propertyFlexDirection) | MUI_PROPERTY_BIT(mui_propertyPaddingStart) |
            MUI_PROPERTY_BIT(mui_propertyPaddingEnd) | MUI_PROPERTY_BIT(mui_propertyPaddingTop) |
            MUI_PROPERTY_BIT(mui_propertyPaddingBottom));
    SampleFill(app, app->root, s_background);
    tour->title = SampleLabel(app, app->root, "Maul UI", 24.0f, s_title);
    MakePicture(tour, app);
    MakeRow(tour, app);
    MakeList(tour, app);
}

static bool FindImage(void* user, uint64_t key, muiRhiImage* imageOut)
{
    const Tour* tour = user;
    if (key != PICTURE)
    {
        return false;
    }
    *imageOut = (muiRhiImage){tour->pictureTexture, 8, 8};
    return true;
}

static void Finish(void* user, SampleApp* app)
{
    Tour* tour = user;
    if (tour->pictureTexture.index1 != 0)
    {
        SampleAppCheck(app,
                       mrhiDestroyTexture(app->sample.device, tour->pictureTexture) == mrhi_success,
                       "the picture let go");
    }
}

// Whether the title has ink: a pixel well above the background's.
static bool Inked(const Tour* tour, const SampleApp* app)
{
    muiRect rect = muiNode_GetRect(app->context, tour->title);
    for (float y = 0.0f; y < rect.height; y += 1.0f)
    {
        for (float x = 0.0f; x < rect.width; x += 1.0f)
        {
            if (SamplePixelOf(app, tour->title, x, y)[1] > 160)
            {
                return true;
            }
        }
    }
    return false;
}

static bool Named(const Tour* tour, const SampleApp* app)
{
    char name[8] = {0};
    size_t length = 0;
    return muiAccessTree_GetName(muiWindowAccess_GetTree(app->access), muiAccessIdOf(tour->button),
                                 name, sizeof name, &length) == mui_success &&
           length == 4 && memcmp(name, "Play", 4) == 0;
}

// The first frame: everything where the tree puts it.
static void Still(void* user, SampleApp* app)
{
    const Tour* tour = user;
    SampleAppCheck(app, SampleNear(SamplePixelOf(app, app->root, 4.0f, 4.0f), s_background),
                   "the background");
    SampleAppCheck(app,
                   SampleNear(SamplePixelOf(app, tour->picture, 8.0f, 8.0f), s_quarters[0]) &&
                       SampleNear(SamplePixelOf(app, tour->picture, 56.0f, 8.0f), s_quarters[1]) &&
                       SampleNear(SamplePixelOf(app, tour->picture, 8.0f, 56.0f), s_quarters[2]) &&
                       SampleNear(SamplePixelOf(app, tour->picture, 56.0f, 56.0f), s_quarters[3]),
                   "the picture's quarters");
    SampleAppCheck(app, Inked(tour, app), "the title's ink");
    SampleAppCheck(app, SampleNear(SamplePixelOf(app, tour->button, 48.0f, 20.0f), s_button),
                   "the button");
    SampleAppCheck(app,
                   SampleNear(SamplePixelOf(app, tour->list, 100.0f, 15.0f), s_rows[0]) &&
                       SampleNear(SamplePixelOf(app, tour->list, 100.0f, 75.0f), s_rows[2]),
                   "the list's first rows");
    SampleAppCheck(app, SampleNear(SamplePixelOf(app, tour->list, 100.0f, 84.0f), s_background),
                   "the rows cut at the list's edge");
    SampleAppCheck(app, Named(tour, app), "the button named in the accessibility tree");
}

// The row the list shows at a point of its own, scrolled by its offset.
static int RowAt(const Tour* tour, const SampleApp* app, float y)
{
    float scrollX = 0.0f;
    float scrollY = 0.0f;
    if (muiNode_GetScroll(app->context, tour->list, &scrollX, &scrollY) != mui_success)
    {
        return -1;
    }
    int row = (int)((y + scrollY) / 30.0f);
    return row >= 0 && row < ROWS ? row : -1;
}

static bool Script(void* user, SampleApp* app, int frame)
{
    Tour* tour = user;
    const uint8_t* button = SamplePixelOf(app, tour->button, 48.0f, 20.0f);
    switch (frame)
    {
    case 0:
        Still(tour, app);
        SamplePost(app, mwin_eventCursorMoved, tour->button, 0);
        return true;
    case 1:
        SampleAppCheck(app, SampleNear(button, s_button), "the button hovered, not pressed");
        SamplePost(app, mwin_eventButtonDown, tour->button, 1);
        return true;
    case 2:
        SampleAppCheck(app, SampleNear(button, s_pressed), "the button pressed");
        SamplePost(app, mwin_eventButtonUp, tour->button, 0);
        SamplePostWheel(app, tour->list, -1.0f);
        return true;
    default:
    {
        SampleAppCheck(app, SampleNear(button, s_button), "the button let go");
        int row = RowAt(tour, app, 15.0f);
        SampleAppCheck(
            app, row > 0 && SampleNear(SamplePixelOf(app, tour->list, 100.0f, 15.0f), s_rows[row]),
            "the list scrolled by the wheel");
        return false;
    }
    }
}

int main(int count, char** arguments)
{
    static Tour tour;
    const SampleAppDef def = {
        .width = 320,
        .height = 240,
        .build = Build,
        .script = Script,
        .still = Still,
        .image = FindImage,
        .finish = Finish,
        .user = &tour,
    };
    return SampleRunApp(&def, count, arguments);
}
