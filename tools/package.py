import argparse
import datetime
import hashlib
import json
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
# voice/ as the recognizer loads it (formats/voice.md): whisper.cpp and ggml with its CPU variants, the two models and
# their notices; the build folder also holds the libraries' debug symbols, which stay out
VOICE_MODELS = ("ggml-base.en-q5_1.bin", "ggml-small.en-q5_1.bin", "ggml-silero-v6.2.0.bin")
VOICE_LIBRARIES = ("whisper", "ggml", "ggml-base", "ggml-cpu-x64")
UPSCALER_RELEASE_FILES = {
    "FSR": "amd_fidelityfx_vk.dll",
    "DLSS": "nvngx_dlss.dll",
    "XeSS": "libxess.dll",
    # Intel XeSS Frame Generation (XeSS SDK 3.0.2, DirectX 12 through the D3D12 bridge) and the XeLL library it requires
    "XeSSFG": "libxess_fg.dll",
}
# the other runtime files a backend needs beside its main DLL
UPSCALER_COMPANION_FILES = {
    "XeSSFG": ("libxell.dll",),
}


def backend_files(name):
    return (UPSCALER_RELEASE_FILES[name], *UPSCALER_COMPANION_FILES.get(name, ()))
# DirectX 12 (docs/rhi.md): an executable that exports D3D12SDKVersion (the Agility SDK's, agility_exports.h) loads the D3D12
# runtime from D3D12/ next to it and the DXIL shaders from shaders/
D3D12_EXPORT_MARK = b"D3D12SDKVersion"
# and AMD's DX12 DLLs (cmake/Upscalers.cmake): the FSR SDK 2.3.0 loader and upscaler (FSR 3.1 and FSR 4 on DirectX 12) and v1.1.4's
# amd_fidelityfx_dx12.dll (FSR frame generation on DirectX 12)
D3D12_RUNTIME_FILES = ("D3D12/D3D12Core.dll", "amd_fidelityfx_loader_dx12.dll", "amd_fidelityfx_upscaler_dx12.dll",
                       "amd_fidelityfx_dx12.dll")


def d3d12_capable(executable_bytes):
    return D3D12_EXPORT_MARK in executable_bytes


# the Linux build's upscaler runtimes (cmake/UpscalersLinux.cmake) next to pt: the FSR library built from AMD's FidelityFX
# sources, and the DLSS SDK's Super Resolution and Ray Reconstruction runtimes (NGX picks the newest version present).
# Their licences are in licenses/ with the others.
LINUX_UPSCALER_FILES = {
    "FSR": ("libamd_fidelityfx_vk.so",),
    "DLSS": ("libnvidia-ngx-dlss.so.*", "libnvidia-ngx-dlssd.so.*"),
}


def linux_upscaler_files(build):
    """The upscaler runtimes a Linux build configured with (upscalers.txt), or exit naming the missing ones."""
    build = Path(build)
    marker = build / "upscalers.txt"
    configured = {}
    if marker.is_file():
        for line in marker.read_text(encoding="utf-8").splitlines():
            if "=" in line:
                key, value = line.split("=", 1)
                configured[key] = value
    files, missing = [], []
    for backend, patterns in LINUX_UPSCALER_FILES.items():
        if configured.get(backend) != "1":
            print(f"warning: the Linux build has no {backend} (upscalers.txt); configure with -DPT_UPSCALERS=ON")
            continue
        for pattern in patterns:
            found = sorted(p for p in build.glob(pattern) if p.is_file() and p.stat().st_size > 0)
            if found:
                files.append(found[-1])
            else:
                missing.append(pattern)
    if missing:
        sys.exit(f"the Linux build lacks upscaler runtime files: {', '.join(missing)}")
    return files


STREAMLINE_RELEASE_FILES = (
    "sl.interposer.dll", "sl.common.dll", "sl.dlss.dll", "sl.dlss_g.dll", "sl.dlss_d.dll",
    "sl.reflex.dll", "sl.pcl.dll", "nvngx_dlssg.dll",
)


