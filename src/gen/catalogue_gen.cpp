// catalogue_gen.cpp - see catalogue_gen.h.
#include "gen/catalogue_gen.h"
#include "gen/arc_reader.h"
#include "gen/arz_reader.h"
#include "gen/placed_records.h"
#include "gen/ut_groups.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <system_error>
#include <unordered_set>

namespace gen {

namespace {

struct ClassSlot { const char* cls; const char* slot; };

// TQ's item classes on GD's slot words (and so on the model's 29 groups, unchanged). TQ has no
// shoulders, belt, boots, medal, dagger, 2H melee or off-hand; its spear is filed under GD's
// spear group, the bow under the two-handed ranged group, the thrown weapon under the one-handed
// ranged group and the staff under the caster-weapon (scepter) group.
const ClassSlot kEquipSlot[] = {
    {"ArmorProtective_Head", "head"}, {"ArmorProtective_UpperBody", "chest"},
    {"ArmorProtective_Forearm", "hands"}, {"ArmorProtective_LowerBody", "legs"},
    {"ArmorJewelry_Ring", "ring"}, {"ArmorJewelry_Amulet", "amulet"},
    {"WeaponMelee_Sword", "sword1h"}, {"WeaponMelee_Axe", "axe1h"},
    {"WeaponMelee_Mace", "mace1h"}, {"WeaponHunting_Spear", "spear2h"},
    {"WeaponHunting_Bow", "ranged2h"}, {"WeaponHunting_RangedOneHand", "ranged1h"},
    {"WeaponMagical_Staff", "scepter"}, {"WeaponArmor_Shield", "shield"},
};
const ClassSlot kOtherSlot[] = {
    {"ItemArtifact", "relic"},
    {"ItemArtifactFormula", "blueprint"},
};
const char* const kBlueprintClasses[] = {"ItemArtifactFormula"};
const char* const kRelicClass = "ItemArtifact";

// The EXCLUDE rule's developer folders: a record under any of these path components is never
// collectible, referenced or not.
const char* const kDevFolders[] = {"_devcheat", "devitem", "sandbox", "dev"};

const char* equipSlot(const std::string& cls) {
    for (const auto& e : kEquipSlot) if (cls == e.cls) return e.slot;
    return nullptr;
}
const char* otherSlot(const std::string& cls) {
    for (const auto& e : kOtherSlot) if (cls == e.cls) return e.slot;
    return nullptr;
}
bool isBlueprintClass(const std::string& cls) {
    for (const char* b : kBlueprintClasses) if (cls == b) return true;
    return false;
}
bool startsWith(const std::string& s, const char* p) {
    std::size_t n = std::strlen(p);
    return s.size() >= n && s.compare(0, n, p) == 0;
}
bool endsWith(const std::string& s, const char* p) {
    std::size_t n = std::strlen(p);
    return s.size() >= n && s.compare(s.size() - n, n, p) == 0;
}
std::string lowerAscii(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

std::string nameTagOf(const ArzRecord& rec, const std::string& cls) {
    if (equipSlot(cls)) return rec.str("itemNameTag");
    std::string t = rec.str("itemNameTag");
    return t.empty() ? rec.str("description") : t;
}
std::string craftedRecord(const ArzRecord& rec, const std::string& cls) {
    return isBlueprintClass(cls) ? rec.str("artifactName") : std::string();
}
std::string bitmapOf(const ArzRecord& rec) {
    std::string v = rec.str("bitmap");
    if (v.empty()) v = rec.str("artifactBitmap");
    if (v.empty()) v = rec.str("relicBitmap");
    if (v.empty()) v = rec.str("artifactFormulaBitmapName");   // an arcane formula's recipe scroll
    return v;
}
bool hasTag(const std::unordered_map<std::string, std::string>& tags, const std::string& t) {
    return tags.find(t) != tags.end();
}
std::string tagText(const std::unordered_map<std::string, std::string>& tags, const std::string& t) {
    auto it = tags.find(t);
    return it == tags.end() ? std::string() : it->second;
}
// TQ: Text_EN keeps trailing blanks on some names ("Cestus  "); item and set display names drop
// trailing ASCII blanks (space, tab). tools/build_catalogue.py does .rstrip(" \t") on the same two.
std::string stripTrailingBlanks(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    return s;
}

// records/(xpackN/)item(s)/ - the game's item folders, on the lower-cased key. The collection
// rule does not test it; inside them the reach walk does not decide (EXCLUDE rule 7).
bool itemPath(const std::string& key) {
    if (!startsWith(key, "records/")) return false;
    std::size_t p = 8;
    if (key.compare(p, 5, "xpack") == 0) {
        std::size_t q = p + 5;
        if (q < key.size() && key[q] >= '0' && key[q] <= '9') ++q;
        if (q < key.size() && key[q] == '/') p = q + 1;
    }
    if (key.compare(p, 5, "item/") == 0 || key.compare(p, 6, "items/") == 0) return true;
    return false;
}

// 0 base, 1 xpack (Immortal Throne), 2..4 xpack2..xpack4, from the record path.
int expansionOf(const std::string& key) {
    if (!startsWith(key, "records/xpack")) return 0;
    const char c = key.size() > 13 ? key[13] : 0;
    if (c == '/') return 1;
    if (c >= '2' && c <= '4' && key.size() > 14 && key[14] == '/') return c - '0';
    return 0;
}

// GD's rule 4 ("FileDescription has no BLANK"), widened to TQ's template words (blank, test,
// template, copy of), each a whole word: no letter a-z right before or right after it.
bool templateWords(const std::string& lowerText) {
    static const char* const kWords[] = {"blank", "test", "template", "copy of"};
    auto letter = [](char c) { return c >= 'a' && c <= 'z'; };
    for (const char* w : kWords) {
        const std::size_t n = std::strlen(w);
        for (std::size_t p = lowerText.find(w); p != std::string::npos; p = lowerText.find(w, p + 1)) {
            const bool before = p > 0 && letter(lowerText[p - 1]);
            const bool after = p + n < lowerText.size() && letter(lowerText[p + n]);
            if (!before && !after) return true;
        }
    }
    return false;
}

// A creature folder anywhere in the lower-cased key (the gear NPCs and monsters wear): the
// collection rule never looks there, in the base game and a custom game alike (GD's junk folders).
bool junkPath(const std::string& key) {
    const std::string k = "/" + key;
    return k.find("/creature/") != std::string::npos || k.find("/creatures/") != std::string::npos;
}

bool isTemplate(const std::string& key, const ArzRecord& rec) {
    return templateWords(lowerAscii(rec.str("FileDescription"))) || templateWords(key);
}

bool devFolder(const std::string& key, const char** which = nullptr) {
    for (const char* d : kDevFolders) {
        const std::string part = std::string("/") + d + "/";
        if (key.find(part) != std::string::npos) { if (which) *which = d; return true; }
    }
    return false;
}

// A holder the reach walk never enters: a developer folder, or a template word in its path.
bool walkSkipped(const std::string& key) { return devFolder(key) || templateWords(key); }

// A record the reach walk passes through: a loot table (Class Loot*) or a record with no class.
bool lootClass(const std::string& cls) { return cls.empty() || startsWith(cls, "Loot"); }

// Not in a junk folder, and a name tag that resolves (a formula's: its crafted artifact's). The
// template words are decided after the scan, with the holders.
bool isShipped(const std::string& key, const ArzRecord& rec, const std::string& cls,
               const GameData& g) {
    if (junkPath(key)) return false;
    if (hasTag(g.tags, nameTagOf(rec, cls))) return true;
    std::string tgt = craftedRecord(rec, cls);
    if (!tgt.empty()) {
        ArzRecord trec;
        if (g.db->get(tgt, trec) && hasTag(g.tags, nameTagOf(trec, trec.str("Class")))) return true;
    }
    return false;
}

std::uint32_t rd32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16)
         | (std::uint32_t(p[3]) << 24);
}

} // namespace

