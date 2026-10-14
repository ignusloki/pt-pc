# Android port research — Galaxy S22

Updated 2026-10-13. Baseline: `codex/macos-arm64-port`, commit
`07bc9029c3a6b6b25d7e0fb1ff45c3df107e69da`, stable macOS 1.0.7.
The experimental spatial audio branch is separate. The initial hardware/source
investigation left game code unchanged. Prototype work is now isolated on
`codex/android-port-prototype`; stable Mac and release branches retain their existing commits.

## Result

**The S22 now runs the first Android APK prototype. Direct PKG import completed
with archive hashes matching the Mac extraction; the game rendered the starting
room and first hallway, and the user confirmed both optional touch sticks work.**

The rebuilt presentation path displays the original and PC settings upright and
holds 30 fps at 960x443 in a short Low-settings starting-room check. The startup
permission flow, built-in phone capture and spoken **Jack** recognition also worked. These are
prototype results, not a completed Android release: the full game, physical
controllers, voice puzzle, lifecycle changes and sustained performance still need
regression. See [the prototype instructions and checklist](android-prototype.md).

The initial concern about converting every original texture was resolved by
checking individual driver formats and the actual game archive. The driver does
not advertise the entire BC family, but it supports BC1, BC2 and BC3. The supplied
original archive's valid 2D texture headers use BC1, BC3 and BGRA8. BC7 is used by
the port's generated enhanced texture cache; keep that feature disabled initially.

## Agreed scope

- First device: Galaxy S22 **SM-S901B/DS**, currently **Android 16**.
- Users select a **P.T. .pkg directly**. A folder-only installer does not meet the
  requested first Android version.
- Reuse the existing gamepad mapping and game behavior. The original controller-only
  scope was expanded by the user during implementation: offer **optional basic touch
  sticks and buttons** through SDL's virtual gamepad, alongside physical controllers.
- Use the **phone microphone**, with permission requested before gameplay when
  microphone input is enabled, and the existing menu option to enable/disable it.
- Begin with the original graphics at low settings. This work does not import the
  experimental spatial audio feature.

Recommended application structure: one installed APK with a package importer and
the native game activity. Import the user's assets into its private files folder,
then launch the game against that folder. Generating and installing a second APK
on the phone is unnecessary. The importer UI may use normal Android buttons and
the system file picker. Touch gameplay controls are optional in the prototype.

## Measurements from the user's phone

Captured through authorized USB debugging, with Google Platform-Tools 37.0.1.
The installed model reports `SM-S901B`; its board/SoC identifier is `s5e9925`.

| Item | Observed result |
| --- | --- |
| Android | 16, API 36 |
| Security patch | 2026-08-05 |
| ABI | `arm64-v8a` available; target 64-bit only initially |
| GPU | Samsung Xclipse 920, integrated GPU |
| Vulkan | 1.3.279 |
| Driver | Samsung proprietary 24.0.545 |
| Memory page size | 4,096 bytes |
| OS-visible physical RAM | 7,624,495,104 bytes, approximately 7.10 GiB |
| System available RAM at capture | 3,581,493,248 bytes, approximately 3.34 GiB |
| Swap | Approximately 8 GiB; this is not additional physical RAM |
| Available storage | Approximately 77.8 GiB |
| Thermal state at capture | Status 1; skin sensor 36.4 C |

Available RAM is a snapshot, not an application allowance or measured game usage.
Vulkan device-local heaps on this integrated GPU must not be treated as additional
dedicated VRAM and added to RAM. Capture per-process memory and GPU allocations
separately, explaining their accounting before combining any figures.

Local evidence is ignored by Git and contains no recorded audio:

- `.deps/android-research-20261013/s22-report.json`
- `.deps/android-research-20261013/s22-first-check/vulkan.json`
- `.deps/android-research-20261013/s22-native-gpu-probe/native-probe.json`
- `.deps/android-research-20261013/texture-inventory.json`

## Graphics compatibility

The stable baseline selected Vulkan 1.3 and unconditionally requested 18 features.
The phone reports all except `textureCompressionBC`. The Android branch now enables
that family flag only when supported, retaining the other 17 required features and
checking each texture format/usage at upload. Desktop feature requests are unchanged.

The flag represents support for the whole family. Vulkan permits checking the
properties of individual formats when that feature is not enabled; see
[VkPhysicalDeviceFeatures](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceFeatures.html).
Do not infer that BC1 and BC3 are unavailable merely because the family flag is
false. Check the actual format/usage combination before creating an image.

The standalone ARM64 probe was built with NDK 28.2.13676358 and run on this S22.
It passed these checks:

- Vulkan device creation with the renderer's other 17 requested features, full BC
  disabled, and the supported ASTC LDR feature enabled.
