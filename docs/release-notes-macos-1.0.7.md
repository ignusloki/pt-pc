# P.T. PC Port for macOS 1.0.7

This release brings our macOS branch in sync with [LoreanXavier/pt-pc](https://github.com/LoreanXavier/pt-pc) through
upstream commit `61139aa` on the 1.0.3 line, while retaining this fork's Mac installers and fixes. We are continuing
the integration work so these Mac changes can be proposed to Lorean's repository in a future pull request.
The fork's release number is independent of upstream's release number.

## Upstream changes included

- Mods can add or replace glTF models and change objects, lights and collision through `level.json`.
- Museum navigation, model inspection, lighting and audio transcripts have been improved.
- Photo mode, the ending camera, moving-camera blur, HDR and several image effects have been corrected.
- Gameplay fixes cover the Gouge it out replay/respawn state, the final-loop X photo, Lisa's behavior and other
  differences from the original game. Game+ announces the current tier once.
- Microphone opening runs in the background. Log and crash-report sharing remain opt-in and identify the
  upstream developer as the recipient.

See [upstream's full 1.0.3 changelog](https://github.com/LoreanXavier/pt-pc/blob/v1.0.3/docs/release-notes-1.0.3.md).

## Mac integration

- Reconciled the upstream renderer and MetalFX interfaces with our Mac performance changes.
- Fixed Vulkan/Metal submission recovery when an upscale pass fails, motion-blur target formats and UI composition
  on interpolated frames.
- Fixed microphone worker lifetime and cancellation handling.
- Retained startup microphone permission, the saved microphone On/Off setting, first-boot settings rendering,
  fullscreen mouse alignment, optional Fast Walk and the forest/P.T. icon in both apps.
- Apple silicon and Intel each have a graphical PKG/folder installer and a portable app. Updates follow this fork's
  Releases page.

## Installation and compatibility

Requires **macOS 14 or newer** and a compatible Metal GPU. Select your Mac's CPU:

- **Apple silicon (M1 or newer):** `P.T.PC.Port.Setup-macOS-arm64.zip`.
- **Intel:** `P.T.PC.Port.Setup-macOS-x64.zip`.

Unzip the Setup app, open it, select your own complete P.T. fake PKG or decrypted game folder, and choose an
installation location. Updating an existing installation preserves game archives, saves, settings and mods.
Back up your profile before updating or rolling back; this update also includes upstream's save-format changes.
Original game archives are not included in these downloads. Portable ZIPs and disk images are available for both CPUs.

Downloads are signed locally and are **not Apple-notarized**. Follow the opening instructions in the
[macOS guide](https://github.com/ignusloki/pt-pc/blob/v1.0.7/docs/macos.md).

MetalFX upscaling depends on GPU support. Experimental frame generation requires supported Apple silicon and
macOS 26 or newer; it remains Off by default. On Intel, start at 1280 × 720, Low, 30 FPS, Reflections Off and
Enhanced textures Off. Integrated and older GPUs may lack required rendering features.

## Validation

The integration candidate passed Apple silicon gameplay regression, startup/menu rendering and MetalFX checks.
The release packages are built on native Apple silicon and Intel GitHub runners, with game, extractor, packaged
voice, dependency, signature and installer tests. Build details and download hashes accompany the release.
Native Intel gameplay was checked for 1.0.6 on a 2019 MacBook Pro; this update's Intel GPU gameplay has not been
retested on that machine. Windows and Linux binaries are not part of this Mac release.
