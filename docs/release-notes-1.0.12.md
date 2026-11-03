# P.T. PC Port 1.0.12 — iOS Beta 2

iOS Beta 2 fixes the iPhone 13 startup failure and a crash when enabling AirPlay
Screen Mirroring. The reporting user confirmed that the game starts and AirPlay
works with the test build. This release includes an optimized iOS build and
rebuilt Apple silicon and Intel Mac packages. Android remains an official source
release, with no new public APK.

## Changes since 1.0.11

- Fix unsupported BC texture uploads for scene film grain and lens-flare masks
  on iOS GPUs that require CPU decoding.
- Reduce texture memory on iPhones and iPads with up to 4 GiB RAM by loading
  mipmapped textures at up to 512 pixels. Larger devices retain the 1024-pixel
  limit. Textures may look softer on the lower-memory profile.
- Service iOS events during loading and pause uploads while backgrounded.
  Memory guards now use the actual iOS process allowance.
- Keep AirPlay mirroring on the existing game instance. External display scenes
  no longer start a second game or send lifecycle events to the primary renderer.
- Ship the IPA in the optimized Release configuration with detailed startup
  tracing disabled. Optional diagnostic builds remain available from source.
- Add regression coverage for BC decoding, texture budgets, loading lifecycle
  handling and duplicate scene startup. Keep received diagnostic logs out of Git.

## Downloads

- iPhone/iPad: `P.T.PC.Port-iOS-Beta-2.ipa`.
- Apple silicon Mac: `P.T.PC.Port.Setup-macOS-arm64.zip`, or the portable
  `P.T.PC.Port-macOS-arm64.zip` / `P.T.PC.Port-macOS-arm64.dmg`.
- Intel Mac: `P.T.PC.Port.Setup-macOS-x64.zip`, or the portable
  `P.T.PC.Port-macOS-x64.zip` / `P.T.PC.Port-macOS-x64.dmg`.
- Android: tagged source and the [build guide](android-installation.md).
  Self-built apps use version 1.0.12, code 17. No new APK is supplied.

Mac packages require macOS 14+ and a compatible Metal GPU. They use ad-hoc
signing; follow the [Mac opening instructions](macos.md).

## iOS game files

The IPA needs to be sideloaded. Users must provide their own game files; no
original P.T. game files are included.

Place `chunk1.psarc`, `texture.qar`, and `pathid_list_ps4.bin` directly in the
app's **Documents** folder, visible in Files as **On My iPhone/iPad → P.T.**
A recognized regional folder such as `CUSA01127` inside Documents also works.
In a container-based installation, use the P.T. guest app's Documents folder.
Reopen P.T. after adding the files. Settings and saves remain in
**Documents/data/**. See `IOS-BETA-INSTALLATION.txt` or the [iOS guide](mobile.md).

## Testing and remaining limits

The iPhone 13 reporter confirmed startup, gameplay and AirPlay with diagnostic
build 5 on iOS 26.7.1. Earlier beta tests covered an iPhone 15 Pro Max and a
12.9-inch iPad Pro (5th generation, M1) on iOS/iPadOS 26.6.2. The new optimized
Release IPA is rebuilt from the tagged source; those hardware reports describe
the test builds, not a separate test of the final IPA.

iOS remains beta. Performance, heat, broader hardware support and full
playthrough testing still need work. The lower-memory profile fixes the reported
startup failure; it is not a general frame-rate guarantee. Simulator support is
not provided. Intel packages are cross-compiled and audited; native Intel runtime
and GPU testing have not been rerun for this release.

`SHA256SUMS` and `RELEASE-BUILD.json` record package checksums, exact source and
release validation. Packages contain no original game archives or private
signing material.
