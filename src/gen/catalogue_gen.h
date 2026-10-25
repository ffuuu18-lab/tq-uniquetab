// catalogue_gen.h - builds catalogue.bin and the record lists from the game's own archives.
//
// Port of tools/build_catalogue.py (the collection rule, the EXCLUDE rule, names from
// Text_<lang>.arc, the icon footprint) and tools/pack_catalogue.py (the deterministic "GDUT"
// layout, v2 for Titan Quest: the footprint and the expansion per item). The Python tools stay
// the reference: for the same game files this produces the same bytes.
//
// TITAN QUEST. The collection is every Epic or Legendary equipment record
// (ArmorProtective_* / ArmorJewelry_* / Weapon*_* of the classes below) and every artifact
// (ItemArtifact) anywhere in the database but a creature folder (GD's junk-folder rule, the
// same for the base game and a custom game) whose name tag resolves, and every arcane formula
// (ItemArtifactFormula) at any classification under the same rule (its icon is
// artifactFormulaBitmapName) - 1,715 records on AE 2.10, an upper bound. A template word
// ("blank / test / template / copy of" as a whole word in the FileDescription or the path)
// drops a record only when nothing hands it out: no other record holds it in a field that can
// drop it and the level and quest archives do not place it. Then the EXCLUDE rule, every
// dropped record listed with its reason (uniq-excluded.txt):
//   the expansion is not installed (the DLC filter), no icon field at all,
//   a developer folder (_devcheat, devitem, sandbox, dev) unless the reach walk below reaches
//   it, no other record references it (no loot table, merchant, container, formula, quest or
//   monster holds its path) and the archives do not place it, the icon is not
//   in the item archives, the footprint is outside 1..2 x 1..5 cells, and (rule 7) unobtainable:
//   other records hold its path, but none in a field that can drop it - not setMembers, not a
//   formula's reagent* fields, not a worn slot (loot<Slot>Item<n>, defaultHeadPiece) of an Npc /
//   Guard / NpcMerchant / NpcEnchanter / NpcSkillReallocator / Pet, not a worn slot of a Monster
//   with dropItems=0, not lootMisc<n>Item<n> of those non-dropper classes - and the game's level
//   and quest archives do not place it (placed_records.h). Its reason names the holder kinds.
//   A record a reachability scan of the level and quest archives cannot reach although a
//   database field could drop it (it sits only in loot tables nothing reaches;
//   placed_records.h kUnreachedRecords) is unobtainable too. Outside the item folders
//   (records/(xpackN/)item(s)/) the reach walk decides instead: from every record that is not a
//   loot table (Class Loot*: a monster, a chest, a merchant, a formula) down the fields that can
//   drop, through the loot tables, never entering a holder in a developer folder or with a
//   template word in its path; a record there it does not reach (only a loot table nothing
//   names holds it) is unobtainable. The walk reads the database only, not the loot tables a
//   quest file names, so it does not decide inside the item folders.
//   collectUnobtainable keeps the rule 7 records in the catalogue, still listed, marked kept.
// MONSTER INFREQUENTS (collectMi = uniquetab.ini's collect_mi): every equipment record of the
// classes below whose own itemClassification is Rare (isMiCandidate; TQ has no Rare rings,
// amulets or artifacts), through the same filter and the same EXCLUDE rule - 1,118 records on
// AE 2.10 before EXCLUDE. Each carries isMi (catalogue.bin flag bit 8, model/catalogue.h
// catIsMi), packed as Epic, sorted after the uniques of its group.
// RELICS AND CHARMS (relics_gen.h, uniq-relics.txt): every ItemRelic / ItemCharm record under the
// same filter whose completion bonus table (bonusTableName) names at least one bonus of non-zero
// weight, through the same EXCLUDE rule (its icon is relicBitmap, 1 x 1 cells). They are not
// catalogue items: no group, page or list but uniq-relics.txt takes them.
// Two decisions (HANDOFF.md section 6): the records only monsters hold STAY
// (monsters drop what they hold), and records that share a display name STAY separate
// (distinct records, distinct drops).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace utg { struct GroupSet; }

