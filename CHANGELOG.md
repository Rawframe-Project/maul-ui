# Changelog

All notable changes to this project are recorded here. The format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and
the project uses [Semantic Versioning](https://semver.org/). Before
1.0.0, any minor release may change the API, the ABI and every data
format.

## [Unreleased]

### Added

- The library skeleton: the build, the family rules and tools, the
  version and result API (`muiGetVersion`, `muiResultName`) and the
  library profile.
- The context (`muiCreateContext`, `muiDestroyContext`) with its
  allocator and node limit, and the node tree: creation with a host
  key, insertion before a sibling, detaching, subtree destruction,
  traversal and stale-id refusal (record mui-0002).
- Layout: authored values (`muiLayoutStyle`: Scale+Offset sizes and
  limits, flex container and item fields, margin, border and padding,
  host content), `muiComputeLayout` with a host measure function, and
  `muiNode_GetRect`. The solver follows CSS Flexbox for a single line
  and is checked against Chrome by a fixture corpus (record mui-0003).
- A layout benchmark (`bench/`): cold, static, one-change and resize
  frames over a 40,001-node list and a 9,841-node nested tree.
- Wrapping (`muiFlexWrap`) and line placement (`muiAlignContent`), with
  15 more fixtures from Chrome.
- Absolute nodes (`muiPlacement`: Scale+Offset insets in the parent's
  padding box and an anchor point) and automatic margins
  (`marginAuto`), with 22 more fixtures from Chrome.
- Right-to-left layout (`muiTextDirection`, inherited), with 11 more
  fixtures from Chrome; the oracle now writes CSS's logical properties.
- Aspect ratio (`muiSizing.aspectRatio`), with 10 more fixtures from
  Chrome.
- Style classes with state variants, node types and direct writes
  (`maul-ui/style.h`), resolved in fixed layers before layout (record
  mui-0004); new context limits for styles, node types and property
  sets. `muiNode_SetLayoutStyle` now writes every layout property
  directly. The benchmark gains a list styled through node types.
- Style conditions over a node's last size and direction and over the
  environment (`muiCondition`, `muiSetContextEnvironment`), with values
  that may not set what their condition reads; `muiIsUpdatePending`.
- A notification queue (`muiNextNotification`, `mui_empty`, the
  `notifications` limit) and oscillation reports: a node whose
  conditions flip with its own layout is reported once and held.
