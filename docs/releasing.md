# Releasing Maul UI

The steps a release of Maul UI takes, in order. The family's shared
steps come first; Maul UI's own follow. A release is a tag `vX.Y.Z` on
`main` (conventions, section 15).

## The family's steps

1. `CHANGELOG.md`: the `[Unreleased]` section becomes `[X.Y.Z]` with
   the date, and an empty `[Unreleased]` starts above it.
2. The version macros in the base header are `X.Y.Z`; CMake reads them.
3. The family drift check reports no difference for the library:
   `python3 tools/family_sync.py --check maul-ui`, run from the
   family's docs repository.
4. CI is green on the commit to be tagged, every job.
5. The tag is made on that commit and pushed.

## Maul UI's own steps

1. **The size budget** (record mui-0001): the CI job "Wasm size against
   the budget" on the commit to be tagged reports the core and the text
   component under their ceilings (160,000 and 640,000 bytes). The
   numbers go into the release's changelog section. A part over its
   ceiling blocks the release; a ceiling is only ever tightened.
2. **Assistive technology** (record mui-0008): a run with each screen
   reader the adapters serve, recorded on the commit to be tagged:
   Narrator or NVDA over UI Automation on Windows, Orca over AT-SPI on
   Linux, VoiceOver on macOS and iOS, TalkBack on Android, and a screen
   reader over the ARIA tree in a browser. Each run walks the widget
   samples: focus moves, names, roles, states and values are read, a
   button and a check box act, a text field takes text, a list scrolls.
   A note of what was run and what was heard goes with the release; a
   failure is fixed or recorded as a known issue in the changelog.
3. **Fuzzing**: each fuzz target (`fuzz_*`) runs for at least ten
   minutes from its seed corpus (`tools/fuzz_seed.py`) without a
   finding.
4. **The guide and the API reference** are current: `tools/check_docs.py`
   and `tools/check_guide.py` pass, and `docs/api.md` is regenerated.
