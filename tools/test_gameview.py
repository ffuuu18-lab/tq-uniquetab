"""test_gameview.py - the generator's custom-game sets against the Python reference, and their cost.

Pass 6 of tools/build_test_catalogue.bat. First, always (no custom game needed): the search order.
test_catalogue.exe --order builds a scratch game (build/test/game-order) whose Resources/Items.arc
holds one entry of the base game's Items.arc, with other bytes, and checks the C++; then
tqarc.ResourceArcs must answer the same: the game's archive (and its bytes) for that entry, the base
archive for an entry only the base holds - the game's archives first, entry by entry.
The custom-game cases run only when TQ_TEST_MODS_DIR names a folder that
holds a custom game - <dir>/<id>/<name>/Database/<name>.arz (Steam's workshop layout) or
<dir>/<name>/Database/<name>.arz; otherwise it prints one line saying why the cases did not run, and
passes. For EVERY game found:
  - test_catalogue.exe --game-view <root> <name> <out>/<leaf>/cpp: the C++ generator in a process of
    its own (the stamp, the second call up to date, and the [measure] lines: the records before and
    after EXCLUDE, the groups and their sizes, the wall time, the peak working set rise, the gray
    icons, the search index's bytes per record at that count, the owned tables at both caps);
  - tools/build_catalogue.py --game-mod <root>/<name> --collect-mi --out <out>/<leaf>/py, then
    tools/pack_catalogue.py: catalogue.bin, uniq-records.txt, uniq-groups.txt and uniq-excluded.txt
    byte-identical with the C++ ones, and the same leaf on both sides;
  - the gray icons COUNTED (every catalogue record whose icon was found has its gray\\ug<hash>.tex)
    and SAMPLED (three files are the size of their source icon: the gray copy keeps the TEX layout);
    their bytes are not compared, the Python reference writes no gray icons;
  - the difference from the start-up game (data/oracle): records added and removed in
    uniq-records.txt, records whose name, icon or footprint changed, G lines that changed.
Then it prints the measurement table. Everything it writes stays under --out.

    python tools/test_gameview.py --exe build/test/test_catalogue.exe --out build/test/game
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import build_catalogue as bc  # noqa: E402
import tqarc  # noqa: E402
import tqpath  # noqa: E402

OUTPUTS = ("catalogue.bin", "uniq-records.txt", "uniq-groups.txt", "uniq-excluded.txt", "uniq-geometry.txt",
        "uniq-relics.txt")
MEASURE = re.compile(r"^\[measure\] (\w+) ?(.*)$")


def gray_name(record: str) -> str:
    """ut_graytex.h utGrayName: ug + FNV-1a 64 of the lower-case '/' record path + .tex."""
    h = 14695981039346656037
    for b in record.replace("\\", "/").lower().encode("utf-8"):
        h = ((h ^ b) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return "ug%016x.tex" % h


def find_games(mods_dir: str):
    games = []
    for top in sorted(os.listdir(mods_dir)):
        p = os.path.join(mods_dir, top)
        if not os.path.isdir(p):
            continue
        cands = [(mods_dir, top)] + [(p, n) for n in sorted(os.listdir(p))
                                     if os.path.isdir(os.path.join(p, n))]
        for root, name in cands:
            if os.path.isfile(os.path.join(root, name, "Database", name + ".arz")):
                games.append((root, name))
    return games


def lines(path: str):
    with open(path, "rb") as fh:
        return fh.read().decode("utf-8").split("\r\n")[:-1]


def fields(text: str):
    return dict(kv.split("=", 1) for kv in text.split() if "=" in kv)


def order_check(exe: str, out: str) -> int:
    """The search order on test_catalogue.exe --order's scratch game, asked of the Python twin."""
    shutil.rmtree(out, ignore_errors=True)
    p = subprocess.run([exe, "--order", out], capture_output=True, text=True)
    got = [l.split("\t") for l in p.stdout.splitlines() if l.startswith("[order]\t")]
    for l in p.stdout.splitlines():
        if not l.startswith("[order]"):
            print(l)
    if p.returncode or len(got) != 1 or len(got[0]) != 4:
        print("[test] FAIL: test_catalogue --order (exit %d) %s" % (p.returncode, p.stderr.strip()))
        return 1
    _, both, base_only, res_dir = got[0]
    game_dir = tqpath.game_dir()
    plain = tqarc.ResourceArcs(game_dir)
    res = tqarc.ResourceArcs(game_dir, res_dir)
    base_tag = plain.source_of(both)
    mine = res.read(both)
    checks = [
        (bool(base_tag) and plain.source_of(base_only) == base_tag,
         "Python: the start-up game resolves both entries to its Items.arc (%s)" % base_tag),
        (res.source_of(both).startswith("game:"),
         "Python: an entry both hold resolves to the game's archive first (%s)" % res.source_of(both)),
        (mine is not None and mine != plain.read(both),
         "Python: and reads the game's bytes, not the base game's"),
        (res.source_of(base_only) == base_tag,
         "Python: an entry only the base holds falls back to the base archive (entry by entry)"),
    ]
    rc = 0
    for ok, what in checks:
        print("[test] %s: %s" % ("ok  " if ok else "FAIL", what))
        rc |= 0 if ok else 1
    return rc


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--exe", required=True, help="test_catalogue.exe")
    ap.add_argument("--out", required=True, help="the scratch folder (one sub-folder per game)")
    ap.add_argument("--oracle", default=os.path.join(HERE, "..", "data", "oracle"))
    args = ap.parse_args()
    rc0 = order_check(args.exe, os.path.join(os.path.dirname(os.path.abspath(args.out)), "game-order"))
    mods = os.environ.get("TQ_TEST_MODS_DIR", "")
    if not mods:
        print("[test] skip: TQ_TEST_MODS_DIR is not set - the custom-game cases did not run")
        return rc0
    if not os.path.isdir(mods):
        print("[test] skip: TQ_TEST_MODS_DIR (%s) is not a folder - the custom-game cases did not run" % mods)
        return rc0
    games = find_games(mods)
    if not games:
        print("[test] skip: TQ_TEST_MODS_DIR (%s) holds no custom game (<id>\\<name>\\Database\\<name>.arz)"
              " - the custom-game cases did not run" % mods)
        return rc0
    game_dir = tqpath.game_dir()
    base_rec = lines(os.path.join(args.oracle, "uniq-records.txt"))
    base_g = [l for l in lines(os.path.join(args.oracle, "uniq-groups.txt")) if l.startswith("G\t")]
    with open(os.path.join(args.oracle, "catalogue.json"), encoding="utf-8") as fh:
        base_items = {i["record"].lower(): i for i in json.load(fh)["items"]}
    rc, rows, owned, geos = rc0, [], [], []
    print("[test] %d custom game(s) under TQ_TEST_MODS_DIR" % len(games))
    dbtn = {}
    for root, name in games:
        leaf = bc.save_set_game(name)
        top = os.path.join(args.out, leaf)
        cdir, pdir = os.path.join(top, "cpp"), os.path.join(top, "py")
        shutil.rmtree(top, ignore_errors=True)
        os.makedirs(cdir)
        print("[test] -- game %s" % leaf)
        p = subprocess.run([args.exe, "--game-view", root, name, cdir], capture_output=True, text=True)
        m, groups = {}, []
        for l in p.stdout.splitlines():
            mm = MEASURE.match(l)
            if not mm:
                print(l)
            elif mm.group(1) == "group":
                groups.append(fields(mm.group(2)))
            elif mm.group(1) == "owned":
                if len(owned) < 2:
                    owned.append(fields(mm.group(2)))
            else:
                m[mm.group(1)] = fields(mm.group(2))
        if p.returncode or "game" not in m:
            print("[test] FAIL: test_catalogue --game-view %s (exit %d) %s" % (leaf, p.returncode, p.stderr.strip()))
            rc = 1
            continue
        t0 = time.time()
        q = subprocess.run([sys.executable, os.path.join(HERE, "build_catalogue.py"), "--game", game_dir,
                            "--game-mod", os.path.join(root, name), "--collect-mi", "--out", pdir],
                           capture_output=True, text=True)
        py_s = time.time() - t0
        if q.returncode == 0:
            q = subprocess.run([sys.executable, os.path.join(HERE, "pack_catalogue.py"), "--in",
                                os.path.join(pdir, "catalogue.json"), "--out",
                                os.path.join(pdir, "catalogue.bin")], capture_output=True, text=True)
        if q.returncode:
            print("[test] FAIL: the Python reference for %s: %s" % (leaf, (q.stderr or q.stdout).strip()[-400:]))
            rc = 1
            continue
        same = True
        for f in OUTPUTS:
            with open(os.path.join(cdir, f), "rb") as a, open(os.path.join(pdir, f), "rb") as b:
                ok = a.read() == b.read()
            same = same and ok
            print("[test] %s: %s %s is byte-identical to the Python reference" % ("ok  " if ok else "FAIL", leaf, f))
        ok = m["game"].get("leaf") == leaf
        print("[test] %s: the C++ leaf (%s) is the Python one" % ("ok  " if ok else "FAIL", m["game"].get("leaf")))
        rc |= 0 if (same and ok) else 1

        with open(os.path.join(pdir, "catalogue.json"), encoding="utf-8") as fh:
            items = json.load(fh)["items"]
        want = [i for i in items if i["bitmapFound"]]
        gray = os.path.join(cdir, "gray")
        have = sum(1 for i in want if os.path.isfile(os.path.join(gray, gray_name(i["record"]))))
        ok = have == len(want) == int(m["game"]["gray"])
        print("[test] %s: %d of %d records with an icon have a gray file (the generator wrote %s)"
              % ("ok  " if ok else "FAIL", have, len(want), m["game"]["gray"]))
        rc |= 0 if ok else 1
        res = tqarc.ResourceArcs(game_dir, bc.GameSpec(root, name).resources_dir())
        sample = [want[k * (len(want) - 1) // 2] for k in range(3)] if want else []
        sizes = []
        for i in sample:
            src = res.read(i["bitmap"])
            gp = os.path.join(gray, gray_name(i["record"]))
            sizes.append(src is not None and os.path.isfile(gp) and os.path.getsize(gp) == len(src))
        ok = bool(sample) and all(sizes)
        print("[test] %s: three sampled gray files are the size of their source icon" % ("ok  " if ok else "FAIL"))
        rc |= 0 if ok else 1

        # the game's default buttons file (utbDefaultText's twin over the C++ groups): one button
        # per group in group order, Other included when the game has a record of a class no
        # group knows
        dg = bc.buttons_groups_from(os.path.join(cdir, "uniq-groups.txt"))
        dt = bc.buttons_default_text(bc.buttons_view_title(name), dg)
        sec, btn = None, []
        for l in dt.split("\n"):
            if l.startswith("[") and l.endswith("]"):
                sec = l[1:-1]
            elif sec not in (None, "tab", "unassigned") and l.startswith("groups  = "):
                btn.append((sec, l[len("groups  = "):]))
        ids = [g[0] for g in dg]
        ok = bool(dg) and [b[1] for b in btn] == ids and len(set(b[0].upper() for b in btn)) == len(btn)
        dbtn[leaf] = (len(btn), len(dg), "other" in ids)
        print("[test] %s: %s default buttons file: %d button(s) over %d group(s), one per group%s"
              % ("ok  " if ok else "FAIL", leaf, len(btn), len(dg),
                 ", Other included" if "other" in ids else ", no Other group"))
        rc |= 0 if ok else 1

        rec = lines(os.path.join(pdir, "uniq-records.txt"))
        g_lines = [l for l in lines(os.path.join(pdir, "uniq-groups.txt")) if l.startswith("G\t")]
        added = len(set(rec) - set(base_rec))
        removed = len(set(base_rec) - set(rec))
        changed = 0
        for i in items:
            b = base_items.get(i["record"].lower())
            if b and any(i[k] != b[k] for k in ("name", "bitmap", "footW", "footH", "itemClassification")):
                changed += 1
        g_changed = len(set(g_lines) ^ set(base_g))
        gm = {g.get("id"): g.get("count") for g in groups}
        rows.append((leaf, m["game"], m.get("index", {}), gm, py_s, added, removed, changed, g_changed, groups))
        geos.append((leaf, m.get("geometry", {})))

    print("\n[measure] the custom games (collect_mi=1; C++ generator, a process per game)")
    print("| game | before EXCLUDE | records | excluded | paged | groups | rings | artifacts | blueprints "
          "| C++ ms | peak +MB | process peak MB | process peak commit MB | gray (MB) | index B/rec | index KB | Python s "
          "| vs start-up: +rec / -rec / changed rec / G lines |")
    print("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|")
    for leaf, g, ix, gm, py_s, added, removed, changed, g_changed, _ in rows:
        print("| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s (%s) | %s | %s | %.0f | +%d / -%d / %d / %d |"
              % (leaf, g["before_exclude"], g["records"], g["excluded"], g["paged"], g["groups"],
                 gm.get("ring", "-"), gm.get("artifact", "-"), gm.get("blueprint", "-"), g["ms"],
                 g["peak_added_mb"], g["process_peak_mb"], g.get("process_commit_mb", "-"),
                 g["gray"], g["gray_mb"],
                 ix.get("bytes_per_record", "-"), ix.get("total_kb", "-"), py_s, added, removed,
                 changed, g_changed))
    for leaf, _g, _ix, _gm, _py, _a, _r, _c, _gc, groups in rows:
        print("[measure] %s groups: %s" % (leaf, ", ".join("%s %s" % (g.get("id"), g.get("count")) for g in groups)))
        if leaf in dbtn:
            print("[measure] %s default buttons file: %d button(s) over %d group(s), Other %s"
                  % (leaf, dbtn[leaf][0], dbtn[leaf][1], "included" if dbtn[leaf][2] else "absent"))
    for o in owned:
        print("[measure] owned tables at cap %s: %s bytes" % (o.get("cap"), o.get("bytes")))
    print("\n[measure] the caravan geometry per game (uniq-geometry.txt; caravan-window units)")
    print("| game | grid origin | grid px | cells | cover | caravan window | Transfer page at | background at "
          "| inside the window |")
    print("|---|---|---|---|---|---|---|---|---|")
    for leaf, g in geos:
        print("| %s | %s,%s | %sx%s | %s x %s | %sx%s | %sx%s | %s,%s | %s,%s | %s |"
              % (leaf, g.get("x0"), g.get("y0"), g.get("w"), g.get("h"), g.get("cols"), g.get("rows"),
                 g.get("plate_w"), g.get("plate_h"), g.get("window_w"), g.get("window_h"),
                 g.get("page_x"), g.get("page_y"), g.get("bg_x"), g.get("bg_y"),
                 "yes" if g.get("inside") == "1" else "NO"))
    print("[test] custom games: %s" % ("ALL PASS" if rc == 0 else "FAILURES"))
    return rc


if __name__ == "__main__":
    sys.exit(main())
