# Upstream integration plan

Prepared: 8 October 2026. Updated: 9 October 2026.

Phase 1 is complete on `codex/upstream-macos-integration`. The historical [baseline report](upstream-macos-baseline.md) and [classified change inventory](upstream-macos-change-inventory.md) remain the original comparison. Upstream was refreshed and rebased onto `0e0e147649c05be3b7782f89f55d9e125ce7671e`; its own Mac compile fix is retained. Our baseline source repair now only adds the missing `.dylib` files to Unicode voice test fixtures.

The agreed current priority is:

1. Reconcile Mac build and packaging, including the native installer.
2. Request microphone access when the game starts.
3. Use the supplied icon by default for the Mac game and builder/installer.
4. Validate Intel Mac builds and packaging.
5. Validate Windows and Linux builds and shared installer/extractor code.

Implementation and verification for these priorities are recorded in [the foundation checks](upstream-macos-foundation-checks.md). MetalFX, remaining window behavior and Fast Walk follow these platform gates.

## Objective

Contribute the remaining features of the tested Apple Silicon fork to
`LoreanXavier/pt-pc`, preserving the current Windows, Linux and macOS build paths
and upstream's newer gameplay, controller and update behavior.

The result should be a reviewable upstream contribution, backed by platform
checks. Passing the current Mac release's tests alone does not establish that
the combined code works on Windows or Linux.

## Starting points

| Item | Verified state |
| --- | --- |
| Upstream repository | https://github.com/LoreanXavier/pt-pc |
| Upstream base | `main`, `0e0e147649c05be3b7782f89f55d9e125ce7671e` |
| Tested fork | `ignusloki/pt-pc`, `codex/macos-arm64-port` |
| Tested fork commit | `877aa3b8474809904309416e27c229a41178b6c8` |
| Fork release checkpoint | `macos-arm64-preview-4`, version 1.0.3 |
| Integration branch | `codex/upstream-macos-integration` |
| Merge rehearsal | 21 conflicting files; no checkout files changed |

Refresh upstream immediately before implementation. These revisions record the
reviewed starting points rather than pinning development to an outdated main.

