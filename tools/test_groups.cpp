// test_groups.cpp - the groups as data, offline: the uniquetab-groups.ini parser, the matcher,
// the auto groups and "other", the G line, the player's file merged in front of the built-in set,
// the derived words, and the built-in template over the shipped catalogue: its groups are the
// oracle's (data\oracle\uniq-groups.txt, and nomi\ for collect_mi=0) group for group and record
// for record. Links the REAL headers the generator compiles (src\gen\ut_groups.h and
// src\gen\ut_groups_default.h) and the model's catalogue loader. No game, no Windows API.
//
//   test_groups.exe <data\oracle> <scratch dir>
#include "gen/ut_groups.h"
#include "gen/ut_groups_default.h"
#include "model/catalogue.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

int g_fail = 0, g_pass = 0;

void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; return; }
    ++g_fail;
    std::printf("[test] FAIL: %s\n", what.c_str());
}

utg::GroupSet mustParse(const std::string& text, const char* what) {
    utg::GroupSet gs;
    std::string err;
    bool ok = utg::parseGroups(text, gs, &err);
    check(ok, std::string(what) + " parses (" + err + ")");
    return gs;
}

// The id of the group a record lands in, "-" for no page.
std::string groupIdOf(const utg::GroupSet& gs, const utg::Item& it, const std::vector<std::string>& keys) {
    std::vector<utg::Item> one{it};
    utg::Assignment a;
    utg::assignGroups(gs, one, keys, a);
    return a.groupOf[0] < 0 ? std::string("-") : a.groups[std::size_t(a.groupOf[0])].id;
}

utg::Item item(const char* rec, const char* cls, const char* rank, unsigned flags = 0,
               bool fromMod = false, bool known = true) {
    utg::Item it;
    it.record = rec;
    it.cls = cls;
    it.rank = rank;
    it.flags = flags;
    it.fromMod = fromMod;
    it.knownClass = known;
    return it;
}

bool readFile(const std::string& path, std::string& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[65536];
    std::size_t n;
    out.clear();
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}
bool writeFile(const std::string& path, const std::string& text) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    return std::fclose(f) == 0 && ok;
}

// Titan Quest's class per catalogue slot word (catalogue_gen.cpp kEquipSlot / kOtherSlot, the
// other way round: every slot the game fills has exactly one class).
const char* classOfSlot(const std::string& slot) {
    static const char* const kMap[][2] = {
        {"head", "ArmorProtective_Head"}, {"chest", "ArmorProtective_UpperBody"},
        {"hands", "ArmorProtective_Forearm"}, {"legs", "ArmorProtective_LowerBody"},
        {"ring", "ArmorJewelry_Ring"}, {"amulet", "ArmorJewelry_Amulet"},
        {"sword1h", "WeaponMelee_Sword"}, {"axe1h", "WeaponMelee_Axe"},
        {"mace1h", "WeaponMelee_Mace"}, {"spear2h", "WeaponHunting_Spear"},
        {"ranged2h", "WeaponHunting_Bow"}, {"ranged1h", "WeaponHunting_RangedOneHand"},
        {"scepter", "WeaponMagical_Staff"}, {"shield", "WeaponArmor_Shield"},
        {"relic", "ItemArtifact"}, {"blueprint", "ItemArtifactFormula"},
    };
    for (const auto& m : kMap) if (slot == m[0]) return m[1];
    return nullptr;
}
// catalogue.bin's group byte -> the slot word (catalogue_gen.cpp slotGroupOf, the other way round)
const char* slotOfGroupByte(int g) {
    static const char* const kSlots[] = {
        "head", "shoulders", "chest", "hands", "waist", "legs", "feet", "axe1h", "mace1h",
        "sword1h", "dagger", "scepter", "axe2h", "mace2h", "sword2h", "spear2h", "ranged1h",
        "ranged2h", "offhand", "shield", "ring", "amulet", "medal", "relic", "blueprint",
        "augment", "consumable", "quest", "other",
    };
    return g >= 0 && g < 29 ? kSlots[g] : "other";
}

// The shipped catalogue as the matcher sees it (catalogue_gen.cpp groupItem): the class from the
// slot, the rank from the packed classification and the KIND mark (an MI is Rare; a Common
// formula is packed as Epic - the formula group takes any rank).
struct OracleSet {
    std::vector<utg::Item> items;
    std::vector<std::string> records;
    bool ok = false;
};
OracleSet loadOracle(const std::string& binPath) {
    OracleSet o;
    gdut::Catalogue cat;
    std::string err;
    if (!cat.loadFromFile(binPath, &err)) {
        check(false, "load " + binPath + " (" + err + ")");
        return o;
    }
    for (std::size_t i = 0; i < cat.itemCount(); ++i) {
        const gdut::ItemView& v = cat.item(i);
        const std::string slot = slotOfGroupByte(int(v.group));
        const char* cls = classOfSlot(slot);
        utg::Item it;
        it.record = std::string(v.record);
        it.cls = cls ? cls : "Unknown";
        it.rank = v.has(gdut::ItemFlag::MonsterInfrequent) ? "Rare" : gdut::classificationName(v.classification);
        if (v.has(gdut::ItemFlag::SetPiece)) it.flags |= utg::kFlagSet;
        if (v.has(gdut::ItemFlag::Relic)) it.flags |= utg::kFlagRelic;
        if (v.has(gdut::ItemFlag::Equipment) || v.has(gdut::ItemFlag::MonsterInfrequent)) it.flags |= utg::kFlagEquipment;
        if (v.has(gdut::ItemFlag::Blueprint)) it.flags |= utg::kFlagBlueprint;
        it.knownClass = slot != "other";
        o.items.push_back(it);
        o.records.push_back(it.record);
    }
    o.ok = true;
    return o;
}

