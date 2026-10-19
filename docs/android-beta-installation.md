# P.T. Android Beta 4 — installation and testing

Version **1.0.9-android-beta.4**, the experimental Android build in the unified **v1.0.9** release.
[Download the APK](https://github.com/ignusloki/pt-pc/releases/tag/v1.0.9).
The file is **PT-1.0.9-android-beta.4-arm64.apk**. Apple silicon, Intel and Android downloads share the same release page;
Android remains beta software.

Existing beta users: install this signed APK over **P.T. Android Beta** to retain
imported files, settings and progress. Do not uninstall or clear storage. Version
code **13** also updates earlier public betas and local test builds through code 12.
The older prototype remains a separate app and keeps its own profile.

The APK includes the port, fonts, shaders and speech-recognition models. **It does
not include P.T.** You must supply your own compatible game package.

## Phone requirements

| Item | Beta requirement or recommendation |
| --- | --- |
| Operating system | The APK's minimum is **Android 10 / API 29**. Only Android 16 has been tested so far. |
| Processor | **64-bit ARM**, with the `arm64-v8a` ABI. No 32-bit ARM or x86 emulator build is provided. |
| Graphics | **Vulkan 1.3**, the port's descriptor-indexing/dynamic-rendering features and limits, and usable **BC1/BC3** compressed texture formats. Vulkan 1.3 alone does not establish compatibility. |
| Memory | **8 GB RAM or more recommended** for the beta. A reliable minimum RAM requirement has not been measured. Virtual/extended RAM does not establish compatibility. |
| Storage | Start with **at least 6 GB free internal storage**, with additional room for your approximately 1.4 GB package if it is not already on the phone. Import requires at least **4 GiB free** in app storage at its initial check; replacing an existing installation can need more. |
| Game files | A complete P.T. fake PKG made from decrypted game files. **CUSA01127 / US** is the tested edition. Encrypted retail/store PKGs cannot be decrypted by this app. |
| Controls | The included basic touch controls, or an Android-compatible controller. Physical controllers are implemented through SDL but still need beta hardware testing. |
| Microphone | Optional for general play; the original final voice puzzle uses it. This build selects the **phone's built-in microphone**, including when headphones are connected. |

The verified phone is a **Samsung Galaxy S22 SM-S901B/DS**, with **Exynos 2200 /
Xclipse 920**, 8 GB nominal RAM and Android 16. A Snapdragon S22 is a different
GPU and has not been verified. Other Galaxy models and other manufacturers are
also unverified. Neither installation success nor an advertised Vulkan version
guarantees that a phone will render the game correctly.

Earlier short S22 samples showed approximately **1.2 GiB process PSS** in the starting
room and **1.6 GiB** during the microphone test. These are snapshots, not peak
memory measurements. The graphics allocations use shared phone memory and must
not be added to those figures as a separate VRAM requirement.

## Install the APK

1. Open the linked GitHub release on your phone and download the APK. The
   accompanying `SHA256SUMS` and `BUILD.json` identify the download and build.
2. Open the downloaded APK in your browser or Files app. If Android asks, allow
   **Install unknown apps** for that browser or file manager, then install
   **P.T. Android Beta**. Follow your phone's security prompts; there is no need
   to disable Play Protect globally.
3. Open **P.T. Android Beta** from your app list. The APK is self-contained;
   a computer, developer mode and USB debugging are not required to play.
4. Put your compatible P.T. `.pkg` somewhere Android's file picker can access,
   such as **Downloads**, then tap **Select P.T. .pkg** and select it. A local
   file avoids having to wait for a cloud provider to download it first.
5. **Keep the app open until import finishes.** It keeps the screen awake while
   importing and preparing runtime files. The first
   S22 import took about **8½ minutes**. The app copies the package, extracts the
   archives and verifies them; the game starts only after a successful import.
   Background import, cancellation and recovery after process termination are
   not yet supported as a complete workflow.
6. Leave **Basic touch controls** checked, or uncheck it and connect a controller.
   Tap **Play P.T.**. On first play, the app also prepares its runtime files.
7. When **Enable phone microphone** is checked, Android requests microphone
   access before gameplay. Choose the permission appropriate for you. Denying it
   still lets the game start, with voice input unavailable.

Android may show slightly different installation wording on different phones.
See Google's [app security guidance](https://support.google.com/android/answer/2812853?hl=en).

## Start with the beta defaults

Use **Low graphics**, **original textures**, and the **30 fps cap** for your
first test. The Android render surface is fixed at **960 pixels wide**, with
height chosen for your screen's aspect ratio; it was 960×443 on the S22.
Original texture uploads are limited to a 1,024-pixel mip level to reduce memory.

HDR, ray queries, desktop upscalers, frame generation and VR are unavailable
in this build. Enhanced Textures was removed upstream. Desktop-style resolution/window choices do not
change the Android activity's fixed render surface. Establish a working baseline
before experimenting with other graphics options or reflections.

## Touch controls

| Where | Controls |
| --- | --- |
| Gameplay | Left stick: move. Right stick: look. **OK / Use**: interact. **Zoom**: hold to zoom. **Menu**: open settings. |
| Original settings | **PC Settings** opens the port's options directly. **Language ▲/▼**, **Bright −/+**, **Flip Y** and **Flip X** adjust the original options. **Back** or **Resume** starts/resumes play. |
| PC Settings | **Up/Down** selects rows. **Left/Right** changes values. **Tab** cycles the new settings tabs; on other pages, **Column** moves between columns. **OK** opens/confirms an option. **Back** returns one page; **Resume** returns to gameplay. |

The two sticks hide while menus are open. Navigation buttons and **Back** hide
during gameplay. The beta intentionally leaves the **loop browser unlocked** so
testers can revisit scenes without completing the whole game first; this does
not mean their save contains a completed playthrough. Basic touch controls do
not expose every controller/keyboard action, including the paired-trigger voice
shortcut. They are not yet a complete replacement for all desktop extras.

## Test the microphone

Open **PC Settings → Sound**, enable microphone input and open **Microphone test**.
Speak **“Jack”**, then pause. The meter should respond and the recognized word
should appear. S22 recognition sometimes took about **7 seconds**; the input
level responds sooner than the recognized text. Final-puzzle timing still needs
more testing.

Input can be disabled in the launcher or in the game's microphone settings.
The device is labelled **Phone microphone**. If permission is denied, use
**Android Settings → Apps → P.T. Android Beta → Permissions → Microphone** to
review it. The app performs recognition on the phone and does not save audio
recordings by default.

## Saves, updates and uninstalling

Game files, settings and saves live in this app's private storage. Selecting a
different package replaces game assets while retaining the existing profile;
it is not a fresh-profile reset.

Future APK updates with the same app ID and release signing key can be installed
over this beta. **Do not uninstall or clear storage to update:** those actions
remove its imported game files and progress. This beta does not enable Android
automatic/cloud backup or provide a save export feature.

The public beta has app ID **`org.ignusloki.pt.beta`** and a dedicated release
signature. Earlier **P.T. Android Prototype** builds use a different app ID and
debug signature. They can coexist, with separate game installations and profiles;
their saves are not automatically migrated. Keep your prototype installed if
you want to preserve that progress.

## Help us test

Check the first settings screen, both sticks, interactions, settings tabs/columns and
Back/Resume. Revisit the hallway/radio, bathroom/mirror, Lisa scenes, maze and
loop transitions. Test microphone On/Off and recognition, quit/reopen, and
20–30 minutes of play. Controller reconnect, headphones, rotation and
background/resume are particularly useful beta checks.

If import fails, check the free space and package type. A **“missing PFSC magic”**
or extraction error can indicate an incompatible, encrypted or incomplete
package. If gameplay closes or renders incorrectly, report your model and
Android version; do not assume another Vulkan-capable phone is supported.

Return to the launcher and tap **Save diagnostic log**. Choose a destination
such as Downloads. This exports recent launcher/game logs and phone/build
information, without game archives, saves or audio recordings. Logs can contain
filenames, error paths or recognized words; review them before sharing.

[Open a GitHub issue](https://github.com/ignusloki/pt-pc/issues) with the beta
version, phone model, Android version, scene/loop, graphics settings, input/audio
device, what happened, and steps to reproduce. Attach the diagnostic text and
screenshots when useful. Please do not attach the game package or archives.
