// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The samples' application frame (record mui-0005).

#include "app.h"

#include "maul-ui-window/clipboard.h"
#include "maul-ui/draw.h"
#include "maul-ui/font.h"
#include "maul-ui/text_style.h"
#include "maul-window/test.h"
#include "maul-window/window.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sans.inc"

static const muiNodeId s_nullNode = {0, 0};

bool SampleAppCheck(SampleApp* app, bool condition, const char* what)
{
    return SampleCheck(&app->sample, condition, what);
}

muiColor SampleColor(const uint8_t rgb[3])
{
    return (muiColor){rgb[0] / 255.0f, rgb[1] / 255.0f, rgb[2] / 255.0f, 1.0f};
}

muiDimension SampleLength(float value)
{
    return (muiDimension){0.0f, value, mui_dimensionValue};
}

void SampleSetLayout(SampleApp* app, muiNodeId node, const muiLayoutStyle* layout,
                     muiPropertyMask mask)
{
    SampleAppCheck(app, muiNode_SetLayoutValues(app->context, node, layout, mask) == mui_success,
                   "layout values");
}

muiNodeId SampleNode(SampleApp* app, muiNodeId parent, float width, float height)
{
    muiNodeDef def = muiDefaultNodeDef();
    muiNodeId node = s_nullNode;
    SampleAppCheck(app, muiCreateNode(app->context, &def, &node) == mui_success, "a node");
    if (parent.index1 != 0)
    {
        SampleAppCheck(app,
                       muiNode_InsertChild(app->context, parent, node, s_nullNode) == mui_success,
                       "a child");
    }
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    muiPropertyMask mask = 0;
    if (width > 0.0f)
    {
        layout.sizing.width = SampleLength(width);
        mask |= MUI_PROPERTY_BIT(mui_propertyWidth);
    }
    if (height > 0.0f)
    {
        layout.sizing.height = SampleLength(height);
        layout.item.shrink = 0.0f;
        mask |= MUI_PROPERTY_BIT(mui_propertyHeight) | MUI_PROPERTY_BIT(mui_propertyShrink);
    }
    SampleSetLayout(app, node, &layout, mask);
    return node;
}

void SampleFill(SampleApp* app, muiNodeId node, const uint8_t rgb[3])
{
    muiVisualStyle visual = muiDefaultVisualStyle();
    visual.background = SampleColor(rgb);
    SampleAppCheck(app,
                   muiNode_SetVisualValues(app->context, node, &visual,
                                           MUI_PROPERTY_BIT(mui_propertyBackground)) == mui_success,
                   "a fill");
}

muiNodeId SampleLabel(SampleApp* app, muiNodeId parent, const char* text, float size,
                      const uint8_t rgb[3])
{
    muiTextBlockId block = {0};
    return SampleTextNode(app, parent, text, size, rgb, &block);
}

muiNodeId SampleTextNode(SampleApp* app, muiNodeId parent, const char* text, float size,
                         const uint8_t rgb[3], muiTextBlockId* blockOut)
{
    muiTextBlockId block = {0};
    SampleAppCheck(app, muiCreateTextBlock(app->text, text, strlen(text), &block) == mui_success,
                   "a block");
    muiNodeDef def = muiDefaultNodeDef();
    def.hostKey = muiTextBlock_GetKey(block);
    muiNodeId node = s_nullNode;
    SampleAppCheck(app,
                   muiCreateNode(app->context, &def, &node) == mui_success &&
                       muiNode_InsertChild(app->context, parent, node, s_nullNode) == mui_success,
                   "a label");
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.content = mui_contentHost;
    SampleSetLayout(app, node, &layout, MUI_PROPERTY_BIT(mui_propertyContent));
    muiTextStyle style = muiDefaultTextStyle();
    style.color = SampleColor(rgb);
    style.size = SampleLength(size);
    SampleAppCheck(app,
                   muiNode_SetTextValues(app->context, node, &style,
                                         MUI_PROPERTY_BIT(mui_propertyTextColor) |
                                             MUI_PROPERTY_BIT(mui_propertyFontSize)) == mui_success,
                   "a label's text");
    *blockOut = block;
    return node;
}

