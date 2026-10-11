# Apple silicon baseline accepted — 11 October 2026

The user confirmed that the fresh installer works in the targeted Apple silicon regression. This closes the upstream-update baseline before native Intel testing. The accepted game and Setup are built from `b687754ddb0572245bccafe5781be471dc4b1e7a` on `codex/macos-arm64-port`, version **1.0.4**, arm64 only, minimum macOS **14.0**.

The regression used the completed profile and Loop Browser to inspect affected scenes, with a separate first-boot check recommended. A complete replay was not required for this update. The user reported that everything seems to work; this records that acceptance without claiming instrumented timings or native Intel validation.

## Build verification

- Clean game, release game, shaders, native Setup and self-contained package extractor compiled.
- 23 native suites passed, including the two GPU reflection/submission checks; 14 Python packaging tests and 10 extractor fixtures passed.
- Packaged voice-model loading and native Setup unpacking/rollback self-tests passed.
- Game, Setup and extractor are arm64. Selected icons, microphone usage metadata, macOS minimum and deep/strict signatures were verified. The distribution ZIP matches the signed Setup and contains no original game archives.
- Existing settings and both save slots remained byte-for-byte unchanged during the automated checks. They were backed up before cleanup. Manual gameplay may subsequently update that profile normally.

Installer: `/Users/suporte/PT-Mac/dist/macos/apple-silicon-regression-20261011/PT-Mac-Setup-arm64.zip` (302,333,458 bytes).

SHA-256: `6b29d578c0fc61d1188b72d0e899f997c9382e5f46603dc18bf51f01b697f565`.

Build records and additional test logs: `/Users/suporte/PT-Mac/.deps/apple-silicon-regression-20261011/`. The checklist and build metadata are alongside the installer.

## Cleanup and Intel checkpoint

Previous local build/distribution folders and untracked investigation notes were removed from the active checkout. The original game package, installed game, player profile and reusable dependency downloads were retained. Untracked notes were copied into the Intel backup before removal.

The complete-history source bundle, verified ARM/Intel Setup ZIPs, integration notes and selected evidence are preserved at `/Users/suporte/PT-Mac/.local-backups/intel-integration-20261011/`. The profile backup is `/Users/suporte/PT-Mac/.local-backups/apple-silicon-regression-profile-before-20261011/`.

The paused `codex/upstream-macos-integration` branch already contains the same complete `src/` and `shaders/` trees as this accepted baseline. Its separate architecture presets, voice CPU modules and unified game/Setup packaging provide the Intel build path. Native gameplay, live microphone input, physical controls and sustained performance on the user's 2019 MacBook Pro remain pending. Its exact macOS version and GPU must be recorded during that test.

No release or tag was created. The integration branch remains local. Windows/Linux CI is still a gate for the eventual upstream PR.
