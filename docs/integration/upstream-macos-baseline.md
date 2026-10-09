# Upstream macOS integration baseline

Recorded: 9 October 2026. Test logs carry the Mac's local 8 October timestamps.

## Result and source

The Apple Silicon developer and release targets build, the portable app packages
and verifies, and all 17 selected upstream test targets pass after two small Mac
repairs. The packaged game renders first-boot options and gameplay, and passes
the cold-boot/focus route with nine expectations and no failures.

| Item | State |
| --- | --- |
| Upstream starting point | `LoreanXavier/pt-pc:main`, `31dc878a7ff33f5ae5bb49659e4f3f7ec449b7fb` |
| Integration branch | `codex/upstream-macos-integration`, separate managed worktree |
| Source repair commit | `80f15fb2f7a43363279534aeaad53324de0f5a30` |
| Comparison fork | `877aa3b8474809904309416e27c229a41178b6c8`, `macos-arm64-preview-4` |
| Version/updater | Upstream `1.0.2`; `https://api.github.com/repos/LoreanXavier/pt-pc/releases/latest` |
| Final ZIP | `pt-port-1.0.2-macos-arm64.zip`, 272,851,243 bytes (260.2 MiB) |
| ZIP SHA-256 | `97accf25d987c1b21e28ec361d1b44e7cd689c9009650ad5c4e560cfa7e3c05b` |
| Host | Apple M1 Pro, macOS 26.6.2 (`25G83`) |
| Tools | Apple Clang 21.0.0, CMake 4.4.4, Ninja, Homebrew Vulkan headers/loader and shader compiler |
| Build | Fresh `RelWithDebInfo` objects, arm64, macOS deployment target 14.0 |
| Vulkan on Metal | Upstream-pinned MoltenVK 1.4.2 |

The source branch started at current upstream main. The tested fork's release
remains the comparison point; feature ports will be adapted in later phases.
The [change inventory](upstream-macos-change-inventory.md) covers every one of the
64 fork paths and all 21 conflicts. The [integration plan](upstream-integration-plan.md)
defines the remaining work and platform gates.

## Failures observed before feature integration

### Save-folder compile failure

Both `pt` and `pt_release` failed at upstream `src/main.cpp:768`:
`use of undeclared identifier 'value'`.

`LegacyUserDataDir()` returns immediately on Apple, but a return expression after
the platform conditional still references `value`, which exists only in the
Windows/Linux branches. The repair encloses that expression in the non-Apple
branch. The Mac return stays empty, preserving upstream's existing profile
policy. Preprocessing the before/after function for Windows and Linux produces
identical code; this is a structural check, not a build on those platforms.

### Voice-test fixture failure

The initial `pt_voice_match_test` passed all 68 keyword cases but failed its
Unicode-path initialization check. The fixture copied `.dll` and `.so` files
but omitted the Mac `.dylib` runtime, so `libwhisper.dylib` was missing from the
temporary model directory.

The repair adds `.dylib` to the existing fixture filter. The rerun passes all
68 cases, loads the models and Apple M1 backend from the Unicode path, and
correctly rejects a missing model directory. Its expected error log for the
missing-directory case is part of the passing regression.

These are baseline upstream failures, discovered before importing native setup,
MetalFX or Fast Walk. Both repairs are in the source commit above.

The first configuration attempt also encountered restricted-network DNS access
while fetching the new voice model. Retrying with network access succeeded;
that environment restriction required no source change.

## Validation

| Check | Outcome |
| --- | --- |
| CMake configuration | Passed. Cached source downloads reused the unchanged dependency pins; no fork object files were reused. Upstream's additional voice model, MoltenVK and texture helper were obtained through its own build rules. |
| Developer and release game | `pt` and `pt_release` passed after the save-folder repair. |
| Shared CPU/model tests | 16 targets passed; the voice fixture's original failure and repaired rerun are retained separately. |
| GPU reflection test | `pt_reflection_mix_test`: all 29 cases passed on MoltenVK. The corrected half-coverage expectation is already upstream. |
| Portable packaging | Unchanged upstream `tools/macos/make_app.py` packaged `pt_release` as `P.T. PC Port.app`; version 1.0.2, upstream bundle ID, macOS 14 minimum, microphone usage description. |
| Voice resources | All three models, three shared runtime libraries and the Apple M1, M2/M3 and M4 `.so` CPU variants are included. |
| Bundled voice runtime | Initialized the Apple M1 backend, base/VAD models and small-model second opinion; 3 seconds of generated silence produced no false detections. |
| First-boot options | Rendered at 1280×720 with fresh isolated settings and no saved game; screenshot inspected. |
| Gameplay rendering | Startup scene rendered at 1280×720; screenshot inspected. |
| Startup/focus regression | Fork checkpoint `tests/pad/boot_focus.txt` ran against current upstream: background cold boot, restored focus, visible options, Escape-to-start, later focus pause and resume; 9 expectations, 0 failed, 3 screenshots. |
| Metal validation | Metal API validation enabled for GPU/packaged checks; no validation errors in captured output. Vulkan validation-layer availability was not established. |
| Native dependencies | Game/runtime modules are arm64; universal bundled helper/driver files include arm64. No non-system absolute library dependencies were found. |
| Signature/ZIP | Ad hoc signatures pass deep/strict verification on the app and after archive extraction; executable ZIP permissions preserved. |
| Distribution contents | No source PKG or core proprietary game archives are in the ZIP. Loop preview generation was intentionally skipped for this baseline. |
| Existing user files | All 14 protected asset, PKG, save, settings and existing package files have identical before/after SHA-256 values. |

