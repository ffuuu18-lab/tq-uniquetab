// test_relicpage.cpp - the relic and charm page's pure layout (src\ut_relicpage.h), offline.
// NO GAME NEEDED: it reads the oracle's uniq-relics.txt (the base game's list) given as argv[1].
#include <stdio.h>
#include <string.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "ut_relicpage.h"

namespace {
int g_pass = 0, g_fail = 0;
void ok(bool cond, const char* what, const char* detail = "") {
    if (cond) {
        ++g_pass;
        printf("  ok   %s%s%s\n", what, detail[0] ? " - " : "", detail);
    } else {
        ++g_fail;
        printf("  FAIL %s%s%s\n", what, detail[0] ? " - " : "", detail);
    }
}

int blockOf(const ut::UtRelicList& l, const char* idTail) {
    for (size_t b = 0; b < l.blocks.size(); ++b) {
        const std::string& id = l.blocks[b].id;
        const size_t n = strlen(idTail);
        if (id.size() >= n && id.compare(id.size() - n, n, idTail) == 0) return (int)b;
    }
    return -1;
}

int pageLines(const ut::UtRelicLayout& y, int p) {
    const int a = y.pageStart[(size_t)p];
    const int e = ut::utRelicWindowEnd(y, a);
    int s = 0;
    for (int b = a; b < e; ++b) s += y.blockLines[(size_t)b] + (b > a ? y.geo.blockGap : 0);
    return s;
}

ut::UtRelicRowFacts fact(unsigned long long seq, unsigned long long at, unsigned var1, int pending,
                         const char* bonus, bool unresolved = false) {
    ut::UtRelicRowFacts f;
    memset(&f, 0, sizeof(f));
    f.seq = seq;
    f.deposited = at;
    f.var1 = var1;
    f.pending = pending;
    f.unresolved = unresolved;
    ut::utRelicFold(bonus, f.bonus, sizeof(f.bonus));
    return f;
}
}  // namespace

