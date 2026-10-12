# Spatial audio feasibility and implementation scope

Evaluated on 11 October 2026 against v1.0.6, commit `b05adee04ec484bffc6d672ec57a830445817367`.

## Recommendation

Support macOS and Windows in two stages. First, add optional spatial headphone rendering of the existing 7.1 mix. Then evaluate sending individual game sounds and their full 3D positions to the native renderers.

The first stage is a moderate output-layer change. The second changes how voices pass through the mixer and needs broader audio regression. AirPods head tracking and personalized profiles are a separate capability and signing investigation.

| Stage | Result | Planning estimate |
| --- | --- | --- |
| Spatial headphones from the existing 7.1 mix | Directional headphone rendering on Mac; Windows Spatial Sound provider support on Windows | Approximately 5–10 focused engineering days for integration, fallback, settings and initial tests across both platforms |
| Individual 3D sounds | Retains individual source positions, including elevation, instead of reducing everything to horizontal speakers | Approximately 10–20 additional engineering days; mixing and hardware findings may extend this |
| AirPods head tracking and personalization | Optional head movement and personal profile support | Estimate after validating runtime support and distribution entitlements |

These are rough engineering estimates, not measured completion times. Hardware test availability affects the schedule. The Mac offline prototype below is already complete; no production spatial-audio feature has been implemented.

## What the game already provides

The engine already knows listener position/orientation and moving sound positions. It applies the original bank's distance curves, filtering, spread, bus gains and room effects. It can render stereo, 5.1 or 7.1 through SDL.

The current speaker panner uses azimuth and spread. Source height contributes to distance calculations but does not become an elevated output channel. A spatial renderer receiving the final 7.1 mix cannot recover the individual source heights discarded during that mix.

The master limiter processes the surround field. Effect buses remain stereo and feed the front left/right speakers. The game's LFE channel is currently silent. These are existing mix characteristics, not additional spatial-audio features.