// The G / E structure of a uniq-groups.txt: per group its id, label, tag, count and records.
struct OGroup { std::string id, label, tag; int count = 0; std::vector<std::string> records; };
std::vector<OGroup> readGroupsTxt(const std::string& path, bool* relicLine = nullptr) {
    std::vector<OGroup> gs;
    std::string text;
    if (!readFile(path, text)) { check(false, "read " + path); return gs; }
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t nl = text.find('\n', at);
        if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(at, nl - at);
        at = nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.compare(0, 2, "G\t") == 0) {
            utg::GLine g;
            check(utg::parseGLine(line, g) && g.nineFields, "the oracle's G line parses: " + line);
            gs.push_back({g.id, g.label, g.tag, g.count, {}});
        } else if (line.compare(0, 2, "E\t") == 0 && !gs.empty()) {
            std::size_t t = line.find('\t', 2);
            gs.back().records.push_back(line.substr(2, t == std::string::npos ? std::string::npos : t - 2));
        }
    }
    // the relic page's G line (id relics) comes last with count 0 and no E line: checked here and
    // left out of the collection's grouping
    bool relic = false;
    if (!gs.empty() && gs.back().id == utg::kRelicGroupId) {
        const OGroup& r = gs.back();
        check(r.count == 0 && r.records.empty() && r.label == utg::kRelicGroupLabel &&
                  r.tag == utg::kRelicGroupTag,
              "the relic G line is last with count 0, no E line, its label and tag: " + path);
        gs.pop_back();
        relic = true;
    }
    for (const OGroup& g : gs) check(g.id != utg::kRelicGroupId, "only the last G line has the id relics");
    if (relicLine) *relicLine = relic;
    return gs;
}

// The groups an assignment gives, in display order, each with its records (catalogue order).
std::vector<OGroup> assigned(const utg::GroupSet& gs, const OracleSet& o, const std::vector<std::string>& keys,
                             std::size_t* unpaged = nullptr) {
    utg::Assignment a;
    utg::assignGroups(gs, o.items, keys, a);
    std::vector<OGroup> out;
    std::map<int, std::size_t> at;
    for (std::size_t g = 0; g < a.groups.size(); ++g) {
        if (!a.used[g]) continue;
        at[int(g)] = out.size();
        out.push_back({a.groups[g].id, a.groups[g].label, a.groups[g].tag, 0, {}});
    }
    if (unpaged) *unpaged = 0;
    for (std::size_t i = 0; i < o.items.size(); ++i) {
        if (a.groupOf[i] < 0) { if (unpaged) ++*unpaged; continue; }
        OGroup& g = out[at[a.groupOf[i]]];
        g.records.push_back(o.records[i]);
        ++g.count;
    }
    return out;
}

// Two groupings agree: the same groups in the same order with the same words and counts, and
// every group holds the same records (the order inside a group is the lists' sort, not the rules').
bool sameGrouping(const std::vector<OGroup>& want, const std::vector<OGroup>& got, std::string* why) {
    if (want.size() != got.size()) { *why = std::to_string(got.size()) + " groups, want " + std::to_string(want.size()); return false; }
    for (std::size_t i = 0; i < want.size(); ++i) {
        const OGroup& w = want[i];
        const OGroup& g = got[i];
        if (w.id != g.id || w.label != g.label || w.tag != g.tag || w.count != g.count) {
            *why = "group " + std::to_string(i) + ": " + g.id + "/" + g.label + "/" + g.tag + "/" +
                   std::to_string(g.count) + ", want " + w.id + "/" + w.label + "/" + w.tag + "/" + std::to_string(w.count);
            return false;
        }
        std::set<std::string> a(w.records.begin(), w.records.end()), b(g.records.begin(), g.records.end());
        if (a != b || w.records.size() != std::size_t(w.count)) { *why = "group " + w.id + " holds other records"; return false; }
    }
    return true;
}

// ---------------------------------------------------------------- the cases
void testDefaultTemplate() {
    utg::GroupSet gs = mustParse(utg::defaultGroupsIni(), "the built-in template");
    std::size_t n = 0;
    const utg::BuiltinGroup* b = utg::builtinGroups(&n);
    check(n == 25 && std::string(b[24].id) == "blueprint" && std::string(b[24].label) == "Arcane Formulae"
              && std::string(b[24].tag) == "BLU" && std::string(b[23].id) == "artifact",
          "the built-in table holds the 24 slot groups and the arcane formulae (Arcane Formulae / BLU)");
    // a custom game's own groups come first (their [view:<leaf>] blocks), then the built-in table's
    const std::size_t own = 1;
    const std::string souls = "svaera_customquest-8350d063.souls";
    check(gs.groups.size() == n + own, "the template declares the custom game's own group and the built-in groups (" +
                                           std::to_string(gs.groups.size()) + ")");
    check(!gs.groups.empty() && gs.groups[0].id == souls && gs.groups[0].label == "Souls" && gs.groups[0].tag == "SOU",
          "group 0 is the custom game's Souls (SOU)");
    const decltype(gs.groups) tg(gs.groups.begin() + (gs.groups.size() < own ? gs.groups.size() : own), gs.groups.end());
    for (std::size_t i = 0; i < tg.size() && i < n; ++i) {
        check(tg[i].id == b[i].id, "group " + std::to_string(i + own) + " is " + b[i].id);
        check(tg[i].label == b[i].label, "group " + tg[i].id + " is labelled " + b[i].label);
        check(tg[i].tag == b[i].tag, "group " + tg[i].id + " is tagged " + b[i].tag);
        check(tg[i].labelSet && tg[i].tagSet, "group " + tg[i].id + " names its label and tag");
        // the group's class is the one Titan Quest class of its slot (or GD's, for the nine it lacks)
        const char* cls = classOfSlot(b[i].slot);
        if (cls) check(tg[i].cls == utg::lowerAscii(cls), "group " + tg[i].id + " takes " + cls);
        else check(!tg[i].cls.empty(),
                   "group " + tg[i].id + " (no Titan Quest class) keeps a class of its own");
    }
    check(gs.autoEnabled && gs.autoMin == 1000, "[auto] is on with min at its maximum (1000)");
    std::size_t mi = 0, excl = 0, viewless = 0, game = 0;
    for (const utg::Rule& r : gs.rules) {
        if (r.exclude) ++excl;
        if (r.view == "svaera_customquest-8350d063") {
            ++game;
            check(r.paths.size() == 2 && r.classes.size() == 1 && r.ranks.size() == 1 && (r.ranks[0] == "*" || r.ranks[0] == "any"),
                  "the souls rule takes the soul ring folders at any rank");
        } else if (r.view == "collect_mi") {
            ++mi;
            check(r.ranks.size() == 1 && r.ranks[0] == "rare" && r.classes.size() == 1,
                  "a collect_mi rule is one class at rank Rare");
        } else {
            check(r.view.empty(), "every other rule is in every view");
            ++viewless;
        }
    }
    check(mi == 14, "the collect_mi view adds a Rare rule to each of the 14 equipment groups (" + std::to_string(mi) + ")");
    check(excl == 0 && viewless == 25 && game == 1, "no [exclude]; one base rule per group; one rule of the custom game's");
    // the souls page takes a soul ring there before the ring group does; elsewhere a ring is a Ring
    check(groupIdOf(gs, item("records/item/equipmentring/soul/a/x.dbr", "ArmorJewelry_Ring", "Epic"),
                    {"svaera_customquest-8350d063"}) == souls &&
              groupIdOf(gs, item("records/item/formulas/souls/x.dbr", "ArmorJewelry_Ring", "Rare"),
                        {"svaera_customquest-8350d063"}) == souls &&
              groupIdOf(gs, item("records/item/equipmentring/soul/a/x.dbr", "ArmorJewelry_Ring", "Epic"), {}) == "ring" &&
              groupIdOf(gs, item("records/item/x.dbr", "ArmorJewelry_Ring", "Epic"), {"svaera_customquest-8350d063"}) == "ring",
          "a soul ring is on the Souls page in its game only; any other ring is a Ring");
    // the item flags, ranks and classes a block of the template matches
    check(groupIdOf(gs, item("records/item/x.dbr", "ArmorProtective_UpperBody", "Legendary"), {}) == "chest",
          "a Legendary torso is a Torso");
    check(groupIdOf(gs, item("records/item/x.dbr", "WeaponHunting_Bow", "Rare"), {}) == "-",
          "a Rare bow is on no page without collect_mi");
    check(groupIdOf(gs, item("records/item/x.dbr", "WeaponHunting_Bow", "Rare"), {"collect_mi"}) == "ranged2h",
          "a Rare bow is a Bow with collect_mi");
    check(groupIdOf(gs, item("records/item/x.dbr", "ItemArtifactFormula", "Common"), {}) == "blueprint",
          "a Common formula is an Arcane Formula");
    check(groupIdOf(gs, item("records/item/x.dbr", "ItemArtifact", "Legendary"), {}) == "artifact",
          "a Legendary artifact is an Artifact");
    // the built-in set loads once and is the template
    std::string why;
    const utg::GroupSet* bs = utg::builtinGroupSet(&why);
    check(bs && bs->groups.size() == 26 && bs->rules.size() == 40, "builtinGroupSet is the parsed template (" + why + ")");
}