namespace gen {

class ArzDatabase;
class ResourceArcs;

// One accepted record, the fields catalogue.json carries.
struct CatalogueItem {
    std::string record;            // real path (original case, '/' separators)
    std::string cls;               // Class
    std::string slot;              // head, ring, relic ... or "other"
    std::string craftsRecord;
    std::string nameTag;
    std::string name;              // UTF-8 display name, "" when the tag has no text
    std::string classification;    // itemClassification word
    std::string setRecord;         // itemSetName
    std::string setDisplayName;
    std::string bitmap;
    std::string source;            // archive tag
    std::int32_t levelRequirement = 0;
    std::int32_t itemLevel = 0;
    int footW = 0, footH = 0;      // TQ: the inventory footprint in cells (icon pixels / 32)
    int expansion = 0;             // TQ: 0 base, 1 Immortal Throne, 2 Ragnarok, 3 Atlantis, 4 Eternal Embers
    bool bitmapFound = false;
    bool isBlueprint = false, isAugment = false, isRelic = false, isSetPiece = false;
    bool isEquipment = false, isExtra = false;
    bool isMi = false;             // KIND mark: a Rare equipment record (a monster infrequent)
    bool fromMod = false;          // only a custom game's own layer has it (the group rules' source)
};

// TQ: a record the collection rule accepted and the EXCLUDE rule dropped, with the reason.
struct ExcludedItem {
    std::string record;
    std::string reason;
    bool kept = false;             // rule 7 with collectUnobtainable: listed, and in the catalogue
};

// A relic or charm record (uniq-relics.txt's R line): `item` carries the record, the class, the
// name, levelRequirement, the expansion, relicBitmap as its icon and the icon's footprint.
struct RelicItem {
    CatalogueItem item;
    std::string shardBitmap;
    std::int32_t shards = 0;               // completedRelicLevel: the shards that complete it
    std::vector<std::string> bonuses;      // the completion bonus records, as the table spells them
};

struct CatalogueStats {
    std::size_t items = 0, sets = 0, members = 0, strings = 0, stringBytes = 0;
    std::size_t equipment = 0, relics = 0, defaultVisible = 0, bytes = 0;
    std::size_t relicsRare = 0, relicsEpic = 0, relicsLegendary = 0, extras = 0;
    std::size_t mi = 0;            // monster infrequents (flag bit 8)
};

// The monster-infrequent rule: a Rare record of an equipment class. The key and the
// FileDescription stay in the signature: a later record-local rule needs no caller change.
bool isMiCandidate(const std::string& key, const std::string& cls,
                   const std::string& classification, const std::string& fileDescription);

// The game data one generation reads; loaded once. gameArz / gameResources: a custom game's
// .arz and Resources folder (generate.h GameSpec), "" for the start-up game - its records over
// database.arz, its Text.arc tags over Text_<lang>.arc, its archives searched first.
struct GameData {
    bool load(const std::string& gameDir, const std::string& lang, std::string* error,
              const std::string& gameArz = std::string(),
              const std::string& gameResources = std::string());
    ArzDatabase* db = nullptr;                                   // owned
    ResourceArcs* res = nullptr;                                 // owned: the item archives
    std::unordered_map<std::string, std::string> tags;
    bool expansionInstalled[5] = {true, false, false, false, false};   // TQ: the DLC filter
    GameData() = default;
    ~GameData();
    GameData(const GameData&) = delete;
    GameData& operator=(const GameData&) = delete;
};

// A .tex header (the first 64 bytes are enough) -> the surface's pixel size; false when it is
// not a TQ texture header.
bool texSize(const std::vector<std::uint8_t>& d, int& w, int& h);

// The collection: every record build_catalogue.py puts in catalogue.json's "items", and every
// record it lists in "excluded" (sorted by record). `warnings` receives the checks the Python
// tool asserts (they do not stop a generation). `relics` (when given) receives the relic and
// charm records, `relicLeft` the ones the EXCLUDE rule left out (sorted by record).
bool collectItems(const GameData& g, std::vector<CatalogueItem>& items,
                  std::vector<ExcludedItem>& excluded, std::vector<std::string>& warnings,
                  std::string* error, bool collectUnobtainable = false, bool collectMi = false,
                  std::vector<RelicItem>* relics = nullptr, std::vector<ExcludedItem>* relicLeft = nullptr);

// pack_catalogue.py: items -> catalogue.bin bytes. False (with the reason) on bad input.
bool packCatalogue(std::vector<CatalogueItem>& items, std::vector<std::uint8_t>& out,
                   CatalogueStats* stats, std::string* error);

// The three text files beside catalogue.bin (CRLF, as the DLL writes them):
//   uniq-records.txt   every paged catalogue record, one per line, in group order
//   uniq-groups.txt    G <n> <label> <cols> <rows> <cellW> <cellH> <count> <id> <tag> (GD's
//                      formatGLine, ut_groups.h), then one E <record> <w> <h> per member - GD's
//                      group model without the F / P / V page-geometry lines, which belong to the
//                      page
//   uniq-excluded.txt  <record> TAB <reason>, one per dropped record
// The order is GD's pages_gen order: group order, Legendary before Epic before Rare (the
// monster infrequents after the uniques), name, record. The groups are the group rules'
// (ut_groups.h): `rules` = the built-in set, or the player's uniquetab-groups.ini merged in front
// of it (ut_groups_default.h loadGroupsFile); `viewKeys` = the keys the generation answers to
// (ut_groups.h viewKeysFor: a custom game's leaf; "collect_mi" while collect_mi=1). A record no group takes (an [exclude], or a known class no
// block names) is on no page and not in uniq-records.txt; it stays in catalogue.bin (`unpaged`
// counts them).
struct ListsOutput {
    std::string records, groups, excluded;
    std::size_t groupCount = 0;
    std::size_t unpaged = 0;
};
// hostCols x hostRows: the grid the pages are laid out on (ui_geometry.h; 16 x 15 cells, the
// Transfer sack TQ.exe sizes).
void buildLists(const std::vector<CatalogueItem>& items, const std::vector<ExcludedItem>& excluded,
                ListsOutput& out, const utg::GroupSet& rules, const std::vector<std::string>& viewKeys,
                int hostCols = 16, int hostRows = 15);

// Slot word -> the on-disk group index of model/catalogue.h (28 = Other).
int slotGroupOf(const std::string& slot);

} // namespace gen
