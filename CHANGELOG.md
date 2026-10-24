# Changelog

All notable changes to this project are recorded here, grouped as
Added, Changed, Fixed, Removed and Security. The project uses
[Semantic Versioning](https://semver.org/). Before 1.0.0, any release
may change the API, the ABI and every data format.

## [0.1.0](https://github.com/Rawframe-Project/maul-ui/compare/v0.0.1...v0.1.0) (2026-10-24)


### ⚠ BREAKING CHANGES

* muiAccessTreeDef, muiAtspiAppDef, muiAriaAdapterDef, muiAndroidAdapterDef, muiNsAdapterDef, muiUiaAdapterDef and muiUikitAdapterDef have a uint32_t version after their cookie, and a def built against other headers is refused with mui_errorVersion. The library no longer exports muiDefaultAccessTreeDef, muiDefaultAtspiAppDef, muiDefaultAriaAdapterDef, muiDefaultAndroidAdapterDef, muiDefaultNsAdapterDef, muiDefaultUiaAdapterDef and muiDefaultUikitAdapterDef: they are static inline in the headers, and programs must be rebuilt against these headers.
* **access:** muiCreateFont returns mui_errorFormat, not mui_errorInvalid, for font data shorter than a header or longer than 2^31 - 1 bytes. muiPerformAccessAction returns mui_errorFormat, not mui_errorInvalid, for a value or point that is not finite, and muiTextPerformAccessAction for offsets that are not character starts of the text or a block not editing; neither counts misuse for them. The NSAccessibility, Android and ARIA adapters no longer ask the host for a value that is not finite or lies outside the range.

### Fixed

* **access:** refuse bad untrusted data as a format error, no misuse ([#19](https://github.com/Rawframe-Project/maul-ui/issues/19)) ([e4bf505](https://github.com/Rawframe-Project/maul-ui/commit/e4bf505be8d99a5c740d43302f25e61ef93ca2b7))
* **layout:** end the flex loop when every clamp is within its slack ([#18](https://github.com/Rawframe-Project/maul-ui/issues/18)) ([b23665e](https://github.com/Rawframe-Project/maul-ui/commit/b23665e3060a659c1493c82aadeb803ff234cdb8)), closes [#17](https://github.com/Rawframe-Project/maul-ui/issues/17)
* stamp every root def with the ABI version ([#22](https://github.com/Rawframe-Project/maul-ui/issues/22)) ([871607d](https://github.com/Rawframe-Project/maul-ui/commit/871607d8295494d375fa4d5c1e1bf247d0a19c48))

## [0.0.1](https://github.com/Rawframe-Project/maul-ui/releases/tag/v0.0.1) (2026-10-23)

The first release: a retained UI core that lays out, styles, draws,
takes input and speaks to screen readers, the same on every platform.

### Added

- A context holding the interface as a tree of nodes, with ids that
  never name a destroyed node's successor, limits fixed at creation and
  no allocation during a frame.
- CSS flexbox layout with wrapping, gaps, automatic margins, aspect
  ratios, absolute placement with anchors, baseline alignment,
  right-to-left mirroring and safe-area padding, held to Chrome on over
  a thousand fixtures. A frame where nothing changed does no work.
- Style in fixed layers: classes and node types with state variants,
  conditions on a node's size and on the viewport, input modality, text
  scale and reduced motion, and values set on the node itself. Design
  tokens with aliases, themes scoped to subtrees, and transitions,
  timed or spring, that move colors in Oklab.
- A draw list of boxes with rounded corners, borders and gradients,
  shadows, nine-slice images and glyph runs, with clip chains and
  transforms a renderer can batch across, snapped to device pixels and
  the same bytes on every platform. Content the program draws itself
  goes into the list through a paint function.
- A text component on FreeType and HarfBuzz: shaping, bidirectional
  text, line breaking, rich-text spans, font fallback, families and
  variable fonts, and glyph images as coverage, multi-channel distance
  fields and colour (COLR, CBDT and sbix).
- Text fields with selection, word and line movement, undo, input
  method compositions, passwords, number filters, maximum lengths, PC
  and Mac keymaps and the program's clipboard.
- Input: hit testing through rounded clips, scrolling and scales;
  hover, press, clicks and pointer capture; focus for up to eight
  players with Tab order and directional navigation for keys and
  gamepads; drags, drag and drop, sliders; routing to the program with
  what the interface leaves passed to the game behind it.
- Scrolling with eased steps, touch panning, flings and optional
  overscroll; virtual lists that keep only the rows near the viewport
  and hold the scroll position through edits; popups placed beside an
  anchor and dismissed by a press outside or Escape; dialogs and menus
  as layers; exits that play out before a node goes.
- An accessibility tree built from what the interface already holds,
  with adapters for UI Automation, AT-SPI, NSAccessibility,
  UIAccessibility, Android and the web's ARIA, including text being read
  and edited.
- `maul-ui-rhi`, a reference renderer on Maul RHI, which can also draw
  a panel in 3D against the scene's depth.
- `maul-ui-window`, the glue to Maul Window: input, gamepads, input
  methods, the clipboard, the on-screen keyboard and the platform's
  accessibility adapter for a window.
- Support for Linux, Windows with `clang-cl`, macOS, iOS, Android and
  the web, with Emscripten or Clang's wasm32-wasi; a static or shared
  library, a CMake package and a pkg-config module.

### Changed

- For programs moving from an earlier build of Maul UI (0.6.1 and
  before): `MUI_DEF_VERSION` is now `MUI_ABI_VERSION`, and before 1.0 it
  carries the whole version, patch included. `muiCreateContext` and
  `muiCreateTextService` now refuse a def built with the headers of any
  other release, another patch release too, with `mui_errorVersion`.
  `muiDefaultContextDef` and `muiDefaultTextServiceDef` are inline and
  stamp the headers' version, so build against the headers of the exact
  library you link.
