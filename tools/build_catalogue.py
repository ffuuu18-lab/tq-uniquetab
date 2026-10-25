"""Reference implementation; the DLL generates these files itself (src/gen/catalogue_gen).

Build data/oracle/catalogue.json + catalogue-stats.md and the three list fixtures
(uniq-records.txt, uniq-groups.txt, uniq-excluded.txt) from Titan Quest AE's own database.

    python build_catalogue.py [--game <dir>] [--out <dir>] [--lang EN] [--collect-unobtainable]
                              [--collect-mi]
    python build_catalogue.py --compare <folder>     the files the DLL wrote into <folder>
                                                     (catalogue.bin and the three lists)
                                                     against data/oracle, byte for byte

Nothing is written outside <out>. tools/pack_catalogue.py packs catalogue.json into
catalogue.bin; the C++ generator writes all four and tools/test_catalogue.cpp (--ensure)
compares them with these files byte for byte.

THE COLLECTION RULE (Titan Quest):
  * equipment (the EQUIP_SLOT classes - ArmorProtective_* / ArmorJewelry_* / Weapon*_*) and
    artifacts (Class ItemArtifact) whose itemClassification is Epic or Legendary, and the
    arcane formulae (Class ItemArtifactFormula, BLUEPRINT_CLASSES) at EVERY classification
    (Common, Epic, Legendary, Quest),
  * anywhere in the database but a creature folder (a path component `creature` or
    `creatures`: the gear NPCs and monsters wear), in the base game and a custom game alike -
    GD's junk-folder rule for every layer; the real filters are the EXCLUDE rule's,
  * whose name tag (itemNameTag; artifacts and formulae: description) resolves in
    Text_<lang>.arc (a formula whose own tag does not resolve passes when its crafted
    artifact's does),
  * that are no template: "blank", "test", "template" or "copy of" as a whole word (no letter
    on either side) in the FileDescription or the path (GD's rule 4, "no BLANK", widened to the
    words TQ's templates use) drops a record only when nothing hands it out - no other record
    holds it in a field that can drop it and the level and quest archives do not place it.
  -> 1,715 records on AE 2.10 (CANDIDATES_EXPECT). That is an UPPER BOUND.
  A formula's icon is its artifactFormulaBitmapName (the 64 x 32 recipe scroll, 2 x 1 cells); it
  carries "isBlueprint" (catalogue.bin flag bit 1, pack_catalogue.py F_BLUEPRINT) at every
  classification, and catalogue.bin packs a Common formula's classification byte as Epic (the
  true word stays in catalogue.json). The group is `blueprint` / BLU, after the Artifacts.

MONSTER INFREQUENTS (--collect-mi = uniquetab.ini's collect_mi=1, the DLL's default; this tool's
default stays off, so the flag-off cut needs no switch): every EQUIP_SLOT record whose own
itemClassification is Rare - the engine's monster-infrequent rule; TQ has no Rare rings, amulets
or artifacts - through the same path / name / template filter and the same EXCLUDE rule:
is_mi_candidate, catalogue_gen.cpp isMiCandidate. 1,118 records on AE 2.10, counted with or
without the flag (MI_CANDIDATES_EXPECT). Each entry carries "isMi" (the KIND mark; catalogue.bin
flag bit 8, pack_catalogue.py F_MI); the lists put them after the uniques of their group (the
rank Rare), catalogue.bin after the uniques of its group (the KIND before the level).

THE EXCLUDE RULE, first reason that applies, every dropped record listed in uniq-excluded.txt:
  1. the expansion is not installed (its Resources folder is missing),
  2. no icon field at all (bitmap / artifactBitmap / relicBitmap),
  3. a developer folder in the path (_devcheat, devitem, sandbox, dev), unless the reach walk
     (rule 7) reaches it from a holder outside those folders,
  4. unreferenced: no OTHER record holds the path in any string field - no loot table,
     merchant, container, formula, quest record or monster (a scan of every string field),
  5. the icon is not in the item archives,
  6. the footprint (icon pixels / 32) is outside 1..2 x 1..5 cells,
  7. unobtainable: other records hold the path, but none in a field that can drop it. A
     reference does not count from `setMembers` (a set lists its members), from a formula's
     `reagent*` fields (inputs), from a worn slot (loot<Slot>Item<n>, defaultHeadPiece) of an
     Npc / Guard / NpcMerchant / NpcEnchanter / NpcSkillReallocator / Pet, from a worn slot of a
     Monster with dropItems=0, or from lootMisc<n>Item<n> of those non-dropper classes. A
     record the game's level or quest archives place directly (src/gen/placed_records.h, written
     by tools/reach_scan.py --write-placed) is never unobtainable, and never unreferenced (the
     archive is its holder). The reason names the holder kinds. With --collect-unobtainable (the ini's collect_unobtainable=1) these records stay in
     the catalogue and uniq-excluded.txt still lists them, marked kept. tools/reach_scan.py
     checks this rule against a reachability scan of the level and quest archives. A record
     that scan cannot reach although a database field could drop it (it sits only in loot
     tables nothing reaches; placed_records.h kUnreachedRecords) is unobtainable too. Outside the
     item folders (records/(xpackN/)item(s)/), where a custom game's records have no such list,
     the reach walk decides: it starts at every record that is not a loot table (Class Loot*; a
     monster, a chest, a merchant, a formula) and follows the fields that can drop down through
     the loot tables; a holder in a developer folder or with a template word in its path is
     never entered. A record there that the walk does not reach (only a loot table nothing
     names holds it: a master table no monster or chest uses) is unobtainable. The walk sees
     the database only, not the loot tables a quest file names (the base game's quest rewards),
     so inside the item folders it does not decide. A record in a developer folder that the
     walk reaches is collected (rule 3 does not drop it).
THE RELICS AND CHARMS (uniq-relics.txt; relic_order, relics_text - src/gen/relics_gen.cpp):
  every ItemRelic / ItemCharm record (the class decides relic or charm) under the same filter
  (no creature folder, a name tag that resolves, the template rule) whose completion bonus table
  (bonusTableName) names at least one bonus of non-zero weight, through the same EXCLUDE rule
  (its icon is relicBitmap, its footprint must be 1 x 1 cells). The records the EXCLUDE rule
  leaves out are the file's X lines; uniq-excluded.txt does not list them, and no group, page or
  uniq-records.txt line takes a relic or a charm.
Two decisions (HANDOFF.md section 6): the records held only by monsters STAY
(monsters drop what they hold), and the records that share a display name STAY separate
(distinct records, distinct drops).
"""

from __future__ import annotations

import argparse
import collections
import errno
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
    "ItemArtifactFormula": "blueprint",
}
BLUEPRINT_CLASSES = ("ItemArtifactFormula",)
RELIC_CLASS = "ItemArtifact"
EQUIP_FAMILY = re.compile(r"^(ArmorProtective|ArmorJewelry|Weapon[A-Za-z]*)_")
DEV_FOLDERS = ("_devcheat", "devitem", "sandbox", "dev")
TEMPLATE_WORDS = ("blank", "test", "template", "copy of")
TEMPLATE_WORD = re.compile(r"(?<![a-z])(blank|test|template|copy of)(?![a-z])")
JUNK_PARTS = ("creature", "creatures")
ITEM_PATH = re.compile(r"^records/(xpack\d?/)?items?/")    # where the reach walk does not decide
EXPANSIONS = {0: "base", 1: "IT", 2: "Ragnarok", 3: "Atlantis", 4: "EternalEmbers"}
EXPANSION_FOLDER = {1: "xpack", 2: "XPack2", 3: "XPack3", 4: "XPack4"}

CANDIDATES_EXPECT = 1715        # the collection rule on AE 2.10, before EXCLUDE (uniques and formulae)
MI_CANDIDATES_EXPECT = 1118     # the monster infrequents the same filter passes, before EXCLUDE

# EXCLUDE rule 7: the classes whose worn gear and misc loot never drop, and the worn slots
NON_DROPPERS = ("Npc", "Guard", "NpcMerchant", "NpcEnchanter", "NpcSkillReallocator", "Pet")
WORN_SLOTS = ("Head", "Torso", "Forearm", "LowerBody", "RightHand", "LeftHand", "Finger", "Neck")
UNOBTAINABLE = "unobtainable: "
UNREACHED = "only in loot tables nothing reaches"
KEPT = " (kept: collect_unobtainable=1)"
PLACED_HEADER = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                             "src", "gen", "placed_records.h")


def load_placed(path: str = PLACED_HEADER, array: str = "kPlacedRecords") -> set:
    """The record paths one array of src/gen/placed_records.h lists: kPlacedRecords (the level and
    quest archives place them) or kUnreachedRecords (no path reaches them). The C++ generator
    compiles the same lists."""
    with open(path, encoding="utf-8") as fh:
        body = fh.read().split(array + "[] = {", 1)[1].split("};", 1)[0]
    return {lower_ascii(m) for m in re.findall(r'^\s*"(records/[^"]+)",\s*$', body, re.M)}

