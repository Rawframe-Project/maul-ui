// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A tour of Maul UI: one program that uses its parts together, in the
// order a host calls them, and checks what they did (record mui-0005).
// It opens a window through Maul Window, builds a tree with a title in Liberation Sans, a generated
// picture, a button whose class colours it while pressed, and a scroll
// container of rows; feeds the window's records through the glue, keeps
// the accessibility tree through the glue's access, lays the tree out,
// paints it and draws it with the reference renderer on Maul RHI into a
// texture, which it reads back. Checked: the background, the picture's
// corners, the title's ink, the button's colours, the rows cut at the
// container's edge and moved by a wheel, and the button named in the
// accessibility tree. That is the headless run (--headless), on Maul
// Window's test backend, records posted as a platform reports them.
// Without it the tour opens a real window and presents onto its surface
// until it is closed, or for --frames N frames, the last read back where
// the surface allows and checked as the first headless frame. Exits 77
// without an adapter, unless MUI_RHI_REQUIRED is set.

#include "harness.h"

#include "maul-ui-rhi/renderer.h"
#include "maul-ui-window/access.h"
#include "maul-ui-window/glue.h"
#include "maul-ui/access.h"
#include "maul-ui/access_tree.h"
#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/font.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/scroll.h"
#include "maul-ui/style.h"
#include "maul-ui/text_block.h"
#include "maul-ui/text_style.h"
#include "maul-ui/visual.h"
#include "maul-window/context.h"
#include "maul-window/test.h"
#include "maul-window/window.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sans.inc"

#define WIDTH  320u
#define HEIGHT 240u

// The picture's key, which the renderer's image function knows.
#define PICTURE 1u

#define ROWS 6

static const muiNodeId s_nullNode = {0, 0};

// Colours, sRGB bytes.
static const uint8_t s_background[3] = {24, 32, 40};
static const uint8_t s_button[3] = {48, 112, 224};
static const uint8_t s_pressed[3] = {224, 112, 48};
static const uint8_t s_rows[ROWS][3] = {
    {200, 60, 60}, {60, 200, 60}, {60, 60, 200}, {200, 200, 60}, {60, 200, 200}, {200, 60, 200},
};
// The picture's quarters: red, green, blue and white.
static const uint8_t s_quarters[4][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}};

typedef struct Tour
{
    Sample sample;
    muiTextService* text;
    muiTextHost host;
    muiContext* context;
    muiNodeId root;
    muiNodeId title;
    muiNodeId picture;
    muiNodeId button;
    muiNodeId list;
    muiNodeId rows[ROWS];
    mwinWindowId window;
    muiWindowGlue* glue;
    muiWindowAccess* access;
    muiRhiRenderer* renderer;
    mrhiTextureId pictureTexture;
    // Headless on Maul Window's test backend, drawn into a texture; else
    // presented on the window's surface, for frames frames when that is
    // not 0, or until the window is closed.
    bool headless;
    int frames;
    bool closing;
    SampleSurface surface;
    // The frame's pixels, read back, its size in pixels and its scale.
    uint8_t* pixels;
    uint32_t pixelWidth;
    uint32_t pixelHeight;
    float scale;
    // Whether this frame's pixels are read back.
    bool reading;
    uint64_t now;
    int frame;
} Tour;

static bool Check(Tour* tour, bool condition, const char* what)
{
    return SampleCheck(&tour->sample, condition, what);
}

static muiColor Color(const uint8_t rgb[3])
{
    return (muiColor){rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f, 1.0f};
}

static muiDimension Length(float value)
{
    return (muiDimension){0.0f, value, mui_dimensionValue};
}

// A node under a parent, of a size where one is given (0 for automatic).
static muiNodeId Node(Tour* tour, muiNodeId parent, float width, float height)
{
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId node = s_nullNode;
    Check(tour, muiCreateNode(tour->context, &def, &node) == mui_success, "a node");
    if (parent.index1 != 0)
    {
        Check(tour, muiNode_InsertChild(tour->context, parent, node, s_nullNode) == mui_success,
              "a child");
    }
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    muiPropertyMask mask = 0;
    if (width > 0.0f)
    {
        layout.sizing.width = Length(width);
        mask |= MUI_PROPERTY_BIT(mui_propertyWidth);
    }
    if (height > 0.0f)
    {
        layout.sizing.height = Length(height);
        layout.item.shrink = 0.0f;
        mask |= MUI_PROPERTY_BIT(mui_propertyHeight) | MUI_PROPERTY_BIT(mui_propertyShrink);
    }
    Check(tour, muiNode_SetLayoutValues(tour->context, node, &layout, mask) == mui_success,
          "a size");
    return node;
}

