# P.T. PC Port 1.0.13 — Original and Enhanced rendering

Mac players can now choose between **Original** rendering and an optional
**Enhanced** pipeline with more control over HDR, colour and image effects.
Original remains the default for both new and existing installations.

## What Enhanced adds

- **Better preservation of bright lighting and colour.** Floating-point scene
  and post-processing buffers carry highlight range through bloom, blur, colour
  grading and upscaling, reducing clipping along the rendering chain.
- **Separate game and interface brightness.** Calibrate game white and peak
  highlights, then adjust menus and subtitles independently with HDR UI
  brightness. A brighter scene no longer requires equally bright interface text.
- **HDR colour customization.** Adjust exposure, contrast, highlights, shadows,
  saturation and highlight saturation. Wide-gamut colour handling preserves
  colours beyond the SDR palette. Neutral and HDR-look presets, a grayscale
  blend and a reset option make it easy to experiment or return to neutral.
- **Control over dark tones.** Choose sRGB, Gamma 2.2 or Gamma 2.4 SDR-response
  emulation to tune the reference shadow and midtone response in HDR.
- **Smoother bloom and lens effects.** Improved filtering and separate bloom,
  flare and ghost intensity controls let you keep bright lights while reducing
  their surrounding glow or displaced lens reflections.
- **More flexible motion blur and grain.** Object-only motion blur keeps blur
  on moving objects while leaving camera movement sharp. Perceptual film grain
  adapts to brightness to better preserve black and highlight colour; the
  original grain option is also available.
- **A live settings preview.** See adjustments against the paused game through
  the settings menu. The preview can be disabled.
- **Mac HDR and MetalFX integration.** Enhanced works with native scRGB HDR,
  MetalFX temporal upscaling and supported MetalFX frame generation, carrying
  extended colour and highlight range through the Mac presentation path.

Enhanced can also be used with SDR output. HDR calibration and colour-grading
controls require HDR output to be available and enabled. Performance depends on
the Mac and selected effects; the additional floating-point buffers use more GPU
memory than Original.

## Choosing a pipeline

Open **Options → PC Settings → Display → Rendering**, choose **Original** or
**Enhanced**, then restart the game. The menu shows a restart notice when needed.
Each pipeline remembers its own effect settings, so trying Enhanced does not
overwrite your Original configuration. Display and upscaling settings are shared.

In Enhanced, HDR calibration and colour controls are under **Display**, with
effect controls under **Graphics**. Controls that support live adjustment show
their effect immediately; changing the rendering pipeline requires a restart.

## Mac downloads

| Mac | Installer | Portable app |
| --- | --- | --- |
| Apple silicon (M1 or newer) | `P.T.PC.Port.Setup-macOS-arm64.zip` | `P.T.PC.Port-macOS-arm64.zip` or `.dmg` |
| Intel | `P.T.PC.Port.Setup-macOS-x64.zip` | `P.T.PC.Port-macOS-x64.zip` or `.dmg` |

Requires macOS 14+ and a compatible Metal GPU. Both the installer and portable
app include the PKG extraction helper. Packages are signed ad hoc; follow the
[Mac opening instructions](macos.md). MetalFX frame generation remains
experimental and requires supported Apple silicon hardware and macOS 26+.

This release's new rendering choice is **Mac only**. Android and iOS retain
Original. No new Android APK, iOS IPA or Windows build is included; the existing
[iOS Beta 2 download](https://github.com/ignusloki/pt-pc/releases/tag/v1.0.12)
remains available.

## Validation

The implementation passed 58 Mac regression suites, the eight-case
Original/Enhanced × SDR/HDR × native/MetalFX matrix, both full gameplay routes
with 27 milestones each, and native HDR/MetalFX frame-generation checks in both
pipelines. The reporting user also confirmed that the tested build appears to
work correctly. Final package checks and exact build provenance are recorded in
`RELEASE-BUILD.json` and `SHA256SUMS` beside the downloads.

Intel packages are cross-compiled and audited. Native Intel GPU testing has not
been repeated for this change. Android/iOS compilation and shared-code checks
passed; physical mobile gameplay was not retested as part of this integration.
Automated HDR tests do not measure calibrated brightness on every display.

## Credits

The original port and Hallway Engine are by **LoreanXavier**. Enhanced rendering
adapts the work in **hartapfel's
[pt-pc-hdr fork](https://github.com/hartapfel/pt-pc-hdr)**, pinned to
`dd90825c060722b9c0cbc90fe5f79ad7c302550e`, to this fork's Mac renderer.
The additional MIT notices for perceptual grain and HDR sharpening are included
in the app bundles.

Players must supply their own P.T. game files. No original game archives or
private signing material are included.