void testGlob() {
    struct G { const char* pat; const char* path; bool want; };
    const G cases[] = {
        {"records/items/*.dbr", "records/items/a.dbr", true},
        {"records/items/*.dbr", "records/items/x/a.dbr", false},
        {"records/items/**", "records/items/x/y/a.dbr", true},
        {"records/items/**", "records/itemsx/a.dbr", false},
        {"**/bow/**", "records/xpack/item/equipmentweapon/bow/b.dbr", true},
        {"**/bow/**", "records/xpack/item/equipmentweapon/rainbow/b.dbr", false},
        {"records/**/bow/*", "records/bow/b.dbr", true},
        {"records/**/bow/*", "records/a/b/bow/b.dbr", true},
        {"records/items/gear?/a.dbr", "records/items/gear1/a.dbr", true},
        {"records/items/gear?/a.dbr", "records/items/gear/a.dbr", false},
        {"records/items/gear?a.dbr", "records/items/gear/a.dbr", false},
        {"*", "records", true},
        {"*", "records/a", false},
        {"**", "", true},
        {"a*b*c", "axxbyyc", true},
        {"a*b*c", "axxbyy", false},
    };
    for (const G& c : cases)
        check(utg::globMatch(c.pat, c.path) == c.want,
              std::string("glob ") + c.pat + " vs " + c.path + (c.want ? " matches" : " does not match"));
    utg::GroupSet gs = mustParse("[group a]\npath = Records/Items/**\n", "a mixed-case glob");
    check(groupIdOf(gs, item("RECORDS\\ITEMS\\X\\A.DBR", "X", "Epic"), {}) == "a",
          "paths compare without case and with either separator");
}