static void Fill(Tour* tour, muiNodeId node, const uint8_t rgb[3])
{
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.background = Color(rgb);
    Check(tour,
          muiNode_SetVisualValues(tour->context, node, &visual,
                                  MUI_PROPERTY_BIT(mui_propertyBackground)) == mui_success,
          "a fill");
}

static void MarginTop(Tour* tour, muiNodeId node, float margin)
{
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.margin.top = margin;
    Check(tour,
          muiNode_SetLayoutValues(tour->context, node, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyMarginTop)) == mui_success,
          "a margin");
}

// The title: a text block in Liberation Sans at 24, nearly white.
static void MakeTitle(Tour* tour)
{
    muiTextBlockId block = {0};
    Check(tour, muiCreateTextBlock(tour->text, "Maul UI", 7, &block) == mui_success, "a block");
    muiNodeDef def = muiDefaultNodeDef();
    def.hostKey = muiTextBlock_GetKey(block);
    Check(tour,
          muiCreateNode(tour->context, &def, &tour->title) == mui_success &&
              muiNode_InsertChild(tour->context, tour->root, tour->title, s_nullNode) ==
                  mui_success,
          "the title");
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.content = mui_contentHost;
    Check(tour,
          muiNode_SetLayoutValues(tour->context, tour->title, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyContent)) == mui_success,
          "host content");
    muiTextStyle text = muiDefaultTextStyle();
    text.color = (muiColor){0.95f, 0.95f, 0.95f, 1.0f};
    text.size = Length(24.0f);
    Check(tour,
          muiNode_SetTextValues(tour->context, tour->title, &text,
                                MUI_PROPERTY_BIT(mui_propertyTextColor) |
                                    MUI_PROPERTY_BIT(mui_propertyFontSize)) == mui_success,
          "the title's text");
}

// A row holding the picture and the button, whose class colours it
// while pressed; the button is named for the accessibility tree.
static void MakeRow(Tour* tour)
{
    muiNodeId row = Node(tour, tour->root, 0.0f, 64.0f);
    MarginTop(tour, row, 12.0f);
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.container.direction = mui_flexRow;
    Check(tour,
          muiNode_SetLayoutValues(tour->context, row, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyFlexDirection)) == mui_success,
          "a row");
    tour->picture = Node(tour, row, 64.0f, 64.0f);
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.image = PICTURE;
    Check(tour,
          muiNode_SetVisualValues(tour->context, tour->picture, &visual,
                                  MUI_PROPERTY_BIT(mui_propertyImage)) == mui_success,
          "the picture");
    tour->button = Node(tour, row, 96.0f, 40.0f);
    layout = muiDefaultLayoutStyle();
    layout.margin.start = 16.0f;
    Check(tour,
          muiNode_SetLayoutValues(tour->context, tour->button, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyMarginStart)) == mui_success,
          "the button's margin");
    muiStyleId pressable = {0};
    visual = muiDefaultVisualStyle();
    visual.background = Color(s_button);
    muiVisualStyle pressed = muiDefaultVisualStyle();
    pressed.background = Color(s_pressed);
    const muiPropertyMask background = MUI_PROPERTY_BIT(mui_propertyBackground);
    Check(tour,
          muiCreateStyle(tour->context, &pressable) == mui_success &&
              muiStyle_SetVisualValues(tour->context, pressable, mui_variantBase, &visual,
                                       background) == mui_success &&
              muiStyle_SetVisualValues(tour->context, pressable, mui_variantPressed, &pressed,
                                       background) == mui_success &&
              muiNode_SetClasses(tour->context, tour->button, &pressable, 1) == mui_success,
          "the button's class");
    Check(tour,
          muiNode_SetAccessRole(tour->context, tour->button, mui_roleButton) == mui_success &&
              muiNode_SetAccessText(tour->context, tour->button, mui_accessLabel, "Play", 4) ==
                  mui_success,
          "the button's role and name");
}

