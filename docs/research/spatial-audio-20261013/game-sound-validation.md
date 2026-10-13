# Real game sound validation after the first listening report

13 October 2026, Mac preview `1.0.7-spatial1` on Apple M1 Pro, macOS 26.6.2. The user reports little/no audible difference on AirPods Pro and confirms restarting between output choices with the active mode changing. Do not attribute that report to the restart requirement. The latest retained gameplay log contains only the most recent Stereo session; it does not preserve the earlier Spatial session.

An offline comparison used the preview's actual `SoundSystem`, original `Play_radio_f010` event and current Mac spatial processor. Two independently initialized engines used seed 137, master volume 0.2 and identical poses: one rendered original stereo, the other rendered eight channels and fed the native processor plus its stereo-linked limiter. A fixed listener faced +Z with +Y up; emitter positions were front +Z, right -X, back -Z, left +X, all three metres away. The right vector follows the engine's `cross(forward, up)` convention. Each capture contains four seconds of the same event. No device, microphone, player profile or full scene was opened; additional room sends/ambience were absent.

## Signal findings

| Source | Original stereo RMS | Native spatial RMS | Normalized waveform correlation |
| --- | ---: | ---: | ---: |
| Front | 0.004335620 | 0.004275728 | 0.665336 |
| Right | 0.004400775 | 0.005163083 | 0.647654 |
| Back | 0.003065746 | 0.004200777 | 0.640912 |
| Left | 0.004400775 | 0.005168661 | 0.645059 |

The front case populated FL/FR with about 3.60914 energy each; the back case populated BL/BR with about 3.60914 each and zero front energy. Side cases populated the corresponding side speaker plus smaller front/back contributions. The eight-channel mix is therefore carrying directional information for this event; it is not limited to front stereo.

These measurements establish different PCM and correct directional routing. Correlation is a waveform check, not a perceptual quality score: filtering and phase changes affect it. It does not establish that listeners identify rear direction reliably or hear a useful improvement.

## Local listening comparison

Private clips were created in `dist/macos/spatial-audio-preview-1/audio-comparison/`. Each direction file plays four seconds of original Stereo, half a second of silence, then four seconds of Spatial. Each half is normalized to RMS 0.05 with a 0.35 peak ceiling; all achieved RMS values are within about `3e-8` of the target. This avoids favouring a louder mix. The native spatial half is already rendered to headphone stereo; do not add a second spatializer in the playback app.

Start with `back-stereo-then-spatial.wav`, then `right-stereo-then-spatial.wav`. The combined clip plays Front, Right, Back, Left pairs in order. No head tracking or elevated objects are present. These clips contain game audio and remain local diagnostic files, outside tracked source and release assets.

Raw captures, comparison source and runner are in ignored `.deps/spatial-audio-evaluation-20261013/`. The local `comparison-results.json` records the before/after RMS and correlations. No production DSP change was made to amplify the effect based on this report.

Next: listen to these controlled comparisons with both earbuds, mono audio off and other spatial processing off. A useful difference here would point to scene/position/comparison conditions in gameplay. A similarly weak difference would motivate further renderer evaluation, personalization eligibility or the separate individual-3D-object stage; it should not be treated as a completed listening acceptance check.
