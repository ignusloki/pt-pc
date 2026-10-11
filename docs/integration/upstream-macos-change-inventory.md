# Upstream macOS change inventory

Historical snapshot. For the refreshed feature set and Intel packages, see [the 11 October report](upstream-integration-20261011.md) and [Intel test checklist](intel-mac-test-checklist.md).

Prepared: 9 October 2026. Phase 1 inventory; feature ports are still pending.

Upstream base: `31dc878a7ff33f5ae5bb49659e4f3f7ec449b7fb` (`LoreanXavier/pt-pc:main`).
Tested fork: `877aa3b8474809904309416e27c229a41178b6c8` (`macos-arm64-preview-4`).
Common ancestor: `ca60666892b66d20044e84471a450bf539482104`.
Integration branch: `codex/upstream-macos-integration`.

The three-dot diff identifies 64 paths changed by the fork since the common
ancestor. The decisions below compare those changes with current upstream,
which has already incorporated several fixes and added newer features.
The conflict column comes from a separate merge rehearsal, which reports 21
conflicting paths without merging either checkout.

## Decisions

- **Adapt:** port the remaining behavior in focused commits around the current upstream implementation.
- **Already upstream:** retain upstream’s equivalent behavior; avoid duplicating the fork fix.
- **Preserve upstream:** upstream replaces or extends the older fork implementation; keep it and its platform support.
- **Optional Fast Walk:** adapt independently, keep it off by default, and allow the Mac contribution to proceed without it.

Phase numbers refer to [the integration plan](upstream-integration-plan.md).
Mixed files identify the optional feature explicitly in their decision notes.

| Decision | Paths |
| --- | ---: |
| Adapt | 35 |
| Preserve upstream | 7 |
| Optional Fast Walk | 14 |
| Already upstream | 8 |

## Per-file inventory

