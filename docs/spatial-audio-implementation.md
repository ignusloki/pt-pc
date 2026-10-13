# Spatial audio implementation plan after v1.0.7

Reviewed 13 October 2026. Baseline: released Mac v1.0.7, `07bc9029c3a6b6b25d7e0fb1ff45c3df107e69da`. Work branch: `codex/spatial-audio-research`; the release branch was merged into it at `6b6645dbeac6af8f5ecd497ba4d0c46f7112af76`.

## Scope of the first playable version

Add an optional **Spatial headphones** output mode for macOS and Windows, off by default. Use the existing eight-channel mix. Keep Stereo and the existing speaker Surround mode available. The first implementation uses fixed headphone rendering; AirPods head tracking, personalization and individual sound objects are later work.

The game's current banks, PKG conversion, saves, game events and microphone puzzle do not need replacement. Source height is already reduced to horizontal speaker panning before the eight-channel output; this first version cannot recover it. See [the feasibility report](spatial-audio-feasibility.md) for the later object-audio scope and hardware test matrix.

## Verified compatibility with the latest release

`git diff b05adee..07bc902 -- src/engine/audio src/game/game_sound.cpp src/engine/platform/settings.cpp src/engine/platform/settings.h` is empty. Existing surround rendering and the audio settings storage boundary are unchanged by the upstream synchronization. The merge into this branch had no conflicts.

The adjacent microphone and controller code did change. Microphone opens now have cancellation and worker cleanup; Bluetooth haptics have a bounded asynchronous HID writer. Those are existing v1.0.7 behaviors to preserve, not reasons to move them into the spatial backend.

The main sound session is initialized in `src/main.cpp` through `GameSound::Init`. Archive theater creates a second `GameSound` in `src/game/archive_theater.cpp:109`, currently with the default stereo mode. Include both sessions when propagating the output mode. The theater can exist while the main session is paused, so native playback must be tested with two initialized sessions and when returning to the main game.

## Changes by area

| Area / files | Required change | Acceptance condition |
| --- | --- | --- |
| `src/engine/audio/audio_engine.h/.cpp` | Replace the surround-only choice with `Stereo`, `Surround`, `SpatialHeadphones`; separate requested mode, active backend and renderer/device channel counts | Selecting Stereo still runs `SoundEngine::Render(..., 2)`; Mac Spatial renders eight channels but delivers two |
| New platform adapters under `src/engine/audio/` | Mac bed-to-stereo processor; Windows spatial output worker; unsupported-platform stub | Platform code is guarded and Linux retains its existing outputs |
| `src/engine/audio/sound_system.h/.cpp`, `src/game/game_sound.h/.cpp` | Pass the output mode; expose active/fallback status; ensure one renderer owns the engine at a time | Native Windows output counts as an open playback session without requiring an SDL stream |
| `src/game/archive_theater.h/.cpp`, `src/main.cpp` | Inherit the active session's output choice when entering the museum theater | A pending restart setting does not change only the theater's sound; leaving theater restores normal playback |
| `src/engine/platform/settings.h/.cpp` | Add explicit `audio.output_mode`; migrate absent key from `audio.surround`; validate values | Existing stereo and surround users keep their selection; invalid values use Stereo; progress and voice settings are preserved |
| `src/main.cpp`, `src/game/ui/pc_settings.cpp`, translated menu tables | Replace Surround On/Off with Audio output choices; explain restart and headphone requirements; show active mode/fallback | The menu distinguishes the saved next-start selection from actual output; Linux offers supported modes only |
| `CMakeLists.txt`, `cmake/MacOS.cmake` | Link Apple AudioToolbox/CoreAudio for the Mac implementation; put Windows COM/system dependencies on `pt_engine`, which tests also link | Both Mac architectures and Windows compile; Linux does not include Apple/Windows headers |
| Audio tests and native experiments | Mapping, migration, lifecycle, fallback, no repeated engine render, peak protection and playback tests | Original stereo/surround captures remain valid; failures leave audible output and explain the active mode |