// GD's PrecedenceAndViews over Titan Quest's keys: a custom game answers to its leaf (viewKeysFor),
// the start-up game to vanilla, and collect_mi rides along while the flag is on - so a player's
// [view:<leaf>] blocks in the ONE groups file apply in that game only.
void testGameViewKeys() {
    const std::vector<std::string> vanilla = utg::viewKeysFor("");
    check(vanilla.size() == 1 && vanilla[0] == "vanilla", "the start-up game answers to vanilla (GD's vanilla view)");
    const std::vector<std::string> leaf = utg::viewKeysFor("my_mod-0e607ba7");
    check(leaf.size() == 1 && leaf[0] == "my_mod-0e607ba7", "a game answers to its leaf");
    check(utg::viewKeysFor("My Mod").empty(), "a name that is no view key gives none");
    const std::string text =
        "[view:my_mod-0e607ba7]\n"
        "[group my.souls]\nlabel = Souls\ntag = SOU\npath = records/item/souls/**\n"
        "[view:collect_mi]\n"
        "[group my.rare]\nlabel = Rare ones\ntag = RAR\nclass = A\nrank = Rare\n"
        "[view:*]\n"
        "[group rest]\nlabel = Rest\ntag = RST\npath = **\n";
    utg::GroupSet gs = mustParse(text, "the game-view file");
    std::vector<std::string> inGame = leaf;
    inGame.push_back(utg::kCollectMiViewKey);
    std::vector<std::string> mainGame = vanilla;
    mainGame.push_back(utg::kCollectMiViewKey);
    check(groupIdOf(gs, item("records/item/souls/a.dbr", "A", "Epic"), inGame) == "my.souls",
          "a [view:<leaf>] block is on in that game");
    check(groupIdOf(gs, item("records/item/souls/a.dbr", "A", "Epic"), mainGame) == "rest",
          "and off in the main game");
    std::vector<std::string> other = utg::viewKeysFor("other_game-12345678");
    other.push_back(utg::kCollectMiViewKey);
    check(groupIdOf(gs, item("records/item/souls/a.dbr", "A", "Epic"), other) == "rest",
          "and off in another game");
    check(groupIdOf(gs, item("records/x/a.dbr", "A", "Rare"), inGame) == "my.rare",
          "[view:collect_mi] still applies in a game while collect_mi=1");
    check(groupIdOf(gs, item("records/x/a.dbr", "A", "Rare"), leaf) == "rest",
          "collect_mi=0 in a game: its block is off, the game's own is not");
    check(groupIdOf(gs, item("records/item/souls/b.dbr", "A", "Epic"), leaf) == "my.souls",
          "the game's block does not need collect_mi");
    // the built-in set in a game: a record only the game has (fromMod) goes by its class as any
    // other, and a source = mod rule of the player's file sees it
    utg::GroupSet src = mustParse("[view:my_mod-0e607ba7]\n[group my.new]\nlabel = New\ntag = NEW\nsource = mod\n", "source");
    check(groupIdOf(src, item("r/a.dbr", "A", "Epic", 0, true), inGame) == "my.new" &&
              groupIdOf(src, item("r/a.dbr", "A", "Epic", 0, false), inGame) == "-",
          "source = mod takes the records only the game has, not the ones it overrides");

    // [view:vanilla]: a block for the main game only (GD's grammar)
    const std::string vt =
        "[view:vanilla]\n"
        "[group main.only]\nlabel = Main only\ntag = MNO\npath = records/item/souls/**\n"
        "[view:*]\n"
        "[group rest]\nlabel = Rest\ntag = RST\npath = **\n";
    utg::GroupSet vs = mustParse(vt, "the vanilla-view file");
    check(groupIdOf(vs, item("records/item/souls/a.dbr", "A", "Epic"), mainGame) == "main.only",
          "a [view:vanilla] block is on in the main game");
    check(groupIdOf(vs, item("records/item/souls/a.dbr", "A", "Epic"), inGame) == "rest",
          "and off in a custom game");
    // a custom game whose folder is literally "vanilla": its leaf is its key, as GD's
    // custom-vanilla view answers to "vanilla" - the block applies in both games
    const std::vector<std::string> leafVanilla = utg::viewKeysFor("vanilla");
    check(leafVanilla.size() == 1 && leafVanilla[0] == "vanilla" &&
              groupIdOf(vs, item("records/item/souls/a.dbr", "A", "Epic"), leafVanilla) == "main.only",
          "a custom game whose leaf is vanilla answers to vanilla too (the block applies in both)");

    // the [view:<key>] keys no installed game answers to (ut_generate.cpp's one INFO line)
    const std::string ut =
        "[view:my_mod-0e607ba7]\n[group a1]\nlabel = A1\ntag = AA1\npath = a/**\n"
        "[view:gone_mod-11111111]\n[group a2]\nlabel = A2\ntag = AA2\npath = b/**\n"
        "[exclude]\npath = c/**\n"
        "[view:vanilla]\n[group a3]\nlabel = A3\ntag = AA3\npath = d/**\n"
        "[view:collect_mi]\n[group a4]\nlabel = A4\ntag = AA4\nrank = Rare\n"
        "[view:empty_header]\n"
        "[view:typo]\n[exclude]\npath = e/**\n"
        "[view:*]\n[group a5]\nlabel = A5\ntag = AA5\npath = **\n";
    utg::GroupSet us = mustParse(ut, "the view-keys file");
    std::vector<std::string> answered = utg::viewKeysFor("");
    for (const std::string& k : utg::viewKeysFor("my_mod-0e607ba7")) answered.push_back(k);
    const std::vector<std::string> un = utg::unansweredViewKeys(us, answered);
    check(un.size() == 2 && un[0] == "gone_mod-11111111" && un[1] == "typo",
          "the keys no installed game answers to are named once each, in file order (not vanilla, "
          "not an installed leaf, not collect_mi, not a header with no block)");
    check(utg::unansweredViewKeys(us, {"vanilla", "my_mod-0e607ba7", "gone_mod-11111111", "typo"}).empty(),
          "every key answered: none is named");
    const std::vector<std::string> noGames = utg::unansweredViewKeys(us, utg::viewKeysFor(""));
    check(noGames.size() == 3 && noGames[0] == "my_mod-0e607ba7",
          "no custom game installed: every leaf key is named, vanilla never");
}

void testPrecedenceAndViews() {
    const std::string text =
        "[group first]\nclass = A\nrank = Epic\n"
        "[exclude]\nclass = A\n"
        "[group second]\nclass = A\n"
        "[view:modx]\n"
        "[group modx.special]\npath = records/special/**\n"
        "[view:*]\n"
        "[group catchall]\npath = **\n";
    utg::GroupSet gs = mustParse(text, "the precedence file");
    check(gs.groups.size() == 4, "four groups declared");
    check(groupIdOf(gs, item("records/x/a.dbr", "A", "Epic"), {}) == "first", "the first block wins");
    check(groupIdOf(gs, item("records/x/a.dbr", "A", "Legendary"), {}) == "-", "an exclude above wins");
    check(groupIdOf(gs, item("records/special/a.dbr", "B", "Epic"), {}) == "catchall",
          "a view block is off outside its view");
    check(groupIdOf(gs, item("records/special/a.dbr", "B", "Epic"), {"modx"}) == "modx.special",
          "a view block is on in its view");
    check(groupIdOf(gs, item("records/other/a.dbr", "B", "Epic"), {"modx"}) == "catchall",
          "[view:*] returns to every view");
    // AND across keys, OR within one
    utg::GroupSet ao = mustParse("[group g]\nclass = A, B\nrank = Epic\nflag = set, relic\nsource = mod\n", "AND/OR");
    check(groupIdOf(ao, item("r/a.dbr", "b", "EPIC", utg::kFlagRelic, true), {}) == "g", "all keys match");
    check(groupIdOf(ao, item("r/a.dbr", "C", "Epic", utg::kFlagRelic, true), {}) == "-", "class fails");
    check(groupIdOf(ao, item("r/a.dbr", "A", "Epic", utg::kFlagEquipment, true), {}) == "-", "flag fails");
    check(groupIdOf(ao, item("r/a.dbr", "A", "Epic", utg::kFlagSet, false), {}) == "-", "source fails");
    // a later block of the same id adds rules, keeps the position
    utg::GroupSet re = mustParse("[group a]\nclass = A\n[group b]\nclass = B\n[group a]\nclass = C\n", "re-opened id");
    check(re.groups.size() == 2 && re.groups[0].id == "a", "a re-opened id keeps its place");
    check(groupIdOf(re, item("r/a.dbr", "C", "Epic"), {}) == "a", "a re-opened id takes its new rule");
    // defaults
    utg::GroupSet df = mustParse("\xEF\xBB\xBF; bom\r\n[group head]\r\nclass = X\r\n[group mymod.long_bows]\r\nclass = Y ; note\r\n", "defaults");
    check(df.groups.size() == 2 && df.groups[0].label == "Helms" && df.groups[0].tag == "HEL",
          "a built-in id takes its built-in label and tag");
    check(df.groups.size() == 2 && df.groups[1].label == "Long_bows" && df.groups[1].tag == "LON",
          "another id takes its last part and three letters of it");
    check(df.rules.size() == 2 && df.rules[1].classes.size() == 1 && df.rules[1].classes[0] == "y",
          "an inline comment is not part of the value");
    check(groupIdOf(df, item("r/a.dbr", "x", "Epic"), {}) == "head", "a class compares without case");
}

