# P.T. PC Port for macOS 1.0.8

Unpublished Apple silicon and Intel release, synchronized with upstream 1.0.5.

This update synchronizes the Mac fork with [LoreanXavier/pt-pc 1.0.5](https://github.com/LoreanXavier/pt-pc/releases/tag/v1.0.5),
including upstream main through `d0a9b8f`. The fork's version remains independent of upstream:
Mac 1.0.8 follows Mac 1.0.7. We are continuing to reconcile the Mac changes for a future upstream pull request.

## Changes available on Mac

- PC Settings uses tabs for Display, Upscaling, Graphics, Sound, Controls, Accessibility and Extras, with proper confirmation dialogs.
- Portable apps can select a local P.T. fake PKG directly and show extraction progress, or remember an already extracted game folder.
  The separate graphical Setup app still accepts a PKG/folder and a destination of your choice.
- HDR gains paper-white and peak-brightness controls. Exposure, maze lighting, reflections, motion blur and several effects are corrected.
- Loading, shader preparation, texture streaming and allocation recovery are improved. The ending releases unused hallway resources.
- Accessibility options include sound captions, controller gyro and field of view. Controller look sensitivity and quick mouse clicks are fixed.
- The Museum adds halls, clearer unlock hints, counters, music playback, unused content and retroactive guaranteed unlocks for completed saves.
- Game+ can be switched off while retaining its saved tier. Additional gameplay, Lisa, ending and photo-mode fixes follow upstream.
- Upstream's bonus Game+ content and its script, translations and music are included in the Mac app.
- Upstream 1.0.5 adds automatic gyro drift correction and manual calibration, mouse-wheel scrolling and scrollbars in PC Settings,
  quieter menu navigation, a fix for Lisa's footsteps during pause, and a Game+ bathtub Lisa fix after the ending.
- Unix crash diagnostics now record the signal and stack; nonterminal launches preserve stderr in `pt-stderr.log`. F9 records the camera pose.

## Mac compatibility

Startup update checks no longer crash the game because of conflicting JSON parser types. A failed background update check now allows
normal startup to continue. Regression tests exercise the asset and update parsers together and simulate failed update requests.

Apple silicon and Intel remain separate builds, requiring macOS 14 or newer and a compatible Metal GPU. The game continues to use
Vulkan through MoltenVK. Upstream's DirectX 12, ray/path tracing, DLSS and Windows Spatial Sound additions do not become Mac features.
The fork's experimental Mac spatial-audio and Android branches remain separate.

Retained Mac behavior includes startup microphone permission, the saved Microphone input switch, first-boot settings rendering,
fullscreen mouse alignment, optional Fast Walk, forest/P.T. icons and the existing external save/profile location. MetalFX upscaling
and supported Apple-silicon frame generation retain their queue and UI-composition fixes against the new graphics interface.

The native installer preserves game archives and player files and replaces a signed app as one complete bundle. Interrupted-update
recovery now understands these bundles. The portable app includes the same PKG helper, its corresponding source and required notices.
Executable-path handling is corrected on macOS for Unicode/long installation folders.

## Installation and regression

Choose `P.T.PC.Port.Setup-macOS-arm64.zip` for Apple silicon or `P.T.PC.Port.Setup-macOS-x64.zip` for Intel. Portable ZIP/DMG packages
remain available. Downloads contain no original game archives and are signed locally, without Apple notarization.

The profile remains `~/Library/Application Support/pt-port/pt/`. Back up both save slots and `pt.ini` before updating or rolling back.
Museum counts can change because upstream now backfills guaranteed exhibits and adds new entries; a changed percentage alone does not
mean completion or previously earned progress was lost.

See the [integration and release checklist](integration/upstream-1.0.4.md) for verified checks and remaining hardware regression.

The earlier Apple silicon candidate containing upstream 1.0.4 was accepted. Publication is paused while the new `1.0.8-rc3` candidate
containing upstream 1.0.5 is built and tested. See the [latest integration record](integration/upstream-1.0.5.md) for its actual validation
status. Final packages for both CPUs must pass native builds before publication. Intel gameplay for this upstream update has not been
repeated on physical user hardware; performance depends on the Mac's GPU.
