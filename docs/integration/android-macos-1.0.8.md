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

Local candidate: `1.0.8-android-sync.3`, version code **10**, using beta app ID
`org.ignusloki.pt.beta` and the existing beta signing certificate. Build with
`tools/android/build.py`, the retained SDK/JDK, `--liborbis`, and private signing
paths. Outputs belong in `dist/android/main-sync-3/`. Preserve both installed
apps and their profiles; install the signed beta only as an update.

The signed-package observations below validate this candidate on the S22. No
Vulkan 1.1/1.2 fallback, wider-phone support or full-game performance claim
follows from this integration.

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
compute bloom on Android nor relaxes a GPU requirement. The replacement signed APKs reached gameplay on the phone.

## Android HDR restriction

The sync.2 signed update (source `c4f61c4`, code 9) reached gameplay and PC
settings. Its display page showed 960x443 and a 30 fps cap, but offered the
shared HDR control: the desktop display query remains unknown on Android.
The Android row and setter now keep HDR unavailable and explain that restriction.
Desktop HDR controls retain their existing behavior. This does not enable a new
Android rendering mode.

## Final signed APK and S22 observations

The final APK is `dist/android/main-sync-3/PT-1.0.8-android-sync.3-arm64.apk`,
built from clean source `812657c0b4b009f006a5bb38b8342982c5abff7e`. Its SHA-256 is
`0a020d74c419243a6797c81ddf178d4089341a3c19d5c37e59915b5c7d2d93cc`.
The installed APK has that exact hash and the retained beta certificate
`926bd1f2a5e195d650ea020703b7d9dc999ed23acac0694496a73b07a926a097`.
It is a non-debuggable arm64 Release APK, minimum Android API 29 / target 36,
with 23 native libraries, verified 16 KiB ZIP/ELF alignment and no original game
archives. The 16 new Lisa Date runtime assets were checked against their source
hashes and the APK runtime manifest in sync.1; subsequent packaging retains them.

On Galaxy S22 SM-S901B / Android 16:

- Installed as beta updates (codes 7 -> 8 -> 9 -> 10), without uninstalling or
  clearing data. Existing game files, microphone On, touch On and subtitle 150%
  remain. The prototype package's version, update time and data directory are
  unchanged, and the beta's data directory is unchanged.
- Final signed launcher and native game start. Gameplay shows Menu, OK / Use,
  Zoom and sticks; menus hide the sticks and show edge navigation. Direct PC
  Settings opens the new Display tab, with 960x443, cap 30 and HDR disabled.
- Touch Tab reaches Graphics. A manual shadow change becomes Custom; Left
  selects Ultra with enhanced textures and traced effects unavailable, and
  further Left taps return through High/Original to Low. Low remains selected
  after normal quit and the sync.3 update/relaunch. These UI changes were first
  exercised on sync.2; persistence was checked on the final sync.3 APK.
- Android Settings -> Back and Home -> Recents -> the existing P.T. card both
  restore the rendered menu and gameplay. Native PID **9490** survives both.
  The diagnostic records surface-restoration generations **1** and **2**;
  the second return follows about **530 seconds** frozen in the background.
  The existing pause-on-focus-loss setting opens the original menu on return.
- Final normal Quit returns to the launcher and records exit code **0**.
  Save diagnostic log produces the 101,258-byte local text report (without its
  UTF-8 BOM), including a complete capability report. Its exported last 64 KiB
  of native log contains no warning/error entries. The startup header fell
  outside that tail; this is not a complete-session log audit.
- Recent samples hold about 30 fps at 960x443, GPU roughly 18–19 ms, PSS around
  1.24 GiB and thermal status 1. Occasional CPU frame spikes around 80–100 ms
  remain in the log. These mostly stationary scene/menu observations do not
  establish full-game or wider-device performance.

Local evidence is under `build/android-validation/`: `mainsync3-verification.json`,
`mainsync3-lifecycle.json`, `mainsync3-S22-diagnostics.txt`, installed/package
metadata, screenshots and the native/host regression logs. The phone export is
`Download/PT-Android-main-sync3-S22-20261019.txt`.

Fresh import/first boot, full playthroughs, simultaneous multitouch stress,
physical controllers, speech recognition and final-puzzle rumble were not rerun
for this sync. Full desktop builds were not run; host settings/preset/job-pool
checks passed and desktop-specific guards were retained. Both platform branches
remain separate, and nothing was pushed, tagged or published. Any later update
for testers who installed this candidate must use an Android version code above
**10**. Vulkan 1.1/1.2 adaptation remains deferred pending further device logs;
its earlier assessment predates this engine merge and must be rechecked against
this new base before implementation.