// TQ .tex: "TEX" + version; v1 = GD's v2 layout (the DDS surface at 12), v2 inserts one flag
// byte (the surface at 13). The surface magic is "DDS " or TQ's "DDSR"; its height and width are
// at +12 and +16. Only the header is read, never the pixels.
static const std::size_t kTexHeader = 64;
bool texSize(const std::vector<std::uint8_t>& d, int& w, int& h) {
    if (d.size() < 4 || d[0] != 'T' || d[1] != 'E' || d[2] != 'X') return false;
    std::size_t off;
    if (d[3] == 1) off = 12;
    else if (d[3] == 2) off = 13;
    else return false;
    if (d.size() < off + 20) return false;
    if (d[off] != 'D' || d[off + 1] != 'D' || d[off + 2] != 'S' || (d[off + 3] != ' ' && d[off + 3] != 'R'))
        return false;
    const std::uint32_t hh = rd32(d.data() + off + 12), ww = rd32(d.data() + off + 16);
    if (ww == 0 || hh == 0 || ww > 4096 || hh > 4096) return false;
    w = int(ww);
    h = int(hh);
    return true;
}


// ---------------------------------------------------------------- GameData
GameData::~GameData() {
    delete db;
    delete res;
}

bool GameData::load(const std::string& gameDir, const std::string& lang, std::string* error,
                    const std::string& gameArz, const std::string& gameResources) {
    delete db; delete res;
    db = new ArzDatabase;
    res = new ResourceArcs;
    tags.clear();
    if (!db->loadGame(gameDir, error, gameArz)) return false;
    res->setRoot(gameDir, gameResources);
    // a custom game's Text.arc that cannot be read is named with the archives it skips
    std::vector<std::string> skipped;
    if (!loadTextTags(gameDir, lang, tags, error, gameResources, &skipped)) return false;
    for (const std::string& s : skipped) res->noteSkipped(s);
    // The DLC filter: an expansion counts as installed when its resource
    // folder is there. Immortal Throne's is "xpack", the later ones "XPack2".."XPack4".
    static const char* const kFolder[5] = {"", "xpack", "XPack2", "XPack3", "XPack4"};
    expansionInstalled[0] = true;
    for (int i = 1; i < 5; ++i) {
        std::error_code ec;
        expansionInstalled[i] =
            std::filesystem::is_directory(gameDir + "\\Resources\\" + kFolder[i], ec) && !ec;
    }
    return true;
}

// ---------------------------------------------------------------- EXCLUDE rule 7
// The classes whose worn gear and misc loot never drop, and the worn slots.
static const char* const kNonDroppers[] = {"Npc", "Guard", "NpcMerchant", "NpcEnchanter",
                                           "NpcSkillReallocator", "Pet"};
static const char* const kWornSlots[] = {"Head", "Torso", "Forearm", "LowerBody", "RightHand",
                                         "LeftHand", "Finger", "Neck"};
static const char* const kUnobtainable = "unobtainable: ";
static const char* const kKept = " (kept: collect_unobtainable=1)";
static const char* const kUnreached = "only in loot tables nothing reaches";

static bool isDigitChar(char c) { return c >= '0' && c <= '9'; }

static bool isNonDropper(const std::string& cls) {
    for (const char* c : kNonDroppers)
        if (cls == c) return true;
    return false;
}

// 'w' for loot<Slot>Item<n> (a worn slot) and defaultHeadPiece, 'm' for lootMisc<n>Item<n>,
// 0 for every other field name (case as the database spells it).
static char lootSlotKind(const std::string& f) {
    if (f == "defaultHeadPiece") return 'w';
    if (f.compare(0, 4, "loot") != 0) return 0;
    std::size_t i = f.size();
    while (i > 0 && isDigitChar(f[i - 1])) --i;
    if (i == f.size() || i < 8 || f.compare(i - 4, 4, "Item") != 0) return 0;
    std::string base = f.substr(4, i - 8);
    std::size_t j = base.size();
    while (j > 0 && isDigitChar(base[j - 1])) --j;
    base.resize(j);
    for (const char* s : kWornSlots)
        if (base == s) return 'w';
    return base == "Misc" ? 'm' : 0;
}

// dropItems as tools/tqarz.py ifield(rec, "dropItems", 1) reads it: the first value as an int; 1 when
// the field is absent, has no value, or is a string that is not a whole number.
static std::int32_t dropItemsOf(const ArzRecord& rec) {
    const ArzField* f = rec.find("dropItems");
    if (!f || f->raw.empty()) return 1;
    if (f->type == kFtString) {
        if (f->strs.empty() || f->strs[0].empty()) return 1;
        char* end = nullptr;
        const long v = std::strtol(f->strs[0].c_str(), &end, 10);
        return (end && *end == 0) ? std::int32_t(v) : 1;
    }
    return rec.i32("dropItems");
}

