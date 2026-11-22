# P.T. PC Port 1.1.1 for Mac, iOS and Android

This release brings LoreanXavier’s 1.1.0 update and 1.1.1 hotfix to the Mac and mobile fork. It fixes the black frames that could flash over the intro and the opening hallway on Mac. Apple silicon and Intel Mac players can choose between Lorean’s Original HDR and the fork’s Enhanced rendering.

## Gameplay validation

A player tested **1.1.1-rc2 on an Apple silicon Mac** in the **standard part of the game** and confirmed that both Original and Enhanced HDR worked, including the intro and the first hallway lights. The release uses the rendering code from that candidate. Extras, New Game+ and Survive the Night were outside the scope of that playtest.

**On-device gameplay validation is pending for Intel Mac, iPhone/iPad and Android.**

## Mac rendering changes since 1.0.13

- **Original** is the default. It now uses Lorean’s rebuilt floating-point HDR pipeline, display calibration and optional Rich look. The 1.1.1 hotfix preserves detail in bright lamp glass, softens the hard depth-of-field ring around nearby objects, and reduces HDR post-processing work.
- **Enhanced** carries our Mac adaptation of hartapfel’s HDR work forward. It keeps its own colour grading, paper white and UI brightness, bloom and flare controls, perceptual film grain, object-only motion blur and live settings preview. Both shader families incorporate the upstream reflection fixes.
- Choose **Options → PC Settings → Display → Rendering**, select **Original** or **Enhanced**, then restart. Each pipeline remembers its effect settings. Display and upscaling settings are shared. The selector is available on desktop Mac. Mobile builds use Original rendering.
- **MetalFX frame generation** could present an empty frame when interpolation first started or restarted. The Mac renderer now uses the newly created interpolator, waits for a usable result and displays the rendered frame when interpolation is unavailable. Static startup screens bypass interpolation, and changing the window or returning from settings resets frame history. Opening settings or switching apps is no longer needed to clear the black flashes seen in RC testing.
- MetalFX preserves extended-range highlights and presents generated and rendered frames in order. The Mac output uses a consistent scRGB reference white, and the Original pipeline stabilizes lamp glass and flare occlusion under temporal upscaling.
- The distributed Mac apps disable runtime logging and frame capture. Optional diagnostic builds remain available from source. The PKG extraction helpers use a smaller .NET runtime and include their corresponding source and notices. This release also incorporates the fork’s dependency and duplicate-code cleanup.

## Gameplay and engine changes from Lorean

- **Survive the Night** unlocks after NG+4. It adds street survival in first or third person, daily seeded runs, five difficulties, mid-wave objectives and an armory.
- **New Game+** now records the finish count and unlock as soon as the ending completes. The hotfix can recover affected 1.1.0 saves that had unlocked Extras but lost their Game+ progress.
- Custom keyboard bindings, layout-aware prompts, optional sprinting in the house, adjustable motion blur and a resolution picker are included. Visual changes cover flashlight orientation and glare, eye adaptation, colour grading and the restored jacket and sink-baby simulations.
- The shared engine includes Lorean’s path-tracing performance hotfix and refinements to glass, floor reflections and reflected effects on supported renderers. Hardware ray tracing and path tracing require a compatible backend.

Full upstream changelogs and rendering credits are available in [Lorean’s 1.1.0 release](https://github.com/LoreanXavier/pt-pc/releases/tag/v1.1.0), [Lorean’s 1.1.1 hotfix](https://github.com/LoreanXavier/pt-pc/releases/tag/v1.1.1) and [hartapfel’s HDR fork](https://github.com/hartapfel/pt-pc-hdr/tree/dd90825c060722b9c0cbc90fe5f79ad7c302550e). The iOS port builds on [Alex Blount’s contribution](https://github.com/ignusloki/pt-pc/pull/5).

## Downloads and installation

| Platform | Download | Installation |
| --- | --- | --- |
| Apple silicon Mac | `P.T.PC.Port.Setup-macOS-arm64.zip` | Use Setup to import a PKG or game folder. The portable app is also available as `P.T.PC.Port-macOS-arm64.zip` or `.dmg`. |
| Intel Mac | `P.T.PC.Port.Setup-macOS-x64.zip` | The portable app is also available as `P.T.PC.Port-macOS-x64.zip` or `.dmg`. |
| iPhone and iPad | `P.T.PC.Port-iOS-1.1.1.ipa` | Sideload and sign the IPA for your device. Put your extracted game files in the app’s Documents folder. |
| Android arm64 | `PT-1.1.1-arm64.apk` | Install over the signed Android beta to keep imported files and saves. On a fresh install, use the launcher to import your own P.T. PKG. |

Mac packages require macOS 14 or newer and a compatible Metal GPU. They use ad-hoc signing. MetalFX frame generation requires supported Apple silicon and macOS 26 or newer. The mobile packages target iOS/iPadOS 17+ and Android 10+ respectively. Android also requires Vulkan 1.3 and a compatible GPU. Each player supplies their own game files. [Mac guide](https://github.com/ignusloki/pt-pc/blob/v1.1.1/docs/macos.md) · [iOS guide](https://github.com/ignusloki/pt-pc/blob/v1.1.1/docs/mobile.md) · [Android guide](https://github.com/ignusloki/pt-pc/blob/v1.1.1/docs/android-installation.md)

`SHA256SUMS` lists the download hashes. `RELEASE-BUILD.json` records the source revision, signing information and package checks. Windows and Linux players can get Lorean’s builds from the [upstream 1.1.1 release](https://github.com/LoreanXavier/pt-pc/releases/tag/v1.1.1).
