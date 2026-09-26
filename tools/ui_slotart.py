#!/usr/bin/env python3
"""the equipment window's slot ground, cropped per collection group.

The character window's item boxes carry NO bitmap of their own (ItemBox.tpl / ItemBoxHand.tpl: only
itemX / itemY / itemXSize / itemYSize, the shade / fails colours and a pick-up sound). The cracked
stone ground with the faint drawing of the item type is baked into the window's background image.
So a slot's art = the window texture CROPPED at the box's rect (the window's own pixels). This
follows the chain exactly as the engine reads it:

    records/ui/character/characterwindow.dbr
        characterDisplayBitmap -> CharacterWindowImage.dbr  bitmapName  CharacterWindow01.tex
        equipHead / equipUpperBody / ... -> CharacterEquip*.dbr  itemX itemY itemXSize itemYSize

and prints, per collection group, the box it uses, its crop and its native aspect against the
collection slot it is stretched onto (the table src/ut_slotart.h carries; test_config checks the
two agree through the oracle file data/oracle/slotart.txt this script writes with --oracle).

    python ui_slotart.py                 the table
    python ui_slotart.py --png DIR       also the window texture with the boxes and one PNG per crop
    python ui_slotart.py --oracle FILE   also write the table as the oracle file

Read-only on the game: the folder is found by tqpath.game_dir() and only read.
"""

from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import tqarc  # noqa: E402
import tqarz  # noqa: E402
import tqpath  # noqa: E402
import tqtex  # noqa: E402
from ui_palette import field, write_png  # noqa: E402

ROOT = "records/ui/character/characterwindow.dbr"

# collection group -> (the equipment box, the group's slot in cells (w, h))
GROUPS = [
    ("Helms", "equipHead", 2, 2),
    ("Torso", "equipUpperBody", 2, 3),
    ("Arms", "equipForearm", 2, 2),
    ("Legs", "equipLowerBody", 2, 2),
    ("Amulets", "equipNeck", 2, 1),
    ("Rings", "equipFinger1", 1, 1),
    ("Shields", "equipHandLeft", 2, 3),
    ("Axes", "equipHandRight", 2, 3),
    ("Maces", "equipHandRight", 2, 4),
    ("Staves", "equipHandRight", 2, 4),
    ("Swords", "equipHandRight", 2, 4),
    ("Throwing", "equipHandRight", 2, 3),
    ("Spears", "equipHandRight", 1, 5),
    ("Bows", "equipHandRight", 1, 4),
    ("Artifacts", "equipArtifact", 2, 2),
]
CELL = 32   # the collection sack's cell at UI scale 1 (the Transfer sack's)


def main(argv):
    png_dir = argv[argv.index("--png") + 1] if "--png" in argv else None
    oracle = argv[argv.index("--oracle") + 1] if "--oracle" in argv else None
    game = tqpath.game_dir()
    db = tqarz.ArzDatabase.load_game(game)
    res = tqarc.ResourceArcs(game)
    root = db.get(ROOT)
    if root is None:
        sys.exit(f"{ROOT} not in the database")
    img = db.get(field(root, "characterDisplayBitmap"))
    tex = field(img, "bitmapName")
    d = tqtex.tex_pixels(res.read(tex)) if tex else None
    print(f"window {ROOT}: extent {field(root, 'windowDefaultExtentX')}x{field(root, 'windowDefaultExtentY')}")
    print(f"image  {field(root, 'characterDisplayBitmap')} bitmapName {tex} "
          f"at ({field(img, 'bitmapPositionX')},{field(img, 'bitmapPositionY')}) -> "
          + (f"{d[0]}x{d[1]}" if d else "NOT DECODED"))
    boxes = {}
    lines = []
    for group, key, sw, sh in GROUPS:
        rec = db.get(field(root, key))
        x, y, w, h = (int(field(rec, n)) for n in ("itemX", "itemY", "itemXSize", "itemYSize"))
        boxes[key] = (x, y, w, h)
        slot_a = sw / sh
        crop_a = w / h
        q = slot_a / crop_a   # utSlotArtFit: the whole box within 35 %, else a centred sub-rect
        verdict = "stretch" if 1 / 1.35 <= q <= 1.35 else "cover"
        print(f"{group:9s} {key:15s} ({x:3d},{y:3d}) {w:3d}x{h:3d}  aspect {crop_a:5.2f}  "
              f"slot {sw}x{sh} = {sw * CELL}x{sh * CELL} aspect {slot_a:5.2f}  -> {verdict}")
        lines.append(f"{group}\t{key}\t{x}\t{y}\t{w}\t{h}\t{sw}\t{sh}\t{verdict}")
    if oracle:
        with open(oracle, "w", newline="\n") as f:
            f.write("# ui_slotart.py: group, box, itemX, itemY, itemXSize, itemYSize, slotW, slotH, draw\n")
            f.write(f"# texture {tex}\n")
            f.write("\n".join(lines) + "\n")
        print(f"oracle {oracle}")
    if png_dir and d:
        os.makedirs(png_dir, exist_ok=True)
        write_png(os.path.join(png_dir, "characterwindow.png"), d[0], d[1], d[2], list(boxes.values()))
        for key, (x, y, w, h) in boxes.items():
            rows = [r[x:x + w] for r in d[2][y:y + h]]
            write_png(os.path.join(png_dir, key + ".png"), w, h, rows, [])
        print(f"PNGs in {png_dir}")


if __name__ == "__main__":
    main(sys.argv[1:])
