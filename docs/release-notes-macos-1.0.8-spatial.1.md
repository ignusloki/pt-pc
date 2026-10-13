# P.T. PC Port for macOS 1.0.8-spatial.1 — Experimental spatial audio

This is an **experimental prerelease** based on the fork's stable macOS **1.0.7**, published from `codex/spatial-audio-research` for community listening and regression checks. It is separate from `main` and `codex/macos-arm64-port`. The stable release remains 1.0.7, and its normal update checker does not offer GitHub prereleases.

Lorean is working on spatial audio upstream. This fork preview gathers feedback on our current implementation while that work continues; it is not presented as upstream's implementation or a finished replacement.

## Changes

- Adds optional **Spatial headphones** in **PC Settings → Sound → Audio output** on macOS. Stereo remains the default; existing Stereo/Surround choices are preserved.
- Converts the game's horizontal 7.1 mix through Apple's native headphone spatial renderer. It works with stereo headphones, including AirPods, and outputs a two-channel headphone signal.
- Shows **Playing through** separately from the saved next-start choice, including **Stereo (fallback)** if native spatial processing is unavailable or fails.
- Includes shared left/right peak protection, bounded processing blocks and an audible transition to the original stereo path if processing fails.
- Applies the session's output choice to Museum audio/video playback. Changing the saved mode requires a full quit/relaunch.
- Adds optional numeric audio diagnostics for routing, source-channel activity, levels, clipping protection, callback timing and errors. It does not record microphone/audio samples.

**This preview has no head tracking, personalized ear/head profile, Dolby Atmos certification, or individual 3D sound objects.** Source height has already been folded into horizontal speaker channels. The effect may be subtle or unconvincing for some listeners. The Direct 3D mode from the separate Audio Lab app is not part of this game build.

Windows and Linux keep their existing Stereo/Surround paths; this release contains Mac packages only.

## Downloads and installation

Requires **macOS 14 or newer** and the same compatible Metal GPU as stable 1.0.7.

- **Apple silicon (M1 or newer):** `P.T.PC.Port.Setup-macOS-arm64.zip`.
- **Intel:** `P.T.PC.Port.Setup-macOS-x64.zip`.

Each Setup app lets you select your own complete P.T. fake PKG or extracted game folder and choose where to install. Portable app ZIPs and disk images are also provided. Game archives and the standalone Audio Lab/radio clip are not included in the release.

Back up your profile before testing. Installing the app to a separate folder keeps the stable app available, but both versions normally share `~/Library/Application Support/pt-port/pt/`. This preview adds an audio setting and retains the baseline game's saves/settings. Before returning to stable, choose Stereo and quit. Use the stable installer for rollback. Unsigned/notarization handling is unchanged: packages are signed ad hoc and **not Apple-notarized**; see the [Mac opening instructions](https://github.com/ignusloki/pt-pc/blob/v1.0.8-spatial.1/docs/macos.md).

## What to test

1. Connect both headphones, select them as the Mac's output and keep other headphone spatial effects off during comparisons. Check that macOS mono output is off.
2. Choose **Spatial headphones**, fully quit/reopen, and confirm **Playing through: Spatial headphones**. Compare with Stereo after another restart, with the same comfortable volume.
3. Listen near the radio while slowly turning the camera. Compare front, side and rear; check bathroom crying, Lisa footsteps/breathing, fridge sounds, loud transitions and Museum playback. Report no audible difference too.
4. Check pause/resume, loop changes, and headset disconnect/reconnect. After the initial comparison, test microphone input and the final voice puzzle, preferably first with the Mac's built-in microphone. Opening a Bluetooth headset microphone may change the playback format.
5. If anything sounds wrong, return to Stereo and compare the same scene. Include your Mac model/GPU, macOS version, headphones/connection, game loop/sound, requested and active modes, microphone device and `pt.log` in a [fork issue](https://github.com/ignusloki/pt-pc/issues).

The ordinary log is `~/Library/Application Support/pt-port/pt/pt.log` and is replaced on the next launch. Keep a copy after each mode comparison. For detailed signal metrics, launch with `PT_AUDIO_DIAGNOSTICS=1`; see the [diagnostic instructions](experimental-spatial-audio.md).

## Validation and limits

The Apple silicon candidate passed scripted startup/menu/gameplay and Museum checks, settings migration, original Stereo/Surround negotiation, native speaker direction/variable-buffer checks, limiter/failure handling, controller routing and microphone cleanup tests. Manual AirPods sessions confirmed both modes, rear-channel activity, no fallback, no invalid/clipped output samples and no recorded callback-budget overruns. The user reported little audible difference, which remains a listening-quality question.

Both release architectures are built and tested on their corresponding native GitHub Mac runners; exact results, source commit and download hashes accompany the packages. Hosted Intel checks do not replace graphics or spatial listening tests on a physical Intel Mac. This experimental feature has not yet been accepted in Intel gameplay. Audio-device changes, headset microphone combinations and convincing spatial perception still need wider hardware feedback.
