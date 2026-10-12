"""Shared signing and dependency checks for the portable app and native installer."""
import re
import shutil
import subprocess
from pathlib import Path
REPO = Path(__file__).resolve().parents[2]
ICON_FILE = "pt.icns"
MINIMUM_MACOS = "14.0"


def copy_icon(resources):
    shutil.copy2(REPO / "assets/macos" / ICON_FILE, resources / ICON_FILE)
    return ICON_FILE

MACH_MAGICS = {b"\xcf\xfa\xed\xfe", b"\xfe\xed\xfa\xcf", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca", b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca"}

def run(*args):
    subprocess.run([str(arg) for arg in args], check=True)


def is_mach(file):
    with file.open("rb") as stream:
        return stream.read(4) in MACH_MAGICS


def bundle_extractor_brotli(root, arch):
    """Homebrew's .NET runtime can link Brotli outside its self-contained publish.

    Copy only those MIT libraries and their license, then replace their absolute
    load paths. Every other external dependency is still rejected by the audit.
    Official Microsoft runtime packs normally need no changes here.
    """
    cpu = {"arm64": "arm64", "x64": "x86_64"}[arch]
    pending = [p for p in root.iterdir() if p.is_file() and is_mach(p)]
    origins = {p: p.resolve() for p in pending}
    seen = set()
    while pending:
        file = pending.pop()
        if file in seen:
            continue
        seen.add(file)
        lines = subprocess.check_output(["otool", "-arch", cpu, "-L", str(file)], text=True).splitlines()[1:]
        for line in lines:
            dependency = line.strip().split(" (", 1)[0]
            name = Path(dependency).name
            if name == file.name or not re.fullmatch(r"libbrotli(?:dec|enc|common)(?:\.\d+)*\.dylib", name):
                continue
            if dependency == "@loader_path/" + name and (file.parent / name).is_file():
                continue  # Already bundled; packaging retries do not relocate it again.
            if dependency.startswith("/"):
                original = Path(dependency).resolve(strict=True)
            elif dependency in ("@rpath/" + name, "@loader_path/" + name):
                # A copied Homebrew library may reference its sibling by rpath.
                original = (origins[file].parent / name).resolve(strict=True)
            else:
                continue
            license = original.parent.parent / "LICENSE"
            if not license.is_file():
                raise RuntimeError(f"Brotli license missing beside {original}")
            target = root / name
            if not target.exists():
                shutil.copy2(original, target)
                origins[target] = original
                run("install_name_tool", "-id", "@loader_path/" + name, target)
                pending.append(target)
            notices = root / "licenses"
            notices.mkdir(exist_ok=True)
            shutil.copy2(license, notices / "Brotli-MIT.txt")
            run("install_name_tool", "-change", dependency, "@loader_path/" + name, file)


def audit_dependencies(root, arch="arm64"):
    """Resolve Mach-O load commands inside the distribution, without SDK paths."""
    root = root.resolve()
    cpu = {"arm64": "arm64", "x64": "x86_64"}[arch]
    executable_dir = root / "Contents/MacOS" if root.suffix == ".app" else root
    binaries = [file for file in sorted(root.rglob("*")) if file.is_file() and is_mach(file)]
    rpaths = {}
    dependencies = {}
    for file in binaries:
        # Put the input first: newer Xcode lipo consumes all arguments after
        # -verify_arch as architecture names, including a trailing file path.
        run("lipo", file, "-verify_arch", cpu)
        commands = subprocess.check_output(["otool", "-arch", cpu, "-l", str(file)], text=True)
        minimums = re.findall(r"\bminos ([\d.]+)|cmd LC_VERSION_MIN_MACOSX\s+cmdsize \d+\s+version ([\d.]+)", commands)
        def numeric(value):
            return tuple((list(map(int, value.split("."))) + [0, 0, 0])[:3])
        for build_version, legacy_version in minimums:
            minimum = build_version or legacy_version
            if numeric(minimum) > numeric(MINIMUM_MACOS):
                raise RuntimeError(f"{file.name} requires macOS {minimum}, above the app minimum {MINIMUM_MACOS}")
        rpaths[file] = re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset \d+\)", commands)
        ids = subprocess.check_output(["otool", "-arch", cpu, "-D", str(file)], text=True).splitlines()[1:]
        dependencies[file] = [line.strip().split(" (", 1)[0] for line in
                              subprocess.check_output(["otool", "-arch", cpu, "-L", str(file)], text=True).splitlines()[1:]
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


def sign_tree(root, identity="-", entitlements=None, arch="arm64"):
    # Sign leaf code before its app. Compression preserves signed bytes in the payload.
    audit_dependencies(root, arch)
    options = ["--options", "runtime", "--timestamp"] if identity != "-" else ["--timestamp=none"]
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
