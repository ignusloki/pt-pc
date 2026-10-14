"""Build the isolated arm64 Android prototype APK; no game assets are packaged."""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
LIBORBIS_REVISION = "643477263b2644e0803e0f58b8726ea4e3f3b7d4"
NDK_VERSION = "28.2.13676358"


def run(*args):
    print("+", " ".join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), cwd=REPO, check=True)


def prepare_extractor(original, destination):
    if destination.exists():
        return destination
    if not (original / "LibOrbisPkg.Core/LibOrbisPkg.Core.csproj").is_file():
        raise RuntimeError("Supply the pinned LibOrbisPkg checkout with --liborbis")
    # Match the desktop extractor's patches, leaving the user's checkout intact.
    shutil.copytree(original, destination, ignore=shutil.ignore_patterns(".git", "bin", "obj"))
    project = destination / "LibOrbisPkg.Core/LibOrbisPkg.Core.csproj"
    source = project.read_text(encoding="utf-8-sig")
    source = source.replace("netcoreapp3.0", "net10.0").replace("<LangVersion>7.3</LangVersion>", "<LangVersion>latest</LangVersion>")
    source = source.replace("<GenerateSerializationAssemblies>Auto</GenerateSerializationAssemblies>", "<GenerateSerializationAssemblies>Off</GenerateSerializationAssemblies>")
    project.write_text(source)
    run(sys.executable, REPO / "tools/patch_liborbis_readers.py", destination)
    return destination