| File | Decision | Conflict | Phase and handling |
| --- | --- | --- | --- |
| `.gitignore` | Adapt | — | **2:** Add .DS_Store without changing upstream asset/build exclusions. |
| `CMakeLists.txt` | Adapt | Yes | **2–5:** Add Apple-only Objective-C++, MetalFX and native setup targets plus focused regressions. Keep upstream version policy, release locks, HDR/controller targets and Windows SDK defaults. |
| `CMakePresets.json` | Adapt | Yes | **2:** Add Mac presets independently. Preserve the Windows release preset with PT_REQUIRE_UPSCALERS=ON. |
| `README.md` | Adapt | Yes | **6:** Document native Mac setup, MetalFX and the optional gameplay feature. Keep upstream release URLs, platform instructions and credits. |
| `cmake/Dependencies.cmake` | Adapt | Yes | **2:** Retain upstream pins and architecture-specific voice builds; add only Apple-guarded Metal export definitions where required. |
| `cmake/EnhancedTextures.cmake` | Preserve upstream | — | **2:** Upstream supports the universal Mac Real-ESRGAN helper. Discard the fork’s earlier blanket Apple exclusion. |
| `cmake/MacOS.cmake` | Adapt | Yes | **2:** Extend upstream’s Apple-only module for MetalFX/Cocoa/setup. Keep pinned MoltenVK, its override, macOS 14 deployment and Intel support. |
| `docs/fast-walk.md` | Optional Fast Walk | — | **5:** Add independently, with the option off by default and the existing interaction button preserved. |
| `docs/installer.md` | Adapt | — | **3:** Describe complete fake-PKG and decrypted-folder inputs for the new Mac setup while retaining Windows/Linux instructions. |
| `docs/macos.md` | Adapt | Yes | **6:** Extend upstream documentation around its app name, remembered folder, CPU architectures, voice layout and release workflow; add setup/MetalFX details. |
| `docs/upscaling.md` | Adapt | — | **4:** Add MetalFX availability, R16Float exposure and timeline queue handoff; retain upstream’s other upscalers. |
| `installer/Extractor/Program.cs` | Adapt | — | **3:** Port incomplete-PFSC detection and the clearer error; keep upstream parser/extraction behavior. |
| `installer/Native/setup_core.h` | Adapt | Yes | **3:** Add optional whole-app replacement, repair and rollback. Keep existing Windows/Linux payload defaults and upstream path/update protections. |
| `installer/Native/setup_macos.mm` | Adapt | — | **3:** Port the native picker and destination UI for P.T. PC Port.app. Initially scope the self-contained setup to Apple Silicon. |
| `installer/Tests/Program.cs` | Adapt | — | **3:** Add the incomplete-PKG regression alongside the current extractor tests. |
| `shaders/lighting.glsl` | Already upstream | — | **4:** The shadow-gather workaround is present upstream. Preserve its newer light-aperture handling rather than replacing the shader. |
| `src/engine/anim/anim_test.cpp` | Preserve upstream | Yes | **2:** Upstream already avoids pre-macOS-26 floating-point from_chars with a C-locale strtod path. Do not port the alternate strtod_l implementation wholesale. |
| `src/engine/assets/enhanced_textures.cpp` | Preserve upstream | Yes | **2:** Keep upstream’s Mac texture helper path and newer enhancement behavior; the fork’s exclusion is obsolete. |
| `src/engine/audio/sound_engine.cpp` | Already upstream | Yes | **2:** ARM64 flush-to-zero is supported upstream. Retain the newer surround output and controller audio capture. |
| `src/engine/core/resource_path.cpp` | Already upstream | Yes | **2:** Upstream provides the real Mac executable path and bundle resource lookup. Reuse its VulkanLibraryPath loader rather than adding a second locator. |
| `src/engine/core/resource_path.h` | Already upstream | Yes | **2:** Reuse the Apple ExecutablePath API; keep future Apple-only callers guarded. |
| `src/engine/platform/http.cpp` | Already upstream | Yes | **2:** System libcurl loading already works on Mac; preserve upstream’s extra fallback. |
| `src/engine/platform/input.cpp` | Optional Fast Walk | — | **5:** Adapt Shift and controller bottom-button detection to the newer input code. |
| `src/engine/platform/input.h` | Optional Fast Walk | — | **5:** Add the optional movement signal without removing upstream’s expanded controller state. |
| `src/engine/platform/livesplit.cpp` | Already upstream | — | **2:** SO_NOSIGPIPE protection matches upstream; no additional port is needed. |
| `src/engine/platform/settings.cpp` | Adapt | — | **4, 5:** Add the MetalFX setting key while preserving upstream fields. Fast Walk persistence belongs to the independently reviewable optional commit. |
| `src/engine/platform/settings.h` | Optional Fast Walk | — | **5:** Add the default-false gameplay option without changing the newer settings structure. |
| `src/engine/platform/update_check.cpp` | Preserve upstream | Yes | **2:** Keep GitHub Releases API discovery and separate macos-arm64/macos-x64 identifiers; discard the older fork-only updater change. |
| `src/engine/render/renderer.cpp` | Adapt | Yes | **4:** Port ordered Vulkan/Metal queue segments, timeline handoff and frame-fence lifetimes around upstream’s current submission, HDR and pipeline-cache code. |
| `src/engine/render/renderer.h` | Adapt | — | **4:** Adapt handoff/segment ownership interfaces while retaining current renderer state. |
| `src/engine/render/scene_frame.cpp` | Adapt | — | **4:** Keep debug-label begin/end on the same command buffer when MetalFX splits a frame. |
| `src/engine/render/scene_renderer.cpp` | Adapt | — | **4:** Add Metal texture export eligibility to scene resources; retain upstream’s newer rendering behavior. |
| `src/engine/render/scene_renderer.h` | Adapt | — | **4:** Adapt dispatcher/context access to upstream’s renderer types. |
| `src/engine/render/upscale/metalfx_backend.mm` | Adapt | — | **4:** Port ginnfx’s backend with the tested deferred Metal submission, R16Float exposure, shared-event handoff and abandoned-frame cleanup. |
| `src/engine/render/upscale/scene_upscale.cpp` | Adapt | — | **4:** Integrate the queue boundary and descriptor rebind without removing existing upscale paths. |
| `src/engine/render/upscale/upscale.cpp` | Adapt | — | **4:** Add MetalFX parsing/availability/dispatch with Apple guards; keep existing Windows SDK backends. |
| `src/engine/render/upscale/upscale.h` | Adapt | — | **4:** Extend backend kind and dispatch context for MetalFX while preserving shared interfaces. |
| `src/engine/render/vk_context.cpp` | Adapt | Yes | **4:** Mac portability and feature checks already exist. Add Metal object exports/timeline support while keeping upstream loader overrides, HDR and ray-query detection. |
| `src/engine/render/vk_context.h` | Adapt | — | **4:** Adapt export-enabled resource creation arguments without changing ordinary platform defaults. |
| `src/engine/ui/unicode_font_harfbuzz.cpp` | Already upstream | — | **2:** Kufi/Naskh Arabic font mappings already match upstream. |
| `src/engine/voice/voice_recognizer.cpp` | Preserve upstream | Yes | **2:** Keep all three models, .so CPU variants, architecture scoring, rescue recognition and QoS. If signing requires a library-location change, adapt it around this implementation. |
| `src/game/game.cpp` | Optional Fast Walk | — | **5:** Connect the default-off preference to movement while preserving upstream’s newer game state. |
| `src/game/game.h` | Optional Fast Walk | Yes | **5:** Merge the optional flag into the expanded upstream configuration; retain all newer fields. |
| `src/game/input_script.cpp` | Optional Fast Walk | — | **5:** Adapt scripted speed/input expectations to the current test interface. |
| `src/game/player.cpp` | Optional Fast Walk | — | **5:** Apply the optional speed boost after original analog/directional relationships and movement locks. |
| `src/game/player.h` | Optional Fast Walk | — | **5:** Add the movement option with the original speed/collision interfaces preserved. |
| `src/game/ui/pc_settings.cpp` | Optional Fast Walk | — | **5:** Add translated preference labels to the newer settings UI. |
| `src/main.cpp` | Adapt | Yes | **3–5:** Keep upstream folder discovery, profile migration, controls and first-focus fix. Add setup/MetalFX UI and SDL window-transition synchronization selectively; Fast Walk remains separate. The baseline compile fix is already committed. |
| `tests/fast_walk_test.cpp` | Optional Fast Walk | — | **5:** Adapt movement/collision/lock checks to current upstream APIs. |
| `tests/installer_update_test.cpp` | Adapt | — | **3:** Port bundle update/repair/rollback assertions for the upstream app name and layout. |
| `tests/pad/boot_focus.txt` | Adapt | — | **4:** Retain the cold-boot/focus regression route. It already passes against the baseline: nine expectations and three rendered captures. |
| `tests/pad/fast_walk.txt` | Optional Fast Walk | — | **5:** Adapt controller/keyboard movement coverage; preserve interaction behavior. |
| `tests/pad/fast_walk_saved.txt` | Optional Fast Walk | — | **5:** Check persistence across isolated runs after the feature is ported. |
| `tests/queue_handoff_test.cpp` | Adapt | — | **4:** Adapt ordering, lifetime and abandoned-frame regression checks to the upstream renderer. |
| `tests/reflection_mix_test.cpp` | Already upstream | Yes | **4:** The corrected half-coverage expectation already exists. Baseline passes all 29 GPU cases; only the fork’s explanatory comment differs. |
| `tests/settings_roundtrip_test.cpp` | Optional Fast Walk | — | **5:** Add the default-off preference’s persistence coverage while retaining upstream’s expanded settings roundtrip. |
| `third_party/macos_notices/MoltenVK-Apache-2.0.txt` | Preserve upstream | — | **2:** Upstream downloads/licenses its pinned MoltenVK. Keep its notice convention instead of adding a duplicate fixed notice file. |
| `third_party/macos_notices/README.md` | Preserve upstream | — | **2:** Fold necessary acknowledgments into upstream’s existing license documentation. |
| `tools/macos/build.py` | Adapt | — | **2, 3:** Adapt the native installer build helper to upstream targets/version policy and architecture-specific voice files; keep portable Intel builds available. |
| `tools/macos/extractor-entitlements.plist` | Adapt | — | **3:** Carry the entitlements required by the self-contained .NET extractor, limited to that executable. |
| `tools/macos/game-entitlements.plist` | Adapt | — | **2:** Reconcile runtime signing and microphone requirements with upstream’s bundle/signing flow. |
| `tools/macos/package.py` | Adapt | — | **2, 3:** Build native setup around tools/macos/make_app.py rather than maintaining incompatible portable packagers. Include all three voice models, built CPU variants, texture tools and licenses. |
| `tools/macos/test_packaging.py` | Adapt | — | **2, 3:** Adapt packaging regressions to upstream naming and resources; add installer payload and signature roundtrip checks. |
| `tools/prepare_native_installer.py` | Adapt | Yes | **3:** Port Mac RID/app payload handling and safe ZIP path semantics while preserving existing Windows/Linux preparation and metadata. |

