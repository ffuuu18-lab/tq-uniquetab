// ut_layoutedit.h - the in-game layout editor, its pure half: where the handles and the toolbar
// are, what a drag does to a rectangle (kept inside the window or the screen, the grid snapped to
// whole cells), whether the edited layout may be saved, and the state machine that opens, drags,
// saves and cancels. No keyboard: everything is a mouse event, the polled cursor or the clock.
//
// Pure arithmetic over ut_layout.h: no Windows, no engine, no allocation, no globals. ut_panel.cpp
// feeds it the window's mouse messages, the polled cursor and button state and the clock, and
// draws what it says; tools\test_layoutedit.cpp proves every transition and clamp.
//
// Units: the rectangles are RECORD px, frame-local (ut_layout.h); the points are SCREEN px,
// frame-local (the canvas point minus the frame's rounded top-left), so a handle is at least
// kUtEdHandleMinPx on screen at every UI scale.
//
//   utEditOpenVerdict   whether the editor may open now (an item on the cursor: never)
//   utEditOpen          the layout in force -> an open editor (or false: no usable scale/pad)
//   utEditHitTest       a point -> a toolbar button, a handle (its edges) or a rectangle's body
//   utEditLeftDown      LEFT DOWN: start a drag or press a button; kUtEdActPass = not consumed
//   utEditToolbarAt     the toolbar button under a point in ANY state but closed (CANCEL's)
//   utEditDragTo        the polled cursor while a drag runs: the rectangle follows, clamped
//   utEditLeftUp        LEFT UP: the drag ends where the event says
//   utEditTick          once a frame: the cancel rules, the polled release, the save handover
//   utEditCheck         the edited rectangles through utLayoutResolve: SAVE only when all hold
//   utEditKeyValue      the text SAVE writes for one part (nothing = the line is removed)
//   utEditOneShot       the layout_edit=1 key as a rising edge
//
// THE REFUSAL RULE. A part may be dragged anywhere - over the grid and over another part too;
// only where it ENDS is it judged (utEditCheck, the resolver the live page runs): a part that
// would not be in force there is drawn red and SAVE is off until it is moved or resized.
//
// THE GRID. The grid has a handle and a body like the other parts (its size snapped to whole
// cells) when the grid can be set (`gridMovable`: the panel's grid interface says so); while it
// cannot, it is outlined only, and a point on it that is on no other part is the page's.
//
// THE GAME'S OWN TRANSFER PAGE (group kUtEdTransferPage): its grid is fixed (never set there) and
// the pad and the label may be placed over its cells - the live page draws them there, whole
// (ut_layout.h utLayoutRealPage). They are judged against the collection's grid instead (the
// `base` utEditCheck is given carries its line): the grid they must stay clear of on the
// collection page, where they are the same rectangles.
//
// THE SCREEN MODE (pad_screen / label_screen, ut_layout.h "the screen-anchored pad and label").
// The pad and the label each have a WINDOW mode (kept inside the caravan window, saved as the
// per-size pad_/label_<W>x<H> line of the settings file in force) and a SCREEN mode (kept inside
// the screen, saved as pad_screen / label_screen = the nearest corner, in that same file). The
// grid is always in window mode. Once the screen is known (utEditSetScreen: the canvas size and
// the frame's origin), a MOVE that carries the pad or the label across the window's edge puts it
// in screen mode - it keeps following the cursor, clamped to the screen - and a move or a resize
// that brings it wholly back inside the window puts it in window mode again; a resize keeps the
// mode otherwise (clamped to the window or to the screen). The rectangles stay frame-local record
// px in both modes (a screen part's lies off the window, negative too), so the hit test, the drag
// and the draw are the same code. The toolbar always sits at the top right of the PAD's rectangle
// and travels with it. RESET puts every part back in the window at its derived rectangle; SAVE
// then removes the screen keys - and the grid's line, also on the game's own Transfer page, where
// the grid is fixed (GD's RESET brings all three back: `gridReset`). utEditSavePlanOf says which key each part writes;
// utEditScreenGrounds says where a screen part takes the clicks outside the window (published for
// the screen input, ut_padinput.h), utEditWindowClient where the window's own clicks are.
//
// Titan Quest: the label is the pad's own row-2 box until it is dragged (`labelInPad`): while it
// is, it follows the pad (in the window or on the screen) and SAVE writes no label line, exactly
// as the live page resolves a file without one. A label dragged back into the pad over the pad's
// label box is the pad's own box again. Every click of the editor reaches it through the game
// window's own mouse messages, inside the caravan window and outside it alike.
#pragma once

#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "ut_layout.h"

