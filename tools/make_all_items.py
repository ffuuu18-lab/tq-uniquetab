"""make_all_items - a format-1 collection file (`tq-uniq-items.jsonl`) in which EVERY record of
the catalogue is present once, with an identity the mod can hand back: a TAKE builds a real item
of that record through the same replica a deposit would have captured.

Port of the Grim Dawn mod's tool of the same name. The structure, the report, the determinism and
the carry-through of real rows are the same; what changes is what Titan Quest forces:

THE ROW OF A RECORD NOBODY DEPOSITED
------------------------------------
Grim Dawn copies a real 400-byte blob and changes its identity bytes. Titan Quest has no blob: a
row is the seven replica strings plus four numbers (src\\ut_rescue.cpp `appendRowLine` /
`rowFromKVs`), and the mod ALREADY builds a replica for every record it shows - the dim
prototype of the collection page (`utReplicaBuild`, src\\ut_proto.h). That recipe IS the
template, field for field, so a row of this file is what the mod would have written had that
prototype been deposited:

    record       the folded key (lower case, '\\' -> '/'): `utJournalKey` of `base`
    deposited    this run's UTC stamp (or --stamp); bookkeeping, the take does not read it
    stack        1 - one copy (`utJournalStack`)
    base         the catalogue record with '/' -> '\\' - `utReplicaBuild`'s own conversion, the
                 engine's spelling of a record in every replica it builds
    prefix, suffix, relic, relicBonus, relic2, relicBonus2
                 "" - `utReplicaBuild` leaves the six other strings empty
    seed         FNV-1a of the key, folded into 1..32767 (see THE SEED)
    var1         0 - `utReplicaBuild`; replica +0x80, the first relic's shard count
    var2         0 - `utReplicaBuild`; replica +0xB4, the SECOND relic's shard count: the Item
                 constructor zeroes it and only ItemEquipment::AddSecondRelic stores into it, so
                 it is no roll and no level input. With relic2 empty the engine never uses it (a
                 loaded item without a second relic carries stack garbage there)
    b8           0 - `utReplicaBuild`; 0 on every replica the engine was seen to build

THE SEED. The prototype passes 0 and lets the engine roll one; a row cannot, because the row is
compared with the live item afterwards (a pending take is settled by finding the item with the
row's exact identity, seed included), so the value the row names must be the one the item gets.
Non-zero, derived from the key (FNV-1a, 32 bit, folded into 1..32767 - the range of the seeds the
engine rolls: a 15-bit random value), so a re-run is byte-identical apart from the stamps.

A --journal is carried through BYTE FOR BYTE: every row the mod itself would read is copied as it
is, and a record that already has a copy in it is not synthesised again. Rows the reader would
drop are NOT copied (the mod would drop them on its own); they are listed in the report.

Nothing here writes outside the folder of --out. The journal given is only read.
"""

from __future__ import annotations

import argparse
import datetime
import json
import os
import sys

JOURNAL_NAME = "titan quest uniquetab"
JOURNAL_FORMAT = 1
DEFAULT_SET = "tq-uniq-items"
STR_KEYS = ["base", "prefix", "suffix", "relic", "relicBonus", "relic2", "relicBonus2"]
NUM_KEYS = ["seed", "var1", "var2", "b8", "stack"]
ID_STR_MAX = 260            # kUtIdStrMax: a string with its NUL
REPLICA_MAX_BASE = 259      # kUtReplicaMaxBase
SEED_RANGE = 0x7FFF         # seeds 1..32767


def fold(base: str) -> str:
    """utJournalKey / utOwnedNormaliseKey: ASCII lower case, '\\' -> '/'."""
    return "".join(("/" if c == "\\" else (c.lower() if "A" <= c <= "Z" else c)) for c in base)


def seed_for(key: str) -> int:
    """FNV-1a (32 bit) over the folded key, folded into 1..32767 - never 0."""
    h = 0x811C9DC5
    for b in key.encode("ascii"):
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return (h % SEED_RANGE) + 1


