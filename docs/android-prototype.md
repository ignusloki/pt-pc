# Android prototype

Branch: `codex/android-port-prototype`, based on stable Mac commit
`07bc9029c3a6b6b25d7e0fb1ff45c3df107e69da` (1.0.7).
This is an isolated test app, not an Android release or a change to the stable Mac branch.

## Install and play

The local APK is `dist/android/prototype/PT-Android-Prototype-arm64.apk`.
It targets `arm64-v8a`, Android 10 or newer, and a Vulkan 1.3 GPU with the required
descriptor features. The first test target is Galaxy S22 SM-S901B on Android 16;
no wider Android device compatibility is established.

1. Install **P.T. Android Prototype**. The APK contains the engine, fonts, shaders
   and speech models. It contains no original P.T. game assets.
2. Copy your P.T. `.pkg` to the phone, then tap **Select P.T. .pkg** and use Android's
   file picker. Import needs at least 4 GB free space and uses a temporary private
   copy of the package; it preserves existing settings/saves when replacing assets.
3. Leave **Basic touch controls** enabled, or turn it off and connect a controller.
4. Tap **Play P.T.**. When microphone input is enabled, Android requests permission
   before starting the game. Denying it permits play with microphone input unavailable.

The launcher copies the APK runtime assets into a private seekable directory,
verifies them, then starts SDL in a separate game process. PKG extraction reuses
the desktop installer's validation and patched LibOrbisPkg readers. Retail/package
restrictions are the same as the existing installer.

## Controls and settings

The optional left stick moves; the right stick turns the camera. **Menu** pauses/
opens the menu, **OK / Use** confirms or interacts, **Back** cancels, and **Zoom**
holds the existing gamepad zoom input. The direction buttons and **Tab** navigate
settings. These feed SDL's virtual gamepad and reuse the existing game mappings.
Physical controllers continue through the ordinary SDL Android controller bridge.

Initial graphics: **Low**, original textures with a 1,024 px maximum uploaded mip
level, a 960 px wide surface preserving the phone's aspect ratio, and a 30 fps cap.
Actual render size and performance must be checked in the log. HDR, enhanced
textures, ray queries, VR and frame generation are unavailable in this prototype.

The **PC Settings → Microphone** page retains the enable/disable switch and input
test. Android recording explicitly selects the built-in phone microphone with
AAudio instead of substituting a headset/controller input. This bypasses SDL's
default-only Android input selection while retaining SDL's audio conversion and
monitoring. The device row shows **Phone microphone** and cannot select a different
input in this prototype. Capture opens only while the puzzle or menu test needs
it, closes when disabled or its test ends, and stops while the activity is in the
background. No microphone recordings are written by default.

The developer prototype leaves the loop browser available to help regression
testing. Its profile is separate from the Mac profile. Archive/loop thumbnails
may be placeholders because desktop preview subprocesses are disabled on Android.

## Checks on the S22

- Package import completes; the extracted PSARC and QAR hashes match the Mac copies.
- A clean first launch renders the settings page. The phone's microphone prompt
  appears before gameplay when enabled; denied/Off states still allow the game to start.
- Both sticks respond at once, return to neutral when released, and stop when the
  app is backgrounded. Check every visible button and a physical controller separately.
- Hallway/radio, bathroom/mirror, Lisa scenes, maze and loop transitions render
  correctly. Start with Low; then test reflections separately if the baseline is usable.
- Microphone test shows the phone's input, permission denial stays denied, and
  enabling it from the menu can request access. Test the final voice puzzle separately.
- Quit/reopen preserves settings and progress. Background/resume, rotation,
  controller reconnect and audio-device changes need explicit testing.
- Run for 20–30 minutes and compare frame rate, memory and thermal status. A short
  startup or hardware probe is insufficient to establish sustained performance.

The game writes `files/profile/pt.log`, `pt.ini` and its saves in this app's sandbox.
The importer writes `files/launcher.log`. The game logs frame rate, render size,
GPU time/driver budget, process PSS/RSS, system available RAM, thermal status and
this engine's live Vulkan allocations. GPU memory overlaps process/system RAM on
the S22; these numbers must not be added as independent memory pools.

