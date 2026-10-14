# Android Beta 1: bringing P.T. to a phone

**14 October 2026 · 1.0.7-android-beta.1**

The first public Android beta is available for testing. It packages the native
P.T. port and a phone launcher into an ARM64 APK. Players select their own P.T.
package, import the game files and launch it on the phone. Original game content
is not included in the download.

This work is based on the fork's macOS **1.0.7** source, which incorporates the
recent LoreanXavier upstream synchronization. Android work stays on
**`codex/android-port-prototype`**, with its own prerelease tag. It has not been
merged into **`codex/macos-arm64-port`**, and does not replace the stable Mac
release or the separate spatial-audio experiment.

## From desktop port to Android app

The game uses the existing C++/Vulkan engine. SDL provides the Android activity,
surface, audio output and controller bridge. A small .NET for Android launcher
handles Android's file picker, package extraction, runtime setup and startup
microphone permission.

The build needed Android-specific shared libraries, Android dependency settings,
Java packaging and mobile resource paths. We also changed shared code for menu
input, speech-library discovery and texture/memory handling. Most platform
behavior is guarded for Android, but the shared build files and interfaces still
deserve desktop regression before any future integration. This is why the beta
remains on a separate branch.

Package import reuses the existing installer extraction code and its patched
LibOrbisPkg readers. The S22's first import took **510.6 seconds**, about
**8½ minutes**. SHA-256 checks showed that the two main archives matched the Mac
extraction. Retail package restrictions remain the same: the app does not
decrypt console-encrypted store packages.

## Making the renderer fit

Our first test device is the **Exynos Galaxy S22 SM-S901B/DS**, running
**Android 16** with the **Xclipse 920** GPU. Its driver supports the individual
BC texture formats used by the tested game files even though it does not
advertise the entire desktop BC family. The Android path checks the formats it
actually uses instead of rejecting that device based only on the family flag.
That does not establish compatibility with other GPUs.

The starting preset is **Low**, with a **960-pixel-wide render surface**, a
**30 fps cap** and a **1,024-pixel uploaded-mip limit** for original textures.
Android texture decoding and upload budgets are smaller than on desktop.
Enhanced textures and desktop GPU extras are unavailable.

The first launches exposed two less visible problems. Lua 5.1's weak-table
string search triggered Android's Bionic buffer checks. The build now generates
an Android compatibility copy that searches using the stored string length;
buffer protection stays enabled and desktop Lua sources are unchanged. An
on-phone collection/string regression passed.

Android also composed the landscape surface differently from the desktop
window assumptions. We corrected presentation orientation and stopped
rebuilding the swapchain repeatedly when the driver reported a harmless
`SUBOPTIMAL` result without a changed surface size. After that fix, a short
starting-room sample held **30 fps** at **960×443**, with approximately
**18–21 ms GPU time**.

## Phone microphone and touch menus

Microphone permission is requested before gameplay when input is enabled. Input
can also be enabled, disabled and tested from the game's settings. Capture uses
the **built-in phone microphone through AAudio**, while speech recognition runs
locally with the existing Whisper backend. Android's native-library directory
and private speech-model directory are handled separately.

The S22 microphone test showed a responsive meter and recognized **“Jack”**.
The base/VAD and rescue models loaded in about **1.43 seconds** with two decoder
threads. Two recorded recognition timings were **7.18 and 7.23 seconds**. That
delay is still a beta concern, particularly for the final puzzle; these tests
do not prove that the entire voice sequence behaves correctly.

Basic touch controls were added after the initial controller-only plan. Both
sticks work together, and users can disable them to use a physical controller.
Menu navigation now uses queued individual taps rather than depending on a
button remaining held across game frames. **PC Settings** opens directly;
**Column** changes columns; Up/Down selects rows and Left/Right changes values.
Gameplay shows only Menu, OK/Use, Zoom and the sticks. Menus hide the sticks and
place their navigation controls at the sides.

The final public package uses a dedicated release signing key and the app ID
**`org.ignusloki.pt.beta`**. It coexists with the earlier local prototype, whose
files and saves are preserved. A **Save diagnostic log** button lets testers
export recent logs through Android's file picker without developer tools.

## What the measurements tell us

Short S22 samples showed approximately **1,214 MiB process PSS** in the starting
room and **1,659 MiB** during the microphone test. Engine Vulkan allocations in
those samples were about **524 MiB** and **540 MiB** respectively. The memory
pools overlap on the phone, so these numbers are not independent RAM and VRAM
requirements. They are also not measured peaks.

The starting-room sample reported thermal status 0; the microphone sample
reported 1. These brief checks do not establish sustained performance. We
recommend **8 GB RAM or more** and S22-class hardware for this first beta,
while leaving the actual minimum RAM requirement unconfirmed.

The touch-menu regression passed **22 expectations**, and the existing
controller-menu regression passed **33** on Apple silicon. The Mac ARM64 release
target and the platform, microphone-worker, voice, settings, graphics-preset and
localization checks also passed during this work. An older PC Settings fixture
has five matching failures in both the stable installed Mac app and this
checkout because its cursor/upscaler expectations are outdated; it has not been
counted as a passing check. Windows/Linux runtime and Intel regression have not
been repeated for the Android branch.

## What we need from beta testers

We need other GPU/driver results, longer sessions, complete loop and final-voice
puzzle tests, and controller/headphone checks. Import interruption, Android
background/resume and rotation also need wider testing. Basic touch controls
cover movement, interaction and settings, but not every desktop extra. Gallery
previews that rely on desktop subprocesses can be placeholders. The loop browser
is deliberately unlocked for regression testing.

The APK passes signature and ARM64 packaging checks, including 16 KiB ZIP/native
library alignment. Our S22 uses 4 KiB pages, so that packaging check is not a
gameplay claim for 16 KiB devices.

Please read the [installation and phone requirements guide](android-beta-installation.md),
then [download the beta](https://github.com/ignusloki/pt-pc/releases/tag/v1.0.7-android-beta.1).
Report your phone model, Android version, settings and reproduction steps, with
the exported diagnostic log when possible. Other phones remain experimental.
