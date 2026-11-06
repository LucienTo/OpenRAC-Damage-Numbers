"""Makes the damage numbers' glyph props, one set per style, into assets/<style>/.

Python's standard library only. Run from the repository's top:  python tools/make_digits.py

Each style's folder holds glyphs.png and one .glb card per glyph: the ten digits (digit_0.glb ...
digit_9.glb) and an exclamation mark (bang.glb) after a critical hit. The texture holds them side
by side in that order (the mod's kGlyphFiles follows it). Glyphs are white inside, so a prop's
tint colours them, with a black outline the tint leaves black, on transparent texels (cut out by
the port: alpha 0 or 255). The styles, all of this project's own drawing:

- pixel: a 5 by 7 pixel font, a font pixel 8 by 8 texels, a one-pixel outline.
- original: square digits with softly rounded corners (the comic's shapes, upright, drawn with a
  round pen), a dark outline, shaded from white at the top to grey at the foot (the tint
  brightest at the top), in the spirit of a game HUD.
- comic: square blocky digits drawn with a square pen, leaning a little, a thin outline all round
  and a thick one set off down and to the right, and two-tone shading (a darker lower part).

Each .glb is one card for one glyph, in the game's axes (z up): x across from -w/2 to w/2 (w the
style's width for a height of 1), z up from -0.5 to 0.5, y 0, both faces (the mod turns it to face
the camera, mesh -y towards it). Its material names the texture by a path beside the mesh,
"glyphs.png", which the port reads from the same folder.
"""
import json
import shutil
import math
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "assets"
NAMES = [f"digit_{d}" for d in range(10)] + ["bang"]
TEXTURE = "glyphs.png"

# ---- the pixel style ----------------------------------------------------------------------------

PIXEL_FONT = [
    ["01110", "10001", "10011", "10101", "11001", "10001", "01110"],
    ["00100", "01100", "00100", "00100", "00100", "00100", "01110"],
    ["01110", "10001", "00001", "00010", "00100", "01000", "11111"],
    ["11111", "00010", "00100", "00010", "00001", "10001", "01110"],
    ["00010", "00110", "01010", "10010", "11111", "00010", "00010"],
    ["11111", "10000", "11110", "00001", "00001", "10001", "01110"],
    ["00110", "01000", "10000", "11110", "10001", "10001", "01110"],
    ["11111", "00001", "00010", "00100", "01000", "01000", "01000"],
    ["01110", "10001", "10001", "01110", "10001", "10001", "01110"],
    ["01110", "10001", "10001", "01111", "00001", "00010", "01100"],
    ["00100", "00100", "00100", "00100", "00100", "00000", "00100"],
]


