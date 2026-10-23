# P.T. PC Port 1.0.10 — macOS and Android Beta 5

This release brings LoreanXavier's upstream **1.0.8** into the shared Mac and
Android branch and fixes the Museum's black X-photo preview. We are continuing
to align this fork with [Lorean's upstream project](https://github.com/LoreanXavier/pt-pc)
while preserving the Mac and Android adaptations.

## Downloads

| Platform | Recommended download |
| --- | --- |
| Apple silicon Mac (M1 or newer) | `P.T.PC.Port.Setup-macOS-arm64.zip` |
| Intel Mac | `P.T.PC.Port.Setup-macOS-x64.zip` |
| Android, 64-bit ARM | `PT-1.0.10-android-beta.5-arm64.apk` |

Mac portable ZIPs and DMGs are also available. Mac requires **macOS 14 or newer**
and a compatible Metal GPU. The apps retain ad-hoc signatures and are not Apple
notarized; follow the [Mac installation guide](https://github.com/ignusloki/pt-pc/blob/v1.0.10/docs/macos.md)
for opening a downloaded app.

Supply your own compatible P.T. fake PKG or decrypted game folder. **No original
P.T. game archives are included.** Run Mac Setup over your existing installation
to retain game files and profiles. For Android, install the APK over **P.T.
Android Beta** without uninstalling or clearing storage. Version code **15**
updates Beta 4 and local test builds through code 14 with the same beta signing
certificate and application ID. The older prototype keeps its separate data.

## Changes

- Integrate upstream 1.0.8: startup/crash diagnostics, Vulkan layer handling,
  display and refresh-rate policy, audio-output selection and controller-speaker
  routing fixes, regional game-folder recognition, portable updates, photo-mode
  grids, exit-door camera handoff and keyboard-prompt corrections.
- Fix the Museum's **X mark photo** preview and strip thumbnail on Android.
  Android cannot start the desktop thumbnail generator. This flat photograph now
  uses its installed model, original texture and UVs directly, including on Mac
  without a thumbnail cache. Opening the interactive 3D exhibit still works as
  before; the game's unlock rules remain in place.
- Package upstream's new **extras.pak** on Mac and Android, replacing the older
  loose bonus-resource folder.
- Preserve Mac HDR brightness, MetalFX HDR frame-generation fixes, fullscreen
  pointer alignment, startup microphone permission and saved microphone toggle,
  optional Fast Walk, the forest icon and graphical PKG/folder installers.
- Preserve Android touch/menu navigation, subtitle sizing, phone microphone,
  Vulkan diagnostics, memory limits and recovery after returning from another
  app. Correct the remaining merged metadata-parser references and align the
  settings regression with upstream's existing legacy-quality migration.

DirectX-only features remain gated to their supported platforms. This release
does not incorporate the separate experimental spatial-audio branch.

## Android remains experimental

Requirements remain **Android 10 / API 29**, **64-bit ARM**, and **Vulkan 1.3**
with the required features, descriptor limits and usable BC texture formats.
Vulkan 1.1/1.2 support has not been added. **8 GB RAM or more is recommended**;
a reliable minimum RAM requirement has not been established.

The verified phone is the **Galaxy S22 SM-S901B/DS**, Exynos 2200 / Xclipse 920,
Android 16. Other devices remain unverified. Start with **Low** and the **30 fps
cap**. The fixed 960-pixel-wide surface and 1,024-pixel texture-mip limit remain.
HDR, ray queries, desktop upscalers, frame generation and VR are unavailable on
Android. Other 3D-model/cutscene preview frames can still be empty on Android;
this fix addresses the X-photo preview specifically.

Read the [Android installation and testing guide](https://github.com/ignusloki/pt-pc/blob/v1.0.10/docs/android-beta-installation.md).
For reports, include the phone model, Android version, settings, reproduction
steps and **Save diagnostic log** output. Review the log before sharing it.

## Validation and build provenance

These packages are built locally from the release tag's source commit. Apple
silicon uses the normal native build checks, bundled voice smoke test and
installer self-test. The X-photo preview and opened exhibit were verified on
Apple silicon and the connected S22 during candidate testing. The final signed
APK is checked for the beta identity, certificate, native contents and 16 KiB
alignment.

The Intel package is **cross-compiled on Apple silicon**. Architecture,
dependencies, signatures and package contents are audited; native Intel gameplay
and Intel GPU tests have not been rerun for this update. A complete playthrough,
fresh imports, additional Android devices and controller hardware also need
further testing on this base. MetalFX frame generation remains experimental and
requires supported Apple silicon with macOS 26 or newer.

`BUILD.json` records Android provenance; `RELEASE-BUILD.json` records the unified
source, validation scope and package hashes. `SHA256SUMS` covers the downloadable
files. The LibOrbisPkg corresponding-source archive and the Mac packages' own
source/notices accompany the extractors. CI investigation was deferred for this
release. Windows and Linux binaries are distributed by upstream and are not
included on this release page.
