"""The header of a Titan Quest .tex, which is all reads of an icon (its size).

  TEX v1: 'TEX' 1, u32 fps, u32 size, the DDS surface at 12   (= GD's 'TEX' 2 layout)
  TEX v2: 'TEX' 2, u32 fps, u8 flag, u32 size, the surface at 13 (one extra byte)
The surface opens with 'DDS ' or TQ's 'DDSR'; its height and width are at +12 and +16.
The C++ twin is texSize() in src/gen/catalogue_gen.cpp.

    python tqtex.py <resource path> ...     the size of each icon, read from the game folder
"""

from __future__ import annotations

import struct
from typing import Optional, Tuple

TEX_HEADER = 64          # bytes read per icon: enough for the TEX and the DDS headers


def tex_size(blob: Optional[bytes]) -> Optional[Tuple[int, int]]:
    """(width, height) in pixels, or None when the bytes are not a TQ .tex header."""
    if not blob or len(blob) < 4 or blob[:3] != b"TEX":
        return None
    if blob[3] == 1:
        off = 12
    elif blob[3] == 2:
        off = 13
    else:
        return None
    if len(blob) < off + 20:
        return None
    if blob[off:off + 3] != b"DDS" or blob[off + 3] not in (0x20, 0x52):   # ' ' or 'R'
        return None
    h, w = struct.unpack_from("<II", blob, off + 12)
    if not (0 < w <= 4096 and 0 < h <= 4096):
        return None
    return (w, h)


def tex_pixels(blob: Optional[bytes]):
    """(width, height, rows) of an UNCOMPRESSED TQ .tex surface (the first mip), rows top to
    bottom, each a list of (a, r, g, b) 0..255 tuples; None when the bytes are not a TQ .tex or the
    surface is block-compressed. The channels come from the DDS pixel-format masks (the caravan art
    is 24 bpp RGB or 32 bpp ARGB; a 24 bpp surface has alpha 255). ui_palette.py samples these."""
    wh = tex_size(blob)
    if wh is None:
        return None
    off = 12 if blob[3] == 1 else 13
    w, h = wh
    pf = off + 4 + 72
    _pfsize, pfflags, fourcc, bpp, rm, gm, bm, am = struct.unpack_from("<8I", blob, pf)
    if fourcc or bpp not in (24, 32):
        return None
    hsize = struct.unpack_from("<I", blob, off + 4)[0]
    data = off + 4 + hsize
    step = bpp // 8
    if not (rm and gm and bm):   # TQ's DDSR headers may leave the masks 0: the D3D order (B, G, R, A)
        rm, gm, bm = 0xFF0000, 0xFF00, 0xFF
        am = 0xFF000000 if bpp == 32 else 0
        pfflags |= 0x1 if bpp == 32 else 0
    if len(blob) < data + w * h * step:
        return None

    def chan(v, m):
        if not m:
            return None
        sh = (m & -m).bit_length() - 1
        top = m >> sh
        return ((v & m) >> sh) * 255 // top

    rows = []
    for y in range(h):
        row = []
        base = data + y * w * step
        for x in range(w):
            p = base + x * step
            v = int.from_bytes(blob[p:p + step], "little")
            a = chan(v, am) if (am and (pfflags & 0x1)) else 255
            row.append((255 if a is None else a, chan(v, rm), chan(v, gm), chan(v, bm)))
        rows.append(row)
    return w, h, rows


if __name__ == "__main__":
    import sys

    import tqarc
    import tqpath

    res = tqarc.ResourceArcs(tqpath.game_dir())
    for p in sys.argv[1:]:
        wh = tex_size(res.read(p, TEX_HEADER))
        print(f"{p}: {wh[0]}x{wh[1]} px, {wh[0] // 32}x{wh[1] // 32} cells" if wh else f"{p}: not found")