// A column scrolling vertically, 200 by 80, of rows 30 tall.
static void MakeList(Tour* tour)
{
    tour->list = Node(tour, tour->root, 200.0f, 80.0f);
    MarginTop(tour, tour->list, 12.0f);
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.container.direction = mui_flexColumn;
    layout.scrollAxes = mui_scrollVertical;
    Check(tour,
          muiNode_SetLayoutValues(tour->context, tour->list, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyFlexDirection) |
                                      MUI_PROPERTY_BIT(mui_propertyScrollAxes)) == mui_success,
          "the list");
    for (int i = 0; i < ROWS; i++)
    {
        tour->rows[i] = Node(tour, tour->list, 200.0f, 30.0f);
        Fill(tour, tour->rows[i], s_rows[i]);
    }
}

static void MakeTree(Tour* tour)
{
    muiContextDef def = muiDefaultContextDef();
    Check(tour, muiCreateContext(&def, &tour->context) == mui_success, "a context");
    tour->host = (muiTextHost){tour->text, tour->context};
    tour->root = Node(tour, s_nullNode, (float)WIDTH, (float)HEIGHT);
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.container.direction = mui_flexColumn;
    layout.padding = (muiEdges){16.0f, 16.0f, 16.0f, 16.0f};
    Check(tour,
          muiNode_SetLayoutValues(tour->context, tour->root, &layout,
                                  MUI_PROPERTY_BIT(mui_propertyFlexDirection) |
                                      MUI_PROPERTY_BIT(mui_propertyPaddingStart) |
                                      MUI_PROPERTY_BIT(mui_propertyPaddingEnd) |
                                      MUI_PROPERTY_BIT(mui_propertyPaddingTop) |
                                      MUI_PROPERTY_BIT(mui_propertyPaddingBottom)) == mui_success,
          "the root");
    Fill(tour, tour->root, s_background);
    MakeTitle(tour);
    MakeRow(tour);
    MakeList(tour);
}

static bool FindImage(void* context, uint64_t key, muiRhiImage* imageOut)
{
    const Tour* tour = context;
    if (key != PICTURE)
    {
        return false;
    }
    *imageOut = (muiRhiImage){tour->pictureTexture, 8, 8};
    return true;
}

// The picture, 8 by 8 in quarters.
static void MakePicture(Tour* tour)
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
    Check(tour, SampleTexture(&tour->sample, 8, 8, texels, &tour->pictureTexture), "the picture");
}

// The renderer, for targets of a format, once its pipeline is ready.
static bool MakeRenderer(Tour* tour, mrhiFormat format)
{
    muiRhiRendererDef def = muiDefaultRhiRendererDef();
    def.device = tour->sample.device;
    def.targetFormat = format;
    def.image = FindImage;
    def.imageContext = tour;
    def.text = tour->text;
    return Check(tour,
                 muiCreateRhiRenderer(&def, &tour->renderer) == mui_success &&
                     SampleAwaitReady(&tour->sample, tour->renderer),
                 "the renderer ready");
}

// Room for a frame's pixels at a size.
static bool Size(Tour* tour, uint32_t width, uint32_t height, float scale)
{
    if (width != tour->pixelWidth || height != tour->pixelHeight)
    {
        free(tour->pixels);
        tour->pixels = malloc((size_t)width * height * 4);
        tour->pixelWidth = width;
        tour->pixelHeight = height;
    }
    tour->scale = scale;
    return Check(tour, tour->pixels != NULL, "room for the pixels");
}

static mwinResult Init(mwinContext* windows, void* user)
{
    Tour* tour = user;
    mwinWindowDef window = mwinDefaultWindowDef();
    window.size = (mwinSize){(float)WIDTH, (float)HEIGHT};
    Check(tour, mwinCreateWindow(windows, &window, &tour->window, NULL) == mwin_success,
          "a window");
    MakeTree(tour);
    muiWindowGlueDef glue = muiDefaultWindowGlueDef();
    glue.windows = windows;
    glue.window = tour->window;
    glue.context = tour->context;
    glue.root = tour->root;
    Check(tour, muiCreateWindowGlue(&glue, &tour->glue) == mui_success, "a glue");
    MakePicture(tour);
    if (tour->headless)
    {
        (void)MakeRenderer(tour, mrhi_formatRgba8UnormSrgb);
        (void)Size(tour, WIDTH, HEIGHT, 1.0f);
    }
    return mwin_success;
}

// Hands the window's records to the glue and the access, as a host does
// each frame, and notes a request to close.
static void Drain(Tour* tour, mwinContext* windows)
{
    mwinEvent event;
    while (mwinNextEvent(windows, &event) == mwin_success)
    {
        bool handled = false;
        Check(tour, muiWindowGlue_HandleEvent(tour->glue, &event, &handled) == mui_success,
              "a record taken");
        Check(tour,
              tour->access == NULL ||
                  muiWindowAccess_HandleEvent(tour->access, &event) == mui_success,
              "a record seen by the access");
        tour->closing = tour->closing || event.type == mwin_eventCloseRequested;
    }
}