## Conflict handling

Every conflict is marked above. Build/resource conflicts should keep upstream’s
platform defaults and add only the needed installer/MetalFX declarations.
The animation, enhanced-texture, audio, resource-path, HTTP, updater and voice
conflicts mostly resolve by retaining newer upstream implementations.
Renderer/Vulkan/main conflicts require selective adaptation of the remaining
features; game configuration conflicts belong to the optional Fast Walk commit.
The reflection-test conflict needs no behavioral change because the corrected
expected result is already upstream and passes on the Mac.

Reproduce the inventory and conflict rehearsal from a checkout with the recorded
refs available:

```sh
git diff upstream/main...macos-arm64-preview-4 --name-only
git merge-tree --write-tree --name-only upstream/main macos-arm64-preview-4
```

The second command returns 1 when conflicts are found; that is the expected
rehearsal result. Its generated tree is not an integration commit. Never resolve
the whole set by accepting one side or overwriting upstream files.

## Attribution and boundaries

Preserve ginnfx’s attribution for [the MetalFX contribution](https://github.com/ignusloki/pt-pc/pull/1)
and the subsequent tested timeline synchronization work. Retain upstream’s
existing credits for ahm3texe’s graphics/fonts and the earlier Mac port fixes.
No fork preview version, fork-specific updater URL, proprietary assets or
personal game paths should become upstream defaults.

The additional baseline repairs in `src/main.cpp` and
`tests/voice_match_test.cpp` address failures found before importing features;
their evidence is recorded in [the baseline report](upstream-macos-baseline.md).
The voice-test repair is outside the 64-path fork inventory and should remain a
small, independent upstream fix.

Phase 1 is complete: all 64 fork paths have a decision and all 21 conflicts have
a handling strategy. The next implementation phase reconciles Mac build and
packaging declarations before adding native setup or MetalFX.
