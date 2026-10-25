// test_layout.cpp - the layout model (src\ut_layout.h) and the pad's pages (src\ut_padpage.h),
// offline. NO GAME, NO FILE, NO WINDOWS: the headers are included, so this runs exactly the code
// the mod runs. GD's sections that apply to TQ: the text of a [layout] line, the pad fitted to a
// rectangle (TQ's two rows), the label, the resolution of the player's rectangles against the
// fixed grid and each other, the pages for N buttons, and the layout from the measured page; the
// game's own Transfer page (GD's materials-page rule): the pad and the label stay where the
// collection page has them, over the real cells they cover, counted and said as the log says it.
#include <stdio.h>
#include <string.h>

#include <vector>

#include "../src/ut_layout.h"
#include "../src/ut_gridpack.h"   // the re-fit on new cells, over the packer the live table runs
#include "../src/ut_screendraw.h"   // the screen parts' frame and what the input reads
#include "../src/ut_caravangeo.h"   // the derived area's band (utCaravanPadBandFor)
#include "../src/ut_reliclook.h"   // the relic page's look (gray icon, lit slots, the ground runs)

namespace {

int g_fail = 0, g_pass = 0;

void ok(bool cond, const char* what, const char* detail = "") {
    if (cond) {
        ++g_pass;
        printf("  ok    %s%s%s\n", what, detail[0] ? " - " : "", detail);
    } else {
        ++g_fail;
        printf("  FAIL  %s%s%s\n", what, detail[0] ? " - " : "", detail);
    }
}

// The measured page of the sweep (test_viewgate's): the frame 565 x 637 x s centred on the canvas,
// the page at + (0,126) s, the grid at the page + (27,0) s, 16 x 15 of the engine's cell.
bool page(float s, float cw, float ch, ut::UtRectF* f, ut::UtRectF* g) {
    ut::UtFrameIn in;
    in.scale = s;
    in.canvasW = cw;
    in.canvasH = ch;
    in.originX = ut::utPadFloor((cw - 565.0f * s) * 0.5f);
    in.originY = (ch - 637.0f * s) * 0.5f + 126.0f * s;
    in.gridX = in.originX + 27.0f * s;
    in.gridY = in.originY;
    in.cellW = ut::utEngineCellPx(s);
    in.cellH = in.cellW;
    in.pageX = 0.0f;
    in.pageY = 126.0f;
    in.frameW = 565.0f;
    in.frameH = 637.0f;
    return ut::utFrameFromHover(in, f, g) == ut::kUtFrameOk;
}

ut::UtLayoutSource source(const ut::UtRectF& f, const ut::UtRectF& g, float s, float cw, float ch,
                          const char* pad, const char* label) {
    ut::UtLayoutSource src;
    src.frame = f;
    src.grid = g;
    src.scale = s;
    src.canvasW = cw;
    src.canvasH = ch;
    src.coverW = 565;
    src.coverH = 637;
    src.pad = pad;
    src.label = label;
    src.groups = 15;
    src.widest = 5;   // Staff, Sword, Spear, Throw, Torso
    src.labelPx = (int)ut::utPadRound(13.0f * s) < 6 ? 6 : (int)ut::utPadRound(13.0f * s);
    src.search = true;
    return src;
}

// The panel's hit test over a UtPadOut: the clear square first, then the buttons in index order.
int hitOut(const ut::UtPadOut& o, float x, float y) {
    if (o.btn[ut::kUtPadClear].w > 0.0f && ut::utRectHas(o.btn[ut::kUtPadClear], x, y))
        return ut::kUtPadClear;
    for (int i = 0; i < ut::kUtPadMax; ++i)
        if (i != ut::kUtPadClear && o.btn[i].w > 0.0f && ut::utRectHas(o.btn[i], x, y)) return i;
    return -1;
}

}  // namespace

