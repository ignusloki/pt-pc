# Android support in the shared macOS 1.0.9 branch

Merge `codex/android-beta-fixes` at `0e494515716625311334d18c389f8541ace91637`
(Android Beta 3) into `codex/macos-arm64-port` at
`d75c18193895ba538cabc8a251937866c52671d2` on 2026-10-19, as requested by the user.
The receiving branch already contains upstream 1.0.7 (`ac71cac`) and the accepted
Mac 1.0.9 candidate. This combines platform support in the same source branch;
it does not publish another release or change the fork's `main` branch.

## Reconciliation

- Preserve the newer upstream engine/game changes, Mac HDR/MetalFX support,
  controller audio, linked-folder handling, metadata-parser isolation and legacy
  neural-quality migration. The common desktop version remains 1.0.9.
- Retain Android's launcher/import path, microphone permissions and AAudio capture,
  lazy native voice modules, touch input/menu navigation, phone rumble, subtitle
  scaling, foreground/surface recovery, Vulkan diagnostics and private profiles.
  JNI marshal methods remain disabled and JNI preload remains ignored.
- Resolve the build conflicts using SDL's platform-selected target: shared on
  Android, static on desktop. Retain the newer SBC dependency on both builds and
  the Android-only Lua/Vulkan diagnostic targets. Android still produces
  `libmain.so`; desktop executables/installers retain their existing targets.
- Keep upstream's removal of Enhanced Textures and background pause/mute settings.
  Remove obsolete Android feature-policy parameters, enhanced-texture functions,
  rows and build references. Retain preset recognition/persistence checks for
  devices without ray queries, as well as legacy-option migration checks.
- Preserve Android's 1,024 px uploaded-mip limit, two decode batches, 32 MiB
  deferred staging limit, format checks, Low/30 fps defaults, fixed-width surface,
  disabled HDR/ray queries/VR/frame generation, and actual resolution display.
- Keep the optional compute-bloom pipeline out of the required-pipeline check;
  Android continues using the render-pass path.
- Upstream's new desktop voice CPU safety probe re-executes the game. Exclude
  that subprocess path on Android, where `/proc/self/exe` is `app_process`, not
  the native game. Preserve Android's existing backend scoring/loading and keep
  the desktop probe intact. Physical speech recognition still needs tester
  coverage on this newer base.
- Update Android dependency notices: remove retired bc7enc and include zstd's BSD
  license; the shared build supplies the new SBC notice. Keep game archives and
  signing credentials out of packaging.
- Adapt the imported touch-menu route to the current Display/Upscaling/Graphics/
  Sound tabs. Android's hardware overlay continues using Tab on tabbed pages and
  Column on other pages. Update the README to describe the combined source and
  distinguish it from the published Android Beta 3.

Vulkan 1.1/1.2 support remains deferred. This merge retains the Vulkan 1.3 gate;
older-device diagnostic collection is preserved without claiming compatibility.

## Validation before the merge commit

- Apple silicon `pt_release`, graphical Setup and all 42 Mac regression targets
  compile. All 42 suites pass, including the Mac HDR/MetalFX GPU tests, voice
  models, settings/presets, stable settings selection and linked-folder tests.
- All 19 Mac Python packaging tests pass.
- Android native game/shaders compile from a fresh build directory with NDK
  28.2.13676358, arm64-v8a/API 29. The four Lua, Vulkan capability, graphics-preset
  and settings binaries compile and pass on the S22. Eight Python capability
  analysis tests pass.
- The updated discrete touch-menu route passes 20 expectations against the Mac
  executable, with temporary settings and saving disabled. This checks direct PC
  access, adjacent directional taps, tabs, values and Back/Resume.
- The additional multilingual check passes subtitle sizing/wrapping checks but
  reports four missing help texts: `pc_note_frame_generation_metalfx` and
  `pc_note_vsync_metalfx_fg` in Czech and Polish. Those translation tables and
  their lookup code are unchanged from the pre-merge Mac commit. This existing
  localization limitation is recorded rather than hidden by weakening the test.

Local logs and fixtures are under `build/android-merge-validation/`, outside Git.
The accepted Mac installer in `dist/macos-1.0.9-rc1/` is preserved. Original PKG,
game archives, backups, recovery files and unrelated `logs/` are preserved.

## Signed Android candidate

Use internal version `1.0.9-android-merge.1`, code **12**, with the existing beta
app ID and signing key. Code 12 can update public Beta 3 (code 11) without an
uninstall. Build outputs belong in `build/android-merged/` and
`dist/android/merge-validation/`; this candidate is not a public release.

The source commit and package hash are recorded in that output's `BUILD.json`.
Phone checks must use the exact signed APK and preserve both installed apps'
profiles. Fresh import, full playthrough, physical controllers, keyword speech,
final-puzzle rumble and native Intel gameplay are separate coverage limits;
they are not established by compilation or the automated suites above.
