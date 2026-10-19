# Upstream 1.0.5 Mac integration

The user requested another upstream sync and a local Apple silicon regression installer before continuing the unpublished Mac 1.0.8 release.
The accepted `1.0.8-rc2` packages and old local Mac build outputs were removed to recover space. Game files, the original PKG, installed apps,
live profiles/backups, and the separate Android worktree remain intact. Reusable vendor downloads were retained for the clean rebuild.

## Source and reconciliation

- Previous upstream: `d46b7c4180e492f43a7a68f2891379a252227750` (1.0.4 plus release-tool fixes).
- New upstream: `d0a9b8f` (1.0.5 plus documentation cleanup), merged into `codex/macos-arm64-port`.
- Premerge Mac source: `97ec29fa3fdef39c8b91d0abe2837c708824cee1`; local safety reference:
  `codex/backup-before-upstream-1.0.5-20261015`.
- Mac version remains 1.0.8; the new local candidate is `1.0.8-rc3`. No release or tag is published by this sync.
- Upstream and the fork independently fixed the update/asset JSON type collision. Retain the fork's separate `pt::metadata_json` namespace,
  injected-fetch regression coverage, and exception guards for both the worker and thread creation. Incorporate upstream's additional malformed
  metadata regression cases. The upstream test fixture's isolated legacy accented byte is normalized to UTF-8 for Apple Clang.
- Preserve Mac executable-path discovery alongside the new upstream foreground-window helpers. Normalize embedded NUL character literals to
  escaped literals so this source remains reviewable as text.
- Retain Fast Walk alongside the new gyro calibration row, and retain the startup microphone request alongside upstream's startup diagnostic.
- Existing Mac fullscreen pointer, MetalFX handoff/composition, icons, microphone setting and signed whole-bundle installer behavior remain.
- The first clean native run exposed five upstream rendering-API test assertions that assume DirectX can be enabled on every OS. Keep those
  assertions Windows-only and verify the Vulkan-only fallback on Mac/Linux instead; no rendering behavior is changed to satisfy the test.

## Relevant upstream changes