void testErrors() {
    struct E { const char* text; int line; const char* what; };
    const E cases[] = {
        {"[group a]\nclass = A\nwhat = 1\n", 3, "an unknown key"},
        {"[nope]\n", 1, "an unknown section"},
        {"[group A]\nclass = A\n", 1, "an upper-case id"},
        {"[group aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa]\nclass = A\n", 1, "a 41-character id"},
        {"[group other]\nclass = A\n", 1, "a reserved id"},
        {"[group auto.x]\nclass = A\n", 1, "an auto. id"},
        {"[group next]\nclass = A\n", 1, "next (the strings file's caption.next)"},
        {"[group vanilla]\nclass = A\n", 1, "vanilla (the Transfer toggle)"},
        {"[group owned]\nclass = A\n", 1, "owned (the owned-only filter)"},
        {"[group a]\ntag = AB\nclass = A\n", 2, "a 2-character tag"},
        {"[group a]\ntag = MAT\nclass = A\n", 2, "the MAT tag"},
        {"[group a]\ntag = OWN\nclass = A\n", 2, "the OWN tag"},
        {"[group a]\nlabel = A\n", 1, "a block with no filter"},
        {"[exclude]\n[group a]\nclass = A\n", 1, "an exclude with no filter"},
        {"class = A\n", 1, "a key outside a block"},
        {"[group a]\nlabel = A\nclass = A\n[group a]\nlabel = B\nclass = B\n", 5, "a second label"},
        {"[group a]\nflag = shiny\n", 2, "an unknown flag"},
        {"[group a]\nsource = elsewhere\n", 2, "a bad source"},
        {"[auto]\nmin = 0\n", 2, "min 0"},
        {"[auto]\nmin = 1001\n", 2, "min 1001"},
        {"[group a\nclass = A\n", 1, "a header without ']'"},
        {"[group a]\nclass\n", 2, "a line without '='"},
        {"[group a]\nclass = ,\n", 2, "an empty list"},
        {"[view:Bad Key]\n", 1, "a bad view key"},
        {"[view:x]\n[auto]\nmin = 3\n", 2, "[auto] inside a view"},
        {"[exclude]\nlabel = X\nclass = A\n", 2, "a label in an [exclude]"},
    };
    for (const E& c : cases) {
        utg::GroupSet gs;
        std::string err;
        bool ok = utg::parseGroups(c.text, gs, &err);
        std::string want = "line " + std::to_string(c.line) + ":";
        check(!ok && err.compare(0, want.size(), want) == 0 && gs.groups.empty() && gs.rules.empty(),
              std::string("refused whole, naming the line: ") + c.what + " (got \"" + err + "\")");
    }
    // GD's words, exactly
    utg::GroupSet gs;
    std::string err;
    utg::parseGroups("[group other]\nclass = A\n", gs, &err);
    check(err == "line 1: the group id \"other\" is reserved", "the reserved id's words (" + err + ")");
    utg::parseGroups("[group a]\ntag = HELM\nclass = A\n", gs, &err);
    check(err == "line 2: a tag is exactly 3 printable ASCII characters", "the bad tag's words (" + err + ")");
    utg::parseGroups("[group a]\nclass = A\nkind = B\n", gs, &err);
    check(err == "line 3: unknown key \"kind\"", "the unknown key's words (" + err + ")");
}

