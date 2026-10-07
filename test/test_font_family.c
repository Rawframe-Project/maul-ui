// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Font families (record mui-0006): faces matched as CSS Fonts 4 matches
// them, by width, then style, then weight, variable faces by their axes'
// ranges; text laid out in the face chosen; faces gone passed over; and
// calls outside the contract refused. Faces are Ahem with the OS/2
// weight, width and selection a test asks for written in.

#include "test_harness.h"

#include "maul-ui/context.h"
#include "maul-ui/draw.h"
#include "maul-ui/font.h"
#include "maul-ui/layout.h"
#include "maul-ui/node.h"
#include "maul-ui/text.h"
#include "maul-ui/text_block.h"
#include "maul-ui/text_style.h"

#include <stddef.h>
#include <string.h>

#include "ahem.inc"
#include "variable.inc"
#include "variable_italic.inc"
#include "variable_slant.inc"

enum
{
    // OS/2 fsSelection: italic, regular, oblique.
    ITALIC = 0x01,
    REGULAR = 0x40,
    OBLIQUE = 0x200
};

static unsigned char s_copy[sizeof s_ahem];

static uint32_t Read32(const unsigned char* bytes)
{
    return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 | (uint32_t)bytes[2] << 8 | bytes[3];
}

static void Write16(unsigned char* bytes, uint32_t value)
{
    bytes[0] = (unsigned char)(value >> 8);
    bytes[1] = (unsigned char)value;
}

// Makes a face of a font, which fits s_copy, with an OS/2 weight class,
// width class and selection.
static muiFontId FaceOf(muiTextService* service, const unsigned char* data, size_t size,
                        uint32_t weight, uint32_t width, uint32_t selection)
{
    memcpy(s_copy, data, size);
    uint32_t tables = (uint32_t)s_copy[4] << 8 | s_copy[5];
    for (uint32_t i = 0; i < tables; i++)
    {
        unsigned char* record = s_copy + 12 + 16 * i;
        if (memcmp(record, "OS/2", 4) == 0)
        {
            unsigned char* os2 = s_copy + Read32(record + 8);
            Write16(os2 + 4, weight);
            Write16(os2 + 6, width);
            Write16(os2 + 62, selection);
        }
    }
    muiFontDef def = muiDefaultFontDef();
    def.data = s_copy;
    def.size = size;
    muiFontId font = {0, 0};
    CHECK(muiCreateFont(service, &def, &font) == mui_success, "a face");
    return font;
}

static muiFontId Face(muiTextService* service, uint32_t weight, uint32_t width, uint32_t selection)
{
    return FaceOf(service, s_ahem, sizeof s_ahem, weight, width, selection);
}

static muiFontFamilyId Family(muiTextService* service, const muiFontId* faces, uint32_t count)
{
    muiFontFamilyDef def = muiDefaultFontFamilyDef();
    def.faces = faces;
    def.faceCount = count;
    muiFontFamilyId family = {0, 0};
    CHECK(muiCreateFontFamily(service, &def, &family) == mui_success, "a family");
    return family;
}

static bool Same(muiFontId a, muiFontId b)
{
    return a.index1 == b.index1 && a.generation == b.generation;
}

// Whether a weight and slant choose a face.
static bool Chooses(muiTextService* service, muiFontFamilyId family, float weight,
                    muiFontSlant slant, muiFontId face)
{
    muiFontId chosen = {0, 0};
    return muiFontFamily_MatchFace(service, family, weight, slant, &chosen) == mui_success &&
           Same(chosen, face);
}

static muiTextService* MakeService(void)
{
    muiTextServiceDef def = muiDefaultTextServiceDef();
    muiTextService* service = NULL;
    CHECK(muiCreateTextService(&def, &service) == mui_success, "service");
    return service;
}

