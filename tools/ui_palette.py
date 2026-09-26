#!/usr/bin/env python3
"""the collection pad's colours, sampled from the game's own caravan textures.

Nothing here is invented: every colour the pad, the slot plates and the grid cover use is the mean
(or the median, where a region has text or a highlight in it) of a named region of a texture that
the caravan window's records name, or a colour field of those records. The chain is followed through
the database exactly as the engine reads it:

    records/xpack/ui/caravan/caravanwindow.dbr
        BackgroundImage -> backgroundimage.dbr  bitmapName  CaravanWindow01.tex   (565 x 637, the window)
        TransferButton  -> transferbutton.dbr   bitmapNameUp / InFocus / Down      (the Transfer tab)
        StashWindow     -> stashwindow.dbr      IncreaseButton -> stashincreasebutton.dbr
                           bitmapNameUp / InFocus / Down   (TQ's own text button: idle / hover / pressed)
                           InventoryDBR -> stashinventory.dbr   backgroundShadeColor*, failsRequirementsColor*
        TransferWindow  -> transferwindow.dbr   WindowLocationY (the page's place in the window)

    python ui_palette.py              prints every region -> ARGB (and the C++ TqColor line)
    python ui_palette.py --png DIR    also writes each texture as a PNG with the regions boxed

Read-only: the game folder is found by tqpath.game_dir() and only read.
"""

from __future__ import annotations

import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import tqarc  # noqa: E402
import tqarz  # noqa: E402
import tqpath  # noqa: E402
import tqtex  # noqa: E402

ROOT = "records/xpack/ui/caravan/caravanwindow.dbr"

# (use, texture key, x, y, w, h, statistic). Texture keys are resolved from the records below.
# The regions are in the texture's own pixels (top-left origin, rows as the surface stores them).
# The Transfer page's grid sits at (27, 126) 512 x 480 of the window (transferwindow.dbr
# WindowLocationY = 126, stashinventory.dbr inventoryX = 27): the regions below avoid it.
REGIONS = [
    # the window's brown ground under the grid (the pad sits on it): the pad ground and the cover
    ("pad ground / grid cover", "window", 40, 612, 480, 18, "mean"),
    # the tab: its face without the caption (left of it), the lit top edge, the dark bottom edge
    ("button idle (tab face)", "tab.up", 6, 8, 40, 16, "median"),
    ("button edge top (tab)", "tab.up", 10, 1, 138, 2, "mean"),
    ("button edge bottom (tab)", "tab.up", 10, 28, 138, 2, "mean"),
    # TQ's text button in three states (the storage's "increase" button): face without caption
    ("button lit (increase up)", "inc.up", 12, 10, 20, 25, "median"),
    ("button lit hover (increase over)", "inc.over", 12, 10, 20, 25, "median"),
    ("button hover / lit bottom (increase down)", "inc.down", 12, 10, 20, 25, "median"),
    # the brightest tenth of the over face: the lit rim = the text / ring / lamp gold
    ("text / ring (increase over, top 10 %)", "inc.over", 2, 2, 107, 41, "top10"),
    # the grid's own cell as the window texture paints it (the grid is at (27,126), 32 px cells):
    # the inside of cell (1,1), its light hairline (the cell's last column) and dark flank
    ("slot plate (grid cell inside)", "window", 27 + 35, 126 + 36, 24, 24, "mean"),
    ("plate edge light (cell hairline)", "window", 27 + 31, 126 + 36, 1, 24, "mean"),
    ("plate edge dark (cell flank)", "window", 27 + 32, 126 + 36, 1, 24, "mean"),
]


def rgb_hex(c):
    a, r, g, b = c
    return f"0x{a:02X}{r:02X}{g:02X}{b:02X}"


