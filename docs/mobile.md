# iOS development prototype

Adapted from Alex Blount (`ilsonb`)'s [PR #5](https://github.com/ignusloki/pt-pc/pull/5),
head `7deae4c591691ba7d072699b87d19efaa02b7586`, onto the shared Mac/Android 1.0.10
source. See [integration decisions](integration/ios-pr5.md). Android continues to
use the [.NET launcher and PKG importer](android-beta-installation.md).

## iOS Beta 1 source release

The `v1.0.11` tag is iOS **Beta 1**. Build and sign the app yourself; no public IPA
or maintainer provisioning profile is supplied. The iPhone 15 Pro Max and M1
12.9-inch iPad Pro worked in short tests on iOS/iPadOS 26.6.2, but the user
reports poor performance on both. Optimization and broader testing remain open.
This is not an App Store or TestFlight release.

On a clean Mac, install the full Xcode app and its iOS SDK, select its developer
tools, and install `cmake`, `ninja`, `shaderc` and `vulkan-headers` with Homebrew.
Clone this repository and check out `v1.0.11`. Then obtain MoltenVK as below.

## Build on a Mac

The integration host has Xcode 27.1 (27A9275), iOS/iOS Simulator SDK 27.1,
CMake, Ninja and host `glslc`. The deployment target is iOS 17.0, a build setting,
not a verified minimum device requirement. The renderer still needs Vulkan 1.3
features through MoltenVK, sufficient descriptor limits and memory.

