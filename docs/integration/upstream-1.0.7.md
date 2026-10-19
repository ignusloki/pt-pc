# Upstream 1.0.7 Mac integration

The Mac branch incorporates upstream `ac71cac` (1.0.7), advancing from `d0a9b8f` (1.0.5).
The previous Mac release is 1.0.8; this update prepares Mac 1.0.9, with local candidate
`1.0.9-rc1`. The player accepted the Apple silicon candidate. Tagging and publication are on hold
at the user's request while integration of the Android beta is assessed.

Before merging, the previous source was preserved as
`codex/backup-before-upstream-1.0.7-20261019`, and both save slots plus `pt.ini` were copied
and hash-checked in `.local-backups/player-profile-20261019-before-upstream-1.0.7/`.
The separate Android worktree is outside this integration.

## Changes and reconciliation

The upstream [1.0.6](https://github.com/LoreanXavier/pt-pc/releases/tag/v1.0.6) and
[1.0.7](https://github.com/LoreanXavier/pt-pc/releases/tag/v1.0.7) updates bring shared lighting,
point-shadow and reflection corrections, start-room exposure timing, peephole fixes,
water-effect-linked bathtub drips, fuller Museum transcripts, an Accessibility FPS counter,
stable PC-settings selection during background changes, update prompts/buttons, and photo-mode
settings persistence with a separate reset action. Enhanced Textures and background pause/mute
options are removed upstream. New DirectX-only neural rendering and ray-regeneration features
retain their platform gates; this sync does not enable them on Mac.

- Keep the tested Mac scRGB presentation adapter with 80 nits per linear unit. Upstream's
  alternative workaround leaves metadata unset to avoid MoltenVK's one-nit scale. The existing
  adapter supplies the correct scale and clears it when returning to SDR.
- Keep the tested MetalFX HDR frame-generation transport, including brightness and highlight
  gain in RGB before interpolation. Upstream's new motion-recording helper is adopted; optional
  GPU timing retains the existing capability guard.
- Apply upstream's tighter mesh-occlusion rule in the existing per-frame occlusion preparation,
  preserving cached visibility for repeated render passes and the mirror's separate visibility.
- Keep startup microphone authorization, its saved on/off control, Fast Walk, fullscreen pointer
  mapping, forest icons, Game+ resources, and both native graphical installers.
- The first compilation found two new neural-runtime metadata readers using upstream's old
  `pt::json` namespace. Use the fork's `pt::metadata_json` reader, retaining the parser isolation
  that prevents the earlier asset/update JSON type collision at startup.
- Upstream's settings regression exposed a missing legacy neural-quality migration in its loader.
  An older `balanced` value without an explicit model becomes Quality/Large, as its existing test
  requires; explicit model choices and the new default remain unchanged. Mac graphics stay gated.
- Adopt linked data-folder resolution for symlinks and Finder aliases on mounted writable drives.
  Existing links and saves are preserved; unresolved or unwritable destinations remain errors.
- Remove retired texture-tool packaging and test targets. Exclude their stale licenses/tools when
  reusing a build cache, and verify that exclusion in the existing packaging regression.
- Keep the fork's tested six-package publisher and exact tag/source verification. Upstream's
  optional publication from a later source commit is not enabled in this fork's publishing workflow.
- Add the new linked-folder, SBC audio, composite-output, point-shadow and settings-refresh checks
  to the Mac build helper alongside the existing Mac HDR and MetalFX GPU checks.
- Dispatch upstream's new `--voice-cpu-probe` mode in the existing voice-test executable before
  running its cases, matching the game's entry point. Without that dispatch, the CPU safety probe
  reruns the test recursively instead of checking a backend. The runtime guard itself is retained.

## Validation plan

Build the Apple silicon graphical installer from the reconciled source. The build helper runs
its normal native, extractor, packaged-voice, installer and bundle checks. Additional automated
gameplay routes are not run for this candidate. Native Intel compilation/package checks use
the Mac workflow; physical Intel rendering remains a separate hardware check.

Use the existing completed profile to select loops. Start with Original graphics, HDR Off and
Frame generation Off; then compare MetalFX Quality, HDR and frame generation independently:

1. Install/repair over the existing installation and confirm saves, unlocks, microphone and Fast Walk
   settings remain. Check startup and window/fullscreen mouse alignment.
2. Move through PC Settings, toggle the FPS counter, and confirm the selected row stays stable.
3. Check start-room exposure, the hallway lamp/clock/plant shadows, bathroom mirror and flashlight,
   floor reflections, and the peephole from the side. Compare the lamp again with HDR and MetalFX
   frame generation on/off to confirm the previous flicker correction remains.
4. Change photo-mode settings, close/reopen it in the same session, and try its separate reset action.
5. Play Museum radio/voice entries with transcripts, and check microphone input/test and recognition.
6. If normally used, check a linked profile on an already mounted external drive and controller audio.
   These paths do not need to be introduced into a working installation just for regression.

## Validation result and publication hold

The user accepted `1.0.9-rc1` on Apple silicon after the regression check. Its game source is
`3d5a60d737d01591ad43a6de79bad45164260744`, containing the reconciled upstream 1.0.7 update.

- The local build helper passed all 42 native suites, including 19 linked-folder checks, 28 native
  HDR presentation assertions and 144 MetalFX HDR transport checks. Extractor scenarios, packaged
  voice loading and the graphical installer self-test passed. Additional automated gameplay
  routes were not run.
- Package verification passed for architecture, minimum macOS 14, signatures, icons, all 349
  embedded payload file hashes and the fork's updater endpoint. The installer excludes original
  game assets, player data, retired texture tools and temporary diagnostic instrumentation.
- [Native Mac workflow 39308617988](https://github.com/ignusloki/pt-pc/actions/runs/39308617988)
  completed successfully for Apple silicon and Intel at that source commit. This verifies native
  builds and packaging; physical Intel gameplay has not been repeated for this upstream sync.
- Both save slots and settings matched their pre-build backup hashes before player validation.
  The backup remains in `.local-backups/player-profile-20261019-before-upstream-1.0.7/`.
- The retained installer is `dist/macos-1.0.9-rc1/arm64/P.T. PC Port Setup.app`. Its Setup ZIP
  SHA-256 is `7a532e17b65d46d96c0abf1999cedb04d6acc3b1d82b6e1f0f2fa0857ff16ec1`.
  `BUILD.json` and `SHA256SUMS.txt` accompany it. Duplicate packaging and extractor staging were
  removed after verification, reclaiming approximately 1.2 GiB of logical files.

The fork's `main` mirrors upstream `ac71cac`; the Mac integration remains on
`codex/macos-arm64-port`. The Android beta branch remains separate. The user requested committing
and pushing this accepted Mac checkpoint, with no release yet, before assessing the Android merge.
No new tag or release is created by recording this acceptance.
