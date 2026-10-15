"""Locate each compressed file inside a Windows/Linux setup for signed range updates.

Windows stores payload resource 100 under RT_RCDATA; Linux appends it before a PTPAYLD1 footer.
Offsets are absolute package offsets, excluding the optional signed-manifest trailer.
"""
from __future__ import annotations

import argparse
import json
import mmap
from pathlib import Path, PurePosixPath
import struct

import signed_trailer


def payload_index(path: Path) -> list[dict]:
    body = signed_trailer.body_size(path)
    with path.open("rb") as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
        def read(fmt: str, offset: int):
            if offset < 0 or offset + struct.calcsize(fmt) > body:
                raise ValueError(f"{path}: truncated setup structure")
            return struct.unpack_from(fmt, data, offset)

        if body >= 24 and data[body - 24:body - 16] == b"PTPAYLD1":
            start, length = read("<QQ", body - 16)
            if start + length + 24 != body:
                raise ValueError(f"{path}: invalid Linux payload footer")
        elif data[:2] == b"MZ":
            pe, = read("<I", 0x3C)
            if data[pe:pe + 4] != b"PE\0\0":
                raise ValueError(f"{path}: missing PE header")
            sections, = read("<H", pe + 6)
            optional_size, = read("<H", pe + 20)
            optional = pe + 24
            magic, = read("<H", optional)
            directories = {0x10B: 96, 0x20B: 112}.get(magic)
            if directories is None or optional_size < directories + 24:
                raise ValueError(f"{path}: invalid PE optional header")
            table = optional + optional_size

            def file_offset(rva: int) -> int:
                for i in range(sections):
                    virtual_size, virtual, raw_size, raw = read("<IIII", table + i * 40 + 8)
                    if virtual <= rva < virtual + max(virtual_size, raw_size) and rva - virtual < raw_size:
                        return raw + rva - virtual
                raise ValueError(f"{path}: PE resource RVA outside file sections")

            resource_rva, resource_size = read("<II", optional + directories + 16)
            resource = file_offset(resource_rva)

            def entry(directory: int, wanted: int | None) -> int:
                if directory + 16 > resource_size:
                    raise ValueError(f"{path}: invalid resource directory")
                named, ids = read("<HH", resource + directory + 12)
                for i in range(named + ids):
                    at = directory + 16 + i * 8
                    if at + 8 > resource_size:
                        raise ValueError(f"{path}: truncated resource entries")
                    name, target = read("<II", resource + at)
                    if wanted is None or name == wanted:
                        if (target & 0x7FFFFFFF) + 16 > resource_size:
                            raise ValueError(f"{path}: resource entry outside directory")
                        return target
                raise ValueError(f"{path}: installer payload resource is missing")

            directory = entry(0, 10)
            if not directory & 0x80000000:
                raise ValueError(f"{path}: RT_RCDATA is not a directory")
            directory = entry(directory & 0x7FFFFFFF, 100)
            if not directory & 0x80000000:
                raise ValueError(f"{path}: payload resource has no language directory")
            leaf = entry(directory & 0x7FFFFFFF, None)
            if leaf & 0x80000000:
                raise ValueError(f"{path}: unexpected resource directory depth")
            rva, length = read("<II", resource + leaf)
            start = file_offset(rva)
        elif data[:8] == b"PTSETUP1":
            start, length = 0, body
        else:
            raise ValueError(f"{path}: no supported setup payload")

        end = start + length
        if length < 12 or end > body or data[start:start + 8] != b"PTSETUP1":
            raise ValueError(f"{path}: invalid installer payload")
        count, = read("<I", start + 8)
        if count > length // 90:
            raise ValueError(f"{path}: impossible payload file count")
        cursor = start + 12
        entries, seen = [], set()
        for _ in range(count):
            if cursor + 2 > end:
                raise ValueError(f"{path}: truncated payload entry")
            name_size, = read("<H", cursor)
            cursor += 2
            if not name_size or cursor + name_size + 80 > end:
                raise ValueError(f"{path}: truncated payload filename")
            name = data[cursor:cursor + name_size].decode("utf-8")
            cursor += name_size
            if (PurePosixPath(name).is_absolute() or "\\" in name or ":" in name or
                    any(part in ("", ".", "..") for part in name.split("/")) or name.casefold() in seen):
                raise ValueError(f"{path}: unsafe or duplicate payload path {name!r}")
            seen.add(name.casefold())
            size, packed = read("<QQ", cursor)
            digest = data[cursor + 16:cursor + 80].decode("ascii")
            cursor += 80
            if not packed or cursor + packed > end or len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
                raise ValueError(f"{path}: invalid payload entry {name!r}")
            entries.append({"path": name, "size": size, "sha256": digest, "offset": cursor, "packed": packed})
            cursor += packed
        if cursor != end:
            raise ValueError(f"{path}: unexpected bytes after payload entries")
        return entries


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("setup", type=Path)
    args = parser.parse_args()
    print(json.dumps(payload_index(args.setup), indent=2))