// The window's surface, made once the platform has made the window and
// configured again as its size in pixels changes; let go while the
// window has none. Whether there is one to draw on.
static bool Surface(Tour* tour, mwinContext* windows)
{
    mwinWindowState state;
    if (mwinGetWindowState(windows, tour->window, &state) != mwin_success || !state.created ||
        state.surfaceLost)
    {
        SampleSurfaceClose(&tour->sample, &tour->surface);
        return false;
    }
    uint32_t width = state.pixelSize.width;
    uint32_t height = state.pixelSize.height;
    if (width == 0 || height == 0)
    {
        return false;
    }
    if (tour->surface.surface.index1 == 0)
    {
        mwinNativeHandles handles;
        if (!Check(tour, mwinGetNativeHandles(windows, tour->window, &handles) == mwin_success,
                   "the window's handles") ||
            !Check(tour, SampleSurfaceOpen(&tour->sample, &handles, width, height, &tour->surface),
                   "a surface") ||
            (tour->renderer == NULL && !MakeRenderer(tour, tour->surface.config.color.format)))
        {
            tour->closing = true;
            return false;
        }
    }
    else if (width != tour->surface.config.width || height != tour->surface.config.height)
    {
        Check(tour, SampleSurfaceResize(&tour->sample, &tour->surface, width, height),
              "the surface resized");
    }
    return Size(tour, width, height, state.scale);
}

// One frame: the records, the access once the window has its surface
// (which its first records bring), layout at now, the accessibility
// tree's changes, the list painted and drawn, into a texture headless,
// else onto the window's surface.
static void Step(Tour* tour, mwinContext* windows)
{
    Drain(tour, windows);
    if (tour->access == NULL)
    {
        muiWindowAccessDef access = muiDefaultWindowAccessDef();
        access.glue = tour->glue;
        Check(tour, muiCreateWindowAccess(&access, &tour->access) == mui_success, "an access");
    }
    if (!tour->headless && !Surface(tour, windows))
    {
        return;
    }
    const muiLayoutInput layout = {(float)WIDTH, (float)HEIGHT, muiMeasureText,
                                   &tour->host,  tour->now,     NULL};
    const muiDrawInput draw = {1, tour->scale, muiPaintText, &tour->host};
    muiDrawList list;
    bool built = muiComputeLayout(tour->context, tour->root, &layout) == mui_success &&
                 muiWindowAccess_Update(tour->access) == mui_success &&
                 muiBuildDrawList(tour->context, tour->root, &draw) == mui_success &&
                 muiGetDrawList(tour->context, &list) == mui_success;
    if (tour->headless)
    {
        Check(tour,
              built && SampleRender(&tour->sample, tour->renderer, &list, tour->pixelWidth,
                                    tour->pixelHeight, tour->pixels),
              "a frame drawn");
        return;
    }
    SamplePresented presented = built ? SamplePresent(&tour->sample, &tour->surface, tour->renderer,
                                                      &list, tour->reading ? tour->pixels : NULL)
                                      : sample_failed;
    Check(tour, presented != sample_failed, "a frame presented");
}

// The pixel at a point of a node's border box.
static const uint8_t* PixelOf(const Tour* tour, muiNodeId node, float x, float y)
{
    float rootX = 0.0f;
    float rootY = 0.0f;
    if (muiNode_MapToRoot(tour->context, node, x, y, &rootX, &rootY) != mui_success)
    {
        rootX = -1.0f;
    }
    // Device pixels, at the frame's scale.
    float pixelX = rootX * tour->scale;
    float pixelY = rootY * tour->scale;
    if (tour->pixels == NULL || pixelX < 0.0f || pixelY < 0.0f ||
        pixelX >= (float)tour->pixelWidth || pixelY >= (float)tour->pixelHeight)
    {
        static const uint8_t none[4] = {0, 0, 0, 0};
        return none;
    }
    return &tour->pixels[((size_t)pixelY * tour->pixelWidth + (size_t)pixelX) * 4];
}

static bool Near(const uint8_t* pixel, const uint8_t rgb[3])
{
    for (int i = 0; i < 3; i++)
    {
        if (abs((int)pixel[i] - (int)rgb[i]) > 3)
        {
            return false;
        }
    }
    return true;
}

