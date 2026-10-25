// test_layoutedit.cpp - the in-game layout editor's pure half (src\ut_layoutedit.h), offline. NO
// GAME, NO FILE WRITTEN, NO WINDOWS: the header is included, so this runs exactly the code the mod
// runs. GD's cases that apply to Titan Quest:
//
//   1. the handle size, the toolbar, the minimum sizes;
//   2. every clamp of a drag: a move at each edge of the caravan window, each edge and a corner,
//      the minimum sizes; a sweep of drags never leaves the window, a drag passes over the grid
//      and only where it ENDS is it judged (a part of the player's over the grid is refused);
//   3. the button a drag polls (swapped or not) and the one-shot key;
//   4. the open: refused at a bad scale, with no page, twice; it opens on the real Transfer page;
//      the verdict (an item on the cursor refuses it);
//   5. the hit test: the toolbar, corners, edge strips, bodies, the label over the pad, the grid
//      only while it can be set, nothing outside; the points the editor owns;
//   6. every transition: LEFT DOWN (pass, drag, SAVE, SAVE refused, CANCEL, RESET), an item on the
//      cursor (only CANCEL answers), the polled drag and its release, LEFT UP, every cancel rule of
//      the tick, the save handover (read back, too slow, a failed write), a click during a drag;
//   7. the check through utLayoutResolve, the label following the pad, what SAVE writes, and the
//      round trip: the lines SAVE writes, read back by utLayoutFromSource, give the edited
//      rectangles at every UI scale;
//   8. the header's purity;
//   9. the label put back into the pad over its label box is the pad's own box again;
//  10. the grid's handle and body (while the grid can be set): its hit test, the cell snap, the
//      floor, the clamp to the window, grid_<W>x<H> in what SAVE writes;
//  11. the screen mode: a pad and a label dragged out of the window (pad_screen / label_screen),
//      the nearest corner, the round trip through utLayoutFromSource, a screen part over the grid
//      refused, back into the window, RESET, the grounds the screen input reads;
//  12. the editor's view of the grid: the shown group's cells as ghost lines (utEditGhost) that
//      follow the drag, and the page's items hidden while it is open (utEditHidesItems), back at
//      SAVE's read-back, CANCEL and every close of the tick;
//  13. the game's own Transfer page: a part placed over the real cells passes wherever the
//      collection page keeps it (judged against the collection's grid line), the grid stays fixed
//      and SAVE never writes it.
// Exit code 0 = all of it held. Argument 1: the repository root (for the purity check).
#include <stdio.h>
#include <string.h>

#include "../src/ut_layoutedit.h"

using namespace ut;

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

bool eq(const UtRect& r, int x, int y, int w, int h) {
    return r.x == x && r.y == y && r.w == w && r.h == h;
}

// The measured page of the sweep (test_layout's): the frame 565 x 637 x s centred on the canvas,
// the page at + (0,126) s, the grid at the page + (27,0) s, 16 x 15 of the engine's cell.
float g_ox = 0.0f, g_oy = 0.0f;   // the last page's frame origin on the canvas (rounded)

bool page(float s, float cw, float ch, UtRectF* f, UtRectF* g) {
    UtFrameIn in;
    in.scale = s;
    in.canvasW = cw;
    in.canvasH = ch;
    in.originX = utPadFloor((cw - 565.0f * s) * 0.5f);
    in.originY = (ch - 637.0f * s) * 0.5f + 126.0f * s;
    in.gridX = in.originX + 27.0f * s;
    in.gridY = in.originY;
    in.cellW = utEngineCellPx(s);
    in.cellH = in.cellW;
    in.pageX = 0.0f;
    in.pageY = 126.0f;
    in.frameW = 565.0f;
    in.frameH = 637.0f;
    const bool okf = utFrameFromHover(in, f, g) == kUtFrameOk;
    if (okf) {
        g_ox = utPadRound(f->x);
        g_oy = utPadRound(f->y);
    }
    return okf;
}

// The live layout at scale s for the two [layout] values (null = no line), as the panel builds it.
bool live(float s, const char* pad, const char* label, UtLayoutLive* out,
          const char* padScreen = nullptr, const char* labelScreen = nullptr, const char* gridKey = nullptr) {
    UtRectF f, g;
    if (!page(s, 2560.0f, 1440.0f, &f, &g)) return false;
    UtLayoutSource src;
    src.frame = f;
    src.grid = g;
    src.scale = s;
    src.canvasW = 2560.0f;
    src.canvasH = 1440.0f;
    src.coverW = 565;
    src.coverH = 637;
    src.pad = pad;
    src.label = label;
    src.groups = 15;
    src.widest = 5;
    src.labelPx = (int)utPadRound(13.0f * s) < 6 ? 6 : (int)utPadRound(13.0f * s);
    src.search = true;
    src.padScreen = padScreen;
    src.labelScreen = labelScreen;
    src.screenKnown = 1;
    src.gridKey = gridKey;
    return utLayoutFromSource(src, out);
}

UtEditOpenIn openIn(const UtLayoutLive& lay, float s, int group, unsigned now, int gridMovable = 0,
                    int screenKnown = 0) {
    UtEditOpenIn o = {};
    o.maxCellW = o.maxCellH = o.cellW = o.cellH = 32;
    o.rowGap = 0;
    o.gridMovable = gridMovable;
    for (int p = 0; p < kUtLayParts; ++p) o.screenIn[p] = lay.L.screen[p];
    o.screenKnown = screenKnown;
    o.screenW = 2560;
    o.screenH = 1440;
    o.originX = g_ox;
    o.originY = g_oy;
    for (int p = 0; p < kUtLayParts; ++p) {
        o.inForce[p] = lay.L.rect[p];
        o.def[p] = lay.in.def[p];
    }
    if (!lay.L.pad.ok) o.inForce[kUtLayPad] = UtRect{0, 0, 0, 0};
    o.plate = lay.in.plate;
    o.labelInPad = lay.L.labelInPad;
    o.groups = lay.in.groups;
    o.gap = lay.in.gap;
    o.widest = lay.in.widest;
    o.scale = s;
    o.coverW = 565;
    o.coverH = 637;
    o.group = group;
    o.now = now;
    return o;
}

// The centre of a record rectangle in screen px (frame-local).
float cx(const UtRect& r, float s) { return ((float)r.x + (float)r.w * 0.5f) * s; }
float cy(const UtRect& r, float s) { return ((float)r.y + (float)r.h * 0.5f) * s; }

UtEdTick tickAt(const UtEdit& e, unsigned now) {
    UtEdTick t = {};
    t.now = now;
    t.group = e.group;
    t.scale = e.scale;
    t.focused = 1;
    t.coverW = e.coverW;
    t.coverH = e.coverH;
    t.layoutOk = 1;
    return t;
}

// The value SAVE writes for `key` (null = the line is removed or not in the plan).
const char* planValue(const UtEditSavePlan& plan, const char* key) {
    for (int i = 0; i < plan.n; ++i)
        if (!strcmp(plan.k[i].key, key)) return plan.k[i].set ? plan.k[i].value : nullptr;
    return nullptr;
}

bool planHas(const UtEditSavePlan& plan, const char* key) {
    for (int i = 0; i < plan.n; ++i)
        if (!strcmp(plan.k[i].key, key)) return true;
    return false;
}

// SAVE's lines read back: the layout the live page resolves from them.
bool readBack(const UtEdit& e, float s, UtLayoutLive* out) {
    UtEditSavePlan plan;
    utEditSavePlanOf(e, "565x637", &plan);
    return live(s, planValue(plan, "pad_565x637"), planValue(plan, "label_565x637"), out,
                planValue(plan, "pad_screen"), planValue(plan, "label_screen"));
}

UtEdit dragged(const UtEdit& g, int part, int edges, float dx, float dy) {
    UtEdit d = g;
    d.state = kUtEdDrag;
    d.part = part;
    d.edges = edges;
    d.x0 = d.y0 = 0.0f;
    d.start = d.rect[part];
    utEditDragTo(&d, dx, dy);
    return d;
}

}  // namespace

// RESET while the pad and the label are on the screen: the derived set with every part back in the
// window (the grid keeps the pad's band: a 20 x 20 caravan area becomes 20 x 19) - one consistent set
// (the pad in the band below the grid, nothing red, SAVE removes the grid's line).
void resetInWindow() {
    printf("\nRESET with the pad on the screen (the grid keeps its band)\n");
    UtEditOpenIn in = {};
    // a custom game's 660 x 699 window at UI scale 1.375: the area 11,57 20 x 20 while the pad and the label are on
    // the screen (band 0); the derived pad then lies below it, over the window's bottom border
    in.def[kUtLayGrid] = UtRect{11, 57, 640, 640};
    in.def[kUtLayPad] = UtRect{67, 697, 529, 26};
    in.def[kUtLayLabel] = UtRect{345, 713, 248, 12};
    in.defWinOk = 1;
    in.defWin[kUtLayGrid] = UtRect{11, 57, 640, 608};
    in.defWin[kUtLayPad] = UtRect{67, 665, 529, 26};
    in.defWin[kUtLayLabel] = UtRect{345, 681, 248, 12};
    in.inForce[kUtLayGrid] = in.def[kUtLayGrid];
    in.inForce[kUtLayPad] = UtRect{-154, -132, 675, 176};
    in.inForce[kUtLayLabel] = UtRect{702, -103, 358, 84};
    in.screenIn[kUtLayPad] = in.screenIn[kUtLayLabel] = 1;
    in.screenKnown = 1;
    in.screenW = 3440;
    in.screenH = 1440;
    in.originX = 807.0f;
    in.originY = 239.0f;
    in.plate = UtRect{0, 0, 660, 699};
    in.maxCellW = in.maxCellH = in.cellW = in.cellH = 32;
    in.gridMovable = 1;
    in.labelInPad = 0;
    in.groups = 19;
    in.gap = 0;
    in.widest = 5;
    in.scale = 1.375f;
    in.coverW = 660;
    in.coverH = 699;
    in.group = 3;
    UtEdit e = {};
    ok(utEditOpen(&e, in), "opened with the pad and the label on the screen");
    ok(utEditPartDerived(e, kUtLayGrid) && !e.resetWin,
       "at the open the 20 x 20 area is the derived grid (SAVE with nothing moved removes its line)");
    // RESET (the toolbar's third button)
    const UtRect rb = e.tool[kUtEdReset];
    const float x = ((float)rb.x + 2.0f) * e.scale, y = ((float)rb.y + 2.0f) * e.scale;
    const int act = utEditLeftDown(&e, x, y, 1, 1, 0, 0);
    ok(act == kUtEdActReset, "RESET pressed");
    ok(utRectEq(e.rect[kUtLayGrid], UtRect{11, 57, 640, 608}) && utRectEq(e.rect[kUtLayPad], UtRect{67, 665, 529, 26}),
       "RESET: the grid keeps its band (20 x 19), the pad lies in the band below it");
    ok(!e.scr[kUtLayPad] && !e.scr[kUtLayLabel] && e.labelInPad && e.resetWin,
       "RESET: the pad and the label back in the window (the label the pad's own box)");
    ok(e.rect[kUtLayPad].y >= e.rect[kUtLayGrid].y + e.rect[kUtLayGrid].h,
       "RESET: the pad does not overlap the grid (no red pixels at its top edge)");
    ok(utEditPartDerived(e, kUtLayGrid) && utEditPartDerived(e, kUtLayPad) && utEditPartDerived(e, kUtLayLabel),
       "RESET: all three are the derived set - SAVE removes the grid's, the pad's and the label's lines");
    // without an in-window set (the pad in the window at the open) RESET is today's
    UtEditOpenIn in2 = in;
    in2.defWinOk = 0;
    UtEdit e2 = {};
    utEditOpen(&e2, in2);
    utEditLeftDown(&e2, x, y, 1, 1, 0, 0);
    ok(utRectEq(e2.rect[kUtLayGrid], in.def[kUtLayGrid]) && !e2.resetWin,
       "no in-window set given: RESET brings the open's derived set back");
}

