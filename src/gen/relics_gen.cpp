// relics_gen.cpp - see relics_gen.h.
#include "gen/relics_gen.h"

#include <algorithm>
#include <map>
#include <utility>

namespace gen {

namespace {

std::string foldKey(std::string s) {
    for (char& c : s) {
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    }
    return s;
}

bool endsWithDbr(const std::string& b) { return b.size() >= 4 && b.compare(b.size() - 4, 4, ".dbr") == 0; }

// ^(.*)_<mark>(_.*)?\.dbr$ with the longest (.*): <mark> is one of n, e, l (dForm false) or d
// followed by 1..3 (dForm true). The tier and <stem><rest> on a match.
bool tierInfix(const std::string& b, bool dForm, int* tier, std::string* stem) {
    const std::size_t mark = dForm ? 3 : 2;   // "_n" / "_d1"
    if (!endsWithDbr(b) || b.size() < mark + 4) return false;
    for (std::size_t p = b.size() - mark - 4 + 1; p-- > 0;) {
        if (b[p] != '_') continue;
        int t = 0;
        if (dForm) {
            if (b[p + 1] != 'd' || b[p + 2] < '1' || b[p + 2] > '3') continue;
            t = b[p + 2] - '0';
        } else {
            const char c = b[p + 1];
            t = c == 'n' ? 1 : c == 'e' ? 2 : c == 'l' ? 3 : 0;
            if (!t) continue;
        }
        const std::string rest = b.substr(p + mark);
        std::string tail;
        if (rest == ".dbr") {
            tail.clear();
        } else if (rest[0] == '_' && rest.size() >= 5) {
            tail = rest.substr(0, rest.size() - 4);
        } else {
            continue;
        }
        *tier = t;
        *stem = b.substr(0, p) + tail;
        return true;
    }
    return false;
}

int pathExpansion(const std::string& key) {
    if (key.compare(0, 13, "records/xpack") != 0) return 0;
    const char c = key.size() > 13 ? key[13] : 0;
    if (c == '/') return 1;
    if (c >= '2' && c <= '4' && key.size() > 14 && key[14] == '/') return c - '0';
    return 0;
}

} // namespace

int relicBlockOf(const std::string& key, std::string* blockId) {
    const std::size_t slash = key.rfind('/');
    const std::string dir = slash == std::string::npos ? std::string() : key.substr(0, slash);
    const std::string b = slash == std::string::npos ? key : key.substr(slash + 1);
    std::string stem;
    int tier = 0;
    if (b.size() >= 7 && b[0] == '0' && b[1] >= '1' && b[1] <= '3' && b[2] == '_' && endsWithDbr(b)) {
        tier = b[1] - '0';
        stem = b.substr(3, b.size() - 7);
    } else if (!tierInfix(b, false, &tier, &stem) && !tierInfix(b, true, &tier, &stem)) {
        tier = 0;
        stem = endsWithDbr(b) ? b.substr(0, b.size() - 4) : b;
    }
    if (blockId) *blockId = dir + "/" + stem;
    return tier;
}

int relicExpansionOf(const std::string& key) {
    const std::size_t slash = key.rfind('/');
    const std::string b = slash == std::string::npos ? key : key.substr(slash + 1);
    const bool ee = b.compare(0, 3, "x4_") == 0 ||
                    (b.size() >= 6 && b[0] == '0' && b[1] >= '1' && b[1] <= '5' && b[2] == '_' &&
                     b.compare(3, 3, "x4_") == 0);
    return ee ? 4 : pathExpansion(key);
}

std::string relicsListText(const std::vector<RelicItem>& relics, const std::vector<ExcludedItem>& left,
                           RelicListCounts* counts) {
    struct Row { int tier; std::string key; const RelicItem* r; };
    std::map<std::pair<std::string, std::string>, std::vector<Row>> blocks;   // (class, block id)
    for (const RelicItem& r : relics) {
        Row row;
        row.key = foldKey(r.item.record);
        std::string bid;
        row.tier = relicBlockOf(row.key, &bid);
        row.r = &r;
        blocks[{r.item.cls, bid}].push_back(std::move(row));
    }
    struct Block { int part = 0, exp = 0; std::int32_t lvl = 0; std::string bid; std::vector<Row> rows; };
    std::vector<Block> order;
    for (auto& kv : blocks) {
        Block b;
        b.rows = std::move(kv.second);
        std::sort(b.rows.begin(), b.rows.end(), [](const Row& x, const Row& y) {
            return x.tier != y.tier ? x.tier < y.tier : x.key < y.key;
        });
        b.part = kv.first.first == "ItemRelic" ? 0 : 1;
        b.exp = relicExpansionOf(b.rows[0].key);
        b.lvl = b.rows[0].r->item.levelRequirement;
        b.bid = kv.first.second;
        order.push_back(std::move(b));
    }
    std::sort(order.begin(), order.end(), [](const Block& x, const Block& y) {
        if (x.part != y.part) return x.part < y.part;
        if (x.exp != y.exp) return x.exp < y.exp;
        if (x.lvl != y.lvl) return x.lvl < y.lvl;
        return x.bid < y.bid;
    });
    RelicListCounts c;
    std::string out;
    std::size_t n = 0;
    for (const Block& b : order) {
        out += "B\t" + std::to_string(++n) + "\t" + b.bid + "\t" + (b.part == 0 ? "R" : "C") + "\t" +
               std::to_string(b.rows.size()) + "\r\n";
        ++(b.part == 0 ? c.relicBlocks : c.charmBlocks);
        if (b.rows.size() == 1) ++c.oneLineBlocks;
        for (const Row& row : b.rows) {
            const RelicItem& r = *row.r;
            out += "R\t" + r.item.record + "\t" + std::to_string(row.tier) + "\t" + std::to_string(r.shards) +
                   "\t" + r.item.bitmap + "\t" + r.shardBitmap + "\t" + std::to_string(r.bonuses.size());
            for (const std::string& bonus : r.bonuses) out += "\t" + bonus;
            out += "\r\n";
            ++c.rows;
            c.bonuses += r.bonuses.size();
        }
    }
    for (const ExcludedItem& x : left) out += "X\t" + x.record + "\t" + x.reason + "\r\n";
    c.left = left.size();
    if (counts) *counts = c;
    return out;
}

} // namespace gen