# the list files: GD's pages_gen order and labels (TQ's labels on the slots TQ has), and each
# group's G line id and tag - the twin of the built-in table in src\gen\ut_groups.h, entry for
# entry, as (id, label, tag, slot): the id is GD's slot id except the Artifacts' (artifact, TQ's
# own word: a relic is a different item in TQ); the tag is GD's where the group is GD's own (same
# id, same label), else GD's tagFrom of TQ's label, except the arcane formulae's: GD's blueprint
# id and its BLU tag (GD's Blueprints group) under TQ's label; the slot is the catalogue's slot word
SLOT_ORDER = [
    ("head", "Helms", "HEL", "head"), ("shoulders", "Shoulders", "SHO", "shoulders"),
    ("chest", "Torso", "TOR", "chest"), ("hands", "Arms", "ARM", "hands"),
    ("waist", "Belts", "BEL", "waist"), ("legs", "Legs", "LEG", "legs"), ("feet", "Boots", "BOO", "feet"),
    ("amulet", "Amulets", "AMU", "amulet"), ("medal", "Medals", "MED", "medal"),
    ("ring", "Rings", "RNG", "ring"), ("offhand", "Off-hands", "OFF", "offhand"),
    ("shield", "Shields", "SHD", "shield"), ("axe1h", "Axes", "AXE", "axe1h"),
    ("dagger", "Daggers", "DAG", "dagger"), ("mace1h", "Maces", "MAC", "mace1h"),
    ("scepter", "Staves", "STA", "scepter"), ("sword1h", "Swords", "SWO", "sword1h"),
    ("ranged1h", "Throwing", "THR", "ranged1h"), ("axe2h", "2H Axes", "AX2", "axe2h"),
    ("mace2h", "2H Maces", "MC2", "mace2h"), ("spear2h", "Spears", "SPR", "spear2h"),
    ("sword2h", "2H Swords", "SW2", "sword2h"), ("ranged2h", "Bows", "BOW", "ranged2h"),
    ("artifact", "Artifacts", "ART", "relic"),
    ("blueprint", "Arcane Formulae", "BLU", "blueprint"),
]
ID_MAX = 40


def valid_id(s):
    """GD's validId: 1..40 of a-z, 0-9, '_', '.', '-'."""
    return 0 < len(s) <= ID_MAX and all(c in "abcdefghijklmnopqrstuvwxyz0123456789_.-" for c in s)


def tag_from(text):
    """GD's tagFrom: the first three letters or digits, upper case, padded with X."""
    t = ""
    for c in text:
        if len(t) == 3:
            break
        if "a" <= c <= "z":
            t += c.upper()
        elif "A" <= c <= "Z" or "0" <= c <= "9":
            t += c
    return t + "X" * (3 - len(t))


# ---- the groups as data: a port of src/gen/ut_groups.h (parser, matcher, auto groups, merge)
GROUPS_TEMPLATE_H = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                                 "src", "gen", "ut_groups_default.h")
COLLECT_MI_VIEW_KEY = "collect_mi"


def template_text():
    """The built-in groups (the DLL's defaultGroupsIni). It lives in ONE place - the raw string
    of src/gen/ut_groups_default.h, which the DLL compiles - and is read from there."""
    src = open(GROUPS_TEMPLATE_H, encoding="utf-8").read()
    a = src.index('R"UTG(') + len('R"UTG(')
    return src[a:src.index(')UTG"', a)]


BUILTIN = [(i, l, t) for i, l, t, _s in SLOT_ORDER]
FLAG_BITS = {"set": 1, "relic": 2, "equipment": 4, "faction": 8, "blueprint": 16,
             "augment": 32, "extra": 64}
ID_CHARS = set("abcdefghijklmnopqrstuvwxyz0123456789_.-")
# The engine's type word per item class (utg::classTypeTags): none is measured for Titan Quest.
CLASS_TYPE_TAG = {}
LABEL_MAX = 48
TAG_ALT = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"


def low_ascii(s):
    return "".join(chr(ord(c) + 32) if "A" <= c <= "Z" else c for c in s)


def cap(w):
    return w[:1].upper() + w[1:] if w[:1].isascii() and w[:1].islower() else w


def class_last_part(cls):
    return cls.rsplit("_", 1)[-1]


def class_family_word(cls):
    """utg::classFamilyWord: the part before the first '_' without Weapon/Armor/Item, capitalised."""
    u = cls.find("_")
    if u <= 0:
        return ""
    f = cls[:u]
    for p in ("weapon", "armor", "item"):
        if len(f) > len(p) and low_ascii(f).startswith(p):
            f = f[len(p):]
            break
    return cap(f)


def valid_label(s):
    b = s.encode("utf-8")
    return 0 < len(b) <= LABEL_MAX and all(c >= 0x20 and c != 0x7F for c in b)


def clean_word(text):
    """utg::cleanWord: colour codes and a leading gender marker removed, blanks trimmed."""
    b = text.encode("utf-8")
    o = bytearray()
    i = 0
    while i < len(b):
        if b[i:i + 1] == b"{" and i + 3 < len(b) and b[i + 1:i + 2] == b"^" and b[i + 3:i + 4] == b"}":
            i += 4
            continue
        if b[i:i + 1] == b"^" and i + 1 < len(b):
            i += 2
            continue
        o.append(0x20 if (b[i] < 0x20 or b[i] == 0x7F) else b[i])
        i += 1
    o = bytes(o).strip(b" ")
    if len(o) > 2 and o[:1] == b"[":
        close = o.find(b"]")
        if 2 <= close <= 4 and all(chr(c).isascii() and chr(c).isalpha() for c in o[1:close]):
            o = o[close + 1:].lstrip(b" ")
    o = o[:LABEL_MAX].rstrip(b" ")
    w = o.decode("utf-8", "replace")
    return w if valid_label(w) and w.encode("utf-8") == o else ""


def plural_word(w):
    """utg::pluralWord."""
    if not w or not (w[-1].isascii() and w[-1].isalpha()):
        return w
    z, y = w[-1].lower(), (w[-2].lower() if len(w) >= 2 else " ")
    if z == "f" and y == "f":
        p = w[:-2] + "ves"
    elif z in "sx" or (z == "h" and y in "cs"):
        p = w + "es"
    elif z == "y" and len(w) >= 2 and w[-2].isascii() and w[-2].isalpha() and y not in "aeiou":
        p = w[:-1] + "ies"
    else:
        p = w + "s"
    return w if len(p.encode("utf-8")) > LABEL_MAX else p


def tag_candidates(word):
    """utg::tagCandidates: three letters, the letter + two consonants, two letters + 0-9 A-Z."""
    t1 = tag_from(word)
    c = [t1]
    letters = [ch.upper() for ch in word if ch.isascii() and ch.isalpha()]
    if letters:
        t2 = letters[0] + "".join(ch for ch in letters[1:] if ch not in "AEIOU")[:2]
        if len(t2) == 3:
            c.append(t2)
    c += [t1[:2] + k for k in TAG_ALT]
    return c


def resolve_names(words, groups, used):
    """utg::resolveNames: the derived labels and tags of the groups that received a record.
    groups: [id, label, tag, labelDerived, tagDerived, cls] lists, changed in place."""
    special = lambda d: d[0] == "other"
    tags, labels = set(), set()
    for d, u in zip(groups, used):
        if not u or special(d):
            continue
        if not d[4]:
            tags.add(d[2])
        if not d[3]:
            labels.add(low_ascii(d[1]))
    free = lambda t: t not in tags and t not in ("MAT", "OWN")
    for d, u in zip(groups, used):
        if not u:
            continue
        if special(d):
            t = d[2]
            for k in TAG_ALT:
                if free(t):
                    break
                t = t[:2] + k
            d[2] = t
            tags.add(t)
            continue
        word = ""
        if d[3]:
            if d[5] and d[5] in CLASS_TYPE_TAG:
                word = clean_word(words.get(d[5], ""))
                if not word:
                    word = clean_word(cap(class_last_part(d[5])))
            if word:
                label = plural_word(word)
                if low_ascii(label) in labels:
                    fam = class_family_word(d[5])
                    if fam and len(fam) + 1 + len(label) <= LABEL_MAX:
                        label = fam + " " + label
                    stem = label
                    n = 2
                    while low_ascii(label) in labels and n < 100:
                        sfx = " %d" % n
                        label = stem[:LABEL_MAX - len(sfx)] + sfx
                        n += 1
                d[1] = label
            labels.add(low_ascii(d[1]))
        if d[4]:
            src = word or d[1]
            cand = [] if word else [d[2]]
            if not any(ch.isascii() and ch.isalnum() for ch in src):
                src = class_last_part(d[5]) if d[5] else d[0].rsplit(".", 1)[-1]
            cand += tag_candidates(src)
            for c in cand:
                if free(c):
                    d[2] = c
                    break
            tags.add(d[2])


def _strip_comment(s):
    for i in range(1, len(s)):
        if s[i] in ";#" and s[i - 1] in " \t":
            return s[:i]
    return s