- The existing texture table: 8,192 2D images, 64 cube images and one storage
  buffer, with the existing binding/update flags; layout, pool and set allocation.
- Small sampled/transfer-destination 2D image creation, device-local memory
  allocation/binding and image views for BC1/2/3 in linear and sRGB forms; BGRA8,
  RGBA8, RG8, R32 float; ASTC 4x4 and 6x6 in linear and sRGB forms.
- BC5 and BC7 were rejected by the image format query as expected. They were not
  passed to image creation.

Probe SHA-256:
`9c95626fbce79aa1b466839a064ccc202cc98bad3dd113fe674dc9ef69c6ffac`.
Its temporary executable and directory were removed from the phone after the run.

The prototype implements format negotiation and disables enhanced textures until
a BC7 fallback exists. Desktop devices keep their existing path. Cube image usage, render-target combinations,
shader pipelines and presentation still require real rendering tests.

## Texture and memory investigation

The inventory reads FTEX headers from the user's restored `texture.qar` without
exporting images. It found:

| Format | Valid 2D/cube texture headers | Cube textures |
| --- | ---: | ---: |
| BC1 | 599 | 6 |
| BC3 | 459 | 2 |
| BGRA8 | 1 | 0 |
| BC5 / BC7 | 0 | 0 |

Two 3D headers are outside the existing loader's 2D path and were excluded. No
unknown/invalid headers were found among the remaining entries. The largest
declared 2D dimensions are 4,096 x 4,096.

Declared mip payload estimates across **all** these archive entries:

| Proposed upload policy | Estimated texture payload |
| --- | ---: |
| Original mip levels | 1,747.5 MiB / 1.707 GiB |
| Discard levels larger than 2,048 px | 1,427.5 MiB |
| Discard levels larger than 1,024 px | 592.5 MiB |
| Discard levels larger than 512 px | 184.5 MiB |

These are header-based estimates, not resident scene memory or measured GPU
allocations. They include declared faces/mips and do not validate every external
stream. They exclude alignment, render targets, geometry, CPU copies, caches and
speech models. A largest-texture RGBA8 decode alone could take 64 MiB.

**The existing Reduced texture detail setting does not implement these caps.**
`src/engine/render/scene_frame.cpp` changes sampling bias, while
`src/engine/render/texture_manager.cpp` still uploads all present mip levels. A
mobile cap would need to discard large levels during loading, including before
retaining their CPU data; otherwise it only reduces GPU memory while keeping
unnecessary decoding/transient allocations.

The prototype now reads Android's `MemAvailable` for the existing prefetch guard,
and logs process PSS/RSS, thermal status and this engine's Vulkan allocations.
The stable baseline only read actual system memory on Windows. Further lifecycle
and memory-pressure testing is still required. Measure native/Java heap state, VMA allocation bytes
and driver budget/usage separately, including transitions and speech recognition.

