"""Reachability scan: which catalogue records can a player actually get?

    python reach_scan.py [--game <dir>] [--lang EN] [--witness] [--assert-rule]
                         [--write-placed <src/gen/placed_records.h>]

The roots are the database records the world places: every record path named inside
Resources\\Levels.arc (the one world) and the Quests.arc of the base game and of each
expansion folder (decompressed, scanned for records/...dbr). From there a breadth-first walk
follows every string field that names another record, EXCEPT the fields that never drop:
  * setMembers (an item set lists its members, it drops nothing);
  * an item record (Armor*, Weapon*, Item*, OneShot*, QuestItem) leads nowhere, but a
    formula (ItemArtifactFormula) leads to its artifactName (the result; reagents are inputs);
  * a worn slot (loot<Slot>Item<n>, defaultHeadPiece) of an Npc / Guard / NpcMerchant /
    NpcEnchanter / NpcSkillReallocator / Pet, or of a Monster with dropItems=0;
  * lootMisc<n>Item<n> of those non-dropper classes;
  * a path in a developer or test folder (_devcheat, devitem, sandbox, dev, test) is never entered.
Each reached record keeps one witness (the holder and the field, or the archive that places it).

It prints the counts and every catalogue record nobody can reach (with --witness, one path for
every reachable one too). With --assert-rule it runs tools/build_catalogue.py's collection with
the EXCLUDE rule 7 records kept and the monster infrequents admitted, and exits 1 unless the records that rule calls unobtainable are
exactly the ones this scan cannot reach (tools/build_test_catalogue.bat runs it when a game
folder is present). Reads the game folder only.

The relic and charm records of uniq-relics.txt take part as catalogue records.
The database alone cannot see what the world places: some set pieces are named only by their
set's setMembers and by a level file. --write-placed writes the catalogue records the level and
quest archives name directly as src/gen/placed_records.h, which both generators read (EXCLUDE
rule 7 never drops a placed record), and beside them the catalogue records this scan cannot reach
(kUnreachedRecords: a loot table nothing reaches is the only holder that could drop them; EXCLUDE
rule 7 drops them); rerun it after a game update when --assert-rule fails.
"""

from __future__ import annotations

import argparse
import collections
import os
import re
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import build_catalogue as bc  # noqa: E402
import tqarc  # noqa: E402
import tqarz as arz  # noqa: E402
import tqpath  # noqa: E402

ROOT_ARCHIVES = ("Resources\\Levels.arc", "Resources\\Quests.arc", "Resources\\xpack\\Quests.arc",
                 "Resources\\XPack2\\Quests.arc", "Resources\\XPack3\\Quests.arc",
                 "Resources\\XPack4\\Quests.arc")
RECORD_PATH = re.compile(rb"records[\\/][^\x00-\x1f\"<>|*?:]{1,240}?\.dbr", re.IGNORECASE)
SKIP_SEGMENTS = ("_devcheat", "devitem", "sandbox", "dev", "test")
ITEM_CLASS = re.compile(r"^(Armor|Weapon|Item|OneShot|QuestItem)")


def norm(p: str) -> str:
    return bc.lower_ascii(p.replace("\\", "/"))


def skipped(key: str) -> bool:
    return any(seg in SKIP_SEGMENTS for seg in key.split("/"))


def roots(game_dir: str, db):
    """record key -> the archive that names it (the first one, in ROOT_ARCHIVES order)."""
    found = {}
    for rel in ROOT_ARCHIVES:
        path = os.path.join(game_dir, rel)
        if not os.path.isfile(path):
            continue
        arc = tqarc.ArcArchive(path)
        for name in arc.keys():
            data = arc.read(name)
            if not data:
                continue
            for m in RECORD_PATH.finditer(data):
                key = norm(m.group(0).decode("latin-1"))
                if key not in found and db.get(key) is not None and not skipped(key):
                    found[key] = rel
    return found


def edges(rec, flds):
    """(target key, field) for every field of this record that can drop what it names."""
    cls = arz.sfield(rec, "Class")
    if ITEM_CLASS.match(cls) and cls != "ItemArtifactFormula":
        return
    drop_items = arz.ifield(rec, "dropItems", 1)
    for field, ftype, vals in flds:
        if ftype != arz.FT_STRING:
            continue
        if cls == "ItemArtifactFormula" and field != "artifactName":
            continue
        if bc.nondrop_kind(field, cls, drop_items):
            continue
        for v in vals:
            for part in v.split(";"):
                if len(part) >= 4 and bc.lower_ascii(part[-4:]) == ".dbr":
                    yield norm(part), field


def scan(game_dir: str):
    db = arz.ArzDatabase.load_game(game_dir)
    start = roots(game_dir, db)
    witness = {k: ("", rel) for k, rel in start.items()}
    queue = collections.deque(sorted(start))
    while queue:
        key = queue.popleft()
        rec = db.get(key)
        if rec is None:
            continue
        arc = db.archives[db._index[key]]
        for tgt, field in edges(rec, arc.fields(arc.entries[key])):
            if tgt in witness or skipped(tgt) or db.get(tgt) is None:
                continue
            witness[tgt] = (key, field)
            queue.append(tgt)
    return db, start, witness


