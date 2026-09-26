"""Reference implementation; the DLL generates these files itself (src/gen/catalogue_gen).

Build data/oracle/catalogue.json + catalogue-stats.md and the three list fixtures
(uniq-records.txt, uniq-groups.txt, uniq-excluded.txt) from Titan Quest AE's own database.

    python build_catalogue.py [--game <dir>] [--out <dir>] [--lang EN]
    python build_catalogue.py --compare <folder>     the files the DLL wrote into <folder>
                                                     (catalogue.bin and the three lists)
                                                     against data/oracle, byte for byte

Nothing is written outside <out>. tools/pack_catalogue.py packs catalogue.json into
catalogue.bin; the C++ generator writes all four and tools/test_catalogue.cpp (--ensure)
compares them with these files byte for byte.

THE COLLECTION RULE (Titan Quest):
  * equipment (the EQUIP_SLOT classes - ArmorProtective_* / ArmorJewelry_* / Weapon*_*) and
    artifacts (Class ItemArtifact) whose itemClassification is Epic or Legendary,
  * under records/(xpackN/)item(s)/,
  * whose name tag (itemNameTag; artifacts: description) resolves in Text_<lang>.arc,
  * that are no template: FileDescription or path free of "blank", "test", "template",
    "copy of" (GD's rule 4, "no BLANK", widened to the words TQ's templates use).
  -> 1,602 records on AE 2.10 (755 Epic + 847 Legendary). That is an UPPER BOUND.

THE EXCLUDE RULE, first reason that applies, every dropped record listed in uniq-excluded.txt:
  1. the expansion is not installed (its Resources folder is missing),
  2. no icon field at all (bitmap / artifactBitmap / relicBitmap),
  3. a developer folder in the path (_devcheat, devitem, sandbox, dev),
  4. unreferenced: no OTHER record holds the path in any string field - no loot table,
     merchant, container, formula, quest record or monster (a scan of every string field),
  5. the icon is not in the item archives,
  6. the footprint (icon pixels / 32) is outside 1..2 x 1..5 cells.
Two decisions (HANDOFF.md section 6): the records held only by monsters STAY
(monsters drop what they hold), and the records that share a display name STAY separate
(distinct records, distinct drops).
"""

from __future__ import annotations

import argparse
import collections
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import tqarc  # noqa: E402
import tqpath  # noqa: E402
import tqarz as arz  # noqa: E402
import tqtex  # noqa: E402

DEFAULT_GAME = tqpath.find_game_dir() or ""
DEFAULT_OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "data", "oracle")

# ---------------------------------------------------------------- class -> slot
# TQ's classes on GD's slot words, so on the model's 29 groups (unchanged). TQ has no shoulders,
# belt, boots, medal, dagger, 2H melee or off-hand.
EQUIP_SLOT = {
    "ArmorProtective_Head": "head",
    "ArmorProtective_UpperBody": "chest",
    "ArmorProtective_Forearm": "hands",
    "ArmorProtective_LowerBody": "legs",
    "ArmorJewelry_Ring": "ring",
    "ArmorJewelry_Amulet": "amulet",
    "WeaponMelee_Sword": "sword1h",
    "WeaponMelee_Axe": "axe1h",
    "WeaponMelee_Mace": "mace1h",
    "WeaponHunting_Spear": "spear2h",
    "WeaponHunting_Bow": "ranged2h",
    "WeaponHunting_RangedOneHand": "ranged1h",
    "WeaponMagical_Staff": "scepter",
    "WeaponArmor_Shield": "shield",
}
OTHER_SLOT = {
    "ItemArtifact": "relic",
}
BLUEPRINT_CLASSES = ("ItemArtifactFormula",)
RELIC_CLASS = "ItemArtifact"
EQUIP_FAMILY = re.compile(r"^(ArmorProtective|ArmorJewelry|Weapon[A-Za-z]*)_")
DEV_FOLDERS = ("_devcheat", "devitem", "sandbox", "dev")
TEMPLATE_WORDS = ("blank", "test", "template", "copy of")
ITEM_PATH = re.compile(r"^records/(xpack\d?/)?items?/")
EXPANSIONS = {0: "base", 1: "IT", 2: "Ragnarok", 3: "Atlantis", 4: "EternalEmbers"}
EXPANSION_FOLDER = {1: "xpack", 2: "XPack2", 3: "XPack3", 4: "XPack4"}

