# Release

Releases are made by release-please from the conventional commit titles on `main`. Versions follow Semantic Versioning; before 1.0.0 any release may change the API, the ABI and the behavior, and a breaking change bumps the minor version. Tags are `vX.Y.Z`.

## Steps

1. release-please keeps a draft pull request titled `chore: release X.Y.Z` open. It moves the version between the markers in `include/maul-ui/base.h`, where CMake reads it and from which `MUI_ABI_VERSION` follows, updates `.release-please-manifest.json`, and adds an entry to `CHANGELOG.md` from the `feat`, `fix` and `perf` titles.
2. Rewrite that entry in user language before merging: what changed for someone using the library, grouped as Added, Changed, Fixed, Removed and Security, with the part first where a change is one part's (the text component, an accessibility adapter, the renderer, the window glue). Leave out refactors, tests, CI and documentation. Say what a caller must change for a breaking change.
3. Check that the `required` CI job passed on the release pull request, including every platform cell, and that the wasm sizes are within their ceilings.
4. Mark the pull request ready and merge it. release-please tags the merge commit and publishes the GitHub release with the changelog entry as its notes. The Pages workflow publishes the documentation from `main`.
5. The `assets` job of `.github/workflows/release.yml` attaches the source archive `maul-ui-X.Y.Z.tar.gz`, its `SHA256SUMS`, an SPDX SBOM and a build provenance attestation. A user verifies a download with `sha256sum -c SHA256SUMS` and `gh attestation verify maul-ui-X.Y.Z.tar.gz -R Rawframe-Project/maul-ui`.

## Notes

- Before 1.0 every release has its own ABI: the shared library's `SOVERSION` is the whole version, the CMake package accepts only the exact version, and `muiCreateContext` and `muiCreateTextService` refuse a def stamped by another release's headers with `mui_errorVersion`.
- The shared family files must match their canonical copies in the organization's `.github` repository before a release.
- Maul UI fetches Maul Tween, Maul Unicode, Maul RHI and Maul Window by tag (`cmake/Tween.cmake`, `cmake/Unicode.cmake`, `cmake/Rhi.cmake`, `cmake/Window.cmake`). A new release of one of them moves its pin in a pull request of its own, with the whole check green. Maul Window and Maul Unicode are the exception: Maul Window builds against Maul UI's Maul Unicode, so configuring stops when the Maul Unicode release Maul Window names differs from Maul UI's pin, and the two pins move together.