The first PKG import on the S22 took **510.6 seconds** and both archive hashes
matched the Mac extraction. A memory snapshot during import showed **466 MiB PSS**
and **550 MiB RSS**; this is not a measured peak. The first successful rendering
test reached 30 fps at 960x443 on the first settings screen, with about 1,013 MiB
process PSS and 488 MiB of engine Vulkan allocations. These are short startup/menu
measurements, not full-game or sustained performance results.

The user reached the first hallway and confirmed that both touch sticks work.
Original and PC settings display upright, and the overlay's short taps navigate
the menus. An Android presentation fix keeps a valid swapchain when the driver
reports `SUBOPTIMAL` only because Android composes the landscape buffer. Before
that fix the game repeatedly rebuilt graphics buffers and slowed down; afterwards
the starting-room check held **30 fps**, roughly **18–21 ms GPU time**, **1,214 MiB
PSS**, and **524 MiB** of engine Vulkan allocations at 960x443. Thermal status was
0 in those samples. These remain short scene checks.

The microphone test opened **built-in phone device 16 at 16 kHz mono through
AAudio** and showed a live input level. The test also exposed a desktop assumption
in the speech recognizer's preflight: Android libraries are installed in the APK's
native-library directory, while speech models live under the private runtime
folder. Both preflight and runtime loading now use that distinction.
The corrected APK selected `ggml-cpu-android_armv9.0_1` on the S22 and loaded the
base/VAD models plus the small rescue model in about **1.43 seconds**, with two
recognition threads. The user confirmed a responsive input meter and **Jack**
recognition. The log recorded two successful words, with **7.18 and 7.23 seconds**
of decoding time using the rescue model. That delay needs further evaluation in
the final puzzle. A microphone-test snapshot showed **1,659 MiB PSS**, **621 MiB
RSS**, **540 MiB** of engine Vulkan allocations and thermal status **1**, while the
menu remained at 30 fps. These are snapshots, not measured memory peaks.
Turning input **Off** greyed out the microphone test and Android reported the
recording session ended. Input was restored to **On** afterwards.

The APK passed ZIP alignment at 16 KB, and every packaged ARM64 shared library's
ELF load-segment alignment is at least 16 KB. The phone itself uses 4 KB pages;
this packaging check does not establish gameplay compatibility on other devices.

The Mac ARM64 release target still builds. Platform, asynchronous microphone
(dummy input), voice keyword/Unicode-path, settings round-trip and graphics-preset
checks passed. The Android device-report tests and the native Lua regression also
passed, as did the existing desktop localization checks. Windows/Linux runtime and physical-controller checks have not been run as
part of this prototype.

Android's first engine launch exposed a Lua 5.1 weak-table string search that
tripped Bionic FORTIFY. The Android build generates a compatibility copy of that
source using Lua's stored string length. Buffer protection remains enabled, and
the native startup/collection/NUL-semantics regression passed on the S22. Desktop
Lua source and dependency checkouts are unchanged.

## Build

Prerequisites: .NET 10 with the `android` workload; Android SDK platform/build-tools
36; JDK 21; NDK **28.2.13676358**; CMake/Ninja and a host `glslc`.
The build uses the pinned LibOrbisPkg revision
`643477263b2644e0803e0f58b8726ea4e3f3b7d4`, patched as in the desktop installer.

```sh
python3 tools/android/build.py \
  --sdk /absolute/path/to/android-sdk \
  --jdk /absolute/path/to/jdk-21 \
  --liborbis /absolute/path/to/pinned-LibOrbisPkg \
  --source-cache /absolute/path/to/existing-native-build
```

`--source-cache` is optional; it reuses dependency sources and verified voice
models to avoid downloading them again. Native output goes to
`build/android-prototype`; the signed debug APK and checksum go to
`dist/android/prototype`. A production signing key and release packaging remain
future work. Debug signing is for local testing and does not establish Play Store
readiness or compatibility with other Android devices.

Integration follows the pinned
[SDL Android activity/library design](https://wiki.libsdl.org/SDL3/README-android),
[SDL virtual gamepads](https://wiki.libsdl.org/SDL3/SDL_AttachVirtualJoystick), and
[.NET Android Java/native build items](https://learn.microsoft.com/en-us/dotnet/android/building-apps/build-items).