The [1.0.5 release notes](https://github.com/LoreanXavier/pt-pc/releases/tag/v1.0.5) describe the startup hotfix, richer Unix crash diagnostics,
gyro calibration/drift correction, scrollable PC settings, fewer repeated menu sounds, paused Lisa footsteps, the Game+ bathtub Lisa fix,
and camera-pose diagnostics. DirectX/Windows-specific ray tracing, neural rendering and upscaler changes remain platform-gated.

## Validation and player checks

The Apple silicon candidate was built from `46aad796ad39821b2e3e7b278af9fe0d44582705`:

- Clean native build and all 36 common native regression suites passed, including the combined JSON/parser stress tests, background update
  failures, gyro calibration, settings/save persistence, microphone controls, Fast Walk and fullscreen mouse mapping.
- Packaging regressions (19), signed payload indexing (4) and extractor scenarios (10) passed. Packaged voice loading and the graphical
  installer's self-test passed. Both app signatures, CPU/dependency audits and forest-icon hashes passed.
- Packaged game startup with online update checks enabled passed in windowed and fullscreen modes: 15 expectations each, zero failures and
  normal exits. The route covers fresh-profile options, PC tabs, wheel scrolling to both ends of Graphics, entering the start room,
  pause and resume. First-boot and scrolling screenshots were visually checked. No GPU validation error or update-check failure was reported.
- Both live save slots and `pt.ini` still match the pretest backup hashes. ZIP audits found no original PKG/game archives, player files or
  private logs. SHA-256 checks confirmed the installer's embedded payload matched its staging payload before duplicate staging was removed.
- The fork's `main` now matches upstream `d0a9b8f`, and the Mac branch contains the reconciled merge. Native Apple silicon/Intel CI for the
  source commit is [run 38836253600](https://github.com/ignusloki/pt-pc/actions/runs/38836253600); it was still running at this local validation.
  Windows/Linux troubleshooting is outside the user-authorized scope. Intel gameplay and publication remain pending.

Local output: `dist/macos-1.0.8-rc3/arm64/P.T. PC Port Setup.app` and `pt-port-1.0.8-rc3-setup-macos-arm64.zip` (approximately 331 MiB each).
The ZIP SHA-256 is `7a61fcbd1fa7d1107529fceb983dd6404ec6d7dab7f361bb6dd2984c089f06c9`.
`BUILD.json` and `SHA256SUMS` accompany the installer. The compiler/test/.NET staging files and duplicate portable app/ZIP were removed after
validation. Vendor downloads and small diagnostic records remain in ignored `.deps/`. Only this new graphical installer and its ZIP remain
under `dist/`; no release or tag was created. Player acceptance of this new candidate is still required before publication.

Use the graphical Setup app with the existing PKG/folder and normal destination. Start with Original graphics, 1280 × 720, 30 FPS, HDR Off,
Enhanced textures Off and Frame generation Off. Then compare MetalFX Quality with Reflections On. Keep the completed profile for loop selection.

Check first-boot/settings rendering, window/fullscreen mouse alignment, PC Settings wheel scrolling and scrollbars, the microphone switch/test,
Fast Walk persistence, gyro calibration with a supported controller, pausing during Lisa footsteps, Museum/music, and Game+ bathtub Lisa after
an ending/restart. Test HDR/frame generation separately after the baseline. A full new playthrough is not necessary for these targeted checks.

The live save slots and settings were copied to ignored `.local-backups/player-profile-20261015-before-upstream-1.0.5/` before automated testing.
Automated gameplay uses private settings/save directories with saving disabled.

## HDR corrections and release acceptance (2026-10-16)

The user accepted the upstream synchronization, then reported dim HDR output and a hallway highlight flickering only with MetalFX
frame generation. Native presentation investigation identified a mismatch between the renderer's 80-nits-per-linear-unit scRGB
output and MoltenVK's default Core Animation metadata scale. A Mac presentation adapter now restores the correct scale, retains the
HDR10 metadata values, reapplies it on resize/level changes, and clears it on return to SDR. Native SDL/MoltenVK checks pass 28
assertions, alongside 20 HDR calculation assertions.

Game captures and an isolated native MetalFX probe identified loss of the custom HDR-gain alpha after interpolation warms up.
The Mac frame-generation backend now carries brightness-adjusted HDR colour in display-encoded RGB, then restores the compositor's
colour/gain representation. This preserves dark-channel precision in half-float intermediates. The production transport passes
144 GPU checks through the actual frame interpolator, covering highlights, colour, shadows, fades, brightness changes and resizing.
The test skips unsupported SDKs, macOS versions and GPUs; Intel does not gain frame-generation support from this change.

The user accepted `1.0.8-lighting.2` on Apple silicon. Its final game capture records 138 paired GPU summaries (125 HDR, 13 SDR),
retained highlight gain in all 28 summaries containing native highlights, and a normal exit with no error-level entries. Gameplay
with HDR and frame generation independently enabled/disabled was captured. Temporary lighting/exposure/GPU instrumentation and
the hard-coded local log path are removed for the stable release; the rendering corrections and regression tests remain.

Stable Mac 1.0.8 retains upstream through `d0a9b8f`. The native build helper now runs 39 suites, plus extractor, packaged voice,
installer and bundle checks. Release packages for both architectures are produced by the Mac workflow before publication. The
earlier Intel gameplay acceptance remains useful, but physical Intel gameplay has not been repeated for the latest upstream sync.

The first final-release CI attempt compiled both architectures and passed the Apple silicon package, but the new native HDR test
aborted inside Vulkan initialization on the hosted Intel machine. Its report identifies an Apple Paravirtual device with Metal 2
and 64 MB VRAM, so absence of a Metal device was not the cause. The test now probes Metal availability and the renderer's required
argument buffers tier 2 before starting MoltenVK, checking every GPU so dual-GPU Intel Macs can use their discrete device.
Unsupported hosts report an explicit skip; supported hardware still runs all presentation assertions. Other Vulkan initialization
failures remain failures and collect a debugger crash stack. These checks change only the test/CI, not the game renderer.
