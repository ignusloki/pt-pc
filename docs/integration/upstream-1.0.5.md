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

## Relevant upstream changes

The [1.0.5 release notes](https://github.com/LoreanXavier/pt-pc/releases/tag/v1.0.5) describe the startup hotfix, richer Unix crash diagnostics,
gyro calibration/drift correction, scrollable PC settings, fewer repeated menu sounds, paused Lisa footsteps, the Game+ bathtub Lisa fix,
and camera-pose diagnostics. DirectX/Windows-specific ray tracing, neural rendering and upscaler changes remain platform-gated.

## Validation and player checks

Packaging regressions (19) and signed payload indexing (4) passed before the native rebuild. The clean ARM build, native suites, packaged voice,
installer self-test, signatures, and update-enabled first-boot/gameplay checks are pending. This file will record the completed results.
Windows/Linux troubleshooting is outside the user-authorized scope. Intel gameplay and publication remain pending.

Use the graphical Setup app with the existing PKG/folder and normal destination. Start with Original graphics, 1280 × 720, 30 FPS, HDR Off,
Enhanced textures Off and Frame generation Off. Then compare MetalFX Quality with Reflections On. Keep the completed profile for loop selection.

Check first-boot/settings rendering, window/fullscreen mouse alignment, PC Settings wheel scrolling and scrollbars, the microphone switch/test,
Fast Walk persistence, gyro calibration with a supported controller, pausing during Lisa footsteps, Museum/music, and Game+ bathtub Lisa after
an ending/restart. Test HDR/frame generation separately after the baseline. A full new playthrough is not necessary for these targeted checks.

The live save slots and settings were copied to ignored `.local-backups/player-profile-20261015-before-upstream-1.0.5/` before automated testing.
Automated gameplay uses private settings/save directories with saving disabled.
