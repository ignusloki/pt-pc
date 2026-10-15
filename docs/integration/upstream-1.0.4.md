# Upstream 1.0.4 integration / Mac 1.0.8 preparation

Preparation date: 15 October 2026. Work branch: `codex/upstream-1.0.4-integration`, based on released Mac `07bc902` (1.0.7).
Upstream input: `d46b7c4` (`v1.0.4`), including release content introduced in `782cd22` and the subsequent signed-trailer tooling fixes.
Android and experimental Mac spatial audio are not merged. No release, tag or upstream PR is authorized by this preparation task.
The user limited this preparation to Apple silicon and Intel Mac builds and gameplay. Windows/Linux CI failures are outside
this task's acceptance checks; no Windows/Linux compatibility result is claimed.

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
- Packaging: 19 tests passed; publishing guards: 8 passed; signed payload indexing: 4 passed; Ed25519 reference-vector test passed.
- All 36 common native regression executables passed, including settings/profile persistence, microphone controls, Fast Walk,
  save loading/reset, fullscreen coordinate mapping, Unicode paths and the new upstream settings/accessibility logic.
- Native Metal/Vulkan queue-handoff and upscaler regressions passed with Metal validation enabled.
- Packaged voice models loaded and processed the smoke-test WAV. The animation/player-publication regression passed against real game files.
- A real PKG installed into a temporary Unicode path. All three extracted archive hashes matched the existing game. Repair removed a stale app
  resource, preserved test player/mod files and left a valid signed bundle. The portable app's separate single-file PKG helper also extracted
  the real package with matching hashes. Temporary duplicate game files were removed after verification.
- Rendered first-boot, tabbed PC Settings, microphone and Fast Walk routes passed 101 expectations in total, with separate test profiles.
  The first-boot settings screen and start room screenshots rendered correctly. MetalFX Quality and an injected MetalFX-pass failure both
  completed the start-room/menu route with no failed expectations or GPU validation errors.
- Packaged, windowed Apple silicon runs passed a further 20 expectations across SDR and HDR: first-boot settings, PC tabs,
  entering gameplay, pausing and resuming with MetalFX frame generation. The HDR run used an HDR swapchain; exit counters confirmed
  interpolated frames in both runs, with Metal validation enabled and no assertion failures. Visual HDR quality still needs user acceptance.
- [GitHub run 38803970601](https://github.com/ignusloki/pt-pc/actions/runs/38803970601) passed both native Mac jobs for source `6520a41`:
  Apple silicon on `macos-26` and Intel on `macos-15-intel`. Both built the game/Setup, ran the 36 common regressions, packaged voice,
  installer self-test, signature/dependency audit and ZIP/DMG creation. The release job was skipped. CI has no game files; Intel GPU
  gameplay on the user's 2019 MacBook Pro remains a separate check.
- Local Apple silicon candidate: `dist/macos-1.0.8-rc1/arm64/`, version `1.0.8-rc1`, source `6520a41`. `BUILD.json` and `SHA256SUMS`
  record the exact source and portable/Setup ZIP hashes. No original game archives or player profile are bundled.
- Intel candidate: [native CI artifact](https://github.com/ignusloki/pt-pc/actions/runs/38803970601/artifacts/12006137858), containing
  `P.T.PC.Port.Setup-macOS-x64.zip`, the portable ZIP/DMG, installer self-test and voice smoke log. It reports version `1.0.8`
  because the manual CI build uses the prepared version; it is still unpublished and uses the same source `6520a41` as the local ARM candidate.
  The approximately 1 GB artifact was left on GitHub after cancelling a slow local download; no local Intel package is claimed.
- A copy of the live profile is preserved in ignored local backups. All gameplay tests use separate settings/save paths and saving disabled.

## Before publishing

- [x] Complete Apple silicon candidate installer/package validation and automated startup/menu regression.
- [x] Native Apple silicon and Intel CI packages, regressions and packaged voice/installer smoke tests.
- [ ] Repeat Intel gameplay on the 2019 MacBook Pro.
- [ ] User acceptance: new tabs, microphone toggle/test, fullscreen pointer, hallway/bathroom/mirror/maze, Museum/music, existing completion and Game+.
- [ ] Supported Apple silicon: MetalFX Quality and experimental frame generation; HDR on an HDR display. Start with frame generation Off.
- [x] Verify no original game assets, player data or private diagnostics in the local candidate; record exact commit and package hashes.
- [ ] At publication time only: confirm version 1.0.8, tag the tested source and publish the canonical Apple silicon and Intel assets.

## Suggested manual settings

Apple silicon: Original graphics, 1280 × 720, 30 FPS, Enhanced textures Off, HDR Off and Frame generation Off. Compare Upscaler Off against
MetalFX Quality with Reflections On. Enable HDR/frame generation separately after the baseline passes. Intel: Low, 1280 × 720, 30 FPS,
Upscaler Off, Reflections Off and Enhanced textures Off; then raise settings gradually.

Keep the completed profile for targeted loop/Museum checks, and use a separate fresh profile for first-boot settings. Do not rename or delete
live save files while the game is running. Existing macOS microphone decisions are respected; no permission reset is part of these tests.

## Targeted player regression

1. Install with the graphical Setup app for the correct CPU. Check the selected PKG/folder and installation destination, then launch the
   game and confirm the forest/P.T. icon. The local automated install/repair already passed; a normal player install checks the full Finder flow.
2. Use the completed profile to load the hallway, bathroom/mirror, maze, final voice puzzle and ending. Check lighting, reflections, motion,
   audio and controller/mouse behavior. A full new playthrough is not required for these targeted checks.
3. In the PC settings tabs, navigate with keyboard/controller, change a setting, cancel another change and switch between window/fullscreen.
   Verify hover/click alignment after both transitions. Check Fast Walk remains optional and keeps its choice after restarting.
4. In Sound, switch Microphone input Off: the test must be disabled and capture must stop. Switch On and use the microphone test, then restart
   to check persistence. A Mac that already allowed or denied microphone access will reuse its decision rather than show another prompt.
5. Check the Museum's existing completion, new unlock hints/counters, music player and the Game+ toggle. More exhibits can change the percentage;
   distinguish newly locked content from previously earned progress. Check accessibility captions/FOV and the controls used by your controller.
6. On Apple silicon, compare the baseline with MetalFX Quality and Reflections On, then test experimental frame generation separately on macOS
   26 or newer. Check menus and pause/resume for doubled UI or brightness changes. Try HDR separately on an HDR display and adjust paper white/peak.
7. On Intel, start with the Low settings above and test the same key rooms, menus, fullscreen pointer and microphone. Leave unsupported options
   disabled. Record the Mac model, GPU, macOS version and any visual/audio problem before raising graphics settings.

If testing a completely fresh profile, quit the game first and preserve both save slots plus `pt.ini`; keep the existing completed profile
available for the targeted checks. The ignored local backup made before integration remains intact.
