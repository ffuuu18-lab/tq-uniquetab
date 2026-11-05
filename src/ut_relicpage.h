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
// page start. A place is keyed (row, slot): slot 0 = the red (or dead) cell, or an artifact's
// plain slot, 1 + i = bonus i.
//
// What a place shows (utRelicPlaceState) comes from the record's journal facts
// (UtRelicRowFacts, ut_relicroute.h): a green slot counts its rows not pending out and shows the
// OLDEST of them (utRelicGreenChoice); the red slot counts the shards of its partial rows not
// pending out and shows a 1-shard piece of the row the next take splits or hands out whole
// (utRelicShardSource); an empty slot shows the synthetic item (red: var1 1; green: var1 N and
// the table's bonus), gray.
//
// The artifacts (collect_artifact_bonuses=1, the list's part A) are their own page, laid out the
// same way with places of 2 x 2 cells (UtRelicGeometry::slot): per artifact its FORMULAE in column 0
// (the list's F lines, a 2 x 1 cell each, one under the other: UtRelicFormulaPlace, unique places of
// their own), its green slots, and its PLAIN slot last in the flow, after the greens and wrapping
// like one (every row no green slot takes: utArtifactPlainChoice); every take of a slot hands out a
// whole row. The relic page lays out parts R and C, the artifact page part A (utRelicPartKeep).
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
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
    int state = 0;                          // its first state (red, then each bonus): the
                                            // per-state tables' index, every row's places in turn
    bool artifact = false;                  // part A: shards 0, its formulae in column 0
    std::vector<std::string> formulae;      // part A: the F lines, the catalogue formulae that craft it
    std::vector<std::string> formulaFolded; // the same, folded
    int formulaFirst = 0;                   // its first formula's index (UtRelicList::formulae)
};

struct UtRelicBlock {
    std::string id;     // folder / stem, folded
    std::string name;   // utRelicBlockName of its tiers' names ("" = the list names none)
    char part = 'R';    // 'R' relic, 'C' charm, 'A' artifact (the class decides)
    int firstRow = 0;   // its rows in UtRelicList::rows, tier order
    int rows = 0;
};

struct UtRelicList {
    std::vector<UtRelicBlock> blocks;
    std::vector<UtRelicRow> rows;
    std::vector<int> byFolded;   // row indexes sorted by folded record (utRelicFind)
    int excluded = 0;            // X lines
    int bonusSlots = 0;          // every row's bonus count summed
    int formulae = 0;            // every row's F lines summed (the formula cells)
    int relicBlocks = 0, charmBlocks = 0, artifactBlocks = 0;
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

// A tag as the page shows it: no colour codes ({^y}, ^y), and of the gender alternatives
// ("[ms]A [fs]B", the translated tags) the first, without its marker.
inline std::string utRelicNameClean(const std::string& in) {
    std::string s;
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '{' && i + 3 < in.size() && in[i + 1] == '^' && in[i + 3] == '}') {
            i += 3;
        } else if (in[i] == '^' && i + 1 < in.size()) {
            ++i;
        } else if (in[i] == '[' && i + 3 < in.size() && in[i + 1] >= 'a' && in[i + 1] <= 'z' &&
                   in[i + 2] >= 'a' && in[i + 2] <= 'z' && in[i + 3] == ']') {
            if (s.find_first_not_of(' ') != std::string::npos) break;
            i += 3;
        } else {
            s.push_back(in[i]);
        }
    }
    const size_t a = s.find_first_not_of(' '), z = s.find_last_not_of(' ');
    return a == std::string::npos ? std::string() : s.substr(a, z - a + 1);
}

