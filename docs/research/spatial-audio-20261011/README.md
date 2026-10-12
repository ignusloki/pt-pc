# Archived spatial-audio experiment — 11 October 2026

This archive preserves the offline Mac experiment accompanying the spatial-audio feasibility report. It contains the exact probe source, analysis script and verified numerical results from that evaluation. These files are research evidence, not a production audio backend.

The probe rendered one impulse per speaker through Apple's native spatial mixer without playing sound, using game assets or touching player data. The arm64 build ran natively on an M1 Pro; the x86_64 build ran through Rosetta on the same Mac. This is not a physical Intel test. See `probe-results.json` for the original hardware, OS, build configuration and limitations.

Raw captures and executable builds remain in the ignored `.deps/spatial-audio-evaluation-20261011/` directory. To reproduce the original experiment from the repository root, copy `macos_spatial_probe.cpp` and `analyze_probe.py` from this archive into that scratch directory, create its `arm64` and `x64` subdirectories, then use:

```sh
clang++ -std=c++20 -O2 -arch arm64 -mmacosx-version-min=14.0 -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk .deps/spatial-audio-evaluation-20261011/macos_spatial_probe.cpp -framework AudioToolbox -framework CoreAudio -o .deps/spatial-audio-evaluation-20261011/macos_spatial_probe-arm64
clang++ -std=c++20 -O2 -arch x86_64 -mmacosx-version-min=14.0 -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk .deps/spatial-audio-evaluation-20261011/macos_spatial_probe.cpp -framework AudioToolbox -framework CoreAudio -o .deps/spatial-audio-evaluation-20261011/macos_spatial_probe-x64
.deps/spatial-audio-evaluation-20261011/macos_spatial_probe-arm64 .deps/spatial-audio-evaluation-20261011/arm64
arch -x86_64 .deps/spatial-audio-evaluation-20261011/macos_spatial_probe-x64 .deps/spatial-audio-evaluation-20261011/x64
python3 .deps/spatial-audio-evaluation-20261011/analyze_probe.py
```

Audio-component discovery required running outside the command sandbox on this machine. The analysis script records the original evaluation's date, machine and SDK; update those metadata fields when recording a new evaluation on different hardware or software. Changing SDK paths alone does not update that metadata.
