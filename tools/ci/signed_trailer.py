"""The signed update manifest at the end of a setup (src/engine/platform/signed_trailer.h; docs/updates.md):

    <setup body: built and stamped>  <manifest JSON>  <Ed25519 signature, 64 bytes>  <footer, 48 bytes>

footer: b"PTSIGND1", u32 version 1, u32 0, u64 manifest offset, u64 manifest length, u64 signature offset, u64 signature length
(little endian). The manifest names the body (size and SHA-256), never the trailer; the setup's integrity stamp covers the body.
"""
from __future__ import annotations

import hashlib
import pathlib
import struct

MAGIC = b"PTSIGND1"
VERSION = 1
FOOTER = 48
SIGNATURE = 64


def parse_footer(last: bytes, file_size: int):
    """(manifest offset, manifest length, signature offset, signature length) of a footer, or None"""
    if len(last) != FOOTER or last[:8] != MAGIC:
        return None
    version, zero, mo, ml, so, sl = struct.unpack("<IIQQQQ", last[8:])
    if version != VERSION or zero or sl != SIGNATURE or not 0 < ml <= 16 << 20:
        return None
    if mo + ml != so or so + sl + FOOTER != file_size:
        return None
    return mo, ml, so, sl


def footer_of(path: pathlib.Path):
    size = path.stat().st_size
    if size < FOOTER:
        return None
    with path.open("rb") as f:
        f.seek(size - FOOTER)
        return parse_footer(f.read(FOOTER), size)


def body_size(path: pathlib.Path) -> int:
    """the setup before its trailer (the whole file without one)"""
    footer = footer_of(path)
    return footer[0] if footer else path.stat().st_size


def body_sha256(path: pathlib.Path) -> str:
    left, h = body_size(path), hashlib.sha256()
    with path.open("rb") as f:
        while left:
            block = f.read(min(left, 4 << 20))
            if not block:
                raise ValueError(f"{path} is shorter than its body")
            h.update(block)
            left -= len(block)
    return h.hexdigest()


def read(path: pathlib.Path) -> tuple[bytes, bytes] | None:
    """(manifest, signature) of a setup, or None without a trailer"""
    footer = footer_of(path)
    if not footer:
        return None
    mo, ml, so, sl = footer
    with path.open("rb") as f:
        f.seek(mo)
        manifest = f.read(ml)
        f.seek(so)
        return manifest, f.read(sl)


def embed(path: pathlib.Path, manifest: bytes, signature: bytes) -> None:
    """appends the trailer to the setup, replacing one it already has (the body is never changed)"""
    if len(signature) != SIGNATURE:
        raise ValueError("an Ed25519 signature has 64 bytes")
    body = body_size(path)
    with path.open("r+b") as f:
        f.truncate(body)
        f.seek(body)
        f.write(manifest)
        f.write(signature)
        f.write(MAGIC + struct.pack("<IIQQQQ", VERSION, 0, body, len(manifest), body + len(manifest), SIGNATURE))
    if footer_of(path) is None:
        raise ValueError(f"{path}: the trailer did not read back")
