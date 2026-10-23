# Upstream 1.0.8 integration

Merge Lorean's `3b1392abe5215773d4c353f5bc1ecb85233be68a` (upstream 1.0.8) into
`codex/macos-arm64-port`, starting from the unified Mac 1.0.9 / Android Beta 4 source
`fe9807508e78874b055caa3ca8b70a16ba03beec`. The fork's `main` mirrors upstream.
The pre-merge source is preserved as `codex/backup-before-upstream-1.0.8-20261023`.

This is a source synchronization. The fork's version remains 1.0.9 until the next
release is prepared. Existing tags and published release files are preserved.
No new tag, release or installed-game update is part of this integration.

## Upstream changes

The [upstream 1.0.8 notes](https://github.com/LoreanXavier/pt-pc/releases/tag/v1.0.8)
describe startup/crash diagnostics, Vulkan layer handling, display/refresh-rate
policy, an audio-output selector and controller-speaker routing fixes, regional
game-folder recognition, portable updates, photo composition grids, exit-door
camera handoff and keyboard-prompt corrections. DirectX-only changes retain their
platform gates. Upstream's Windows Intel-driver notice does not establish a fix
or new hardware requirement for Intel Macs.

## Reconciliation

- Preserve Mac HDR presentation metadata, MetalFX HDR frame-generation transport,
  fullscreen transition synchronization and pointer-coordinate regression cases.
  Adopt upstream's display policy and refresh-rate checks alongside those cases.
- Preserve saved microphone enable/disable controls, startup permission handling,
  Fast Walk, subtitle scaling and the unified Android platform code. Android keeps
  its fixed fullscreen/resolution rows, touch navigation, AAudio microphone path,
  Vulkan diagnostics, memory limits and surface/foreground recovery.
- Adopt `extras.pak` in place of the deleted loose `assets/lisa_date` folder.
  Mac bundles require and copy the pack; Android stages it into the private
  runtime manifest. The existing packaging regression checks its inclusion.
  The bonus-scene test depends on the pack even when built as a standalone target.
- Keep whole-bundle Mac installer replacement/rollback alongside upstream's
  regional game detection, portable-install recognition and obsolete-file notes.
  Gather obsolete paths before collapsing bundle operations, preserving the
  existing signed-tree transaction behavior.
- Keep the structured multi-platform payload builder, corresponding extractor
  source and architecture checks; extend original-game folder rejection to all
  CUSA title IDs, including renamed region folders.
- Retain isolated `pt::metadata_json` types in the new frame-generation manifest
  reader, avoiding the earlier asset/runtime JSON type collision.
- Resolve the binary platform-file conflict by retaining the Mac/Android platform
  implementations and adding upstream's Windows clear-window function and POSIX
  stub. Retain Mac-specific help text alongside updated upstream translations.
- Extend the normal Mac build checks with upstream's game-folder, display-policy,
  audio-output, photo-panel, Vulkan-address and Vulkan-layer regressions.

## Verification

The 19 existing Mac Python packaging regressions and four payload-index checks
passed locally after reconciliation. The source passes the conflict-marker and
whitespace checks. Native Apple silicon and Intel compilation, bundled runtime,
installer and GPU checks run through the Mac workflow after pushing the merge;
their result is recorded separately from this local packaging verification.

No additional automated gameplay route is run for this synchronization. Mac
gameplay and Android phone behavior have not been revalidated on the new upstream
base. Future player checks should cover output-device selection, fullscreen and
exclusive refresh-rate changes, HDR/MetalFX, photo grids, regional PKG/folder
import, bonus content, and Android microphone/touch/foreground recovery.

Original packages, extracted game files, installed games, profiles, backups and
the separate Android worktree are preserved. Unrelated private `logs/` remains
outside the commit. Review evidence is kept in ignored `.deps/upstream-1.0.8-review/`.

## Local candidate follow-up (2026-10-23)

CI investigation is deferred at the user's request; the next validation is local
Android and Apple silicon packages. Native compilation exposed two remaining
metadata-parser references in the shared installer header and portable-update
manifest reader. Both now use `pt::metadata_json`, retaining the parser isolation.

The public Android beta on the Galaxy S22 reproduces the Museum's black X-photo
preview. Opening the exhibit displays the actual photograph correctly. Android
cannot spawn the desktop Museum thumbnail generator, so the preview file never
exists. The X-photo card now uses the existing photo-piece renderer with the
exhibit's original model, texture and UVs, instead of a generated PNG. Its hallway
package is mounted when needed, including when entering the Museum from another
level. This also works on desktop without a thumbnail cache. The interactive 3D
exhibit, unlock rules and Android texture-memory limit are unchanged; no original
game images are included in the APK or Mac installer.

Local regression: check the X-photo card and strip thumbnail before opening it,
open and leave its 3D exhibit, and revisit the card. Check the ordinary photo
pieces and completed photo too. For the upstream synchronization, also check
output-device selection, menu input, HDR/MetalFX on the Mac, and Android touch,
microphone and foreground recovery. Signed Android candidates must update the
public beta in place, preserving its installed assets and profile.
