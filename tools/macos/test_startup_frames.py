"""Check a whole-frame trace from tests/macos_hdr_startup_frames.txt on a Mac.

Use a saved profile, PT_BOOT_SHOTS, and a private copy of the save directory.
The window stream contains SDR code values. Compare each stream to its own lit
neighbors so the title fade, loading screen and idle capture notifications are
distinguishable from black images inserted during steady presentation.
"""
import argparse
import csv
import json
import re
from bisect import bisect_right
from collections import Counter
from itertools import groupby
from pathlib import Path


def rows(directory, pattern):
    result = []
    for path in directory.glob(pattern):
        with path.open(newline="") as stream:
            result.extend(csv.DictReader(stream))
    return result


def black_runs(stream):
    """Find brief runs of near-black samples bracketed by two lit images in one phase."""
    def black(row):
        return int(row["black_pixels"]) >= .999 * int(row["samples"])

    def lit(row):
        return int(row["black_pixels"]) < .97 * int(row["samples"]) and float(row["peak_luma"]) > 8

    findings = []
    for phase, samples in groupby(stream, key=lambda row: (row["phase"], row["swapchain"])):
        previous = None
        dark = []
        for row in samples:
            if black(row):
                dark.append(row)
                continue
            if dark and previous and lit(previous) and lit(row) and row["time"] - previous["time"] <= .35:
                findings.append(dict(phase=phase[0], swapchain=phase[1], seconds=round(dark[0]["time"], 6), frames=len(dark)))
            previous = row
            dark = []
    return findings


def check(directory):
    screen = rows(directory, "screen-*.csv")
    windows = rows(directory, "window-*.csv")
    presentations = rows(directory, "presents-*.csv")
    events = rows(directory, "events.csv")
    log = (directory / "pt.log").read_text(errors="replace")
    def key(row):
        return (row["swapchain"], row["present_id"])
    actual = {key(p): int(p["actual_ns"]) * 1e-9 for p in presentations if p["event"] == "displayed" and int(p["actual_ns"])}
    queued = {(p["swapchain"], p["sequence"], p["kind"]): p for p in presentations if p["event"] == "queued" and p["result"] == "0"}
    clock = re.search(r"mach ticks (\d+) at ([\d.]+) log seconds, timebase (\d+)/(\d+) ns", log)
    tick_seconds = int(clock[3]) / int(clock[4]) * 1e-9 if clock else 0
    origin = int(clock[1]) * tick_seconds - float(clock[2]) if clock else 0
    for row in screen:
        p = queued.get((row["swapchain"], row["sequence"], row["kind"]))
        row["time"] = float(p["submit_seconds"] if p else row["scene_seconds"])
        if clock and p and key(p) in actual:
            row["time"] = actual[key(p)] - origin
    screen.sort(key=lambda row: row["time"])
    times = [row["time"] for row in screen]
    sampled = []
    for w in windows:
        if w["status"] not in {"complete", "started"} or not int(w["samples"]):
            continue
        w["time"] = int(w["display_mach_ticks"]) * tick_seconds - origin if clock and int(w["display_mach_ticks"]) else float(w["log_seconds"])
        current = bisect_right(times, w["time"]) - 1
        if current < 0:
            continue
        w.update(phase=screen[current]["phase"], swapchain=screen[current]["swapchain"])
        sampled.append(w)
    sampled.sort(key=lambda row: row["time"])
    covered = {(r["swapchain"], r["sequence"], r["kind"]) for r in screen}
    summary = dict(
        drawable_images=len(screen), submitted_images=len(queued), driver_display_times=len(actual),
        missing_drawable_samples=len(queued.keys() - covered),
        drawable_by_phase=dict(Counter(r["phase"] for r in screen)),
        window_images=len(sampled), window_by_phase=dict(Counter(r["phase"] for r in sampled)),
        window_status=dict(Counter(r["status"] for r in windows)),
        title_generated=sum(r["phase"] == "title" and r["kind"] != "rendered" for r in screen),
        generated_gameplay=sum(r["phase"] == "gameplay" and r["kind"] == "generated" for r in screen),
        lit_title_samples=sum(r["phase"] == "title" and float(r["peak_luma"]) > 8 for r in screen),
        invalid_pixels=sum(int(r["invalid_pixels"]) for r in screen + sampled),
        drawable_black_flashes=black_runs(screen), window_black_flashes=black_runs(sampled),
        settings_round_trip="input script: 2 expectations, 0 failed" in log,
        events=[e["event"] for e in events],
    )
    summary["passed"] = (summary["settings_round_trip"] and summary["lit_title_samples"] >= 8
                         and summary["title_generated"] == 0 and summary["invalid_pixels"] == 0
                         and summary["missing_drawable_samples"] == 0 and not summary["drawable_black_flashes"]
                         and not summary["window_black_flashes"] and all(
                             summary["window_by_phase"].get(p, 0) > 0 for p in ("title", "gameplay", "settings")))
    return summary


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("session", type=Path)
    result = check(parser.parse_args().session)
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result["passed"] else 1)