def json_str(s: str) -> str:
    """ut_rescue.cpp `jsonEscapeTo`, byte for byte."""
    out = ['"']
    for ch in s:
        o = ord(ch)
        if ch == '"':
            out.append('\\"')
        elif ch == "\\":
            out.append("\\\\")
        elif ch == "\b":
            out.append("\\b")
        elif ch == "\f":
            out.append("\\f")
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\r":
            out.append("\\r")
        elif ch == "\t":
            out.append("\\t")
        elif o < 0x20:
            out.append("\\u%04X" % o)
        else:
            out.append(ch)
    out.append('"')
    return "".join(out)


def printable(s: str) -> bool:
    return all(0x20 <= ord(c) < 0x7F for c in s)


def row_problem(obj) -> str | None:
    """`rowFromKVs`: why the mod would DROP this row, or None when it reads it.

    One difference is left on purpose: Python's json refuses a number spelled with a leading zero
    (`01`), which the mod's jsonU32 reads; such a line is reported as "does not parse" and left
    out. Only a hand-edited file has one."""
    if not isinstance(obj, dict):
        return "not a flat object"
    if not isinstance(obj.get("record"), str):
        return 'no "record"'
    for k in STR_KEYS:
        v = obj.get(k)
        if not isinstance(v, str):
            return 'no "%s"' % k
        if len(v.encode("utf-8")) >= ID_STR_MAX or not printable(v):
            return '"%s" is too long or not printable ASCII' % k
    # utJournalKey folds `base` into a 256-byte key buffer: a longer base has no key and the mod
    # drops the row (the longest catalogue base is 87 characters)
    if len(obj["base"].encode("utf-8")) > 255:
        return '"base" is longer than 255 characters (the key buffer)'
    for k in NUM_KEYS:
        v = obj.get(k)
        if isinstance(v, bool) or not isinstance(v, int) or not 0 <= v <= 0xFFFFFFFF:
            return 'a number (%s) is missing or not a plain number' % k
    if obj["b8"] > 255:
        return "b8 out of range"
    if not obj["base"] or fold(obj["base"]) != obj["record"]:
        return '"record" is not the folded "base"'
    p = obj.get("pending")
    if p is not None and p not in ("in", "out"):
        return '"pending" is neither "in" nor "out"'
    return None


def synth_row(record: str, stamp: str) -> dict:
    """The prototype recipe (utReplicaBuild) as a row, for the catalogue record `record`."""
    base = record.replace("/", "\\")
    key = fold(base)
    row = {"record": key, "deposited": stamp, "stack": 1}
    row["base"] = base
    for k in STR_KEYS[1:]:
        row[k] = ""
    row.update({"seed": seed_for(key), "var1": 0, "var2": 0, "b8": 0})
    return row


def row_line(row: dict) -> str:
    """`appendRowLine`'s key order: record, deposited, stack, the seven strings, seed, var1,
    var2, b8 (no pending: a generated row is settled)."""
    parts = ["{%s:%s" % (json_str("record"), json_str(row["record"])),
             "%s:%s" % (json_str("deposited"), json_str(row["deposited"])),
             "%s:%u" % (json_str("stack"), row["stack"])]
    for k in STR_KEYS:
        parts.append("%s:%s" % (json_str(k), json_str(row[k])))
    for k in ("seed", "var1", "var2", "b8"):
        parts.append("%s:%u" % (json_str(k), row[k]))
    return ",".join(parts) + "}"


def header_line(written: str, set_leaf: str, entries: int, collected: int, pin: int,
                pout: int) -> str:
    """`buildText`'s header, byte for byte."""
    return ('{"journal":"%s","format":%u,"written":"%s","set":"%s","entries":%u,"collected":%u,'
            '"pendingIn":%u,"pendingOut":%u}' % (JOURNAL_NAME, JOURNAL_FORMAT, written, set_leaf,
                                                  entries, collected, pin, pout))


