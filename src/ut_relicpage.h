// ut_relicpage.h - the relic and charm page (pure: no engine, no Windows calls).
//
// uniq-relics.txt (the generator's list, one B line per block followed by its R lines in tier
// order, then the X lines) is read into blocks and rows; the blocks are laid out on the page's
// cells: per row a red slot in column 0 (a DEAD cell for a record of one shard), `bonusGap` empty
// columns, then the bonus slots in the table's order, wrapping onto the next line from the same
// first bonus column; `blockGap` empty lines between blocks; a block is never split.
//
// Paging: the FIXED pages are computed once from the data at the area's size (as many whole
// blocks as fit, no break between the relic part and the charm part); the window shows as many
// whole blocks as fit from its first block; the wheel moves that first block by one (it stops
// when the last block is shown); `<` `>` (PageUp / PageDown) jump to the previous / next fixed
// page start. A place is keyed (row, slot): slot 0 = the red (or dead) cell, 1 + i = bonus i.
//
// What a place shows (utRelicPlaceState) comes from the record's journal facts
// (UtRelicRowFacts, ut_relicroute.h): a green slot counts its rows not pending out and shows the
// OLDEST of them (utRelicGreenChoice); the red slot counts the shards of its partial rows not
// pending out and shows a 1-shard piece of the row the next take splits or hands out whole
// (utRelicShardSource); an empty slot shows the synthetic item (red: var1 1; green: var1 N and
// the table's bonus), gray.
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "ut_relicroute.h"