// "" when a record path in this field of a record of class `cls` can drop; else the kind of
// holder that never drops it (the text after "unobtainable: " in uniq-excluded.txt).
static std::string nondropKind(const std::string& field, const std::string& cls, std::int32_t dropItems) {
    if (field == "setMembers") return "setMembers";
    if (cls == "ItemArtifactFormula" && field.compare(0, 7, "reagent") == 0) return "formula reagent";
    const bool npc = isNonDropper(cls);
    if (npc || (cls == "Monster" && dropItems == 0)) {
        const char k = lootSlotKind(field);
        if (k == 'w') return npc ? "worn by " + cls : std::string("worn by Monster with dropItems 0");
        if (k == 'm' && npc) return "lootMisc of " + cls;
    }
    return std::string();
}

// ---------------------------------------------------------------- the relics and charms
static bool isRelicClass(const std::string& cls) { return cls == "ItemRelic" || cls == "ItemCharm"; }

// A field's first value as a number, as tools/build_catalogue.py relic_weight reads it: an int (or
// bool) or a float; 0 for a string, an empty or an absent field.
static double numberOf(const ArzField* f) {
    if (!f || f->raw.empty()) return 0.0;
    if (f->type == kFtInt || f->type == kFtBool) return double(std::int32_t(f->raw[0]));
    if (f->type == kFtFloat) {
        float v = 0.0f;
        std::memcpy(&v, &f->raw[0], sizeof v);
        return double(v);
    }
    return 0.0;
}

// The bonus records a completion bonus table names (build_catalogue.py relic_bonuses): every
// randomizerName<n> (any letter case) holding one string, ordered by n, whose randomizerWeight<n>
// is above 0; a repeat (case and slashes folded) is kept once, as the table spells it first.
static void relicBonuses(const GameData& g, const std::string& table, std::vector<std::string>& out) {
    out.clear();
    ArzRecord t;
    if (table.empty() || !g.db->get(table, t)) return;
    struct Ent { unsigned long long n; std::string val; double w; };
    std::vector<Ent> ents;
    std::set<std::string> names;
    for (const ArzField& f : t.fields) {
        if (f.name.size() <= 14 || f.name.size() > 14 + 18 || lowerAscii(f.name.substr(0, 14)) != "randomizername")
            continue;
        const std::string digits = f.name.substr(14);
        if (!std::all_of(digits.begin(), digits.end(), isDigitChar)) continue;
        if (!names.insert(f.name).second) continue;     // a repeated name: the last field holds it
        const ArzField* v = t.find(f.name.c_str());
        if (!v || v->type != kFtString || v->strs.size() != 1 || v->strs[0].empty()) continue;
        ents.push_back({std::strtoull(digits.c_str(), nullptr, 10), v->strs[0],
                        numberOf(t.find(("randomizerWeight" + digits).c_str()))});
    }
    std::sort(ents.begin(), ents.end(), [](const Ent& a, const Ent& b) {
        if (a.n != b.n) return a.n < b.n;
        if (a.val != b.val) return a.val < b.val;
        return a.w < b.w;
    });
    std::set<std::string> seen;
    for (const Ent& e : ents) {
        std::string f = e.val;
        for (char& c : f) {
            if (c == '\\') c = '/';
            else if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        }
        if (e.w <= 0.0 || !seen.insert(f).second) continue;
        out.push_back(e.val);
    }
}

// ---------------------------------------------------------------- the collection
// ---------------------------------------------------------------- monster infrequents
bool isMiCandidate(const std::string& /*key*/, const std::string& cls,
                   const std::string& classification, const std::string& /*fileDescription*/) {
    // Every Rare record of the equipment classes. The key and the FileDescription stay in the
    // signature: the callers pass them, and a later record-local rule needs no caller change.
    return classification == "Rare" && equipSlot(cls) != nullptr;
}