def parse_groups(text):
    """uniquetab-groups.ini -> {"groups": [[id, label, tag, ...]], "rules": [...], "auto", "min"}.
    Raises ValueError("line N: ...") on the first error, like utg::parseGroups."""
    if text.startswith("\ufeff"):
        text = text[1:]
    gs = {"groups": [], "rules": [], "auto": True, "min": 5, "autoSet": False, "minSet": False,
          "words": {}}
    index, view, section, cur, block_keys = {}, "", None, None, set()

    def fail(n, what):
        raise ValueError("line %d: %s" % (n, what))

    def close():
        if cur is not None:
            if not cur["filter"]:
                fail(cur["line"], ("[exclude]" if cur["exclude"] else "the [group] block")
                     + " has no filter (class, path, rank, flag or source)")
            if not cur["exclude"] and cur["classes"] and not gs["groups"][cur["group"]][8]:
                gs["groups"][cur["group"]][8] = cur["classes"][0]
            gs["rules"].append(cur)

    for n, raw in enumerate(text.split("\n"), 1):
        line = raw.rstrip("\r").strip(" \t")
        if not line or line[0] in ";#":
            continue
        if line[0] == "[":
            line = _strip_comment(line).strip(" \t")
            if not line.endswith("]"):
                fail(n, "a section header must end with ']'")
            close()
            cur, block_keys = None, set()
            h = line[1:-1].strip(" \t")
            if h.startswith("view:"):
                k = h[5:].strip(" \t")
                if k != "*" and not (0 < len(k) <= 80 and set(k) <= ID_CHARS | {"+"}):
                    fail(n, 'bad view key "%s"' % k)
                view, section = ("" if k == "*" else k), None
            elif h == "exclude":
                section = "rule"
                cur = {"exclude": True, "group": -1, "view": view, "line": n, "filter": False,
                       "classes": [], "paths": [], "ranks": [], "flags": 0, "source": "any"}
            elif h == "auto":
                if view:
                    fail(n, "[auto] applies to every view; put it before any [view:...]")
                section = "auto"
            elif h.startswith("group "):
                gid = h[6:].strip(" \t")
                if not (0 < len(gid) <= 40 and set(gid) <= ID_CHARS):
                    fail(n, 'bad group id "%s" (a-z 0-9 _ . -, 1..40)' % gid)
                if gid in ("vanilla", "owned", "next", "other", RELIC_GROUP_ID) or gid.startswith("auto."):
                    fail(n, 'the group id "%s" is reserved' % gid)
                if gid not in index:
                    index[gid] = len(gs["groups"])
                    # id, label, tag, line, labelSet, tagSet, labelDerived, tagDerived, cls
                    gs["groups"].append([gid, "", "", n, False, False, False, False, ""])
                section = "rule"
                cur = {"exclude": False, "group": index[gid], "view": view, "line": n,
                       "filter": False, "classes": [], "paths": [], "ranks": [], "flags": 0,
                       "source": "any"}
            else:
                fail(n, "unknown section [%s]" % h)
            continue
        if "=" not in line:
            fail(n, "expected key = value")
        key, val = line.split("=", 1)
        key, val = key.strip(" \t").lower(), _strip_comment(val).strip(" \t")
        if section is None:
            fail(n, '"%s" is outside a [group], [exclude] or [auto] block' % key)
        if section == "auto":
            if key == "min":
                if not val.isdigit() or not 1 <= int(val) <= 1000:
                    fail(n, "min must be 1..1000")
                gs["min"] = int(val)
                gs["minSet"] = True
            elif key == "enabled":
                v = val.lower()
                if v not in ("yes", "1", "true", "on", "no", "0", "false", "off"):
                    fail(n, "enabled must be yes or no")
                gs["auto"] = v in ("yes", "1", "true", "on")
                gs["autoSet"] = True
            else:
                fail(n, 'unknown key "%s" in [auto]' % key)
            continue
        if key in ("label", "tag"):
            if cur["exclude"]:
                fail(n, key + " belongs to a [group] block")
            if key in block_keys:
                fail(n, key + " given twice")
            block_keys.add(key)
            d = gs["groups"][cur["group"]]
            slot = 1 if key == "label" else 2
            if key == "label" and not (0 < len(val.encode("utf-8")) <= 48
                                       and all(ord(c) >= 0x20 and ord(c) != 0x7F for c in val)):
                fail(n, "a label is 1..48 bytes, no tab")
            if key == "tag" and not (len(val) == 3 and all(0x21 <= ord(c) <= 0x7E for c in val)):
                fail(n, "a tag is exactly 3 printable ASCII characters")
            if key == "tag" and val in ("MAT", "OWN"):
                fail(n, "the tag %s is the pad's own" % val)
            if d[slot] and d[slot] != val:
                fail(n, 'group %s already has the %s "%s"' % (d[0], key, d[slot]))
            d[slot] = val
            d[3 + slot] = True
            continue
        vals = [v.strip(" \t") for v in val.split(",") if v.strip(" \t")]
        if not vals:
            fail(n, key + " has no value")
        if key == "class":
            cur["classes"] += [v.lower() for v in vals]
        elif key == "path":
            cur["paths"] += [v.lower().replace("\\", "/") for v in vals]
        elif key == "rank":
            for v in vals:
                if v.lower() == "any":
                    cur["ranks"] = ["*"]
                    break
                cur["ranks"].append(v.lower())
        elif key == "flag":
            for v in vals:
                if v.lower() not in FLAG_BITS:
                    fail(n, 'unknown flag "%s"' % v)
                cur["flags"] |= FLAG_BITS[v.lower()]
        elif key == "source":
            if len(vals) != 1 or vals[0].lower() not in ("base", "mod", "any"):
                fail(n, "source must be base, mod or any")
            cur["source"] = vals[0].lower()
        else:
            fail(n, 'unknown key "%s"' % key)
        cur["filter"] = True
    close()
    builtin = {b[0]: b for b in BUILTIN}
    for d in gs["groups"]:
        b = builtin.get(d[0])
        if not d[1]:
            d[1] = b[1] if b else d[0].rsplit(".", 1)[-1][:1].upper() + d[0].rsplit(".", 1)[-1][1:]
            d[6] = not b
        if not d[2]:
            d[2] = b[2] if b else tag_from(d[1])
            d[7] = not b
        if d[2] in ("MAT", "OWN"):
            fail(d[3], "the tag %s is the pad's own" % d[2])
    return gs


def merge_groups(user, base):
    """utg::mergeGroupSets: the player's set IN FRONT of the built-in one - its new groups first,
    then the built-in groups (a built-in id the user names keeps its place and takes the player's
    label/tag/first class), every user rule, then every built-in rule, the player's [auto] keys
    over the built-in ones. -> (merged, (builtin, yours, extended))."""
    base_idx = {d[0]: n for n, d in enumerate(base["groups"])}
    user_idx = {d[0]: n for n, d in enumerate(user["groups"])}
    out = {"groups": [], "rules": [], "words": dict(base["words"], **user["words"])}
    umap, bmap = {}, {}
    yours = extended = 0
    for n, d in enumerate(user["groups"]):
        if d[0] in base_idx:
            continue
        umap[n] = len(out["groups"])
        out["groups"].append(list(d))
        yours += 1
    for n, d in enumerate(base["groups"]):
        d = list(d)
        bmap[n] = len(out["groups"])
        if d[0] in user_idx:
            u = user["groups"][user_idx[d[0]]]
            umap[user_idx[d[0]]] = bmap[n]
            extended += 1
            if u[4]:
                d[1], d[4], d[6] = u[1], True, False
            if u[5]:
                d[2], d[5], d[7] = u[2], True, False
            if u[8]:
                d[8] = u[8]
        out["groups"].append(d)
    for r in user["rules"]:
        out["rules"].append(dict(r, group=r["group"] if r["exclude"] else umap[r["group"]]))
    for r in base["rules"]:
        out["rules"].append(dict(r, group=r["group"] if r["exclude"] else bmap[r["group"]]))
    out["auto"] = user["auto"] if user["autoSet"] else base["auto"]
    out["min"] = user["min"] if user["minSet"] else base["min"]
    out["autoSet"] = user["autoSet"] or base["autoSet"]
    out["minSet"] = user["minSet"] or base["minSet"]
    return out, (len(base["groups"]), yours, extended)


def load_groups(path):
    """utg::loadGroupsFile: the built-in set, or the player's file merged in front of it. Only a
    missing file is absent; one that cannot be opened, read or parsed is refused whole, in the
    C++ words -> (rules, warning or None). Never writes."""
    base = parse_groups(template_text())
    if not path:
        return base, None

    def refuse(why):
        return base, "uniquetab-groups.ini is ignored - " + why + "; the built-in groups are used"

    try:
        fh = open(path, "rb")
    except OSError as e:
        if e.errno == errno.ENOENT:
            return base, None
        return refuse("cannot open " + path)
    with fh:
        try:
            data = fh.read((1 << 20) + 1)
        except OSError:
            return refuse("cannot read " + path)
    if len(data) > (1 << 20):
        return refuse(path + " is over 1 MB")
    try:
        user = parse_groups(data.decode("utf-8", "surrogateescape"))
    except ValueError as e:
        return refuse(path + ": " + str(e))
    return merge_groups(user, base)[0], None


def glob_match(p, s):
    """'*' any run without '/', '**' any run (and "**/" also matches no segment), '?' one
    non-'/' character - utg::globMatch."""
    i = j = 0
    while i < len(p):
        if p.startswith("**", i):
            i += 2
            if i == len(p):
                return True
            if p[i] == "/" and glob_match(p[i + 1:], s[j:]):
                return True
            return any(glob_match(p[i:], s[t:]) for t in range(j, len(s) + 1))
        if p[i] == "*":
            i += 1
            t = j
            while True:
                if glob_match(p[i:], s[t:]):
                    return True
                if t >= len(s) or s[t] == "/":
                    return False
                t += 1
        if j >= len(s):
            return False
        if p[i] == "?":
            if s[j] == "/":
                return False
        elif p[i] != s[j]:
            return False
        i += 1
        j += 1
    return j == len(s)


def group_item(i):
    """What the group rules read of a catalogue.json item (catalogue_gen.cpp groupItem): fromMod
    when only a custom game's layer has the record, none in a faction folder."""
    flags = 0
    for key, bit in (("isSetPiece", 1), ("isRelic", 2), ("isEquipment", 4), ("isBlueprint", 16),
                     ("isAugment", 32), ("isExtra", 64)):
        if i.get(key):
            flags |= bit
    if i.get("isMi"):
        flags |= 4
    return {"record": i["record"].lower().replace("\\", "/"), "class": (i.get("class") or "").lower(),
            "rank": (i.get("itemClassification") or "").lower(), "flags": flags,
            "fromMod": bool(i.get("fromMod")), "known": i.get("slot") != "other",
            "class_orig": i.get("class") or ""}