def read_journal(path: str):
    """A real journal, read only: (set leaf or None, [(line bytes, row)], [(line no, why)])."""
    with open(path, "rb") as fh:
        data = fh.read()
    lines = data.split(b"\n")
    set_leaf, kept, dropped = None, [], []
    header_seen = False
    for n, raw in enumerate(lines, 1):
        line = raw.rstrip(b"\r")
        if not line.strip(b" \t"):
            continue
        try:
            obj = json.loads(line.decode("utf-8"))
        except (ValueError, UnicodeDecodeError) as exc:
            if not header_seen:
                raise SystemExit("%s: line 1 is not a JSON object (%s)" % (path, exc))
            dropped.append((n, "does not parse"))
            continue
        if not header_seen:
            header_seen = True
            if not isinstance(obj, dict) or obj.get("journal") != JOURNAL_NAME:
                raise SystemExit('%s: line 1 does not name "%s"' % (path, JOURNAL_NAME))
            if obj.get("format") != JOURNAL_FORMAT:
                raise SystemExit("%s: format %r, this tool writes format %d"
                                 % (path, obj.get("format"), JOURNAL_FORMAT))
            if isinstance(obj.get("set"), str) and obj["set"]:
                set_leaf = obj["set"]
            continue
        why = row_problem(obj)
        if why:
            dropped.append((n, why))
            continue
        kept.append((line, obj))
    return set_leaf, kept, dropped, len(data)


