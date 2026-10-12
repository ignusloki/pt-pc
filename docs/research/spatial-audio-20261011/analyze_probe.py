"""Summarize the offline native Mac spatial-mixer impulse captures."""
from array import array
from pathlib import Path
import hashlib
import json
import math
import sys

root = Path(__file__).resolve().parent
channels = ["front-left", "front-right", "center", "lfe", "side-left", "side-right", "back-left", "back-right"]
captures = {}
results = {}

def correlation(a, b):
    return math.fsum(x * y for x, y in zip(a, b)) / math.sqrt(
        math.fsum(x * x for x in a) * math.fsum(y * y for y in b)
    )

for architecture in ("arm64", "x64"):
    captures[architecture] = {}
    results[architecture] = {}
    for channel in channels:
        samples = array("f")
        samples.frombytes((root / architecture / f"{channel}.f32").read_bytes())
        if sys.byteorder != "little":
            samples.byteswap()
        assert len(samples) == 48000 * 2
        assert all(math.isfinite(sample) for sample in samples)
        ears = [samples[0::2], samples[1::2]]
        energy = [math.fsum(sample * sample for sample in ear) for ear in ears]
        peak = [max(abs(sample) for sample in ear) for ear in ears]
        first = [next(i for i, sample in enumerate(ear) if abs(sample) > 1e-7) for ear in ears]
        if channel.endswith("left"):
            assert energy[0] > energy[1] > 0
        elif channel.endswith("right"):
            assert energy[1] > energy[0] > 0
        else:
            assert min(energy) > 0
        captures[architecture][channel] = samples
        results[architecture][channel] = {"energy_lr": energy, "peak_lr": peak, "first_frame_lr": first}
    results[architecture]["front_vs_back_left_waveform_cosine_similarity"] = correlation(
        captures[architecture]["front-left"], captures[architecture]["back-left"]
    )
    results[architecture]["front_vs_back_right_waveform_cosine_similarity"] = correlation(
        captures[architecture]["front-right"], captures[architecture]["back-right"]
    )

maximum_architecture_difference = max(
    abs(a - b)
    for channel in channels
    for a, b in zip(captures["arm64"][channel], captures["x64"][channel])
)
assert maximum_architecture_difference < 1e-5

report = {
    "date": "2026-10-11",
    "source_commit": "b05adee04ec484bffc6d672ec57a830445817367",
    "host": {"processor": "Apple M1 Pro", "macos": "26.6.2", "build": "25G83"},
    "build": {"sdk": "macOS 15.5", "deployment_target": "14.0", "architectures": ["arm64", "x86_64"]},
    "execution": {"arm64": "native", "x64": "Rosetta on Apple silicon; not a physical Intel Mac"},
    "configuration": {
        "renderer": "AUSpatialMixer", "input": "8-channel MPEG_7_1_C ambience bed", "output": "stereo headphones",
        "sample_rate": 48000, "frames": 48000, "maximum_frames_per_slice": 512,
        "impulse_frame": 4096, "impulse_amplitude": 0.25, "extra_reverb": False,
        "extra_distance_attenuation": False, "personalized_hrtf": False, "head_tracking": False,
        "sdl_to_apple_channel_indices": [0, 1, 2, 3, 6, 7, 4, 5],
    },
    "compatibility": "The x64 instance cannot write the optional head-tracking property. Skipping unsupported optional properties allows fixed headphone rendering to succeed.",
    "source_sha256": hashlib.sha256((root / "macos_spatial_probe.cpp").read_bytes()).hexdigest(),
    "results": results,
    "maximum_absolute_arm64_x64_sample_difference": maximum_architecture_difference,
    "limitations": ["No physical Intel Mac", "No Windows output", "No listening test", "No live SDL integration", "No full-game performance measurement", "No head tracking or personalized profile"],
}
(root / "probe-results.json").write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps({
    "passed": True,
    "maximum_absolute_arm64_x64_sample_difference": maximum_architecture_difference,
    "front_vs_back_waveform_similarity_lr": [
        results["arm64"]["front_vs_back_left_waveform_cosine_similarity"],
        results["arm64"]["front_vs_back_right_waveform_cosine_similarity"],
    ],
    "report": str(root / "probe-results.json"),
}))