// A block's name from its first and last tier's names: the whole words they share at the end
// ("Essence of Hecate's Crescent" + "Incarnation of Hecate's Crescent" -> "Hecate's Crescent",
// "Peng Claw" + "Legendary Peng Claw" -> "Peng Claw"), without a list of tier words. Words are cut
// at blanks: with none before the shared end the first tier's whole name stays. A block of one
// tier has only its own name: the game's English tier words are taken off it.
inline std::string utRelicBlockName(const std::string& firstTag, const std::string& lastTag) {
    const std::string one = utRelicNameClean(firstTag), two = utRelicNameClean(lastTag);
    const std::string& first = one.empty() ? two : one;
    const std::string& last = two.empty() ? one : two;
    std::string s = first;
    if (first != last) {
        size_t n = 0;
        while (n < first.size() && n < last.size() &&
               first[first.size() - 1 - n] == last[last.size() - 1 - n])
            ++n;
        size_t at = first.size() - n;
        while (at < first.size() && at > 0 && first[at - 1] != ' ') ++at;   // whole words only
        if (at < first.size()) s = first.substr(at);
    } else {
        static const char* const kTier[] = {"Essence of ", "Embodiment of ", "Incarnation of ", "Epic ",
                                            "Legendary "};
        for (const char* t : kTier)
            if (s.compare(0, strlen(t), t) == 0) s.erase(0, strlen(t));
    }
    if (s.compare(0, 3, "of ") == 0) s.erase(0, 3);
    if (s.compare(0, 4, "the ") == 0) s.erase(0, 4);
    return s;
}

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
        if (f[0] == "B" && f.size() >= 5 && toInt(f[1], 1, 100000, &v) &&
            (int)v == (int)l.blocks.size() + 1 && (f[3] == "R" || f[3] == "C" || f[3] == "A") &&
            toInt(f[4], 1, f[3] == "A" ? 1 : 3, &n)) {
            UtRelicBlock b;
            b.id = f[2];
            if (f.size() == 7)   // a tab inside a name: no name; an artifact's is its own, whole
                b.name = f[3] == "A" ? utRelicNameClean(f[5]) : utRelicBlockName(f[5], f[6]);
            b.part = f[3][0];
            b.firstRow = (int)l.rows.size();
            l.blocks.push_back(b);
            if (b.part == 'R') ++l.relicBlocks;
            else if (b.part == 'C') ++l.charmBlocks;
            else ++l.artifactBlocks;
            continue;
        }
        const bool art = !l.blocks.empty() && l.blocks.back().part == 'A';   // shards 0, any bonuses
        if (f[0] == "R" && f.size() >= 7 && !l.blocks.empty() && !f[1].empty() &&
            toInt(f[2], 0, art ? 0 : 3, &t) && toInt(f[3], art ? 0 : 1, art ? 0 : 1000, &v) &&
            toInt(f[6], art ? 0 : 1, 1000, &n) && f.size() == (size_t)(7 + n)) {
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
            r.state = (int)l.rows.size() + l.bonusSlots;
            r.artifact = art;
            r.formulaFirst = l.formulae;
            l.blocks.back().rows++;
            l.bonusSlots += (int)n;
            l.rows.push_back(r);
            continue;
        }
        if (f[0] == "F" && f.size() == 2 && !f[1].empty() && art && !l.rows.empty() &&
            l.rows.back().block == (int)l.blocks.size() - 1) {   // after its artifact's R line
            UtRelicRow& r = l.rows.back();
            r.formulae.push_back(f[1]);
            r.formulaFolded.push_back(fold(f[1]));
            ++l.formulae;
            continue;
        }
        if (f[0] == "X" && f.size() >= 3) {
            ++l.excluded;
            continue;
        }
        snprintf(buf, sizeof(buf), "uniq-relics.txt line %d is not a B, R, F or X line", lineNo);
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

// relic_sort: each part's blocks by name, or back in the game's order (the relics stay before the
// charms, the charms before the artifacts). The rows stay where they are; a row's block index
// follows its block.
inline int utRelicPartRank(char part) { return part == 'R' ? 0 : part == 'C' ? 1 : 2; }
inline void utRelicListSort(UtRelicList* l, bool byName) {
    const int n = (int)l->blocks.size();
    std::vector<int> order((size_t)n), remap((size_t)n);
    std::vector<std::string> key((size_t)n);
    for (int i = 0; i < n; ++i) {
        order[(size_t)i] = i;
        key[(size_t)i] = relicpage_detail::fold(l->blocks[(size_t)i].name);
    }
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const char pa = l->blocks[(size_t)a].part, pb = l->blocks[(size_t)b].part;
        if (pa != pb) return utRelicPartRank(pa) < utRelicPartRank(pb);
        if (!byName) return l->blocks[(size_t)a].firstRow < l->blocks[(size_t)b].firstRow;
        return key[(size_t)a] < key[(size_t)b];
    });
    std::vector<UtRelicBlock> sorted;
    sorted.reserve((size_t)n);
    for (int k = 0; k < n; ++k) {
        sorted.push_back(l->blocks[(size_t)order[(size_t)k]]);
        remap[(size_t)order[(size_t)k]] = k;
    }
    l->blocks.swap(sorted);
    for (UtRelicRow& r : l->rows) r.block = remap[(size_t)r.block];
}