The 16 CPU/model targets are `pt_tests`, `pt_platform_test`,
`pt_settings_roundtrip_test`, `pt_save_status_test`, `pt_save_reset_test`,
`pt_language_default_test`, `pt_graphics_preset_test`, `pt_voice_match_test`,
`pt_display_modes_test`, `pt_pipeline_cache_store_test`,
`pt_controller_feedback_test`, `pt_controller_speaker_route_test`,
`pt_photo_preset_test`, `pt_photo_panel_test`, `pt_user_data_test` and
`pt_unicode_paths_test`.

## Reproducing the build and package

Set `PT_INTEGRATION_SOURCE` to the integration checkout,
`PT_INTEGRATION_BUILD` to a fresh build directory and `PT_INTEGRATION_OUT` to a
new package directory. The Vulkan arguments below match this Apple Silicon
Homebrew installation; adjust those paths for a different host.

```sh
cmake -G Ninja -S "$PT_INTEGRATION_SOURCE" -B "$PT_INTEGRATION_BUILD" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
  -DVulkan_INCLUDE_DIR=/opt/homebrew/include \
  -DVulkan_LIBRARY=/opt/homebrew/lib/libvulkan.dylib \
  -DVulkan_GLSLC_EXECUTABLE=/opt/homebrew/bin/glslc
cmake --build "$PT_INTEGRATION_BUILD" --parallel 6 --target pt pt_release
python3 "$PT_INTEGRATION_SOURCE/tools/macos/make_app.py" \
  --build "$PT_INTEGRATION_BUILD" --exe "$PT_INTEGRATION_BUILD/pt_release" \
  --out "$PT_INTEGRATION_OUT" --game "$PT_INTEGRATION_BUILD/no-game-assets"
```

The last `--game` path must be absent to reproduce this asset-free packaging
baseline without generating previews. Game-dependent checks separately use the
locally extracted game assets, isolated settings/save folders, `--no-save`,
`--no-mods`, `--no-update-check` and `--audio-offline`.

For example, set `PT_TEST_DIR` to a new test folder and `PT_TEST_GAME` to the
local decrypted game folder:

```sh
"$PT_INTEGRATION_OUT/P.T. PC Port.app/Contents/MacOS/pt" \
  --headless --game "$PT_TEST_GAME" --options-menu --frames 240 \
  --width 1280 --height 720 --settings "$PT_TEST_DIR/pt.ini" \
  --save-dir "$PT_TEST_DIR/saves" --log "$PT_TEST_DIR/pt.log" \
  --no-save --no-mods --no-update-check --no-vr --audio-offline \
  --screenshot "$PT_TEST_DIR/options.png"
```

## Local evidence and package

The build and evidence were kept under `build/upstream-integration/` in the
original workspace; the source worktree is separate. Evidence is ignored by
Git and includes:

- `baseline-checks/configure.log` and `configure-result.json`.
- `baseline-checks/build.log` and `build-result.json`: original upstream compile failure.
- `baseline-checks/build-foundation.log` and `build-foundation-result.json`: repaired build.
- `baseline-checks/cpu-results.json`, individual test logs and `voice-fixture-result.json`.
- `baseline-checks/gpu-results.json` and `gpu-final-results.json` with commands, exit codes and durations.
- `baseline-checks/boot-focus-final/`: inspected options/gameplay/pause captures and the expectation log.
- `baseline-checks/package-audit.json`: native dependencies, signing, archive hash and profile protection.
- `baseline-checks/protected-before.json` and `protected-after.json`.
- `baseline-checks/change-inventory.json`: validated 64-file/21-conflict classification.

The final app and ZIP are in `dist/macos/upstream-baseline/` in the original
workspace. The ZIP is `pt-port-1.0.2-macos-arm64.zip`. Its final size and SHA-256
are recorded in `package-audit.json`; this is a local integration baseline,
not a published release.

## Coverage still pending

Windows, Linux and Intel Mac compilation/runtime checks have not run. The
Windows/Linux preprocessor comparison above covers only the tiny save-folder
repair. The existing release presets and architecture workflows are retained.
The macOS workflow currently runs on release publication/manual dispatch; a
pull-request verification workflow belongs to the later validation phase.

This baseline uses headless GPU rendering and scripted/virtual controls.
Finder launch, the native remembered-folder picker, live update fetching/download,
physical controller output,
live microphone permission/keyword capture, fullscreen transitions and extended
gameplay remain runtime checks for later phases. The configured macOS 14 minimum
has not been tested on a macOS 14 host. The bundled texture helper's presence
and signing were checked; enhancement inference was not exercised. Signing is
ad hoc; Developer ID signing and notarization have not been performed.

## Phase 1 completion

Current upstream was refreshed and isolated, its original failures were
recorded and repaired, the Mac app built and passed the selected checks, and
all fork changes/conflicts have an integration decision. Continue with Phase 2:
adapt the Apple-only build and packaging foundations around upstream's existing
portable app before porting the native installer and MetalFX.