int main(int argc, char** argv) {
    char d[256];
    printf("test_relicpage - src\\ut_relicpage.h\n");
    std::string text;
    {
        std::ifstream in(argc > 1 ? argv[1] : "data/oracle/uniq-relics.txt", std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        text = ss.str();
    }
    ut::UtRelicList l;
    std::string err;
    const bool read = !text.empty() && ut::utRelicListRead(text, &l, &err);
    ok(read, "the oracle's uniq-relics.txt reads", err.c_str());
    if (!read) return 1;

    // ---- 1. the list (the base game's counts) ----
    printf("1. the list\n");
    snprintf(d, sizeof(d), "%zu blocks (%d relic + %d charm), %zu rows, %d bonus slots, %d X",
             l.blocks.size(), l.relicBlocks, l.charmBlocks, l.rows.size(), l.bonusSlots, l.excluded);
    ok(l.blocks.size() == 93 && l.relicBlocks == 53 && l.charmBlocks == 40 && l.rows.size() == 253 &&
           l.bonusSlots == 1971 && l.excluded == 2,
       "base: 93 blocks (53 + 40), 253 rows, 1,971 bonus slots, 2 X lines", d);
    bool relicsFirst = true;
    for (size_t b = 1; b < l.blocks.size(); ++b)
        if (l.blocks[b - 1].part == 'C' && l.blocks[b].part == 'R') relicsFirst = false;
    ok(relicsFirst, "the relic blocks come first, then the charm blocks");
    const int arch = blockOf(l, "act1_archimedesmirror");
    const int nerthus = blockOf(l, "act5_nerthusmistletoe");
    ok(arch == 1 && l.blocks[(size_t)arch].rows == 3, "Archimedes' Mirror: block 2, three rows");
    ok(nerthus >= 0 && l.blocks[(size_t)nerthus].rows == 1, "the Power of Nerthus: one row");
    const int r0 = ut::utRelicFind(l, "records/item/relics/02_act1_archimedesmirror.dbr");
    ok(r0 >= 0 && l.rows[(size_t)r0].tier == 2 && l.rows[(size_t)r0].shards == 3u &&
           l.rows[(size_t)r0].bonuses.size() == 16u,
       "utRelicFind: the Epic Archimedes' Mirror (tier 2, 3 shards, 16 bonuses)");
    ok(ut::utRelicFind(l, "records/item/relics/02_act1_archimedesmirror") < 0 &&
           ut::utRelicFind(l, "records/item/equipmenthelm/u_l_agamemnon'sdeathmask.dbr") < 0,
       "utRelicFind: no answer for a stem or a unique");
    ok(l.rows[0].bonuses[0].find('\\') != std::string::npos &&
           l.rows[0].bonusFolded[0].find('\\') == std::string::npos,
       "the bonuses as the table spells them, and folded");
    ut::UtRelicList bad;
    bool refused = !ut::utRelicListRead("B\t1\tx\tR\t3\r\nQ\tnonsense\r\n", &bad, &err);
    ok(refused, "a malformed line refused", err.c_str());
    refused = !ut::utRelicListRead("B\t1\tx\tR\t3\r\n", &bad, &err);
    ok(refused, "a block without an R line refused", err.c_str());

    // ---- 2. the base area 16 x 15 ----
    printf("2. the fixed pages at 16 x 15\n");
    ut::UtRelicGeometry g;
    ut::UtRelicLayout y;
    ok(ut::utRelicLayoutBuild(l, g, &y) && y.perLine == 14, "16 x 15: 14 bonus slots a line");
    snprintf(d, sizeof(d), "%d pages, %d rows wrap, %d blocks too tall", ut::utRelicPageCount(y),
             y.wrappedRows, y.tallBlocks);
    ok(ut::utRelicPageCount(y) == 23 && y.wrappedRows == 8 && y.tallBlocks == 0,
       "23 fixed pages, 8 rows wrap, every block fits", d);
    ok(y.blockLines[(size_t)arch] == 5, "Archimedes' Mirror is 5 lines (16, 16, 11 bonuses)");
    ok(y.blockLines[(size_t)nerthus] == 1, "the Power of Nerthus is 1 line");
    const int lastRelic = l.relicBlocks - 1;
    snprintf(d, sizeof(d), "the last relic block on page %d, the first charm block on page %d",
             ut::utRelicPageOf(y, lastRelic) + 1, ut::utRelicPageOf(y, lastRelic + 1) + 1);
    ok(ut::utRelicPageOf(y, lastRelic) == 13 && ut::utRelicPageOf(y, lastRelic + 1) == 13,
       "relics on pages 1-14, charms on pages 14-23 (no page break between them)", d);
    ok(y.pageStart[1] == 3 && ut::utRelicWindowEnd(y, 3) == 7 && pageLines(y, 1) == 15 &&
           l.blocks[3].id.find("dionysus") != std::string::npos &&
           l.blocks[4].id.find("goldenfleece") != std::string::npos &&
           l.blocks[5].id.find("hecate") != std::string::npos,
       "page 2 = the sketch: Dionysus' Wineskin, the Golden Fleece, Hecate's Crescent and the "
       "fourth block, four blocks of three lines, 15 lines");
    const int p10 = ut::utRelicPageOf(y, nerthus);
    snprintf(d, sizeof(d), "page %d, blocks %d-%d, %d lines", p10 + 1, y.pageStart[(size_t)p10] + 1,
             ut::utRelicWindowEnd(y, y.pageStart[(size_t)p10]), pageLines(y, p10));
    ok(p10 == 9 && y.pageStart[9] == blockOf(l, "act5_eponashorses") &&
           ut::utRelicWindowEnd(y, y.pageStart[9]) == blockOf(l, "act5_ogmioseloquence") + 1 &&
           pageLines(y, 9) == 13,
       "page 10 = Epona, Belenus, Nerthus (one line), Ogmios = 13 lines", d);
    int most = 0, mostPage = -1;
    static ut::UtRelicPlace pl[1024];
    for (int p = 0; p < ut::utRelicPageCount(y); ++p) {
        const int n = ut::utRelicWindowPlaces(l, y, y.pageStart[(size_t)p], pl, 1024);
        int green = 0;   // the prototypes: green and red slots (a dead cell has none)
        for (int i = 0; i < n; ++i) green += pl[i].kind != ut::kUtRelicSlotDead ? 1 : 0;
        if (green > most) {
            most = green;
            mostPage = p;
        }
    }
    snprintf(d, sizeof(d), "%d on page %d", most, mostPage + 1);
    ok(most == 137 && mostPage == 3, "most prototypes on a page: 137 (page 4: 125 bonus + 12 red slots)", d);
    int all = 0;
    for (int p = 0; p < ut::utRelicPageCount(y); ++p) {
        const int n = ut::utRelicWindowPlaces(l, y, y.pageStart[(size_t)p], pl, 1024);
        for (int i = 0; i < n; ++i) all += pl[i].kind == ut::kUtRelicSlotGreen ? 1 : 0;
    }
    ok(all == 1971, "the fixed pages hold every bonus slot once (1,971)");

    // ---- 3. the places of a window ----
    printf("3. the places\n");
    const int n0 = ut::utRelicWindowPlaces(l, y, arch, pl, 1024);
    int redAt0 = 0, wrapOk = 0, maxLine = 0;
    for (int i = 0; i < n0; ++i) {
        if (pl[i].slot == 0 && pl[i].col == 0 && pl[i].kind == ut::kUtRelicSlotRed) ++redAt0;
        if (pl[i].row == arch * 3 && pl[i].slot == 15 && pl[i].col == 2 && pl[i].line == 1) ++wrapOk;
        if (pl[i].line > maxLine) maxLine = pl[i].line;
    }
    ok(redAt0 == 3 * (ut::utRelicWindowEnd(y, arch) - arch) || redAt0 >= 9,
       "every row's red slot in column 0");
    ok(n0 > 0 && pl[0].row == 3 && pl[0].slot == 0 && pl[1].slot == 1 && pl[1].col == 2 &&
           pl[1].line == 0,
       "row 1: the red cell, the gap column, bonus 1 in column 2");
    ok(wrapOk == 1, "Archimedes' 15th bonus wraps to the next line's first bonus column");
    ok(maxLine < 15, "no place below the area's last line");
    const int nn = ut::utRelicWindowPlaces(l, y, nerthus, pl, 1024);
    ok(nn > 0 && pl[0].kind == ut::kUtRelicSlotDead && pl[0].col == 0 && pl[1].line == 0,
       "Nerthus' column 0 is a DEAD cell (one shard), its bonuses on one line");
    ok(ut::utRelicWindowPlaces(l, y, 0, pl, 5) == 5, "the places stop at the cap");

    // ---- 4. the wheel and the page keys ----
    printf("4. the wheel windows\n");
    ok(ut::utRelicWheelStep(y, 0, -1) == 0 && ut::utRelicWheelStep(y, 0, 1) == 1,
       "the wheel moves one block (up stops at block 1)");
    ok(ut::utRelicWindowEnd(y, 1) == 4, "the window from block 2: blocks 2-4 (Archimedes 5 lines)",
       "");
    const int nb = (int)l.blocks.size();
    int f = 0, notches = 0;
    while (ut::utRelicWheelStep(y, f, 1) != f && notches < 200) {
        f = ut::utRelicWheelStep(y, f, 1);
        ++notches;
    }
    snprintf(d, sizeof(d), "stops at block %d after %d notches, its window ends at block %d", f + 1,
             notches, ut::utRelicWindowEnd(y, f));
    ok(ut::utRelicWindowEnd(y, f) == nb && ut::utRelicWindowEnd(y, f - 1) < nb,
       "the wheel stops when the last block is shown", d);
    ok(ut::utRelicPageStep(y, 0, 1) == 3 && ut::utRelicPageStep(y, 3, 1) == y.pageStart[2] &&
           ut::utRelicPageStep(y, 3, -1) == 0 && ut::utRelicPageStep(y, 0, -1) == 0,
       "> / < jump to the next / previous fixed page start");
    ok(ut::utRelicPageStep(y, 5, -1) == 3 && ut::utRelicPageStep(y, 5, 1) == y.pageStart[2],
       "from a wheel window inside page 2: < = page 2's start, > = page 3's start");
    const int last = y.pageStart.back();
    ok(ut::utRelicPageStep(y, last, 1) == last && ut::utRelicPageStep(y, nb - 1, 1) == nb - 1,
       "> on the last page stays");
    ok(ut::utRelicPageOf(y, 5) == 1 && ut::utRelicClampFirst(y, 500) == nb - 1 &&
           ut::utRelicClampFirst(y, -3) == 0,
       "the label's page is the fixed page of the first shown block; a stored offset is clamped");

    // ---- 5. SVAERA's window on the base data, and the gaps ----
    printf("5. other areas\n");
    ut::UtRelicGeometry g2;
    g2.cols = 20;
    g2.rows = 19;
    ut::UtRelicLayout y2;
    ok(ut::utRelicLayoutBuild(l, g2, &y2) && y2.perLine == 18, "20 x 19: 18 bonus slots a line");
    snprintf(d, sizeof(d), "%d pages, relics 1-%d, charms %d-%d, %d rows wrap",
             ut::utRelicPageCount(y2), ut::utRelicPageOf(y2, lastRelic) + 1,
             ut::utRelicPageOf(y2, lastRelic + 1) + 1, ut::utRelicPageCount(y2), y2.wrappedRows);
    ok(ut::utRelicPageCount(y2) == 18 && ut::utRelicPageOf(y2, lastRelic) == 10 &&
           ut::utRelicPageOf(y2, lastRelic + 1) == 10 && y2.wrappedRows == 3,
       "20 x 19: 18 pages, relics 1-11, charms 11-18, 3 rows wrap (the Golden Scarab)", d);
    ut::UtRelicGeometry g3;
    g3.blockGap = 0;
    g3.bonusGap = 0;
    ut::UtRelicLayout y3;
    ok(ut::utRelicLayoutBuild(l, g3, &y3) && y3.perLine == 15 &&
           ut::utRelicPageCount(y3) < ut::utRelicPageCount(y),
       "gaps 0 / 0: 15 bonus slots a line, fewer pages");
    ut::UtRelicGeometry g4;
    g4.cols = 1;
    ok(!ut::utRelicLayoutBuild(l, g4, &y3), "an area without a bonus column is refused");
    {   // the relic area in force and the two gap settings (ut_live liveSetRelicGeometry)
        const ut::UtRelicGeometry a = ut::utRelicGeometryOf(16, 15, 1, 1);
        const ut::UtRelicGeometry c = ut::utRelicGeometryOf(16, 15, 9, -3);
        ok(a.cols == 16 && a.rows == 15 && a.blockGap == 1 && a.bonusGap == 1,
           "the geometry takes the area's cells and both gaps");
        ok(c.blockGap == 2 && c.bonusGap == 0, "the gaps are kept to 0..2 and 0..1");
        ok(ut::utRelicGeometrySame(a, ut::utRelicGeometryOf(16, 15, 1, 1)) &&
               !ut::utRelicGeometrySame(a, ut::utRelicGeometryOf(15, 15, 1, 1)) &&
               !ut::utRelicGeometrySame(a, ut::utRelicGeometryOf(16, 14, 1, 1)) &&
               !ut::utRelicGeometrySame(a, ut::utRelicGeometryOf(16, 15, 0, 1)) &&
               !ut::utRelicGeometrySame(a, ut::utRelicGeometryOf(16, 15, 1, 0)),
           "a change of the cells or of either gap is a change (the layout is built again)");
        ut::UtRelicLayout ya, yb, yc, yd;
        ok(ut::utRelicLayoutBuild(l, a, &ya) && ya.perLine == 14 && ut::utRelicPageCount(ya) == 23,
           "16 x 15, gaps 1 / 1: 14 bonus slots a line, 23 pages");
        ok(ut::utRelicLayoutBuild(l, ut::utRelicGeometryOf(16, 15, 1, 0), &yb) &&
               yb.perLine == ya.perLine + 1,
           "relic_bonus_gap=0: one more bonus slot a line");
        ok(ut::utRelicLayoutBuild(l, ut::utRelicGeometryOf(16, 15, 0, 1), &yc) &&
               ut::utRelicPageCount(yc) < ut::utRelicPageCount(ya),
           "relic_block_gap=0: fewer pages");
        ok(ut::utRelicLayoutBuild(l, ut::utRelicGeometryOf(12, 10, 1, 1), &yd) && yd.perLine == 10 &&
               ut::utRelicPageCount(yd) > ut::utRelicPageCount(ya),
           "a relics_ area of 12 x 10 cells: 10 bonus slots a line, more pages");
        // the page counts one game's list shows: 23 on its own 16 x 15 area (the start-up line),
        // 29 on a 16 x 14 grid in force (grid_565x637=24,124,512,448), and 18 only on a 20 x 19
        // area - another game's relic area, which a game switch no longer carries over
        ut::UtRelicLayout ye, yf;
        ok(ut::utRelicLayoutBuild(l, ut::utRelicGeometryOf(16, 14, 1, 1), &ye) &&
               ut::utRelicPageCount(ye) == 29,
           "16 x 14 cells (a user grid in force): 29 pages");
        ok(ut::utRelicLayoutBuild(l, ut::utRelicGeometryOf(20, 19, 1, 1), &yf) &&
               ut::utRelicPageCount(yf) == 18 && ut::utRelicPageCount(ya) == 23,
           "20 x 19 cells: 18 pages; the list's own 16 x 15: 23");
    }

    // ---- 6. what a place shows ----
    printf("6. the place states\n");
    const ut::UtRelicRow& row = l.rows[(size_t)r0];
    const std::string b1 = row.bonuses[0], b2 = row.bonuses[1];
    ut::UtRelicRowFacts fs[8];
    fs[0] = fact(11, 100, 2, ut::kUtRelicPendNone, "");            // a settled 2/3
    fs[1] = fact(12, 200, 1, ut::kUtRelicPendIn, "");              // a 1/3 pending in
    fs[2] = fact(13, 300, 1, ut::kUtRelicPendOut, "");             // a shard taken (out)
    fs[3] = fact(14, 150, 3, ut::kUtRelicPendNone, b1.c_str());    // a completed, bonus 1
    fs[4] = fact(15, 120, 3, ut::kUtRelicPendIn, b1.c_str());      // a second copy, pending in
    fs[5] = fact(16, 110, 3, ut::kUtRelicPendOut, b2.c_str());     // bonus 2: only a row out
    ut::UtRelicPlaceState st;
    ut::utRelicPlaceState(row, 0, fs, 6, &st);
    snprintf(d, sizeof(d), "count %u, seq %llu, split %d, var1 %u", st.count, st.seq, st.split ? 1 : 0,
             st.protoVar1);
    ok(st.count == 3u && !st.empty && st.seq == 12u && !st.split && st.protoVar1 == 1u,
       "red: 3 shards (the out row left out), the 1-shard row handed out whole", d);
    ut::utRelicPlaceState(row, 0, fs, 1, &st);
    ok(st.count == 2u && st.seq == 11u && st.split && st.protoVar1 == 1u,
       "red with one 2/3 row: count 2, the take splits it, the prototype is a 1-shard piece");
    ut::utRelicPlaceState(row, 1, fs, 6, &st);
    ok(st.count == 2u && !st.empty && st.seq == 14u && st.protoVar1 == 3u,
       "green bonus 1: two copies, the oldest SETTLED row shown (settled before pending in)");
    ut::utRelicPlaceState(row, 2, fs, 6, &st);
    ok(st.count == 0u && st.empty && st.seq == 0u && st.protoVar1 == 3u,
       "green bonus 2: its only row is pending out - empty, the synthetic prototype");
    ut::utRelicPlaceState(row, 0, nullptr, 0, &st);
    ok(st.empty && st.count == 0u && st.protoVar1 == 1u, "an empty red slot: the synthetic 1-shard");
    fs[6] = fact(17, 90, 0, ut::kUtRelicPendNone, "");               // a dropped piece: var1 0
    fs[7] = fact(18, 95, 2, ut::kUtRelicPendNone, "");               // a settled 2/3
    ut::utRelicPlaceState(row, 0, fs + 6, 2, &st);
    ok(st.count == 3u && !st.empty && st.seq == 17u && !st.split &&
           ut::utRelicProtoVar1(fs[6].var1) == 0u,
       "red with a var1 0 row and a 2/3: 3 shards, the var1 0 row handed out whole, its prototype "
       "keeps var1 0");
    const int nr = l.blocks[(size_t)nerthus].firstRow;
    ut::utRelicPlaceState(l.rows[(size_t)nr], 0, fs, 6, &st);
    ok(st.empty && st.protoVar1 == 0u && st.count == 0u, "a dead cell shows nothing");
    ut::utRelicPlaceState(row, 99, fs, 6, &st);
    ok(st.empty && st.count == 0u, "a slot past the table is empty");
    ok(ut::utRelicProtoSlot(0) == 1 && ut::utRelicProtoSlot(3) == 4,
       "the prototype key: a relic place is 1 + its slot (0 = a unique)");

    // ---- 7. OWN, the search, the have-mark and the rollover lines ----
    printf("7. OWN, the search, the have-mark, the rollover lines\n");
    {
        ut::UtRelicGeometry g16;
        std::vector<int> first;
        std::vector<ut::UtRelicPlaceState> rst;
        for (size_t r = 0; r < l.rows.size(); ++r) {
            first.push_back((int)rst.size());
            for (size_t s = 0; s <= l.rows[r].bonuses.size(); ++s) {
                ut::UtRelicPlaceState e;
                memset(&e, 0, sizeof(e));
                e.empty = true;
                rst.push_back(e);
            }
        }
        std::vector<unsigned char> keep(l.blocks.size(), 9);
        ok(ut::utRelicBlocksHeld(l, first.data(), rst.data(), keep.data()) == 0 && keep[0] == 0,
           "OWN: an empty collection holds no block");
        const int ob1 = 1, ob5 = 5;
        rst[(size_t)first[(size_t)l.blocks[ob1].firstRow]].count = 2;   // two shards in block 1's red slot
        const int lastRow = l.blocks[ob5].firstRow + l.blocks[ob5].rows - 1;
        rst[(size_t)(first[(size_t)lastRow] + 1)].count = 1;          // one copy of block 5's last trow, bonus 1
        ok(ut::utRelicBlocksHeld(l, first.data(), rst.data(), keep.data()) == 2 && keep[ob1] && keep[ob5] &&
               !keep[0] && !keep[2],
           "OWN: a red slot's shards and a green slot's copy each make their block held");
        ut::UtRelicLayout own, allY;
        ut::utRelicLayoutBuild(l, g16, &allY);
        ok(ut::utRelicLayoutBuild(l, g16, &own, keep.data()) && own.blockLines.size() == 2u &&
               own.blockOf.size() == 2u && own.blockOf[0] == ob1 && own.blockOf[1] == ob5 &&
               ut::utRelicPageCount(own) == 1 && allY.blockOf.size() == l.blocks.size() &&
               allY.blockOf[7] == 7,
           "OWN: only the held blocks are laid out (one page), every block without the filter");
        static ut::UtRelicPlace opl[512];
        const int no = ut::utRelicWindowPlaces(l, own, 0, opl, 512);
        int want = 0;
        for (int bb : {ob1, ob5})
            for (int k = 0; k < l.blocks[bb].rows; ++k)
                want += 1 + (int)l.rows[(size_t)(l.blocks[bb].firstRow + k)].bonuses.size();
        bool inPlace = no == want && opl[0].row == l.blocks[ob1].firstRow && opl[0].line == 0;
        for (int i = 0; i < no && inPlace; ++i)   // the columns of the unfiltered layout
            inPlace = opl[i].col == (opl[i].slot == 0 ? 0 : 1 + g16.bonusGap + (opl[i].slot - 1) % own.perLine);
        ok(inPlace, "OWN: every slot of a held block is in place (red column 0, the bonuses in table order)");
        std::vector<unsigned char> none(l.blocks.size(), 0);
        ut::UtRelicLayout empty;
        ok(ut::utRelicLayoutBuild(l, g16, &empty, none.data()) && ut::utRelicPageCount(empty) == 0 &&
               ut::utRelicWindowPlaces(l, empty, 0, opl, 512) == 0,
           "OWN: nothing held - no page, no place");

        std::vector<unsigned char> match(rst.size(), 0);
        const int sr0 = l.blocks[ob1].firstRow;
        match[(size_t)(first[(size_t)sr0] + 2)] = 1;   // bonus 2 of block 1's first trow
        match[(size_t)(first[(size_t)l.blocks[3].firstRow] + 1)] = 1;   // block 3: not held
        ok(ut::utRelicSearchHit(match.data(), first[(size_t)sr0] + 2, 2, true) &&
               !ut::utRelicSearchHit(match.data(), first[(size_t)sr0] + 2, 2, false) &&
               !ut::utRelicSearchHit(match.data(), first[(size_t)sr0], 0, true),
           "search: a green slot whose text holds the query is marked; no query, or the red slot, never");
        ok(ut::utRelicSearchFound(l, first.data(), match.data(), nullptr) == 2 &&
               ut::utRelicSearchFound(l, first.data(), match.data(), keep.data()) == 1,
           "search: found counts the green matches, under OWN those of the held blocks");
        {
            int greenAll = 0, greenKept = 0;
            for (size_t r = 0; r < l.rows.size(); ++r) {
                greenAll += (int)l.rows[r].bonuses.size();
                const int b = l.rows[r].block;
                if (b < 0 || keep[(size_t)b]) greenKept += (int)l.rows[r].bonuses.size();
            }
            ok(ut::utRelicGreenSlots(l, nullptr) == l.bonusSlots && l.bonusSlots == greenAll &&
                   ut::utRelicGreenSlots(l, keep.data()) == greenKept && greenKept < greenAll,
               "search: of = the green slots laid out, under OWN those of the held blocks");
        }

        char hk[300];
        ok(ut::utRelicHaveKey("records/x/r.dbr", "Records\\B\\One.dbr", hk, sizeof(hk)) &&
               strcmp(hk, "records/x/r.dbr>records/b/one.dbr") == 0 &&
               !ut::utRelicHaveKey("records/x/r.dbr", "", hk, sizeof(hk)) &&
               !ut::utRelicHaveKey("records/x/r.dbr", "records/b/one.dbr", hk, 20),
           "have-mark: keyed by (record, folded bonus); a partial piece (no bonus) has no key");

        ut::UtRelicRow trow;
        trow.record = trow.folded = "records/x/r.dbr";
        trow.shards = 3;
        trow.bonuses = {"Records/B/One.dbr", "records/b/two.dbr"};
        trow.bonusFolded = {"records/b/one.dbr", "records/b/two.dbr"};
        ut::UtRelicRowFacts tf[4] = {fact(1, 20, 2, ut::kUtRelicPendNone, ""),
                                     fact(2, 10, 0, ut::kUtRelicPendNone, ""),
                                     fact(3, 5, 1, ut::kUtRelicPendOut, ""),
                                     fact(4, 30, 3, ut::kUtRelicPendIn, "records/b/one.dbr")};
        ut::UtRelicTip tip;
        ut::utRelicTipOf(trow, 1, "", tf, 4, &tip);
        ok(tip.state == 1 && tip.pieces == 3u && tip.pieces == ut::utRelicRedShards(tf, 4) && tip.shards == 3u,
           "rollover: a partial gives the red slot's count - the stored shards (var1 0 = one), out rows left out");
        ut::utRelicTipOf(trow, 1, "", tf + 2, 2, &tip);
        ok(tip.state == 2 && tip.pieces == 0u, "rollover: a partial with no stored piece - not in the collection");
        ut::utRelicTipOf(trow, 3, "RECORDS\\b\\one.dbr", tf, 4, &tip);
        ok(tip.state == 1 && tip.pieces == 0u, "rollover: a completed relic whose bonus is held - in the collection");
        ut::utRelicTipOf(trow, 3, "records/b/two.dbr", tf, 4, &tip);
        ok(tip.state == 3, "rollover: only another bonus of the record held - another roll");
        ut::utRelicTipOf(trow, 3, "records/b/two.dbr", tf, 3, &tip);
        ok(tip.state == 2, "rollover: no completed one held - not in the collection");
        ut::utRelicTipOf(trow, 3, "", tf, 4, &tip);
        ok(tip.state == 0, "rollover: a completed piece without a bonus gets no line");

        char tl[64], tr[64];
        ut::utRelicTipLine("In your collection", 15, tl, sizeof(tl));
        ut::utRelicTipLine("\xD0\x92 \xD0\xBA\xD0\xBE\xD0\xBB", 1, tr, sizeof(tr));   // the player's words, UTF-8
        ok(!strcmp(tl, "In your collection (pieces 15)") && !strcmp(tr, "\xD0\x92 \xD0\xBA\xD0\xBE\xD0\xBB (pieces 1)"),
           "rollover: a partial's ONE line - the collected words in force and the red slot's shards");
    }

    printf("%s: %d passed, %d failed\n", g_fail ? "RELICPAGE TESTS FAILED" : "RELICPAGE TESTS PASSED",
           g_pass, g_fail);
    return g_fail ? 1 : 0;
}
