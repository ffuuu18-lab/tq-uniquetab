// catalogue_gen.h - builds catalogue.bin and the record lists from the game's own archives.
//
// Port of tools/build_catalogue.py (the collection rule, the EXCLUDE rule, names from
// Text_<lang>.arc, the icon footprint) and tools/pack_catalogue.py (the deterministic "GDUT"
// layout, v2 for Titan Quest: the footprint and the expansion per item). The Python tools stay
// the reference: for the same game files this produces the same bytes.
//
// TITAN QUEST. The collection is every Epic or Legendary equipment record
// (ArmorProtective_* / ArmorJewelry_* / Weapon*_* of the classes below) and every artifact
// (ItemArtifact) under records/(xpackN/)item(s)/ whose name tag resolves and that is no
// template ("blank / test / template / copy of") - 1,602 records on AE 2.10, an upper bound -
// minus the EXCLUDE rule, every dropped record listed with its reason (uniq-excluded.txt):
//   the expansion is not installed (the DLC filter), no icon field at all,
//   a developer folder (_devcheat, devitem, sandbox, dev), no other record references it (no
//   loot table, merchant, container, formula, quest or monster holds its path), the icon is not
//   in the item archives, the footprint is outside 1..2 x 1..5 cells.
// Two decisions (HANDOFF.md section 6): the records only monsters hold STAY
// (monsters drop what they hold), and records that share a display name STAY separate
// (distinct records, distinct drops).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

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
};

// TQ: a record the collection rule accepted and the EXCLUDE rule dropped, with the reason.
struct ExcludedItem {
    std::string record;
    std::string reason;
};

struct CatalogueStats {
    std::size_t items = 0, sets = 0, members = 0, strings = 0, stringBytes = 0;
    std::size_t equipment = 0, relics = 0, defaultVisible = 0, bytes = 0;
    std::size_t relicsRare = 0, relicsEpic = 0, relicsLegendary = 0, extras = 0;
};

// The game data one generation reads; loaded once.
struct GameData {
    bool load(const std::string& gameDir, const std::string& lang, std::string* error);
    ArzDatabase* db = nullptr;                                   // owned
    ResourceArcs* res = nullptr;                                 // owned: the item archives
    std::unordered_map<std::string, std::string> tags;
    bool expansionInstalled[5] = {true, false, false, false, false};   // TQ: the DLC filter
    GameData() = default;
    ~GameData();
    GameData(const GameData&) = delete;
    GameData& operator=(const GameData&) = delete;
};

// The collection: every record build_catalogue.py puts in catalogue.json's "items", and every
// record it lists in "excluded" (sorted by record). `warnings` receives the checks the Python
// tool asserts (they do not stop a generation).
bool collectItems(const GameData& g, std::vector<CatalogueItem>& items,
                  std::vector<ExcludedItem>& excluded, std::vector<std::string>& warnings,
                  std::string* error);

// pack_catalogue.py: items -> catalogue.bin bytes. False (with the reason) on bad input.
bool packCatalogue(std::vector<CatalogueItem>& items, std::vector<std::uint8_t>& out,
                   CatalogueStats* stats, std::string* error);

// The three text files beside catalogue.bin (CRLF, as the DLL writes them):
//   uniq-records.txt   every catalogue record, one per line, in group order
//   uniq-groups.txt    G <n> <label> <cols> <rows> <cellW> <cellH> <count>, then one
//                      E <record> <w> <h> per member - GD's group model without the F / P / V
//                      page-geometry lines, which belong to the page
//   uniq-excluded.txt  <record> TAB <reason>, one per dropped record
// The order is GD's pages_gen order: slot order, Legendary before Epic, name, record.
struct ListsOutput {
    std::string records, groups, excluded;
    std::size_t groupCount = 0;
};
void buildLists(const std::vector<CatalogueItem>& items, const std::vector<ExcludedItem>& excluded,
                ListsOutput& out);

// Slot word -> the on-disk group index of model/catalogue.h (28 = Other).
int slotGroupOf(const std::string& slot);

} // namespace gen
