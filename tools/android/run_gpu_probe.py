#!/usr/bin/env python3
"""Run the standalone allocation probe in a temporary folder on the selected phone."""
import argparse
import datetime as dt
import hashlib
import json
import subprocess
import uuid
from pathlib import Path


def command(argv, timeout=30):
    result = subprocess.run(argv, capture_output=True, text=True, timeout=timeout)
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or result.stdout.strip() or "Command failed")
    return result.stdout.strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", required=True)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--serial")
    args = parser.parse_args()
    # No emulator or automatic selection among multiple phones.
    devices = [line.split()[0] for line in command([args.adb, "devices"]).splitlines()
               if len(line.split()) >= 2 and line.split()[1] == "device" and not line.startswith("emulator-")]
    if args.serial:
        devices = [serial for serial in devices if serial == args.serial]
    if len(devices) != 1:
        raise RuntimeError("Connect and authorize the S22; use --serial if more than one phone is connected")
    prefix = [args.adb, "-s", devices[0]]
    if command(prefix + ["shell", "getprop", "ro.product.model"]) != "SM-S901B":
        raise RuntimeError("The selected device is not the S22 used for this study")
    data = args.binary.read_bytes()
    if data[:4] != b"\x7fELF" or data[4] != 2 or int.from_bytes(data[18:20], "little") != 183:
        raise RuntimeError("Expected an Android ARM64 ELF executable")
    args.out.mkdir(parents=True, exist_ok=False)
    stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    remote_dir = "/data/local/tmp/pt-port-gpu-probe-" + stamp + "-" + uuid.uuid4().hex[:10]
    remote = remote_dir + "/probe"
    created = False
    try:
        command(prefix + ["shell", "mkdir", remote_dir])
        created = True
        command(prefix + ["push", str(args.binary), remote], timeout=60)
        command(prefix + ["shell", "chmod", "700", remote])
        result = subprocess.run(prefix + ["shell", remote], text=True, capture_output=True, timeout=45)
        (args.out / "stdout.txt").write_text(result.stdout)
        (args.out / "stderr.txt").write_text(result.stderr)
        report = json.loads(result.stdout)
        report.update({"captured_utc": stamp, "exit_code": result.returncode,
                       "probe_sha256": hashlib.sha256(data).hexdigest(), "model": "SM-S901B"})
        (args.out / "native-probe.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report, indent=2))
        if result.returncode or not report.get("pass"):
            raise RuntimeError("Native allocation probe failed; preserve its logs before further work")
    finally:
        if created:
            # Delete only this run's own executable, then remove the now-empty generated directory.
            command(prefix + ["shell", "rm", "-f", remote])
            command(prefix + ["shell", "rmdir", remote_dir])


if __name__ == "__main__":
    main()
