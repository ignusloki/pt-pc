"""Check a diagnostic session from tests/macos_hdr_lamp_menu.txt with MetalFX frame generation.

Use --start-floor f030 with the fullscreen Original or Enhanced HDR profile. The replay holds the
same camera on each lamp, opens Settings, closes it without edits and holds the camera again.
"""
import argparse
import csv
import json
from collections import defaultdict
from pathlib import Path
from statistics import median


def rows(directory, pattern):
    for path in sorted(directory.glob(pattern)):
        with path.open(newline="") as stream:
            yield from csv.DictReader(stream)


def check(directory):
    presents = {}
    actual = {}
    for row in rows(directory, "presents-*.csv"):
        key = (row["swapchain"], row["present_id"])
        if row["event"] == "queued" and row["result"] == "0":
            presents[int(row["sequence"]), row["kind"]] = key
        elif row["event"] == "displayed":
            actual[key] = int(row["actual_ns"])

    cameras = {"f030": ("door-tube", "-0.0010", "8.5600"), "f040": ("flare0", "-0.0090", "12.9300")}
    frames = defaultdict(dict)
    for row in rows(directory, "frames-*.csv"):
        floor = row["floor"]
        if row["kind"] == "rendered" and cameras.get(floor) == (row["region"], row["cam_x"], row["cam_z"]):
            frames[floor][int(row["sequence"])] = row

    results = []
    for floor in cameras:
        phases = {"before": [], "after": []}
        phase = "before"
        for seq, row in sorted(frames[floor].items()):
            if row["menu_open"] == "1":
                phase = "after"
            else:
                phases[phase].append((seq, row))
        for phase, samples in phases.items():
            samples = samples[8:]  # Allow the camera cut or the menu fade to finish.
            gaps = []
            cross_pair_reversals = 0
            for seq, _ in samples:
                generated = actual.get(presents.get((seq, "generated")))
                rendered = actual.get(presents.get((seq, "rendered")))
                if not generated or not rendered:
                    continue
                gaps.append((rendered - generated) / 1e6)
                previous = actual.get(presents.get((seq - 1, "rendered")))
                if previous and generated < previous:
                    cross_pair_reversals += 1
            levels = [float(row["mean_nits"]) for _, row in samples]
            level = median(levels) if levels else 0
            reversed_pairs = sum(g < 0 for g in gaps)
            same_refresh = sum(g == 0 for g in gaps)
            results.append(dict(floor=floor, fixture=cameras[floor][0], phase=phase, frames=len(samples), timed_pairs=len(gaps),
                                reversed_pairs=reversed_pairs, cross_pair_reversals=cross_pair_reversals, same_refresh=same_refresh,
                                median_spacing_ms=round(median(gaps), 3) if gaps else None, median_luminance_nits=round(level, 2),
                                passed=len(gaps) >= max(20, .9 * len(samples)) and level > 20 and reversed_pairs == 0
                                       and cross_pair_reversals == 0 and same_refresh <= .05 * len(gaps)))
    return results


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("session", type=Path)
    results = check(parser.parse_args().session)
    print(json.dumps(results, indent=2))
    raise SystemExit(0 if all(r["passed"] for r in results) else 1)
