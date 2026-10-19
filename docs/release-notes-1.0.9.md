# P.T. PC Port 1.0.9 — macOS and Android

One release page now provides **Apple silicon**, **Intel Mac** and **Android**
downloads built from the same source revision. **Android remains experimental
Beta 4** (`1.0.9-android-beta.4`, version code 13).

All downloads contain the port and required runtime components, **no original
P.T. game archives**. Supply your own compatible fake PKG or decrypted game
folder; the Android launcher imports a compatible fake PKG on the phone.

## Choose your download

| Platform | Recommended download |
| --- | --- |
| Mac with Apple silicon (M1 or newer) | `P.T.PC.Port.Setup-macOS-arm64.zip` |
| Intel Mac | `P.T.PC.Port.Setup-macOS-x64.zip` |
| Android, 64-bit ARM | `PT-1.0.9-android-beta.4-arm64.apk` |

Mac portable apps are also available as `P.T.PC.Port-macOS-arm64.zip` /
`P.T.PC.Port-macOS-x64.zip` and the corresponding DMGs. Use **macOS 14 or newer**
and a compatible Metal GPU. The Mac apps are signed ad hoc, without Apple
notarization; see the [Mac guide](https://github.com/ignusloki/pt-pc/blob/v1.0.9/docs/macos.md)
for opening and installation steps.

Use the Mac Setup over your existing installation to keep game files and
profiles. Install the Android APK over **P.T. Android Beta**; **do not uninstall
or clear storage to update**. Its app ID and release signature are unchanged,
and code 13 updates both earlier public betas and internal tests through code 12.
The older Android prototype keeps its separate files and progress.

## Shared engine update

This fork incorporates upstream **1.0.7** (`ac71cac`), following the earlier
upstream 1.0.5 base. The shared update includes lighting, point-shadow and
reflection corrections, start-room exposure and peephole fixes, bathtub drips
linked to water effects, fuller Museum transcripts, an Accessibility FPS counter,
stable selection in PC Settings during background changes, update prompts, and
photo-mode settings persistence with a separate reset action.

Upstream removed **Enhanced Textures** and the **background pause/mute options**.
Old settings are ignored without discarding ordinary graphics or audio settings.
New DirectX-only neural rendering and ray-regeneration features retain their
platform gates; they are not enabled on Mac or Android by this release.

## macOS

- Keep the tested HDR brightness correction, MetalFX upscaling and HDR frame
  interpolation fixes, fullscreen mouse alignment, optional Fast Walk and saved
  microphone On/Off control with startup permission handling.
- Resolve game-data folders linked through symlinks or Finder aliases to mounted,
  writable drives while retaining existing links and saves.
- Retain native graphical PKG/folder installers, portable PKG import, forest/P.T.
  icons, Game+ resources and whole-bundle update/repair behavior.
- Adopt the newer controller-audio handling and preserve the desktop voice CPU
  safety probe. Keep isolated runtime metadata parsing and legacy neural-quality
  migration fixes.

MetalFX frame generation remains experimental, Off by default, and requires
supported Apple silicon with macOS 26 or newer. Intel builds retain their
supported rendering/upscaling paths. Physical Intel gameplay and controller
hardware coverage have not been repeated for this update. Some Czech/Polish
MetalFX help text is missing.

## Android Beta 4

- Bring Android Beta 3's launcher, native renderer and controls onto the newer
  shared engine while retaining the tested Android-specific platform paths.
- Retain graphics-preset recognition/persistence, subtitle sizing and smooth
  glyph coverage, saved launcher preferences, phone vibration and recovery after
  returning from other apps. PC Settings uses **Tab** on tabbed pages and
  **Column** on other pages; gameplay keeps its minimal buttons and sticks.
- Keep built-in phone microphone capture through AAudio and local Whisper
  recognition. Adapt to the newer desktop voice probe without attempting to
  re-execute Android's activity process.
- Retain the Vulkan capability report in **Save diagnostic log**, including on
  rejected GPUs. **Vulkan 1.1/1.2 support has not been added.**

Android requires **Android 10 / API 29**, **64-bit ARM**, **Vulkan 1.3** with the
required renderer features/descriptor limits, and usable BC texture formats.
An advertised Vulkan version alone does not guarantee compatibility.

The verified phone remains the **Galaxy S22 SM-S901B/DS**, **Exynos 2200 /
Xclipse 920**, **Android 16**, 8 GB RAM. Other phones remain unverified. Start
with **Low**, original textures and the **30 fps cap**. The fixed 960-pixel-wide
surface preserves the phone's aspect ratio (960×443 on the S22); texture uploads
retain the 1,024-pixel mip limit and bounded background loading. HDR, ray queries,
desktop upscalers, frame generation and VR remain unavailable.

The signed shared-engine candidate was checked for startup, saved preferences,
gameplay/settings rendering, actual resolution, microphone/runtime initialization,
Settings/Home/Recents recovery in the same process, normal exit and diagnostic
export. The final release APK receives the same signed-package startup/recovery
check before publication. Recent stationary scene/menu samples held near 30 fps
with occasional frame-time spikes; this is not a full-game performance guarantee.
Fresh import/first boot, full playthroughs, physical controllers, spoken-keyword
recognition and final-puzzle rumble need further tester coverage on this base.
Recognition can take several seconds. Android may terminate a background process
under memory pressure, requiring startup from the saved profile.

Read the [Android installation guide](https://github.com/ignusloki/pt-pc/blob/v1.0.9/docs/android-beta-installation.md).
Use **Save diagnostic log** when reporting issues, with phone model, Android
version, settings and reproduction steps. Review the text before sharing; do
not attach game archives or packages.

## Build provenance

Apple silicon and Intel Mac packages are built and tested on native GitHub
runners at the release tag's exact source commit. The APK uses the retained beta
signing certificate. `BUILD.json` records Android provenance; `RELEASE-BUILD.json`
records the unified release source/workflow and asset hashes; `SHA256SUMS` covers
the downloadable packages. The LibOrbisPkg source archive supplies the patched
Android dependency and relinking instructions; Mac packages contain their own
extractor source/notices. Windows/Linux packages and experimental spatial-audio
releases are not part of this release.
