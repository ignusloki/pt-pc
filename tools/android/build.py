"""Build an isolated arm64 Android APK; Release requires a private signing key."""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import re
import struct
import zipfile
from datetime import datetime, timezone
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
LIBORBIS_REVISION = "643477263b2644e0803e0f58b8726ea4e3f3b7d4"
NDK_VERSION = "28.2.13676358"
VERSION = "1.0.7-android-beta.1"


def run(*args):
    print("+", " ".join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), cwd=REPO, check=True)


def prepare_extractor(original, destination):
    if not (original / "LibOrbisPkg.Core/LibOrbisPkg.Core.csproj").is_file():
        raise RuntimeError("Supply the pinned LibOrbisPkg checkout with --liborbis")
    revision = subprocess.check_output(["git", "-C", str(original), "rev-parse", "HEAD"], text=True).strip()
    if revision != LIBORBIS_REVISION:
        raise RuntimeError(f"LibOrbisPkg must be at {LIBORBIS_REVISION}, found {revision}")
    if destination.exists():
        return destination
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
    if (build / "licenses").is_dir():
        shutil.copytree(build / "licenses", licenses, dirs_exist_ok=True)
    dependencies = sdl_source.parent
    notices = {"SDL3": sdl_source / "LICENSE.txt", "SDL-hidapi": sdl_source / "src/hidapi/LICENSE-bsd.txt",
               "zlib": dependencies / "zlib-src/LICENSE", "volk": dependencies / "volk-src/LICENSE.md",
               "VMA": dependencies / "vma-src/LICENSE.txt", "GLM": dependencies / "glm-src/copying.txt",
               "Dear-ImGui": dependencies / "imgui-src/LICENSE.txt", "stb": dependencies / "stb-src/LICENSE",
               "Lua": dependencies / "lua51-src/COPYRIGHT", "HarfBuzz": dependencies / "harfbuzz-src/COPYING",
               "Ogg": dependencies / "ogg-src/COPYING", "Vorbis": dependencies / "vorbis-src/COPYING",
               "bc7enc": dependencies / "bc7enc-src/LICENSE"}
    for name, source in notices.items():
        if not source.is_file():
            raise RuntimeError(f"Missing dependency license: {source}")
        shutil.copy2(source, licenses / f"{name}.txt")
    shutil.copytree(REPO / "third_party/android_notices", licenses, dirs_exist_ok=True)
    shutil.copy2(build / "liborbis/LICENSE.txt", licenses / "LibOrbisPkg-LGPL-3.0.txt")
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
    parser.add_argument("--configuration", choices=("Debug", "Release"), default="Debug")
    parser.add_argument("--version", default=VERSION)
    parser.add_argument("--version-code", type=int, default=3)
    parser.add_argument("--keystore", type=Path, help="Private APK signing key; required for Release")
    parser.add_argument("--key-alias", help="Alias in the private signing keystore")
    parser.add_argument("--password-file", type=Path, help="Private file containing the keystore/key password")
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9A-Za-z][0-9A-Za-z.+-]*", args.version) or args.version_code < 1:
        parser.error("Use a valid release version and a positive version code")
    release = args.configuration == "Release"
    if release and not args.native_only:
        if not (args.keystore and args.keystore.is_file() and args.key_alias and args.password_file and args.password_file.is_file()):
            parser.error("Release requires --keystore, --key-alias and --password-file")
    source_commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=REPO, text=True).strip()
    source_dirty = subprocess.run(["git", "diff", "HEAD", "--quiet"], cwd=REPO).returncode != 0
    if release and source_dirty:
        parser.error("Commit tracked source changes before building a public Release APK")
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
        f"-DPT_VERSION_OVERRIDE={args.version}", *cached)
    run("cmake", "--build", build, "--parallel", args.jobs, "--target", "pt")
    if args.native_only:
        return
    sdl_source = (args.source_cache.resolve() / "_deps/sdl3-src") if args.source_cache else build / "_deps/sdl3-src"
    extractor = prepare_extractor(args.liborbis.resolve(), build / "liborbis")
    stage = stage_runtime(build, ndk, sdl_source)
    signing = []
    if release:
        signing = ["-p:AndroidKeyStore=true", f"-p:AndroidSigningKeyStore={args.keystore.resolve()}",
                   f"-p:AndroidSigningKeyAlias={args.key_alias}",
                   f"-p:AndroidSigningKeyPass=file:{args.password_file.resolve()}",
                   f"-p:AndroidSigningStorePass=file:{args.password_file.resolve()}"]
    run("dotnet", "build", REPO / "android/Launcher/Launcher.csproj", "-c", args.configuration,
        "-t:SignAndroidPackage", f"-p:ApplicationDisplayVersion={args.version}", f"-p:ApplicationVersion={args.version_code}",
        f"-p:AndroidSdkDirectory={sdk}", f"-p:JavaSdkDirectory={args.jdk.resolve()}",
        f"-p:LibOrbisSource={extractor}", f"-p:PtAndroidStage={stage}", *signing)
    app_id = "org.ignusloki.pt.beta" if release else "org.ignusloki.pt.prototype"
    apk = REPO / f"android/Launcher/bin/{args.configuration}/net10.0-android36.0/android-arm64/{app_id}-Signed.apk"
    if not apk.is_file():
        raise RuntimeError(f"Signed APK missing: {apk}")
    libraries = []
    with zipfile.ZipFile(apk) as archive:
        names = archive.namelist()
        if any(name.endswith((".pkg", ".psarc", ".qar", ".ftex", ".ftexs", ".wem")) for name in names):
            raise RuntimeError("Original game assets unexpectedly included in APK")
        packaged = hashlib.sha256(archive.read("lib/arm64-v8a/libmain.so")).hexdigest()
        with (build / "libmain.so").open("rb") as stream:
            if packaged != hashlib.file_digest(stream, "sha256").hexdigest():
                raise RuntimeError("Packaged native game library is stale")
        for name in names:
            if not name.startswith("lib/") or not name.endswith(".so"):
                continue
            if not name.startswith("lib/arm64-v8a/"):
                raise RuntimeError(f"Unexpected native ABI in APK: {name}")
            data = archive.read(name)
            if data[:6] != b"\x7fELF\x02\x01":
                raise RuntimeError(f"Expected little-endian 64-bit ELF: {name}")
            phoff, = struct.unpack_from("<Q", data, 32)
            phentsize, phnum = struct.unpack_from("<HH", data, 54)
            for index in range(phnum):
                offset = phoff + index * phentsize
                kind, = struct.unpack_from("<I", data, offset)
                align, = struct.unpack_from("<Q", data, offset + 48)
                if kind == 1 and align < 16384:
                    raise RuntimeError(f"Native library is not 16 KiB aligned: {name}")
            libraries.append(name)
    build_tools = sdk / "build-tools/36.0.0"
    run(build_tools / "zipalign", "-c", "-P", "16", "4", apk)
    signer_env = os.environ.copy()
    signer_env["JAVA_HOME"] = str(args.jdk.resolve())
    signed = subprocess.check_output([str(build_tools / "apksigner"), "verify", "--verbose", "--print-certs", str(apk)], env=signer_env, text=True)
    certificate = re.search(r"Signer #1 certificate SHA-256 digest: (\w+)", signed)
    if not certificate:
        raise RuntimeError("Unable to verify the APK signing certificate")
    manifest = subprocess.check_output([str(build_tools / "aapt"), "dump", "badging", str(apk)], text=True)
    if f"name='{app_id}'" not in manifest or f"versionCode='{args.version_code}'" not in manifest or f"versionName='{args.version}'" not in manifest:
        raise RuntimeError("Packaged application identity/version does not match the requested build")
    if "sdkVersion:'29'" not in manifest:
        raise RuntimeError("Unexpected Android minimum API level")
    if release and "application-debuggable" in manifest:
        raise RuntimeError("Public Release APK must not be debuggable")
    args.out.mkdir(parents=True, exist_ok=True)
    if release:
        source_zip = args.out / "LibOrbisPkg-Android-source.zip"
        with zipfile.ZipFile(source_zip, "w", zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(extractor.rglob("*")):
                relative = path.relative_to(extractor)
                if not path.is_file() or any(part in (".git", "bin", "obj") for part in relative.parts):
                    continue
                if path.suffix.lower() in (".dll", ".exe", ".pdb", ".pkg", ".psarc", ".qar"):
                    continue
                archive.write(path, "LibOrbisPkg/" + relative.as_posix())
            archive.write(REPO / "third_party/android_notices/GPL-3.0.txt", "GPL-3.0.txt")
            archive.writestr("BUILD-ANDROID.txt", f"LibOrbisPkg {LIBORBIS_REVISION}, patched for .NET 10 and robust reads.\n"
                            "Launcher/application source and build/relink instructions: \n"
                            f"https://github.com/ignusloki/pt-pc/tree/{source_commit}\n"
                            "See docs/android-prototype.md and tools/android/build.py.\n"
                            "Keep AndroidLinkMode=None and PublishTrimmed=false to retain the library's code.\n")
    destination = args.out / (f"PT-{args.version}-arm64.apk" if release else "PT-Android-Prototype-arm64.apk")
    shutil.copy2(apk, destination)
    with destination.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    (args.out / "SHA256SUMS").write_text(digest + "  " + destination.name + "\n")
    metadata = {"version": args.version, "version_code": args.version_code, "application_id": app_id,
                "configuration": args.configuration, "source_commit": source_commit, "tracked_source_dirty": source_dirty,
                "built_at_utc": datetime.now(timezone.utc).isoformat(), "apk": destination.name, "apk_bytes": destination.stat().st_size,
                "apk_sha256": digest, "native_game_sha256": packaged, "signer_certificate_sha256": certificate.group(1),
                "abi": "arm64-v8a", "minimum_android_api": 29, "target_android_api": 36,
                "ndk": NDK_VERSION, "liborbis_revision": LIBORBIS_REVISION,
                "native_libraries": len(libraries), "zip_alignment_16k": True, "elf_alignment_16k": True, "game_archives_included": False}
    (args.out / "BUILD.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(f"APK: {destination.resolve()}")


if __name__ == "__main__":
    main()
