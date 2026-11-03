# iPhone and iPad — iOS Beta 2

Release `v1.0.12` provides `P.T.PC.Port-iOS-Beta-2.ipa`, an unsigned, optimized
Release build that needs to be sideloaded. No original game files or maintainer
provisioning profiles are included. This is a beta release for iOS/iPadOS 17+;
the deployment target does not establish compatibility with every device.

The port is adapted from Alex Blount (`ilsonb`)'s
[PR #5](https://github.com/ignusloki/pt-pc/pull/5). Android continues to use its
[separate launcher and importer](android-installation.md).

## Game files and saves

Users must provide their own game files. Place `chunk1.psarc`, `texture.qar`, and
`pathid_list_ps4.bin` directly in the app's **Documents** folder, visible in Files
as **On My iPhone/iPad → P.T.** A recognized regional folder such as `CUSA01127`
inside Documents also works. For container-based installations, use the P.T.
guest app's Documents folder. Reopen the app after adding the files.

Settings, saves, mods and ordinary logs are in **Documents/data/**. Preserve that
folder when updating. The app uses the same forest icon as the Mac version.
Native file selection and in-app package extraction are not implemented.

## Controls and performance

Left drag moves and right drag looks. X interacts, O goes back, ZOOM holds zoom,
II opens the menu, and PC opens port settings. TAB cycles settings tabs/columns.
In menus, drag to navigate and use X/O to confirm/back. Controller input hides the
touch overlay; touching the screen restores it.

Start with Low graphics and 30 fps. Rendering defaults to half the drawable
resolution. Devices with up to 4 GiB physical RAM use a 512-pixel mipmapped texture
limit; larger devices retain 1024 pixels. Performance and heat need wider testing.

The iPhone 13 reporter confirmed startup, gameplay and AirPlay with test build 5
on iOS 26.7.1. Earlier beta tests covered an iPhone 15 Pro Max and a 12.9-inch iPad
Pro (5th generation, M1), on iOS/iPadOS 26.6.2. The final optimized IPA is a fresh
build; these reports do not establish a full playthrough or minimum supported
device. See the [1.0.12 changelog](release-notes-1.0.12.md).

## Build from source on a Mac

Check out `v1.0.12`. Install the full Xcode app and iOS SDK, select its developer
tools, and install `cmake`, `ninja`, `shaderc` and `vulkan-headers` with Homebrew.
The build uses the device arm64 SDK; simulator execution is not supported.

Use the official MoltenVK **1.4.2** `MoltenVK-ios.tar` from
[Khronos](https://github.com/KhronosGroup/MoltenVK/releases/tag/v1.4.2), SHA-256
`b5d947b1660e6e9fed40b9cd2387e160aaab9e80b775c0cef7e14059405178c1`.
Pass the extracted dynamic framework to the builder:

```sh
python3 tools/ios/build.py --build build/ios-release --configuration Release \
  --moltenvk /absolute/path/MoltenVK/MoltenVK/dynamic/MoltenVK.xcframework/ios-arm64/MoltenVK.framework
python3 tools/ios/audit.py build/ios-release/pt.app
python3 tools/ios/package.py build/ios-release/pt.app dist/P.T.PC.Port-iOS-Beta-2.ipa
```

The IPA needs to be sideloaded. `--source-cache build/macos-local-validation`
optionally reuses dependency sources and voice models. `--decoder-source` can
reuse the pinned bc7enc_rdo checkout. Debug remains the default configuration
when `--configuration` is omitted. `--xcode --configure-only` generates a project
for developers who sign their own builds. Keep the same identifier and signing
account when updating an existing installation.

The numeric bundle build number is derived from the version (1.0.12 → 101200),
or explicitly set with `--build-number`. The bundle contains shaders, fonts,
`extras.pak`, voice models and embedded MoltenVK. Original game files do not
belong in the bundle. Mac and Android retain their own runtime selection.

## Optional diagnostics

Use a separate build folder and add `--diagnostics` to enable detailed startup,
memory, operation and scene tracing. Those builds collect logs under
**Documents/data/ios-diagnostics/**. After a failure, compress that whole folder
before reopening the app. The public Release IPA disables this detailed recorder
and retains the ordinary engine/crash logs under Documents/data.
