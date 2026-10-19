"""The release signing key and the signed update manifest (docs/updates.md, "Signed update manifest").

    python tools/ci/release_signing.py embed --version 1.0.4 --package windows=<setup.exe> --files windows=<payload-manifest.json> ... [--keys <folder>]
    python tools/ci/release_signing.py verify <setup> [--public <hex>]
    python tools/ci/release_signing.py manifest --version 1.0.4 --out <dir> --package windows=<setup.exe> --files windows=<payload-manifest.json> ...

keygen makes the Ed25519 key pair once: the private seed stays in --keys (default %USERPROFILE%\\pt-release-keys, outside every
repository; it is never printed), the public key goes into src/engine/platform/release_key.h, which the game and the setups
embed. An existing key is never replaced (--force is refused while the old key is still there: delete it yourself).

embed makes the update manifest (the version, each platform's package: asset name, and the size and SHA-256 of the setup before
its trailer; the files its payload installs: path, size, SHA-256, and where its zlib data lies in the package: offset and packed
size), signs it with the private seed and appends it with the signature to each setup (tools/ci/signed_trailer.py); the release
carries no separate manifest file. manifest writes the same manifest to --out for a look; verify checks a setup's embedded one.
Ed25519 here is plain Python (RFC 8032); the C++ side is src/engine/platform/ed25519.cpp. Both are checked against RFC 8032
test vector 1 by `python tools/ci/release_signing.py selftest` and pt_platform_test.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import secrets
import sys

REPO = pathlib.Path(__file__).resolve().parents[2]
DEFAULT_KEYS = pathlib.Path(os.environ.get("PT_RELEASE_KEYS") or pathlib.Path.home() / "pt-release-keys")
SEED_NAME = "pt-release-ed25519.seed"
PUBLIC_NAME = "pt-release-ed25519.pub"
HEADER = REPO / "src" / "engine" / "platform" / "release_key.h"
MANIFEST_NAME = "pt-update-manifest.json"
PACKAGE_NAMES = {"windows": "P.T.PC.Port.Setup.exe", "linux": "P.T.PC.Port.Setup-linux"}

# ---- Ed25519 (RFC 8032, section 5.1) ----
P = 2**255 - 19
L = 2**252 + 27742317777372353535851937790883648493
D = -121665 * pow(121666, P - 2, P) % P
SQRT_M1 = pow(2, (P - 1) // 4, P)


def _recover_x(y: int, sign: int):
    if y >= P:
        return None
    x2 = (y * y - 1) * pow(D * y * y + 1, P - 2, P) % P
    if x2 == 0:
        return None if sign else 0
    x = pow(x2, (P + 3) // 8, P)
    if (x * x - x2) % P:
        x = x * SQRT_M1 % P
    if (x * x - x2) % P:
        return None
    if (x & 1) != sign:
        x = P - x
    return x


_GY = 4 * pow(5, P - 2, P) % P
_GX = _recover_x(_GY, 0)
BASE = (_GX, _GY, 1, _GX * _GY % P)


def _add(p, q):
    a = (p[1] - p[0]) * (q[1] - q[0]) % P
    b = (p[1] + p[0]) * (q[1] + q[0]) % P
    c = 2 * p[3] * q[3] * D % P
    d = 2 * p[2] * q[2] % P
    e, f, g, h = b - a, d - c, d + c, b + a
    return (e * f % P, g * h % P, f * g % P, e * h % P)


def _mul(s: int, p):
    q = (0, 1, 1, 0)
    while s:
        if s & 1:
            q = _add(q, p)
        p = _add(p, p)
        s >>= 1
    return q


def _equal(p, q) -> bool:
    return (p[0] * q[2] - q[0] * p[2]) % P == 0 and (p[1] * q[2] - q[1] * p[2]) % P == 0


def _compress(p) -> bytes:
    zinv = pow(p[2], P - 2, P)
    x, y = p[0] * zinv % P, p[1] * zinv % P
    return int.to_bytes(y | ((x & 1) << 255), 32, "little")


def _decompress(data: bytes):
    if len(data) != 32:
        return None
    y = int.from_bytes(data, "little")
    sign = y >> 255
    y &= (1 << 255) - 1
    x = _recover_x(y, sign)
    return None if x is None else (x, y, 1, x * y % P)


def _expand(seed: bytes):
    h = hashlib.sha512(seed).digest()
    a = int.from_bytes(h[:32], "little")
    a &= (1 << 254) - 8
    a |= 1 << 254
    return a, h[32:]


def public_key(seed: bytes) -> bytes:
    return _compress(_mul(_expand(seed)[0], BASE))


def sign(seed: bytes, message: bytes) -> bytes:
    a, prefix = _expand(seed)
    pub = _compress(_mul(a, BASE))
    r = int.from_bytes(hashlib.sha512(prefix + message).digest(), "little") % L
    big_r = _compress(_mul(r, BASE))
    h = int.from_bytes(hashlib.sha512(big_r + pub + message).digest(), "little") % L
    return big_r + int.to_bytes((r + h * a) % L, 32, "little")


def verify(pub: bytes, message: bytes, signature: bytes) -> bool:
    if len(signature) != 64:
        return False
    a = _decompress(pub)
    r = _decompress(signature[:32])
    s = int.from_bytes(signature[32:], "little")
    if a is None or r is None or s >= L:
        return False
    h = int.from_bytes(hashlib.sha512(signature[:32] + pub + message).digest(), "little") % L
    return _equal(_mul(s, BASE), _add(r, _mul(h, a)))


# RFC 8032, 7.1, TEST 1
RFC_SEED = bytes.fromhex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60")
RFC_PUBLIC = bytes.fromhex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a")
RFC_SIGNATURE = bytes.fromhex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b")


def selftest() -> bool:
    ok = public_key(RFC_SEED) == RFC_PUBLIC and sign(RFC_SEED, b"") == RFC_SIGNATURE and verify(RFC_PUBLIC, b"", RFC_SIGNATURE)
    ok = ok and not verify(RFC_PUBLIC, b"x", RFC_SIGNATURE) and not verify(public_key(b"\1" * 32), b"", RFC_SIGNATURE)
    return ok


# ---- keys ----
def read_seed(keys: pathlib.Path) -> bytes:
    path = keys / SEED_NAME
    if not path.is_file():
        sys.exit(f"no release signing key at {path} (run: python tools/ci/release_signing.py keygen)")
    seed = bytes.fromhex(path.read_text(encoding="ascii").strip())
    if len(seed) != 32:
        sys.exit(f"{path} is not a 32-byte Ed25519 seed")
    return seed


def header_text(pub: bytes) -> str:
    return ("#pragma once\n\n"
            "// The public half of the release signing key (tools/ci/release_signing.py keygen; docs/updates.md). The game and the\n"
            "// setups accept an update manifest only with a signature this key verifies. The private half never enters a repository.\n\n"
            "#include <string_view>\n\n"
            "namespace pt::update {\n"
            f"inline constexpr std::string_view kReleasePublicKeyHex = \"{pub.hex()}\";\n"
            "}\n")


def keygen(keys: pathlib.Path) -> None:
    if not selftest():
        sys.exit("Ed25519 self test failed")
    try:
        keys.resolve().relative_to(REPO.resolve())
        sys.exit(f"{keys} is inside the repository; keep the private key outside every repository")
    except ValueError:
        pass
    seed_path = keys / SEED_NAME
    if seed_path.exists():
        sys.exit(f"a release key already exists at {seed_path}; it is never replaced by this tool")
    keys.mkdir(parents=True, exist_ok=True)
    seed = secrets.token_bytes(32)
    pub = public_key(seed)
    # created for this user only, then written
    fd = os.open(seed_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "w", encoding="ascii") as out:
        out.write(seed.hex() + "\n")
    (keys / PUBLIC_NAME).write_text(pub.hex() + "\n", encoding="ascii")
    HEADER.write_bytes(header_text(pub).encode("ascii"))
    print(f"release key made: private seed in {seed_path} (not shown), public key {pub.hex()} written to {HEADER.relative_to(REPO)}")


def embedded_public_key() -> bytes:
    text = HEADER.read_text(encoding="ascii")
    marker = 'kReleasePublicKeyHex = "'
    start = text.index(marker) + len(marker)
    return bytes.fromhex(text[start:start + 64])


# ---- manifest ----
def sha256_file(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(4 * 1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def manifest_bytes(version: str, packages: dict[str, pathlib.Path], files: dict[str, pathlib.Path]) -> bytes:
    """packages: platform -> package file (published under its canonical asset name); files: platform -> payload-manifest.json
    of tools/prepare_native_installer.py (path -> bytes, sha256). A package's size and SHA-256 are those of its body, the setup
    before its signed trailer (tools/ci/signed_trailer.py), so the manifest never names itself."""
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
    import signed_trailer
    from payload_index import payload_index
    manifest = {"format": 1, "product": "pt-pc-port", "version": version, "packages": {}, "files": {}}
    for platform, path in sorted(packages.items()):
        manifest["packages"][platform] = {"name": PACKAGE_NAMES[platform], "size": signed_trailer.body_size(path),
                                          "sha256": signed_trailer.body_sha256(path)}
    for platform, path in sorted(files.items()):
        listing = json.loads(path.read_text(encoding="utf-8"))
        # where each file's zlib data lies in the package (tools/ci/payload_index.py): the updater fetches only the files that
        # differ, by HTTP range (docs/updates.md, "Only what changed"). The index must agree with the listing file by file.
        index = {}
        if platform in packages:
            index = {e["path"]: e for e in payload_index(packages[platform])}
            if set(index) != set(listing) or any(index[n]["size"] != e["bytes"] or index[n]["sha256"] != e["sha256"] for n, e in listing.items()):
                raise SystemExit(f"{packages[platform]}: its payload does not match {path}")
        entries = []
        for name, entry in sorted(listing.items()):
            item = {"path": name, "size": entry["bytes"], "sha256": entry["sha256"]}
            if name in index:
                item["offset"], item["packed"] = index[name]["offset"], index[name]["packed"]
            entries.append(item)
        manifest["files"][platform] = entries
    return (json.dumps(manifest, indent=1, sort_keys=False) + "\n").encode("utf-8")


def write_manifest(out: pathlib.Path, version: str, packages: dict[str, pathlib.Path], files: dict[str, pathlib.Path]) -> pathlib.Path:
    """the manifest as a file, for a look at it (it is published only inside the setups)"""
    target = out / MANIFEST_NAME
    target.write_bytes(manifest_bytes(version, packages, files))
    return target


def release_seed(keys: pathlib.Path) -> bytes:
    """the release key, only when it is the one the games embed"""
    seed = read_seed(keys)
    if public_key(seed) != embedded_public_key():
        sys.exit(f"the key in {keys} is not the one in {HEADER.relative_to(REPO)}: games would refuse this signature")
    return seed


def embed_signed(version: str, packages: dict[str, pathlib.Path], files: dict[str, pathlib.Path], seed: bytes) -> bytes:
    """the signed manifest of all packages, appended to each of them (tools/ci/signed_trailer.py); returns the manifest"""
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
    import signed_trailer
    data = manifest_bytes(version, packages, files)
    signature = sign(seed, data)
    if not verify(public_key(seed), data, signature):
        sys.exit("signature check failed")
    for path in packages.values():
        signed_trailer.embed(path, data, signature)
        read = signed_trailer.read(path)
        if not read or read[0] != data or not verify(public_key(seed), read[0], read[1]):
            sys.exit(f"{path}: the embedded manifest does not verify")
    return data


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="command", required=True)
    k = sub.add_parser("keygen")
    k.add_argument("--keys", type=pathlib.Path, default=DEFAULT_KEYS)
    m = sub.add_parser("manifest")
    m.add_argument("--version", required=True)
    m.add_argument("--out", type=pathlib.Path, required=True)
    m.add_argument("--package", action="append", default=[], help="platform=file")
    m.add_argument("--files", action="append", default=[], help="platform=payload-manifest.json")
    e = sub.add_parser("embed")
    e.add_argument("--version", required=True)
    e.add_argument("--package", action="append", default=[], help="platform=file")
    e.add_argument("--files", action="append", default=[], help="platform=payload-manifest.json")
    e.add_argument("--keys", type=pathlib.Path, default=DEFAULT_KEYS)
    v = sub.add_parser("verify")
    v.add_argument("setup", type=pathlib.Path)
    v.add_argument("--public", help="hex public key (default: the embedded one)")
    sub.add_parser("selftest")
    a = p.parse_args()
    if a.command == "keygen":
        keygen(a.keys)
    elif a.command == "manifest":
        pairs = lambda items: {x.split("=", 1)[0]: pathlib.Path(x.split("=", 1)[1]) for x in items}
        print(write_manifest(a.out, a.version, pairs(a.package), pairs(a.files)))
    elif a.command == "embed":
        pairs = lambda items: {x.split("=", 1)[0]: pathlib.Path(x.split("=", 1)[1]) for x in items}
        data = embed_signed(a.version, pairs(a.package), pairs(a.files), release_seed(a.keys))
        print(f"signed manifest ({len(data)} bytes) embedded in {', '.join(map(str, pairs(a.package).values()))}")
    elif a.command == "verify":
        sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
        import signed_trailer
        pub = bytes.fromhex(a.public) if a.public else embedded_public_key()
        read = signed_trailer.read(a.setup)
        ok = bool(read) and verify(pub, read[0], read[1])
        if ok:
            manifest = json.loads(read[0])
            ok = any(p["size"] == signed_trailer.body_size(a.setup) and p["sha256"] == signed_trailer.body_sha256(a.setup)
                     for p in manifest["packages"].values())
        print("PASS the embedded manifest verifies and names this setup" if ok else "FAIL no embedded manifest, or it does not verify")
        sys.exit(0 if ok else 1)
    elif a.command == "selftest":
        ok = selftest()
        print("PASS Ed25519 RFC 8032 test 1" if ok else "FAIL Ed25519 self test")
        sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
