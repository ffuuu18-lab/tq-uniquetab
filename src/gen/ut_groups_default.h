// ut_groups_default.h - the built-in groups (compiled in, never written to disk) and the optional
// user file that ADDS to them (loadGroupsFile -> mergeGroupSets).
//
// A custom game's own groups come first, each in its [view:<leaf>] block (the Grim Dawn mod's
// practice): SVAERA's soul rings (records/item/equipmentring/soul/** and records/item/formulas/
// souls/**), declared before the slot groups so the ring group never takes them there and empty
// (nothing emitted) in every other game. Then the template's groups are exactly the 25 of the
// built-in table (ut_groups.h), in its order with
// its labels and tags: each block's class rule is the Titan Quest class that feeds the group (the
// EQUIP_SLOT class -> slot map of catalogue_gen.cpp; ItemArtifact -> artifact; ItemArtifactFormula
// -> blueprint). The nine slots no Titan Quest class fills (Shoulders, Belts, Boots, Medals,
// Off-hands, Daggers and the three two-handed melee groups) keep GD's class, which no record of
// the game has: they are declared, so a user block of their id fills them, and emit nothing. The
// last section, [view:collect_mi], adds the monster-infrequent rule (every EQUIP_SLOT record whose
// itemClassification is Rare) to each group a Titan Quest class feeds; generate.cpp makes the
// generation answer to that key only while collect_mi=1. [auto] sits at its maximum, so every
// class the mod does not know with fewer than 1000 records shares the one group "other" (a class
// with 1000 or more records gets a group of its own).
//
// The mod never writes uniquetab-groups.ini: no template copy on disk, no rename, no repair.
//
// tools/build_catalogue.py reads the text between the raw-string markers below, so this header is
// the one source of the template for both implementations.
#pragma once

#include "gen/ut_groups.h"

#include <cerrno>
#include <cstdio>
#include <string>