bool collectItems(const GameData& g, std::vector<CatalogueItem>& items,
                  std::vector<ExcludedItem>& excluded, std::vector<std::string>& warnings,
                  std::string* error, bool collectUnobtainable, bool collectMi,
                  std::vector<RelicItem>* relics, std::vector<ExcludedItem>* relicLeft) {
    items.clear();
    excluded.clear();
    if (relics) relics->clear();
    if (relicLeft) relicLeft->clear();
    if (!g.db || !g.res) { if (error) *error = "game data not loaded"; return false; }
    std::map<std::string, std::string> setNames;
    auto setDisplayName = [&](const std::string& setRec) -> std::string {
        if (setRec.empty()) return std::string();
        auto it = setNames.find(setRec);
        if (it != setNames.end()) return it->second;
        std::string nm;
        ArzRecord sr;
        if (g.db->get(setRec, sr)) {
            nm = tagText(g.tags, sr.str("setName"));
            if (nm.empty()) nm = tagText(g.tags, sr.str("description"));
            nm = stripTrailingBlanks(nm);
        }
        setNames[setRec] = nm;
        return nm;
    };
    auto makeEntry = [&](const std::string& key, const std::string& record, const std::string& src,
                         const ArzRecord& rec, const std::string& cls) {
        CatalogueItem it;
        it.record = record;
        it.cls = cls;
        const char* s = equipSlot(cls);
        if (!s) s = otherSlot(cls);
        it.slot = s ? s : "other";
        it.craftsRecord = craftedRecord(rec, cls);
        it.nameTag = nameTagOf(rec, cls);
        it.name = stripTrailingBlanks(tagText(g.tags, it.nameTag));
        it.bitmap = bitmapOf(rec);
        it.classification = rec.str("itemClassification");
        it.setRecord = rec.str("itemSetName");
        it.setDisplayName = setDisplayName(it.setRecord);
        it.levelRequirement = rec.i32("levelRequirement");
        it.itemLevel = rec.i32("itemLevel");
        it.source = src;
        it.expansion = expansionOf(key);
        it.isBlueprint = endsWith(cls, "Formula");
        it.isAugment = false;
        it.isRelic = cls == kRelicClass;
        it.isSetPiece = !it.setRecord.empty();
        it.isEquipment = equipSlot(cls) != nullptr;
        it.isExtra = false;
        it.isMi = it.isEquipment && it.classification == "Rare";   // the KIND mark
        // GD's fromMod: the custom game's layer (loaded last, tag "game") has it and no archive
        // of the base game does
        {
            const auto& arcs = g.db->archives();
            bool mine = !arcs.empty() && arcs.back().tag() == "game";
            for (std::size_t ai = 0; mine && ai + 1 < arcs.size(); ++ai)
                if (arcs[ai].entry(key)) mine = false;
            it.fromMod = mine;
        }
        return it;
    };

    // The reachability index, filled in the same pass: for every string of the table that names
    // a .dbr, the first record (winner index) whose field holds it, and whether a second,
    // different record holds it too. A record is referenced when a record OTHER than itself
    // holds its path in a string field that can drop it (as tools/build_catalogue.py decides
    // it); the fields that never drop (rule 7) fill `nondrop` with their holder kinds instead.
    const std::vector<std::pair<std::size_t, const ArzEntry*>>& winners = g.db->winners();
    const std::size_t nArch = g.db->archives().size();
    std::vector<std::vector<std::uint8_t>> isDbr(nArch), multi(nArch), rootMulti(nArch);
    std::vector<std::vector<std::int32_t>> firstRef(nArch), rootFirst(nArch);
    // The reach walk (EXCLUDE rule 3 and 7), filled in the same pass: per string, the first
    // holder that is a root (not a loot table, not skipped) and whether a second one holds it;
    // per loot table the walk may enter, its key and the strings its drop fields name.
    std::unordered_map<std::string, std::uint32_t> lootOf;   // loot table key -> winner index
    std::vector<std::size_t> lootBegin(winners.size(), 0), lootEnd(winners.size(), 0);
    std::vector<std::uint32_t> lootEdges;   // a string index in the loot table's own archive
    for (std::size_t ai = 0; ai < nArch; ++ai) {
        const ArzArchive& arc = g.db->archives()[ai];
        const std::size_t ns = arc.stringCount();
        isDbr[ai].resize(ns);
        multi[ai].assign(ns, 0);
        firstRef[ai].assign(ns, -1);
        rootMulti[ai].assign(ns, 0);
        rootFirst[ai].assign(ns, -1);
        for (std::size_t i = 0; i < ns; ++i) {
            // the stored latin-1 bytes end in ".dbr" exactly when their UTF-8 form does
            std::size_t len = 0;
            const char* t = arc.raw(i, &len);
            isDbr[ai][i] = len >= 4 && lowerAscii(std::string(t + len - 4, 4)) == ".dbr";
        }
    }

    std::unordered_map<std::string, std::set<std::string>> nondrop;   // record key -> holder kinds
    std::vector<std::pair<CatalogueItem, std::size_t>> cands;   // item, winner index
    std::vector<std::uint8_t> templ;                            // per candidate: a template word
    std::vector<RelicItem> relicCands;                          // the relic and charm records
    std::vector<std::uint8_t> relicTempl;                       // per relic candidate: a template word
    ArzRecord rec;
    std::string winnerKey;   // the key of the winner in hand (its capacity reused)
    for (std::size_t wi = 0; wi < winners.size(); ++wi) {
        const auto& w = winners[wi];
        const ArzArchive& a = g.db->archives()[w.first];
        const ArzEntry& e = *w.second;
        a.key(e, winnerKey);
        const std::string& key = winnerKey;
        std::string why;
        if (!a.decode(e, rec, &why)) {
            if (error) *error = "record " + a.name(e) + " in " + a.path() + " does not decode (" + why + ")";
            return false;
        }
        const std::string cls = rec.str("Class");
        const std::int32_t dropItems = dropItemsOf(rec);
        const bool skipped = walkSkipped(key);
        const bool loot = lootClass(cls);
        if (!skipped && loot) {
            lootOf[key] = std::uint32_t(wi);
            lootBegin[wi] = lootEdges.size();
        }
        for (const ArzField& f : rec.fields) {
            if (f.type != kFtString) continue;
            int drops = -1;               // the field's rule 7 kind, worked out at its first .dbr
            std::string kind;
            for (std::uint32_t idx : f.raw) {
                if (idx >= isDbr[w.first].size() || !isDbr[w.first][idx]) continue;
                if (drops < 0) {
                    kind = nondropKind(f.name, cls, dropItems);
                    drops = kind.empty() ? 1 : 0;
                }
                if (!drops) {
                    const std::string k = ArzArchive::normKey(a.str(idx));
                    if (k != key) nondrop[k].insert(kind);
                    continue;
                }
                std::int32_t& fr = firstRef[w.first][idx];
                if (fr < 0) fr = std::int32_t(wi);
                else if (fr != std::int32_t(wi)) multi[w.first][idx] = 1;
                if (skipped) continue;
                if (loot) {
                    lootEdges.push_back(idx);
                } else {
                    std::int32_t& rf = rootFirst[w.first][idx];
                    if (rf < 0) rf = std::int32_t(wi);
                    else if (rf != std::int32_t(wi)) rootMulti[w.first][idx] = 1;
                }
            }
        }
        if (!skipped && loot) lootEnd[wi] = lootEdges.size();
        if (isRelicClass(cls)) {
            if (relics && isShipped(key, rec, cls, g)) {
                std::vector<std::string> bonuses;
                relicBonuses(g, rec.str("bonusTableName"), bonuses);
                if (!bonuses.empty()) {
                    RelicItem r;
                    r.item = makeEntry(key, a.name(e), a.tag(), rec, cls);
                    r.item.bitmap = rec.str("relicBitmap");
                    r.shardBitmap = rec.str("shardBitmap");
                    r.shards = rec.i32("completedRelicLevel");
                    r.bonuses = std::move(bonuses);
                    relicCands.push_back(std::move(r));
                    relicTempl.push_back(isTemplate(key, rec) ? 1 : 0);
                }
            }
            continue;
        }
        if (!equipSlot(cls) && !otherSlot(cls)) continue;
        std::string cla = rec.str("itemClassification");
        // an arcane formula is collected at every classification (a Quest one is a quest's reward:
        // the EXCLUDE rule keeps it when a quest file places it); the rest takes Epic or
        // Legendary, or the monster-infrequent rule
        if (cla != "Epic" && cla != "Legendary" && !isBlueprintClass(cls)) {
            // a monster infrequent: admitted with collect_mi=1 (build_catalogue.py counts the
            // candidates always and asserts MI_CANDIDATES_EXPECT)
            if (!isMiCandidate(key, cls, cla, rec.str("FileDescription")) || !isShipped(key, rec, cls, g))
                continue;
            if (collectMi) {
                cands.emplace_back(makeEntry(key, a.name(e), a.tag(), rec, cls), wi);
                templ.push_back(isTemplate(key, rec) ? 1 : 0);
            }
            continue;
        }
        if (!isShipped(key, rec, cls, g)) continue;
        cands.emplace_back(makeEntry(key, a.name(e), a.tag(), rec, cls), wi);
        templ.push_back(isTemplate(key, rec) ? 1 : 0);
    }

    // referenced[key] = some record other than the one named key holds the path; reachedKeys:
    // the keys a root other than the record itself holds, then (the walk) every key a loot table
    // the walk enters names, from the loot tables those roots name on
    std::unordered_map<std::string, bool> referenced;
    std::unordered_set<std::string> reachedKeys;
    std::vector<std::uint8_t> lootSeen(winners.size(), 0);
    std::vector<std::uint32_t> queue;
    auto reach = [&](const std::string& k) {
        reachedKeys.insert(k);
        const auto lt = lootOf.find(k);
        if (lt != lootOf.end() && !lootSeen[lt->second]) {
            lootSeen[lt->second] = 1;
            queue.push_back(lt->second);
        }
    };
    for (std::size_t ai = 0; ai < nArch; ++ai) {
        const ArzArchive& arc = g.db->archives()[ai];
        for (std::size_t i = 0; i < arc.stringCount(); ++i) {
            if (firstRef[ai][i] < 0) continue;
            const std::string k = ArzArchive::normKey(arc.str(i));
            const auto& holder = winners[std::size_t(firstRef[ai][i])];
            const bool other = multi[ai][i] || !g.db->archives()[holder.first].keyIs(*holder.second, k);
            bool& r = referenced[k];
            r = r || other;
            if (rootFirst[ai][i] < 0) continue;
            const auto& root = winners[std::size_t(rootFirst[ai][i])];
            if (rootMulti[ai][i] || !g.db->archives()[root.first].keyIs(*root.second, k)) reach(k);
        }
    }
    for (std::size_t qi = 0; qi < queue.size(); ++qi) {
        const std::uint32_t wi = queue[qi];
        const auto& lw = winners[wi];
        const ArzArchive& la = g.db->archives()[lw.first];
        for (std::size_t ei = lootBegin[wi]; ei < lootEnd[wi]; ++ei) {
            const std::string k = ArzArchive::normKey(la.str(lootEdges[ei]));
            if (!la.keyIs(*lw.second, k)) reach(k);
        }
    }

    // The template words drop only what nothing hands out: no drop holder, not placed.
    const std::unordered_set<std::string> placed(std::begin(kPlacedRecords), std::end(kPlacedRecords));
    {
        std::size_t keep = 0;
        for (std::size_t ci = 0; ci < cands.size(); ++ci) {
            if (templ[ci]) {
                const std::string key = ArzArchive::normKey(cands[ci].first.record);
                const auto r = referenced.find(key);
                if (!placed.count(key) && !(r != referenced.end() && r->second)) continue;
            }
            if (keep != ci) cands[keep] = std::move(cands[ci]);
            ++keep;
        }
        cands.resize(keep);
        keep = 0;
        for (std::size_t ci = 0; ci < relicCands.size(); ++ci) {
            if (relicTempl[ci]) {
                const std::string key = ArzArchive::normKey(relicCands[ci].item.record);
                const auto r = referenced.find(key);
                if (!placed.count(key) && !(r != referenced.end() && r->second)) continue;
            }
            if (keep != ci) relicCands[keep] = std::move(relicCands[ci]);
            ++keep;
        }
        relicCands.resize(keep);
    }

    // The EXCLUDE rule, first reason that applies -> reason ("" none) and kept; a relic's icon
    // is 1 x 1 cells. False on an archive that cannot be read.
    const std::unordered_set<std::string> unreached(std::begin(kUnreachedRecords), std::end(kUnreachedRecords));
    std::vector<std::uint8_t> data;
    auto decide = [&](CatalogueItem& it, bool relic, std::string& reason, bool& kept) -> bool {
        const std::string key = ArzArchive::normKey(it.record);
        const bool reached = reachedKeys.count(key) != 0;
        reason.clear();
        kept = false;
        const char* dev = nullptr;
        if (!g.expansionInstalled[it.expansion]) {
            reason = "expansion not installed";
        } else if (it.bitmap.empty()) {
            reason = "no icon field (bitmap / artifactBitmap)";
        } else if (!reached && devFolder(key, &dev)) {
            reason = std::string("developer folder ") + dev;
        }
        const auto r = referenced.find(key);
        const bool dropRef = r != referenced.end() && r->second;
        const auto nd = nondrop.find(key);
        if (reason.empty() && !dropRef && nd == nondrop.end() && !placed.count(key))
            reason = "unreferenced (no loot table, merchant, container, formula, quest or monster holds it)";
        if (reason.empty()) {
            std::string err;
            int w = 0, h = 0;
            it.bitmapFound = g.res->read(it.bitmap, data, kTexHeader, &err) && texSize(data, w, h);
            if (!err.empty()) { if (error) *error = err; return false; }
            if (!it.bitmapFound) {
                reason = "icon not found in the item archives";
            } else {
                it.footW = w / 32;
                it.footH = h / 32;
                if (relic && (it.footW != 1 || it.footH != 1)) {
                    char buf[96];
                    std::snprintf(buf, sizeof buf, "footprint %dx%d (icon %dx%d) outside 1..1 x 1..1",
                                  it.footW, it.footH, w, h);
                    reason = buf;
                } else if (it.footW < 1 || it.footW > 2 || it.footH < 1 || it.footH > 5) {
                    char buf[96];
                    std::snprintf(buf, sizeof buf, "footprint %dx%d (icon %dx%d) outside 1..2 x 1..5",
                                  it.footW, it.footH, w, h);
                    reason = buf;
                }
            }
        }
        // inside the item folders the reach scan's list decides (the walk cannot see the loot
        // tables a quest file names); anywhere else the walk does
        const bool walkOut = !reached && !itemPath(key);
        if (reason.empty() && !placed.count(key) &&
            ((!dropRef && nd != nondrop.end()) || (dropRef && (unreached.count(key) || walkOut)))) {
            reason = kUnobtainable;
            if (dropRef) {
                reason += kUnreached;      // the only dropping holders are loot nothing reaches
            } else {
                bool first = true;
                for (const std::string& k : nd->second) {
                    if (!first) reason += ", ";
                    reason += k;
                    first = false;
                }
            }
            if (collectUnobtainable) {
                reason += kKept;
                kept = true;
            }
        }
        return true;
    };
    auto byRecord = [](const ExcludedItem& a, const ExcludedItem& b) { return a.record < b.record; };
    for (auto& c : cands) {
        CatalogueItem& it = c.first;
        std::string reason;
        bool kept = false;
        if (!decide(it, false, reason, kept)) return false;
        if (!reason.empty()) excluded.push_back({it.record, reason, kept});
        if (reason.empty() || kept) items.push_back(std::move(it));
    }
    std::sort(excluded.begin(), excluded.end(), byRecord);
    if (relics) {
        for (RelicItem& r : relicCands) {
            std::string reason;
            bool kept = false;
            if (!decide(r.item, true, reason, kept)) return false;
            if (!reason.empty() && relicLeft) relicLeft->push_back({r.item.record, reason, kept});
            if (reason.empty() || kept) relics->push_back(std::move(r));
        }
        if (relicLeft) std::sort(relicLeft->begin(), relicLeft->end(), byRecord);
    }

    for (const CatalogueItem& it : items) {
        if (it.name.empty()) warnings.push_back("no display text for " + it.nameTag + ": " + it.record);
        if (!it.setRecord.empty() && it.setDisplayName.empty())
            warnings.push_back("set without a display name: " + it.setRecord);
    }
    return true;
}