static void TestWeights(void)
{
    muiTextService* service = MakeService();
    muiFontId light = Face(service, 300, 5, REGULAR);
    muiFontId regular = Face(service, 400, 5, REGULAR);
    muiFontId medium = Face(service, 500, 5, REGULAR);
    muiFontId bold = Face(service, 700, 5, REGULAR);
    muiFontFamilyId all = Family(service, (muiFontId[]){bold, light, medium, regular}, 4);
    CHECK(Chooses(service, all, 400.0f, mui_slantNormal, regular) &&
              Chooses(service, all, 700.0f, mui_slantNormal, bold),
          "the weights asked for");
    CHECK(Chooses(service, all, 450.0f, mui_slantNormal, medium),
          "from 400 to 500: heavier up to 500 first");
    CHECK(Chooses(service, all, 600.0f, mui_slantNormal, bold), "above 500: heavier first");
    CHECK(Chooses(service, all, 350.0f, mui_slantNormal, light), "below 400: lighter first");
    muiFontFamilyId noMedium = Family(service, (muiFontId[]){light, regular, bold}, 3);
    CHECK(Chooses(service, noMedium, 450.0f, mui_slantNormal, regular) &&
              Chooses(service, noMedium, 500.0f, mui_slantNormal, regular),
          "then lighter, before heavier than 500");
    muiFontFamilyId twoLight = Family(service, (muiFontId[]){light, regular}, 2);
    CHECK(Chooses(service, twoLight, 600.0f, mui_slantNormal, regular) &&
              Chooses(service, twoLight, 900.0f, mui_slantNormal, regular),
          "above 500 with nothing heavier: the heaviest lighter");
    muiFontFamilyId twoHeavy = Family(service, (muiFontId[]){bold, medium}, 2);
    CHECK(Chooses(service, twoHeavy, 100.0f, mui_slantNormal, medium) &&
              Chooses(service, twoHeavy, 420.0f, mui_slantNormal, medium),
          "below with nothing lighter: the lightest heavier");
    muiFontId again = Face(service, 400, 5, REGULAR);
    muiFontFamilyId twins = Family(service, (muiFontId[]){again, regular}, 2);
    CHECK(Chooses(service, twins, 400.0f, mui_slantNormal, again), "ties to the earlier");
    muiDestroyTextService(service);
}

static void TestStylesAndWidths(void)
{
    muiTextService* service = MakeService();
    muiFontId upright = Face(service, 400, 5, REGULAR);
    muiFontId italic = Face(service, 400, 5, ITALIC);
    muiFontId oblique = Face(service, 400, 5, OBLIQUE);
    muiFontFamilyId pair = Family(service, (muiFontId[]){upright, italic}, 2);
    CHECK(Chooses(service, pair, 400.0f, mui_slantItalic, italic) &&
              Chooses(service, pair, 400.0f, mui_slantNormal, upright) &&
              Chooses(service, pair, 400.0f, mui_slantOblique, italic),
          "italic for italic and for oblique without an oblique face");
    muiFontFamilyId leaning = Family(service, (muiFontId[]){upright, oblique}, 2);
    CHECK(Chooses(service, leaning, 400.0f, mui_slantItalic, oblique),
          "oblique for italic without an italic face");
    muiFontFamilyId slanted = Family(service, (muiFontId[]){italic, oblique}, 2);
    CHECK(Chooses(service, slanted, 400.0f, mui_slantNormal, oblique),
          "oblique, then italic, for normal without a normal face");
    // Style before weight: a bold italic beats a regular upright.
    muiFontId boldItalic = Face(service, 700, 5, ITALIC);
    muiFontFamilyId mixed = Family(service, (muiFontId[]){upright, boldItalic}, 2);
    CHECK(Chooses(service, mixed, 400.0f, mui_slantItalic, boldItalic),
          "the style narrows before the weight");
    // Width before style: normal width wins over a condensed italic.
    muiFontId condensedItalic = Face(service, 400, 3, ITALIC);
    muiFontId condensed = Face(service, 400, 3, REGULAR);
    muiFontId expanded = Face(service, 400, 7, REGULAR);
    muiFontFamilyId widths = Family(service, (muiFontId[]){condensedItalic, upright}, 2);
    CHECK(Chooses(service, widths, 400.0f, mui_slantItalic, upright),
          "the width narrows before the style");
    muiFontFamilyId noNormal = Family(service, (muiFontId[]){expanded, condensed}, 2);
    CHECK(Chooses(service, noNormal, 400.0f, mui_slantNormal, condensed),
          "narrower before wider without a normal width");
    muiFontId semiExpanded = Face(service, 400, 6, REGULAR);
    muiFontFamilyId wider = Family(service, (muiFontId[]){expanded, semiExpanded}, 2);
    CHECK(Chooses(service, wider, 400.0f, mui_slantNormal, semiExpanded), "the nearest wider");
    muiDestroyTextService(service);
}