def pixel_cell(rows: list[str]) -> list[list[tuple]]:
    """One glyph, 56 by 72 texels: 7 by 9 font pixels (the glyph and a pixel of outline round it)."""
    pad, texels = 1, 8
    w, h = 5 + 2 * pad, 7 + 2 * pad
    grid = [[0] * w for _ in range(h)]
    for y in range(7):
        for x in range(5):
            if rows[y][x] == "1":
                grid[y + pad][x + pad] = 2
    for y in range(h):
        for x in range(w):
            if grid[y][x] == 0 and any(
                0 <= y + dy < h and 0 <= x + dx < w and grid[y + dy][x + dx] == 2
                for dy in (-1, 0, 1) for dx in (-1, 0, 1)
            ):
                grid[y][x] = 1
    out = [[(0, 0, 0, 0)] * (w * texels) for _ in range(h * texels)]
    for y in range(h * texels):
        for x in range(w * texels):
            v = grid[y // texels][x // texels]
            if v:
                out[y][x] = (255, 255, 255, 255) if v == 2 else (0, 0, 0, 255)
    return out


# ---- the drawn styles: strokes and arcs --------------------------------------------------------

# Each glyph as strokes in a box 0..1 across and 0..1 down: ("line", x0, y0, x1, y1) or
# ("arc", cx, cy, rx, ry, from, to) with angles in degrees (0 right, 90 down, -90 up), or
# ("dot", x, y). Drawn with a round pen.
STROKES = [
    [("arc", 0.5, 0.5, 0.30, 0.38, 0, 360)],
    [("line", 0.52, 0.12, 0.52, 0.88), ("line", 0.30, 0.28, 0.52, 0.12),
     ("line", 0.30, 0.88, 0.74, 0.88)],
    [("arc", 0.5, 0.33, 0.26, 0.21, 190, 380), ("line", 0.74, 0.40, 0.24, 0.88),
     ("line", 0.24, 0.88, 0.78, 0.88)],
    [("arc", 0.5, 0.31, 0.25, 0.19, 200, 450), ("arc", 0.5, 0.68, 0.27, 0.20, 270, 520)],
    [("line", 0.64, 0.12, 0.64, 0.88), ("line", 0.64, 0.12, 0.20, 0.64),
     ("line", 0.20, 0.64, 0.80, 0.64)],
    [("line", 0.76, 0.12, 0.30, 0.12), ("line", 0.30, 0.12, 0.28, 0.47),
     ("arc", 0.5, 0.65, 0.27, 0.23, -150, 150)],
    [("arc", 0.5, 0.65, 0.27, 0.23, 0, 360), ("arc", 0.64, 0.66, 0.41, 0.54, 180, 262)],
    [("line", 0.22, 0.12, 0.78, 0.12), ("line", 0.78, 0.12, 0.40, 0.88)],
    [("arc", 0.5, 0.30, 0.22, 0.18, 0, 360), ("arc", 0.5, 0.68, 0.27, 0.21, 0, 360)],
    [("arc", 0.5, 0.35, 0.27, 0.23, 0, 360), ("arc", 0.36, 0.34, 0.41, 0.54, 0, 82)],
    [("line", 0.5, 0.12, 0.5, 0.62), ("dot", 0.5, 0.86)],
]

# The comic style's square digits: ("poly", x0, y0, x1, y1, ...) is a line through the points,
# drawn with a square pen, so corners stay square.
SQUARE_STROKES = [
    [("poly", 0.24, 0.12, 0.76, 0.12, 0.76, 0.88, 0.24, 0.88, 0.24, 0.12)],
    [("poly", 0.32, 0.24, 0.52, 0.12, 0.52, 0.88), ("line", 0.28, 0.88, 0.76, 0.88)],
    [("poly", 0.24, 0.12, 0.76, 0.12, 0.76, 0.50, 0.24, 0.50, 0.24, 0.88, 0.76, 0.88)],
    [("poly", 0.24, 0.12, 0.76, 0.12, 0.76, 0.88, 0.24, 0.88), ("line", 0.36, 0.50, 0.76, 0.50)],
    [("poly", 0.24, 0.12, 0.24, 0.56, 0.78, 0.56), ("line", 0.66, 0.12, 0.66, 0.88)],
    [("poly", 0.76, 0.12, 0.24, 0.12, 0.24, 0.50, 0.76, 0.50, 0.76, 0.88, 0.24, 0.88)],
    [("poly", 0.76, 0.12, 0.24, 0.12, 0.24, 0.88, 0.76, 0.88, 0.76, 0.50, 0.24, 0.50)],
    [("poly", 0.24, 0.12, 0.76, 0.12, 0.76, 0.40, 0.52, 0.88)],
    [("poly", 0.24, 0.12, 0.76, 0.12, 0.76, 0.88, 0.24, 0.88, 0.24, 0.12),
     ("line", 0.24, 0.50, 0.76, 0.50)],
    [("poly", 0.76, 0.50, 0.24, 0.50, 0.24, 0.12, 0.76, 0.12, 0.76, 0.88, 0.24, 0.88)],
    [("line", 0.5, 0.12, 0.5, 0.62), ("dot", 0.5, 0.86)],
]


def segments(strokes: list[tuple]) -> list[tuple]:
    """The strokes as straight pieces (x0, y0, x1, y1), arcs cut into short ones."""
    out = []
    for s in strokes:
        if s[0] == "line":
            out.append(s[1:])
        elif s[0] == "poly":
            pts = list(zip(s[1::2], s[2::2]))
            out += [(*pts[i], *pts[i + 1]) for i in range(len(pts) - 1)]
        elif s[0] == "dot":
            out.append((s[1], s[2], s[1], s[2]))
        else:
            _, cx, cy, rx, ry, a0, a1 = s
            steps = max(8, int(abs(a1 - a0) / 6))
            pts = [(cx + rx * math.cos(math.radians(a0 + (a1 - a0) * i / steps)),
                    cy + ry * math.sin(math.radians(a0 + (a1 - a0) * i / steps))) for i in range(steps + 1)]
            out += [(*pts[i], *pts[i + 1]) for i in range(steps)]
    return out


def rounded(strokes: list[tuple], radius: float, aspect: float) -> list[tuple]:
    """The strokes with each "poly"'s corners rounded off, `radius` heights (the box's x is
    `aspect` heights wide): a curve from `radius` before the corner to `radius` after it, at most
    half of either side, a closed poly's start corner too."""
    out = []
    for s in strokes:
        if s[0] != "poly":
            out.append(s)
            continue
        pts = [(x * aspect, y) for x, y in zip(s[1::2], s[2::2])]
        closed = pts[0] == pts[-1]
        if closed:
            pts = pts[:-1]
        n = len(pts)
        path = []
        for i, p in enumerate(pts):
            if not closed and i in (0, n - 1):
                path.append(p)
                continue
            a, b = pts[i - 1], pts[(i + 1) % n]
            la, lb = math.dist(p, a), math.dist(p, b)
            r = min(radius, la / 2, lb / 2)
            p1 = (p[0] + (a[0] - p[0]) * r / la, p[1] + (a[1] - p[1]) * r / la)
            p2 = (p[0] + (b[0] - p[0]) * r / lb, p[1] + (b[1] - p[1]) * r / lb)
            for k in range(9):        # a quadratic curve through the corner's two ends
                t = k / 8
                path.append(((1 - t) ** 2 * p1[0] + 2 * (1 - t) * t * p[0] + t * t * p2[0],
                             (1 - t) ** 2 * p1[1] + 2 * (1 - t) * t * p[1] + t * t * p2[1]))
        if closed:
            path.append(path[0])
        flat = []
        for x, y in path:
            flat += [x / aspect, y]
        out.append(("poly", *flat))
    return out


def drawn_cell(strokes, *, width, height, aspect, pen, outline, slant, shadow, shade,
               shadow_grow=0.0, square=False, corner=0.0):
    """One glyph, `width` by `height` texels. The stroke box (0..1 by 0..1) fills the cell's
    height and `aspect` of it across; `pen` and `outline` are radii as shares of the height,
    `slant` leans the top forwards by that share of the height, `shadow` (dx, dy) an outline-black
    copy behind, `shadow_grow` wider than the outline, `shade(y)` the fill's brightness 0..255
    from 0 at the top to 1 at the foot; `square` a square pen instead of a round one; `corner`
    rounds the strokes' corners off by that many heights."""
    if corner:
        strokes = rounded(strokes, corner, aspect)
    # Drawn a little smaller than the cell, so the pen, the outline and the shadow fit in it.
    room = pen + outline + (max(shadow) + shadow_grow if shadow else 0.0)
    fit = 1.0 - 2.0 * max(0.0, room - 0.1)
    segs = [(0.5 + (x0 - 0.5) * fit, 0.5 + (y0 - 0.5) * fit, 0.5 + (x1 - 0.5) * fit, 0.5 + (y1 - 0.5) * fit)
            for x0, y0, x1, y1 in segments(strokes)]
    left = (width / height - aspect) / 2      # the stroke box's left, in heights
    out = [[(0, 0, 0, 0)] * width for _ in range(height)]
    for y in range(height):
        v = (y + 0.5) / height
        for x in range(width):
            u = (x + 0.5) / height              # across in heights, as the strokes are
            # Undo the slant (the top leans right), then into the stroke box.
            bx = (u - left + slant * (v - 0.5)) / aspect
            d = distance_scaled(bx, v, segs, aspect, square)
            sd = None
            if shadow:
                # The shadow at (u, v) is the glyph's point (u - dx, v - dy).
                sv = v - shadow[1]
                sx = (u - shadow[0] - left + slant * (sv - 0.5)) / aspect
                sd = distance_scaled(sx, sv, segs, aspect, square)
            if d <= pen:
                b = int(shade(v))
                out[y][x] = (b, b, b, 255)
            elif d <= pen + outline or (sd is not None and sd <= pen + outline + shadow_grow):
                out[y][x] = (0, 0, 0, 255)
    return out


def distance_scaled(bx: float, by: float, segs: list[tuple], aspect: float, square: bool = False) -> float:
    """The distance from (bx, by) in the stroke box to the strokes, measured in heights (the
    box's x is `aspect` heights wide); with `square`, the larger of the two axes' distances (a
    square pen: square corners and ends)."""
    best = 1e9
    px = bx * aspect
    for x0, y0, x1, y1 in segs:
        x0 *= aspect
        x1 *= aspect
        dx, dy = x1 - x0, y1 - y0
        length = dx * dx + dy * dy
        t = 0.0 if length == 0 else max(0.0, min(1.0, ((px - x0) * dx + (by - y0) * dy) / length))
        ex, ey = px - (x0 + t * dx), by - (y0 + t * dy)
        best = min(best, max(ex * ex, ey * ey) if square else ex * ex + ey * ey)
    return math.sqrt(best)


def original_cells() -> list:
    # The square digits' shapes (as the comic's), upright and squashed wide like a game menu's
    # lettering: rounded corners, a round pen, shaded from bright at the top to dark at the foot,
    # a thin outline and a dark shadow just below and to the right.
    def menu_shade(v: float) -> float:
        return 255 - 115 * max(0.0, min(1.0, (v - 0.15) / 0.7))
    return [drawn_cell(s, width=144, height=128, aspect=1.1, pen=0.07, outline=0.03,
                       slant=0.0, shadow=(0.02, 0.03), shade=menu_shade, corner=0.13)
            for s in SQUARE_STROKES]


def comic_cells() -> list:
    # Square blocky digits, leaning a little, a thin outline all round and a thick one set off
    # down and to the right, as a misprinted comic would have it.
    def two_tone(v: float) -> float:
        return 255 if v < 0.6 else 200
    return [drawn_cell(s, width=116, height=128, aspect=0.74, pen=0.07, outline=0.02,
                       slant=0.1, shadow=(0.04, 0.045), shadow_grow=0.03, shade=two_tone,
                       square=True) for s in SQUARE_STROKES]


# ---- files ----------------------------------------------------------------------------------------

def png(width: int, height: int, rows: list[list[tuple]]) -> bytes:
    def chunk(kind: bytes, data: bytes) -> bytes:
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    raw = bytearray()
    for row in rows:
        raw.append(0)
        for p in row:
            raw += bytes(p)
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
            + chunk(b"IEND", b""))


