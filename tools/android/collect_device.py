#!/usr/bin/env python3
"""Read an Android device's capabilities; this does not install or run the game."""
import argparse
import datetime as dt
import json
import math
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]


def run(argv, timeout=30):
    try:
        result = subprocess.run(argv, text=True, capture_output=True, timeout=timeout)
        return result.returncode, result.stdout.strip(), result.stderr.strip()
    except subprocess.TimeoutExpired:
        return 124, "", "Command timed out"


def requirements():
    source = (REPO / "src/engine/render/vk_context.cpp").read_text()
    if "props.apiVersion < VK_API_VERSION_1_3" not in source or "VkPhysicalDeviceVulkan13Features features13{" not in source:
        raise RuntimeError("Renderer API requirement changed; review this collector before using it")
    required_block = source.split("VkPhysicalDeviceVulkan13Features features13{", 1)[1].split("features.features.independentBlend", 1)[0]
    # Analyze the Android branch: full BC is now enabled only when supported; each uploaded format is checked separately.
    required_block = re.sub(r"#ifdef __ANDROID__\n(.*?)#else\n.*?#endif", r"\1", required_block, flags=re.S)
    enabled = re.findall(r"\bfeatures(?:12|13)?\.(?:features\.)?(\w+)\s*=\s*VK_TRUE;", required_block)
    features = sorted(set(enabled))
    if len(features) != 17 or "features.features.textureCompressionBC = supported.textureCompressionBC" not in required_block:
        raise RuntimeError("Renderer feature requests changed; review this collector before using it")
    texture_source = (REPO / "src/engine/render/texture_manager.h").read_text()
    count = sum(int(re.search(r"\b" + key + r"\s*=\s*(\d+)", texture_source)[1])
                for key in ("kMaxTextures", "kMaxCubeTextures"))
    return features, {
        "maxPerStageDescriptorUpdateAfterBindSamplers": count,
        "maxPerStageDescriptorUpdateAfterBindSampledImages": count,
        "maxDescriptorSetUpdateAfterBindSamplers": count,
        "maxDescriptorSetUpdateAfterBindSampledImages": count,
    }


def values_named(tree, name, path=""):
    normalized = lambda value: re.sub(r"[^a-z0-9]", "", value.lower())
    found = []
    if isinstance(tree, dict):
        for key, value in tree.items():
            location = f"{path}.{key}" if path else key
            if normalized(key) == normalized(name):
                found.append((location, value))
            else:
                found.extend(values_named(value, name, location))
    elif isinstance(tree, list):
        for index, value in enumerate(tree):
            found.extend(values_named(value, name, f"{path}[{index}]"))
    return found


def reported_integer(tree, name):
    evidence = values_named(tree, name)
    # Android's GPU service emits many Vulkan integers as JSON numbers such as 1.0.
    numbers = [int(value) for _, value in evidence if isinstance(value, (bool, int)) or
               (isinstance(value, float) and math.isfinite(value) and value.is_integer() and abs(value) <= 2**53)]
    if not numbers or len(set(numbers)) != 1 or len(numbers) != len(evidence):
        return None, evidence
    return int(numbers[0]), evidence


def analyze_vulkan(payload):
    features, limits = requirements()
    devices = payload.get("devices", [])
    if not isinstance(devices, list):
        raise ValueError("Expected Vulkan JSON with a devices array")
    reports = []
    for device in devices:
        properties = device.get("properties", {})
        packed, version_evidence = reported_integer(properties, "apiVersion")
        version = None if packed is None else [(packed >> 22) & 127, (packed >> 12) & 1023, packed & 4095]
        checks = [{"name": "Vulkan API", "required": "1.3", "reported": version,
                   "pass": None if version is None else version[:2] >= [1, 3], "evidence": version_evidence}]
        for name in features:
            value, evidence = reported_integer(device, name)
            checks.append({"name": name, "required": True, "reported": value,
                           "pass": None if value not in (0, 1) else bool(value), "evidence": evidence})
        for name, minimum in limits.items():
            value, evidence = reported_integer(device, name)
            checks.append({"name": name, "required": minimum, "reported": value,
                           "pass": None if value is None else value >= minimum, "evidence": evidence})
        failed = [check["name"] for check in checks if check["pass"] is False]
        unknown = [check["name"] for check in checks if check["pass"] is None]
        full_bc, bc_evidence = reported_integer(device, "textureCompressionBC")
        reports.append({"gpu": properties.get("deviceName", "unknown"), "api_version": version,
                        "optional_full_bc_family": {"reported": full_bc, "evidence": bc_evidence,
                            "note": "Not mandatory on Android; individual game texture formats must still be validated"},
                        "driver_version": properties.get("driverVersion"),
                        "queried_requirements": "incompatible" if failed else "incomplete" if unknown else "pass",
                        "failed": failed, "unknown": unknown, "checks": checks})
    return {"devices": reports, "scope": "Feature and texture-table limit screen only; no rendering or game test",
            "remaining": ["Check image formats and remaining descriptor/pipeline limits in an APK",
                          "Create the actual window, device and render pipelines",
                          "Measure game memory, frame times, microphone load and sustained thermals"]}