// The renderer, for targets of a format, once its pipeline is ready.
// Makes the renderer; headless, it is waited for, which windowed frames
// cannot do in a browser: they pump it until it is ready.
static bool MakeRenderer(SampleApp* app, mrhiFormat format)
{
    muiRhiRendererDef def = muiDefaultRhiRendererDef();
    def.device = app->sample.device;
    def.targetFormat = format;
    def.image = app->def->image;
    def.imageContext = app->def->user;
    def.text = app->text;
    return SampleAppCheck(app,
                          muiCreateRhiRenderer(&def, &app->renderer) == mui_success &&
                              (!app->headless || SampleAwaitReady(&app->sample, app->renderer)),
                          "the renderer ready");
}

// Room for a frame's pixels at a size.
static bool Size(SampleApp* app, uint32_t width, uint32_t height, float scale)
{
    if (width != app->pixelWidth || height != app->pixelHeight)
    {
        free(app->pixels);
        app->pixels = malloc((size_t)width * height * 4);
        app->pixelWidth = width;
        app->pixelHeight = height;
    }
    app->scale = scale;
    return SampleAppCheck(app, app->pixels != NULL, "room for the pixels");
}

// The context and its root, the size of the window.
static void MakeContext(SampleApp* app)
{
    muiContextDef def = muiDefaultContextDef();
    SampleAppCheck(app, muiCreateContext(&def, &app->context) == mui_success, "a context");
    app->host = (muiTextHost){app->text, app->context};
    app->root = SampleNode(app, s_nullNode, (float)app->def->width, (float)app->def->height);
    // Its background reaches every edge; what it holds keeps out of what
    // a display's cutouts and bars cover.
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.safeArea = mui_edgeStart | mui_edgeEnd | mui_edgeTop | mui_edgeBottom;
    SampleAppCheck(app,
                   muiNode_SetLayoutValues(app->context, app->root, &layout,
                                           MUI_PROPERTY_BIT(mui_propertySafeArea)) == mui_success,
                   "the root keeps to the safe area");
}

// The window's safe area, which Maul Window gives in the same order; none
// headless.
static muiSides SafeAreaOf(const SampleApp* app)
{
    mwinWindowState state;
    if (app->headless || mwinGetWindowState(app->windows, app->window, &state) != mwin_success)
    {
        return (muiSides){0};
    }
    return (muiSides){state.safeArea.top, state.safeArea.right, state.safeArea.bottom,
                      state.safeArea.left};
}

static mwinResult Init(mwinContext* windows, void* user)
{
    SampleApp* app = user;
    app->windows = windows;
    mwinWindowDef window = mwinDefaultWindowDef();
    window.size = (mwinSize){(float)app->def->width, (float)app->def->height};
    SampleAppCheck(app, mwinCreateWindow(windows, &window, &app->window, NULL) == mwin_success,
                   "a window");
    MakeContext(app);
    app->def->build(app->def->user, app);
    muiWindowGlueDef glue = muiDefaultWindowGlueDef();
    glue.windows = windows;
    glue.window = app->window;
    glue.context = app->context;
    glue.root = app->root;
    SampleAppCheck(app, muiCreateWindowGlue(&glue, &app->glue) == mui_success, "a glue");
    if (app->headless)
    {
        (void)MakeRenderer(app, mrhi_formatRgba8UnormSrgb);
        (void)Size(app, app->def->width, app->def->height, 1.0f);
    }
    return mwin_success;
}

// Takes the clipboard's answer into the field that asked for a paste.
static void TakePaste(SampleApp* app, const mwinEvent* event)
{
    if (app->pasteBlock.index1 == 0)
    {
        return;
    }
    bool changed = false;
    muiResult result = muiWindowGlue_Paste(app->glue, app->text, app->pasteBlock, event, &changed);
    SampleAppCheck(app, result == mui_success || result == mui_empty, "a paste");
    if (result != mui_success)
    {
        return;
    }
    app->pasteBlock = (muiTextBlockId){0, 0};
    SampleAppCheck(
        app, !changed || muiNode_MarkContentChanged(app->context, app->pasteNode) == mui_success,
        "a paste shown");
    if (changed && app->def->edited != NULL)
    {
        app->def->edited(app->def->user, app, app->pasteNode);
    }
}