def _validate_manifest(executable_bytes, manifest_bytes):
    try:
        manifest = json.loads(manifest_bytes)
        backends = manifest["backends"]
        digest = manifest["sha256"]
    except (ValueError, KeyError, TypeError) as error:
        raise ValueError("Windows release build provenance manifest is invalid") from error
    if (not isinstance(backends, dict) or not isinstance(digest, str)
            or any(type(backends.get(name)) is not bool for name in (*UPSCALER_RELEASE_FILES, "Streamline"))):
        raise ValueError("Windows release build provenance manifest is invalid")
    if hashlib.sha256(executable_bytes).hexdigest() != digest:
        raise ValueError("Windows release executable does not match its build provenance manifest")
    missing = [name for name in UPSCALER_RELEASE_FILES if backends.get(name) is not True]
    if missing:
        raise ValueError("Windows release build provenance lacks required backends: " + ", ".join(missing))
    return backends


def validate_upscaler_release(build, executable):
    """Require all Windows release backends to be compiled and shipped."""
    build = Path(build)
    marker = build / "upscalers.txt"
    if not marker.is_file():
        raise ValueError(f"{marker} missing; configure a strict Windows release build")
    configured = {}
    for line in marker.read_text(encoding="utf-8").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            configured[key] = value
    exe_bytes = Path(executable).read_bytes()
    provenance = Path(f"{executable}.upscalers.json")
    if not provenance.is_file():
        raise ValueError(f"Windows release build provenance missing: {provenance}")
    expected_keys = (*UPSCALER_RELEASE_FILES, "Streamline")
    missing = [name for name in expected_keys if configured.get(name) not in ("0", "1")]
    missing.extend(name for name in UPSCALER_RELEASE_FILES if configured.get(name) != "1")
    if missing:
        raise ValueError("Windows release upscaler configuration is incomplete or disabled: " + ", ".join(dict.fromkeys(missing)))
    manifest_backends = _validate_manifest(exe_bytes, provenance.read_bytes())
    mismatch = [name for name in expected_keys if manifest_backends[name] != (configured[name] == "1")]
    if mismatch:
        raise ValueError("Windows release executable provenance disagrees with upscalers.txt: " + ", ".join(mismatch))
    missing.extend(filename for name in UPSCALER_RELEASE_FILES if configured.get(name) == "1" for filename in backend_files(name)
                   if not (build / filename).is_file() or (build / filename).stat().st_size == 0)
    if configured["Streamline"] == "1":
        missing.extend(filename for filename in STREAMLINE_RELEASE_FILES
                       if not (build / filename).is_file() or (build / filename).stat().st_size == 0)
    if missing:
        raise ValueError("Windows release is missing required upscaler backend/runtime files: " + ", ".join(missing))


def validate_upscaler_archive(archive):
    """Reject Windows installer payload archives without a backend record bound to pt.exe."""
    with zipfile.ZipFile(archive) as package:
        files = {}
        for entry in package.infolist():
            parts = Path(entry.filename).parts
            if len(parts) > 1 and parts[1].lower() == "data":
                raise ValueError("Windows runtime ZIP contains the reserved data directory")
            if not entry.is_dir() and len(parts) > 1:
                files[Path(*parts[1:]).as_posix()] = package.read(entry)
    exe = files.get("pt.exe")
    manifest = files.get("upscalers-build.json")
    if exe is None or manifest is None:
        raise ValueError("Windows runtime ZIP lacks pt.exe or its build provenance manifest")
    backends = _validate_manifest(exe, manifest)
    missing = [filename for name in UPSCALER_RELEASE_FILES for filename in backend_files(name) if not files.get(filename)]
    if d3d12_capable(exe):
        missing.extend(filename for filename in D3D12_RUNTIME_FILES if not files.get(filename))
    if backends["Streamline"]:
        missing.extend(filename for filename in STREAMLINE_RELEASE_FILES if not files.get(filename))
    if missing:
        raise ValueError("Windows runtime ZIP lacks required upscaler runtime files: " + ", ".join(missing))


# The package's own file list (docs/installer.md, "Updating an install"): every file it ships with its size and SHA-256, as
# pt-files.json at the package root (not listing itself). tools/prepare_native_installer.py writes it again for the payload, which
# adds the extraction helper; the installed copy tells the next update exactly which files this version shipped.
FILE_LIST = "pt-files.json"


def file_list(files):
    """pt-files.json for {path: bytes or Path}; the list itself is left out"""
    entries = []
    for name in sorted(files):
        if name == FILE_LIST:
            continue
        data = files[name]
        if isinstance(data, Path):
            digest, size = hashlib.sha256(), 0
            with data.open("rb") as f:
                for block in iter(lambda: f.read(1 << 20), b""):
                    digest.update(block)
                    size += len(block)
            entries.append({"path": name, "size": size, "sha256": digest.hexdigest()})
        else:
            entries.append({"path": name, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()})
    return (json.dumps({"format": 1, "files": entries}, indent=1, ensure_ascii=False) + "\n").encode("utf-8")