// Whether the title has ink: a pixel well above the background's.
static bool Inked(const Tour* tour)
{
    muiRect rect = muiNode_GetRect(tour->context, tour->title);
    for (float y = 0.0f; y < rect.height; y += 1.0f)
    {
        for (float x = 0.0f; x < rect.width; x += 1.0f)
        {
            if (PixelOf(tour, tour->title, x, y)[1] > 160)
            {
                return true;
            }
        }
    }
    return false;
}

static bool Named(const Tour* tour)
{
    char name[8] = {0};
    size_t length = 0;
    return muiAccessTree_GetName(muiWindowAccess_GetTree(tour->access), muiAccessIdOf(tour->button),
                                 name, sizeof name, &length) == mui_success &&
           length == 4 && memcmp(name, "Play", 4) == 0;
}

// The first frame: everything where the tree puts it.
static void CheckStill(Tour* tour)
{
    Check(tour, Near(PixelOf(tour, tour->root, 4.0f, 4.0f), s_background), "the background");
    Check(tour,
          Near(PixelOf(tour, tour->picture, 8.0f, 8.0f), s_quarters[0]) &&
              Near(PixelOf(tour, tour->picture, 56.0f, 8.0f), s_quarters[1]) &&
              Near(PixelOf(tour, tour->picture, 8.0f, 56.0f), s_quarters[2]) &&
              Near(PixelOf(tour, tour->picture, 56.0f, 56.0f), s_quarters[3]),
          "the picture's quarters");
    Check(tour, Inked(tour), "the title's ink");
    Check(tour, Near(PixelOf(tour, tour->button, 48.0f, 20.0f), s_button), "the button");
    Check(tour,
          Near(PixelOf(tour, tour->list, 100.0f, 15.0f), s_rows[0]) &&
              Near(PixelOf(tour, tour->list, 100.0f, 75.0f), s_rows[2]),
          "the list's first rows");
    Check(tour, Near(PixelOf(tour, tour->list, 100.0f, 84.0f), s_background),
          "the rows cut at the list's edge");
    Check(tour, Named(tour), "the button named in the accessibility tree");
}

// Posts records as a platform reports them, stamped now.
static void Post(Tour* tour, mwinContext* windows, mwinEventType type, muiNodeId node,
                 uint8_t buttons)
{
    float x = 0.0f;
    float y = 0.0f;
    muiRect rect = muiNode_GetRect(tour->context, node);
    Check(tour,
          muiNode_MapToRoot(tour->context, node, rect.width / 2.0f, rect.height / 2.0f, &x, &y) ==
              mui_success,
          "a point");
    mwinEvent event = {.type = type, .window = tour->window};
    event.data.pointer.position = (mwinPosition){x, y};
    event.data.pointer.button = type == mwin_eventCursorMoved ? 0 : mwin_buttonLeft;
    event.data.pointer.buttons = buttons;
    Check(tour, mwinTestPost(windows, &event) == mwin_success, "a record posted");
}

static void PostWheel(Tour* tour, mwinContext* windows)
{
    Post(tour, windows, mwin_eventCursorMoved, tour->list, 0);
    mwinEvent wheel = {.type = mwin_eventWheel, .window = tour->window};
    wheel.data.wheel.y = -1.0f;
    Check(tour, mwinTestPost(windows, &wheel) == mwin_success, "a wheel posted");
}

// The row the list shows at a point of its own, scrolled by its offset.
static int RowAt(const Tour* tour, float y)
{
    float scrollX = 0.0f;
    float scrollY = 0.0f;
    if (muiNode_GetScroll(tour->context, tour->list, &scrollX, &scrollY) != mui_success)
    {
        return -1;
    }
    int row = (int)((y + scrollY) / 30.0f);
    return row >= 0 && row < ROWS ? row : -1;
}