bool SampleEdit(SampleApp* app, muiNodeId node, const muiEvent* event, bool* changedOut)
{
    const muiTextEditInput input = {SAMPLE_KEYMAP, muiWindowGlue_WriteClipboard, app->glue};
    muiTextEditOutcome outcome = {false, false, false};
    SampleAppCheck(app, muiTextEditEvent(&app->host, node, event, &input, &outcome) == mui_success,
                   "an event edited");
    SampleAppCheck(
        app, !outcome.changed || muiNode_MarkContentChanged(app->context, node) == mui_success,
        "an edit shown");
    if (outcome.paste)
    {
        uint64_t key = muiNode_GetHostKey(app->context, node);
        app->pasteNode = node;
        app->pasteBlock = (muiTextBlockId){(uint32_t)key, (uint32_t)(key >> 32)};
        SampleAppCheck(app, muiWindowGlue_RequestPaste(app->glue) == mui_success,
                       "a paste asked for");
    }
    if (changedOut != NULL)
    {
        *changedOut = outcome.changed;
    }
    return outcome.handled;
}

void SamplePaintEditing(SampleApp* app, muiNodeId node, float width, muiDrawSink* sink,
                        const uint8_t caret[3])
{
    uint64_t key = muiNode_GetHostKey(app->context, node);
    const muiTextBlockId block = {(uint32_t)key, (uint32_t)(key >> 32)};
    muiTextSelection selection;
    if (muiTextBlock_GetSelection(app->text, block, &selection) != mui_success)
    {
        return;
    }
    uint32_t at = selection.caret.offset;
    uint32_t start = selection.anchor < at ? selection.anchor : at;
    uint32_t end = selection.anchor > at ? selection.anchor : at;
    muiRect rects[8];
    uint32_t count = 0;
    if (start != end &&
        muiTextGetRangeRects(&app->host, node, width, start, end, rects, 8, &count) == mui_success)
    {
        for (uint32_t i = 0; i < count && i < 8; i++)
        {
            (void)muiDrawSink_AddRect(sink, rects[i], (muiColor){0.2f, 0.45f, 0.9f, 0.5f});
        }
    }
    muiTextCaret bar;
    if (muiTextGetCaret(&app->host, node, width, selection.caret, &bar) == mui_success)
    {
        const muiRect rect = {bar.x, bar.y, 2.0f, bar.height};
        (void)muiDrawSink_AddRect(sink, rect, SampleColor(caret));
    }
}

// Hands the window's records to the glue and the access, as a host does
// each frame, and notes a request to close.
static void Drain(SampleApp* app)
{
    mwinEvent event;
    while (mwinNextEvent(app->windows, &event) == mwin_success)
    {
        TakePaste(app, &event);
        if (app->def->record != NULL)
        {
            app->def->record(app->def->user, app, &event);
        }
        bool handled = false;
        SampleAppCheck(app, muiWindowGlue_HandleEvent(app->glue, &event, &handled) == mui_success,
                       "a record taken");
        SampleAppCheck(app,
                       app->access == NULL ||
                           muiWindowAccess_HandleEvent(app->access, &event) == mui_success,
                       "a record seen by the access");
        app->closing = app->closing || event.type == mwin_eventCloseRequested;
        app->now = event.timeNs > app->now ? event.timeNs : app->now;
    }
}

// The window's surface, made once the platform has made the window and
// configured again as its size in pixels changes; let go while the
// window has none. Whether there is one to draw on.
static bool Surface(SampleApp* app)
{
    mwinWindowState state;
    if (mwinGetWindowState(app->windows, app->window, &state) != mwin_success || !state.created ||
        state.surfaceLost)
    {
        SampleSurfaceClose(&app->sample, &app->surface);
        return false;
    }
    uint32_t width = state.pixelSize.width;
    uint32_t height = state.pixelSize.height;
    if (width == 0 || height == 0)
    {
        return false;
    }
    if (app->surface.surface.index1 == 0)
    {
        mwinNativeHandles handles;
        if (!SampleAppCheck(
                app, mwinGetNativeHandles(app->windows, app->window, &handles) == mwin_success,
                "the window's handles") ||
            !SampleAppCheck(app,
                            SampleSurfaceOpen(&app->sample, &handles, width, height, &app->surface),
                            "a surface") ||
            (app->renderer == NULL && !MakeRenderer(app, app->surface.drawFormat)))
        {
            app->closing = true;
            return false;
        }
    }
    else if (width != app->surface.config.width || height != app->surface.config.height)
    {
        SampleAppCheck(app, SampleSurfaceResize(&app->sample, &app->surface, width, height),
                       "the surface resized");
    }
    return Size(app, width, height, state.scale);
}