# what the DLSS 5 unlock (src/engine/render/upscale/neural_unlock.h) puts next to the game: the player's files, never package
# files. A build folder the unlock was tried in holds them, and the *.dll copy below must not pick them up.
UNLOCK_FILES = {"dxgi.dll", "nvngx_dlssnr.dll", "renodx-dlss5.addon64", "reshade.ini", "dlss5-autopilot.json"}


def unlock_files(folder):
    """the unlock's files in a folder: the fixed names and whatever its dlss5-autopilot.json lists"""
    names = set(UNLOCK_FILES)
    record = Path(folder) / "dlss5-autopilot.json"
    if record.is_file():
        try:
            listed = json.loads(record.read_text(encoding="utf-8")).get("files", [])
            names |= {str(name).replace("\\", "/").lower() for name in listed if isinstance(name, str)}
        except (ValueError, AttributeError):
            pass
    return names


def copy_voice(source, target):
    # Windows DLLs, or the shared objects of a Linux build (docs/linux.md)
    prefix, extension = ("", ".dll") if (source / "whisper.dll").is_file() else ("lib", ".so")
    libraries = tuple(f"{prefix}{n}{extension}" for n in VOICE_LIBRARIES)
    missing = [n for n in VOICE_MODELS + libraries if not (source / n).is_file()] + ([] if (source / "licenses").is_dir() else ["licenses"])
    if missing:
        sys.exit(f"{source} lacks {', '.join(missing)}; build first with tools\\build_pt.bat release")
    target.mkdir(parents=True)
    for name in VOICE_MODELS:
        shutil.copy2(source / name, target / name)
    for library in sorted(source.glob(f"{prefix}*{extension}")):
        shutil.copy2(library, target / library.name)
    shutil.copytree(source / "licenses", target / "licenses")


def version():
    try:
        rev = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=REPO, capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        rev = "local"
    return f"{datetime.date.today():%Y%m%d}-{rev}"


