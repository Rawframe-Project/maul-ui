# Releasing Maul UI

A release follows the family's release checklist (`docs/conventions.md`,
section 15) in order. Maul UI's own steps below are taken between its
steps 4 and 5: on the `release: X.Y.Z` commit, once CI is green on it,
and before the tag.

## Maul UI's own steps

1. **The size budget** (record mui-0001): the CI job "Wasm size against
   the budget" on the release commit reports the core and the text
   component under their ceilings (160,000 and 640,000 bytes). The
   numbers go into the release's notes. A part over its ceiling blocks
   the release; a ceiling is only ever tightened.
2. **Assistive technology** (record mui-0008): a run with each screen
   reader the adapters serve, on the release commit: Narrator or NVDA
   over UI Automation on Windows, Orca over AT-SPI on Linux, VoiceOver
   on macOS and iOS, TalkBack on Android, and a screen reader over the
   ARIA tree in a browser. Each run walks the widget samples: focus
   moves, names, roles, states and values are read, a button and a
   check box act, a text field takes text, a list scrolls. The notes
   record what was run and what was heard; a run that could not be
   made is listed as not run, never as passed, and a failure is fixed
   or listed as a known issue. On Linux, `tools/screen_reader_walk.sh`
   walks a sample with Orca from a file of steps
   (`tools/screen_reader/*.steps`): the sample under Xvfb on a private
   session and accessibility bus, Orca with speech off, and what it
   would have said written a line each. VoiceOver on macOS and NVDA on
   Windows walk the same steps in the `screen-readers` workflow on
   every push (`tools/screen_reader/walk.mjs`, through Guidepup); its
   run on the release commit is the release's, its artifacts what was
   heard; a reader whose setup the machine refused is a warning there
   and counts as not run, so the job is run again. Every walk checks
   the phrases its steps file expects.
3. **Fuzzing**: each fuzz target (`fuzz_*`) runs for at least ten
   minutes from its seed corpus (`tools/fuzz_seed.py`) without a
   finding.
4. **The guide and the API reference** are current: `tools/check_docs.py`
   and `tools/check_guide.py` pass, and `docs/api.md` is regenerated.