Do not use an eight-channel device request as proof of spatial rendering. Existing `AudioOutput::Open` resolves stereo endpoints to a two-channel engine mix. A selected spatial mode must explicitly use its native path and report whether it actually started.

## Mac output sequence

Apple documents a spatial mixer that receives multichannel game-engine output and produces headphone stereo. [Apple's multichannel sample](https://developer.apple.com/documentation/audiotoolbox/generating-spatial-audio-from-a-multichannel-audio-stream).

1. Open the existing SDL playback path with **stereo stream input at 48 kHz**. SDL can convert it for the device's supported rate/format.
2. Configure one `AUSpatialMixer` input as an eight-channel `MPEG_7_1_C` ambience bed; choose generic headphone output. Disable additional distance attenuation and reverb. Query optional properties, especially on Intel.
3. Before resuming playback, allocate the surround input, planar conversion, stereo result and protection buffers. Break large requests into bounded native slices; handle zero-byte requests without advancing the engine. SDL reports requests in the stream's input format, so compute frames from **two** channels, while rendering **eight**. [SDL callback contract](https://wiki.libsdl.org/SDL3/SDL_SetAudioStreamGetCallback).
4. Render each source block once with `SoundEngine::Render(..., 8)`. Convert SDL order `FL FR FC LFE BL BR SL SR` to Apple order `FL FR FC LFE SL SR BL BR` using indices `{0,1,2,3,6,7,4,5}`. The AU's pull callback copies the prepared block; it must not render the engine again.
5. Keep the bed in listener-relative coordinates. The engine already applies camera orientation; adding that rotation to the native bed again would double it.
6. Apply stereo-linked output protection after native rendering and submit the final two-channel PCM to SDL. Measure effects on transients and level matching against the original stereo reference. Keep this processing confined to Spatial mode.
7. On shutdown, stop/detach the SDL callback before disposing the AU or engine. Keep errors and UI notifications outside the real-time callback.

The [new streaming experiment](research/spatial-audio-20261013/README.md) verifies variable request sizes and speaker directions offline on this Mac. It does not verify playback callbacks under game load, routing or listening quality. A runtime failure after rendering an eight-channel block must not re-render that block as stereo: fold the already-produced PCM for the transition, then use the original two-channel path for subsequent blocks. Test this with an injected failure and engine-clock assertions.

The stress experiment's output peak was approximately **3.1465**, above full scale, despite every input being bounded below one. It used seven correlated tones and silent LFE. This demonstrates the need for output protection, not a suitable fixed gain value for actual gameplay. Capture busy game scenes before selecting the limiter/headroom configuration.

## Windows output sequence

Use provider-neutral `ISpatialAudioClient` / `ISpatialAudioObjectRenderStream`, rather than the Windows-Sonic-only HRTF interfaces. This lets the player's configured Sonic, Atmos or DTS renderer consume the same stream. The player enables the provider in Windows; our game reports fallback when it is unavailable. [Microsoft integration examples](https://learn.microsoft.com/en-us/windows/win32/coreaudio/render-spatial-sound-using-spatial-audio-objects).

1. On the output worker, initialize COM and open the default multimedia playback endpoint. Query `IsSpatialAudioStreamAvailable`, enumerate mono object formats, and select a supported float format. Prefer 48 kHz; if no compatible format/conversion exists, fall back rather than feeding the wrong sample rate.
2. Query `GetNativeStaticObjectTypeMask` and validate the needed horizontal speaker bed. Do not request only the supported subset and silently discard other channels. The first implementation can reject incomplete beds and use audible stereo; alternative bed folding needs its own mapping tests. [Supported speaker mask](https://learn.microsoft.com/en-us/windows/win32/api/spatialaudioclient/nf-spatialaudioclient-ispatialaudioclient-getnativestaticobjecttypemask).
3. Query `GetMaxFrameCount` for the selected format and preallocate buffers. Set `MinDynamicObjectCount=0` and `MaxDynamicObjectCount=0`; stage 1 uses static speakers only. A zero dynamic-object budget alone is not the test for this mode. [Frame capacity](https://learn.microsoft.com/en-us/windows/win32/api/spatialaudioclient/nf-spatialaudioclient-ispatialaudioclient-getmaxframecount).
4. Start the event-driven stream. On each update, obtain the requested frame count, render one eight-channel block, then split it into mono object buffers. For SDL indices 0–7 use named types `FrontLeft`, `FrontRight`, `FrontCenter`, `LowFrequency`, `BackLeft`, `BackRight`, `SideLeft`, `SideRight`; do not use the enumeration's declaration order as a channel map. Balance every successful begin-update with end-update. [Object type definitions](https://learn.microsoft.com/en-us/windows/win32/api/spatialaudioclient/ne-spatialaudioclient-audioobjecttype).
5. Handle stop signals, bounded event timeouts, device invalidation and provider changes. Stop/join the native producer before handing engine ownership to SDL. Close resources on their owning thread; do not let native and SDL callbacks render the same engine concurrently.
6. Measure level/headroom through the provider's resulting playback using a supported capture setup and listening tests. This API gives the app object buffers, not the final binaural stereo sum, so the Mac's post-render limiter location cannot simply be copied here.

Native Windows output should replace the main SDL playback device for Spatial mode. Microphone capture and controller audio remain on their existing independent routes. Add `ole32` and other dependencies to the library/test link boundary as required by the actual implementation, not only to the game executable.

## Fallback, settings and microphone behavior

Keep a distinction between **requested mode**, **active mode** and **failure reason**. A saved Spatial choice can remain selected for a later restart even when the current session fell back to Stereo. Show a plain explanation in Sound settings and log it once. Unsupported OS/device/provider/format must not make the game silent.

Migrate `audio.surround` only when the new mode key is absent. Save the legacy key as a compatibility projection for older executables, with `surround=1` only for speaker Surround. Read a future/invalid mode safely as Stereo. Changing output mode initially requires restarting; avoid live backend replacement in the first playable version. Device disconnect/recovery still needs a controlled handoff to working output.

Use a typed internal mode and serialize canonical strings: `audio.output_mode=stereo`, `surround`, or `spatial`. Keep this output choice separate from `OutputLayout`, which currently means the renderer's channel count. For the first version, propagate the mode used at session startup to theater sessions; a changed next-start selection must not take effect only when a museum item opens.

Preserve startup microphone permission and `voice.microphone_enabled`. Do not move capture into the playback spatializer or reopen it on every mode change. AirPods microphone activation can change the active Bluetooth playback format; test it as part of route handling. Controller PCM is captured inside the existing voice render before the main output conversion. It must remain unchanged, and there must still be a single producer for each sound engine's capture queue.

## Order of implementation and release gates

1. **Shared output contract:** mode/status types, legacy migration, isolated tests and theater propagation. Keep the playable Spatial option unavailable until a working backend exists.
2. **Mac playable prototype:** wrap the tested native processor, integrate the SDL stereo callback, add output protection and fallback, then provide an isolated test app for Apple silicon and Intel listening.
3. **Windows backend:** implement the static-bed worker and failure handoff. Compile on Windows and use the user's PC for Sonic first, then Atmos if available. Add Linux regression builds for shared changes.
4. **Settings and full-game checks:** expose Stereo / Surround / Spatial headphones, translate the menu, show real active/fallback state, and test the main game plus archive sessions.
5. **Release decision:** require unchanged original output, no missing audio or clipping, correct directions, microphone/final puzzle, controller routes, pause/reset, device changes and measured callback deadlines. Native Intel gameplay and Windows listening remain hardware gates; Rosetta and hosted CI do not replace them.

The next development milestone is step 1 followed by the Mac playable prototype. Object-based 3D output and AirPods-specific features remain subsequent decisions after comparing the first mode with stereo using the available AirPods Pro 3 and Maxwell 2.