Whole-image RGBA decompression should not be the default solution. The supplied
original package can retain BC1/3; other devices or enhanced caches may require
ASTC or uncompressed fallbacks. Any ASTC work should preserve linear/sRGB usage,
alpha, normal-map conventions, cube faces and mip levels and compare rendered
results. The [Arm ASTC encoder](https://github.com/ARM-software/astc-encoder) is a
candidate library if that becomes necessary.

## Android implementation and remaining validation

| Area | Current desktop assumption | Android implementation / proof |
| --- | --- | --- |
| Build and startup | Executables, static SDL, desktop CMake targets | ARM64 shared game library and SDL Android activity tested on the S22; Mac release target still builds |
| Assets and player data | Files next to executable or a Mac app resources folder | Private asset/runtime/profile paths implemented; restart/update and interruption checks remain |
| Graphics | Full BC family requested, optional desktop upscalers/VR | Original BC1/BC3/BGRA textures, 1024 px mip cap, 960x443 surface and upright presentation tested; full-scene/sustained checks remain |
| Package import | A desktop .NET 10 command-line extractor | SAF selection, private staging and existing extractor tested; import took 510.6 s with matching hashes; cancellation/interruption and failure paths remain |
| Gamepad | Existing SDL gamepad mapping | Virtual gamepad overlay tested by the user; physical controller/reconnect/foreground changes remain |
| Microphone | SDL capture plus Mac-only startup permission helper | Startup permission, AAudio built-in capture, native Whisper/ggml loading, spoken Jack and Off tested; decoding took about 7.2 s; denial, pause/resume and final puzzle need regression |
| Memory and lifecycle | Windows system-memory monitor, desktop prefetch | Android measurements, bounded decode queues, real low-memory/pause/resume handling and cache limits |
| Desktop helpers | Subprocess-generated previews, desktop HTTP/updater paths | Avoid unsupported process/desktop updater paths; offline-first startup and Android-safe preview/export handling |

The current extractor uses a memory-mapped package and a 1 MiB output buffer; it
does not deliberately load the whole package into one managed byte array. That is
useful logic to preserve. Its desktop executable cannot be shipped unchanged in
an APK. The prototype instead hosts the existing `LibOrbisPkg.Core` and extraction
code in .NET for Android; the first import produced identical archives. Assess a
native extractor only if performance or lifecycle limits justify it.

Use Android's [Storage Access Framework](https://developer.android.com/training/data-storage/shared/documents-files)
for package selection. Its result is a content URI, not a desktop path. Stage the
selected package as a seekable app-owned file before invoking the current
extractor, check space for package plus extracted data and temporary/cache files,
and retain the existing complete fPKG/decrypted-data validation rules. No original
game data belongs inside a distributed APK.

Permission declarations alone do not grant recording access. Follow
[Android runtime permission handling](https://developer.android.com/training/permissions/requesting)
and the pinned SDL Android interfaces. Request permission when entering the game
with microphone input enabled, then open capture only when gameplay or the menu
test needs it. Default to the built-in phone mic, and test Bluetooth audio routing.

Build native dependencies with modern NDK tooling and verify APK/native library
alignment for [16 KB page sizes](https://developer.android.com/guide/practices/page-sizes).
This particular phone reports 4 KB; that does not establish compatibility with
other Android 16 devices. NDK r28 or later is the starting toolchain.

## Ordered prototype plan

1. **Android build proof.** Create an isolated branch from this stable Mac baseline.
   Build an SDL/Vulkan APK, establish landscape presentation, resource paths and
   pause/resume, and negotiate the supported BC subset. Keep the desktop build
   definitions unchanged for their targets. Record exact baseline/source/toolchain.
2. **Importer proof.** Keep direct `.pkg` selection as the user flow. Validate the
   existing extractor on Android against the original package and compare the
   extracted archive hashes with the PC extraction. Measure extraction time, peak
   memory, disk use and cancellation/error cleanup separately from gameplay.
3. **First playable build.** Physical controller or optional basic touch controls, original textures, enhanced textures
   and ray tracing off, SDR, VR/frame generation off. Target approximately
   960x540 internal rendering at 30 fps, and start with a 1,024 px mip cap. Log
   actual drawable/render dimensions; a desktop window size is not proof of them.
4. **Scene and memory checks.** First boot/settings, hallway/radio, bathroom/mirror,
   Lisa events, maze and loop transitions. Collect cold/warm startup and memory
   after repeated transitions. If memory is tight, test the 512 px mip cap next.
5. **Microphone and sustained test.** Permission at game startup, built-in mic/menu
   meter, denied/disabled input, final voice puzzle, controller/audio reconnect and
   foreground changes. Compare memory/CPU with speech models loaded. Run 20–30
   minutes while recording frame-time percentiles, memory trends and thermal
   status. Available storage and a successful graphics probe are not FPS results.

Accept the prototype only after the actual game renders correctly, does not lose
settings/progress across normal restarts, survives lifecycle/device changes and
has a stable measured memory/performance baseline. Then decide which quality
settings and device range an experimental Android release can support.

## Reproduce the current checks

The diagnostic tools live under `tools/android/`. GPU probes remain standalone;
`build.py` now builds the real Android engine and launcher through Android-specific
CMake branches. Desktop targets continue to build as desktop executables.

```sh
python3 tests/android_device_test.py
python3 tools/android/collect_device.py --adb /path/to/platform-tools/adb
python3 tools/android/inventory_textures.py --game /path/to/CUSA01127 \
  --out .deps/android-texture-inventory.json
```

To build the native graphics probe with a verified NDK, use its separate CMake
project with `arm64-v8a`, `android-29` and `c++_static`; then run
`tools/android/run_gpu_probe.py` with the explicit adb path, binary and a new output
folder. The runner verifies the S22 model, uses a uniquely named temporary
directory, saves results locally and removes only its own files after the test.

The device report collector uses targeted reads and never installs an app. It
understands the integral floating-point numbers emitted by Android's GPU service
and reports missing/conflicting fields as unknown. Eight parser checks pass.
Google's platform-tools and NDK downloads were checked against the official SDK
manifest checksums before use; the installed toolchain and raw reports stay in
ignored `.deps/android-research-20261013/`.

Additional primary references:

- [Android native Vulkan engine guidance](https://developer.android.com/games/develop/vulkan/native-engine-support)
- [AOSP GPU service implementation](https://android.googlesource.com/platform/frameworks/native/+/7fb707802e/services/gpuservice/GpuService.cpp)
- [SDL Android build and runtime documentation](https://github.com/libsdl-org/SDL/blob/release-3.4.16/docs/README-android.md)
- [Samsung Exynos/Xclipse background](https://news.samsung.com/global/all-about-exynos-1-meet-the-gpu-isp-development-leaders)
