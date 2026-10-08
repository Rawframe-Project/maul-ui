# Test fonts

Fonts the text component's tests read (record mui-0006), exactly as
published.

- `Ahem.ttf`: Ahem 1.50, from the Web Platform Tests,
  `https://github.com/web-platform-tests/wpt/blob/master/fonts/Ahem.ttf`,
  fetched 2026-10-01. Its name table: "The Ahem font belongs to the
  public domain. In jurisdictions that do not recognize public domain
  ownership of these files, the following Creative Commons Zero
  declaration applies"
  (`http://labs.creativecommons.org/licenses/zero-waive/1.0/us/legalcode`).
  1000 units per em, ascent 800 and descent 200, every glyph a box, so
  measurements have exact values.
- `LiberationSans-Regular.ttf`: Liberation Sans 2.1.5, from the
  release archive `liberation-fonts-ttf-2.1.5.tar.gz` (SHA-256
  `7191c669bf38899f73a2094ed00f7b800553364f90e2637010a69c0e268f25d0`)
  linked from
  `https://github.com/liberationfonts/liberation-fonts/releases/tag/2.1.5`,
  fetched 2026-10-01, unmodified. SIL Open Font License 1.1, in
  `LiberationSans-LICENSE.txt`; Reserved Font Name Liberation. 2048
  units per em, a line gap, and kerning in GPOS, so tests see real
  shaping.

- `MaulBreakTest.ttf`: written by `make_break_test_font.py` (fontTools
  4.60.1), MIT like the rest of this repository: boxes on 1000 units
  per em, a hyphen kerned -200 against V and a ligature of space and x,
  both across line break opportunities, so tests see shaping that
  changes when a line breaks, and a ligature of Hebrew alef and bet,
  one right-to-left glyph for two clusters. The script writes the same
  bytes each time.

- `MaulLargeGlyph.ttf`: written by `make_large_glyph_font.py`
  (fontTools 4.60.1), MIT: 16 units per em and an A 16 ems a side,
  too large to render at the largest size.

- `MaulOverlap.ttf`: written by `make_overlap_font.py` (fontTools
  4.60.1), MIT: an A of two overlapping boxes marked with the TrueType
  overlap flag, for distance fields of overlapping contours.

- `MaulCff.otf`: written by `make_cff_font.py` (fontTools 4.66.1), MIT:
  the same A in CFF outlines, so the CFF driver's unhinted field is held
  to the TrueType one.

- `MaulColor.ttf`: written by `make_color_font.py` (fontTools
  4.66.1), MIT: an A of COLR version 0 layers (one with no outline, a
  box of palette entry 0, its right half of entry 1, a small box of the
  text's colour and one of an entry past the palettes) over two CPAL
  palettes, and a B with no colour, for colour glyphs.

- `MaulVariable.ttf`, `MaulVariableSlant.ttf`, `MaulVariableItalic.ttf`
  and `MaulItalic.ttf`: written by `make_instance_fonts.py` (fontTools
  4.60.1), MIT: an A whose wght, ital, slnt and opsz axes move its sides
  and advance by known amounts; the first with all four axes, the
  second without ital and with wght stopping at 500 so bold is out of
  its reach, the third with ital alone, the last with none and its face
  marked italic, for font instances; and `MaulCoverage.ttf`, one box for
  a tab, a space, an A, a comma, a combining acute and U+4E00, for font
  fallback.

SHA-256 of each file:

```text
b719ecb31c5b21fc573c03f6421c74ac63c271a5a3ff841e34f9705fb94b8448  Ahem.ttf
76d04c18ea243f426b7de1f3ad208e927008f961dc5945e5aad352d0dfde8ee8  LiberationSans-Regular.ttf
5c1132f0c118d748d7717212d950f39e13475b3cbab2ba402d169e42c376a4a6  MaulBreakTest.ttf
54006ca29b1100a85e93568294e006596c0abf35d44e3dd01d4885b8aca85ee7  MaulCff.otf
665a276933f1b6948326ed2db09ea0ec19a1f4efc2c180dbed6f93282ade316f  MaulCoverage.ttf
433d1ea83dfbcbc4ec1379b7753dc0a778a2072f1d53cc19f2a6b98dc03f3ed3  MaulItalic.ttf
1a0b8e0c87b9cf3fd2b847c47f677cdbd2af980a3e5d2fb3fbfeb8133bbbcc8f  MaulLargeGlyph.ttf
a4e7b6f71982af76fa911502cac52db2e1290d4db5b64becc65d562f8afe1a63  MaulOverlap.ttf
4103c45d335e421ab154ad5ec2b9c962024ef1a9a023eed47c614abfd09b7446  MaulVariable.ttf
553014f54c138cb998407e2a3cb08c3b51a85e430ef09f311149e45342e22ea8  MaulVariableItalic.ttf
05289f6fbd722881c3d81dfb3564c932aa899b3a323ee7e2179f0959ec8124c7  MaulVariableSlant.ttf
```