def match_item(gs, it, keys):
    """The first active block that matches: a group index, -1 (an [exclude]) or -2 (none)."""
    for r in gs["rules"]:
        if r["view"] and r["view"] not in keys:
            continue
        if r["classes"] and it["class"] not in r["classes"]:
            continue
        if r["ranks"] and r["ranks"][0] != "*" and it["rank"] not in r["ranks"]:
            continue
        if r["flags"] and not r["flags"] & it["flags"]:
            continue
        if r["source"] == "base" and it["fromMod"] or r["source"] == "mod" and not it["fromMod"]:
            continue
        if r["paths"] and not any(glob_match(g, it["record"]) for g in r["paths"]):
            continue
        return -1 if r["exclude"] else r["group"]
    return -2


def assign_groups(gs, items, keys):
    """-> (groups [[id, label, tag]] in display order, group index per item or -1): the declared
    groups, then the auto groups by class name, then "other" - utg::assignGroups."""
    # id, label, tag, labelDerived, tagDerived, cls
    groups = [[d[0], d[1], d[2], d[6], d[7], d[8]] for d in gs["groups"]]
    group_of = [-1] * len(items)
    auto = {}
    for n, it in enumerate(items):
        m = match_item(gs, it, keys)
        if m >= 0:
            group_of[n] = m
        elif m == -2 and not it["known"] and gs["auto"]:
            auto.setdefault(it["class_orig"], []).append(n)
    other = []
    for cls in sorted(auto):
        if len(auto[cls]) < gs["min"]:
            other += auto[cls]
            continue
        label = (cls.rsplit("_", 1)[-1] or cls or "Other")[:48]
        tag = tag_from(label)
        gid = "auto." + "".join(c if c in ID_CHARS else "-" for c in cls.lower())[:35]
        groups.append([gid, label, tag, True, True, low_ascii(cls)])
        for n in auto[cls]:
            group_of[n] = len(groups) - 1
    if other:
        groups.append(["other", "Other", "OTH", False, False, ""])
        for n in sorted(other):
            group_of[n] = len(groups) - 1
    used = [False] * len(groups)
    for g in group_of:
        if g >= 0:
            used[g] = True
    resolve_names(gs.get("words", {}), groups, used)
    return [d[:3] for d in groups], group_of
HOST_COLS, HOST_ROWS, CELL_PX = 16, 15, 32     # the Transfer sack: 16 x 15 cells of 32 px (measured)
# the relic page's group (src/gen/ut_groups.h kRelicGroupId / Label / Tag): one G line after the
# collection's groups while uniq-relics.txt lists a record, no E line; the id is reserved
RELIC_GROUP_ID, RELIC_GROUP_LABEL, RELIC_GROUP_TAG = "relics", "Relics and Charms", "REL"


# The caravan geometry (src/gen/ui_geometry.h, whose header is the authoritative description of the
# records read and of TQ.exe's code behind them): the grid's origin is the Transfer page's place in
# the caravan window plus its inventory's place in the page, through the merged database, from the
# caravan window the HUD record names (hudCaravanWindow); TQ.exe sizes the game's own Transfer sack
# 16 x 15 cells whatever inventoryXSize / inventoryYSize say, and the rectangle is the area the
# collection's own sack uses: the inventory record's whole cells of CELL_PX, never fewer than
# HOST_COLS x HOST_ROWS. uniq-geometry.txt holds the P line (x0, y0, usableW, usableH, plateW, plateH,
# plate; a page without a cover: 0, 0, no name) and the W line (the window's size, the page's place
# and, when the window's BackgroundImage record is read, the background's bitmapPositionX/Y: its
# place in the window, the texture the window's lattice is drawn in).
HUD_RECORDS = ("records/xpack3/ui/hud/hud_x3.dbr", "records/xpack3/ui/hud/hud_x3_new.dbr")
CARAVAN_WINDOW = "records/xpack/ui/caravan/caravanwindow.dbr"
VANILLA_GEOMETRY = {"x0": 27, "y0": 126, "usableW": 512, "usableH": 480, "plateW": 565, "plateH": 521,
                    "bmpX": 0, "bmpY": 1, "plate": "XPack\\UI\\Caravan\\InventoryTransferCover01.tex",
                    "cols": HOST_COLS, "rows": HOST_ROWS, "windowW": 565, "windowH": 637,
                    "pageX": 0, "pageY": 126, "bgKnown": True, "bgX": 0, "bgY": 0}


