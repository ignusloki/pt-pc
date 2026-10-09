"""Package the arm64 runtime and optional native macOS installer on a Mac."""
import argparse
import hashlib
import plistlib
import re
import shutil
import stat
import subprocess
import sys
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
APP_NAME = "P.T..app"
SETUP_NAME = "P.T. Mac Setup.app"
MODELS = ("ggml-base.en-q5_1.bin", "ggml-small.en-q5_1.bin", "ggml-silero-v6.2.0.bin")
VOICE_LIBS = ("libwhisper.dylib", "libggml.dylib", "libggml-base.dylib")
MACH_MAGICS = {b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xcf", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca",
               b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca"}


def run(*args):
    subprocess.run([str(arg) for arg in args], check=True)


def info(name, executable, identifier, version, microphone=False):
    data = dict(CFBundleName=name, CFBundleDisplayName=name, CFBundleExecutable=executable,
                CFBundleIdentifier=identifier, CFBundlePackageType="APPL",
                CFBundleShortVersionString=version.split("-")[0], CFBundleVersion=version.split("-")[0],
                LSMinimumSystemVersion="14.0", NSHighResolutionCapable=True,
                SDL_FILESYSTEM_BASE_DIR_TYPE="resource")
    if microphone:
        data["NSMicrophoneUsageDescription"] = "P.T. listens for your voice during the final puzzle."
    return data


def make_bundle(app, metadata):
    for folder in ("MacOS", "Resources", "Frameworks"):
        (app / "Contents" / folder).mkdir(parents=True, exist_ok=True)
    (app / "Contents/Info.plist").write_bytes(plistlib.dumps(metadata))
    (app / "Contents/PkgInfo").write_bytes(b"APPL????")


def executable(source, target):
    shutil.copy2(source, target)
    target.chmod(target.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def zip_tree(root, archive):
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
        for file in sorted(root.rglob("*")):
            if file.is_symlink():
                raise RuntimeError(f"Bundle contains a symlink that the installer cannot preserve: {file}")
            if file.is_file():
                entry = zipfile.ZipInfo.from_file(file, (Path(root.name) / file.relative_to(root)).as_posix())
                entry.create_system = 3
                mode = file.stat().st_mode
                if file.parent.name == "MacOS" or file.suffix == ".dylib":
                    mode |= 0o111
                entry.external_attr = mode << 16
                entry.compress_type = zipfile.ZIP_DEFLATED
                with file.open("rb") as source, z.open(entry, "w", force_zip64=True) as target:
                    shutil.copyfileobj(source, target)


def is_mach(file):
    with file.open("rb") as stream:
        return stream.read(4) in MACH_MAGICS


def audit_dependencies(root):
    """Resolve Mach-O load commands inside the distribution, without SDK paths."""
    root = root.resolve()
    executable_dir = root / "Contents/MacOS" if root.suffix == ".app" else root
    binaries = [file for file in sorted(root.rglob("*")) if file.is_file() and is_mach(file)]
    rpaths = {}
    dependencies = {}
    for file in binaries:
        run("lipo", "-verify_arch", "arm64", file)
        commands = subprocess.check_output(["otool", "-arch", "arm64", "-l", str(file)], text=True)
        for modern, legacy in re.findall(r"\bminos ([\d.]+)|cmd LC_VERSION_MIN_MACOSX\s+cmdsize \d+\s+version ([\d.]+)", commands):
            minimum = tuple((list(map(int, (modern or legacy).split('.'))) + [0, 0, 0])[:3])
            if minimum > (14, 0, 0):
                raise RuntimeError(f"{file.name} requires macOS {modern or legacy}, above the app minimum 14.0")
        rpaths[file] = re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset \d+\)", commands)
        ids = subprocess.check_output(["otool", "-arch", "arm64", "-D", str(file)], text=True).splitlines()[1:]
        dependencies[file] = [line.strip().split(" (", 1)[0] for line in
                              subprocess.check_output(["otool", "-arch", "arm64", "-L", str(file)], text=True).splitlines()[1:]
                              if line.strip().split(" (", 1)[0] not in ids]
    executable_rpaths = [(file, path) for file, paths in rpaths.items() if file.parent == executable_dir for path in paths]

    def expand(path, loader):
        if path == "@loader_path":
            return loader.parent
        if path == "@executable_path":
            return executable_dir
        if path.startswith("@loader_path/"):
            return loader.parent / path[len("@loader_path/"):]
        if path.startswith("@executable_path/"):
            return executable_dir / path[len("@executable_path/"):]
        return Path(path) if path.startswith("/") else None

    for file, names in dependencies.items():
        for dependency in names:
            if dependency.startswith(("/usr/lib/", "/System/Library/")):
                continue
            if dependency.startswith("@rpath/"):
                candidates = [base / dependency[len("@rpath/"):] for loader, path in
                              [(file, path) for path in rpaths[file]] + executable_rpaths
                              if (base := expand(path, loader)) is not None]
            else:
                candidates = [expand(dependency, file)]
            if not any(target is not None and target.resolve().is_relative_to(root) and target.is_file()
                       for target in candidates):
                raise RuntimeError(f"Unbundled Mach-O dependency in {file.name}: {dependency}")


def sign_tree(root, identity="-", entitlements=None):
    # Sign leaf code before its app. Compression preserves signed bytes in the payload.
    audit_dependencies(root)
    options = ["--options", "runtime", "--timestamp"] if identity != "-" else []
    for file in sorted(root.rglob("*")):
        if file.is_file() and is_mach(file):
            executable_entitlements = entitlements if file.parent.name == "MacOS" else None
            if file.name == "PT.PkgExtract":
                executable_entitlements = REPO / "tools/macos/extractor-entitlements.plist"
            flags = ["--entitlements", executable_entitlements] if executable_entitlements else []
            run("codesign", "--force", "--sign", identity, *options, *flags, file)
    if root.suffix == ".app":
        flags = ["--entitlements", entitlements] if entitlements else []
        run("codesign", "--force", "--sign", identity, *options, *flags, root)
        run("codesign", "--verify", "--deep", "--strict", root)


def relocate_voice(folder):
    libraries = sorted(p for p in folder.iterdir() if p.is_file() and p.suffix in {".dylib", ".so"})
    names = {file.name for file in libraries}
    for library in libraries:
        # GGML's dynamically loaded CPU module has no dylib install ID.
        ids = subprocess.check_output(["otool", "-arch", "arm64", "-D", str(library)], text=True).splitlines()
        if len(ids) > 1:
            run("install_name_tool", "-id", f"@rpath/{library.name}", library)
        dependencies = subprocess.check_output(["otool", "-arch", "arm64", "-L", str(library)], text=True).splitlines()[1:]
        for line in dependencies:
            dependency = line.strip().split(" (", 1)[0]
            name = Path(dependency).name
            if name == library.name:
                continue
            if name in names and dependency != f"@loader_path/{name}":
                run("install_name_tool", "-change", dependency, f"@loader_path/{name}", library)
            elif dependency.startswith("/") and not dependency.startswith(("/usr/lib/", "/System/Library/")):
                raise RuntimeError(f"Unbundled voice dependency: {dependency}")


def runtime(build, output, version, identity="-", exe=None, moltenvk=None):
    root = output / "pt-port-macos-arm64"
    archive = output / "pt-port-macos-arm64.zip"
    if root.exists() or archive.exists():
        raise RuntimeError(f"Output already exists; choose a new output directory: {root}")
    exe = exe or build / "pt_release"
    moltenvk = moltenvk or build / "libMoltenVK.dylib"
    required = [exe, moltenvk, build / "voice/licenses", build / "fonts"]
    cpu_modules = sorted((build / "voice").glob("libggml-cpu-*.so"))
    if not cpu_modules:
        raise RuntimeError("Missing build output: architecture-specific voice CPU modules")
    required += [build / "voice" / name for name in MODELS + VOICE_LIBS]
    for file in required:
        if not file.exists():
            raise RuntimeError(f"Missing build output: {file}")
    for file in [exe, moltenvk, *(build / "voice" / name for name in VOICE_LIBS), *cpu_modules]:
        if not is_mach(file):
            raise RuntimeError(f"Expected a Mach-O executable or library: {file}")
    shaders = sorted((build / "shaders").glob("*.spv"))
    expected = {f"{file.name}.spv" for file in (REPO / "shaders").iterdir() if file.suffix in {".vert", ".frag", ".comp"}}
    if not expected.issubset({file.name for file in shaders}):
        raise RuntimeError("Shader build is incomplete; build the pt_shaders target first")
    app = root / APP_NAME
    make_bundle(app, info("P.T.", "pt", "org.pt-port.game", version, microphone=True))
    resources = app / "Contents/Resources"
    executable(exe, app / "Contents/MacOS/pt")
    executable(moltenvk, app / "Contents/Frameworks/libMoltenVK.dylib")
    (resources / "shaders").mkdir()
    for file in shaders:
        shutil.copy2(file, resources / "shaders" / file.name)
    shutil.copytree(build / "fonts", resources / "fonts")
    voice = resources / "voice"
    voice.mkdir()
    frameworks = app / "Contents/Frameworks"
    for name in MODELS:
        shutil.copy2(build / "voice" / name, voice / name)
    for name in VOICE_LIBS:
        executable(build / "voice" / name, voice / name)
    for file in cpu_modules:
        executable(file, voice / file.name)
    shutil.copytree(build / "voice/licenses", voice / "licenses")
    relocate_voice(voice)
    notices = resources / "licenses"
    if (build / "licenses").exists():
        shutil.copytree(build / "licenses", notices)
    else:
        notices.mkdir()
    shutil.copy2(REPO / "LICENSE", notices / "PT-MIT.txt")
    shutil.copytree(REPO / "third_party/macos_notices", notices / "macos")
    shutil.copy2(REPO / "docs/macos.md", root / "README-macOS.md")
    sign_tree(app, identity, REPO / "tools/macos/game-entitlements.plist")
    zip_tree(root, archive)
    return archive


def setup(build, runtime_zip, extractor, source, output, version, identity="-"):
    app = output / SETUP_NAME
    if app.exists() or (output / "PT-Mac-Setup-arm64.zip").exists():
        raise RuntimeError("Setup output already exists; choose a new output directory")
    if not (extractor / "PT.PkgExtract").is_file():
        raise RuntimeError("Publish the self-contained osx-arm64 extractor first")
    for file in (extractor / "PT.PkgExtract", build / "pt_setup_macos"):
        if not file.is_file() or not is_mach(file):
            raise RuntimeError(f"Expected a Mach-O executable: {file}")
    sign_tree(extractor, identity)
    payload_dir = output / "payload"
    run(sys.executable, REPO / "tools/prepare_native_installer.py", "--runtime", runtime_zip,
        "--extractor", extractor, "--source", source, "--out", payload_dir, "--runtime-id", "osx-arm64")
    make_bundle(app, info("P.T. Mac Setup", "pt_setup_macos", "org.pt-port.setup", version))
    executable(build / "pt_setup_macos", app / "Contents/MacOS/pt_setup_macos")
    payload = app / "Contents/Resources/payload.bin"
    shutil.copy2(payload_dir / "payload.bin", payload)
    with payload.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    (app / "Contents/Resources/payload.sha256").write_text(digest + "\n", encoding="ascii")
    sign_tree(app, identity)
    # ditto creates a Finder-compatible ZIP, including the app's execute bits.
    run("ditto", "-c", "-k", "--keepParent", app, output / "PT-Mac-Setup-arm64.zip")
    return app


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--build", type=Path, default=REPO / "build/macos-arm64")
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--version", default="1.0.3")
    p.add_argument("--identity", default="-", help="Developer ID Application identity, or '-' for local ad-hoc signing")
    p.add_argument("--exe", type=Path)
    p.add_argument("--moltenvk", type=Path)
    p.add_argument("--extractor", type=Path)
    p.add_argument("--source", type=Path)
    args = p.parse_args()
    if sys.platform != "darwin":
        p.error("Build and sign the macOS app on a Mac")
    if bool(args.extractor) != bool(args.source):
        p.error("--extractor and --source must be supplied together")
    args.out.mkdir(parents=True, exist_ok=True)
    archive = runtime(args.build.resolve(), args.out.resolve(), args.version, args.identity, args.exe, args.moltenvk)
    print(archive)
    if args.extractor:
        print(setup(args.build.resolve(), archive, args.extractor.resolve(), args.source.resolve(), args.out.resolve(), args.version, args.identity))


if __name__ == "__main__":
    main()