def path_of(key: str, witness) -> str:
    chain, seen = [], set()
    while key in witness and key not in seen:
        seen.add(key)
        holder, field = witness[key]
        if not holder:
            chain.append(f"{key} <- placed by {field}")
            break
        chain.append(f"{key} <- {holder}.{field}")
        key = holder
    return "\n      ".join(chain)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--game", default=tqpath.find_game_dir() or "")
    ap.add_argument("--lang", default="EN")
    ap.add_argument("--witness", action="store_true", help="print a path for every reachable record")
    ap.add_argument("--write-placed", metavar="HEADER",
                    help="write the catalogue records the archives place directly as a C++ header")
    ap.add_argument("--assert-rule", action="store_true",
                    help="exit 1 unless EXCLUDE rule 7 drops exactly the unreachable records")
    args = ap.parse_args()
    if not args.game:
        tqpath.game_dir()        # exits with the message
    db, start, witness = scan(args.game)
    sink = {}
    with tempfile.TemporaryDirectory() as tmp:
        items, excluded, _c, _g = bc.build(args.game, tmp, args.lang, collect_unobtainable=True,
                                           collect_mi=True, relic_sink=sink)
    # the relic and charm records of uniq-relics.txt are checked and listed with the catalogue's
    items, excluded = items + sink["relics"], excluded + sink["excluded"]
    catalogue = {x["record"].lower(): x["record"] for x in items}
    rule7 = {x["record"].lower() for x in excluded if x["kept"]}
    reach = sorted(k for k in catalogue if k in witness)
    if args.write_placed:
        # a record no database field holds that a quest archive names (a quest's reward) is
        # "unreferenced" until the header names it: the header lists those candidates too (a
        # level's record table alone does not hand a record out)
        unref = {x["record"].lower() for x in excluded if x["reason"].startswith("unreferenced")}
        quest = {k for k in unref if k in start and start[k] != ROOT_ARCHIVES[0]}
        placed = sorted(k for k in set(catalogue) | quest if k in start)
        lines = ["// placed_records.h - written by tools/reach_scan.py --write-placed; do not edit by hand.",
                 "//",
                 "// The catalogue records that the game's level and quest archives (Resources\\Levels.arc and",
                 "// every Quests.arc) name directly. EXCLUDE rule 7 (catalogue_gen.h) never calls one of",
                 "// these unobtainable, nor rule 4 unreferenced: the world places them although no",
                 "// database field drops them.",
                 "// tools/build_catalogue.py reads the same list. %d records." % len(placed),
                 "#pragma once", "", "namespace gen {", "",
                 "static const char* const kPlacedRecords[] = {"]
        lines += ['    "%s",' % k for k in placed]
        unreached = sorted(k for k in catalogue if k not in witness)
        lines += ["};", "",
                  "// The catalogue records no path from the level and quest archives reaches (the monsters,",
                  "// chests, merchants and loot tables those places name, followed). EXCLUDE rule 7 drops one",
                  "// whose only dropping holders are loot tables nothing reaches. %d records." % len(unreached),
                  "static const char* const kUnreachedRecords[] = {"]
        lines += ['    "%s",' % k for k in unreached]
        lines += ["};", "", "} // namespace gen", ""]
        with open(args.write_placed, "w", encoding="utf-8", newline="\n") as fh:
            fh.write("\n".join(lines))
        print(f"reach: wrote {args.write_placed} ({len(placed)} placed catalogue records, "
              f"{len(unreached)} unreached)")
    unreach = sorted(k for k in catalogue if k not in witness)
    print(f"reach: {len(start)} records placed by the level and quest archives, {len(witness)} reached")
    print(f"reach: catalogue (rule 7 records kept) {len(catalogue)}: {len(reach)} reachable, "
          f"{len(unreach)} unreachable")
    for k in unreach:
        print(f"  unreachable {catalogue[k]}" + ("   (EXCLUDE rule 7)" if k in rule7 else "   (NOT rule 7)"))
    if args.witness:
        for k in reach:
            print(f"  reachable {path_of(k, witness)}")
    extra = sorted(rule7 - set(unreach))
    missing = sorted(set(unreach) - rule7)
    for k in extra:
        print(f"  rule 7 drops a reachable record: {path_of(k, witness)}")
    for k in missing:
        print(f"  unreachable but kept by rule 7: {catalogue[k]}")
    same = not extra and not missing
    print(f"reach: EXCLUDE rule 7 drops {len(rule7)}, the scan finds {len(unreach)} unreachable - "
          + ("the same records" if same else "DIFFERENT"))
    if args.assert_rule and not same:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