namespace utg {

inline const char* defaultGroupsIni() {
    return R"UTG(; The built-in groups of the Unique Collection tab. Compiled into the mod; never written.
;
; Your own uniquetab-groups.ini beside uniquetab.ini is optional and ADDS to these: its blocks
; are tried first, so a block of yours takes its records off their usual page, and its [exclude]
; keeps records off every page; a block with a built-in id adds rules to that group (its label or
; tag replaces the built-in one); its [auto] applies last. A file with an error is ignored as a
; whole (the log names the file and the line). README.md, "Your own groups".
;
; [group <id>]   a block of rules feeding one group. The id (a-z 0-9 _ . -, up to 40) is how
;                other files name the group; the first block of an id places the group.
;   label  = the group's name              tag    = the 3-character button caption
;   class  = item classes                  path   = record path patterns: * is one folder
;   rank   = Legendary, Epic, Rare, any             level or part of one, ** any depth
;   flag   = set, relic, equipment, faction, blueprint, augment, extra
;   source = base (the base game has the record) or mod (only the mod has it)
;   Values in one key are alternatives; every key of a block must match.
; [exclude]      a block of rules whose records stay off every page.
; [view:<key>]   the blocks after it apply only while the collection answers to that key;
;                [view:*] ends it. While collect_mi=1 (uniquetab.ini) the collection answers
;                to [view:collect_mi].
; [auto]         a class no block names and the mod does not know gets a group of its own
;                when it has at least `min` records; smaller ones share the group "Other".
;
; Every record is tried against the blocks from the top; the first block that matches wins.
; An item you own that no group takes is kept in the collection but not shown; the label ends
; in "+N not on these pages" while there are such items, and they are back once a group takes them.

; ---- A class the mod does not know with fewer than 1000 records shares the group "Other".
[auto]
enabled = yes
min     = 1000

; ---- A custom game's own groups: they apply only inside that game and come first there, so their
; ---- records leave the slot pages below. SVAERA: its soul rings on a page of their own.
[view:svaera_customquest-8350d063]
[group svaera_customquest-8350d063.souls]
label = Souls
tag   = SOU
class = ArmorJewelry_Ring
path  = records/item/equipmentring/soul/**, records/item/formulas/souls/**
rank  = any
[view:*]

; ---- The slot groups. A slot no class of the game fills keeps GD's class: it emits nothing.
[group head]
label = Helms
tag   = HEL
class = ArmorProtective_Head
rank  = Epic, Legendary
[group shoulders]
label = Shoulders
tag   = SHO
class = ArmorProtective_Shoulders
rank  = Epic, Legendary
[group chest]
label = Torso
tag   = TOR
class = ArmorProtective_UpperBody
rank  = Epic, Legendary
[group hands]
label = Arms
tag   = ARM
class = ArmorProtective_Forearm
rank  = Epic, Legendary
[group waist]
label = Belts
tag   = BEL
class = ArmorProtective_Waist
rank  = Epic, Legendary
[group legs]
label = Legs
tag   = LEG
class = ArmorProtective_LowerBody
rank  = Epic, Legendary
[group feet]
label = Boots
tag   = BOO
class = ArmorProtective_Feet
rank  = Epic, Legendary
[group amulet]
label = Amulets
tag   = AMU
class = ArmorJewelry_Amulet
rank  = Epic, Legendary
[group medal]
label = Medals
tag   = MED
class = ArmorJewelry_Medal
rank  = Epic, Legendary
[group ring]
label = Rings
tag   = RNG
class = ArmorJewelry_Ring
rank  = Epic, Legendary
[group offhand]
label = Off-hands
tag   = OFF
class = WeaponArmor_Offhand
rank  = Epic, Legendary
[group shield]
label = Shields
tag   = SHD
class = WeaponArmor_Shield
rank  = Epic, Legendary
[group axe1h]
label = Axes
tag   = AXE
class = WeaponMelee_Axe
rank  = Epic, Legendary
[group dagger]
label = Daggers
tag   = DAG
class = WeaponMelee_Dagger
rank  = Epic, Legendary
[group mace1h]
label = Maces
tag   = MAC
class = WeaponMelee_Mace
rank  = Epic, Legendary
[group scepter]
label = Staves
tag   = STA
class = WeaponMagical_Staff
rank  = Epic, Legendary
[group sword1h]
label = Swords
tag   = SWO
class = WeaponMelee_Sword
rank  = Epic, Legendary
[group ranged1h]
label = Throwing
tag   = THR
class = WeaponHunting_RangedOneHand
rank  = Epic, Legendary
[group axe2h]
label = 2H Axes
tag   = AX2
class = WeaponMelee_Axe2h
rank  = Epic, Legendary
[group mace2h]
label = 2H Maces
tag   = MC2
class = WeaponMelee_Mace2h
rank  = Epic, Legendary
[group spear2h]
label = Spears
tag   = SPR
class = WeaponHunting_Spear
rank  = Epic, Legendary
[group sword2h]
label = 2H Swords
tag   = SW2
class = WeaponMelee_Sword2h
rank  = Epic, Legendary
[group ranged2h]
label = Bows
tag   = BOW
class = WeaponHunting_Bow
rank  = Epic, Legendary
[group artifact]
label = Artifacts
tag   = ART
class = ItemArtifact
rank  = any
; ---- The arcane formulae, at every rarity: one scroll per record.
[group blueprint]
label = Arcane Formulae
tag   = BLU
class = ItemArtifactFormula
rank  = any

; ---- Monster infrequents (collect_mi=1 in uniquetab.ini, the default): every Rare record of an
; ---- equipment class on that class's page after its uniques (Rare ranks after Epic), one box
; ---- per record. With collect_mi=0 these blocks are off and no monster infrequent is in the
; ---- catalogue: an MI you stored earlier stays in the collection, hidden until collect_mi=1 again.
[view:collect_mi]
[group head]
class = ArmorProtective_Head
rank  = Rare
[group chest]
class = ArmorProtective_UpperBody
rank  = Rare
[group hands]
class = ArmorProtective_Forearm
rank  = Rare
[group legs]
class = ArmorProtective_LowerBody
rank  = Rare
[group amulet]
class = ArmorJewelry_Amulet
rank  = Rare
[group ring]
class = ArmorJewelry_Ring
rank  = Rare
[group shield]
class = WeaponArmor_Shield
rank  = Rare
[group axe1h]
class = WeaponMelee_Axe
rank  = Rare
[group mace1h]
class = WeaponMelee_Mace
rank  = Rare
[group scepter]
class = WeaponMagical_Staff
rank  = Rare
[group sword1h]
class = WeaponMelee_Sword
rank  = Rare
[group ranged1h]
class = WeaponHunting_RangedOneHand
rank  = Rare
[group spear2h]
class = WeaponHunting_Spear
rank  = Rare
[group ranged2h]
class = WeaponHunting_Bow
rank  = Rare
[view:*]
)UTG";
}

// The view key the template's monster-infrequent blocks are filed under.
const char* const kCollectMiViewKey = "collect_mi";

// The built-in set, parsed once. Null (with the reason) only if the template is broken.
inline const GroupSet* builtinGroupSet(std::string* why) {
    struct Parsed { GroupSet set; std::string why; bool ok = false; };
    static const Parsed p = [] {
        Parsed r;
        r.ok = parseGroups(defaultGroupsIni(), r.set, &r.why);
        return r;
    }();
    if (!p.ok) {
        if (why) *why = "the built-in template: " + p.why;
        return nullptr;
    }
    return &p.set;
}

enum class GroupsFile {
    Default,    // no user file: the built-in set
    Loaded,     // the player's file parsed and was merged in front of the built-in set
    Refused,    // a user file that cannot be read or parsed: the built-in set alone
};

struct GroupsLoad {
    GroupsFile from = GroupsFile::Default;
    std::string bytes;        // Loaded: the player's file as read, for the stamp
    std::string why;          // Refused: the reason, naming the file (and the line)
    MergeStats stats;         // Loaded: what the merge counted
};

// The rule set a generation uses. `out` is always usable: the built-in set, or the player's file
// merged in front of it. Only reads `path`; never writes, renames or creates anything.
inline GroupsFile loadGroupsFile(const std::string& path, GroupSet& out, GroupsLoad* info) {
    GroupsLoad local;
    GroupsLoad& r = info ? *info : local;
    r = GroupsLoad();
    std::string err;
    const GroupSet* base = builtinGroupSet(&err);
    out = base ? *base : GroupSet();
    auto refuse = [&](const std::string& why) {
        r.from = GroupsFile::Refused;
        r.why = base ? why : err;
        return r.from;
    };
    if (!base) return refuse(err);
    r.stats.builtin = base->groups.size();
    if (path.empty()) return r.from;
    errno = 0;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        if (errno == ENOENT) return r.from;
        return refuse("cannot open " + path);
    }
    std::string text;
    char buf[16384];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0 && text.size() <= (std::size_t(1) << 20))
        text.append(buf, n);
    const bool bad = std::ferror(f) != 0;
    std::fclose(f);
    if (bad || text.size() > (std::size_t(1) << 20))
        return refuse(bad ? "cannot read " + path : path + " is over 1 MB");
    GroupSet user;
    if (!parseGroups(text, user, &err)) return refuse(path + ": " + err);
    GroupSet merged;
    mergeGroupSets(user, *base, merged, &r.stats);
    out = merged;
    r.bytes = text;
    r.from = GroupsFile::Loaded;
    return r.from;
}

} // namespace utg
