// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A password's mask is held to a limit: a bullet of three bytes per
// grapheme cluster, made only while the bullets fit, never cut or wrapped
// to fit. The library's limit is MUI_MAX_TEXT_BYTES, whose third takes a
// text of 716 MB (test_text_mask_limit, labeled large); here the same
// bound is met with limits of a few bytes, at the last cluster that fits
// and at the first that does not.

#include "test_harness.h"
#include "text_blocks.h"
#include "text_mask.h"

#include "maul-ui/text.h"
#include "maul-ui/text_block.h"

#include <stdint.h>
#include <string.h>

// U+2022 BULLET in UTF-8.
static const char s_bullet[] = "\xE2\x80\xA2";

// The length of a text's mask under a limit, -1 for none; the text set
// first, so the mask is made again for it.
static int64_t MaskLength(muiTextService* service, muiTextBlockId id, const char* text,
                          uint32_t limit)
{
    CHECK(muiTextBlock_SetText(service, id, text, strlen(text)) == mui_success, "the text");
    muiTextBlock* block = muiResolveTextBlock(service, id);
    const muiTextBlock* mask = block != NULL ? muiMakeMask(service, block, limit) : NULL;
    if (mask == NULL)
    {
        return -1;
    }
    for (uint32_t at = 0; at + 3 <= mask->length; at += 3)
    {
        CHECK(memcmp((const char*)mask->text.data + at, s_bullet, 3) == 0, "a bullet");
    }
    return (int64_t)mask->length;
}

int main(void)
{
    muiTextServiceDef def = muiDefaultTextServiceDef();
    muiTextService* service = NULL;
    CHECK(muiCreateTextService(&def, &service) == mui_success, "service");
    muiTextBlockDef blockDef = muiDefaultTextBlockDef();
    muiTextBlockId id = {0, 0};
    CHECK(muiCreateTextBlock(service, &blockDef, &id) == mui_success, "block");

    // Four one-byte clusters take twelve bytes of bullets, five fifteen.
    CHECK(MaskLength(service, id, "abcd", 12) == 12, "four clusters within 12 bytes");
    CHECK(MaskLength(service, id, "abcd", 11) == -1, "four clusters past 11 bytes");
    CHECK(MaskLength(service, id, "abcde", 12) == -1, "five clusters past 12 bytes");
    CHECK(MaskLength(service, id, "abcde", 14) == -1, "five clusters past 14 bytes");
    CHECK(MaskLength(service, id, "abcde", 15) == 15, "five clusters within 15 bytes");
    // Clusters count, not bytes: four of three bytes each, a letter with
    // a combining acute, or one ideograph.
    CHECK(MaskLength(service, id,
                     "e\xCC\x81"
                     "e\xCC\x81"
                     "e\xCC\x81"
                     "e\xCC\x81",
                     12) == 12,
          "four clusters of three bytes within 12 bytes");
    CHECK(MaskLength(service, id, "\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2\xE6\xBC\xA2",
                     12) == -1,
          "five ideographs past 12 bytes");
    // No text takes no bullets, within any limit; one bullet takes three.
    CHECK(MaskLength(service, id, "", 0) == 0, "an empty mask within 0 bytes");
    CHECK(MaskLength(service, id, "a", 2) == -1, "one cluster past 2 bytes");
    // The library's own limit leaves short passwords as they were.
    CHECK(MaskLength(service, id, "abcd", MUI_MAX_TEXT_BYTES) == 12,
          "four clusters within the limit");

    muiDestroyTextService(service);
    return s_failures == 0 ? 0 : 1;
}
