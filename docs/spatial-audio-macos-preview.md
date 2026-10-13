# macOS spatial-audio preview

Version `1.0.7-spatial2`, built on the `codex/spatial-audio-research` branch for Apple silicon. This is a local test app with generic fixed-headphone spatial rendering and opt-in audio diagnostics. Head tracking, personalized profiles and individual elevated sound objects are not included.

## Open the test copy

The app is `dist/macos/spatial-audio-preview-1/P.T. Spatial Audio Test.app` under the repository. Double-click it. Its launcher uses the existing `game/CUSA01127` assets in `/Users/suporte/PT-Mac` and separate settings at `dist/macos/spatial-audio-preview-1/test-profile/pt.ini`. Keep the repository/game folder in place while using this preview.

The installed game is separate. This copy has game saving disabled and uses its own settings, log and caches. It has the developer loop browser so the test scenes are available without playing through the game again. A backup and hash record of the original profile were placed in `dist/macos/spatial-audio-preview-1/original-profile-backup/`; these are private files, not release assets.

In **PC Settings > Sound**, check:

- **Audio output:** Spatial headphones.
- **Playing through:** Spatial headphones.
- **Microphone input:** Off for the initial listening test.
- **Controller speaker:** Off initially, so directional sounds come only from the headphones.

If Playing through says **Stereo (fallback)**, native processing did not start or stopped. The game remains audible; report that status and the session log described below. The selected Audio output is the saved choice for the next start. Changing it requires quitting and opening the app again.

## Diagnostic logs

The test app's launcher enables `PT_AUDIO_DIAGNOSTICS=1` and writes each session to `logs/spatial-audio/pt-<UTC date/time>-<process ID>.log` in the repository. `logs/spatial-audio/latest.log` points to the newest session. Restarting preserves the previous log, so both Stereo and Spatial headphones comparisons can be inspected. The directory is ignored by Git. The app's existing test profile and the original game's saves are preserved.

Lines containing `audio diag` include:

- Saved output choice, active processing mode, restart pending after a menu change, and any fallback.
- Actual playback device, system default, SDL driver, stream/device sample rates, formats, channel counts and hardware buffer size. Device/format changes are reported during play.
- Native spatial backend configuration, optional head-tracking/personalization property values, and engine voices/listener position and direction.
- Per-channel RMS and peak before spatial processing, including rear/side energy; binaural levels before and after peak protection; protection gain reduction, invalid samples and samples above full scale.
- Callback frame sizes/timing, processed spatial frames, submitted/pending bytes, stream-write errors and a final interval summary on normal exit. A callback exceeding its audio duration is a timing observation, not proof of a device underrun.
- Volume/mute changes, microphone settings and the existing microphone/device lifecycle messages.

Spatial processing intentionally sends **two channels** to SDL/headphones. `active=spatial`, eight renderer channels and increasing `spatial_frames` confirm that the native renderer is working; a two-channel device does not imply stereo processing. RMS/energy/correlation describe the signal and cannot establish how convincing the spatial effect sounds. No audio samples are saved by these diagnostics; numeric measurements are collected in the callback, and log formatting/writes occur on the game thread.

For an A/B check, spend at least 15 seconds listening near the radio while slowly turning the camera, quit normally, change Audio output, then restart and repeat with the same volume and microphone choice. Keep both session logs for analysis. Diagnostics remain off in ordinary builds unless explicitly enabled with the environment variable.

## AirPods comparison

Connect both AirPods to the Mac and select them as the sound output before opening the game. Keep volume and noise-cancellation mode consistent. Turn Apple's separate Spatial Audio/head tracking off for this app if available, to isolate the game's spatial processing. [Apple controls](https://support.apple.com/guide/airpods/control-spatial-audio-and-head-tracking-dev00eb7e0a3/web).

Compare each scene in Spatial headphones and Stereo, restarting between modes. Match perceived loudness rather than judging the louder option as better. Start at a comfortable volume. Turning the game camera should change the sound direction; moving your physical head is not tracked by this preview.

| Priority | Loop / sound | Check |
| --- | --- | --- |
| 1 | Loop 1: radio | Stop and turn the camera slowly through a full circle. Listen for front, side and rear distinctions and smooth movement. |
| 2 | Loop 3: bathroom crying | Face toward and away from the bathroom; approach and leave it. Direction and distance should make sense, without missing room effects. |
| 3 | Loops 5–7: Lisa footsteps/breathing when audible | Follow a moving source by sound; watch for reversed direction, jumps or disappearance. |
| 4 | Loop 10: fridge sounds | Check continuity while moving through the hallway. This initial speaker-bed mode does not preserve individual source height. |
| 5 | Jumpscares, loud transitions, music, menu sounds | Listen for distortion, crackling, abrupt volume changes, lost sounds or altered timing. |
| 6 | Extras / Museum | Play voices and a cutscene; exit the viewer and return to the game. Sound should remain audible and consistent. |

After the first comparison, enable microphone input and use **Microphone test**, then the final loop. Prefer the Mac's built-in microphone first; test the AirPods microphone separately. Using a Bluetooth headset microphone can reduce playback quality, so a quality change only when that microphone opens is a separate routing issue to record. [Apple Bluetooth microphone guidance](https://support.apple.com/en-us/102217).

Finally check pause/resume, switching loops, restarting, and AirPods disconnect/reconnect. Report the loop/sound, selected output, Playing through status, microphone device and whether the issue also happens in Stereo. Physical Intel and Windows listening are separate checks; this packaged preview contains only the arm64 executable.

## Verification performed

The native processor's isolated checks verified each speaker's direction, fixed/irregular frame sizes and a single advancing source clock. The post-render limiter held the loud correlated-channel stress signal to approximately 0.98 full scale while preserving the left/right ratio. An injected failure reused the already-rendered surround block, then resumed the original stereo rendering without advancing that block twice.

Settings migration, original stereo/surround negotiation, controller routing, asynchronous microphone cleanup and localized text/font checks passed. The packaged app also ran the startup/menu/gameplay route on this Mac's real CoreAudio output. This does not replace the user's listening tests with AirPods, real Intel hardware or Windows providers.