CANDIDATES_EXPECT = 1602        # the collection rule on AE 2.10, before EXCLUDE

# the list files: GD's pages_gen order and labels (TQ's labels on the slots TQ has)
SLOT_ORDER = [
    ("head", "Helms"), ("shoulders", "Shoulders"), ("chest", "Torso"), ("hands", "Arms"),
    ("waist", "Belts"), ("legs", "Legs"), ("feet", "Boots"), ("amulet", "Amulets"),
    ("medal", "Medals"), ("ring", "Rings"), ("offhand", "Off-hands"), ("shield", "Shields"),
    ("axe1h", "Axes"), ("dagger", "Daggers"), ("mace1h", "Maces"), ("scepter", "Staves"),
    ("sword1h", "Swords"), ("ranged1h", "Throwing"), ("axe2h", "2H Axes"), ("mace2h", "2H Maces"),
    ("spear2h", "Spears"), ("sword2h", "2H Swords"), ("ranged2h", "Bows"), ("relic", "Artifacts"),
]
HOST_COLS, HOST_ROWS, CELL_PX = 16, 15, 32     # the Transfer sack: 16 x 15 cells of 32 px (measured)
LIST_FILES = ("catalogue.bin", "uniq-records.txt", "uniq-groups.txt", "uniq-excluded.txt")


def slot_of(cls: str) -> str:
    return EQUIP_SLOT.get(cls) or OTHER_SLOT.get(cls) or "other"


def name_tag_of(rec, cls: str) -> str:
    """Equipment records use itemNameTag; every other item class uses `description`."""
    if cls in EQUIP_SLOT:
        return arz.sfield(rec, "itemNameTag")
    return arz.sfield(rec, "itemNameTag") or arz.sfield(rec, "description")


def crafted_record(rec, cls: str) -> str:
    if cls in BLUEPRINT_CLASSES:
        return arz.sfield(rec, "artifactName")
    return ""


def bitmap_of(rec, cls: str) -> str:
    for f in ("bitmap", "artifactBitmap", "relicBitmap"):
        v = arz.sfield(rec, f)
        if v:
            return v
    return ""


def template_words(lower_text: str) -> bool:
    return any(w in lower_text for w in TEMPLATE_WORDS)


def lower_ascii(s: str) -> str:
    return "".join(chr(ord(c) + 32) if "A" <= c <= "Z" else c for c in s)


def is_shipped(key: str, rec, cls: str, tags, db=None) -> bool:
    if not ITEM_PATH.match(key):
        return False
    if template_words(lower_ascii(arz.sfield(rec, "FileDescription"))) or template_words(key):
        return False
    if name_tag_of(rec, cls) in tags:
        return True
    tgt = crafted_record(rec, cls)
    if tgt and db is not None:
        trec = db.get(tgt)
        if trec is not None and name_tag_of(trec, arz.sfield(trec, "Class")) in tags:
            return True
    return False


def expansion_of(key: str) -> int:
    m = re.match(r"records/xpack([234]?)/", key)
    if not m:
        return 0
    return int(m.group(1)) if m.group(1) else 1


def lower_name(s: str) -> bytes:
    """The C++ lowerName() on the UTF-8 bytes: ASCII A-Z and the Latin-1 capitals (U+00C0..U+00DE
    but U+00D7), exactly - not str.lower(), so the two sides fold every name the same way."""
    b = bytearray(s.encode("utf-8"))
    i = 0
    while i < len(b):
        c = b[i]
        if 0x41 <= c <= 0x5A:
            b[i] = c + 32
        elif c == 0xC3 and i + 1 < len(b):
            d = b[i + 1]
            if 0x80 <= d <= 0x9E and d != 0x97:
                b[i + 1] = d + 0x20
            i += 1
        i += 1
    return bytes(b)


