"""Make a native PKG/folder installer from the canonical Mac portable ZIP."""
import argparse
import hashlib
import plistlib
import shutil
import sys
from pathlib import Path

from bundle import ICON_FILE, REPO, bundle_extractor_brotli, copy_icon, is_mach, run, sign_tree
from make_app import APP_NAME, BUNDLE_ID, MINIMUM_MACOS, architecture, version

SETUP_NAME = APP_NAME + " Setup"


def setup(build, runtime, extractor, source, out, identity="-"):
    arch = architecture(build / "pt_setup_macos")
    app = out / (SETUP_NAME + ".app")
    archive = out / f"pt-port-{version(build)}-setup-macos-{arch}.zip"
    if app.exists() or archive.exists() or (out / "payload").exists():
        raise RuntimeError("Setup output already exists; choose a new --out directory")
    if not (extractor / "PT.PkgExtract").is_file() or not is_mach(extractor / "PT.PkgExtract"):
        raise RuntimeError(f"Publish the self-contained osx-{arch} extractor first")
    bundle_extractor_brotli(extractor, arch)
    sign_tree(extractor, identity, arch=arch)
    payload = out / "payload"
    run(sys.executable, REPO / "tools/prepare_native_installer.py", "--runtime", runtime,
        "--extractor", extractor, "--source", source, "--out", payload, "--runtime-id", "osx-" + arch)
    contents = app / "Contents"
    (contents / "MacOS").mkdir(parents=True)
    resources = contents / "Resources"
    resources.mkdir()
    copy_icon(resources)
    shutil.copy2(build / "pt_setup_macos", contents / "MacOS/pt_setup_macos")
    shutil.copy2(payload / "payload.bin", resources / "payload.bin")
    with (resources / "payload.bin").open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    (resources / "payload.sha256").write_text(digest + "\n", encoding="ascii")
    (contents / "Info.plist").write_bytes(plistlib.dumps({
        "CFBundleDevelopmentRegion": "en", "CFBundleExecutable": "pt_setup_macos",
        "CFBundleIconFile": ICON_FILE,
        "CFBundleIdentifier": BUNDLE_ID + ".setup", "CFBundleInfoDictionaryVersion": "6.0",
        "CFBundleName": SETUP_NAME, "CFBundleDisplayName": SETUP_NAME,
        "CFBundlePackageType": "APPL", "CFBundleShortVersionString": version(build).split("-")[0],
        "CFBundleVersion": version(build), "LSMinimumSystemVersion": MINIMUM_MACOS,
        "LSArchitecturePriority": ["arm64" if arch == "arm64" else "x86_64"],
        "NSHighResolutionCapable": True,
    }))
    (contents / "PkgInfo").write_bytes(b"APPL????")
    sign_tree(app, identity, arch=arch)
    run("ditto", "-c", "-k", "--keepParent", app, archive)
    print(f"{app}\n{archive}")
    return app


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("build", "runtime", "extractor", "source", "out"):
        p.add_argument("--" + name, type=Path, required=True)
    p.add_argument("--identity", default="-")
    a = p.parse_args()
    setup(a.build.resolve(), a.runtime.resolve(), a.extractor.resolve(), a.source.resolve(), a.out.resolve(), a.identity)


if __name__ == "__main__":
    main()