def stat(rows, x, y, w, h, how):
    px = [rows[j][i] for j in range(y, y + h) for i in range(x, x + w)]
    if how == "mean":
        n = len(px)
        return tuple(round(sum(p[k] for p in px) / n) for k in range(4))
    if how == "median":
        return tuple(sorted(p[k] for p in px)[len(px) // 2] for k in range(4))
    if how == "top10":
        px.sort(key=lambda p: p[1] + p[2] + p[3], reverse=True)
        top = px[: max(1, len(px) // 10)]
        n = len(top)
        return tuple(round(sum(p[k] for p in top) / n) for k in range(4))
    raise ValueError(how)


def field(rec, name):
    v = rec.get(name) if rec else None
    return v[0] if isinstance(v, list) and v else v


def resolve(db):
    """texture key -> resource path, and the grid records' colour fields, from the records."""
    root = db.get(ROOT)
    if root is None:
        sys.exit(f"{ROOT} not in the database")
    tex = {}
    bg = db.get(field(root, "BackgroundImage"))
    tex["window"] = field(bg, "bitmapName")
    tb = db.get(field(root, "TransferButton"))
    tex["tab.up"] = field(tb, "bitmapNameUp")
    tex["tab.over"] = field(tb, "bitmapNameInFocus")
    tex["tab.down"] = field(tb, "bitmapNameDown")
    sw = db.get(field(root, "StashWindow"))
    ib = db.get(field(sw, "IncreaseButton"))
    tex["inc.up"] = field(ib, "bitmapNameUp")
    tex["inc.over"] = field(ib, "bitmapNameInFocus")
    tex["inc.down"] = field(ib, "bitmapNameDown")
    inv = db.get(field(sw, "InventoryDBR")) or db.get("records/xpack/ui/caravan/stashinventory.dbr")
    grid = {}
    for base in ("backgroundShadeColor", "failsRequirementsColor"):
        vals = [field(inv, base + ch) for ch in ("Alpha", "Red", "Green", "Blue")]
        if all(v is not None for v in vals):
            grid[base] = tuple(round(float(v) * 255) for v in vals)
    tw = db.get(field(root, "TransferWindow"))
    return tex, grid, field(tw, "WindowLocationY")


def write_png(path, w, h, rows, boxes):
    img = [[list(p) for p in r] for r in rows]
    for (x, y, bw, bh) in boxes:
        for i in range(x, x + bw):
            for j in (y, y + bh - 1):
                if 0 <= i < w and 0 <= j < h:
                    img[j][i] = [255, 0, 255, 255]
        for j in range(y, y + bh):
            for i in (x, x + bw - 1):
                if 0 <= i < w and 0 <= j < h:
                    img[j][i] = [255, 0, 255, 255]
    raw = b"".join(b"\0" + bytes(v for p in r for v in (p[1], p[2], p[3], p[0])) for r in img)

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def main(argv):
    png_dir = argv[argv.index("--png") + 1] if "--png" in argv else None
    game = tqpath.game_dir()
    db = tqarz.ArzDatabase.load_game(game)
    res = tqarc.ResourceArcs(game)
    tex, grid, page_y = resolve(db)
    pix = {}
    for key, path in tex.items():
        d = tqtex.tex_pixels(res.read(path)) if path else None
        pix[key] = d
        print(f"texture {key:9s} {path} " + (f"{d[0]}x{d[1]}" if d else "NOT DECODED"))
    print(f"transferwindow.dbr WindowLocationY = {page_y}")
    for base, c in grid.items():
        print(f"record  stashinventory.dbr {base:24s} -> ARGB {rgb_hex(c)}  {c}")
    print()
    boxes = {}
    for use, key, x, y, w, h, how in REGIONS:
        d = pix.get(key)
        if not d:
            print(f"{use:42s} {key}: no pixels")
            continue
        c = stat(d[2], x, y, w, h, how)
        boxes.setdefault(key, []).append((x, y, w, h))
        a, r, g, b = c
        print(f"{use:42s} {key:9s} ({x},{y}) {w}x{h} {how:6s} -> ARGB {rgb_hex(c)}  "
              f"{{{r / 255:.3f}f, {g / 255:.3f}f, {b / 255:.3f}f}}")
    if png_dir:
        os.makedirs(png_dir, exist_ok=True)
        for key, d in pix.items():
            if d:
                write_png(os.path.join(png_dir, key + ".png"), d[0], d[1], d[2], boxes.get(key, []))
        print(f"PNGs in {png_dir}")


if __name__ == "__main__":
    main(sys.argv[1:])
