"""Build and record the offline Mac streaming experiment, without game assets."""
from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import pathlib
import platform
import subprocess


def run(*command: str) -> str:
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode:
        raise SystemExit(f"{command[0]} failed ({result.returncode}): {result.stderr or result.stdout}")
    return result.stdout.strip()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=pathlib.Path, default=pathlib.Path(
        "/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk"))
    parser.add_argument("--scratch", type=pathlib.Path, default=pathlib.Path(
        ".deps/spatial-audio-evaluation-20261013"))
    args = parser.parse_args()
    if platform.system() != "Darwin":
        parser.error("this probe uses Apple's native audio component")
    source = pathlib.Path(__file__).resolve().with_name("macos_stream_probe.cpp")
    args.scratch.mkdir(parents=True, exist_ok=True)
    architectures = ["arm64", "x86_64"] if platform.machine() == "arm64" else ["x86_64"]
    results = {}
    for architecture in architectures:
        binary = (args.scratch / f"macos_stream_probe-{architecture}").resolve()
        run("/usr/bin/clang++", "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror", "-arch", architecture,
            "-mmacosx-version-min=14.0", "-isysroot", str(args.sdk), str(source), "-framework", "AudioToolbox",
            "-framework", "CoreAudio", "-o", str(binary))
        result = json.loads(run("/usr/bin/arch", f"-{architecture}", str(binary)))
        result["execution"] = "Rosetta on Apple silicon" if architecture == "x86_64" and platform.machine() == "arm64" else "native"
        results[architecture] = result
        (args.scratch / f"{architecture}.json").write_text(json.dumps(result, indent=2) + "\n")
        print(f"{architecture}: passed; peak {result['correlated_seven_channel_peak']:.6f}")
    record = {
        "recorded_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "release_baseline": run("git", "rev-parse", "v1.0.7^{commit}"),
        "research_branch_head_before_record": run("git", "rev-parse", "HEAD"),
        "host_cpu": run("/usr/sbin/sysctl", "-n", "machdep.cpu.brand_string"),
        "host_architecture": platform.machine(),
        "macos_version": run("/usr/bin/sw_vers", "-productVersion"),
        "macos_build": run("/usr/bin/sw_vers", "-buildVersion"),
        "compiler": run("/usr/bin/clang++", "--version").splitlines()[0],
        "probe_source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
        "sdk": str(args.sdk),
        "deployment_target": "14.0",
        "results": results,
        "limitations": [
            "Offline processing only; no SDL playback device, headphones or microphone opened.",
            "Deterministic synthetic source, not the complete game mixer or actual gameplay audio.",
            "x86_64 on Apple silicon uses Rosetta; physical Intel and macOS 14 remain unverified.",
            "Timings include synthetic generation and conversion but exclude initialization and the game mixer.",
            "Offline timings do not prove real-time deadlines or end-to-end latency.",
            "Stress signal uses seven correlated 0.9-amplitude tones, with silent LFE; it is not recorded gameplay.",
            "Windows Spatial Sound playback, endpoint recovery and native providers remain unverified.",
        ],
    }
    destination = args.scratch / "stream-results.json"
    destination.write_text(json.dumps(record, indent=2) + "\n")
    print(f"Saved {destination}")


if __name__ == "__main__":
    main()
