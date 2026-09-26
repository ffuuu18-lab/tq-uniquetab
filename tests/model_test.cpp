// model_test.cpp - console test for the model (catalogue / collection /
// layout).  No engine, no DLL, no game process: it only reads
// data\oracle\catalogue.bin.
//
// Build and run: build_model.bat   (output kept in tests\model_test.out.txt)
// Optional argument: path to catalogue.bin (default data\oracle\catalogue.bin
// relative to the working directory used by build_model.bat).

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "../src/model/catalogue.h"
#include "../src/model/collection.h"
#include "../src/model/layout.h"

using namespace gdut;

namespace {

int g_checks = 0;

void failed(const char* expr, const char* msg, const char* file, int line) {
    std::printf("\n*** ASSERT FAILED ***\n  %s\n  condition: %s\n  at %s:%d\n",
                msg, expr, file, line);
    std::fflush(stdout);
    std::abort();
}

#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        ++g_checks;                                                         \
        if (!(cond)) failed(#cond, (msg), __FILE__, __LINE__);              \
    } while (0)

void checkEq(long long got, long long want, const char* what,
             const char* file, int line) {
    ++g_checks;
    if (got != want) {
        char msg[256];
        std::snprintf(msg, sizeof msg, "%s: got %lld, expected %lld", what, got, want);
        failed("got == want", msg, file, line);
    }
}

#define CHECK_EQ(got, want, what) checkEq((long long)(got), (long long)(want), (what), __FILE__, __LINE__)

void rule(const char* title) {
    std::printf("\n== %s ==\n", title);
}

// Reads a whole file; returns an empty vector on failure.
std::vector<std::uint8_t> readFile(const char* path) {
    std::FILE* fh = nullptr;
#if defined(_MSC_VER)
    if (::fopen_s(&fh, path, "rb") != 0) fh = nullptr;
#else
    fh = std::fopen(path, "rb");
#endif
    std::vector<std::uint8_t> out;
    if (!fh) return out;
    std::fseek(fh, 0, SEEK_END);
    const long size = std::ftell(fh);
    std::rewind(fh);
    if (size > 0) {
        out.resize(static_cast<std::size_t>(size));
        if (std::fread(out.data(), 1, out.size(), fh) != out.size()) out.clear();
    }
    std::fclose(fh);
    return out;
}

std::string rectStr(const Rect& r) {
    char buf[80];
    std::snprintf(buf, sizeof buf, "(x=%d y=%d w=%d h=%d)", r.x, r.y, r.w, r.h);
    return std::string(buf);
}

}  // namespace