static void TestVariableFaces(void)
{
    muiTextService* service = MakeService();
    muiFontId regular = Face(service, 400, 5, REGULAR);
    muiFontDef def = muiDefaultFontDef();
    def.data = s_variable;
    def.size = sizeof s_variable;
    muiFontId variable = {0, 0};
    CHECK(muiCreateFont(service, &def, &variable) == mui_success, "variable");
    muiFontFamilyId family = Family(service, (muiFontId[]){regular, variable}, 2);
    CHECK(Chooses(service, family, 400.0f, mui_slantNormal, regular) &&
              Chooses(service, family, 650.0f, mui_slantNormal, variable),
          "a variable face reaches every weight of its axis");
    CHECK(Chooses(service, family, 400.0f, mui_slantItalic, variable) &&
              Chooses(service, family, 400.0f, mui_slantOblique, variable),
          "and its ital and slnt make it italic and oblique");
    muiFontId bold = Face(service, 700, 5, REGULAR);
    muiFontFamilyId heavy = Family(service, (muiFontId[]){bold, variable}, 2);
    CHECK(Chooses(service, heavy, 650.0f, mui_slantNormal, variable),
          "a weight within the axis before a nearer face without it");
    // An axis alone makes the face italic or oblique.
    muiFontId italAlone =
        FaceOf(service, s_variableItalic, sizeof s_variableItalic, 400, 5, REGULAR);
    muiFontId slntAlone = FaceOf(service, s_variableSlant, sizeof s_variableSlant, 400, 5, REGULAR);
    muiFontFamilyId withItal = Family(service, (muiFontId[]){regular, italAlone}, 2);
    muiFontFamilyId withSlnt = Family(service, (muiFontId[]){regular, slntAlone}, 2);
    CHECK(Chooses(service, withItal, 400.0f, mui_slantItalic, italAlone) &&
              Chooses(service, withSlnt, 400.0f, mui_slantOblique, slntAlone),
          "ital for italic, slnt for oblique");
    // And reaching 0 makes a face marked italic or oblique upright too.
    muiFontId italic = Face(service, 400, 5, ITALIC);
    muiFontId oblique = Face(service, 400, 5, OBLIQUE);
    muiFontId markedItal =
        FaceOf(service, s_variableItalic, sizeof s_variableItalic, 400, 5, ITALIC);
    muiFontId markedSlnt =
        FaceOf(service, s_variableSlant, sizeof s_variableSlant, 400, 5, OBLIQUE);
    muiFontFamilyId uprightItal = Family(service, (muiFontId[]){italic, markedItal}, 2);
    muiFontFamilyId uprightSlnt = Family(service, (muiFontId[]){oblique, markedSlnt}, 2);
    CHECK(Chooses(service, uprightItal, 400.0f, mui_slantNormal, markedItal) &&
              Chooses(service, uprightSlnt, 400.0f, mui_slantNormal, markedSlnt),
          "ital or slnt reaching 0 for normal");
    muiDestroyTextService(service);
}