Download the official MoltenVK **1.4.2** `MoltenVK-ios.tar` from
[Khronos](https://github.com/KhronosGroup/MoltenVK/releases/tag/v1.4.2), verify
SHA-256 `b5d947b1660e6e9fed40b9cd2387e160aaab9e80b775c0cef7e14059405178c1`,
and extract it. Pass its dynamic `ios-arm64/MoltenVK.framework` to the builder:

```sh
python3 tools/ios/build.py \
  --moltenvk /absolute/path/MoltenVK/MoltenVK/dynamic/MoltenVK.xcframework/ios-arm64/MoltenVK.framework \
  --source-cache build/macos-local-validation
```

`--source-cache` is optional; it reuses dependency sources and voice models.
`--decoder-source` optionally reuses the pinned bc7enc_rdo decoder checkout.
Output is the unsigned development bundle `build/ios/pt.app`. It includes shaders,
fonts, `extras.pak`, base.en/VAD voice models and the dynamic MoltenVK framework.
Run `python3 tools/ios/audit.py build/ios/pt.app` to audit the resulting bundle.
No original game files belong in this bundle. Whisper/ggml are statically linked
only for iOS; Mac/Android keep their dynamic runtime and model selection.

## Free account signing and installation

A free Apple account can use Xcode's **Personal Team** for development on personal
devices. [Apple documents](https://developer.apple.com/help/account/basics/about-your-developer-account)
up to 10 App IDs, 3 devices and 3 installed apps per device; provisioning expires
after seven days and requires rebuilding/reinstalling. It does not provide
TestFlight or App Store distribution. Paid membership is not required for this
prototype. Do not revoke existing certificates or delete other apps to set it up.

Generate a separate Xcode project:

```sh
python3 tools/ios/build.py --build build/ios-xcode --xcode --configure-only \
  --moltenvk /absolute/path/MoltenVK.framework --source-cache build/macos-local-validation
```

The initial host setup failed loading `IDESimulatorFoundation` because of a
`DVTDownloads` framework mismatch. This was resolved before device deployment;
Xcode project generation and Personal Team provisioning now succeed after the
user repaired account authentication and renewed the development certificate.

Open `build/ios-xcode/pt_port.xcodeproj` in Xcode, sign in under Settings → Accounts,
select the **pt** scheme and connected device. In pt → Signing & Capabilities,
select your Personal Team, enable automatic signing and choose your own unique
bundle identifier. Set **Code Signing Allowed = Yes** for the pt target (the
project starts unsigned). Use the same identifier/team for future updates to
preserve the container. Xcode embeds/signs MoltenVK before signing the app.
Enable Developer Mode on the device if requested and run from Xcode. Never
uninstall or clear the container to renew a profile.

The optional `ios/deploy.sh` is for an **existing explicit development profile**;
it requires a device ID and never guesses a device or changes the bundle ID.
The Xcode Personal Team route is preferable for creating/renewing a free profile.

## Game files and saves

Extract your own compatible decrypted/fake PKG with the existing Mac Setup into
a **new chosen destination**, or reuse your already extracted folder. The iOS
app does not run the desktop .NET extractor. Encrypted retail PKGs are unsupported.

After installation, use Finder → device → Files → P.T. to copy the extracted
`CUSA01127` folder into Documents. Required files include `chunk1.psarc`,
`texture.qar`, and `pathid_list_ps4.bin`. Files can also live directly in Documents.
The app discovers supported regional title folders by content. For USB transfer
with devicectl, explicitly set `PT_DEVICE`, `PT_PROFILE`, `PT_IDENTITY` and optional
`PT_GAME` before running `ios/deploy.sh`. Only use PT_GAME for a first import;
existing game archives are never overwritten by the script.

Settings, saves, mods and logs are in **Documents/data/**. This is writable and
visible through file sharing. Keep a copy of that folder before experimenting.
A first launch without archives explains where to copy them, then exits. Reopen
after copying. No native Files picker or in-app PKG extraction is implemented;
a future importer needs security-scoped URL access, streaming extraction,
free-space checks and atomic replacement that preserves Documents/data.

## Prototype controls and testing

Left drag moves, right drag looks; X interacts, O goes back, ZOOM holds zoom,
II opens the menu and PC opens port settings. In menus, drag to send directional
navigation; X/O confirm/back. Touches release on interruption, focus loss and
resize; real controller input hides the overlay and a touch restores it. Safe
areas constrain buttons and iOS edge gestures are deferred. The TAB button cycles settings tabs/columns; controller reconnect needs device testing.

Start with Low graphics, 30 fps, HDR/upscalers/frame generation off. Rendering is
half the drawable resolution; `display.render_scale` in pt.ini accepts 0.25–1.
Textures are limited to a 1024-pixel mip before BC decompression to bound memory;
decoded formats preserve sRGB and normal-map channels. The iOS GPU path is paused
while backgrounded. Microphone input uses the existing saved toggle and shared
AVFoundation startup permission flow; denial must allow ordinary gameplay.

Planned user test devices: **iPhone 15 Pro Max**, reported iOS **26.6.2**, and
**12.9-inch iPad Pro (5th generation, M1)**. The connected iPad was verified to run iPadOS 26.6.2.
The PR author's hardware reports are contributor evidence, not local validation.

- Fresh launch without assets, Finder copy, reopen and game discovery.
- Landscape both ways, notch/home-indicator safe areas and menu readability.
- Movement/look, lifting both thumbs, edge swipes, interrupted touches, menu
  opening/closing, all tabs/columns, controller use/disconnect/reconnect.
- Home/lock/Control Center/background during load and gameplay: no GPU work in
  background, no crash or stuck movement on resume; repeat several times.
- Microphone permission grant/denial, saved toggle Off/On, relaunch, meter and
  “Jack” recognition in microphone test; interruptions during capture.
- Hallway, mirror, Lisa, maze, scene transitions, Museum X-photo preview and open
  exhibit; optional Fast Walk, subtitles, saves after restart.
- 20–30 minutes: memory pressure, heat and frame pacing; retain logs.
- Renew signing and install over the same identifier: game files/profile survive.

Compilation, simulator execution and physical-device testing are separate
validation stages. The downloaded framework has only a device arm64 slice;
this prototype does not claim simulator execution support.

## Shared app icon

Both iPhone and iPad use `assets/macos/pt-icon.png`, the same forest artwork as
the Mac app. The iOS build generates opaque size variants and compiles them with
Apple's asset compiler; the original Mac image is unchanged. `make_icons.swift`
uses CoreGraphics/ImageIO from the host Xcode tools, so no Python imaging package
is required. The bundle audit checks the compiled asset catalog and primary-icon
metadata for both device families.