void testAutoAndOffPage() {
    const std::string text = "[exclude]\npath = records/junk/**\n"
                             "[group head]\nclass = ArmorProtective_Head\n"
                             "[auto]\nmin = 3\n";
    utg::GroupSet gs = mustParse(text, "the auto file");
    std::vector<utg::Item> items;
    for (int i = 0; i < 4; ++i) items.push_back(item("records/m/staff.dbr", "WeaponMagical_Staff", "Epic", 0, true, false));
    for (int i = 0; i < 3; ++i) items.push_back(item("records/m/sta.dbr", "WeaponX_Stab", "Epic", 0, true, false));
    items.push_back(item("records/m/odd.dbr", "Weird_Thing", "Epic", 0, true, false));        // 7
    items.push_back(item("records/m/odd2.dbr", "Strange_Thing", "Epic", 0, true, false));     // 8
    items.push_back(item("records/m/hat.dbr", "ArmorProtective_Head", "Epic"));               // 9
    items.push_back(item("records/junk/hat.dbr", "ArmorProtective_Head", "Epic"));            // 10 (excluded)
    items.push_back(item("records/items/bp.dbr", "ItemArtifactFormula", "Epic"));             // 11 (known, unmatched)
    items.push_back(item("records/items/bp2.dbr", "ItemArtifactFormula", "Epic"));            // 12
    items.push_back(item("records/junk/staff.dbr", "WeaponMagical_Staff", "Epic", 0, true, false)); // 13 excluded
    utg::Assignment a;
    utg::assignGroups(gs, items, {}, a);
    auto idOf = [&](std::size_t i) { return a.groupOf[i] < 0 ? std::string("-") : a.groups[std::size_t(a.groupOf[i])].id; };
    check(idOf(0) == "auto.weaponmagical_staff", "a class with min records gets its own auto group");
    check(idOf(4) == "auto.weaponx_stab", "the second auto class too");
    check(idOf(7) == "other" && idOf(8) == "other", "classes under min share Other");
    check(idOf(9) == "head", "a declared group keeps its records");
    check(idOf(10) == "-", "a record an exclude drops is on no page");
    check(idOf(11) == "-", "a record of a known class no rule takes is on no page");
    check(idOf(12) == "-", "another record no rule takes is on no page");
    check(idOf(13) == "-", "a record of an unknown class that is excluded is on no page");
    std::vector<std::string> order;
    for (const utg::GroupDef& d : a.groups) order.push_back(d.id);
    check(order == std::vector<std::string>{"head", "auto.weaponmagical_staff", "auto.weaponx_stab", "other"},
          "display order: declared, auto by class, Other");
    check(a.groups.size() == 4 && a.groups[1].label == "Staff" && a.groups[1].tag == "STA",
          "an auto group is named from its class's last part - Staff / STA (" +
              (a.groups.size() == 4 ? a.groups[1].label + "/" + a.groups[1].tag : std::string()) + ")");
    check(a.groups.size() == 4 && a.groups[2].label == "Stab" && a.groups[2].tag == "STB",
          "STA is taken, so the consonant step gives STB (" + (a.groups.size() == 4 ? a.groups[2].tag : std::string()) + ")");
    check(a.groups.size() == 4 && a.groups[3].tag == "OTH" && a.groups[3].label == "Other", "Other OTH");
    // auto off: unknown classes stay off the page
    utg::GroupSet off = mustParse("[auto]\nenabled = no\n[group h]\nclass = H\n", "auto off");
    utg::assignGroups(off, {item("r/s.dbr", "S", "Epic", 0, true, false), item("r/t.dbr", "S", "Epic", 0, true, false)}, {}, a);
    check(a.groupOf[0] == -1 && a.groupOf[1] == -1 && a.groups.size() == 1,
          "with auto off an unknown class is off the page, and no group is made for it");
    // the built-in set: a mod's record of a Titan Quest item class goes to that class's
    // group; every class the mod does not know shares the one group Other, however many records
    std::string why;
    const utg::GroupSet* bs = utg::builtinGroupSet(&why);
    std::vector<utg::Item> mod;
    mod.push_back(item("records/mod/hat.dbr", "ArmorProtective_Head", "Legendary", 0, true, true));
    for (int i = 0; i < 40; ++i) mod.push_back(item("records/mod/soul.dbr", "ItemEquipment", "Epic", 0, true, false));
    for (int i = 0; i < 7; ++i) mod.push_back(item("records/mod/scroll.dbr", "OneShot_Scroll", "Rare", 0, true, false));
    utg::assignGroups(*bs, mod, {"collect_mi"}, a);
    bool allOther = true;
    for (std::size_t i = 1; i < mod.size(); ++i) allOther = allOther && a.groups[std::size_t(a.groupOf[i])].id == "other";
    check(a.groups[std::size_t(a.groupOf[0])].id == "head", "a mod's helm is a Helm");
    check(allOther && a.groups.back().id == "other" && a.groups.back().label == "Other" && a.groups.back().tag == "OTH",
          "a mod's unknown classes (40 + 7 records) share Other / OTH");
}

void testGLine() {
    utg::GLine g;
    g.index = 3; g.label = "Arms"; g.cols = 8; g.rows = 7; g.cellW = 64; g.cellH = 64; g.count = 151;
    g.id = "hands"; g.tag = "ARM";
    const std::string s = utg::formatGLine(g);
    check(s == "G\t3\tArms\t8\t7\t64\t64\t151\thands\tARM", "the 9-field form");
    utg::GLine r;
    check(utg::parseGLine(s, r) && r.nineFields && r.id == "hands" && r.tag == "ARM" && r.count == 151
              && r.cellH == 64 && r.label == "Arms", "the 9-field form reads back");
    check(utg::parseGLine("G\t1\tTorso\t8\t5\t64\t96\t1", r) && !r.nineFields && r.id == "chest"
              && r.tag == "TOR", "a 7-field line takes its id and tag from its label");
    check(utg::parseGLine("G\t3\tMy pick\t16\t15\t32\t32\t1", r) && r.id == "group3" && r.tag == "MYP",
          "an unknown label gets group<n> and its tagFrom (no position step)");
    check(!utg::parseGLine("G\t3\tArms\t8\t7\t64\t64\t151\thands", r), "8 fields are refused");
    check(!utg::parseGLine("G\t3\tArms\t8\t7\t64\t64", r), "6 fields are refused");
    check(!utg::parseGLine("G\t3\tArms\t8\t7\t64\t64\t151\tHands\tARM", r), "a bad id is refused");
    check(!utg::parseGLine("G\t3\tArms\t8\t7\t64\t64\t151\thands\tARMS", r), "a bad tag is refused");
    check(!utg::parseGLine("E\tx\ty", r), "an E line is not a G line");
}

// The built-in template over the shipped catalogue is the oracle's grouping: collect_mi=1 (the
// default) against data\oracle\uniq-groups.txt.
void testOracle(const std::string& oracle, const OracleSet& o) {
    if (!o.ok) return;
    std::string why;
    const utg::GroupSet* bs = utg::builtinGroupSet(&why);
    std::size_t unpaged = 0;
    std::vector<OGroup> got = assigned(*bs, o, {"collect_mi"}, &unpaged);
    bool relic = false;
    std::vector<OGroup> want = readGroupsTxt(oracle + "\\uniq-groups.txt", &relic);
    check(want.size() == 16, "the oracle has 16 groups (" + std::to_string(want.size()) + ")");
    check(relic, "the oracle's last G line is the relic page's");
    check(sameGrouping(want, got, &why), "the built-in groups give the oracle's grouping (" + why + ")");
    check(unpaged == 0, "every catalogue record is on a page (" + std::to_string(unpaged) + " not)");
    std::size_t total = 0;
    for (const OGroup& g : got) total += std::size_t(g.count);
    check(total == o.items.size() && total == 2774, "2774 records on the pages (" + std::to_string(total) + ")");
}