static void TestLayout(void)
{
    muiTextService* service = MakeService();
    muiFontId regular = Face(service, 400, 5, REGULAR);
    muiFontId bold = Face(service, 700, 5, REGULAR);
    muiFontFamilyId family = Family(service, (muiFontId[]){regular, bold}, 2);
    muiFontFamilyId lone = Family(service, (muiFontId[]){regular}, 1);
    muiContextDef contextDef = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&contextDef, &context) == mui_success, "context");
    muiTextHost host = {service, context};
    muiTextBlockId block = {0, 0};
    CHECK(muiCreateTextBlock(service, "X", 1, &block) == mui_success, "block");
    muiNodeDef nodeDef = muiDefaultNodeDef();
    nodeDef.hostKey = muiTextBlock_GetKey(block);
    muiNodeId node = {0, 0};
    CHECK(muiCreateNode(context, &nodeDef, &node) == mui_success, "node");
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.content = mui_contentHost;
    CHECK(muiNode_SetLayoutValues(context, node, &layout, MUI_PROPERTY_BIT(mui_propertyContent)) ==
              mui_success,
          "host content");
    const muiLayoutInput input = {1000.0f, 1000.0f, muiMeasureText, &host, 0, NULL, {0, 0, 0, 0}};
    const muiDrawInput draw = {1, 1.0f, muiPaintText, &host};
    const muiPropertyMask mask =
        MUI_PROPERTY_BIT(mui_propertyFont) | MUI_PROPERTY_BIT(mui_propertyFontWeight);
    muiTextStyle style = muiDefaultTextStyle();
    style.font = muiFontFamily_GetKey(family);
    style.weight = 700.0f;
    muiDrawList list;
    CHECK(muiNode_SetTextValues(context, node, &style, mask) == mui_success &&
              muiComputeLayout(context, node, &input) == mui_success &&
              muiBuildDrawList(context, node, &draw) == mui_success &&
              muiGetDrawList(context, &list) == mui_success && list.commandCount == 1 &&
              list.commands[0].glyphRun.font == muiFont_GetKey(bold),
          "text in the face the weight chooses, as it is");
    style.font = muiFontFamily_GetKey(lone);
    CHECK(muiNode_SetTextValues(context, node, &style, mask) == mui_success &&
              muiComputeLayout(context, node, &input) == mui_success &&
              muiBuildDrawList(context, node, &draw) == mui_success &&
              muiGetDrawList(context, &list) == mui_success && list.commandCount == 1 &&
              (list.commands[0].glyphRun.font & 0xFFFFFFFFFFull) == muiFont_GetKey(regular) &&
              list.commands[0].glyphRun.font != muiFont_GetKey(regular),
          "a family without bold: its regular face, made bold");
    // Faces gone are passed over; a family with none left draws nothing.
    style.font = muiFontFamily_GetKey(family);
    CHECK(muiDestroyFont(service, bold) == mui_success &&
              muiNode_SetTextValues(context, node, &style, mask) == mui_success &&
              muiComputeLayout(context, node, &input) == mui_success &&
              muiBuildDrawList(context, node, &draw) == mui_success &&
              muiGetDrawList(context, &list) == mui_success && list.commandCount == 1 &&
              (list.commands[0].glyphRun.font & 0xFFFFFFFFFFull) == muiFont_GetKey(regular),
          "a face gone is passed over");
    CHECK(muiDestroyFont(service, regular) == mui_success &&
              muiNode_MarkContentChanged(context, node) == mui_success &&
              muiComputeLayout(context, node, &input) == mui_success &&
              muiBuildDrawList(context, node, &draw) == mui_success &&
              muiGetDrawList(context, &list) == mui_success && list.commandCount == 0,
          "no face left: nothing drawn");
    muiFontId face = {0, 0};
    CHECK(muiFontFamily_MatchFace(service, family, 400.0f, mui_slantNormal, &face) ==
              mui_errorStale,
          "no face left to match");
    // A family key with more bits than a family's names none.
    muiFontId kept = Face(service, 400, 5, REGULAR);
    muiFontFamilyId keptFamily = Family(service, (muiFontId[]){kept}, 1);
    style.font = muiFontFamily_GetKey(keptFamily) | 1ull << 40;
    CHECK(muiNode_SetTextValues(context, node, &style, mask) == mui_success &&
              muiComputeLayout(context, node, &input) == mui_success &&
              muiBuildDrawList(context, node, &draw) == mui_success &&
              muiGetDrawList(context, &list) == mui_success && list.commandCount == 0,
          "a family key with an instance names nothing");
    // A family gone draws nothing, though its key's slot is a font's too.
    muiFontId other = Face(service, 400, 5, REGULAR);
    muiFontFamilyId gone = Family(service, (muiFontId[]){other}, 1);
    style.font = muiFontFamily_GetKey(gone);
    CHECK(muiDestroyFontFamily(service, gone) == mui_success &&
              muiNode_SetTextValues(context, node, &style, mask) == mui_success &&
              muiComputeLayout(context, node, &input) == mui_success &&
              muiBuildDrawList(context, node, &draw) == mui_success &&
              muiGetDrawList(context, &list) == mui_success && list.commandCount == 0,
          "a family gone draws nothing");
    muiDestroyContext(context);
    muiDestroyTextService(service);
}

