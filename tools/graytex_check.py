"""the gray icons the catalogue generator writes (gray\\ug<hash>.tex, src/ut_graytex.h),
checked offline against the game's own icons.

For every catalogue record (data/oracle/catalogue.json) the name is computed here the way
utGrayName does (FNV-1a 64 of the record path, lower case, '\\' as '/'), the file must exist in the
generator's folder, and for a sample of records both the ORIGINAL icon (from the game archives,
tools/tqarc.py) and the gray file (from disk) are decoded with tools/tqtex.py: every texel must be
R = G = B = the Rec. 601 luma of the original, alpha unchanged, same size and header.

    python graytex_check.py [<gray folder>] [--all]
        <gray folder>  default build\\test\\gen-mod\\gray (written by tools\\build_test_catalogue.bat)
        --all          decode every record instead of a sample of 60 (slow: pure Python)
"""

from __future__ import annotations

import json
import os
import sys

import tqarc
import tqpath
import tqtex

HERE = os.path.dirname(os.path.abspath(__file__))
TREE = os.path.dirname(HERE)


def gray_name(record: str) -> str:
    h = 14695981039346656037
    for ch in record.encode("latin-1", "replace"):
        c = 0x2F if ch == 0x5C else ch
        if 0x41 <= c <= 0x5A:
            c += 0x20
        h ^= c
        h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return "ug%016x.tex" % h


def luma(r: int, g: int, b: int) -> int:
    return (299 * r + 587 * g + 114 * b + 500) // 1000


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    folder = args[0] if args else os.path.join(TREE, "build", "test", "gen-mod", "gray")
    everything = "--all" in sys.argv
    cat = json.load(open(os.path.join(TREE, "data", "oracle", "catalogue.json"), encoding="utf-8"))
    items = cat["items"]
    res = tqarc.ResourceArcs(tqpath.game_dir())
    bad = 0
    # the published FNV-1a 64 vectors, the same rows as test_viewgate
    if gray_name("") != "ugcbf29ce484222325.tex" or gray_name("a") != "ugaf63dc4c8601ec8c.tex":
        print("FAIL the FNV-1a 64 vectors")
        bad += 1
    names = {}
    for it in items:
        n = gray_name(it["record"])
        if n in names and names[n] != it["record"]:
            print("FAIL two records share %s: %s, %s" % (n, names[n], it["record"]))
            bad += 1
        names[n] = it["record"]
    missing = [r for n, r in names.items() if not os.path.isfile(os.path.join(folder, n))]
    total = sum(os.path.getsize(os.path.join(folder, n)) for n in names if os.path.isfile(os.path.join(folder, n)))
    print("names: %d records -> %d distinct names, %d missing in %s, %.1f MB" %
          (len(items), len(names), len(missing), os.path.relpath(folder, TREE), total / 1048576.0))
    if missing:
        print("FAIL e.g. %s" % missing[0])
        bad += 1
    step = 1 if everything else max(1, len(items) // 60)
    checked = texels = 0
    for it in items[::step]:
        orig = res.read(it["bitmap"])
        path = os.path.join(folder, gray_name(it["record"]))
        if not orig or not os.path.isfile(path):
            print("FAIL %s: the original or the gray file is not there" % it["record"])
            bad += 1
            continue
        gray = open(path, "rb").read()
        po, pg = tqtex.tex_pixels(orig), tqtex.tex_pixels(gray)
        off = 12 if orig[3] == 1 else 13
        head = off + 4 + 124
        if po is None or pg is None or po[:2] != pg[:2] or len(orig) != len(gray) or orig[:head] != gray[:head]:
            print("FAIL %s: not the same surface (size / header)" % it["record"])
            bad += 1
            continue
        wrong = 0
        for ro, rg in zip(po[2], pg[2]):
            for (ao, r, g, b), (ag, r2, g2, b2) in zip(ro, rg):
                y = luma(r, g, b)
                if ag != ao or not (r2 == g2 == b2 == y):
                    wrong += 1
                texels += 1
        if wrong:
            print("FAIL %s: %d texel(s) are not the luma of the original" % (it["record"], wrong))
            bad += 1
        checked += 1
    print("decoded %d record(s) through tqtex.py (%d texels): original vs gray" % (checked, texels))
    print("graytex_check: %s" % ("ALL PASS" if bad == 0 else "%d FAILURE(S)" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