Code evidence: [listener and emitters](https://github.com/ignusloki/pt-pc/blob/b05adee04ec484bffc6d672ec57a830445817367/src/game/game_sound.cpp#L223), [voice positioning](https://github.com/ignusloki/pt-pc/blob/b05adee04ec484bffc6d672ec57a830445817367/src/engine/audio/sound_engine.cpp#L2663), [speaker panner](https://github.com/ignusloki/pt-pc/blob/b05adee04ec484bffc6d672ec57a830445817367/src/engine/audio/sound_engine.cpp#L111), [master render](https://github.com/ignusloki/pt-pc/blob/b05adee04ec484bffc6d672ec57a830445817367/src/engine/audio/sound_engine.cpp#L3425).

Existing surround output negotiates the actual playback-device format. Requesting eight channels alone does not guarantee spatial headphone rendering: a stereo endpoint can still resolve to stereo. Spatial mode needs an explicit rendering path.

## Stage 1: spatial headphones from 7.1

### macOS

Use Apple's `AUSpatialMixer` with one eight-channel ambience-bed input and stereo headphone output. Apple documents this configuration for game-engine multichannel streams. [Apple multichannel spatial-audio sample](https://developer.apple.com/documentation/audiotoolbox/generating-spatial-audio-from-a-multichannel-audio-stream).

The offline probe demonstrates that the mixer can run without replacing the whole engine. The proposed first integration pulls eight-channel engine PCM through the native mixer and sends the resulting stereo PCM through SDL. Live callback, routing and device-change behavior still need validation.

Use generic, fixed headphone rendering initially. Disable extra native reverb and distance attenuation because the game already applies them. Query optional properties before setting them; the x64 probe exposed an unsupported head-tracking setting.

Speaker order needs an explicit conversion:

| Boundary | Channel order |
| --- | --- |
| Engine's internal Wwise field | FL, FR, FC, BL, BR, SL, SR, LFE |
| Existing eight-channel engine output / SDL | FL, FR, FC, LFE, BL, BR, SL, SR |
| Apple's `MPEG_7_1_C` input used by the probe | FL, FR, FC, LFE, SL, SR, BL, BR |

Build the Apple input from SDL indices `{0, 1, 2, 3, 6, 7, 4, 5}`. Apple's sample uses a different 7.1 layout with front-wide channels; copying that layout would mislabel our side/rear channels. The layout definitions were checked in the installed SDK and exercised in the probe.

### Windows

Add an event-driven `ISpatialAudioClient` output path for spatial mode. Split each eight-channel mix block into static speaker objects using their named types. Query supported formats and the static channel mask before activating the stream. Microsoft's static-object example provides this integration pattern; it requests no dynamic objects. [Native rendering examples](https://learn.microsoft.com/en-us/windows/win32/coreaudio/render-spatial-sound-using-spatial-audio-objects).

The user selects Windows Sonic, Dolby Atmos for Headphones or DTS in Windows. The same application output serves those providers; we do not implement separate Dolby processing. Windows Spatial Sound must be enabled on the selected endpoint. [Microsoft Spatial Sound overview](https://learn.microsoft.com/en-us/windows/win32/coreaudio/spatial-sound).

The desktop API starts at Windows 10 version 1703. Initialization and format checks must handle unavailable support and fall back to SDL. A dynamic-object budget is not required for this static-bed stage; validate the static stream itself. [ISpatialAudioClient requirements and capabilities](https://learn.microsoft.com/en-us/windows/win32/api/spatialaudioclient/nn-spatialaudioclient-ispatialaudioclient).

### Shared changes

| Area | Intended work |
| --- | --- |
| `AudioOutput` | Select SDL or native spatial output; report the active mode; handle start, stop, failure and route changes |
| `SoundSystem` and `GameSound` initialization | Pass an output mode instead of a surround boolean |
| Settings and PC Settings menu | Offer Stereo, Surround and Spatial; retain Stereo as default; require restart initially |
| Existing settings migration | When the new key is absent, map `audio.surround=1` to Surround and `0` to Stereo |
| English menu strings and 13 translated text tables | Add the mode label, restart note and relevant platform help |
| Platform files and CMake | Add guarded Mac/Windows implementations and system-library links; keep Linux's existing output |
| Tests | Channel mapping, settings migration, fallback, buffer lifecycle and platform playback |

The first stage can reuse `SoundEngine::Render(..., 8)` without changing bank decoding or game event logic. Render each block once and then split/reorder it; rendering separately for each speaker would advance the engine clock repeatedly. When Stereo is selected, continue using the original two-channel render rather than downmixing the new path.

Preallocate callback buffers and adapt to the native stream's frame count and supported sample format. Measure peaks after spatial processing and provide suitable headroom or protection: limiting the individual input speakers does not guarantee that their final binaural sum cannot clip.

No new game assets or PKG conversion steps are needed. Installer packaging should pick up the rebuilt executable. Runtime failure must leave audible stereo playback and an accurate status message, rather than silently claiming Spatial is active.

The entire 7.1 mix is spatialized, including stereo music/menu material placed in that mix. This can make it sound external to the listener. [Microsoft's integration discussion](https://learn.microsoft.com/en-us/windows/win32/coreaudio/spatial-sound). Compare this behavior during listening tests before accepting it.

## Stage 2: retain individual 3D sounds

Expose voice PCM before speaker panning together with its resolved position and a stable voice-instance identity. Existing events can contain multiple voices, and some positioning comes from authored paths, so event IDs or raw game-object positions alone are insufficient.

Route suitable positional voices to native point sources or Windows dynamic objects. Retain a speaker bed for diffuse room effects, non-positional material and overflow. Preserve original pitch, fades, filtering, attenuation, bus ducking and auxiliary sends. Prevent duplicated dry audio, double attenuation/reverb and applying camera rotation twice.

The master limiter's gain and lookahead must remain coherent between the object and bed paths. Controller-speaker PCM and subtitle/event timing also need protection. Native object availability can change; query the budget and move overflow to the bed instead of dropping audible sounds. [Windows object-budget notifications](https://learn.microsoft.com/en-us/windows/win32/api/spatialaudioclient/nf-spatialaudioclient-ispatialaudioclient-getmaxdynamicobjectcount).

The main additional work is in `sound_engine.h/.cpp`, voice routing and DSP coordination. This is the substantial part of supporting full positional audio rather than virtualizing horizontal surround speakers.

## AirPods features and developer-account implications

Apple identifies A3064 as AirPods Pro 3. They provide hardware for later AirPods-specific tests. [Apple model identification](https://support.apple.com/en-us/109525).

Generic headphone rendering succeeded locally without a personalized-profile or head-pose entitlement. That is evidence for the basic path; it does not validate entitlement-restricted features in our distributed app.

Apple documents `com.apple.developer.coremotion.head-pose` plus API opt-in for head tracking, and `com.apple.developer.spatial-audio.profile-access` for personalized rendering. We must verify provisioning and signing eligibility before promising these features in the current distribution. The sources reviewed do not establish that the user's free account can provision them. [Apple personalization and head tracking](https://developer.apple.com/documentation/phase/personalizing-spatial-audio-in-your-app).

Apple's Mac head-tracking support guidance specifies Apple silicon. Generic fixed headphone output is the initial Intel target; AirPods head tracking is not part of the Intel promise. [Apple Mac support guidance](https://support.apple.com/en-us/102469).

## Completed local verification

An asset-free C++ probe used 48 kHz float PCM, one impulse per input channel, an eight-channel ambience bed, and two-channel native headphone rendering. It neither played audio nor opened the microphone or game profile.

| Check | Result |
| --- | --- |
| arm64 executable on this M1 Pro | Passed all eight channels |
| x86_64 executable through Rosetta on this M1 Pro | Passed after capability-checking optional properties |
| Finite output and expected left/right directional energy | Passed |
| Front/rear filter distinction | Different waveforms; normalized similarities approximately 0.781 left and 0.690 right |
| arm64 versus x64 sample agreement | Maximum absolute difference approximately `4.47e-8` |

Both builds used the macOS 15.5 SDK and a macOS 14 deployment target. Execution was on macOS 26.6.2, build 25G83. The initial x64 attempt failed when blindly setting head tracking off (`-10865`); querying writability and skipping the optional property resolved it.

These results verify a rendering primitive, not listening quality, full-game performance, macOS 14 behavior or physical Intel compatibility. Windows playback, live SDL integration, head tracking and personalized profiles remain untested.

The probe source, analysis script and numerical results are archived under `docs/research/spatial-audio-20261011/` on the research branch. Executable builds and raw captures remain ignored scratch data at `/Users/suporte/PT-Mac/.deps/spatial-audio-evaluation-20261011/`. The archive includes reproduction instructions so the experiment can be rebuilt if scratch data is cleaned later.

## Test plan with available hardware

| Hardware | Initial checks |
| --- | --- |
| Apple silicon Mac + AirPods Pro 3 | Stereo versus fixed native spatial output; camera rotation; microphone enabled/disabled; Bluetooth route changes |
| Apple silicon Mac + Maxwell 2 over USB/dongle | Repeat listening comparison with consistent EQ and no extra surround processing |
| 2019 Intel MacBook Pro | Basic spatial output, graceful handling of unsupported optional properties, callback cost and microphone behavior |
| Windows PC + Maxwell 2 | Windows Sonic first; Atmos when licensed; provider off/on, endpoint switching and disconnect/reconnect |

Audeze states that both Maxwell 2 editions support Dolby Access on Windows; only the Xbox edition includes the license. The user's edition is unspecified, so an Atmos license is not assumed. [Audeze compatibility information](https://www.audeze.com/products/maxwell-2-wireless-gaming-headset).

Acceptance checks:

1. Compare radio, telephone, bathroom baby sounds and Lisa's footsteps/breathing in front, beside and behind the player. Turn the camera and move away; check direction and distance consistently.
2. Check hallway/bathroom room transitions, jumpscares, music, menu sounds and the ending. Listen for changed room effects, missing voices or clipping; verify subtitle timing.
3. Enable the microphone and test the final puzzle while each headset is connected. Check capture initialization, playback format changes and recovery when a device disconnects.
4. Disable system/headset spatial processing for the stereo baseline. Avoid running two headphone spatializers together. Check that unsupported native output falls back visibly and audibly.
5. Verify unchanged stereo reference captures and existing 5.1/7.1 speaker mappings. Test settings migration, restart, pause/resume and loop reset.
6. Measure worst-case callback time against its buffer deadline, underruns, latency and bounded buffer memory during busy scenes. For stage 2, also record active voices and available native object slots.

The next implementation milestone is the optional 7.1-based headphone mode with both native paths and fallback. Evaluate its listening and performance results before expanding the mixer to individual 3D objects.
