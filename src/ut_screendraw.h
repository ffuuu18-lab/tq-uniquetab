#pragma once
// The SCREEN pad's draw, its pure half: the ground and its 1-px frame, and the rectangles the
// screen pad's input reads (ut_padinput.h's UtPadScreenRects).
//
// The draw site (hooks.cpp hk_RollDraw, ut_panel.cpp panelRolloverDrawPre): InGameUI's Render
// (TQ.exe 0x56980) draws the HUD (InGameUI+0xC94) and then the item on the cursor (InGameUI+0xE4,
// its CursorHandler's Render); the HUD's pass 0 (0x131600) draws the caravan window (hud+0x4838C)
// and, 15 children later, the item rollover (hud+0x48240, Draw 0x18E6D0, the box of
// UI/UI_RolloverBox_*.tex). The screen parts are drawn at the HEAD of the rollover's Draw - one
// level below the tooltips and the item on the cursor - in a frame whose Transfer page draw already
// ran (utScreenSiteDecide); in any other frame, or without the site, from the Present detour (over
// everything). The in-window pad keeps its own site (the Transfer page draw's POST, inside the
// caravan's draw: under the rollover and the cursor item too).
//
// Pure: <string.h> and the two headers it extends, nothing of Windows; tools\test_layout.cpp
// links it.
#include <string.h>

#include "ut_layout.h"
#include "ut_padinput.h"

namespace ut {

// ---- the ground and its frame -----------------------------------------------------------------
// The screen pad / label ground is `ground` (canvas px, opaque kPadBack); its frame is `line` px
// wide, OUTSIDE the ground (the ground keeps the in-window pad's exact pixels, the cells and their
// gutters inside it never move): the top and left edges in the light line colour, the bottom and
// right edges in the dark one - the buttons' own border-box look. Whole pixels: the edges are
// rounded once (floor(v + 0.5)), every part is built from them.
struct UtScreenFrame {
    int x, y, w, h;          // the ground, rounded
    int ox, oy, ow, oh;      // the whole frame (ground + line on every side): the click-eater
    int top[4], left[4];     // x, y, w, h of the light edges
    int bottom[4], right[4]; // x, y, w, h of the dark edges
    int line;
};

inline int utScreenRound(float v) { return (int)(v >= 0.0f ? v + 0.5f : v - 0.5f); }

inline UtScreenFrame utScreenFrame(const UtRectF& ground, int line) {
    UtScreenFrame f;
    memset(&f, 0, sizeof(f));
    if (line < 1) line = 1;
    const int x0 = utScreenRound(ground.x), y0 = utScreenRound(ground.y);
    const int x1 = utScreenRound(ground.x + ground.w), y1 = utScreenRound(ground.y + ground.h);
    f.line = line;
    f.x = x0;
    f.y = y0;
    f.w = x1 - x0;
    f.h = y1 - y0;
    f.ox = x0 - line;
    f.oy = y0 - line;
    f.ow = f.w + 2 * line;
    f.oh = f.h + 2 * line;
    // top (light) and bottom (dark) run the whole outer width and own the four corners; left
    // (light) and right (dark) run the ground's height between them - no pixel drawn twice
    const int top[4] = {f.ox, f.oy, f.ow, line};
    const int left[4] = {f.ox, y0, line, f.h};
    const int bottom[4] = {f.ox, y1, f.ow, line};
    const int right[4] = {x1, y0, line, f.h};
    memcpy(f.top, top, sizeof(top));
    memcpy(f.left, left, sizeof(left));
    memcpy(f.bottom, bottom, sizeof(bottom));
    memcpy(f.right, right, sizeof(right));
    return f;
}

// ---- what the screen pad's input reads ---------------------------------------------------------
// Starts an empty publication (shown = 0, nothing in it).
inline void utScreenPubReset(UtPadScreenRects* p) {
    if (p) memset(p, 0, sizeof(*p));
}

// The pad: its whole frame (the ground and the 1-px edge are both click-eaters) and shown = 1.
// `k` = client px per canvas px (the event's space over the draw's; 1 when they are the same).
inline void utScreenPubPad(UtPadScreenRects* p, const UtScreenFrame& f, float kx = 1.0f,
                           float ky = 1.0f) {
    if (!p || f.w <= 0 || f.h <= 0) return;
    p->pad = UtPadScreenRect{(float)f.ox * kx, (float)f.oy * ky, (float)f.ow * kx, (float)f.oh * ky};
    p->shown = 1;
}

// One button as DRAWN: `r` canvas px, rounded exactly as the draw rounds it, so the click and the
// paint agree to the pixel; `index` = the pad's button. False when full or the rectangle is empty.
inline bool utScreenPubButton(UtPadScreenRects* p, const UtRectF& r, int index, float kx = 1.0f,
                              float ky = 1.0f) {
    if (!p || index < 0) return false;
    if (p->buttonCount < 0) p->buttonCount = 0;
    if (p->buttonCount >= kUtPadScreenMaxButtons) return false;
    const int x = utScreenRound(r.x), y = utScreenRound(r.y);
    const int w = utScreenRound(r.w), h = utScreenRound(r.h);
    if (w <= 0 || h <= 0) return false;
    UtPadScreenButton& b = p->buttons[p->buttonCount++];
    b.r = UtPadScreenRect{(float)x * kx, (float)y * ky, (float)w * kx, (float)h * ky};
    b.index = index;
    return true;
}

// The label: its whole frame, shown = 1.
inline void utScreenPubLabel(UtPadScreenRects* p, const UtScreenFrame& f, float kx = 1.0f,
                             float ky = 1.0f) {
    if (!p || f.w <= 0 || f.h <= 0) return;
    p->label = UtPadScreenRect{(float)f.ox * kx, (float)f.oy * ky, (float)f.ow * kx, (float)f.oh * ky};
    p->shown = 1;
}

// ---- the draw site's per-frame decision ---------------------------------------------------------
// At the head of the HUD item rollover's Draw: draw the screen parts here, or leave them to the
// Present detour (and why). `parts` = a part on the screen or the editor open (else the page draw's
// POST draws the in-window pad); `engineCanvas` = the call's canvas is the engine's; `pageThisFrame`
// = the Transfer page draw ran earlier in this frame (the caravan is under the rollover); `fresh` =
// the last Present laid the parts out under 250 ms ago; `drawn` = this frame's parts are drawn.
enum UtScreenSiteWhy {
    kUtSiteDraw = 0,
    kUtSiteNoParts,        // nothing on the screen and the editor closed
    kUtSiteNotEngine,      // another canvas
    kUtSiteNoPage,         // the caravan was not drawn before the rollover in this frame
    kUtSiteStale,          // no fresh layout
    kUtSiteDrawnAlready,   // once a frame
};
inline UtScreenSiteWhy utScreenSiteDecide(bool parts, bool engineCanvas, bool pageThisFrame, bool fresh,
                                          bool drawn) {
    if (!parts) return kUtSiteNoParts;
    if (!engineCanvas) return kUtSiteNotEngine;
    if (!pageThisFrame) return kUtSiteNoPage;
    if (!fresh) return kUtSiteStale;
    if (drawn) return kUtSiteDrawnAlready;
    return kUtSiteDraw;
}

}  // namespace ut