static void TestKeysApart(void)
{
    // A font and a family in the same slot with the same generation.
    muiTextService* service = MakeService();
    muiFontId face = Face(service, 400, 5, REGULAR);
    muiFontFamilyId family = Family(service, &face, 1);
    CHECK(face.index1 == family.index1 && face.generation == family.generation, "same slots");
    muiContextDef contextDef = muiDefaultContextDef();
    muiContext* context = NULL;
    CHECK(muiCreateContext(&contextDef, &context) == mui_success, "context");
    muiTextHost host = {service, context};
    muiTextBlockId block = {0, 0};
    CHECK(muiCreateTextBlock(service, "X", 1, &block) == mui_success, "block");
    muiNodeDef nodeDef = muiDefaultNodeDef();
    nodeDef.hostKey = muiTextBlock_GetKey(block);
    muiNodeId node = {0, 0};
    CHECK(muiCreateNode(context, &nodeDef, &node) == mui_success, "node");
    muiLayoutStyle layout = muiDefaultLayoutStyle();
    layout.content = mui_contentHost;
    muiTextStyle style = muiDefaultTextStyle();
    style.font = muiFontFamily_GetKey(family);
    const muiLayoutInput input = {1000.0f, 1000.0f, muiMeasureText, &host, 0, NULL, {0, 0, 0, 0}};
    const muiDrawInput draw = {1, 1.0f, muiPaintText, &host};
    muiDrawList list;
    CHECK(muiNode_SetLayoutValues(context, node, &layout, MUI_PROPERTY_BIT(mui_propertyContent)) ==
                  mui_success &&
              muiNode_SetTextValues(context, node, &style, MUI_PROPERTY_BIT(mui_propertyFont)) ==
                  mui_success &&
              muiDestroyFontFamily(service, family) == mui_success &&
              muiComputeLayout(context, node, &input) == mui_success &&
              muiBuildDrawList(context, node, &draw) == mui_success &&
              muiGetDrawList(context, &list) == mui_success && list.commandCount == 0,
          "a family gone is not the font in its slot");
    muiDestroyContext(context);
    muiDestroyTextService(service);
}

