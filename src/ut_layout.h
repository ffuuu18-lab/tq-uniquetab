// ut_layout.h - where the collection page's three parts go: the item GRID, the button PAD and the
// LABEL, each a rectangle in RECORD px, frame-local (the caravan window's own coordinates: 0,0 is
// its top-left, 565 x 637 in vanilla; the live side multiplies by the UI scale with
// floorf(v*s+0.5f) per edge, as it always has).
//
// Pure arithmetic: no Windows, no engine, no allocation, no globals. ut_panel.cpp and
// tools\test_layout.cpp run exactly these functions.
//
//   utRectParse / utRectFormat   "x,y,w,h" <-> UtRect (the value of a [layout] key)
//   utCoverKeyParse / Format     "<W>x<H>", the suffix of a [layout] key: the caravan window size
//                                it is for (the caravan record's size, 565x637 in vanilla)
//   utPadFitRect                 rectangle + N group buttons -> the two-row pad's cells, its
//                                caption size, and the pages (ut_padpage.h) when the groups do not
//                                all fit with a readable caption
//   utPadCellRect                one row-1 cell of a fitted pad, record px
//   utPadOutFromFit              a fitted pad on one page -> the drawn rects the draw and the hit
//                                test read (ut_padlayout.h UtPadOut)
//   utLabelFit                   rectangle + the label's size -> the size drawn, or refused
//   utLayoutResolve              derived + the player's rectangles -> the three in force, and for
//                                each rectangle not in force the reason it was refused
//   utLayoutFromSource           the measured frame and grid + the [layout] values -> the layout
//   utGridFit                    rectangle + cell size -> cols x rows under a box cap (GD's rule)
//   utLayoutGridCells            the collection sack's cols x rows in a grid rectangle (32 px cells)
//   utLayoutBoxCap               the most cells a grid on the caravan window may hold
//   utLayoutCanonical            a grid override as its canonical "x,y,w,h"
//   utScreenRectParse / Format   "<corner>,x,y,w,h" <-> UtScreenRect (pad_screen, label_screen)
//   utScreenPlace                a screen rectangle -> client px, shifted into the viewport
//   utScreenToLocal / utLocalToClient   client px <-> frame-local record px
//   utScreenLocate               the two above in one: where a screen part lands and is drawn
//   utScreenFromClient           a client rectangle -> the key of its nearest corner (the editor)
//   utLayoutRealPage             the game's own Transfer page: the collection page's pad and label
//   utRealCellsCovered           the real sack's cells a part drawn over them covers
//   utRealPadText                the real page's pad and label as the log says them
//   utRelicGroupIs               the relic and charm pages' group (its G line id)
//   utPageGridKeys               the grid line each resolution takes, per page kind
//   utRelicAreaOf / FromLine     the relic pages' area: relics_<W>x<H>, else the collection's grid
//   utRelicGridInForce           the grid the page on screen and the sack take on a relic page
//
// THE GRID. The derived grid is the engine's own Transfer sack, 16 x 15 cells, measured where the
// engine draws it (ut_padlayout.h). A grid_<W>x<H> rectangle of the player's moves and sizes the
// collection's own sack while the view is ON: whole cells from the top-left, the rest stays empty.
// The grid stays the first part of the layout: a pad or a label of the player's that meets it
// falls back; a grid of the player's that meets a derived pad or label falls back itself.
//
// THE REAL TRANSFER PAGE (the view OFF) keeps the game's own sack: its grid in force is always the
// derived grid, never moved or re-dimmed. The pad and the label stay where the collection page has
// them, WHOLE - the player's rectangle, the derived one or the screen part (utLayoutRealPage) -
// drawn over the real cells they cover (utRealCellsCovered counts them for the log). A click or a
// hover inside them is theirs, never the cell's (ut_padinput.h utPadMsgDecide, the panel's pick-up
// and rollover refusal).
//
// THE DERIVED PAD AND LABEL are what the mod has always drawn: utPadLayout (ut_padlayout.h) fits
// the pad to the band below the grid, its label in the pad's second row. A pad_<W>x<H> rectangle
// replaces the pad (its label box comes with it); a label_<W>x<H> rectangle takes the label out of
// the pad (the pad's label box is then plain ground).
//
// THE RELIC PAGES (the group whose G line id is "relics") have an area of their own: a
// relics_<W>x<H> rectangle of the player's, resolved exactly as a grid_ line is (inside the window,
// clear of the pad and the label), else the collection's grid in force (the player's grid_, or the
// derived one). While a relic page is shown the page on screen, the collection sack and the
// lattice take that area, in whole cells; the groups of the uniques stay packed on the collection's
// grid, so a page of theirs never changes when a relic page is visited.
//
// THE SCREEN PARTS (pad_screen / label_screen, "the screen-anchored pad and label" below): the pad
// and the label may instead be pinned to a corner of the screen, outside the caravan window. The
// pad pinned there carries its label box with it while the label is not the player's.
#pragma once

#include <math.h>

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "ut_padlayout.h"
#include "ut_padpage.h"

static_assert(ut::kUtPadButtonsMax == ut::kUtPadCells, "the pad holds every button utPadPaginate draws");

