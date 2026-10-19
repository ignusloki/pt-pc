# P.T. Android Beta 3 — shared engine update

**Version: 1.0.8-android-beta.3 · Android version code 11**

Install `PT-1.0.8-android-beta.3-arm64.apk` over **P.T. Android Beta** to keep
imported files, settings and progress. The app ID and release signature are
unchanged. **Do not uninstall or clear storage to update.** The older local
prototype keeps its separate game files and profile.

The APK contains the port, runtime files and local speech models. **It contains
no original P.T. archives.** New installations require the player's own compatible
fake PKG; see the [installation guide](https://github.com/ignusloki/pt-pc/blob/v1.0.8-android-beta.3/docs/android-beta-installation.md).

## Changes since Android Beta 2

- Synchronize the shared engine and game code with macOS 1.0.8, including its
  graphics abstraction and newer shared content/runtime assets.
- Adapt touch navigation to the new PC Settings tabs. **Tab** cycles tabs; other
  pages retain **Column**. Up/Down selects rows, Left/Right changes values, OK
  confirms and Back returns. Gameplay retains its minimal buttons and sticks.
- Fix Android startup being rejected by a check for an optional Mac compute-bloom
  pipeline. Android keeps the existing render-pass bloom path.
- Keep HDR unavailable in Android settings instead of exposing the desktop
  display control. Preserve original textures, unavailable ray tracing/enhanced
  textures, the mip cap and bounded texture loading.
- Include a detailed Vulkan capability report in the launcher's **Save diagnostic
  log**, including on devices rejected before the renderer starts.

Beta 2's subtitle-size controls, saved launcher preferences, phone vibration and
background/resume recovery are retained.

## Requirements and verified scope

Requirements remain **Android 10 / API 29**, **64-bit ARM**, and **Vulkan 1.3**
with the renderer's required features, descriptor limits and usable BC texture
formats. **Vulkan 1.1 and 1.2 support has not been added.** An advertised Vulkan
version alone does not guarantee compatibility.

The verified device is the **Galaxy S22 SM-S901B/DS, Exynos 2200 / Xclipse 920,
Android 16, 8 GB RAM**. Other phones remain unverified. Start with **Low**, original
textures and the **30 fps cap**. The 960-pixel-wide surface preserves the phone's
aspect ratio (960×443 on this S22); original texture uploads retain a 1,024-pixel
mip limit.

The integration checks covered signed-package startup, native gameplay/menu
rendering, preset changes and persistence, the real resolution, diagnostic
export, and Settings/Home/Recents return in the same game process. Recent
stationary scene/menu samples averaged about 30 fps, with occasional frame-time
spikes; this is not a full-game performance guarantee.

Fresh import/first boot, a complete playthrough, physical controllers, speech
recognition and final-puzzle rumble need further tester coverage on this new
engine base. Recognition can take several seconds. Android can terminate a
background process under memory pressure, requiring startup from the saved
profile. Physical rumble strength and final-puzzle timing remain for testers.

## Reporting and downloads

Use **Save diagnostic log** in the launcher. Include phone model, Android version,
scene/loop, graphics settings and reproduction steps. Review the exported text
before sharing. Do not attach game packages or archives.

Release assets: the signed ARM64 APK, `BUILD.json`, `SHA256SUMS`, and
`LibOrbisPkg-Android-source.zip` containing the patched dependency source and
relinking instructions. Desktop releases are unchanged by this Android prerelease.
