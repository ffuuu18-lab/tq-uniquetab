// catalogue_gen.cpp - see catalogue_gen.h.
#include "gen/catalogue_gen.h"
#include "gen/arc_reader.h"
#include "gen/arz_reader.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <system_error>

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

// records/(xpackN/)item(s)/ - the collection rule's path test, on the lower-cased key.
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

// GD's rule 4 ("FileDescription has no BLANK"), widened to TQ's template words (blank, test, template, copy of).
bool templateWords(const std::string& lowerText) {
    static const char* const kWords[] = {"blank", "test", "template", "copy of"};
    for (const char* w : kWords) if (lowerText.find(w) != std::string::npos) return true;
    return false;
}

bool isShipped(const std::string& key, const ArzRecord& rec, const std::string& cls,
               const GameData& g) {
    if (!itemPath(key)) return false;
    if (templateWords(lowerAscii(rec.str("FileDescription"))) || templateWords(key)) return false;
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

// TQ .tex: "TEX" + version; v1 = GD's v2 layout (the DDS surface at 12), v2 inserts one flag
// byte (the surface at 13). The surface magic is "DDS " or TQ's "DDSR"; its height and width are
// at +12 and +16. Only the header is read, never the pixels.
const std::size_t kTexHeader = 64;
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

} // namespace

// ---------------------------------------------------------------- GameData
GameData::~GameData() {
    delete db;
    delete res;
}