// One frame: the records, the access once the window has its surface
// (which its first records bring), layout at now, the accessibility
// tree's changes, the list painted and drawn, into a texture headless,
// else onto the window's surface once the renderer is ready. Whether it
// was drawn.
static bool Step(SampleApp* app)
{
    Drain(app);
    if (app->access == NULL)
    {
        muiWindowAccessDef access = muiDefaultWindowAccessDef();
        access.glue = app->glue;
        SampleAppCheck(app, muiCreateWindowAccess(&access, &app->access) == mui_success,
                       "an access");
    }
    if (app->def->update != NULL)
    {
        app->def->update(app->def->user, app);
    }
    if (!app->headless && (!Surface(app) || !SamplePump(&app->sample, app->renderer)))
    {
        return false;
    }
    const muiLayoutInput layout = {
        (float)app->def->width, (float)app->def->height, muiMeasureText, &app->host, app->now, NULL,
        SafeAreaOf(app)};
    const muiDrawInput draw = {1, app->scale,
                               app->def->paint != NULL ? app->def->paint : muiPaintText,
                               app->def->paint != NULL ? app->def->user : (void*)&app->host};
    muiDrawList list;
    bool built = muiComputeLayout(app->context, app->root, &layout) == mui_success;
    for (int pass = 0;
         built && pass < 2 && app->def->settle != NULL && app->def->settle(app->def->user, app);
         pass++)
    {
        built = muiComputeLayout(app->context, app->root, &layout) == mui_success;
    }
    built = built && muiWindowAccess_Update(app->access) == mui_success &&
            muiBuildDrawList(app->context, app->root, &draw) == mui_success &&
            muiGetDrawList(app->context, &list) == mui_success;
    if (app->headless)
    {
        SampleAppCheck(app,
                       built && SampleRender(&app->sample, app->renderer, &list, app->pixelWidth,
                                             app->pixelHeight, app->pixels),
                       "a frame drawn");
        return true;
    }
    SamplePresented presented =
        built ? SamplePresent(&app->sample, &app->surface, app->renderer, &list, app->reading)
              : sample_failed;
    SampleAppCheck(app, presented != sample_failed, "a frame presented");
    app->closing = app->closing || presented == sample_lost_device;
    return presented == sample_presented;
}

const uint8_t* SamplePixelOf(const SampleApp* app, muiNodeId node, float x, float y)
{
    float rootX = 0.0f;
    float rootY = 0.0f;
    if (muiNode_MapToRoot(app->context, node, x, y, &rootX, &rootY) != mui_success)
    {
        rootX = -1.0f;
    }
    // Device pixels, at the frame's scale.
    float pixelX = rootX * app->scale;
    float pixelY = rootY * app->scale;
    if (app->pixels == NULL || pixelX < 0.0f || pixelY < 0.0f || pixelX >= (float)app->pixelWidth ||
        pixelY >= (float)app->pixelHeight)
    {
        static const uint8_t none[4] = {0, 0, 0, 0};
        return none;
    }
    return &app->pixels[((size_t)pixelY * app->pixelWidth + (size_t)pixelX) * 4];
}

bool SampleNear(const uint8_t* pixel, const uint8_t rgb[3])
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

// Posts a record, stamped now.
static void PostRecord(SampleApp* app, const mwinEvent* event)
{
    SampleAppCheck(app, mwinTestPost(app->windows, event) == mwin_success, "a record posted");
}

