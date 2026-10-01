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
  changes when a line breaks. The script writes the same bytes each
  time.

- `MaulLargeGlyph.ttf`: written by `make_large_glyph_font.py`
  (fontTools 4.60.1), MIT: 16 units per em and an A 16 ems a side,
  too large to render at the largest size.

SHA-256 of each file:

```text
b719ecb31c5b21fc573c03f6421c74ac63c271a5a3ff841e34f9705fb94b8448  Ahem.ttf
76d04c18ea243f426b7de1f3ad208e927008f961dc5945e5aad352d0dfde8ee8  LiberationSans-Regular.ttf
9874b4f1bf8f95c0c1ab5c810a89c1bca65ba666731b79a72de2eb1291d4b13a  MaulBreakTest.ttf
1a0b8e0c87b9cf3fd2b847c47f677cdbd2af980a3e5d2fb3fbfeb8133bbbcc8f  MaulLargeGlyph.ttf
```
