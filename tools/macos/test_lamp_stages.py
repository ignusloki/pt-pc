"""Verify a marked diagnostic replay of both fixtures from tests/macos_hdr_lamp_menu.txt.

Run with the session directory. Add --no-frame-generation for a rendered-only control.
Stage values are in each pass's own units. Output tiles store four codes per nit.
"""
import argparse
import csv
import json
import math
import struct
from collections import defaultdict
from pathlib import Path
from statistics import median

from test_lamp_presentation import rows


FIXTURES = {"f030": ("door-tube", "door-halo", "door-wall"), "f040": ("hall-glass", "hall-halo", "hall-wall")}
STAGES = {"diffuse-light", "scene-hdr", "reactive-mask", "upscale-resolve", "bloom", "optical-flare", "pre-composite"}


def check_tiles(directory, frames):
    remaining = set(frames)
    for path in directory.glob("tiles-*.pgm"):
        with path.open("rb") as stream:
            while magic := stream.readline():
                assert magic == b"P5\n", (path, stream.tell())
                comment, seq, kind, region, scale = stream.readline().decode("ascii").split()
                assert (comment, scale) == ("#", "nits=code/4")
                key = int(seq), kind, region
                remaining.remove(key)
                frame = frames[key]
                nx, ny = map(int, stream.readline().split())
                assert nx == ny == int(frame["side"]) // 2 and stream.readline() == b"65535\n"
                pixels = struct.unpack(f">{nx * ny}H", stream.read(nx * ny * 2))
                if max(pixels) < 65535:
                    mean_nits = sum(pixels) / (4 * len(pixels))
                    assert abs(mean_nits - float(frame["mean_nits"])) <= .13, (key, mean_nits, frame["mean_nits"])
    assert not remaining, f"{len(remaining)} output samples lack matching tiles"
    return len(frames)


def display_times(directory):
    queued, displayed = {}, {}
    for row in rows(directory, "presents-*.csv"):
        key = row["swapchain"], row["present_id"]
        if row["event"] == "queued" and row["result"] == "0":
            queued[int(row["sequence"]), row["kind"]] = key
        elif row["event"] == "displayed":
            displayed[key] = int(row["actual_ns"])
    return {key: displayed[value] for key, value in queued.items() if value in displayed}


def check(directory, frame_generation):
    frames = {(int(r["sequence"]), r["kind"], r["region"]): r for r in rows(directory, "frames-*.csv")
              if r["region"] in FIXTURES.get(r["floor"], ())}
    tiles = check_tiles(directory, frames)
    stages = defaultdict(dict)
    for row in rows(directory, "stages-*.csv"):
        name, side = row["stage"], int(row["side"])
        assert 0 < side <= 48 and 0 <= int(row["x"]) <= int(row["width"]) - side and 0 <= int(row["y"]) <= int(row["height"]) - side
        assert math.isfinite(float(row["mean"])) and math.isfinite(float(row["peak"]))
        if name == "reactive-mask":
            assert row["unit"] == "mask_0_1" and 0 <= float(row["mean"]) <= float(row["peak"]) <= 1
        else:
            assert row["unit"] == "pass_rgb_luma"
        stages[int(row["sequence"]), row["region"]][name] = row
    times = display_times(directory)
    with (directory / "events.csv").open(newline="") as stream:
        events = list(csv.DictReader(stream))
    result = []
    for floor, regions in FIXTURES.items():
        marker, opened, closed = [next(e for e in events if e["floor"] == floor and e["event"] == what)
                                  for what in ("player-F5", "settings-open", "settings-closed")]
        mark, start, end = (int(e["sequence"]) for e in (marker, opened, closed))
        assert mark < start < end and marker["menu_open"] == closed["menu_open"] == "0" and opened["menu_open"] == "1"
        marked = frames[mark, "rendered", regions[0]]
        camera_keys = ("cam_x", "cam_y", "cam_z", "yaw", "pitch", "fov")
        for phase in ("before", "after"):
            seqs = sorted(seq for (seq, kind, region), r in frames.items()
                          if kind == "rendered" and region == regions[0] and r["menu_open"] == "0"
                          and all(r[k] == marked[k] for k in camera_keys)
                          and (seq < start if phase == "before" else seq >= end))
            seqs = seqs[8:]  # Allow the camera cut and Settings fade to finish.
            assert len(seqs) >= 20, (floor, phase, len(seqs))
            stats = {}
            for region in regions:
                for seq in seqs:
                    final = frames[seq, "rendered", region]
                    assert final["pipeline"] == marker["pipeline"] and final["fixture_primary_present"] == "1"
                    assert final["fixture_secondary_present"] == ("1" if floor == "f030" else "0")
                    assert stages[seq, region].keys() == STAGES, (seq, region, stages[seq, region].keys())
                level = median(float(frames[s, "rendered", region]["mean_nits"]) for s in seqs)
                assert level > 0
                stats[region] = {"median_nits": round(level, 2), "median_stage_pixel_step": {}}
                for stage in sorted(STAGES):
                    steps = [float(v) for s in seqs if (v := stages[s, region][stage]["mean_abs_pixel_step"])]
                    assert len(steps) > len(seqs) * .8 and all(math.isfinite(v) and v >= 0 for v in steps)
                    stats[region]["median_stage_pixel_step"][stage] = round(median(steps), 6)
            pairs = [(times[s, "generated"], times[s, "rendered"]) for s in seqs if (s, "generated") in times and (s, "rendered") in times]
            if frame_generation:
                assert len(pairs) >= .9 * len(seqs) and all(g <= r for g, r in pairs), (floor, phase, len(pairs))
                assert all(times[s - 1, "rendered"] <= times[s, "generated"] for s in seqs if (s - 1, "rendered") in times and (s, "generated") in times)
            else:
                assert not pairs and all((s, "generated", regions[0]) not in frames for s in seqs)
            result.append(dict(floor=floor, phase=phase, pipeline=marker["pipeline"], marker=mark, frames=len(seqs), timed_pairs=len(pairs), regions=stats))
    return dict(output_tiles=tiles, phases=result)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("session", type=Path)
    parser.add_argument("--no-frame-generation", action="store_true")
    args = parser.parse_args()
    print(json.dumps(check(args.session, not args.no_frame_generation), indent=2))
