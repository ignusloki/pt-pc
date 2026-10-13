# Experimental macOS spatial-audio testing

The optional game feature on `codex/spatial-audio-research` is released as `v1.0.8-spatial.1`. The stable Mac branch is unchanged. See the [release notes and listening checklist](release-notes-macos-1.0.8-spatial.1.md).

## Audio output and restart

In **PC Settings → Sound**, choose **Stereo**, **Surround speakers** or **Spatial headphones**. Changing the choice applies after quitting and reopening. **Playing through** reports the active session, which may differ while a restart is pending or if the spatial processor fell back to Stereo. Headphone output is two channels even when spatial processing is active.

For comparisons, use the same scene, position, camera movement, headphones, microphone selection and volume. The game modes are not loudness matched, so adjust listening level if needed; a louder mix is not proof of improved direction. Start with the radio and both earbuds. Rotating the camera changes the listener's direction; physical head movement is not tracked.

## Detailed diagnostics

Ordinary launches already log requested/active mode, startup routing and spatial failures in `~/Library/Application Support/pt-port/pt/pt.log`. Copy that file after each comparison because the next run replaces it.

Detailed diagnostics are optional and remain off during ordinary launches. To enable them for a session, fully quit the game, open Terminal and run the app's executable with the environment variable below. Replace the quoted app path with the actual location of your installed app:

```sh
PT_AUDIO_DIAGNOSTICS=1 "/path/to/P.T. PC Port.app/Contents/MacOS/pt"
```

To keep each comparison as a separate log, supply an existing writable directory and a distinct filename:

```sh
PT_AUDIO_DIAGNOSTICS=1 "/path/to/P.T. PC Port.app/Contents/MacOS/pt" --log "$HOME/Desktop/pt-spatial-test.log"
```

Repeat with Stereo selected and a different log filename. Starting the app from Finder later returns to ordinary logging. Numeric diagnostics record the selected/active mode, actual playback device, source/device sample rates and channels, native settings, per-channel RMS/peaks, rear/side energy, peak protection, stream-write errors, callback timing and engine/listener state. They save no audio samples. Callback durations exceeding their requested audio duration are observations, not direct device-underrun measurements. Logs cannot establish how convincing spatial direction sounds or inspect additional processing after the game's signal reaches macOS.

## Profile and rollback

Back up `~/Library/Application Support/pt-port/pt/` before testing. A separate installation folder does not isolate the normal profile. This release keeps existing save formats and migrates the older surround setting only when the new `audio.output_mode` key is absent. It also saves the legacy surround key for compatibility with stable executables. Return to Stereo and quit before rolling back to stable 1.0.7.

The standalone Audio Lab is archived separately from this repository and is not included in experimental game packages. The game preview uses the existing horizontal speaker-bed renderer; it does not add the Lab's direct mono 3D mode.
