"""Build and package the Apple Silicon installer on macOS."""
import argparse
import datetime
import json
import os
import platform
import shutil
import subprocess
import sys
import uuid
import wave
from pathlib import Path

import package

REPO = Path(__file__).resolve().parents[2]
LIBORBIS_REVISION = "643477263b2644e0803e0f58b8726ea4e3f3b7d4"
TESTS = ("pt_tests", "pt_platform_test", "pt_settings_roundtrip_test", "pt_save_status_test",
         "pt_save_reset_test", "pt_language_default_test", "pt_graphics_preset_test", "pt_voice_match_test", "pt_installer_update_test",
         "pt_reflection_mix_test", "pt_fast_walk_test", "pt_queue_handoff_test")


def run(*args):
    print("+", " ".join(map(str, args)), flush=True)
    subprocess.run([str(arg) for arg in args], cwd=REPO, check=True)


def prepare_source(original, destination):
    if not (original / "LibOrbisPkg.Core/LibOrbisPkg.Core.csproj").is_file():
        raise RuntimeError(f"Not a LibOrbisPkg source checkout: {original}")
    if original.resolve().is_relative_to(destination.resolve()) or destination.resolve().is_relative_to(original.resolve()):
        raise RuntimeError("The original source and patched copy must not overlap")
    # Never overlay an older patched tree: deleted upstream files would survive.
    shutil.copytree(original, destination, ignore=shutil.ignore_patterns(".git", "bin", "obj"))
    project = destination / "LibOrbisPkg.Core/LibOrbisPkg.Core.csproj"
    text = project.read_text(encoding="utf-8-sig")
    text = text.replace("netcoreapp3.0", "net10.0").replace("<LangVersion>7.3</LangVersion>", "<LangVersion>latest</LangVersion>")
    text = text.replace("<GenerateSerializationAssemblies>Auto</GenerateSerializationAssemblies>", "<GenerateSerializationAssemblies>Off</GenerateSerializationAssemblies>")
    project.write_text(text, encoding="utf-8")
    run(sys.executable, REPO / "tools/patch_liborbis_readers.py", destination)
    return destination


def verify_packaged_voice(app, output):
    # Load the shipped libraries/models and process silence, without a microphone
    # permission prompt or original game data.
    fixture = output / "voice-smoke.wav"
    log = output / "voice-smoke.log"
    with wave.open(str(fixture), "wb") as audio:
        audio.setnchannels(1)
        audio.setsampwidth(2)
        audio.setframerate(16000)
        audio.writeframes(b"\0\0" * 16000)
    run(app / "Contents/MacOS/pt", "--voice-test", fixture, "--log", log)
    if "voice test: file voice-smoke.wav" not in log.read_text(encoding="utf-8"):
        raise RuntimeError("Packaged voice smoke test did not process the WAV; see voice-smoke.log")


