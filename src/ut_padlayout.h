// ut_padlayout.h - WHERE the collection pad goes. Pure arithmetic (no engine, no Windows, no
// allocation): the caravan frame derived from a proven hover, the compact two-row pad inside that
// frame's band below the grid, the lone toggle used before the first measurement, and the short
// captions. ut_panel.cpp draws and hit-tests from these rects only; tools\test_viewgate.cpp tests
// them with a measured 1366 x 768 window (grid at (93,177)) at UI scale 1.0 / 1.25 / 0.8.
//
// WHY: the column was placed from the caravan window's RECORD rect (10,66)
// 565 x 637, but the engine lays the window out elsewhere (the grid it measured spans x 93..605, so
// the frame sits at about x 66..631) - the column at x 581 covered the grid's last column and the
// character window. The pad now follows the LIVE frame: the page mouse handler's parent origin is
// the Transfer page's own absolute origin (TQ.exe 0xC31A0, the page window's draw, passes
// `parent origin + its own position` to the inventory at +0x88; the handler 0xBFED0 adds its
// [this+0x10] page pos to that origin), so the frame = parent origin - the page's place inside the
// frame (TransferWindow.dbr (0,126) x UI scale), sized by caravanwindow.dbr (565 x 637 x scale).
// The measured grid must lie inside that frame, and the frame inside the canvas - otherwise the
// frame is refused (the pad and the label are then not drawn while the view is ON).
//
// The pad (GD's flat 9 x 3 design, re-cut for 15 groups; record px, scaled by the UI scale with
// both edges of every rect rounded, GD's rule, so gutters stay exact):
//   row 1  the 15 group buttons, cellW x cellH each (cellW = 34 when the grid is wide enough,
//          else the widest that keeps the whole pad inside the grid's own width: 32 at 512 px)
//   row 2  Transfer (2 cells) | OWN (1 cell) | < | > (half a cell each): the four in 4 cells; then
//          the search field (3 cells, its clear square at the right end; empty ground with
//          search=0; with the view OFF only the toggle, the field and the label are shown);
//          then the label (the remaining 8 cells - the same box whether the search is on or off)
//   a 3 px margin round both rows, a pad_gap gutter between cells.
// Placed centred under the grid (+ pad_x, clamped to the frame's edges -), pad_y
// below its bottom row; a band too short is tried again with pad_y 0, then with 12 px rows. The band
// above the grid (below the tab row) is tried last, but with TQ's caravan records it can never fit:
// the grid's top is at frame + 111 record px, above the tab row's bottom (113) -.
// So in practice: no room below = no pad (one WARN). no longer - the pad is FITTED to
// the band (utPadLayout); the band above is never tried.
// The frame FOLLOWS the window: utFrameTrackStep derives it again whenever a
// proven hover's grid origin moves.
#pragma once

#include <stdio.h>   // _snprintf_s: the label's line

