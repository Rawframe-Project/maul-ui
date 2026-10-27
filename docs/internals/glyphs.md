# Glyphs

The draw list carries glyph ids, not images, so a list never depends on a renderer's atlas ([Draw list](draw-list.md#commands)). The text component renders glyph images on request and packs them into atlases a renderer uploads. Everything here is the same bytes on every platform: FreeType rasterizes in integers, and the library's own rendering uses only correctly rounded float operations.

## Contents

- [Coverage](#coverage)
- [Distance fields](#distance-fields)
- [Multi-channel fields](#multi-channel-fields)
- [Colour glyphs](#colour-glyphs)
- [Atlases](#atlases)

## Coverage

`muiRenderGlyph` renders a glyph of a font key at an em in device pixels, and a pen offset right of a pixel boundary, into the caller's bytes as 8-bit linear coverage, and gives its size and bearing ([glyph_image.c](../../src/glyph_image.c)). A size it needs that the bytes lack is reported, not allocated. Outlines are unhinted and drawn without the font's embedded bitmaps, so images sit on the unhinted advances text is laid out with. LCD rendering is not drawn.

## Distance fields

`muiRenderGlyphField` renders a signed distance field that scales: 128 at the outline, changing by 128 / spread a pixel inside and out, reaching the spread past the outline ([distance_field.c](../../src/distance_field.c)). The library draws it itself rather than with FreeType's `sdf` module, which spends most of its time in fixed-point vector lengths and is about 30 times slower.

- The outline is flattened into segments within 1/32 pixel by Wang's formula ([flatten.c](../../src/flatten.c)).
- Each pixel's center is inside by nonzero winding along its row, or even-odd when the outline says so, and its distance is to the nearest part of a segment on the edge of the union of the contours.
- Overlapping contours, as variable fonts' and composite glyphs' are, make one shape. Segments are cut into pieces of at most a pixel and sorted into the cells their boxes' corners are in, and the places where pieces cross or touch are found among those in nearby cells. Between such places a contour is on the edge or within the union throughout, which the windings either side of it tell, so the edge is exact. A stray point or a contour without area is no edge.

## Multi-channel fields

`muiRenderGlyphMultiField` renders the same field with three more channels (MTSDF), after Chlumský's method and msdfgen ([multi_field.c](../../src/multi_field.c)). The union's edge is followed into loops across the places where it leaves one contour for another, and cut into edges where lines or curves meet turning past msdfgen's threshold of 3 radians. Edges are coloured as msdfgen colours them, so the two edges at a corner share one channel, and each colour channel holds the signed distance to the nearest edge of its colour. The median of the three keeps corners sharp at any scale, where the one-channel field, kept in alpha for outlines and shadows, rounds them. Where the median's side is not the pixel's, the three take the true distance.

`test_field_quality` measures the fields against FreeType's exact coverage at the drawn size, for Liberation Sans's printable ASCII drawn 1.5 to 6 times larger than the field's em. With a spread of 4 field pixels the median's mean error is 0.036 and 0.46% of edge pixels are off by more than a quarter, where the one-channel field's are 0.056 and 3.7%. A spread of an eighth of the em, tried first, measured 0.082 and 7.2%: a byte then steps by an eighth of a field pixel, which a magnified field turns into edges ramped and placed off by a third of a pixel and more.

## Colour glyphs

`muiRenderColorGlyph` renders a glyph's colour image into premultiplied pixels stored as an sRGB texture holds them, choosing the first form the font has ([color_glyph.c](../../src/color_glyph.c)):

- **COLR version 1.** The paint graph, read through FreeType, is drawn into a premultiplied linear surface over the glyph's clip box (one whose maximum is below its minimum is `mui_errorFormat`), transforms composed down from font units, layers composited source over (Porter and Duff), at most 64 paints deep, so a graph that paints itself is `mui_errorFormat` ([colr_paint.c](../../src/colr_paint.c)). Gradients take each pixel's centre back into font units: linear along p0 to p1 turned by p2, radial as CSS's two-point conical gradient, and sweep counter-clockwise from the x axis. Their colour lines pad, repeat or reflect and interpolate in premultiplied linear light, as CPAL asks. Composites take W3C Compositing and Blending Level 1's modes in linear light ([colr_composite.c](../../src/colr_composite.c)).
- **COLR version 0.** Each layer, another glyph's outline, is FreeType's coverage filled with its CPAL entry from the palette the caller names, or for entry 0xFFFF the text's colour, composited in order, source over. FreeType's own COLR rendering is not used because it takes no palette and no text colour.
- **Colour bitmaps.** A glyph from a CBLC and CBDT or an sbix table is read in place, from the smallest strike reaching the size, else the largest ([bitmap_glyph.c](../../src/bitmap_glyph.c)). Its PNG is decoded by the library's own decoder, written from the W3C PNG specification and RFC 1950 and 1951 for its zlib streams: every colour type, depth and interlacing, its checksums checked, since FreeType is built without libpng ([png.c](../../src/png.c), [inflate.c](../../src/inflate.c)). The image is scaled to the size in premultiplied linear light, 16 bytes a pixel, averaged when shrinking and bilinear when growing ([image_scale.c](../../src/image_scale.c)). A bitmap of 2^28 pixels or more, whose linear bytes would not fit 32 bits, is `mui_errorFormat` on every platform, so a 32-bit build computes no wrapped size and gives the same result as the others. FreeType opens faces ignoring sbix, so a font with sbix and outlines stays scalable, and an sbix glyph is placed from its outline box where it has contours, as OpenType says.

A font of bitmaps alone, without outlines, is accepted, its units per em read from its head table. A glyph without colour is `mui_empty` and is drawn as coverage.

## Atlases

A glyph atlas is an owner object of a service, in its memory ([glyph_atlas.c](../../src/glyph_atlas.c)). Its pages are of the caller's size, made as needed up to a limit and cut into plots, each packed with a skyline bottom-left, after Jylänki's survey of rectangle packing ([skyline.c](../../src/skyline.c)), each image inside a one-pixel empty gutter.

- A glyph is asked for with its pen and baseline in device pixels. The pen is taken to the nearest quarter pixel and the baseline to the nearest pixel, and the key is the font, glyph, size and quarter.
- When no plot has room, the least recently used plot the current frame does not use is emptied. Its entries go stale at once, as each names its plot's generation.
- The renderer marks frames, reads the pages' pixels, and takes each changed plot's rectangle to upload. The atlas uses no graphics API.
- Distance fields share the pages, keyed by font, glyph, size and spread without a pen: a field is placed at its own size and drawn at any size by scaling. The empty gutter reads as far outside, as a field's border does.
- An atlas's pages are of one format, set when it is made: one byte a pixel for coverage and fields, four for multi-channel fields, or sRGB for colour glyphs, which are keyed also by the palette and the text's colour. A renderer drawing several kinds makes an atlas of each, so pages of different formats never share an index space and each atlas uploads as textures of one format.