bool GameData::load(const std::string& gameDir, const std::string& lang, std::string* error) {
    delete db; delete res;
    db = new ArzDatabase;
    res = new ResourceArcs;
    tags.clear();
    if (!db->loadGame(gameDir, error)) return false;
    if (!loadTextTags(gameDir, lang, tags, error)) return false;
    res->setRoot(gameDir);
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

// ---------------------------------------------------------------- the collection
bool collectItems(const GameData& g, std::vector<CatalogueItem>& items,
                  std::vector<ExcludedItem>& excluded, std::vector<std::string>& warnings,
                  std::string* error) {
    items.clear();
    excluded.clear();
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
    auto makeEntry = [&](const std::string& key, const ArzEntry& e, const std::string& src,
                         const ArzRecord& rec, const std::string& cls) {
        CatalogueItem it;
        it.record = e.name;
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
        return it;
    };

    // The reachability index, filled in the same pass: for every string of the table that names
    // a .dbr, the first record (winner index) whose field holds it, and whether a second,
    // different record holds it too. A record is referenced when a record OTHER than itself
    // holds its path in any string field (as tools/build_catalogue.py decides it).
    const std::vector<std::pair<std::size_t, const ArzEntry*>>& winners = g.db->winners();
    const std::size_t nArch = g.db->archives().size();
    std::vector<std::vector<std::uint8_t>> isDbr(nArch), multi(nArch);
    std::vector<std::vector<std::int32_t>> firstRef(nArch);
    for (std::size_t ai = 0; ai < nArch; ++ai) {
        const std::vector<std::string>& strs = g.db->archives()[ai].strings();
        isDbr[ai].resize(strs.size());
        multi[ai].assign(strs.size(), 0);
        firstRef[ai].assign(strs.size(), -1);
        for (std::size_t i = 0; i < strs.size(); ++i) {
            const std::string& t = strs[i];
            isDbr[ai][i] = t.size() >= 4 && lowerAscii(t.substr(t.size() - 4)) == ".dbr";
        }
    }

    std::vector<std::pair<CatalogueItem, std::size_t>> cands;   // item, winner index
    ArzRecord rec;
    for (std::size_t wi = 0; wi < winners.size(); ++wi) {
        const auto& w = winners[wi];
        const ArzArchive& a = g.db->archives()[w.first];
        const ArzEntry& e = *w.second;
        const std::string& key = e.key;
        std::string why;
        if (!a.decode(e, rec, &why)) {
            if (error) *error = "record " + e.name + " in " + a.path() + " does not decode (" + why + ")";
            return false;
        }
        for (const ArzField& f : rec.fields) {
            if (f.type != kFtString) continue;
            for (std::uint32_t idx : f.raw) {
                if (idx >= isDbr[w.first].size() || !isDbr[w.first][idx]) continue;
                std::int32_t& fr = firstRef[w.first][idx];
                if (fr < 0) fr = std::int32_t(wi);
                else if (fr != std::int32_t(wi)) multi[w.first][idx] = 1;
            }
        }
        std::string cls = rec.str("Class");
        if (!equipSlot(cls) && !otherSlot(cls)) continue;
        std::string cla = rec.str("itemClassification");
        if (cla != "Epic" && cla != "Legendary") continue;
        if (!isShipped(key, rec, cls, g)) continue;
        cands.emplace_back(makeEntry(key, e, a.tag(), rec, cls), wi);
    }

    // referenced[key] = some record other than the one named key holds the path
    std::unordered_map<std::string, bool> referenced;
    for (std::size_t ai = 0; ai < nArch; ++ai) {
        const std::vector<std::string>& strs = g.db->archives()[ai].strings();
        for (std::size_t i = 0; i < strs.size(); ++i) {
            if (firstRef[ai][i] < 0) continue;
            const std::string k = ArzArchive::normKey(strs[i]);
            const bool other = multi[ai][i] || winners[std::size_t(firstRef[ai][i])].second->key != k;
            bool& r = referenced[k];
            r = r || other;
        }
    }

    // The EXCLUDE rule, first reason that applies.
    std::vector<std::uint8_t> data;
    for (auto& c : cands) {
        CatalogueItem& it = c.first;
        const std::string key = ArzArchive::normKey(it.record);
        std::string reason;
        if (!g.expansionInstalled[it.expansion]) {
            reason = "expansion not installed";
        } else if (it.bitmap.empty()) {
            reason = "no icon field (bitmap / artifactBitmap)";
        } else {
            for (const char* d : kDevFolders) {
                const std::string part = std::string("/") + d + "/";
                if (key.find(part) != std::string::npos) { reason = std::string("developer folder ") + d; break; }
            }
        }
        if (reason.empty()) {
            auto r = referenced.find(key);
            if (r == referenced.end() || !r->second)
                reason = "unreferenced (no loot table, merchant, container, formula, quest or monster holds it)";
        }
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
                if (it.footW < 1 || it.footW > 2 || it.footH < 1 || it.footH > 5) {
                    char buf[96];
                    std::snprintf(buf, sizeof buf, "footprint %dx%d (icon %dx%d) outside 1..2 x 1..5",
                                  it.footW, it.footH, w, h);
                    reason = buf;
                }
            }
        }
        if (!reason.empty()) excluded.push_back({it.record, reason});
        else items.push_back(std::move(it));
    }
    std::sort(excluded.begin(), excluded.end(),
              [](const ExcludedItem& a, const ExcludedItem& b) { return a.record < b.record; });

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
    // slot group, item level, name, record - byte order on UTF-8 is code point order. TQ: the
    // item level, not GD's level requirement, which TQ leaves 0 on 1,137 of the 1,588 records (the
    // real requirement comes from the itemCostName equations); itemLevel is 0 on one record
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        if (a.grp != b.grp) return a.grp < b.grp;
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
            || it.classification == "Common") cls = 0;
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
        for (const CatalogueItem& it : items) {
            if (it.isExtra) ++stats->extras;
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

struct SlotInfo { const char* slot; const char* label; };
// GD's layout order, with Titan Quest's labels on the slots TQ has.
const SlotInfo kSlotOrder[] = {
    {"head", "Helms"}, {"shoulders", "Shoulders"}, {"chest", "Torso"}, {"hands", "Arms"},
    {"waist", "Belts"}, {"legs", "Legs"}, {"feet", "Boots"}, {"amulet", "Amulets"},
    {"medal", "Medals"}, {"ring", "Rings"}, {"offhand", "Off-hands"}, {"shield", "Shields"},
    {"axe1h", "Axes"}, {"dagger", "Daggers"}, {"mace1h", "Maces"}, {"scepter", "Staves"},
    {"sword1h", "Swords"}, {"ranged1h", "Throwing"}, {"axe2h", "2H Axes"}, {"mace2h", "2H Maces"},
    {"spear2h", "Spears"}, {"sword2h", "2H Swords"}, {"ranged2h", "Bows"}, {"relic", "Artifacts"},
};
// The host page: TQ's Transfer sack is a 16 x 15 grid of 32 px cells (measured), so a group's grid is
// as many of its largest footprint as that holds.
const int kHostCols = 16, kHostRows = 15, kCellPx = 32;

int slotOrder(const std::string& slot) {
    for (int i = 0; i < int(sizeof kSlotOrder / sizeof *kSlotOrder); ++i)
        if (slot == kSlotOrder[i].slot) return i;
    return 99;
}
std::string slotLabel(const std::string& slot) {
    for (const SlotInfo& s : kSlotOrder) if (slot == s.slot) return s.label;
    return slot.empty() ? "?" : slot;
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
                ListsOutput& out) {
    out = ListsOutput();
    struct Picked { const CatalogueItem* it; int order, rank; std::string lname; };
    std::vector<Picked> picked;
    picked.reserve(items.size());
    for (const CatalogueItem& it : items)
        picked.push_back(Picked{&it, slotOrder(it.slot), rankOf(it.classification), lowerName(it.name)});
    std::sort(picked.begin(), picked.end(), [](const Picked& a, const Picked& b) {
        if (a.order != b.order) return a.order < b.order;
        if (a.rank != b.rank) return a.rank < b.rank;
        int c = a.lname.compare(b.lname);
        if (c != 0) return c < 0;
        return a.it->record < b.it->record;
    });
    struct Group { std::string slot; std::vector<const Picked*> items; int cw = 0, ch = 0; };
    std::vector<Group> groups;
    for (const Picked& p : picked) {
        if (groups.empty() || groups.back().slot != p.it->slot) { groups.push_back(Group()); groups.back().slot = p.it->slot; }
        Group& gr = groups.back();
        gr.items.push_back(&p);
        gr.cw = std::max(gr.cw, p.it->footW);
        gr.ch = std::max(gr.ch, p.it->footH);
    }
    for (std::size_t gi = 0; gi < groups.size(); ++gi) {
        const Group& gr = groups[gi];
        const int cols = std::max(1, kHostCols / std::max(1, gr.cw));
        const int rows = std::max(1, kHostRows / std::max(1, gr.ch));
        out.groups += fmt("G\t%d\t", int(gi)) + slotLabel(gr.slot) + fmt("\t%d\t%d", cols, rows)
                    + fmt("\t%d\t%d", gr.cw * kCellPx, gr.ch * kCellPx)
                    + fmt("\t%d\r\n", int(gr.items.size()));
        for (const Picked* p : gr.items) {
            out.records += p->it->record + "\r\n";
            out.groups += "E\t" + p->it->record + fmt("\t%d\t%d\r\n", p->it->footW, p->it->footH);
        }
    }
    out.groupCount = groups.size();
    for (const ExcludedItem& x : excluded) out.excluded += x.record + "\t" + x.reason + "\r\n";
}

} // namespace gen