def stage_runtime(build, ndk, sdl_source):
    stage = build / "stage"
    native = stage / "lib/arm64-v8a"
    assets = stage / "assets"
    native.mkdir(parents=True, exist_ok=True)
    assets.mkdir(parents=True, exist_ok=True)
    icon = stage / "resources/drawable/pt_icon.png"
    icon.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(REPO / "assets/macos/pt-icon.png", icon)
    libraries = [build / "libmain.so", build / "_deps/sdl3-build/libSDL3.so"]
    libraries += list((build / "voice").glob("*.so"))
    libraries.append(ndk / "toolchains/llvm/prebuilt/darwin-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so")
    for library in libraries:
        if not library.is_file():
            raise RuntimeError(f"Missing native library: {library}")
        shutil.copy2(library, native / library.name)
    for directory, source in (("shaders", build / "shaders"), ("fonts", REPO / "assets/fonts"), ("voice", build / "voice")):
        target = assets / "runtime" / directory
        target.mkdir(parents=True, exist_ok=True)
        for path in source.rglob("*"):
            if not path.is_file() or path.suffix in (".so", ".dylib", ".dll"):
                continue
            destination = target / path.relative_to(source)
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, destination)
    licenses = assets / "licenses"
    licenses.mkdir(exist_ok=True)
    for name in ("LICENSE", "LICENSE.md", "NOTICE", "COPYING"):
        if (REPO / name).is_file():
            shutil.copy2(REPO / name, licenses / name)
    java = stage / "java"
    shutil.copytree(sdl_source / "android-project/app/src/main/java", java, dirs_exist_ok=True)
    entries = {}
    for path in assets.rglob("*"):
        if path.is_file():
            entries[path.relative_to(assets).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    (assets / "runtime-manifest.json").write_text(json.dumps(entries, indent=2) + "\n")
    return stage


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--jdk", type=Path, required=True)
    parser.add_argument("--liborbis", type=Path, required=True, help="Pinned LibOrbisPkg source checkout used by the desktop installer")
    parser.add_argument("--source-cache", type=Path, help="Reuse an existing CMake build's dependency sources and voice models")
    parser.add_argument("--build", type=Path, default=REPO / "build/android-prototype")
    parser.add_argument("--out", type=Path, default=REPO / "dist/android/prototype")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--native-only", action="store_true")
    args = parser.parse_args()
    build, sdk = args.build.resolve(), args.sdk.resolve()
    ndk = sdk / "ndk" / NDK_VERSION
    if not ndk.is_dir():
        parser.error(f"Install NDK {NDK_VERSION} in {sdk}")
    glslc = shutil.which("glslc")
    if not glslc:
        parser.error("Install a host glslc shader compiler")
    build.mkdir(parents=True, exist_ok=True)
    cached = []
    if args.source_cache:
        cache = args.source_cache.resolve()
        for name in ("sdl3", "zlib", "volk", "vma", "glm", "imgui", "stb", "lua51", "whisper", "bc7enc", "harfbuzz", "ogg", "vorbis"):
            path = cache / "_deps" / f"{name}-src"
            if path.is_dir():
                cached.append(f"-DFETCHCONTENT_SOURCE_DIR_{name.upper()}={path}")
        (build / "voice").mkdir(exist_ok=True)
        for model in (cache / "voice").glob("*.bin"):
            shutil.copy2(model, build / "voice" / model.name)
        codebooks = cache / "_deps/ww2ogg/packed_codebooks_aoTuV_603.bin"
        if codebooks.is_file():
            (build / "_deps/ww2ogg").mkdir(parents=True, exist_ok=True)
            shutil.copy2(codebooks, build / "_deps/ww2ogg" / codebooks.name)
    run("cmake", "-S", REPO, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
        f"-DCMAKE_TOOLCHAIN_FILE={ndk}/build/cmake/android.toolchain.cmake",
        "-DCMAKE_SYSTEM_NAME=Android", "-DANDROID_ABI=arm64-v8a", "-DANDROID_PLATFORM=android-29", "-DANDROID_STL=c++_shared",
        f"-DVulkan_GLSLC_EXECUTABLE={glslc}", "-DPT_UPSCALERS=OFF", "-DPT_STREAMLINE=OFF", "-DPT_OPENXR=OFF",
        "-DPT_VERSION_OVERRIDE=1.0.7-android.1", *cached)
    run("cmake", "--build", build, "--parallel", args.jobs, "--target", "pt")
    if args.native_only:
        return
    sdl_source = (args.source_cache.resolve() / "_deps/sdl3-src") if args.source_cache else build / "_deps/sdl3-src"
    stage = stage_runtime(build, ndk, sdl_source)
    extractor = prepare_extractor(args.liborbis.resolve(), build / "liborbis")
    run("dotnet", "build", REPO / "android/Launcher/Launcher.csproj", "-c", "Debug",
        f"-p:AndroidSdkDirectory={sdk}", f"-p:JavaSdkDirectory={args.jdk.resolve()}",
        f"-p:LibOrbisSource={extractor}", f"-p:PtAndroidStage={stage}")
    apk = REPO / "android/Launcher/bin/Debug/net10.0-android36.0/android-arm64/org.ignusloki.pt.prototype-Signed.apk"
    if not apk.is_file():
        raise RuntimeError(f"Signed APK missing: {apk}")
    import zipfile
    with zipfile.ZipFile(apk) as archive:
        names = archive.namelist()
        if any(name.endswith((".pkg", ".psarc", ".qar", ".ftex", ".ftexs", ".wem")) for name in names):
            raise RuntimeError("Original game assets unexpectedly included in APK")
        packaged = hashlib.sha256(archive.read("lib/arm64-v8a/libmain.so")).hexdigest()
        with (build / "libmain.so").open("rb") as stream:
            if packaged != hashlib.file_digest(stream, "sha256").hexdigest():
                raise RuntimeError("Packaged native game library is stale")
    args.out.mkdir(parents=True, exist_ok=True)
    destination = args.out / "PT-Android-Prototype-arm64.apk"
    shutil.copy2(apk, destination)
    (args.out / "SHA256SUMS").write_text(hashlib.sha256(destination.read_bytes()).hexdigest() + "  " + destination.name + "\n")
    print(f"APK: {destination.resolve()}")


if __name__ == "__main__":
    main()