// The row of a folded record (lower case, '/'), -1 = not on the list. No allocation.
// The take's journal call by the place's prototype slot: none or 0 = a unique's place (the take
// of its newest row); 1 = the red slot (ONE shard; the cursor then holds the shard's own row);
// 2 and above = a green slot (its row whole). An artifact's every slot, its plain one too, hands
// out its row whole (`artifact`: the record is on the list's part A).
enum UtRelicTakeCall { kUtRelicTakeUnique = 0, kUtRelicTakeShard = 1, kUtRelicTakeRow = 2 };
inline UtRelicTakeCall utRelicTakeCallOf(bool haveSlot, int slot, bool artifact = false) {
    if (!haveSlot || slot <= 0) return kUtRelicTakeUnique;
    return slot == 1 && !artifact ? kUtRelicTakeShard : kUtRelicTakeRow;
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
// The relic facts are taken again only when the journal's facts generation (journalFactsGen)
// moved since the last list.
inline bool utRelicRelistDue(long listedGen, long gen) { return listedGen != gen; }
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
    int nameLine = 0;           // relic_names: each block's name on the line above it
    int slot = 1;               // a place's cells per side: 1 (relics, charms), 2 (artifacts)
};

// The line a window's first block starts on (its name's line above it), and the lines between
// blocks: a block's name sits in the gap, so with names the gap is at least that one line.
inline int utRelicTopLines(const UtRelicGeometry& g) { return g.nameLine ? 1 : 0; }
inline int utRelicGapLines(const UtRelicGeometry& g) { return g.nameLine && g.blockGap < 1 ? 1 : g.blockGap; }

// The relic layout's geometry from the relic area in force (its whole cells) and the settings,
// each kept to its range (relic_block_gap 0..2, relic_bonus_gap 0..1).
inline UtRelicGeometry utRelicGeometryOf(int cols, int rows, int blockGap, int bonusGap, int nameLine = 0,
                                         int slot = 1) {
    UtRelicGeometry g;
    g.cols = cols;
    g.rows = rows;
    g.nameLine = nameLine ? 1 : 0;
    g.slot = slot == 2 ? 2 : 1;
    g.blockGap = blockGap < 0 ? 0 : blockGap > 2 ? 2 : blockGap;
    g.bonusGap = bonusGap < 0 ? 0 : bonusGap > 1 ? 1 : bonusGap;
    return g;
}
inline bool utRelicGeometrySame(const UtRelicGeometry& a, const UtRelicGeometry& b) {
    return a.cols == b.cols && a.rows == b.rows && a.blockGap == b.blockGap && a.bonusGap == b.bonusGap &&
           a.nameLine == b.nameLine && a.slot == b.slot;
}

struct UtRelicLayout {
    UtRelicGeometry geo;
    int perLine = 0;               // bonus slots on one line (of `geo.slot` lines each)
    std::vector<int> rowLines;     // lines of each row (wrapped), in cells
    std::vector<int> blockLines;   // lines of each block
    std::vector<int> pageStart;    // the fixed pages: their first block
    std::vector<int> blockOf;      // the list block laid out at each position (OWN leaves blocks out)
    std::vector<unsigned char> shown;   // per state: the slot is shown (empty = every slot)
    std::vector<unsigned char> formulaShown;   // per formula: its cell is shown (empty = every one)
    int tallBlocks = 0;            // blocks with more lines than the area (shown cut)
    int wrappedRows = 0;           // rows longer than a line
};

