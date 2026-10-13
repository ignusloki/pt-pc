# Offline streaming spatial-audio experiment — 13 October 2026

This follow-up to the [original impulse probe](../spatial-audio-20261011/README.md) checks the adapter shape needed to turn the current game's interleaved eight-channel output into Apple's planar ambience bed and back into headphone stereo. It is a standalone research executable, not part of the game build.

It opens no playback device, microphone, game assets or player profile. The source is deterministic synthetic PCM in the public `SoundEngine::Render(..., 8)` channel order. It does not instantiate `SoundEngine` or measure the complete game's audio cost.

## Verified results

Both arm64 native and x86_64 through Rosetta passed on an M1 Pro, macOS 26.6.2 build 25G83, using the macOS 15.5 SDK and deployment target 14.0. Warnings are treated as errors. These are not physical Intel or macOS 14 execution tests.

| Check | Result |
| --- | --- |
| Requests of 1, 7, 127, 255, 511, 512, 997 and 4096 frames | Passed with bounded 512-frame native slices |
| Prepared-input range accepted by native pulls | Passed; callback never requests another source range in these cases |
| Source clock and native processed clock | Both 96,000 frames; 237 native slices/pulls for irregular requests |
| Fixed 256-frame versus irregular request output | Maximum difference about `8.94e-8` arm64 / `5.96e-8` x86_64 |
| Independent impulses in each SDL input channel | Finite output and expected left/right direction; side/rear map preserved |
| Seven correlated 0.9-amplitude tones, silent LFE | Native headphone peak about **3.1465**; 141,084 of 192,000 output samples above full scale |
| Recorded offline cost for two seconds of synthetic PCM | About 9.62 ms arm64 / 18.57 ms Rosetta; not a real-time deadline result |

The headroom case is deliberately extreme synthetic input. It proves that bounded speaker inputs do not guarantee bounded stereo output; it does not prove ordinary gameplay clips. The prototype deliberately records the unprotected native result. A playable backend needs output protection and actual-scene level measurements.

All numeric results, host/build metadata and limitations are in [stream-results.json](stream-results.json). Initialization and the real engine mixer are excluded from timings. Native-unit internal allocation, actual playback latency, SDL lifecycle, microphone/endpoint changes, fallback and Windows providers remain untested here.

## Reproduce

From the repository root on an Apple silicon Mac:

```sh
python3 docs/research/spatial-audio-20261013/run_probe.py
```

The runner compiles both architectures, runs x86_64 with Rosetta and writes the report/binaries to ignored `.deps/spatial-audio-evaluation-20261013/`. On a real Intel Mac it builds/runs only x86_64 natively. Choose an available SDK with `--sdk /absolute/path/to/MacOSX.sdk`; the report records the actual SDK, compiler, CPU and OS. It requires the installed SDK to include the spatial mixer declarations.

Apple's component discovery required execution outside the command sandbox on this machine. Without that access the probe reports `AUSpatialMixer unavailable`; this is not a conclusion about the user's playback hardware.

Keep the initial 11 October archive intact as historical evidence. See [the implementation plan](../../spatial-audio-implementation.md) for how the streaming findings constrain the actual game integration.
