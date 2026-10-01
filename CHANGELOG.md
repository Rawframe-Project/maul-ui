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