def build(game_dir: str, out_dir: str, lang: str = "EN"):
    db = arz.ArzDatabase.load_game(game_dir)
    tags = tqarc.load_text_tags(game_dir, lang)
    res = tqarc.ResourceArcs(game_dir)
    installed = {0: True}
    for n, folder in EXPANSION_FOLDER.items():
        installed[n] = os.path.isdir(os.path.join(game_dir, "Resources", folder))

    set_name_cache = {}

    def set_display_name(set_record: str) -> str:
        if not set_record:
            return ""
        if set_record in set_name_cache:
            return set_name_cache[set_record]
        sr = db.get(set_record)
        nm = ""
        if sr:
            nm = tags.get(arz.sfield(sr, "setName"), "") or tags.get(arz.sfield(sr, "description"), "")
            nm = nm.rstrip(" \t")        # TQ: Text_EN keeps trailing blanks; the C++ strips them too
        set_name_cache[set_record] = nm
        return nm

    def entry(key, src, rec, cls):
        tag = name_tag_of(rec, cls)
        set_rec = arz.sfield(rec, "itemSetName")
        return {
            "record": db.real_name(key) or key,
            "class": cls,
            "slot": slot_of(cls),
            "craftsRecord": crafted_record(rec, cls),
            "itemNameTag": tag,
            "name": tags.get(tag, "").rstrip(" \t"),
            "itemClassification": arz.sfield(rec, "itemClassification"),
            "itemSetName": set_rec,
            "setDisplayName": set_display_name(set_rec),
            "levelRequirement": arz.ifield(rec, "levelRequirement"),
            "itemLevel": arz.ifield(rec, "itemLevel"),
            "bitmap": bitmap_of(rec, cls),
            "bitmapFound": False,
            "bitmapArchive": "",
            "footW": 0,
            "footH": 0,
            "expansion": expansion_of(key),
            "source": src,
            "isBlueprint": cls.endswith("Formula"),
            "isRelicOrComponent": False,
            "isAugment": False,
            "isRelic": cls == RELIC_CLASS,
            "isSetPiece": bool(set_rec),
            "isEquipment": cls in EQUIP_SLOT,
            "isExtra": False,
        }

    # one pass: the reverse index of every string field that names a .dbr, and the candidates
    refs = collections.defaultdict(set)          # target key -> referrer keys (not itself)
    cands, unmapped = [], []
    for key, src, rec, flds in db.items_with_fields():
        for _field, ftype, vals in flds:
            if ftype != arz.FT_STRING:
                continue
            for v in vals:
                if len(v) >= 4 and lower_ascii(v[-4:]) == ".dbr":
                    tgt = lower_ascii(v.replace("\\", "/"))
                    refs[tgt].add(key)
        cls = arz.sfield(rec, "Class")
        cla = arz.sfield(rec, "itemClassification")
        if cls not in EQUIP_SLOT and cls not in OTHER_SLOT:
            if cla in ("Epic", "Legendary") and EQUIP_FAMILY.match(cls) and ITEM_PATH.match(key):
                unmapped.append((key, cls))
            continue
        if cla not in ("Epic", "Legendary"):
            continue
        if not is_shipped(key, rec, cls, tags, db):
            continue
        cands.append(entry(key, src, rec, cls))
    assert not unmapped, "Epic/Legendary records of a class with no slot: %r" % (unmapped[:5],)
    assert len(cands) == CANDIDATES_EXPECT, (
        "the collection rule gives %d records, expected %d (AE 2.10). The game database moved - "
        "re-read it and update CANDIDATES_EXPECT; do NOT silence this." % (len(cands), CANDIDATES_EXPECT))

    items, excluded = [], []
    for e in cands:
        key = e["record"].lower()
        reason = ""
        if not installed[e["expansion"]]:
            reason = "expansion not installed"
        elif not e["bitmap"]:
            reason = "no icon field (bitmap / artifactBitmap)"
        else:
            for d in DEV_FOLDERS:
                if "/" + d + "/" in key:
                    reason = "developer folder " + d
                    break
        if not reason and not any(r != key for r in refs.get(key, ())):
            reason = "unreferenced (no loot table, merchant, container, formula, quest or monster holds it)"
        if not reason:
            wh = tqtex.tex_size(res.read(e["bitmap"], tqtex.TEX_HEADER))
            e["bitmapFound"] = wh is not None
            e["bitmapArchive"] = res.source_of(e["bitmap"]) if wh else ""
            if wh is None:
                reason = "icon not found in the item archives"
            else:
                e["footW"], e["footH"] = wh[0] // 32, wh[1] // 32
                if not (1 <= e["footW"] <= 2 and 1 <= e["footH"] <= 5):
                    reason = "footprint %dx%d (icon %dx%d) outside 1..2 x 1..5" % (
                        e["footW"], e["footH"], wh[0], wh[1])
        if reason:
            excluded.append({"record": e["record"], "reason": reason, "class": e["class"],
                             "name": e["name"], "expansion": e["expansion"]})
        else:
            items.append(e)
    excluded.sort(key=lambda x: x["record"])

    items.sort(key=lambda e: (e["slot"], e["itemClassification"], e["record"]))
    sets = sorted({lower_ascii(e["itemSetName"].replace("\\", "/")) for e in items if e["itemSetName"]})
    meta = {
        "game": "Titan Quest Anniversary Edition 2.10",
        "generated": "tools/build_catalogue.py",
        "archives": [{"tag": a.tag, "records": len(a)} for a in db.archives],
        "mergedRecords": len(db),
        "textTags": len(tags),
        "lang": lang,
        "expansionsInstalled": {EXPANSIONS[k]: v for k, v in sorted(installed.items())},
        "candidates": len(cands),
        "excluded": len(excluded),
        "collectionRule": "Epic/Legendary EQUIP_SLOT classes + ItemArtifact under "
                          "records/(xpackN/)item(s)/, name tag resolves, no template words",
        "excludeRule": ["expansion not installed", "no icon field", "developer folder",
                        "unreferenced", "icon not found", "footprint outside 1..2 x 1..5"],
    }
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "catalogue.json"), "w", encoding="utf-8") as fh:
        fh.write("{\n")
        fh.write('"meta": ' + json.dumps(meta, indent=1) + ",\n")
        for label, lst in (("items", items), ("excluded", excluded)):
            fh.write(f'"{label}": [\n')
            fh.write(",\n".join(json.dumps(e, sort_keys=True) for e in lst))
            fh.write("\n],\n")
        fh.write('"itemSets": ' + json.dumps(sets, indent=1) + "\n}\n")

    groups = write_lists(out_dir, items, excluded)
    write_stats(os.path.join(out_dir, "catalogue-stats.md"), items, excluded, cands, groups, meta)
    return items, excluded, cands, groups


