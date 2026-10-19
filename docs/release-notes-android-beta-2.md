# P.T. Android Beta 2 — experimental ARM64 update

**Version: 1.0.7-android-beta.2**

Install this signed update over **P.T. Android Beta** to keep your imported files,
settings and progress. The application ID remains `org.ignusloki.pt.beta`; the older
local prototype keeps its separate files and profile. No original game assets are
included in the APK.

## Changes

- Recover Android's rendering surface when returning from another app, retaining
  the running game state instead of leaving a black screen.
- Add phone vibration through the touch controller, using the game's authored
  rumble and the existing Vibration setting. Feedback stops when backgrounded.
- Fix graphics presets getting stuck on Custom and requesting unavailable ray
  tracing. Android presets use the available effects and original textures.
- Add saved subtitle sizes from 100% to 200%, with a 150% Android default, improved
  glyph edges and wrapping for enlarged subtitles.
- Let the launcher follow portrait/landscape orientation, keep controls clear of
  system bars, and remember touch-control, microphone and subtitle-size choices.
- Show the actual Android render resolution in PC Settings. Display mode and
  resolution are managed by Android; the S22 renders at 960×443.

## Requirements and testing scope

The requirements remain **Android 10 / API 29**, **ARM64**, and **Vulkan 1.3** with
the required features, resource limits and BC1/BC3 texture support. Vulkan 1.1
support has not been added. Other phones, including Snapdragon variants, remain
unverified.

The test device is an **Exynos Galaxy S22 SM-S901B/DS, Xclipse 920, Android 16**.
Start with Low, original textures and the 30 fps cap. The render width remains
960 pixels, preserving the phone's aspect ratio, with a 1,024-pixel uploaded-mip
limit. These checks do not establish sustained performance or complete-game
compatibility.

Phone vibration and the final-puzzle timing need tester feedback. A phone actuator
adapts the controller's motor signals; vibration strength and feel vary by device.
Android may terminate a background game process under memory pressure, requiring
normal startup from the saved profile. Microphone recognition can take several
seconds. Fresh import, physical controllers and wider lifecycle/device coverage
still need regression testing.

Use **Save diagnostic log** in the launcher when reporting issues. Include phone
model, Android version, scene/loop, settings and reproduction steps. Review logs
before sharing, and keep the original package and game archives out of reports.

Build assets: `PT-1.0.7-android-beta.2-arm64.apk`, `BUILD.json`, `SHA256SUMS`, and the
patched LibOrbisPkg source archive required by its license. Developer details are
in `docs/devlog-android-beta-2.md` on the Android branch.
