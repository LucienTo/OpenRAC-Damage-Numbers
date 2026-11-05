"""Makes the damage numbers' digit props: assets/digits.png and assets/digit_0.glb ... digit_9.glb.

Python's standard library only. Run from the repository's top:  python tools/make_digits.py

The texture holds the ten digits side by side, each a 5 by 7 pixel font glyph with a one-pixel
black outline round it (white inside, so a prop's tint colours the digit and leaves the outline
black), every font pixel 8 by 8 texels, transparent elsewhere (cut out by the port: alpha 0 or 255).

Each .glb is one card for one digit, in the game's axes (z up): x across from -w/2 to w/2, z up from
-0.5 to 0.5, y 0, both faces (the mod turns it to face the camera, mesh -y towards it). Its material
names the texture by a path beside the mesh, "digits.png", which the port reads from the same
folder.
"""
import json
import struct
import zlib
from pathlib import Path

FONT = {
    "0": ["01110", "10001", "10011", "10101", "11001", "10001", "01110"],
    "1": ["00100", "01100", "00100", "00100", "00100", "00100", "01110"],
    "2": ["01110", "10001", "00001", "00010", "00100", "01000", "11111"],
    "3": ["11111", "00010", "00100", "00010", "00001", "10001", "01110"],
    "4": ["00010", "00110", "01010", "10010", "11111", "00010", "00010"],
    "5": ["11111", "10000", "11110", "00001", "00001", "10001", "01110"],
    "6": ["00110", "01000", "10000", "11110", "10001", "10001", "01110"],
    "7": ["11111", "00001", "00010", "00100", "01000", "01000", "01000"],
    "8": ["01110", "10001", "10001", "01110", "10001", "10001", "01110"],
    "9": ["01110", "10001", "10001", "01111", "00001", "00010", "01100"],
}
GLYPH_W, GLYPH_H = 5, 7
PAD = 1                      # font pixels of outline round the glyph
CELL_W, CELL_H = GLYPH_W + 2 * PAD, GLYPH_H + 2 * PAD   # 7 by 9 font pixels
TEXELS = 8                   # texels a font pixel
SLOT_W, SLOT_H = 64, 80      # each digit's place in the texture, in texels (the cell is 56 by 72)
ATLAS_W, ATLAS_H = SLOT_W * 10, SLOT_H

OUT = Path(__file__).resolve().parent.parent / "assets"


def cell(digit: str) -> list[list[int]]:
    """The cell's font pixels: 2 the digit, 1 its outline, 0 nothing."""
    grid = [[0] * CELL_W for _ in range(CELL_H)]
    rows = FONT[digit]
    for y in range(GLYPH_H):
        for x in range(GLYPH_W):
            if rows[y][x] == "1":
                grid[y + PAD][x + PAD] = 2
    for y in range(CELL_H):
        for x in range(CELL_W):
            if grid[y][x] != 0:
                continue
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    yy, xx = y + dy, x + dx
                    if 0 <= yy < CELL_H and 0 <= xx < CELL_W and grid[yy][xx] == 2:
                        grid[y][x] = 1
    return grid


def png(width: int, height: int, rgba: bytearray) -> bytes:
    def chunk(kind: bytes, data: bytes) -> bytes:
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw += rgba[y * width * 4:(y + 1) * width * 4]
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
            + chunk(b"IEND", b""))


def atlas() -> bytes:
    pixels = bytearray(ATLAS_W * ATLAS_H * 4)
    for i in range(10):
        grid = cell(str(i))
        for cy in range(CELL_H):
            for cx in range(CELL_W):
                v = grid[cy][cx]
                if v == 0:
                    continue
                colour = (255, 255, 255, 255) if v == 2 else (0, 0, 0, 255)
                for ty in range(TEXELS):
                    for tx in range(TEXELS):
                        x = i * SLOT_W + cx * TEXELS + tx
                        y = cy * TEXELS + ty
                        o = (y * ATLAS_W + x) * 4
                        pixels[o:o + 4] = bytes(colour)
    return png(ATLAS_W, ATLAS_H, pixels)


def glb(digit: int) -> bytes:
    w = CELL_W / CELL_H                      # the card's width for a height of 1
    u0 = digit * SLOT_W / ATLAS_W
    u1 = (digit * SLOT_W + CELL_W * TEXELS) / ATLAS_W
    v0, v1 = 0.0, CELL_H * TEXELS / ATLAS_H  # glTF's v runs down the picture
    corners = [(-w / 2, 0.5, u0, v0), (w / 2, 0.5, u1, v0), (w / 2, -0.5, u1, v1), (-w / 2, -0.5, u0, v1)]
    positions, normals, uvs = [], [], []
    for normal_y in (-1.0, 1.0):            # the face towards the camera (-y), then the back
        for x, z, u, v in corners:
            positions += [x, 0.0, z]
            normals += [0.0, normal_y, 0.0]
            uvs += [u, v]
    indices = [0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7]

    blob = bytearray()
    views = []

    def add(data: bytes, target: int) -> int:
        while len(blob) % 4:
            blob.append(0)
        views.append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(data), "target": target})
        blob.extend(data)
        return len(views) - 1

    pos_view = add(struct.pack(f"<{len(positions)}f", *positions), 34962)
    nrm_view = add(struct.pack(f"<{len(normals)}f", *normals), 34962)
    uv_view = add(struct.pack(f"<{len(uvs)}f", *uvs), 34962)
    idx_view = add(struct.pack(f"<{len(indices)}H", *indices), 34963)
    while len(blob) % 4:
        blob.append(0)

    doc = {
        "asset": {"version": "2.0", "generator": "damage-numbers make_digits.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": f"digit_{digit}"}],
        "meshes": [{"primitives": [{
            "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
            "indices": 3, "material": 0}]}],
        "materials": [{
            "name": "digits",
            "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 0.0},
            "alphaMode": "MASK", "alphaCutoff": 0.5, "doubleSided": True}],
        "textures": [{"source": 0, "sampler": 0}],
        "samplers": [{"magFilter": 9728, "minFilter": 9729, "wrapS": 33071, "wrapT": 33071}],
        "images": [{"uri": "digits.png"}],
        "buffers": [{"byteLength": len(blob)}],
        "bufferViews": views,
        "accessors": [
            {"bufferView": pos_view, "componentType": 5126, "count": 8, "type": "VEC3",
             "min": [-w / 2, 0.0, -0.5], "max": [w / 2, 0.0, 0.5]},
            {"bufferView": nrm_view, "componentType": 5126, "count": 8, "type": "VEC3"},
            {"bufferView": uv_view, "componentType": 5126, "count": 8, "type": "VEC2"},
            {"bufferView": idx_view, "componentType": 5123, "count": len(indices), "type": "SCALAR"},
        ],
    }
    text = json.dumps(doc, separators=(",", ":")).encode()
    while len(text) % 4:
        text += b" "
    body = (struct.pack("<II", len(text), 0x4E4F534A) + text
            + struct.pack("<II", len(blob), 0x004E4942) + bytes(blob))
    return struct.pack("<III", 0x46546C67, 2, 12 + len(body)) + body


def main() -> None:
    OUT.mkdir(exist_ok=True)
    (OUT / "digits.png").write_bytes(atlas())
    for d in range(10):
        (OUT / f"digit_{d}.glb").write_bytes(glb(d))
    print(f"wrote {OUT / 'digits.png'} and 10 digit meshes")


if __name__ == "__main__":
    main()
