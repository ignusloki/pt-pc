# Android integration of macOS 1.0.8

On 2026-10-19, merge `codex/macos-arm64-port` at
`49ca168a30db4b2e178018983ef17a3c2dcbfca5` (the `v1.0.8` tag) into the isolated
`codex/android-beta-fixes` branch. The Android parent is `93eaa08` and includes
the signed beta 2 fixes and the Vulkan capability diagnostic collector.
The macOS checkout and branch are not modified. This is a local integration,
not a published Android release or a Vulkan compatibility implementation.

## Integration decisions

- Retain upstream's GPU abstraction and the new shared engine/game changes.
  Adapt Android texture format checks to its Vulkan device wrapper; retain the
  1,024 px source-mip cap, two outstanding texture decode batches and 32 MiB
  pending upload threshold. Desktop upload limits and job-pool behavior remain
  upstream's values.
- Preserve Android foreground/surface recovery, presentation error checks,
  microphone capture/native-library lookup, phone rumble and touch input.
- Preserve subtitle scaling, smooth glyph coverage, Android graphics-preset
  restrictions, the real render-size display and launcher preferences. Keep
  desktop rendering-API and HDR settings alongside the Android subtitle key.
- Upstream PC settings now have tabs. Android's existing side button shows
  **Tab** and cycles them on tabbed pages; it remains **Column** and changes
  columns on other PC pages. Directional input and direct PC Settings access
  remain discrete menu taps. Gameplay keeps its existing minimal controls.
- Android packages the new `assets/lisa_date` runtime directory next to fonts,
  shaders and voice models, with the existing runtime manifest checks.
- Guard desktop `execinfo` backtrace calls and desktop child-process startup
  on Android. Preserve the Android JSON floating-point fallback and Lua/Bionic
  compatibility patch. No changes to Android API/ABI requirements or JNI
  registration settings.

## Validation

The merged native Android engine and shader target build successfully with NDK
28.2.13676358. Android settings, graphics-preset, Lua and Vulkan-diagnostic test
binaries also compile. Host settings round-trip, graphics presets (including
Android feature-policy checks), shared job-pool and all nine simulated Vulkan
capability scenarios pass. Android preset tests also cover save/reload of each
preset with enhanced textures and ray tracing unavailable.

Local candidate: `1.0.8-android-sync.2`, version code **9**, using beta app ID
`org.ignusloki.pt.beta` and the existing beta signing certificate. Build with
`tools/android/build.py`, the retained SDK/JDK, `--liborbis`, and private signing
paths. Outputs belong in `dist/android/main-sync-2/`. Preserve both installed
apps and their profiles; install the signed beta only as an update.

Signed APK packaging, installed-package checks and device observations will be
recorded after the candidate build. A successful native compile is not runtime
validation. No Vulkan 1.1/1.2 fallback, wider-phone support, sustained-performance
or final-puzzle rumble claim follows from this integration.

## First signed-package finding

The sync.1 APK (source `7946a1a`, code 8) installed and launched the .NET
launcher, but the native game returned code 1 during scene initialization.
The S22 diagnostic export records `scene renderer: bloom chain on render
passes`, followed by `scene renderer: pipeline creation failed`.

The shared required-pipeline list included `bloom_kawase_`, although compute bloom
is selected only inside an Apple platform guard. Android therefore always had
an empty optional handle. Remove that optional handle from the required list;
retain the existing compute-creation fallback and destruction logic. This also
keeps desktop builds that use render-pass bloom viable. The fix neither enables
compute bloom on Android nor relaxes a GPU requirement. Verify the replacement
signed APK on the phone before treating the integration as runtime-tested.
