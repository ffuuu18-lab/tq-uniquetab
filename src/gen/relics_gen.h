// relics_gen.h - uniq-relics.txt: the relic and charm records in blocks, in page order.
//
// Port of tools/build_catalogue.py's relic part (relic_block, relic_expansion, relic_order,
// relics_text): for the same records the same bytes. collectItems (catalogue_gen.h) gives the
// records - every ItemRelic / ItemCharm record under the collection's filter whose completion
// bonus table names at least one bonus of non-zero weight, through the same EXCLUDE rule with a
// 1 x 1 icon (relicBitmap) - and the records that rule leaves out.
//
// The blocks join a record's difficulty tiers by its basename (the folded key): 0N_<stem>
// (N = 1..3), else <stem>_n|_e|_l[_<rest>], else <stem>_dN[_<rest>] (N = 1..3); any other record
// is a block of its own with tier 0. The block id is the folder and the stem (<stem><rest>), with
// no ".dbr". The order: the relic blocks (Class ItemRelic), then the charm blocks (ItemCharm);
// inside each by expansion (a basename that starts x4_, after its 0N_ when it has one, is Eternal
// Embers wherever its folder is), then the first row's levelRequirement, then the block id (bytes).
// A block's rows go in tier order, then by key.
//
// The file, CRLF, tab-separated, UTF-8:
//   B <block no. from 1> <block id> <R relic | C charm> <rows in the block>
//   R <record> <tier 0..3> <shards to complete> <relicBitmap> <shardBitmap> <bonus count> <bonus>...
//   X <record> <reason, the words of uniq-excluded.txt>   by record, after every block
// No group, page or uniq-records.txt line takes these records; uniq-excluded.txt does not list
// the X records.
#pragma once

#include "gen/catalogue_gen.h"

#include <cstddef>
#include <string>
#include <vector>

namespace gen {

// The tier (0..3) of a folded record key ('/' separators, lower-case ASCII); *blockId receives the
// block id.
int relicBlockOf(const std::string& key, std::string* blockId);

// The expansion a block is ordered by: 4 for a basename that starts x4_ (after its 0N_), else
// the record path's (catalogue_gen expansionOf).
int relicExpansionOf(const std::string& key);

struct RelicListCounts {
    std::size_t relicBlocks = 0, charmBlocks = 0, oneLineBlocks = 0, rows = 0, bonuses = 0, left = 0;
};

// uniq-relics.txt from the accepted records and the left-out ones (sorted by record).
std::string relicsListText(const std::vector<RelicItem>& relics, const std::vector<ExcludedItem>& left,
                           RelicListCounts* counts = nullptr);

} // namespace gen
