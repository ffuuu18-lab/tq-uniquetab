// ut_panel.cpp - the collection pad and label (see ut_panel.h). TQ port of GD's ut_panel.cpp.
//
// Kept from GD: the flat look - every pixel a solid quad (fillRect / outlineRect) plus the game's
// own font for the captions, no texture at all - the palette constants verbatim, the green ring on
// the selected group, the amber lit toggle (GD's OWN button look, here the view toggle), ONE
// geometry function for draw and click, absolute clicks (one button, one group), the pad's
// session kill switch on a draw fault, and "nothing allocated, logged or loaded per frame".
// Changed for TQ: the pad is a compact two-row pad INSIDE the caravan frame, in its band below the
// grid (GD: a 9 x 3 strip over the reagent plate; a column beside the window), 19
// buttons (Collection, <, >, 15 groups, OWN) placed by ut_padlayout.h from the LIVE frame, the
// draw runs from the PresentSurface detour (GD: the ReagentWindow::Draw tail - TQ has no such
// export), the font is loaded by name with TQ's LoadFont(name, true, false), the input is the game
// window's procedure, and the captions are fixed short names (TQ has no text-width export for
// GD's ut_textfold / ut_fontmetrics fold).

#include "ut_panel.h"

#include "ut_padlayout.h"
#include "ut_slotart.h"   // the rect route and the equipment-slot art (pure)
#include "ut_graytex.h"   // the gray icons' names (pure)
#include "ut_color.h"     // search_mark_color (pure)

#include <stdio.h>
#include <string.h>

#include "hooks.h"
#include "tq_runtime.h"
#include "ut_bindings.h"
#include "ut_config.h"
#include "ut_generate.h"   // generateDone
#include "ut_live.h"
#include "ut_log.h"
#include "ut_owned.h"
#include "ut_paths.h"   // the gray folder beside the mod
#include "ut_plate.h"
#include "ut_proto.h"
#include "ut_search.h"
#include "ut_view.h"
#include "ut_viewgate.h"