namespace ut {

struct UtRectF {
    float x, y, w, h;
};

inline bool utPadFinite(float v) { return v == v && v > -1.0e7f && v < 1.0e7f; }

inline bool utRectInside(const UtRectF& in, const UtRectF& out, float tol) {
    return in.x >= out.x - tol && in.y >= out.y - tol && in.x + in.w <= out.x + out.w + tol &&
           in.y + in.h <= out.y + out.h + tol;
}

inline bool utRectsMeet(const UtRectF& a, const UtRectF& b) {
    return a.w > 0.0f && a.h > 0.0f && b.w > 0.0f && b.h > 0.0f && a.x < b.x + b.w &&
           b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

inline bool utRectHas(const UtRectF& r, float x, float y) {
    return r.w > 0.0f && r.h > 0.0f && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

inline float utPadFloor(float v) {   // floorf(v) without <math.h> (|v| < 1e7)
    const float t = (float)(long)v;
    return t > v ? t - 1.0f : t;
}
inline float utPadCeil(float v) { return -utPadFloor(-v); }

inline float utPadRound(float v) {   // floorf(v + 0.5f) without <math.h>
    const float t = v + 0.5f;
    int i = (int)t;
    if ((float)i > t) --i;
    return (float)i;
}

// ---- the frame ---------------------------------------------------------------------------------
const int kUtPadGridCols = 16;   // the Transfer sack, forced by its accessor
const int kUtPadGridRows = 15;
const float kUtFrameTol = 1.0f;  // DRAWN px (canvas px): every frame / grid number is drawn px

struct UtFrameIn {
    float originX, originY;   // the page mouse handler's parent origin (canvas px)
    float gridX, gridY;       // page pos + parent origin: the grid's top-left (canvas px)
    float cellW, cellH;       // the sack's cell size (canvas px)
    float scale;              // Engine::GetUIScale
    float canvasW, canvasH;
    float pageX, pageY;       // record: the Transfer page inside the caravan window (0, 126)
    float frameW, frameH;     // record: the caravan window (565 x 637)
};

enum {
    kUtFrameOk = 1,
    kUtFrameBadInput = -1,     // a NaN, a scale outside (0.3, 4), an empty cell or canvas
    kUtFrameOffCanvas = -2,    // the derived frame leaves the canvas
    kUtFrameGridOutside = -3,  // the measured grid is not inside the derived frame
};

inline int utFrameFromHover(const UtFrameIn& in, UtRectF* frame, UtRectF* grid) {
    const float s = in.scale;
    if (!utPadFinite(in.originX) || !utPadFinite(in.originY) || !utPadFinite(in.gridX) ||
        !utPadFinite(in.gridY) || !utPadFinite(s) || !(s > 0.3f && s < 4.0f) ||
        !(in.cellW > 0.0f && in.cellW < 1000.0f) || !(in.cellH > 0.0f && in.cellH < 1000.0f) ||
        !(in.canvasW > 0.0f) || !(in.canvasH > 0.0f) || !(in.frameW > 0.0f) ||
        !(in.frameH > 0.0f) || !utPadFinite(in.pageX) || !utPadFinite(in.pageY))
        return kUtFrameBadInput;
    const UtRectF f = {in.originX - in.pageX * s, in.originY - in.pageY * s, in.frameW * s,
                       in.frameH * s};
    const UtRectF g = {in.gridX, in.gridY, (float)kUtPadGridCols * in.cellW,
                       (float)kUtPadGridRows * in.cellH};
    if (frame) *frame = f;
    if (grid) *grid = g;
    const UtRectF canvas = {0.0f, 0.0f, in.canvasW, in.canvasH};
    if (!utRectInside(f, canvas, kUtFrameTol)) return kUtFrameOffCanvas;
    if (!utRectInside(g, f, kUtFrameTol)) return kUtFrameGridOutside;
    return kUtFrameOk;
}

inline const char* utFrameWhyText(int r) {
    return r == kUtFrameOk            ? "ok"
           : r == kUtFrameOffCanvas   ? "the derived frame leaves the canvas"
           : r == kUtFrameGridOutside ? "the measured grid is not inside the derived frame"
                                      : "a number is not usable (NaN, scale, cell or canvas)";
}

// ---- the frame follows the window ----------------------------------------------
// A decided frame (accepted or refused) stands while the proven hovers report the grid origin it was
// decided on (within kUtFrameTol); a hover whose origin moved derives it again. More than
// kUtFrameMovesMax moves in one showing of the page = the window will not hold still: refused until
// the page is shown again (no pad and no label while ON). A non-finite origin is no evidence.
const int kUtFrameMovesMax = 16;
struct UtFrameTrack {
    int verdict;          // 0 undecided, 1 accepted, -1 refused
    float gridX, gridY;   // the grid origin the verdict was decided on
    int moves;            // derivations after the first, in this showing
    bool unstable;
};
enum { kUtFrameTrackKeep = 0, kUtFrameTrackDerive = 1, kUtFrameTrackUnstable = 2 };

inline void utFrameTrackReset(UtFrameTrack* t) {
    t->verdict = 0;
    t->gridX = 0.0f;
    t->gridY = 0.0f;
    t->moves = 0;
    t->unstable = false;
}

inline bool utFrameMoved(float ax, float ay, float bx, float by) {
    const float dx = ax - bx, dy = ay - by;
    return !(dx <= kUtFrameTol && dx >= -kUtFrameTol && dy <= kUtFrameTol && dy >= -kUtFrameTol);
}

inline int utFrameTrackStep(UtFrameTrack* t, float gridX, float gridY) {
    if (t->unstable) return kUtFrameTrackKeep;
    if (t->verdict == 0) return kUtFrameTrackDerive;
    if (!utPadFinite(gridX) || !utPadFinite(gridY)) return kUtFrameTrackKeep;
    if (!utFrameMoved(gridX, gridY, t->gridX, t->gridY)) return kUtFrameTrackKeep;
    if (t->moves >= kUtFrameMovesMax) {
        t->unstable = true;
        t->verdict = -1;
        return kUtFrameTrackUnstable;
    }
    ++t->moves;
    return kUtFrameTrackDerive;
}

inline void utFrameTrackDecided(UtFrameTrack* t, int verdict, float gridX, float gridY) {
    t->verdict = verdict;
    t->gridX = gridX;
    t->gridY = gridY;
}

// ---- the pad -----------------------------------------------------------------------------------
enum {
    kUtPadToggle = 0,   // Collection: the view toggle (drawn whenever the Transfer tab is shown)
    kUtPadPrev = 1,     // <  (view ON)
    kUtPadNext = 2,     // >  (view ON)
    kUtPadGroup0 = 3,   // 3..17: the 15 groups of uniq-groups.txt, in order (view ON)
    kUtPadOwn = 18,     // OWN: GD's owned-only filter (view ON)
    kUtPadSearch = 19,  // the search field (search=1; also with the view OFF): the 3 cells right of >
    kUtPadClear = 20,   // the field's clear button: a square of the row's height at its right end
    kUtPadMax = 21
};
const int kUtPadGroups = 15;
const int kUtPadMargin = 3;      // record px round the two rows (GD's design)
const int kUtPadCellWMax = 34;   // GD's cell width
const int kUtPadCellWMin = 20;   // below this the captions cannot be read: refused
const int kUtPadTabRowBottom = 113;   // caravan record: the tab buttons at y 82, 31 high

// pad_h / pad_gap, clamped on USE (GD: 12..20 and 0..4; the ini clamps them too).
inline int utPadClampH(int h) { return h < 12 ? 12 : (h > 20 ? 20 : h); }
inline int utPadClampGap(int g) { return g < 0 ? 0 : (g > 4 ? 4 : g); }

struct UtPadIn {
    UtRectF frame;           // the caravan frame (canvas px): the pad never leaves it
    UtRectF grid;            // the 16 x 15 grid (canvas px): the pad never meets it
    float scale;
    float canvasW, canvasH;
    int dx, dy;              // pad_x / pad_y: record px right of the centred spot / off the grid
    int cellH, gap;          // pad_h / pad_gap
    bool search = false;     // the search field is laid out (search=1); off = the label as before
};

enum { kUtPadBadInput = -1, kUtPadNoRoom = 0, kUtPadBelow = 1, kUtPadAbove = 2, kUtPadBorder = 3 };

// how the pad was fitted into the band below the grid (the log names it).
enum {
    kUtPadFitAsSet = 0,       // pad_y, pad_gap, pad_h as set (record px x scale, GD's rounding)
    kUtPadFitNoPadY = 1,      // pad_y dropped
    kUtPadFitNoGap = 2,       // ... and the gap between the rows
    kUtPadFitRows = 3,        // ... and the rows shrunk toward GD's floor (the gap regrown if room)
    kUtPadFitMargin = 4,      // ... and the margin cut to 1 px
    kUtPadFitBelowFloor = 5,  // rows under GD's floor (a shorter caption font; the caller WARNs)
    kUtPadFitBorder = 6,      // no 2-row pad fits the band: over the frame's bottom border (WARN)
};
const int kUtPadRowFloor = 12;        // record px: GD's pad_h floor (the ini clamps 12..20)
const float kUtPadRowMinPx = 8.0f;    // drawn px: a size-6 caption with 1 px above and below

struct UtPadOut {
    UtRectF ground;          // the whole pad (its dark ground)
    UtRectF btn[kUtPadMax];
    UtRectF label;           // the label's box in row 2
    UtRectF fieldText;       // the field's text area: the field minus the clear square and a gap
    int band;                // kUtPadBelow / kUtPadBorder
    int cellW, cellH, gap;   // record px: cellW in use, cellH = rowPx / scale rounded, gap as set
    float rowPx, gapPx, marginPx, dyPx;   // the DRAWN px in use
    int fit;                 // kUtPadFit*
};

// The rect of record box (rx, ry, rw, rh) of a pad whose top-left is (ox, oy): both edges rounded.
inline UtRectF utPadRect(float ox, float oy, float s, float rx, float ry, float rw, float rh) {
    const float x0 = utPadRound(rx * s), y0 = utPadRound(ry * s);
    const float x1 = utPadRound((rx + rw) * s), y1 = utPadRound((ry + rh) * s);
    const UtRectF r = {ox + x0, oy + y0, x1 - x0, y1 - y0};
    return r;
}
// record px across (both edges rounded, GD's rule), drawn px down (the fitted rows).
inline UtRectF utPadRectX(float ox, float s, float rx, float rw, float y, float h) {
    const float x0 = utPadRound(rx * s), x1 = utPadRound((rx + rw) * s);
    const UtRectF r = {ox + x0, y, x1 - x0, h};
    return r;
}
// The same from the two record edges [rx0, rx1): two rects that share an edge value meet exactly.
inline UtRectF utPadRectEdges(float ox, float s, float rx0, float rx1, float y, float h) {
    const float x0 = utPadRound(rx0 * s), x1 = utPadRound(rx1 * s);
    const UtRectF r = {ox + x0, y, x1 - x0, h};
    return r;
}

inline const char* utPadFitText(int fit) {
    return fit == kUtPadFitAsSet        ? "as set"
           : fit == kUtPadFitNoPadY     ? "pad_y dropped"
           : fit == kUtPadFitNoGap      ? "pad_y and the row gap dropped"
           : fit == kUtPadFitRows       ? "pad_y dropped, rows fitted between pad_h and GD's floor"
           : fit == kUtPadFitMargin     ? "pad_y dropped, rows at GD's floor, margin 1 px"
           : fit == kUtPadFitBelowFloor ? "rows UNDER GD's floor (shorter captions)"
                                        : "no 2-row pad fits the band: over the frame's bottom border";
}

// the pad ALWAYS fits the band below the grid. Everything is record px x
// the UI scale rounded as GD does (floorf(v * s + 0.5f)); the band is the frame's bottom minus the
// grid's bottom in DRAWN px, which is 31 record px x scale only up to rounding (the grid is 15 x the
// engine's rounded cell). An earlier pad vanished at 1.375: 31 x 1.375 = 42.6 px of band, 43 px of pad.
// The fit, first that holds: as set; pad_y 0; the row gap 0; the tallest rows from pad_h down to
// GD's floor (12 record px), the gap regrown from what is left; the margin 1 px; rows under the
// floor down to 8 drawn px (the caller WARNs: shorter captions); and only when even that 2-row pad
// cannot fit, the pad over the frame's bottom border (below the grid, past the frame, inside the
// canvas - kUtPadBorder). kUtPadNoRoom only when not even that is on the canvas. The band above
// the grid is never tried: with TQ's records it can never fit.
inline int utPadLayout(const UtPadIn& in, UtPadOut* out) {
    const float s = in.scale;
    if (!out || !utPadFinite(s) || !(s > 0.3f && s < 4.0f) || !utPadFinite(in.frame.x) ||
        !utPadFinite(in.frame.y) || !utPadFinite(in.grid.x) || !utPadFinite(in.grid.y) ||
        !(in.grid.w > 0.0f) || !(in.grid.h > 0.0f) || !(in.frame.w > 0.0f) ||
        !(in.frame.h > 0.0f) || !(in.canvasW > 0.0f) || !(in.canvasH > 0.0f) ||
        in.dx < -200 || in.dx > 200 || in.dy < 0 || in.dy > 200)
        return kUtPadBadInput;
    const int m = kUtPadMargin;
    const int gap = utPadClampGap(in.gap);
    const float gridWrec = in.grid.w / s;
    int cw = (int)((gridWrec - 2.0f * (float)m - (float)((kUtPadGroups - 1) * gap)) /
                   (float)kUtPadGroups);
    if (cw > kUtPadCellWMax) cw = kUtPadCellWMax;
    if (cw < kUtPadCellWMin) return kUtPadNoRoom;
    const float wRec = (float)(2 * m + kUtPadGroups * cw + (kUtPadGroups - 1) * gap);
    const float wPx = utPadRound(wRec * s);
    float ox = in.grid.x + utPadRound(((gridWrec - wRec) * 0.5f + (float)in.dx) * s);
    {   // pad_x moves the pad up to the frame's edges, never past them
        const float lo = utPadCeil(in.frame.x), hi = utPadFloor(in.frame.x + in.frame.w) - wPx;
        if (lo > hi) return kUtPadNoRoom;
        if (ox < lo) ox = lo;
        if (ox > hi) ox = hi;
    }
    // ---- down: the band below the grid, in drawn px ----
    const float top = in.grid.y + in.grid.h;   // never above: the pad never meets the grid
    float bottom = in.frame.y + in.frame.h;
    if (bottom > in.canvasH) bottom = in.canvasH;
    const float band = bottom - top;
    const float m0 = utPadRound((float)m * s);
    const float gap0 = utPadRound((float)gap * s);
    const float dy0 = utPadRound((float)in.dy * s);
    const float rowSet = utPadRound((float)utPadClampH(in.cellH) * s);
    float rowFloor = utPadRound((float)kUtPadRowFloor * s);
    if (rowFloor > rowSet) rowFloor = rowSet;
    if (rowFloor < kUtPadRowMinPx) rowFloor = kUtPadRowMinPx;
    const float m1 = m0 < 1.0f ? m0 : 1.0f;
    struct Step {
        float dy, gap, m, rowHi, rowLo;
        int fit;
    };
    const Step steps[6] = {
        {dy0, gap0, m0, rowSet, rowSet, kUtPadFitAsSet},
        {0.0f, gap0, m0, rowSet, rowSet, kUtPadFitNoPadY},
        {0.0f, 0.0f, m0, rowSet, rowSet, kUtPadFitNoGap},
        {0.0f, 0.0f, m0, rowSet, rowFloor, kUtPadFitRows},
        {0.0f, 0.0f, m1, rowFloor, rowFloor, kUtPadFitMargin},
        {0.0f, 0.0f, m1, rowFloor, kUtPadRowMinPx, kUtPadFitBelowFloor},
    };
    int fit = -1;
    float dy = 0.0f, gp = 0.0f, mg = 0.0f, row = 0.0f;
    for (int i = 0; i < 6 && fit < 0; ++i) {
        const Step& st = steps[i];
        float r = utPadFloor((band - st.dy - 2.0f * st.m - st.gap) * 0.5f);
        if (r > st.rowHi) r = st.rowHi;
        if (!(r >= st.rowLo)) continue;
        fit = st.fit;
        dy = st.dy;
        gp = st.gap;
        mg = st.m;
        row = r;
    }
    if (fit >= kUtPadFitRows) {   // the gap back, as far as the rows leave room for it
        const float left = utPadFloor(band - 2.0f * mg - 2.0f * row);
        gp = left < gap0 ? (left > 0.0f ? left : 0.0f) : gap0;
    }
    int where = kUtPadBelow;
    float oy = top + dy;
    if (fit < 0) {   // not even rows of 8 px: over the frame's bottom border, inside the canvas
        fit = kUtPadFitBorder;
        where = kUtPadBorder;
        dy = 0.0f;
        gp = 0.0f;
        mg = m1;
        row = rowFloor;
        const float h = 2.0f * mg + 2.0f * row;
        oy = top;
        if (oy + h > in.canvasH) oy = utPadFloor(in.canvasH - h);
        if (oy < top - 0.001f) return kUtPadNoRoom;   // it would meet the grid
    }
    const float hPx = 2.0f * mg + 2.0f * row + gp;
    out->band = where;
    out->fit = fit;
    out->cellW = cw;
    out->cellH = (int)utPadRound(row / s);
    out->gap = gap;
    out->rowPx = row;
    out->gapPx = gp;
    out->marginPx = mg;
    out->dyPx = dy;
    out->ground = UtRectF{ox, oy, wPx, hPx};
    const float step = (float)(cw + gap);
    const float r1 = oy + mg, r2 = oy + mg + row + gp;
    for (int i = 0; i < kUtPadGroups; ++i)
        out->btn[kUtPadGroup0 + i] =
            utPadRectX(ox, s, (float)m + (float)i * step, (float)cw, r1, row);
    // row 2: the four buttons in 4 cells + 3 gaps - Transfer 2 steps, OWN 1 step, < and > half a
    // step each - every button ending one gap before the next one's left edge, the last at
    // m + 4 steps - gap (so OWN is one cell, < and > half of the last cell each, a gap between);
    // the search field in the next 3 cells + 2 gaps.
    const float gapF = (float)gap;
    const float e[5] = {(float)m, (float)m + 2.0f * step, (float)m + 3.0f * step,
                        (float)m + 3.5f * step, (float)m + 4.0f * step};
    static const int kRow2[4] = {kUtPadToggle, kUtPadOwn, kUtPadPrev, kUtPadNext};
    for (int i = 0; i < 4; ++i)
        out->btn[kRow2[i]] = utPadRectEdges(ox, s, e[i], e[i + 1] - gapF, r2, row);
    // the field (search=1): its clear square, the row's height, at its right end INSIDE it; the
    // text area is the rest less one gap. With the search off both are empty rects and the three
    // cells are the pad's ground.
    if (in.search) {
        const UtRectF f = utPadRectEdges(ox, s, e[4], (float)m + 7.0f * step - gapF, r2, row);
        float side = row;
        if (side > utPadFloor(f.w * 0.5f)) side = utPadFloor(f.w * 0.5f);
        out->btn[kUtPadSearch] = f;
        out->btn[kUtPadClear] = UtRectF{f.x + f.w - side, r2, side, row};
        float tw = f.w - side - utPadRound(gapF * s);
        if (tw < 0.0f) tw = 0.0f;
        out->fieldText = UtRectF{f.x, r2, tw, row};
    } else {
        out->btn[kUtPadSearch] = UtRectF{0.0f, 0.0f, 0.0f, 0.0f};
        out->btn[kUtPadClear] = UtRectF{0.0f, 0.0f, 0.0f, 0.0f};
        out->fieldText = UtRectF{0.0f, 0.0f, 0.0f, 0.0f};
    }
    // the label: from 7 cells + 3 record px to the pad's right margin, the box it always had
    const float fx = (float)m + 7.0f * step;
    const float lx = fx + 3.0f;
    out->label = utPadRectX(ox, s, lx, wRec - (float)m - lx, r2, row);
    return where;
}


// the Collection toggle's own ground while the view is OFF (the other buttons
// hidden): the toggle plus the margin, cut to the pad's own ground - rounding round(a) + round(b)
// can no longer push it 1 px past the pad (and so past the frame). the margin in use.
inline UtRectF utPadToggleGround(const UtPadOut& o, float s) {
    const UtRectF& t = o.btn[kUtPadToggle];
    const float m = o.marginPx > 0.0f ? o.marginPx : utPadRound((float)kUtPadMargin * s);
    float x0 = t.x - m, y0 = t.y - m, x1 = t.x + t.w + m;
    const float y1 = o.ground.y + o.ground.h;
    if (x0 < o.ground.x) x0 = o.ground.x;
    if (y0 < o.ground.y) y0 = o.ground.y;
    if (x1 > o.ground.x + o.ground.w) x1 = o.ground.x + o.ground.w;
    const UtRectF r = {x0, y0, x1 - x0, y1 - y0};
    return r;
}

// ---- (rule): ONE drawn cell for the shown page ---------------------------------
// TQ's InventorySack keeps its cell in DRAWN px: its ctor and InventorySack::OnUIScaleChange
// (Game.dll 0x1B6220, the sack is an options listener) set it with 0x198C40 = floorf(32 x
// Engine::GetUIScale + 0.5f) (when not downsizing) and rescale every item rect by new / old. The
// Transfer page's grid is 16 x 15 of that cell; the mouse point the page handler passes to
// GetItemUnderPoint (0x1B6580: x <= px < x + w on the item's sack rect, no division, no scaling) is
// in the same px. So the drawn cell = the measured grid's width / 16, and it must be the mod
// sack's live cell (1 px); the engine's own formula is the third witness.
inline float utEngineCellPx(float scale) { return utPadRound(32.0f * scale); }

enum { kUtCellOk = 0, kUtCellBadInput = -1, kUtCellGridVsSack = -2, kUtCellNotSquare = -3 };
inline int utDrawnCell(float gridW, float gridH, float sackW, float sackH, float* cellPx) {
    if (!utPadFinite(gridW) || !utPadFinite(gridH) || !(gridW > 0.0f) || !(gridH > 0.0f) ||
        !utPadFinite(sackW) || !utPadFinite(sackH) || !(sackW >= 4.0f) || !(sackH >= 4.0f))
        return kUtCellBadInput;
    const float cw = gridW / (float)kUtPadGridCols, ch = gridH / (float)kUtPadGridRows;
    const float dw = cw - sackW, dh = ch - sackH;
    if (dw > kUtFrameTol || dw < -kUtFrameTol || dh > kUtFrameTol || dh < -kUtFrameTol)
        return kUtCellGridVsSack;
    if (cw - ch > kUtFrameTol || ch - cw > kUtFrameTol) return kUtCellNotSquare;
    if (cellPx) *cellPx = cw;
    return kUtCellOk;
}
inline const char* utDrawnCellWhyText(int r) {
    return r == kUtCellOk            ? "ok"
           : r == kUtCellGridVsSack  ? "the grid / 16 is not the mod sack's cell"
           : r == kUtCellNotSquare   ? "the grid's cell is not square"
                                     : "a number is not usable";
}

inline UtRectF utRectInset(const UtRectF& r, float k) {
    const UtRectF o = {r.x + k, r.y + k, r.w - 2.0f * k, r.h - 2.0f * k};
    return o;
}

// ONE helper: a slot (col, row, w, h in cells) -> its DRAWN rect on the grid (canvas px). Every
// cells-to-px turn of the draw side (plates, slot art, grid cover, ring, veils) goes through it.
inline UtRectF utSlotDrawnRect(const UtRectF& grid, int col, int row, int w, int h) {
    const float cw = grid.w / (float)kUtPadGridCols, ch = grid.h / (float)kUtPadGridRows;
    const UtRectF r = {grid.x + (float)col * cw, grid.y + (float)row * ch, (float)w * cw,
                       (float)h * ch};
    return r;
}
// The search's highlight on one slot (drawn px). The frame lies on the slot's OUTERMOST pixel
// ring (inset 0: clear of the engine's rarity art inside the slot, never in the next slot), 1 px
// thick below a 48 px cell (UI scale under 1.5), 2 px from there. The wash fills the slot inset by
// 1 px, under the icon.
inline float utSearchMarkThick(float cellPx) { return cellPx >= 48.0f ? 2.0f : 1.0f; }
inline UtRectF utSearchMarkRect(const UtRectF& slot, bool wash) {
    return wash ? utRectInset(slot, 1.0f) : slot;
}
// The real Transfer page (the view OFF): one item's rect in the real sack (InventorySack's
// RectExt: drawn px from the grid's top-left, the unit GetItemUnderPoint compares the page point
// with) -> canvas px, both edges rounded. False = not a number, empty, or leaving the 16 x 15 grid
// by more than half a pixel: such an item is never marked.
inline bool utSearchRealRect(const UtRectF& grid, float x, float y, float w, float h, UtRectF* out) {
    if (!utPadFinite(x) || !utPadFinite(y) || !utPadFinite(w) || !utPadFinite(h) ||
        !utPadFinite(grid.x) || !utPadFinite(grid.y) || !(grid.w > 0.0f) || !(grid.h > 0.0f) ||
        !(w >= 1.0f) || !(h >= 1.0f) || x < -0.5f || y < -0.5f || x + w > grid.w + 0.5f ||
        y + h > grid.h + 0.5f)
        return false;
    const float x0 = utPadRound(grid.x + x), y0 = utPadRound(grid.y + y);
    const float x1 = utPadRound(grid.x + x + w), y1 = utPadRound(grid.y + y + h);
    if (out) *out = UtRectF{x0, y0, x1 - x0, y1 - y0};
    return true;
}
// ... and a point on the page (px from the grid's top-left, the handler's point) -> its cell.
// -1 when outside the 16 x 15 grid or not a number.
inline bool utCellAtPoint(float px, float py, float cellPx, int* col, int* row) {
    if (!(cellPx >= 4.0f) || !(px >= 0.0f) || !(py >= 0.0f) || !(px < 1.0e6f) || !(py < 1.0e6f))
        return false;
    const int c = (int)(px / cellPx), r = (int)(py / cellPx);
    if (c >= kUtPadGridCols || r >= kUtPadGridRows) return false;
    if (col) *col = c;
    if (row) *row = r;
    return true;
}

// Before the first measurement (and after a refused one, view OFF): the Collection toggle alone,
// 3 x 32 + 2 record px wide, centred on the RECORD frame's middle, its bottom 10 record px above
// the record frame's bottom. The records put the frame 56 px left of where the engine drew it on
// a measured 1366 x 768 window, so only a small centred button is safe: it stays inside the
// live frame for any horizontal error below ~230 px, far from the character window.
inline UtRectF utPadFallbackToggle(const UtRectF& recordFrame, float s, int cellH) {
    const float ch = (float)utPadClampH(cellH);
    const float wRec = 3.0f * 32.0f + 2.0f;
    const float cx = recordFrame.x + recordFrame.w * 0.5f;
    const float bottom = recordFrame.y + recordFrame.h - utPadRound(10.0f * s);
    const float w = utPadRound(wRec * s), h = utPadRound(ch * s);
    const UtRectF r = {utPadRound(cx - w * 0.5f), bottom - h, w, h};
    return r;
}

// ---- captions ------------------------------------------------------------------------------------
// Fixed short names (one per group): TQ has no text-width export the mod can call, so GD's
// ut_textfold / ut_fontmetrics fold is replaced by these and a conservative width estimate.
inline const char* utPadShortName(const char* label) {
    static const char* const kMap[][2] = {
        {"Helms", "Helm"},  {"Torso", "Torso"}, {"Arms", "Arms"},     {"Legs", "Legs"},
        {"Amulets", "Amul"}, {"Rings", "Ring"},  {"Shields", "Shld"},  {"Axes", "Axe"},
        {"Maces", "Mace"},  {"Staves", "Staff"}, {"Swords", "Sword"}, {"Throwing", "Throw"},
        {"Spears", "Spear"}, {"Bows", "Bow"},    {"Artifacts", "Artf"},
    };
    if (!label) return "?";
    for (int i = 0; i < (int)(sizeof(kMap) / sizeof(kMap[0])); ++i) {
        const char* a = kMap[i][0];
        const char* b = label;
        while (*a && *a == *b) ++a, ++b;
        if (!*a && !*b) return kMap[i][1];
    }
    return label;   // an unknown label is drawn as it is (the estimate below sizes it)
}

inline int utPadStrLen(const char* s) {
    int n = 0;
    while (s && s[n]) ++n;
    return n;
}

// A generous width for the caption font (Albertus MT Light, mixed case): 0.55 em per character.
const float kUtPadEmPerChar = 0.55f;
inline float utPadTextWidth(const char* s, int size) {
    return kUtPadEmPerChar * (float)size * (float)utPadStrLen(s);
}

// The Transfer toggle: a lamp (GD's LED, 0.4 of the row high) 4 record px in and its caption 4
// record px after it. The caption's offset from the button's left edge.
inline float utPadLampTextOffset(float hPx, float s) {
    return utPadRound(4.0f * s) + utPadRound(hPx * 0.4f) + utPadRound(4.0f * s);
}
// The toggle's caption at the pad's ONE caption size `fs` (the pad's size is never cut for it):
// "Transfer" when it fits right of the lamp (the caption rule's 2 px margin), else "Trans.", else
// "Tab" (drawn even when that does not fit either).
inline const char* utPadToggleCaption(float wPx, float hPx, float s, int fs) {
    const float room = wPx - utPadLampTextOffset(hPx, s) - 2.0f;
    if (!(utPadTextWidth("Transfer", fs) > room)) return "Transfer";
    if (!(utPadTextWidth("Trans.", fs) > room)) return "Trans.";
    return "Tab";
}

// ---- the plate label's line while the view is ON ------------------------------------------
// One line in the label's box, at the label's size `fs`: the first of these forms that fits -
//   full:    "Torso  rows 3-7 / 22  owned 12 / 241  all 312 / 1588  found 9"
//   compact: "Torso 3-7/22  12/241  all 312/1588  found 9"
//   (i)   the search word's short form: "Torso 3-7/22  12/241  all 312/1588  =9" (a query only)
//   (ii)  the rows dropped:             "Torso  12/241  all 312/1588  =9"
//   (iii) the whole collection dropped: "Torso  12/241  =9"
//   (iv)  the owned count dropped:      "Torso  =9" (a query only)
// With no query the line ends after the `all` count, and (iii) is the last form. The last form
// that is still too wide is drawn at the largest size down to 6 that fits, or at 6 when none does.
// Returns the font size; `line` gets the text.
struct UtPadLabelWords {
    const char* group = "";   // the group's label (the compact forms use its short name)
    const char* span = "0";   // the window's rows, "3-7" ("0" for an empty group)
    int rows = 0;
    bool known = false;       // the owned count is known
    int owned = 0;
    int total = 0;
    const char* found = "";   // "indexing k/n" or "found N"; empty while no query stands
    const char* brief = "";   // its short form, "k/n" or "=N"
    bool allKnown = false;    // the whole collection's owned count is known
    int allOwned = 0;
    int allTotal = 0;         // the catalogue's records; 0 = no `all` count at all
};
inline bool utPadLabelFits(const char* line, int size, float boxW) {
    return !(utPadTextWidth(line, size) > boxW);
}
inline int utPadLabelLine(const UtPadLabelWords& w, float boxW, int fs, char* line, size_t cap) {
    if (!line || cap == 0) return fs;
    const char* f = w.found ? w.found : "";
    const char* b = w.brief && w.brief[0] ? w.brief : f;
    const bool q = f[0] != 0;
    const char* sep = q ? "  " : "";
    const char* sn = utPadShortName(w.group);
    char own[32], cnt[32], all[40], allC[40];
    if (w.known) _snprintf_s(own, sizeof(own), _TRUNCATE, "owned %d / %d", w.owned, w.total);
    else _snprintf_s(own, sizeof(own), _TRUNCATE, "owned ? / %d", w.total);
    if (w.known) _snprintf_s(cnt, sizeof(cnt), _TRUNCATE, "%d/%d", w.owned, w.total);
    else _snprintf_s(cnt, sizeof(cnt), _TRUNCATE, "?/%d", w.total);
    all[0] = allC[0] = 0;
    if (w.allTotal > 0 && w.allKnown) {
        _snprintf_s(all, sizeof(all), _TRUNCATE, "  all %d / %d", w.allOwned, w.allTotal);
        _snprintf_s(allC, sizeof(allC), _TRUNCATE, "  all %d/%d", w.allOwned, w.allTotal);
    } else if (w.allTotal > 0) {
        _snprintf_s(all, sizeof(all), _TRUNCATE, "  all ? / %d", w.allTotal);
        _snprintf_s(allC, sizeof(allC), _TRUNCATE, "  all ?/%d", w.allTotal);
    }
    _snprintf_s(line, cap, _TRUNCATE, "%s  rows %s / %d  %s%s%s%s", w.group, w.span, w.rows, own,
                all, sep, f);
    if (utPadLabelFits(line, fs, boxW)) return fs;
    _snprintf_s(line, cap, _TRUNCATE, "%s %s/%d  %s%s%s%s", sn, w.span, w.rows, cnt, allC, sep, f);
    if (utPadLabelFits(line, fs, boxW)) return fs;
    if (q) {
        _snprintf_s(line, cap, _TRUNCATE, "%s %s/%d  %s%s%s%s", sn, w.span, w.rows, cnt, allC,
                    sep, b);
        if (utPadLabelFits(line, fs, boxW)) return fs;
    }
    _snprintf_s(line, cap, _TRUNCATE, "%s  %s%s%s%s", sn, cnt, allC, sep, b);
    if (utPadLabelFits(line, fs, boxW)) return fs;
    _snprintf_s(line, cap, _TRUNCATE, "%s  %s%s%s", sn, cnt, sep, b);
    if (utPadLabelFits(line, fs, boxW) || !q) {
        int lfs = fs;
        while (lfs > 6 && !utPadLabelFits(line, lfs, boxW)) --lfs;
        return lfs;
    }
    _snprintf_s(line, cap, _TRUNCATE, "%s  %s", sn, b);
    int lfs = fs;
    while (lfs > 6 && !utPadLabelFits(line, lfs, boxW)) --lfs;
    return lfs;
}

// The label while the real Transfer page is shown (the view OFF). No query: "the real Transfer
// page" (or "Transfer" when that does not fit). With one, the first form that fits at the label's
// size: `the real Transfer page  found 9`, `Transfer found 9`, `Transfer  =9` (while the items are
// read: `reading 3/40`, `3/40`); the last form shrinks to the largest size down to 6 that fits.
inline int utPadLabelRealLine(const char* found, const char* brief, float boxW, int fs, char* line,
                              size_t cap) {
    if (!line || cap == 0) return fs;
    const char* f = found ? found : "";
    const char* b = brief && brief[0] ? brief : f;
    if (!f[0]) {
        _snprintf_s(line, cap, _TRUNCATE, "the real Transfer page");
        if (utPadLabelFits(line, fs, boxW)) return fs;
        _snprintf_s(line, cap, _TRUNCATE, "Transfer");
    } else {
        _snprintf_s(line, cap, _TRUNCATE, "the real Transfer page  %s", f);
        if (utPadLabelFits(line, fs, boxW)) return fs;
        _snprintf_s(line, cap, _TRUNCATE, "Transfer %s", f);
        if (utPadLabelFits(line, fs, boxW)) return fs;
        _snprintf_s(line, cap, _TRUNCATE, "Transfer  %s", b);
    }
    int lfs = fs;
    while (lfs > 6 && !utPadLabelFits(line, lfs, boxW)) --lfs;
    return lfs;
}

// Whether the search field (and its clear square) is laid out: search=1, and the view ON or the
// real Transfer page with search_transfer=1.
inline bool utPadFieldLaidOut(bool viewOn, int search, int searchTransfer) {
    return search != 0 && (viewOn || searchTransfer != 0);
}

// the plate label's font (labelPx = utPlateLabelPx: plate_label_size x the UI
// scale) cut by the LABEL ROW's own height: the row's drawn height minus 1 px (the label is text
// with no frame to clear), never by the group buttons' caption (which shrinks with their width
// and short names). Never under 6 (TQ's floor; GD's is 9 - deviations): 0 = the row cannot hold
// size 6 (or is not a number) - the caller draws no label, as the caption rule's 0.
inline int utPadLabelFont(int labelPx, float rowHPx) {
    if (!utPadFinite(rowHPx) || !(rowHPx > 0.0f) || rowHPx > 1.0e5f) return 0;
    const int top = (int)rowHPx - 1;
    int v = labelPx < 6 ? 6 : labelPx;
    if (v > top) v = top;
    return v < 6 ? 0 : v;
}

// GD's caption rule: three quarters of the cell height, capped at h - 2 and at 14, and the widest
// caption must fit w - 2. 0 = not even size 6 fits (the caller draws the button unlabelled).
// the cap of 14 is record px, scaled like everything else (s = the UI scale).
inline int utPadCaptionSize(float wPx, float hPx, int widestChars, float s = 1.0f) {
    int top = (int)(hPx * 3.0f / 4.0f);
    if (top > (int)hPx - 2) top = (int)hPx - 2;
    const int cap = (utPadFinite(s) && s > 0.3f && s < 4.0f) ? (int)utPadRound(14.0f * s) : 14;
    if (top > cap) top = cap;
    for (int size = top; size >= 6; --size) {
        if (kUtPadEmPerChar * (float)size * (float)widestChars <= wPx - 2.0f) return size;
    }
    return 0;
}

// the caravan frame from the Transfer page DRAW (TQ.exe 0xC31A0), so the
// pad is there from the first frame the tab is shown. The draw's own arithmetic:
//   pos = the page's own place, floats [page+0x1C] / [page+0x20]
//   out = 0x176750(pos): pos x Engine::GetUIScale() when GraphicsEngine::IsDownsizing() is false
//         (0x1767E3), a width/height-based scale when it is true
//   pass != 1: the inventory is drawn at (origin.x + pos.x, origin.y + out.y)   (0xC3229..0xC3241)
//   pass == 1: its y is out.y x 0x176840 (a second, height-based factor)
// That point is the page's parent origin as the page MOUSE HANDLER receives it (the earlier reading,
// the same number utFrameFromHover subtracts the page's record place from), and the grid is it plus
// the inventory's own place [UIStashInventory+0x10/+0x14] (the handler's `F3 0F 10 5B 10`). Refused
// (the hover route stays the fallback) when downsizing is true or unknown, in pass 1, or when a
// number is not finite or not a plausible canvas coordinate.
enum { kUtDrawOk = 0, kUtDrawDownsizing, kUtDrawPass1, kUtDrawBadInput };
struct UtDrawOriginIn {
    float originX, originY;   // the draw's origin argument ([ebp+0xC] -> 2 floats)
    float posX, posY;         // [page+0x1C] / [page+0x20]
    float invX, invY;         // [UIStashInventory+0x10] / [+0x14]
    float uiScale;            // Engine::GetUIScale
    int pass;                 // the draw's pass argument
    bool downsizingKnown, downsizing;
};
inline int utDrawOrigin(const UtDrawOriginIn& in, float* pageX, float* pageY, float* gridX,
                        float* gridY) {
    if (!in.downsizingKnown || in.downsizing) return kUtDrawDownsizing;
    if (in.pass == 1) return kUtDrawPass1;
    const float lim = 20000.0f;
    const float v[7] = {in.originX, in.originY, in.posX, in.posY, in.invX, in.invY, in.uiScale};
    for (int i = 0; i < 7; ++i)
        if (!utPadFinite(v[i]) || v[i] < -lim || v[i] > lim) return kUtDrawBadInput;
    if (!(in.uiScale > 0.3f && in.uiScale < 4.0f)) return kUtDrawBadInput;
    const float px = in.originX + in.posX;             // x: the raw place (0xC322D)
    const float py = in.originY + in.posY * in.uiScale;   // y: 0x176750's out.y (0xC323C)
    if (pageX) *pageX = px;
    if (pageY) *pageY = py;
    if (gridX) *gridX = px + in.invX;
    if (gridY) *gridY = py + in.invY;
    return kUtDrawOk;
}
inline const char* utDrawWhyText(int r) {
    return r == kUtDrawOk           ? "ok"
           : r == kUtDrawDownsizing ? "GraphicsEngine::IsDownsizing is set or unknown"
           : r == kUtDrawPass1      ? "the pass-1 draw (a second, height-based factor)"
                                    : "a number is not usable";
}
// A draw-derived frame is taken only after the same page point held for this many frames in a row
// (a window that slides in never feeds a moving point to the frame tracker).
const int kUtDrawStableFrames = 3;

// the Ctrl+wheel / Ctrl+PgUp / Ctrl+PgDn cycle in the pad's own order:
// Transfer, G1 ... Gn, Transfer ... `from` = the group shown (0-based), or kUtCycleTransfer for the
// real Transfer page (the view OFF); dir -1 = back (wheel up, PgUp), +1 = forward (wheel down,
// PgDn). One step per event. Returns the target group, kUtCycleTransfer, or kUtCycleNone (no groups).
enum { kUtCycleTransfer = -1, kUtCycleNone = -2 };
inline int utGroupCycleStep(int from, int n, int dir) {
    if (n < 1 || dir == 0) return kUtCycleNone;
    if (from < 0 || from >= n) return dir > 0 ? 0 : n - 1;   // from Transfer: G1 / Gn
    const int next = from + (dir > 0 ? 1 : -1);
    return (next < 0 || next >= n) ? kUtCycleTransfer : next;
}

// the wheel's notch accumulator - the usual remainder rule. A wheel, a
// touchpad or a keyboard that reports deltas smaller than one notch (WHEEL_DELTA = 120) adds up
// until |sum| reaches a notch: the whole notches are answered (signed, + = wheel up) and the
// remainder is kept; a delta in the other direction drops the remainder first. ut_panel keeps one
// for the Ctrl cycle and one for the plain wheel (a switch between the two resets the other).
struct UtWheelAcc {
    int sum = 0;
};
inline int utWheelAccumulate(UtWheelAcc& a, int delta, int notch = 120) {
    if (delta == 0 || notch <= 0) return 0;
    if (a.sum != 0 && ((a.sum > 0) != (delta > 0))) a.sum = 0;   // a direction change
    long long s = (long long)a.sum + (long long)delta;
    const long long cap = 1000LL * notch;   // no overflow from a flood of deltas
    if (s > cap) s = cap;
    if (s < -cap) s = -cap;
    const long long n = s / notch;   // toward zero: the remainder keeps the sum's sign
    a.sum = (int)(s - n * notch);
    return (int)n;
}

// ---- the slot-wide item tint - pure -----------------------------------
// The inventory draw (TQ.exe 0x16E300, the Transfer page draw's `FF 50 0C`) gives every item widget
// a background through 0x10A9B0 -> 0x10A850, at the item's FOOTPRINT inset by the inventory's
// [+0x134] px. What 0x10A850 draws (disassembled, section 2):
//   requirements NOT met (EquipmentCtrl::AreRequirementsMet false)  -> the inventory's
//       failsRequirementsColor [+0x124] (stashinventory.dbr 0.5, 0, 0, 0.5)             = Fails
//   met, the item-background option on (Options::GetBool(0x18)), Item::GetActualItemClassification
//       != 0 and GameEngine::GetItemColor(cls) answers -> that colour with
//       GameEngine::GetItemBackgroundOpacity(hover) as alpha (then its border texture)  = Class
//   met otherwise -> the inventory's backgroundShadeColor [+0x114] (0.5, 0.5, 0.5, 0.3)  = Shade
//       (the engine's hovered widget draws 0.4, 0.6, 0.8, 0.3 instead - not reproduced)
enum { kUtTintNone = 0, kUtTintFails = 1, kUtTintClass = 2, kUtTintShade = 3 };
inline int utTintKind(bool met, bool optionOn, int cls, bool colourOk) {
    if (!met) return kUtTintFails;
    if (optionOn && cls != 0 && colourOk) return kUtTintClass;
    return kUtTintShade;
}

// The RING between the item's tinted rect and its slot's (both already inset by the engine's
// inset): up to four rectangles - the band above the item, the band below it (both the slot's
// whole width), and the parts left and right of it (the item's height). The item is clipped to the
// slot first; an item that fills the slot gives none, an item outside the slot the whole slot. The
// rectangles never overlap the item's rect (the icon is inside it), nor each other. Slivers under
// 0.01 px are dropped. Returns the count.
inline int utSlotRing(const UtRectF& slot, const UtRectF& item, UtRectF out[4]) {
    const float eps = 0.01f;
    if (!(slot.w > eps && slot.h > eps)) return 0;
    const float sx1 = slot.x + slot.w, sy1 = slot.y + slot.h;
    float ix0 = item.x > slot.x ? item.x : slot.x, iy0 = item.y > slot.y ? item.y : slot.y;
    float ix1 = item.x + item.w < sx1 ? item.x + item.w : sx1;
    float iy1 = item.y + item.h < sy1 ? item.y + item.h : sy1;
    int n = 0;
    if (!(ix1 - ix0 > eps && iy1 - iy0 > eps)) {   // no overlap: the whole slot
        out[n++] = slot;
        return n;
    }
    if (iy0 - slot.y > eps) out[n++] = UtRectF{slot.x, slot.y, slot.w, iy0 - slot.y};
    if (sy1 - iy1 > eps) out[n++] = UtRectF{slot.x, iy1, slot.w, sy1 - iy1};
    if (ix0 - slot.x > eps) out[n++] = UtRectF{slot.x, iy0, ix0 - slot.x, iy1 - iy0};
    if (sx1 - ix1 > eps) out[n++] = UtRectF{ix1, iy0, sx1 - ix1, iy1 - iy0};
    return n;
}

}  // namespace ut
