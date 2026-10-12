"""Write the example mods' own glTF models (no game data): a small wooden table and a crate, each a .glb with an
embedded PNG texture. The models are built from boxes here so the examples carry nothing of the game.

    python tools/make_example_glb.py examples/mods
"""
import argparse
import json
import math
import struct
import zlib
from pathlib import Path


def png(width, height, pixel):
    rows = b"".join(b"\x00" + b"".join(bytes(pixel(x, y)) for x in range(width)) for y in range(height))

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))


def wood(x, y):
    # warm planks with a grain that waves along u
    grain = 0.5 + 0.5 * math.sin(x * 0.35 + 3.0 * math.sin(y * 0.05))
    plank = 0.85 if (y // 32) % 2 else 1.0
    r = int((96 + 50 * grain) * plank)
    g = int((60 + 30 * grain) * plank)
    b = int((32 + 14 * grain) * plank)
    return (r, g, b, 255)


def crate(x, y):
    edge = x < 10 or y < 10 or x > 117 or y > 117 or abs(x - y) < 6
    base = wood(x, y)
    return tuple(int(c * (0.55 if edge else 1.0)) for c in base[:3]) + (255,)


def box(lo, hi):
    """24 vertices (4 a face, so each face has its own normal and UVs) and 36 indices, counter-clockwise from outside."""
    (x0, y0, z0), (x1, y1, z1) = lo, hi
    faces = [
        ((0, 0, 1), [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]),
        ((0, 0, -1), [(x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0)]),
        ((1, 0, 0), [(x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1)]),
        ((-1, 0, 0), [(x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)]),
        ((0, 1, 0), [(x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0)]),
        ((0, -1, 0), [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)]),
    ]
    positions, normals, uvs, indices = [], [], [], []
    for normal, corners in faces:
        base = len(positions)
        positions += corners
        normals += [normal] * 4
        uvs += [(0, 1), (1, 1), (1, 0), (0, 0)]
        indices += [base, base + 1, base + 2, base, base + 2, base + 3]
    return positions, normals, uvs, indices


def merge(parts):
    positions, normals, uvs, indices = [], [], [], []
    for p, n, u, i in parts:
        base = len(positions)
        positions += p
        normals += n
        uvs += u
        indices += [base + k for k in i]
    return positions, normals, uvs, indices


def write_glb(path, mesh, texture, name):
    positions, normals, uvs, indices = mesh
    blob = bytearray()
    views = []

    def add(data, target=None):
        while len(blob) % 4:
            blob.append(0)
        view = {"buffer": 0, "byteOffset": len(blob), "byteLength": len(data)}
        if target:
            view["target"] = target
        blob.extend(data)
        views.append(view)
        return len(views) - 1

    pos_view = add(b"".join(struct.pack("<3f", *p) for p in positions), 34962)
    nrm_view = add(b"".join(struct.pack("<3f", *n) for n in normals), 34962)
    uv_view = add(b"".join(struct.pack("<2f", *u) for u in uvs), 34962)
    idx_view = add(b"".join(struct.pack("<H", i) for i in indices), 34963)
    img_view = add(texture)
    lo = [min(p[k] for p in positions) for k in range(3)]
    hi = [max(p[k] for p in positions) for k in range(3)]
    doc = {
        "asset": {"version": "2.0", "generator": "pt-port tools/make_example_glb.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": name, "mesh": 0}],
        "meshes": [{"name": name, "primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3, "material": 0}]}],
        "materials": [{"name": name, "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 0.0, "roughnessFactor": 0.8}}],
        "textures": [{"source": 0}],
        "images": [{"bufferView": img_view, "mimeType": "image/png"}],
        "accessors": [
            {"bufferView": pos_view, "componentType": 5126, "count": len(positions), "type": "VEC3", "min": lo, "max": hi},
            {"bufferView": nrm_view, "componentType": 5126, "count": len(normals), "type": "VEC3"},
            {"bufferView": uv_view, "componentType": 5126, "count": len(uvs), "type": "VEC2"},
            {"bufferView": idx_view, "componentType": 5123, "count": len(indices), "type": "SCALAR"},
        ],
        "bufferViews": views,
        "buffers": [{"byteLength": len(blob)}],
    }
    text = json.dumps(doc, separators=(",", ":")).encode()
    text += b" " * (-len(text) % 4)
    while len(blob) % 4:
        blob.append(0)
    out = struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(text) + 8 + len(blob))
    out += struct.pack("<II", len(text), 0x4E4F534A) + text
    out += struct.pack("<II", len(blob), 0x004E4942) + bytes(blob)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(out)
    print(path, len(out), "bytes")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("examples", type=Path, help="the examples/mods folder")
    args = parser.parse_args()
    # a table 0.9 x 0.75 x 0.6 m, its feet at y = 0 and its centre at x = z = 0
    legs = [box((sx * 0.4 - 0.03, 0.0, sz * 0.25 - 0.03), (sx * 0.4 + 0.03, 0.71, sz * 0.25 + 0.03)) for sx in (-1, 1) for sz in (-1, 1)]
    table = merge([box((-0.45, 0.71, -0.3), (0.45, 0.75, 0.3))] + legs)
    write_glb(args.examples / "added-table/Assets/mods/added_table/table.glb", table, png(128, 128, wood), "table")
    write_glb(args.examples / "replace-basket/Assets/mods/replace_basket/crate.glb", box((-0.25, 0.0, -0.25), (0.25, 0.5, 0.25)),
              png(128, 128, crate), "crate")


if __name__ == "__main__":
    main()