def parse_meminfo(text):
    values = {}
    for line in text.splitlines():
        match = re.match(r"^(MemTotal|MemAvailable|SwapTotal|SwapFree):\s*(\d+)\s+kB$", line.strip())
        if match:
            values[match[1] + "_bytes"] = int(match[2]) * 1024
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", help="Path to Google's adb")
    parser.add_argument("--serial", help="Explicit device selector when multiple phones are attached")
    parser.add_argument("--expected-model", default="SM-S901B")
    parser.add_argument("--vk-json", type=Path, help="Analyze an already captured GPU JSON without using USB")
    parser.add_argument("--out", type=Path, help="New output folder (defaults to ignored .deps)")
    args = parser.parse_args()
    stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")
    output = args.out or REPO / ".deps/android-research-20261013" / ("device-" + stamp)
    output.mkdir(parents=True, exist_ok=False)
    if args.vk_json:
        report = analyze_vulkan(json.loads(args.vk_json.read_text()))
        (output / "vulkan-analysis.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps({"output": str(output), "devices": [{key: device[key] for key in
              ("gpu", "api_version", "queried_requirements", "failed", "unknown")} for device in report["devices"]]}, indent=2))
        return 0
    adb = args.adb or shutil.which("adb")
    if not adb:
        parser.error("Supply --adb with the path to SDK Platform-Tools")
    code, listing, error = run([adb, "devices", "-l"])
    if code:
        raise RuntimeError("ADB could not list devices: " + error)
    ready, unauthorized = [], 0
    for line in listing.splitlines():
        fields = line.split()
        if len(fields) >= 2 and fields[1] == "unauthorized":
            unauthorized += 1
        if len(fields) >= 2 and fields[1] == "device" and not fields[0].startswith("emulator-"):
            ready.append(fields[0])
    if args.serial:
        ready = [serial for serial in ready if serial == args.serial]
    if len(ready) != 1:
        state = {"status": "needs_device", "authorized_physical_devices": len(ready),
                 "unauthorized_devices": unauthorized,
                 "next": "Connect and authorize the S22; use --serial if more than one phone is attached"}
        (output / "connection.json").write_text(json.dumps(state, indent=2) + "\n")
        print(json.dumps(state, indent=2))
        return 2
    # Every command below only reads device information. No broad getprop dump, app list or input event capture.
    prefix = [adb, "-s", ready[0], "shell"]
    model_result = run(prefix + ["getprop", "ro.product.model"])
    model = model_result[1]
    if model_result[0] or model.replace("_", "-") != args.expected_model:
        raise RuntimeError("Connected model does not match the selected test phone: " + model)
    properties = {"model": model}
    for key, prop in {"manufacturer": "ro.product.manufacturer", "android": "ro.build.version.release",
                      "sdk": "ro.build.version.sdk", "security_patch": "ro.build.version.security_patch",
                      "soc_manufacturer": "ro.soc.manufacturer", "soc_model": "ro.soc.model",
                      "board": "ro.product.board", "abis": "ro.product.cpu.abilist"}.items():
        code, text, _ = run(prefix + ["getprop", prop])
        properties[key] = text if code == 0 else None
    collected = {}
    for name, command in {
        "memory": ["cat", "/proc/meminfo"], "page-size": ["getconf", "PAGE_SIZE"],
        "storage": ["df", "-k", "/data"], "vulkan": ["cmd", "gpu", "vkjson"],
        "thermal": ["dumpsys", "thermalservice"],
    }.items():
        code, text, error = run(prefix + command, timeout=45)
        (output / (name + ".txt")).write_text(text + "\n")
        if error:
            (output / (name + "-error.txt")).write_text(error + "\n")
        collected[name] = {"exit_code": code, "output": name + ".txt"}
    report = {"captured_utc": stamp, "source_commit": run(["git", "-C", str(REPO), "rev-parse", "HEAD"])[1],
              "device": properties, "memory": parse_meminfo((output / "memory.txt").read_text()),
              "page_size_bytes": None, "commands": collected,
              "memory_scope": "System memory snapshot; game RAM and GPU allocations have not been measured"}
    try:
        report["page_size_bytes"] = int((output / "page-size.txt").read_text().strip())
    except ValueError:
        pass
    try:
        if collected["vulkan"]["exit_code"]:
            raise ValueError("Android GPU service command failed")
        payload = json.loads((output / "vulkan.txt").read_text())
        report["vulkan"] = analyze_vulkan(payload)
        (output / "vulkan.json").write_text(json.dumps(payload, indent=2) + "\n")
    except (ValueError, TypeError, AttributeError) as error:
        report["vulkan"] = {"status": "unknown", "reason": str(error),
                            "next": "Use a native diagnostic APK; do not assume absent features are unsupported"}
    (output / "device-report.json").write_text(json.dumps(report, indent=2) + "\n")
    summary = {"output": str(output), "device": properties, "memory": report["memory"],
               "page_size_bytes": report["page_size_bytes"], "vulkan": report["vulkan"]}
    if "devices" in report["vulkan"]:
        summary["vulkan"] = [{key: device[key] for key in
               ("gpu", "api_version", "queried_requirements", "failed", "unknown")} for device in report["vulkan"]["devices"]]
    print(json.dumps(summary, indent=2))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print("Device check: " + str(error), file=sys.stderr)
        sys.exit(1)