def main():
    parser = argparse.ArgumentParser(description="Copy pt.exe with its shaders, voice model and upscaler DLLs into a portable folder and zip it")
    parser.add_argument("--build", default=str(REPO / "build" / "release"))
    parser.add_argument("--out", default=str(REPO / "dist"))
    parser.add_argument("--exe", help="Explicit executable: pt_release.exe for the release 1.0 game (loop browser locked until the game "
                        "is finished, PT_RELEASE_LOCKS); shipped as pt.exe")
    parser.add_argument("--game", default=str(REPO / "game" / "CUSA01127"),
                        help="the game files, for the loop browser previews (shot by the packaged exe, --make-loop-previews)")
    args = parser.parse_args()
    build = Path(args.build)
    exe = Path(args.exe) if args.exe else build / "pt.exe"
    if not exe.exists():
        sys.exit(f"{exe} not found; build first with tools\\build_pt.bat release")
    if exe.suffix.lower() == ".exe":
        try:
            validate_upscaler_release(build, exe)
        except ValueError as error:
            sys.exit(str(error))
    # a Linux build (build/linux-cross, docs/linux.md) has no .exe and no DLLs
    exe_name = "pt.exe" if exe.suffix.lower() == ".exe" else "pt"
    name = f"pt-port-{version()}"
    root = Path(args.out) / name
    archive = Path(args.out) / f"{name}.zip"
    if root.exists() or archive.exists():
        sys.exit(f"Package output already exists; choose another --out folder: {root}")
    (root / "shaders").mkdir(parents=True)
    shutil.copy2(exe, root / exe_name)
    if exe.suffix.lower() == ".exe":
        provenance = Path(f"{exe}.upscalers.json")
        if provenance.is_file():
            shutil.copy2(provenance, root / "upscalers-build.json")
    for spv in sorted((build / "shaders").glob("*.spv")):
        shutil.copy2(spv, root / "shaders" / spv.name)
    # DirectX 12: the DXIL shaders with their reflection, and the Agility SDK runtime (the debug layer stays out)
    if exe.suffix.lower() == ".exe" and d3d12_capable(exe.read_bytes()):
        dxil = sorted((build / "shaders").glob("*.dxil"))
        if not dxil:
            sys.exit(f"{exe.name} runs on DirectX 12 but {build / 'shaders'} has no DXIL shaders")
        for path in dxil + sorted((build / "shaders").glob("*.dxil.json")):
            shutil.copy2(path, root / "shaders" / path.name)
        for runtime in D3D12_RUNTIME_FILES:
            source = build / runtime
            if not source.is_file() or source.stat().st_size == 0:
                sys.exit(f"{exe.name} runs on DirectX 12 but {source} is missing")
            (root / runtime).parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, root / runtime)
        print(f"DirectX 12: {len(dxil)} DXIL shaders, {', '.join(D3D12_RUNTIME_FILES)}")
    copy_voice(build / "voice", root / "voice")
    shutil.copytree(build / "fonts", root / "fonts")
    # Coffee with Lisa's script, text, art and music (src/game/lisa_date), swappable files the game reads at run time
    if (build / "lisa_date").is_dir():
        shutil.copytree(build / "lisa_date", root / "lisa_date")
    # the loop browser's previews are shot by the packaged game itself from each entry's real start (pt.exe --make-loop-previews,
    # docs/gameplay.md loop browser); without the game files the game shoots them on the player's PC at the browser's first use
    if exe_name != "pt.exe":
        # a Linux build is cross built and cannot run on the machine that packages it; tools/ci/release.py copies the previews the
        # Windows package shot into the Linux folder (without them the game shoots them at the browser's first use)
        print(f"loop browser previews skipped: {exe_name} is not a Windows build and cannot run here")
    elif Path(args.game).is_dir():
        previews = root / "loop-previews"
        subprocess.run([str(root / "pt.exe"), "--make-loop-previews", str(previews), "--game", args.game], cwd=root, check=True,
                       timeout=1800)
        shots = sorted(previews.glob("loop-*.png"))
        print(f"loop browser previews: {len(shots)}")
        # the game shows them only with all 18 and the capture's version.txt; a capture that dropped one fails the package
        if len(shots) != 18 or not (previews / "version.txt").is_file():
            sys.exit(f"loop browser previews incomplete: {len(shots)} of 18, see {previews / 'capture.log'}")
        for extra in ("capture.txt", "capture.log", "preview.ini"):
            (previews / extra).unlink(missing_ok=True)
        # the capture run's own Vulkan pipeline cache (this machine's GPU and driver) is no part of the package
        shutil.rmtree(previews / "pipeline-cache", ignore_errors=True)
    else:
        print(f"loop browser previews skipped: no game files at {args.game}")
    if (build / "texture-tools").is_dir():
        shutil.copytree(build / "texture-tools", root / "texture-tools")
    # the port's own ReShade for the DLSS 5 community route (tools/build_reshade.py), which pt-updater installs over Autopilot's
    if (build / "reshade" / "dxgi.dll").is_file():
        (root / "reshade").mkdir()
        shutil.copy2(build / "reshade" / "dxgi.dll", root / "reshade" / "dxgi.dll")
    skipped = unlock_files(build)
    for dll in sorted(build.glob("*.dll")):
        if dll.name.lower() in skipped:
            print(f"left out {dll.name}: a DLSS 5 unlock file, not part of the package")
            continue
        shutil.copy2(dll, root / dll.name)
    if exe_name == "pt":
        for library in linux_upscaler_files(build):
            shutil.copy2(library, root / library.name)
            print(f"Linux upscaler runtime: {library.name}")
    # the in-game update's updater (docs/updates.md): the setup's program without a payload, pt-updater(.exe) next to the game
    updater = build / ("pt-updater.exe" if exe_name == "pt.exe" else "pt_setup_linux")
    if updater.is_file():
        shutil.copy2(updater, root / ("pt-updater.exe" if exe_name == "pt.exe" else "pt-updater"))
    else:
        print(f"warning: no updater at {updater}; this package cannot update itself in game")
    if (build / "licenses").is_dir():
        shutil.copytree(build / "licenses", root / "licenses")
    shutil.copy2(REPO / "README.md", root / "README.md")
    (root / FILE_LIST).write_bytes(file_list({p.relative_to(root).as_posix(): p for p in root.rglob("*") if p.is_file()}))
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
        for path in sorted(root.rglob("*")):
            if path.is_file():
                z.write(path, Path(name) / path.relative_to(root))
    size = archive.stat().st_size / (1024 * 1024)
    print(f"{root}\n{archive} ({size:.1f} MB)")


if __name__ == "__main__":
    main()
