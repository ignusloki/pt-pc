# Upstream 1.0.4 integration / Mac 1.0.8 preparation

Preparation date: 15 October 2026. Work branch: `codex/upstream-1.0.4-integration`, based on released Mac `07bc902` (1.0.7).
Upstream input: `d46b7c4` (`v1.0.4` at `782cd22`, followed by signed-trailer release tooling fixes).
Android and experimental Mac spatial audio are not merged. No release, tag or upstream PR is authorized by this preparation task.

## Reconciliation

- Adopt upstream's coherent Vulkan/D3D12 graphics abstraction and new settings tabs instead of retaining obsolete Vulkan call sites.
- Preserve Mac startup permission and the microphone On/Off setting, Fast Walk, pointer-coordinate mapping, icon, installers and profile path.
- Retain Mac compute bloom with the new command API, bounded asynchronous uploads, pending effect textures, draw culling and sampled GPU timing.
- Keep failed MetalFX passes releasing their continuation and reject repeated splits in one frame. Compose generated-frame UI at the correct
  floating-point target format, preserve HDR gain, and apply output transforms/grain once. Repeated rendered frames already contain their UI.
- Add the PKG helper and its LGPL corresponding source/notices to portable apps. Stage the new bonus-content assets for `pt_release` and copy
  them into the app; a release-only target must not rely on a developer build's files.
- Preserve whole signed-bundle replacement within upstream's per-file/journal transaction. Test rollback, repair, legacy app names and crash recovery.
- Implement `os::ExecutableFile` on Mac using dyld; upstream's new function originally returned an empty path outside Windows/Linux.
- Keep Windows Spatial Sound selectable on Windows only. Mac keeps stereo/surround; the separate experimental Mac spatial preview is not included.
- Supply the missing `tools/ci/payload_index.py` used by upstream's signed-update tooling. Synthetic PE/Linux fixtures check the absolute compressed
  ranges and signed-trailer handling without release keys, downloads or game archives.
- Update fork acceptance routes to use the new tabs; remove a duplicate MetalFX help key and repair an obsolete graphics-preset fixture.

## Validation record

- Apple silicon game and graphical installer: compiled locally, macOS 14 deployment target.
- Packaging: 18 tests passed; publishing guards: 8 passed; signed payload indexing: 4 passed; Ed25519 reference-vector test passed.
- Native Metal/Vulkan queue-handoff and upscaler regressions passed with Metal validation enabled.
- CPU regression, packaged extractor/voice, real startup/menu rendering and native Intel / Windows / Linux builds: in progress.
- A copy of the live profile is preserved in ignored local backups. All gameplay tests use separate settings/save paths and saving disabled.

## Before publishing

- [ ] Complete Apple silicon candidate installer/package validation and targeted gameplay regression.
- [ ] Native Intel CI package and smoke tests; repeat gameplay on the 2019 MacBook Pro where possible.
- [ ] Windows/Linux compilation and installer validation against the merged source. Do not infer GPU gameplay from compilation.
- [ ] User acceptance: new tabs, microphone toggle/test, fullscreen pointer, hallway/bathroom/mirror/maze, Museum/music, existing completion and Game+.
- [ ] Supported Apple silicon: MetalFX Quality and experimental frame generation; HDR on an HDR display. Start with frame generation Off.
- [ ] Verify no original game assets, player data or private diagnostics in the packages; record exact commit and package hashes.
- [ ] At publication time only: confirm version 1.0.8, tag the tested source and publish the canonical Apple silicon and Intel assets.

## Suggested manual settings

Apple silicon: Original graphics, 1280 × 720, 30 FPS, Enhanced textures Off, HDR Off and Frame generation Off. Compare Upscaler Off against
MetalFX Quality with Reflections On. Enable HDR/frame generation separately after the baseline passes. Intel: Low, 1280 × 720, 30 FPS,
Upscaler Off, Reflections Off and Enhanced textures Off; then raise settings gradually.

Keep the completed profile for targeted loop/Museum checks, and use a separate fresh profile for first-boot settings. Do not rename or delete
live save files while the game is running. Existing macOS microphone decisions are respected; no permission reset is part of these tests.