// Headless: a second a frame on the records' clock, so the wheel's
// easing ends between frames, and records posted as a platform reports
// them, each frame checking what the last one's did.
static mwinFrameResult Scripted(Tour* tour, mwinContext* windows)
{
    tour->now += 1000000000u;
    Check(tour, mwinTestSetTime(windows, tour->now) == mwin_success, "the clock");
    Step(tour, windows);
    switch (tour->frame++)
    {
    case 0:
        CheckStill(tour);
        Post(tour, windows, mwin_eventCursorMoved, tour->button, 0);
        break;
    case 1:
        Check(tour, Near(PixelOf(tour, tour->button, 48.0f, 20.0f), s_button),
              "the button hovered, not pressed");
        Post(tour, windows, mwin_eventButtonDown, tour->button, 1);
        break;
    case 2:
        Check(tour, Near(PixelOf(tour, tour->button, 48.0f, 20.0f), s_pressed),
              "the button pressed");
        Post(tour, windows, mwin_eventButtonUp, tour->button, 0);
        PostWheel(tour, windows);
        break;
    default:
    {
        Check(tour, Near(PixelOf(tour, tour->button, 48.0f, 20.0f), s_button), "the button let go");
        int row = RowAt(tour, 15.0f);
        Check(tour, row > 0 && Near(PixelOf(tour, tour->list, 100.0f, 15.0f), s_rows[row]),
              "the list scrolled by the wheel");
        return mwin_frameStop;
    }
    }
    return tour->frame > 8 ? mwin_frameStop : mwin_frameContinue;
}

// Now on the monotonic clock the records are stamped on, where the C
// library has it; the newest record's time otherwise.
static uint64_t Now(const Tour* tour)
{
#ifdef TIME_MONOTONIC
    struct timespec now = {0};
    if (timespec_get(&now, TIME_MONOTONIC) == TIME_MONOTONIC)
    {
        uint64_t ns = (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
        return ns > tour->now ? ns : tour->now;
    }
#endif
    return tour->now;
}

// With a window: frames presented as the user uses it, until it is
// closed, or for the frames asked, the last read back, where the
// surface allows, and checked as the headless run's first.
static mwinFrameResult Shown(Tour* tour, mwinContext* windows)
{
    tour->now = Now(tour);
    tour->reading = tour->frames != 0 && tour->frame == tour->frames - 1;
    Step(tour, windows);
    tour->frame++;
    if (tour->reading && tour->surface.copies)
    {
        CheckStill(tour);
    }
    bool done = tour->closing || (tour->frames != 0 && tour->frame >= tour->frames);
    return done ? mwin_frameStop : mwin_frameContinue;
}

static mwinFrameResult Frame(mwinContext* windows, void* user)
{
    Tour* tour = user;
    return tour->headless ? Scripted(tour, windows) : Shown(tour, windows);
}

// The arguments: --headless, and --frames N for a window shown N frames.
static bool Arguments(Tour* tour, int count, char** arguments)
{
    for (int i = 1; i < count; i++)
    {
        if (strcmp(arguments[i], "--headless") == 0)
        {
            tour->headless = true;
        }
        else if (strcmp(arguments[i], "--frames") == 0 && i + 1 < count)
        {
            tour->frames = atoi(arguments[++i]);
        }
        else
        {
            printf("usage: %s [--headless] [--frames N]\n", arguments[0]);
            return false;
        }
    }
    return tour->frames >= 0;
}

int main(int count, char** arguments)
{
    static Tour tour;
    if (!Arguments(&tour, count, arguments))
    {
        return 1;
    }
    int status = SampleOpen(&tour.sample);
    if (status != 0)
    {
        return status;
    }
    muiTextServiceDef textDef = muiDefaultTextServiceDef();
    muiFontDef font = muiDefaultFontDef();
    font.data = s_sans;
    font.size = sizeof s_sans;
    font.dataMode = mui_fontDataBorrow;
    muiFontId sans = {0};
    bool ready = muiCreateTextService(&textDef, &tour.text) == mui_success &&
                 muiCreateFont(tour.text, &font, &sans) == mui_success &&
                 muiSetDefaultFont(tour.text, sans) == mui_success;
    if (Check(&tour, ready, "the text service"))
    {
        mwinAppDef def = mwinDefaultAppDef();
        def.context.backend = tour.headless ? mwin_backendTest : mwin_backendNative;
        def.init = Init;
        def.frame = Frame;
        def.user = &tour;
        Check(&tour, mwinRun(&def) == mwin_success && (!tour.headless || tour.frame >= 4),
              "the program ran");
    }
    muiDestroyRhiRenderer(tour.renderer);
    SampleSurfaceClose(&tour.sample, &tour.surface);
    muiDestroyWindowAccess(tour.access);
    muiDestroyWindowGlue(tour.glue);
    muiDestroyContext(tour.context);
    muiDestroyTextService(tour.text);
    if (tour.pictureTexture.index1 != 0)
    {
        Check(&tour, mrhiDestroyTexture(tour.sample.device, tour.pictureTexture) == mrhi_success,
              "the picture let go");
    }
    free(tour.pixels);
    return SampleClose(&tour.sample);
}