namespace ut {
namespace {

// ---- the palette ---------------------------------------------------------------------------------
// GD's layout of the palette (the same names, the same roles), the COLOURS
// are TQ's own, sampled from the caravan window's textures by tools\ui_palette.py (region -> ARGB;
// section 1 lists each one). No colour here is invented.
//   window = CaravanWindow01.tex (565 x 637), tab = InventoryTransferTab01.tex (the Transfer tab,
//   one texture for up / focus / down), inc.up / inc.over / inc.down = ExpandStorageButton*01.tex
//   (the storage's "increase" text button: TQ's idle / hover / pressed faces).
const TqColor kPadBack   = {0.302f, 0.243f, 0.145f, 1.00f};   // 0xFF4D3E25 window (40,612) 480x18: the ground under the grid
const TqColor kBtnBg     = {0.380f, 0.275f, 0.129f, 1.00f};   // 0xFF614621 tab face (6,8) 40x16 median
const TqColor kBtnHover  = {0.475f, 0.424f, 0.255f, 1.00f};   // 0xFF796C41 inc.down face (12,10) 20x25 median
const TqColor kBtnDown   = {0.302f, 0.243f, 0.145f, 1.00f};   // 0xFF4D3E25 = the ground: pressed sinks into it
const TqColor kLineTop   = {0.682f, 0.620f, 0.443f, 1.00f};   // 0xFFAE9E71 tab top edge (10,1) 138x2
const TqColor kLineBot   = {0.098f, 0.086f, 0.055f, 1.00f};   // 0xFF19160E window grid cell's dark flank
const TqColor kRing      = {0.847f, 0.776f, 0.565f, 1.00f};   // 0xFFD8C690 inc.over lit rim (top 10 %): the shown group
const TqColor kCaption   = {0.847f, 0.776f, 0.565f, 1.00f};   // 0xFFD8C690 the same gold: button and label text
const TqColor kTglBg     = {0.380f, 0.275f, 0.129f, 1.00f};   // 0xFF614621 = kBtnBg (a toggle off is a button)
const TqColor kTglOnBg   = {0.667f, 0.584f, 0.322f, 1.00f};   // 0xFFAA9552 inc.up face: lit (Transfer OFF, OWN on, the shown group)
const TqColor kTglOnTop  = {0.741f, 0.659f, 0.396f, 1.00f};   // 0xFFBDA865 inc.over face: lit top line / lit hover
const TqColor kTglOnBot  = {0.475f, 0.424f, 0.255f, 1.00f};   // 0xFF796C41 inc.down face: lit bottom line
const TqColor kTglOnText = {0.098f, 0.086f, 0.055f, 1.00f};   // 0xFF19160E the cell flank: text on lit, 6.0:1
const TqColor kLedOff    = {0.369f, 0.267f, 0.125f, 1.00f};   // 0xFF5E4420 tab bottom edge (10,28) 138x2
const TqColor kLedOn     = {0.847f, 0.776f, 0.565f, 1.00f};   // 0xFFD8C690 the lit rim
// The search marks keep Grim Dawn's look and GD's own two colours: a marked group's PLATE turns a
// muted blue, so a button can be marked and be the shown group at once and both facts read (the
// blue plate, the lit ring) - and GD's caption colour on it, which GD picked to read on both
// plates (5.31:1, 4.56:1 hovered; TQ's gold would give 3.8:1 and 3.2:1). While a query stands, the
// captions of the fully indexed groups with no match are dimmed to the tab's own top-edge colour.
const TqColor kMarkBg     = {0.227f, 0.384f, 0.533f, 1.00f};   // #3a6288 (GD's plate)
const TqColor kMarkHover  = {0.271f, 0.424f, 0.576f, 1.00f};   // #456c93 (GD's hovered plate)
const TqColor kCaptionDim = {0.682f, 0.620f, 0.443f, 1.00f};   // 0xFFAE9E71 = kLineTop
const TqColor kMarkCaption = {0.910f, 0.918f, 0.929f, 1.00f};  // #e8eaed (GD's caption)
// The search's highlight on a matched slot: a thin frame on the slot's outermost pixel ring and
// a translucent wash under the icon, both in search_mark_color. Non-matching slots are left alone.
// the search mark's colour (search_mark_color, parsed again only when the key's text changes):
// the frame opaque, the wash the same colour at 35 %
const float kSearchWashAlpha = 0.35f;
char g_markColorSeen[sizeof(UtConfig::searchMarkColor)] = "";
bool g_markColorInit = false;
TqColor g_markColor = {1.0f, 0.843f, 0.0f, 1.0f};   // gold #ffd700
// GD's owned/unowned look - an UNOWNED prototype is dimmed by a translucent dark quad over
// its cells (the pad ground colour). 62 % left 0.38 of the icon plus 0.03 -
// a mid-dark TQ icon (sRGB 0.35) came out at 0.17, near black; 45 % leaves 0.55 of it plus 0.03
// (0.22), still clearly darker than a collected one.
const TqColor kUnownedDim = {0.055f, 0.059f, 0.067f, 0.45f};
const TqColor kHaveTick = {0.847f, 0.784f, 0.588f, 0.90f};   // the light "have one" tick
// owned_marks=2 - a thin frame instead of the veil (still readable if the canvas
// ignores alpha; the icon stays whole).
const TqColor kUnownedFrame = {0.420f, 0.439f, 0.471f, 1.00f};   // #6b7078
// the slot plate so every item fills its slot. the grid cell's own inside
// (window (62,162) 24x24 mean), OPAQUE, drawn inside the slot's outer cell edge (2 px of the
// cell's dark flank left / top, its flank + light hairline 0xFF928F87 right / bottom stay): the
// slot reads as ONE engine cell of the slot's size - the plate edge is the texture's own.
const TqColor kPlateFill = {0.180f, 0.165f, 0.122f, 1.00f};   // 0xFF2E2A1F

int g_hover = -1;
// where the frame came from, and the page-draw route's state.
enum { kFrameSrcHover = 0, kFrameSrcDraw = 1 };
const char* const kFrameSrcText[2] = {"the page mouse handler's parent origin",
                                      "the Transfer page draw's page origin"};
int g_frameSrc = kFrameSrcHover;
float g_frameOrgX = 0.0f, g_frameOrgY = 0.0f;   // the origin the accepted frame was derived from
bool g_drawRouteOff = false;   // the hover disagreed with the draw once: the hover only, this session
bool g_drawAgreeSaid = false;
int g_drawWhySaid = -1;
int g_drawStable = 0;          // frames in a row with the same page point (kUtDrawStableFrames)
unsigned long long g_drawFrameNo = ~0ull;
float g_drawPX = 0.0f, g_drawPY = 0.0f, g_drawGX = 0.0f, g_drawGY = 0.0f;
bool g_cursorOk = false;          // the cursor in canvas px this frame (it hides the marks)
float g_cursorX = 0.0f, g_cursorY = 0.0f;
int g_down = -1;          // the button a LDOWN landed on (its LUP acts)
DWORD g_drawnAt = 0;      // GetTickCount of the last frame the pad painted
// the pad is drawn by the Transfer page draw's POST (under the engine's tooltip, which is
// drawn after the page); the Present detour draws it only when the POST did not in the last 250 ms
// (the plates' rule). The clicks never read either draw (layout() + the WndProc subclass).
DWORD g_padPageAt = 0;    // the last POST that drew the pad
bool g_padByPage = false;  // the POST drew the pad THIS frame (read by Present)
DWORD g_layoutAt = 0;     // the last Present that laid the pad out (the POST draws a fresh layout only)
int g_padPresentRun = 0;  // Present-drawn pad frames in a row (the fallback line after 120)
bool g_padPageSaid = false, g_padPresentSaid = false;
// the notch accumulators (utWheelAccumulate): the Ctrl cycle and the plain wheel
UtWheelAcc g_wheelCtrl, g_wheelPlain;
volatile LONG g_padOff = 0;
const void* g_font = nullptr;
bool g_fontTried = false;
float g_canvasPerClientX = 1.0f, g_canvasPerClientY = 1.0f;
bool g_perClientOk = false;   // panelCursorNow scales once panelDraw measured it
PlateGeometry g_geo;
bool g_geoSaid = false;
char g_status[96] = "pad=idle";

TqCanvas* canvasNow() {
    TqEngine* e = engine();
    if (!e || !g_tq.EngineGetGraphicsEngine || !g_tq.GfxGetCanvas) return nullptr;
    TqGraphicsEngine* gfx = g_tq.EngineGetGraphicsEngine(e);
    return gfx ? g_tq.GfxGetCanvas(gfx) : nullptr;
}

void fillRect(TqCanvas* c, float x, float y, float w, float h, const TqColor& col) {
    TqRect r = {x, y, w, h};
    g_tq.CanvasRenderRect(c, &r, &col, nullptr, nullptr);
}

void outlineRect(TqCanvas* c, float x, float y, float w, float h, float t, const TqColor& col) {
    fillRect(c, x, y, w, t, col);
    fillRect(c, x, y + h - t, w, t, col);
    fillRect(c, x, y, t, h, col);
    fillRect(c, x + w - t, y, t, h, col);
}

void text(TqCanvas* c, float x, float y, const TqColor& col, const char* s, int size) {
    if (!g_font || !g_tq.CanvasRenderTextA || !s || !*s) return;
    g_tq.CanvasRenderTextA(c, (int)x, (int)y, &col, s, g_font, size, 0, 0, 0u, 0, 0, 0);
}

// The engine's std::string (VS2012 x86, 0x18 bytes) over TEXT without the CRT's allocator: the short
// form in the object's own buffer (n < 16), else the heap form pointing at the caller's static
// buffer (capacity = size) - the engine only reads it during the call. False when it does not fit.
// One helper for the font and the slot art.
bool tqStdStringOver(TqStdString* s, char* heap, size_t heapSize, const char* text, size_t n) {
    memset(s, 0, sizeof(*s));
    if (n < 16) {
        memcpy(s->buf, text, n);
        s->res = 15;
    } else {
        if (!heap || n + 1 > heapSize) return false;
        memcpy(heap, text, n);
        heap[n] = 0;
        s->ptr = heap;
        s->res = (unsigned)n;
    }
    s->size = (unsigned)n;
    return true;
}

// The font by NAME, once per world (TQ LoadFont(name, override=true, flag=false): the engine's own
// callers' arguments). An unknown name faults inside the engine: caught, said once, no captions.
void ensureFont() {
    if (g_font || g_fontTried || !g_tq.GfxLoadFont) return;
    g_fontTried = true;
    TqEngine* e = engine();
    if (!e || !g_tq.EngineGetGraphicsEngine) return;
    TqStdString name;
    const size_t n = strnlen(g_cfg.fontName, sizeof(g_cfg.fontName) - 1);
    if (n == 0 || n >= 16 * 16) return;
    static char heap[256];
    if (!tqStdStringOver(&name, heap, sizeof(heap), g_cfg.fontName, n)) return;
    const void* f = nullptr;
    utGuardEnter();
    __try {
        TqGraphicsEngine* gfx = g_tq.EngineGetGraphicsEngine(e);
        if (gfx) f = g_tq.GfxLoadFont(gfx, &name, 1u, 0u);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        f = nullptr;
    }
    utGuardLeave();
    g_font = f;
    if (f) {
        logI("panel: font \"%s\" loaded by name (%p)", g_cfg.fontName, f);
    } else {
        logW("panel: font \"%s\" could not be loaded - the pad draws without captions",
             g_cfg.fontName);
    }
}

// ---- the live frame and the pad's rects ----------------------------------------------
// The frame verdict for the current window geometry (canvas size + UI scale): 0 unmeasured, 1
// accepted (the pad follows g_frame), -1 refused (no pad / label while ON).
int g_frameVerdict = 0;
UtRectF g_frame = {0.0f, 0.0f, 0.0f, 0.0f};
UtRectF g_frameGrid = {0.0f, 0.0f, 0.0f, 0.0f};
int g_frameGeoW = 0, g_frameGeoH = 0;
float g_frameGeoScale = 0.0f;
// the frame follows the window (ut_padlayout.h utFrameTrackStep), and the frame
// logged last in full (the same frame measured again is a DEBUG line).
UtFrameTrack g_track = {0, 0.0f, 0.0f, 0, false};
UtRectF g_frameLogged = {0.0f, 0.0f, 0.0f, 0.0f};
bool g_frameLoggedOk = false;
// The pad as laid out this frame (layout() is the only writer; draw and click both read it).
UtRectF g_btnR[kUtPadMax];
bool g_btnShown[kUtPadMax];
UtRectF g_fieldTextR;   // the search field's text area (the field less its clear square and a gap)
bool g_groundOk = false, g_labelOk = false;
UtRectF g_ground = {0.0f, 0.0f, 0.0f, 0.0f};
UtRectF g_labelR = {0.0f, 0.0f, 0.0f, 0.0f};
int g_padBandSaid = -99;   // the last band logged (once per change, never per frame)
const float kPageRecordX = 0.0f;   // TransferWindow.dbr WindowLocationX (no ini key: always 0)

void hideAll() {
    for (int i = 0; i < kUtPadMax; ++i) g_btnShown[i] = false;
    g_groundOk = false;
    g_labelOk = false;
}

// THE geometry: draw and click both read g_btnR / g_btnShown, which only this function writes.
void layout(const PlateGeometry& g, bool on) {
    hideAll();
    if (g_frameVerdict == 1) {
        UtPadIn in;
        in.frame = g_frame;
        in.grid = g_frameGrid;
        in.scale = g.scale;
        in.canvasW = (float)g.canvasW;
        in.canvasH = (float)g.canvasH;
        in.dx = g_cfg.padX;
        in.dy = g_cfg.padY;
        in.cellH = g_cfg.padH;
        in.gap = g_cfg.padGap;
        // the field whenever search=1 and the view is ON, or the real Transfer page is shown with
        // search_transfer=1 - drawn disabled while it cannot take keys, so row 2 never changes its
        // shape
        in.search = utPadFieldLaidOut(on, g_cfg.search, g_cfg.searchTransfer);
        UtPadOut o;
        const int band = utPadLayout(in, &o);
        if (band != g_padBandSaid) {   // said once per change (a new frame, an ini edit)
            g_padBandSaid = band;
            // ONE drawn cell - the grid / 16, the mod sack's live cell, the engine's formula
            float cell = 0.0f;
            const bool cellOk = panelCellPx(&cell);
            if (!cellOk) cell = g_frameGrid.w / (float)kUtPadGridCols;
            if (band > 0) {
                logI("panel: pad in the band below the grid at (%.0f,%.0f) %.0fx%.0f, cell %.0f px, "
                     "scale %.3f - rows %.0f px (pad_h %d record px), gap %.0f px, margin %.0f px, "
                     "pad_y %.0f px (%s), group cells %d record px - inside the frame %.0f..%.0f x "
                     "%.0f..%.0f%s, clear of the grid %.0f..%.0f x %.0f..%.0f",
                     o.ground.x, o.ground.y, o.ground.w, o.ground.h, cell, g.scale, o.rowPx,
                     g_cfg.padH, o.gapPx, o.marginPx, o.dyPx, utPadFitText(o.fit), o.cellW,
                     g_frame.x, g_frame.x + g_frame.w, g_frame.y, g_frame.y + g_frame.h,
                     band == kUtPadBorder ? " (NOT: over its bottom border)" : "", g_frameGrid.x,
                     g_frameGrid.x + g_frameGrid.w, g_frameGrid.y, g_frameGrid.y + g_frameGrid.h);
                if (o.fit >= kUtPadFitBelowFloor)
                    logW("panel: the band below the grid is %.1f px at UI scale %.3f - the pad's rows "
                         "are %.0f px, under GD's floor of %d record px (%.0f px): %s; the captions "
                         "use a shorter font",
                         g_frame.y + g_frame.h - (g_frameGrid.y + g_frameGrid.h), g.scale, o.rowPx,
                         kUtPadRowFloor, utPadRound((float)kUtPadRowFloor * g.scale),
                         utPadFitText(o.fit));
            } else {
                logW("panel: no room for the pad even over the measured frame's bottom border "
                     "(%.0f,%.0f) %.0fx%.0f below the grid (%.0f,%.0f) %.0fx%.0f on the %dx%d canvas "
                     "(pad_x %d, pad_y %d, pad_h %d, pad_gap %d, scale %.3f) - no pad and no label "
                     "while the view is ON",
                     g_frame.x, g_frame.y, g_frame.w, g_frame.h, g_frameGrid.x, g_frameGrid.y,
                     g_frameGrid.w, g_frameGrid.h, g.canvasW, g.canvasH, g_cfg.padX, g_cfg.padY,
                     g_cfg.padH, g_cfg.padGap, g.scale);
            }
            unsigned sw = 0, sh = 0;
            if (protoCellSize(&sw, &sh)) {
                const float eng = utEngineCellPx(g.scale);
                if (cellOk) {
                    logI("panel: one drawn cell for the page: %.0f px = the grid %.0f / 16 = the mod "
                         "sack's live cell %ux%u%s floorf(32 x %.3f + 0.5) = %.0f (the engine's "
                         "0x198C40); every cells-to-px turn uses it",
                         cell, g_frameGrid.w, sw, sh, cell == eng ? " =" : " - NOT", g.scale, eng);
                } else {
                    logW("panel: the grid %.0fx%.0f / 16 x 15 is not the mod sack's live cell %ux%u "
                         "(%s; the engine's floorf(32 x %.3f + 0.5) = %.0f) - the rect route and the "
                         "per-slot overlays (plates, ring, veils, grid cover) are refused and the "
                         "marks wait for the proven hovers; the pad stays",
                         g_frameGrid.w, g_frameGrid.h, sw, sh,
                         utDrawnCellWhyText(utDrawnCell(g_frameGrid.w, g_frameGrid.h, (float)sw,
                                                        (float)sh, nullptr)),
                         g.scale, eng);
                }
            }
        }
        if (band > 0) {   // the whole pad, ON or OFF - "Transfer" and the group list
            const int groups = liveGroupCount();
            for (int i = 0; i < kUtPadMax; ++i) g_btnR[i] = o.btn[i];
            g_btnShown[kUtPadToggle] = true;
            g_btnShown[kUtPadPrev] = true;
            g_btnShown[kUtPadNext] = true;
            g_btnShown[kUtPadOwn] = true;
            g_btnShown[kUtPadSearch] = in.search;
            g_btnShown[kUtPadClear] = in.search;
            g_fieldTextR = o.fieldText;
            for (int i = 0; i < kUtPadGroups; ++i) g_btnShown[kUtPadGroup0 + i] = i < groups;
            g_ground = o.ground;
            g_groundOk = true;
            g_labelR = o.label;
            g_labelOk = true;
        }
        (void)on;
        return;   // no room: nothing (there is no lone toggle any more)
    }
    // unmeasured or refused: NOTHING is drawn (the frame comes from the first mouse move over
    // the Transfer grid, the real page or the collection alike).
    (void)on;
}

int hit(float x, float y) {
    // the clear square first: it lies inside the field (and is the field's own target while empty)
    if (g_btnShown[kUtPadClear] && utRectHas(g_btnR[kUtPadClear], x, y)) return kUtPadClear;
    for (int i = 0; i < kUtPadMax; ++i) {
        if (i != kUtPadClear && g_btnShown[i] && utRectHas(g_btnR[i], x, y)) return i;
    }
    return -1;
}

// -------------------------------------------------------------------------------------------
DWORD g_padPressAt = 0;         // the last LDOWN on a pad button (a pad click is never a page drop)
DWORD g_platesAt = 0;           // 4(b): the last frame the page-draw PRE-detour drew the plates
unsigned long long g_plateFrame = ~0ull;
bool g_platesSaid = false, g_plateCanvasSaid = false, g_frameFallbackSaid = false;
// the page draw's per-frame call pattern (the frame = hookFrameCount, constant between two
// Presents). Once the same pattern held for kPdStable frames, the LAST call of a frame (its count
// and pass as in the previous frame) draws the veils AFTER the original: over the items, under
// whatever the engine draws later (the tooltip). g_veilsByPage tells the Present detour.
const int kPdStable = 30;
unsigned long long g_pdFrame = ~0ull;
int g_pdCalls = 0, g_pdLastPass = -99, g_pdFirstPass = -99;
int g_pdPrevCalls = 0, g_pdPrevLastPass = -99;
int g_pdStable = 0;
bool g_pdSaid = false, g_veilsByPage = false, g_veilsPageSaid = false;
bool g_realMarksByPage = false;   // the real Transfer page's search marks drawn by the POST

// 4(b) fallback: a thin frame per slot, drawn from the Present detour (over the items), whenever
// the page-draw plates did not run in the last 250 ms (no signature, or a canvas mismatch).
void drawPlateFrames(TqCanvas* c) {
    if (g_cfg.slotPlates == 0) return;   // slot_plates=0 - no ground at all
    if ((g_cfg.slotPlates == 1 || g_cfg.slotPlates == 3) && g_platesAt &&
        GetTickCount() - g_platesAt < 250)
        return;   // 3 (the slot art) falls back like 1
    UtRectF gr;
    if (!panelSlotGrid(&gr)) return;   // not on a grid the items do not share
    const int n = protoCount();
    for (int i = 0; i < n; ++i) {
        int col = 0, row = 0, w = 0, h = 0;
        if (!protoSlotAt(i, &col, &row, &w, &h) || w < 1 || h < 1) continue;
        const UtRectF r = utRectInset(utSlotDrawnRect(gr, col, row, w, h), 1.0f);
        outlineRect(c, r.x, r.y, r.w, r.h, 1.0f, kLineTop);
    }
    if (!g_frameFallbackSaid && n > 0) {
        g_frameFallbackSaid = true;
        logI("panel: slot plates as thin frames from the Present detour (the page-draw PRE-detour "
             "did not draw them: %s)", g_cfg.slotPlates == 2 ? "slot_plates=2 in uniquetab.ini"
             : g_tq.sigPageDrawRva ? "no call on this page with the engine canvas"
                                   : "exe.transferPageDraw not confirmed");
    }
}

// A lamp (GD's LED) at the left of a toggle; returns where its caption starts.
float lamp(TqCanvas* c, const UtRectF& b, bool lit, float s) {
    const float led = utPadRound(b.h * 0.4f);
    const float lx = b.x + utPadRound(4.0f * s);
    fillRect(c, lx, b.y + utPadRound((b.h - led) * 0.5f), led, led, lit ? kLedOn : kLedOff);
    return lx + led + utPadRound(4.0f * s);
}

bool grayDrawn(int i);   // the rect route handed prototype i's icon draw its gray copy

// The owned marks (the veil or the thin frame per unowned slot, then the have-ticks). Called by the
// page draw's POST (under the tooltip) or, when that did not run this frame, by the Present detour.
// style 3 (the icon in gray) draws the veil only where the gray icon was not drawn.
void drawMarks(TqCanvas* c, int markStyle) {
    const int n = protoCount();
    for (int i = 0; i < n; ++i) {
        float mx = 0.0f, my = 0.0f, mw = 0.0f, mh = 0.0f;
        if (!ownedMarkRect(i, &mx, &my, &mw, &mh)) continue;
        if (markStyle == 3 && grayDrawn(i)) continue;
        if (mx < 0.0f || my < 0.0f || mx + mw > (float)g_geo.canvasW ||
            my + mh > (float)g_geo.canvasH)
            continue;
        if (markStyle == 2) {
            const float ft = g_geo.scale >= 1.5f ? 2.0f : 1.0f;
            outlineRect(c, mx + ft, my + ft, mw - 2.0f * ft, mh - 2.0f * ft, ft, kUnownedFrame);
        } else {
            fillRect(c, mx, my, mw, mh, kUnownedDim);
        }
    }
    // GD's "have one somewhere" hint - a small light tick in the slot's corner of an
    // uncollected record the player already holds (ini have_marks).
    for (int i = 0; i < n; ++i) {
        float mx = 0.0f, my = 0.0f, mw = 0.0f, mh = 0.0f;
        if (!ownedHaveRect(i, &mx, &my, &mw, &mh)) continue;
        if (mx < 0.0f || my < 0.0f || mx + mw > (float)g_geo.canvasW ||
            my + mh > (float)g_geo.canvasH)
            continue;
        fillRect(c, mx, my, mw, mh, kHaveTick);
    }
}

TqColor searchMarkColour(bool wash) {
    if (!g_markColorInit || strcmp(g_markColorSeen, g_cfg.searchMarkColor) != 0) {
        g_markColorInit = true;
        _snprintf_s(g_markColorSeen, sizeof(g_markColorSeen), _TRUNCATE, "%s", g_cfg.searchMarkColor);
        unsigned char r = 0xff, g = 0xd7, b = 0x00;
        if (!utColorParse(g_cfg.searchMarkColor, &r, &g, &b)) utColorParse(kUtColorDefault, &r, &g, &b);
        g_markColor = TqColor{(float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f, 1.0f};
    }
    TqColor col = g_markColor;
    if (wash) col.a = kSearchWashAlpha;
    return col;
}

// The search's highlight (search_mark: 1 the frame, 2 the wash, 3 both, in search_mark_color) on
// every slot whose record holds the query; nothing is dimmed or hidden. frame = false: the wash,
// drawn by the page draw's PRE (over the slot art, under the tint ring and the icon); true: the
// frame on the slot's outermost pixel ring, after the last page draw (over the veils, under the
// tooltip), or by the Present detour in a frame where that did not run.
void drawRealSearchMarks(TqCanvas* c);

void drawSearchMarks(TqCanvas* c, bool frame) {
    if (!viewOn()) {   // the real Transfer page: its items' rects, the frame pass only
        if (frame) drawRealSearchMarks(c);
        return;
    }
    if (!(g_cfg.searchMark & (frame ? 1 : 2)) || !searchQueryStands()) return;
    UtRectF gr;
    if (!panelSlotGrid(&gr)) return;   // not on a grid the items do not share
    const float cell = gr.w / (float)kUtPadGridCols;
    const float t = utSearchMarkThick(cell);
    const TqColor col = searchMarkColour(!frame);
    const int n = protoCount();
    for (int i = 0; i < n; ++i) {
        const char* rec = nullptr;
        int sc = 0, sr = 0, sw = 0, sh = 0;
        if (!protoAt(i, &rec, nullptr, nullptr, nullptr, nullptr, nullptr) ||
            !protoSlotAt(i, &sc, &sr, &sw, &sh) || sw < 1 || sh < 1 || !searchHighlight(i, rec))
            continue;
        const UtRectF r = utSearchMarkRect(utSlotDrawnRect(gr, sc, sr, sw, sh), !frame);
        if (frame) outlineRect(c, r.x, r.y, r.w, r.h, t, col);
        else fillRect(c, r.x, r.y, r.w, r.h, col);
    }
}

// The real Transfer page (the view OFF, search_transfer=1): the same highlight on each item of the
// real sack whose text holds the query - the item's own rect in the sack (drawn px from the grid's
// top-left) put on the measured grid; an item whose rect leaves the grid is never marked. The page
// draw's PRE does not run OFF, so the wash (search_mark 2 or 3) is drawn here too, under the frame
// (over the icon, at the wash's 35 %).
UtSackEntry g_realMarkBuf[kUtSearchRealMax];
void drawRealSearchMarks(TqCanvas* c) {
    if (!(g_cfg.searchMark & 3) || !g_cfg.searchTransfer || !searchQueryStands()) return;
    UtRectF gr;
    if (!panelSlotGrid(&gr)) return;   // not on a grid the items do not share
    const int n = searchRealMarks(g_realMarkBuf, kUtSearchRealMax);
    if (n <= 0) return;
    const float t = utSearchMarkThick(gr.w / (float)kUtPadGridCols);
    const TqColor wash = searchMarkColour(true), col = searchMarkColour(false);
    for (int i = 0; i < n; ++i) {
        const UtSackEntry& e = g_realMarkBuf[i];
        UtRectF slot;
        if (!utSearchRealRect(gr, e.x, e.y, e.w, e.h, &slot)) continue;
        if (g_cfg.searchMark & 2) {
            const UtRectF r = utSearchMarkRect(slot, true);
            fillRect(c, r.x, r.y, r.w, r.h, wash);
        }
        if (g_cfg.searchMark & 1) {
            const UtRectF r = utSearchMarkRect(slot, false);
            outlineRect(c, r.x, r.y, r.w, r.h, t, col);
        }
    }
}

// The search field (search=1; the view ON, or OFF with search_transfer=1): a sunken cell with the query's tail in its text area, a
// blinking caret while it has the focus, a dim "search" while it is empty and has none, and the
// clear button's "x" centred in its square while the field holds text. While the field cannot take
// the focus (the search off after a fault, no key gate, the collection not active) it is drawn
// disabled: the ground, the idle outline and a dim "search off".
void drawField(TqCanvas* c, const UtRectF& b, const UtRectF& sq, const UtRectF& area, int fs,
               float t) {
    const bool enabled = searchFieldWanted();
    const bool focused = enabled && searchFieldFocused();
    const bool hovered = enabled && (g_hover == kUtPadSearch || g_hover == kUtPadClear);
    fillRect(c, b.x, b.y, b.w, b.h, kPlateFill);
    outlineRect(c, b.x, b.y, b.w, b.h, t, focused ? kTglOnTop : (hovered ? kRing : kLineTop));
    if (fs < 6) return;
    const float pad = t + 2.0f;
    const float ty = b.y + utPadRound((b.h - (float)fs) * 0.5f);
    if (!enabled) {
        text(c, area.x + pad, ty, kCaptionDim, "search off", fs);
        return;
    }
    wchar_t q[kUtSearchFieldMax + 1];
    const int n = searchFieldText(q, kUtSearchFieldMax + 1);
    if (n > 0)   // the clear button
        text(c, sq.x + utPadRound((sq.w - utPadTextWidth("x", fs)) * 0.5f), ty,
             g_hover == kUtPadClear ? kCaption : kCaptionDim, "x", fs);
    if (n == 0 && !focused) {
        text(c, area.x + pad, ty, kCaptionDim, "search", fs);
        return;
    }
    if (!g_font || !g_tq.CanvasRenderTextW) return;
    // the tail that fits the text area (the pad's conservative width estimate), one place kept for
    // the caret
    const int fit = (int)((area.w - 2.0f * pad) / ((float)fs * kUtPadEmPerChar)) - 1;
    wchar_t line[kUtSearchFieldMax + 2];
    int m = 0;
    for (int i = utSearchFieldTailStart(n, fit); i < n; ++i) line[m++] = q[i];
    if (focused && ((GetTickCount() / 500u) & 1u) == 0u) line[m++] = L'_';   // 500 ms
    line[m] = 0;
    if (m > 0)
        g_tq.CanvasRenderTextW(c, (int)(area.x + pad), (int)ty, &kCaption, line, g_font, fs, 0, 0,
                               0u, 0, 0, 0);
}

// A click on the field or on its clear square. The square clears a standing query through the
// same edit as Esc (the focus stays as it was); on an empty field it is the field's own target.
// A field that cannot take the focus says why, once a session.
void fieldClick(bool square) {
    if (!searchFieldWanted()) {
        searchFieldOffClick();
        return;
    }
    wchar_t q[kUtSearchFieldMax + 1];
    if (square && searchFieldText(q, kUtSearchFieldMax + 1) > 0) {
        searchFieldClearQuery();
        return;
    }
    searchFieldFocus();
}

bool drawPad(TqCanvas* c);

// pad = false when the page draw's POST drew the pad in the last 250 ms (the marks and
// the plate frames stay here as before).
bool drawInner(TqCanvas* c, bool pad) {
    const bool on = viewOn();   // layout() ran in panelDraw, before the hover test
    // the owned marks first (under the pad). Nothing is allocated; ut_owned decides whether
    // the set and the grid origin are trusted, and keeps every rect inside the measured 16 x 15 grid.
    // the clip is the canvas, no longer the records' page rect - the engine put the grid's
    // top at window y + 111, above the records' page top (+126), so row 0 would never be marked.
    if (on) drawPlateFrames(c);   // only when the page-draw plates did not run
    // the veils were drawn by the page draw's POST this frame (under the tooltip): not
    // again here. The flag is consumed by every Present, so it never outlives its frame.
    const bool byPage = g_veilsByPage;
    g_veilsByPage = false;
    const bool realByPage = g_realMarksByPage;
    g_realMarksByPage = false;
    if (!on && !realByPage) drawSearchMarks(c, true);   // the real page's marks (the fallback)
    const int markStyle =
        (on && !byPage) ? ownedMarksBegin(g_geo, g_cursorOk, g_cursorX, g_cursorY) : 0;
    if (markStyle) drawMarks(c, markStyle);
    if (on && !byPage) drawSearchMarks(c, true);   // the frame, when the POST did not draw it
    return pad ? drawPad(c) : true;
}

// The pad itself: the ground, the buttons, the lamps and the label, from the rects layout() wrote.
// Drawn by the Transfer page draw's POST (panelPageDrawPost, under the tooltip) or, as the
// fallback, by the Present detour (drawInner).
bool drawPad(TqCanvas* c) {
    const bool on = viewOn();
    if (!g_groundOk) return true;   // a refused frame while ON: no pad, no label
    const float s = g_geo.scale;
    const float t = s >= 1.5f ? 2.0f : 1.0f;
    ensureFont();
    fillRect(c, g_ground.x, g_ground.y, g_ground.w, g_ground.h, kPadBack);
    // One caption size for the whole pad (GD: fitted to the smallest cell): the group cells'
    // widest short name, OWN's three letters at least. The Transfer toggle shortens its caption
    // to fit beside its lamp (utPadToggleCaption); the pad's size is never cut for it.
    const UtRectF& g0 = g_btnR[kUtPadGroup0];
    int widest = 3;
    for (int i = 0; i < kUtPadGroups; ++i) {
        const int n = utPadStrLen(utPadShortName(liveGroupLabel(i)));
        if (g_btnShown[kUtPadGroup0 + i] && n > widest) widest = n;
    }
    const int fs = utPadCaptionSize(g0.w, g0.h, widest, s);
    // the label font = plate_label_size x the UI scale (utPlateLabelPx), cut to the row -
    // the LABEL row's own height rule, not the group caption size
    const int labelFs = utPadLabelFont(utPlateLabelPx(g_cfg.plateLabelSize, s), g_labelR.h);
    const int shownGroup = liveShownGroup();
    const bool ownOn = liveOwnedOnly();
    // the search: the marked groups (0 while no query stands or search_buttons=0), and whether
    // the unmarked group captions are dimmed (never with search_buttons=0; only a group whose
    // index is complete - one still being indexed may yet hold a match)
    const unsigned marks = on ? searchMarks() : 0u;
    const bool queryOn = on && searchQueryStands() && g_cfg.searchButtons;
    const unsigned indexedGroups = queryOn ? liveSearchIndexed() : 0u;
    for (int i = 0; i < kUtPadMax; ++i) {
        if (!g_btnShown[i] || i == kUtPadSearch || i == kUtPadClear) continue;   // the field: below
        const UtRectF& b = g_btnR[i];
        const bool isToggle = i == kUtPadToggle || i == kUtPadOwn;
        // lit = what the page shows - "Transfer" while OFF, the shown group while ON.
        const bool isGroup = i >= kUtPadGroup0 && i < kUtPadGroup0 + kUtPadGroups;
        const bool lit = (i == kUtPadToggle && !on) || (i == kUtPadOwn && ownOn) ||
                         (isGroup && on && i - kUtPadGroup0 == shownGroup);
        // a marked group shows the mark's plate unless it is being pressed; then it is exactly the
        // button it is without the search
        const bool marked =
            isGroup && (marks & (1u << (unsigned)(i - kUtPadGroup0))) != 0 && g_down != i;
        const bool litFace = lit && !marked;
        const TqColor& bg = marked ? (g_hover == i ? kMarkHover : kMarkBg)
                            : litFace
                                ? kTglOnBg
                                : (g_down == i ? kBtnDown
                                               : (g_hover == i ? kBtnHover
                                                               : (isToggle ? kTglBg : kBtnBg)));
        fillRect(c, b.x, b.y, b.w, b.h, bg);
        if (b.h > 2.0f * t) {
            fillRect(c, b.x, b.y, b.w, t, litFace ? kTglOnTop : kLineTop);
            fillRect(c, b.x, b.y + b.h - t, b.w, t, litFace ? kTglOnBot : kLineBot);
        }
        if (i >= kUtPadGroup0 && i < kUtPadGroup0 + kUtPadGroups && on &&
            i - kUtPadGroup0 == shownGroup)
            outlineRect(c, b.x, b.y, b.w, b.h, t, kRing);   // GD's inset ring: no size change
        const char* cap = i == kUtPadToggle ? utPadToggleCaption(b.w, b.h, s, fs)
                          : i == kUtPadOwn  ? "OWN"
                          : i == kUtPadPrev ? "<"
                          : i == kUtPadNext ? ">"
                                            : utPadShortName(liveGroupLabel(i - kUtPadGroup0));
        if (fs < 6) continue;   // too small for any caption: the button stays unlabelled
        float tx;
        if (i == kUtPadToggle) {   // the lamp; OWN shows its state by its lit face alone
            tx = lamp(c, b, lit, s);
        } else {   // centred on the conservative width estimate, never left of the button
            tx = b.x + utPadRound((b.w - utPadTextWidth(cap, fs)) * 0.5f);
            if (tx < b.x + t) tx = b.x + t;
        }
        const unsigned bit = isGroup ? 1u << (unsigned)(i - kUtPadGroup0) : 0u;
        const bool dim = queryOn && isGroup && !litFace && (indexedGroups & bit) != 0 &&
                         (marks & bit) == 0;
        text(c, tx, b.y + utPadRound((b.h - (float)fs) * 0.5f),
             litFace ? kTglOnText : (marked ? kMarkCaption : (dim ? kCaptionDim : kCaption)), cap,
             fs);
    }
    if (g_btnShown[kUtPadSearch])   // ON, or OFF with search_transfer=1 (layout decides)
        drawField(c, g_btnR[kUtPadSearch], g_btnR[kUtPadClear], g_fieldTextR, fs, t);
    if (!on && g_labelOk && g_cfg.plateLabel && labelFs >= 6) {   // the real page is shown
        // "the real Transfer page", and with a query "found N" / "reading k/n" over its items
        char found[40], brief[24], line[96];
        found[0] = brief[0] = 0;
        if (g_btnShown[kUtPadSearch]) searchRealLabel(found, sizeof(found), brief, sizeof(brief));
        const int lfs = utPadLabelRealLine(found, brief, g_labelR.w, labelFs, line, sizeof(line));
        text(c, g_labelR.x, g_labelR.y + utPadRound((g_labelR.h - (float)lfs) * 0.5f), kCaption,
             line, lfs);
        return true;
    }
    if (on && g_labelOk && g_cfg.plateLabel && labelFs >= 6) {   // the ini key is honoured
        char line[128];
        const int g = liveShownGroup();
        UtPadLabelWords w;
        w.group = liveGroupLabel(g);
        w.known = ownedLabel(&w.owned, &w.total);   // owned / total for the group
        if (!w.known) w.total = liveGroupEntries(g);
        w.allKnown = ownedLabelAll(&w.allOwned, &w.allTotal);   // the whole collection
        // the window's slot rows, "rows 3-7 / 22" (1-based, the last clipped to the
        // group's rows); a group that fits one window says "rows 1-3 / 3".
        const int r0 = liveShownPage() + 1, rows = liveShownRows();
        int r1 = liveShownPage() + liveWindowRows();
        if (r1 > rows) r1 = rows;
        if (r1 < r0) r1 = r0;
        char span[32];   // an empty (OWN) group says "rows 0 / 0"
        if (rows > 0) _snprintf_s(span, sizeof(span), _TRUNCATE, "%d-%d", r0, r1);
        else _snprintf_s(span, sizeof(span), _TRUNCATE, "0");
        w.span = span;
        w.rows = rows;
        // "indexing k/1588" or "found N", and the short forms "k/1588" / "=N" for a narrow box;
        // both empty while no query stands
        char found[40], brief[24];
        searchLabel(found, sizeof(found), brief, sizeof(brief));
        w.found = found;
        w.brief = brief;
        const int lfs = utPadLabelLine(w, g_labelR.w, labelFs, line, sizeof(line));
        text(c, g_labelR.x, g_labelR.y + utPadRound((g_labelR.h - (float)lfs) * 0.5f), kCaption,
             line, lfs);
    }
    return true;
}

bool drawGuarded(TqCanvas* c, bool pad) {
    bool ok = false;
    utGuardEnter();
    __try {
        ok = drawInner(c, pad);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    utGuardLeave();
    return ok;
}

// the pad alone, for the page draw's POST (the same guard and kill switch).
bool padGuarded(TqCanvas* c) {
    bool ok = false;
    utGuardEnter();
    __try {
        ok = drawPad(c);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    utGuardLeave();
    return ok;
}

void updateHover(HWND h) {
    POINT pt;
    g_hover = -1;
    g_cursorOk = false;
    if (!h || !GetCursorPos(&pt) || !ScreenToClient(h, &pt)) return;
    g_cursorX = (float)pt.x * g_canvasPerClientX;
    g_cursorY = (float)pt.y * g_canvasPerClientY;
    g_cursorOk = true;
    g_hover = hit(g_cursorX, g_cursorY);
}

// A new canvas size or UI scale: the engine lays the window out again - measure it again.
void frameCheckGeometry(const PlateGeometry& g) {
    if (g_frameVerdict == 0) return;
    if (g.canvasW == g_frameGeoW && g.canvasH == g_frameGeoH && g.scale == g_frameGeoScale) return;
    // the draw's stability count restarts only on a REAL change (before, it was
    // zeroed on every Present while a frame was held, so the draw never cross-checked the hover).
    g_drawStable = 0;
    g_frameVerdict = 0;
    utFrameTrackReset(&g_track);
    g_padBandSaid = -99;
    logI("panel: the canvas or UI scale changed - the caravan frame is measured again (the Transfer "
         "page draw, or the next mouse move over the Transfer grid); no pad until then");
}

// the page was hidden (a tab change or a close). The next showing may lay the
// window out elsewhere, and while OFF no hover can measure it again - so the frame is forgotten: the
// Collection toggle alone (centred in the records' frame) until the next hover while ON.
void frameForget() {
    g_drawStable = 0;   // the next showing is measured from its own page draws
    if (g_frameVerdict == 0 && g_track.verdict == 0 && !g_track.unstable) return;
    g_frameVerdict = 0;
    utFrameTrackReset(&g_track);
}

}  // namespace

// the cursor NOW, at the hover - the page handler runs per mouse event and a
// cursor read at the last Present lags a moving mouse by up to a frame. Game thread (the handler
// and the Present detour share it), no allocation.
bool panelCursorNow(float* x, float* y) {
    if (!g_perClientOk) return false;
    HWND h = hookGameWindow();
    POINT pt;
    if (!h || !GetCursorPos(&pt) || !ScreenToClient(h, &pt)) return false;
    if (x) *x = (float)pt.x * g_canvasPerClientX;
    if (y) *y = (float)pt.y * g_canvasPerClientY;
    return true;
}

// The LIVE frame, decided once per window geometry from the
// first proven hover (ut_padlayout.h utFrameFromHover: parent origin - the page's record place;
// the measured grid must lie inside; the frame inside the canvas).
// the derivation shared by both routes (src = kFrameSrcHover / kFrameSrcDraw).
static void noteFrame(int src, float originX, float originY, float gridX, float gridY,
                      unsigned cellW, unsigned cellH) {
    g_track.verdict = g_frameVerdict;   // frameCheckGeometry / frameForget may have reset it
    const int step = utFrameTrackStep(&g_track, gridX, gridY);
    if (step == kUtFrameTrackUnstable) {
        g_frameVerdict = -1;
        g_frameLoggedOk = false;
        logW("panel: the caravan frame moved more than %d times in this showing of the page (the "
             "grid now at (%.1f,%.1f)) - REFUSED until the page is shown again: no pad and no label "
             "while the view is ON; the view itself is unaffected",
             kUtFrameMovesMax, gridX, gridY);
        return;
    }
    if (step != kUtFrameTrackDerive) return;
    PlateGeometry g;
    if (!plateGeometry(&g)) return;
    if (g_frameVerdict != 0)
        logI("panel: the grid moved from (%.1f,%.1f) to (%.1f,%.1f) - the caravan frame is derived "
             "again (move %d of at most %d in this showing of the page)",
             g_track.gridX, g_track.gridY, gridX, gridY, g_track.moves, kUtFrameMovesMax);
    UtFrameIn in;
    in.originX = originX;
    in.originY = originY;
    in.gridX = gridX;
    in.gridY = gridY;
    in.cellW = (float)cellW;
    in.cellH = (float)cellH;
    in.scale = g.scale;
    in.canvasW = (float)g.canvasW;
    in.canvasH = (float)g.canvasH;
    in.pageX = kPageRecordX;
    in.pageY = (float)g_cfg.pageY;
    in.frameW = (float)g_cfg.caravanW;
    in.frameH = (float)g_cfg.caravanH;
    UtRectF f = {0.0f, 0.0f, 0.0f, 0.0f}, gr = {0.0f, 0.0f, 0.0f, 0.0f};
    const int r = utFrameFromHover(in, &f, &gr);
    g_frameGeoW = g.canvasW;
    g_frameGeoH = g.canvasH;
    g_frameGeoScale = g.scale;
    utFrameTrackDecided(&g_track, r == kUtFrameOk ? 1 : -1, gridX, gridY);
    // a frame measured again (a new showing, a move) is logged in full only when
    // it differs from the one logged last; the same frame again is a DEBUG line.
    const bool same = r == kUtFrameOk && g_frameLoggedOk &&
                      !utFrameMoved(f.x, f.y, g_frameLogged.x, g_frameLogged.y) &&
                      !utFrameMoved(f.w, f.h, g_frameLogged.w, g_frameLogged.h);
    if (!same) g_padBandSaid = -99;
    if (r == kUtFrameOk) {
        g_frameSrc = src;
        g_frameOrgX = originX;
        g_frameOrgY = originY;
    }
    if (r == kUtFrameOk && same) {
        g_frame = f;
        g_frameGrid = gr;
        g_frameVerdict = 1;
        logD("panel: caravan window measured again at (%.0f,%.0f) %.0fx%.0f - unchanged", f.x, f.y,
             f.w, f.h);
    } else if (r == kUtFrameOk) {
        g_frame = f;
        g_frameGrid = gr;
        g_frameVerdict = 1;
        g_frameLogged = f;
        g_frameLoggedOk = true;
        logI("panel: caravan window measured at (%.0f,%.0f) %.0fx%.0f - %s (%.1f,%.1f) minus "
             "the Transfer page's record place (%.0f,%d) x %.3f; "
             "the grid (%.0f,%.0f) %.0fx%.0f (page pos (%.1f,%.1f)) lies inside it, the frame "
             "inside the %dx%d canvas; the records said (%.0f,%.0f) %.0fx%.0f (off by %+.0f,%+.0f)",
             f.x, f.y, f.w, f.h, kFrameSrcText[src], originX, originY, kPageRecordX,
             g_cfg.pageY, g.scale, gr.x, gr.y,
             gr.w, gr.h, gridX - originX, gridY - originY, g.canvasW, g.canvasH, g.winX, g.winY,
             g.winW, g.winH, f.x - g.winX, f.y - g.winY);
    } else {
        g_frameVerdict = -1;
        g_frameLoggedOk = false;
        logW("panel: the caravan frame derived from the proven hover is REFUSED (%s) - frame "
             "(%.0f,%.0f) %.0fx%.0f = parent origin (%.1f,%.1f) minus the page's record place "
             "(%.0f,%d) x %.3f, grid (%.0f,%.0f) %.0fx%.0f, canvas %dx%d - no pad and no label "
             "while the view is ON (the view hotkey, a tab change or closing the caravan turns it "
             "OFF); the view itself is unaffected",
             utFrameWhyText(r), f.x, f.y, f.w, f.h, originX, originY, kPageRecordX, g_cfg.pageY,
             g.scale, gr.x, gr.y, gr.w, gr.h, g.canvasW, g.canvasH);
    }
}

// The hover route: the fallback, and the cross-check of a frame the page draw gave.
void panelNoteHover(float originX, float originY, float gridX, float gridY, unsigned cellW,
                    unsigned cellH) {
    if (g_frameVerdict == 1 && g_frameSrc == kFrameSrcDraw) {
        if (!utFrameMoved(originX, originY, g_frameOrgX, g_frameOrgY) &&
            !utFrameMoved(gridX, gridY, g_frameGrid.x, g_frameGrid.y)) {
            if (!g_drawAgreeSaid) {
                g_drawAgreeSaid = true;
                logI("panel: the page mouse handler confirms the caravan frame taken from the Transfer "
                     "page draw (origin (%.1f,%.1f), grid (%.0f,%.0f))", originX, originY, gridX,
                     gridY);
            }
            return;
        }
        g_drawRouteOff = true;
        logW("panel: the page mouse handler's origin (%.1f,%.1f) / grid (%.0f,%.0f) differ from the "
             "Transfer page draw's (%.1f,%.1f) / (%.0f,%.0f) - the page-draw route is OFF for this "
             "session; the frame is measured from the hover again",
             originX, originY, gridX, gridY, g_frameOrgX, g_frameOrgY, g_frameGrid.x, g_frameGrid.y);
        g_frameVerdict = 0;
        utFrameTrackReset(&g_track);
        g_padBandSaid = -99;
    }
    noteFrame(kFrameSrcHover, originX, originY, gridX, gridY, cellW, cellH);
}

// the frame from the Transfer page draw's PRE-detour - the pad is there from
// the first frames the tab is shown, no mouse move needed. Once per frame (the first call); taken
// after kUtDrawStableFrames frames with the same page point; refused (the hover stays the route)
// when downsizing, in pass 1, or on an unusable number; switched off for the session when the
// hover ever disagrees. Reads only; nothing allocated.
static void drawFrameStep(void* page, const void* origin, int pass) {
    if (g_drawRouteOff || !g_cfg.groupButtons || InterlockedCompareExchange(&g_padOff, 0, 0)) return;
    if (!plateTransferVisible() || !viewAvailable()) return;
    const unsigned long long fr = hookFrameCount();
    if (fr == g_drawFrameNo) return;
    g_drawFrameNo = fr;
    float px = 0.0f, py = 0.0f, gx = 0.0f, gy = 0.0f;
    unsigned cw = 0, ch = 0;
    const int r = viewDrawOrigin(page, origin, pass, &px, &py, &gx, &gy, &cw, &ch);
    if (r != kUtDrawOk) {
        g_drawStable = 0;
        if (r != g_drawWhySaid) {
            g_drawWhySaid = r;
            logI("panel: no caravan frame from the Transfer page draw (%s, pass %d) - the pad waits "
                 "for the first mouse move over the Transfer grid", utDrawWhyText(r), pass);
        }
        return;
    }
    if (g_drawStable > 0 && !utFrameMoved(px, py, g_drawPX, g_drawPY) &&
        !utFrameMoved(gx, gy, g_drawGX, g_drawGY)) {
        if (g_drawStable < 1000000) ++g_drawStable;
    } else {
        g_drawStable = 1;
        g_drawPX = px;
        g_drawPY = py;
        g_drawGX = gx;
        g_drawGY = gy;
    }
    if (g_drawStable < kUtDrawStableFrames) return;
    // the frame this route gave, unchanged - nothing to do (a moved page point
    // falls through: noteFrame's tracker derives it again, as the hover route does).
    if (g_frameVerdict == 1 && g_frameSrc == kFrameSrcDraw &&
        !utFrameMoved(px, py, g_frameOrgX, g_frameOrgY) &&
        !utFrameMoved(gx, gy, g_frameGrid.x, g_frameGrid.y))
        return;
    if (g_frameVerdict == 1 && g_frameSrc == kFrameSrcHover) {   // the hover measured it first
        if (!utFrameMoved(px, py, g_frameOrgX, g_frameOrgY) &&
            !utFrameMoved(gx, gy, g_frameGrid.x, g_frameGrid.y)) {
            if (!g_drawAgreeSaid) {
                g_drawAgreeSaid = true;
                logI("panel: the Transfer page draw agrees with the page mouse handler's caravan frame "
                     "(origin (%.1f,%.1f), grid (%.0f,%.0f))", px, py, gx, gy);
            }
            return;
        }
        g_drawRouteOff = true;
        logW("panel: the Transfer page draw's origin (%.1f,%.1f) / grid (%.0f,%.0f) differ from the "
             "page mouse handler's (%.1f,%.1f) / (%.0f,%.0f) - the page-draw route is OFF for this "
             "session (the hover's frame stays)",
             px, py, gx, gy, g_frameOrgX, g_frameOrgY, g_frameGrid.x, g_frameGrid.y);
        return;
    }
    noteFrame(kFrameSrcDraw, px, py, gx, gy, cw, ch);
}

bool panelFrame(UtRectF* frame) {
    if (g_frameVerdict != 1) return false;
    if (frame) *frame = g_frame;
    return true;
}

bool panelFrameGrid(UtRectF* grid) {
    if (g_frameVerdict != 1) return false;
    if (grid) *grid = g_frameGrid;
    return true;
}

bool panelCellPx(float* cell) { return panelCellVerdict(cell) == 1; }

int panelCellVerdict(float* cell) {
    if (g_frameVerdict != 1) return 0;
    unsigned sw = 0, sh = 0;
    if (!protoCellSize(&sw, &sh)) return 0;   // unreadable (or no sack): no evidence
    return utDrawnCell(g_frameGrid.w, g_frameGrid.h, (float)sw, (float)sh, cell) == kUtCellOk ? 1
                                                                                             : -1;
}

bool panelSlotGrid(UtRectF* grid) {
    return panelFrameGrid(grid) && panelCellVerdict(nullptr) >= 0;
}

const char* panelFrameSourceText() {
    return kFrameSrcText[g_frameSrc == kFrameSrcDraw ? kFrameSrcDraw : kFrameSrcHover];
}

namespace {

// Ctrl+wheel / Ctrl+PgUp / Ctrl+PgDn walk the pad's own order - Transfer,
// G1 ... G15, Transfer ... (utGroupCycleStep, pure). "From" is the view's state INCLUDING a queued
// request, so a fast wheel does not repeat the same step. Transfer = the real page (view OFF).
bool cycleStep(int dir, int* fromOut, int* toOut) {
    const int n = liveGroupCount();
    const int from = viewPendingOn() ? liveWantedGroup() : kUtCycleTransfer;
    const int to = utGroupCycleStep(from, n, dir);
    if (fromOut) *fromOut = from;   // for the trace
    if (toOut) *toOut = to;
    if (to == kUtCycleNone) return false;
    if (to == kUtCycleTransfer) {
        viewRequest(kUtViewReqOff);
    } else {
        liveSelectGroup(to);
        viewRequest(kUtViewReqOn);
    }
    return true;
}

// ---- the Ctrl cycle's trace -----------------------------------------
// ONE DEBUG line per Ctrl+wheel and Ctrl+PgUp / Ctrl+PgDn the subclass sees - what the cycle did,
// or why it did nothing - never per frame. The plain wheel keeps its own line (ut_live).
const char* cycleName(int g) {
    return g == kUtCycleTransfer ? "Transfer" : g == kUtCycleNone ? "none" : liveGroupLabel(g);
}

void cycleIgnored(const char* what, const char* why) {
    logD("live: %s -> ignored (%s)", what, why);
}

// steps: signed, + = forward (wheel down, PgDn); each step starts from the state the previous one
// queued (cycleStep reads the queued request). Writes the trace; true = at least one step.
bool cycleSteps(const char* what, int steps) {
    const bool queued =
        viewPendingOn() != viewOn() || (viewOn() && liveWantedGroup() != liveShownGroup());
    const int k = steps < 0 ? -steps : steps;
    int first = kUtCycleNone, last = kUtCycleNone, done = 0;
    for (int i = 0; i < k && i < 32; ++i) {
        int f = kUtCycleNone, t = kUtCycleNone;
        const bool ok = cycleStep(steps > 0 ? +1 : -1, &f, &t);
        if (i == 0) first = f;
        if (!ok) break;
        last = t;
        ++done;
    }
    if (!done) {
        cycleIgnored(what, "no group in the catalogue");
        return false;
    }
    char more[64] = "";
    if (done > 1)
        _snprintf_s(more, sizeof(more), _TRUNCATE, " (%d steps%s)", done,
                    queued ? ", from a switch still queued" : "");
    else if (queued)
        _snprintf_s(more, sizeof(more), _TRUNCATE, " (from a switch still queued)");
    logD("live: %s -> stepped %s -> %s%s", what, cycleName(first), cycleName(last), more);
    return true;
}

// Ctrl for a wheel tick = the message's MK_CONTROL OR the thread's key state. A keyboard
// that reports Ctrl late (its WM_KEYDOWN read before the wheel, the wheel's own flag still clear)
// is taken as Ctrl+wheel - the key state Ctrl+PgUp / Ctrl+PgDn have always read - and said.
bool wheelCtrl(WPARAM wp) {
    const bool mk = (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL) != 0;
    const bool ks = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (!mk && ks)
        logD("live: a wheel tick (delta %+d) with MK_CONTROL clear while GetKeyState(VK_CONTROL) is "
             "down - a late Ctrl; taken as Ctrl+wheel", (int)GET_WHEEL_DELTA_WPARAM(wp));
    return mk || ks;
}

bool ctrlKey(WPARAM wp) {
    return (wp == VK_PRIOR || wp == VK_NEXT) && (GetKeyState(VK_CONTROL) & 0x8000) != 0;
}

void wheelWhat(char* out, size_t n, WPARAM wp, const char* at) {
    const int d = (int)GET_WHEEL_DELTA_WPARAM(wp);
    _snprintf_s(out, n, _TRUNCATE, "ctrl wheel %s%s delta %+d",
                d > 0 ? "up" : d < 0 ? "down" : "none", at, d);
}

void keyWhat(char* out, size_t n, WPARAM wp, LPARAM lp) {
    _snprintf_s(out, n, _TRUNCATE, "ctrl %s%s", wp == VK_PRIOR ? "PgUp" : "PgDn",
                (lp & (1 << 30)) ? " (auto-repeat)" : "");
}

}  // namespace

void panelDraw() {
    // the pad is the page draw POST's when it drew it THIS frame; else here.
    // a per-frame flag like the veils' g_veilsByPage, not a 250 ms rule - a
    // frame whose POST did not draw it (the call pattern broke) gets the pad here, never none.
    const bool padByPage = g_padByPage;
    g_padByPage = false;
    if (InterlockedCompareExchange(&g_padOff, 0, 0) || !g_cfg.groupButtons) return;
    const bool shown = plateTransferVisible();
    if (!shown || !viewAvailable()) {
        hideAll();
        if (!shown) frameForget();   // measured again at the next showing
        return;
    }
    if (!g_tq.CanvasRenderRect || !plateGeometry(&g_geo)) return;
    frameCheckGeometry(g_geo);
    HWND h = hookGameWindow();
    RECT rc = {0, 0, 0, 0};
    if (h && GetClientRect(h, &rc) && rc.right > 0 && rc.bottom > 0) {
        g_canvasPerClientX = (float)g_geo.canvasW / (float)rc.right;
        g_canvasPerClientY = (float)g_geo.canvasH / (float)rc.bottom;
        g_perClientOk = true;
    }
    if (!g_geoSaid) {
        g_geoSaid = true;
        logI("panel: caravan window (records) at (%.0f,%.0f) %.0fx%.0f on a %dx%d canvas (UI scale "
             "%.3f, client %ldx%ld) - the pad waits for the measured frame (the Transfer page draw, "
             "else the first mouse move over the Transfer grid); nothing is drawn until then",
             g_geo.winX, g_geo.winY, g_geo.winW, g_geo.winH, g_geo.canvasW, g_geo.canvasH,
             g_geo.scale, h ? rc.right : 0L, h ? rc.bottom : 0L);
    }
    TqCanvas* c = canvasNow();
    if (!c) return;
    layout(g_geo, viewOn());   // the hover test below reads this frame's rects
    g_layoutAt = GetTickCount();
    updateHover(h);
    if (!drawGuarded(c, !padByPage)) {
        InterlockedExchange(&g_padOff, 1);
        hideAll();
        logW("panel: the pad's draw FAULTED - the pad is off for this session");
        return;
    }
    if (padByPage || !g_groundOk) {
        g_padPresentRun = 0;
    } else if (!g_padPresentSaid && ++g_padPresentRun >= 120) {   // the route line
        g_padPresentSaid = true;
        logI("panel: the pad is drawn from the Present detour (over the engine's tooltip) - %s; the "
             "Transfer page draw's POST takes it over in every frame it draws it",
             !g_tq.sigPageDrawRva      ? "exe.transferPageDraw not confirmed (no page-draw detour)"
             : g_pdStable < kPdStable ? "the page draw's per-frame call pattern is not steady yet"
                                      : "the page draw's POST did not draw it for 120 frames");
    }
    g_drawnAt = GetTickCount();
    if (logWants(UT_LOG_TRACE))   // only the TRACE heartbeat reads it
        _snprintf_s(g_status, sizeof(g_status), _TRUNCATE, "pad=drawn font=%s hover=%d frame=%d",
                g_font ? "ok" : "none", g_hover, g_frameVerdict);
}

bool panelInput(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (!plateTransferVisible() || !viewAvailable()) {
        g_down = -1;
        // a Ctrl tick the cycle cannot take here is said (one DEBUG line per event)
        const bool wheel = msg == WM_MOUSEWHEEL && wheelCtrl(wp);
        // a held Ctrl+PgUp/PgDn's auto-repeat (lParam bit 30) is not said again
        if (wheel ||
            (msg == WM_KEYDOWN && g_cfg.pageHotkeys && ctrlKey(wp) && !(lp & (1 << 30)))) {
            char what[64];
            if (wheel) wheelWhat(what, sizeof(what), wp, "");
            else keyWhat(what, sizeof(what), wp, lp);
            cycleIgnored(what, !plateTransferVisible()
                                   ? "the caravan's Transfer tab is not on screen"
                                   : "the collection view is not available");
            g_wheelCtrl.sum = g_wheelPlain.sum = 0;
        }
        return false;
    }
    const bool padLive = g_cfg.groupButtons && !InterlockedCompareExchange(&g_padOff, 0, 0) &&
                         GetTickCount() - g_drawnAt < 500;
    switch (msg) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK: {
        if (!padLive) return false;
        const float x = (float)(short)LOWORD(lp) * g_canvasPerClientX;
        const float y = (float)(short)HIWORD(lp) * g_canvasPerClientY;
        const int b = hit(x, y);   // a SHOWN button's rect only: nothing else is ever swallowed
        // a press anywhere but on the field drops its focus (the press itself goes on as ever);
        // a press on the Transfer toggle, or on a group button while the real page is shown, only
        // switches the page and keeps it (one query for both pages)
        const bool switches = b == kUtPadToggle ||
                              (!viewOn() && b >= kUtPadGroup0 && b < kUtPadGroup0 + kUtPadGroups);
        if (b != kUtPadSearch && b != kUtPadClear && !switches &&
            (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK || msg == WM_RBUTTONDOWN ||
             msg == WM_RBUTTONDBLCLK))
            searchFieldBlur("a click outside the field");
        if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) {
            if (b < 0) return false;
            g_down = b;
            g_padPressAt = GetTickCount();   // the engine's own read of this press is claimed
            return true;
        }
        if (msg == WM_LBUTTONUP) {
            const int was = g_down;
            g_down = -1;
            if (was < 0) return false;          // not our press: the engine's own click
            g_padPressAt = GetTickCount();
            // An ABSOLUTE click: one button, one action. "Transfer" = the real page (view
            // OFF), a group = the view ON on that group. also while an item is held - the
            // item stays on the cursor and the page changes (the held-item refusal is gone).
            if (b == was) {
                if (b == kUtPadToggle) {
                    viewRequest(kUtViewReqOff);
                } else if (b == kUtPadPrev) {
                    if (viewOn()) liveStepPage(-1);
                } else if (b == kUtPadNext) {
                    if (viewOn()) liveStepPage(+1);
                } else if (b == kUtPadOwn) {
                    liveToggleOwnedOnly();
                } else if (b == kUtPadSearch || b == kUtPadClear) {
                    fieldClick(b == kUtPadClear);   // laid out ON, or OFF with search_transfer=1
                } else {
                    liveSelectGroup(b - kUtPadGroup0);
                    viewRequest(kUtViewReqOn);
                }
            }
            return true;                        // our press: its release is ours too
        }
        return b >= 0;                          // right button on the pad: swallowed, nothing
    }
    case WM_MOUSEWHEEL: {
        // MK_CONTROL or a late Ctrl (wheelCtrl); every Ctrl tick is traced below
        const bool ctrl = wheelCtrl(wp);
        char what[64];
        if (!g_cfg.pageHotkeys) {
            if (ctrl) {
                wheelWhat(what, sizeof(what), wp, "");
                cycleIgnored(what, "page_hotkeys=0 in uniquetab.ini");
            }
            return false;
        }
        // OFF (the real Transfer page shown) only Ctrl+wheel is the mod's - the way into the
        // cycle; the plain wheel stays the engine's
        if (!viewOn() && !ctrl) return false;
        POINT pt = {(short)LOWORD(lp), (short)HIWORD(lp)};
        if (!ScreenToClient(hwnd, &pt)) {
            if (ctrl) {
                wheelWhat(what, sizeof(what), wp, "");
                cycleIgnored(what, "ScreenToClient failed");
            }
            return false;
        }
        const float x = (float)pt.x * g_canvasPerClientX, y = (float)pt.y * g_canvasPerClientY;
        char at[32];
        _snprintf_s(at, sizeof(at), _TRUNCATE, " at (%.0f,%.0f)", x, y);
        if (ctrl) wheelWhat(what, sizeof(what), wp, at);
        bool over = hit(x, y) >= 0 || (g_groundOk && utRectHas(g_ground, x, y));
        if (g_frameVerdict == 1) {   // the measured grid; the records' page rect before
            over = over || utRectHas(g_frameGrid, x, y);
        } else {
            PlateGeometry g;
            if (plateGeometry(&g) && platePointOnPage(g, x, y)) over = true;
        }
        if (!over) {
            if (ctrl)
                cycleIgnored(what, g_frameVerdict == 1
                                       ? "not over the pad, its ground or the measured grid"
                                       : "not over the pad, its ground or the records' page");
            g_wheelCtrl.sum = g_wheelPlain.sum = 0;
            return false;
        }
        // whole notches from the raw delta (utWheelAccumulate: the remainder is kept, a
        // direction change drops it) - a delta under one notch is no longer silently dropped
        const int delta = GET_WHEEL_DELTA_WPARAM(wp);
        if (ctrl) {   // the cycle (wheel up = back, down = forward), Transfer is a stop
            g_wheelPlain.sum = 0;
            const int notches = utWheelAccumulate(g_wheelCtrl, delta, WHEEL_DELTA);
            if (notches == 0) {
                char why[64];
                _snprintf_s(why, sizeof(why), _TRUNCATE, "under one notch: %+d of %d kept",
                            g_wheelCtrl.sum, WHEEL_DELTA);
                cycleIgnored(what, why);
                return true;   // ours, as the whole notches are
            }
            cycleSteps(what, -notches);   // up (+) = back
            return true;
        }
        g_wheelCtrl.sum = 0;
        const int notches = utWheelAccumulate(g_wheelPlain, delta, WHEEL_DELTA);
        if (notches == 0) return true;   // a part of a notch: kept for the next tick
        return liveHandleWheel(notches, false);   // one call, one line (as before)
    }
    case WM_KEYDOWN:
        // while the field has the focus the keys are its text (the key gate takes them from the
        // engine): the view hotkey and PgUp / PgDn do nothing here
        if (searchFieldFocused()) return false;
        if (g_cfg.viewHotkey && (int)wp == g_cfg.viewHotkey) {
            if (!(lp & (1 << 30))) viewRequestToggle();   // not the auto-repeat
            return true;
        }
        if (g_cfg.pageHotkeys && (wp == VK_PRIOR || wp == VK_NEXT)) {
            // Ctrl+PgUp / Ctrl+PgDn follow the wheel's cycle, from the real page too;
            // the plain keys turn pages while ON only
            if (ctrlKey(wp)) {   // traced like the wheel
                char what[64];
                keyWhat(what, sizeof(what), wp, lp);
                cycleSteps(what, wp == VK_PRIOR ? -1 : +1);
                return true;
            }
            if (viewOn()) return liveHandleKey((int)wp, false);
        }
        return false;
    default:
        return false;
    }
}

bool panelSearchFieldShown() {
    return g_btnShown[kUtPadSearch] && searchFieldWanted() && g_cfg.groupButtons &&
           !InterlockedCompareExchange(&g_padOff, 0, 0) && GetTickCount() - g_drawnAt < 500;
}

namespace {

// ---- the equipment window's slot art (slot_plates=3) ---------------------------------
// CharacterWindow01.tex by NAME through GraphicsEngine::LoadTexture, once per world (like the font),
// drawn per slot with the textured GraphicsCanvas::RenderRect (the overload the engine's own item
// border and icon use) at the group's equipment box (kUtSlotArt, ut_slotart.h), in the page draw's
// PRE where the plate was: under the tint, the border and the icon. Nothing is allocated per frame;
// a texture that does not load is said once (WARN) and the plate is drawn instead.
const TqTexture* g_art = nullptr;
bool g_artTried = false, g_artFailSaid = false, g_artSaid = false;
float g_artSx = 1.0f, g_artSy = 1.0f;   // the loaded size over the file's (660 x 637)
// a texture whose size answers 0 (a lazy load, or an empty object for a file the
// engine did not find) is NOT ready: the plate is drawn and the size asked again each frame (two
// getters); still no size after kArtWaitMs = a failure (one WARN, the plate for the world).
const DWORD kArtWaitMs = 5000;
const TqTexture* g_artWait = nullptr;
DWORD g_artWaitSince = 0;

void artFail(const char* why) {
    if (g_artFailSaid) return;
    g_artFailSaid = true;
    logW("panel: the equipment window's slot art (%s) is not drawn - %s; the flat slot plates are "
         "drawn instead (slot_plates=3 falls back to 1)", kUtSlotArtTexture, why);
}

bool artSizeOk(int w, int h) { return w > 0 && h > 0 && w <= 8192 && h <= 8192; }

// The texture is drawn from now on. w, h = 0: its size is unknown (no GetWidth / GetHeight export)
// and taken as the file's.
const TqTexture* artReady(const TqTexture* t, int w, int h) {
    // The crops are in the FILE's pixels; a texture the engine loaded at another size (a lower
    // texture detail) is addressed in its own: scale. Computed only once the size is known.
    if (artSizeOk(w, h)) {
        g_artSx = (float)w / (float)kUtSlotArtTexW;
        g_artSy = (float)h / (float)kUtSlotArtTexH;
    } else {
        g_artSx = g_artSy = 1.0f;
    }
    g_art = t;
    g_artWait = nullptr;
    logI("panel: the equipment window's slot art %s loaded by name (%p, %d x %d; the file is %d x %d)",
         kUtSlotArtTexture, (const void*)t, w, h, kUtSlotArtTexW, kUtSlotArtTexH);
    return g_art;
}

bool artSize(const TqTexture* t, int* w, int* h) {
    *w = *h = 0;
    bool ok = true;
    utGuardEnter();
    __try {
        *w = g_tq.TextureGetWidth(t);
        *h = g_tq.TextureGetHeight(t);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    utGuardLeave();
    return ok;
}

const TqTexture* ensureSlotArt() {
    if (g_art) return g_art;
    if (g_artWait) {   // loaded with no size yet: ask again, give up after kArtWaitMs
        int w = 0, h = 0;
        if (artSize(g_artWait, &w, &h) && artSizeOk(w, h)) return artReady(g_artWait, w, h);
        if (GetTickCount() - g_artWaitSince < kArtWaitMs) return nullptr;
        g_artWait = nullptr;
        artFail("GraphicsEngine::LoadTexture answered a texture with no size (GraphicsTexture::"
                "GetWidth / GetHeight 0 for 5 s)");
        return nullptr;
    }
    if (g_artTried) return nullptr;
    g_artTried = true;
    if (!g_tq.GfxLoadTexture || !g_tq.CanvasRenderRectTex || !g_tq.EngineGetGraphicsEngine) {
        artFail("an export is missing (GraphicsEngine::LoadTexture or the textured "
                "GraphicsCanvas::RenderRect)");
        return nullptr;
    }
    TqEngine* e = engine();
    if (!e) {
        g_artTried = false;   // no engine yet: ask again on the next draw
        return nullptr;
    }
    static char heap[64];   // the heap form's text; the engine only reads the string
    static_assert(sizeof(kUtSlotArtTexture) <= sizeof(heap), "the name fits");
    TqStdString name;
    tqStdStringOver(&name, heap, sizeof(heap), kUtSlotArtTexture, sizeof(kUtSlotArtTexture) - 1);
    const TqTexture* t = nullptr;
    int w = 0, h = 0;
    utGuardEnter();
    __try {
        TqGraphicsEngine* gfx = g_tq.EngineGetGraphicsEngine(e);
        if (gfx) t = g_tq.GfxLoadTexture(gfx, &name);
        if (t && g_tq.TextureGetWidth && g_tq.TextureGetHeight) {
            w = g_tq.TextureGetWidth(t);
            h = g_tq.TextureGetHeight(t);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        t = nullptr;
    }
    utGuardLeave();
    if (!t) {
        artFail("GraphicsEngine::LoadTexture answered nothing");
        return nullptr;
    }
    if (!g_tq.TextureGetWidth || !g_tq.TextureGetHeight) return artReady(t, 0, 0);   // size unknown
    if (artSizeOk(w, h)) return artReady(t, w, h);
    g_artWait = t;   // no size yet: the plate this frame, the size asked again on the next
    g_artWaitSince = GetTickCount();
    return nullptr;
}

// The shown group's box, with the texture loaded; null = draw the plate.
const UtSlotArtBox* slotArtNow() {
    if (!ensureSlotArt()) return nullptr;
    const int k = utSlotArtFor(liveGroupLabel(liveShownGroup()));
    return k >= 0 ? &kUtSlotArt[k] : nullptr;
}

bool drawSlotArt(TqCanvas* c, const UtSlotArtBox& b, float x, float y, float w, float h) {
    UtRectF t;
    if (!g_art || w <= 0.0f || h <= 0.0f || !utSlotArtFit(b, w, h, &t)) return false;
    const TqRect r = {x, y, w, h};
    const TqRect tr = {t.x * g_artSx, t.y * g_artSy, t.w * g_artSx, t.h * g_artSy};
    const TqColor white = {1.0f, 1.0f, 1.0f, 1.0f};   // the engine's icon draw passes the same
    g_tq.CanvasRenderRectTex(c, &r, &tr, g_art, &white, nullptr, nullptr);
    return true;
}

void slotArtSay(const UtSlotArtBox* art, int drawn, int n) {
    if (g_artSaid || !art || drawn <= 0) return;
    g_artSaid = true;
    logI("panel: slot ground = the equipment window's art, %s (%d,%d) %dx%d of %s, on %d of %d "
         "slots of group \"%s\" (the page draw's PRE-detour, under the items)",
         art->box, art->x, art->y, art->w, art->h, kUtSlotArtTexture, drawn, n, art->group);
}

void panelForgetSlotArt() {
    g_art = nullptr;
    g_artWait = nullptr;
    g_artTried = false;
}

// ---- the rect route (the TQ.exe 0x10A9B0 detour, hooks.cpp) ---------------------------
// Only between panelRectRouteBegin and panelRectRouteEnd, which hk_PageDraw calls around the
// Transfer page draw's ORIGINAL while the view is ON on the view's page: an item widget whose item
// is a live PROTOTYPE gets its SLOT rect written to [widget+0x10..+0x1C] before the engine draws
// its background (the tint and the border texture over the slot), and its CENTRED footprint after
// it (the icon that follows is drawn from that). Every widget touched is put back at its footprint
// rect when the page draw returns, so the hit-tests, the tooltip anchor and the drag placement -
// all outside the draw - read exactly what they read before. A widget whose item is not a
// prototype, a real sack's widget and every other page are never touched (the id must be a live
// prototype's, and the route is off outside the view's own page draw).
struct RrSave {
    unsigned char* w;
    unsigned id;
    UtRectF foot, centred;
    int proto;                     // the prototype index
    const TqTexture* iconOrig;     // the widget's own icon while the gray one is lent
    const TqTexture* iconGray;
};
RrSave g_rrSave[kUtProtoMax];
int g_rrN = 0;
bool g_rrActive = false;
int g_rrApplied = 0, g_rrRefused = 0;   // this page-draw call
// the prototype widgets whose background draw (TQ.exe 0x10A9B0) the detour SKIPPED in this
// page-draw call (uncollected, not hovered, owned_marks != 0: ownedBackgroundSkip). The skip rides
// on the route's token (ut_viewgate.h utBgToken); the log reads the count, never per frame.
int g_rrSkipped = 0;
DWORD g_rrCleanAt = 0;                    // the first clean routed call (the slot-wide line's clock)
unsigned g_bgDbgGen = ~0u;                // the prototype set the DEBUG skip count was last said for
int g_bgDbgCount = -1;
static_assert(kUtProtoMax < kUtBgNoSave, "the route token's save index is 16 bits");
// which prototypes the engine drew slot-wide, per prototype index. g_rrCall counts
// the page draws the route was active in; g_rrMark[i] = the call prototype i was routed in; the
// g_rrLast* fields describe the last call that drew at least one prototype.
unsigned g_rrCall = 0;
// the page's ONE drawn cell for this page draw, read once at Begin (not per
// widget): > 0 = agreed, 0 = no evidence (no check), -1 = the units disagree (refused)
float g_rrCell = 0.0f;
unsigned g_rrMark[kUtProtoMax];
unsigned g_rrLastCall = 0, g_rrLastGen = 0;
int g_rrLastCount = -1, g_rrLastRefused = 0;
DWORD g_rrLastAt = 0;
bool g_rrLeftSaid = false;
int g_rrUnwoundLeft = 0, g_rrUnwoundRestored = 0;   // panelRectRouteUnwound
struct RrIdx {
    unsigned id;
    int i;
};
RrIdx g_rrIdx[kUtProtoMax];
int g_rrIdxN = 0;
unsigned g_rrGen = ~0u;
int g_rrCount = -1;
bool g_rrSaid = false, g_rrRefSaid = false;

int rrIndexOf(unsigned id) {
    int lo = 0, hi = g_rrIdxN - 1;
    while (lo <= hi) {
        const int m = (lo + hi) / 2;
        if (g_rrIdx[m].id == id) return g_rrIdx[m].i;
        if (g_rrIdx[m].id < id) lo = m + 1;
        else hi = m - 1;
    }
    return -1;
}

// The ring is the fallback: it is skipped where the engine's own tint is slot-wide. All of it
// while the last page draw (within 500 ms) routed every prototype it drew; else only the slots of
// the prototypes it routed (a refused widget gets the ring, the others never get the band twice).
bool rrRecent() { return g_rrLastAt && GetTickCount() - g_rrLastAt < 500; }
bool rectRouteLive() { return rrRecent() && g_rrLastRefused == 0; }
bool rrCovers(int i) {
    if (!rrRecent()) return false;
    if (g_rrLastRefused == 0) return true;
    return g_rrLastGen == protoGeneration() && g_rrLastCount == protoCount() && i >= 0 &&
           i < kUtProtoMax && g_rrMark[i] == g_rrLastCall;
}

// Every widget the route left at its centred rect goes back to its footprint - only one still as the
// route left it (the same item, the centred rect): the engine owns the rect again the moment it
// writes one of its own. From End, and from Begin for the leftovers of a draw whose End was skipped
//.
int rrRestore() {
    int restored = 0;
    for (int k = 0; k < g_rrN && k < kUtProtoMax; ++k) {
        const RrSave& s = g_rrSave[k];
        __try {   // the widget's own icon back first, if it still holds the gray one
            const TqTexture** icon = (const TqTexture**)(s.w + kUtWidgetIcon);
            if (s.iconGray && *(const unsigned*)(s.w + kUtWidgetItemId) == s.id && *icon == s.iconGray)
                *icon = s.iconOrig;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        __try {
            float* r = (float*)(s.w + kUtWidgetRect);
            if (*(const unsigned*)(s.w + kUtWidgetItemId) == s.id && r[0] == s.centred.x &&
                r[1] == s.centred.y && r[2] == s.centred.w && r[3] == s.centred.h) {
                r[0] = s.foot.x;
                r[1] = s.foot.y;
                r[2] = s.foot.w;
                r[3] = s.foot.h;
                ++restored;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    return restored;
}

// ---- the gray icons (owned_marks=3) -------------------------
// The engine's own icon draw (TQ.exe 0x10ADE0) renders [widget+0x3C] with the textured RenderRect.
// For an uncollected prototype (ownedGrayWanted: the veil's trust, not the hovered slot) the rect
// route lends it the GRAY copy of the icon between the background draw (Post) and the end of the
// page draw (rrRestore gives the widget's own texture back, only if it still holds the gray one).
// The gray copies are the generator's gray\ug<hash>.tex (ut_graytex.h); the engine reads them by
// NAME through GraphicsEngine::LoadTexture once the mod's gray\ folder is a DIRECTORY source of the
// engine's file system (FileSystem::AddSource, partition 1 - the call TQ.exe makes for
// "./Settings/"; FileSystem::OpenFile tries the archives' partition 0, then partition 1). Loaded
// when the prototype set changes (reused by record, the rest given back with UnloadTexture),
// forgotten with the world like the font and the slot art. Nothing is allocated per frame.
struct GrayTex {
    unsigned long long hash;
    const TqTexture* tex;
    int state;   // 0 unknown size, 1 ready (the engine's size = the icon's), -1 not usable
};
GrayTex g_gray[kUtProtoMax], g_grayNext[kUtProtoMax];
int g_grayN = 0;
unsigned g_grayGen = ~0u;
int g_grayCount = -1;
unsigned g_grayMark[kUtProtoMax];   // the rect-route call prototype i was drawn gray in
bool g_graySource = false, g_grayOff = false, g_grayFailSaid = false, g_graySaid = false;
bool g_graySizeSaid = false, g_grayMissSaid = false;
int g_grayLent = 0, g_grayWanted = 0;   // this page-draw call
int g_grayReadyEver = 0, g_grayLoadedEver = 0, g_grayTriedEver = 0;
DWORD g_grayFirstAt = 0;
char g_grayDir[MAX_PATH] = "";   // the folder, '\\' separators (for the existence test)
char g_grayHeap[64];             // LoadTexture's std::string (heap form: the name is 22 chars)
char g_grayDirHeap[MAX_PATH + 8];

void grayFail(const char* why) {
    g_grayOff = true;
    if (g_grayFailSaid) return;
    g_grayFailSaid = true;
    logW("panel: the uncollected records' icons are NOT drawn in gray - %s; owned_marks=3 draws the "
         "dark veil instead (as owned_marks=1) for this session", why);
}

// Once per process: the mod's gray\ folder becomes a directory source of the engine's file system.
// called only from panelGrayPrepare, at a Present before the first world.
bool grayEnsureSource() {
    if (g_graySource) return true;
    if (g_grayOff) return false;
    if (!g_tq.EngineGetFileSystem || !g_tq.FsAddSource || !g_tq.GfxLoadTexture ||
        !g_tq.EngineGetGraphicsEngine || !g_tq.TextureGetWidth || !g_tq.TextureGetHeight) {
        grayFail("Engine::GetFileSystem / FileSystem::AddSource / GraphicsEngine::LoadTexture / "
                 "GraphicsTexture::GetWidth were not all found in Engine.dll");
        return false;
    }
    HMODULE self = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)&grayFail, &self) ||
        !self) {
        grayFail("the mod's own folder is not known");
        return false;
    }
    utModPathA(self, "gray", g_grayDir, sizeof(g_grayDir));
    char stamp[MAX_PATH + 16];
    _snprintf_s(stamp, sizeof(stamp), _TRUNCATE, "%s\\gray.stamp", g_grayDir);
    if (GetFileAttributesA(stamp) == INVALID_FILE_ATTRIBUTES) {
        grayFail("the gray copies are not there (gray\\gray.stamp is missing - the catalogue "
                 "generation writes them at the next launch; if it could not make the folder, "
                 "catalogue.stamp says so and it does not try again: delete catalogue.stamp)");
        return false;
    }
    char dir[MAX_PATH + 2];   // the engine's own form: '/' separators and a trailing '/'
    size_t n = 0;
    for (const char* p = g_grayDir; *p && n + 2 < sizeof(dir); ++p) dir[n++] = *p == '\\' ? '/' : *p;
    dir[n++] = '/';
    dir[n] = 0;
    TqStdString s;
    if (!tqStdStringOver(&s, g_grayDirHeap, sizeof(g_grayDirHeap), dir, n)) {
        grayFail("the gray folder's path is too long");
        return false;
    }
    bool ok = false;
    __try {
        TqEngine* e = *g_tq.ppEngine;
        void* fs = e ? g_tq.EngineGetFileSystem(e) : nullptr;
        ok = fs && g_tq.FsAddSource(fs, 1, &s, nullptr, 0, 0, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    if (!ok) {
        grayFail("FileSystem::AddSource refused the mod's gray folder");
        return false;
    }
    g_graySource = true;
    logI("panel: gray icons - the mod's gray\\ folder is a directory source of the engine's file "
         "system (FileSystem::AddSource, partition 1, as TQ.exe adds ./Settings/); the icons load by "
         "name (gray\\ug<hash>.tex) through GraphicsEngine::LoadTexture");
    return true;
}

const TqTexture* grayLoad(unsigned long long h) {
    char name[32];
    if (!utGrayNameOf(h, name, sizeof(name))) return nullptr;
    char path[MAX_PATH + 32];   // never ask the engine for a name that has no file: no placeholder
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\%s", g_grayDir, name);
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        if (!g_grayMissSaid) {
            g_grayMissSaid = true;
            logW("panel: a prototype has no gray icon (gray\\%s is missing) - its slot gets the veil; "
                 "delete catalogue.stamp to write the gray copies again", name);
        }
        return nullptr;
    }
    TqStdString s;
    if (!tqStdStringOver(&s, g_grayHeap, sizeof(g_grayHeap), name, kUtGrayNameLen)) return nullptr;
    const TqTexture* t = nullptr;
    ++g_grayTriedEver;
    __try {
        TqEngine* e = *g_tq.ppEngine;
        TqGraphicsEngine* gfx = e ? g_tq.EngineGetGraphicsEngine(e) : nullptr;
        if (gfx) t = g_tq.GfxLoadTexture(gfx, &s);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        t = nullptr;
    }
    if (t) ++g_grayLoadedEver;
    return t;
}

void grayUnload(const TqTexture* t) {
    if (!t || !g_tq.GfxUnloadTexture) return;   // without the export the engine keeps it
    __try {
        TqEngine* e = *g_tq.ppEngine;
        TqGraphicsEngine* gfx = e ? g_tq.EngineGetGraphicsEngine(e) : nullptr;
        if (gfx) g_tq.GfxUnloadTexture(gfx, t);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

// Begin of a routed page draw: the gray texture of every prototype, when the set changed.
void grayBegin(int n, unsigned gen) {
    g_grayLent = g_grayWanted = 0;
    if (g_cfg.ownedMarks != 3 || g_grayOff) return;
    if (gen == g_grayGen && n == g_grayCount) return;
    if (!g_graySource) {   // added before the first world only (panelGrayPrepare)
        grayFail("the gray folder was not added to the engine's file system before the first world "
                 "(it is added only at the main menu, once the catalogue generation is done)");
        return;
    }
    g_grayGen = gen;
    g_grayCount = n;
    int loaded = 0, reused = 0;
    for (int i = 0; i < n && i < kUtProtoMax; ++i) {
        g_grayNext[i] = GrayTex{0ull, nullptr, 0};
        const char* rec = nullptr;
        if (!protoAt(i, &rec, nullptr, nullptr, nullptr) || !rec || !*rec) continue;
        const unsigned long long h = utGrayHash(rec);
        for (int k = 0; k < g_grayN; ++k) {   // the same record in the last set: kept
            if (g_gray[k].tex && g_gray[k].hash == h) {
                g_grayNext[i] = g_gray[k];
                g_gray[k].tex = nullptr;
                ++reused;
                break;
            }
        }
        if (!g_grayNext[i].tex) {
            g_grayNext[i] = GrayTex{h, grayLoad(h), 0};
            if (g_grayNext[i].tex) ++loaded;
        }
    }
    for (int k = 0; k < g_grayN; ++k) {   // what this set does not show is given back
        if (g_gray[k].tex) grayUnload(g_gray[k].tex);
    }
    g_grayN = n < kUtProtoMax ? n : kUtProtoMax;
    for (int i = 0; i < g_grayN; ++i) g_gray[i] = g_grayNext[i];
    if (g_grayTriedEver > 0 && !g_grayLoadedEver) {   // files there, and the engine answered none
        char why[160];
        _snprintf_s(why, sizeof(why), _TRUNCATE,
                    "GraphicsEngine::LoadTexture answered no texture for %d gray icon file(s) that "
                    "exist (the engine does not read the gray folder)", g_grayTriedEver);
        grayFail(why);
    }
    logD("panel: gray icons for %d prototype(s): %d loaded by name, %d kept from the last page", n,
         loaded, reused);
}

// Post of a prototype's background draw: lend the icon draw the gray copy.
void grayLend(int token) {
    RrSave& s = g_rrSave[token];
    const int i = s.proto;
    if (g_cfg.ownedMarks != 3 || g_grayOff || i < 0 || i >= g_grayN || !ownedGrayWanted(i)) return;
    ++g_grayWanted;
    GrayTex& gt = g_gray[i];
    if (!gt.tex || gt.state < 0) return;
    // the readiness clock starts at the first WANTED lend of a loaded copy (a
    // fully collected group, the OWN filter, untrusted marks or the hovered slot never start it)
    if (!g_grayFirstAt) g_grayFirstAt = GetTickCount() | 1u;
    __try {
        const TqTexture** icon = (const TqTexture**)(s.w + kUtWidgetIcon);
        const TqTexture* orig = *icon;
        if (!orig || orig == gt.tex) return;
        if (gt.state == 0) {   // the engine's size of the gray copy must be the icon's own
            const int gw = g_tq.TextureGetWidth(gt.tex), gh = g_tq.TextureGetHeight(gt.tex);
            if (gw <= 0 || gh <= 0) return;   // not loaded yet: the veil this frame
            ++g_grayReadyEver;   // the engine answered a size (the right one or not)
            const int ow = g_tq.TextureGetWidth(orig), oh = g_tq.TextureGetHeight(orig);
            if (ow != gw || oh != gh) {
                gt.state = -1;
                if (!g_graySizeSaid) {
                    g_graySizeSaid = true;
                    logW("panel: a gray icon loaded at %dx%d where the item's own icon is %dx%d - "
                         "not the file the generator wrote? That slot gets the veil", gw, gh, ow, oh);
                }
                return;
            }
            gt.state = 1;
        }
        *icon = gt.tex;
        s.iconOrig = orig;
        s.iconGray = gt.tex;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    g_grayMark[i] = g_rrCall;
    ++g_grayLent;
}

// End of a routed page draw: the route's line once, and the readiness rule (5 s after the first
// WANTED lend of a world with no size answered = the engine does not read the folder: the veil).
void grayEnd() {
    if (g_cfg.ownedMarks != 3 || g_grayOff) return;
    if (g_grayLent > 0 && !g_graySaid) {
        g_graySaid = true;
        logI("panel: uncollected records drawn in gray - the engine's own icon draw (TQ.exe+0x10ADE0, "
             "the texture at [widget+0x%X]) given the gray copy on %d of %d uncollected prototype(s); "
             "the hovered one shows its colours, a slot without a gray copy keeps the veil",
             kUtWidgetIcon, g_grayLent, g_grayWanted);
    }
    if (!g_grayReadyEver && g_grayFirstAt && GetTickCount() - g_grayFirstAt > 5000) {
        char why[192];
        _snprintf_s(why, sizeof(why), _TRUNCATE,
                    "%d gray icon(s) were loaded by name and none answered a size in the 5 s "
                    "after one was first wanted (the engine does not read the gray folder)",
                    g_grayLoadedEver);
        grayFail(why);
    }
}

bool grayDrawn(int i) {
    return i >= 0 && i < kUtProtoMax && g_grayMark[i] != 0 && g_grayMark[i] == g_rrLastCall &&
           rrRecent();
}

void grayForget() {
    g_grayN = 0;
    g_grayGen = ~0u;
    g_grayCount = -1;
    g_grayFirstAt = 0;
    g_grayReadyEver = g_grayLoadedEver = g_grayTriedEver = 0;
    for (int i = 0; i < kUtProtoMax; ++i) g_grayMark[i] = 0;
}

}  // namespace

// Begin's gates (the view's page, the view on, the pad on, the plate visible) -
// the ring asks them too, so it skips a bare prototype only on a frame whose Begin arms.
static bool rrGatesOpen(void* page) {
    return page && page == viewPageWindow() && viewOn() &&
           !InterlockedCompareExchange(&g_padOff, 0, 0) && plateTransferVisible();
}

void panelRectRouteBegin(void* page) {
    if (g_rrN > 0 || g_rrUnwoundLeft > 0) {   // the last page draw did not reach End (an
        // exception unwound past it; hk_PageDraw's __finally restored them then)
        const int left = g_rrN > 0 ? g_rrN : g_rrUnwoundLeft;
        const int restored = g_rrN > 0 ? rrRestore() : g_rrUnwoundRestored;
        g_rrUnwoundLeft = g_rrUnwoundRestored = 0;
        if (!g_rrLeftSaid) {
            g_rrLeftSaid = true;
            logW("panel: the last Transfer page draw did not return normally - %d prototype "
                 "widget(s) left at their centred rect, %d put back at their footprint now", left,
                 restored);
        }
    }
    g_rrActive = false;
    g_rrN = 0;
    g_rrApplied = g_rrRefused = g_rrSkipped = 0;
    if (!rrGatesOpen(page)) return;
    const int n = protoCount();
    const unsigned gen = protoGeneration();
    if (gen != g_rrGen || n != g_rrCount) {   // the id index: once per prototype set, never per frame
        g_rrGen = gen;
        g_rrCount = n;
        g_rrIdxN = 0;
        for (int i = 0; i < n && g_rrIdxN < kUtProtoMax; ++i) {
            unsigned id = 0;
            if (!protoAt(i, nullptr, nullptr, nullptr, &id) || !id) continue;
            int k = g_rrIdxN++;
            while (k > 0 && g_rrIdx[k - 1].id > id) {   // insertion: sorted by id
                g_rrIdx[k] = g_rrIdx[k - 1];
                --k;
            }
            g_rrIdx[k] = RrIdx{id, i};
        }
    }
    g_rrActive = g_rrIdxN > 0;
    if (g_rrActive && ++g_rrCall == 0) g_rrCall = 1;   // 0 = never routed (g_rrMark's start)
    if (g_rrActive) {
        float cell = 0.0f;   // once per page draw; a disagreement refuses
        const int cv = panelCellVerdict(&cell);
        g_rrCell = cv > 0 ? cell : (cv < 0 ? -1.0f : 0.0f);
        // the hovered slot from the cursor this page draw's POST reads, so the
        // gray lend and the veil agree (no one-frame veil on the slot the cursor just left)
        ownedUnveilRefresh(g_cursorOk, g_cursorX, g_cursorY);
        grayBegin(n, gen);   // the gray icons of this prototype set
    }
}

int panelItemBackgroundPre(void* widget) {
    if (!g_rrActive || !widget || g_rrN >= kUtProtoMax) return -1;
    unsigned char* w = (unsigned char*)widget;
    unsigned id = 0;
    UtRectF foot = {0.0f, 0.0f, 0.0f, 0.0f};
    float scale = 1.0f;
    __try {
        id = *(const unsigned*)(w + kUtWidgetItemId);
        const float* r = (const float*)(w + kUtWidgetRect);
        foot = UtRectF{r[0], r[1], r[2], r[3]};
        scale = *(const float*)(w + kUtWidgetScale);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
    const int i = rrIndexOf(id);
    if (i < 0) return -1;   // not a prototype: never touched
    int col = 0, row = 0, iw = 0, ih = 0, sc = 0, sr = 0, sw = 0, sh = 0;
    unsigned pid = 0;
    UtRectF slot, centred;
    const bool isProto = protoAt(i, nullptr, &col, &row, &pid, &iw, &ih) && pid == id;
    // an UNCOLLECTED record's background (the red, the class tint, the rarity border) is
    // not drawn unless its slot is hovered - the detour skips the original. Only a live prototype
    // of this page draw (the id and the prototype agree); a collected one, the hovered slot and
    // owned_marks=0 draw as before. A refused widget skips too (its ring skips the same way).
    const bool bare = isProto && ownedBackgroundSkip(i, true);
    // the page's ONE drawn cell; 0 = not known (no check); -1 = the grid
    // and the mod sack disagree (refused); read once at Begin
    const float cellPx = g_rrCell;
    if (!isProto || !protoSlotAt(i, &sc, &sr, &sw, &sh) ||
        !utWidgetSlotRects(foot, col, row, iw, ih, sc, sr, sw, sh, scale, &slot, &centred,
                           cellPx)) {
        ++g_rrRefused;
        if (!g_rrRefSaid) {
            g_rrRefSaid = true;
            logW("panel: a prototype's item widget (id %u) is not drawn slot-wide - its rect (%.1f, "
                 "%.1f, %.1f x %.1f, scale %.3f: a drawn cell of %.1f px, the page's %.1f px; -1 = "
                 "the grid and the mod sack's cell disagree) is not a %d x %d footprint at cell "
                 "(%d,%d) of a %d x %d slot; it keeps its footprint and the ring tints the "
                 "slot",
                 id, foot.x, foot.y, foot.w, foot.h, scale,
                 iw > 0 ? foot.w / (float)iw * scale : 0.0f, cellPx, iw, ih, col, row, sw, sh);
        }
        if (bare) ++g_rrSkipped;
        return utBgToken(-1, bare);   // no Post (the rect was not changed); skip or not
    }
    __try {
        float* r = (float*)(w + kUtWidgetRect);
        r[0] = slot.x;
        r[1] = slot.y;
        r[2] = slot.w;
        r[3] = slot.h;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
    g_rrSave[g_rrN] = RrSave{w, id, foot, centred, i, nullptr, nullptr};
    if (i < kUtProtoMax) g_rrMark[i] = g_rrCall;
    ++g_rrApplied;
    if (bare) ++g_rrSkipped;
    return utBgToken(g_rrN++, bare);
}

void panelItemBackgroundPost(int token) {
    if (token < 0 || token >= g_rrN) return;
    const RrSave& s = g_rrSave[token];
    __try {
        float* r = (float*)(s.w + kUtWidgetRect);
        r[0] = s.centred.x;
        r[1] = s.centred.y;
        r[2] = s.centred.w;
        r[3] = s.centred.h;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    grayLend(token);   // the icon draw that follows reads [widget+0x3C]
}

void panelRectRouteEnd() {
    const int restored = rrRestore();
    if (g_rrActive && (g_rrApplied > 0 || g_rrRefused > 0)) {
        g_rrLastCall = g_rrCall;
        g_rrLastGen = g_rrGen;
        g_rrLastCount = g_rrCount;
        g_rrLastRefused = g_rrRefused;
        g_rrLastAt = GetTickCount();
    }
    if (g_rrActive) grayEnd();   // the route line, the readiness rule
    if (g_rrActive) {
        const bool clean = g_rrApplied > 0 && g_rrRefused == 0;
        if (clean && !g_rrCleanAt) g_rrCleanAt = GetTickCount() | 1u;
        // said once, at the first clean call that skipped a background - or 3 s after the
        // first clean call (the owned marks may not be trusted yet on the very first frames)
        if (!g_rrSaid && clean &&
            (g_rrSkipped > 0 || g_cfg.ownedMarks == 0 || GetTickCount() - g_rrCleanAt > 3000)) {
            g_rrSaid = true;
            char bg[256];
            if (g_cfg.ownedMarks == 0)
                _snprintf_s(bg, sizeof(bg), _TRUNCATE,
                            "uncollected records: tint, red and border as the engine draws them "
                            "(owned_marks=0)");
            else if (g_rrSkipped > 0 || ownedMarksNow())   // say the trust
                _snprintf_s(bg, sizeof(bg), _TRUNCATE,
                            "uncollected records: no tint, red or border unless hovered (%d "
                            "skipped this frame%s)",
                            g_rrSkipped,
                            g_rrSkipped > 0 ? "" : ": every record shown is collected or hovered");
            else
                _snprintf_s(bg, sizeof(bg), _TRUNCATE,
                            "uncollected records: no tint, red or border unless hovered (0 "
                            "skipped this frame: the owned marks are not trusted yet - the grid "
                            "not measured, the journal unknown or the OWN filter on)");
            logI("panel: items drawn slot-wide - the engine draws each prototype's background (its "
                 "tint and rarity border) over its SLOT and its icon centred in it (TQ.exe+0x%X "
                 "detour: %d widget(s), %d put back at their footprint after the page draw); the "
                 "tint ring is off while this holds (kept as the fallback); %s",
                 g_tq.sigItemBackgroundRva, g_rrApplied, restored, bg);
        }
        // the skip count once per prototype set (a page change), DEBUG -
        // at the set's first frame whose marks are trusted (Pre read the same g_marksNow: End runs
        // before the POST's ownedMarksBegin), so the first showing does not say "0 of N"; a set
        // whose marks are never trusted (the OWN filter) skips nothing and says nothing
        if ((g_rrApplied > 0 || g_rrRefused > 0) &&
            (g_rrGen != g_bgDbgGen || g_rrCount != g_bgDbgCount) &&
            (g_cfg.ownedMarks == 0 || ownedMarksNow())) {
            g_bgDbgGen = g_rrGen;
            g_bgDbgCount = g_rrCount;
            logD("panel: backgrounds skipped for %d of %d prototype widget(s) on this page "
                 "(uncollected, not hovered; owned_marks=%d)",
                 g_rrSkipped, g_rrApplied + g_rrRefused, g_cfg.ownedMarks);
        }
    }
    g_rrActive = false;
    g_rrN = 0;
}

void panelRectRouteUnwound() {
    __try {
        if (g_rrN > 0) {
            g_rrUnwoundLeft = g_rrN;
            g_rrUnwoundRestored = rrRestore();
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    g_rrActive = false;
    g_rrN = 0;
}

// (see ut_panel.h). Retried each Present until the engine's file system is up.
void panelGrayPrepare(bool noWorldYet) {
    if (g_graySource || g_grayOff || g_cfg.ownedMarks != 3 || !noWorldYet) return;
    if (!generateDone()) return;   // a first launch writes gray\ now
    if (g_tq.EngineGetFileSystem && g_tq.ppEngine) {
        bool up = false;
        __try {
            TqEngine* e = *g_tq.ppEngine;
            up = e && g_tq.EngineGetFileSystem(e);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            up = false;
        }
        if (!up) return;   // the next frame
    }
    grayEnsureSource();   // added, or one WARN and the veil (grayFail)
}

void panelForgetFonts() {
    g_font = nullptr;
    g_fontTried = false;
    g_geoSaid = false;
    panelForgetSlotArt();   // the slot art is loaded again in the next world, like the font
    grayForget();           // the gray icons too (forgotten, never unloaded across a world)
}

// a click on the pad (a button pressed in the last 400 ms, or the cursor on the pad's ground)
// never drops the held item on the page either: the pad sits in the page's rect below the grid, and
// the engine reads the same button through DirectInput. The ground never meets the grid (viewgate).
bool panelPadClaimsPress() {
    if (!plateTransferVisible()) return false;
    const DWORD now = GetTickCount();
    if (g_padPressAt && now - g_padPressAt < 400) return true;
    if (!g_groundOk || now - g_drawnAt >= 500) return false;
    float x = 0.0f, y = 0.0f;
    if (!panelCursorNow(&x, &y)) return false;
    return utRectHas(g_ground, x, y);
}

namespace {
void drawTintRing(void* page, TqCanvas* c, const UtRectF& gr);
void pageDrawVeils(TqCanvas* c, int pass);   // the ON half of panelPageDrawPost
}  // namespace

// the slot plates, drawn by the Transfer page draw's PRE-detour under the items.
// then the tint ring, over the plates and under the items.
void panelPageDrawPre(void* page, void* canvas, const void* origin, int pass) {
    if (!page || page != viewPageWindow()) return;
    drawFrameStep(page, origin, pass);   // the caravan frame, before anything draws
    const unsigned long long frame = hookFrameCount();
    if (frame != g_pdFrame) {   // a new frame: close the previous one's pattern
        if (g_pdFrame != ~0ull) {
            const bool same = g_pdCalls == g_pdPrevCalls && g_pdLastPass == g_pdPrevLastPass;
            g_pdStable = same ? (g_pdStable < 1000000 ? g_pdStable + 1 : g_pdStable) : 0;
            g_pdPrevCalls = g_pdCalls;
            g_pdPrevLastPass = g_pdLastPass;
            if (g_pdStable == kPdStable && !g_pdSaid) {
                g_pdSaid = true;
                logI("panel: the Transfer page draw runs %d time(s) a frame (first pass %d, last pass "
                     "%d) - the slot plates go before the first call, the veils after the last",
                     g_pdCalls, g_pdFirstPass, g_pdLastPass);
            }
        }
        g_pdFrame = frame;
        g_pdCalls = 0;
        g_pdFirstPass = pass;
    }
    ++g_pdCalls;
    g_pdLastPass = pass;
    if (!viewOn() || InterlockedCompareExchange(&g_padOff, 0, 0) || !plateTransferVisible())
        return;
    if (g_pdCalls != 1 || frame == g_plateFrame) return;   // one set of plates, before the first call
    // 0 none, 2 thin frames from Present. 3 = the equipment window's slot
    // art, drawn where the plate was (the plate when the texture is not there).
    const bool plates = g_cfg.slotPlates == 1 || g_cfg.slotPlates == 3;
    TqCanvas* c = canvasNow();
    if (!c || (void*)c != canvas) {
        if (plates && !g_plateCanvasSaid) {
            g_plateCanvasSaid = true;
            logW("panel: the Transfer page draw's canvas %p is not the engine canvas %p - no plates "
                 "from the page draw (thin frames instead)", canvas, (void*)c);
        }
        return;
    }
    UtRectF gr;
    if (!panelSlotGrid(&gr)) return;   // not on a grid the items do not share
    const float cw = gr.w / (float)kUtPadGridCols;   // the drawn cell (the art edge scales by it)
    if (plates) {
        const int n = protoCount();
        const UtSlotArtBox* art = g_cfg.slotPlates == 3 ? slotArtNow() : nullptr;
        int drawnArt = 0;
        for (int i = 0; i < n; ++i) {
            int col = 0, row = 0, w = 0, h = 0;
            if (!protoSlotAt(i, &col, &row, &w, &h) || w < 1 || h < 1) continue;
            const UtRectF sd = utSlotDrawnRect(gr, col, row, w, h);   // the one helper
            const float x = sd.x, y = sd.y, ww = sd.w, hh = sd.h;
            const float s = cw / 32.0f;   // the texture's cell edge: 2 px left / top, 3 px right / bottom
            if (art && drawSlotArt(c, *art, x + 2.0f * s, y + 2.0f * s, ww - 5.0f * s, hh - 5.0f * s)) {
                ++drawnArt;
                continue;
            }
            fillRect(c, x + 2.0f * s, y + 2.0f * s, ww - 5.0f * s, hh - 5.0f * s, kPlateFill);
        }
        slotArtSay(art, drawnArt, n);
        g_plateFrame = frame;
        g_platesAt = GetTickCount();
        if (!g_platesSaid) {
            g_platesSaid = true;
            logI("panel: slot plates drawn by the Transfer page draw's PRE-detour (TQ.exe+0x%X, pass %d, "
                 "%d slots) under the items", g_tq.sigPageDrawRva, pass, n);
        }
    }
    drawSearchMarks(c, false);   // the search's wash: over the slot art, under the ring and the icon
    drawTintRing(page, c, gr);   // under the icons; the engine tint goes over it
}

namespace {

// ---- the slot-wide item tint ----------------------------------------------------------
// The engine's own colours and condition (TQ.exe 0x16E300 / 0x10A850, utTintKind), asked on the
// mod's prototypes and cached: refreshed when the prototype set changes and every 250 ms (a level
// up or an equipment change moves "requirements met"), never per frame. Everything is a const
// engine read under SEH; nothing is written. the one engine allocation is the
// controller lookup (protoFindObjects: GetObjectList, an engine vector of every world object) - it
// runs only when the world or the main player's controller id changed, never on the 250 ms
// refresh. the ring is drawn in the PRE (drawTintRing), under the icons.
struct Tint {
    TqColor col;
    int kind;   // kUtTint*, kUtTintNone = no ring for this prototype
};
Tint g_tint[kUtProtoMax];
int g_tintN = 0;
unsigned g_tintGen = ~0u;           // protoGeneration() of the cached set
const void* g_tintCtrl = nullptr;   // the controller found for g_tintCtrlGen
unsigned g_tintCtrlId = 0;
unsigned g_tintCtrlGen = ~0u;      // viewWorldGeneration(), not the prototype set
DWORD g_tintAt = 0;
float g_tintInset = 0.0f;   // the inventory's [+0x134] (px at UI scale 1)
bool g_tintOk = false;
bool g_tintSaid = false, g_tintMissSaid = false;

// The UIStashInventory sub-object of the Transfer page (page + 0x88, the page draw's `lea ecx,
// [esi+0x88]`): its shade [+0x114], fails [+0x124] colours and its inset [+0x134] (int px).
bool readInventoryTint(const void* page, TqColor* shade, TqColor* fails, float* inset) {
    bool ok = false;
    __try {
        const unsigned char* inv = (const unsigned char*)page + kUtInvOfPage;   // pinned by test_bindings
        const float* a = (const float*)(inv + kUtInvShade);
        const float* b = (const float*)(inv + kUtInvFails);
        const int k = *(const int*)(inv + kUtInvInset);
        *shade = TqColor{a[0], a[1], a[2], a[3]};
        *fails = TqColor{b[0], b[1], b[2], b[3]};
        *inset = (float)k;
        ok = k >= 0 && k <= 8;
        for (int i = 0; i < 4 && ok; ++i)
            ok = a[i] >= 0.0f && a[i] <= 1.0f && b[i] >= 0.0f && b[i] <= 1.0f;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    return ok;
}

bool tintRefresh(const void* page) {
    const int n = protoCount();
    const unsigned gen = protoGeneration();
    const DWORD now = GetTickCount();
    if (n == g_tintN && gen == g_tintGen && now - g_tintAt < 250) return g_tintOk;
    g_tintN = n;
    g_tintGen = gen;
    g_tintAt = now;
    g_tintOk = false;
    TqGameEngine* ge = gameEngine();
    TqEngine* e = engine();
    if (!ge || !e || !g_tq.GameGetMainPlayer || !g_tq.CharGetControllerId ||
        !g_tq.CtrlGetEquipmentCtrl || !g_tq.EquipAreRequirementsMet ||
        !g_tq.ItemGetActualClassification || !g_tq.GameGetItemColor ||
        !g_tq.GameGetItemBackgroundOpacity || !g_tq.EngineGetOptions || !g_tq.OptionsGetBool) {
        if (!g_tintMissSaid) {
            g_tintMissSaid = true;
            logI("panel: the slot-wide item tint is off - an export of the inventory draw's tint is "
                 "missing (the engine still tints each item's own footprint)");
        }
        return false;
    }
    TqColor shade, fails;
    float inset = 0.0f;
    if (n <= 0 || !readInventoryTint(page, &shade, &fails, &inset)) return false;
    unsigned ctrlId = 0, id0 = 0;
    const char* rec = nullptr;
    int col = 0, row = 0;
    if (!protoAt(0, &rec, &col, &row, &id0)) return false;
    utGuardEnter();
    __try {
        const TqPlayer* player = g_tq.GameGetMainPlayer(ge);
        if (player) ctrlId = g_tq.CharGetControllerId(player);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ctrlId = 0;
    }
    utGuardLeave();
    // the object-list walk only for a new world or controller id.
    // keyed on the WORLD, not the prototype set - a row scroll (the earlier shift) bumps
    // protoGeneration() and must not walk every world object; the generation stays the key of
    // g_tint[] only. item0 is not used after the lookup.
    const unsigned wgen = viewWorldGeneration();
    const void* ctrl = g_tintCtrlId == ctrlId && g_tintCtrlGen == wgen ? g_tintCtrl : nullptr;
    if (!ctrl) {
        const void* item0 = nullptr;
        g_tintCtrl = nullptr;
        g_tintCtrlId = 0;
        if (!ctrlId || !protoFindObjects(id0, ctrlId, &item0, &ctrl) || !ctrl) return false;
        g_tintCtrl = ctrl;
        g_tintCtrlId = ctrlId;
        g_tintCtrlGen = wgen;
    }
    bool ok = true;
    int counts[4] = {0, 0, 0, 0};
    utGuardEnter();
    __try {
        const void* eq = g_tq.CtrlGetEquipmentCtrl((void*)ctrl);
        void* opts = g_tq.EngineGetOptions(e);
        const bool optOn = opts && g_tq.OptionsGetBool(opts, kUtOptItemTint);   // TQ.exe 0x10A86C
        const float alpha = g_tq.GameGetItemBackgroundOpacity(ge, false);
        for (int i = 0; i < n && eq; ++i) {
            const void* item = protoItem(i);
            Tint t = {shade, kUtTintNone};
            if (item) {
                const bool met = g_tq.EquipAreRequirementsMet(eq, item);
                const int cls = met ? g_tq.ItemGetActualClassification(item) : 0;
                TqColorF cf = {1.0f, 1.0f, 1.0f, 1.0f};
                const bool colOk = met && optOn && cls != 0 && g_tq.GameGetItemColor(ge, cls, &cf);
                t.kind = utTintKind(met, optOn, cls, colOk);
                t.col = t.kind == kUtTintFails   ? fails
                        : t.kind == kUtTintClass ? TqColor{cf.r, cf.g, cf.b, alpha}
                                                 : shade;
            }
            g_tint[i] = t;
            ++counts[t.kind & 3];
        }
        ok = eq != nullptr && alpha >= 0.0f && alpha <= 1.0f;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    utGuardLeave();
    g_tintInset = inset;
    g_tintOk = ok;
    if (!ok) g_tintCtrl = nullptr;   // the next refresh looks the controller up again
    if (ok && !g_tintSaid) {
        g_tintSaid = true;
        logI("panel: slot-wide item tint ON - the engine's own colour and condition on each "
             "prototype (EquipmentCtrl::AreRequirementsMet, Item::GetActualItemClassification, "
             "GameEngine::GetItemColor / GetItemBackgroundOpacity; inset %.0f px): %d red, %d by "
             "classification, %d shade of %d",
             inset, counts[kUtTintFails], counts[kUtTintClass], counts[kUtTintShade], n);
    }
    return ok;
}

// the ring between each prototype's footprint and its slot, in the engine's tint.
// drawn in the page draw's PRE, over the plates and UNDER the icons - its inner
// edge is the footprint inset by k (where the engine's own tint starts), so it reaches k px into
// the footprint where the item does not touch the slot edge: that band is under the icon, and the
// engine's tint, drawn over it later, continues it without a gap.
void drawTintRing(void* page, TqCanvas* c, const UtRectF& gr) {
    if (rectRouteLive()) return;   // the engine's own tint is slot-wide (the ring is the fallback)
    if (!tintRefresh(page)) return;
    // the ring skips the prototypes whose background the detour skips (uncollected, not
    // hovered, owned_marks != 0) while the route runs - the hovered slot from the same cursor the
    // route's Begin reads next (a refused widget's footprint is skipped by the detour too).
    // and only when this frame's Begin arms (its gates), not on a transitional
    // frame within rrRecent's 500 ms where the original runs with its footprint tint
    const bool routed = rrRecent() && rrGatesOpen(page);
    if (routed) ownedUnveilRefresh(g_cursorOk, g_cursorX, g_cursorY);
    // the inset [inv+0x134] is px at UI scale 1: x the drawn cell / 32 (= the UI scale)
    const float k = g_tintInset * (gr.w / (float)kUtPadGridCols / 32.0f);
    const int n = protoCount() < g_tintN ? protoCount() : g_tintN;
    for (int i = 0; i < n; ++i) {
        if (g_tint[i].kind == kUtTintNone || rrCovers(i)) continue;   // slot-wide already
        if (ownedBackgroundSkip(i, routed)) continue;   // bare unless hovered
        const char* rec = nullptr;
        int col = 0, row = 0, w = 0, h = 0, sc = 0, sr = 0, sw = 0, sh = 0;
        unsigned id = 0;
        if (!protoAt(i, &rec, &col, &row, &id, &w, &h) || !protoSlotAt(i, &sc, &sr, &sw, &sh))
            continue;
        const UtRectF slot = utRectInset(utSlotDrawnRect(gr, sc, sr, sw, sh), k);
        const UtRectF item = utRectInset(utSlotDrawnRect(gr, col, row, w, h), k);
        UtRectF ring[4];
        const int nr = utSlotRing(slot, item, ring);
        for (int j = 0; j < nr; ++j)
            fillRect(c, ring[j].x, ring[j].y, ring[j].w, ring[j].h, g_tint[i].col);
    }
}

}  // namespace

// the veils, after the LAST page-draw call of the frame (see g_pdStable). Quads only.
// first the grid cover (3(b)) - over the grid cells, under the veils and whatever the engine
// draws later (the tooltip). The tint ring (2) is drawn in the PRE.
void panelPageDrawPost(void* page, void* canvas, int pass) {
    // ON or OFF (the pad is drawn on the real Transfer page too); the veils ON only
    if (!page || page != viewPageWindow() || InterlockedCompareExchange(&g_padOff, 0, 0) ||
        !plateTransferVisible())
        return;
    if (g_pdStable < kPdStable || g_pdCalls != g_pdPrevCalls || pass != g_pdPrevLastPass) return;
    TqCanvas* c = canvasNow();
    if (!c || (void*)c != canvas) return;
    if (viewOn()) {
        pageDrawVeils(c, pass);
    } else {   // the real Transfer page: the search's marks on its items, under the tooltip
        drawSearchMarks(c, true);
        g_realMarksByPage = true;
    }
    // the pad - ground, buttons, lamps, label - after the page draw, so the
    // engine's item tooltip (drawn after the page) lies over it. The rects and the hover are the
    // last Present's layout (panelDraw); the clicks never read this draw.
    if (!g_cfg.groupButtons || !g_groundOk || GetTickCount() - g_layoutAt >= 250) return;
    if (!padGuarded(c)) {
        InterlockedExchange(&g_padOff, 1);
        hideAll();
        logW("panel: the pad's draw FAULTED (after the Transfer page draw) - the pad is off for this "
             "session");
        return;
    }
    g_padPageAt = GetTickCount();
    g_drawnAt = g_padPageAt;
    g_padByPage = true;
    if (!g_padPageSaid) {
        g_padPageSaid = true;
        logI("panel: the pad is drawn after the Transfer page draw (TQ.exe+0x%X POST, pass %d, call %d "
             "of the frame, view %s): the engine's item tooltip goes over it; the Present detour draws "
             "it only in a frame where this did not", g_tq.sigPageDrawRva, pass, g_pdCalls,
             viewOn() ? "ON" : "OFF");
    }
}

namespace {
// The veils and the grid cover: after the LAST page draw of the frame, over
// the items, under the tooltip. The view is ON (panelPageDrawPost).
void pageDrawVeils(TqCanvas* c, int pass) {
    UtRectF gr;
    if (panelSlotGrid(&gr)) {   // not on a grid the items do not share
        if (g_cfg.gridCover) {   // the cells no slot of the window uses
            UtCellRect lo[3];
            const int nl = liveLeftover(lo, 3);
            for (int i = 0; i < nl; ++i) {   // the one helper
                const UtRectF r = utSlotDrawnRect(gr, lo[i].col, lo[i].row, lo[i].w, lo[i].h);
                fillRect(c, r.x, r.y, r.w, r.h, kPadBack);
            }
        }
    }
    const int markStyle = ownedMarksBegin(g_geo, g_cursorOk, g_cursorX, g_cursorY);
    if (markStyle) drawMarks(c, markStyle);
    drawSearchMarks(c, true);   // the search's frame: over the veils, under the tooltip
    g_veilsByPage = true;
    if (!g_veilsPageSaid && markStyle) {
        g_veilsPageSaid = true;
        logI("panel: the owned veils are drawn after the Transfer page draw (pass %d, call %d of the "
             "frame): over the items, under the tooltip", pass, g_pdCalls);
    }
}
}  // namespace

const char* panelStatus() { return g_status; }

}  // namespace ut