namespace ut {

struct UtRelicRow {
    std::string record;                     // as the database spells it (the R line)
    std::string folded;                     // lower case, '/' (utRelicFold)
    int tier = 0;                           // 0..3 (0 = the path names no tier)
    unsigned shards = 0;                    // N = completedRelicLevel
    std::string relicBitmap, shardBitmap;   // the icons (the gray copies are keyed by these)
    std::vector<std::string> bonuses;       // as the table spells them, table order
    std::vector<std::string> bonusFolded;   // the same, folded
    int block = -1;                         // its block
};

struct UtRelicBlock {
    std::string id;     // folder / stem, folded
    char part = 'R';    // 'R' relic, 'C' charm (the class decides)
    int firstRow = 0;   // its rows in UtRelicList::rows, tier order
    int rows = 0;
};

struct UtRelicList {
    std::vector<UtRelicBlock> blocks;
    std::vector<UtRelicRow> rows;
    std::vector<int> byFolded;   // row indexes sorted by folded record (utRelicFind)
    int excluded = 0;            // X lines
    int bonusSlots = 0;          // every row's bonus count summed
    int relicBlocks = 0, charmBlocks = 0;
};

namespace relicpage_detail {
inline void splitTabs(const std::string& line, std::vector<std::string>* f) {
    f->clear();
    size_t a = 0;
    for (;;) {
        const size_t b = line.find('\t', a);
        f->push_back(line.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (b == std::string::npos) break;
        a = b + 1;
    }
}
inline bool toInt(const std::string& s, long lo, long hi, long* v) {
    if (s.empty() || s.size() > 9) return false;
    for (size_t i = 0; i < s.size(); ++i)
        if (s[i] < '0' || s[i] > '9') return false;
    *v = strtol(s.c_str(), nullptr, 10);
    return *v >= lo && *v <= hi;
}
inline std::string fold(const std::string& s) {
    std::string o(s);
    for (size_t i = 0; i < o.size(); ++i) {
        const char c = o[i];
        o[i] = c == '\\' ? '/' : (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    return o;
}
}  // namespace relicpage_detail

// Reads uniq-relics.txt. False (and `err` says the line) on any malformed line; an empty file is
// a list without blocks.
inline bool utRelicListRead(const std::string& text, UtRelicList* out, std::string* err) {
    using namespace relicpage_detail;
    UtRelicList l;
    std::vector<std::string> f;
    size_t pos = 0;
    int lineNo = 0;
    char buf[160];
    while (pos < text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(pos, e - pos);
        pos = e + 1;
        ++lineNo;
        if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
        if (line.empty()) continue;
        splitTabs(line, &f);
        long v = 0, t = 0, n = 0;
        if (f[0] == "B" && f.size() == 5 && toInt(f[1], 1, 100000, &v) &&
            (int)v == (int)l.blocks.size() + 1 && (f[3] == "R" || f[3] == "C") &&
            toInt(f[4], 1, 3, &n)) {
            UtRelicBlock b;
            b.id = f[2];
            b.part = f[3][0];
            b.firstRow = (int)l.rows.size();
            l.blocks.push_back(b);
            if (b.part == 'R') ++l.relicBlocks;
            else ++l.charmBlocks;
            continue;
        }
        if (f[0] == "R" && f.size() >= 7 && !l.blocks.empty() && !f[1].empty() &&
            toInt(f[2], 0, 3, &t) && toInt(f[3], 1, 1000, &v) && toInt(f[6], 1, 1000, &n) &&
            f.size() == (size_t)(7 + n)) {
            UtRelicRow r;
            r.record = f[1];
            r.folded = fold(f[1]);
            r.tier = (int)t;
            r.shards = (unsigned)v;
            r.relicBitmap = f[4];
            r.shardBitmap = f[5];
            for (long i = 0; i < n; ++i) {
                r.bonuses.push_back(f[(size_t)(7 + i)]);
                r.bonusFolded.push_back(fold(f[(size_t)(7 + i)]));
            }
            r.block = (int)l.blocks.size() - 1;
            l.blocks.back().rows++;
            l.bonusSlots += (int)n;
            l.rows.push_back(r);
            continue;
        }
        if (f[0] == "X" && f.size() >= 3) {
            ++l.excluded;
            continue;
        }
        snprintf(buf, sizeof(buf), "uniq-relics.txt line %d is not a B, R or X line", lineNo);
        if (err) *err = buf;
        return false;
    }
    for (size_t b = 0; b < l.blocks.size(); ++b) {
        if (l.blocks[b].rows <= 0) {
            snprintf(buf, sizeof(buf), "uniq-relics.txt block %d has no R line", (int)b + 1);
            if (err) *err = buf;
            return false;
        }
    }
    l.byFolded.resize(l.rows.size());
    for (size_t i = 0; i < l.rows.size(); ++i) l.byFolded[i] = (int)i;
    const UtRelicList* lp = &l;
    std::vector<int>& idx = l.byFolded;
    for (size_t i = 1; i < idx.size(); ++i) {   // insertion sort: a few hundred rows, once
        const int x = idx[i];
        size_t j = i;
        while (j > 0 && lp->rows[(size_t)idx[j - 1]].folded > lp->rows[(size_t)x].folded) {
            idx[j] = idx[j - 1];
            --j;
        }
        idx[j] = x;
    }
    *out = l;
    return true;
}

// The row of a folded record (lower case, '/'), -1 = not on the list. No allocation.
// The take's journal call by the place's prototype slot: none or 0 = a unique's place (the take
// of its newest row); 1 = the red slot (ONE shard; the cursor then holds the shard's own row);
// 2 and above = a green slot (its row whole).
enum UtRelicTakeCall { kUtRelicTakeUnique = 0, kUtRelicTakeShard = 1, kUtRelicTakeRow = 2 };
inline UtRelicTakeCall utRelicTakeCallOf(bool haveSlot, int slot) {
    if (!haveSlot || slot <= 0) return kUtRelicTakeUnique;
    return slot == 1 ? kUtRelicTakeShard : kUtRelicTakeRow;
}
// The row the cursor holds after the take: the shard's own row for a red take the journal wrote,
// else the place's row.
inline unsigned long long utRelicTakeHeld(UtRelicTakeCall c, bool taken, unsigned long long seq,
                                          unsigned long long shardSeq) {
    return c == kUtRelicTakeShard && taken ? shardSeq : seq;
}
// A relic take the journal refused (the page is stale): the page is rebuilt from the journal.
inline bool utRelicTakeRebuild(UtRelicTakeCall c, bool taken) {
    return c != kUtRelicTakeUnique && !taken;
}
// The relic facts are taken again only when the journal's write count moved since the last list.
inline bool utRelicRelistDue(long listedWrites, long writes) { return listedWrites != writes; }
// The unique pages' "+N not on these pages" counts a held record no unique page lists, unless it
// is a relic / charm of the relic page (`relicRow` = its row there, -1 = none).
inline bool utNotShownCounts(bool onAUniquePage, int relicRow) {
    return !onAUniquePage && relicRow < 0;
}

inline int utRelicFind(const UtRelicList& l, const char* folded) {
    if (!folded) return -1;
    size_t lo = 0, hi = l.byFolded.size();
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        const int c = strcmp(l.rows[(size_t)l.byFolded[mid]].folded.c_str(), folded);
        if (c == 0) return l.byFolded[mid];
        if (c < 0) lo = mid + 1;
        else hi = mid;
    }
    return -1;
}

// ---- the layout --------------------------------------------------------------------------------
struct UtRelicGeometry {
    int cols = 16, rows = 15;   // the area in cells
    int blockGap = 1;           // empty lines between blocks (0..2)
    int bonusGap = 1;           // empty columns between the red slot and the first bonus (0..1)
};

// The relic layout's geometry from the relic area in force (its whole cells) and the two gap
// settings, each kept to its range (relic_block_gap 0..2, relic_bonus_gap 0..1).
inline UtRelicGeometry utRelicGeometryOf(int cols, int rows, int blockGap, int bonusGap) {
    UtRelicGeometry g;
    g.cols = cols;
    g.rows = rows;
    g.blockGap = blockGap < 0 ? 0 : blockGap > 2 ? 2 : blockGap;
    g.bonusGap = bonusGap < 0 ? 0 : bonusGap > 1 ? 1 : bonusGap;
    return g;
}
inline bool utRelicGeometrySame(const UtRelicGeometry& a, const UtRelicGeometry& b) {
    return a.cols == b.cols && a.rows == b.rows && a.blockGap == b.blockGap && a.bonusGap == b.bonusGap;
}

struct UtRelicLayout {
    UtRelicGeometry geo;
    int perLine = 0;               // bonus slots on one line
    std::vector<int> rowLines;     // lines of each row (wrapped)
    std::vector<int> blockLines;   // lines of each block
    std::vector<int> pageStart;    // the fixed pages: their first block
    std::vector<int> blockOf;      // the list block laid out at each position (OWN leaves blocks out)
    int tallBlocks = 0;            // blocks with more lines than the area (shown cut)
    int wrappedRows = 0;           // rows longer than a line
};

// `keep` (one flag per list block, null = every block): the OWN filter lays out only the blocks that
// hold something, each whole; positions (pages, windows, the wheel) then count the kept blocks.
inline bool utRelicLayoutBuild(const UtRelicList& l, const UtRelicGeometry& g, UtRelicLayout* out,
                               const unsigned char* keep = nullptr) {
    UtRelicLayout y;
    y.geo = g;
    if (g.cols < 2 || g.rows < 1 || g.blockGap < 0 || g.bonusGap < 0) return false;
    y.perLine = g.cols - 1 - g.bonusGap;
    if (y.perLine < 1) return false;
    y.rowLines.resize(l.rows.size());
    for (size_t r = 0; r < l.rows.size(); ++r) {
        const int n = (int)l.rows[r].bonuses.size();
        y.rowLines[r] = n <= 0 ? 1 : (n + y.perLine - 1) / y.perLine;
        if (y.rowLines[r] > 1) ++y.wrappedRows;
    }
    for (size_t b = 0; b < l.blocks.size(); ++b) {
        if (keep && !keep[b]) continue;
        int s = 0;
        for (int k = 0; k < l.blocks[b].rows; ++k) s += y.rowLines[(size_t)(l.blocks[b].firstRow + k)];
        y.blockOf.push_back((int)b);
        y.blockLines.push_back(s);
        if (s > g.rows) ++y.tallBlocks;
    }
    // the fixed pages: whole blocks, greedy, a page holds at least one block
    const int nb = (int)y.blockLines.size();
    int b = 0;
    while (b < nb) {
        y.pageStart.push_back(b);
        int used = y.blockLines[(size_t)b];
        ++b;
        while (b < nb && used + g.blockGap + y.blockLines[(size_t)b] <= g.rows) {
            used += g.blockGap + y.blockLines[(size_t)b];
            ++b;
        }
    }
    *out = y;
    return true;
}

inline int utRelicPageCount(const UtRelicLayout& y) { return (int)y.pageStart.size(); }

// One past the last block the window from block `first` shows (at least one block).
inline int utRelicWindowEnd(const UtRelicLayout& y, int first) {
    const int nb = (int)y.blockLines.size();
    if (first < 0 || first >= nb) return first < 0 ? 0 : nb;
    int used = y.blockLines[(size_t)first];
    int b = first + 1;
    while (b < nb && used + y.geo.blockGap + y.blockLines[(size_t)b] <= y.geo.rows) {
        used += y.geo.blockGap + y.blockLines[(size_t)b];
        ++b;
    }
    return b;
}

// The fixed page that holds block `b` (0-based; 0 for an empty list).
inline int utRelicPageOf(const UtRelicLayout& y, int b) {
    int p = 0;
    for (size_t i = 1; i < y.pageStart.size(); ++i)
        if (y.pageStart[i] <= b) p = (int)i;
    return p;
}

// The wheel: one block per notch. Down stops once the last block is shown; up stops at block 0.
inline int utRelicWheelStep(const UtRelicLayout& y, int first, int dir) {
    const int nb = (int)y.blockLines.size();
    if (nb <= 0) return 0;
    if (first < 0) first = 0;
    if (first >= nb) first = nb - 1;
    if (dir > 0) return utRelicWindowEnd(y, first) < nb ? first + 1 : first;
    if (dir < 0) return first > 0 ? first - 1 : 0;
    return first;
}

// `<` `>` / PageUp / PageDown: the previous / next FIXED page start (clamped to the first / last).
inline int utRelicPageStep(const UtRelicLayout& y, int first, int dir) {
    if (y.pageStart.empty()) return 0;
    if (dir > 0) {
        for (size_t i = 0; i < y.pageStart.size(); ++i)
            if (y.pageStart[i] > first) return y.pageStart[i];
        return first < y.pageStart.back() ? y.pageStart.back() : first;
    }
    if (dir < 0) {
        int best = y.pageStart[0];
        for (size_t i = 0; i < y.pageStart.size(); ++i)
            if (y.pageStart[i] < first) best = y.pageStart[i];
        return best;
    }
    return first;
}

// The first block a window may start on (a stored offset from another layout is clamped).
inline int utRelicClampFirst(const UtRelicLayout& y, int first) {
    const int nb = (int)y.blockLines.size();
    if (nb <= 0 || first < 0) return 0;
    return first >= nb ? nb - 1 : first;
}

// ---- the places of a window: THE API the page draw reads --------------------------------------
enum UtRelicSlotKind {
    kUtRelicSlotRed = 1,     // the record's shards (a record of N >= 2 shards)
    kUtRelicSlotGreen = 2,   // one completion bonus of the record
    kUtRelicSlotDead = 3     // column 0 of a record of ONE shard: no plate, no prototype
};

struct UtRelicPlace {
    int row;    // the record's row in UtRelicList::rows
    int slot;   // 0 = the red / dead cell, 1 + i = bonus i (table order)
    int kind;   // UtRelicSlotKind
    int col;    // the cell in the area
    int line;
};

// Every place of the window from block `first`, in reading order; returns how many were written
// (at most `cap`; the lines past the area's last line of a cut block are left out).
inline int utRelicWindowPlaces(const UtRelicList& l, const UtRelicLayout& y, int first,
                               UtRelicPlace* out, int cap) {
    int n = 0;
    if (!out || cap <= 0) return 0;
    const int end = utRelicWindowEnd(y, first);
    int line = 0;
    for (int b = first < 0 ? 0 : first; b < end; ++b) {
        const UtRelicBlock& bk = l.blocks[(size_t)y.blockOf[(size_t)b]];
        for (int k = 0; k < bk.rows; ++k) {
            const int r = bk.firstRow + k;
            const UtRelicRow& row = l.rows[(size_t)r];
            if (line < y.geo.rows && n < cap) {
                out[n].row = r;
                out[n].slot = 0;
                out[n].kind = row.shards >= 2u ? kUtRelicSlotRed : kUtRelicSlotDead;
                out[n].col = 0;
                out[n].line = line;
                ++n;
            }
            for (int i = 0; i < (int)row.bonuses.size(); ++i) {
                const int ln = line + i / y.perLine;
                if (ln >= y.geo.rows || n >= cap) continue;
                out[n].row = r;
                out[n].slot = 1 + i;
                out[n].kind = kUtRelicSlotGreen;
                out[n].col = 1 + y.geo.bonusGap + i % y.perLine;
                out[n].line = ln;
                ++n;
            }
            line += y.rowLines[(size_t)r];
        }
        line += y.geo.blockGap;
    }
    return n;
}

// What one place shows, from its record's journal facts (taken once per journal change).
struct UtRelicPlaceState {
    unsigned count;           // green: rows not pending out; red: their shards; dead: 0
    bool empty;               // nothing to hand out: the synthetic prototype, gray
    unsigned long long seq;   // the row the prototype is built from (0 = synthetic)
    bool split;               // red: the take splits `seq` (false = hands it out whole)
    unsigned protoVar1;       // the prototype's shard count: red 1, green N, dead 0
};

inline void utRelicPlaceState(const UtRelicRow& row, int slot, const UtRelicRowFacts* facts, int n,
                              UtRelicPlaceState* out) {
    memset(out, 0, sizeof(*out));
    out->empty = true;
    if (slot <= 0) {
        if (row.shards < 2u) return;   // dead
        out->protoVar1 = 1;
        out->count = facts && n > 0 ? utRelicRedShards(facts, n) : 0u;
        bool split = false;
        const int k = facts && n > 0 ? utRelicShardSource(facts, n, &split) : -1;
        if (k >= 0) {
            out->empty = false;
            out->seq = facts[k].seq;
            out->split = split;
        }
        return;
    }
    const int i = slot - 1;
    if (i >= (int)row.bonusFolded.size()) return;
    out->protoVar1 = row.shards;
    const char* b = row.bonusFolded[(size_t)i].c_str();
    out->count = facts && n > 0 ? utRelicGreenCount(facts, n, b) : 0u;
    const int k = facts && n > 0 ? utRelicGreenChoice(facts, n, b) : -1;
    if (k >= 0) {
        out->empty = false;
        out->seq = facts[k].seq;
    }
}

// The place key the prototype list compares (with the record and the row's seq): 0 is a unique's
// slot, a relic place is 1 + its slot.
inline int utRelicProtoSlot(int slot) { return 1 + (slot < 0 ? 0 : slot); }

// ---- OWN: the blocks that hold something -------------------------------------------------------
// `state` holds every row's places (red, then each bonus) from `stateFirst[row]`. A block holds
// something when any slot of any of its rows counts at least one (a red slot its shards, a green
// slot its rows). Writes one flag per block into `keep`; returns how many blocks hold something.
inline int utRelicBlocksHeld(const UtRelicList& l, const int* stateFirst, const UtRelicPlaceState* state,
                             unsigned char* keep) {
    int held = 0;
    for (size_t b = 0; b < l.blocks.size(); ++b) {
        bool any = false;
        for (int k = 0; k < l.blocks[b].rows && !any; ++k) {
            const int r = l.blocks[b].firstRow + k;
            const int n = 1 + (int)l.rows[(size_t)r].bonuses.size();
            for (int s = 0; s < n && !any; ++s) any = state[stateFirst[r] + s].count > 0u;
        }
        keep[b] = (unsigned char)(any ? 1 : 0);
        held += any ? 1 : 0;
    }
    return held;
}

// ---- the search: one text per (record, bonus) ---------------------------------------------------
// Only a GREEN slot has a text (the completed relic with that bonus, its rollover in the game's
// words); the red slot is never marked. `match` holds one flag per state (stateFirst + slot).
inline bool utRelicSearchHit(const unsigned char* match, int stateIndex, int slot, bool query) {
    return query && match && slot > 0 && stateIndex >= 0 && match[stateIndex] != 0;
}
// The matches the relic page can show: green slots whose text holds the query, in the blocks the
// page lays out (`keep` = OWN's flags, null = every block).
inline int utRelicSearchFound(const UtRelicList& l, const int* stateFirst, const unsigned char* match,
                              const unsigned char* keep) {
    int found = 0;
    for (size_t r = 0; match && r < l.rows.size(); ++r) {
        const int b = l.rows[r].block;
        if (keep && b >= 0 && !keep[b]) continue;
        for (size_t i = 0; i < l.rows[r].bonuses.size(); ++i)
            if (match[stateFirst[r] + 1 + (int)i]) ++found;
    }
    return found;
}
// The green slots the page lays out (`keep` = OWN's flags, null = every block): the search's "of".
inline int utRelicGreenSlots(const UtRelicList& l, const unsigned char* keep) {
    int n = 0;
    for (size_t r = 0; r < l.rows.size(); ++r) {
        const int b = l.rows[r].block;
        if (keep && b >= 0 && !keep[b]) continue;
        n += (int)l.rows[r].bonuses.size();
    }
    return n;
}

// ---- the have-mark: (record, bonus) -------------------------------------------------------------
// The owned set's key of a completed relic or charm the player carries: the folded record, '>',
// the folded bonus record. False when it does not fit `cap` or the bonus is empty (a partial piece:
// the red slot never gets the mark).
inline bool utRelicHaveKey(const char* recordFolded, const char* bonus, char* out, size_t cap) {
    if (!out || !cap) return false;
    out[0] = 0;
    if (!recordFolded || !recordFolded[0] || !bonus || !bonus[0]) return false;
    const size_t n = strlen(recordFolded);
    if (n + 2 >= cap) return false;
    memcpy(out, recordFolded, n);
    out[n] = '>';
    if (!utRelicFold(bonus, out + n + 1, cap - n - 1)) {
        out[0] = 0;
        return false;
    }
    return true;
}

// ---- the rollover line of a relic or charm ------------------------------------------------------
// A partial piece (no bonus, fewer shards than N) speaks of its record's RED slot in one line:
// "In your collection" with the slot's own count - the shards of the stored pieces, rows pending
// out left out (utRelicRedShards) - or "Not in your collection". A completed one speaks of its
// (record, bonus) GREEN slot: "In your collection" when it holds one, "(another roll)" when only
// other bonuses of the record are held, else "Not in your collection". Rows pending out do not
// count (a take in flight). Anything else (a completed one without a bonus) gets no line.
struct UtRelicTip {
    int state;         // 0 no line, 1 in, 2 not in, 3 another roll
    unsigned pieces;   // a partial's state 1: the red slot's shards; else 0
    unsigned shards;   // N
};
inline void utRelicTipOf(const UtRelicRow& row, unsigned var1, const char* bonus,
                         const UtRelicRowFacts* f, int n, UtRelicTip* out) {
    memset(out, 0, sizeof(*out));
    out->shards = row.shards;
    const bool hasBonus = bonus && bonus[0];
    if (!hasBonus && var1 < row.shards) {   // a partial: var1 0 and 1 are both one shard
        out->pieces = utRelicRedShards(f, n);
        out->state = out->pieces > 0 ? 1 : 2;
        return;
    }
    if (!hasBonus || var1 < row.shards) return;
    char b[kUtRelicBonusMax];
    if (!utRelicFold(bonus, b, sizeof(b))) return;
    if (utRelicGreenCount(f, n, b) > 0u) {
        out->state = 1;
        return;
    }
    for (int i = 0; f && i < n; ++i)
        if (!utRelicRowIsRed(f[i]) && f[i].pending != kUtRelicPendOut) {
            out->state = 3;
            return;
        }
    out->state = 2;
}

// A partial piece's one line: the collected words in force (the buttons file's tooltip.yes) and
// the red slot's shards in the relic label's words, "In your collection (pieces 15)".
inline void utRelicTipLine(const char* yes, unsigned pieces, char* out, size_t cap) {
    if (!out || cap == 0) return;
    _snprintf_s(out, cap, _TRUNCATE, "%s (pieces %u)", yes ? yes : "", pieces);
}

}  // namespace ut
