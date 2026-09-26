// ut_gridcheck.h - the owned marks' grid-origin check, PURE.
// No engine, no Windows, no allocation: ut_owned.cpp feeds it every proven hover and logs what it
// answers; tools\test_viewgate.cpp runs this same code over exhaustive hover sequences.
//
// The origin the engine measured (page pos + parent origin, read from the page mouse handler's own
// frame) is the truth; the records only bound it. It is ACCEPTED when
//   (a) check 1: the prototype the engine returned for the point covers the cell the point falls in
//       (its footprint - what GetItemUnderPoint hit-tests), on EVERY hover the check counts;
//   (b) a second proven hover on a DIFFERENT prototype gives the same origin (within kOriginTol)
//       and passes (a);
//   (c) the 16 x 15 grid of the measured cell size lies inside the canvas (always required), and
//       inside the caravan window rect the pad computes from the records - OR, where the records
//       disagree with the engine, the cursor PROOF holds: at least two proven hovers' points matched
//       the OS cursor (canvas px, read at the hover) within kCursorTol on both axes, and the matched
//       points lie at least kSpreadCells cells apart on x or y (two matches near one
//       corner could hide a small scale error that grows across the grid). That is a record-free
//       proof that the engine's UI units are the canvas pixels the marks are drawn in. (In a measured
//       1366 x 768 window the grid ends at x 93 + 512 = 605 and the records' window at
//       10 + 565 = 575.)
// Any failure of (a), of (b)'s origin or of the canvas bound REFUSES (for this geometry). A grid
// outside the window rect without the proof WAITS - for as long as it takes, never refused
// (a fast first sweep must not cost the marks for the whole session); the caller
// logs the wait once at DEBUG and once at INFO after kWaitSay decisive hovers.
// Once decided, the state never changes again until utGridReset (a new canvas size / UI scale).
//
// The grid can be FED. When the caravan frame is known (the
// Transfer page draw's page origin, or the page mouse handler's hover - ut_panel's verdict 1), its
// grid is the origin proof: utGridFeed accepts it at once (inside the canvas and inside that frame,
// the cell the mod sack's), so the marks draw from the first frame the collection is shown - no
// mouse move. The two-hover proof stays as the CROSS-CHECK: every proven hover after a feed runs
// (a) and compares its origin with the fed one; a disagreement REFUSES exactly as (b) does
// (kUtGridRefusedOrigin: the marks go off), and two agreeing hovers on different prototypes
// complete it (kUtGridConfirmed; later hovers are ignored as after any decision). A hover that came
// BEFORE the feed is the cross-check's first hover (its origin must agree with the fed one).
// "agree" with the FED grid means within kUtGridFedTol = ut_panel's kUtFrameTol
// (1.0 px) - the frame ut_panel confirms is the frame the check accepts. The draw route and the
// hover route compute the grid apart (a fractional UI scale splits them by a sub-pixel), so a
// hover 0.5 .. 1.0 px off the fed grid is the SAME frame: the grid ADOPTS the hover's origin (the
// precise one - what GetItemUnderPoint hit-tests; kUtGridCrossAdopted) and the cross-check goes on
// from it. Beyond kUtGridFedTol it refuses (kUtGridRefusedOrigin). The hovers among themselves keep
// kUtGridOriginTol, exactly as (b).
#pragma once