def derive_geometry(db, res):
    """(geometry dict, None) or (None, the reason) - the twin of gen::deriveGeometry."""
    def rec(name):
        return db.get(name.replace("\\", "/").lower()) if name else None
    win_name = ""
    for h in HUD_RECORDS:
        hud = rec(h)
        if hud is not None and arz.sfield(hud, "hudCaravanWindow"):
            win_name = arz.sfield(hud, "hudCaravanWindow")
            break
    win_name = win_name or CARAVAN_WINDOW
    win = rec(win_name)
    if win is None:
        return None, "the caravan window %s is not in the database" % win_name
    page = rec(arz.sfield(win, "TransferWindow"))
    if page is None:
        return None, "the caravan window has no readable TransferWindow record"
    inv = rec(arz.sfield(page, "InventoryDBR"))
    if inv is None:
        return None, "the Transfer page has no readable InventoryDBR record"
    cover_name = arz.sfield(page, "TransferPlate")
    cover = rec(cover_name)
    if cover_name and cover is None:
        return None, "the Transfer page's TransferPlate %s is not in the database" % cover_name
    bitmap = arz.sfield(cover, "bitmapName") if cover_name else ""
    wh = (0, 0)
    if cover_name:
        wh = tqtex.tex_size(res.read(bitmap, 64)) if bitmap else None
        if wh is None:
            return None, 'cannot read the pixel size of the cover "%s"' % bitmap
    bg = rec(arz.sfield(win, "BackgroundImage"))
    page_x, page_y = arz.ifield(page, "WindowLocationX"), arz.ifield(page, "WindowLocationY")
    cols = max(HOST_COLS, arz.ifield(inv, "inventoryXSize") // CELL_PX)
    rows = max(HOST_ROWS, arz.ifield(inv, "inventoryYSize") // CELL_PX)
    g = {"x0": page_x + arz.ifield(inv, "inventoryX"), "y0": page_y + arz.ifield(inv, "inventoryY"),
         "usableW": cols * CELL_PX, "usableH": rows * CELL_PX, "plateW": wh[0], "plateH": wh[1],
         "bmpX": arz.ifield(cover, "bitmapPositionX") if cover_name else 0,
         "bmpY": arz.ifield(cover, "bitmapPositionY") if cover_name else 0,
         "plate": bitmap, "cols": cols, "rows": rows,
         "windowW": arz.ifield(win, "windowDefaultExtentX"), "windowH": arz.ifield(win, "windowDefaultExtentY"),
         "pageX": page_x, "pageY": page_y, "bgKnown": bg is not None,
         "bgX": arz.ifield(bg, "bitmapPositionX") if bg is not None else 0,
         "bgY": arz.ifield(bg, "bitmapPositionY") if bg is not None else 0}
    return g, None


def geometry_text(g) -> bytes:
    """uniq-geometry.txt: the P line and the W line, CRLF."""
    p = "P\t%d\t%d\t%d\t%d\t%d\t%d\t%s" % (g["x0"], g["y0"], g["usableW"], g["usableH"], g["plateW"],
                                         g["plateH"], g["plate"])
    w = "W\t%d\t%d\t%d\t%d" % (g["windowW"], g["windowH"], g["pageX"], g["pageY"])
    if g.get("bgKnown"):
        w += "\t%d\t%d" % (g["bgX"], g["bgY"])
    return (p + "\r\n" + w + "\r\n").encode("utf-8")


LIST_FILES = ("catalogue.bin", "uniq-records.txt", "uniq-groups.txt", "uniq-excluded.txt",
              "uniq-geometry.txt", "uniq-relics.txt")


def loot_slot_kind(field: str) -> str:
    """'worn' for loot<Slot>Item<n> (a worn slot) and defaultHeadPiece, 'misc' for
    lootMisc<n>Item<n>, '' for every other field name (case as the database spells it)."""
    if field == "defaultHeadPiece":
        return "worn"
    if not field.startswith("loot"):
        return ""
    i = len(field)
    while i > 0 and "0" <= field[i - 1] <= "9":
        i -= 1
    if i == len(field) or not field[:i].endswith("Item"):
        return ""
    mid = field[4:i - 4]
    j = len(mid)
    while j > 0 and "0" <= mid[j - 1] <= "9":
        j -= 1
    base = mid[:j]
    if base in WORN_SLOTS:
        return "worn"
    if base == "Misc":
        return "misc"
    return ""


def nondrop_kind(field: str, cls: str, drop_items: int) -> str:
    """'' when a record path in this field of a record of class `cls` can drop; else the kind
    of holder that never drops it (the text uniq-excluded.txt gives after 'unobtainable: ')."""
    if field == "setMembers":
        return "setMembers"
    if cls == "ItemArtifactFormula" and field.startswith("reagent"):
        return "formula reagent"
    if cls in NON_DROPPERS or (cls == "Monster" and drop_items == 0):
        k = loot_slot_kind(field)
        if k == "worn":
            return "worn by " + (cls if cls != "Monster" else "Monster with dropItems 0")
        if k == "misc" and cls in NON_DROPPERS:
            return "lootMisc of " + cls
    return ""


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
    for f in ("bitmap", "artifactBitmap", "relicBitmap", "artifactFormulaBitmapName"):
        v = arz.sfield(rec, f)
        if v:
            return v
    return ""


def template_words(lower_text: str) -> bool:
    """A TEMPLATE_WORDS word with no letter a-z on either side (catalogue_gen.cpp templateWords)."""
    return TEMPLATE_WORD.search(lower_text) is not None


def junk_path(key: str) -> bool:
    """A creature folder anywhere in the (lower-cased) path: the collection rule never looks there."""
    k = "/" + key
    return any("/" + p + "/" in k for p in JUNK_PARTS)


def is_template(key: str, rec) -> bool:
    return template_words(lower_ascii(arz.sfield(rec, "FileDescription"))) or template_words(key)


def walk_skipped(key: str) -> bool:
    """A holder the reach walk never enters: a developer folder, or a template word in its path."""
    return any("/" + d + "/" in key for d in DEV_FOLDERS) or template_words(key)


def is_loot(cls: str) -> bool:
    """A record the reach walk passes through: a loot table (Class Loot*) or a record with no class."""
    return not cls or cls.startswith("Loot")


def reach_walk(refs, cls_of) -> set:
    """The records a chain of holders hands out (catalogue_gen.cpp, the same walk): from every
    holder that is not a loot table, down the fields that can drop, through the loot tables."""
    fwd = collections.defaultdict(list)
    for tgt in sorted(refs):
        for h in sorted(refs[tgt]):
            if h != tgt and not walk_skipped(h):
                fwd[h].append(tgt)
    queue = collections.deque(h for h in sorted(fwd) if not is_loot(cls_of.get(h, "")))
    seen = set(queue)
    reached = set()
    while queue:
        h = queue.popleft()
        for t in fwd[h]:
            reached.add(t)
            if t not in seen and t in fwd and is_loot(cls_of.get(t, "")):
                seen.add(t)
                queue.append(t)
    return reached


def lower_ascii(s: str) -> str:
    return "".join(chr(ord(c) + 32) if "A" <= c <= "Z" else c for c in s)


def is_shipped(key: str, rec, cls: str, tags, db=None) -> bool:
    """Not in a junk folder, and a name tag that resolves (a formula's: its crafted artifact's).
    The template words are decided after the scan (they need the holders)."""
    if junk_path(key):
        return False
    if name_tag_of(rec, cls) in tags:
        return True
    tgt = crafted_record(rec, cls)
    if tgt and db is not None:
        trec = db.get(tgt)
        if trec is not None and name_tag_of(trec, arz.sfield(trec, "Class")) in tags:
            return True
    return False


def is_mi_candidate(key: str, cls: str, cla: str, file_description: str) -> bool:
    """catalogue_gen.cpp isMiCandidate: an EQUIP_SLOT class and itemClassification Rare - the
    engine's monster-infrequent rule. The key and the FileDescription stay in the signature: a
    later record-local rule needs no caller change."""
    return cla == "Rare" and cls in EQUIP_SLOT


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


def save_set_game(mod_name: str) -> str:
    """The leaf of a custom game (src/ut_depositgate.h utSaveSetLeaf + utSaveSetGame): its folder
    mods/<leaf>/ and its journal's set. Lower case a-z 0-9, every other run one '_', at most 48
    characters, "custom" -> "mod_custom", and "-<FNV-1a 32 of the folded name>" when a byte was
    lost; "" when the name sanitises to nothing."""
    s, sep, lost, h = "", False, False, 2166136261
    for b in mod_name.encode("utf-8", "surrogateescape"):
        c = b + 32 if 65 <= b <= 90 else b
        h = ((h ^ c) * 16777619) & 0xFFFFFFFF
        if len(s) >= 48:
            lost = True
            continue
        if 97 <= c <= 122 or 48 <= c <= 57:
            if sep and 0 < len(s) < 47:
                s += "_"
            s += chr(c)
            sep = False
        else:
            sep = lost = True
    if not s:
        return ""
    return ("mod_" if s == "custom" else "") + s + ("-%08x" % h if lost else "")


class GameSpec:
    """WHICH database the outputs describe (src/gen/generate.h GameSpec): the root and the name
    Engine::InitializeMod receives - <root><name>/Database/<name>.arz over database.arz, its
    Resources/Text.arc over Text_<lang>.arc, its archives searched first. No name = the start-up
    game."""

    def __init__(self, root: str = "", mod_name: str = ""):
        self.root, self.mod_name = root, mod_name

    @classmethod
    def from_folder(cls, folder: str) -> "GameSpec":
        folder = os.path.normpath(folder)
        return cls(os.path.dirname(folder), os.path.basename(folder))

    def startup(self) -> bool:
        return not self.mod_name

    def dir(self) -> str:
        if self.startup():
            return ""
        d = self.root.replace("/", "\\")
        if d and not d.endswith("\\"):
            d += "\\"
        return d + self.mod_name

    def arz_path(self) -> str:
        return "" if self.startup() else self.dir() + "\\Database\\" + self.mod_name + ".arz"

    def resources_dir(self) -> str:
        return "" if self.startup() else self.dir() + "\\Resources"

    def name(self) -> str:
        return "" if self.startup() else save_set_game(self.mod_name)


VIEW_KEY = re.compile(r"^[a-z0-9_.+\-]{1,80}$")
VANILLA_VIEW_KEY = "vanilla"      # the start-up game's key (ut_groups.h kVanillaViewKey)


def view_keys(game_name: str):
    """utg::viewKeysFor: a custom game answers to its leaf, the start-up game to "vanilla"."""
    if not game_name:
        return [VANILLA_VIEW_KEY]
    return [game_name] if VIEW_KEY.match(game_name) else []


# ---- the relics and charms (uniq-relics.txt) -------------------------------------------------
# The blocks join a record's difficulty tiers by its basename: 0N_<stem> (N = 1..3), else
# <stem>_n|_e|_l[_<rest>], else <stem>_dN[_<rest>] (N = 1..3); any other record is a block of its
# own with tier 0. The order: the relic blocks, then the charm blocks; inside each by expansion (a
# basename that starts x4_, after its 0N_ when it has one, is Eternal Embers wherever its folder
# is), then the first row's levelRequirement, then the block id. A block's rows go in tier order.
# The file, CRLF, tab-separated, in page order:
#   B <block no. from 1> <block id: folder/stem, folded> <R relic | C charm> <rows>
#   R <record> <tier 0..3> <shards to complete> <relicBitmap> <shardBitmap> <bonus count> <bonus>...
#   X <record> <reason, the words of uniq-excluded.txt>   by record, after every block
# The bonuses are the table's records as the table spells them, in table order, an entry of weight
# 0 or less dropped and a repeat (case and slashes folded) kept once.
RELIC_CLASSES = ("ItemRelic", "ItemCharm")
RELIC_LIST = "uniq-relics.txt"
RELIC_TIER = re.compile(r"^0([1-3])_(.*)\.dbr$")
RELIC_INFIX = re.compile(r"^(.*)_([nel])(_.*)?\.dbr$")
RELIC_DTIER = re.compile(r"^(.*)_d([1-3])(_.*)?\.dbr$")
RELIC_RAND = re.compile(r"^randomizername(\d+)$", re.I)
RELIC_EE = re.compile(r"^0[1-5]_x4_")


def relic_fold(s: str) -> str:
    return lower_ascii(s.replace("\\", "/"))


def relic_block(key: str):
    """(tier 0..3, block id) of a folded record key - relics_gen.cpp relicBlockOf."""
    d, _, b = key.rpartition("/")
    m = RELIC_TIER.match(b)
    if m:
        return int(m.group(1)), d + "/" + m.group(2)
    m = RELIC_INFIX.match(b)
    if m:
        return "nel".index(m.group(2)) + 1, d + "/" + m.group(1) + (m.group(3) or "")
    m = RELIC_DTIER.match(b)
    if m:
        return int(m.group(2)), d + "/" + m.group(1) + (m.group(3) or "")
    return 0, d + "/" + (b[:-4] if b.endswith(".dbr") else b)


def relic_expansion(key: str) -> int:
    """The expansion a block is ordered by - relics_gen.cpp relicExpansionOf."""
    b = key.rpartition("/")[2]
    return 4 if b.startswith("x4_") or RELIC_EE.match(b) else expansion_of(key)


def relic_weight(v) -> float:
    if isinstance(v, list):
        v = v[0] if v else 0
    return v if isinstance(v, (int, float)) else 0


def relic_bonuses(db, table: str):
    """The bonus records a completion bonus table names (catalogue_gen.cpp relicBonuses)."""
    t = db.get(table) if table else None
    if not t:
        return []
    raw = []
    for k, v in t.items():
        m = RELIC_RAND.match(k)
        if m and isinstance(v, str) and v:
            raw.append((int(m.group(1)), v, relic_weight(t.get("randomizerWeight" + m.group(1), 0))))
    raw.sort()
    seen, out = set(), []
    for _i, v, w in raw:
        f = relic_fold(v)
        if w <= 0 or f in seen:
            continue
        seen.add(f)
        out.append(v)
    return out


def relic_order(relics):
    """[(class, block id, [(tier, key, entry)])] in page order."""
    blocks = {}
    for r in relics:
        key = relic_fold(r["record"])
        tier, bid = relic_block(key)
        blocks.setdefault((r["class"], bid), []).append((tier, key, r))
    order = []
    for (cls, bid), rows in blocks.items():
        rows.sort(key=lambda t: (t[0], t[1].encode("utf-8")))
        order.append(((0 if cls == "ItemRelic" else 1, relic_expansion(rows[0][1]),
                       rows[0][2]["levelRequirement"], bid.encode("utf-8")), cls, bid, rows))
    order.sort(key=lambda o: o[0])
    return [(cls, bid, rows) for _k, cls, bid, rows in order]


def relics_text(relics, rexcluded) -> bytes:
    """uniq-relics.txt (relics_gen.cpp relicsListText)."""
    lines = []
    for n, (cls, bid, rows) in enumerate(relic_order(relics), 1):
        lines.append("B\t%d\t%s\t%s\t%d" % (n, bid, "R" if cls == "ItemRelic" else "C", len(rows)))
        for tier, _key, r in rows:
            lines.append("\t".join(["R", r["record"], str(tier), str(r["shards"]), r["bitmap"],
                                    r["shardBitmap"], str(len(r["bonuses"]))] + r["bonuses"]))
    lines += ["X\t%s\t%s" % (x["record"], x["reason"]) for x in rexcluded]
    return "".join(l + "\r\n" for l in lines).encode("utf-8")


def build(game_dir: str, out_dir: str, lang: str = "EN", collect_unobtainable: bool = False,
          collect_mi: bool = False, groups_file=None, game=None, relic_sink=None):
    """relic_sink: a dict that receives "relics" and "excluded" (the relic rows and X lines)."""
    game = game or GameSpec()
    db = arz.ArzDatabase.load_game(game_dir, game_arz=game.arz_path())
    skipped = []            # the custom game's archives that cannot be read (arc_reader.h ResourceArcs)
    tags = tqarc.load_text_tags(game_dir, lang, game.resources_dir(), skipped)
    placed = load_placed()
    unreached = load_placed(array="kUnreachedRecords")
    res = tqarc.ResourceArcs(game_dir, game.resources_dir())
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
        e = {
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
            "isBlueprint": cls in BLUEPRINT_CLASSES,
            "isRelicOrComponent": False,
            "isAugment": False,
            "isRelic": cls == RELIC_CLASS,
            "isSetPiece": bool(set_rec),
            "isEquipment": cls in EQUIP_SLOT,
            "isExtra": False,
            # the KIND mark: a Rare equipment record, a monster infrequent (catalogue_gen.cpp isMi)
            "isMi": cls in EQUIP_SLOT and arz.sfield(rec, "itemClassification") == "Rare",
        }
        if not game.startup() and not any(key in a for a in db.archives[:-1]):
            e["fromMod"] = True     # GD's fromMod: only the custom game's layer (loaded last) has it
        return e

    # one pass: the reverse index of every string field that names a .dbr, and the candidates.
    # refs: the holders whose field can drop the record; nondrop: the kinds of holder (other
    # than the record itself) whose field never drops it (EXCLUDE rule 7)
    refs = collections.defaultdict(set)          # target key -> referrer keys (not itself)
    nondrop = collections.defaultdict(set)       # target key -> holder kinds
    cands, mi_cands, unmapped, relic_cands = [], [], [], []
    templ = set()                                # candidates with a template word
    cls_of = {}                                  # record key -> Class (the reach walk)
    for key, src, rec, flds in db.items_with_fields():
        cls = arz.sfield(rec, "Class")
        cls_of[key] = cls
        drop_items = arz.ifield(rec, "dropItems", 1)
        for field, ftype, vals in flds:
            if ftype != arz.FT_STRING:
                continue
            kind = None
            for v in vals:
                if len(v) >= 4 and lower_ascii(v[-4:]) == ".dbr":
                    tgt = lower_ascii(v.replace("\\", "/"))
                    if kind is None:
                        kind = nondrop_kind(field, cls, drop_items)
                    if not kind:
                        refs[tgt].add(key)
                    elif tgt != key:
                        nondrop[tgt].add(kind)
        if cls in RELIC_CLASSES:
            if is_shipped(key, rec, cls, tags, db):
                bonuses = relic_bonuses(db, arz.sfield(rec, "bonusTableName"))
                if bonuses:
                    tag = name_tag_of(rec, cls)
                    relic_cands.append({
                        "record": db.real_name(key) or key, "class": cls, "itemNameTag": tag,
                        "name": tags.get(tag, "").rstrip(" \t"),
                        "levelRequirement": arz.ifield(rec, "levelRequirement"),
                        "shards": arz.ifield(rec, "completedRelicLevel"),
                        "bitmap": arz.sfield(rec, "relicBitmap"),
                        "shardBitmap": arz.sfield(rec, "shardBitmap"), "bonuses": bonuses,
                        "expansion": expansion_of(key), "bitmapFound": False, "footW": 0, "footH": 0})
                    if is_template(key, rec):
                        templ.add(key)
            continue
        cla = arz.sfield(rec, "itemClassification")
        if cls not in EQUIP_SLOT and cls not in OTHER_SLOT:
            if cla in ("Epic", "Legendary", "Rare") and EQUIP_FAMILY.match(cls) and not junk_path(key):
                unmapped.append((key, cls))
            continue
        # an arcane formula is collected at every classification (a Quest one is a quest's reward:
        # the EXCLUDE rule keeps it when a quest file places it)
        if cla not in ("Epic", "Legendary") and cls not in BLUEPRINT_CLASSES:
            if (is_mi_candidate(key, cls, cla, arz.sfield(rec, "FileDescription"))
                    and is_shipped(key, rec, cls, tags, db)):
                mi_cands.append(entry(key, src, rec, cls))
                if is_template(key, rec):
                    templ.add(key)
            continue
        if not is_shipped(key, rec, cls, tags, db):
            continue
        cands.append(entry(key, src, rec, cls))
        if is_template(key, rec):
            templ.add(key)

    # the template words drop only what nothing hands out: no drop holder, not placed
    def hands_out(e):
        k = e["record"].lower()
        return k in placed or any(r != k for r in refs.get(k, ()))
    cands = [e for e in cands if e["record"].lower() not in templ or hands_out(e)]
    mi_cands = [e for e in mi_cands if e["record"].lower() not in templ or hands_out(e)]
    relic_cands = [e for e in relic_cands if e["record"].lower() not in templ or hands_out(e)]
    if not game.startup():
        # a custom game's counts are its own: the AE 2.10 checks below are the base game's
        if unmapped:
            print("WARN %d Epic/Legendary/Rare record(s) of a class with no slot are not collected, "
                  "the first %s (%s)" % (len(unmapped), unmapped[0][0], unmapped[0][1]))
    else:
        assert not unmapped, "Epic/Legendary/Rare records of a class with no slot: %r" % (unmapped[:5],)
    assert not game.startup() or len(cands) == CANDIDATES_EXPECT, (
        "the collection rule gives %d records, expected %d (AE 2.10). The game database moved - "
        "re-read it and update CANDIDATES_EXPECT; do NOT silence this." % (len(cands), CANDIDATES_EXPECT))
    assert not game.startup() or len(mi_cands) == MI_CANDIDATES_EXPECT, (
        "the monster-infrequent rule gives %d records, expected %d (AE 2.10). The game database "
        "moved - re-read it and update MI_CANDIDATES_EXPECT; do NOT silence this."
        % (len(mi_cands), MI_CANDIDATES_EXPECT))
    if collect_mi:
        cands = cands + mi_cands

    walk = reach_walk(refs, cls_of)
    items, excluded = [], []

    def exclude_reason(e, relic=False):
        """The EXCLUDE rule, first reason that applies -> (reason or "", kept). A relic's icon is
        1 x 1 cells."""
        key = e["record"].lower()
        reached = key in walk
        reason = ""
        if not installed[e["expansion"]]:
            reason = "expansion not installed"
        elif not e["bitmap"]:
            reason = "no icon field (bitmap / artifactBitmap)"
        elif not reached:
            for d in DEV_FOLDERS:
                if "/" + d + "/" in key:
                    reason = "developer folder " + d
                    break
        dropref = any(r != key for r in refs.get(key, ()))
        if not reason and not dropref and key not in nondrop and key not in placed:
            reason = "unreferenced (no loot table, merchant, container, formula, quest or monster holds it)"
        if not reason:
            wh = tqtex.tex_size(res.read(e["bitmap"], tqtex.TEX_HEADER))
            e["bitmapFound"] = wh is not None
            e["bitmapArchive"] = res.source_of(e["bitmap"]) if wh else ""
            if wh is None:
                reason = "icon not found in the item archives"
            else:
                e["footW"], e["footH"] = wh[0] // 32, wh[1] // 32
                if relic and not (e["footW"] == 1 and e["footH"] == 1):
                    reason = "footprint %dx%d (icon %dx%d) outside 1..1 x 1..1" % (
                        e["footW"], e["footH"], wh[0], wh[1])
                elif not (1 <= e["footW"] <= 2 and 1 <= e["footH"] <= 5):
                    reason = "footprint %dx%d (icon %dx%d) outside 1..2 x 1..5" % (
                        e["footW"], e["footH"], wh[0], wh[1])
        kept = False
        # inside the item folders the reach scan's list decides (the walk cannot see the loot
        # tables a quest file names); anywhere else the walk does
        walk_out = not reached and not ITEM_PATH.match(key)
        if not reason and key not in placed and (not dropref or key in unreached or walk_out):
            reason = UNOBTAINABLE + (UNREACHED if dropref else ", ".join(sorted(nondrop[key])))
            if collect_unobtainable:
                reason += KEPT
                kept = True
        return reason, kept

    for e in cands:
        reason, kept = exclude_reason(e)
        if reason:
            excluded.append({"record": e["record"], "reason": reason, "class": e["class"],
                             "name": e["name"], "expansion": e["expansion"], "kept": kept})
        if kept or not reason:
            items.append(e)
    excluded.sort(key=lambda x: x["record"])
    relics, rexcluded = [], []
    for e in relic_cands:
        reason, kept = exclude_reason(e, relic=True)
        if reason:
            rexcluded.append({"record": e["record"], "reason": reason, "class": e["class"],
                              "name": e["name"], "expansion": e["expansion"], "kept": kept})
        if kept or not reason:
            relics.append(e)
    rexcluded.sort(key=lambda x: x["record"])
    if relic_sink is not None:
        relic_sink["relics"], relic_sink["excluded"] = relics, rexcluded

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
        "miCandidates": len(mi_cands),
        "collectMi": bool(collect_mi),
        "excluded": sum(1 for x in excluded if not x["kept"]),
        "collectUnobtainable": bool(collect_unobtainable),
        "collectionRule": "Epic/Legendary EQUIP_SLOT classes + ItemArtifact, and every "
                          "ItemArtifactFormula, outside the creature folders, name tag resolves, "
                          "no template words unless something hands it out; with "
                          "collectMi the Rare EQUIP_SLOT records too (monster infrequents)",
        "excludeRule": ["expansion not installed", "no icon field", "developer folder",
                        "unreferenced", "icon not found", "footprint outside 1..2 x 1..5",
                        "unobtainable"],
    }
    if not game.startup():
        meta["gameLeaf"] = game.name()
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "catalogue.json"), "w", encoding="utf-8") as fh:
        fh.write("{\n")
        fh.write('"meta": ' + json.dumps(meta, indent=1) + ",\n")
        for label, lst in (("items", items), ("excluded", excluded), ("relics", relics),
                           ("relicsExcluded", rexcluded)):
            fh.write(f'"{label}": [\n')
            fh.write(",\n".join(json.dumps(e, sort_keys=True) for e in lst))
            fh.write("\n],\n")
        fh.write('"itemSets": ' + json.dumps(sets, indent=1) + "\n}\n")

    geo, why = derive_geometry(db, res)
    if geo is None:
        print("WARN the caravan geometry cannot be derived (%s) - the base game's grid is used" % why)
        geo = dict(VANILLA_GEOMETRY)
    for sk in skipped + res.skipped:
        print("WARN skips an unreadable archive (%s); the rest of its archives are read" % sk)
    with open(os.path.join(out_dir, "uniq-geometry.txt"), "wb") as fh:
        fh.write(geometry_text(geo))
    groups = write_lists(out_dir, items, excluded, collect_mi, groups_file, game.name(),
                         geo["cols"], geo["rows"], relic_rows=len(relics))
    with open(os.path.join(out_dir, RELIC_LIST), "wb") as fh:
        fh.write(relics_text(relics, rexcluded))
    write_stats(os.path.join(out_dir, "catalogue-stats.md"), items, excluded, cands, groups, meta,
                relics, rexcluded)
    return items, excluded, cands, groups