namespace ut {

struct UtRect {
    int x, y, w, h;   // top-left and size; the right edge is x + w (exclusive)
};

inline bool utRectEq(const UtRect& a, const UtRect& b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

// True when `in` lies wholly inside `out`.
inline bool utRectInside(const UtRect& in, const UtRect& out) {
    return in.x >= out.x && in.y >= out.y && in.x + in.w <= out.x + out.w &&
           in.y + in.h <= out.y + out.h;
}

// True when the two share at least one pixel (touching edges do not meet).
inline bool utRectsMeet(const UtRect& a, const UtRect& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

// True when each edge of `a` is at most `tol` px from the same edge of `b`.
inline bool utRectNear(const UtRect& a, const UtRect& b, int tol) {
    const int d[4] = {a.x - b.x, a.y - b.y, a.x + a.w - (b.x + b.w), a.y + a.h - (b.y + b.h)};
    for (int i = 0; i < 4; ++i)
        if (d[i] > tol || d[i] < -tol) return false;
    return true;
}

// ---- the grid ----------------------------------------------------------------------------------

const int kUtGridCell = 32;          // record px: one sack cell (the engine's is 32 x the UI scale)
const int kUtGridRowGap = 0;         // the sack's rows touch
const int kUtGridBoxFloor = 16 * 15; // the vanilla Transfer sack's cells
const int kUtGridBoxMax = 512;       // GD's frame box ceiling (the prototype table's size)

struct UtGrid {
    int cols, rows;
    int capped;   // 1 = the cap took rows away (the group paginates further)
};

// cols = w / cellW, rows = h / (cellH + rowGap), each at least 1; rows then walk back while
// cols * rows exceeds `cap`. The slack stays empty at the right and the bottom (top-left anchor).
inline UtGrid utGridFit(const UtRect& r, int cellW, int cellH, int rowGap, int cap) {
    if (cellW < 1) cellW = 1;
    if (cellH < 1) cellH = 1;
    if (rowGap < 0) rowGap = 0;
    UtGrid g;
    g.cols = r.w / cellW;
    if (g.cols < 1) g.cols = 1;
    g.rows = r.h / (cellH + rowGap);
    if (g.rows < 1) g.rows = 1;
    const int full = g.rows;
    while (g.cols * g.rows > cap && g.rows > 1) --g.rows;
    g.capped = g.rows < full ? 1 : 0;
    return g;
}

// The box count for a caravan window (GD's utFrameBoxes at the sack's one cell size): the most
// cells ANY grid on the window can show - utGridFit over the whole window, uncapped - clamped to
// kUtGridBoxFloor..kUtGridBoxMax. So no grid inside the window is capped below the ceiling.
// Vanilla 565x637: 17 x 19 = 323.
inline int utLayoutBoxCap(int coverW, int coverH) {
    const UtRect cover = {0, 0, coverW < 1 ? 1 : coverW, coverH < 1 ? 1 : coverH};
    const UtGrid g = utGridFit(cover, kUtGridCell, kUtGridCell, kUtGridRowGap, 1 << 30);
    int best = g.cols * g.rows;
    if (best < kUtGridBoxFloor) best = kUtGridBoxFloor;
    if (best > kUtGridBoxMax) best = kUtGridBoxMax;
    return best;
}

// The collection sack's cells in a grid rectangle: utGridFit at the sack's 32 record px cell,
// at most `cap` cells.
inline UtGrid utLayoutGridCells(const UtRect& r, int cap) {
    return utGridFit(r, kUtGridCell, kUtGridCell, kUtGridRowGap, cap < 1 ? 1 : cap);
}

// The first visible ITEM stays the first visible item across a cols change: row r of `oldCols`
// starts at item r * oldCols, which is row (r * oldCols) / newCols of `newCols`.
inline int utRowKeep(int row, int oldCols, int newCols) {
    if (row <= 0 || oldCols < 1 || newCols < 1) return 0;
    return (row * oldCols) / newCols;
}

// ---- text: "x,y,w,h" and "<W>x<H>" ------------------------------------------------------------

// One signed whole number of at most 5 digits, blanks around it skipped. Advances `p`.
inline bool utParseNum(const char*& p, int* v) {
    while (*p == ' ' || *p == '\t') ++p;
    bool neg = false;
    if (*p == '-' || *p == '+') neg = (*p++ == '-');
    if (*p < '0' || *p > '9') return false;
    int acc = 0, digits = 0;
    while (*p >= '0' && *p <= '9') {
        if (++digits > 5) return false;
        acc = acc * 10 + (*p++ - '0');
    }
    while (*p == ' ' || *p == '\t') ++p;
    *v = neg ? -acc : acc;
    return true;
}

// "x,y,w,h": four whole numbers, w and h at least 1, nothing after the fourth. A position may be
// negative (the parse is format only; utLayoutResolve decides whether the rectangle is usable).
inline bool utRectParse(const char* s, UtRect* out) {
    if (!s || !out) return false;
    const char* p = s;
    int v[4];
    for (int i = 0; i < 4; ++i) {
        if (!utParseNum(p, &v[i])) return false;
        if (i < 3) {
            if (*p != ',') return false;
            ++p;
        }
    }
    while (*p == '\r' || *p == '\n') ++p;
    if (*p) return false;
    if (v[2] < 1 || v[3] < 1) return false;
    out->x = v[0];
    out->y = v[1];
    out->w = v[2];
    out->h = v[3];
    return true;
}

// Writes "x,y,w,h". Returns the length, or 0 when it did not fit.
inline int utRectFormat(const UtRect& r, char* out, size_t cap) {
    if (!out || cap == 0) return 0;
    const int n = _snprintf_s(out, cap, _TRUNCATE, "%d,%d,%d,%d", r.x, r.y, r.w, r.h);
    return (n > 0 && (size_t)n < cap) ? n : 0;
}

// "<W>x<H>": 1..4 digits, a lower-case x, 1..4 digits, both at least 1. Nothing else.
inline bool utCoverKeyParse(const char* s, int* w, int* h) {
    if (!s) return false;
    int v[2] = {0, 0};
    const char* p = s;
    for (int i = 0; i < 2; ++i) {
        int digits = 0;
        while (*p >= '0' && *p <= '9') {
            if (++digits > 4) return false;
            v[i] = v[i] * 10 + (*p++ - '0');
        }
        if (digits == 0 || v[i] < 1) return false;
        if (i == 0) {
            if (*p != 'x') return false;
            ++p;
        }
    }
    if (*p) return false;
    if (w) *w = v[0];
    if (h) *h = v[1];
    return true;
}

inline int utCoverKeyFormat(int w, int h, char* out, size_t cap) {
    if (!out || cap == 0) return 0;
    const int n = _snprintf_s(out, cap, _TRUNCATE, "%dx%d", w, h);
    return (n > 0 && (size_t)n < cap) ? n : 0;
}

// A record-px rectangle at UI scale `s`, each EDGE rounded with floorf(v*s+0.5f) - the rounding
// the pad draw, the hit test and the outline share. Relative to the frame's (rounded) top-left.
inline UtRectF utScaledRectF(const UtRect& r, float s) {
    const float x = utPadRound((float)r.x * s), y = utPadRound((float)r.y * s);
    const UtRectF f = {x, y, utPadRound((float)(r.x + r.w) * s) - x,
                       utPadRound((float)(r.y + r.h) * s) - y};
    return f;
}

// ---- the grid in force (ut_panel.cpp gridApply, every frame of the caravan window) -------------
// The collection is packed on the player's grid_ rectangle whenever its line resolves (the same
// resolution with the view ON or OFF: the real Transfer page never reads that packing, so a view
// toggle re-packs nothing), else on the derived grid. The page on screen shows the player's grid
// only while the view is ON; OFF it is the real Transfer page's own (derived) grid. The drawn
// grid is the rectangle's top-left (frame-local, at the UI scale, from the frame's rounded corner)
// and its whole cells at the drawn cell.
struct UtGridForceIn {
    int on;                     // 1 = the view is ON (the collection page is shown)
    int userSet;                // 1 = a grid_<W>x<H> line resolved (L.user[kUtLayGrid])
    UtRect userRect;            // its rectangle, record px, frame-local
    int coverW, coverH;         // the caravan window (the box cap)
    UtRectF frame, frameGrid;   // the measured caravan frame and the derived grid, drawn px
    float scale;                // the UI scale
    float cell;                 // the drawn cell, px (the mod sack's live cell)
    int derivedCols, derivedRows;   // the derived grid's cells
    int failCols, failRows;     // cells the groups could not be packed on (0 x 0 = none)
};
struct UtGridForce {
    int packUser;               // 1 = the collection is packed on the player's grid
    int packCols, packRows;     // the cells every group is packed on (ON and OFF)
    float dx, dy;               // the player's grid's top-left against the derived grid's, drawn px
    UtRectF drawn;              // the grid the page on screen shows, drawn px
    int drawnCols, drawnRows;   // its cells
    int drawnUser;              // 1 = the drawn grid is the player's (the view ON)
};
inline UtGridForce utGridInForce(const UtGridForceIn& in) {
    UtGridForce f = {};
    f.packCols = in.derivedCols;
    f.packRows = in.derivedRows;
    f.drawn = in.frameGrid;
    f.drawnCols = in.derivedCols;
    f.drawnRows = in.derivedRows;
    if (!in.userSet || !(in.cell > 0.0f) || !(in.scale > 0.0f)) return f;
    const UtGrid c = utLayoutGridCells(in.userRect, utLayoutBoxCap(in.coverW, in.coverH));
    if (c.cols == in.failCols && c.rows == in.failRows) return f;   // could not be packed: derived
    const UtRectF r = utScaledRectF(in.userRect, in.scale);
    const UtRectF d = {utPadRound(in.frame.x) + r.x, utPadRound(in.frame.y) + r.y,
                       in.cell * (float)c.cols, in.cell * (float)c.rows};
    f.packUser = 1;
    f.packCols = c.cols;
    f.packRows = c.rows;
    f.dx = d.x - in.frameGrid.x;
    f.dy = d.y - in.frameGrid.y;
    if (in.on) {
        f.drawn = d;
        f.drawnCols = c.cols;
        f.drawnRows = c.rows;
        f.drawnUser = 1;
    }
    return f;
}

// The grid the caravan window's lattice is painted for (ut_cover coverSetGrid): the collection's
// grid in force in caravan-window units - the player's rectangle when the collection is packed on
// it, else the game's derived area when it is larger than the real sack (`area`: its origin and
// whole cells, record px), else none (the background's own lattice is the derived grid). The
// cover compares it with the derived grid and repaints only when they differ.
struct UtLatticeGrid {
    int user;                   // 1 = a grid other than the derived one is in force
    int x, y, cols, rows;       // its top-left (caravan-window units) and its cells
};
inline UtLatticeGrid utLatticeGridOf(const UtGridForce& f, const UtRect& userRect, int areaInForce,
                                     const UtRect& area) {
    UtLatticeGrid l = {0, 0, 0, f.packCols, f.packRows};
    if (f.packUser) {
        l.user = 1;
        l.x = userRect.x;
        l.y = userRect.y;
    } else if (areaInForce) {
        l.user = 1;
        l.x = area.x;
        l.y = area.y;
    }
    return l;
}

// The grid the lattice is painted for while a relic page is shown (ut_panel gridApply): with the
// grid cover on, none - 0 x 0 cells, the caravan background with its lattice erased (each slot
// stands on its own plate, the window's own texture between them; one painted copy a background);
// else `lat`, the grid in force as on an item page.
inline UtLatticeGrid utRelicLatticeOf(bool relicPage, bool gridCover, const UtLatticeGrid& lat) {
    if (!relicPage || !gridCover) return lat;
    return UtLatticeGrid{1, 0, 0, 0, 0};
}

// The grid line each resolution of the layout is made with (ut_panel layout): the page on screen
// takes it only while the view is ON (the real Transfer page keeps its own grid); the collection
// takes it ON and OFF alike, so its packing never changes with the view - ON through the layout
// in force, OFF through a resolution of its own (`own`), whose pad and label are also the real
// page's (utLayoutRealPage). Null = no grid line for that resolution.
struct UtCollGridKeys {
    const char* page;   // the layout in force (the page on screen)
    const char* coll;   // the collection's grid
    int own;            // 1 = the collection needs its own resolution (the view OFF, a line set)
    int relic;          // 1 = the page on screen resolves with the relic pages' relics_ line
};
inline UtCollGridKeys utCollGridKeys(int on, const char* gridLine) {
    const char* g = gridLine && gridLine[0] ? gridLine : nullptr;
    UtCollGridKeys k;
    k.page = on ? g : nullptr;
    k.coll = g;
    k.own = !on && g ? 1 : 0;
    k.relic = 0;
    return k;
}

// ---- the relic pages' area ---------------------------------------------------------------------

// The relic and charm pages' group: its G line id.
const char* const kUtRelicGroupId = "relics";
inline bool utRelicGroupIs(const char* groupId) {
    if (!groupId) return false;
    const char* a = groupId;
    const char* b = kUtRelicGroupId;
    for (; *a && *b; ++a, ++b) {
        const char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a - 'A' + 'a') : *a;
        if (ca != *b) return false;
    }
    return *a == 0 && *b == 0;
}

// The grid lines per page kind (ut_panel layout): a unique page as utCollGridKeys; a RELIC page
// shown with the view ON resolves the page on screen with its relics_<W>x<H> line when one is set
// (`relic` = 1) and the collection keeps its own resolution with the grid line (`own` = 1), so the
// uniques stay packed where they were; with no relics line the relic page shows the collection's
// grid in force (the grid line, or none = the derived grid) and the two resolutions are one.
inline UtCollGridKeys utPageGridKeys(int on, int relicPage, const char* gridLine, const char* relicsLine) {
    UtCollGridKeys k = utCollGridKeys(on, gridLine);
    const char* r = relicsLine && relicsLine[0] ? relicsLine : nullptr;
    if (!on || !relicPage || !r) return k;
    k.page = r;
    k.own = 1;
    k.relic = 1;
    return k;
}

// The relic pages' area for a caravan window: the player's relics_<W>x<H> rectangle while it is in
// force (`relicRect`, null = no line or a refused one), else the collection's grid in force
// (`collRect` with its `collCols` x `collRows`). Its whole cells: utLayoutGridCells under the
// window's box cap, exactly as the collection's own grid (utGridInForce).
struct UtRelicArea {
    int user;           // 1 = the player's relics_ rectangle, 0 = the collection's grid in force
    UtRect rect;        // record px, frame-local
    int cols, rows;     // its whole cells
};
inline UtRelicArea utRelicAreaOf(const UtRect* relicRect, const UtRect& collRect, int collCols,
                                 int collRows, int coverW, int coverH) {
    UtRelicArea a = {0, collRect, collCols < 1 ? 1 : collCols, collRows < 1 ? 1 : collRows};
    if (!relicRect || relicRect->w < 1 || relicRect->h < 1) return a;
    const UtGrid c = utLayoutGridCells(*relicRect, utLayoutBoxCap(coverW, coverH));
    a.user = 1;
    a.rect = *relicRect;
    a.cols = c.cols;
    a.rows = c.rows;
    return a;
}
// The same from the line's text (the [layout] value of relics_<W>x<H>, null = no line); a value
// that does not parse counts as no line.
inline UtRelicArea utRelicAreaFromLine(const char* relicsLine, const UtRect& collRect, int collCols,
                                       int collRows, int coverW, int coverH) {
    UtRect r;
    const bool have = relicsLine && utRectParse(relicsLine, &r);
    return utRelicAreaOf(have ? &r : nullptr, collRect, collCols, collRows, coverW, coverH);
}

// The grid a relic page on screen and the collection sack take (ut_panel gridApply): the relic
// area's whole cells from its top-left when its line is in force (`relicSet`, `relicRect`), else
// the collection's grid in force `coll` unchanged. `in` is the collection's own input (the frame,
// the drawn cell, the box cap).
inline UtGridForce utRelicGridInForce(const UtGridForceIn& in, const UtGridForce& coll, int relicSet,
                                      const UtRect& relicRect) {
    if (!relicSet) return coll;
    UtGridForceIn r = in;
    r.on = 1;
    r.userSet = 1;
    r.userRect = relicRect;
    r.failCols = 0;
    r.failRows = 0;
    return utGridInForce(r);
}

// The collection's area on screen (ut_panel layout): its own grid (cols x rows) and the game's
// whole derived area (fullCols x fullRows), both from the measured grid's top-left at the drawn
// cell; the real sack's 16 x 15 keeps the measured rectangle itself. Ctrl+wheel answers over the
// grid on screen and the whole area.
inline void utCaravanAreaRects(const UtRectF& measured, float cell, int cols, int rows, int fullCols,
                               int fullRows, UtRectF* grid, UtRectF* full) {
    *grid = measured;
    *full = measured;
    if (!(cell > 0.0f)) return;
    if (cols != 16 || rows != 15) {
        grid->w = cell * (float)cols;
        grid->h = cell * (float)rows;
    }
    if (fullCols != 16 || fullRows != 15) {
        full->w = cell * (float)fullCols;
        full->h = cell * (float)fullRows;
    }
}

// A drawn rectangle back to record px, frame-local: each edge divided by the scale. `outward` =
// the left / top edge floored and the right / bottom one ceiled, so the record rectangle covers
// every drawn pixel (the grid's rule); else INWARD (the derived pad and label: the record
// rectangle lies inside what is drawn, so a derived part never meets the grid's record rectangle
// it is flush with on screen).
inline UtRect utRecordRect(const UtRectF& d, const UtRectF& frame, float s, bool outward) {
    UtRect r = {0, 0, 0, 0};
    if (!(s > 0.3f && s < 4.0f)) return r;
    const float x0 = (d.x - frame.x) / s, y0 = (d.y - frame.y) / s;
    const float x1 = (d.x + d.w - frame.x) / s, y1 = (d.y + d.h - frame.y) / s;
    const float e = 0.001f;
    const float ax = outward ? utPadFloor(x0 + e) : utPadCeil(x0 - e);
    const float ay = outward ? utPadFloor(y0 + e) : utPadCeil(y0 - e);
    const float bx = outward ? utPadCeil(x1 - e) : utPadFloor(x1 + e);
    const float by = outward ? utPadCeil(y1 - e) : utPadFloor(y1 + e);
    r.x = (int)ax;
    r.y = (int)ay;
    r.w = (int)(bx - ax);
    r.h = (int)(by - ay);
    return r;
}

// ---- the pad -----------------------------------------------------------------------------------
// The pad has two rows inside a 3 px margin (ut_padlayout.h): row 1 the group buttons, row 2
// Transfer (2 cells), OWN (1), < and > (half a cell each), the search field (3 cells) and the label
// (the rest), on a grid of 15 cells across the pad. In a rectangle, row 1 takes the groups of the
// page in equal cells (plus one NEXT cell while the groups are paged); row 2 keeps its 15-cell grid
// whatever row 1 holds, so the search field and the label keep their cells.

const int kUtPadMinCellW = kUtPadCellWMin;   // record px (20): narrower cells are refused
const int kUtPadMinCellH = kUtPadRowFloor;   // record px (12): a row's floor
const int kUtPadRowCells = 15;               // row 2's grid
const int kUtPadRow2CellMin = 16;            // record px: row 2's cell floor (< and > are half one)
const int kUtPadDefGap = 1;                  // the gap between two cells, record px
const int kUtPadDefY = 4;                    // the derived pad: its gap below the grid's last row
const int kUtPadDefH = 14;                   // the derived pad: one row's height
const int kUtPadNextChars = 2;               // NEXT's caption (">>", next.caption's default)
// NEXT's caption width in characters for the fit: next.caption's, counted as a group caption is
// (utPadFitChars); 0 or less = the default's 2.
inline int utPadNextMin(int nextChars) { return nextChars > 0 ? nextChars : kUtPadNextChars; }

struct UtPadFit {
    int ok;              // 1 = a layout was found
    int cols, rows;      // row 1's cells (the page's groups, + NEXT while paged) and the rows (2)
    int cellW, cellH;    // row 1's cell, record px; cellH = each row's height
    int gap;             // record px between two cells
    int x0, y0;          // row 1's first cell, record px (frame-local)
    int stepW;           // row 2's cell, record px (its 15-cell grid)
    int buttons;         // the group cells of one page
    int caption;         // the caption size in SCREEN px at the scale of the fit
    int empty;           // row 1's cells left unused on the last page
    int minW, minH;      // the smallest row-1 cell, SCREEN px
    UtPadPages pages;    // the N groups over the pages (pages == 1: not paged)
    UtRect rect;         // the rectangle the pad fills
};

// Row-1 cell `slot` of a fitted pad, record px (0 .. cols - 1).
inline UtRect utPadCellRect(const UtPadFit& f, int slot) {
    UtRect r = {0, 0, 0, 0};
    if (!f.ok || slot < 0 || slot >= f.cols) return r;
    r.x = f.x0 + slot * (f.cellW + f.gap);
    r.y = f.y0;
    r.w = f.cellW;
    r.h = f.cellH;
    return r;
}

namespace layout_detail {
// The pad with `cells` row-1 cells in `r`: false when a cell is narrower than 20 or a row lower
// than 12 record px, row 2's cell narrower than 16, or the smallest row-1 cell cannot carry a
// size-6 caption (TQ's caption rule, utPadCaptionSize, over the widest caption).
inline bool padTry(const UtRect& r, int cells, int gap, float s, int widest, UtPadFit* f) {
    if (cells < 1) return false;
    const int m = kUtPadMargin;
    UtPadFit t = {};
    t.cols = cells;
    t.rows = 2;
    t.gap = gap;
    t.x0 = r.x + m;
    t.y0 = r.y + m;
    t.cellW = (r.w - 2 * m - (cells - 1) * gap) / cells;
    t.cellH = (r.h - 2 * m - gap) / 2;
    t.stepW = (r.w - 2 * m - (kUtPadRowCells - 1) * gap) / kUtPadRowCells;
    t.rect = r;
    if (t.cellW < kUtPadMinCellW || t.cellH < kUtPadMinCellH || t.stepW < kUtPadRow2CellMin)
        return false;
    t.minW = 1 << 24;
    t.minH = 1 << 24;
    for (int i = 0; i < cells; ++i) {
        const UtRect c = utPadCellRect(UtPadFit{1, t.cols, 2, t.cellW, t.cellH, t.gap, t.x0, t.y0}, i);
        const UtRectF d = utScaledRectF(c, s);
        if ((int)d.w < t.minW) t.minW = (int)d.w;
        if ((int)d.h < t.minH) t.minH = (int)d.h;
    }
    t.caption = utPadCaptionSize((float)t.minW, (float)t.minH, widest, s);
    if (t.caption < 6) return false;
    t.ok = 1;
    if (f) *f = t;
    return true;
}
}  // namespace layout_detail

// The pad for `groups` group buttons in `r` at UI scale `s`: every group in row 1 when they fit;
// else the pages of ut_padpage.h (utPadPaginate: K groups and NEXT per page, K the largest that
// fits); else not ok (no pad - the caller falls back to the derived one). `widest` = the widest
// group caption in characters (NEXT's ">>" counts too while paged). Row 2 keeps its 15 cells of at
// least 16 record px, so a pad narrower than 260 record px is refused whatever the group count: the
// pages serve only the widths from 260 up to the one that holds every group in one row.
inline UtPadFit utPadFitRect(const UtRect& r, int groups, int gap, float s, int widest,
                             int nextChars = kUtPadNextChars) {
    UtPadFit best = {};
    if (groups < 1 || groups > kUtPadButtonsMax || r.w < 1 || r.h < 1 || !(s > 0.3f && s < 4.0f))
        return best;
    if (gap < 0) gap = 0;
    if (gap > 4) gap = 4;
    // GD's button count: G + 2 unpaged (the materials page, G groups, OWN), K + 3 paged (NEXT
    // too). Row 1 holds the groups only (Transfer and OWN are in row 2): n - 2 cells.
    const int nx = utPadNextMin(nextChars);
    const int wid = widest > nx ? widest : nx;
    const UtPadPages pg = utPadPaginate(groups, [&](int n) {
        return layout_detail::padTry(r, n - 2, gap, s, n - 2 > groups ? wid : widest, nullptr);
    });
    const int cells = pg.pages > 1 ? pg.perPage + 1 : groups;
    if (!layout_detail::padTry(r, cells, gap, s, pg.pages > 1 ? wid : widest, &best)) {
        best = UtPadFit{};
        return best;
    }
    best.pages = pg;
    best.buttons = pg.perPage;
    best.empty = pg.pages > 1 ? pg.pages * pg.perPage - groups : 0;
    return best;
}

// The row-1 slot's content on page `page`: a group index, -1 = an empty cell (the last page's
// unused ones), -2 = NEXT.
const int kUtPadSlotEmpty = -1;
const int kUtPadSlotNext = -2;
inline int utPadSlotGroup(const UtPadFit& f, int page, int slot) {
    if (!f.ok || slot < 0 || slot >= f.cols) return kUtPadSlotEmpty;
    if (f.pages.pages <= 1) return slot < f.pages.groups ? slot : kUtPadSlotEmpty;
    if (slot == f.cols - 1) return kUtPadSlotNext;
    const int g = page * f.pages.perPage + slot;
    return g < f.pages.groups ? g : kUtPadSlotEmpty;
}

// ---- the derived pad keyed by buttons ---------------------------------------------------------
// The derived pad (utPadLayout's, no [layout] line) for `buttons` buttons: the ground and row 2
// exactly as utPadLayout placed them (Transfer, OWN, < >, the field, its x and the label keep their
// cells); row 1's span - the 15 cells and their gaps - cut into one equal cell per button while
// each is at least 20 record px (utPadLayout's own floor), else paged (utPadPaginate: K buttons and
// NEXT per page, K the largest that fits). 15 buttons are the 15 cells utPadLayout cuts, exactly.
// `def` = utPadLayout's output, `rec` = its ground in record px; `widest` = the widest caption.
inline UtPadFit utPadDerivedFit(const UtPadOut& def, const UtRect& rec, int buttons, int widest, float s,
                                int nextChars = kUtPadNextChars) {
    UtPadFit d = {};
    if (buttons < 1 || buttons > kUtPadButtonsMax || def.cellW < 1 || !(s > 0.3f && s < 4.0f)) return d;
    const int gap = def.gap;
    const int span = kUtPadDefCells * def.cellW + (kUtPadDefCells - 1) * gap;
    auto cellOf = [&](int cells) { return cells < 1 ? 0 : (span - (cells - 1) * gap) / cells; };
    const UtPadPages pg = utPadPaginate(buttons, [&](int n) { return cellOf(n - 2) >= kUtPadMinCellW; });
    const int cells = pg.pages > 1 ? pg.perPage + 1 : buttons;
    if (cellOf(cells) < kUtPadMinCellW) return d;   // not even one button and NEXT: no pad
    d.ok = 1;
    d.cols = cells;
    d.rows = 2;
    d.cellW = cellOf(cells);
    d.cellH = def.cellH;
    d.gap = gap;
    d.x0 = rec.x + kUtPadMargin;
    d.y0 = rec.y + kUtPadMargin;
    d.stepW = def.cellW;
    d.buttons = pg.perPage;
    d.pages = pg;
    d.empty = pg.pages > 1 ? pg.pages * pg.perPage - buttons : 0;
    d.rect = rec;
    d.minW = 1 << 24;
    for (int i = 0; i < cells; ++i) {
        const float x0 = (float)kUtPadMargin + (float)i * (float)(d.cellW + gap);
        const int w = (int)(utPadRound((x0 + (float)d.cellW) * s) - utPadRound(x0 * s));
        if (w < d.minW) d.minW = w;
    }
    d.minH = (int)def.rowPx;
    const int wid = pg.pages > 1 && widest < utPadNextMin(nextChars) ? utPadNextMin(nextChars) : widest;
    d.caption = utPadCaptionSize((float)d.minW, def.rowPx, wid, s);
    return d;
}

// Row 1 of the derived pad `o` (utPadLayout's output, row 2 untouched) cut for the fit `d` on page
// `page`: button b's cell in btn[kUtPadGroup0 + b] (every other button's empty), NEXT in
// btn[kUtPadPage] while paged. Both edges of a cell rounded from the pad's left as utPadLayout's.
inline void utPadDerivedCut(const UtPadFit& d, int page, float s, UtPadOut* o) {
    if (!o || !d.ok) return;
    const UtRectF zero = {0.0f, 0.0f, 0.0f, 0.0f};
    const float ox = o->ground.x, y = o->ground.y + o->marginPx, h = o->rowPx;
    for (int i = 0; i < kUtPadCells; ++i) o->btn[kUtPadGroup0 + i] = zero;
    o->btn[kUtPadPage] = zero;
    const float step = (float)(d.cellW + d.gap);
    for (int slot = 0; slot < d.cols; ++slot) {
        const int b = utPadSlotGroup(d, page, slot);
        const UtRectF r = utPadRectX(ox, s, (float)kUtPadMargin + (float)slot * step, (float)d.cellW, y, h);
        if (b >= 0 && b < kUtPadCells) o->btn[kUtPadGroup0 + b] = r;
        else if (b == kUtPadSlotNext) o->btn[kUtPadPage] = r;
    }
    o->cellW = d.cellW;
}

namespace layout_detail {
// Record x edges [x0, x1) and y edges [y0, y1) at the scale, both edges rounded, from (ox, oy).
inline UtRectF edges(float ox, float oy, float s, float x0, float x1, float y0, float y1) {
    const float a = utPadRound(x0 * s), b = utPadRound(x1 * s);
    const float c = utPadRound(y0 * s), d = utPadRound(y1 * s);
    const UtRectF r = {ox + a, oy + c, b - a, d - c};
    return r;
}
}  // namespace layout_detail

// A fitted pad on page `page`, drawn from the frame's rounded top-left (ox, oy): the ground, the
// groups of the page (every other group's rect empty), NEXT while paged (btn[kUtPadPage]), row 2
// as ut_padlayout.h lays it out (the field and its clear square with `search`, EDIT in cell 7 with
// `edit`), and the label box (one cell later with `edit`).
inline bool utPadOutFromFit(const UtPadFit& f, int page, float ox, float oy, float s, bool search,
                            bool edit, UtPadOut* out) {
    if (!out || !f.ok) return false;
    const UtRectF zero = {0.0f, 0.0f, 0.0f, 0.0f};
    for (int i = 0; i < kUtPadMax; ++i) out->btn[i] = zero;
    const UtRect& r = f.rect;
    const float m = (float)kUtPadMargin, gap = (float)f.gap;
    out->ground = utPadRect(ox, oy, s, (float)r.x, (float)r.y, (float)r.w, (float)r.h);
    const float r1 = (float)f.y0, r2 = (float)(f.y0 + f.cellH + f.gap), h = (float)f.cellH;
    for (int slot = 0; slot < f.cols; ++slot) {
        const int g = utPadSlotGroup(f, page, slot);
        const UtRect c = utPadCellRect(f, slot);
        const UtRectF d = layout_detail::edges(ox, oy, s, (float)c.x, (float)(c.x + c.w), r1, r1 + h);
        if (g >= 0 && g < kUtPadCells) out->btn[kUtPadGroup0 + g] = d;
        else if (g == kUtPadSlotNext) out->btn[kUtPadPage] = d;
    }
    // row 2: the four buttons in 4 cells of the 15-cell grid, as utPadLayout cuts them
    const float step = (float)(f.stepW + f.gap);
    const float bx = (float)r.x;
    const float e[5] = {bx + m, bx + m + 2.0f * step, bx + m + 3.0f * step, bx + m + 3.5f * step,
                        bx + m + 4.0f * step};
    static const int kRow2[4] = {kUtPadToggle, kUtPadOwn, kUtPadPrev, kUtPadNext};
    for (int i = 0; i < 4; ++i)
        out->btn[kRow2[i]] = layout_detail::edges(ox, oy, s, e[i], e[i + 1] - gap, r2, r2 + h);
    const UtRectF row2 = layout_detail::edges(ox, oy, s, bx, bx + 1.0f, r2, r2 + h);
    if (search) {
        const UtRectF fld = layout_detail::edges(ox, oy, s, e[4], bx + m + 7.0f * step - gap, r2, r2 + h);
        float side = row2.h;
        if (side > utPadFloor(fld.w * 0.5f)) side = utPadFloor(fld.w * 0.5f);
        out->btn[kUtPadSearch] = fld;
        out->btn[kUtPadClear] = UtRectF{fld.x + fld.w - side, fld.y, side, fld.h};
        float tw = fld.w - side - utPadRound(gap * s);
        if (tw < 0.0f) tw = 0.0f;
        out->fieldText = UtRectF{fld.x, fld.y, tw, fld.h};
    } else {
        out->fieldText = zero;
    }
    if (edit)   // EDIT: cell 7, right of the field (zero above without it)
        out->btn[kUtPadEdit] = layout_detail::edges(ox, oy, s, bx + m + 7.0f * step, bx + m + 8.0f * step - gap,
                                                    r2, r2 + h);
    const float lx = bx + m + (edit ? 8.0f : 7.0f) * step + 3.0f;
    out->label = layout_detail::edges(ox, oy, s, lx, bx + (float)r.w - m, r2, r2 + h);
    out->band = kUtPadBelow;
    out->fit = kUtPadFitAsSet;
    out->cellW = f.cellW;
    out->cellH = f.cellH;
    out->gap = f.gap;
    out->rowPx = row2.h;
    out->gapPx = utPadRound(gap * s);
    out->marginPx = utPadRound(m * s);
    out->dyPx = 0.0f;
    return true;
}

// The label box a fitted pad carries in its row 2, record px (the derived label of a moved pad);
// with `edit` (the EDIT button in cell 7) it starts one cell later.
inline UtRect utPadLabelBox(const UtPadFit& f, bool edit) {
    UtRect r = {0, 0, 0, 0};
    if (!f.ok) return r;
    const int step = f.stepW + f.gap;
    const int lx = f.rect.x + kUtPadMargin + (edit ? 8 : 7) * step + 3;
    r.x = lx;
    r.y = f.y0 + f.cellH + f.gap;
    r.w = f.rect.x + f.rect.w - kUtPadMargin - lx;
    r.h = f.cellH;
    if (r.w < 1) r.w = 1;
    return r;
}

// ---- the label ---------------------------------------------------------------------------------

const int kUtLabelSizeMin = 6;   // screen px: TQ's label floor (utPadLabelFont)

struct UtLabelFit {
    int ok;        // 1 = the label can be drawn in the rectangle
    int size;      // the font size drawn, SCREEN px
    int bounded;   // 1 = the rectangle's height brought the requested size down
    int x, top;    // where the text starts, record px: the left edge, the top
    int limitPx;   // the width budget, SCREEN px
};

// `labelPx` is plate_label_size at the scale (utPlateLabelPx). The size is the row rule of the
// pad's label (utPadLabelFont: the rectangle's drawn height less 1 px, never under 6); under 6, or
// with under 8 px of width, the label is refused.
inline UtLabelFit utLabelFit(const UtRect& r, int labelPx, float s) {
    UtLabelFit f = {};
    if (!(s > 0.3f && s < 4.0f) || r.w < 1 || r.h < 1) return f;
    const UtRectF d = utScaledRectF(r, s);
    const int fs = utPadLabelFont(labelPx, d.h);
    if (fs < kUtLabelSizeMin) return f;
    f.x = r.x;
    f.top = r.y;
    f.limitPx = (int)d.w;
    if (f.limitPx < 8) return f;
    f.size = fs;
    f.bounded = fs < labelPx ? 1 : 0;
    f.ok = 1;
    return f;
}

// ---- resolving the player's rectangles --------------------------------------------------------

enum { kUtLayGrid = 0, kUtLayPad = 1, kUtLayLabel = 2, kUtLayParts = 3 };

enum UtLayoutWhy {
    kUtLayoutOk = 0,      // the player's rectangle is in force
    kUtLayoutUnset,       // no rectangle of the player's; the derived one is in force
    kUtLayoutOutside,     // not wholly inside the caravan window
    kUtLayoutGridSmall,   // smaller than the largest cell (its height plus the row gap)
    kUtLayoutPadNoFit,    // no row of cells 20 x 12 with a caption of 6 or more
    kUtLayoutLabelSmall,  // under 6 px of text, or under 8 px of width, at this UI scale
    kUtLayoutOverlap,     // meets another rectangle in force; `against` names it
    // The screen parts only (pad_screen / label_screen):
    kUtLayoutScreenOverGrid,   // its client rectangle meets the item grid in force (the
                               // collection's: the real Transfer page's cells may be covered)
    kUtLayoutScreenLarge,      // at this UI scale it is as wide or as tall as the screen
    kUtLayoutScreenNoView      // the screen size or the window's place is not known (yet)
};

inline const char* utLayoutPartName(int part) {
    return part == kUtLayGrid ? "grid" : part == kUtLayPad ? "pad" : part == kUtLayLabel ? "label"
                                                                                       : "?";
}

inline const char* utLayoutWhyText(int why) {
    switch (why) {
        case kUtLayoutOk: return "in force";
        case kUtLayoutUnset: return "not set";
        case kUtLayoutOutside: return "is not inside the caravan window";
        case kUtLayoutGridSmall: return "is smaller than the largest item cell";
        case kUtLayoutPadNoFit: return "has no room for the buttons with readable captions";
        case kUtLayoutLabelSmall: return "is too small for the label text";
        case kUtLayoutOverlap: return "overlaps another part of the page";
        case kUtLayoutScreenOverGrid: return "would cover the item grid";
        case kUtLayoutScreenLarge: return "is as large as the screen";
        case kUtLayoutScreenNoView: return "cannot be placed yet (no screen size or window place)";
        default: return "?";
    }
}

// ---- the screen-anchored pad and label: pad_screen / label_screen ------------------------------
//
// MEANING. `pad_screen` / `label_screen` = "<corner>,x,y,w,h" (UtScreenRect): <corner> is one of
// tl tr bl br (the screen corner the part is pinned to), x,y record px from that corner INWARD to
// the part's nearest edges, w,h the part's size in record px. ONE value for every caravan window
// size. Record px are the caravan window's own unit: the measured UI scale `s` turns them into
// canvas px exactly as it does for the page, so the part keeps its corner and its UI-scaled margin
// on every resolution. The grid is NEVER a screen part.
//
// PLACEMENT (utScreenPlace): each number scaled with floorf(v*s+0.5f); left = ix (tl, bl) or
// screenW - ix - pw (tr, br); top = iy (tl, tr) or screenH - iy - ph (bl, br); then SHIFTED, never
// resized, back into 0..screenW x 0..screenH when the offset pushes it past an edge. screenW/H =
// the canvas the pad is drawn on.
//
// INTO THE PAGE'S MODEL (utScreenToLocal): the canvas rectangle becomes a FRAME-LOCAL record
// rectangle relative to the frame's rounded top-left on the canvas (originX/Y). It lies outside the
// window, often far outside (negative too), and that is fine: utPadFitRect / utLabelFit, the cells,
// the hover and the outline read it exactly as they read an in-window rectangle, and
// utLocalToClient(rect, originX, originY, s) is where it is drawn.
//
// RESOLUTION (utLayoutResolve). A screen part is IN FORCE when its key parses, the view is known
// (screenKnown), it is smaller than the screen at this scale, its local rectangle fits (the pad:
// utPadFitRect finds a layout with a readable caption; the label: utLabelFit), and its drawn
// rectangle does not meet the drawn rectangle of the GRID in force (kUtLayoutScreenOverGrid - the
// collection's cells must stay clickable; on the game's own Transfer page the screen part is the
// collection page's, over whatever real cells lie under it: utLayoutRealPage). A screen label that
// meets a screen pad yields to it
// (kUtLayoutOverlap, against the pad). It skips kUtLayoutOutside and the pairwise rules against
// the IN-WINDOW parts (it is drawn above the page; only the grid is protected). Refused, the part
// falls back to what the page has without the key - the player's in-window pad_/label_<W>x<H>
// when legal, else the derived one - and L.screenWhy says why. In force: L.screen[p] = 1,
// L.user[p] = 1, L.why[p] = kUtLayoutOk, L.rect[p] = the local rectangle, L.screenPx[p] = the
// canvas rectangle drawn. A refusal is a property of THIS window place: the caller resolves again
// whenever the canvas size, the origin or the scale change.

enum UtCorner { kUtCornerTL = 0, kUtCornerTR, kUtCornerBL, kUtCornerBR };

struct UtScreenRect {
    int corner;   // UtCorner: the screen corner the part is pinned to
    int x, y;     // record px from that corner INWARD to the part's nearest edges (>= 0)
    int w, h;     // record px (>= 1)
};

const int kUtScreenMax = 16384;   // the largest number a screen key may hold (record px)

inline const char* utCornerName(int c) {
    switch (c) {
        case kUtCornerTL: return "tl";
        case kUtCornerTR: return "tr";
        case kUtCornerBL: return "bl";
        case kUtCornerBR: return "br";
        default: return "?";
    }
}

inline bool utScreenRectEq(const UtScreenRect& a, const UtScreenRect& b) {
    return a.corner == b.corner && a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

// "<corner>,x,y,w,h": the corner tl|tr|bl|br (any case), then four whole numbers 0..16384, w and
// h at least 1; blanks around every item. A minus sign anywhere is refused (the offset runs
// inward from the corner; there is no negative of it). Nothing after the fifth item.
inline bool utScreenRectParse(const char* s, UtScreenRect* out) {
    if (!s || !out) return false;
    for (const char* q = s; *q; ++q)
        if (*q == '-') return false;
    const char* p = s;
    while (*p == ' ' || *p == '\t') ++p;
    char c0 = p[0];
    char c1 = c0 ? p[1] : (char)0;
    if (c0 >= 'A' && c0 <= 'Z') c0 = (char)(c0 - 'A' + 'a');
    if (c1 >= 'A' && c1 <= 'Z') c1 = (char)(c1 - 'A' + 'a');
    int corner = -1;
    if (c0 == 't' && c1 == 'l') corner = kUtCornerTL;
    if (c0 == 't' && c1 == 'r') corner = kUtCornerTR;
    if (c0 == 'b' && c1 == 'l') corner = kUtCornerBL;
    if (c0 == 'b' && c1 == 'r') corner = kUtCornerBR;
    if (corner < 0) return false;
    p += 2;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p != ',') return false;
    ++p;
    int v[4];
    for (int i = 0; i < 4; ++i) {
        if (!utParseNum(p, &v[i])) return false;
        if (i < 3) {
            if (*p != ',') return false;
            ++p;
        }
    }
    while (*p == '\r' || *p == '\n') ++p;
    if (*p) return false;
    for (int i = 0; i < 4; ++i)
        if (v[i] < 0 || v[i] > kUtScreenMax) return false;
    if (v[2] < 1 || v[3] < 1) return false;
    out->corner = corner;
    out->x = v[0];
    out->y = v[1];
    out->w = v[2];
    out->h = v[3];
    return true;
}

// Writes "tr,8,8,320,44". Returns the length, or 0 when it did not fit.
inline int utScreenRectFormat(const UtScreenRect& r, char* out, size_t cap) {
    if (!out || cap == 0) return 0;
    const int n = _snprintf_s(out, cap, _TRUNCATE, "%s,%d,%d,%d,%d", utCornerName(r.corner), r.x,
                              r.y, r.w, r.h);
    return (n > 0 && (size_t)n < cap) ? n : 0;
}

// Where `r` lands on a screenW x screenH canvas at UI scale `s`, canvas px (whole numbers held in
// floats): see PLACEMENT above. *shifted = 1 when an edge was brought back into the viewport. A
// zero rectangle for an unusable screen size or scale. A part as large as the screen ends at the
// top-left and still overflows - utScreenLocate refuses it first (kUtLayoutScreenLarge).
inline UtRectF utScreenPlace(const UtScreenRect& r, int screenW, int screenH, float s,
                             int* shifted) {
    UtRectF f = {0.0f, 0.0f, 0.0f, 0.0f};
    if (shifted) *shifted = 0;
    if (screenW < 1 || screenH < 1 || !(s >= 0.2f && s <= 8.0f)) return f;
    const float ix = floorf((float)r.x * s + 0.5f), iy = floorf((float)r.y * s + 0.5f);
    const float pw = floorf((float)r.w * s + 0.5f), ph = floorf((float)r.h * s + 0.5f);
    const bool right = r.corner == kUtCornerTR || r.corner == kUtCornerBR;
    const bool bottom = r.corner == kUtCornerBL || r.corner == kUtCornerBR;
    float left = right ? (float)screenW - ix - pw : ix;
    float top = bottom ? (float)screenH - iy - ph : iy;
    int moved = 0;
    if (left + pw > (float)screenW) {
        left = (float)screenW - pw;
        moved = 1;
    }
    if (left < 0.0f) {
        left = 0.0f;
        moved = 1;
    }
    if (top + ph > (float)screenH) {
        top = (float)screenH - ph;
        moved = 1;
    }
    if (top < 0.0f) {
        top = 0.0f;
        moved = 1;
    }
    if (shifted) *shifted = moved;
    f.x = left;
    f.y = top;
    f.w = pw;
    f.h = ph;
    return f;
}

// Frame-local record px -> canvas px: the page's own rule - the frame's origin plus each EDGE
// rounded with floorf(v*s+0.5f) (utScaledRectF), which is where the draw puts it.
inline UtRectF utLocalToClient(const UtRect& r, float originX, float originY, float s) {
    UtRectF f = utScaledRectF(r, s);
    f.x += originX;
    f.y += originY;
    return f;
}

// Canvas px -> frame-local record px: the position (px - origin) / s rounded to the nearest record
// px, the size the key's own w x h (never re-derived from px, which a scale under 1 would round
// away). utLocalToClient of the result lies within s/2 + 1 px of `px` per edge.
inline UtRect utScreenToLocal(const UtRectF& px, int w, int h, float originX, float originY,
                              float s) {
    UtRect r = {0, 0, w, h};
    if (!(s >= 0.2f && s <= 8.0f)) return r;
    r.x = (int)floorf((px.x - originX) / s + 0.5f);
    r.y = (int)floorf((px.y - originY) / s + 0.5f);
    return r;
}

struct UtScreenSpot {
    int why;          // kUtLayoutOk, kUtLayoutScreenNoView or kUtLayoutScreenLarge
    UtRect local;     // frame-local record px: L.rect of a screen part in force
    UtRectF px;       // canvas px where it is drawn: utLocalToClient(local, origin, s)
    UtRectF placed;   // canvas px as utScreenPlace put it (px is within s/2 + 1 of it)
    int shifted;      // 1 = utScreenPlace brought it back into the viewport
};

// Place, convert, and keep the DRAWN rectangle on screen: the local position moves by whole
// record px until utLocalToClient lies inside 0..screenW x 0..screenH (the rounding alone can put
// an edge a pixel past the screen's). screenKnown = 0 (no frame drawn yet, the window's place not
// measured) refuses with kUtLayoutScreenNoView; a part as wide or as tall as the screen with
// kUtLayoutScreenLarge.
inline UtScreenSpot utScreenLocate(const UtScreenRect& r, int screenKnown, int screenW,
                                   int screenH, float originX, float originY, float s) {
    UtScreenSpot o = {};
    o.why = kUtLayoutScreenNoView;
    if (!screenKnown || screenW < 1 || screenH < 1 || !(s >= 0.2f && s <= 8.0f)) return o;
    o.placed = utScreenPlace(r, screenW, screenH, s, &o.shifted);
    if (o.placed.w >= (float)screenW || o.placed.h >= (float)screenH) {
        o.why = kUtLayoutScreenLarge;
        return o;
    }
    o.local = utScreenToLocal(o.placed, r.w, r.h, originX, originY, s);
    for (int k = 0; k < 64; ++k) {
        const UtRectF d = utLocalToClient(o.local, originX, originY, s);
        bool moved = false;
        if (d.x < 0.0f) {
            ++o.local.x;
            moved = true;
        } else if (d.x + d.w > (float)screenW) {
            --o.local.x;
            moved = true;
        }
        if (d.y < 0.0f) {
            ++o.local.y;
            moved = true;
        } else if (d.y + d.h > (float)screenH) {
            --o.local.y;
            moved = true;
        }
        if (!moved) break;
    }
    o.px = utLocalToClient(o.local, originX, originY, s);
    o.why = kUtLayoutOk;
    return o;
}

// The editor's SAVE: a canvas rectangle -> the screen key of its NEAREST corner (the one whose
// two edges are closest; left and top on a tie), offsets and size back in record px at `s`.
inline UtScreenRect utScreenFromClient(const UtRectF& px, int screenW, int screenH, float s) {
    UtScreenRect r = {kUtCornerTL, 0, 0, 1, 1};
    if (!(s >= 0.2f && s <= 8.0f)) return r;
    const float dl = px.x, dr = (float)screenW - (px.x + px.w);
    const float dt = px.y, db = (float)screenH - (px.y + px.h);
    const bool right = dr < dl, bottom = db < dt;
    r.corner = right ? (bottom ? kUtCornerBR : kUtCornerTR) : (bottom ? kUtCornerBL : kUtCornerTL);
    const float ox = right ? dr : dl, oy = bottom ? db : dt;
    r.x = (int)floorf((ox < 0.0f ? 0.0f : ox) / s + 0.5f);
    r.y = (int)floorf((oy < 0.0f ? 0.0f : oy) / s + 0.5f);
    r.w = (int)floorf(px.w / s + 0.5f);
    r.h = (int)floorf(px.h / s + 0.5f);
    if (r.w < 1) r.w = 1;
    if (r.h < 1) r.h = 1;
    if (r.x > kUtScreenMax) r.x = kUtScreenMax;
    if (r.y > kUtScreenMax) r.y = kUtScreenMax;
    if (r.w > kUtScreenMax) r.w = kUtScreenMax;
    if (r.h > kUtScreenMax) r.h = kUtScreenMax;
    return r;
}

// Two drawn rectangles share at least one pixel (touching edges do not meet).
inline bool utRectsOverlap(const UtRectF& a, const UtRectF& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

struct UtLayoutInput {
    UtRect plate;                 // the caravan window, record px frame-local: 0,0,W,H
    UtRect def[kUtLayParts];      // the measured grid, today's pad and its label box
    const UtRect* user[kUtLayParts];   // the player's rectangles; null = no key
    UtPadFit defPad;              // today's pad as a fit (what the derived pad draws)
    int groups;                   // N, the group buttons
    int gap;                      // record px between two cells
    float scale;                  // the UI scale the pad and the label are fitted at
    int widest;                   // the widest group caption, characters
    int nextChars = kUtPadNextChars;   // NEXT's caption, characters (next.caption)
    int labelPx;                  // plate_label_size at the scale
    int maxCellW, maxCellH;       // the largest slot of any group, record px (a user grid holds
                                  // it); 0 = one sack cell
    int rowGap;                   // the grid's row gap (the sack's rows touch: 0)
    int edit = 0;                 // 1 = the EDIT button in row 2's cell 7 (edit_button=1): the pad's
                                  // label box starts one cell later
    // The screen parts (pad_screen / label_screen), BY VALUE so a copy of the input is
    // self-contained. All zero = no screen key = exactly the in-window resolution. The grid's
    // slot is never read: the grid is never a screen part.
    int screenSet[kUtLayParts];           // 1 = screenRect[p] is the player's key for part p
    UtScreenRect screenRect[kUtLayParts];
    int screenKnown;              // 1 = screenW/H and originX/Y are this frame's
    int screenW, screenH;         // the canvas, px
    float originX, originY;       // the frame's rounded top-left on the canvas
};

struct UtLayout {
    UtRect rect[kUtLayParts];     // the rectangles in force
    int user[kUtLayParts];        // 1 = the player's rectangle is the one in force
    int why[kUtLayParts];         // UtLayoutWhy
    int against[kUtLayParts];     // for kUtLayoutOverlap: the part it met, else -1
    UtPadFit pad;                 // the pad in force (today's, or fitted in the player's rectangle)
    UtLabelFit label;             // the label fitted in rect[kUtLayLabel]
    int labelInPad;               // 1 = the label is the pad's own row-2 box (not the player's)
    int padDerived;               // 1 = the player's pad is the derived pad's rectangle (each edge
                                  // within one record px): the derived pad is the one drawn
    int screen[kUtLayParts];      // 1 = the part in force is its screen key (pad_screen ...)
    int screenWhy[kUtLayParts];   // what became of the screen key: Unset, Ok or the refusal
    int screenAgainst[kUtLayParts];   // the part a refused screen key met, else -1
    UtRectF screenPx[kUtLayParts];    // a screen part in force: canvas px, as drawn
    int screenShifted[kUtLayParts];   // 1 = it was shifted back into the viewport
    // 1 = the part's <part>_<W>x<H> line is set but was NOT judged: its screen key is set, so the
    // line is ignored (no refusal, no overlap).
    int ignored[kUtLayParts];
};

namespace layout_detail {
// The label box of the pad in force: the derived one's, or row 2 of the player's pad fitted.
inline UtRect padLabelBoxOf(const UtLayoutInput& in, const UtLayout& L) {
    if (!L.user[kUtLayPad] || L.padDerived) return in.def[kUtLayLabel];
    return utPadLabelBox(utPadFitRect(L.rect[kUtLayPad], in.groups, in.gap, in.scale, in.widest, in.nextChars),
                         in.edit != 0);
}

// The in-window resolution (GD's rules for the <W>x<H> rectangles). A part with skip[p] set is
// pinned to the screen and takes no part in the pairwise rules. TQ's one change: the derived label
// is the pad's own row-2 box, so the pad and the label are checked against each other only while
// the label is the player's. `labelRides` = the pad is pinned to the screen (in force there, or
// waiting to be placed): the derived label is that pad's box, on the screen with it, so while the
// label is not the player's it takes no
// part in the pairwise rules either (no in-window part meets it).
inline UtLayout resolveWindow(const UtLayoutInput& in, const int skip[kUtLayParts],
                              bool labelRides = false) {
    UtLayout L = {};
    for (int p = 0; p < kUtLayParts; ++p) {
        L.rect[p] = in.def[p];
        L.user[p] = 0;
        L.why[p] = in.user[p] ? kUtLayoutOk : kUtLayoutUnset;
        L.against[p] = -1;
        if (!in.user[p]) continue;
        const UtRect& u = *in.user[p];
        int why = kUtLayoutOk;
        bool asDerived = false;
        if (!utRectInside(u, in.plate)) {
            why = kUtLayoutOutside;
        } else if (p == kUtLayGrid) {
            const int cw = in.maxCellW > 0 ? in.maxCellW : kUtGridCell;
            const int ch = in.maxCellH > 0 ? in.maxCellH : kUtGridCell;
            if (u.w < cw || u.h < ch + in.rowGap) why = kUtLayoutGridSmall;
        } else if (p == kUtLayPad) {
            // the derived pad's own numbers (the outline's, rounded at another UI scale) give the
            // derived pad
            if (in.defPad.ok && utRectNear(u, in.def[kUtLayPad], 1))
                asDerived = true;
            else if (!utPadFitRect(u, in.groups, in.gap, in.scale, in.widest, in.nextChars).ok)
                why = kUtLayoutPadNoFit;
        } else if (!utLabelFit(u, in.labelPx, in.scale).ok) {
            why = kUtLayoutLabelSmall;
        }
        L.why[p] = why;
        if (why == kUtLayoutOk) {
            L.rect[p] = asDerived ? in.def[p] : u;
            L.user[p] = 1;
            if (asDerived) L.padDerived = 1;
        }
    }
    static const int kPairs[3][2] = {{kUtLayGrid, kUtLayPad},
                                     {kUtLayGrid, kUtLayLabel},
                                     {kUtLayPad, kUtLayLabel}};
    for (int pass = 0; pass < 2 * kUtLayParts; ++pass) {
        bool moved = false;
        for (int k = 0; k < 3 && !moved; ++k) {
            const int a = kPairs[k][0], b = kPairs[k][1];
            if (skip[a] || skip[b]) continue;
            if (labelRides && b == kUtLayLabel && !L.user[kUtLayLabel]) continue;
            if (a == kUtLayPad && b == kUtLayLabel) {
                if (!L.user[kUtLayLabel]) continue;
                // a label of the player's inside the pad's own label box is that label moved
                // within its box; one within a record px of the box (its numbers rounded at another
                // UI scale) is the box
                const UtRect box = padLabelBoxOf(in, L);
                if (box.w > 0 && box.h > 0 && utRectInside(L.rect[b], box)) continue;
                if (box.w > 0 && box.h > 0 && utRectNear(L.rect[b], box, 1)) {
                    L.rect[b] = box;
                    continue;
                }
            }
            if (!utRectsMeet(L.rect[a], L.rect[b])) continue;
            const int drop = L.user[b] ? b : L.user[a] ? a : -1;
            if (drop < 0) continue;   // two derived rectangles: legal by construction
            L.rect[drop] = in.def[drop];
            L.user[drop] = 0;
            if (drop == kUtLayPad) L.padDerived = 0;
            L.why[drop] = kUtLayoutOverlap;
            L.against[drop] = drop == a ? b : a;
            moved = true;
        }
        if (!moved) break;
    }
    return L;
}
}  // namespace layout_detail

// Each rectangle of the player's is checked on its own first (inside the caravan window; the pad
// fits its buttons; the label its text), then the three in force must be pairwise disjoint, in the
// order grid, pad, label: of two of the player's that meet, the later one falls back to its derived
// rectangle; one that meets a DERIVED rectangle falls back itself. This repeats until nothing
// meets, so the worst case is all three derived - what the mod has always drawn, legal by
// construction. Nothing is clamped: a refused rectangle is refused whole, and `why` / `against`
// say why, for the one WARN line per change. TQ: a pad within one record px of the derived pad's
// rectangle IS the derived pad, and a label inside the pad's label box (or within one record px of
// it: the box) is not an overlap, so the numbers the outline shows can be typed back at any scale.
//
// THE SCREEN PARTS (see "the screen-anchored pad and label" above): a pad or label whose screen
// key is set is placed first (utScreenLocate) and fitted at its local rectangle; the in-window
// rules then run without it; a screen part whose drawn rectangle meets the grid in force is
// refused (kUtLayoutScreenOverGrid) and takes part in the in-window rules again, and so on until
// nothing changes (at most three rounds: a screen part is only ever dropped). A screen label that
// meets a screen pad yields to it. The grid in force never depends on a screen part. A part whose
// screen key is in force or waits to be placed (kUtLayoutScreenNoView) is PINNED: its
// <part>_<W>x<H> line is not judged at all (L.ignored). A pad on the screen carries its row-2
// label box with it while the label is not the player's: that label is on the screen too, so no
// in-window part is refused for meeting the window's derived label box (GD's pinned-part rule).
inline UtLayout utLayoutResolve(const UtLayoutInput& in) {
    UtLayoutInput win = in;   // what the in-window rules see: no per-cover line for a pinned part
    int scr[kUtLayParts] = {0, 0, 0};
    int scrWhy[kUtLayParts] = {kUtLayoutUnset, kUtLayoutUnset, kUtLayoutUnset};
    int scrAgainst[kUtLayParts] = {-1, -1, -1};
    UtScreenSpot spot[kUtLayParts] = {};
    for (int p = kUtLayPad; p < kUtLayParts; ++p) {   // never the grid
        if (!in.screenSet[p]) continue;
        spot[p] = utScreenLocate(in.screenRect[p], in.screenKnown, in.screenW, in.screenH,
                                 in.originX, in.originY, in.scale);
        int why = spot[p].why;
        if (why == kUtLayoutOk) {
            if (p == kUtLayPad) {
                if (!utPadFitRect(spot[p].local, in.groups, in.gap, in.scale, in.widest, in.nextChars).ok)
                    why = kUtLayoutPadNoFit;
            } else if (!utLabelFit(spot[p].local, in.labelPx, in.scale).ok) {
                why = kUtLayoutLabelSmall;
            }
        }
        scrWhy[p] = why;
        scr[p] = why == kUtLayoutOk ? 1 : 0;
    }
    // pinned: on the screen, or waiting to be placed - out of the in-window rules either way
    int pin[kUtLayParts] = {0, 0, 0};
    for (int p = kUtLayPad; p < kUtLayParts; ++p) {
        pin[p] = scr[p] || (in.screenSet[p] && scrWhy[p] == kUtLayoutScreenNoView);
        if (pin[p]) win.user[p] = nullptr;
    }
    UtLayout L = layout_detail::resolveWindow(win, pin, pin[kUtLayPad] && !pin[kUtLayLabel]);
    for (int round = 0; round < kUtLayParts + 1; ++round) {
        bool dropped = false;
        const UtRectF grid = utLocalToClient(L.rect[kUtLayGrid], in.originX, in.originY, in.scale);
        for (int p = kUtLayPad; p < kUtLayParts; ++p) {
            if (!scr[p] || !utRectsOverlap(spot[p].px, grid)) continue;
            scr[p] = 0;
            pin[p] = 0;
            win.user[p] = in.user[p];   // back in the window: its per-cover line is judged
            scrWhy[p] = kUtLayoutScreenOverGrid;
            scrAgainst[p] = kUtLayGrid;
            dropped = true;
        }
        if (scr[kUtLayPad] && scr[kUtLayLabel] &&
            utRectsOverlap(spot[kUtLayPad].px, spot[kUtLayLabel].px)) {
            scr[kUtLayLabel] = 0;
            pin[kUtLayLabel] = 0;
            win.user[kUtLayLabel] = in.user[kUtLayLabel];
            scrWhy[kUtLayLabel] = kUtLayoutOverlap;
            scrAgainst[kUtLayLabel] = kUtLayPad;
            dropped = true;
        }
        if (!dropped) break;
        L = layout_detail::resolveWindow(win, pin, pin[kUtLayPad] && !pin[kUtLayLabel]);
    }
    for (int p = 0; p < kUtLayParts; ++p) {
        L.ignored[p] = (pin[p] && in.user[p]) ? 1 : 0;
        L.screenWhy[p] = scrWhy[p];
        L.screenAgainst[p] = scrAgainst[p];
        if (!scr[p]) continue;
        L.rect[p] = spot[p].local;
        L.user[p] = 1;
        L.why[p] = kUtLayoutOk;
        L.against[p] = -1;
        L.screen[p] = 1;
        L.screenPx[p] = spot[p].px;
        L.screenShifted[p] = spot[p].shifted;
        if (p == kUtLayPad) L.padDerived = 0;
    }
    const bool fitted = L.user[kUtLayPad] && !L.padDerived;
    L.pad = fitted ? utPadFitRect(L.rect[kUtLayPad], in.groups, in.gap, in.scale, in.widest, in.nextChars) : in.defPad;
    L.labelInPad = L.user[kUtLayLabel] ? 0 : 1;
    if (!L.user[kUtLayLabel] && fitted) L.rect[kUtLayLabel] = utPadLabelBox(L.pad, in.edit != 0);
    L.label = utLabelFit(L.rect[kUtLayLabel], in.labelPx, in.scale);
    return L;
}

// ---- from the measured page and the ini to the layout in force ---------------------------------

// What the live page reads: the frame and the grid it measured (drawn px), the caravan record's
// size (the cover), and the [layout] values for that cover (null = no key).
struct UtLayoutSource {
    UtRectF frame, grid;      // drawn px
    float scale;
    float canvasW, canvasH;
    int coverW, coverH;       // record: caravan_w x caravan_h (565 x 637)
    const char* pad;          // pad_<W>x<H>, or null
    const char* label;        // label_<W>x<H>, or null
    const char* gridKey = nullptr;   // grid_<W>x<H>, or null (the real Transfer page: always null)
    int maxCellW = 0, maxCellH = 0;  // the largest slot of any group, record px
    int groups;               // N
    int widest;               // the widest group caption, characters
    int nextChars = kUtPadNextChars;   // NEXT's caption, characters (next.caption)
    int labelPx;              // plate_label_size at the scale
    bool search;              // the search field is laid out
    bool edit = false;        // the EDIT button is laid out (edit_button=1)
    // the screen parts: pad_screen / label_screen (null = no key), and the canvas the frame is on
    const char* padScreen = nullptr;
    const char* labelScreen = nullptr;
    int screenKnown = 0;      // 1 = the canvas size and the frame's place are this frame's
};

// Everything the panel keeps of one resolution: the input (the player's rectangles in `user`), the
// layout, today's pad as utPadLayout drew it (`defOut`, `defBand`) and the cover's text.
struct UtLayoutLive {
    UtLayoutInput in;
    UtRect user[kUtLayParts];
    UtLayout L;
    UtPadOut defOut;
    int defBand;
    char cover[16];
};

// The band lines of the derived pad ("pad in the band below the grid", the band's WARN) are said
// only while the derived pad can be in force: never while the pad is pinned on the screen.
inline bool utPadBandLinesWanted(const UtLayout& L) { return !L.screen[kUtLayPad]; }

// Today's pad (utPadLayout with the derived numbers) and the resolution over it. A derived pad that
// does not fit becomes an empty rectangle off the page (no pad, nothing can meet it). A value that
// does not parse counts as no key (the ini reader has already refused it with a WARN).
inline bool utLayoutFromSource(const UtLayoutSource& src, UtLayoutLive* out) {
    if (!out) return false;
    UtLayoutLive& o = *out;
    o = UtLayoutLive{};
    utCoverKeyFormat(src.coverW, src.coverH, o.cover, sizeof(o.cover));
    const float s = src.scale;
    UtPadIn pin;
    pin.frame = src.frame;
    pin.grid = src.grid;
    pin.scale = s;
    pin.canvasW = src.canvasW;
    pin.canvasH = src.canvasH;
    pin.dx = 0;
    pin.dy = kUtPadDefY;
    pin.cellH = kUtPadDefH;
    pin.gap = kUtPadDefGap;
    pin.search = src.search;
    pin.edit = src.edit;
    o.defBand = utPadLayout(pin, &o.defOut);
    if (o.defBand == kUtPadBadInput) return false;
    UtLayoutInput& I = o.in;
    I.plate = UtRect{0, 0, src.coverW, src.coverH};
    I.def[kUtLayGrid] = utRecordRect(src.grid, src.frame, s, true);
    if (o.defBand > 0) {
        I.def[kUtLayPad] = utRecordRect(o.defOut.ground, src.frame, s, false);
        I.def[kUtLayLabel] = utRecordRect(o.defOut.label, src.frame, s, false);
        UtPadFit& d = I.defPad;
        d.ok = 1;
        d.cols = src.groups;
        d.rows = 2;
        d.cellW = o.defOut.cellW;
        d.cellH = o.defOut.cellH;
        d.gap = o.defOut.gap;
        d.x0 = I.def[kUtLayPad].x + kUtPadMargin;
        d.y0 = I.def[kUtLayPad].y + kUtPadMargin;
        d.stepW = o.defOut.cellW;
        d.buttons = src.groups;
        const UtRectF& g0 = o.defOut.btn[kUtPadGroup0];
        d.minW = (int)g0.w;
        d.minH = (int)g0.h;
        d.caption = utPadCaptionSize(g0.w, g0.h, src.widest, s);
        d.pages = UtPadPages{src.groups, src.groups, 1, src.groups + 2};
        d.rect = I.def[kUtLayPad];
        if (src.groups != kUtPadDefCells)   // another count of buttons: row 1 cut again
            d = utPadDerivedFit(o.defOut, I.def[kUtLayPad], src.groups, src.widest, s, src.nextChars);
    } else {
        I.def[kUtLayPad] = UtRect{-30000, -30000, 0, 0};
        I.def[kUtLayLabel] = UtRect{-30000, -30000, 0, 0};
    }
    const char* val[kUtLayParts] = {src.gridKey, src.pad, src.label};
    for (int p = 0; p < kUtLayParts; ++p)
        I.user[p] = (val[p] && utRectParse(val[p], &o.user[p])) ? &o.user[p] : nullptr;
    I.maxCellW = src.maxCellW;
    I.maxCellH = src.maxCellH;
    I.rowGap = kUtGridRowGap;
    const char* scrVal[kUtLayParts] = {nullptr, src.padScreen, src.labelScreen};
    for (int p = kUtLayPad; p < kUtLayParts; ++p)
        I.screenSet[p] = (scrVal[p] && utScreenRectParse(scrVal[p], &I.screenRect[p])) ? 1 : 0;
    I.screenKnown = src.screenKnown && src.canvasW >= 1.0f && src.canvasH >= 1.0f ? 1 : 0;
    I.screenW = (int)src.canvasW;
    I.screenH = (int)src.canvasH;
    I.originX = utPadRound(src.frame.x);
    I.originY = utPadRound(src.frame.y);
    I.groups = src.groups;
    I.gap = kUtPadDefGap;
    I.scale = s;
    I.widest = src.widest;
    I.nextChars = src.nextChars;
    I.edit = src.edit ? 1 : 0;
    I.labelPx = src.labelPx;
    o.L = utLayoutResolve(I);
    return true;
}

// ---- the game's own Transfer page ----------------------------------------------------------------

// THE REAL TRANSFER PAGE (the view OFF): `page` is its resolution (the derived grid in force, the
// game's own sack, never moved), `coll` the collection page's (its grid line). The pad and the
// label in force there become the collection page's, whole and unchanged - the player's
// rectangle, the derived one or the screen part, and every verdict that came with them - so they
// stay where the player put them whatever real cells lie under them. The grid of `page` is not
// touched. `page->in` keeps its own input: the pad and label lines are the same text in both.
inline void utLayoutRealPage(UtLayoutLive* page, const UtLayoutLive& coll) {
    if (!page || page == &coll) return;
    UtLayout& P = page->L;
    const UtLayout& C = coll.L;
    for (int p = kUtLayPad; p < kUtLayParts; ++p) {
        P.rect[p] = C.rect[p];
        P.user[p] = C.user[p];
        P.why[p] = C.why[p];
        P.against[p] = C.against[p];
        P.screen[p] = C.screen[p];
        P.screenWhy[p] = C.screenWhy[p];
        P.screenAgainst[p] = C.screenAgainst[p];
        P.screenPx[p] = C.screenPx[p];
        P.screenShifted[p] = C.screenShifted[p];
        P.ignored[p] = C.ignored[p];
    }
    P.pad = C.pad;
    P.label = C.label;
    P.labelInPad = C.labelInPad;
    P.padDerived = C.padDerived;
}

// The cells of a `cols` x `rows` sack drawn at `grid` (drawn px) that a part drawn at `r` covers:
// the cells it shares at least one pixel with (touching edges do not meet). 0 for an empty part
// or an unusable grid.
inline int utRealCellsCovered(const UtRectF& grid, int cols, int rows, const UtRectF& r) {
    if (cols <= 0 || rows <= 0 || !(grid.w > 0.0f) || !(grid.h > 0.0f) || !(r.w > 0.0f) ||
        !(r.h > 0.0f))
        return 0;
    const float cw = grid.w / (float)cols, ch = grid.h / (float)rows;
    int n = 0;
    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < cols; ++col) {
            const UtRectF c = {grid.x + cw * (float)col, grid.y + ch * (float)row, cw, ch};
            if (utRectsOverlap(c, r)) ++n;
        }
    return n;
}

// The decision as the log says it (GD's materials-page line): "the pad 300,520,240,60
// (pad_565x637) covers 12 of the sack's 240 cell(s)" / "... leaves the sack's 240 cell(s) clear",
// and, while the label is the player's own rectangle, "; the label ... (label_565x637) covers 3 of
// them" / "leaves them clear". `padUnder` / `labelUnder` = utRealCellsCovered of each; `cells` =
// the sack's. Returns the length written, cut (never overrun) at `cap`.
inline int utRealPadText(const UtLayoutLive& lay, int padUnder, int labelUnder, int cells, char* out,
                         size_t cap) {
    if (!out || cap == 0) return 0;
    out[0] = 0;
    const UtLayout& L = lay.L;
    char src[40];
    if (L.screen[kUtLayPad]) _snprintf_s(src, sizeof(src), _TRUNCATE, "pad_screen");
    else if (L.user[kUtLayPad] && !L.padDerived)
        _snprintf_s(src, sizeof(src), _TRUNCATE, "pad_%s", lay.cover);
    else _snprintf_s(src, sizeof(src), _TRUNCATE, "the derived pad");
    const UtRect& r = L.rect[kUtLayPad];
    int n;
    if (padUnder > 0)
        n = _snprintf_s(out, cap, _TRUNCATE, "the pad %d,%d,%d,%d (%s) covers %d of the sack's %d cell(s)",
                        r.x, r.y, r.w, r.h, src, padUnder, cells);
    else
        n = _snprintf_s(out, cap, _TRUNCATE, "the pad %d,%d,%d,%d (%s) leaves the sack's %d cell(s) clear",
                        r.x, r.y, r.w, r.h, src, cells);
    if (n < 0) return (int)strlen(out);
    if (L.user[kUtLayLabel]) {
        const UtRect& b = L.rect[kUtLayLabel];
        char lsrc[40];
        if (L.screen[kUtLayLabel]) _snprintf_s(lsrc, sizeof(lsrc), _TRUNCATE, "label_screen");
        else _snprintf_s(lsrc, sizeof(lsrc), _TRUNCATE, "label_%s", lay.cover);
        const size_t at = strlen(out);
        if (labelUnder > 0)
            _snprintf_s(out + at, cap - at, _TRUNCATE, "; the label %d,%d,%d,%d (%s) covers %d of them",
                        b.x, b.y, b.w, b.h, lsrc, labelUnder);
        else
            _snprintf_s(out + at, cap - at, _TRUNCATE, "; the label %d,%d,%d,%d (%s) leaves them clear",
                        b.x, b.y, b.w, b.h, lsrc);
    }
    return (int)strlen(out);
}

// ---- the log lines of a layout -----------------------------------------------------------------

// The WARN of a refused [layout] rectangle of part `p` (`val` = the player's text, GD's words);
// false and "" when that part is not refused.
inline bool utLayoutRefusalText(const UtLayoutLive& lay, int p, const char* val, float s, char* buf,
                                size_t n) {
    if (!buf || n == 0) return false;
    buf[0] = 0;
    const UtLayout& L = lay.L;
    if (p < kUtLayGrid || p >= kUtLayParts || !lay.in.user[p] || L.why[p] == kUtLayoutOk) return false;
    const int a = L.against[p];
    _snprintf_s(buf, n, _TRUNCATE,
                "layout: %s_%s=%s is refused - it %s%s%s%s at UI scale %.3f; the derived %s is used",
                utLayoutPartName(p), lay.cover, val ? val : "", utLayoutWhyText(L.why[p]),
                a >= 0 ? " (the " : "", a >= 0 ? utLayoutPartName(a) : "", a >= 0 ? ")" : "", s,
                utLayoutPartName(p));
    return true;
}

// The INFO line of the layout in force: the grid, the pad (whose, its cells, the pages while the
// groups are paged) and the label.
inline void utLayoutSaidText(const UtLayoutLive& lay, float s, char* buf, size_t n) {
    if (!buf || n == 0) return;
    const UtLayout& L = lay.L;
    const UtRect& g = L.rect[kUtLayGrid];
    const UtRect& pd = L.rect[kUtLayPad];
    const UtRect& lb = L.rect[kUtLayLabel];
    _snprintf_s(buf, n, _TRUNCATE,
                "layout: %s at UI scale %.3f - grid %d,%d,%d,%d (%s), pad %d,%d,%d,%d (%s) %s%d x %d "
                "cells of %d x %d caption %d, label %d,%d,%d,%d (%s) size %d%s",
                lay.cover, s, g.x, g.y, g.w, g.h, L.user[kUtLayGrid] ? "user" : "derived", pd.x, pd.y,
                pd.w, pd.h,
                L.screen[kUtLayPad] ? "screen" : L.user[kUtLayPad] ? "user" : "derived",
                L.pad.ok ? "" : "NOT DRAWN ", L.pad.cols,
                L.pad.rows, L.pad.cellW, L.pad.cellH, L.pad.caption, lb.x, lb.y, lb.w, lb.h,
                L.screen[kUtLayLabel] ? "screen"
                : L.user[kUtLayLabel] ? "user" : (L.user[kUtLayPad] ? "the pad's" : "derived"),
                L.label.size, L.label.ok ? (L.label.bounded ? " (brought down to fit)" : "")
                                         : " NOT DRAWN");
    if (L.pad.ok && L.pad.pages.pages > 1) {
        const size_t at = strlen(buf);
        _snprintf_s(buf + at, n - at, _TRUNCATE,
                    "; %d buttons do not fit the pad - %d pages of %d with a NEXT button",
                    L.pad.pages.groups, L.pad.pages.pages, L.pad.pages.perPage);
    }
}

// The canonical text of a grid override: "x,y,w,h" re-formatted from the parsed rectangle, so
// blanks or a leading + never change it. Empty when `value` is null or does not parse. Returns the
// length.
inline int utLayoutCanonical(const char* value, char* out, size_t cap) {
    if (!out || cap == 0) return 0;
    out[0] = 0;
    UtRect r;
    if (!value || !utRectParse(value, &r)) return 0;
    return utRectFormat(r, out, cap);
}

}  // namespace ut