static void TestContract(void)
{
    muiTextServiceDef serviceDef = muiDefaultTextServiceDef();
    CHECK(serviceDef.limits.fontFamilies == 16, "16 families by default");
    serviceDef.limits.fontFamilies = 1;
    muiTextService* service = NULL;
    CHECK(muiCreateTextService(&serviceDef, &service) == mui_success, "service");
    muiFontId face = Face(service, 400, 5, REGULAR);
    muiFontFamilyDef def = muiDefaultFontFamilyDef();
    def.faces = &face;
    def.faceCount = 1;
    muiFontFamilyId family = {7, 7};
    muiFontFamilyDef bad = def;
    bad.cookie = 0;
    CHECK(muiCreateFontFamily(NULL, &def, &family) == mui_errorInvalid && family.index1 == 0 &&
              muiCreateFontFamily(service, NULL, &family) == mui_errorInvalid &&
              muiCreateFontFamily(service, &def, NULL) == mui_errorInvalid &&
              muiCreateFontFamily(service, &bad, &family) == mui_errorInvalid,
          "arguments");
    bad = def;
    bad.faces = NULL;
    muiFontFamilyDef empty = def;
    empty.faceCount = 0;
    muiFontFamilyDef many = def;
    many.faceCount = 257;
    muiFontId dead = {9, 9};
    muiFontFamilyDef stale = def;
    stale.faces = &dead;
    CHECK(muiCreateFontFamily(service, &bad, &family) == mui_errorInvalid &&
              muiCreateFontFamily(service, &empty, &family) == mui_errorInvalid &&
              muiCreateFontFamily(service, &many, &family) == mui_errorInvalid &&
              muiCreateFontFamily(service, &stale, &family) == mui_errorStale,
          "faces");
    CHECK(muiCreateFontFamily(service, &def, &family) == mui_success &&
              muiFontFamily_IsValid(service, family),
          "made");
    muiFontFamilyId second = {0, 0};
    CHECK(muiCreateFontFamily(service, &def, &second) == mui_errorCapacity && second.index1 == 0,
          "the limit");
    muiFontId chosen = {0, 0};
    CHECK(muiFontFamily_MatchFace(NULL, family, 400.0f, 0, &chosen) == mui_errorInvalid &&
              muiFontFamily_MatchFace(service, family, 400.0f, 0, NULL) == mui_errorInvalid &&
              muiFontFamily_MatchFace(service, (muiFontFamilyId){0, 0}, 400.0f, 0, &chosen) ==
                  mui_errorInvalid &&
              muiFontFamily_MatchFace(service, family, 0.5f, 0, &chosen) == mui_errorInvalid &&
              muiFontFamily_MatchFace(service, family, 1001.0f, 0, &chosen) == mui_errorInvalid &&
              muiFontFamily_MatchFace(service, family, 400.0f, 3, &chosen) == mui_errorInvalid &&
              chosen.index1 == 0,
          "matching outside the contract");
    CHECK(muiFontFamily_GetKey(family) != 0 &&
              muiFontFamily_GetKey(family) != muiFont_GetKey(face) &&
              muiFontFamily_GetKey((muiFontFamilyId){0, 0}) == 0,
          "keys apart from fonts'");
    CHECK(muiDestroyFontFamily(service, family) == mui_success &&
              !muiFontFamily_IsValid(service, family) &&
              muiDestroyFontFamily(service, family) == mui_errorStale &&
              muiFontFamily_MatchFace(service, family, 400.0f, 0, &chosen) == mui_errorStale &&
              muiDestroyFontFamily(NULL, family) == mui_errorInvalid &&
              muiDestroyFontFamily(service, (muiFontFamilyId){0, 0}) == mui_errorInvalid &&
              !muiFontFamily_IsValid(NULL, family) &&
              !muiFontFamily_IsValid(service, (muiFontFamilyId){0, 0}),
          "destroyed");
    CHECK(muiCreateFontFamily(service, &def, &second) == mui_success, "its slot again");
    serviceDef.limits.fontFamilies = 65537;
    muiTextService* tooMany = NULL;
    CHECK(muiCreateTextService(&serviceDef, &tooMany) == mui_errorInvalid, "a family limit past");
    muiDestroyTextService(service);
}

int main(void)
{
    TestWeights();
    TestStylesAndWidths();
    TestVariableFaces();
    TestLayout();
    TestKeysApart();
    TestContract();
    return s_failures == 0 ? 0 : 1;
}