// ---------------------------------------------------------------- packing
int slotGroupOf(const std::string& slot) {
    static const char* const kSlots[] = {
        "head", "shoulders", "chest", "hands", "waist", "legs", "feet", "axe1h", "mace1h",
        "sword1h", "dagger", "scepter", "axe2h", "mace2h", "sword2h", "spear2h", "ranged1h",
        "ranged2h", "offhand", "shield", "ring", "amulet", "medal", "relic", "blueprint",
        "augment", "consumable", "quest",
    };
    for (int i = 0; i < int(sizeof kSlots / sizeof *kSlots); ++i) if (slot == kSlots[i]) return i;
    return 28;
}

namespace {

// v2 (TQ): the item entry grows from 24 to 28 bytes - footprint w, h (cells), expansion, pad.
const std::uint32_t kFormatVersion = 2, kHeaderSize = 80, kItemEntrySize = 28, kSetEntrySize = 16;
const std::uint32_t kGroupCount = 29;
enum : std::uint16_t {
    kFSetPiece = 1, kFBlueprint = 2, kFAugment = 4, kFRelic = 8, kFFaction = 16,
    kFEquipment = 32, kFDefaultVisible = 64, kFHasBitmap = 128,
    kFMi = 256,   // the KIND mark: a monster infrequent (model/catalogue.h ItemFlag::MonsterInfrequent)
};

struct StringTable {
    std::vector<std::uint8_t> blob{0};
    std::unordered_map<std::string, std::uint32_t> refs{{std::string(), 0}};
    std::uint32_t add(const std::string& s) {
        auto it = refs.find(s);
        if (it != refs.end()) return it->second;
        std::uint32_t ref = std::uint32_t(blob.size());
        blob.insert(blob.end(), s.begin(), s.end());
        blob.push_back(0);
        refs.emplace(s, ref);
        return ref;
    }
};

void put32(std::vector<std::uint8_t>& o, std::uint32_t v) {
    o.push_back(std::uint8_t(v)); o.push_back(std::uint8_t(v >> 8));
    o.push_back(std::uint8_t(v >> 16)); o.push_back(std::uint8_t(v >> 24));
}
void put16(std::vector<std::uint8_t>& o, std::uint16_t v) {
    o.push_back(std::uint8_t(v)); o.push_back(std::uint8_t(v >> 8));
}

bool clampU16(std::int32_t v, std::uint16_t& out) {
    if (v < 0) v = 0;
    if (v > 0xFFFF) return false;
    out = std::uint16_t(v);
    return true;
}

} // namespace

