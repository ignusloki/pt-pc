#!/usr/bin/env python3
"""Estimate texture payloads from original FTEX headers without exporting game assets."""
import argparse
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from qar import QarArchive
from pathcode import TYPE_SHIFT, extension_type

FORMATS = {0: ("BGRA8", 4, 1), 1: ("R8", 1, 1), 2: ("BC1", 8, 4),
           3: ("BC2", 16, 4), 4: ("BC3", 16, 4), 5: ("BC5", 16, 4),
           6: ("R32_FLOAT", 4, 1), 8: ("BC7", 16, 4)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    archive = QarArchive(args.game / "texture.qar")
    stats, invalid, ignored_3d = {}, 0, 0
    largest_decode = 0
    mip_caps = {512: 0, 1024: 0, 2048: 0}
    try:
        for code, (offset, length) in archive.entries.items():
            if code >> TYPE_SHIFT != extension_type("ftex"):
                continue
            header = archive.map[offset:offset + 0x40]
            if length < 0x40 or header[:4] != b"FTEX":
                invalid += 1
                continue
            fmt, width, height, depth = struct.unpack_from("<HHHH", header, 8)
            levels = header[0x10]
            flags, = struct.unpack_from("<I", header, 0x1c)
            faces = 6 if flags & 4 else 1
            if depth > 1:
                ignored_3d += 1
                continue
            if fmt not in FORMATS or not width or not height or not levels:
                invalid += 1
                continue
            name, block_bytes, block_dim = FORMATS[fmt]
            row = stats.setdefault(name, {"textures": 0, "cubemaps": 0, "mip_payload_bytes": 0,
                                         "fallback_payload_bytes": 0, "max_width": 0, "max_height": 0})
            row["textures"] += 1
            row["cubemaps"] += faces == 6
            row["max_width"] = max(row["max_width"], width)
            row["max_height"] = max(row["max_height"], height)
            largest_decode = max(largest_decode, width * height * 4)
            for level in range(levels):
                w, h = max(1, width >> level), max(1, height >> level)
                packed = ((w + block_dim - 1) // block_dim) * ((h + block_dim - 1) // block_dim) * block_bytes * faces
                # BC5 -> two-channel RG8; BC7 -> RGBA8. Keep the phone's BC1/2/3 formats.
                fallback = w * h * (2 if name == "BC5" else 4) * faces if name in ("BC5", "BC7") else packed
                row["mip_payload_bytes"] += packed
                row["fallback_payload_bytes"] += fallback
                for maximum in mip_caps:
                    if max(w, h) <= maximum:
                        mip_caps[maximum] += packed
    finally:
        archive.close()
    report = {"scope": "All valid 2D FTEX headers in this archive, not per-scene residency or total game RAM",
              "formats": stats, "invalid_or_unknown_headers": invalid, "ignored_3d_headers": ignored_3d,
              "original_mip_payload_bytes": sum(row["mip_payload_bytes"] for row in stats.values()),
              "proposed_fallback_mip_payload_bytes": sum(row["fallback_payload_bytes"] for row in stats.values()),
              "largest_single_rgba8_decode_bytes": largest_decode,
              "proposed_max_dimension_payload_bytes": mip_caps,
              "assumptions": ["Includes all declared mip levels and faces; missing streams are not validated",
                              "Dimension caps estimate a new mip-discard policy; the existing Texture detail setting only biases sampling",
                              "Excludes image alignment, render targets, geometry, CPU copies, caches and speech models",
                              "Payload estimates must not be presented as measured game GPU allocations"]}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
