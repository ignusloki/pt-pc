"""Check final scRGB frames from the f030 -> f040 lamp replay.

Run the game with an isolated Original HDR / MetalFX Quality profile at 2048x1280, 190-nit paper
white and a 1000-nit peak. Set PT_LAMP_TRACE to an empty output directory and pass --start-floor f030
--input-script tests/macos_hdr_lamps.txt --no-save. Pass the resulting session directory to this check.
"""
import argparse
import csv
import json
from collections import defaultdict
from pathlib import Path
from statistics import median


def percentile(values, fraction):
    ordered = sorted(values)
    index = (len(ordered) - 1) * fraction
    lower = int(index)
    return ordered[lower] + (ordered[min(lower + 1, len(ordered) - 1)] - ordered[lower]) * (index - lower)


def measure(directory, frame_generation):
    rows = defaultdict(dict)
    for path in directory.glob("frames-*.csv"):
        with path.open(newline="") as stream:
            for row in csv.DictReader(stream):
                if row["floor"] != "f040" or (row["width"], row["height"]) != ("2048", "1280"):
                    continue
                rows[float(row["cam_z"]), row["region"], row["kind"]][int(row["sequence"])] = row

    checks = []
    kinds = ("rendered", "generated") if frame_generation else ("rendered",)
    # A peak-only test cannot detect the bulb changing apparent size while its center stays at 1000 nits.
    for z, region, metric, floor, ceiling in (
        (12.89, "flare0", "mean_nits", 200, 12),
        (13.8854, "flare0", "mean_nits", 200, 8),
        (13.8854, "flare1", "peak_nits", 200, 12),
        (13.8854, "flare0-right", "mean_nits", 10, 1.5),
    ):
        for kind in kinds:
            sample = rows[z, region, kind]
            sequence = sorted(sample)[10:]  # Allow temporal reconstruction to settle after each camera cut.
            values = [float(sample[i][metric]) for i in sequence]
            steps = [abs(float(sample[i][metric]) - float(sample[j][metric]))
                     for j, i in zip(sequence, sequence[1:]) if i == j + 1]
            level = median(values) if values else 0
            step = percentile(steps, .95) if steps else None
            checks.append(dict(camera_z=z, region=region, frame_kind=kind, metric=metric, samples=len(steps),
                               median_nits=round(level, 2), p95_step_nits=None if step is None else round(step, 2),
                               passed=len(steps) >= 30 and level >= floor and step <= ceiling))
    return checks


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("session", type=Path)
    parser.add_argument("--frame-generation", action="store_true", help="require and check interpolated frames too")
    args = parser.parse_args()
    results = measure(args.session, args.frame_generation)
    print(json.dumps(results, indent=2))
    raise SystemExit(0 if all(item["passed"] for item in results) else 1)