Upstream already contains Apple Silicon and Intel Mac support, a remembered dump
folder picker, a release workflow, and several fixes credited to this fork.
The refreshed upstream base excludes native PKG installation and Fast Walk; this integration branch now adapts the native installer.
References: [upstream macOS documentation](https://github.com/LoreanXavier/pt-pc/blob/31dc878a7ff33f5ae5bb49659e4f3f7ec449b7fb/docs/macos.md),
[macOS release workflow](https://github.com/LoreanXavier/pt-pc/blob/31dc878a7ff33f5ae5bb49659e4f3f7ec449b7fb/.github/workflows/macos.yml).

## Integration strategy

Create a separate managed worktree and branch from the latest upstream main.
Port the necessary changes in focused commits. Because upstream has already
adapted part of the fork, a wholesale replacement of upstream files would risk
discarding newer work. Keep the tested fork release available as the comparison
and recovery point.

For each change, classify it as already present upstream, still needed,
superseded by an upstream implementation, or an optional feature. Preserve the
original contributor attribution, including ginnfx's MetalFX PR and ahm3texe's
graphics/font work, when adapting their code.

Use the current priority order above for foundation work. Then keep MetalFX and GPU queue synchronization, remaining fullscreen/resolution behavior, and optional Fast Walk in focused follow-up commits. Native microphone permissions and framework linkage stay Apple-only. Source attribution and upstream gameplay/update behavior remain preserved.

Fast Walk should be independently reviewable because it changes gameplay on all
platforms. If the maintainer wants a separate feature PR, the Mac contribution
must be able to proceed without it.

## Phase 1 — Establish the integration baseline

- Refresh upstream and create the integration worktree from its current main.
- Build upstream's Apple Silicon game and app before adding changes. Record
  failures so an existing upstream issue is distinguishable from a regression.
- Inventory the fork's unique changes and map the 21 conflicts by subsystem.
- Preserve upstream's version policy and GitHub Releases API updater. Fork
  preview numbers and fork-specific download links are not upstream defaults.

Conflicts identified in the review:

| Group | Files |
| --- | --- |
| Build and docs | `CMakeLists.txt`, `CMakePresets.json`, `README.md`, `cmake/Dependencies.cmake`, `cmake/MacOS.cmake`, `docs/macos.md` |
| Installer | `installer/Native/setup_core.h`, `tools/prepare_native_installer.py` |
| Platform and resources | `src/engine/core/resource_path.cpp`, `src/engine/core/resource_path.h`, `src/engine/platform/http.cpp`, `src/engine/platform/update_check.cpp` |
| Rendering and audio | `src/engine/render/renderer.cpp`, `src/engine/render/vk_context.cpp`, `src/engine/assets/enhanced_textures.cpp`, `src/engine/audio/sound_engine.cpp`, `src/engine/voice/voice_recognizer.cpp` |
| Game and tests | `src/engine/anim/anim_test.cpp`, `src/game/game.h`, `src/main.cpp`, `tests/reflection_mix_test.cpp` |

**Exit condition:** upstream baseline results are recorded, and every fork change
has an integration decision.

## Phase 2 — Reconcile build and app packaging

- Keep upstream's pinned MoltenVK download and local-library override.
- Enable Objective-C++/Cocoa and AVFoundation only on macOS; MetalFX follows in Phase 4. Keep Apple framework and
  Metal headers out of Windows and Linux compilation.
- Preserve the existing Windows and Linux compiler, dependency, shader and
  release settings. Keep their enhanced-texture and upscaler defaults.
- Retain upstream's macOS enhanced-texture support rather than restoring the
  fork's earlier blanket exclusion.
- Preserve upstream's architecture-specific voice backends, including Intel
  variants. Adapt signing and packaging to include the modules actually built;
  do not assume the fork's single ARM64 voice module exists in this layout.
- Use upstream's app name and resource conventions consistently in the runtime,
  installer, update detection and repair tests.
- Build separate Apple Silicon and Intel game/installer packages; preserve upstream's per-CPU voice modules. Distinguish cross-compilation from native runtime/GPU validation.
- Request microphone permission asynchronously during normal game startup, respecting existing macOS decisions and keeping recording timing unchanged.
- Include the supplied default icon in both Mac bundles before signing.

**Exit condition:** the integrated game and portable app build on Apple Silicon;
non-Apple builds acquire no Apple dependencies; the Intel build path remains
supported and has a scheduled compile/package check.

## Phase 3 — Add native PKG installation

- Adapt the Cocoa installer to install the upstream app layout.
- Support a complete fake PKG or an already decrypted game folder as input and
  a user-selected destination.
- Keep the existing portable app's remembered-folder launch behavior.
- Port whole-app replacement, repair and rollback so the signed resource seal
  remains valid during an update.
- Keep Windows/Linux executable names, installer defaults and payload behavior.
- Retain incomplete-PKG detection and clear extraction errors.
- Include the matching extractor source, licenses and correct rebuild target in
  the installer payload.

Check fresh installation, update, repair, cancellation and rollback; paths with
spaces/non-ASCII characters; complete and incomplete PKGs; dump-folder input;
and preservation of game archives, saves, settings and mods. Compare protected
file hashes before and after an update.

**Exit condition:** the signed installer works with the integrated app, and the
shared installer/extractor regression checks pass on the tested platforms.

## Phase 4 — Integrate MetalFX and window fixes

- Port MetalFX with explicit Metal texture exports and R16Float exposure.
- Adapt the timeline semaphore handoff to upstream's current renderer. Preserve
  submission order, frame-fence lifetimes and cleanup of abandoned frames.
- Keep MetalFX optional and off by default. Unsupported hardware should retain
  the existing renderer and report why the option is unavailable.
- Preserve Windows/Linux rendering with upscaling disabled and existing
  Windows DLSS/FSR/XeSS and frame-generation paths.
- Adapt the queue-handoff regression to the current renderer interfaces.
- Apply the macOS window-transition fix without removing upstream's newer
  display, controller, focus or startup handling. Retest first-boot options.

On the Mac, check Native, Quality, Balanced and Performance; disable/re-enable
MetalFX during play; change resolution and window mode; toggle v-sync; and
return from focus loss. Check dark scenes, mirrors, particles and UI. Use Metal
validation and the renderer's timing/log output.

**Exit condition:** packaged Mac graphics checks pass, and the shared renderer
passes available Windows/Linux compilation and rendering checks.

## Phase 5 — Adapt optional Fast Walk

- Keep the option off by default and preserve saved settings.
- Adapt Shift and controller bottom-button handling to upstream's newer input
  code. Preserve the button's original interaction action.
- Preserve original directional/analog speed relationships, collisions, camera
  behavior, movement locks and game timing beneath the optional boost.
- Update input, movement and persistence regressions against the integrated
  game interfaces.

**Exit condition:** default behavior is unchanged, enabled behavior passes the
movement/input checks, and the feature can be reviewed or omitted independently.

## Phase 6 — Validate all platform paths

| Platform | Build/package checks | Runtime checks |
| --- | --- | --- |
| Windows x64 | Developer/release targets, native setup, portable packaging, shared CPU/settings/installer/extractor tests, existing SDK defaults | Original rendering; controller/keyboard behavior; available DLSS/FSR/XeSS and frame generation; update/repair |
| Linux x64 | Native or existing cross-build path, portable runtime and setup, shared tests, required X11/Wayland/audio backend settings | Original rendering, controller/audio, voice runtime, startup/save/restart and update/repair |
| macOS arm64 | Developer/release targets, signed portable app and PKG installer, all Mac/shared checks | First boot, MetalFX transitions, window changes, voice/microphone, controls, gameplay and save/restart |
| macOS Intel | Existing architecture preset/workflow, portable app, signing and voice-module packaging | Available hardware smoke test; ordinary rendering and a clear unavailable MetalFX state where unsupported |

Run asset-free compilation and CPU/installer tests in automation. Run GPU tests
on compatible hardware or a software Vulkan implementation with the required
features. A skipped or unavailable GPU check must be recorded as unverified.
Windows upscaler/frame-generation checks need supported hardware; they cannot be
established by a Mac test or a hosted runner without a suitable GPU.

Add a PR verification workflow if practical: upstream's current macOS workflow
runs on release publication or manual dispatch, not pull requests. Preserve its
release jobs and asset naming. Keep proprietary game assets out of CI and all
distribution files; game-dependent checks use local assets.

Arrange Windows/Linux build or test hosts before declaring compatibility. Keep
the PR in draft while a required platform gate is unresolved.

**Exit condition:** the validation matrix records what passed, what ran on real
hardware, and any outstanding limitations. No platform is called verified solely
because its Mac-only counterpart passed.

## Phase 7 — Submit the upstream contribution

- Review the final diff for duplicated upstream fixes, fork release/version
  changes, hardcoded local paths and accidentally weakened platform defaults.
- Push the integration branch to `ignusloki/pt-pc`.
- Open a draft PR targeting `LoreanXavier/pt-pc:main`. Describe the additions,
  contributor credits, platform checks and remaining hardware coverage.
- Keep installer, MetalFX, window and Fast Walk changes in focused commits. Split
  Fast Walk into its own PR if requested during review.
- Resolve maintainer feedback and new upstream changes, rerunning the checks
  affected by each change. Mark ready for review once the required gates pass.
- Let upstream choose the final merge and release version. Rebuild the fork's
  next package from the final integrated source after the contribution is settled.

## First implementation milestone

Finish the foundation checks in the agreed priority order, with native Intel, Windows and Linux validation where available. Obtain manual confirmation of the first-launch microphone dialog and live input on the packaged app. Keep the current tested fork release as the comparison until the integration preview and later renderer/gameplay phases are approved for release.
