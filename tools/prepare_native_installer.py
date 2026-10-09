"""Prepare an assets-free native setup payload; the replaceable extractor is LGPL."""
import argparse
import hashlib
import json
import stat
import struct
import zipfile
import zlib
from pathlib import Path, PurePosixPath

REPO = Path(__file__).resolve().parent.parent


def safe_parts(name):
    parts = name.split("/")
    if name.startswith("/") or "\\" in name or ":" in name or any(p in (".", "..") for p in parts):
        raise RuntimeError(f"Unsafe runtime path: {name}")
    return PurePosixPath(name).parts


def runtime_id(archive):
    with zipfile.ZipFile(archive) as z:
        names = z.namelist()
        if any(n.lower().endswith(".exe") for n in names):
            return "win-x64"
        for name in names:
            parts = safe_parts(name)
            if len(parts) == 4 and parts[0].endswith(".app") and parts[1:] == ("Contents", "MacOS", "pt"):
                with z.open(name) as stream:
                    header = stream.read(8)
                if header[:4] != b"\xcf\xfa\xed\xfe":
                    raise RuntimeError("The Mac game must be a thin 64-bit Mach-O executable")
                cpu = int.from_bytes(header[4:8], "little")
                if cpu in (0x0100000C, 0x01000007):
                    return "osx-arm64" if cpu == 0x0100000C else "osx-x64"
                raise RuntimeError("Unsupported Mac CPU type")
        return "linux-x64"


def prepare(runtime, extractor, source, out, rid=None):
    detected = runtime_id(runtime)
    rid = rid or detected
    if rid != detected:
        raise RuntimeError(f"Runtime architecture mismatch: requested {rid}, archive is {detected}")
    if rid == "win-x64":
        # Keep Windows' SDK fingerprint and upscaler policy validation.
        from package import validate_upscaler_archive
        validate_upscaler_archive(runtime)
    files = {}
    normalized = set()

    def add(name, data):
        key = name.casefold()
        if key in normalized:
            raise RuntimeError(f"Duplicate payload path: {name}")
        normalized.add(key)
        files[name] = data

    with zipfile.ZipFile(runtime) as z:
        for entry in z.infolist():
            parts = safe_parts(entry.filename)
            if entry.is_dir() or not parts:
                continue
            if rid.startswith("osx-") and parts[0] == "__MACOSX":
                continue  # ditto resource-fork metadata is not part of the sealed app.
            if stat.S_ISLNK(entry.external_attr >> 16):
                raise RuntimeError(f"Symlink in runtime ZIP: {entry.filename}")
            # A Mac portable archive contains the app itself at its root. Windows
            # and Linux archives have a wrapper directory that is stripped.
            relative = parts if rid.startswith("osx-") else parts[1:]
            if not relative:
                continue
            lower = {p.lower() for p in relative}
            cached_textures = any(p.lower() == "enhanced-textures" and
                                  (i == 0 or relative[i - 1].lower() != "licenses" or Path(relative[-1]).suffix.lower() not in {".txt", ".json", ".md"})
                                  for i, p in enumerate(relative))
            if cached_textures or lower.intersection({"game", "cusa01127", "cusa01114", "cusa01098"}) or Path(relative[-1]).suffix.lower() in {".pkg", ".psarc", ".qar"}:
                raise RuntimeError(f"Game assets in runtime ZIP: {entry.filename}")
            if relative[0].lower() == "data":
                raise RuntimeError("Writable data directory in runtime ZIP")
            if rid.startswith("osx-") and not relative[0].endswith(".app"):
                raise RuntimeError("The Mac portable ZIP must contain a top-level app bundle")
            add(PurePosixPath(*relative).as_posix(), z.read(entry))
    for file in extractor.rglob("*"):
        if file.is_symlink():
            raise RuntimeError(f"Symlink in extractor: {file}")
        if file.is_file() and file.suffix.lower() != ".pdb":
            add("extractor/" + file.relative_to(extractor).as_posix(), file.read_bytes())
    out.mkdir(parents=True, exist_ok=True)
    sourcezip = out / "extractor-source.zip"
    with zipfile.ZipFile(sourcezip, "w", zipfile.ZIP_DEFLATED) as z:
        for root, prefix in ((source, "LibOrbisPkg"), (REPO / "installer/Extractor", "PT.PkgExtract")):
            for file in root.rglob("*"):
                if file.is_file() and not {"bin", "obj", ".git"}.intersection(file.relative_to(root).parts):
                    z.write(file, prefix + "/" + file.relative_to(root).as_posix())
        microsoft_packs = " -p:NetCoreTargetingPackRoot=<empty folder>" if rid.startswith("osx-") else ""
        z.writestr("BUILD.txt", f"dotnet publish PT.PkgExtract/Extractor.csproj -c Release -r {rid} --self-contained true -p:LibOrbisSource=<absolute LibOrbisPkg folder>{microsoft_packs}. Library and wrapper are LGPL-3.0-or-later; the library DLL may be replaced. Changes: net10.0/modern SDK, fixed three-file root extraction, identity validation, safe output creation and unreadable PKG diagnostics; PFSC decompression uses ReadExactly and chunked reads use the current chunk length. These read fixes are included in this corresponding source. No original game assets in this archive.\n")
    add("extractor/LibOrbisPkg-corresponding-source.zip", sourcezip.read_bytes())
    add("extractor/LICENSE-LibOrbisPkg.txt", (source / "LICENSE.txt").read_bytes())
    with (out / "payload.bin").open("wb") as f:
        f.write(b"PTSETUP1" + struct.pack("<I", len(files)))
        for name, data in sorted(files.items()):
            encoded = name.encode("utf-8")
            packed = zlib.compress(data, 6)
            f.write(struct.pack("<H", len(encoded)) + encoded + struct.pack("<QQ", len(data), len(packed)) + hashlib.sha256(data).hexdigest().encode("ascii") + packed)
    (out / "payload-manifest.json").write_text(json.dumps({n: {"bytes": len(d), "sha256": hashlib.sha256(d).hexdigest()} for n, d in sorted(files.items())}, indent=2))
    print(f"{len(files)} assets-free {rid} payload files; {(out / 'payload.bin').stat().st_size / 1024**2:.1f} MiB")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("runtime", "extractor", "source"):
        p.add_argument("--" + name, type=Path, required=True)
    p.add_argument("--out", type=Path, default=REPO / "dump/installer-20261002/native")
    p.add_argument("--runtime-id", choices=("win-x64", "linux-x64", "osx-arm64", "osx-x64"))
    a = p.parse_args()
    prepare(a.runtime, a.extractor, a.source, a.out, a.runtime_id)


if __name__ == "__main__":
    main()