int main(int argc, char** argv) {
    const char* path = (argc > 1) ? argv[1] : "data/oracle/catalogue.bin";

    // ------------------------------------------------------------------ load
    rule("catalogue.bin");
    Catalogue cat;
    std::string err;
    if (!cat.loadFromFile(path, &err)) {
        std::printf("load failed: %s\n", err.c_str());
        return 2;
    }
    std::printf("loaded %s\n  format version %u, %zu bytes, %zu items, %zu sets\n",
                path, cat.formatVersion(), cat.byteSize(), cat.itemCount(), cat.setCount());
    CHECK_EQ(cat.formatVersion(), Catalogue::kFormatVersion, "format version");
    CHECK(cat.loaded(), "catalogue reports loaded");
    CHECK_EQ(cat.itemCount(), 1588, "item count");   // TQ AE 2.10: 1,602 - 14 EXCLUDE

    // -------------------------------------------------------- group breakdown
    rule("items per slot group");
    std::size_t groupTotal = 0;
    std::size_t equipmentGroups = 0;
    for (int g = 0; g < kSlotGroupCount; ++g) {
        const SlotGroup group = static_cast<SlotGroup>(g);
        const GroupRange r = cat.groupRange(group);
        groupTotal += r.count;
        if (g < static_cast<int>(SlotGroup::Relic)) equipmentGroups += r.count;
        if (r.count == 0) continue;
        std::size_t epic = 0;
        for (std::uint32_t i = 0; i < r.count; ++i) {
            if (cat.item(r.first + i).classification == Classification::Epic) ++epic;
        }
        std::printf("  %2d %-11s %5u  first=%-5u epic=%-4zu legendary=%zu\n",
                    g, slotGroupName(group), r.count, r.first, epic, r.count - epic);
    }
    CHECK_EQ(groupTotal, cat.itemCount(), "sum of group ranges");
    CHECK_EQ(equipmentGroups, 1498, "equipment items (groups Helm..Medal)");
    CHECK_EQ(cat.groupRange(SlotGroup::Relic).count, 90, "relic items (TQ artifacts)");
    CHECK_EQ(cat.equipmentCount(), 1498, "header equipment count");
    CHECK_EQ(cat.relicCount(), 90, "header relic count");
    CHECK_EQ(cat.defaultVisibleCount(), 1588, "header default-visible count");

    std::size_t defaultVisible = 0, legendaryDefault = 0, setPieces = 0, withBitmap = 0;
    for (const ItemView& it : cat.items()) {
        if (it.has(ItemFlag::DefaultVisible)) {
            ++defaultVisible;
            if (it.classification == Classification::Legendary) ++legendaryDefault;
        }
        if (it.has(ItemFlag::SetPiece)) ++setPieces;
        if (it.has(ItemFlag::HasBitmap)) ++withBitmap;
    }
    std::printf("  default visible %zu (legendary %zu), set pieces %zu, with bitmap %zu\n",
                defaultVisible, legendaryDefault, setPieces, withBitmap);
    CHECK_EQ(defaultVisible, 1588, "items flagged DefaultVisible");
    CHECK_EQ(setPieces, 464, "items flagged SetPiece");

    // --------------------------------------------------------- record lookup
    rule("record -> index map");
    const char* kKnown = "records/item/equipmenthelm/u_l_agamemnon'sdeathmask.dbr";
    const int known = cat.indexOfRecord(kKnown);
    CHECK(known >= 0, "known record resolves");
    std::printf("  %s -> #%d \"%.*s\" (%s, %s, level %u, set %d)\n",
                kKnown, known,
                static_cast<int>(cat.item(known).name.size()), cat.item(known).name.data(),
                slotGroupName(cat.item(known).group),
                classificationName(cat.item(known).classification),
                cat.item(known).levelRequirement, cat.item(known).setIndex);
    CHECK(cat.item(known).name == "Agamemnon's Death Mask", "known record display name");
    // v2: the footprint and the expansion (a 64x64 icon = 2x2 cells, a base-game record)
    CHECK(cat.item(known).footW == 2 && cat.item(known).footH == 2, "known record footprint 2x2");
    CHECK_EQ(cat.item(known).expansion, 0, "known record expansion (base game)");
    {
        int inRange = 0, perExp[5] = {0, 0, 0, 0, 0};
        for (const ItemView& it : cat.items()) {
            if (it.footW >= 1 && it.footW <= 2 && it.footH >= 1 && it.footH <= 5) ++inRange;
            if (it.expansion <= 4) ++perExp[it.expansion];
        }
        std::printf("  per expansion: base %d, IT %d, Ragnarok %d, Atlantis %d, Eternal Embers %d\n",
                    perExp[0], perExp[1], perExp[2], perExp[3], perExp[4]);
        CHECK_EQ(inRange, static_cast<int>(cat.itemCount()), "every footprint in 1..2 x 1..5");
        CHECK_EQ(perExp[0] + perExp[1] + perExp[2] + perExp[3] + perExp[4],
                 static_cast<int>(cat.itemCount()), "every expansion in 0..4");
    }
    CHECK_EQ(cat.indexOfRecord("records/items/does_not_exist.dbr"), -1, "unknown record lookup");
    CHECK_EQ(cat.indexOfRecord(""), -1, "empty record lookup");
    for (std::size_t i = 0; i < cat.itemCount(); i += 617) {
        CHECK_EQ(cat.indexOfRecord(cat.item(i).record), static_cast<int>(i), "round-trip lookup");
    }

    // ------------------------------------------------------------ item sets
    rule("item sets");
    std::size_t memberLinks = 0;
    for (std::size_t s = 0; s < cat.setCount(); ++s) {
        const SetView& set = cat.sets()[s];
        memberLinks += set.memberCount;
        for (std::uint32_t m = 0; m < set.memberCount; ++m) {
            const std::uint32_t idx = set.members[m];
            CHECK(idx < cat.itemCount(), "set member index in range");
            CHECK_EQ(cat.item(idx).setIndex, static_cast<int>(s), "member points back at its set");
        }
    }
    std::printf("  %zu sets, %zu member links; first set \"%.*s\" (%u pieces)\n",
                cat.setCount(), memberLinks,
                static_cast<int>(cat.sets()[0].name.size()), cat.sets()[0].name.data(),
                cat.sets()[0].memberCount);
    CHECK_EQ(cat.setCount(), 110, "set count (TQ: set records folded; 2 sets are spelled two ways)");
    {
        int trailing = 0, order = 0;
        for (std::size_t i = 0; i < cat.itemCount(); ++i) {
            const ItemView& it = cat.item(i);
            if (!it.name.empty() && (it.name.back() == ' ' || it.name.back() == '\t')) ++trailing;
            if (i > 0 && cat.item(i - 1).group == it.group && cat.item(i - 1).itemLevel > it.itemLevel)
                ++order;
        }
        CHECK_EQ(trailing, 0, "no display name ends in a blank");
        CHECK_EQ(order, 0, "items ascend by item level inside each group (the TQ sort key)");
    }
    CHECK_EQ(memberLinks, setPieces, "set member links == set pieces");

    // -------------------------------------------------- rejecting bad buffers
    rule("loader rejects damaged files");
    {
        std::vector<std::uint8_t> tiny(16, 0);
        Catalogue bad;
        std::string why;
        CHECK(!bad.loadFromMemory(tiny, &why), "truncated file rejected");
        std::printf("  truncated: %s\n", why.c_str());

        std::vector<std::uint8_t> junk(200, 0);
        std::memcpy(junk.data(), "XXXX", 4);
        CHECK(!bad.loadFromMemory(junk, &why), "bad magic rejected");
        std::printf("  bad magic: %s\n", why.c_str());

        // valid file with the version bumped
        const std::vector<std::uint8_t> raw = readFile(path);
        CHECK_EQ(raw.size(), cat.byteSize(), "reread size");
        std::vector<std::uint8_t> wrongVersion = raw;
        wrongVersion[4] = 99;
        CHECK(!bad.loadFromMemory(wrongVersion, &why), "wrong version rejected");
        std::printf("  wrong version: %s\n", why.c_str());

        std::vector<std::uint8_t> wildOffset = raw;
        wildOffset[24] = 0xFF; wildOffset[25] = 0xFF; wildOffset[26] = 0xFF; wildOffset[27] = 0x7F;
        CHECK(!bad.loadFromMemory(wildOffset, &why), "wild item-table offset rejected");
        std::printf("  wild offset: %s\n", why.c_str());

        // a string reference past the end of the string table
        std::vector<std::uint8_t> wildString = raw;
        const std::uint32_t itemOff = 80 + 29 * 8;   // header + group table
        wildString[itemOff + 0] = 0xFF; wildString[itemOff + 1] = 0xFF;
        wildString[itemOff + 2] = 0xFF; wildString[itemOff + 3] = 0x7F;
        CHECK(!bad.loadFromMemory(wildString, &why), "wild string reference rejected");
        std::printf("  wild string ref: %s\n", why.c_str());

        // every 32-bit header field, forced to 0 and to 0xFFFFFFFF: the loader
        // must either refuse the file or come back with a fully consistent one,
        // and must never read outside the buffer.
        int refused = 0, accepted = 0;
        for (int field = 1; field < 20; ++field) {
            const std::uint32_t poison[2] = {0u, 0xFFFFFFFFu};
            for (int p = 0; p < 2; ++p) {
                std::vector<std::uint8_t> mutated = raw;
                std::memcpy(mutated.data() + field * 4, &poison[p], 4);
                Catalogue mutCat;
                std::string ignored;
                if (!mutCat.loadFromMemory(mutated, &ignored)) {
                    ++refused;
                    CHECK(!mutCat.loaded(), "refused file leaves an empty catalogue");
                    continue;
                }
                ++accepted;
                for (std::size_t i = 0; i < mutCat.itemCount(); ++i) {
                    const ItemView& v = mutCat.item(i);
                    CHECK(!v.record.empty(), "accepted file has no empty record");
                    CHECK(v.setIndex < static_cast<int>(mutCat.setCount()), "set index in range");
                    CHECK(mutCat.indexOfRecord(v.record) == static_cast<int>(i), "lookup consistent");
                }
                for (const SetView& s : mutCat.sets()) {
                    for (std::uint32_t m = 0; m < s.memberCount; ++m) {
                        CHECK(s.members[m] < mutCat.itemCount(), "set member in range");
                    }
                }
            }
        }
        std::printf("  header field sweep: %d variants refused, %d accepted and self-consistent\n",
                    refused, accepted);
        CHECK_EQ(refused + accepted, 19 * 2, "every header field tried");
        CHECK(refused > 0, "the sweep refuses something");
    }

    // ------------------------------------------------- layout: transfer grid
    rule("layout: game transfer grid (x=105 y=4 w=320 h=608)");
    GridLayout grid;
    grid.setCatalogue(&cat);
    Metrics metrics;                     // 40 px cells, 2 px gap, no headers
    grid.setMetrics(metrics);
    Filters filters;                     // default: equipment + relics, both tiers
    const Viewport transferGrid{105, 4, 320, 608};
    grid.relayout(transferGrid, filters);
    std::printf("  visible items %d, columns %d, rows %d, contentHeight %d, maxScroll %d\n",
                grid.visibleItemCount(), grid.columns(), grid.rows(),
                grid.contentHeight(), grid.maxScroll());
    CHECK_EQ(grid.visibleItemCount(), 1588, "default filter shows equipment + relics");
    CHECK_EQ(grid.columns(), 7, "columns in a 320 px wide grid");
    CHECK_EQ(grid.rows(), (1588 + 6) / 7, "rows");
    CHECK_EQ(grid.contentHeight(), grid.rows() * 42 - 2, "content height");
    CHECK_EQ(grid.maxScroll(), grid.contentHeight() - 608, "max scroll");
    CHECK_EQ(grid.scrollOffset(), 0, "initial scroll");

    VisibleRange vr = grid.visibleRange();
    std::printf("  visibleRange: first=%d count=%d (rows on screen %d)\n",
                vr.first, vr.count, (vr.count + grid.columns() - 1) / grid.columns());
    CHECK_EQ(vr.first, 0, "first visible slot at scroll 0");
    CHECK_EQ(vr.count, 15 * 7, "visible cells in 608 px");

    // hit tests: centre of the first visible cell, centre of the last, a gap
    {
        const int firstItem = grid.itemAtSlot(vr.first);
        const Rect r0 = grid.cellRectBySlot(vr.first);
        std::printf("  first visible cell slot %d item #%d %s \"%.*s\"\n",
                    vr.first, firstItem, rectStr(r0).c_str(),
                    static_cast<int>(cat.item(firstItem).name.size()),
                    cat.item(firstItem).name.data());
        CHECK_EQ(r0.x, 105, "first cell x");
        CHECK_EQ(r0.y, 4, "first cell y");
        CHECK_EQ(r0.w, 40, "cell width");
        CHECK_EQ(grid.hitTest(r0.x + r0.w / 2, r0.y + r0.h / 2), firstItem, "hit centre of first cell");
        CHECK(grid.cellRect(firstItem).contains(r0.x + 1, r0.y + 1), "cellRect(itemIndex) agrees");

        // 608 px / 42 px is 14.5 rows, so the last visible row is clipped by the
        // viewport: visibleRange() includes it (it has to be drawn) but only its
        // top strip is clickable.
        const int lastSlot = vr.first + vr.count - 1;
        const int lastItem = grid.itemAtSlot(lastSlot);
        const Rect rl = grid.cellRectBySlot(lastSlot);
        std::printf("  last visible cell  slot %d item #%d %s \"%.*s\" (clipped by %d px)\n",
                    lastSlot, lastItem, rectStr(rl).c_str(),
                    static_cast<int>(cat.item(lastItem).name.size()),
                    cat.item(lastItem).name.data(),
                    (rl.y + rl.h) - (transferGrid.y + transferGrid.h));
        CHECK(rl.y < transferGrid.y + transferGrid.h, "last visible cell starts inside the viewport");
        CHECK_EQ(grid.hitTest(rl.x + rl.w / 2, rl.y + 2), lastItem, "hit the visible strip of the last cell");
        CHECK_EQ(grid.hitTest(rl.x + rl.w / 2, rl.y + rl.h / 2), -1, "clipped part of the last cell is outside");

        // last cell that is fully inside the viewport
        int lastFull = lastSlot;
        while (lastFull > 0 &&
               grid.cellRectBySlot(lastFull).y + metrics.cellSize > transferGrid.y + transferGrid.h) {
            --lastFull;
        }
        const Rect rf = grid.cellRectBySlot(lastFull);
        const int fullItem = grid.itemAtSlot(lastFull);
        std::printf("  last fully visible slot %d item #%d %s \"%.*s\"\n",
                    lastFull, fullItem, rectStr(rf).c_str(),
                    static_cast<int>(cat.item(fullItem).name.size()),
                    cat.item(fullItem).name.data());
        CHECK_EQ(grid.hitTest(rf.x + rf.w / 2, rf.y + rf.h / 2), fullItem, "hit centre of the last full cell");
        CHECK(rf.y + rf.h <= transferGrid.y + transferGrid.h, "last full cell fits the viewport");

        const int gapX = r0.x + metrics.cellSize;          // between column 0 and 1
        const int gapY = r0.y + metrics.cellSize;          // between row 0 and 1
        std::printf("  hitTest gaps: column gap at x=%d -> %d, row gap at y=%d -> %d\n",
                    gapX, grid.hitTest(gapX, r0.y + 5), gapY, grid.hitTest(r0.x + 5, gapY));
        CHECK_EQ(grid.hitTest(gapX, r0.y + 5), -1, "column gap is not a cell");
        CHECK_EQ(grid.hitTest(r0.x + 5, gapY), -1, "row gap is not a cell");
        CHECK_EQ(grid.hitTest(transferGrid.x - 1, r0.y + 5), -1, "left of the viewport");
        CHECK_EQ(grid.hitTest(transferGrid.x + transferGrid.w, r0.y + 5), -1, "right of the viewport");
        CHECK_EQ(grid.hitTest(r0.x + 5, transferGrid.y + transferGrid.h), -1, "below the viewport");
        // 7 columns x 42 = 294, so x 399..424 is right-hand padding
        CHECK_EQ(grid.hitTest(transferGrid.x + 7 * 42, r0.y + 5), -1, "padding right of the last column");
    }

    // ------------------------------------------------------------- scrolling
    rule("scrolling");
    grid.scrollBy(1000000);
    std::printf("  scrolled to end: offset %d (max %d)\n", grid.scrollOffset(), grid.maxScroll());
    CHECK_EQ(grid.scrollOffset(), grid.maxScroll(), "scroll clamps to the end");
    vr = grid.visibleRange();
    std::printf("  visibleRange at end: first=%d count=%d, last slot %d of %d\n",
                vr.first, vr.count, vr.first + vr.count - 1, grid.visibleItemCount() - 1);
    CHECK_EQ(vr.first + vr.count, grid.visibleItemCount(), "last item visible at the end");
    {
        const int lastSlot = grid.visibleItemCount() - 1;
        const Rect rl = grid.cellRectBySlot(lastSlot);
        CHECK_EQ(grid.hitTest(rl.x + 5, rl.y + 5), grid.itemAtSlot(lastSlot), "hit the last item at the end");
        CHECK(rl.y + rl.h <= transferGrid.y + transferGrid.h, "last cell inside the viewport at max scroll");
    }
    grid.scrollBy(-1000000);
    std::printf("  scrolled back: offset %d\n", grid.scrollOffset());
    CHECK_EQ(grid.scrollOffset(), 0, "scroll clamps to the start");
    vr = grid.visibleRange();
    CHECK_EQ(vr.first, 0, "back at the top");
    grid.scrollTo(420);
    CHECK_EQ(grid.scrollOffset(), 420, "scrollTo inside the range");
    CHECK_EQ(grid.visibleRange().first, 10 * 7, "first visible row after scrolling 10 rows");
    grid.scrollTo(0);

    // -------------------------------------------- layout: 900x600 with headers
    rule("layout: 900x600 window with group headers");
    GridLayout wide;
    wide.setCatalogue(&cat);
    Metrics wideMetrics;
    wideMetrics.headerHeight = 18;
    wide.setMetrics(wideMetrics);
    const Viewport window{0, 0, 900, 600};
    wide.relayout(window, filters);
    std::printf("  visible items %d, columns %d, rows %d, sections %zu, contentHeight %d\n",
                wide.visibleItemCount(), wide.columns(), wide.rows(),
                wide.sections().size(), wide.contentHeight());
    CHECK_EQ(wide.columns(), (900 + 2) / 42, "columns in a 900 px wide grid");
    CHECK_EQ(wide.sections().size(), 15, "one section per non-empty default group (TQ: 15)");
    {
        int expectedRows = 0, expectedHeight = 0;
        for (const Section& s : wide.sections()) {
            const int rows = (s.count + wide.columns() - 1) / wide.columns();
            expectedRows += rows;
            expectedHeight += 18 + 2 + rows * 42;
        }
        CHECK_EQ(wide.rows(), expectedRows, "rows over all sections");
        CHECK_EQ(wide.contentHeight(), expectedHeight - 2, "content height with headers");
    }
    for (std::size_t i = 0; i < wide.sections().size(); ++i) {
        const Section& s = wide.sections()[i];
        std::printf("    %-11s %5d items, %3d rows, headerTop %6d, gridTop %6d\n",
                    slotGroupName(s.group), s.count, s.rows, s.headerTop, s.gridTop);
        CHECK_EQ(s.gridTop, s.headerTop + 20, "header occupies 18 px + 2 px gap");
        CHECK_EQ(wide.hitTest(10, s.headerTop - wide.scrollOffset() + 5), -1,
                 "header row is not a cell");
    }
    {
        const Rect h0 = wide.headerRect(0);
        std::printf("  headerRect(0) %s\n", rectStr(h0).c_str());
        CHECK_EQ(h0.h, 18, "header rect height");
        CHECK_EQ(h0.w, 900, "header rect width");
        const VisibleRange wr = wide.visibleRange();
        std::printf("  visibleRange: first=%d count=%d\n", wr.first, wr.count);
        CHECK_EQ(wr.first, 0, "first slot visible");
        CHECK(wr.count > 0 && wr.count <= wide.visibleItemCount(), "sane visible count");
        const Rect r0 = wide.cellRectBySlot(0);
        CHECK_EQ(r0.y, 20, "first cell sits under the first header");
        CHECK_EQ(wide.hitTest(r0.x + 5, r0.y + 5), wide.itemAtSlot(0), "hit the first cell");
    }

    // ------------------------------------------------------------ collection
    rule("collection: ownership events");
    Collection owned;
    owned.reset(&cat);
    CHECK_EQ(owned.ownedCount(), 0, "empty collection");

    const int idxA = known;                                   // Agamemnon's Death Mask
    const std::string recA(cat.item(idxA).record);
    const int idxB = cat.indexOfRecord(cat.item(0).record);
    const std::string recB(cat.item(0).record);

    owned.onItemAdded(1001, recA);
    CHECK_EQ(owned.owned(idxA), 1, "one copy after the first add");
    CHECK_EQ(owned.ownedCount(), 1, "one distinct item owned");
    CHECK(owned.isOwned(idxA), "isOwned");

    owned.onItemAdded(1001, recA);                            // duplicate id
    CHECK_EQ(owned.owned(idxA), 1, "duplicate id does not double count");
    CHECK_EQ(owned.duplicateIdEvents(), 1, "duplicate id counted");

    owned.onItemAdded(1002, recA);                            // second copy
    CHECK_EQ(owned.owned(idxA), 2, "two copies");
    CHECK_EQ(owned.ownedCount(), 1, "still one distinct item");
    CHECK_EQ(owned.totalItems(), 2, "two copies tracked");

    owned.onItemAdded(2001, "records/items/gearhead/not_a_real_record.dbr");
    owned.onItemAdded(2002, "");
    std::printf("  unknown record events %llu (%zu distinct), duplicate ids %llu\n",
                static_cast<unsigned long long>(owned.unknownRecordEvents()),
                owned.distinctUnknownRecords(),
                static_cast<unsigned long long>(owned.duplicateIdEvents()));
    CHECK_EQ(owned.unknownRecordEvents(), 2, "unknown records counted");
    CHECK_EQ(owned.distinctUnknownRecords(), 2, "distinct unknown records");
    CHECK_EQ(owned.ownedCount(), 1, "unknown records do not change ownership");

    owned.onItemRemoved(1002);
    CHECK_EQ(owned.owned(idxA), 1, "removal drops one copy");
    owned.onItemRemoved(999999);                              // never seen
    CHECK_EQ(owned.owned(idxA), 1, "unknown id removal is a no-op");
    owned.onItemRemoved(2001);                                // unknown record's id
    CHECK_EQ(owned.ownedCount(), 1, "unknown id removal keeps the count");
    owned.onItemRemoved(1001);
    CHECK_EQ(owned.owned(idxA), 0, "last copy removed");
    CHECK_EQ(owned.ownedCount(), 0, "nothing owned");
    CHECK_EQ(owned.trackedIds(), 0, "no live ids");

    // an id recycled onto a different record
    owned.onItemAdded(3000, recA);
    owned.onItemAdded(3000, recB);
    CHECK_EQ(owned.owned(idxA), 0, "recycled id released the old record");
    CHECK_EQ(owned.owned(idxB), 1, "recycled id owns the new record");
    owned.clear();
    CHECK_EQ(owned.ownedCount(), 0, "clear()");
    CHECK_EQ(owned.trackedIds(), 0, "clear() drops ids");

    // ------------------------------------------------------- owned filtering
    rule("filters");
    // four marked through the real event path, one through the test helper
    const int marked[5] = {0, 100, 500, 1000, 1587};
    for (int i = 0; i < 4; ++i) {
        owned.onItemAdded(static_cast<std::uint32_t>(4000 + i), cat.item(marked[i]).record);
        CHECK_EQ(owned.owned(marked[i]), 1, "onItemAdded marks the record owned");
    }
    CHECK(owned.setOwnedByRecord(cat.item(marked[4]).record, 1), "setOwnedByRecord on a catalogue record");
    CHECK_EQ(owned.owned(marked[4]), 1, "setOwnedByRecord marks the record owned");
    CHECK_EQ(owned.ownedCount(), 5, "five items owned");
    CHECK(!owned.setOwnedByRecord("records/items/nope.dbr", 1), "setOwnedByRecord rejects unknown");

    Filters ownedOnly = filters;
    ownedOnly.owned = OwnedFilter::OwnedOnly;
    grid.relayout(transferGrid, ownedOnly, &owned);
    std::printf("  owned-only: %d items, rows %d, contentHeight %d\n",
                grid.visibleItemCount(), grid.rows(), grid.contentHeight());
    CHECK_EQ(grid.visibleItemCount(), 5, "owned-only filter");
    for (int i = 0; i < grid.visibleItemCount(); ++i) {
        CHECK(owned.owned(grid.itemAtSlot(i)) > 0, "every shown item is owned");
    }
    CHECK_EQ(grid.contentHeight(), 40, "one row of owned items");
    CHECK_EQ(grid.maxScroll(), 0, "no scrolling needed");

    Filters missingOnly = filters;
    missingOnly.owned = OwnedFilter::MissingOnly;
    grid.relayout(transferGrid, missingOnly, &owned);
    std::printf("  missing-only: %d items\n", grid.visibleItemCount());
    CHECK_EQ(grid.visibleItemCount(), 1588 - 5, "missing-only filter");

    Filters legendaryOnly = filters;
    legendaryOnly.classificationMask = 1u << static_cast<int>(Classification::Legendary);
    grid.relayout(transferGrid, legendaryOnly);
    std::printf("  legendary-only: %d items\n", grid.visibleItemCount());
    CHECK_EQ(grid.visibleItemCount(), static_cast<int>(legendaryDefault), "classification mask");

    Filters helmsOnly = filters;
    helmsOnly.groupMask = groupBit(SlotGroup::Helm);
    grid.relayout(transferGrid, helmsOnly);
    std::printf("  helms only: %d items\n", grid.visibleItemCount());
    CHECK_EQ(grid.visibleItemCount(), cat.groupRange(SlotGroup::Helm).count, "slot-group mask");

    Filters named = filters;
    named.nameQuery = "AgAmEmNoN";
    grid.relayout(transferGrid, named);
    std::printf("  name contains \"agamemnon\" (case-insensitive): %d items, e.g. \"%.*s\"\n",
                grid.visibleItemCount(),
                static_cast<int>(cat.item(grid.itemAtSlot(0)).name.size()),
                cat.item(grid.itemAtSlot(0)).name.data());
    CHECK(grid.visibleItemCount() > 0, "name filter finds something");
    for (int i = 0; i < grid.visibleItemCount(); ++i) {
        CHECK(containsNoCase(cat.item(grid.itemAtSlot(i)).name, "agamemnon"), "name filter is exact");
    }
    named.nameQuery = "zzzz-no-such-item";
    grid.relayout(transferGrid, named);
    CHECK_EQ(grid.visibleItemCount(), 0, "name filter can empty the grid");
    CHECK_EQ(grid.contentHeight(), 0, "empty grid has no content");
    CHECK_EQ(grid.hitTest(200, 200), -1, "empty grid hit test");
    CHECK_EQ(grid.visibleRange().count, 0, "empty grid visible range");

    Filters everything = filters;
    everything.groupMask = kAllGroupsMask;
    everything.requireDefaultVisible = false;
    grid.relayout(transferGrid, everything);
    std::printf("  no filters at all: %d items\n", grid.visibleItemCount());
    CHECK_EQ(grid.visibleItemCount(), cat.itemCount(), "unfiltered layout shows everything");

    // ---- the slot packer over the real uniq-groups.txt ---------------------------------
    rule("slot packer (uniform slots per group, 16 x 15 Transfer pages)");
    {
        // synthetic: the slot is the largest footprint, reading order, centring, page breaks
        std::vector<PackItem> syn;
        for (int i = 0; i < 70; ++i) syn.push_back(PackItem{i, 2, 2});   // 8 x 7 = 56 per page
        std::vector<PackPlacement> pl;
        SlotGeometry sg;
        CHECK_EQ(packSlots(syn, kHostCols, kHostRows, &pl, &sg), 2, "70 2x2 items -> 2 pages");
        CHECK(sg.w == 2 && sg.h == 2 && sg.cols == 8 && sg.rows == 7, "2x2 slot, 8 x 7 per page");
        CHECK_EQ(pl[55].page, 0, "the 56th 2x2 is still on page 1");
        CHECK(pl[55].slotCol == 14 && pl[55].slotRow == 12, "the 56th sits in the last slot");
        CHECK_EQ(pl[56].page, 1, "the 57th opens page 2");
        CHECK(pl[56].col == 0 && pl[56].row == 0, "a new page starts top-left");
        // mixed footprints: slot 2x5 (max w 2 from a 2x3, max h 5 from a 1x5)
        std::vector<PackItem> mix{PackItem{0, 1, 3}, PackItem{1, 2, 3}, PackItem{2, 1, 5},
                                  PackItem{3, 1, 1}, PackItem{4, 2, 2}};
        CHECK_EQ(packSlots(mix, kHostCols, kHostRows, &pl, &sg), 1, "5 mixed items -> 1 page");
        CHECK(sg.w == 2 && sg.h == 5 && sg.cols == 8 && sg.rows == 3, "slot = max w x max h");
        CHECK(pl[0].slotCol == 0 && pl[0].col == 0 && pl[0].row == 1, "1x3 in 2x5: (0,1)");
        CHECK(pl[1].slotCol == 2 && pl[1].col == 2 && pl[1].row == 1, "2x3 in 2x5: (2,1)");
        CHECK(pl[2].slotCol == 4 && pl[2].col == 4 && pl[2].row == 0, "1x5 in 2x5: (4,0)");
        CHECK(pl[3].slotCol == 6 && pl[3].col == 6 && pl[3].row == 2, "1x1 in 2x5: (6,2)");
        CHECK(pl[4].slotCol == 8 && pl[4].col == 8 && pl[4].row == 1, "2x2 in 2x5: (8,1)");
        std::vector<PackItem> bad{PackItem{0, 3, 1}};
        CHECK_EQ(packSlots(bad, 2, 15, &pl), -1, "a footprint wider than the page is refused");
        CHECK(pl.empty(), "nothing placed when refused");
        CHECK_EQ(packSlots(std::vector<PackItem>(), 16, 15, &pl), 0, "no items, no page");
        std::vector<PackItem> zero{PackItem{0, 0, 1}};
        CHECK_EQ(packSlots(zero, 16, 15, &pl), -1, "a 0-wide footprint is refused");
    }
    {
        const char* gpath = (argc > 2) ? argv[2] : "data/oracle/uniq-groups.txt";
        std::vector<std::uint8_t> raw = readFile(gpath);
        CHECK(!raw.empty(), "uniq-groups.txt readable");
        std::string text(raw.begin(), raw.end());
        std::vector<GroupList> groups;
        std::string gerr;
        CHECK(parseGroupsText(text, &groups, &gerr), gerr.c_str());
        CHECK_EQ(groups.size(), 15, "15 groups");
        std::vector<GroupList> junk;
        CHECK(!parseGroupsText("E\trecords/x.dbr\t1\t1\r\n", &junk, &gerr), "E before G refused");
        CHECK(!parseGroupsText("G\t0\tX\t16\t15\t32\t32\t2\r\nE\ta\t1\t1\r\n", &junk, &gerr),
              "a short group refused");
        CHECK(!parseGroupsText("G\t0\tX\t8\t7\t64\t64\t1\r\nE\ta\t3\t1\r\n", &junk, &gerr),
              "a 3-wide footprint refused");
        CHECK(parseGroupsText("G\t0\tX\t8\t3\t64\t128\t2\r\nE\ta\t1\t4\r\nE\tb\t2\t3\r\n", &junk,
                              &gerr),
              "a G line carrying its entries' slot (2x4 -> 8 x 3, 64 x 128 px) is accepted");
        CHECK(!parseGroupsText("G\t0\tX\t8\t5\t64\t96\t2\r\nE\ta\t1\t4\r\nE\tb\t2\t3\r\n", &junk,
                               &gerr),
              "a G line whose geometry is not its entries' slot is refused");
        CHECK(!parseGroupsText("G\t0\tX\t8\t3\t64\t128\t1\r\nE\ta\t2\t3\r\nG\t1\tY\t16\t15\t32\t32"
                               "\t1\r\nE\tb\t1\t1\r\n", &junk, &gerr),
              "a wrong G geometry is refused before the next group too");
        // the page table (section 1), from the data's own footprints
        struct Expect { const char* label; int w, h, per, pages; };
        static const Expect kTable[15] = {
            {"Helms", 2, 2, 56, 3},  {"Torso", 2, 3, 40, 7},    {"Arms", 2, 2, 56, 3},
            {"Legs", 2, 2, 56, 3},   {"Amulets", 2, 1, 120, 1}, {"Rings", 1, 1, 240, 1},
            {"Shields", 2, 3, 40, 3}, {"Axes", 2, 3, 40, 2},    {"Maces", 2, 4, 24, 4},
            {"Staves", 2, 4, 24, 4}, {"Swords", 2, 4, 24, 4},   {"Throwing", 2, 3, 40, 2},
            {"Spears", 1, 5, 48, 2}, {"Bows", 1, 4, 48, 2},     {"Artifacts", 2, 2, 56, 2}};
        int totalPages = 0, totalRecords = 0;
        std::unordered_map<std::string, int> seen;
        for (std::size_t g = 0; g < groups.size(); ++g) {
            const GroupList& gl = groups[g];
            std::vector<PackItem> items;
            for (std::size_t k = 0; k < gl.entries.size(); ++k)
                items.push_back(PackItem{static_cast<int>(k), gl.entries[k].w, gl.entries[k].h});
            std::vector<PackPlacement> pl;
            SlotGeometry sg;
            const int pages = packSlots(items, kHostCols, kHostRows, &pl, &sg);
            CHECK(pages >= 1, "every group packs");
            CHECK_EQ(pl.size(), items.size(), "every entry placed exactly once");
            CHECK(sg.cols == gl.cols && sg.rows == gl.rows && sg.w * kCellPx == gl.cellW &&
                      sg.h * kCellPx == gl.cellH,
                  "the G line carries the slot");
            int maxW = 0, maxH = 0;
            for (const GroupEntry& e : gl.entries) {
                maxW = std::max(maxW, e.w);
                maxH = std::max(maxH, e.h);
            }
            CHECK(sg.w == maxW && sg.h == maxH, "slot = the group's largest footprint");
            CHECK_EQ(pages, (static_cast<int>(items.size()) + sg.perPage() - 1) / sg.perPage(),
                     "pages = ceil(records / slots per page)");
            CHECK(g < 15 && gl.label == kTable[g].label && sg.w == kTable[g].w &&
                      sg.h == kTable[g].h && sg.perPage() == kTable[g].per &&
                      pages == kTable[g].pages,
                  "the design notes' page table row");
            std::vector<unsigned char> occ(static_cast<std::size_t>(pages) * kHostCols * kHostRows,
                                           0);
            std::vector<unsigned char> slotUsed(static_cast<std::size_t>(pages) * kHostCols *
                                                    kHostRows,
                                                0);
            for (std::size_t i = 0; i < pl.size(); ++i) {
                const PackPlacement& p = pl[i];
                const GroupEntry& e = gl.entries[static_cast<std::size_t>(p.index)];
                // catalogue order: placement i is entry i, in reading order of the slots
                CHECK_EQ(p.index, static_cast<int>(i), "catalogue order kept");
                const int k = static_cast<int>(i) % sg.perPage();
                CHECK_EQ(p.page, static_cast<int>(i) / sg.perPage(), "page = order / per page");
                CHECK(p.slotCol == (k % sg.cols) * sg.w && p.slotRow == (k / sg.cols) * sg.h,
                      "slot-aligned, reading order");
                CHECK(p.col == p.slotCol + (sg.w - e.w) / 2 && p.row == p.slotRow + (sg.h - e.h) / 2,
                      "centred in its slot");
                CHECK(p.col >= p.slotCol && p.row >= p.slotRow && p.col + e.w <= p.slotCol + sg.w &&
                          p.row + e.h <= p.slotRow + sg.h,
                      "the footprint stays inside its slot");
                CHECK(p.slotCol >= 0 && p.slotRow >= 0 && p.slotCol + sg.w <= kHostCols &&
                          p.slotRow + sg.h <= kHostRows,
                      "the slot is inside the 16 x 15 grid");
                for (int y = p.slotRow; y < p.slotRow + sg.h; ++y)
                    for (int x = p.slotCol; x < p.slotCol + sg.w; ++x) {
                        unsigned char& c = slotUsed[(static_cast<std::size_t>(p.page) * kHostRows +
                                                     y) * kHostCols + x];
                        CHECK(c == 0, "no two slots share a cell");
                        c = 1;
                    }
                for (int y = p.row; y < p.row + e.h; ++y)
                    for (int x = p.col; x < p.col + e.w; ++x) {
                        unsigned char& c =
                            occ[(static_cast<std::size_t>(p.page) * kHostRows + y) * kHostCols + x];
                        CHECK(c == 0, "no two items share a cell");
                        c = 1;
                    }
                ++seen[e.record];
            }
            std::printf("  %-10s %4zu records  slot %dx%d  %2d x %2d = %3d per page  -> %d page(s)\n",
                        gl.label.c_str(), items.size(), sg.w, sg.h, sg.cols, sg.rows, sg.perPage(),
                        pages);
            totalPages += pages;
            totalRecords += static_cast<int>(items.size());
        }
        std::printf("  total: %d records on %d pages\n", totalRecords, totalPages);
        CHECK_EQ(totalRecords, 1588, "every catalogue record is on a page");
        CHECK_EQ(seen.size(), 1588, "each record exactly once");
        CHECK_EQ(totalPages, 43, "43 pages in total (the design notes' table)");
        for (const auto& kv : seen)
            CHECK(cat.indexOfRecord(kv.first) >= 0, "every paged record is in the catalogue");

        // ---- the OWN filter (GD's owned-only view) over the same groups ---------------
        rule("OWN filter (owned records only, the group's same slots, catalogue order)");
        {   // synthetic: the GROUP's slot is kept even when the owned subset is smaller
            SlotGeometry g;
            std::vector<PackItem> mix = {{0, 1, 1}, {1, 2, 3}, {2, 1, 2}, {3, 2, 2}, {4, 1, 3}};
            CHECK(slotOf(mix, kHostCols, kHostRows, &g) && g.w == 2 && g.h == 3, "mix slot 2x3");
            const unsigned char fw[5] = {1, 2, 1, 2, 1}, fh[5] = {1, 3, 2, 2, 3};
            const unsigned char own[5] = {1, 0, 0, 1, 1};
            PackPlacement out[8];
            int shown = -1;
            const int n = placePage(g, fw, fh, 5, own, 0, out, 8, &shown);
            CHECK_EQ(n, 3, "3 owned records on page 0");
            CHECK_EQ(shown, 3, "3 shown in all");
            CHECK(out[0].index == 0 && out[1].index == 3 && out[2].index == 4, "catalogue order");
            CHECK(out[0].slotCol == 0 && out[1].slotCol == 2 && out[2].slotCol == 4 &&
                      out[2].slotRow == 0,
                  "consecutive 2x3 slots: the owned records close the gaps");
            CHECK(out[0].col == 0 && out[0].row == 1 && out[1].col == 2 && out[1].row == 0 &&
                      out[2].col == 4 && out[2].row == 0,
                  "each centred in the group's 2x3 slot (floor)");
            const unsigned char none[5] = {0, 0, 0, 0, 0};
            CHECK_EQ(placePage(g, fw, fh, 5, none, 0, out, 8, &shown), 0, "nothing owned: empty");
            CHECK_EQ(shown, 0, "nothing shown");
            CHECK_EQ(pagesFor(g, 0), 1, "an empty OWN view is ONE page, never zero");
            CHECK_EQ(pagesFor(g, 40), 1, "40 in a 40-slot page: 1");
            CHECK_EQ(pagesFor(g, 41), 2, "41: 2");
            CHECK_EQ(pagesFor(SlotGeometry(), 5), 0, "no geometry, no page");
            CHECK_EQ(placePage(g, fw, fh, 5, own, 0, out, 2, &shown), 2, "cap honoured");
            CHECK_EQ(shown, 3, "shown counts past the cap");
            PackPlacement one;
            CHECK(!placeInSlot(g, 0, 0, 3, 1, &one), "a footprint wider than the slot refused");
            CHECK(!placeInSlot(g, -1, 0, 1, 1, &one), "a negative ordinal refused");
        }
        {   // the real groups, many owned masks: the filter is packSlots over the owned subsequence
            unsigned rng = 0x2545F491u;
            int masks = 0, placedTotal = 0;
            for (std::size_t gi = 0; gi < groups.size(); ++gi) {
                const GroupList& gl = groups[gi];
                const int n = static_cast<int>(gl.entries.size());
                std::vector<PackItem> items;
                std::vector<unsigned char> fw(static_cast<std::size_t>(n)), fh(static_cast<std::size_t>(n));
                for (int k = 0; k < n; ++k) {
                    const GroupEntry& e = gl.entries[static_cast<std::size_t>(k)];
                    items.push_back(PackItem{k, e.w, e.h});
                    fw[static_cast<std::size_t>(k)] = static_cast<unsigned char>(e.w);
                    fh[static_cast<std::size_t>(k)] = static_cast<unsigned char>(e.h);
                }
                std::vector<PackPlacement> all;
                SlotGeometry sg;
                const int allPages = packSlots(items, kHostCols, kHostRows, &all, &sg);
                std::vector<PackPlacement> buf(static_cast<std::size_t>(sg.perPage()));
                // the unfiltered view IS packSlots, page by page
                std::size_t at = 0;
                for (int pg = 0; pg < allPages; ++pg) {
                    int shown = 0;
                    const int m = placePage(sg, fw.data(), fh.data(), n, nullptr, pg, buf.data(),
                                            sg.perPage(), &shown);
                    CHECK_EQ(shown, n, "unfiltered: every record shown");
                    for (int i = 0; i < m; ++i, ++at) {
                        const PackPlacement& a = buf[static_cast<std::size_t>(i)];
                        const PackPlacement& b = all[at];
                        CHECK(a.index == b.index && a.page == b.page && a.col == b.col &&
                                  a.row == b.row && a.slotCol == b.slotCol && a.slotRow == b.slotRow,
                              "placePage(no filter) == packSlots");
                    }
                }
                CHECK_EQ(at, all.size(), "placePage(no filter) places every record once");
                for (int mk = 0; mk < 9; ++mk) {
                    std::vector<unsigned char> own(static_cast<std::size_t>(n));
                    for (int k = 0; k < n; ++k) {
                        rng = rng * 1664525u + 1013904223u;
                        const unsigned r = (rng >> 8) % 100u;
                        bool f = false;
                        switch (mk) {
                        case 0: f = true; break;
                        case 1: f = false; break;
                        case 2: f = k % 2 == 0; break;
                        case 3: f = k % 3 == 1; break;
                        case 4: f = k == n - 1; break;
                        case 5: f = k == 0; break;
                        case 6: f = r < 10u; break;
                        case 7: f = r < 50u; break;
                        default: f = r < 90u; break;
                        }
                        own[static_cast<std::size_t>(k)] = f ? 1 : 0;
                    }
                    ++masks;
                    std::vector<PackItem> sub;
                    for (int k = 0; k < n; ++k)
                        if (own[static_cast<std::size_t>(k)]) sub.push_back(items[static_cast<std::size_t>(k)]);
                    const int ownedN = static_cast<int>(sub.size());
                    const int pages = pagesFor(sg, ownedN);
                    CHECK_EQ(pages, ownedN == 0 ? 1 : (ownedN + sg.perPage() - 1) / sg.perPage(),
                             "OWN pages = max(1, ceil(owned / per page))");
                    int ordinal = 0, lastIndex = -1;
                    for (int pg = 0; pg <= pages; ++pg) {   // one past the end: must be empty
                        int shown = -1;
                        const int m = placePage(sg, fw.data(), fh.data(), n, own.data(), pg,
                                                buf.data(), sg.perPage(), &shown);
                        CHECK_EQ(shown, ownedN, "shown = the owned count");
                        if (pg == pages) {
                            CHECK_EQ(m, 0, "nothing past the last OWN page");
                            break;
                        }
                        std::vector<unsigned char> used(static_cast<std::size_t>(kHostCols * kHostRows), 0);
                        for (int i = 0; i < m; ++i, ++ordinal) {
                            const PackPlacement& p = buf[static_cast<std::size_t>(i)];
                            CHECK(own[static_cast<std::size_t>(p.index)] != 0, "only owned records");
                            CHECK(p.index > lastIndex, "catalogue order across pages");
                            lastIndex = p.index;
                            const int k = ordinal % sg.perPage();
                            CHECK(p.page == pg && ordinal / sg.perPage() == pg, "page = ordinal / per page");
                            CHECK(p.slotCol == (k % sg.cols) * sg.w && p.slotRow == (k / sg.cols) * sg.h,
                                  "consecutive slots in reading order (no gaps)");
                            const GroupEntry& e = gl.entries[static_cast<std::size_t>(p.index)];
                            CHECK(p.col == p.slotCol + (sg.w - e.w) / 2 &&
                                      p.row == p.slotRow + (sg.h - e.h) / 2,
                                  "centred in the GROUP's slot");
                            for (int y = p.slotRow; y < p.slotRow + sg.h; ++y)
                                for (int x = p.slotCol; x < p.slotCol + sg.w; ++x) {
                                    CHECK(x < kHostCols && y < kHostRows, "inside 16 x 15");
                                    unsigned char& c = used[static_cast<std::size_t>(y * kHostCols + x)];
                                    CHECK(c == 0, "no two slots share a cell");
                                    c = 1;
                                }
                            ++placedTotal;
                        }
                        if (pg < pages - 1) CHECK_EQ(m, sg.perPage(), "every OWN page but the last is full");
                    }
                    CHECK_EQ(ordinal, ownedN, "every owned record placed exactly once");
                    // the same answer as packing the owned subsequence, whenever its slot agrees
                    if (ownedN > 0) {
                        SlotGeometry subSlot;
                        std::vector<PackPlacement> subPl;
                        packSlots(sub, kHostCols, kHostRows, &subPl, &subSlot);
                        if (subSlot.w == sg.w && subSlot.h == sg.h) {
                            std::size_t o = 0;
                            for (int pg = 0; pg < pages; ++pg) {
                                const int m = placePage(sg, fw.data(), fh.data(), n, own.data(), pg,
                                                        buf.data(), sg.perPage());
                                for (int i = 0; i < m; ++i, ++o) {
                                    const PackPlacement& a = buf[static_cast<std::size_t>(i)];
                                    CHECK(a.col == subPl[o].col && a.row == subPl[o].row &&
                                              a.page == subPl[o].page,
                                          "OWN == packSlots(owned subsequence) when the slot agrees");
                                }
                            }
                        }
                    }
                }
            }
            std::printf("  OWN: %d masks over %zu groups, %d owned placements checked\n", masks,
                        groups.size(), placedTotal);
        }

        // ---- the row-offset window, its clamp and the leftover cells ------------------
        rule("window (row offset, full windows, leftover cells) - 1x1 / 2x2 / 2x3 / 2x4 slots "
             "+ the real 1x4 / 1x5 / 2x1");
        {
            const int shapes[7][2] = {{1, 1}, {2, 2}, {2, 3}, {2, 4}, {1, 4}, {1, 5}, {2, 1}};
            const int wantCols[7] = {16, 8, 8, 8, 16, 16, 8}, wantRows[7] = {15, 7, 5, 3, 3, 3, 15};
            long long windows = 0;
            for (int si = 0; si < 7; ++si) {
                SlotGeometry g;
                g.w = shapes[si][0];
                g.h = shapes[si][1];
                g.cols = kHostCols / g.w;
                g.rows = kHostRows / g.h;
                CHECK(g.cols == wantCols[si] && g.rows == wantRows[si], "the expected slots per window");
                const int per = g.perPage();
                std::vector<unsigned char> fw(static_cast<std::size_t>(3 * per + 7), static_cast<unsigned char>(g.w));
                std::vector<unsigned char> fh(fw.size(), static_cast<unsigned char>(g.h));
                std::vector<PackPlacement> buf(static_cast<std::size_t>(per));
                for (int shown = 0; shown <= 3 * per + 7; ++shown) {
                    const int rowsNeed = slotRowsFor(g, shown);
                    CHECK_EQ(rowsNeed, (shown + g.cols - 1) / g.cols, "slot rows = ceil(shown / cols)");
                    const int mx = maxRowOffset(g, shown);
                    CHECK_EQ(mx, rowsNeed > g.rows ? rowsNeed - g.rows : 0, "max offset");
                    for (int off = -3; off <= mx + 3; ++off) {
                        const int c = clampRowOffset(g, shown, off);
                        CHECK_EQ(c, off < 0 ? 0 : off > mx ? mx : off, "clamped at both ends");
                        if (off != c) continue;
                        ++windows;
                        int sh = -1;
                        const int m = placeWindow(g, fw.data(), fh.data(), shown, nullptr, c,
                                                  buf.data(), per, &sh);
                        CHECK_EQ(sh, shown, "shown counts every record");
                        const int inWin = std::min(per, std::max(0, shown - c * g.cols));
                        CHECK_EQ(m, inWin, "the window holds its records");
                        if (rowsNeed >= g.rows)
                            CHECK(m > (g.rows - 1) * g.cols, "FULL window: every slot row holds a record");
                        std::vector<unsigned char> used(static_cast<std::size_t>(kHostCols * kHostRows), 0);
                        for (int i = 0; i < m; ++i) {
                            const PackPlacement& p = buf[static_cast<std::size_t>(i)];
                            CHECK_EQ(p.index, c * g.cols + i, "the window starts at ordinal offset x cols");
                            CHECK_EQ(p.page, 0, "a window is page 0");
                            CHECK(p.slotCol == (i % g.cols) * g.w && p.slotRow == (i / g.cols) * g.h,
                                  "reading order from the window's top");
                            for (int y = p.slotRow; y < p.slotRow + g.h; ++y)
                                for (int x = p.slotCol; x < p.slotCol + g.w; ++x) {
                                    CHECK(x < kHostCols && y < kHostRows, "inside 16 x 15");
                                    used[static_cast<std::size_t>(y * kHostCols + x)] = 1;
                                }
                        }
                        CellRect lo[3];
                        const int nl = windowLeftover(g, shown, c, kHostCols, kHostRows, lo);
                        CHECK(nl >= 0 && nl <= 3, "at most three leftover rectangles");
                        for (int r = 0; r < nl; ++r) {
                            CHECK(lo[r].w > 0 && lo[r].h > 0, "no empty rectangle");
                            for (int y = lo[r].row; y < lo[r].row + lo[r].h; ++y)
                                for (int x = lo[r].col; x < lo[r].col + lo[r].w; ++x) {
                                    CHECK(x >= 0 && y >= 0 && x < kHostCols && y < kHostRows,
                                          "a leftover cell is on the grid");
                                    unsigned char& u = used[static_cast<std::size_t>(y * kHostCols + x)];
                                    CHECK(u == 0, "the cover never hides a record's slot (nor twice)");
                                    u = 2;
                                }
                        }
                        int left = 0;
                        for (unsigned char u : used) left += u == 0;
                        CHECK_EQ(left, 0, "slots + leftover = the whole 16 x 15 grid");
                        if (si == 0 && m == per) CHECK_EQ(nl, 0, "a full 1x1 window leaves nothing");
                        if (si == 2 && m == per) CHECK_EQ(nl, 0, "a full 2x3 window leaves nothing (8 x 5)");
                        if (si == 1 && m == per)
                            CHECK(nl == 1 && lo[0].row == 14 && lo[0].h == 1, "2x2: ONE leftover cell row");
                        if (si == 3 && m == per)
                            CHECK(nl == 1 && lo[0].row == 12 && lo[0].h == 3, "2x4: THREE leftover cell rows");
                        if (si == 4 && m == per)
                            CHECK(nl == 1 && lo[0].row == 12 && lo[0].h == 3, "1x4 (Bows): THREE leftover cell rows");
                        if ((si == 5 || si == 6) && m == per)
                            CHECK_EQ(nl, 0, "a full 1x5 / 2x1 window leaves nothing");
                    }
                }
                if (si == 1) {   // the wheel and the window step, clamped (57 records in 2x2)
                    CHECK_EQ(maxRowOffset(g, 57), 1, "57 in 2x2 = 8 slot rows: offsets 0..1");
                    CHECK_EQ(clampRowOffset(g, 57, 0 + g.rows), 1,
                             "a window step past the end lands on the last FULL window (overlap)");
                    CHECK_EQ(clampRowOffset(g, 57, 1 - g.rows), 0, "a window step back to 0");
                    int sh = 0;
                    CHECK_EQ(placeWindow(g, fw.data(), fh.data(), 57, nullptr, 1, buf.data(), per, &sh), 49,
                             "the last window shows 49 (not the page model's lone 57th record)");
                    CHECK_EQ(placeWindow(g, fw.data(), fh.data(), 57, nullptr, 7, buf.data(), per, &sh), 1,
                             "mutant: the unclamped page-2 offset (7) shows one record");
                    CHECK_EQ(maxRowOffset(g, 56), 0, "exactly one window: no scroll");
                    CHECK_EQ(maxRowOffset(g, 0), 0, "empty: no scroll");
                }
            }
            CellRect lo[3];
            CHECK(windowLeftover(SlotGeometry(), 0, 0, kHostCols, kHostRows, lo) == 1 && lo[0].w == 16 &&
                      lo[0].h == 15,
                  "no geometry / nothing shown: the whole grid is left over");
            std::printf("  window: %lld windows checked over 7 slot shapes\n", windows);
        }
        {   // the real groups: every offset of every group (OWN off), each record in some window
            long long checked = 0;
            for (std::size_t gi = 0; gi < groups.size(); ++gi) {
                const GroupList& gl = groups[gi];
                const int n = static_cast<int>(gl.entries.size());
                std::vector<PackItem> items;
                std::vector<unsigned char> fw(static_cast<std::size_t>(n)), fh(static_cast<std::size_t>(n));
                for (int k = 0; k < n; ++k) {
                    const GroupEntry& e = gl.entries[static_cast<std::size_t>(k)];
                    items.push_back(PackItem{k, e.w, e.h});
                    fw[static_cast<std::size_t>(k)] = static_cast<unsigned char>(e.w);
                    fh[static_cast<std::size_t>(k)] = static_cast<unsigned char>(e.h);
                }
                SlotGeometry sg;
                std::vector<PackPlacement> all;
                packSlots(items, kHostCols, kHostRows, &all, &sg);
                std::vector<PackPlacement> buf(static_cast<std::size_t>(sg.perPage()));
                std::vector<int> seenAt(static_cast<std::size_t>(n), 0);
                for (int off = 0; off <= maxRowOffset(sg, n); ++off) {
                    const int m = placeWindow(sg, fw.data(), fh.data(), n, nullptr, off, buf.data(),
                                              sg.perPage());
                    if (slotRowsFor(sg, n) >= sg.rows)
                        CHECK(m > (sg.rows - 1) * sg.cols, "real group: every window full");
                    for (int i = 0; i < m; ++i) {
                        const PackPlacement& p = buf[static_cast<std::size_t>(i)];
                        const PackPlacement& a = all[static_cast<std::size_t>(p.index)];
                        CHECK(p.col - p.slotCol == a.col - a.slotCol && p.row - p.slotRow == a.row - a.slotRow,
                              "the same centring as the page model");
                        CHECK(p.slotRow / sg.h + off == p.index / sg.cols, "slot row = ordinal row - offset");
                        ++seenAt[static_cast<std::size_t>(p.index)];
                        ++checked;
                    }
                }
                for (int k = 0; k < n; ++k)
                    CHECK(seenAt[static_cast<std::size_t>(k)] >= 1, "every record is in some window");
            }
            std::printf("  window: %lld real placements checked\n", checked);
        }
        {   // the OWN mask - the offset counts SHOWN ordinals, *shown = the mask's count
            long long masked = 0;
            for (std::size_t gi = 0; gi < groups.size(); ++gi) {
                const GroupList& gl = groups[gi];
                const int n = static_cast<int>(gl.entries.size());
                std::vector<PackItem> items;
                std::vector<unsigned char> fw(static_cast<std::size_t>(n)), fh(static_cast<std::size_t>(n));
                for (int k = 0; k < n; ++k) {
                    const GroupEntry& e = gl.entries[static_cast<std::size_t>(k)];
                    items.push_back(PackItem{k, e.w, e.h});
                    fw[static_cast<std::size_t>(k)] = static_cast<unsigned char>(e.w);
                    fh[static_cast<std::size_t>(k)] = static_cast<unsigned char>(e.h);
                }
                SlotGeometry sg;
                std::vector<PackPlacement> all;
                packSlots(items, kHostCols, kHostRows, &all, &sg);
                const int per = sg.perPage();
                std::vector<PackPlacement> buf(static_cast<std::size_t>(per));
                for (int pat = 0; pat < 4; ++pat) {
                    std::vector<unsigned char> show(static_cast<std::size_t>(n), 0);
                    std::vector<int> ords;   // shown ordinal -> record index
                    for (int k = 0; k < n; ++k) {
                        const bool on = pat == 0 ? k % 3 != 1 : pat == 1 ? k % 7 == 0 : pat == 2 ? k >= n / 2 : false;
                        show[static_cast<std::size_t>(k)] = on ? 1 : 0;
                        if (on) ords.push_back(k);
                    }
                    const int cnt = static_cast<int>(ords.size());
                    for (int off = 0; off <= maxRowOffset(sg, cnt); ++off) {
                        int sh = -1;
                        const int m = placeWindow(sg, fw.data(), fh.data(), n, show.data(), off, buf.data(),
                                                  per, &sh);
                        CHECK_EQ(sh, cnt, "OWN: *shown = the mask's count");
                        CHECK_EQ(m, std::min(per, std::max(0, cnt - off * sg.cols)),
                                 "OWN: the window holds its shown records");
                        if (slotRowsFor(sg, cnt) >= sg.rows)
                            CHECK(m > (sg.rows - 1) * sg.cols, "OWN: every window full");
                        for (int i = 0; i < m; ++i) {
                            const PackPlacement& p = buf[static_cast<std::size_t>(i)];
                            CHECK_EQ(p.index, ords[static_cast<std::size_t>(off * sg.cols + i)],
                                     "OWN: slot i of window `off` = shown ordinal off x cols + i");
                            CHECK(show[static_cast<std::size_t>(p.index)] != 0, "OWN: only shown records");
                            ++masked;
                        }
                    }
                    CHECK_EQ(clampRowOffset(sg, cnt, maxRowOffset(sg, n) + 1), maxRowOffset(sg, cnt),
                             "OWN: an unfiltered offset clamps to the filtered last window");
                }
            }
            std::printf("  window: %lld OWN-masked placements checked\n", masked);
        }
    }

    std::printf("\n%d checks passed\n", g_checks);
    std::printf("MODEL TESTS PASSED\n");
    return 0;
}