def atlas(cells: list) -> tuple[bytes, int, int]:
    """The cells side by side; the texture, a cell's width and height."""
    cw, ch = len(cells[0][0]), len(cells[0])
    rows = [[] for _ in range(ch)]
    for c in cells:
        for y in range(ch):
            rows[y] += c[y]
    return png(cw * len(cells), ch, rows), cw, ch


def glb(index: int, count: int, name: str, aspect: float, texture: str = TEXTURE,
        glow: float = 0.0) -> bytes:
    """A card for glyph `index` of `count`, `aspect` heights wide, its texture at the path
    `texture` from the mesh. With `glow`, the texture is its emission too, at that strength: lit
    by itself as well as by the level (OpenRAC's enhanced rendering adds a material's emission
    after the lighting), so it shows in dark places."""
    w = aspect
    u0, u1 = index / count, (index + 1) / count
    v0, v1 = 0.0, 1.0                        # glTF's v runs down the picture
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
        "nodes": [{"mesh": 0, "name": name}],
        "meshes": [{"primitives": [{
            "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
            "indices": 3, "material": 0}]}],
        "materials": [dict({
            "name": "glyphs",
            "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 0.0},
            "alphaMode": "MASK", "alphaCutoff": 0.5, "doubleSided": True},
            **({"emissiveTexture": {"index": 0}, "emissiveFactor": [glow, glow, glow]} if glow else {}))],
        "textures": [{"source": 0, "sampler": 0}],
        "samplers": [{"magFilter": 9729, "minFilter": 9729, "wrapS": 33071, "wrapT": 33071}],
        "images": [{"uri": texture}],
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


def write_style(style: str, cells: list) -> None:
    folder = ROOT / style
    folder.mkdir(parents=True, exist_ok=True)
    texture, cw, ch = atlas(cells)
    (folder / TEXTURE).write_bytes(texture)
    for i, name in enumerate(NAMES):
        (folder / f"{name}.glb").write_bytes(glb(i, len(NAMES), name, cw / ch))
    # The white glow copies of earlier versions: the mod writes the glow meshes itself now, in the
    # settings' colours (assets/<style>/tinted/, src/mod.cpp's write_glow_meshes).
    if (folder / "glow").is_dir():
        shutil.rmtree(folder / "glow")
    print(f"{style}: {folder / TEXTURE}, {len(NAMES)} glyphs, a card {cw / ch:.3f} wide")


def main() -> None:
    # Files of earlier versions, which kept one set at the top of assets/.
    for old in list(ROOT.glob("*.glb")) + list(ROOT.glob("*.png")):
        old.unlink()
    write_style("pixel", [pixel_cell(rows) for rows in PIXEL_FONT])
    write_style("original", original_cells())
    write_style("comic", comic_cells())


if __name__ == "__main__":
    main()