// collect_mi: the [view:collect_mi] rules place the MIs; without the key the same rules give the
// nomi\ cut's grouping over the uniques and leave every MI off the page.
void testMonsterInfrequents(const std::string& oracle, const OracleSet& o) {
    if (!o.ok) return;
    std::string why;
    const utg::GroupSet* bs = utg::builtinGroupSet(&why);
    utg::Assignment a;
    utg::assignGroups(*bs, o.items, {}, a);
    std::size_t mi = 0, miOff = 0, uniqueOff = 0;
    for (std::size_t i = 0; i < o.items.size(); ++i) {
        const bool isMi = o.items[i].rank == "Rare";
        mi += isMi ? 1 : 0;
        if (a.groupOf[i] < 0) (isMi ? miOff : uniqueOff) += 1;
    }
    check(mi == 1099 && miOff == mi && uniqueOff == 0,
          "without collect_mi every one of the 1099 MIs is off the page and every unique on one (" +
              std::to_string(mi) + " / " + std::to_string(miOff) + " / " + std::to_string(uniqueOff) + ")");
    OracleSet uniques;
    uniques.ok = true;
    for (std::size_t i = 0; i < o.items.size(); ++i)
        if (o.items[i].rank != "Rare") { uniques.items.push_back(o.items[i]); uniques.records.push_back(o.records[i]); }
    bool relic = false;
    std::vector<OGroup> want = readGroupsTxt(oracle + "\\nomi\\uniq-groups.txt", &relic);
    check(relic, "the nomi cut's last G line is the relic page's");
    std::vector<OGroup> got = assigned(*bs, uniques, {});
    check(sameGrouping(want, got, &why), "collect_mi=0 gives the nomi cut's grouping (" + why + ")");
    // with the key each MI is on its own class's page, after nothing else: the class rule decides
    utg::assignGroups(*bs, o.items, {"collect_mi"}, a);
    std::size_t wrong = 0;
    for (std::size_t i = 0; i < o.items.size(); ++i) {
        if (o.items[i].rank != "Rare") continue;
        const utg::GroupDef& d = a.groups[std::size_t(a.groupOf[i])];
        const char* cls = classOfSlot(utg::builtinById(d.id) ? utg::builtinById(d.id)->slot : "");
        if (!cls || utg::lowerAscii(cls) != utg::lowerAscii(o.items[i].cls)) ++wrong;
    }
    check(wrong == 0, "with collect_mi every MI is on its class's group (" + std::to_string(wrong) + " not)");
}

// loadGroupsFile: absent, good, refused; it never writes, renames or creates a file.
void testFiles(const std::string& scratch) {
    const std::string path = scratch + "\\uniquetab-groups.ini";
    std::remove(path.c_str());
    utg::GroupSet gs;
    utg::GroupsLoad info;
    check(utg::loadGroupsFile(path, gs, &info) == utg::GroupsFile::Default && gs.groups.size() == 26
              && info.stats.builtin == 26 && info.bytes.empty(), "no file: the built-in set");
    std::FILE* f = std::fopen(path.c_str(), "rb");
    check(!f, "no file is created by reading");
    if (f) std::fclose(f);
    check(utg::loadGroupsFile("", gs, &info) == utg::GroupsFile::Default, "no path: the built-in set");
    const std::string good = "\xEF\xBB\xBF; mine\r\n[group embers]\r\nlabel = Eternal Embers\r\ntag = EMB\r\n"
                             "path = records/xpack4/**\r\n[group ring]\r\nlabel = My Rings\r\nclass = X\r\n";
    check(writeFile(path, good), "write the test file");
    check(utg::loadGroupsFile(path, gs, &info) == utg::GroupsFile::Loaded && info.bytes == good
              && info.stats.builtin == 26 && info.stats.yours == 1 && info.stats.extended == 1,
          "a good file is loaded: 26 built-in + 1 of yours, 1 extended");
    check(gs.groups.size() == 27 && gs.groups[0].id == "embers" && gs.groups[11].id == "ring"
              && gs.groups[11].label == "My Rings" && gs.groups[11].tag == "RNG",
          "the player's group first; the extended built-in keeps its place, takes the label, keeps its tag");
    std::string after;
    check(readFile(path, after) && after == good, "the file is unchanged after the load");
    const std::string bad = "[group mine]\nclass = A\ntag = TOOLONG\n";
    check(writeFile(path, bad), "write the bad file");
    check(utg::loadGroupsFile(path, gs, &info) == utg::GroupsFile::Refused && gs.groups.size() == 26
              && info.why == path + ": line 3: a tag is exactly 3 printable ASCII characters",
          "a bad file is refused whole, naming the file and the line (" + info.why + ")");
    check(readFile(path, after) && after == bad, "a refused file is left as it is");
    f = std::fopen((path + ".old").c_str(), "rb");
    check(!f, "nothing is renamed: no .old");
    if (f) std::fclose(f);
    std::remove(path.c_str());
}