bool packCatalogue(std::vector<CatalogueItem>& items, std::vector<std::uint8_t>& out,
                   CatalogueStats* stats, std::string* error) {
    out.clear();
    struct Row {
        int grp; std::uint16_t lvl, ilvl; const CatalogueItem* it;
    };
    std::vector<Row> rows;
    rows.reserve(items.size());
    for (const CatalogueItem& it : items) {
        Row r{slotGroupOf(it.slot), 0, 0, &it};
        if (!clampU16(it.levelRequirement, r.lvl)) {
            if (error) *error = "levelRequirement out of range on " + it.record;
            return false;
        }
        if (!clampU16(it.itemLevel, r.ilvl)) {
            if (error) *error = "itemLevel out of range on " + it.record;
            return false;
        }
        if (it.footW < 1 || it.footW > 2 || it.footH < 1 || it.footH > 5 || it.expansion < 0
            || it.expansion > 4) {
            if (error) *error = "footprint or expansion out of range on " + it.record;
            return false;
        }
        rows.push_back(r);
    }
    // slot group, KIND (the uniques first), item level, name, record - byte order on UTF-8 is
    // code point order. TQ sorts by the item level, not GD's level requirement, which TQ leaves 0
    // on 1,134 of the 1,582 uniques (the real requirement comes from the itemCostName equations);
    // itemLevel is 0 on one record
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        if (a.grp != b.grp) return a.grp < b.grp;
        if (a.it->isMi != b.it->isMi) return b.it->isMi;
        if (a.ilvl != b.ilvl) return a.ilvl < b.ilvl;
        int c = a.it->name.compare(b.it->name);
        if (c != 0) return c < 0;
        return a.it->record < b.it->record;
    });
    for (std::size_t i = 1; i < rows.size(); ++i)
        if (rows[i].it->record == rows[i - 1].it->record) {
            if (error) *error = "duplicate record " + rows[i].it->record;
            return false;
        }

    // TQ: a set is keyed by its record path folded the way the database folds it (normKey: '\\'
    // -> '/', A-Z -> a-z), because TQ spells 2 set records two ways (x4set_XuanwusRainment); the
    // folded path is also the record string stored for the set
    std::set<std::string> setRecs;
    for (const Row& r : rows)
        if (!r.it->setRecord.empty()) setRecs.insert(ArzArchive::normKey(r.it->setRecord));
    std::vector<std::string> setList(setRecs.begin(), setRecs.end());
    std::unordered_map<std::string, std::uint32_t> setIndex;
    for (std::uint32_t i = 0; i < setList.size(); ++i) setIndex[setList[i]] = i;
    std::vector<std::vector<std::uint32_t>> setMembers(setList.size());
    std::unordered_map<std::string, std::string> setDisplay;
    for (std::uint32_t i = 0; i < rows.size(); ++i) {
        const CatalogueItem& it = *rows[i].it;
        if (it.setRecord.empty()) continue;
        const std::string sk = ArzArchive::normKey(it.setRecord);
        setMembers[setIndex[sk]].push_back(i);
        setDisplay.emplace(sk, it.setDisplayName);
    }

    StringTable st;
    struct ItemRow {
        std::uint32_t rec, name, bmp; std::int32_t set; std::uint8_t cls, grp;
        std::uint16_t lvl, ilvl, flags; std::uint8_t fw, fh, exp;
    };
    std::vector<ItemRow> itemRows;
    itemRows.reserve(rows.size());
    for (const Row& r : rows) {
        const CatalogueItem& it = *r.it;
        std::uint8_t cls;
        if (it.classification == "Epic" || it.classification == "Rare"
            || it.classification == "Common" || it.classification == "Quest") cls = 0;
        else if (it.classification == "Legendary") cls = 1;
        else { if (error) *error = "unexpected itemClassification '" + it.classification + "' on " + it.record; return false; }
        ItemRow row;
        row.rec = st.add(it.record);
        row.name = st.add(it.name);
        row.bmp = st.add(it.bitmap);
        row.set = it.setRecord.empty() ? -1 : std::int32_t(setIndex[ArzArchive::normKey(it.setRecord)]);
        row.cls = cls;
        row.grp = std::uint8_t(r.grp);
        row.lvl = r.lvl;
        row.ilvl = r.ilvl;
        std::uint16_t f = 0;
        if (it.isSetPiece) f |= kFSetPiece;
        if (it.isBlueprint) f |= kFBlueprint;
        if (it.isAugment) f |= kFAugment;
        if (it.isRelic) f |= kFRelic;
        if (startsWith(it.record, "records/items/faction/")) f |= kFFaction;
        if (it.isEquipment) f |= kFEquipment;
        if (it.isEquipment || it.isRelic) f |= kFDefaultVisible;
        if (!it.bitmap.empty()) f |= kFHasBitmap;
        if (it.isMi) f |= kFMi;
        row.flags = f;
        row.fw = std::uint8_t(it.footW);
        row.fh = std::uint8_t(it.footH);
        row.exp = std::uint8_t(it.expansion);
        itemRows.push_back(row);
    }
    struct SetRow { std::uint32_t name, rec, first, count; };
    std::vector<SetRow> setRows;
    std::vector<std::uint32_t> members;
    for (std::uint32_t si = 0; si < setList.size(); ++si) {
        std::uint32_t first = std::uint32_t(members.size());
        members.insert(members.end(), setMembers[si].begin(), setMembers[si].end());
        setRows.push_back({st.add(setDisplay[setList[si]]), st.add(setList[si]), first,
                           std::uint32_t(setMembers[si].size())});
    }
    std::uint32_t groupFirst[kGroupCount] = {}, groupCount[kGroupCount] = {};
    for (std::uint32_t i = 0; i < itemRows.size(); ++i) {
        std::uint8_t gi = itemRows[i].grp;
        if (groupCount[gi] == 0) groupFirst[gi] = i;
        ++groupCount[gi];
    }
    std::uint32_t itemCount = std::uint32_t(itemRows.size()), setCount = std::uint32_t(setRows.size());
    std::uint32_t memberCount = std::uint32_t(members.size());
    std::uint32_t groupOff = kHeaderSize, itemOff = groupOff + kGroupCount * 8;
    std::uint32_t setOff = itemOff + itemCount * kItemEntrySize;
    std::uint32_t memberOff = setOff + setCount * kSetEntrySize;
    std::uint32_t stringOff = memberOff + memberCount * 4;
    std::uint32_t stringSize = std::uint32_t(st.blob.size());
    std::uint32_t total = stringOff + stringSize;
    if (total % 4) total += 4 - total % 4;
    std::uint32_t equipment = 0, relics = 0, visible = 0;
    for (const ItemRow& r : itemRows) {
        if (r.flags & kFEquipment) ++equipment;
        if (r.flags & kFRelic) ++relics;
        if (r.flags & kFDefaultVisible) ++visible;
    }
    out.reserve(total);
    out.insert(out.end(), {'G', 'D', 'U', 'T'});
    const std::uint32_t hdr[19] = {
        kFormatVersion, kHeaderSize, 0, itemCount, kItemEntrySize, itemOff, setCount,
        kSetEntrySize, setOff, memberCount, memberOff, kGroupCount, groupOff, stringSize,
        stringOff, total, visible, equipment, relics,
    };
    for (std::uint32_t v : hdr) put32(out, v);
    for (std::uint32_t gi = 0; gi < kGroupCount; ++gi) { put32(out, groupFirst[gi]); put32(out, groupCount[gi]); }
    for (const ItemRow& r : itemRows) {
        put32(out, r.rec); put32(out, r.name); put32(out, r.bmp); put32(out, std::uint32_t(r.set));
        out.push_back(r.cls); out.push_back(r.grp);
        put16(out, r.lvl); put16(out, r.ilvl); put16(out, r.flags);
        out.push_back(r.fw); out.push_back(r.fh); out.push_back(r.exp); out.push_back(0);
    }
    for (const SetRow& r : setRows) { put32(out, r.name); put32(out, r.rec); put32(out, r.first); put32(out, r.count); }
    for (std::uint32_t m : members) put32(out, m);
    out.insert(out.end(), st.blob.begin(), st.blob.end());
    while (out.size() % 4) out.push_back(0);
    if (stats) {
        stats->items = itemCount; stats->sets = setCount; stats->members = memberCount;
        stats->strings = st.refs.size(); stats->stringBytes = stringSize;
        stats->equipment = equipment; stats->relics = relics; stats->defaultVisible = visible;
        stats->bytes = out.size();
        stats->relicsRare = stats->relicsEpic = stats->relicsLegendary = stats->extras = 0;
        stats->mi = 0;
        for (const CatalogueItem& it : items) {
            if (it.isExtra) ++stats->extras;
            if (it.isMi) ++stats->mi;
            if (!it.isRelic) continue;
            if (it.classification == "Rare") ++stats->relicsRare;
            else if (it.classification == "Epic") ++stats->relicsEpic;
            else if (it.classification == "Legendary") ++stats->relicsLegendary;
        }
    }
    return true;
}