def write_lists(out_dir, items, excluded):
    """uniq-records.txt, uniq-groups.txt, uniq-excluded.txt - CRLF, as the DLL writes them."""
    order = {s: n for n, (s, _l) in enumerate(SLOT_ORDER)}
    label = dict(SLOT_ORDER)
    rank = {"Legendary": 0, "Epic": 1, "Rare": 2, "Common": 3}
    picked = sorted(items, key=lambda i: (order.get(i["slot"], 99), rank.get(i["itemClassification"], 9),
                                          lower_name(i["name"]), i["record"].encode("utf-8")))
    groups = []
    for it in picked:
        if not groups or groups[-1]["slot"] != it["slot"]:
            groups.append({"slot": it["slot"], "items": [], "cw": 0, "ch": 0})
        g = groups[-1]
        g["items"].append(it)
        g["cw"] = max(g["cw"], it["footW"])
        g["ch"] = max(g["ch"], it["footH"])
    rec_lines, grp_lines = [], []
    for n, g in enumerate(groups):
        cols = max(1, HOST_COLS // max(1, g["cw"]))
        rows = max(1, HOST_ROWS // max(1, g["ch"]))
        g["label"], g["cols"], g["rows"] = label.get(g["slot"], g["slot"] or "?"), cols, rows
        grp_lines.append("G\t%d\t%s\t%d\t%d\t%d\t%d\t%d" % (n, g["label"], cols, rows, g["cw"] * CELL_PX,
                                                        g["ch"] * CELL_PX, len(g["items"])))
        for it in g["items"]:
            rec_lines.append(it["record"])
            grp_lines.append("E\t%s\t%d\t%d" % (it["record"], it["footW"], it["footH"]))
    exc_lines = ["%s\t%s" % (x["record"], x["reason"]) for x in excluded]
    for name, lines in (("uniq-records.txt", rec_lines), ("uniq-groups.txt", grp_lines),
                        ("uniq-excluded.txt", exc_lines)):
        with open(os.path.join(out_dir, name), "wb") as fh:
            fh.write("".join(l + "\r\n" for l in lines).encode("utf-8"))
    return groups


def write_stats(path, items, excluded, cands, groups, meta):
    L = []
    A = L.append
    A("# Catalogue statistics (Titan Quest AE 2.10)\n")
    A("Generated by `tools/build_catalogue.py` from the game's own database (set TQ_GAME_DIR, or let "
      "it find Steam).")
    A(f"Database: {meta['mergedRecords']} records; {meta['textTags']} text tags (Text_{meta['lang']}).\n")
    A(f"- collection rule (upper bound): **{len(cands)}** records "
      f"({sum(1 for c in cands if c['itemClassification'] == 'Epic')} Epic + "
      f"{sum(1 for c in cands if c['itemClassification'] == 'Legendary')} Legendary, "
      f"{sum(1 for c in cands if c['isRelic'])} artifacts)")
    A(f"- after EXCLUDE: **{len(items)}** records, {len(excluded)} dropped")
    A(f"- set pieces: {sum(1 for e in items if e['isSetPiece'])} in "
      f"{len({e['itemSetName'].lower() for e in items if e['itemSetName']})} sets\n")
    A("## Per expansion\n")
    A("| expansion | before EXCLUDE | after | dropped |")
    A("|---|---:|---:|---:|")
    for k, nm in EXPANSIONS.items():
        b = sum(1 for c in cands if c["expansion"] == k)
        a = sum(1 for c in items if c["expansion"] == k)
        A(f"| {nm} | {b} | {a} | {b - a} |")
    A(f"| **all** | **{len(cands)}** | **{len(items)}** | **{len(cands) - len(items)}** |\n")
    A("## Per group (uniq-groups.txt order)\n")
    A("| # | group | slot | records | Epic | Legendary | largest footprint | grid on the 16 x 15 sack |")
    A("|---:|---|---|---:|---:|---:|---|---|")
    for n, g in enumerate(groups):
        ep = sum(1 for i in g["items"] if i["itemClassification"] == "Epic")
        A(f"| {n} | {g['label']} | {g['slot']} | {len(g['items'])} | {ep} | {len(g['items']) - ep} | "
          f"{g['cw']}x{g['ch']} | {g['cols']} x {g['rows']} |")
    A("")
    A("## Footprints (cells)\n")
    fp = collections.Counter("%dx%d" % (i["footW"], i["footH"]) for i in items)
    A("| w x h | records |")
    A("|---|---:|")
    for k, v in sorted(fp.items()):
        A(f"| {k} | {v} |")
    A("")
    A("## Dropped by the EXCLUDE rule (uniq-excluded.txt)\n")
    A("| record | expansion | class | name | reason |")
    A("|---|---|---|---|---|")
    for x in excluded:
        A(f"| `{x['record']}` | {EXPANSIONS[x['expansion']]} | {x['class']} | {x['name']} | {x['reason']} |")
    A("")
    names = collections.defaultdict(list)
    for i in items:
        names[i["name"]].append(i["record"])
    shared = {k: v for k, v in names.items() if len(v) > 1}
    A("## Display names shared by several records (kept separate - decision 2)\n")
    A(f"{len(shared)} names, {sum(len(v) for v in shared.values())} records.\n")
    for k in sorted(shared):
        A(f"- {k}: " + ", ".join(f"`{r}`" for r in sorted(shared[k])))
    A("")
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(L) + "\n")


def compare(folder: str, oracle: str) -> int:
    rc = 0
    for name in LIST_FILES:
        a, b = os.path.join(folder, name), os.path.join(oracle, name)
        try:
            got, want = open(a, "rb").read(), open(b, "rb").read()
        except OSError as ex:
            print(f"FAIL {name}: {ex}")
            rc = 1
            continue
        if got == want:
            print(f"ok   {name}: byte-identical ({len(got)} B)")
            continue
        at = next((i for i in range(min(len(got), len(want))) if got[i] != want[i]), min(len(got), len(want)))
        print(f"FAIL {name}: differs at offset {at} (folder {len(got)} B, oracle {len(want)} B)")
        rc = 1
    print("ALL IDENTICAL" if rc == 0 else "DIFFERENCES")
    return rc


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--game", default=DEFAULT_GAME)
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--lang", default="EN")
    ap.add_argument("--compare", metavar="FOLDER", help="compare a mod folder's outputs with --out")
    args = ap.parse_args()
    if args.compare:
        return compare(args.compare, args.out)
    if not args.game:
        tqpath.game_dir()        # exits with the message
    items, excluded, cands, groups = build(args.game, args.out, args.lang)
    print(f"collection rule: {len(cands)} records; after EXCLUDE: {len(items)} "
          f"({len(excluded)} dropped) in {len(groups)} groups")
    for x in excluded:
        print(f"  dropped {x['record']}: {x['reason']}")
    print(f"wrote {args.out}: catalogue.json, catalogue-stats.md, uniq-records.txt, "
          f"uniq-groups.txt, uniq-excluded.txt (run pack_catalogue.py for catalogue.bin)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
