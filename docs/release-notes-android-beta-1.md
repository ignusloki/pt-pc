# P.T. Android Beta 1 — experimental ARM64 release

**Version: 1.0.7-android-beta.1**

Our first public Android beta lets you select your own P.T. `.pkg`, import its
game files and play on a phone. It includes basic touch controls, the existing
SDL controller bridge, and the phone microphone for local voice recognition.
**No original game assets are included.**

This prerelease is based on macOS 1.0.7, including the recent synchronization with
[LoreanXavier's upstream port](https://github.com/LoreanXavier/pt-pc). Android
build and shared-code changes remain on **`codex/android-port-prototype`**.
The stable macOS branch and latest stable release are unchanged. This APK does
not include the separate experimental spatial-audio work.

- **Download:** `PT-1.0.7-android-beta.1-arm64.apk` in the assets below.
- **Install and phone requirements:** [Installation guide](https://github.com/ignusloki/pt-pc/blob/v1.0.7-android-beta.1/docs/android-beta-installation.md).
- **Development log:** [Android Beta 1 devlog](https://github.com/ignusloki/pt-pc/blob/v1.0.7-android-beta.1/docs/devlog-android-beta-1.md).
- **Source/build details:** [Developer notes](https://github.com/ignusloki/pt-pc/blob/v1.0.7-android-beta.1/docs/android-prototype.md).

## Requirements and tested device

The APK minimum is **Android 10 / API 29**, **ARM64** and **Vulkan 1.3** with the
required descriptor features/limits and BC1/BC3 texture support. Vulkan 1.3 alone
does not guarantee compatibility. We recommend **8 GB RAM or more** and **6 GB
free internal storage**, plus space for your package if needed. A dependable
minimum RAM specification is not yet established.

Verified on an **Exynos Galaxy S22 SM-S901B/DS, Xclipse 920, Android 16**.
Snapdragon variants and other phones are unverified. Start with **Low**, original
textures and the **30 fps cap**. Short starting-room samples held 30 fps at
960×443; this is not a full-game or sustained-performance guarantee.

## Included in this beta

- Android file-picker import using the existing PKG extractor; no PC required.
- Reduced texture/upload budgets and a 960-pixel-wide Android render surface.
- Correct landscape presentation and a fix for repeated swapchain recreation.
- Startup microphone permission, built-in phone capture, enable/disable and word test.
- Simultaneous movement/look sticks and context-sensitive menu buttons. **PC
  Settings** opens directly; **Column** changes menu columns. Navigation controls
  hide during gameplay.
- Unlocked loop browser for beta testing and a **Save diagnostic log** button.
- A signed, non-debuggable Release APK with its own app ID, checksums and build metadata.

## Known limits

Recognition took approximately **7 seconds** in two S22 word tests; the final
puzzle and longer sessions need more testing. Physical controllers, Bluetooth,
other GPUs, rotation and Android lifecycle behavior need wider coverage. Keep
the app in the foreground during import; import interruption/recovery is not yet
a complete feature. HDR, enhanced textures, ray queries, desktop upscalers,
frame generation and VR are unavailable. Some desktop preview features are
placeholders, and basic touch controls do not expose every controller action.

The public **P.T. Android Beta** (`org.ignusloki.pt.beta`) coexists with the earlier
local prototype. Their files and progress are separate and are not migrated
automatically. Updates to this beta should be installed over the app;
uninstalling or clearing storage erases its imported files and saves.

Please [report issues](https://github.com/ignusloki/pt-pc/issues) with the phone,
Android version, scene/loop, settings and reproduction steps. Export diagnostics
from the launcher, review the log and attach it if useful. Do not attach game
archives or the original package.

The release also provides the patched **LibOrbisPkg source** used by the launcher,
its license notices, the installation guide, devlog, `SHA256SUMS` and `BUILD.json`.
