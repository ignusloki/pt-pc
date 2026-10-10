# P.T. for Mac — Apple Silicon preview 5 (1.0.4)

For Apple silicon Macs (M1 and newer) running macOS 14 or later. The native installer and included game runtime are arm64-only. Intel Macs are not supported by these packages.

## Changes

- Incorporates upstream's 1.0.2 gameplay, platform and controller updates while retaining the Mac port's MetalFX synchronization and optional Fast Walk.
- Corrects menu mouse coordinates when switching between windowed and fullscreen modes, including Retina scaling and borders around the rendered image.
- Requests microphone permission before gameplay when microphone input is enabled. Permission requests do not start recording, and denying access does not block gameplay.
- Adds **PC settings (F10) > Sound > Microphone input**. On is the default. Off stops capture, disables the microphone test and device selection, and skips the permission request at the next launch. The choice is saved; the optional keyboard/controller trigger remains independent.
- Includes the supplied P.T. artwork as the default icon for the game and native Setup.

macOS remembers the microphone authorization decision. An already approved app does not ask again; denied access can be changed in **System Settings > Privacy & Security > Microphone**.

MetalFX remains off by default. To enable it, use **PC settings > Upscaling > Apple MetalFX** and start with **Quality**. Fast Walk remains off by default under **PC settings > Extras > Fast walk**.

## Install or update

1. Download **PT-Mac-Setup-arm64.zip** and unzip it.
2. Open **P.T. Mac Setup.app**.
3. Select your own complete P.T. fake PKG or decrypted game dump folder.
4. Choose a writable destination. To update, choose the parent of your existing **P.T. PC Port** folder.
5. Open **P.T..app** inside **P.T. PC Port**.

Keep **P.T..app** and **CUSA01127** together. The installer reuses complete existing game archives when updating and preserves saves, settings and mods outside the app bundle. Use a separate destination to compare another game copy.

Players do not need Homebrew, a Vulkan SDK or .NET. Original P.T. game files are not included. **pt-port-macos-arm64.zip** is the optional portable runtime: place your own **CUSA01127** folder beside **P.T..app**.

## Validation

The user confirmed correct mouse alignment after switching display modes and confirmed that the microphone prompt appeared at startup. The Sound menu was checked with input both On and Off; its live microphone test received 16 kHz input. The scripted microphone menu regression passed all 21 assertions, with settings persistence, voice-matching, display/pointer and packaging checks passing.

The rebuilt package's exact version, source commit, automated checks, architecture, download hashes and signing status are recorded in **BUILD-INFO.json** and **SHA256SUMS.txt**. Testing was performed on an M1 Pro. Intel Macs and other Mac/OS combinations have not been validated for this preview.

## macOS approval

This preview is ad-hoc signed and has not been notarized by Apple. If macOS blocks a trusted downloaded copy, attempt to open it and use **System Settings > Privacy & Security > Open Anyway** if offered. See [Apple's instructions](https://support.apple.com/en-us/102445).
