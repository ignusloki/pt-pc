# User listening-session log review

13 October 2026, preview `1.0.7-spatial2`, build `debbec7-dirty` on the spatial-audio research branch. These are the user's two manual sessions, separate from the earlier automated gameplay checks.

The private source logs are in the repository's ignored `logs/spatial-audio/` directory:

- `pt-20261013T182448Z-88526.log`: Spatial headphones, 110.921 seconds wall time.
- `pt-20261013T182642Z-89156.log`: Stereo, 158.903 seconds wall time.

Parsed interval measurements, with source line numbers, are saved locally as `logs/spatial-audio/analysis-20261013.json`. No PCM was recorded by the diagnostics. No executable, settings or saves were changed for this review.

## Mode and routing

| Check | Spatial session | Stereo session |
| --- | --- | --- |
| Saved choice at launch / active processing | Spatial / Spatial | Stereo / Stereo |
| Playback device | AirPods Pro | Same AirPods Pro |
| Stream and device sample rate | 48,000 Hz | 48,000 Hz |
| Game renderer channels | 8 | 2 |
| Device channels | 2, carrying the native headphone render | 2 |
| Native spatial frames processed | 5,221,376 | 0 |
| Stereo fallback frames | 0 | 0 |
| Native processing failures / SDL write errors | 0 / 0 | 0 / 0 |
| Reported device/format changes | 0 | 0 |

The native processor initialized successfully. Head tracking and personalized HRTF properties both read back as zero. The renderer is Apple's spatial mixer with a 7.1 ambience bed, fixed headphone output and interaural delay enabled. The two-channel device format is expected for this binaural path.

At 105.205 seconds in the spatial session, the user selected Stereo in the menu; the log correctly reports `active=spatial restart_pending=true`. The following launch starts and remains in Stereo. This confirms the saved choice, restart requirement and active-status reporting worked together.

## Signal and timing

During the `f010` radio-floor portion, rear channels carried between 2.56% and 61.70% of the eight-channel mix's energy across the reported intervals. Side channels were also active, and the logged listener direction changed. These are whole-game mix measurements, so they do not isolate the radio. They show that the spatial processor received directional content, including rear content.

| Measurement | Spatial | Stereo |
| --- | ---: | ---: |
| Maximum output peak, full scale = 1 | 0.953478 | 0.869252 |
| Output samples above full scale / invalid samples | 0 / 0 | 0 / 0 |
| Frames with spatial limiter gain reduction | 0 | Not applicable |
| Maximum observed callback time | 7.443 ms | 8.011 ms |
| Callbacks exceeding their audio duration | 0 | 0 |

All callbacks requested 1,024 frames: 21.333 ms of audio at 48 kHz. The timing measurements show no callback-budget overruns in either session; they do not measure Bluetooth latency or prove the physical device never underruns. The spatial limiter never attenuated the signal, so limiter gain reduction is not an explanation for an indistinct effect in this test.

Both sessions used game volume 1.0 with microphone input and controller speaker disabled. The Stereo session muted when the game entered the background from 12.277 to 83.914 seconds, then restored volume to 1.0. That is the configured background-mute behavior. The sessions also have different camera paths and timings, so their overall RMS/peak values are not a controlled loudness comparison.

The only warnings were the existing missing menu animation `UI_sys_opt_setin_s`, present in both modes. No audio warnings or errors appeared.

## Conclusion and remaining question

The two modes were selected and rendered correctly, and native processing stayed active without fallback or observable signal corruption. The user's earlier report of little audible difference remains a listening-quality question; these metrics cannot establish convincing depth or front/back separation. They also cannot verify macOS mono output or an additional system spatialization stage after the game hands off its stereo signal.

The next useful comparison is the existing loudness-matched front/back radio clips, which use the same sound and fixed poses in each mode. If the effect remains weak there, evaluate the current generic ambience-bed headphone renderer before treating the feature as perceptually validated. The game and saved profile remain unchanged by this review.