void SamplePost(SampleApp* app, mwinEventType type, muiNodeId node, uint8_t buttons)
{
    float x = 0.0f;
    float y = 0.0f;
    muiRect rect = muiNode_GetRect(app->context, node);
    SampleAppCheck(app,
                   muiNode_MapToRoot(app->context, node, rect.width / 2.0f, rect.height / 2.0f, &x,
                                     &y) == mui_success,
                   "a point");
    SamplePostAt(app, type, x, y, buttons);
}

void SamplePostAt(SampleApp* app, mwinEventType type, float x, float y, uint8_t buttons)
{
    SamplePostButton(app, type, x, y, mwin_buttonLeft, buttons);
}

void SamplePostButton(SampleApp* app, mwinEventType type, float x, float y, mwinMouseButton button,
                      uint8_t buttons)
{
    mwinEvent event = {.type = type, .window = app->window};
    event.data.pointer.position = (mwinPosition){x, y};
    event.data.pointer.button = type == mwin_eventCursorMoved ? 0 : button;
    event.data.pointer.buttons = buttons;
    PostRecord(app, &event);
}

void SamplePostWheel(SampleApp* app, muiNodeId node, float detents)
{
    SamplePost(app, mwin_eventCursorMoved, node, 0);
    mwinEvent wheel = {.type = mwin_eventWheel, .window = app->window};
    wheel.data.wheel.y = detents;
    PostRecord(app, &wheel);
}

void SamplePostKey(SampleApp* app, mwinKeyCode code, mwinKey key, const char* text)
{
    mwinEvent down = {.type = mwin_eventKeyDown, .window = app->window};
    down.data.key = (mwinKeyEvent){.code = code, .key = key};
    PostRecord(app, &down);
    if (text != NULL)
    {
        mwinEvent typed = {.type = mwin_eventTextInput, .window = app->window};
        typed.data.text = (mwinTextEvent){text, (uint32_t)strlen(text)};
        PostRecord(app, &typed);
    }
    mwinEvent up = down;
    up.type = mwin_eventKeyUp;
    PostRecord(app, &up);
}

// Headless: a step a frame on the records' clock, a second unless the
// sample asks another; the sample checks each frame and posts the next
// input.
static mwinFrameResult Scripted(SampleApp* app)
{
    app->now += app->def->frameNs != 0 ? app->def->frameNs : 1000000000u;
    SampleAppCheck(app, mwinTestSetTime(app->windows, app->now) == mwin_success, "the clock");
    Step(app);
    bool going = app->def->script(app->def->user, app, app->frame++);
    return going && app->frame < 64 ? mwin_frameContinue : mwin_frameStop;
}