// `keep` (one flag per list block, null = every block): the OWN filter lays out only the blocks that
// hold something, each whole; positions (pages, windows, the wheel) then count the kept blocks.
// `slots` (one flag per state, UtRelicRow::state + slot; null = every slot), relic_own_slots: only
// the flagged slots are shown, each on its own cell (a hidden one is ground); a row without one
// gets no line, a block without one is left out. `formulae` (one flag per list formula, null = every
// one) the same for the formula cells: a row with a shown formula keeps its lines. The layout keeps a
// copy of the flags. An artifact row's flow is its greens and its plain slot (n + 1 places); its
// formulae (one line each) make it as tall as their count when that is more.
inline bool utRelicLayoutBuild(const UtRelicList& l, const UtRelicGeometry& g, UtRelicLayout* out,
                               const unsigned char* keep = nullptr, const unsigned char* slots = nullptr,
                               const unsigned char* formulae = nullptr) {
    UtRelicLayout y;
    y.geo = g;
    const int z = g.slot == 2 ? 2 : 1;
    if (g.cols < 2 * z || g.rows < z || g.blockGap < 0 || g.bonusGap < 0) return false;
    y.perLine = (g.cols - z - g.bonusGap) / z;
    if (y.perLine < 1) return false;
    y.rowLines.resize(l.rows.size());
    for (size_t r = 0; r < l.rows.size(); ++r) {
        const UtRelicRow& row = l.rows[r];
        const int n = (int)row.bonuses.size();
        const int flow = n + (row.artifact ? 1 : 0);   // an artifact's plain slot after its greens
        y.rowLines[r] = z * (flow <= 0 ? 1 : (flow + y.perLine - 1) / y.perLine);
        if (y.rowLines[r] > z) ++y.wrappedRows;
        const int nf = (int)row.formulae.size();
        if (nf > y.rowLines[r]) y.rowLines[r] = nf;   // a third formula adds a line
        bool any = !slots;
        for (int s = 0; s <= n && !any; ++s) any = slots[row.state + s] != 0;
        for (int k = 0; k < nf && !any; ++k) any = !formulae || formulae[row.formulaFirst + k] != 0;
        if (!any) y.rowLines[r] = 0;
    }
    if (slots) y.shown.assign(slots, slots + l.rows.size() + (size_t)l.bonusSlots);
    if (formulae) y.formulaShown.assign(formulae, formulae + l.formulae);
    for (size_t b = 0; b < l.blocks.size(); ++b) {
        if (keep && !keep[b]) continue;
        int s = 0;
        for (int k = 0; k < l.blocks[b].rows; ++k) s += y.rowLines[(size_t)(l.blocks[b].firstRow + k)];
        if (s <= 0) continue;   // relic_own_slots: nothing of the block is shown
        y.blockOf.push_back((int)b);
        y.blockLines.push_back(s);
        if (s + utRelicTopLines(g) > g.rows) ++y.tallBlocks;
    }
    // the fixed pages: whole blocks, greedy, a page holds at least one block
    const int nb = (int)y.blockLines.size();
    int b = 0;
    while (b < nb) {
        y.pageStart.push_back(b);
        int used = utRelicTopLines(g) + y.blockLines[(size_t)b];
        ++b;
        while (b < nb && used + utRelicGapLines(g) + y.blockLines[(size_t)b] <= g.rows) {
            used += utRelicGapLines(g) + y.blockLines[(size_t)b];
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
    int used = utRelicTopLines(y.geo) + y.blockLines[(size_t)first];
    int b = first + 1;
    while (b < nb && used + utRelicGapLines(y.geo) + y.blockLines[(size_t)b] <= y.geo.rows) {
        used += utRelicGapLines(y.geo) + y.blockLines[(size_t)b];
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
    kUtRelicSlotDead = 3,    // column 0 of a record of ONE shard, or of an artifact (its formulae's
                             // column: UtRelicFormulaPlace): no plate, no prototype
    kUtRelicSlotPlain = 4    // an artifact's last slot (flow place n): its rows no green slot takes
};

struct UtRelicPlace {
    int row;    // the record's row in UtRelicList::rows
    int slot;   // 0 = the red / dead / plain cell, 1 + i = bonus i (table order)
    int kind;   // UtRelicSlotKind
    int col;    // the cell in the area (the place's top left one)
    int line;
    int size;   // its cells per side (the layout's slot)
};

// A formula cell of the artifact page (column 0, line k of its row for formula k): a unique's place
// of the formula's catalogue record, kept apart from the relic places (their index is the states').
struct UtRelicFormulaPlace {
    int row;     // the artifact's row
    int index;   // the formula's index in the list (UtRelicRow::formulaFirst + k)
    int col, line;
    int w, h;    // 2 x 1 cells (the slot's width, one line)
};

// The blocks of one page: the relic page lays out parts R and C, the artifact page part A; `own`
// (null = every block) is OWN's held flags. One flag per list block into `keep`.
inline void utRelicPartKeep(const UtRelicList& l, bool artifacts, const unsigned char* own,
                            unsigned char* keep) {
    for (size_t b = 0; b < l.blocks.size(); ++b)
        keep[b] = (unsigned char)(((l.blocks[b].part == 'A') == artifacts && (!own || own[b])) ? 1 : 0);
}

// Every place of the window from block `first`, in reading order; returns how many were written
// (at most `cap`; the lines past the area's last line of a cut block are left out). `formulae`
// (when given) receives the window's formula cells, at most `fcap`, their number in `*fn`.
inline int utRelicWindowPlaces(const UtRelicList& l, const UtRelicLayout& y, int first,
                               UtRelicPlace* out, int cap, UtRelicFormulaPlace* formulae = nullptr,
                               int fcap = 0, int* fn = nullptr) {
    int n = 0;
    if (fn) *fn = 0;
    if (!out || cap <= 0) return 0;
    const int end = utRelicWindowEnd(y, first);
    const int z = y.geo.slot == 2 ? 2 : 1;
    int line = utRelicTopLines(y.geo);
    for (int b = first < 0 ? 0 : first; b < end; ++b) {
        const UtRelicBlock& bk = l.blocks[(size_t)y.blockOf[(size_t)b]];
        for (int k = 0; k < bk.rows; ++k) {
            const int r = bk.firstRow + k;
            const UtRelicRow& row = l.rows[(size_t)r];
            if (y.rowLines[(size_t)r] <= 0) continue;   // relic_own_slots: nothing of the row shown
            // relic_own_slots: a hidden red slot is dead, ground (column 0 stays the row's, for the
            // panels and names); an artifact's column 0 is always dead (its formulae are their own
            // places)
            const unsigned char* shown = y.shown.empty() ? nullptr : &y.shown[(size_t)row.state];
            if (line + z <= y.geo.rows && n < cap) {
                out[n].row = r;
                out[n].slot = 0;
                out[n].kind = shown && !shown[0] ? kUtRelicSlotDead
                              : row.artifact     ? kUtRelicSlotDead
                              : row.shards >= 2u ? kUtRelicSlotRed
                                                 : kUtRelicSlotDead;
                out[n].col = 0;
                out[n].line = line;
                out[n].size = z;
                ++n;
            }
            for (int i = 0; i < (int)row.bonuses.size(); ++i) {
                const int ln = line + z * (i / y.perLine);
                if (ln + z > y.geo.rows || n >= cap || (shown && !shown[1 + i])) continue;
                out[n].row = r;
                out[n].slot = 1 + i;
                out[n].kind = kUtRelicSlotGreen;
                out[n].col = z + y.geo.bonusGap + z * (i % y.perLine);
                out[n].line = ln;
                out[n].size = z;
                ++n;
            }
            const int nb = (int)row.bonuses.size();   // an artifact's plain slot: flow place nb, last
            const int pl = line + z * (nb / y.perLine);
            if (row.artifact && pl + z <= y.geo.rows && n < cap && !(shown && !shown[0])) {
                out[n].row = r;
                out[n].slot = 0;
                out[n].kind = kUtRelicSlotPlain;
                out[n].col = z + y.geo.bonusGap + z * (nb % y.perLine);
                out[n].line = pl;
                out[n].size = z;
                ++n;
            }
            for (int j = 0; formulae && fn && j < (int)row.formulae.size(); ++j) {
                const int fi = row.formulaFirst + j;
                const bool fs = y.formulaShown.empty() ||
                                (fi < (int)y.formulaShown.size() && y.formulaShown[(size_t)fi]);
                if (!fs || line + j + 1 > y.geo.rows || *fn >= fcap) continue;   // relic_own_slots: ground
                formulae[*fn] = UtRelicFormulaPlace{r, fi, 0, line + j, z, 1};
                ++*fn;
            }
            line += y.rowLines[(size_t)r];
        }
        line += utRelicGapLines(y.geo);
    }
    return n;
}

// What one place shows, from its record's journal facts (taken once per journal change).
struct UtRelicPlaceState {
    unsigned count;           // green: rows not pending out; red: their shards; dead: 0
    bool empty;               // nothing to hand out: the synthetic prototype, gray
    unsigned long long seq;   // the row the prototype is built from (0 = synthetic)
    bool split;               // red: the take splits `seq` (false = hands it out whole)
    unsigned protoVar1;       // the prototype's shard count: red 1, green N, dead 0 (an artifact 0)
};

// An artifact row's table holds `bonus` (folded or not).
inline bool utRelicRowLists(const UtRelicRow& row, const char* bonus) {
    for (size_t i = 0; i < row.bonusFolded.size(); ++i)
        if (utRelicFoldSame(row.bonusFolded[i].c_str(), bonus)) return true;
    return false;
}

inline void utRelicPlaceState(const UtRelicRow& row, int slot, const UtRelicRowFacts* facts, int n,
                              UtRelicPlaceState* out) {
    memset(out, 0, sizeof(*out));
    out->empty = true;
    if (slot <= 0 && row.artifact) {   // the plain slot: whole rows, the oldest first
        auto listed = [&row](const char* b) { return utRelicRowLists(row, b); };
        out->count = facts && n > 0 ? utArtifactPlainCount(facts, n, listed) : 0u;
        const int k = facts && n > 0 ? utArtifactPlainChoice(facts, n, listed) : -1;
        if (k >= 0) {
            out->empty = false;
            out->seq = facts[k].seq;
        }
        return;
    }
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
// slot its rows), or one of its formulae is stored (`formulaRows`: per list formula, its rows; null =
// none). Writes one flag per block into `keep`; returns how many blocks hold something.
inline int utRelicBlocksHeld(const UtRelicList& l, const int* stateFirst, const UtRelicPlaceState* state,
                             unsigned char* keep, const unsigned* formulaRows = nullptr) {
    int held = 0;
    for (size_t b = 0; b < l.blocks.size(); ++b) {
        bool any = false;
        for (int k = 0; k < l.blocks[b].rows && !any; ++k) {
            const int r = l.blocks[b].firstRow + k;
            const int n = 1 + (int)l.rows[(size_t)r].bonuses.size();
            for (int s = 0; s < n && !any; ++s) any = state[stateFirst[r] + s].count > 0u;
            const UtRelicRow& row = l.rows[(size_t)r];
            for (int f = 0; formulaRows && f < (int)row.formulae.size() && !any; ++f)
                any = formulaRows[row.formulaFirst + f] > 0u;
        }
        keep[b] = (unsigned char)(any ? 1 : 0);
        held += any ? 1 : 0;
    }
    return held;
}
// relic_own_slots: one flag per state (of `n`), set when the slot counts at least one; returns how
// many are set. Each block holding something (utRelicBlocksHeld) has at least one.
inline int utRelicSlotsHeld(const UtRelicPlaceState* state, int n, unsigned char* keep) {
    int held = 0;
    for (int k = 0; k < n; ++k) {
        keep[k] = (unsigned char)(state[k].count > 0u ? 1 : 0);
        held += keep[k];
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
// page lays out (`keep` = OWN's flags, null = every block; `slots` = relic_own_slots' per state,
// null = every slot of a kept block).
inline int utRelicSearchFound(const UtRelicList& l, const int* stateFirst, const unsigned char* match,
                              const unsigned char* keep, const unsigned char* slots = nullptr) {
    int found = 0;
    for (size_t r = 0; match && r < l.rows.size(); ++r) {
        const int b = l.rows[r].block;
        if (keep && b >= 0 && !keep[b]) continue;
        for (size_t i = 0; i < l.rows[r].bonuses.size(); ++i) {
            const int k = stateFirst[r] + 1 + (int)i;
            if (match[k] && (!slots || slots[k])) ++found;
        }
    }
    return found;
}
// The green slots the page lays out (`keep` = OWN's flags, null = every block; `slots` as above):
// the search's "of".
inline int utRelicGreenSlots(const UtRelicList& l, const unsigned char* keep,
                             const unsigned char* slots = nullptr) {
    int n = 0;
    for (size_t r = 0; r < l.rows.size(); ++r) {
        const int b = l.rows[r].block;
        if (keep && b >= 0 && !keep[b]) continue;
        if (!slots) {
            n += (int)l.rows[r].bonuses.size();
            continue;
        }
        for (size_t i = 0; i < l.rows[r].bonuses.size(); ++i) n += slots[l.rows[r].state + 1 + (int)i] ? 1 : 0;
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
// An artifact speaks of its slot the same way: a listed bonus of its green slot ("(another roll)"
// when only other rows of the record are held), anything else of its plain slot (in or not in).
inline void utRelicTipOf(const UtRelicRow& row, unsigned var1, const char* bonus,
                         const UtRelicRowFacts* f, int n, UtRelicTip* out) {
    memset(out, 0, sizeof(*out));
    out->shards = row.shards;
    const bool hasBonus = bonus && bonus[0];
    if (row.artifact && (!hasBonus || !utRelicRowLists(row, bonus))) {
        auto listed = [&row](const char* b) { return utRelicRowLists(row, b); };
        out->state = utArtifactPlainCount(f, n, listed) > 0u ? 1 : 2;
        return;
    }
    if (row.artifact) var1 = row.shards;   // a listed bonus: its green slot
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