def copy_runtime_notices(extractor, artifacts, rid):
    config = json.loads((extractor / "PT.PkgExtract.runtimeconfig.json").read_text())
    version = next(f["version"] for f in config["runtimeOptions"]["includedFrameworks"] if f["name"] == "Microsoft.NETCore.App")
    assets = json.loads((artifacts / "obj/Extractor/project.assets.json").read_text())
    pack = next((Path(root) / ("microsoft.netcore.app.runtime." + rid) / version
                 for root in assets["packageFolders"]
                 if (Path(root) / ("microsoft.netcore.app.runtime." + rid) / version).is_dir()), None)
    if pack is None:
        raise RuntimeError("Microsoft runtime pack not found for its distribution notices")
    notices = extractor / "licenses"
    notices.mkdir(exist_ok=True)
    for name in ("LICENSE.TXT", "THIRD-PARTY-NOTICES.TXT"):
        shutil.copy2(pack / name, notices / ("dotnet-" + name))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--liborbis", type=Path, help="Existing upstream source; a pinned copy is downloaded when omitted")
    p.add_argument("--out", type=Path, help="New output directory (defaults to a timestamp under dist/macos)")
    p.add_argument("--version", default="1.0.4")
    p.add_argument("--build", type=Path, help="Optional isolated build directory")
    p.add_argument("--cmake-init", type=Path, help="Initial CMake cache")
    p.add_argument("--cmake-arg", action="append", default=[], help="Extra CMake option")
    p.add_argument("--identity", default="-", help="Developer ID Application identity, or '-' for local testing")
    p.add_argument("--jobs", type=int, default=6)
    args = p.parse_args()
    if sys.platform != "darwin" or platform.machine() != "arm64":
        p.error("Run this build on an Apple Silicon Mac using native arm64 Python")
    if sys.version_info < (3, 11):
        p.error("Python 3.11 or newer is required")
    for tool in ("cmake", "ninja", "clang", "clang++", "git", "dotnet", "glslc", "codesign", "lipo", "otool", "install_name_tool", "ditto"):
        if not shutil.which(tool):
            p.error(f"Missing {tool}; see docs/macos.md for prerequisites and Vulkan SDK setup")
    sdk = subprocess.check_output(["dotnet", "--version"], text=True).strip()
    if int(sdk.split(".")[0]) < 10:
        p.error("Install the .NET 10 arm64 SDK to build the package extractor")
    output = (args.out or REPO / "dist/macos" / datetime.datetime.now().strftime("%Y%m%d-%H%M%S")).resolve()
    if output.exists():
        p.error("Choose a new output directory; existing releases are preserved")
    build = (args.build or REPO / "build/macos-arm64").resolve()
    os.environ["PT_VERSION"] = args.version
    initial = ["-C", args.cmake_init.resolve()] if args.cmake_init else []
    run("cmake", "--preset", "macos-arm64", "-B", build, *initial, f"-DPT_VERSION_OVERRIDE={args.version}", *args.cmake_arg)
    run("cmake", "--build", build, "--parallel", args.jobs, "--target", "pt", "pt_release", "pt_setup_macos")
    run("cmake", "--build", build, "--parallel", args.jobs, "--target", *TESTS)
    fixtures = build / f"test-fixtures-{uuid.uuid4().hex}"
    fixtures.mkdir()
    test_args = {
        "pt_settings_roundtrip_test": [fixtures / "settings"],
        "pt_save_status_test": [fixtures / "save-status"],
        "pt_save_reset_test": [fixtures / "save-reset"],
        "pt_graphics_preset_test": [fixtures / "graphics.ini"],
        "pt_installer_update_test": [fixtures / "installer"],
    }
    for test in TESTS:
        run(build / test, *test_args.get(test, []))
    upstream = args.liborbis.resolve() if args.liborbis else build / "liborbis-upstream"
    if not args.liborbis:
        if not upstream.exists():
            run("git", "clone", "--no-checkout", "https://github.com/maxton/LibOrbisPkg.git", upstream)
            run("git", "-C", upstream, "checkout", "--detach", LIBORBIS_REVISION)
        revision = subprocess.check_output(["git", "-C", str(upstream), "rev-parse", "HEAD"], text=True).strip()
        if revision != LIBORBIS_REVISION:
            p.error("Cached LibOrbisPkg revision differs; pass --liborbis to explicitly use another checkout")
    source = prepare_source(upstream, build / f"liborbis-src-{uuid.uuid4().hex}")
    extractor = build / f"extractor-osx-arm64-{uuid.uuid4().hex}"
    dotnet_artifacts = build / f"dotnet-artifacts-{uuid.uuid4().hex}"
    # Use official Microsoft NuGet packs even when the SDK is from Homebrew.
    # Its bundled runtime pack can link bottles built for this host's newer OS.
    microsoft_packs = build / "microsoft-runtime-packs"
    microsoft_packs.mkdir(exist_ok=True)
    # .NET 10 reads pruning metadata before downloading targeting packs. Keep
    # using the SDK's metadata without selecting its host-specific runtime.
    sdk_packs = subprocess.check_output(["dotnet", "msbuild", str(REPO / "installer/Extractor/Extractor.csproj"),
                                         "-getProperty:NetCoreTargetingPackRoot"], cwd=REPO, text=True).strip()
    run("dotnet", "publish", REPO / "installer/Extractor/Extractor.csproj", "-c", "Release", "-r", "osx-arm64",
        "--self-contained", "true", f"-p:LibOrbisSource={source}", f"-p:NetCoreTargetingPackRoot={microsoft_packs}",
        f"-p:PrunePackageTargetingPackRoots={sdk_packs}",
        "--artifacts-path", dotnet_artifacts, "-o", extractor)
    copy_runtime_notices(extractor, dotnet_artifacts, "osx-arm64")
    run("dotnet", "build", REPO / "installer/Tests/Tests.csproj", "-c", "Release",
        f"-p:LibOrbisSource={source}", "--artifacts-path", dotnet_artifacts)
    run("dotnet", dotnet_artifacts / "bin/Tests/release/Tests.dll", build / f"extractor-tests-{uuid.uuid4().hex}")
    # Packaging checks all Mach-O files for arm64 and signs the app after relocation.
    output.mkdir(parents=True)
    archive = package.runtime(build, output, args.version, args.identity)
    verify_packaged_voice(output / "pt-port-macos-arm64" / package.APP_NAME, output)
    installer = package.setup(build, archive, extractor, source, output, args.version, args.identity)
    run(installer / "Contents/MacOS/pt_setup_macos", "--self-test", output / "installer-self-test.txt")
    print(f"\nInstaller: {installer}\nDistribution ZIP: {output / 'PT-Mac-Setup-arm64.zip'}")


if __name__ == "__main__":
    main()