namespace ut {

const float kUtGridOriginTol = 0.5f;   // the handler adds the same two floats every time
const float kUtGridFedTol = 1.0f;      // a hover vs the FED grid = kUtFrameTol
const float kUtGridCursorTol = 4.0f;   // the cursor is read at the hover (panelCursorNow)
const int kUtGridSpreadCells = 8;      // the two matched points: this many cells apart on x or y
const int kUtGridWaitSay = 32;         // decisive hovers without the proof before the INFO line
const int kUtGridCols = 16, kUtGridRows = 15;

enum UtGridResult {
    kUtGridIgnored = 0,       // decided already, or the hover carries no item
    kUtGridFirst,             // the first counted hover: its origin is the candidate
    kUtGridSamePrototype,     // (b) wants a different prototype: nothing changes but the counters
    kUtGridWaitCursor,        // (c) outside the records' window rect; waiting for the cursor proof
    kUtGridAccepted,          // decided: the grid is at the candidate's origin
    kUtGridRefusedCell,       // (a) failed
    kUtGridRefusedOrigin,     // (b) two proven hovers disagree
    kUtGridRefusedCanvas,     // (c) the grid leaves the canvas
    // the fed grid
    kUtGridFed,               // accepted from the fed origin (the measured caravan frame)
    kUtGridFedNotUsable,      // the fed grid leaves the canvas or the frame, or the cell is absurd:
                              // not fed (the hovers decide as before) - never a refusal
    kUtGridCrossAgree,        // a proven hover agrees with the fed grid (the first of two)
    kUtGridConfirmed,         // a second prototype agrees too: the cross-check is complete
    kUtGridCrossAdopted       // the first cross-check hover is 0.5 .. 1.0 px off
                              // the fed grid: the grid adopts its origin (the first of two)
};

struct UtGridHover {
    float gridX, gridY;       // the origin the handler used
    float x, y;               // the point in sack px (what GetItemUnderPoint got)
    unsigned id;              // the prototype the engine returned (0 = none)
    int col, row, w, h;       // that prototype's footprint, cells
    unsigned cw, ch;          // the mod sack's cell size, px
    bool cursorRead;          // the OS cursor was read at this hover
    float cursorX, cursorY;   // ... in canvas px
};

struct UtGridBounds {
    float canvasW, canvasH;
    float winX, winY, winW, winH;   // the caravan window rect (records x UI scale), canvas px
};

struct UtGridState {
    int verdict;              // 0 undecided, 1 accepted, -1 refused
    bool candSet;
    float candX, candY;       // the first counted hover's origin
    unsigned candId;
    int candCol, candRow;     // its cell
    int cursorAgree, cursorRead;
    float cursorLast;         // the last hover's |point - cursor| in px (-1 = never read)
    float agreeMinX, agreeMaxX, agreeMinY, agreeMaxY;   // the matched points, sack px
    int waits;                // decisive hovers that found no proof (outside the window)
    bool windowOk;            // at acceptance: inside the records' window rect
    float gridX, gridY;       // accepted origin
    unsigned cw, ch;          // accepted cell size
    bool fed;                 // accepted from the fed origin (the hovers cross-check it)
    bool confirmed;           // ... and two proven hovers on different prototypes agreed
    int fedAgree;             // proven hovers that agreed with the fed grid
    unsigned fedFirstId;      // the first of them
    int fedFirstCol, fedFirstRow;
    float fedX, fedY;         // the origin as fed (gridX/Y may adopt a hover's)
    float firstX, firstY;     // the first agreeing hover's origin (the hovers keep kOriginTol)
    bool adopted;             // gridX/Y is that hover's origin, not the fed one
};

inline void utGridReset(UtGridState* s) {
    s->verdict = 0;
    s->candSet = false;
    s->candX = s->candY = 0.0f;
    s->candId = 0;
    s->candCol = s->candRow = 0;
    s->cursorAgree = s->cursorRead = 0;
    s->cursorLast = -1.0f;
    s->agreeMinX = s->agreeMaxX = s->agreeMinY = s->agreeMaxY = 0.0f;
    s->waits = 0;
    s->windowOk = false;
    s->gridX = s->gridY = 0.0f;
    s->cw = s->ch = 0;
    s->fed = false;
    s->confirmed = false;
    s->fedAgree = 0;
    s->fedFirstId = 0;
    s->fedFirstCol = s->fedFirstRow = 0;
    s->fedX = s->fedY = 0.0f;
    s->firstX = s->firstY = 0.0f;
    s->adopted = false;
}

inline float utGridAbs(float v) { return v < 0.0f ? -v : v; }

// within kUtGridFedTol of the fed origin on both axes (a NaN: no).
inline bool utGridNearFed(const UtGridState* s, float gx, float gy) {
    return utGridAbs(gx - s->fedX) <= kUtGridFedTol && utGridAbs(gy - s->fedY) <= kUtGridFedTol;
}

// Floor division of a sack coordinate by the cell size (a negative point is outside anyway).
inline int utGridCell(float v, unsigned cell) {
    if (!cell || !(v >= 0.0f)) return -1;
    return (int)(v / (float)cell);
}

// Check 1 alone: the point's cell lies inside the prototype's footprint.
inline bool utGridCellOk(const UtGridHover& h) {
    if (!h.cw || !h.ch || h.w < 1 || h.h < 1) return false;
    const int cx = utGridCell(h.x, h.cw), cy = utGridCell(h.y, h.ch);
    return cx >= 0 && cy >= 0 && cx >= h.col && cx < h.col + h.w && cy >= h.row &&
           cy < h.row + h.h;
}

inline bool utGridInside(float gx, float gy, float gw, float gh, float x, float y, float w,
                         float h) {
    const float t = kUtGridOriginTol;
    return gx >= x - t && gy >= y - t && gx + gw <= x + w + t && gy + gh <= y + h + t;
}

// The cursor proof of (c): two matches, kSpreadCells cells apart on x or y.
inline bool utGridCursorProof(const UtGridState* s, unsigned cw, unsigned ch) {
    return s->cursorAgree >= 2 &&
           (s->agreeMaxX - s->agreeMinX >= (float)(kUtGridSpreadCells * (int)cw) ||
            s->agreeMaxY - s->agreeMinY >= (float)(kUtGridSpreadCells * (int)ch));
}

// feed the grid of the measured caravan frame (canvas px) with the mod sack's cell size.
// b.win* is that frame. Only while undecided; a candidate hover that disagrees refuses (b).
inline int utGridFeed(UtGridState* s, float gx, float gy, unsigned cw, unsigned ch,
                      const UtGridBounds& b) {
    if (s->verdict != 0) return kUtGridIgnored;
    if (cw < 8u || cw > 256u || ch < 8u || ch > 256u) return kUtGridFedNotUsable;
    const float gw = (float)(kUtGridCols * (int)cw), gh = (float)(kUtGridRows * (int)ch);
    if (!(gx == gx) || !(gy == gy) ||   // NaN
        !utGridInside(gx, gy, gw, gh, 0.0f, 0.0f, b.canvasW, b.canvasH) ||
        !utGridInside(gx, gy, gw, gh, b.winX, b.winY, b.winW, b.winH))
        return kUtGridFedNotUsable;
    s->fedX = gx;
    s->fedY = gy;
    if (s->candSet && !utGridNearFed(s, s->candX, s->candY)) {
        s->verdict = -1;   // the hover that came first disagrees with the fed grid (> kFedTol)
        return kUtGridRefusedOrigin;
    }
    s->verdict = 1;
    s->fed = true;
    s->confirmed = false;
    s->windowOk = true;
    s->gridX = gx;
    s->gridY = gy;
    s->cw = cw;
    s->ch = ch;
    s->fedAgree = 0;
    s->adopted = false;
    if (s->candSet) {   // the hover before the feed agreed: the cross-check's first hover
        s->fedAgree = 1;
        s->fedFirstId = s->candId;
        s->fedFirstCol = s->candCol;
        s->fedFirstRow = s->candRow;
        s->firstX = s->candX;
        s->firstY = s->candY;
        if (!(utGridAbs(s->candX - gx) <= kUtGridOriginTol) ||
            !(utGridAbs(s->candY - gy) <= kUtGridOriginTol)) {   // off by 0.5 .. 1.0 px - adopt it
            s->gridX = s->candX;
            s->gridY = s->candY;
            s->adopted = true;
        }
    }
    return kUtGridFed;
}

// a proven hover against the FED grid (the cross-check). Refusals as (a) / (b).
inline int utGridCrossCheck(UtGridState* s, const UtGridHover& h) {
    if (!utGridCellOk(h)) {
        s->verdict = -1;
        return kUtGridRefusedCell;
    }
    // the fed grid within kUtGridFedTol (ut_panel's frame tolerance); a second hover must also
    // agree with the first within kUtGridOriginTol, as (b)
    if (!utGridNearFed(s, h.gridX, h.gridY) || h.cw != s->cw || h.ch != s->ch ||
        (s->fedAgree > 0 && (!(utGridAbs(h.gridX - s->firstX) <= kUtGridOriginTol) ||
                             !(utGridAbs(h.gridY - s->firstY) <= kUtGridOriginTol)))) {
        s->verdict = -1;
        return kUtGridRefusedOrigin;
    }
    if (s->fedAgree == 0) {
        s->fedAgree = 1;
        s->fedFirstId = h.id;
        s->fedFirstCol = utGridCell(h.x, h.cw);
        s->fedFirstRow = utGridCell(h.y, h.ch);
        s->firstX = h.gridX;
        s->firstY = h.gridY;
        if (!(utGridAbs(h.gridX - s->gridX) <= kUtGridOriginTol) ||
            !(utGridAbs(h.gridY - s->gridY) <= kUtGridOriginTol)) {   // 0.5 .. 1.0 px: adopt it
            s->gridX = h.gridX;
            s->gridY = h.gridY;
            s->adopted = true;
            return kUtGridCrossAdopted;
        }
        return kUtGridCrossAgree;
    }
    if (h.id == s->fedFirstId) return kUtGridSamePrototype;
    ++s->fedAgree;
    s->confirmed = true;
    return kUtGridConfirmed;
}

// One proven hover. Returns what it did (UtGridResult); s->verdict carries the decision.
inline int utGridStep(UtGridState* s, const UtGridHover& h, const UtGridBounds& b) {
    if (s->verdict == 1 && s->fed && !s->confirmed && h.id) return utGridCrossCheck(s, h);
    if (s->verdict != 0 || !h.id) return kUtGridIgnored;
    if (!utGridCellOk(h)) {   // (a), on every counted hover
        s->verdict = -1;
        return kUtGridRefusedCell;
    }
    if (h.cursorRead) {
        const float dx = utGridAbs(h.gridX + h.x - h.cursorX);
        const float dy = utGridAbs(h.gridY + h.y - h.cursorY);
        s->cursorLast = dx > dy ? dx : dy;
        ++s->cursorRead;
        if (dx <= kUtGridCursorTol && dy <= kUtGridCursorTol) {   // NaN: no
            if (s->cursorAgree == 0) {
                s->agreeMinX = s->agreeMaxX = h.x;
                s->agreeMinY = s->agreeMaxY = h.y;
            } else {
                if (h.x < s->agreeMinX) s->agreeMinX = h.x;
                if (h.x > s->agreeMaxX) s->agreeMaxX = h.x;
                if (h.y < s->agreeMinY) s->agreeMinY = h.y;
                if (h.y > s->agreeMaxY) s->agreeMaxY = h.y;
            }
            ++s->cursorAgree;
        }
    }
    if (!s->candSet) {
        s->candSet = true;
        s->candX = h.gridX;
        s->candY = h.gridY;
        s->candId = h.id;
        s->candCol = utGridCell(h.x, h.cw);
        s->candRow = utGridCell(h.y, h.ch);
        return kUtGridFirst;
    }
    if (!(utGridAbs(h.gridX - s->candX) <= kUtGridOriginTol) ||
        !(utGridAbs(h.gridY - s->candY) <= kUtGridOriginTol)) {   // (b); a NaN refuses too
        s->verdict = -1;
        return kUtGridRefusedOrigin;
    }
    if (h.id == s->candId) return kUtGridSamePrototype;
    const float gw = (float)(kUtGridCols * (int)h.cw), gh = (float)(kUtGridRows * (int)h.ch);
    if (!utGridInside(s->candX, s->candY, gw, gh, 0.0f, 0.0f, b.canvasW, b.canvasH)) {   // (c)
        s->verdict = -1;
        return kUtGridRefusedCanvas;
    }
    const bool windowOk = utGridInside(s->candX, s->candY, gw, gh, b.winX, b.winY, b.winW, b.winH);
    if (!windowOk && !utGridCursorProof(s, h.cw, h.ch)) {
        ++s->waits;   // never a refusal - no marks until the proof comes
        return kUtGridWaitCursor;
    }
    s->verdict = 1;
    s->windowOk = windowOk;
    s->gridX = s->candX;
    s->gridY = s->candY;
    s->cw = h.cw;
    s->ch = h.ch;
    return kUtGridAccepted;
}

}  // namespace ut