// Now on the monotonic clock the records are stamped on, where the C
// library has it; the newest record's time otherwise.
static uint64_t Now(const SampleApp* app)
{
#ifdef TIME_MONOTONIC
    struct timespec now = {0};
    if (timespec_get(&now, TIME_MONOTONIC) == TIME_MONOTONIC)
    {
        uint64_t ns = (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
        return ns > app->now ? ns : app->now;
    }
#endif
    return app->now;
}

// Windowed, after the last frame asked for: its image taken once the
// device answers, a frame or more later, and handed to the still check.
static mwinFrameResult Taken(SampleApp* app)
{
    (void)SamplePump(&app->sample, app->renderer);
    SampleTaken taken = SampleTakeFrame(&app->sample, &app->surface, app->pixels,
                                        (size_t)app->pixelWidth * app->pixelHeight * 4);
    if (taken == sample_waiting && ++app->waits < 600)
    {
        return mwin_frameContinue;
    }
    if (SampleAppCheck(app, taken == sample_taken, "the presented frame read back"))
    {
        app->def->still(app->def->user, app);
        printf("the presented frame checked\n");
    }
    return mwin_frameStop;
}

// Windowed: frames presented as the user uses the window, until it is
// closed, or for the frames asked, the last read back where the surface
// allows and handed to the still check. Nothing here waits, as the
// browser's frames may not.
static mwinFrameResult Shown(SampleApp* app)
{
    app->now = Now(app);
    if (app->surface.reading)
    {
        return Taken(app);
    }
    app->reading = app->frames != 0 && app->frame == app->frames - 1 && app->def->still != NULL;
    if (Step(app))
    {
        app->frame++;
        if (app->reading && app->surface.reading)
        {
            return mwin_frameContinue;
        }
        if (app->reading)
        {
            printf("the surface allows no copies: the presented frame not checked\n");
        }
    }
    bool done = app->closing || (app->frames != 0 && app->frame >= app->frames);
    return done ? mwin_frameStop : mwin_frameContinue;
}

static mwinFrameResult Frame(mwinContext* windows, void* user)
{
    (void)windows;
    SampleApp* app = user;
    return app->headless ? Scripted(app) : Shown(app);
}

// The arguments: --headless, and --frames N for a window shown N frames.
static bool Arguments(SampleApp* app, int count, char** arguments)
{
    for (int i = 1; i < count; i++)
    {
        if (strcmp(arguments[i], "--headless") == 0)
        {
            app->headless = true;
        }
        else if (strcmp(arguments[i], "--frames") == 0 && i + 1 < count)
        {
            app->frames = atoi(arguments[++i]);
        }
        else
        {
            printf("usage: %s [--headless] [--frames N]\n", arguments[0]);
            return false;
        }
    }
    return app->frames >= 0;
}

// Liberation Sans, the text service's default font.
static bool MakeText(SampleApp* app)
{
    muiTextServiceDef def = muiDefaultTextServiceDef();
    muiFontDef font = muiDefaultFontDef();
    font.data = s_sans;
    font.size = sizeof s_sans;
    font.dataMode = mui_fontDataBorrow;
    muiFontId sans = {0};
    return muiCreateTextService(&def, &app->text) == mui_success &&
           muiCreateFont(app->text, &font, &sans) == mui_success &&
           muiSetDefaultFont(app->text, sans) == mui_success;
}

// Lets go of what lives on the window while it still exists: a surface
// must not outlive its window, whose connection the driver's swapchain
// may still use. Once.
static void Release(SampleApp* app)
{
    if (app->released)
    {
        return;
    }
    app->released = true;
    if (app->def->finish != NULL && app->sample.device != NULL)
    {
        app->def->finish(app->def->user, app);
    }
    muiDestroyRhiRenderer(app->renderer);
    app->renderer = NULL;
    SampleSurfaceClose(&app->sample, &app->surface);
    muiDestroyWindowAccess(app->access);
    app->access = NULL;
    muiDestroyWindowGlue(app->glue);
    app->glue = NULL;
}

static void Destroy(SampleApp* app);

// Called by Maul Window before it lets go of the window. In a browser
// mwinRun does not return, the page owning the loop: the program ends
// here.
static void Quit(mwinContext* windows, mwinResult status, void* user)
{
    (void)windows;
    (void)status;
    SampleApp* app = user;
    Release(app);
#ifdef __EMSCRIPTEN__
    if (!app->headless)
    {
        SampleAppCheck(app, app->frame > 0, "the program ran");
        Destroy(app);
        (void)SampleExit(SampleClose(&app->sample));
    }
#endif
}

static void Destroy(SampleApp* app)
{
    Release(app);
    muiDestroyContext(app->context);
    muiDestroyTextService(app->text);
    free(app->pixels);
}

int SampleRunApp(const SampleAppDef* def, int count, char** arguments)
{
    static SampleApp app;
    app = (SampleApp){.def = def};
    if (!Arguments(&app, count, arguments))
    {
        return SampleExit(1);
    }
    int status = SampleOpen(&app.sample);
    if (status != 0)
    {
        return SampleExit(status);
    }
    if (SampleAppCheck(&app, MakeText(&app), "the text service"))
    {
        mwinAppDef run = mwinDefaultAppDef();
        run.context.backend = app.headless ? mwin_backendTest : mwin_backendNative;
        run.init = Init;
        run.frame = Frame;
        run.quit = Quit;
        run.user = &app;
        SampleAppCheck(&app, mwinRun(&run) == mwin_success && app.frame > 0, "the program ran");
    }
    Destroy(&app);
    return SampleExit(SampleClose(&app.sample));
}

bool SampleSame(muiNodeId a, muiNodeId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

void SampleRound(SampleApp* app, muiNodeId node, float radius)
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