namespace ut {

// The edges a drag moves; all four = the rectangle moves.
enum { kUtEdgeL = 1, kUtEdgeR = 2, kUtEdgeT = 4, kUtEdgeB = 8, kUtEdgeMove = 15 };

// The toolbar's buttons, left to right.
enum { kUtEdSave = 0, kUtEdCancel = 1, kUtEdReset = 2, kUtEdButtons = 3 };

enum UtEdState { kUtEdClosed = 0, kUtEdOpen, kUtEdDrag, kUtEdSaving };

// The page the pad is shown on (UtEdit::group): a collection group (>= 0), the real Transfer page,
// or none (the pad is not shown).
const int kUtEdNoPage = -1;
const int kUtEdTransferPage = -2;

// Why the editor closed (UtEdit::why).
enum UtEdWhy {
    kUtEdWhyNone = 0,
    kUtEdWhyCancel,     // CANCEL
    kUtEdWhyGone,       // the page was not drawn: caravan closed, another page, world teardown
    kUtEdWhyGroup,      // the pad is not shown any more
    kUtEdWhyFocus,      // the game lost the focus
    kUtEdWhyScale,      // the UI scale changed
    kUtEdWhyCover,      // another caravan window size
    kUtEdWhyLayout,     // no layout in force any more
    kUtEdWhySaved,      // SAVE, and the layout in force now is the edited one
    kUtEdWhySaveSlow,   // SAVE, and the layout in force did not follow in time
    kUtEdWhyScreen,     // the screen size changed (a resolution change)
    kUtEdWhyKind        // the page changed between a unique page and a relic page
};

inline const char* utEditWhyText(int why) {
    switch (why) {
        case kUtEdWhyCancel: return "CANCEL - nothing was written";
        case kUtEdWhyGone: return "the page left the screen (caravan closed, another caravan "
                                  "page or a world change) - nothing was written";
        case kUtEdWhyGroup: return "the pad is not shown any more - nothing was written";
        case kUtEdWhyFocus: return "the game lost the focus - nothing was written";
        case kUtEdWhyScale: return "the UI scale changed - nothing was written";
        case kUtEdWhyCover: return "the caravan window size changed - nothing was written";
        case kUtEdWhyLayout: return "the layout could not be resolved - nothing was written";
        case kUtEdWhySaved: return "saved; the page follows the new layout";
        case kUtEdWhySaveSlow: return "saved; the settings file has not been read back yet";
        case kUtEdWhyScreen: return "the screen size changed - nothing was written";
        case kUtEdWhyKind: return "the page changed between the item grid and the relic area - nothing was written";
        default: return "?";
    }
}

// What a LEFT DOWN did.
enum UtEdAct {
    kUtEdActPass = 0,     // not the editor's: the engine gets the event
    kUtEdActDrag,         // a handle or a rectangle: the drag started (consumed)
    kUtEdActSave,         // SAVE pressed and allowed: the caller writes, then utEditSaved
    kUtEdActSaveRefused,  // SAVE pressed while a rectangle is refused (consumed, nothing done)
    kUtEdActCancel,       // CANCEL: closed (consumed)
    kUtEdActReset         // RESET: the derived rectangles are back, not saved yet (consumed)
};

enum { kUtEdHitNone = 0, kUtEdHitButton, kUtEdHitHandle, kUtEdHitBody };

struct UtEdHit {
    int kind;     // kUtEdHit*
    int part;     // kUtLay* for a handle or a body, else -1
    int edges;    // kUtEdge* the drag moves
    int button;   // kUtEd* for a toolbar button, else -1
};

const int kUtEdHandleMinPx = 8;          // screen px: the smallest handle and edge strip
const unsigned kUtEdGoneMs = 400;        // not drawn for this long = the page left the screen
const unsigned kUtEdSaveWaitMs = 3000;   // SAVE waits this long for the settings read-back
const int kUtEdButtonW = 52;             // record px, the toolbar's buttons
const int kUtEdButtonH = 14;
const int kUtEdButtonGap = 3;
// The pad's floor: row 2's 15 cells of 16 record px with their gaps, two rows of 12, the margins.
const int kUtEdMinPadW = 2 * kUtPadMargin + kUtPadRowCells * kUtPadRow2CellMin +
                         (kUtPadRowCells - 1) * kUtPadDefGap;
const int kUtEdMinPadH = 2 * kUtPadMargin + 2 * kUtPadMinCellH + kUtPadDefGap;
const int kUtEdMinLabelW = 24;

// Whether the editor may open now (the EDIT button, Ctrl+right-click on the pad or the label, or layout_edit=1):
// wherever the pad is shown - a collection group or the real Transfer page.
enum UtEdOpenVerdict {
    kUtEdOpenYes = 0,
    kUtEdOpenAlready,   // it is open
    kUtEdOpenHeld,      // an item is on the cursor: a click of the editor could drop it
    kUtEdOpenNoPad      // no layout in force (no pad drawn)
};

inline int utEditOpenVerdict(int state, int cursorEmpty, int layoutOk) {
    if (state != kUtEdClosed) return kUtEdOpenAlready;
    if (!cursorEmpty) return kUtEdOpenHeld;
    if (!layoutOk) return kUtEdOpenNoPad;
    return kUtEdOpenYes;
}

inline const char* utEditOpenVerdictText(int v) {
    switch (v) {
        case kUtEdOpenYes: return "opens";
        case kUtEdOpenAlready: return "it is open already";
        case kUtEdOpenHeld: return "an item is on the cursor - put it down first, the editor's "
                                   "clicks must never drop it";
        case kUtEdOpenNoPad: return "the pad is not drawn here";
        default: return "?";
    }
}

struct UtEdit {
    int state;                    // UtEdState
    int why;                      // UtEdWhy: why it last closed
    UtRect rect[kUtLayParts];     // the rectangles being edited
    UtRect def[kUtLayParts];      // the derived ones (RESET; a part equal to it is not stored)
    // the derived ones with every part back in the window: the grid keeps the pad's band below it
    // (the derived area takes all its rows only while the pad and the label are on the screen).
    // RESET brings these back and makes them the derived ones (one consistent set: nothing red)
    UtRect defWin[kUtLayParts];
    int resetWin;                 // 1 = a RESET took defWin, and it differs from the open's def
    UtRect orig[kUtLayParts];     // the ones in force when the editor opened
    UtRect plate;                 // the caravan window: the grid and a window-mode part stay inside
    UtRect tool[kUtEdButtons];    // the toolbar, record px
    int minW[kUtLayParts], minH[kUtLayParts];
    int snapW, snapH;             // the grid's step: the cell, its height + row gap
    float scale;                  // the UI scale at open; another one cancels
    int coverW, coverH;           // the window size at open; another one cancels
    int group;                    // the page shown (kUtEdTransferPage or a collection group)
    unsigned lastTick;            // the clock of the last utEditTick (or the open)
    unsigned savedAt;
    // the drag
    int part, edges;
    float x0, y0;                 // where it started, screen px frame-local
    UtRect start;                 // the rectangle when it started
    int button;                   // the PHYSICAL button it polls: 1 left, 2 right, 0 none held
    // utEditCheck
    int ok;                       // 1 = every rectangle holds: SAVE is allowed
    int partWhy[kUtLayParts];     // UtLayoutWhy per part (a screen part: its screen key's)
    int against[kUtLayParts];
    // the screen mode
    int scr[kUtLayParts];         // 1 = the part is in screen mode (never the grid)
    int scrOrig[kUtLayParts];     // the modes at the open
    int screenKnown;              // 1 = screenW/H and originX/Y are known
    int screenW, screenH;         // the canvas, px
    float originX, originY;       // the frame's rounded top-left on it
    UtRect screen;                // the screen as frame-local record px (0 x 0 = unknown)
    // the grid: 1 = it can be set (it has a handle and a body), 0 = outlined only
    int gridMovable;
    // 1 = the grid part is the relic pages' area (a relic page is shown): SAVE writes
    // relics_<W>x<H>, and its derived rectangle is the collection's grid in force (no line)
    int relicArea;
    // 1 = RESET while the grid is fixed (the game's own Transfer page): the collection's grid is
    // the derived one again - judged so, and SAVE removes its grid_<W>x<H> line
    int gridReset;
    // the pad's fit (the label box follows a moved pad) and the label's mode
    int groups, gap, widest;
    int nextChars;                // NEXT's caption, characters (0 = the default's)
    int edit;                     // 1 = the EDIT button in row 2 (the pad's label box one cell later)
    int labelInPad;               // 1 = the label is the pad's own row-2 box, it follows the pad
    int labelInPadOrig;
};

// The side of a handle in screen px: 6 record px, never under kUtEdHandleMinPx.
inline int utEditHandlePx(float s) {
    const int v = (int)(6.0f * s + 0.5f);
    return v < kUtEdHandleMinPx ? kUtEdHandleMinPx : v;
}

// The toolbar: three buttons right-aligned at the top of `band` (the pad's rectangle).
inline void utEditToolbar(const UtRect& band, UtRect out[kUtEdButtons]) {
    const int w = kUtEdButtons * kUtEdButtonW + (kUtEdButtons - 1) * kUtEdButtonGap;
    int x = band.x + band.w - kUtPadMargin - w;
    if (x < band.x) x = band.x;
    for (int b = 0; b < kUtEdButtons; ++b) {
        out[b].x = x + b * (kUtEdButtonW + kUtEdButtonGap);
        out[b].y = band.y;
        out[b].w = kUtEdButtonW;
        out[b].h = kUtEdButtonH;
    }
}

// The smallest rectangle each part may be dragged to, record px: the grid holds the largest
// cell, the pad its row-2 floor, the label a line of TQ's smallest label size.
inline void utEditMinSizes(int maxCellW, int maxCellH, int rowGap, float s,
                           int minW[kUtLayParts], int minH[kUtLayParts]) {
    minW[kUtLayGrid] = maxCellW < 1 ? 1 : maxCellW;
    minH[kUtLayGrid] = (maxCellH < 1 ? 1 : maxCellH) + (rowGap < 0 ? 0 : rowGap);
    minW[kUtLayPad] = kUtEdMinPadW;
    minH[kUtLayPad] = kUtEdMinPadH;
    const float sc = (s >= 0.2f && s <= 8.0f) ? s : 1.0f;
    int lw = (int)ceilf(8.0f / sc);
    if (lw < kUtEdMinLabelW) lw = kUtEdMinLabelW;
    minW[kUtLayLabel] = lw;
    minH[kUtLayLabel] = 1 + (int)ceilf((float)kUtLabelSizeMin / sc);
}

// A length after a drag: whole `unit`s when unit > 1 (rounded to the nearest), never under
// `minLen` (rounded up to whole units), never over `room` (rounded down). When the room cannot
// hold the minimum the room wins, and utEditCheck then refuses the rectangle.
inline int utEditSnap(int len, int unit, int minLen, int room) {
    if (room < 1) room = 1;
    if (unit <= 1) {
        if (len < minLen) len = minLen;
        if (len > room) len = room;
        return len < 1 ? 1 : len;
    }
    int v = len <= 0 ? 0 : ((len + unit / 2) / unit) * unit;
    const int lo = ((minLen + unit - 1) / unit) * unit;
    const int hi = (room / unit) * unit;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    if (v < 1) v = room < unit ? room : unit;
    return v;
}

// `start` dragged by dx, dy record px on `edges`. A move keeps the size and stays inside the
// plate; a resize moves only the dragged edges, keeps the opposite ones where they were, and
// snaps the length to whole snapW x snapH steps (1 = every pixel).
inline UtRect utEditDragRect(const UtRect& start, int edges, int dx, int dy, const UtRect& plate,
                             int minW, int minH, int snapW, int snapH) {
    UtRect r = start;
    const int px1 = plate.x + plate.w, py1 = plate.y + plate.h;
    if (edges == kUtEdgeMove) {
        r.x = start.x + dx;
        r.y = start.y + dy;
        if (r.x > px1 - r.w) r.x = px1 - r.w;
        if (r.x < plate.x) r.x = plate.x;
        if (r.y > py1 - r.h) r.y = py1 - r.h;
        if (r.y < plate.y) r.y = plate.y;
        return r;
    }
    if (edges & kUtEdgeL) {
        const int right = start.x + start.w;
        const int len = utEditSnap(start.w - dx, snapW, minW, right - plate.x);
        r.x = right - len;
        r.w = len;
    } else if (edges & kUtEdgeR) {
        const int left = start.x < plate.x ? plate.x : start.x;
        r.x = left;
        r.w = utEditSnap(start.x + start.w + dx - left, snapW, minW, px1 - left);
    }
    if (edges & kUtEdgeT) {
        const int bottom = start.y + start.h;
        const int len = utEditSnap(start.h - dy, snapH, minH, bottom - plate.y);
        r.y = bottom - len;
        r.h = len;
    } else if (edges & kUtEdgeB) {
        const int top = start.y < plate.y ? plate.y : start.y;
        r.y = top;
        r.h = utEditSnap(start.y + start.h + dy - top, snapH, minH, py1 - top);
    }
    return r;
}

// The physical button a drag polls (GetAsyncKeyState reads PHYSICAL buttons): the logical left
// one - the right one when the buttons are swapped - if it is down at the start, else the other
// one if that is down (an engine reading the physical left), else none (the click is over).
inline int utEditDragButton(int swapped, int physLeftDown, int physRightDown) {
    const int want = swapped ? 2 : 1;
    const int other = swapped ? 1 : 2;
    const int wantDown = want == 1 ? physLeftDown : physRightDown;
    const int otherDown = other == 1 ? physLeftDown : physRightDown;
    return wantDown ? want : otherDown ? other : 0;
}

// The one-shot key: true once per 0 -> 1 edge. The caller sets *latch = 1 when it consumed the
// key; the latch clears only when the key reads 0 again, so the second or so before the
// settings file is read back can never open the editor twice.
inline bool utEditOneShot(int value, int* latch) {
    if (!value) {
        *latch = 0;
        return false;
    }
    return *latch == 0;
}

struct UtEditOpenIn {
    UtRect inForce[kUtLayParts];  // the rectangles in force (a pad with no size: none drawn)
    UtRect def[kUtLayParts];      // the derived ones
    UtRect defWin[kUtLayParts];   // the derived ones with every part in the window (UtEdit::defWin)
    int defWinOk;                 // 0 = defWin not given: the derived ones serve
    UtRect plate;
    int maxCellW, maxCellH, rowGap;
    int cellW, cellH;             // the grid's cell
    int gridMovable;              // 1 = the grid can be set (the panel's grid interface)
    int relicArea;                // 1 = a relic page: the grid part is the relic area (relics_)
    int labelInPad;               // 1 = the label in force is the pad's own box (UtLayout::labelInPad)
    int groups, gap, widest;      // the pad's fit (UtLayoutInput)
    int nextChars;                // NEXT's caption, characters (0 = the default's)
    int edit;                     // 1 = the EDIT button in row 2 (UtLayoutInput::edit)
    float scale;
    int coverW, coverH;
    int group;                    // the page shown; kUtEdNoPage = the editor does not open
    unsigned now;
    // the screen (zero = not known: the pad and the label cannot leave the window)
    int screenIn[kUtLayParts];    // 1 = the part in force is its screen key (UtLayout::screen)
    int screenKnown;
    int screenW, screenH;         // canvas px
    float originX, originY;       // the frame's rounded top-left, canvas px
};

inline bool utEditUsable(const UtRect& r) { return r.w > 0 && r.h > 0; }

// The grid's cell, as the grid's snapping step.
inline void utEditSetCell(UtEdit* e, int cellW, int cellH, int rowGap) {
    e->snapW = cellW > 0 ? cellW : 1;
    e->snapH = (cellH > 0 ? cellH : 1) + (rowGap > 0 ? rowGap : 0);
}

// ---- the screen mode ------------------------------------------------------------------------

// The screenW x screenH canvas as FRAME-LOCAL record px at scale s (origin = the frame's place
// on it): the whole record px whose drawn edges stay on the canvas. 0 x 0 when unknown.
inline UtRect utEditScreenBounds(int screenW, int screenH, float originX, float originY, float s) {
    UtRect r = {0, 0, 0, 0};
    if (screenW < 1 || screenH < 1 || !(s >= 0.2f && s <= 8.0f)) return r;
    const int x0 = (int)ceilf(-originX / s), y0 = (int)ceilf(-originY / s);
    const int x1 = (int)floorf(((float)screenW - originX) / s);
    const int y1 = (int)floorf(((float)screenH - originY) / s);
    if (x1 <= x0 || y1 <= y0) return r;
    r.x = x0;
    r.y = y0;
    r.w = x1 - x0;
    r.h = y1 - y0;
    return r;
}

// True when `part` may leave the window: the pad or the label, with the screen known.
inline bool utEditCanLeave(const UtEdit& e, int part) {
    return (part == kUtLayPad || part == kUtLayLabel) && e.screenKnown && utEditUsable(e.screen);
}

// What a part is kept inside: the screen in screen mode (no bound while the screen is unknown),
// the window in window mode (the grid always).
inline UtRect utEditBounds(const UtEdit& e, int part) {
    if (part != kUtLayGrid && part >= 0 && part < kUtLayParts && e.scr[part]) {
        if (utEditUsable(e.screen)) return e.screen;
        const UtRect any = {-kUtScreenMax, -kUtScreenMax, 2 * kUtScreenMax, 2 * kUtScreenMax};
        return any;
    }
    return e.plate;
}

// The toolbar at the top right of the PAD's rectangle (utEditToolbar), shifted (never resized)
// back inside the pad's bounds when a narrow pad near an edge would push it out.
inline void utEditPlaceToolbar(UtEdit* e) {
    utEditToolbar(e->rect[kUtLayPad], e->tool);
    const UtRect b = utEditBounds(*e, kUtLayPad);
    const int x0 = e->tool[0].x, x1 = e->tool[kUtEdButtons - 1].x + kUtEdButtonW;
    int dx = 0;
    if (x1 > b.x + b.w) dx = b.x + b.w - x1;
    if (x0 + dx < b.x) dx = b.x - x0;
    int dy = 0;
    if (e->tool[0].y + kUtEdButtonH > b.y + b.h) dy = b.y + b.h - kUtEdButtonH - e->tool[0].y;
    if (e->tool[0].y + dy < b.y) dy = b.y - e->tool[0].y;
    for (int k = 0; k < kUtEdButtons; ++k) {
        e->tool[k].x += dx;
        e->tool[k].y += dy;
    }
}

// The screen size and the frame's origin (once a frame; the open's too). Unknown (`known` 0 or
// an unusable size) = the pad and the label cannot leave the window; a part already on the screen
// stays there. A screen part's bounds follow the origin, and so does the toolbar.
inline void utEditSetScreen(UtEdit* e, int known, int screenW, int screenH, float originX,
                            float originY) {
    const UtRect b = known ? utEditScreenBounds(screenW, screenH, originX, originY, e->scale)
                           : UtRect{0, 0, 0, 0};
    e->screenKnown = utEditUsable(b) ? 1 : 0;
    e->screenW = e->screenKnown ? screenW : 0;
    e->screenH = e->screenKnown ? screenH : 0;
    e->originX = e->screenKnown ? originX : 0.0f;
    e->originY = e->screenKnown ? originY : 0.0f;
    e->screen = b;
    utEditPlaceToolbar(e);
}

inline bool utEditAnyScreen(const UtEdit& e) {
    return e.scr[kUtLayPad] != 0 || e.scr[kUtLayLabel] != 0;
}

// True when `part` is in screen mode with a screen key of its own (the label in the pad has none).
inline bool utEditOwnScreen(const UtEdit& e, int part) {
    if (part <= kUtLayGrid || part >= kUtLayParts || !e.scr[part]) return false;
    return !(part == kUtLayLabel && e.labelInPad);
}

// A screen part's key: the NEAREST screen corner of where it is drawn (utScreenFromClient), its
// size the rectangle's own record px (never re-derived from the drawn px, which a scale under 1
// would round away).
inline UtScreenRect utEditScreenKey(const UtEdit& e, int part) {
    const UtRect& r = e.rect[part];
    const UtRectF px = utLocalToClient(r, e.originX, e.originY, e.scale);
    UtScreenRect k = utScreenFromClient(px, e.screenW, e.screenH, e.scale);
    k.w = r.w < 1 ? 1 : r.w > kUtScreenMax ? kUtScreenMax : r.w;
    k.h = r.h < 1 ? 1 : r.h > kUtScreenMax ? kUtScreenMax : r.h;
    return k;
}

// The rectangle the live page will have for `part` once SAVE is read back: the edited one in
// window mode; in screen mode the one its key resolves to (utScreenLocate - a record px off at
// most, from the rounding of the corner offsets).
inline UtRect utEditResolvedRect(const UtEdit& e, int part) {
    if (part < 0 || part >= kUtLayParts || !e.scr[part]) return e.rect[part];
    const UtScreenSpot spot = utScreenLocate(utEditScreenKey(e, part), e.screenKnown, e.screenW,
                                             e.screenH, e.originX, e.originY, e.scale);
    return spot.why == kUtLayoutOk ? spot.local : e.rect[part];
}

// Where a part in SCREEN mode takes the clicks outside the window, frame-local screen px: the
// pad's rectangle joined with the toolbar, the label's rectangle, each grown by half a handle
// (the handles straddle the edges). A part in window mode: 0 x 0 - a window-mode pad's toolbar
// only where it leaves the window. The caller adds the frame's origin and publishes them with
// utEditWindowClient: the parts of these grounds inside the window stay the window's.
inline void utEditScreenGrounds(const UtEdit& e, UtRectF* pad, UtRectF* label) {
    const UtRectF none = {0.0f, 0.0f, 0.0f, 0.0f};
    *pad = none;
    *label = none;
    if (e.state == kUtEdClosed) return;
    const float s = e.scale;
    const float half = (float)utEditHandlePx(s) * 0.5f;
    if (e.scr[kUtLayPad]) {
        UtRect u = e.rect[kUtLayPad];
        for (int k = 0; k < kUtEdButtons; ++k) {
            const UtRect& t = e.tool[k];
            const int x1 = (u.x + u.w > t.x + t.w) ? u.x + u.w : t.x + t.w;
            const int y1 = (u.y + u.h > t.y + t.h) ? u.y + u.h : t.y + t.h;
            if (t.x < u.x) u.x = t.x;
            if (t.y < u.y) u.y = t.y;
            u.w = x1 - u.x;
            u.h = y1 - u.y;
        }
        const UtRectF f = utScaledRectF(u, s);
        *pad = UtRectF{f.x - half, f.y - half, f.w + 2.0f * half, f.h + 2.0f * half};
    } else {
        // A window-mode pad: its toolbar is the screen input's only where it leaves the window
        // (utEditPlaceToolbar keeps it inside, so normally never).
        bool out = false;
        UtRect u = e.tool[0];
        for (int k = 0; k < kUtEdButtons; ++k) {
            const UtRect& t = e.tool[k];
            if (!utRectInside(t, e.plate)) out = true;
            const int x1 = (u.x + u.w > t.x + t.w) ? u.x + u.w : t.x + t.w;
            const int y1 = (u.y + u.h > t.y + t.h) ? u.y + u.h : t.y + t.h;
            if (t.x < u.x) u.x = t.x;
            if (t.y < u.y) u.y = t.y;
            u.w = x1 - u.x;
            u.h = y1 - u.y;
        }
        if (out) {
            const UtRectF f = utScaledRectF(u, s);
            *pad = UtRectF{f.x - half, f.y - half, f.w + 2.0f * half, f.h + 2.0f * half};
        }
    }
    if (utEditOwnScreen(e, kUtLayLabel)) {
        const UtRectF f = utScaledRectF(e.rect[kUtLayLabel], s);
        *label = UtRectF{f.x - half, f.y - half, f.w + 2.0f * half, f.h + 2.0f * half};
    }
}

// The caravan window as the editor sees it - the plate, frame-local screen px (the caller adds the
// frame's origin). The screen input leaves every point inside it to the window while the editor
// is open.
inline UtRectF utEditWindowClient(const UtEdit& e) {
    if (e.state == kUtEdClosed || !utEditUsable(e.plate)) return UtRectF{0.0f, 0.0f, 0.0f, 0.0f};
    return utScaledRectF(e.plate, e.scale);
}

// The status words while a part is on the screen; null when none is.
inline const char* utEditScreenHint(const UtEdit& e) {
    if (e.scr[kUtLayPad] && e.scr[kUtLayLabel])
        return "on the screen - SAVE writes pad_screen and label_screen";
    if (e.scr[kUtLayPad]) return "on the screen - SAVE writes pad_screen";
    if (e.scr[kUtLayLabel]) return "on the screen - SAVE writes label_screen";
    return nullptr;
}

// ---- Titan Quest's pad and label --------------------------------------------------------------

// The pad at its derived rectangle (each edge within one record px: utLayoutResolve's rule), in
// the window.
inline bool utEditPadDerived(const UtEdit& e) {
    return !e.scr[kUtLayPad] && utRectNear(e.rect[kUtLayPad], e.def[kUtLayPad], 1);
}

// The pad's own label box where the pad is now: the derived box for the derived pad, row 2's box
// of the pad fitted in its rectangle otherwise (0 x 0 when nothing fits).
inline UtRect utEditPadLabelBox(const UtEdit& e) {
    if (utEditPadDerived(e)) return e.def[kUtLayLabel];
    const UtPadFit f = utPadFitRect(e.rect[kUtLayPad], e.groups, e.gap, e.scale, e.widest, e.nextChars);
    return f.ok ? utPadLabelBox(f, e.edit != 0) : UtRect{0, 0, 0, 0};
}

// While the label is the pad's own box it follows the pad (kept where it was when nothing fits),
// and takes the pad's mode (its bounds while it is dragged out of the pad); it has no key of its
// own then.
inline void utEditLabelFollow(UtEdit* e) {
    if (!e->labelInPad) return;
    const UtRect box = utEditPadLabelBox(*e);
    if (utEditUsable(box)) e->rect[kUtLayLabel] = box;
    e->scr[kUtLayLabel] = e->scr[kUtLayPad];
}


// The end of a label drag: a label of the player's put back into the pad over the pad's own label
// box is that box again (it follows the pad, SAVE removes its line).
inline bool utEditLabelHome(UtEdit* e) {
    if (e->labelInPad) return false;
    const UtRect box = utEditPadLabelBox(*e);
    if (!utEditUsable(box) || !utRectInside(e->rect[kUtLayLabel], e->rect[kUtLayPad]) ||
        !utRectsMeet(e->rect[kUtLayLabel], box))
        return false;
    e->labelInPad = 1;
    utEditLabelFollow(e);
    return true;
}

// True when the part's line is removed at SAVE (window mode): the pad at its derived rectangle;
// the label while it is the pad's own box, or at its derived box with the pad at its derived
// rectangle; the grid at its derived rectangle.
inline bool utEditPartDerived(const UtEdit& e, int part) {
    if (part == kUtLayPad) return utEditPadDerived(e);
    if (part == kUtLayLabel)
        return e.labelInPad != 0 || (!e.scr[kUtLayLabel] &&
                                     utRectEq(e.rect[kUtLayLabel], e.def[kUtLayLabel]) &&
                                     utEditPadDerived(e));
    return utRectEq(e.rect[kUtLayGrid], e.def[kUtLayGrid]);
}

// A refused part's reason (utLayoutWhyText) and the part it meets ("" = none named). On the game's
// own Transfer page the editor outlines the real sack's grid but judges the parts against the
// collection's (utEditCheck), so the grid is named as the collection's there: "would cover the
// collection's item grid", "overlaps another part of the page: the collection's item grid".
inline const char* utEditWhyText(const UtEdit& e, int part) {
    const int why = e.partWhy[part];
    return e.group == kUtEdTransferPage && why == kUtLayoutScreenOverGrid ? "would cover the collection's item grid"
                                                                          : utLayoutWhyText(why);
}

inline const char* utEditAgainstName(const UtEdit& e, int part) {
    const int a = e.against[part];
    if (a < 0 || e.partWhy[part] == kUtLayoutScreenOverGrid) return "";
    return e.group == kUtEdTransferPage && a == kUtLayGrid ? "collection's item grid" : utLayoutPartName(a);
}

// The words for a refused part: "the pad would cover the item grid", "the label overlaps another
// part of the page: the pad" (the real Transfer page's words: utEditWhyText / utEditAgainstName).
// Empty when the part holds. Returns the length written.
inline int utEditRefusal(const UtEdit& e, int part, char* out, size_t cap) {
    if (!out || cap == 0) return 0;
    out[0] = 0;
    if (part < 0 || part >= kUtLayParts || e.partWhy[part] == kUtLayoutOk) return 0;
    const char* against = utEditAgainstName(e, part);
    const int n = _snprintf_s(out, cap, _TRUNCATE, "the %s %s%s%s", utLayoutPartName(part),
                              utEditWhyText(e, part), against[0] ? ": the " : "", against);
    return (n > 0 && (size_t)n < cap) ? n : (int)(cap - 1);
}

// The value SAVE writes for `part` in window mode: "x,y,w,h", or nothing (returns 0) when the part
// is derived (utEditPartDerived) - its line is removed, so a page left at the defaults keeps no
// rectangle.
inline int utEditKeyValue(const UtEdit& e, int part, char* out, size_t cap) {
    if (!out || cap == 0) return 0;
    out[0] = 0;
    if (part < 0 || part >= kUtLayParts) return 0;
    if (part == kUtLayGrid && !e.gridMovable) return 0;
    if (utEditPartDerived(e, part)) return 0;
    return utRectFormat(e.rect[part], out, cap);
}

// What SAVE writes, per part, to the settings file in force (the main uniquetab.ini, or the
// game's own mods\<leaf>\uniquetab.ini). The grid (only while it can be set): grid_<cover> (its
// value, or removed when it is the derived rectangle). A part in WINDOW mode: <part>_<cover>
// likewise AND <part>_screen removed (back in the window). A part in SCREEN mode: <part>_screen
// = its key; its <part>_<cover> line is not touched (the screen key wins over it while set;
// RESET + SAVE brings the window back). `mainOnly` = a screen key: its removal pins the part to
// the window.
struct UtEditSaveKey {
    char key[40];
    char value[48];
    int set;        // 1 = `value` is written; 0 = the line is removed
    int mainOnly;   // 1 = a screen key: removed = the part back in the window
};
const int kUtEdSaveKeysMax = 5;
struct UtEditSavePlan {
    int n;
    UtEditSaveKey k[kUtEdSaveKeysMax];
};

inline void utEditSavePlanOf(const UtEdit& e, const char* cover, UtEditSavePlan* out) {
    out->n = 0;
    for (int p = 0; p < kUtLayParts; ++p) {
        if (p == kUtLayGrid && !e.gridMovable) {
            if (!e.gridReset) continue;
            UtEditSaveKey& g = out->k[out->n++];   // RESET on a fixed grid: its line removed
            _snprintf_s(g.key, sizeof(g.key), _TRUNCATE, "grid_%s", cover ? cover : "");
            g.value[0] = 0;
            g.set = 0;
            g.mainOnly = 0;
            continue;
        }
        if (utEditOwnScreen(e, p)) {
            UtEditSaveKey& s = out->k[out->n++];
            _snprintf_s(s.key, sizeof(s.key), _TRUNCATE, "%s_screen", utLayoutPartName(p));
            s.set = utScreenRectFormat(utEditScreenKey(e, p), s.value, sizeof(s.value)) > 0 ? 1 : 0;
            s.mainOnly = 1;
            continue;
        }
        UtEditSaveKey& w = out->k[out->n++];
        _snprintf_s(w.key, sizeof(w.key), _TRUNCATE, "%s_%s",
                    p == kUtLayGrid && e.relicArea ? "relics" : utLayoutPartName(p), cover ? cover : "");
        w.set = utEditKeyValue(e, p, w.value, sizeof(w.value)) > 0 ? 1 : 0;
        if (!w.set) w.value[0] = 0;
        w.mainOnly = 0;
        if (p == kUtLayGrid) continue;
        UtEditSaveKey& s = out->k[out->n++];
        _snprintf_s(s.key, sizeof(s.key), _TRUNCATE, "%s_screen", utLayoutPartName(p));
        s.value[0] = 0;
        s.set = 0;
        s.mainOnly = 1;
    }
}

inline bool utEditOpen(UtEdit* e, const UtEditOpenIn& in) {
    if (e->state != kUtEdClosed) return false;
    if (!(in.scale >= 0.2f && in.scale <= 8.0f) || in.group == kUtEdNoPage) return false;
    if (!utEditUsable(in.def[kUtLayPad]) || !utEditUsable(in.def[kUtLayLabel])) return false;
    UtEdit n = {};
    n.state = kUtEdOpen;
    n.why = kUtEdWhyNone;
    n.plate = in.plate;
    for (int p = 0; p < kUtLayParts; ++p) {
        n.def[p] = in.def[p];
        n.defWin[p] = in.defWinOk && utEditUsable(in.defWin[p]) ? in.defWin[p] : in.def[p];
        n.rect[p] = utEditUsable(in.inForce[p]) ? in.inForce[p] : n.def[p];
        n.orig[p] = n.rect[p];
        n.partWhy[p] = kUtLayoutOk;
        n.against[p] = -1;
        n.scr[p] = (p != kUtLayGrid && in.screenIn[p] && utEditUsable(in.inForce[p])) ? 1 : 0;
        n.scrOrig[p] = n.scr[p];
    }
    n.gridMovable = in.gridMovable ? 1 : 0;
    n.relicArea = in.relicArea ? 1 : 0;
    utEditMinSizes(in.maxCellW, in.maxCellH, in.rowGap, in.scale, n.minW, n.minH);
    utEditSetCell(&n, in.cellW, in.cellH, in.rowGap);
    n.scale = in.scale;
    n.groups = in.groups;
    n.gap = in.gap;
    n.widest = in.widest;
    n.nextChars = in.nextChars;
    n.edit = in.edit ? 1 : 0;
    n.labelInPad = in.labelInPad ? 1 : 0;
    n.labelInPadOrig = n.labelInPad;
    if (n.labelInPad) {
        n.scr[kUtLayLabel] = n.scr[kUtLayPad];   // the pad's own box: the pad's place and mode
        n.scrOrig[kUtLayLabel] = n.scr[kUtLayLabel];
    }
    utEditSetScreen(&n, in.screenKnown, in.screenW, in.screenH, in.originX, in.originY);   // + toolbar
    n.coverW = in.coverW;
    n.coverH = in.coverH;
    n.group = in.group;
    n.lastTick = in.now;
    n.part = -1;
    n.ok = 1;
    *e = n;
    return true;
}

// The edited rectangles through the resolver the live page runs, every one as the player's
// rectangle (the label only while it is not the pad's own box; the grid only while it can be
// set): SAVE is allowed only when all three would be in force as they are. A part in screen mode
// goes in as its screen key (placed against the editor's screen and origin) and is judged by its
// screen verdict (L.screenWhy); a part in window mode as its <W>x<H> rectangle with no screen key.
// The grid that cannot be set is the derived one, except on the game's own Transfer page until a
// RESET (gridReset: the derived one then too): there it
// is `base`'s (the collection's grid line, or none = the derived grid), so a part over the real
// cells passes wherever the collection page would keep it.
inline bool utEditCheck(UtEdit* e, const UtLayoutInput& base) {
    UtLayoutInput in = base;
    in.screenKnown = e->screenKnown;
    in.screenW = e->screenW;
    in.screenH = e->screenH;
    in.originX = e->originX;
    in.originY = e->originY;
    for (int p = 0; p < kUtLayParts; ++p) {
        in.screenSet[p] = 0;
        in.user[p] = &e->rect[p];
        if (p != kUtLayGrid && e->scr[p]) {
            in.screenSet[p] = 1;
            in.screenRect[p] = utEditScreenKey(*e, p);
            in.user[p] = nullptr;
        }
    }
    if (!e->gridMovable)
        in.user[kUtLayGrid] =
            e->group == kUtEdTransferPage && !e->gridReset ? base.user[kUtLayGrid] : nullptr;
    if (e->labelInPad) {
        in.user[kUtLayLabel] = nullptr;
        in.screenSet[kUtLayLabel] = 0;
    }
    const UtLayout L = utLayoutResolve(in);
    e->ok = 1;
    for (int p = 0; p < kUtLayParts; ++p) {
        const bool scr = p != kUtLayGrid && e->scr[p] && in.screenSet[p];
        const int why = scr ? L.screenWhy[p] : L.why[p];
        e->partWhy[p] = why == kUtLayoutUnset ? kUtLayoutOk : why;
        e->against[p] = scr ? L.screenAgainst[p] : L.against[p];
        if (e->partWhy[p] != kUtLayoutOk) e->ok = 0;
    }
    return e->ok != 0;
}

inline bool utEditInside(float x, float y, const UtRectF& r) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

// Toolbar first; then the handles of the label, the pad and the grid (corners, then the edge
// strips); then their bodies in the same order - the small parts win over the grid, the label
// inside the pad over the pad. The grid takes part only while it can be set.
inline UtEdHit utEditHitTest(const UtEdit& e, float x, float y) {
    UtEdHit h = {kUtEdHitNone, -1, 0, -1};
    if (e.state != kUtEdOpen) return h;
    const float s = e.scale;
    for (int b = 0; b < kUtEdButtons; ++b) {
        if (utEditInside(x, y, utScaledRectF(e.tool[b], s))) {
            h.kind = kUtEdHitButton;
            h.button = b;
            return h;
        }
    }
    static const int kOrder[kUtLayParts] = {kUtLayLabel, kUtLayPad, kUtLayGrid};
    const int parts = e.gridMovable ? kUtLayParts : kUtLayParts - 1;
    const float half = (float)utEditHandlePx(s) * 0.5f;
    for (int k = 0; k < parts; ++k) {
        const int p = kOrder[k];
        const UtRectF r = utScaledRectF(e.rect[p], s);
        const float l = r.x, t = r.y, rr = r.x + r.w, b = r.y + r.h;
        const float xs[2] = {l, rr}, ys[2] = {t, b};
        for (int cy = 0; cy < 2; ++cy) {
            for (int cx = 0; cx < 2; ++cx) {
                if (fabsf(x - xs[cx]) <= half && fabsf(y - ys[cy]) <= half) {
                    h.kind = kUtEdHitHandle;
                    h.part = p;
                    h.edges = (cx ? kUtEdgeR : kUtEdgeL) | (cy ? kUtEdgeB : kUtEdgeT);
                    return h;
                }
            }
        }
        int edges = 0;
        if (y >= t && y <= b) {
            if (fabsf(x - l) <= half) edges = kUtEdgeL;
            else if (fabsf(x - rr) <= half) edges = kUtEdgeR;
        }
        if (!edges && x >= l && x <= rr) {
            if (fabsf(y - t) <= half) edges = kUtEdgeT;
            else if (fabsf(y - b) <= half) edges = kUtEdgeB;
        }
        if (edges) {
            h.kind = kUtEdHitHandle;
            h.part = p;
            h.edges = edges;
            return h;
        }
    }
    for (int k = 0; k < parts; ++k) {
        const int p = kOrder[k];
        if (utEditInside(x, y, utScaledRectF(e.rect[p], s))) {
            h.kind = kUtEdHitBody;
            h.part = p;
            h.edges = kUtEdgeMove;
            return h;
        }
    }
    return h;
}

// ---- the grid as the editor shows it ---------------------------------------------------------
// The edited grid's cells: the shown group's SLOTS (`slotW` x `slotH` sack cells, the group's
// largest footprint) over the sack's cells the rectangle holds (utLayoutGridCells - the 32 record px
// cell, at most `cap` cells), from the rectangle's top-left, drawn as GHOST LINES: cols + 1
// verticals and rows + 1 horizontals over the slots' own extent, so the lines show where whole
// items fall (the slack at the right and the bottom stays empty, as on the page). Record px,
// frame-local; they follow the rectangle through every drag. A slot not known = one cell.
struct UtEdGhost {
    int cols, rows;         // whole slots across and down (0 = the rectangle holds none)
    int pitchW, pitchH;     // one slot, record px
    UtRect cells;           // the slots' extent: cols x pitchW by rows x pitchH less one row gap
};
inline UtEdGhost utEditGhost(const UtRect& grid, int cap, int slotW = 1, int slotH = 1) {
    const UtGrid g = utLayoutGridCells(grid, cap);
    if (slotW < 1) slotW = 1;
    if (slotH < 1) slotH = 1;
    UtEdGhost o;
    o.cols = g.cols / slotW;
    o.rows = g.rows / slotH;
    o.pitchW = slotW * kUtGridCell;
    o.pitchH = slotH * (kUtGridCell + kUtGridRowGap);
    o.cells = UtRect{grid.x, grid.y, o.cols * o.pitchW, o.rows > 0 ? o.rows * o.pitchH - kUtGridRowGap : 0};
    return o;
}
// A line's place in drawn px: the editor's origin + the record px at the scale (utScaledRectF's
// rounding, so the lines meet the rectangle's outline).
inline float utEditGhostAt(int rec, float s, float origin) { return origin + utPadRound((float)rec * s); }

// The page's items are HIDDEN while the editor is open and was ticked recently (a page that left
// the screen gets them back at its next build even before the editor hears of it); every close -
// SAVE once read back, CANCEL, each of the tick's close reasons - brings them back. The view builds
// an empty page while this holds and the normal page again when it stops holding.
inline bool utEditHidesItems(const UtEdit& e, unsigned now) {
    return e.state != kUtEdClosed && now - e.lastTick <= kUtEdGoneMs;
}

// True when (x, y) lies on something of the editor's in any state but closed: the toolbar, the
// pad or the label grown by half a handle (the handles straddle the edges), the grid while it can
// be set. A press there is the editor's, never the page's under it - also where a part lies over
// the grid.
inline bool utEditOwnsPoint(const UtEdit& e, float x, float y) {
    if (e.state == kUtEdClosed) return false;
    const float s = e.scale;
    for (int b = 0; b < kUtEdButtons; ++b)
        if (utEditInside(x, y, utScaledRectF(e.tool[b], s))) return true;
    const float half = (float)utEditHandlePx(s) * 0.5f;
    for (int p = e.gridMovable ? kUtLayGrid : kUtLayPad; p < kUtLayParts; ++p) {
        const UtRectF r = utScaledRectF(e.rect[p], s);
        const UtRectF g = {r.x - half, r.y - half, r.w + 2.0f * half, r.h + 2.0f * half};
        if (x >= g.x && x <= g.x + g.w && y >= g.y && y <= g.y + g.h) return true;
    }
    return false;
}

// The page's pick-up of a collected item under a press (InventorySack::GetItemUnderPoint at the
// page's click sites): refused while the open editor - on a page on the screen, drawn within
// kUtEdGoneMs - owns the cursor's point (x, y in the editor's frame), so the engine's copy of the
// editor's press never puts an item on the cursor.
inline bool utEditOwnsTake(const UtEdit& e, bool pageShown, unsigned long sinceDrawnMs, bool cursorKnown,
                           float x, float y) {
    return pageShown && e.state != kUtEdClosed && sinceDrawnMs <= kUtEdGoneMs && cursorKnown &&
           utEditOwnsPoint(e, x, y);
}

// THE ENGINE'S COPY OF AN EDITOR PRESS. The editor acts on the window's mouse messages; the engine
// reads the same click through its own input device (ut_padinput.h), in the window or outside it
// (a handle reaches half a handle past its part's edge, past the window's edge too). The screen
// input asks utEditClaims for every DOWN while the editor is open or has just taken a press: true
// = the press is the editor's and the engine never sees it, its drag or its UP. The mark is the
// last press the window procedure gave the editor (the point frame-local screen px, the clock),
// so the claim holds in either order - the engine's read before the message (the editor's parts
// are still where the press landed) or after it (a CANCEL that closed the editor, a SAVE, a drag
// that began): a DOWN within kUtEdClaimPx of the mark and at most kUtEdClaimMs after it.
const unsigned kUtEdClaimMs = 400;
const float kUtEdClaimPx = 2.0f;
struct UtEdPressMark {
    int have;      // 1 = a press was taken
    unsigned at;   // GetTickCount of it
    float x, y;    // where, screen px frame-local
};
inline void utEditMarkPress(UtEdPressMark* m, unsigned now, float x, float y) {
    if (!m) return;
    m->have = 1;
    m->at = now;
    m->x = x;
    m->y = y;
}
inline bool utEditMarkNear(const UtEdPressMark& m, unsigned now, float x, float y) {
    if (!m.have || now - m.at > kUtEdClaimMs) return false;
    const float dx = x - m.x, dy = y - m.y;
    return dx <= kUtEdClaimPx && dx >= -kUtEdClaimPx && dy <= kUtEdClaimPx && dy >= -kUtEdClaimPx;
}
inline bool utEditClaims(const UtEdit& e, unsigned long sinceDrawnMs, const UtEdPressMark& m,
                         unsigned now, float x, float y) {
    if (e.state != kUtEdClosed && sinceDrawnMs <= kUtEdGoneMs && utEditOwnsPoint(e, x, y)) return true;
    return utEditMarkNear(m, now, x, y);
}
// The claim as the screen input asks it: a mark answers ONE DOWN, the copy of its own press, so
// a second quick click at the same point after a CANCEL or a SAVE closed the editor is not the
// editor's (the page's, or the world's under a screen toolbar).
inline bool utEditClaimTake(const UtEdit& e, unsigned long sinceDrawnMs, UtEdPressMark* m,
                            unsigned now, float x, float y) {
    if (!m) return false;
    if (!utEditClaims(e, sinceDrawnMs, *m, now, x, y)) return false;
    if (utEditMarkNear(*m, now, x, y)) m->have = 0;
    return true;
}

inline void utEditClose(UtEdit* e, int why) {
    e->state = kUtEdClosed;
    e->why = why;
    e->part = -1;
    e->button = 0;
}

// The toolbar button under (x, y) - screen px, frame-local - in EVERY state but closed (the hit
// test answers only while open), or -1.
inline int utEditToolbarAt(const UtEdit& e, float x, float y) {
    if (e.state == kUtEdClosed) return -1;
    for (int b = 0; b < kUtEdButtons; ++b)
        if (utEditInside(x, y, utScaledRectF(e.tool[b], e.scale))) return b;
    return -1;
}

// LEFT DOWN at (x, y). `inputOk` = the cursor holds no item: with it false nothing is done -
// except CANCEL. physL / physR / swapped pick the button the drag polls.
//
// CANCEL answers in EVERY state - open, mid-drag, saving - and whatever the cursor holds, so the
// editor always has an exit (while saving it stops the wait: the file is written, the page
// follows it once read back). A LEFT DOWN during a drag ends that drag first (its release was
// lost and the poll has not run yet) and is then judged as a click of an open editor.
inline int utEditLeftDown(UtEdit* e, float x, float y, int inputOk, int physL, int physR,
                          int swapped) {
    if (e->state == kUtEdClosed) return kUtEdActPass;
    if (utEditToolbarAt(*e, x, y) == kUtEdCancel) {
        utEditClose(e, e->state == kUtEdSaving ? kUtEdWhySaveSlow : kUtEdWhyCancel);
        return kUtEdActCancel;
    }
    if (e->state == kUtEdDrag) {
        e->state = kUtEdOpen;
        e->part = -1;
        e->button = 0;
    }
    if (e->state != kUtEdOpen || !inputOk) return kUtEdActPass;
    const UtEdHit h = utEditHitTest(*e, x, y);
    if (h.kind == kUtEdHitButton) {
        if (h.button == kUtEdSave) return e->ok ? kUtEdActSave : kUtEdActSaveRefused;
        for (int p = 0; p < kUtLayParts; ++p) {   // every part back in the window: its derived set
            if (!utRectEq(e->defWin[p], e->def[p])) e->resetWin = 1;
            e->def[p] = e->defWin[p];
            e->rect[p] = e->def[p];
            e->scr[p] = 0;   // back in the window: SAVE removes the screen keys
        }
        if (!e->gridMovable) e->gridReset = 1;   // the collection's grid too: SAVE removes its line
        e->labelInPad = 1;   // the label is the pad's own box again: SAVE removes its line
        utEditPlaceToolbar(e);
        return kUtEdActReset;
    }
    if (h.kind == kUtEdHitNone) return kUtEdActPass;
    e->state = kUtEdDrag;
    e->part = h.part;
    e->edges = h.edges;
    e->x0 = x;
    e->y0 = y;
    e->start = e->rect[h.part];
    e->button = utEditDragButton(swapped, physL, physR);
    return kUtEdActDrag;
}

// The window's point -> the editor's: canvas px (the message's client px times the canvas px per
// client px) less the frame's rounded top-left on the canvas = screen px, frame-local.
inline void utEditPointOf(float canvasX, float canvasY, float originX, float originY, float* x,
                          float* y) {
    *x = canvasX - originX;
    *y = canvasY - originY;
}

// One LEFT DOWN while the editor is open, as the window procedure routes it (ut_panel.cpp
// editInput): utEditLeftDown's action, what the press took (the part and its edges for a drag, the
// toolbar button) and whether the press is the editor's - swallowed, its LEFT UP too, and the
// engine's own read of it claimed. A press on anything of the editor's is the editor's, also one it
// does not act on (an item on the cursor): the grid while it can be set, the pad, the label, the
// toolbar.
struct UtEdPress {
    int act;      // UtEdAct
    UtEdHit hit;  // what the press took; kind kUtEdHitNone when it took nothing
    int owns;     // the point lies on something of the editor's (utEditOwnsPoint)
    int mine;     // the press is the editor's
};
inline UtEdPress utEditPress(UtEdit* e, float x, float y, int inputOk, int physL, int physR,
                             int swapped) {
    UtEdPress p;
    p.hit.kind = kUtEdHitNone;
    p.hit.part = -1;
    p.hit.edges = 0;
    p.hit.button = -1;
    p.owns = utEditOwnsPoint(*e, x, y) ? 1 : 0;
    const int tb = utEditToolbarAt(*e, x, y);
    p.act = utEditLeftDown(e, x, y, inputOk, physL, physR, swapped);
    if (p.act == kUtEdActDrag) {
        p.hit.kind = e->edges == kUtEdgeMove ? kUtEdHitBody : kUtEdHitHandle;
        p.hit.part = e->part;
        p.hit.edges = e->edges;
    } else if (p.act != kUtEdActPass && tb >= 0) {
        p.hit.kind = kUtEdHitButton;
        p.hit.button = tb;
    }
    p.mine = (p.act != kUtEdActPass || p.owns) ? 1 : 0;
    return p;
}

// The words of one press for the editor's DEBUG line: "the grid's body - a move", "the grid's
// top-left corner - a resize", "the SAVE button", "nothing of the editor's - passed to the page".
inline void utEditPressText(const UtEdPress& p, int inputOk, char* out, size_t cap) {
    static const char* const kTags[kUtEdButtons] = {"SAVE", "CANCEL", "RESET"};
    if (!out || cap < 1) return;
    out[0] = 0;
    if (p.hit.kind == kUtEdHitBody) {
        snprintf(out, cap, "the %s's body - a move", utLayoutPartName(p.hit.part));
    } else if (p.hit.kind == kUtEdHitHandle) {
        const int g = p.hit.edges;
        const char* where = g == (kUtEdgeL | kUtEdgeT)   ? "top-left corner"
                            : g == (kUtEdgeR | kUtEdgeT) ? "top-right corner"
                            : g == (kUtEdgeL | kUtEdgeB) ? "bottom-left corner"
                            : g == (kUtEdgeR | kUtEdgeB) ? "bottom-right corner"
                            : g == kUtEdgeL              ? "left edge"
                            : g == kUtEdgeR              ? "right edge"
                            : g == kUtEdgeT              ? "top edge"
                                                         : "bottom edge";
        snprintf(out, cap, "the %s's %s - a resize", utLayoutPartName(p.hit.part), where);
    } else if (p.hit.kind == kUtEdHitButton && p.hit.button >= 0 && p.hit.button < kUtEdButtons) {
        snprintf(out, cap, "the %s button%s", kTags[p.hit.button],
                 p.act == kUtEdActSaveRefused ? " - SAVE is off" : "");
    } else if (!inputOk && p.owns) {
        snprintf(out, cap, "an item is on the cursor - only CANCEL answers (the press is the editor's)");
    } else if (p.owns) {
        snprintf(out, cap, "on the editor's parts, nothing taken (the press is the editor's)");
    } else {
        snprintf(out, cap, "nothing of the editor's - passed to the page");
    }
}

// The drag follows the point; true when the rectangle or its mode changed. The pad and the label
// (with the screen known): a MOVE is clamped to the screen and the mode follows the rectangle -
// wholly inside the window = window mode, across its edge = screen mode; a resize keeps the mode
// (clamped to its bounds) unless it brings a screen part wholly inside the window. The grid, and
// every part while the screen is unknown: clamped to the window, the grid snapped to whole cells.
// Nothing keeps a part off the grid or off another part: only where it ends is judged.
// Titan Quest: a pad within one record px of the derived pad IS the derived pad; a pad taken away
// from its place grows to the fitted pad's floor; a moved pad takes the toolbar along, and its
// label box while the label is the pad's own; a label that moves becomes the player's own.
inline bool utEditDragTo(UtEdit* e, float x, float y) {
    if (e->state != kUtEdDrag || e->part < 0 || e->part >= kUtLayParts) return false;
    const int p = e->part;
    if (p == kUtLayGrid && !e->gridMovable) return false;
    const float s = e->scale;
    const int dx = (int)floorf((x - e->x0) / s + 0.5f);
    const int dy = (int)floorf((y - e->y0) / s + 0.5f);
    const bool grid = p == kUtLayGrid;
    int scr = e->scr[p];
    UtRect r;
    if (utEditCanLeave(*e, p) && e->edges == kUtEdgeMove) {
        r = utEditDragRect(e->start, kUtEdgeMove, dx, dy, e->screen, e->minW[p], e->minH[p], 1, 1);
        scr = utRectInside(r, e->plate) ? 0 : 1;
    } else {
        UtEdit tmp = *e;
        tmp.scr[p] = scr;
        r = utEditDragRect(e->start, e->edges, dx, dy, utEditBounds(tmp, p), e->minW[p], e->minH[p],
                           grid ? e->snapW : 1, grid ? e->snapH : 1);
        if (scr && utRectInside(r, e->plate)) scr = 0;
    }
    if (p == kUtLayPad) {
        if (!scr && utRectNear(r, e->def[kUtLayPad], 1)) {
            // within one record px of the derived pad IS the derived pad (utLayoutResolve's rule)
            r = e->def[kUtLayPad];
        } else if (r.w < e->minW[p] || r.h < e->minH[p]) {
            // the derived pad is utPadLayout's, drawn by its own rules: its record rectangle can
            // be under the floor of a pad fitted in a rectangle (30 px high at UI scale 1.25), so
            // a pad taken away from its place grows to that floor, kept inside its bounds
            UtEdit tmp = *e;
            tmp.scr[p] = scr;
            const UtRect b = utEditBounds(tmp, p);
            if (r.w < e->minW[p]) r.w = e->minW[p];
            if (r.h < e->minH[p]) r.h = e->minH[p];
            if (r.x + r.w > b.x + b.w) r.x = b.x + b.w - r.w;
            if (r.y + r.h > b.y + b.h) r.y = b.y + b.h - r.h;
            if (r.x < b.x) r.x = b.x;
            if (r.y < b.y) r.y = b.y;
            if (scr && utRectInside(r, e->plate)) scr = 0;
        }
    }
    if (utRectEq(r, e->rect[p]) && scr == e->scr[p]) return false;
    e->rect[p] = r;
    e->scr[p] = scr;
    if (p == kUtLayLabel) e->labelInPad = 0;
    if (p == kUtLayPad) {
        utEditPlaceToolbar(e);
        utEditLabelFollow(e);
    }
    return true;
}

// The drag is over (its LEFT UP, or the polled release): the label's home rule, then open.
inline void utEditDragEnd(UtEdit* e) {
    if (e->part == kUtLayLabel) utEditLabelHome(e);
    e->state = kUtEdOpen;
    e->part = -1;
    e->button = 0;
}

// LEFT UP (the window's message): the drag ends at (x, y). True when a drag ended.
inline bool utEditLeftUp(UtEdit* e, float x, float y) {
    if (e->state != kUtEdDrag) return false;
    utEditDragTo(e, x, y);
    utEditDragEnd(e);
    return true;
}

// After a SAVE: `written` = the settings file took the keys. Then the editor waits until the
// layout in force is the edited one; a failed write leaves it open.
inline void utEditSaved(UtEdit* e, int written, unsigned now) {
    if (e->state != kUtEdOpen || !written) return;
    e->state = kUtEdSaving;
    e->savedAt = now;
}

struct UtEdTick {
    unsigned now;
    int group;          // the page shown (kUtEdNoPage = the pad is not shown)
    int relicArea;      // 1 = the page shown is a relic page (its grid part is the relic area)
    float scale;
    int focused;        // the game has the focus
    int coverW, coverH;
    int layoutOk;       // a layout is in force at a usable scale
    int physL, physR;   // the polled physical buttons
    int haveCursor;
    float cx, cy;       // the polled cursor, screen px frame-local
    int matches;        // the layout in force equals the edited rectangles (the save handover)
    // the screen this frame (screenKnown 0 = not known: nothing changes)
    int screenKnown;
    int screenW, screenH;
    float originX, originY;
};

// Once a frame while the page is drawn. Returns the UtEdWhy it closed with this tick, else 0;
// `*moved` = a drag moved a rectangle.
inline int utEditTick(UtEdit* e, const UtEdTick& t, int* moved) {
    if (moved) *moved = 0;
    if (e->state == kUtEdClosed) return kUtEdWhyNone;
    int why = kUtEdWhyNone;
    if (t.now - e->lastTick > kUtEdGoneMs) why = kUtEdWhyGone;
    else if (!t.layoutOk) why = kUtEdWhyLayout;
    else if (t.coverW != e->coverW || t.coverH != e->coverH) why = kUtEdWhyCover;
    else if (fabsf(t.scale - e->scale) > 0.0005f) why = kUtEdWhyScale;
    else if (t.screenKnown && e->screenKnown &&
             (t.screenW != e->screenW || t.screenH != e->screenH))
        why = kUtEdWhyScreen;
    else if (t.group == kUtEdNoPage) why = kUtEdWhyGroup;
    else if ((t.relicArea ? 1 : 0) != e->relicArea) why = kUtEdWhyKind;
    else if (!t.focused) why = kUtEdWhyFocus;
    e->lastTick = t.now;
    if (why != kUtEdWhyNone) {
        utEditClose(e, why);
        return why;
    }
    e->group = t.group;
    if (t.screenKnown &&
        (!e->screenKnown || t.originX != e->originX || t.originY != e->originY))
        utEditSetScreen(e, 1, t.screenW, t.screenH, t.originX, t.originY);
    if (e->state == kUtEdDrag) {
        bool m = false;
        if (t.haveCursor) m = utEditDragTo(e, t.cx, t.cy);
        const int held = e->button == 1 ? t.physL : e->button == 2 ? t.physR : 0;
        if (!held) utEditDragEnd(e);
        if (moved) *moved = m ? 1 : 0;
        return kUtEdWhyNone;
    }
    if (e->state == kUtEdSaving) {
        if (t.matches) why = kUtEdWhySaved;
        else if (t.now - e->savedAt > kUtEdSaveWaitMs) why = kUtEdWhySaveSlow;
        if (why != kUtEdWhyNone) utEditClose(e, why);
    }
    return why;
}

// True when every edited rectangle equals the one in force (the save has been read back), each
// edge within one record px (a line typed at the derived numbers resolves to the derived part):
// a screen part's as its key resolves (utEditResolvedRect). `screenInForce` (UtLayout::screen,
// optional) must then say the same mode for the pad and the label.
inline bool utEditMatches(const UtEdit& e, const UtRect inForce[kUtLayParts],
                          const int* screenInForce = nullptr) {
    for (int p = 0; p < kUtLayParts; ++p) {
        const UtRect r = utEditOwnScreen(e, p) ? utEditResolvedRect(e, p) : e.rect[p];
        if (!utRectNear(r, inForce[p], 1)) return false;
        if (screenInForce && p != kUtLayGrid && (screenInForce[p] != 0) != utEditOwnScreen(e, p))
            return false;
    }
    return true;
}

// True when at least one rectangle differs from the one in force at the open.
inline bool utEditChanged(const UtEdit& e) {
    for (int p = 0; p < kUtLayParts; ++p)
        if (!utRectEq(e.rect[p], e.orig[p]) || e.scr[p] != e.scrOrig[p]) return true;
    return e.labelInPad != e.labelInPadOrig;
}

}  // namespace ut