int main() {
    char d[256];
    printf("1. the text of a [layout] line (GD's grammar)\n");
    {
        ut::UtRect r;
        ok(ut::utRectParse("34,600,500,31", &r) && r.x == 34 && r.y == 600 && r.w == 500 && r.h == 31,
           "x,y,w,h parses");
        ok(ut::utRectParse(" 1 , -2 ,3, 4 ", &r) && r.x == 1 && r.y == -2, "blanks and a negative place");
        ok(!ut::utRectParse("1,2,0,4", &r) && !ut::utRectParse("1,2,3,-4", &r), "w and h from 1");
        ok(!ut::utRectParse("1,2,3", &r) && !ut::utRectParse("1,2,3,4,5", &r) &&
               !ut::utRectParse("1,2,3,4x", &r) && !ut::utRectParse("", &r) &&
               !ut::utRectParse("123456,1,1,1", &r),
           "three or five numbers, junk, empty, six digits: refused");
        char buf[32];
        ok(ut::utRectFormat(ut::UtRect{27, 40, 512, 40}, buf, sizeof(buf)) > 0 &&
               !strcmp(buf, "27,40,512,40"),
           "the format round-trips");
        int w = 0, h = 0;
        ok(ut::utCoverKeyParse("565x637", &w, &h) && w == 565 && h == 637, "the cover key 565x637");
        ok(!ut::utCoverKeyParse("565X637", &w, &h) && !ut::utCoverKeyParse("0x637", &w, &h) &&
               !ut::utCoverKeyParse("12345x1", &w, &h) && !ut::utCoverKeyParse("565x", &w, &h) &&
               !ut::utCoverKeyParse("x637", &w, &h) && !ut::utCoverKeyParse("565x637 ", &w, &h),
           "the cover key: capital X, 0, five digits, a missing side, a blank: refused");
        ok(ut::utCoverKeyFormat(565, 637, buf, sizeof(buf)) > 0 && !strcmp(buf, "565x637"),
           "the cover key's text");
    }

    printf("\n2. the pages for N buttons (GD's utPadPaginate over TQ's row 1)\n");
    {
        auto upTo = [](int cap) { return [cap](int n) { return n <= cap; }; };
        const ut::UtPadPages a = ut::utPadPaginate(15, upTo(17));
        ok(a.pages == 1 && a.perPage == 15 && a.buttons == 17, "15 groups that fit: one page");
        const ut::UtPadPages b = ut::utPadPaginate(15, upTo(9));
        ok(b.pages == 3 && b.perPage == 6 && b.buttons == 9, "room for 9: 6 groups a page, 3 pages");
        bool seen[15] = {};
        int twice = 0;
        for (int pg = 0; pg < b.pages; ++pg)
            for (int slot = 0; slot < b.buttons; ++slot) {
                const int l = ut::utPadSlotLogical(b, pg, slot);
                if (l >= 1 && l <= 15) {
                    if (seen[l - 1]) ++twice;
                    seen[l - 1] = true;
                }
            }
        int all = 0;
        for (bool x : seen) all += x ? 1 : 0;
        ok(all == 15 && twice == 0, "every group on exactly one page");
        ok(ut::utPadPageOf(b, 0) == 0 && ut::utPadPageOf(b, 6) == 1 && ut::utPadPageOf(b, 14) == 2,
           "the page of a group");
        ok(ut::utPadNextTarget(b, 0) == 6 && ut::utPadNextTarget(b, 2) == 0, "NEXT: the next page's first group, wrapping");
        const ut::UtPadPages c = ut::utPadPaginate(15, upTo(2));
        ok(c.pages == 1 && c.buttons == 17, "nothing fits: the whole table unpaged (the pad draws nothing)");
        const std::vector<int> st = ut::utPadWheelStops(15, [](int g) { return g; });
        ok(st.size() == 16 && st[0] == -1 && st[15] == 14, "one button per group: the wheel's stops in group order");
        ok(ut::utPadWheelButton(14, 1, 15, [](int g) { return g; }) == -1 &&
               ut::utPadWheelButton(-1, 1, 15, [](int g) { return g; }) == 0,
           "the wheel wraps through the real Transfer page");
    }

    printf("\n3. the pad fitted to a rectangle (two rows; row 2 on its 15-cell grid)\n");
    {
        const ut::UtPadFit f = ut::utPadFitRect(ut::UtRect{27, 40, 512, 40}, 15, 1, 1.0f, 5);
        _snprintf_s(d, sizeof(d), _TRUNCATE, "cols %d, cell %d x %d, step %d, caption %d, pages %d",
                    f.cols, f.cellW, f.cellH, f.stepW, f.caption, f.pages.pages);
        ok(f.ok && f.cols == 15 && f.cellW == 32 && f.cellH == 16 && f.stepW == 32 && f.pages.pages == 1,
           "512 x 40: the 15 groups in one row of 32 px cells", d);
        const ut::UtPadFit p = ut::utPadFitRect(ut::UtRect{27, 40, 260, 40}, 15, 1, 1.0f, 5);
        _snprintf_s(d, sizeof(d), _TRUNCATE, "ok %d, cols %d, %d a page, %d pages, step %d", p.ok,
                    p.cols, p.pages.perPage, p.pages.pages, p.stepW);
        ok(p.ok && p.pages.pages > 1 && p.cols == p.pages.perPage + 1 && p.cellW >= 20,
           "260 wide: paged, K groups and NEXT in row 1", d);
        ok(!ut::utPadFitRect(ut::UtRect{27, 40, 250, 40}, 15, 1, 1.0f, 5).ok,
           "250 wide: row 2's cells under 16 px - refused");
        ok(!ut::utPadFitRect(ut::UtRect{27, 40, 512, 30}, 15, 1, 1.0f, 5).ok,
           "30 high: rows under 12 px - refused");
        // every page drawn: inside the ground, disjoint, every group once, NEXT only while paged,
        // every button's centre hits that button
        int bad = 0, groupsSeen = 0, nexts = 0;
        for (int pg = 0; pg < p.pages.pages; ++pg) {
            ut::UtPadOut o;
            if (!ut::utPadOutFromFit(p, pg, 100.0f, 50.0f, 1.25f, true, false, &o)) { ++bad; continue; }
            for (int i = 0; i < ut::kUtPadMax; ++i) {
                const ut::UtRectF& r = o.btn[i];
                if (!(r.w > 0.0f)) continue;
                if (i >= ut::kUtPadGroup0 && i < ut::kUtPadGroup0 + 15) ++groupsSeen;
                if (i == ut::kUtPadPage) ++nexts;
                if (!ut::utRectInside(r, o.ground, 0.0f)) ++bad;
                const int h = hitOut(o, r.x + r.w * 0.5f, r.y + r.h * 0.5f);
                const bool fieldCentre = i == ut::kUtPadSearch && h == ut::kUtPadClear;
                if (h != i && !fieldCentre) ++bad;
                for (int j = i + 1; j < ut::kUtPadMax; ++j)
                    if (j != ut::kUtPadClear && i != ut::kUtPadSearch && i != ut::kUtPadClear &&
                        j != ut::kUtPadSearch && ut::utRectsMeet(r, o.btn[j]))
                        ++bad;
            }
            if (!ut::utRectInside(o.label, o.ground, 0.0f) || ut::utRectsMeet(o.label, o.btn[ut::kUtPadSearch])) ++bad;
        }
        _snprintf_s(d, sizeof(d), _TRUNCATE, "%d bad, %d group cells over %d pages, %d NEXT", bad,
                    groupsSeen, p.pages.pages, nexts);
        ok(bad == 0 && groupsSeen == 15 && nexts == p.pages.pages,
           "the paged pad at 1.25: inside the ground, disjoint, each group once, NEXT on every page, hits", d);
        ut::UtPadOut o;
        ut::utPadOutFromFit(f, 0, 0.0f, 0.0f, 1.0f, false, false, &o);
        ok(o.btn[ut::kUtPadPage].w == 0.0f && o.btn[ut::kUtPadSearch].w == 0.0f &&
               o.btn[ut::kUtPadToggle].x == 30.0f && o.btn[ut::kUtPadToggle].w == 65.0f,
           "unpaged: no NEXT; search=0: no field; Transfer two cells from the margin");
        const ut::UtRect lb = ut::utPadLabelBox(f, false);
        ok(lb.x == 27 + 3 + 7 * 33 + 3 && lb.y == 40 + 3 + 16 + 1 && lb.h == 16 && lb.x + lb.w == 27 + 512 - 3,
           "the pad's label box: 7 cells + 3 px to the right margin, row 2");
    }

    printf("\n4. the label\n");
    {
        const ut::UtLabelFit a = ut::utLabelFit(ut::UtRect{27, 22, 512, 16}, 13, 1.0f);
        ok(a.ok && a.size == 13 && !a.bounded, "16 high at 1.0: size 13 as set");
        const ut::UtLabelFit b = ut::utLabelFit(ut::UtRect{27, 22, 512, 10}, 13, 1.0f);
        ok(b.ok && b.size == 9 && b.bounded, "10 high: brought down to 9 (the row less 1 px)");
        ok(!ut::utLabelFit(ut::UtRect{27, 22, 512, 6}, 13, 1.0f).ok, "6 high: under size 6 - refused");
        ok(!ut::utLabelFit(ut::UtRect{27, 22, 7, 16}, 13, 1.0f).ok, "7 wide: under 8 px - refused");
    }

    printf("\n5. the resolution against the grid and each other (GD's rules)\n");
    {
        ut::UtRectF f, g;
        const float s = 1.0f;
        page(s, 1366.0f, 768.0f, &f, &g);
        auto run = [&](const char* pad, const char* label) {
            ut::UtLayoutLive L;
            ut::utLayoutFromSource(source(f, g, s, 1366.0f, 768.0f, pad, label), &L);
            return L;
        };
        const ut::UtLayoutLive none = run(nullptr, nullptr);
        const ut::UtRect& gr = none.L.rect[ut::kUtLayGrid];
        _snprintf_s(d, sizeof(d), _TRUNCATE, "grid %d,%d,%d,%d; pad %d,%d,%d,%d; label %d,%d,%d,%d",
                    gr.x, gr.y, gr.w, gr.h, none.L.rect[1].x, none.L.rect[1].y, none.L.rect[1].w,
                    none.L.rect[1].h, none.L.rect[2].x, none.L.rect[2].y, none.L.rect[2].w,
                    none.L.rect[2].h);
        ok(gr.x == 27 && gr.y == 126 && gr.w == 512 && gr.h == 480 && ut::utRectEq(none.L.rect[1], ut::UtRect{33, 606, 500, 31}) && ut::utRectEq(none.L.rect[2], ut::UtRect{270, 622, 260, 12}) && !none.L.user[1] && !none.L.user[2] &&
               none.L.why[1] == ut::kUtLayoutUnset && none.L.labelInPad && !strcmp(none.cover, "565x637"),
           "no key: the grid 27,126,512,480, today's pad 33,606,500,31 and its label 270,622,260,12", d);
        const ut::UtLayoutLive top = run("27,40,512,40", "27,22,512,16");
        ok(top.L.user[1] && top.L.user[2] && top.L.why[1] == ut::kUtLayoutOk && top.L.pad.ok &&
               top.L.label.ok && !top.L.labelInPad,
           "pad_565x637=27,40,512,40 and label_565x637=27,22,512,16 are in force");
        const ut::UtLayoutLive padOnly = run("27,40,512,40", nullptr);
        const ut::UtRect pb = ut::utPadLabelBox(padOnly.L.pad, false);
        ok(padOnly.L.user[1] && padOnly.L.labelInPad && ut::utRectEq(padOnly.L.rect[2], pb),
           "a moved pad takes its label box along");
        const ut::UtLayoutLive out = run("-5,40,512,40", nullptr);
        ok(!out.L.user[1] && out.L.why[1] == ut::kUtLayoutOutside && ut::utRectEq(out.L.rect[1], none.L.rect[1]),
           "a pad leaving the caravan window: refused whole, today's pad drawn");
        const ut::UtLayoutLive big = run("27,40,512,600", nullptr);
        ok(big.L.why[1] == ut::kUtLayoutOutside, "a pad past the window's bottom: refused (outside)");
        const ut::UtLayoutLive grid = run("27,500,512,40", nullptr);
        ok(grid.L.why[1] == ut::kUtLayoutOverlap && grid.L.against[1] == ut::kUtLayGrid,
           "a pad over the grid: refused, against the grid");
        const ut::UtLayoutLive small = run("27,40,150,40", nullptr);
        ok(small.L.why[1] == ut::kUtLayoutPadNoFit, "a pad with no room for readable buttons: refused");
        const ut::UtLayoutLive lbDef = run(nullptr, "40,610,300,16");
        ok(lbDef.L.why[2] == ut::kUtLayoutOverlap && lbDef.L.against[2] == ut::kUtLayPad,
           "a label over today's pad: refused, against the pad");
        const ut::UtLayoutLive both = run("27,40,512,40", "40,50,300,16");
        ok(both.L.user[1] && both.L.why[2] == ut::kUtLayoutOverlap && both.L.against[2] == ut::kUtLayPad,
           "a label over the player's pad: the label (the later part) falls back");
        const ut::UtLayoutLive lbSmall = run(nullptr, "27,22,512,5");
        ok(lbSmall.L.why[2] == ut::kUtLayoutLabelSmall, "a label too low for size 6: refused");
        const ut::UtLayoutLive junk = run("27,40,512", nullptr);
        ok(junk.in.user[1] == nullptr && junk.L.why[1] == ut::kUtLayoutUnset, "a value that does not parse is no key");
    }

    printf("\n6. the layout from the measured page, over the scale sweep\n");
    {
        const float scales[] = {0.7f, 0.8f, 1.0f, 1.0625f, 1.15625f, 1.25f, 1.375f, 1.5f, 1.6875f, 2.0f};
        const float canv[3][2] = {{1366.0f, 768.0f}, {1920.0f, 1080.0f}, {3440.0f, 1440.0f}};
        int combos = 0, bad = 0, userOk = 0;
        char first[160] = "";
        for (float s : scales)
            for (const auto& cv : canv) {
                if (565.0f * s > cv[0] || 637.0f * s > cv[1]) continue;
                ut::UtRectF f, g;
                if (!page(s, cv[0], cv[1], &f, &g)) continue;
                ++combos;
                ut::UtLayoutLive L;
                ut::utLayoutFromSource(source(f, g, s, cv[0], cv[1], nullptr, nullptr), &L);
                // the derived pad = utPadLayout with the derived numbers, byte for byte
                ut::UtPadIn p;
                p.frame = f;
                p.grid = g;
                p.scale = s;
                p.canvasW = cv[0];
                p.canvasH = cv[1];
                p.dx = 0;
                p.dy = 4;
                p.cellH = 14;
                p.gap = 1;
                p.search = true;
                ut::UtPadOut o;
                const int band = ut::utPadLayout(p, &o);
                bool good = band == L.defBand && band == ut::kUtPadBelow &&
                            !memcmp(&o, &L.defOut, sizeof(o)) && !L.L.user[1] && !L.L.user[2];
                // its record rect: inside the window, clear of the grid's record rect
                good = good && ut::utRectInside(L.L.rect[1], L.in.plate) &&
                       !ut::utRectsMeet(L.L.rect[1], L.L.rect[0]);
                // the player's pad above the tab row: in force, drawn inside the frame and clear of
                // the grid
                ut::UtLayoutLive U;
                ut::utLayoutFromSource(source(f, g, s, cv[0], cv[1], "27,40,512,40", "27,22,512,16"), &U);
                if (U.L.user[1]) {
                    ++userOk;
                    ut::UtPadOut uo;
                    ut::utPadOutFromFit(U.L.pad, 0, ut::utPadRound(f.x), ut::utPadRound(f.y), s, true, false, &uo);
                    good = good && ut::utRectInside(uo.ground, f, ut::kUtFrameTol) && !ut::utRectsMeet(uo.ground, g);
                }
                if (!good && !first[0])
                    _snprintf_s(first, sizeof(first), _TRUNCATE,
                                "scale %.4f on %.0f (band %d/%d, same %d, pad %d,%d,%d,%d in plate %d, grid met %d)",
                                s, cv[0], band, L.defBand, memcmp(&o, &L.defOut, sizeof(o)) == 0,
                                L.L.rect[1].x, L.L.rect[1].y, L.L.rect[1].w, L.L.rect[1].h,
                                ut::utRectInside(L.L.rect[1], L.in.plate), ut::utRectsMeet(L.L.rect[1], L.L.rect[0]));
                if (!good) ++bad;
            }
        _snprintf_s(d, sizeof(d), _TRUNCATE, "%d combinations, %d bad%s%s, the player's pad in force in %d",
                    combos, bad, first[0] ? " - first " : "", first, userOk);
        ok(combos >= 20 && bad == 0 && userOk == combos,
           "no key = today's pad exactly; a typed pad is inside the frame and clear of the grid", d);
    }

    printf("\n7. the derived numbers typed back (the outline's and the README's)\n");
    {
        const float scales[] = {0.7f, 0.8f, 1.0f, 1.0625f, 1.15625f, 1.25f, 1.375f, 1.5f, 1.6875f, 2.0f};
        const float canv[3][2] = {{1366.0f, 768.0f}, {1920.0f, 1080.0f}, {3440.0f, 1440.0f}};
        int combos = 0, bad = 0, readmeBad = 0;
        char first[200] = "", firstR[200] = "";
        for (float s : scales)
            for (const auto& cv : canv) {
                if (565.0f * s > cv[0] || 637.0f * s > cv[1]) continue;
                ut::UtRectF f, g;
                if (!page(s, cv[0], cv[1], &f, &g)) continue;
                ++combos;
                ut::UtLayoutLive D;
                ut::utLayoutFromSource(source(f, g, s, cv[0], cv[1], nullptr, nullptr), &D);
                const ut::UtRect dp = D.L.rect[1], dl = D.L.rect[2];
                char pv[48], lv[48];
                _snprintf_s(pv, sizeof(pv), _TRUNCATE, "%d,%d,%d,%d", dp.x, dp.y, dp.w, dp.h);
                _snprintf_s(lv, sizeof(lv), _TRUNCATE, "%d,%d,%d,%d", dl.x, dl.y, dl.w, dl.h);
                ut::UtLayoutLive U;
                ut::utLayoutFromSource(source(f, g, s, cv[0], cv[1], pv, lv), &U);
                const bool good = U.L.user[1] && U.L.padDerived && U.L.why[1] == ut::kUtLayoutOk &&
                                  ut::utRectEq(U.L.rect[1], dp) &&
                                  !memcmp(&U.L.pad, &D.L.pad, sizeof(U.L.pad)) && U.L.user[2] &&
                                  U.L.why[2] == ut::kUtLayoutOk && ut::utRectEq(U.L.rect[2], dl) &&
                                  U.L.label.ok;
                if (!good) {
                    ++bad;
                    if (!first[0])
                        _snprintf_s(first, sizeof(first), _TRUNCATE,
                                    "scale %.4f on %.0f: pad %s why %d derived %d, label %s why %d against %d",
                                    s, cv[0], pv, U.L.why[1], U.L.padDerived, lv, U.L.why[2], U.L.against[2]);
                }
                ut::UtLayoutLive R;
                ut::utLayoutFromSource(source(f, g, s, cv[0], cv[1], "33,606,500,31", "270,622,260,12"), &R);
                // at UI scale 1 and above they are the derived pad and its label; below 1 the
                // engine's cell makes the grid and the band below it other record numbers, and these
                // are the player's (in force) or refused with the derived ones drawn
                const bool rOk =
                    s >= 1.0f ? R.L.user[1] && R.L.padDerived && ut::utRectEq(R.L.rect[1], dp) &&
                                    R.L.user[2] && R.L.why[2] == ut::kUtLayoutOk &&
                                    ut::utRectInside(R.L.rect[2], dl)
                              : (R.L.user[1] || ut::utRectEq(R.L.rect[1], dp)) &&
                                    (R.L.user[2] || ut::utRectEq(R.L.rect[2], dl));
                if (!rOk) {
                    ++readmeBad;
                    if (!firstR[0])
                        _snprintf_s(firstR, sizeof(firstR), _TRUNCATE,
                                    "scale %.4f on %.0f: derived pad %s label %s; why %d/%d derived %d against %d",
                                    s, cv[0], pv, lv, R.L.why[1], R.L.why[2], R.L.padDerived, R.L.against[2]);
                }
            }
        _snprintf_s(d, sizeof(d), _TRUNCATE, "%d combinations, %d bad%s%s", combos, bad,
                    first[0] ? " - first " : "", first);
        ok(combos >= 20 && bad == 0, "the derived pad and label typed back: in force, drawn as the derived ones", d);
        _snprintf_s(d, sizeof(d), _TRUNCATE, "%d combinations, %d bad%s%s", combos, readmeBad,
                    firstR[0] ? " - first " : "", firstR);
        ok(combos >= 20 && readmeBad == 0,
           "the README's 33,606,500,31 and 270,622,260,12: the derived pad and its label from UI scale 1, "
           "in force or refused below", d);

        ut::UtRectF f, g;
        page(1.0f, 1366.0f, 768.0f, &f, &g);
        ut::UtLayoutLive X;
        ut::utLayoutFromSource(source(f, g, 1.0f, 1366.0f, 768.0f, "27,40,512,40", nullptr), &X);
        const ut::UtRect box = X.L.rect[2];
        char in[48], out[48];
        _snprintf_s(in, sizeof(in), _TRUNCATE, "%d,%d,%d,%d", box.x + 4, box.y, box.w - 8, box.h);
        _snprintf_s(out, sizeof(out), _TRUNCATE, "%d,%d,%d,%d", box.x - 3, box.y, box.w, box.h);
        ut::UtLayoutLive Y, Z;
        ut::utLayoutFromSource(source(f, g, 1.0f, 1366.0f, 768.0f, "27,40,512,40", in), &Y);
        ut::utLayoutFromSource(source(f, g, 1.0f, 1366.0f, 768.0f, "27,40,512,40", out), &Z);
        ok(X.L.user[1] && !X.L.padDerived && Y.L.user[2] && Y.L.why[2] == ut::kUtLayoutOk &&
               Y.L.rect[2].x == box.x + 4 && Y.L.rect[2].w == box.w - 8,
           "a label inside a moved pad's label box: in force as typed", in);
        ok(!Z.L.user[2] && Z.L.why[2] == ut::kUtLayoutOverlap && Z.L.against[2] == 1,
           "a label 3 px out of that box: refused against the pad", out);
    }

    printf("\n8. the layout's log lines (GD's words)\n");
    {
        ut::UtRectF f, g;
        page(1.0f, 1366.0f, 768.0f, &f, &g);
        char w[320], s1[320];
        ut::UtLayoutLive R;
        ut::utLayoutFromSource(source(f, g, 1.0f, 1366.0f, 768.0f, "27,300,512,40", nullptr), &R);
        const bool refused = ut::utLayoutRefusalText(R, 1, "27,300,512,40", 1.0f, w, sizeof(w));
        ok(refused && !strcmp(w, "layout: pad_565x637=27,300,512,40 is refused - it overlaps another part "
                                 "of the page (the grid) at UI scale 1.000; the derived pad is used"),
           "a pad over the grid: the WARN", w);
        ok(!ut::utLayoutRefusalText(R, 2, nullptr, 1.0f, w, sizeof(w)) && !w[0], "no label line: no WARN");
        ut::utLayoutSaidText(R, 1.0f, s1, sizeof(s1));
        ok(!strncmp(s1, "layout: 565x637 at UI scale 1.000 - grid ", 41) && strstr(s1, "(derived), pad ") &&
               strstr(s1, " (derived) 15 x 2 cells of ") && strstr(s1, "(derived) size ") &&
               !strstr(s1, "NOT DRAWN") && !strstr(s1, "pages of"),
           "the derived layout: the INFO line", s1);
        ut::UtLayoutLive P;
        ut::utLayoutFromSource(source(f, g, 1.0f, 1366.0f, 768.0f, "27,40,265,40", nullptr), &P);
        ut::utLayoutSaidText(P, 1.0f, s1, sizeof(s1));
        ok(P.L.user[1] && P.L.pad.pages.pages > 1 && strstr(s1, " (user) ") && strstr(s1, "(the pad's) size ") &&
               strstr(s1, "; 15 buttons do not fit the pad - 2 pages of "),
           "a paged pad of the player's: the INFO line with the pages", s1);
    }

    // ---- 9. the pad keyed by buttons: the derived row 1 cut per button, its pages, the wheel ----
    printf("9. the derived pad keyed by buttons (row 1 = one cell per button; row 2 kept)\n");
    {
        const float scales[] = {0.7f, 0.8f, 1.0f, 1.25f, 1.5f, 2.0f};
        int sweeps = 0, bad15 = 0, badRow2 = 0, bad3 = 0, bad40 = 0, pages40 = 0, per40 = 0, cells40 = 0;
        char d9[200];
        for (float s : scales) {
            ut::UtRectF f, g;
            if (!page(s, 2560.0f, 1440.0f, &f, &g)) continue;
            ++sweeps;
            ut::UtLayoutSource src = source(f, g, s, 2560.0f, 1440.0f, nullptr, nullptr);
            ut::UtLayoutLive lay15;
            ut::utLayoutFromSource(src, &lay15);
            ut::UtPadOut c15 = lay15.defOut;
            const ut::UtPadFit d15 = ut::utPadDerivedFit(lay15.defOut, lay15.in.def[1], 15, 5, s);
            ut::utPadDerivedCut(d15, 0, s, &c15);
            if (memcmp(c15.btn, lay15.defOut.btn, sizeof(c15.btn)) != 0 || d15.pages.pages != 1 ||
                d15.cellW != lay15.defOut.cellW || lay15.L.pad.cols != 15 || lay15.L.pad.pages.pages != 1)
                ++bad15;
            for (int n : {3, 40}) {
                src.groups = n;
                ut::UtLayoutLive Ln;
                ut::utLayoutFromSource(src, &Ln);
                const ut::UtPadFit& pf = Ln.L.pad;
                int& bad = n == 3 ? bad3 : bad40;
                if (!pf.ok || memcmp(&Ln.defOut, &lay15.defOut, sizeof(Ln.defOut)) != 0) ++bad;
                std::vector<int> seen((size_t)n, 0);
                for (int pg = 0; pg < pf.pages.pages; ++pg) {
                    ut::UtPadOut o = Ln.defOut;
                    ut::utPadDerivedCut(pf, pg, s, &o);
                    const int keep[6] = {ut::kUtPadToggle, ut::kUtPadOwn, ut::kUtPadPrev, ut::kUtPadNext,
                                         ut::kUtPadSearch, ut::kUtPadClear};
                    bool same = memcmp(&o.ground, &lay15.defOut.ground, sizeof(o.ground)) == 0 &&
                                memcmp(&o.label, &lay15.defOut.label, sizeof(o.label)) == 0 &&
                                memcmp(&o.fieldText, &lay15.defOut.fieldText, sizeof(o.fieldText)) == 0;
                    for (int k : keep) same = same && memcmp(&o.btn[k], &lay15.defOut.btn[k], sizeof(o.btn[k])) == 0;
                    if (!same) ++badRow2;
                    const ut::UtRectF& a = lay15.defOut.btn[ut::kUtPadGroup0];
                    const ut::UtRectF& z = lay15.defOut.btn[ut::kUtPadGroup0 + 14];
                    for (int b = 0; b < ut::kUtPadCells; ++b) {
                        const ut::UtRectF& r = o.btn[ut::kUtPadGroup0 + b];
                        if (!(r.w > 0.0f)) continue;
                        if (b >= n || r.x < a.x || r.x + r.w > z.x + z.w || r.y != a.y || r.h != a.h ||
                            ut::utPadPageOf(pf.pages, b) != pg || hitOut(o, r.x + r.w * 0.5f, r.y + r.h * 0.5f) !=
                                                                     ut::kUtPadGroup0 + b)
                            ++bad;
                        else ++seen[(size_t)b];
                        if (n == 40 && s == 1.0f) ++cells40;
                    }
                    const ut::UtRectF& nx = o.btn[ut::kUtPadPage];
                    if ((nx.w > 0.0f) != (pf.pages.pages > 1) ||
                        (nx.w > 0.0f && (nx.x + nx.w > z.x + z.w || hitOut(o, nx.x + 1.0f, nx.y + 1.0f) != ut::kUtPadPage)))
                        ++bad;
                }
                for (int v : seen) if (v != 1) ++bad;   // the pages show every button exactly once
                if (n == 3 && (pf.pages.pages != 1 || pf.cols != 3)) ++bad3;
                if (n == 40 && s == 1.0f) {
                    pages40 = pf.pages.pages;
                    per40 = pf.pages.perPage;
                }
            }
        }
        ok(sweeps >= 5 && bad15 == 0, "15 buttons: the cut is utPadLayout's 15 cells byte for byte at every scale");
        ok(badRow2 == 0, "3 and 40 buttons: the ground, Transfer, OWN, < >, the field, its x and the label untouched");
        ok(bad3 == 0, "3 buttons: 3 equal cells in row 1's span, one page, every centre hits its button");
        _snprintf_s(d9, sizeof(d9), _TRUNCATE, "%d pages of %d at UI scale 1 (%d cells drawn)", pages40, per40, cells40);
        ok(bad40 == 0 && pages40 == 2 && per40 == 22 && cells40 == 40,
           "40 buttons at the derived pad: paged with NEXT, each button on exactly one page", d9);
        char s1[320];
        ut::UtRectF f, g;
        page(1.0f, 1366.0f, 768.0f, &f, &g);
        ut::UtLayoutSource src = source(f, g, 1.0f, 1366.0f, 768.0f, nullptr, nullptr);
        src.groups = 40;
        ut::UtLayoutLive lay40;
        ut::utLayoutFromSource(src, &lay40);
        ut::utLayoutSaidText(lay40, 1.0f, s1, sizeof(s1));
        ok(strstr(s1, " (derived) 23 x 2 cells of 20 x 12 ") && strstr(s1, "; 40 buttons do not fit the pad - 2 pages of 22 "),
           "40 buttons: the layout INFO line names the pages", s1);
        src.groups = 20;
        ut::UtLayoutLive lay20;
        ut::utLayoutFromSource(src, &lay20);
        ut::utLayoutSaidText(lay20, 1.0f, s1, sizeof(s1));
        ok(lay20.L.pad.pages.pages == 1 && lay20.L.pad.cols == 20 && lay20.L.pad.cellW == 23 && !strstr(s1, "pages of"),
           "20 buttons fit the derived pad unpaged (cells of 23 record px)", s1);
        src.groups = 13;   // a file with one union and one group left off, in a narrow pad of the player's
        src.pad = "27,40,265,40";
        ut::UtLayoutLive narrow13;
        ut::utLayoutFromSource(src, &narrow13);
        ut::utLayoutSaidText(narrow13, 1.0f, s1, sizeof(s1));
        ok(narrow13.L.user[1] && narrow13.L.pad.ok && narrow13.L.pad.pages.pages == 2 && narrow13.L.pad.pages.perPage == 11 &&
               strstr(s1, "; 13 buttons do not fit the pad - 2 pages of 11 with a NEXT button"),
           "13 buttons in pad_565x637=27,40,265,40: 2 pages of 11 and NEXT", s1);
        src.pad = "27,40,512,40";
        ut::UtLayoutLive wide13;
        ut::utLayoutFromSource(src, &wide13);
        ok(wide13.L.user[1] && wide13.L.pad.ok && wide13.L.pad.pages.pages == 1 && wide13.L.pad.cols == 13,
           "13 buttons in pad_565x637=27,40,512,40: one row");
        src.pad = "300,606,400,31";
        ut::UtLayoutLive out13;
        ut::utLayoutFromSource(src, &out13);
        ok(!out13.L.user[1] || out13.L.why[1] == ut::kUtLayoutOutside, "a pad past the window's right edge is refused (outside)");
        src.groups = 15;
        src.pad = "33,606,280,31";
        ut::UtLayoutLive N;
        ut::utLayoutFromSource(src, &N);
        ut::utLayoutSaidText(N, 1.0f, s1, sizeof(s1));
        ok(N.L.user[1] && N.L.pad.ok && N.L.pad.pages.pages > 1, "pad_565x637=33,606,280,31: the 15 buttons paged", s1);
    }
    {   // the wheel: Transfer, then the buttons in pad order (ut_padpage.h utPadWheelButton)
        int bad = 0;
        for (int from = -1; from < 15; ++from)
            for (int dir = -1; dir <= 1; dir += 2)
                if (ut::utPadWheelButton(from, dir, 15, [](int x) { return x; }) != ut::utGroupCycleStep(from, 15, dir))
                    ++bad;
        ok(bad == 0, "one button per group: Ctrl+wheel = 1.0.1's group cycle, Transfer a stop");
        // [B0] groups 2; [B1] groups 0 and 1 (one page); [B2] group 4; group 3 on no button
        auto bo = [](int x) {
            static const int m[5] = {1, 1, 0, -1, 2};
            return x >= 0 && x < 5 ? m[x] : -1;
        };
        std::vector<int> walk;
        int cur = -1;
        for (int i = 0; i < 5; ++i) walk.push_back(cur = ut::utPadWheelButton(cur, +1, 5, bo));
        ok(walk == std::vector<int>({2, 0, 4, -1, 2}),
           "a file's order: Transfer, [B0], the union [B1] (its first group), [B2], Transfer");
        ok(ut::utPadWheelButton(1, +1, 5, bo) == 4 && ut::utPadWheelButton(3, +1, 5, bo) == 2 &&
               ut::utPadWheelButton(3, -1, 5, bo) == 4 && ut::utPadWheelButton(-1, -1, 5, bo) == 4,
           "a union's other member steps from its button; a group on no button steps from Transfer");
        // a union's slot: the largest member's (TQ's slots: 2x2 helms, 2x3 torso, 1x1 rings ...)
        const std::vector<ut::UtCell> view = {{2, 2}, {2, 3}, {2, 4}, {1, 1}, {2, 1}};
        int exact = 0;
        const ut::UtCell c = ut::utUnionCell({{2, 2}, {2, 3}}, view, &exact);
        int exact2 = 0;
        const ut::UtCell c2 = ut::utUnionCell({{1, 1}, {2, 1}}, view, &exact2);
        ok(c.w == 2 && c.h == 3 && exact == 1 && c2.w == 2 && c2.h == 1 && exact2 == 1,
           "a union's slot is its largest member's (2x2 + 2x3 = 2x3; 1x1 + 2x1 = 2x1)");
    }

    {   // a long caption is fitted on its own button; the pad keeps the short names' size
        const int fs = ut::utPadCaptionSize(32.0f, 12.0f, ut::utPadFitChars(10), 1.0f);
        char buf[64];
        const char* out = nullptr;
        const int a = ut::utPadCellCaption("Helm", 32.0f, 12.0f, 1.0f, fs, buf, sizeof(buf), &out);
        const bool aOk = a == fs && out && strcmp(out, "Helm") == 0;
        const int b = ut::utPadCellCaption("Abcdefgh", 32.0f, 12.0f, 1.0f, fs, buf, sizeof(buf), &out);
        const bool bOk = b == 6 && out && strcmp(out, "Abcdefgh") == 0;
        const int c = ut::utPadCellCaption("Abcdefghijkl", 32.0f, 12.0f, 1.0f, fs, buf, sizeof(buf), &out);
        const bool cOk = c == 6 && out && strcmp(out, "Abcdefghi") == 0;
        const int dz = ut::utPadCellCaption("Abcdefghijkl", 2.0f, 12.0f, 1.0f, fs, buf, sizeof(buf), &out);
        ok(fs == 9 && aOk && bOk && cOk && dz == 0,
           "a long caption: smaller (8 chars at 6) or cut (12 chars -> 9) on its own button; the pad keeps 9");
    }

    {   // the caption ceiling in GD's order: size.caption_max (6..36) wins, else pad_caption_max clamped
        ok(ut::utPadCaptionCeiling(20, 14) == 20 && ut::utPadCaptionCeiling(40, 14) == 36 &&
               ut::utPadCaptionCeiling(6, 30) == 6 && ut::utPadCaptionCeiling(-1, 20) == 20 &&
               ut::utPadCaptionCeiling(5, 20) == 20 && ut::utPadCaptionCeiling(0, 14) == 14 &&
               ut::utPadCaptionCeiling(-1, 3) == 6 && ut::utPadCaptionCeiling(-1, 99) == 36,
           "utPadCaptionCeiling: size.caption_max >= 6 wins (36 at most), else pad_caption_max clamped 6..36");
        // a seven-letter caption on a wide, tall cell: the ceiling in force reaches it
        char buf[64];
        const char* out = nullptr;
        const int at20 = ut::utPadCellCaption("Abcdefg", 200.0f, 40.0f, 1.0f, 20, buf, sizeof(buf), &out, -1, 20);
        const int at14 = ut::utPadCellCaption("Abcdefg", 200.0f, 40.0f, 1.0f, 20, buf, sizeof(buf), &out);
        const int btn = ut::utPadButtonCaption("Abcdefg", 200.0f, 40.0f, 1.0f, 20, true, -1, 0, buf, sizeof(buf),
                                               &out, -1, ut::utPadCaptionCeiling(-1, 20));
        ok(at20 == 20 && at14 == 14 && btn == 20,
           "pad_caption_max=20: a caption longer than five letters is fitted up to 20 on a cell that holds it");
    }

    {   // the buttons file's [tab] sizes on the captions: size.caption > button size > size.caption_max > 14
        ok(ut::utPadCaptionTop(40.0f, 1.0f) == 14 && ut::utPadCaptionTop(40.0f, 1.0f, ut::kUtPadCaptionMax) == 14 &&
               ut::utPadCaptionTop(40.0f, 1.0f, 9) == 9 && ut::utPadCaptionTop(40.0f, 2.0f, 9) == 18 &&
               ut::utPadCaptionTop(12.0f, 1.0f, 20) == 9,
           "size.caption_max replaces the ceiling of 14 (record px at the scale), the height still rules");
        int same = 0, total = 0;
        const float scales[4] = {0.7f, 1.0f, 1.25f, 2.0f};
        for (float s : scales)
            for (int h = 9; h <= 40; h += 3)
                for (int w = 16; w <= 80; w += 8) {
                    ++total;
                    const int top = (int)((float)h * 3.0f / 4.0f) > h - 2 ? h - 2 : (int)((float)h * 3.0f / 4.0f);
                    const int cap = (int)ut::utPadRound(14.0f * s);
                    const int old = top > cap ? cap : top;
                    if (ut::utPadCaptionTop((float)h, s) == old &&
                        ut::utPadCaptionSizeOf((float)w, (float)h, "Staff", s) ==
                            ut::utPadCaptionSizeOf((float)w, (float)h, "Staff", s, -1, ut::kUtPadCaptionMax))
                        ++same;
                }
        ok(same == total, "unset: the caption rule of before at every size and scale (today's look)");
        ok(ut::utPadCaptionFixed(40.0f, 1.0f, 20) == 20 && ut::utPadCaptionFixed(16.0f, 1.0f, 20) == 12 &&
               ut::utPadCaptionFixed(40.0f, 1.25f, 10) == 13 && ut::utPadCaptionFixed(7.0f, 1.0f, 20) == 0,
           "size.caption: one size at the scale, bounded by the cell's height (0 = not even 6)");
        const int fs = ut::utPadCaptionSize(32.0f, 12.0f, ut::utPadFitChars(10), 1.0f);
        char buf[64], cut[64];
        const char* out = nullptr;
        const char* ref = nullptr;
        bool sameCell = true;
        const char* caps[4] = {"Helm", "Abcdefgh", "Abcdefghijkl", "X"};
        for (const char* cp : caps) {
            const int a = ut::utPadCellCaption(cp, 32.0f, 12.0f, 1.0f, fs, buf, sizeof(buf), &ref);
            const int b = ut::utPadButtonCaption(cp, 32.0f, 12.0f, 1.0f, fs, true, -1, 0, cut, sizeof(cut), &out);
            if (a != b || !out || !ref || strcmp(out, ref) != 0) sameCell = false;
        }
        ok(sameCell, "no [tab] size and no button size: a group caption is fitted as before");
        ok(ut::utPadButtonCaption("Helm", 32.0f, 12.0f, 1.0f, fs, true, 7, 0, cut, sizeof(cut), &out) == 7 &&
               ut::utPadButtonCaption("Helm", 32.0f, 12.0f, 1.0f, fs, true, 12, 0, cut, sizeof(cut), &out) ==
                   ut::utPadCaptionSizeOf(32.0f, 12.0f, "Helm", 1.0f, -1, 12) &&
               out && !strcmp(out, "Helm"),
           "a button's own size is its caption's ceiling, fitted on its cell");
        ok(ut::utPadButtonCaption("Helm", 64.0f, 40.0f, 1.0f, 8, true, 20, 0, cut, sizeof(cut), &out) == 20 &&
               ut::utPadButtonCaption("Helm", 64.0f, 40.0f, 1.5f, 8, true, 20, 0, cut, sizeof(cut), &out) ==
                   ut::utPadCaptionSizeOf(64.0f, 40.0f, "Helm", 1.5f, -1, 20),
           "a button's own size draws its caption above the pad's size too, at the scale");
        ok(ut::utPadButtonCaption("Helm", 32.0f, 40.0f, 1.0f, 20, true, 9, 20, cut, sizeof(cut), &out) == 9 &&
               out && !strcmp(out, "Helm"),
           "a button's own size wins over size.caption");
        ok(ut::utPadButtonCaption("Abcdefghijkl", 20.0f, 12.0f, 1.0f, fs, true, 7, 0, cut, sizeof(cut), &out) ==
               ut::utPadCellCaption("Abcdefghijkl", 20.0f, 12.0f, 1.0f, fs, buf, sizeof(buf), &ref) &&
               ut::utPadCaptionSizeOf(20.0f, 12.0f, "Abcdefghijkl", 1.0f, -1, 7) < 6,
           "a button's own size that does not hold its caption: the pad's rule (smaller, else cut)");
        ok(ut::utPadButtonCaption("OWN", 32.0f, 12.0f, 1.0f, fs, false, -1, 0, cut, sizeof(cut), &out) == fs &&
               out && !strcmp(out, "OWN"),
           "Transfer, OWN, <, >, NEXT: drawn at the pad's size as before");
        const int big = ut::utPadCaptionFixed(40.0f, 1.0f, 20);
        const int fx = ut::utPadButtonCaption("Abcdefghijkl", 32.0f, 40.0f, 1.0f, big, true, -1, 20, cut, sizeof(cut), &out);
        ok(fx == 20 && out == cut && ut::utTmCodePoints(out) >= 1 && !(ut::utPadTextWidth(out, 20) > 30.0f),
           "size.caption: every caption at its one size, cut to its button");
        const int fo = ut::utPadButtonCaption("OWN", 20.0f, 40.0f, 1.0f, big, false, -1, 20, cut, sizeof(cut), &out);
        ok(fo == 20 && out && !(ut::utPadTextWidth(out, 20) > 18.0f), "  OWN too");
        ok(ut::utPadButtonCaption("Helm", 32.0f, 12.0f, 1.0f, 5, true, -1, 0, cut, sizeof(cut), &out) == 0,
           "a pad size under 6: unlabelled");
        // next.caption through the fit, NEXT's width from it
        ok(ut::utPadNextCaption(">>", 32.0f, 9, buf, sizeof(buf)) != buf &&
               !strcmp(ut::utPadNextCaption(">>", 32.0f, 9, buf, sizeof(buf)), ">>"),
           "NEXT's default \">>\": whole, as before");
        const char* nc = ut::utPadNextCaption("Next page", 32.0f, 9, buf, sizeof(buf));
        ok(nc == buf && ut::utTmCodePoints(nc) >= 1 && !(ut::utPadTextWidth(nc, 9) > 30.0f),
           "a long next.caption: cut to NEXT's cell like mat.caption");
        const ut::UtRect pr = ut::UtRect{27, 40, 260, 40};
        const ut::UtPadFit n0 = ut::utPadFitRect(pr, 15, 1, 1.0f, 1);
        const ut::UtPadFit n2 = ut::utPadFitRect(pr, 15, 1, 1.0f, 1, 2);
        const ut::UtPadFit nz = ut::utPadFitRect(pr, 15, 1, 1.0f, 1, 0);
        const ut::UtPadFit n5 = ut::utPadFitRect(pr, 15, 1, 1.0f, 1, 5);
        ok(n0.ok && memcmp(&n0, &n2, sizeof(n0)) == 0 && memcmp(&n0, &nz, sizeof(n0)) == 0 &&
               n5.ok && n5.pages.pages > 1 && n5.caption <= n0.caption,
           "NEXT's width from next.caption: \">>\" (2) as before, a longer one fits the paged pad to it");
    }

    {   // the grid part: GD's utGridFit, the box cap, the canonical text
        const ut::UtGrid a = ut::utLayoutGridCells(ut::UtRect{27, 126, 512, 480}, 323);
        const ut::UtGrid b = ut::utLayoutGridCells(ut::UtRect{0, 0, 565, 637}, 323);
        const ut::UtGrid c = ut::utLayoutGridCells(ut::UtRect{0, 0, 565, 637}, 240);
        const ut::UtGrid dg = ut::utLayoutGridCells(ut::UtRect{40, 20, 335, 330}, 323);
        const ut::UtGrid e = ut::utGridFit(ut::UtRect{0, 0, 10, 10}, 32, 32, 0, 100);
        ok(a.cols == 16 && a.rows == 15 && !a.capped && b.cols == 17 && b.rows == 19 && !b.capped &&
               c.cols == 17 && c.rows == 14 && c.capped && dg.cols == 10 && dg.rows == 10 &&
               e.cols == 1 && e.rows == 1,
           "the grid's cells: 512x480 = 16 x 15, the window 17 x 19, capped at 240 the rows walk back "
           "(17 x 14), the slack stays empty (335x330 = 10 x 10), never under 1 x 1");
        ok(ut::utLayoutBoxCap(565, 637) == 323 && ut::utLayoutBoxCap(100, 100) == 240 &&
               ut::utLayoutBoxCap(2000, 2000) == 512,
           "the box cap: the vanilla window's 323, never under the 16 x 15 sack's 240, never over 512");
        char cb[64];
        const int n1 = ut::utLayoutCanonical(" 27, 126,512 ,+480", cb, sizeof(cb));
        const bool c1 = n1 > 0 && !strcmp(cb, "27,126,512,480");
        const int n2 = ut::utLayoutCanonical("27,126,512", cb, sizeof(cb));
        ok(c1 && n2 == 0 && !cb[0] && ut::utLayoutCanonical(nullptr, cb, sizeof(cb)) == 0,
           "a grid override's canonical text: blanks and + do not change it; a bad value has none");
    }

    {   // the grid part: GD's rules in utLayoutResolve (the vanilla page at UI scale 1)
        ut::UtRectF f, g;
        page(1.0f, 1366.0f, 768.0f, &f, &g);
        char w[320], s1[320];
        auto with = [&](const char* grid, const char* pad, int mw, int mh) {
            ut::UtLayoutSource src = source(f, g, 1.0f, 1366.0f, 768.0f, pad, nullptr);
            src.gridKey = grid;
            src.maxCellW = mw;
            src.maxCellH = mh;
            ut::UtLayoutLive R;
            ut::utLayoutFromSource(src, &R);
            return R;
        };
        const ut::UtLayoutLive U = with("40,20,335,330", nullptr, 64, 128);
        ok(U.L.user[0] == 1 && U.L.why[0] == ut::kUtLayoutOk && ut::utRectEq(U.L.rect[0], ut::UtRect{40, 20, 335, 330}) &&
               U.L.user[1] == 0 && U.L.why[1] == ut::kUtLayoutUnset,
           "a grid of the player's inside the window, off the derived pad: in force");
        ut::utLayoutSaidText(U, 1.0f, s1, sizeof(s1));
        ok(strstr(s1, "- grid 40,20,335,330 (user), pad ") != nullptr, "... the INFO line says (user)", s1);
        const ut::UtLayoutLive O = with("400,400,320,320", nullptr, 64, 128);
        ok(O.L.user[0] == 0 && O.L.why[0] == ut::kUtLayoutOutside && ut::utRectEq(O.L.rect[0], O.in.def[0]),
           "a grid that leaves the caravan window: refused whole, the derived grid in force");
        const ut::UtLayoutLive S = with("10,10,60,200", nullptr, 64, 128);
        const ut::UtLayoutLive S2 = with("10,10,64,127", nullptr, 64, 128);
        const ut::UtLayoutLive S3 = with("10,10,64,128", nullptr, 64, 128);
        ok(S.L.why[0] == ut::kUtLayoutGridSmall && S2.L.why[0] == ut::kUtLayoutGridSmall &&
               S3.L.why[0] == ut::kUtLayoutOk,
           "GD's minimum: a grid narrower or lower than the largest slot (2 x 4 cells = 64 x 128) is refused");
        const ut::UtLayoutLive M = with("27,200,512,420", nullptr, 64, 128);
        const bool mw = ut::utLayoutRefusalText(M, 0, "27,200,512,420", 1.0f, w, sizeof(w));
        ok(M.L.why[0] == ut::kUtLayoutOverlap && M.L.against[0] == 1 && M.L.user[0] == 0 && mw &&
               !strcmp(w, "layout: grid_565x637=27,200,512,420 is refused - it overlaps another part of "
                          "the page (the pad) at UI scale 1.000; the derived grid is used"),
           "a grid of the player's over the DERIVED pad falls back itself (GD's order): the WARN", w);
        const ut::UtLayoutLive Q = with("27,40,512,480", "33,500,500,31", 64, 128);
        ok(Q.L.user[0] == 1 && Q.L.why[1] == ut::kUtLayoutOverlap && Q.L.against[1] == 0 &&
               Q.L.user[1] == 0,
           "a pad of the player's over the grid of the player's: the pad falls back (the grid is first)");
        const ut::UtLayoutLive QB = with("27,40,512,480", "33,560,500,31", 64, 128);
        ok(QB.L.user[0] == 1 && QB.L.user[1] == 1,
           "the pad of the player's moved below the moved grid: both in force");
        const ut::UtLayoutLive N = with(nullptr, "27,300,512,40", 64, 128);
        ok(N.L.user[0] == 0 && N.L.why[1] == ut::kUtLayoutOverlap && N.L.against[1] == 0,
           "no grid line (the real Transfer page): a pad over the derived grid falls back - nothing of "
           "the layout lies over the real Transfer sack's live cells");
    }

    printf("\n10. the screen parts: pad_screen / label_screen (GD's grammar and placement)\n");
    {
        using namespace ut;
        UtScreenRect r = {};
        ok(utScreenRectParse("tr,8,8,320,44", &r) && r.corner == kUtCornerTR && r.x == 8 && r.y == 8 &&
               r.w == 320 && r.h == 44,
           "\"tr,8,8,320,44\": the corner and four numbers");
        ok(utScreenRectParse(" BL , 0,0,1,1", &r) && r.corner == kUtCornerBL, "any case, blanks around every item");
        UtScreenRect bad;
        ok(!utScreenRectParse("tr,-8,8,320,44", &bad) && !utScreenRectParse("tx,8,8,320,44", &bad) &&
               !utScreenRectParse("tr,8,8,0,44", &bad) && !utScreenRectParse("tr,8,8,320", &bad) &&
               !utScreenRectParse("tr,8,8,320,44,1", &bad) && !utScreenRectParse("tr,8,8,16385,44", &bad) &&
               !utScreenRectParse("8,8,320,44", &bad),
           "refused: a minus, an unknown corner, w 0, four or six items, past 16384, no corner");
        char buf[48];
        UtScreenRect fr = {kUtCornerBR, 12, 34, 500, 31};
        ok(utScreenRectFormat(fr, buf, sizeof(buf)) > 0 && !strcmp(buf, "br,12,34,500,31") &&
               utScreenRectParse(buf, &r) && utScreenRectEq(r, fr),
           "formatted and parsed back", buf);
        // placement at UI scale 1.5 on 1920 x 1080: each corner
        int sh = 0;
        UtRectF f = utScreenPlace(UtScreenRect{kUtCornerTL, 10, 20, 100, 30}, 1920, 1080, 1.5f, &sh);
        ok(f.x == 15.0f && f.y == 30.0f && f.w == 150.0f && f.h == 45.0f && !sh, "tl: the offsets from the top-left");
        f = utScreenPlace(UtScreenRect{kUtCornerBR, 10, 20, 100, 30}, 1920, 1080, 1.5f, &sh);
        ok(f.x == 1920.0f - 15.0f - 150.0f && f.y == 1080.0f - 30.0f - 45.0f && !sh, "br: inward from the bottom-right");
        f = utScreenPlace(UtScreenRect{kUtCornerTR, 2000, 0, 100, 30}, 1920, 1080, 1.0f, &sh);
        ok(f.x == 0.0f && sh == 1, "an offset past the edge is shifted back, never resized");
        UtScreenSpot sp = utScreenLocate(UtScreenRect{kUtCornerTL, 0, 0, 100, 30}, 0, 1920, 1080, 0, 0, 1.0f);
        ok(sp.why == kUtLayoutScreenNoView, "no screen known: cannot be placed yet");
        sp = utScreenLocate(UtScreenRect{kUtCornerTL, 0, 0, 1920, 30}, 1, 1920, 1080, 0, 0, 1.0f);
        ok(sp.why == kUtLayoutScreenLarge, "as wide as the screen: refused");
        sp = utScreenLocate(UtScreenRect{kUtCornerBL, 5, 5, 500, 31}, 1, 1920, 1080, 677.0f, 222.0f, 1.25f);
        ok(sp.why == kUtLayoutOk && sp.px.x >= 0.0f && sp.px.y + sp.px.h <= 1080.0f &&
               sp.local.w == 500 && sp.local.h == 31,
           "located: frame-local record px, its drawn rectangle on the screen");
        const UtScreenRect back = utScreenFromClient(sp.px, 1920, 1080, 1.25f);
        ok(back.corner == kUtCornerBL && back.x >= 4 && back.x <= 6 && back.y >= 4 && back.y <= 6,
           "the nearest corner of a drawn rectangle gives the key back (within a record px)");
        ok(utScreenFromClient(UtRectF{1800, 10, 100, 20}, 1920, 1080, 1.0f).corner == kUtCornerTR &&
               utScreenFromClient(UtRectF{10, 10, 100, 20}, 1920, 1080, 1.0f).corner == kUtCornerTL,
           "the nearest corner: right and top, left and top");

        // the resolution over the vanilla page at UI scale 1 on 2560 x 1440
        UtRectF fr0, gr0;
        page(1.0f, 2560.0f, 1440.0f, &fr0, &gr0);
        UtLayoutSource src;
        src.frame = fr0;
        src.grid = gr0;
        src.scale = 1.0f;
        src.canvasW = 2560.0f;
        src.canvasH = 1440.0f;
        src.coverW = 565;
        src.coverH = 637;
        src.pad = nullptr;
        src.label = nullptr;
        src.groups = 15;
        src.widest = 5;
        src.labelPx = 13;
        src.search = true;
        src.screenKnown = 1;
        src.padScreen = "tr,20,20,500,31";
        UtLayoutLive L;
        ok(utLayoutFromSource(src, &L) && L.L.screen[kUtLayPad] == 1 && L.L.user[kUtLayPad] == 1 &&
               L.L.screenWhy[kUtLayPad] == kUtLayoutOk && L.L.pad.ok && L.L.labelInPad == 1 &&
               utRectInside(L.L.rect[kUtLayLabel], L.L.rect[kUtLayPad]) &&
               L.L.screenPx[kUtLayPad].x + L.L.screenPx[kUtLayPad].w == 2540.0f,
           "pad_screen in force: the pad at the top-right corner, its label box with it");
        char said[320];
        utLayoutSaidText(L, 1.0f, said, sizeof(said));
        ok(strstr(said, "(screen)") != nullptr, "the INFO line says the pad is on the screen", said);
        src.screenKnown = 0;
        ok(utLayoutFromSource(src, &L) && L.L.screen[kUtLayPad] == 0 &&
               L.L.screenWhy[kUtLayPad] == kUtLayoutScreenNoView && !L.L.user[kUtLayPad],
           "before the page was measured: waiting, the derived pad meanwhile");
        src.screenKnown = 1;
        src.padScreen = "tl,1000,520,500,31";   // over the grid (the frame sits at 997,402)
        ok(utLayoutFromSource(src, &L) && L.L.screen[kUtLayPad] == 0 &&
               L.L.screenWhy[kUtLayPad] == kUtLayoutScreenOverGrid && L.L.screenAgainst[kUtLayPad] == kUtLayGrid,
           "a screen pad over the grid is refused: it would cover the item grid");
        src.padScreen = "tr,20,20,500,31";
        src.labelScreen = "tr,30,25,200,12";   // inside the screen pad
        ok(utLayoutFromSource(src, &L) && L.L.screen[kUtLayPad] == 1 && L.L.screen[kUtLayLabel] == 0 &&
               L.L.screenWhy[kUtLayLabel] == kUtLayoutOverlap && L.L.screenAgainst[kUtLayLabel] == kUtLayPad,
           "a screen label over the screen pad yields to it");
        src.labelScreen = "bl,20,20,300,14";
        src.pad = "33,40,500,31";   // a pinned pad's own line is not judged
        ok(utLayoutFromSource(src, &L) && L.L.screen[kUtLayLabel] == 1 && L.L.labelInPad == 0 &&
               L.L.ignored[kUtLayPad] == 1 && L.L.why[kUtLayPad] == kUtLayoutOk,
           "both on the screen: the label its own; pad_565x637 ignored while the pad is pinned");

        // the frame and what the input reads
        const UtScreenFrame sf = utScreenFrame(UtRectF{100.4f, 50.6f, 200.0f, 31.0f}, 1);
        ok(sf.x == 100 && sf.y == 51 && sf.w == 200 && sf.h == 31 && sf.ox == 99 && sf.oy == 50 && sf.ow == 202 &&
               sf.oh == 33 && sf.top[2] == 202 && sf.left[3] == 31 && sf.bottom[1] == 82 && sf.right[0] == 300,
           "the 1-px frame outside the ground: light top / left, dark bottom / right, no pixel twice");
        UtPadScreenRects pub;
        utScreenPubReset(&pub);
        ok(!pub.shown && pub.buttonCount == 0, "an empty publication");
        utScreenPubPad(&pub, sf, 0.5f, 0.5f);
        ok(pub.shown == 1 && pub.pad.x == 49.5f && pub.pad.w == 101.0f, "the pad's whole frame, in client px");
        ok(utScreenPubButton(&pub, UtRectF{103.0f, 54.0f, 40.0f, 12.0f}, 7, 0.5f, 0.5f) && pub.buttonCount == 1 &&
               pub.buttons[0].index == 7 && pub.buttons[0].r.x == 51.5f,
           "a button as drawn, its pad index, in client px");
        ok(!utScreenPubButton(&pub, UtRectF{0, 0, 0, 10}, 3) && !utScreenPubButton(&pub, UtRectF{0, 0, 5, 5}, -1),
           "an empty button or no index is not published");
        utScreenPubLabel(&pub, utScreenFrame(UtRectF{10, 10, 50, 12}, 1));
        ok(pub.label.w == 52.0f, "the label's whole frame");
    }

    printf("\nthe pages follow the grid (ut_gridpack.h: every group re-fitted, all or none)\n");
    {
        // 400 records of 1 x 1, 90 of up to 2 x 3, 50 of 1 x 4: a group's slot is its largest footprint
        std::vector<std::vector<gdut::PackItem>> items(3);
        for (int k = 0; k < 400; ++k) items[0].push_back(gdut::PackItem{k, 1, 1});
        for (int k = 0; k < 90; ++k) items[1].push_back(gdut::PackItem{k, k % 2 ? 2 : 1, k % 3 ? 3 : 2});
        for (int k = 0; k < 50; ++k) items[2].push_back(gdut::PackItem{k, 1, 4});
        using ut::UtGridPack;
        using ut::utGridPackAll;
        using ut::utGridUnionSlots;
        using ut::utRowKeep;
        UtGridPack a, b, c, e;
        ok(utGridPackAll(items, 16, 15, &a) && a.failed == -1 && a.slot[0].cols == 16 && a.slot[0].rows == 15 &&
               a.pages[0] == 2 && a.slot[1].w == 2 && a.slot[1].h == 3 && a.slot[1].cols == 8 &&
               a.slot[1].rows == 5 && a.pages[1] == 3 && a.slot[2].cols == 16 && a.slot[2].rows == 3 &&
               a.pages[2] == 2 && a.total == 7,
           "16 x 15 (the derived grid): 2 + 3 + 2 pages");
        ok(utGridPackAll(items, 17, 19, &b) && b.slot[1].cols == 8 && b.slot[1].rows == 6 && b.pages[0] == 2 &&
               b.pages[1] == 2 && b.pages[2] == 1 && b.total == 5 && b.placed[0].size() == 400 &&
               b.placed[0][323].page == 1 && b.placed[0][323].slotCol == 0 && b.placed[0][323].slotRow == 0 &&
               b.placed[0][322].page == 0 && b.placed[0][322].slotCol == 16 && b.placed[0][322].slotRow == 18,
           "17 x 19 (a larger grid): 2 + 2 + 1 pages; record 323 opens page 2, 322 ends page 1 at 16,18");
        ok(utGridPackAll(items, 10, 8, &c) && c.pages[0] == 5 && c.slot[1].cols == 5 && c.slot[1].rows == 2 &&
               c.pages[1] == 9 && c.slot[2].rows == 2 && c.pages[2] == 3 && c.total == 17,
           "10 x 8 (a smaller grid): 5 + 9 + 3 pages");
        ok(!utGridPackAll(items, 10, 3, &e) && e.failed == 2,
           "10 x 3: the 1 x 4 group does not fit - refused as a whole (the grid in force stays), group 2 named");
        gdut::SlotGeometry u;
        u.w = 2;
        u.h = 3;
        ok(utGridUnionSlots(&u, 17, 19, 100) == 3 && u.cols == 8 && u.rows == 6 &&
               utGridUnionSlots(&u, 16, 15, 100) == 3 && u.cols == 8 && u.rows == 5,
           "a several-group button's view: its slot counted again on the new cells, its pages");
        ok(utRowKeep(7, 8, 5) == 11 && 11 * 5 <= 7 * 8 && 7 * 8 < 12 * 5 && utRowKeep(0, 8, 5) == 0 &&
               utRowKeep(3, 0, 5) == 0 && utRowKeep(4, 5, 5) == 4,
           "the shown row keeps its first visible item across a column change (GD's utRowKeep)");
    }

    // ---- the screen parts' draw site (the head of the HUD item rollover's Draw) ----
    ok(ut::utScreenSiteDecide(true, true, true, true, false) == ut::kUtSiteDraw,
       "site: a screen part in force, the page drawn this frame - drawn under the tooltips");
    ok(ut::utScreenSiteDecide(false, true, true, true, false) == ut::kUtSiteNoParts,
       "site: nothing on the screen, the editor closed - the page draw's POST draws the pad");
    ok(ut::utScreenSiteDecide(true, false, true, true, false) == ut::kUtSiteNotEngine,
       "site: another canvas - not drawn");
    ok(ut::utScreenSiteDecide(true, true, false, true, false) == ut::kUtSiteNoPage,
       "site: the caravan not drawn before the rollover this frame - left to Present");
    ok(ut::utScreenSiteDecide(true, true, true, false, false) == ut::kUtSiteStale,
       "site: no fresh layout - left to Present");
    ok(ut::utScreenSiteDecide(true, true, true, true, true) == ut::kUtSiteDrawnAlready,
       "site: once a frame");

    {   // the grid in force (utGridInForce, ut_panel gridApply): the player's grid_ rectangle packs the
        // collection ON and OFF; the page on screen shows it only ON. The numbers of the measured
        // window (UI scale 1.375): the frame (873,282) 777x876, the derived grid (910.1,455.2) 704x660.
        using namespace ut;
        UtGridForceIn base = {};
        base.on = 1;
        base.coverW = 565;
        base.coverH = 637;
        base.frame = UtRectF{873.0f, 282.0f, 777.0f, 876.0f};
        base.frameGrid = UtRectF{910.1f, 455.2f, 704.0f, 660.0f};
        base.scale = 1.375f;
        base.cell = 44.0f;
        base.derivedCols = 16;
        base.derivedRows = 15;
        auto near = [](float a, float b) { return a - b < 0.05f && b - a < 0.05f; };
        auto inside = [](const UtRectF& r, const UtRectF& f) {
            return r.x >= f.x && r.y >= f.y && r.x + r.w <= f.x + f.w + 0.01f && r.y + r.h <= f.y + f.h + 0.01f;
        };
        auto with = [&](int on, int set, UtRect u) {
            UtGridForceIn in = base;
            in.on = on;
            in.userSet = set;
            in.userRect = u;
            return utGridInForce(in);
        };
        const UtGridForce d0 = with(1, 0, UtRect{0, 0, 0, 0});
        ok(d0.packUser == 0 && d0.packCols == 16 && d0.packRows == 15 && d0.drawnUser == 0 &&
               d0.drawn.x == base.frameGrid.x && d0.drawn.w == 704.0f && d0.drawnCols == 16 && d0.drawnRows == 15,
           "grid in force: no grid line - the derived grid packs and is drawn (16 x 15)");
        const UtGridForce a = with(1, 1, UtRect{27, 126, 512, 384});
        ok(a.packUser == 1 && a.packCols == 16 && a.packRows == 12 && a.drawnUser == 1 &&
               a.drawn.x == 910.0f && a.drawn.y == 455.0f && a.drawn.w == 704.0f && a.drawn.h == 528.0f &&
               near(a.dx, -0.1f) && near(a.dy, -0.2f) && inside(a.drawn, base.frame),
           "grid in force: grid_565x637=27,126,512,384 ON - 16 x 12 cells drawn at (910,455) 704x528, "
           "the sack shifted by -0.1,-0.2 px (the log's numbers)");
        const UtGridForce aOff = with(0, 1, UtRect{27, 126, 512, 384});
        ok(aOff.packUser == 1 && aOff.packCols == 16 && aOff.packRows == 12 && aOff.drawnUser == 0 &&
               aOff.drawn.x == base.frameGrid.x && aOff.drawn.y == base.frameGrid.y && aOff.drawnCols == 16 &&
               aOff.drawnRows == 15 && near(aOff.dx, a.dx) && near(aOff.dy, a.dy),
           "grid in force: the same line OFF - still packed 16 x 12, the real Transfer page drawn on its own "
           "16 x 15 grid");
        const UtGridForce m = with(1, 1, UtRect{0, 253, 544, 384});
        ok(m.packCols == 17 && m.packRows == 12 && m.drawn.x == 873.0f && m.drawn.y == 630.0f &&
               m.drawn.w == 748.0f && m.drawn.h == 528.0f && near(m.dx, -37.1f) && near(m.dy, 174.8f) &&
               inside(m.drawn, base.frame),
           "grid in force: a moved grid 0,253,544,384 - 17 x 12 at (873,630) 748x528, shift -37.1,174.8 px, "
           "inside the caravan window (the log's numbers)");
        const UtGridForce t = with(1, 1, UtRect{0, 0, 544, 608});
        ok(t.packCols == 17 && t.packRows == 19 && t.drawn.w == 748.0f && t.drawn.h == 836.0f &&
               inside(t.drawn, base.frame),
           "grid in force: 0,0,544,608 - 17 x 19 = the window's box cap 323, inside the window");
        UtGridForceIn sv = base;   // a modded caravan window 660 x 699 with a 20 x 20 derived grid
        sv.coverW = 660;
        sv.coverH = 699;
        sv.frame = UtRectF{100.0f, 50.0f, 907.5f, 961.1f};
        sv.frameGrid = UtRectF{115.0f, 128.0f, 880.0f, 880.0f};
        sv.derivedCols = 20;
        sv.derivedRows = 20;
        sv.userSet = 1;
        sv.userRect = UtRect{11, 57, 640, 640};
        const UtGridForce v = utGridInForce(sv);
        sv.userSet = 0;
        const UtGridForce vd = utGridInForce(sv);
        ok(v.packCols == 20 && v.packRows == 20 && v.drawn.x == 115.0f && v.drawn.y == 128.0f &&
               v.drawn.w == 880.0f && v.drawn.h == 880.0f && inside(v.drawn, sv.frame) && vd.packUser == 0 &&
               vd.packCols == 20 && vd.packRows == 20 && vd.drawn.w == 880.0f,
           "grid in force: a 660 x 699 window - grid_660x699=11,57,640,640 is 20 x 20 (cap 420), and with no "
           "line its derived 20 x 20 grid packs");
        UtGridForceIn fl = base;
        fl.userSet = 1;
        fl.userRect = UtRect{27, 126, 512, 384};
        fl.failCols = 16;
        fl.failRows = 12;
        const UtGridForce f = utGridInForce(fl);
        fl.cell = 0.0f;
        const UtGridForce z = utGridInForce(fl);
        ok(f.packUser == 0 && f.packCols == 16 && f.packRows == 15 && f.drawnUser == 0 && z.packUser == 0,
           "grid in force: cells the groups could not be packed on, or no drawn cell - the derived grid");

        // the lattice's grid (utLatticeGridOf, ut_panel gridApply -> ut_cover coverSetGrid): the
        // collection's grid in force in caravan-window units, the same cells the overlays use
        const UtRect baseArea = {27, 126, 512, 480};
        const UtLatticeGrid l0 = utLatticeGridOf(d0, UtRect{0, 0, 0, 0}, 0, baseArea);
        const UtLatticeGrid la = utLatticeGridOf(a, UtRect{27, 126, 512, 384}, 0, baseArea);
        const UtLatticeGrid lOff = utLatticeGridOf(aOff, UtRect{27, 126, 512, 384}, 0, baseArea);
        const UtLatticeGrid lm = utLatticeGridOf(m, UtRect{0, 253, 544, 384}, 0, baseArea);
        const UtLatticeGrid lf = utLatticeGridOf(f, UtRect{27, 126, 512, 384}, 0, baseArea);
        ok(l0.user == 0 && l0.cols == 16 && l0.rows == 15 && la.user == 1 && la.x == 27 && la.y == 126 &&
               la.cols == 16 && la.rows == 12 && lOff.user == 1 && lOff.cols == 16 && lOff.rows == 12 &&
               lm.user == 1 && lm.x == 0 && lm.y == 253 && lm.cols == m.packCols && lm.rows == m.packRows &&
               lf.user == 0 && lf.cols == 16 && lf.rows == 15,
           "lattice grid: no line - none (the background's own lattice); 27,126 16 x 12 and the moved 0,253 "
           "17 x 12 - the player's rectangle and the packed cells, ON and OFF alike (the cover is out while "
           "OFF); a grid the groups could not be packed on - none");
        UtGridForceIn sa = sv;   // the modded caravan's own area with no line: 20 x 19 (the pad's band)
        sa.userSet = 0;
        sa.derivedRows = 19;
        sa.frameGrid.h = 44.0f * 19.0f;
        const UtGridForce va = utGridInForce(sa);
        const UtLatticeGrid lva = utLatticeGridOf(va, UtRect{0, 0, 0, 0}, 1, UtRect{11, 57, 640, 608});
        const UtLatticeGrid lvu = utLatticeGridOf(v, UtRect{11, 57, 640, 640}, 0, UtRect{11, 57, 640, 608});
        ok(va.packUser == 0 && va.packCols == 20 && va.packRows == 19 && lva.user == 1 && lva.x == 11 &&
               lva.y == 57 && lva.cols == 20 && lva.rows == 19 && lvu.user == 1 && lvu.x == 11 && lvu.y == 57 &&
               lvu.cols == 20 && lvu.rows == 20,
           "lattice grid: a 660 x 699 window's area 20 x 19 at 11,57 is the grid in force (its 20 x 20 lattice "
           "repainted); grid_660x699=11,57,640,640 = the derived 20 x 20 at 11,57 (the cover keeps its own)");
    }
    printf("\nthe game's own Transfer page: the pad and the label stay where the collection page has them\n");
    {
        using namespace ut;
        UtRectF f, g;
        const bool pg = page(1.0f, 1920.0f, 1080.0f, &f, &g);
        const char* line = "27,126,512,384";   // a 16 x 12 grid of the player's
        // the pad over the real sack's rows 12 and 13, the label over row 14 (columns 8..14)
        UtLayoutSource src = source(f, g, 1.0f, 1920.0f, 1080.0f, "27,512,512,60", "300,580,200,20");
        src.screenKnown = 1;
        UtLayoutSource sc = src;
        sc.gridKey = line;   // the collection page's resolution (the view OFF: its own)
        UtLayoutLive coll, real;
        const bool made = pg && utLayoutFromSource(sc, &coll) && utLayoutFromSource(src, &real);
        ok(made && coll.L.user[kUtLayGrid] == 1 && coll.L.user[kUtLayPad] == 1 && coll.L.why[kUtLayPad] == kUtLayoutOk &&
               coll.L.user[kUtLayLabel] == 1 && coll.L.why[kUtLayLabel] == kUtLayoutOk,
           "the collection page with a 16 x 12 grid: the pad 27,512,512,60 and the label 300,580,200,20 in force");
        ok(made && real.L.user[kUtLayPad] == 0 && real.L.why[kUtLayPad] == kUtLayoutOverlap &&
               real.L.against[kUtLayPad] == kUtLayGrid && real.L.user[kUtLayLabel] == 0,
           "the real page's own resolution (the real 16 x 15 sack): both would fall back - the rule replaced");
        utLayoutRealPage(&real, coll);
        ok(utRectEq(real.L.rect[kUtLayPad], UtRect{27, 512, 512, 60}) && real.L.user[kUtLayPad] == 1 &&
               real.L.why[kUtLayPad] == kUtLayoutOk && real.L.pad.ok && real.L.padDerived == 0 &&
               utRectEq(real.L.rect[kUtLayLabel], UtRect{300, 580, 200, 20}) && real.L.user[kUtLayLabel] == 1 &&
               real.L.labelInPad == 0 && real.L.label.ok,
           "utLayoutRealPage: the real page draws the collection page's pad and label, whole and unchanged");
        ok(utRectEq(real.L.rect[kUtLayGrid], real.in.def[kUtLayGrid]) && real.L.user[kUtLayGrid] == 0 &&
               utRectEq(real.in.def[kUtLayGrid], UtRect{27, 126, 512, 480}),
           "  its grid stays the game's own sack 27,126,512,480 (never moved, never re-dimmed)");
        const UtRectF pd = utLocalToClient(real.L.rect[kUtLayPad], real.in.originX, real.in.originY, 1.0f);
        const UtRectF lb = utLocalToClient(real.L.rect[kUtLayLabel], real.in.originX, real.in.originY, 1.0f);
        const int pu = utRealCellsCovered(g, 16, 15, pd), lu = utRealCellsCovered(g, 16, 15, lb);
        _snprintf_s(d, sizeof(d), _TRUNCATE, "pad %d, label %d", pu, lu);
        ok(pu == 32 && lu == 7, "the real cells covered: the pad 2 rows x 16, the label 7 of row 14", d);
        char t[320];
        utRealPadText(real, pu, lu, 240, t, sizeof(t));
        ok(!strcmp(t, "the pad 27,512,512,60 (pad_565x637) covers 32 of the sack's 240 cell(s); the label "
                      "300,580,200,20 (label_565x637) covers 7 of them"),
           "the log's words (GD's materials-page line)", t);
        // no line: the derived pad in the band below the sack covers nothing
        UtLayoutSource sd = source(f, g, 1.0f, 1920.0f, 1080.0f, nullptr, nullptr);
        UtLayoutLive der;
        const bool dm = utLayoutFromSource(sd, &der);
        const UtRectF dp = utLocalToClient(der.L.rect[kUtLayPad], der.in.originX, der.in.originY, 1.0f);
        utRealPadText(der, utRealCellsCovered(g, 16, 15, dp), 0, 240, t, sizeof(t));
        ok(dm && utRealCellsCovered(g, 16, 15, dp) == 0 &&
               !strcmp(t, "the pad 33,606,500,31 (the derived pad) leaves the sack's 240 cell(s) clear"),
           "the derived pad leaves the sack clear", t);
        // a pad pinned to the screen over the window's bottom rows: refused against the real sack,
        // kept by the collection page's 16 x 12 grid, so the real page draws it there
        UtLayoutSource ss = source(f, g, 1.0f, 1920.0f, 1080.0f, nullptr, nullptr);
        ss.padScreen = "tl,704,734,512,60";
        ss.screenKnown = 1;
        UtLayoutSource ssc = ss;
        ssc.gridKey = line;
        UtLayoutLive scoll, sreal;
        const bool sm = utLayoutFromSource(ssc, &scoll) && utLayoutFromSource(ss, &sreal);
        ok(sm && scoll.L.screen[kUtLayPad] == 1 && sreal.L.screen[kUtLayPad] == 0 &&
               sreal.L.screenWhy[kUtLayPad] == kUtLayoutScreenOverGrid,
           "pad_screen over the window's bottom rows: in force on the collection page, refused by the real sack");
        utLayoutRealPage(&sreal, scoll);
        const int su = utRealCellsCovered(g, 16, 15, sreal.L.screenPx[kUtLayPad]);
        utRealPadText(sreal, su, 0, 240, t, sizeof(t));
        ok(sreal.L.screen[kUtLayPad] == 1 && sreal.L.screenWhy[kUtLayPad] == kUtLayoutOk && su > 0 &&
               !strncmp(t, "the pad ", 8) && strstr(t, "(pad_screen) covers ") != nullptr,
           "  utLayoutRealPage: the real page keeps it pinned there, over the cells it covers", t);
        UtLayoutLive same = coll;
        utLayoutRealPage(&same, same);
        utLayoutRealPage(nullptr, coll);
        ok(utRectEq(same.L.rect[kUtLayPad], coll.L.rect[kUtLayPad]), "itself or no page: nothing happens");
        ok(utRealCellsCovered(g, 0, 15, pd) == 0 && utRealCellsCovered(g, 16, 15, UtRectF{0, 0, 0, 10}) == 0 &&
               utRealCellsCovered(g, 16, 15, UtRectF{g.x + g.w, g.y, 40.0f, 40.0f}) == 0,
           "no cells, an empty part, a part touching the sack's right edge: none covered");
        char small[12];
        ok(utRealPadText(real, pu, lu, 240, small, sizeof(small)) == 11 && !utRealPadText(real, 0, 0, 0, nullptr, 4),
           "the text is cut at its buffer, never overrun");
    }
    printf("\nthe in-window derived set while the pad and the label are on the screen (the editor's RESET)\n");
    {
        // the panel's derivation (ut_panel layout): a 660 x 699 window at UI scale 1.375, the area 20 x 20
        // at 11,57; pad_screen and no label line give band 0 (20 x 20), the in-window set keeps the pad's
        // band (20 x 19) and lays the derived pad and label in it with no screen key
        using namespace ut;
        UtCaravanGeo geo = utCaravanGeoBase();
        geo.x0 = 11;
        geo.y0 = 57;
        geo.w = geo.h = 640;
        geo.cols = geo.rows = 20;
        geo.windowW = 660;
        geo.windowH = 699;
        const int band = utCaravanPadBandFor(true, false, false, true);
        int c0 = 0, r0 = 0, c1 = 0, r1 = 0;
        ok(band == 0 && utCaravanCollectionCells(geo, band, &c0, &r0) && c0 == 20 && r0 == 20 &&
               utCaravanCollectionCells(geo, kUtCaravanPadBand, &c1, &r1) && c1 == 20 && r1 == 19,
           "pad_screen with no label line: band 0 gives 20 x 20; the in-window set keeps the band: 20 x 19");
        const float s = 1.375f, cell = 32.0f * s;
        const UtRectF measured = {807.0f + 11.0f * s, 239.0f + 57.0f * s, 512.0f * s, 480.0f * s};
        UtRectF gWin, gFull, g0, f0;
        utCaravanAreaRects(measured, cell, c1, r1, 20, 20, &gWin, &gFull);
        utCaravanAreaRects(measured, cell, c0, r0, 20, 20, &g0, &f0);
        UtLayoutSource sw;
        sw.frame = UtRectF{807.0f, 239.0f, 660.0f * s, 699.0f * s};
        sw.grid = gWin;
        sw.scale = s;
        sw.canvasW = 3440.0f;
        sw.canvasH = 1440.0f;
        sw.coverW = 660;
        sw.coverH = 699;
        sw.pad = nullptr;
        sw.label = nullptr;
        sw.groups = 19;
        sw.widest = 5;
        sw.labelPx = 18;
        sw.search = true;
        sw.screenKnown = 1;
        UtLayoutLive lw;
        char dw[96];
        const bool mw = utLayoutFromSource(sw, &lw);
        const UtRect gp = lw.L.rect[kUtLayGrid], pp = lw.L.rect[kUtLayPad];
        _snprintf_s(dw, sizeof(dw), _TRUNCATE, "grid %d,%d,%d,%d pad %d,%d,%d,%d", gp.x, gp.y, gp.w, gp.h, pp.x,
                    pp.y, pp.w, pp.h);
        ok(mw && utRectEq(gp, UtRect{11, 57, 640, 608}) && lw.L.user[kUtLayGrid] == 0 && lw.L.user[kUtLayPad] == 0 &&
               lw.L.screen[kUtLayPad] == 0 && pp.y >= 57 + 608 && pp.y + pp.h <= 699 && !utRectsMeet(pp, gp),
           "the in-window set: the grid 11,57,640,608, the derived pad in the band below it, inside the window", dw);
        UtLayoutSource s0 = sw;
        s0.grid = g0;
        UtLayoutLive l0;
        const bool m0 = utLayoutFromSource(s0, &l0);
        const UtRect g0r = l0.L.rect[kUtLayGrid], p0r = l0.L.rect[kUtLayPad];
        ok(m0 && utRectEq(g0r, UtRect{11, 57, 640, 640}) && (p0r.y + p0r.h > 699 || utRectsMeet(p0r, g0r)),
           "  the band-0 area with the pad back in the window has no room for it (RESET needs the in-window set)");
    }
    printf("\nthe label of a pad pinned to the screen is on the screen (a 660x699 window, UI scale 1.375)\n");
    {
        // The window measured at 807,239 on a 3440 x 1440 canvas; the derived area 20 x 19 cells at
        // 11,57 (the band below kept - what the page derived while the label counted as in the
        // window), 880 x 836 px; pad_screen=tl,477,1,675,176, no label line, the grid line
        // 11,57,640,640 (the bottom edge dragged to the window's last whole row).
        using namespace ut;
        const float s = 1.375f;
        UtLayoutSource sv;
        sv.frame = UtRectF{807.0f, 239.0f, 660.0f * s, 699.0f * s};
        sv.grid = UtRectF{807.0f + 11.0f * s, 239.0f + 57.0f * s, 20.0f * 44.0f, 19.0f * 44.0f};
        sv.scale = s;
        sv.canvasW = 3440.0f;
        sv.canvasH = 1440.0f;
        sv.coverW = 660;
        sv.coverH = 699;
        sv.pad = nullptr;
        sv.label = nullptr;
        sv.groups = 17;
        sv.widest = 5;
        sv.labelPx = 18;
        sv.search = true;
        sv.screenKnown = 1;
        sv.padScreen = "tl,477,1,675,176";
        sv.gridKey = "11,57,640,640";
        UtLayoutLive la;
        char sd[96];
        const bool made = utLayoutFromSource(sv, &la);
        _snprintf_s(sd, sizeof(sd), _TRUNCATE, "pad %d,%d,%d,%d label %d,%d,%d,%d", la.L.rect[kUtLayPad].x,
                    la.L.rect[kUtLayPad].y, la.L.rect[kUtLayPad].w, la.L.rect[kUtLayPad].h,
                    la.L.rect[kUtLayLabel].x, la.L.rect[kUtLayLabel].y, la.L.rect[kUtLayLabel].w,
                    la.L.rect[kUtLayLabel].h);
        ok(made && la.L.screen[kUtLayPad] == 1 && utRectEq(la.L.rect[kUtLayPad], UtRect{-110, -173, 675, 176}) &&
               la.L.labelInPad == 1 && utRectInside(la.L.rect[kUtLayLabel], la.L.rect[kUtLayPad]),
           "the pad on the screen at -110,-173,675,176, the label its own box, on the screen with it", sd);
        ok(utRectEq(la.L.rect[kUtLayLabel], UtRect{204, -85, 358, 84}), "  the label box 204,-85,358,84 (the page's own words)", sd);
        ok(la.L.user[kUtLayGrid] == 1 && la.L.why[kUtLayGrid] == kUtLayoutOk &&
               utRectEq(la.L.rect[kUtLayGrid], UtRect{11, 57, 640, 640}),
           "grid_660x699=11,57,640,640 in force: it meets the window's derived label box, never the label on the screen");
        ok(utRectsMeet(la.in.def[kUtLayLabel], la.L.rect[kUtLayGrid]),
           "  (the window's derived label box does lie under that grid: the rule, not the numbers, keeps it)");
        UtLayoutSource sn = sv;
        sn.screenKnown = 0;   // pad_screen waiting to be placed: pinned, the band already 0
        UtLayoutLive ln;
        ok(utLayoutFromSource(sn, &ln) && ln.L.screenWhy[kUtLayPad] == kUtLayoutScreenNoView &&
               ln.L.user[kUtLayGrid] == 1 && ln.L.why[kUtLayGrid] == kUtLayoutOk &&
               utRectEq(ln.L.rect[kUtLayGrid], UtRect{11, 57, 640, 640}),
           "the pad waiting to be placed is pinned: the label rides with it, the same grid line in force");
        UtLayoutSource sw = sv;
        sw.padScreen = nullptr;
        UtLayoutLive lb;
        ok(utLayoutFromSource(sw, &lb) && lb.L.user[kUtLayGrid] == 0 && lb.L.why[kUtLayGrid] == kUtLayoutOverlap,
           "the pad in the window: its label box is in the window again - the same grid line is refused");
        UtLayoutSource sl2 = sv;
        sl2.label = "300,660,200,30";   // a label line of the player's in the window, under the grid
        UtLayoutLive lc;
        ok(utLayoutFromSource(sl2, &lc) && lc.L.why[kUtLayGrid] == kUtLayoutOk && lc.L.why[kUtLayLabel] == kUtLayoutOverlap &&
               lc.L.against[kUtLayLabel] == kUtLayGrid && lc.L.labelInPad == 1 &&
               utRectInside(lc.L.rect[kUtLayLabel], lc.L.rect[kUtLayPad]),
           "a label line in the window is still judged: it meets the grid and falls back to the screen pad's box");
        UtLayoutSource sl = sv;
        sl.labelScreen = "tr,855,71,358,84";
        UtLayoutLive le;
        ok(utLayoutFromSource(sl, &le) && le.L.screen[kUtLayLabel] == 1 && le.L.why[kUtLayGrid] == kUtLayoutOk,
           "label_screen=tr,855,71,358,84 as well: the same grid in force (as before)");
        // the derived area's band, from the keys in force
        ok(utCaravanPadBandFor(true, false, false, true) == 0 && utCaravanPadBandFor(true, true, false, true) == 0 &&
               utCaravanPadBandFor(true, true, true, true) == 0,
           "the band is 0 with pad_screen and the label on the screen: label_screen, or no label line");
        ok(utCaravanPadBandFor(true, false, true, true) == kUtCaravanPadBand &&
               utCaravanPadBandFor(false, true, false, true) == kUtCaravanPadBand &&
               utCaravanPadBandFor(true, true, false, false) == kUtCaravanPadBand &&
               utCaravanPadBandFor(false, false, false, true) == kUtCaravanPadBand,
           "kept: a label line in the window, the pad in the window, the screen input not working, no keys");
        UtCaravanGeo geo = utCaravanGeoBase();
        geo.valid = true;
        geo.x0 = 11;
        geo.y0 = 57;
        geo.w = 640;
        geo.h = 640;
        geo.windowW = 660;
        geo.windowH = 699;
        geo.cols = 20;
        geo.rows = 20;
        int c0 = 0, r0 = 0, c1 = 0, r1 = 0;
        const bool k0 = utCaravanCollectionCells(geo, utCaravanPadBandFor(true, false, false, true), &c0, &r0);
        const bool k1 = utCaravanCollectionCells(geo, utCaravanPadBandFor(true, false, true, true), &c1, &r1);
        ok(k0 && k1 && c0 == 20 && r0 == 20 && c1 == 20 && r1 == 19,
           "the 20 x 20 area at y 57: 20 x 20 with the pad on the screen and no label line, 20 x 19 with one");
    }

    printf("\nE. the EDIT button (edit_button=1): row 2's cell 7, right of the field; the label one cell later\n");
    {
        const float scales[3] = {0.8f, 1.0f, 1.25f};
        const float m = (float)ut::kUtPadMargin;
        int good = 0;
        char why[240] = "";
        for (float s : scales) {
            ut::UtRectF f, g;
            const bool pg = page(s, 1920.0f, 1080.0f, &f, &g);
            ut::UtLayoutSource off = source(f, g, s, 1920.0f, 1080.0f, nullptr, nullptr);
            ut::UtLayoutSource on = off;
            on.edit = true;
            static ut::UtLayoutLive a, b;
            const bool la = pg && ut::utLayoutFromSource(off, &a), lb = pg && ut::utLayoutFromSource(on, &b);
            const ut::UtPadOut& oa = a.defOut;
            const ut::UtPadOut& ob = b.defOut;
            bool same = la && lb && a.defBand > 0 && b.defBand == a.defBand;
            for (int i = 0; i < ut::kUtPadMax && same; ++i)
                if (i != ut::kUtPadEdit && memcmp(&oa.btn[i], &ob.btn[i], sizeof(ut::UtRectF)) != 0) same = false;
            same = same && !memcmp(&oa.ground, &ob.ground, sizeof(oa.ground)) &&
                   !memcmp(&oa.fieldText, &ob.fieldText, sizeof(oa.fieldText));
            const float step = (float)(oa.cellW + oa.gap);
            const ut::UtRectF& e = ob.btn[ut::kUtPadEdit];
            const ut::UtRectF& fld = ob.btn[ut::kUtPadSearch];
            const float x7 = oa.ground.x + ut::utPadRound((m + 7.0f * step) * s);
            const float x8 = oa.ground.x + ut::utPadRound((m + 8.0f * step - (float)oa.gap) * s);
            const float l7 = oa.ground.x + ut::utPadRound((m + 7.0f * step + 3.0f) * s);
            const float l8 = oa.ground.x + ut::utPadRound((m + 8.0f * step + 3.0f) * s);
            const bool offOk = oa.btn[ut::kUtPadEdit].w == 0.0f && oa.label.x == l7;
            const bool onOk = e.x == x7 && e.x + e.w == x8 && e.y == fld.y && e.h == fld.h && e.x >= fld.x + fld.w &&
                              ob.label.x == l8 && ob.label.x > e.x + e.w &&
                              ob.label.x + ob.label.w == oa.label.x + oa.label.w && ob.label.y == oa.label.y &&
                              ut::utRectInside(e, ob.ground, 0.0f);
            const bool defOk = a.in.edit == 0 && b.in.edit == 1 && b.L.labelInPad == 1 &&
                               b.in.def[ut::kUtLayLabel].x > a.in.def[ut::kUtLayLabel].x &&
                               b.in.def[ut::kUtLayLabel].x + b.in.def[ut::kUtLayLabel].w ==
                                   a.in.def[ut::kUtLayLabel].x + a.in.def[ut::kUtLayLabel].w;
            const float cx = e.x + e.w * 0.5f, cy = e.y + e.h * 0.5f;
            const bool hitOk = hitOut(ob, cx, cy) == ut::kUtPadEdit && hitOut(oa, cx, cy) == -1;
            if (same && offOk && onOk && defOk && hitOk) ++good;
            else if (!why[0])
                _snprintf_s(why, sizeof(why), _TRUNCATE,
                            "scale %.2f: same %d off %d on %d def %d hit %d - EDIT %.0f,%.0f %.0fx%.0f, "
                            "label %.0f (want %.0f)",
                            s, same, offOk, onOk, defOk, hitOk, e.x, e.y, e.w, e.h, ob.label.x, l8);
        }
        ok(good == 3,
           "at UI scale 0.8 / 1.0 / 1.25: edit_button=0 lays out no EDIT and the label where it always was; "
           "edit_button=1 changes only EDIT (cell 7, the field's row, inside the pad) and the label's left edge "
           "(one cell later, its right edge kept), the derived label box follows, and EDIT's centre hits EDIT",
           why);
        const ut::UtRect r = {27, 512, 512, 60};
        const ut::UtPadFit fit = ut::utPadFitRect(r, 15, 1, 1.0f, 5);
        static ut::UtPadOut o0, o1;
        const bool b0 = ut::utPadOutFromFit(fit, 0, 0.0f, 0.0f, 1.0f, true, false, &o0);
        const bool b1 = ut::utPadOutFromFit(fit, 0, 0.0f, 0.0f, 1.0f, true, true, &o1);
        bool same = b0 && b1;
        for (int i = 0; i < ut::kUtPadMax && same; ++i)
            if (i != ut::kUtPadEdit && memcmp(&o0.btn[i], &o1.btn[i], sizeof(ut::UtRectF)) != 0) same = false;
        const ut::UtRect k0 = ut::utPadLabelBox(fit, false), k1 = ut::utPadLabelBox(fit, true);
        const int step = fit.stepW + fit.gap;
        const ut::UtRectF& e = o1.btn[ut::kUtPadEdit];
        _snprintf_s(d, sizeof(d), _TRUNCATE, "step %d: EDIT %.0f..%.0f, label %d / %d", step, e.x, e.x + e.w, k0.x, k1.x);
        ok(fit.ok && same && o0.btn[ut::kUtPadEdit].w == 0.0f && e.x == (float)(r.x + ut::kUtPadMargin + 7 * step) &&
               e.x + e.w == (float)(r.x + ut::kUtPadMargin + 8 * step - fit.gap) && k1.x == k0.x + step &&
               k1.x + k1.w == k0.x + k0.w && (float)k0.x == o0.label.x && (float)k1.x == o1.label.x &&
               e.x + e.w < o1.label.x && ut::kUtPadEdit == ut::kUtPadGroup0 + ut::kUtPadCells &&
               ut::kUtPadMax == ut::kUtPadEdit + 1 && !ut::utPadIsCell(ut::kUtPadEdit),
           "a fitted pad (27,512,512,60): EDIT in cell 7, the label box one cell later, nothing else moves; "
           "kUtPadEdit appended after the buttons (no index renumbered)",
           d);
    }
    {   // a player's pad through utLayoutResolve: SVAERA's pad_565x637=0,606,565,31 with its
        // grid_565x637=11,91,544,448 (18 groups, the widest caption 6), drawn as the panel draws it
        const float scales[3] = {0.8f, 1.0f, 1.25f};
        int good = 0;
        char why[240] = "", at1[160] = "";
        for (float s : scales) {
            ut::UtRectF f, g;
            const bool pg = page(s, 1920.0f, 1080.0f, &f, &g);
            ut::UtLayoutSource off = source(f, g, s, 1920.0f, 1080.0f, "0,606,565,31", nullptr);
            off.gridKey = "11,91,544,448";
            off.groups = 18;
            off.widest = 6;
            ut::UtLayoutSource on = off;
            on.edit = true;
            static ut::UtLayoutLive a, b;
            const bool la = pg && ut::utLayoutFromSource(off, &a), lb = pg && ut::utLayoutFromSource(on, &b);
            const bool inForce = la && lb && a.L.user[ut::kUtLayPad] && b.L.user[ut::kUtLayPad] &&
                                 !a.L.padDerived && !b.L.padDerived && a.L.pad.ok && b.L.pad.ok &&
                                 a.L.labelInPad == 1 && b.L.labelInPad == 1 &&
                                 ut::utRectEq(a.L.rect[ut::kUtLayPad], b.L.rect[ut::kUtLayPad]) &&
                                 ut::utRectEq(a.L.rect[ut::kUtLayGrid], b.L.rect[ut::kUtLayGrid]);
            static ut::UtPadOut oa, ob;
            const float ox = ut::utPadRound(f.x), oy = ut::utPadRound(f.y);
            const bool da = inForce && ut::utPadOutFromFit(a.L.pad, 0, ox, oy, s, true, false, &oa);
            const bool db = inForce && ut::utPadOutFromFit(b.L.pad, 0, ox, oy, s, true, true, &ob);
            int moved = (da && db) ? 0 : -1;
            for (int i = 0; i < ut::kUtPadMax && moved >= 0; ++i)
                if (i != ut::kUtPadEdit && memcmp(&oa.btn[i], &ob.btn[i], sizeof(ut::UtRectF)) != 0) ++moved;
            const bool same = moved == 0 && !memcmp(&oa.ground, &ob.ground, sizeof(oa.ground)) &&
                              !memcmp(&oa.fieldText, &ob.fieldText, sizeof(oa.fieldText));
            const ut::UtRectF& e = ob.btn[ut::kUtPadEdit];
            const ut::UtRectF& fld = ob.btn[ut::kUtPadSearch];
            const bool editOk = oa.btn[ut::kUtPadEdit].w == 0.0f && e.w > 0.0f && e.x >= fld.x + fld.w &&
                                e.y == fld.y && e.h == fld.h && ut::utRectInside(e, ob.ground, 0.0f);
            const bool labelOk = ob.label.x > e.x + e.w && ob.label.x > oa.label.x &&
                                 ob.label.x + ob.label.w == oa.label.x + oa.label.w && ob.label.y == oa.label.y &&
                                 ob.label.h == oa.label.h;
            if (s == 1.0f)
                _snprintf_s(at1, sizeof(at1), _TRUNCATE, "UI 1.0: EDIT %.0f..%.0f (%.0f x %.0f), label %.0f..%.0f "
                            "(%.0f without EDIT)", e.x, e.x + e.w, e.w, e.h, ob.label.x, ob.label.x + ob.label.w,
                            oa.label.x);
            // the measured numbers at UI 1.0 on the 1920 x 1080 canvas (the frame at x 677)
            const bool pin = s != 1.0f || (e.x == 939.0f && e.w == 36.0f && e.h == 12.0f && ob.label.x == 979.0f &&
                                           ob.label.x + ob.label.w == 1239.0f && oa.label.x == 942.0f);
            if (inForce && same && editOk && labelOk && pin) ++good;
            else if (!why[0])
                _snprintf_s(why, sizeof(why), _TRUNCATE,
                            "scale %.2f: in force %d same %d (moved %d) edit %d label %d pin %d - EDIT %.0f,%.0f %.0fx%.0f, "
                            "label %.0f / %.0f",
                            s, inForce, same, moved, editOk, labelOk, pin, e.x, e.y, e.w, e.h, ob.label.x, oa.label.x);
        }
        ok(good == 3,
           "a player's pad in force (SVAERA's pad_565x637=0,606,565,31, 18 groups) at UI 0.8 / 1.0 / 1.25: "
           "edit_button=1 lays out EDIT inside the pad after the field and moves only the label's left edge; "
           "no other button, the ground or the field text moves",
           why[0] ? why : at1);
    }

    // the relic pages: their own area (relics_<W>x<H>), else the collection's grid in force
    {
        ok(ut::utRelicGroupIs("relics") && ut::utRelicGroupIs("RELICS") && !ut::utRelicGroupIs("relic") &&
               !ut::utRelicGroupIs("relicsx") && !ut::utRelicGroupIs("") && !ut::utRelicGroupIs(nullptr),
           "the relic pages' group is the G line id relics (any case), nothing else");
        ut::UtCollGridKeys k = ut::utPageGridKeys(1, 0, "24,124,512,480", "40,150,480,416");
        ok(k.page && !strcmp(k.page, "24,124,512,480") && k.coll == k.page && !k.own && !k.relic,
           "a unique page: the grid line for the page and the collection; the relics line is not read");
        k = ut::utPageGridKeys(1, 1, "24,124,512,480", "40,150,480,416");
        ok(k.page && !strcmp(k.page, "40,150,480,416") && k.coll && !strcmp(k.coll, "24,124,512,480") && k.own &&
               k.relic,
           "a relic page with a relics line: the page takes it, the collection keeps the grid line (its own "
           "resolution: the uniques stay packed where they were)");
        k = ut::utPageGridKeys(1, 1, "24,124,512,480", "");
        ok(k.page && !strcmp(k.page, "24,124,512,480") && !k.own && !k.relic,
           "a relic page without a relics line: the collection's grid line, one resolution");
        k = ut::utPageGridKeys(1, 1, nullptr, nullptr);
        ok(!k.page && !k.coll && !k.own && !k.relic, "neither line: the derived grid on both kinds");
        k = ut::utPageGridKeys(0, 1, "24,124,512,480", "40,150,480,416");
        ok(!k.page && k.coll && !strcmp(k.coll, "24,124,512,480") && k.own && !k.relic,
           "the view OFF: the real page keeps its own grid, the relics line is not read (utCollGridKeys)");
        const ut::UtRect coll = {27, 126, 512, 480};
        ut::UtRelicArea a = ut::utRelicAreaFromLine(nullptr, coll, 16, 15, 565, 637);
        ok(!a.user && ut::utRectEq(a.rect, coll) && a.cols == 16 && a.rows == 15,
           "no relics line: the relic area is the collection's grid in force (16 x 15)");
        a = ut::utRelicAreaFromLine("40,150,480,416", coll, 16, 15, 565, 637);
        ok(a.user && a.rect.x == 40 && a.rect.y == 150 && a.cols == 15 && a.rows == 13,
           "relics_565x637=40,150,480,416: its whole cells, 15 x 13");
        a = ut::utRelicAreaFromLine("40,150,500,430", coll, 16, 15, 565, 637);
        ok(a.user && a.cols == 15 && a.rows == 13, "a rectangle off the cell: whole cells from its top-left (15 x 13)");
        a = ut::utRelicAreaFromLine("not,a,rect", coll, 20, 19, 660, 699);
        ok(!a.user && a.cols == 20 && a.rows == 19, "a value that does not parse counts as no line");
        a = ut::utRelicAreaFromLine("0,0,565,637", coll, 16, 15, 565, 637);
        ok(a.user && a.cols == 17 && a.rows == 19 && a.cols * a.rows <= ut::utLayoutBoxCap(565, 637),
           "the whole window: 17 x 19, under the window's box cap");
        ut::UtGridForceIn in = {};
        in.on = 1;
        in.coverW = 565;
        in.coverH = 637;
        in.frame = ut::UtRectF{10.0f, 0.0f, 565.0f, 637.0f};
        in.frameGrid = ut::UtRectF{37.0f, 126.0f, 512.0f, 480.0f};
        in.scale = 1.0f;
        in.cell = 32.0f;
        in.derivedCols = 16;
        in.derivedRows = 15;
        const ut::UtGridForce f = ut::utGridInForce(in);
        ut::UtGridForce r = ut::utRelicGridInForce(in, f, 0, coll);
        ok(r.packUser == f.packUser && r.drawnCols == 16 && r.drawnRows == 15 && r.drawn.x == f.drawn.x &&
               r.drawn.y == f.drawn.y,
           "a relic page without a relics line: the collection's grid in force, unchanged");
        r = ut::utRelicGridInForce(in, f, 1, ut::UtRect{40, 150, 480, 416});
        ok(r.packUser && r.drawnUser && r.packCols == 15 && r.packRows == 13 && r.drawn.x == 50.0f &&
               r.drawn.y == 150.0f && r.drawn.w == 480.0f && r.drawn.h == 416.0f && r.dx == 13.0f && r.dy == 24.0f,
           "a relics line: the page and the sack take its whole cells from its top-left (15 x 13 at 50,150; "
           "13 / 24 px from the derived grid)");
        ut::UtGridForceIn ug = in;
        ug.userSet = 1;
        ug.userRect = ut::UtRect{24, 124, 512, 480};
        const ut::UtGridForce fu = ut::utGridInForce(ug);
        const ut::UtGridForce ru = ut::utRelicGridInForce(ug, fu, 1, ut::UtRect{40, 150, 480, 416});
        const ut::UtGridForce rn = ut::utRelicGridInForce(ug, fu, 0, ug.userRect);
        ok(fu.packUser && fu.packCols == 16 && fu.packRows == 15 && ru.packCols == 15 && ru.packRows == 13 &&
               rn.packCols == 16 && rn.drawn.x == fu.drawn.x && rn.drawn.y == fu.drawn.y,
           "a grid_ line and a relics line: the uniques' packing stays 16 x 15, the relic page takes 15 x 13; "
           "with no relics line it shows the player's grid");
        ut::UtGridForceIn half = in;
        half.scale = 1.375f;
        half.cell = 44.0f;
        const ut::UtGridForce rh = ut::utRelicGridInForce(half, ut::utGridInForce(half), 1, ut::UtRect{40, 150, 480, 416});
        ok(rh.packCols == 15 && rh.packRows == 13 && rh.drawn.w == 660.0f && rh.drawn.h == 572.0f,
           "at UI scale 1.375 the same 15 x 13 cells at the drawn 44 px cell");
    }

    printf("\nN. the relic and charm page's button: one more button on the pad\n");
    {
        const ut::UtPadFit f16 = ut::utPadFitRect(ut::UtRect{27, 40, 512, 40}, 16, 1, 1.0f, 5);
        const ut::UtPadFit f17 = ut::utPadFitRect(ut::UtRect{27, 40, 512, 40}, 17, 1, 1.0f, 5);
        _snprintf_s(d, sizeof(d), _TRUNCATE, "16: cols %d cell %d pages %d; 17: cols %d cell %d pages %d", f16.cols,
                    f16.cellW, f16.pages.pages, f17.cols, f17.cellW, f17.pages.pages);
        ok(f16.ok && f17.ok && f17.pages.pages == f16.pages.pages && f17.cellH == f16.cellH && f17.cellW >= 20,
           "512 x 40: the base game's 16 groups and the relic page's button fit as the 16 did", d);
    }

    printf("\nN. the relic and charm page's label (its own short form)\n");
    {
        ut::UtPadRelicLabel r;
        r.first = 5; r.last = 8; r.blocks = 93; r.page = 2; r.pages = 23;
        r.bonuses = 12; r.bonusSlots = 1971; r.pieces = 7;
        char line[256], dd[200];
        int fs = ut::utPadLabelRelic(r, 4000.0f, 12, line, sizeof(line));
        ok(fs == 12 && !strcmp(line, "5-8/93  page 2/23  12/1971  pieces 7"),
           "a wide box: blocks first-last/blocks, the fixed page, bonuses k/N, pieces n - and no page name", line);
        ok(!strstr(line, "all") && !strstr(line, "owned") && !strstr(line, "Relic"),
           "no collection count (all N / M) and no group name on the relic page", line);
        {
            ut::UtPadRelicLabel u = r;
            u.first = 64; u.last = 67; u.page = 17; u.bonuses = 8; u.pieces = 50;
            char nl[256];
            ut::utPadLabelRelic(u, 4000.0f, 12, nl, sizeof(nl));
            const float wNew = ut::utTmWidth(ut::kUtTmLabel, nl, 12);
            const float wOld =
                ut::utTmWidth(ut::kUtTmLabel, "Relics and Charms 64-67/93  page 17/23  bonuses 8/1971  pieces 50", 12);
            _snprintf_s(dd, sizeof(dd), _TRUNCATE, "\"%s\" %.0f px, the old compact form %.0f px", nl, wNew, wOld);
            ok(!strcmp(nl, "64-67/93  page 17/23  8/1971  pieces 50") && wNew < wOld * 0.7f,
               "the whole line needs well under 0.7 of the old compact form's box", dd);
        }
        const float w1 = ut::utTmWidth(ut::kUtTmLabel, "5-8/93  12/1971  pieces 7", 12);
        fs = ut::utPadLabelRelic(r, w1, 12, line, sizeof(line));
        ok(fs == 12 && !strcmp(line, "5-8/93  12/1971  pieces 7"),
           "a narrower box: the fixed page dropped first (the blocks say where)", line);
        const float w2 = ut::utTmWidth(ut::kUtTmLabel, "12/1971  pieces 7", 12);
        fs = ut::utPadLabelRelic(r, w2, 12, line, sizeof(line));
        ok(fs == 12 && !strcmp(line, "12/1971  pieces 7"), "narrower: the counts only", line);
        const float w3 = ut::utTmWidth(ut::kUtTmLabel, "12/1971", 12);
        fs = ut::utPadLabelRelic(r, w3, 12, line, sizeof(line));
        ok(fs == 12 && !strcmp(line, "12/1971"), "narrower: the bonuses only", line);
        fs = ut::utPadLabelRelic(r, w3 * 0.6f, 12, line, sizeof(line));
        ok(fs < 12 && fs >= 6 && !strcmp(line, "12/1971") && (fs == 6 || ut::utPadLabelFits(line, fs, w3 * 0.6f)),
           "a box no form fits at the size: the last form at a smaller size", line);
        r.found = "found 3";
        r.brief = "=3";
        fs = ut::utPadLabelRelic(r, 4000.0f, 12, line, sizeof(line));
        ok(fs == 12 && !strcmp(line, "5-8/93  page 2/23  12/1971  pieces 7  found 3"),
           "a query standing: the collection pages' found N ends the line", line);
        const float w5 = ut::utTmWidth(ut::kUtTmLabel, "5-8/93  page 2/23  12/1971  pieces 7  =3", 12);
        fs = ut::utPadLabelRelic(r, w5, 12, line, sizeof(line));
        ok(fs == 12 && !strcmp(line, "5-8/93  page 2/23  12/1971  pieces 7  =3"),
           "a query standing: the short =N before any part is dropped", line);
        const float w4 = ut::utTmWidth(ut::kUtTmLabel, "12/1971  =3", 12);
        fs = ut::utPadLabelRelic(r, w4, 12, line, sizeof(line));
        ok(fs == 12 && !strcmp(line, "12/1971  =3"), "a query standing, a narrow box: the short form =N", line);
        r.found = "indexing 40/4649";
        r.brief = "40/4649";
        fs = ut::utPadLabelRelic(r, 4000.0f, 12, line, sizeof(line));
        ok(fs == 12 && strstr(line, "pieces 7  indexing 40/4649") != nullptr,
           "while the index is read: indexing k/n", line);
        ut::UtPadRelicLabel z;
        z.bonusSlots = 1971;
        fs = ut::utPadLabelRelic(z, 4000.0f, 12, line, sizeof(line));
        ok(fs == 12 && !strcmp(line, "0/0  page 0/0  0/1971  pieces 0"),
           "no block laid out (OWN with nothing held): 0/0", line);
    }

    printf("\nthe relic page's look and the band lines\n");
    {
        ut::UtLayout L = {};
        ok(ut::utPadBandLinesWanted(L), "the derived pad's band lines are said while the pad is in the window", "");
        L.screen[ut::kUtLayPad] = 1;
        ok(!ut::utPadBandLinesWanted(L), "no band line while the pad is pinned on the screen", "");
    }
    {
        ut::UtRelicRow r;
        r.relicBitmap = "XPack\\Items\\Relic\\a.tex";
        r.shardBitmap = "XPack\\Items\\Relic\\a_shard.tex";
        ok(!strcmp(ut::utRelicGrayIcon(r, 1), r.shardBitmap.c_str()) &&
               !strcmp(ut::utRelicGrayIcon(r, 2), r.relicBitmap.c_str()) &&
               !strcmp(ut::utRelicGrayIcon(r, 7), r.relicBitmap.c_str()) && !ut::utRelicGrayIcon(r, 0),
           "gray icon by ICON path: the red slot the shard bitmap, a green slot the relic bitmap, a unique none", "");
        r.shardBitmap.clear();
        ok(!ut::utRelicGrayIcon(r, 1), "a row with no shard bitmap: no gray copy for its red slot", "");
        ok(ut::utRelicProtoLit(1, 42) && ut::utRelicProtoLit(3, 7) && !ut::utRelicProtoLit(2, 0) &&
               !ut::utRelicProtoLit(0, 9),
           "a relic slot built from a row is lit (its own colours); the synthetic one is empty (gray)", "");
        ok(ut::utRelicMarksStyle(true, false, 3, true) == 3 && ut::utRelicMarksStyle(false, false, 3, true) == 0 &&
               ut::utRelicMarksStyle(false, true, 3, true) == 3 && ut::utRelicMarksStyle(false, true, 3, false) == 0 &&
               ut::utRelicMarksStyle(false, true, 0, true) == 0,
           "a relic slot keeps its gray copy with only the journal set known (no frame of colour before "
           "the owned recount); an item slot waits for the trusted marks; never with owned_marks 0", "");
        // OWN: the marks are not trusted (marksNow false) - a held block's slots still follow
        // owned_marks 3, so the empty one (the synthetic item) is gray, the stored one lit
        const int own = ut::utRelicMarksStyle(false, true, 3, true);
        ok(own == 3 && !ut::utRelicProtoLit(2, 0) && ut::utRelicProtoLit(2, 11) &&
               ut::utRelicMarksStyle(false, false, 3, true) == 0,
           "OWN on the relic page: empty slots of a held block stay gray, only slots with a count are "
           "coloured; an item page's marks still wait for the trusted ones", "");
    }
    {   // the block panels (relic_panels) on a 5 x 9 area (3 bonuses a line after the gap column): a
        // relic block of three rows (the second wraps), a charm block of one shard (a dead red cell),
        // a relic block of two bonuses
        ut::UtRelicList l;
        const char parts[3] = {'R', 'C', 'R'};
        const int rowsOf[3] = {3, 1, 1};
        const int bonusN[5] = {2, 4, 3, 1, 2};
        const unsigned shardN[5] = {3, 3, 3, 1, 2};
        int r = 0;
        for (int b = 0; b < 3; ++b) {
            ut::UtRelicBlock bk;
            bk.part = parts[b];
            bk.firstRow = r;
            bk.rows = rowsOf[b];
            l.blocks.push_back(bk);
            for (int k = 0; k < rowsOf[b]; ++k, ++r) {
                ut::UtRelicRow row;
                row.block = b;
                row.shards = shardN[r];
                for (int i = 0; i < bonusN[r]; ++i) row.bonuses.push_back("b");
                l.rows.push_back(row);
            }
        }
        ut::UtRelicLayout y;
        ut::UtRelicPlace pl[64];
        ut::UtRelicPanel pn[8];
        const bool built = ut::utRelicLayoutBuild(l, ut::utRelicGeometryOf(5, 9, 1, 1), &y);
        int np = built ? ut::utRelicWindowPlaces(l, y, 0, pl, 64) : 0;
        int n = ut::utRelicPanelsOf(l, pl, np, 0, pn, 8);
        ok(built && n == 3 && pn[0].block == 0 && pn[0].part == 'R' && pn[0].pos == 0 && pn[0].col == 0 &&
               pn[0].line == 0 && pn[0].cols == 5 && pn[0].lines == 4 && pn[1].block == 1 &&
               pn[1].part == 'C' && pn[1].pos == 1 && pn[1].col == 0 && pn[1].line == 5 && pn[1].cols == 3 &&
               pn[1].lines == 1 && pn[2].block == 2 && pn[2].line == 7 && pn[2].cols == 4 && pn[2].lines == 1,
           "one panel a block: column 0 to its widest line, its first line to its last (a wrapped row's "
           "two lines, a dead red cell inside)", "");
        np = ut::utRelicWindowPlaces(l, y, 1, pl, 64);
        n = ut::utRelicPanelsOf(l, pl, np, 1, pn, 8);
        ok(n == 2 && pn[0].block == 1 && pn[0].pos == 1 && pn[0].line == 0 && pn[1].block == 2 &&
               pn[1].pos == 2 && pn[1].line == 2 && ut::utRelicPanelTone(pn[1].part, pn[1].pos) == 0,
           "a notch of the wheel: the panels follow their blocks and keep their tones", "");
        ut::UtRelicLayout tall;
        const bool tallBuilt = ut::utRelicLayoutBuild(l, ut::utRelicGeometryOf(5, 3, 1, 1), &tall);
        np = tallBuilt ? ut::utRelicWindowPlaces(l, tall, 0, pl, 64) : 0;
        n = ut::utRelicPanelsOf(l, pl, np, 0, pn, 8);
        ok(tallBuilt && n == 1 && pn[0].lines == 3 && pn[0].cols == 5 &&
               ut::utRelicPanelsOf(l, pl, np, 0, pn, 0) == 0,
           "a block taller than the area: its panel ends at the area's last line; no room, no panel", "");
        bool alt = true;
        for (int p = 0; p < 6; ++p)
            alt = alt && ut::utRelicPanelTone('R', p) != ut::utRelicPanelTone('R', p + 1) &&
                  ut::utRelicPanelTone('C', p) != ut::utRelicPanelTone('C', p + 1) &&
                  (ut::utRelicPanelTone('R', p) >> 1) == 0 && (ut::utRelicPanelTone('C', p) >> 1) == 1;
        ok(alt, "two tones alternate between neighbouring blocks; relic and charm blocks in their own hue", "");
        ok(ut::utRelicEdgePx(0.8f) == 1 && ut::utRelicEdgePx(1.0f) == 1 && ut::utRelicEdgePx(1.375f) == 1 &&
               ut::utRelicEdgePx(1.5f) == 2 && ut::utRelicEdgePx(0.2f) == 1,
           "the count's dark edge and the panel frame: 1 px at UI scale 0.8 to 1.375, the scale rounded above", "");
        const ut::UtLatticeGrid lat = {1, 27, 126, 16, 15};
        const ut::UtLatticeGrid ga = ut::utRelicLatticeOf(true, true, lat);
        const ut::UtLatticeGrid gb = ut::utRelicLatticeOf(true, false, lat);
        const ut::UtLatticeGrid gc = ut::utRelicLatticeOf(false, true, lat);
        ok(ga.user == 1 && ga.cols == 0 && ga.rows == 0 && ga.x == 0 && ga.y == 0 && gb.cols == 16 &&
               gb.x == 27 && gc.cols == 16 && gc.rows == 15,
           "a relic page with the grid cover: the lattice erased (0 x 0 cells, one copy a background); "
           "grid_cover=0 or an item page: the grid in force", "");
    }
    {   // a 6 x 4 area: a block of two lines (red at 0, gap column 1, bonuses from 2), a dead red cell
        ut::UtRelicPlace pl[] = {
            {0, 0, ut::kUtRelicSlotRed, 0, 0},   {0, 1, ut::kUtRelicSlotGreen, 2, 0},
            {0, 2, ut::kUtRelicSlotGreen, 3, 0}, {0, 3, ut::kUtRelicSlotGreen, 4, 0},
            {1, 0, ut::kUtRelicSlotRed, 0, 1},   {1, 1, ut::kUtRelicSlotGreen, 2, 1},
            {2, 0, ut::kUtRelicSlotDead, 0, 3},  {2, 1, ut::kUtRelicSlotGreen, 2, 3},
            {3, 1, ut::kUtRelicSlotGreen, 9, 9},   // outside the area: ignored
        };
        unsigned char occ[6 * 4];
        ut::utRelicOccupancy(pl, (int)(sizeof(pl) / sizeof(pl[0])), 6, 4, occ);
        ut::UtRelicRun runs[32];
        const int n = ut::utRelicGroundRuns(occ, 6, 4, runs, 32);
        // line 0: col 1, col 5; line 1: col 1, cols 3-5; line 2: all 6; line 3: cols 0-1, cols 3-5
        const bool shape = n == 7 && runs[0].col == 1 && runs[0].w == 1 && runs[1].col == 5 &&
                           runs[2].line == 1 && runs[2].col == 1 && runs[3].col == 3 && runs[3].w == 3 &&
                           runs[4].line == 2 && runs[4].w == 6 && runs[5].line == 3 && runs[5].col == 0 &&
                           runs[5].w == 2 && runs[6].col == 3 && runs[6].w == 3;
        int cells = 0;
        for (int i = 0; i < n; ++i) cells += runs[i].w;
        ok(shape && cells == 24 - 7,
           "the ground covers every cell but the slots: the gap column, the empty line, short rows, a dead red cell",
           "");
        ok(ut::utRelicGroundRuns(occ, 6, 4, runs, 3) == 3, "the runs stop at the array's end", "");
    }

    printf(g_fail ? "[layout]  FAILED: %d of %d\n" : "[layout]  ALL PASS (%d checks)\n",
           g_fail ? g_fail : g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