def read_classes(path: str | None) -> dict[str, str]:
    """catalogue.json -> {record key: the item class} (a census only; no display text is read)."""
    if not path or not os.path.exists(path):
        return {}
    with open(path, "r", encoding="utf-8") as fh:
        cat = json.load(fh)
    return {fold(it["record"]): it.get("class") or "?" for it in cat.get("items", [])}


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(prog="make_all_items", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--records", required=True, help="uniq-records.txt (the catalogue order)")
    ap.add_argument("--catalogue", default=None,
                    help="catalogue.json - optional; only the report's per-class census uses it")
    ap.add_argument("--journal", default=None,
                    help="a real collection file whose rows are carried through (read only)")
    ap.add_argument("--out", required=True, help="the .jsonl to write")
    ap.add_argument("--report", default=None, help="the report (default: <out>.report.txt)")
    ap.add_argument("--set", default=None,
                    help='the header\'s "set" (default: the journal\'s, else "%s")' % DEFAULT_SET)
    ap.add_argument("--stamp", default=None,
                    help="the ISO stamp for \"written\" / \"deposited\" (default: now, UTC)")
    a = ap.parse_args(argv)

    report_path = a.report or os.path.splitext(a.out)[0] + ".report.txt"
    stamp = a.stamp or datetime.datetime.now(datetime.timezone.utc).strftime(
        "%Y-%m-%dT%H:%M:%S.0000000Z")

    with open(a.records, "r", encoding="utf-8") as fh:
        records = [ln.strip() for ln in fh if ln.strip()]
    classes = read_classes(a.catalogue)

    set_leaf, kept, dropped, src_bytes = (None, [], [], 0)
    if a.journal:
        set_leaf, kept, dropped, src_bytes = read_journal(a.journal)
    set_leaf = a.set or set_leaf or DEFAULT_SET

    have = {obj["record"] for _line, obj in kept if obj.get("pending") != "out"}
    added, refused, by_class, seen = [], [], {}, set()
    for rec in records:
        key = fold(rec)
        if key in seen:
            refused.append((rec, "the record list names it twice"))
            continue
        seen.add(key)
        if key in have:
            continue
        if len(rec) > REPLICA_MAX_BASE or not printable(rec) or not rec:
            refused.append((rec, "too long or not printable - utReplicaBuild refuses it"))
            continue
        row = synth_row(rec, stamp)
        why = row_problem(row)
        if why:
            refused.append((rec, why))
            continue
        added.append(row)
        cls = classes.get(key, "(no catalogue)" if not classes else "(not in the catalogue)")
        by_class[cls] = by_class.get(cls, 0) + 1

    pin = sum(1 for _l, o in kept if o.get("pending") == "in")
    pout = sum(1 for _l, o in kept if o.get("pending") == "out")
    entries = len(kept) + len(added)
    collected = entries - pout
    out_lines = [header_line(stamp, set_leaf, entries, collected, pin, pout).encode("ascii")]
    out_lines += [line for line, _o in kept]
    out_lines += [row_line(r).encode("ascii") for r in added]
    blob = b"\n".join(out_lines) + b"\n"

    # Self-check: every line this tool wrote reads back as the row it meant.
    for r in added[:1] + added[-1:]:
        if json.loads(row_line(r)) != r:
            raise SystemExit("internal: a synthetic row does not read back")
    seeds = [r["seed"] for r in added]
    if any(not 1 <= s <= SEED_RANGE for s in seeds):
        raise SystemExit("internal: a seed out of 1..%d" % SEED_RANGE)

    out_dir = os.path.dirname(os.path.abspath(a.out))
    os.makedirs(out_dir, exist_ok=True)
    with open(a.out, "wb") as fh:
        fh.write(blob)

    rep = []
    rep.append("%s - every record of the catalogue, one copy each" % os.path.basename(a.out))
    rep.append("written %s" % stamp)
    rep.append("")
    rep.append("rows         %d = %d carried + %d generated" % (entries, len(kept), len(added)))
    rep.append("header       entries %d, collected %d, pendingIn %d, pendingOut %d, set \"%s\""
               % (entries, collected, pin, pout, set_leaf))
    rep.append("records      %s (%d records)" % (os.path.basename(a.records), len(records)))
    if a.journal:
        rep.append("journal      %s (%d bytes): %d rows carried through byte for byte, %d rows "
                   "the mod would drop left out" % (os.path.basename(a.journal), src_bytes,
                                                    len(kept), len(dropped)))
        for n, why in dropped:
            rep.append("               line %d: %s" % (n, why))
    else:
        rep.append("journal      none - every row is generated")
    rep.append("output       %s (%d bytes)" % (os.path.basename(a.out), len(blob)))
    rep.append("")
    rep.append("THE ROW OF A GENERATED RECORD - the collection page's prototype recipe")
    rep.append("(utReplicaBuild), so it is what the mod writes when that prototype is deposited:")
    rep.append("  record          the folded key: lower case, '\\' -> '/'")
    rep.append("  deposited       this run's stamp")
    rep.append("  stack           1")
    rep.append("  base            the record with '/' -> '\\' (the engine's spelling)")
    rep.append("  prefix, suffix, relic, relicBonus, relic2, relicBonus2    empty")
    rep.append("  seed            FNV-1a of the key folded into 1..32767 (deterministic, never 0)")
    rep.append("  var1, var2, b8  0 (var2 = the second relic's shard count; no second relic)")
    if seeds:
        rep.append("  seeds used      %d distinct among %d rows, %d..%d"
                   % (len(set(seeds)), len(seeds), min(seeds), max(seeds)))
    rep.append("")
    rep.append("PER ITEM CLASS (generated rows)")
    for cls in sorted(by_class):
        rep.append("  %-40s %5d" % (cls, by_class[cls]))
    rep.append("  %-40s %5d" % ("TOTAL", len(added)))
    rep.append("")
    if refused:
        rep.append("RECORDS NOT GENERATED (%d)" % len(refused))
        for rec, why in refused:
            rep.append("  %-70s %s" % (rec, why))
    else:
        rep.append("RECORDS NOT GENERATED: none. Every record of the list is in the file.")
    text = "\n".join(rep) + "\n"
    with open(report_path, "w", encoding="ascii", newline="\n") as fh:
        fh.write(text)
    print(text, end="")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