def write_lists(out_dir, items, excluded, collect_mi=False, groups_file=None, game_name="",
                host_cols=HOST_COLS, host_rows=HOST_ROWS, relic_rows=0):
    """uniq-records.txt, uniq-groups.txt, uniq-excluded.txt - CRLF, as the DLL writes them. The
    groups are the group rules' (the built-in template of src/gen/ut_groups_default.h, or the
    user's uniquetab-groups.ini merged in front of it); a record no group takes is on no page and
    not in uniq-records.txt."""
    rules, warning = load_groups(groups_file)
    if warning:
        print("WARN " + warning)
    keys = view_keys(game_name) + ([COLLECT_MI_VIEW_KEY] if collect_mi else [])
    gdefs, group_of = assign_groups(rules, [group_item(i) for i in items], keys)
    rank = {"Legendary": 0, "Epic": 1, "Rare": 2, "Common": 3}
    paged = [(group_of[n], it) for n, it in enumerate(items) if group_of[n] >= 0]
    # the page order: group, rank, lower-case name, item level, record. The item level puts one
    # name's tiers in order (normal < epic < legendary): TQ's level requirement comes from it,
    # levelRequirement being 0 on the monster infrequents; catalogue.bin orders them the same way
    picked = sorted(paged, key=lambda p: (p[0], rank.get(p[1]["itemClassification"], 9),
                                          lower_name(p[1]["name"]), p[1].get("itemLevel", 0),
                                          p[1]["record"].encode("utf-8")))
    groups = []
    for gi, it in picked:
        if not groups or groups[-1]["def"] != gi:
            groups.append({"def": gi, "slot": it["slot"], "items": [], "cw": 0, "ch": 0})
        g = groups[-1]
        g["items"].append(it)
        g["cw"] = max(g["cw"], it["footW"])
        g["ch"] = max(g["ch"], it["footH"])
    rec_lines, grp_lines = [], []
    for n, g in enumerate(groups):
        cols = max(1, host_cols // max(1, g["cw"]))
        rows = max(1, host_rows // max(1, g["ch"]))
        g["id"], g["label"], g["tag"] = gdefs[g["def"]]
        g["cols"], g["rows"] = cols, rows
        grp_lines.append("G\t%d\t%s\t%d\t%d\t%d\t%d\t%d\t%s\t%s" % (
            n, g["label"], cols, rows, g["cw"] * CELL_PX, g["ch"] * CELL_PX, len(g["items"]), g["id"], g["tag"]))
        for it in g["items"]:
            rec_lines.append(it["record"])
            grp_lines.append("E\t%s\t%d\t%d" % (it["record"], it["footW"], it["footH"]))
    if relic_rows > 0:   # the relic page's group: one G line after the collection's, no E line
        grp_lines.append("G\t%d\t%s\t%d\t%d\t%d\t%d\t0\t%s\t%s" % (
            len(groups), RELIC_GROUP_LABEL, host_cols, host_rows, CELL_PX, CELL_PX, RELIC_GROUP_ID,
            RELIC_GROUP_TAG))
    exc_lines = ["%s\t%s" % (x["record"], x["reason"]) for x in excluded]
    for name, lines in (("uniq-records.txt", rec_lines), ("uniq-groups.txt", grp_lines),
                        ("uniq-excluded.txt", exc_lines)):
        with open(os.path.join(out_dir, name), "wb") as fh:
            fh.write("".join(l + "\r\n" for l in lines).encode("utf-8"))
    return groups


def write_stats(path, items, excluded, cands, groups, meta, relics=(), rexcluded=()):
    L = []
    A = L.append
    A("# Catalogue statistics (Titan Quest AE 2.10)\n")
    A("Generated by `tools/build_catalogue.py` from the game's own database (set TQ_GAME_DIR, or let "
      "it find Steam).")
    A(f"Database: {meta['mergedRecords']} records; {meta['textTags']} text tags (Text_{meta['lang']}).\n")
    A(f"- collection rule (upper bound): **{len(cands)}** records "
      f"({sum(1 for c in cands if c['itemClassification'] == 'Epic')} Epic + "
      f"{sum(1 for c in cands if c['itemClassification'] == 'Legendary')} Legendary + "
      f"{sum(1 for c in cands if c['itemClassification'] == 'Common')} Common + "
      f"{sum(1 for c in cands if c['isMi'])} Rare (monster infrequents), "
      f"{sum(1 for c in cands if c['isRelic'])} artifacts, "
      f"{sum(1 for c in cands if c['isBlueprint'])} arcane formulae)")
    A(f"- after EXCLUDE: **{len(items)}** records, {sum(1 for x in excluded if not x['kept'])} dropped")
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
    A("| # | group | slot | records | Epic | Legendary | Common | MI | largest footprint | grid on the 16 x 15 sack |")
    A("|---:|---|---|---:|---:|---:|---:|---:|---|---|")
    for n, g in enumerate(groups):
        ep = sum(1 for i in g["items"] if i["itemClassification"] == "Epic")
        lg = sum(1 for i in g["items"] if i["itemClassification"] == "Legendary")
        co = sum(1 for i in g["items"] if i["itemClassification"] == "Common")
        mi = sum(1 for i in g["items"] if i["isMi"])
        A(f"| {n} | {g['label']} | {g['slot']} | {len(g['items'])} | {ep} | {lg} | {co} | {mi} | "
          f"{g['cw']}x{g['ch']} | {g['cols']} x {g['rows']} |")
    A("")
    A("## Footprints (cells)\n")
    fp = collections.Counter("%dx%d" % (i["footW"], i["footH"]) for i in items)
    A("| w x h | records |")
    A("|---|---:|")
    for k, v in sorted(fp.items()):
        A(f"| {k} | {v} |")
    A("")
    A("## Listed by the EXCLUDE rule (uniq-excluded.txt; a kept record is in the catalogue)\n")
    A("| record | expansion | class | name | reason |")
    A("|---|---|---|---|---|")
    for x in excluded:
        A(f"| `{x['record']}` | {EXPANSIONS[x['expansion']]} | {x['class']} | {x['name']} | {x['reason']} |")
    A("")
    order = relic_order(relics)
    A("## Relics and charms (uniq-relics.txt)\n")
    A("| part | records | blocks | of them one-line | bonus slots |")
    A("|---|---:|---:|---:|---:|")
    for cls, nm in (("ItemRelic", "relic"), ("ItemCharm", "charm")):
        bl = [rows for c, _b, rows in order if c == cls]
        A(f"| {nm} | {sum(len(r) for r in bl)} | {len(bl)} | {sum(1 for r in bl if len(r) == 1)} | "
          f"{sum(len(t[2]['bonuses']) for r in bl for t in r)} |")
    A(f"| **all** | **{len(relics)}** | **{len(order)}** | | "
      f"**{sum(len(r['bonuses']) for r in relics)}** |\n")
    A(f"Left out by the EXCLUDE rule (the X lines): {len(rexcluded)}.\n")
    for x in rexcluded:
        A(f"- `{x['record']}`: {x['reason']}")
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


# ---- the default buttons file (uniquetab-buttons.ini) ------------------------------------------
# The twin of src\ut_buttons.h utbDefaultText (with src\ut_textcfg.h's built-in words): the same
# bytes, CRLF. test_buttons compares the C++ text with data\oracle\uniquetab-buttons.ini, which
# --buttons-default writes from data\oracle\uniq-groups.txt.
BUTTON_CAPTIONS = {   # the pad's short names (utTextBuiltins' captions), by group id
    "head": "Helm", "shoulders": "SHO", "chest": "Torso", "hands": "Arms", "waist": "BEL",
    "legs": "Legs", "feet": "BOO", "amulet": "Amul", "medal": "MED", "ring": "Ring",
    "offhand": "OFF", "shield": "Shld", "axe1h": "Axe", "dagger": "DAG", "mace1h": "Mace",
    "scepter": "Staff", "sword1h": "Sword", "ranged1h": "Throw", "axe2h": "AX2", "mace2h": "MC2",
    "spear2h": "Spear", "sword2h": "SW2", "ranged2h": "Bow", "artifact": "Artf",
    "blueprint": "BLU", "relics": "Relic",
}
LABEL_FORMATS = [
    "{group}  rows {first}-{last} / {rows}  owned {owned} / {entries}  all {all} / {coll}  {found}",
    "{group} {first}-{last}/{rows}  {owned}/{entries}  all {all}/{coll}  {found}",
    "{group}  {owned}/{entries}  all {all}/{coll}  {found}",
    "{group}  {owned}/{entries}  {found}",
]
TAB_ORDER = [
    "label.format.1", "label.format.2", "label.format.3", "label.format.4",
    "label.mode_unknown", "label.unknown", "tooltip.yes", "tooltip.no", "tooltip.roll",
    "tooltip.class_yes", "tooltip.class_no", "mat.caption", "own.caption", "next.caption",
    "font.caption", "font.label", "size.label", "size.caption_max", "size.caption",
    "color.caption", "color.label", "color.ring", "color.mark",
]
LATE_BUILTIN_IDS = ["svaera_customquest-8350d063.souls", "relics"]   # utbLateBuiltinIds
OFFERED_PREFIX = "; added once by the mod - delete its button and it stays deleted:"


def buttons_tab_defaults():
    d = {"label.format.%d" % (i + 1): f for i, f in enumerate(LABEL_FORMATS)}
    d.update({"label.mode_unknown": "mode unknown - ", "label.unknown": "?",
              "tooltip.yes": "In your collection", "tooltip.no": "Not in your collection",
              "tooltip.roll": " (another roll)", "tooltip.class_yes": "22", "tooltip.class_no": "24",
              "mat.caption": "Transfer", "own.caption": "OWN", "next.caption": ">>"})
    return d


def buttons_quote(v):
    """utbQuote: in double quotes exactly when the parser would lose part of the value."""
    q = not v or v[0] in " \t\"" or v[-1] in " \t"
    for i in range(1, len(v)):
        if q:
            break
        q = v[i] == ";" and v[i - 1] in " \t"
    if not q and v and v[0] in ";#":
        q = True
    return '"' + v + '"' if q else v


def buttons_valid_tag(t):
    return 0 < len(t) <= 3 and all(0x20 < ord(c) < 0x7F and c not in '[];=,"#' for c in t)


def buttons_reserved_tag(t):
    return t.upper() in ("MAT", "OWN", "TAB")


def buttons_name_of(label):
    """utbNameOf: at most 48 UTF-16 units."""
    n = label
    while n and len(n.encode("utf-16-le")) // 2 > 48:
        n = n[:-1]
    return n


def buttons_view_title(game):
    return "the main game" if not game else "the game " + game


def buttons_default_text(view_title, groups):
    """utbDefaultText: `groups` = [(id, tag, label)] in group order; LF."""
    t = ("; uniquetab-buttons.ini - the buttons of the Unique Collection tab in " + view_title + ".\n"
         "; Written once by the mod; edit it freely (it is re-read within a second); delete it to\n"
         "; get the default back. UTF-8. A line starting with ; is a comment, and so is the rest of\n"
         "; a line after a blank and a ;. Put a value in \"double quotes\" to keep blanks or a ; in it.\n"
         ";\n"
         "; [TAG]    one button; the section name is its tag (1 to 3 characters; MAT, OWN and TAB\n"
         ";          are taken). Button order = file order.\n"
         ";   name    = the page name in the label line\n"
         ";   caption = the button's text when it is not the tag (optional)\n"
         ";   groups  = the item groups on it (ids from the groups file); several ids = one button,\n"
         ";             all of their items on one page, in group order\n"
         ";   size    = this button's largest caption size, 6..36 (optional)\n"
         ";   font    = this button's font, for example fonts/albertus mt light.fnt (optional)\n"
         "; [tab]    the label line's templates and words, the rollover lines and their text\n"
         ";          styles (tooltip.class_yes / tooltip.class_no, 0..57), the captions of the\n"
         ";          Transfer toggle (mat.caption), OWN and >>; it also takes font.caption,\n"
         ";          font.label, size.label, size.caption_max, size.caption and color.caption,\n"
         ";          color.label, color.ring, color.mark (RRGGBB). Label placeholders: {mode}\n"
         ";          {group} {owned} {entries} {first} {last} {rows} {all} {coll} {found}.\n"
         "; [unassigned] the groups no button holds - their items have no page. The mod rewrites\n"
         ";          that section (only it) whenever the set changes: move an id from it into a\n"
         ";          button's groups line to show them.\n"
         "; A mistake anywhere makes the mod use the built-in buttons; the log names the line.\n"
         "\n"
         "[tab]\n")
    tab = buttons_tab_defaults()
    for k in TAB_ORDER:
        if k in tab:
            t += k.ljust(18) + " = " + buttons_quote(tab[k]) + "\n"
    unassigned, seen = [], []
    for gid, tag, label in groups:
        if not buttons_valid_tag(tag) or buttons_reserved_tag(tag) or tag.upper() in seen:
            unassigned.append(gid)
            continue
        seen.append(tag.upper())
        t += "\n[" + tag + "]\n"
        t += "name    = " + buttons_quote(buttons_name_of(label)) + "\n"
        if gid in BUTTON_CAPTIONS:
            t += "caption = " + buttons_quote(BUTTON_CAPTIONS[gid]) + "\n"
        t += "groups  = " + gid + "\n"
    t += ("\n[unassigned]\n"
          "; rewritten by the mod: every group of this game that no button lists\n")
    late = [i for i in LATE_BUILTIN_IDS if any(g[0] == i for g in groups)]
    if late:
        t += OFFERED_PREFIX + " " + ", ".join(late) + "\n"
    t += "groups =" + "".join((", " if i else " ") + x for i, x in enumerate(unassigned)) + "\n"
    return t


def buttons_groups_from(path):
    """(id, tag, label) of every G line of a uniq-groups.txt (the 10-field form)."""
    out = []
    with open(path, encoding="latin-1", newline="") as f:
        for line in f.read().split("\r\n"):
            p = line.split("\t")
            if len(p) == 10 and p[0] == "G":
                out.append((p[8], p[9], p[2]))
    return out


def write_buttons_default(groups_txt, out_path):
    text = buttons_default_text(buttons_view_title(""), buttons_groups_from(groups_txt))
    with open(out_path, "wb") as f:
        f.write(text.replace("\n", "\r\n").encode("utf-8"))
    print(f"wrote {out_path} ({len(buttons_groups_from(groups_txt))} buttons)")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--game", default=DEFAULT_GAME)
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--lang", default="EN")
    ap.add_argument("--compare", metavar="FOLDER", help="compare a mod folder's outputs with --out")
    ap.add_argument("--collect-unobtainable", action="store_true",
                    help="keep the EXCLUDE rule 7 records (the ini's collect_unobtainable=1)")
    ap.add_argument("--collect-mi", action="store_true",
                    help="admit the monster infrequents, every Rare equipment record (the ini's "
                         "collect_mi=1, the DLL's default)")
    ap.add_argument("--game-mod", metavar="FOLDER",
                    help="a custom game's folder (<root>/<name>, holding Database/<name>.arz): its "
                         "records, text and archives over the base game's (the DLL's GameSpec)")
    ap.add_argument("--groups", metavar="FILE",
                    help="a uniquetab-groups.ini to merge in front of the built-in groups, as the "
                         "DLL reads the one beside uniquetab.ini (refused whole on an error)")
    ap.add_argument("--buttons-default", nargs=2, metavar=("GROUPS_TXT", "OUT"),
                    help="write the default uniquetab-buttons.ini for a uniq-groups.txt (the twin of "
                         "the DLL's)")
    args = ap.parse_args()
    if args.buttons_default:
        return write_buttons_default(*args.buttons_default)
    if args.compare:
        return compare(args.compare, args.out)
    if not args.game:
        tqpath.game_dir()        # exits with the message
    game = GameSpec.from_folder(args.game_mod) if args.game_mod else GameSpec()
    if not game.startup() and not (os.path.isfile(game.arz_path()) and game.name()):
        print("no custom game database at %s (or the name gives no folder)" % game.arz_path())
        return 2
    items, excluded, cands, groups = build(args.game, args.out, args.lang, args.collect_unobtainable,
                                           args.collect_mi, args.groups, game)
    print(f"collection rule: {len(cands)} records ({sum(1 for c in cands if c['isMi'])} monster "
          f"infrequents); after EXCLUDE: {len(items)} ({sum(1 for i in items if i['isMi'])} MIs) "
          f"({sum(1 for x in excluded if not x['kept'])} dropped) in {len(groups)} groups")
    for x in excluded:
        print(f"  {'kept' if x['kept'] else 'dropped'} {x['record']}: {x['reason']}")
    print(f"wrote {args.out}: catalogue.json, catalogue-stats.md, uniq-records.txt, "
          f"uniq-groups.txt, uniq-excluded.txt, uniq-geometry.txt, uniq-relics.txt (run pack_catalogue.py for "
          f"catalogue.bin)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