// ---------------------------------------------------------------- the lists
// Ported from GD's pages_gen.cpp (the text half: the slot order, the rank, the name fold and the
// G / E lines). The page records and the F / P / V geometry lines stay with the page,.
namespace {

// The groups come from the group rules (ut_groups.h, ut_groups_default.h): every record is tried
// against the blocks, the first match decides its group, and the groups are written in their
// declaration order with their labels, ids and tags.
// The host page: the grid the caller passes (ui_geometry.h: TQ's Transfer sack, 16 x 15 cells of
// 32 px in every game), so a group's grid is as many of its largest footprint as that holds.
const int kCellPx = 32;

// What the group rules read of a catalogue record (GD's groupItem): a class the catalogue has no
// slot for ("other") is unknown, so an auto group can take it. fromMod: only a custom game's
// layer has the record (a record the game overrides is the base game's); none lies in a faction
// folder.
utg::Item groupItem(const CatalogueItem& it) {
    utg::Item g;
    g.record = it.record;
    g.cls = it.cls;
    g.rank = it.classification;
    if (it.isSetPiece) g.flags |= utg::kFlagSet;
    if (it.isRelic) g.flags |= utg::kFlagRelic;
    if (it.isEquipment || it.isMi) g.flags |= utg::kFlagEquipment;
    if (it.isBlueprint) g.flags |= utg::kFlagBlueprint;
    if (it.isAugment) g.flags |= utg::kFlagAugment;
    if (it.isExtra) g.flags |= utg::kFlagExtra;
    g.fromMod = it.fromMod;
    g.knownClass = it.slot != "other";
    return g;
}
int rankOf(const std::string& c) {
    if (c == "Legendary") return 0;
    if (c == "Epic") return 1;
    if (c == "Rare") return 2;
    if (c == "Common") return 3;
    return 9;
}
std::string lowerName(const std::string& s) {
    std::string o = s;
    for (std::size_t i = 0; i < o.size(); ++i) {
        unsigned char c = (unsigned char)o[i];
        if (c >= 'A' && c <= 'Z') o[i] = char(c + 32);
        else if (c == 0xC3 && i + 1 < o.size()) {
            unsigned char d = (unsigned char)o[i + 1];
            if (d >= 0x80 && d <= 0x9E && d != 0x97) o[i + 1] = char(d + 0x20);
            ++i;
        }
    }
    return o;
}
std::string fmt(const char* f, int a, int b = 0) {
    char buf[160];
    std::snprintf(buf, sizeof buf, f, a, b);
    return buf;
}

} // namespace