// mergeGroupSets: the player's set in front; the README's two examples over the shipped catalogue.
void testMerge(const OracleSet& o) {
    std::string why;
    const utg::GroupSet* bs = utg::builtinGroupSet(&why);
    utg::GroupSet user = mustParse("[exclude]\npath = records/junk/**\n"
                                   "[group mine]\nclass = ArmorProtective_Head\nrank = Legendary\n"
                                   "[group head]\ntag = HAT\nclass = Hat_Class\n"
                                   "[auto]\nmin = 2\n", "a user file");
    utg::GroupSet m;
    utg::MergeStats st;
    utg::mergeGroupSets(user, *bs, m, &st);
    check(st.builtin == 26 && st.yours == 1 && st.extended == 1, "the merge counts 26 + 1, 1 extended");
    check(m.groups.size() == 27 && m.groups[0].id == "mine" && m.groups[2].id == "head" && m.groups[2].tag == "HAT"
              && m.groups[2].label == "Helms", "new groups first; a built-in id keeps its place and takes the tag");
    check(m.rules.size() == 3 + bs->rules.size() && m.rules[0].exclude, "every user rule, then every built-in rule");
    check(m.autoMin == 2 && m.autoEnabled, "the player's [auto] min over the built-in one");
    check(groupIdOf(m, item("records/junk/x.dbr", "ArmorProtective_Head", "Epic"), {}) == "-", "the player's exclude first");
    check(groupIdOf(m, item("records/a/x.dbr", "ArmorProtective_Head", "Legendary"), {}) == "mine", "the player's group first");
    check(groupIdOf(m, item("records/a/x.dbr", "ArmorProtective_Head", "Epic"), {}) == "head", "then the built-in rule");
    check(groupIdOf(m, item("records/a/x.dbr", "Hat_Class", "Rare"), {}) == "head", "the player's rule of a built-in id");
    if (!o.ok) return;
    // README example 1: Rings split in two pages
    utg::GroupSet ex1 = mustParse("[group ring.legendary]\nlabel = Legendary Rings\ntag = RNL\n"
                                  "class = ArmorJewelry_Ring\nrank = Legendary\n", "README example 1");
    utg::mergeGroupSets(ex1, *bs, m, nullptr);
    std::vector<OGroup> got = assigned(m, o, {"collect_mi"});
    std::size_t rnl = 0, rng = 0;
    for (const OGroup& g : got) {
        if (g.id == "ring.legendary") rnl = std::size_t(g.count);
        if (g.id == "ring") rng = std::size_t(g.count);
    }
    std::printf("[test] README example 1: Legendary Rings %zu, Rings %zu\n", rnl, rng);
    check(!got.empty() && got[0].id == "ring.legendary" && got[0].label == "Legendary Rings" && rnl > 0 && rng > 0,
          "example 1: the Legendary rings get the first page, the rest stay on Rings");
    // README example 2: a new page from a path glob
    utg::GroupSet ex2 = mustParse("[group embers]\nlabel = Eternal Embers\ntag = EMB\npath = records/xpack4/**\n",
                                  "README example 2");
    utg::mergeGroupSets(ex2, *bs, m, nullptr);
    got = assigned(m, o, {"collect_mi"});
    std::size_t xp4 = 0;
    for (const std::string& r : o.records) xp4 += utg::normPath(r).compare(0, 15, "records/xpack4/") == 0 ? 1 : 0;
    std::printf("[test] README example 2: Eternal Embers %d of %zu records under records/xpack4/\n",
                got.empty() ? 0 : got[0].count, xp4);
    check(!got.empty() && got[0].id == "embers" && std::size_t(got[0].count) == xp4 && xp4 > 0,
          "example 2: every records/xpack4/ record is on the new page");
}

// The derived words: Titan Quest has no class type words, so a derived group keeps the id's last
// part; the tag ladder keeps tags apart; the pure helpers behave as GD's.
void testDerivation() {
    check(utg::classTypeTag("WeaponMelee_Sword") == nullptr && utg::classTypeTag("ArmorProtective_Head") == nullptr,
          "no class has an engine type word");
    utg::GroupSet gs = mustParse("[group mymod.swords]\nclass = WeaponMelee_Sword\n"
                                 "[group mymod.swirls]\nclass = Swirl\n"
                                 "[group mymod.sw]\ntag = SWO\nclass = Sw\n", "derived groups");
    utg::Assignment a;
    utg::assignGroups(gs, {item("r/a.dbr", "WeaponMelee_Sword", "Epic"), item("r/b.dbr", "Swirl", "Epic"),
                           item("r/c.dbr", "Sw", "Epic")}, {}, a);
    check(a.groups.size() == 3 && a.groups[0].label == "Swords" && a.groups[1].label == "Swirls",
          "a derived label is the id's last part, capitalised");
    check(a.groups.size() == 3 && a.groups[2].tag == "SWO" && a.groups[0].tag == "SWR" && a.groups[1].tag != "SWO"
              && a.groups[1].tag != a.groups[0].tag && utg::validTag(a.groups[1].tag),
          "a derived tag steps past a taken one (" + (a.groups.size() == 3 ? a.groups[0].tag + "/" + a.groups[1].tag : std::string()) + ")");
    // a group that receives nothing keeps its parser words and is not emitted
    check(a.used.size() == 3 && a.used[0] && a.used[1] && a.used[2], "every group received its record");
    check(utg::pluralWord("Staff") == "Staves" && utg::pluralWord("Axe") == "Axes" && utg::pluralWord("Cross") == "Crosses"
              && utg::pluralWord("Ruby") == "Rubies" && utg::pluralWord("Day") == "Days", "pluralWord");
    check(utg::cleanWord("{^y}[ms]Spear ") == "Spear" && utg::cleanWord("^kHelm") == "Helm", "cleanWord");
    check(utg::classFamilyWord("WeaponHunting_Spear") == "Hunting" && utg::classLastPart("ArmorProtective_Head") == "Head",
          "the class family word and last part");
    std::vector<std::string> c = utg::tagCandidates("Staff");
    check(c.size() >= 3 && c[0] == "STA" && c[1] == "STF" && c[2] == "ST0", "the tag ladder STA, STF, ST0 ...");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: test_groups.exe <data\\oracle> <scratch dir>\n");
        return 2;
    }
    const std::string oracle = argv[1], scratch = argv[2];
    const OracleSet o = loadOracle(oracle + "\\catalogue.bin");
    struct Case { const char* name; int fails; };
    std::vector<Case> cases;
    auto run = [&](const char* name, auto fn) {
        const int before = g_fail;
        fn();
        cases.push_back({name, g_fail - before});
    };
    run("DefaultTemplate", [&] { testDefaultTemplate(); });
    run("Glob", [&] { testGlob(); });
    run("PrecedenceAndViews", [&] { testPrecedenceAndViews(); });
    run("GameViewKeys", [&] { testGameViewKeys(); });
    run("Errors", [&] { testErrors(); });
    run("AutoAndOffPage", [&] { testAutoAndOffPage(); });
    run("GLine", [&] { testGLine(); });
    run("Oracle", [&] { testOracle(oracle, o); });
    run("Files", [&] { testFiles(scratch); });
    run("Merge", [&] { testMerge(o); });
    run("Derivation", [&] { testDerivation(); });
    run("MonsterInfrequents", [&] { testMonsterInfrequents(oracle, o); });
    for (const Case& c : cases) std::printf("[test] %-20s %s\n", c.name, c.fails ? "FAIL" : "ok");
    std::printf("[test] %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