int main(int argc, char** argv) {
    printf("test_layoutedit - the layout editor (src\\ut_layoutedit.h)\n");
    UtLayoutLive lay;
    const bool have = live(1.0f, nullptr, nullptr, &lay);
    ok(have && eq(lay.in.def[kUtLayPad], 33, 606, 500, 31) && eq(lay.in.def[kUtLayLabel], 270, 622, 260, 12) &&
           eq(lay.in.def[kUtLayGrid], 27, 126, 512, 480),
       "the vanilla page at UI scale 1: grid 27,126,512,480, pad 33,606,500,31, label 270,622,260,12");

    printf("\n1. handles, toolbar, minimum sizes\n");
    ok(utEditHandlePx(1.0f) == 8 && utEditHandlePx(0.8f) == 8 && utEditHandlePx(2.0f) == 12,
       "a handle is 6 record px, never under 8 screen px");
    UtEdit e = {};
    ok(utEditOpen(&e, openIn(lay, 1.0f, 3, 1000)), "the editor opens on a collection group");
    ok(eq(e.tool[0], 368, 606, 52, 14) && eq(e.tool[1], 423, 606, 52, 14) && eq(e.tool[2], 478, 606, 52, 14),
       "SAVE / CANCEL / RESET right-aligned at the top of the pad");
    ok(e.minW[kUtLayPad] == kUtEdMinPadW && kUtEdMinPadW == 260 && e.minH[kUtLayPad] == 31,
       "the pad's floor is row 2's: 260 x 31 record px (LY1's paging limit)");
    ok(e.minW[kUtLayLabel] == 24 && e.minH[kUtLayLabel] == 7, "the label's floor: 24 x 7 at UI scale 1");
    ok(e.labelInPad == 1 && eq(e.rect[kUtLayLabel], 270, 622, 260, 12), "the derived label is the pad's own box");

    printf("\n2. clamps of a drag\n");
    {
        const UtRect plate = {0, 0, 565, 637};
        const UtRect pad = {33, 606, 500, 31};
        ok(eq(utEditDragRect(pad, kUtEdgeMove, 400, 0, plate, 260, 31, 1, 1), 65, 606, 500, 31),
           "a move to the right stops at the window's right edge");
        ok(eq(utEditDragRect(pad, kUtEdgeMove, -400, -900, plate, 260, 31, 1, 1), 0, 0, 500, 31),
           "a move up-left stops at the window's top-left");
        ok(eq(utEditDragRect(pad, kUtEdgeMove, 0, 100, plate, 260, 31, 1, 1), 33, 606, 500, 31),
           "a move down stops at the bottom edge");
        ok(eq(utEditDragRect(pad, kUtEdgeR, -400, 0, plate, 260, 31, 1, 1), 33, 606, 260, 31),
           "the right edge stops at the pad's floor");
        ok(eq(utEditDragRect(pad, kUtEdgeL, -100, 0, plate, 260, 31, 1, 1), 0, 606, 533, 31),
           "the left edge stops at the window's left edge, the right one stays");
        ok(eq(utEditDragRect(pad, kUtEdgeT | kUtEdgeL, 20, -30, plate, 260, 31, 1, 1), 53, 576, 480, 61),
           "a corner moves two edges, the opposite ones stay");
        ok(eq(utEditDragRect(pad, kUtEdgeB, 0, 50, plate, 260, 31, 1, 1), 33, 606, 500, 31),
           "the bottom edge has no room below the window");
        // the sweep: every drag of the pad and the label stays in the window; a drag may end over
        // the grid, and then it is refused there; a part SAVE allows never meets the grid
        int outside = 0, onGrid = 0, overGrid = 0, drags = 0, allowed = 0, gridRefused = 0;
        const float scales[5] = {0.8f, 1.0f, 1.25f, 1.5f, 2.0f};
        for (int si = 0; si < 5; ++si) {
            const float s = scales[si];
            UtLayoutLive L;
            if (!live(s, nullptr, nullptr, &L)) continue;
            static const int kEdges[9] = {kUtEdgeMove, kUtEdgeL, kUtEdgeR, kUtEdgeT, kUtEdgeB,
                                          kUtEdgeL | kUtEdgeT, kUtEdgeR | kUtEdgeT, kUtEdgeL | kUtEdgeB,
                                          kUtEdgeR | kUtEdgeB};
            for (int part = kUtLayPad; part < kUtLayParts; ++part)
                for (int k = 0; k < 9; ++k)
                    for (int dx = -700; dx <= 700; dx += 70)
                        for (int dy = -700; dy <= 700; dy += 35) {
                            UtEdit d = {};
                            utEditOpen(&d, openIn(L, s, 0, 0));
                            d.state = kUtEdDrag;
                            d.part = part;
                            d.edges = kEdges[k];
                            d.x0 = d.y0 = 0.0f;
                            d.start = d.rect[part];
                            utEditDragTo(&d, (float)dx, (float)dy);
                            ++drags;
                            bool meets = false;
                            for (int p = kUtLayPad; p < kUtLayParts; ++p) {
                                if (!utRectInside(d.rect[p], d.plate)) ++outside;
                                if (!utEditPartDerived(d, p) && utRectsMeet(d.rect[p], d.rect[kUtLayGrid]))
                                    meets = true;
                            }
                            if (meets) ++onGrid;
                            const bool allows = utEditCheck(&d, L.in);
                            if (meets && !allows) ++gridRefused;
                            if (allows) {
                                ++allowed;
                                // a part of the player's (SAVE writes its line) never meets the grid
                                for (int p = kUtLayPad; p < kUtLayParts; ++p)
                                    if (!utEditPartDerived(d, p) && utRectsMeet(d.rect[p], d.def[kUtLayGrid]))
                                        ++overGrid;
                            }
                        }
        }
        char d1[128];
        _snprintf_s(d1, sizeof(d1), _TRUNCATE, "%d drags at 5 scales, %d SAVE allows", drags, allowed);
        ok(drags > 30000 && outside == 0, "no drag leaves the caravan window", d1);
        ok(onGrid > 0 && gridRefused == onGrid, "a drag may END over the grid - and every such part is refused there", d1);
        ok(allowed > 0 && overGrid == 0, "no pad or label SAVE allows meets the grid", d1);
        {
            // GD's refusal rule: a part follows the cursor over the grid; only where it ENDS is it
            // judged - red, SAVE off - and carried past the grid it holds where it lands
            UtLayoutLive one;
            const bool have1 = live(1.0f, nullptr, nullptr, &one);
            UtEdit g = {};
            utEditOpen(&g, openIn(one, 1.0f, 0, 0));
            UtEdit a = dragged(g, kUtLayPad, kUtEdgeMove, -20.0f, -100.0f);
            ok(have1 && eq(a.rect[kUtLayPad], 13, 506, 500, 31) && !utEditCheck(&a, one.in) &&
                   a.partWhy[kUtLayPad] == kUtLayoutOverlap && a.against[kUtLayPad] == kUtLayGrid,
               "a pad moved into the grid lies over it, red: it overlaps the grid, SAVE is off");
            UtEdit b = dragged(g, kUtLayPad, kUtEdgeMove, -20.0f, -600.0f);
            ok(eq(b.rect[kUtLayPad], 13, 6, 500, 31) && utEditCheck(&b, one.in),
               "carried past the grid it lands above it and holds");
            UtEdit c = dragged(g, kUtLayPad, kUtEdgeT, 0.0f, -50.0f);
            ok(eq(c.rect[kUtLayPad], 33, 556, 500, 81) && !utEditCheck(&c, one.in),
               "its top edge dragged into the grid: the pad grows over it, red");
            UtEdit l = dragged(g, kUtLayLabel, kUtEdgeMove, 0.0f, -300.0f);
            ok(l.rect[kUtLayLabel].y == 322 && !utEditCheck(&l, one.in) && l.partWhy[kUtLayLabel] == kUtLayoutOverlap,
               "a label moved into the grid lies over it, red");
        }
    }

    {
        // the derived pad's record rectangle is utPadLayout's (30 px high at UI scale 1.25, under the
        // floor of a pad fitted in a rectangle): 1 px off it is the derived pad; moved away it grows
        UtLayoutLive L;
        live(1.25f, nullptr, nullptr, &L);
        UtEdit d = {};
        utEditOpen(&d, openIn(L, 1.25f, 0, 0));
        const UtRect p0 = d.rect[kUtLayPad];
        d.state = kUtEdDrag;
        d.part = kUtLayPad;
        d.edges = kUtEdgeMove;
        d.x0 = d.y0 = 0.0f;
        d.start = p0;
        utEditLeftUp(&d, 1.25f, 0.0f);
        const bool snapped = utRectEq(d.rect[kUtLayPad], p0);
        utEditLeftDown(&d, cx(p0, 1.25f) - 100.0f, cy(p0, 1.25f), 1, 1, 0, 0);
        utEditLeftUp(&d, cx(p0, 1.25f) - 100.0f, cy(p0, 1.25f) - 560.0f * 1.25f);
        char d2[96];
        _snprintf_s(d2, sizeof(d2), _TRUNCATE, "derived %d,%d,%d,%d -> %d,%d,%d,%d", p0.x, p0.y, p0.w, p0.h,
                    d.rect[1].x, d.rect[1].y, d.rect[1].w, d.rect[1].h);
        ok(snapped && p0.h < kUtEdMinPadH && d.rect[kUtLayPad].h == kUtEdMinPadH && utEditCheck(&d, L.in),
           "UI scale 1.25: a pad 1 px off is the derived one; moved away it grows to its floor and fits", d2);
    }

    printf("\n3. drag button and one-shot key\n");
    ok(utEditDragButton(0, 1, 0) == 1 && utEditDragButton(1, 0, 1) == 2 && utEditDragButton(0, 0, 1) == 2 &&
           utEditDragButton(0, 0, 0) == 0,
       "the polled button: the logical left, swapped or not, else the other, else none");
    {
        int latch = 0;
        const bool a = utEditOneShot(1, &latch);
        latch = 1;
        const bool b = utEditOneShot(1, &latch);
        const bool c = utEditOneShot(0, &latch);
        const bool d = utEditOneShot(1, &latch);
        ok(a && !b && !c && d && latch == 0, "layout_edit=1 opens once per 0 -> 1 edge");
    }

    printf("\n4. open\n");
    {
        UtEdit x = {};
        ok(!utEditOpen(&x, openIn(lay, 0.1f, 3, 0)) && x.state == kUtEdClosed, "refused at a bad scale");
        ok(!utEditOpen(&x, openIn(lay, 1.0f, kUtEdNoPage, 0)), "refused where the pad is not shown");
        {
            UtEdit tp = {};
            ok(utEditOpen(&tp, openIn(lay, 1.0f, kUtEdTransferPage, 1000)) && tp.group == kUtEdTransferPage,
               "it opens on the real Transfer page too (the pad is shown there)");
            UtEdTick u = tickAt(tp, 1100);
            ok(utEditTick(&tp, u, nullptr) == kUtEdWhyNone && tp.state == kUtEdOpen,
               "and stays open there");
        }
        ok(utEditOpen(&x, openIn(lay, 1.0f, 3, 0)) && !utEditOpen(&x, openIn(lay, 1.0f, 3, 0)),
           "opened once, not twice");
        ok(utEditOpenVerdict(kUtEdClosed, 0, 1) == kUtEdOpenHeld &&
               strstr(utEditOpenVerdictText(kUtEdOpenHeld), "item is on the cursor") != nullptr,
           "an item on the cursor: the editor does not open (GD's rule), and says why");
        ok(utEditOpenVerdict(kUtEdClosed, 1, 1) == kUtEdOpenYes &&
               utEditOpenVerdict(kUtEdOpen, 1, 1) == kUtEdOpenAlready &&
               utEditOpenVerdict(kUtEdClosed, 1, 0) == kUtEdOpenNoPad &&
               utEditOpenVerdict(kUtEdClosed, 0, 0) == kUtEdOpenHeld,
           "the verdict: open, already open, no pad - the held item first");
        UtLayoutLive moved;
        live(1.0f, "33,40,500,31", "27,100,300,14", &moved);
        UtEdit y = {};
        utEditOpen(&y, openIn(moved, 1.0f, 3, 0));
        ok(eq(y.rect[kUtLayPad], 33, 40, 500, 31) && eq(y.rect[kUtLayLabel], 27, 100, 300, 14) && y.labelInPad == 0 &&
               eq(y.tool[0], 368, 40, 52, 14),
           "it opens on the rectangles in force (the player's), the toolbar on the pad");
    }

    printf("\n5. hit test\n");
    {
        UtEdHit h = utEditHitTest(e, 370.0f, 610.0f);
        ok(h.kind == kUtEdHitButton && h.button == kUtEdSave, "the toolbar first: SAVE");
        h = utEditHitTest(e, 33.0f, 606.0f);
        ok(h.kind == kUtEdHitHandle && h.part == kUtLayPad && h.edges == (kUtEdgeL | kUtEdgeT),
           "the pad's top-left corner");
        h = utEditHitTest(e, 533.0f, 606.0f);
        ok(h.kind == kUtEdHitHandle && h.part == kUtLayPad && h.edges == (kUtEdgeR | kUtEdgeT),
           "the pad's top-right corner");
        h = utEditHitTest(e, 532.0f, 637.0f);
        ok(h.kind == kUtEdHitHandle && h.part == kUtLayLabel && h.edges == (kUtEdgeR | kUtEdgeB),
           "near the pad's bottom-right corner the label's corner wins (GD's order: the label first)");
        h = utEditHitTest(e, 30.0f, 620.0f);
        ok(h.kind == kUtEdHitHandle && h.part == kUtLayPad && h.edges == kUtEdgeL, "the pad's left edge strip");
        h = utEditHitTest(e, 100.0f, 615.0f);
        ok(h.kind == kUtEdHitBody && h.part == kUtLayPad && h.edges == kUtEdgeMove, "the pad's body");
        h = utEditHitTest(e, 400.0f, 628.0f);
        ok(h.kind == kUtEdHitBody && h.part == kUtLayLabel, "the label inside the pad wins over the pad");
        h = utEditHitTest(e, 270.0f, 628.0f);
        ok(h.kind == kUtEdHitHandle && h.part == kUtLayLabel && h.edges == kUtEdgeL, "the label's left edge");
        h = utEditHitTest(e, 300.0f, 300.0f);
        ok(h.kind == kUtEdHitNone, "the grid that cannot be set has no handle and no body: its clicks are the page's");
        h = utEditHitTest(e, 27.0f, 126.0f);
        ok(h.kind == kUtEdHitNone, "not even its corner");
        h = utEditHitTest(e, 100.0f, 604.0f);
        ok(h.kind == kUtEdHitHandle && h.part == kUtLayPad && h.edges == kUtEdgeT,
           "the pad's top strip grabs its top edge on both sides of the grid's edge");
        ok(utEditOwnsPoint(e, 100.0f, 615.0f) && utEditOwnsPoint(e, 30.0f, 608.0f) && utEditOwnsPoint(e, 370.0f, 610.0f) &&
               utEditOwnsPoint(e, 30.0f, 604.0f) && !utEditOwnsPoint(e, 300.0f, 300.0f) && !utEditOwnsPoint(e, 20.0f, 600.0f),
           "the points the editor owns: the pad and the label grown by half a handle, the toolbar");
        UtEdit over = e;
        over.rect[kUtLayPad] = UtRect{33, 300, 500, 31};
        utEditPlaceToolbar(&over);
        h = utEditHitTest(over, 100.0f, 315.0f);
        ok(h.kind == kUtEdHitBody && h.part == kUtLayPad && utEditOwnsPoint(over, 100.0f, 315.0f),
           "a pad lying over the grid is grabbed there (to move it off again) and owns the point");
        UtEdit closed = e;
        closed.state = kUtEdClosed;
        ok(!utEditOwnsPoint(closed, 100.0f, 615.0f) && utEditHitTest(closed, 100.0f, 615.0f).kind == kUtEdHitNone,
           "closed: nothing is the editor's");
    }

    printf("\n6. transitions\n");
    {
        UtEdit x = e;
        ok(utEditLeftDown(&x, 300.0f, 300.0f, 1, 1, 0, 0) == kUtEdActPass && x.state == kUtEdOpen,
           "a LEFT DOWN on the grid passes");
        ok(utEditLeftDown(&x, 100.0f, 615.0f, 0, 1, 0, 0) == kUtEdActPass && x.state == kUtEdOpen,
           "an item on the cursor: a LEFT DOWN on the pad starts nothing");
        ok(utEditLeftDown(&x, 100.0f, 615.0f, 1, 1, 0, 0) == kUtEdActDrag && x.state == kUtEdDrag &&
               x.part == kUtLayPad && x.button == 1,
           "a LEFT DOWN on the pad starts a move polling the left button");
        UtEdTick t = tickAt(x, 1100);
        t.physL = 1;
        t.haveCursor = 1;
        t.cx = 100.0f;
        t.cy = 615.0f - 566.0f;
        int moved = 0;
        ok(utEditTick(&x, t, &moved) == kUtEdWhyNone && moved == 1 && eq(x.rect[kUtLayPad], 33, 40, 500, 31) &&
               x.state == kUtEdDrag,
           "the polled cursor moves the pad while the button is held");
        ok(eq(x.tool[0], 368, 40, 52, 14), "the toolbar travels with the pad");
        ok(x.labelInPad == 1 && utRectInside(x.rect[kUtLayLabel], x.rect[kUtLayPad]) &&
               !utRectEq(x.rect[kUtLayLabel], x.def[kUtLayLabel]),
           "the label box follows the pad (row 2 of the pad fitted there)");
        t.now = 1200;
        t.physL = 0;
        ok(utEditTick(&x, t, &moved) == kUtEdWhyNone && x.state == kUtEdOpen && x.part == -1,
           "the polled release ends the drag");
        ok(utEditCheck(&x, lay.in) && x.ok == 1, "the pad above the grid holds: SAVE is allowed");
        ok(utEditLeftDown(&x, 370.0f, 44.0f, 1, 1, 0, 0) == kUtEdActSave, "SAVE pressed and allowed");
        UtEdit fail = x;
        utEditSaved(&fail, 0, 1300);
        ok(fail.state == kUtEdOpen, "a failed write leaves the editor open");
        utEditSaved(&x, 1, 1300);
        ok(x.state == kUtEdSaving, "a written SAVE waits for the read-back");
        UtEdit slow = x;
        t = tickAt(x, 1350);
        t.matches = 0;
        ok(utEditTick(&x, t, nullptr) == kUtEdWhyNone && x.state == kUtEdSaving, "not read back yet: still waiting");
        t.now = 1400;
        t.matches = 1;
        ok(utEditTick(&x, t, nullptr) == kUtEdWhySaved && x.state == kUtEdClosed && x.why == kUtEdWhySaved,
           "read back: closed, saved");
        UtEdTick ts = tickAt(slow, 1350);
        for (unsigned k = 1; k <= 20; ++k) {
            ts.now = 1300 + k * 200;
            if (utEditTick(&slow, ts, nullptr) != kUtEdWhyNone) break;
        }
        ok(slow.state == kUtEdClosed && slow.why == kUtEdWhySaveSlow, "never read back: closed after 3 s, said so");
        // SAVE refused, RESET, CANCEL
        UtEdit r = e;
        utEditLeftDown(&r, 100.0f, 615.0f, 1, 1, 0, 0);
        utEditLeftUp(&r, 80.0f, 515.0f);
        ok(r.state == kUtEdOpen && eq(r.rect[kUtLayPad], 13, 506, 500, 31),
           "LEFT UP ends a drag where it says - over the grid too");
        ok(!utEditCheck(&r, lay.in) && r.partWhy[kUtLayPad] == kUtLayoutOverlap && r.against[kUtLayPad] == kUtLayGrid,
           "a pad over the grid is refused (it overlaps the grid)");
        char words[160];
        utEditRefusal(r, kUtLayPad, words, sizeof(words));
        ok(!strcmp(words, "the pad overlaps another part of the page: the grid"), "the refusal's words", words);
        const float tx = cx(r.tool[0], 1.0f), ty = cy(r.tool[0], 1.0f);
        ok(utEditLeftDown(&r, tx, ty, 1, 1, 0, 0) == kUtEdActSaveRefused && r.state == kUtEdOpen,
           "SAVE pressed while a rectangle is refused: nothing done");
        ok(utEditLeftDown(&r, cx(r.tool[2], 1.0f), ty, 1, 1, 0, 0) == kUtEdActReset &&
               eq(r.rect[kUtLayPad], 33, 606, 500, 31) && eq(r.rect[kUtLayLabel], 270, 622, 260, 12) && r.labelInPad,
           "RESET: the derived pad and label are back");
        ok(utEditCheck(&r, lay.in) && !utEditChanged(r), "RESET's layout holds and is the one at the open");
        UtEdit c = e;
        utEditLeftDown(&c, 100.0f, 615.0f, 1, 1, 0, 0);
        ok(utEditLeftDown(&c, cx(c.tool[1], 1.0f), cy(c.tool[1], 1.0f), 0, 0, 0, 0) == kUtEdActCancel &&
               c.state == kUtEdClosed && c.why == kUtEdWhyCancel,
           "CANCEL answers mid-drag and with an item on the cursor");
        UtEdit d = e;
        utEditLeftDown(&d, 100.0f, 615.0f, 1, 1, 0, 0);
        ok(utEditLeftDown(&d, 300.0f, 300.0f, 1, 1, 0, 0) == kUtEdActPass && d.state == kUtEdOpen && d.part == -1,
           "a LEFT DOWN during a drag whose release was lost ends that drag");
        // the cancel rules
        struct Rule {
            int field;
            int why;
            const char* what;
        };
        static const Rule kRules[6] = {{0, kUtEdWhyGone, "not drawn for 400 ms"},
                                       {1, kUtEdWhyLayout, "no layout"},
                                       {2, kUtEdWhyCover, "another window size"},
                                       {3, kUtEdWhyScale, "another UI scale"},
                                       {4, kUtEdWhyGroup, "the pad not shown"},
                                       {5, kUtEdWhyFocus, "the focus lost"}};
        int good = 0;
        for (int k = 0; k < 6; ++k) {
            UtEdit y = e;
            UtEdTick u = tickAt(y, 1100);
            if (kRules[k].field == 0) u.now = 1000 + kUtEdGoneMs + 1;
            if (kRules[k].field == 1) u.layoutOk = 0;
            if (kRules[k].field == 2) u.coverW = 600;
            if (kRules[k].field == 3) u.scale = 1.25f;
            if (kRules[k].field == 4) u.group = kUtEdNoPage;
            if (kRules[k].field == 5) u.focused = 0;
            if (utEditTick(&y, u, nullptr) == kRules[k].why && y.state == kUtEdClosed &&
                strstr(utEditWhyText(kRules[k].why), "nothing was written"))
                ++good;
            else
                printf("        (rule %s failed)\n", kRules[k].what);
        }
        ok(good == 6, "every cancel rule closes it, nothing written: gone, layout, size, scale, page, focus");
    }

    printf("\n7. the check, what SAVE writes, the round trip\n");
    {
        UtEdit x = e;
        UtEditSavePlan plan;
        utEditSavePlanOf(x, "565x637", &plan);
        ok(plan.n == 4 && !strcmp(plan.k[0].key, "pad_565x637") && !plan.k[0].set &&
               !strcmp(plan.k[1].key, "pad_screen") && !plan.k[1].set && !strcmp(plan.k[2].key, "label_565x637") &&
               !plan.k[2].set && !strcmp(plan.k[3].key, "label_screen") && !plan.k[3].set && !planHas(plan, "grid_565x637"),
           "the derived layout: SAVE removes both lines and both screen keys (no grid line: it cannot be set)");
        // the pad too narrow for its buttons
        utEditLeftDown(&x, 532.0f, 620.0f, 1, 1, 0, 0);   // the right edge
        x.rect[kUtLayPad] = UtRect{33, 606, 250, 31};     // under the floor (as a hand line would be)
        ok(!utEditCheck(&x, lay.in) && x.partWhy[kUtLayPad] == kUtLayoutPadNoFit, "a pad under 260 px has no room");
        x = e;
        // move the pad above the grid, then the label out of it
        utEditLeftDown(&x, 100.0f, 615.0f, 1, 1, 0, 0);
        utEditLeftUp(&x, 100.0f, 49.0f);
        utEditLeftDown(&x, 400.0f, cy(x.rect[kUtLayLabel], 1.0f), 1, 1, 0, 0);
        const int ly0 = x.rect[kUtLayLabel].y;
        utEditLeftUp(&x, 400.0f - 243.0f, cy(x.rect[kUtLayLabel], 1.0f) + (float)(100 - ly0));
        ok(x.labelInPad == 0 && x.rect[kUtLayLabel].y == 100 && utEditCheck(&x, lay.in),
           "a label dragged out of the pad is the player's own, and holds");
        utEditSavePlanOf(x, "565x637", &plan);
        char want[64];
        _snprintf_s(want, sizeof(want), _TRUNCATE, "%d,%d,%d,%d", x.rect[kUtLayLabel].x, x.rect[kUtLayLabel].y,
                    x.rect[kUtLayLabel].w, x.rect[kUtLayLabel].h);
        ok(plan.k[0].set && !strcmp(plan.k[0].value, "33,40,500,31") && plan.k[2].set && !strcmp(plan.k[2].value, want),
           "SAVE writes pad_565x637=33,40,500,31 and the label's line", plan.k[2].value);
        // the label over the pad is refused
        UtEdit o = x;
        o.rect[kUtLayLabel] = UtRect{40, 30, 300, 14};
        ok(!utEditCheck(&o, lay.in) && o.partWhy[kUtLayLabel] == kUtLayoutOverlap && o.against[kUtLayLabel] == kUtLayPad,
           "a label across the pad's edge overlaps the pad");
        // the round trip at every scale: the pad moved to the top, the label left in it / moved
        const float scales[6] = {0.8f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f};
        int rt = 0, rtOk = 0;
        for (int si = 0; si < 6; ++si) {
            const float s = scales[si];
            UtLayoutLive L;
            if (!live(s, nullptr, nullptr, &L)) continue;
            for (int variant = 0; variant < 3; ++variant) {
                UtEdit y = {};
                utEditOpen(&y, openIn(L, s, 0, 0));
                const UtRect p0 = y.rect[kUtLayPad];
                const float px = cx(p0, s) - 60.0f * s, py = cy(p0, s);
                utEditLeftDown(&y, px, py, 1, 1, 0, 0);
                utEditLeftUp(&y, px, py - (float)(p0.y - 40) * s);
                if (variant >= 1) {   // the label to the left top
                    const UtRect l0 = y.rect[kUtLayLabel];
                    const float lx = cx(l0, s), lyy = cy(l0, s);
                    utEditLeftDown(&y, lx, lyy, 1, 1, 0, 0);
                    utEditLeftUp(&y, lx - (float)(l0.x - 27) * s, lyy + (float)(90 - l0.y) * s);
                }
                if (variant == 2) {   // and the pad resized narrower from its right edge
                    const UtRect p1 = y.rect[kUtLayPad];
                    const float ex = (float)(p1.x + p1.w) * s, ey = cy(p1, s);
                    utEditLeftDown(&y, ex, ey, 1, 1, 0, 0);
                    utEditLeftUp(&y, ex - 120.0f * s, ey);
                }
                ++rt;
                if (!utEditCheck(&y, L.in)) {
                    char w1[160], w2[160];
                    utEditRefusal(y, kUtLayPad, w1, sizeof(w1));
                    utEditRefusal(y, kUtLayLabel, w2, sizeof(w2));
                    printf("        scale %.2f variant %d refused: %s %s (pad %d,%d,%d,%d label %d,%d,%d,%d)\n", s,
                           variant, w1, w2, y.rect[1].x, y.rect[1].y, y.rect[1].w, y.rect[1].h, y.rect[2].x,
                           y.rect[2].y, y.rect[2].w, y.rect[2].h);
                    continue;
                }
                UtLayoutLive back = {};
                if (readBack(y, s, &back) && utEditMatches(y, back.L.rect) && back.L.user[kUtLayPad] &&
                    (variant == 0 ? back.L.labelInPad == 1 : back.L.user[kUtLayLabel] == 1))
                    ++rtOk;
                else
                    printf("        scale %.2f variant %d read back differently: pad %d,%d,%d,%d label %d,%d,%d,%d "
                           "-> %d,%d,%d,%d / %d,%d,%d,%d\n",
                           s, variant, y.rect[1].x, y.rect[1].y, y.rect[1].w, y.rect[1].h, y.rect[2].x, y.rect[2].y,
                           y.rect[2].w, y.rect[2].h, back.L.rect[1].x, back.L.rect[1].y, back.L.rect[1].w,
                           back.L.rect[1].h, back.L.rect[2].x, back.L.rect[2].y, back.L.rect[2].w, back.L.rect[2].h);
            }
        }
        char d7b[64];
        _snprintf_s(d7b, sizeof(d7b), _TRUNCATE, "%d of %d", rtOk, rt);
        ok(rt == 18 && rtOk == rt, "SAVE round-trips: the lines read back give the edited pad and label (6 scales)", d7b);
        // RESET + SAVE round-trips to no line at all
        UtEdit z = x;
        utEditLeftDown(&z, cx(z.tool[2], 1.0f), cy(z.tool[2], 1.0f), 1, 1, 0, 0);
        utEditSavePlanOf(z, "565x637", &plan);
        UtLayoutLive back;
        ok(!plan.k[0].set && !plan.k[2].set && readBack(z, 1.0f, &back) && utEditMatches(z, back.L.rect) &&
               !back.L.user[kUtLayPad] && !back.L.user[kUtLayLabel],
           "RESET then SAVE: both lines removed, the derived layout read back");
        char v[48];
        ok(utEditKeyValue(z, kUtLayPad, v, sizeof(v)) == 0 && utEditKeyValue(x, kUtLayPad, v, sizeof(v)) > 0 &&
               !strcmp(v, "33,40,500,31") && utEditKeyValue(x, kUtLayGrid, v, sizeof(v)) == 0,
           "the value per part: none for a derived part or the grid");
    }

    printf("\n8. purity\n");
    {
        char path[600];
        _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\src\\ut_layoutedit.h", argc > 1 ? argv[1] : "..");
        FILE* f = nullptr;
        fopen_s(&f, path, "rb");
        static char buf[1 << 17];
        size_t n = 0;
        if (f) {
            n = fread(buf, 1, sizeof(buf) - 1, f);
            fclose(f);
        }
        buf[n] = 0;
        ok(n > 0 && !strstr(buf, "<windows.h>") && !strstr(buf, "GetTickCount(") && !strstr(buf, "GetAsyncKeyState(") &&
               !strstr(buf, "malloc(") && !strstr(buf, "std::") && !strstr(buf, "static UtEdit"),
           "the header is pure: no Windows, no clock, no allocation, no state of its own");
    }

    printf("\n9. the label back in the pad\n");
    {
        UtEdit x = e;
        const UtRect home = x.rect[kUtLayLabel];
        UtEdit out = dragged(x, kUtLayLabel, kUtEdgeMove, -243.0f, -500.0f);
        utEditDragEnd(&out);
        ok(out.labelInPad == 0 && out.rect[kUtLayLabel].y == 122, "a label dragged out of the pad is the player's");
        UtEdit back = out;
        back.state = kUtEdDrag;
        back.part = kUtLayLabel;
        back.edges = kUtEdgeMove;
        back.x0 = back.y0 = 0.0f;
        back.start = back.rect[kUtLayLabel];
        utEditLeftUp(&back, 245.0f, 502.0f);
        ok(back.labelInPad == 1 && utRectEq(back.rect[kUtLayLabel], home) && utEditCheck(&back, lay.in),
           "put back near where the derived label sits (inside the pad, over its label box): the pad's own box, held");
        UtEditSavePlan plan;
        utEditSavePlanOf(back, "565x637", &plan);
        ok(!planValue(plan, "label_565x637") && planHas(plan, "label_565x637"), "SAVE removes the label's line");
        UtEdit row1 = out;
        row1.state = kUtEdDrag;
        row1.part = kUtLayLabel;
        row1.edges = kUtEdgeMove;
        row1.x0 = row1.y0 = 0.0f;
        row1.start = row1.rect[kUtLayLabel];
        row1.rect[kUtLayLabel] = UtRect{40, 520, 100, 12};
        row1.start = row1.rect[kUtLayLabel];
        utEditLeftUp(&row1, 0.0f, 89.0f);   // over row 1's buttons, off the label box
        ok(row1.labelInPad == 0 && !utEditCheck(&row1, lay.in) && row1.partWhy[kUtLayLabel] == kUtLayoutOverlap,
           "dropped over the pad's buttons (not its label box): still the player's, refused over the pad");
    }

    printf("\n10. the grid's handle (while the grid can be set)\n");
    {
        UtEdit gm = {};
        ok(utEditOpen(&gm, openIn(lay, 1.0f, 3, 1000, 1)) && gm.gridMovable == 1 && gm.snapW == 32 && gm.snapH == 32 &&
               gm.minW[kUtLayGrid] == 32 && gm.minH[kUtLayGrid] == 32,
           "opened with the grid settable: its step and floor are one cell (32 record px)");
        UtEdHit h = utEditHitTest(gm, 27.0f, 126.0f);
        ok(h.kind == kUtEdHitHandle && h.part == kUtLayGrid && h.edges == (kUtEdgeL | kUtEdgeT), "the grid's top-left corner");
        h = utEditHitTest(gm, 300.0f, 300.0f);
        ok(h.kind == kUtEdHitBody && h.part == kUtLayGrid && utEditOwnsPoint(gm, 300.0f, 300.0f), "the grid's body");
        h = utEditHitTest(gm, 100.0f, 615.0f);
        ok(h.kind == kUtEdHitBody && h.part == kUtLayPad, "the pad still wins over the grid");
        UtEdit r = dragged(gm, kUtLayGrid, kUtEdgeR, -100.0f, 0.0f);
        ok(eq(r.rect[kUtLayGrid], 27, 126, 416, 480), "the right edge: whole cells (512 - 100 -> 416 = 13 cells)");
        r = dragged(gm, kUtLayGrid, kUtEdgeB, 0.0f, -470.0f);
        ok(eq(r.rect[kUtLayGrid], 27, 126, 512, 32), "the bottom edge stops at one cell");
        r = dragged(gm, kUtLayGrid, kUtEdgeMove, 100.0f, -200.0f);
        ok(eq(r.rect[kUtLayGrid], 53, 0, 512, 480), "a move stays inside the window");
        UtEditSavePlan plan;
        utEditSavePlanOf(r, "565x637", &plan);
        ok(plan.n == 5 && !strcmp(plan.k[0].key, "grid_565x637") && plan.k[0].set && !strcmp(plan.k[0].value, "53,0,512,480"),
           "SAVE writes grid_565x637 first", plan.k[0].value);
        utEditSavePlanOf(gm, "565x637", &plan);
        ok(plan.n == 5 && !plan.k[0].set, "the grid at its derived rectangle: its line removed");
        UtEdit fixed = dragged(e, kUtLayGrid, kUtEdgeMove, 100.0f, 0.0f);
        ok(utRectEq(fixed.rect[kUtLayGrid], e.rect[kUtLayGrid]), "a grid that cannot be set never moves");
    }

    printf("\n10b. a press on the grid through the window's route (editInput: utEditPointOf, utEditPress)\n");
    {
        // a measured game: the caravan window at 873,282 of the 3440x1440 canvas (client
        // px = canvas px), UI scale 1.375, the grid at its derived 27,126,512,480
        const float s = 1.375f, ox = 873.0f, oy = 282.0f;
        UtEdit gs = {};
        ok(utEditOpen(&gs, openIn(lay, s, 3, 1000, 1)) && gs.gridMovable == 1 &&
               eq(gs.rect[kUtLayGrid], 27, 126, 512, 480),
           "opened on a collection group at UI scale 1.375 with the grid settable");
        float x = 0.0f, y = 0.0f;
        char words[128];
        // the grid's body: record 300,300 = 412.5,412.5 of the window = canvas 1285,694
        utEditPointOf(1285.0f, 694.0f, ox, oy, &x, &y);
        UtEdit b1 = gs;
        const UtEdPress p1 = utEditPress(&b1, x, y, 1, 1, 0, 0);
        utEditPressText(p1, 1, words, sizeof(words));
        ok(p1.act == kUtEdActDrag && p1.hit.kind == kUtEdHitBody && p1.hit.part == kUtLayGrid && p1.mine &&
               b1.state == kUtEdDrag && b1.part == kUtLayGrid && b1.edges == kUtEdgeMove &&
               !strcmp(words, "the grid's body - a move"),
           "LEFT DOWN on the grid's body: the editor's, the grid's move", words);
        ok(utEditLeftUp(&b1, x - 22.0f, y) && eq(b1.rect[kUtLayGrid], 11, 126, 512, 480) && b1.state == kUtEdOpen,
           "its LEFT UP 22 screen px left: the grid moved 16 record px");
        // the grid's top-left corner: record 27,126 = 37.1,173.3 of the window = canvas 910,455
        utEditPointOf(910.0f, 455.0f, ox, oy, &x, &y);
        UtEdit b2 = gs;
        const UtEdPress p2 = utEditPress(&b2, x, y, 1, 1, 0, 0);
        utEditPressText(p2, 1, words, sizeof(words));
        ok(p2.act == kUtEdActDrag && p2.hit.kind == kUtEdHitHandle && p2.hit.part == kUtLayGrid &&
               p2.hit.edges == (kUtEdgeL | kUtEdgeT) && p2.mine &&
               !strcmp(words, "the grid's top-left corner - a resize"),
           "LEFT DOWN on the grid's top-left corner: the editor's, a resize", words);
        ok(utEditLeftUp(&b2, x + 44.0f, y + 44.0f) && eq(b2.rect[kUtLayGrid], 59, 158, 480, 448),
           "its LEFT UP 44 px right and down: one cell off the left and the top");
        // an item on the cursor: nothing but CANCEL answers, and the press is still the editor's
        UtEdit b3 = gs;
        utEditPointOf(1285.0f, 694.0f, ox, oy, &x, &y);
        const UtEdPress p3 = utEditPress(&b3, x, y, 0, 1, 0, 0);
        utEditPressText(p3, 0, words, sizeof(words));
        ok(p3.act == kUtEdActPass && p3.mine && b3.state == kUtEdOpen && strstr(words, "an item is on the cursor"),
           "with an item on the cursor: no drag, the press swallowed", words);
        // off every part of the editor: the page's
        UtEdit b4 = gs;
        utEditPointOf(880.0f, 351.0f, ox, oy, &x, &y);
        const UtEdPress p4 = utEditPress(&b4, x, y, 1, 1, 0, 0);
        utEditPressText(p4, 1, words, sizeof(words));
        ok(p4.act == kUtEdActPass && !p4.mine && !strcmp(words, "nothing of the editor's - passed to the page"),
           "off the editor's parts: passed to the page", words);
        // the grid fixed (the real Transfer page): its body is the page's
        UtEdit fx = {};
        utEditOpen(&fx, openIn(lay, s, kUtEdTransferPage, 1000, 0));
        utEditPointOf(1285.0f, 694.0f, ox, oy, &x, &y);
        const UtEdPress p5 = utEditPress(&fx, x, y, 1, 1, 0, 0);
        ok(p5.act == kUtEdActPass && !p5.mine, "the real Transfer page: a press on the fixed grid is the page's");
        // the page's pick-up under the editor's press (hk_GetItemUnderPoint): refused on the movable
        // grid only while the editor is open on a page on the screen and the cursor is known
        utEditPointOf(1285.0f, 694.0f, ox, oy, &x, &y);
        UtEdit cl = gs;
        cl.state = kUtEdClosed;
        ok(utEditOwnsTake(gs, true, 0, true, x, y) && !utEditOwnsTake(fx, true, 0, true, x, y) &&
               !utEditOwnsTake(gs, false, 0, true, x, y) &&
               !utEditOwnsTake(gs, true, kUtEdGoneMs + 1, true, x, y) &&
               !utEditOwnsTake(gs, true, 0, false, x, y) && !utEditOwnsTake(cl, true, 0, true, x, y),
           "the page's pick-up under the press on the movable grid: refused; the fixed grid, the page "
           "gone, the cursor unknown or the editor closed - the page's");
    }

    printf("\n10c. the editor's grid against the drawn grid (a collection page, the view ON)\n");
    {
        // the panel opens the editor on utRecordRect(the drawn grid, the frame) and reads a press
        // at the canvas point minus the rounded frame origin (edOx / edOy): the drawn grid's
        // centre, its top corners and its side middles must all be the editor's grid
        const float s = 1.375f;
        UtLayoutLive L;
        UtRectF f = {0.0f, 0.0f, 0.0f, 0.0f}, g = {0.0f, 0.0f, 0.0f, 0.0f};
        const bool made = live(s, nullptr, nullptr, &L) && page(s, 2560.0f, 1440.0f, &f, &g);
        UtEdit ge = {};
        const bool opened = made && utEditOpen(&ge, openIn(L, s, 3, 1000, 1)) && ge.gridMovable == 1;
        const float ox = utPadRound(f.x), oy = utPadRound(f.y);
        const float pts[5][2] = {{g.x + g.w * 0.5f, g.y + g.h * 0.5f}, {g.x + 3.0f, g.y + 3.0f},
                                 {g.x + g.w - 3.0f, g.y + 3.0f}, {g.x + 3.0f, g.y + g.h * 0.5f},
                                 {g.x + g.w - 3.0f, g.y + g.h * 0.5f}};
        int onGrid = 0;
        for (int i = 0; i < 5; ++i) {
            float x = 0.0f, y = 0.0f;
            utEditPointOf(pts[i][0], pts[i][1], ox, oy, &x, &y);
            const auto h = utEditHitTest(ge, x, y);
            onGrid += h.kind != kUtEdHitNone && h.part == kUtLayGrid && utEditOwnsPoint(ge, x, y) ? 1 : 0;
        }
        ok(opened && onGrid == 5,
           "the drawn grid's centre, top corners and side middles are the editor's grid (5 of 5)");
    }

    printf("\n11. the screen mode\n");
    {
        const float s = 1.0f;
        UtLayoutLive L;
        live(s, nullptr, nullptr, &L);
        UtEdit x = {};
        ok(utEditOpen(&x, openIn(L, s, 3, 1000, 0, 1)) && x.screenKnown && x.screen.w > 565,
           "the screen known: the editor's screen bounds hold the whole canvas");
        // the pad carried left out of the window
        UtEdit a = dragged(x, kUtLayPad, kUtEdgeMove, -200.0f, -300.0f);
        ok(a.scr[kUtLayPad] == 1 && a.rect[kUtLayPad].x == -167 && !utRectInside(a.rect[kUtLayPad], a.plate) &&
               a.labelInPad == 1 && utRectInside(a.rect[kUtLayLabel], a.rect[kUtLayPad]),
           "a pad carried across the window's edge is in screen mode, its label box with it");
        ok(!utEditCheck(&a, L.in) && a.partWhy[kUtLayPad] == kUtLayoutScreenOverGrid,
           "a screen pad that covers part of the grid is refused: it would cover the item grid");
        char words[160];
        utEditRefusal(a, kUtLayPad, words, sizeof(words));
        ok(!strcmp(words, "the pad would cover the item grid"), "the refusal's words", words);
        UtEdit ar = a;
        ar.group = kUtEdTransferPage;
        utEditRefusal(ar, kUtLayPad, words, sizeof(words));
        ok(!strcmp(words, "the pad would cover the collection's item grid") &&
               !strcmp(utEditWhyText(ar, kUtLayPad), "would cover the collection's item grid") &&
               !utEditAgainstName(ar, kUtLayPad)[0],
           "on the game's own Transfer page the words name the collection's item grid (the real sack's is outlined)",
           words);
        UtEdit b = dragged(x, kUtLayPad, kUtEdgeMove, -600.0f, -500.0f);
        ok(b.scr[kUtLayPad] == 1 && utEditCheck(&b, L.in), "carried wholly off the window: it holds");
        UtEditSavePlan plan;
        utEditSavePlanOf(b, "565x637", &plan);
        const char* ps = planValue(plan, "pad_screen");
        ok(ps && planHas(plan, "label_565x637") && !planValue(plan, "label_565x637") && !planHas(plan, "pad_565x637"),
           "SAVE writes pad_screen (its nearest corner), removes the label's lines, leaves pad_565x637 alone",
           ps ? ps : "");
        UtScreenRect key;
        ok(ps && utScreenRectParse(ps, &key) && key.corner == kUtCornerTL && key.w == 500 && key.h == 31,
           "the key: the top-left corner, the pad's own size");
        UtLayoutLive back;
        ok(readBack(b, s, &back) && back.L.screen[kUtLayPad] == 1 && back.L.labelInPad == 1 &&
               utEditMatches(b, back.L.rect, back.L.screen),
           "read back: the pad on the screen where it was left, the label in it");
        UtEdit c = dragged(b, kUtLayPad, kUtEdgeMove, 0.0f, 0.0f);
        c.start = c.rect[kUtLayPad];
        c.x0 = c.y0 = 0.0f;
        utEditDragTo(&c, 600.0f, 500.0f);
        ok(c.scr[kUtLayPad] == 0 && eq(c.rect[kUtLayPad], 33, 606, 500, 31), "dragged back into the window: window mode");
        // the label alone onto the screen
        UtEdit d = dragged(x, kUtLayLabel, kUtEdgeMove, 400.0f, -100.0f);
        ok(d.scr[kUtLayLabel] == 1 && d.labelInPad == 0 && utEditCheck(&d, L.in),
           "the label carried out of the window: screen mode, the player's own, it holds");
        utEditSavePlanOf(d, "565x637", &plan);
        ok(planValue(plan, "label_screen") && !planValue(plan, "pad_screen") && planHas(plan, "pad_565x637"),
           "SAVE writes label_screen and keeps the pad in the window");
        ok(readBack(d, s, &back) && back.L.screen[kUtLayLabel] == 1 && utEditMatches(d, back.L.rect, back.L.screen),
           "read back: the label on the screen");
        UtRectF gp, gl;
        utEditScreenGrounds(b, &gp, &gl);
        const UtRectF win = utEditWindowClient(b);
        ok(gp.w > 0.0f && gl.w == 0.0f && win.w == 565.0f && win.h == 637.0f,
           "the grounds: the screen pad's (with its toolbar), no label of its own; the window's rect");
        UtEdit r = b;
        utEditLeftDown(&r, (float)(r.tool[2].x + 2), (float)(r.tool[2].y + 2), 1, 1, 0, 0);
        utEditSavePlanOf(r, "565x637", &plan);
        ok(r.scr[kUtLayPad] == 0 && planHas(plan, "pad_screen") && !planValue(plan, "pad_screen") &&
               eq(r.rect[kUtLayPad], 33, 606, 500, 31),
           "RESET: back in the window at the derived pad; SAVE removes pad_screen");
        ok(utEditScreenHint(b) && !utEditScreenHint(r), "the status words while a part is on the screen");
        UtEdTick t = tickAt(b, 1100);
        t.screenKnown = 1;
        t.screenW = 1920;
        t.screenH = 1080;
        ok(utEditTick(&b, t, nullptr) == kUtEdWhyScreen, "another screen size closes it, nothing written");
    }

    printf("\n12. the editor's view of the grid: the shown group's slots as ghost lines, the items hidden while open\n");
    {
        const UtEdGhost g = utEditGhost(UtRect{27, 126, 512, 480}, 1 << 20);
        ok(g.cols == 16 && g.rows == 15 && eq(g.cells, 27, 126, 512, 480),
           "the vanilla grid: 16 x 15 cells - 17 vertical and 16 horizontal lines over 512 x 480");
        const UtEdGhost sl = utEditGhost(UtRect{27, 126, 500, 470}, 1 << 20);
        ok(sl.cols == 15 && sl.rows == 14 && eq(sl.cells, 27, 126, 480, 448),
           "a rectangle between whole cells: the lines stop at the cells, the slack right and below stays empty");
        const UtEdGhost cp = utEditGhost(UtRect{27, 126, 512, 480}, 64);
        ok(cp.cols == 16 && cp.rows == 4 && eq(cp.cells, 27, 126, 512, 128), "the box cap takes rows away");
        const UtEdGhost one = utEditGhost(UtRect{27, 126, 20, 20}, 1 << 20);
        ok(one.cols == 1 && one.rows == 1, "a rectangle under one cell still shows that one cell");
        const UtEdGhost hel = utEditGhost(UtRect{27, 126, 512, 480}, 1 << 20, 2, 2);
        ok(hel.cols == 8 && hel.rows == 7 && hel.pitchW == 64 && hel.pitchH == 64 && eq(hel.cells, 27, 126, 512, 448),
           "the shown group's slots (Helms, 2 x 2 cells): 8 x 7 slots of 64 x 64, the last cell row left empty");
        const UtEdGhost spr = utEditGhost(UtRect{27, 126, 480, 480}, 1 << 20, 1, 5);
        ok(spr.cols == 15 && spr.rows == 3 && spr.pitchW == 32 && spr.pitchH == 160 && eq(spr.cells, 27, 126, 480, 480),
           "a tall slot (Spears, 1 x 5 cells): 15 x 3 slots of 32 x 160");
        const UtEdGhost none = utEditGhost(UtRect{27, 126, 40, 40}, 1 << 20, 2, 4);
        ok(none.cols == 0 && none.rows == 0 && none.cells.w == 0 && none.cells.h == 0,
           "a rectangle that holds no whole slot: no lines");
        ok(utEditGhostAt(59, 1.25f, 10.0f) == 10.0f + utPadRound(59.0f * 1.25f) &&
               utEditGhostAt(27, 1.0f, 0.0f) == utScaledRectF(UtRect{27, 126, 512, 480}, 1.0f).x,
           "a line's place: the editor's origin + the outline's own rounding");
        UtEdit gm = {};
        utEditOpen(&gm, openIn(lay, 1.0f, 3, 1000, 1));
        const UtEdit dr = dragged(gm, kUtLayGrid, kUtEdgeR, -100.0f, 0.0f);
        const UtEdGhost a = utEditGhost(gm.rect[kUtLayGrid], 1 << 20), b = utEditGhost(dr.rect[kUtLayGrid], 1 << 20);
        ok(b.cols < a.cols && b.cells.x == dr.rect[kUtLayGrid].x && b.cells.w <= dr.rect[kUtLayGrid].w,
           "the lines follow the dragged rectangle");
        UtEdit h = {};
        ok(!utEditHidesItems(h, 1000), "closed: the page's items are shown");
        ok(utEditOpen(&h, openIn(lay, 1.0f, 3, 1000)) && utEditHidesItems(h, 1000) &&
               utEditHidesItems(h, 1000 + kUtEdGoneMs),
           "open: the page's items are hidden");
        ok(!utEditHidesItems(h, 1000 + kUtEdGoneMs + 1),
           "not ticked for 400 ms (the page left the screen): the items come back before the tick closes it");
        UtEdit c = h;
        ok(utEditLeftDown(&c, cx(c.tool[1], 1.0f), cy(c.tool[1], 1.0f), 1, 1, 0, 0) == kUtEdActCancel &&
               !utEditHidesItems(c, 1000),
           "CANCEL: the items are back");
        UtEdit sv = h;
        ok(utEditLeftDown(&sv, cx(sv.tool[0], 1.0f), cy(sv.tool[0], 1.0f), 1, 1, 0, 0) == kUtEdActSave, "SAVE pressed");
        utEditSaved(&sv, 1, 1100);
        ok(sv.state == kUtEdSaving && utEditHidesItems(sv, 1100), "  written, waiting for the read-back: still hidden");
        UtEdTick t = tickAt(sv, 1150);
        t.matches = 1;
        ok(utEditTick(&sv, t, nullptr) == kUtEdWhySaved && !utEditHidesItems(sv, 1150), "  read back: the items are back");
        int back = 0;
        for (int k = 0; k < 6; ++k) {
            UtEdit y = h;
            UtEdTick u = tickAt(y, 1100);
            if (k == 0) u.now = 1000 + kUtEdGoneMs + 1;
            if (k == 1) u.layoutOk = 0;
            if (k == 2) u.coverW = 600;
            if (k == 3) u.scale = 1.25f;
            if (k == 4) u.group = kUtEdNoPage;
            if (k == 5) u.focused = 0;
            if (utEditTick(&y, u, nullptr) != kUtEdWhyNone && y.state == kUtEdClosed && !utEditHidesItems(y, u.now))
                ++back;
        }
        ok(back == 6, "every close of the tick (gone, layout, size, scale, page, focus): the items are back");
    }

    printf("\n13. the game's own Transfer page: a part over the real cells\n");
    {
        UtLayoutLive coll, real;
        const bool made = live(1.0f, nullptr, nullptr, &coll, nullptr, nullptr, "27,126,512,384") &&
                          live(1.0f, nullptr, nullptr, &real);
        UtEdit tp = {};
        ok(made && utEditOpen(&tp, openIn(real, 1.0f, kUtEdTransferPage, 1000, 0)) && tp.gridMovable == 0 &&
               eq(tp.rect[kUtLayGrid], 27, 126, 512, 480),
           "the editor opens on the real Transfer page: the real sack's grid 27,126,512,480, fixed");
        tp.rect[kUtLayPad] = UtRect{27, 512, 512, 60};
        ok(utEditCheck(&tp, coll.in) && tp.ok == 1,
           "a 16 x 12 grid line: the pad over the real sack's rows 12-13 passes (clear of the collection's grid)");
        ok(!utEditCheck(&tp, real.in) && tp.partWhy[kUtLayPad] == kUtLayoutOverlap && tp.against[kUtLayPad] == kUtLayGrid,
           "no grid line: the collection's grid IS the real sack - the same pad is refused (the collection page "
           "would drop it)");
        UtEdit cg = tp;
        cg.group = 3;
        ok(!utEditCheck(&cg, coll.in) && cg.partWhy[kUtLayPad] == kUtLayoutOverlap,
           "a collection group whose grid cannot be set: judged against the derived grid, as before");
        utEditCheck(&tp, coll.in);
        UtEditSavePlan plan;
        utEditSavePlanOf(tp, "565x637", &plan);
        ok(planValue(plan, "pad_565x637") && !strcmp(planValue(plan, "pad_565x637"), "27,512,512,60") &&
               !planHas(plan, "grid_565x637"),
           "SAVE on the real page: the pad's line, never the grid's");
        tp.rect[kUtLayPad] = UtRect{27, 400, 512, 60};
        ok(!utEditCheck(&tp, coll.in) && tp.partWhy[kUtLayPad] == kUtLayoutOverlap && tp.against[kUtLayPad] == kUtLayGrid,
           "a pad over the collection's own 16 x 12 grid: refused on the real page too");
        char rw[160];
        utEditRefusal(tp, kUtLayPad, rw, sizeof(rw));
        ok(!strcmp(rw, "the pad overlaps another part of the page: the collection's item grid"),
           "  its words name the collection's item grid, not the outlined real sack's", rw);
        utEditRefusal(cg, kUtLayPad, rw, sizeof(rw));
        ok(!strcmp(rw, "the pad overlaps another part of the page: the grid"), "  a collection group's words: the grid", rw);
        tp.rect[kUtLayPad] = UtRect{27, 512, 512, 60};
        tp.labelInPad = 0;
        tp.rect[kUtLayLabel] = UtRect{300, 580, 200, 20};
        ok(utEditCheck(&tp, coll.in) && tp.ok == 1, "a label of the player's over the real sack's row 14: passes");
    }

    printf("\n14. the press the editor claims (in the window or out of it)\n");
    {
        // The base window at UI scale 1.375 (777 x 876 screen px); the grid moved to 21,120,544,160
        // and its right edge pressed at 779,271 of the window - 2 px past the window's right edge.
        const float s = 1.375f;
        UtLayoutLive clay;
        UtEdit ce = {};
        ok(live(s, nullptr, nullptr, &clay) && utEditOpen(&ce, openIn(clay, s, 0, 1000, 1)), "the editor opens, the grid movable");
        ce.rect[kUtLayGrid] = UtRect{21, 120, 544, 160};
        const UtRectF win = utEditWindowClient(ce);
        const UtEdHit h = utEditHitTest(ce, 779.0f, 271.0f);
        const UtEdPressMark none = {};
        ok(!(779.0f < win.x + win.w) && h.kind == kUtEdHitHandle && h.part == kUtLayGrid && h.edges == kUtEdgeR,
           "779,271 is outside the window and on the grid's right-edge handle");
        ok(utEditClaims(ce, 0, none, 1000, 779.0f, 271.0f), "  the editor claims it: the engine never sees that press");
        ok(utEditClaims(ce, 0, none, 1000, cx(ce.rect[kUtLayGrid], s), cy(ce.rect[kUtLayGrid], s)) &&
               utEditClaims(ce, 0, none, 1000, cx(ce.tool[kUtEdSave], s), cy(ce.tool[kUtEdSave], s)),
           "the grid's body and the SAVE button inside the window: claimed too");
        ok(!utEditClaims(ce, 0, none, 1000, 900.0f, 271.0f) && !utEditClaims(ce, 0, none, 1000, 400.0f, 60.0f),
           "a point on no part, outside the window or inside it: not the editor's");
        ok(!utEditClaims(ce, kUtEdGoneMs + 1, none, 1000, 779.0f, 271.0f), "not drawn for over 400 ms: nothing claimed");
        UtEdPressMark m = {};
        utEditMarkPress(&m, 2000, 300.0f, 40.0f);
        UtEdit c = ce;
        utEditClose(&c, kUtEdWhyCancel);
        ok(utEditClaims(c, 0, m, 2100, 301.5f, 38.5f),
           "the press just taken (CANCEL closed the editor first): its copy within 2 px and 400 ms is claimed");
        ok(!utEditClaims(c, 0, m, 2401, 300.0f, 40.0f) && !utEditClaims(c, 0, m, 2100, 303.0f, 40.0f),
           "  after 400 ms, or 3 px away: not");
        UtEdPressMark mt = m;
        const bool first = utEditClaimTake(c, 0, &mt, 2100, 301.5f, 38.5f);
        ok(first && mt.have == 0 && !utEditClaimTake(c, 0, &mt, 2150, 300.0f, 40.0f),
           "the mark answers one DOWN: a second quick click after the CANCEL is not the editor's");
        UtEdPressMark mo = m;
        ok(utEditClaimTake(ce, 0, &mo, 2100, 779.0f, 271.0f) && mo.have == 1 &&
               utEditClaimTake(ce, 0, &mo, 2100, 300.0f, 40.0f) && mo.have == 0,
           "  a press on the open editor's parts leaves the mark; its own copy answers it");
        UtEdit d = ce;
        ok(utEditLeftDown(&d, 779.0f, 271.0f, 1, 1, 0, 0) == kUtEdActDrag && d.state == kUtEdDrag &&
               utEditClaims(d, 0, m, 1000, 779.0f, 271.0f),
           "mid-drag (the message came first): the press point is still the editor's");
    }

    printf("\n15. RESET on the game's own Transfer page brings the grid back too\n");
    {
        UtLayoutLive coll, real;
        const bool made = live(1.0f, nullptr, nullptr, &coll, nullptr, nullptr, "27,126,512,384") &&
                          live(1.0f, nullptr, nullptr, &real);
        UtEdit tp = {};
        ok(made && utEditOpen(&tp, openIn(real, 1.0f, kUtEdTransferPage, 1000, 0)) && tp.gridReset == 0,
           "the editor on the real page, the collection's grid line 27,126,512,384");
        tp.rect[kUtLayPad] = UtRect{27, 512, 512, 60};
        ok(utEditCheck(&tp, coll.in), "before RESET: a pad over the real rows 12-13 is clear of the collection's grid");
        UtEdit r = tp;
        ok(utEditLeftDown(&r, cx(r.tool[kUtEdReset], 1.0f), cy(r.tool[kUtEdReset], 1.0f), 1, 1, 0, 0) == kUtEdActReset &&
               r.gridReset == 1 && utRectEq(r.rect[kUtLayPad], r.def[kUtLayPad]) && r.labelInPad == 1,
           "RESET: the derived pad and label, and the grid marked for its line's removal");
        UtEditSavePlan plan;
        utEditSavePlanOf(r, "565x637", &plan);
        ok(planHas(plan, "grid_565x637") && !planValue(plan, "grid_565x637") && planHas(plan, "pad_565x637") &&
               !planValue(plan, "pad_565x637") && planHas(plan, "label_565x637") && plan.n == 5,
           "SAVE after it removes grid_565x637 with the pad's and the label's lines (GD's three keys)");
        ok(utEditCheck(&r, coll.in), "  the derived three pass");
        r.rect[kUtLayPad] = UtRect{27, 512, 512, 60};
        ok(!utEditCheck(&r, coll.in) && r.partWhy[kUtLayPad] == kUtLayoutOverlap && r.against[kUtLayPad] == kUtLayGrid,
           "  then judged against the derived grid (the line goes): that pad is refused now");
        UtEdit cg = {};
        ok(utEditOpen(&cg, openIn(coll, 1.0f, 0, 1000, 1)) && eq(cg.rect[kUtLayGrid], 27, 126, 512, 384) &&
               utEditLeftDown(&cg, cx(cg.tool[kUtEdReset], 1.0f), cy(cg.tool[kUtEdReset], 1.0f), 1, 1, 0, 0) ==
                   kUtEdActReset &&
               utRectEq(cg.rect[kUtLayGrid], cg.def[kUtLayGrid]) && cg.gridReset == 0,
           "a collection page (the grid movable): RESET puts the grid itself back, as before");
        utEditSavePlanOf(cg, "565x637", &plan);
        ok(planHas(plan, "grid_565x637") && !planValue(plan, "grid_565x637"), "  SAVE removes its line");
    }

    printf("\n16. a label in a screen pad: the editor judges it on the screen (660x699, UI scale 1.375)\n");
    {
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
        UtLayoutLive sl;
        g_ox = 807.0f;
        g_oy = 239.0f;
        UtEditOpenIn oi = {};
        const bool made = utLayoutFromSource(sv, &sl);
        if (made) {
            oi = openIn(sl, s, 0, 1000, 1, 1);
            oi.screenW = 3440;
            oi.coverW = 660;
            oi.coverH = 699;
        }
        UtEdit ce = {};
        ok(made && utEditOpen(&ce, oi) && ce.scr[kUtLayPad] == 1 && ce.labelInPad == 1 && ce.scr[kUtLayLabel] == 1 &&
               eq(ce.rect[kUtLayGrid], 11, 57, 640, 608),
           "open: grid 11,57,640,608, the pad on the screen, the label in it");
        ce.rect[kUtLayGrid] = UtRect{11, 57, 640, 640};
        ok(utEditCheck(&ce, sl.in) && ce.partWhy[kUtLayGrid] == kUtLayoutOk,
           "the grid's bottom edge to 640: SAVE stays on (no label in the window to meet)");
        UtEdit w = ce;
        w.scr[kUtLayPad] = 0;
        w.scr[kUtLayLabel] = 0;
        w.rect[kUtLayPad] = w.def[kUtLayPad];
        ok(!utEditCheck(&w, sl.in) && w.partWhy[kUtLayGrid] == kUtLayoutOverlap,
           "the pad back in the window: the same grid meets the pad's band and is refused");
    }

    printf("\nE. the EDIT button: the editor's pad floor and the label box of a moved pad\n");
    {
        UtEdit x = {};
        x.rect[kUtLayPad] = UtRect{20, 520, 500, 40};
        x.def[kUtLayPad] = UtRect{30, 607, 500, 30};
        x.groups = 15;
        x.gap = 1;
        x.scale = 1.0f;
        x.widest = 5;
        x.nextChars = 2;
        x.edit = 0;
        const UtRect b0 = utEditPadLabelBox(x);
        x.edit = 1;
        const UtRect b1 = utEditPadLabelBox(x);
        const UtPadFit f = utPadFitRect(x.rect[kUtLayPad], 15, 1, 1.0f, 5, 2);
        char d[160];
        _snprintf_s(d, sizeof(d), _TRUNCATE, "box %d,%d %dx%d -> %d,%d %dx%d, step %d", b0.x, b0.y, b0.w, b0.h, b1.x, b1.y,
                    b1.w, b1.h, f.stepW + f.gap);
        ok(kUtEdMinPadW == 260 && f.ok && b0.w > 0 && b1.x == b0.x + f.stepW + f.gap && b1.x + b1.w == b0.x + b0.w &&
               b1.y == b0.y && b1.h == b0.h,
           "kUtEdMinPadW stays 260; with EDIT the label box of a moved pad starts one cell later, its right edge kept",
           d);
        UtEditOpenIn oi = openIn(lay, 1.0f, 3, 1000);
        oi.edit = 1;
        UtEdit y = {};
        ok(utEditOpen(&y, oi) && y.edit == 1, "the editor takes the pad's EDIT flag at the open");
    }
    printf("\n11. the relic area: the editor on a relic page writes relics_<W>x<H>\n");
    {
        UtEditOpenIn oi = openIn(lay, 1.0f, 3, 1000, 1);
        oi.relicArea = 1;
        oi.def[kUtLayGrid] = UtRect{24, 124, 512, 480};   // the collection's grid in force (a grid_ line)
        oi.inForce[kUtLayGrid] = oi.def[kUtLayGrid];        // no relics_ line: the relic page shows it
        UtEdit ra = {};
        ok(utEditOpen(&ra, oi) && ra.relicArea == 1 && ra.gridMovable == 1 && eq(ra.rect[kUtLayGrid], 24, 124, 512, 480),
           "opened on a relic page: the grid part is the relic area, at the collection's grid in force");
        UtEditSavePlan plan;
        utEditSavePlanOf(ra, "565x637", &plan);
        ok(plan.n == 5 && planHas(plan, "relics_565x637") && !planValue(plan, "relics_565x637") &&
               !planHas(plan, "grid_565x637"),
           "untouched: SAVE removes relics_565x637 (no line = the collection's grid) and never touches grid_565x637");
        UtEdit m = dragged(ra, kUtLayGrid, kUtEdgeR, -100.0f, 0.0f);
        utEditSavePlanOf(m, "565x637", &plan);
        const char* v = planValue(plan, "relics_565x637");
        ok(v && !strcmp(v, "24,124,416,480") && !planHas(plan, "grid_565x637") && planHas(plan, "pad_565x637"),
           "a smaller relic area: SAVE writes relics_565x637=24,124,416,480 (whole cells), the pad and label as ever",
           v ? v : "(none)");
        UtEdit back = m;
        back.rect[kUtLayGrid] = back.def[kUtLayGrid];
        utEditSavePlanOf(back, "565x637", &plan);
        ok(planHas(plan, "relics_565x637") && !planValue(plan, "relics_565x637"),
           "back at the collection's grid (RESET): relics_565x637 is removed");
        UtEdit over = dragged(ra, kUtLayGrid, kUtEdgeB, 0.0f, 100.0f);
        ok(over.rect[kUtLayGrid].y + over.rect[kUtLayGrid].h > 606 && !utEditCheck(&over, lay.in) &&
               over.partWhy[kUtLayGrid] != kUtLayoutOk,
           "a relic area over the pad is refused by the grid's own rule: SAVE off");
        int moved = 0;
        UtEdit stay = ra;
        UtEdTick t = tickAt(stay, 1100);
        t.relicArea = 1;
        ok(utEditTick(&stay, t, &moved) == kUtEdWhyNone && stay.state == kUtEdOpen,
           "another relic page: the editor stays open");
        t = tickAt(stay, 1150);
        t.relicArea = 0;
        ok(utEditTick(&stay, t, &moved) == kUtEdWhyKind && stay.state == kUtEdClosed &&
               strstr(utEditWhyText(kUtEdWhyKind), "nothing was written") != nullptr,
           "a unique page under an open relic-area editor: closed, nothing written");
        UtEdit uq = {};
        ok(utEditOpen(&uq, openIn(lay, 1.0f, 3, 1000, 1)) && uq.relicArea == 0, "a unique page: the grid part is the item grid");
        utEditSavePlanOf(uq, "565x637", &plan);
        ok(planHas(plan, "grid_565x637") && !planHas(plan, "relics_565x637"), "... and SAVE never names relics_565x637");
        UtEdTick tu = tickAt(uq, 1100);
        tu.relicArea = 1;
        ok(utEditTick(&uq, tu, &moved) == kUtEdWhyKind, "a relic page under an open grid editor: closed");
    }
    resetInWindow();
    printf("\n%d checks, %d failed - %s\n", g_pass + g_fail, g_fail,
           g_fail ? "LAYOUT EDITOR TESTS FAILED" : "LAYOUT EDITOR TESTS PASSED");
    return g_fail ? 1 : 0;
}