void buildLists(const std::vector<CatalogueItem>& items, const std::vector<ExcludedItem>& excluded,
                ListsOutput& out, const utg::GroupSet& rules, const std::vector<std::string>& viewKeys,
                int hostCols, int hostRows) {
    out = ListsOutput();
    std::vector<utg::Item> gitems;
    gitems.reserve(items.size());
    for (const CatalogueItem& it : items) gitems.push_back(groupItem(it));
    utg::Assignment assign;
    utg::assignGroups(rules, gitems, viewKeys, assign);
    struct Picked { const CatalogueItem* it; int order, rank; std::string lname; };
    // the page order: group, rank, lower-case name, item level, record. The item level puts one
    // name's tiers in order (normal < epic < legendary): TQ's level requirement comes from it,
    // levelRequirement being 0 on the monster infrequents; catalogue.bin orders them the same way
    std::vector<Picked> picked;
    picked.reserve(items.size());
    for (std::size_t ii = 0; ii < items.size(); ++ii) {
        const int grp = assign.groupOf[ii];
        if (grp < 0) { ++out.unpaged; continue; }   // an [exclude], or a known class no block takes
        const CatalogueItem& it = items[ii];
        picked.push_back(Picked{&it, grp, rankOf(it.classification), lowerName(it.name)});
    }
    std::sort(picked.begin(), picked.end(), [](const Picked& a, const Picked& b) {
        if (a.order != b.order) return a.order < b.order;
        if (a.rank != b.rank) return a.rank < b.rank;
        int c = a.lname.compare(b.lname);
        if (c != 0) return c < 0;
        if (a.it->itemLevel != b.it->itemLevel) return a.it->itemLevel < b.it->itemLevel;
        return a.it->record < b.it->record;
    });
    struct Group { int def = -1; std::vector<const Picked*> items; int cw = 0, ch = 0; };
    std::vector<Group> groups;
    for (const Picked& p : picked) {
        if (groups.empty() || groups.back().def != p.order) { groups.push_back(Group()); groups.back().def = p.order; }
        Group& gr = groups.back();
        gr.items.push_back(&p);
        gr.cw = std::max(gr.cw, p.it->footW);
        gr.ch = std::max(gr.ch, p.it->footH);
    }
    for (std::size_t gi = 0; gi < groups.size(); ++gi) {
        const Group& gr = groups[gi];
        const int cols = std::max(1, hostCols / std::max(1, gr.cw));
        const int rows = std::max(1, hostRows / std::max(1, gr.ch));
        const utg::GroupDef& def = assign.groups[std::size_t(gr.def)];
        utg::GLine gl;
        gl.index = int(gi);
        gl.label = def.label;
        gl.cols = cols; gl.rows = rows; gl.cellW = gr.cw * kCellPx; gl.cellH = gr.ch * kCellPx;
        gl.count = int(gr.items.size());
        gl.id = def.id;
        gl.tag = def.tag;
        out.groups += utg::formatGLine(gl) + "\r\n";
        for (const Picked* p : gr.items) {
            out.records += p->it->record + "\r\n";
            out.groups += "E\t" + p->it->record + fmt("\t%d\t%d\r\n", p->it->footW, p->it->footH);
        }
    }
    out.groupCount = groups.size();
    for (const ExcludedItem& x : excluded) out.excluded += x.record + "\t" + x.reason + "\r\n";
}

} // namespace gen
