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
// export), the fonts are loaded by name with TQ's LoadFont(name, true, false), the input is the
// game window's procedure, and the text is measured over the game's own fonts (ut_textmeasure.h:
// the advances of Resources\Fonts.arc, read once at start; each string is drawn in the font its
// letters need).

#include "ut_panel.h"

#include "ut_padlayout.h"
#include "ut_layout.h"    // the pad and the label as rectangles (pure)
#include "ut_layoutedit.h"   // the in-game layout editor (pure)
#include "ut_screendraw.h"   // the screen parts' frame and what their input reads (pure)
#include "ut_padinput.h"     // the window procedure's clicks and their latch (pure)
#include "ut_slotart.h"   // the rect route and the equipment-slot art (pure)
#include "ut_graytex.h"   // the gray icons' names (pure)
#include "ut_color.h"     // search_mark_color (pure)
#include "ut_textmeasure.h"   // the text measure and the font that follows the letters (pure)

#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "gen/ut_groups.h"   // the built-in group ids (an id the file may name)
#include "gen/arc_reader.h"  // Resources\Fonts.arc (the start-up font read)
#include "ut_buttons.h"      // the buttons file uniquetab-buttons.ini (pure)
#include "ut_tooltip.h"      // the rollover words

#include "hooks.h"
#include "tq_runtime.h"
#include "ut_bindings.h"
#include "ut_config.h"
#include "ut_gameswitch.h"   // a custom game's gray names
#include "ut_generate.h"   // generateDone, the game in force
#include "ut_live.h"
#include "ut_log.h"
#include "ut_owned.h"
#include "ut_paths.h"   // the gray folder beside the mod
#include "ut_resname.h"   // the engine's 63-character name parts (pure)
#include "ut_plate.h"
#include "ut_proto.h"
#include "ut_screenprobe.h"   // the screen input: ready, the publication
#include "ut_retire.h"
#include "ut_search.h"
#include "ut_view.h"
#include "ut_cover.h"
#include "ut_viewgate.h"
#include "ut_reliclook.h"   // the relic page's look (pure)
#include "ut_ownedfold.h"   // utOwnedNormaliseKey (the relic list's folded key)

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
// the window procedure's clicks (ut_padinput.h utPadMsgDecide): .down = the button a LDOWN landed
// on (its LUP acts), .kept = a swallowed DOWN on a screen part whose own UP is swallowed too
UtPadMsgState g_padMsg = {-1, 0, 0};
DWORD g_drawnAt = 0;      // GetTickCount of the last frame the pad painted
// the pad is drawn by the Transfer page draw's POST (under the engine's tooltip, which is
// drawn after the page); the Present detour draws it only when the POST did not in the last 250 ms
// (the plates' rule). The clicks never read either draw (layout() + the WndProc subclass).
DWORD g_padPageAt = 0;    // the last POST that drew the pad
bool g_padByPage = false;  // the POST drew the pad THIS frame (read by Present)
// the screen parts' draw site (panelRolloverDrawPre): the frame the Transfer page draw last ran
// in (the caravan was drawn before the rollover), the object whose inputs were said (one per
// world), and the once-lines
unsigned long long g_pageFrame = 0;
const void* g_rollSaidFor = nullptr;
bool g_rollSiteSaid = false, g_rollMissSaid = false;
DWORD g_layoutAt = 0;     // the last Present that laid the pad out (the POST draws a fresh layout only)
int g_padPresentRun = 0;  // Present-drawn pad frames in a row (the fallback line after 120)
bool g_padPageSaid = false, g_padPresentSaid = false;
// the notch accumulators (utWheelAccumulate): the Ctrl cycle and the plain wheel
UtWheelAcc g_wheelCtrl, g_wheelPlain;
volatile LONG g_padOff = 0;
const void* g_font = nullptr;
bool g_fontTried = false;
// the game's nine fonts, each loaded by name the first time a string needs it (per world)
const void* g_fonts[kUtTmFontCount] = {};
bool g_fontsTried[kUtTmFontCount] = {};
bool g_capSaid[kUtPadMax] = {};   // the measure against RenderText's float: once per world per button
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

const void* fontAt(int i);   // below: one of the nine fonts, loaded once per world

// One string in the font that draws it (ut_textmeasure.h: `font` when >= 0 - a button's own -,
// else the role's, followed to the font its letters need; the ini's font while no table is in
// force), through the wide call when it is not plain ASCII. Returns RenderText's float, 0 when
// nothing was drawn.
float textIn(TqCanvas* c, float x, float y, const TqColor& col, const char* s, int size, int role,
             int font) {
    if (!s || !*s) return 0.0f;
    const int fi = utTmDrawFont(role, s, font);
    const void* f = fi >= 0 ? fontAt(fi) : nullptr;
    if (!f) f = g_font;
    if (!f) return 0.0f;
    if (utTmIsPlainAscii(s)) {
        if (!g_tq.CanvasRenderTextA) return 0.0f;
        return g_tq.CanvasRenderTextA(c, (int)x, (int)y, &col, s, f, size, 0, 0, 0u, 0, 0, 0);
    }
    if (!g_tq.CanvasRenderTextW) return 0.0f;
    unsigned short w[256];
    utWidenUtf8(s, w, sizeof(w) / sizeof(w[0]));
    return g_tq.CanvasRenderTextW(c, (int)x, (int)y, &col, (const wchar_t*)w, f, size, 0, 0, 0u, 0, 0, 0);
}

void text(TqCanvas* c, float x, float y, const TqColor& col, const char* s, int size) {
    textIn(c, x, y, col, s, size, kUtTmCaption, -1);
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

// A font by NAME (TQ LoadFont(name, override=true, flag=false): the engine's own callers'
// arguments). An unknown name faults inside the engine: caught, nullptr. *refused: the name has a
// part the engine does not take (ut_resname.h) - it is not handed over (the caller says so once).
const void* loadFontNamed(const char* nm, bool* refused) {
    *refused = false;
    TqEngine* e = engine();
    if (!e || !nm || !g_tq.EngineGetGraphicsEngine || !g_tq.GfxLoadFont) return nullptr;
    TqStdString name;
    const size_t n = strnlen(nm, 255);
    if (n == 0 || n >= 16 * 16) return nullptr;
    if (!utResNameOk(nm, n)) {
        *refused = true;
        return nullptr;
    }
    static char heap[256];
    if (!tqStdStringOver(&name, heap, sizeof(heap), nm, n)) return nullptr;
    const void* f = nullptr;
    utGuardEnter();
    __try {
        TqGraphicsEngine* gfx = g_tq.EngineGetGraphicsEngine(e);
        if (gfx) f = g_tq.GfxLoadFont(gfx, &name, 1u, 0u);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        f = nullptr;
    }
    utGuardLeave();
    return f;
}

// GraphicsEngine::GetFallbackFonts, once per process: how many fonts the engine falls back to,
// each by pointer, named when it is a font the tab loaded.
void logFallbackFontsOnce() {
    static bool said = false;
    if (said || !g_tq.GfxGetFallbackFonts || !g_tq.EngineGetGraphicsEngine) return;
    said = true;
    TqEngine* e = engine();
    if (!e) return;
    const void* items[8] = {};
    int n = -1;
    utGuardEnter();
    __try {
        TqGraphicsEngine* gfx = g_tq.EngineGetGraphicsEngine(e);
        const TqFontVector* v = gfx ? g_tq.GfxGetFallbackFonts(gfx) : nullptr;
        if (v && v->last >= v->first && v->last - v->first < 4096) {
            n = (int)(v->last - v->first);
            for (int k = 0; k < n && k < 8; ++k) items[k] = v->first[k];
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        n = -1;
    }
    utGuardLeave();
    if (n < 0) {
        logI("fonts: GraphicsEngine::GetFallbackFonts could not be read");
        return;
    }
    char list[512];
    list[0] = 0;
    size_t at = 0;
    for (int k = 0; k < n && k < 8; ++k) {
        const char* nm = items[k] && items[k] == g_font ? g_cfg.fontName : "";
        for (int i = 0; i < kUtTmFontCount; ++i)
            if (items[k] && items[k] == g_fonts[i]) nm = utTmFontPath(i);
        const int w = _snprintf_s(list + at, sizeof(list) - at, _TRUNCATE, "%s%p%s%s%s", k ? ", " : "", items[k],
                                  *nm ? " (" : "", nm, *nm ? ")" : "");
        if (w < 0) break;
        at += (size_t)w;
    }
    logI("fonts: GraphicsEngine::GetFallbackFonts holds %d font(s)%s%s%s", n, n ? ": " : "", list,
         n > 8 ? ", ..." : "");
}

// The ini's caption font by NAME, once per world. An unknown name: said once, no captions.
void ensureFont() {
    if (g_font || g_fontTried || !g_tq.GfxLoadFont) return;
    g_fontTried = true;
    if (!engine() || !g_tq.EngineGetGraphicsEngine) return;
    bool refused = false;
    const void* f = loadFontNamed(g_cfg.fontName, &refused);
    g_font = f;
    if (f) {
        logI("panel: font \"%s\" loaded by name (%p)", g_cfg.fontName, f);
        logFallbackFontsOnce();
    } else if (refused) {
        logW("panel: font \"%s\" has a part of %zu characters - the engine takes 1..63 between '/' or '\\', so it is NOT loaded - the pad draws without captions",
             g_cfg.fontName, utResLongestPart(g_cfg.fontName));
    } else {
        logW("panel: font \"%s\" could not be loaded - the pad draws without captions",
             g_cfg.fontName);
    }
}

// One of the nine fonts, loaded by name the first time a string needs it and kept for the world
// (the ini's caption font is that same load). Said once per world when it cannot be loaded; the
// measure then treats it as a font without a table (utTmRefuse), so its strings are measured in
// the font that draws them from the next frame on.
const void* fontAt(int i) {
    if (i < 0 || i >= kUtTmFontCount) return nullptr;
    if (i == utTmFontOfPath(g_cfg.fontName)) {
        ensureFont();
        return g_font;
    }
    if (g_fonts[i] || g_fontsTried[i]) return g_fonts[i];
    g_fontsTried[i] = true;
    bool refused = false;
    g_fonts[i] = loadFontNamed(utTmFontPath(i), &refused);
    if (g_fonts[i]) {
        logI("panel: font \"%s\" loaded by name (%p) for the letters that need it", utTmFontPath(i), g_fonts[i]);
    } else if (refused) {
        utTmRefuse(i);
        logW("panel: font \"%s\" has a part of %zu characters - the engine takes 1..63 between '/' or '\\', so it is NOT loaded - its text is measured and drawn in another "
             "font", utTmFontPath(i), utResLongestPart(utTmFontPath(i)));
    } else {
        utTmRefuse(i);
        logW("panel: font \"%s\" could not be loaded - its text is measured and drawn in another font (\"%s\" "
             "when no other has its letters)", utTmFontPath(i), g_cfg.fontName);
    }
    return g_fonts[i];
}

// ---- the live frame and the pad's rects ----------------------------------------------
// The frame verdict for the current window geometry (canvas size + UI scale): 0 unmeasured, 1
// accepted (the pad follows g_frame), -1 refused (no pad / label while ON).
int g_frameVerdict = 0;
UtRectF g_frame = {0.0f, 0.0f, 0.0f, 0.0f};
UtRectF g_frameGrid = {0.0f, 0.0f, 0.0f, 0.0f};
// The collection page's grid in force, drawn (canvas px): the measured grid, or the whole cells of
// the player's grid_ rectangle whenever its line resolves (gridApply). The page on screen shows it
// only while the view is ON (shownGrid, read at the draw); the per-slot overlays and the owned grid
// check read it through panelFrameGrid; the frame stays measured on the derived grid.
UtRectF g_gridLive = {0.0f, 0.0f, 0.0f, 0.0f};
int g_gridUser = 0, g_gridCols = 16, g_gridRows = 15;
UtRect g_gridRec = {27, 126, 512, 480};      // the collection's grid in force, record px (frame-local)
UtRect g_gridDefRec = {27, 126, 512, 480};   // the derived grid, record px (frame-local)
// The game's derived area (uniq-geometry.txt's P line, at the measured grid's origin), drawn: the
// collection's own grid when no grid line is in force (g_areaGrid, g_areaCols x g_areaRows:
// utCaravanCollectionCells) and the whole area (g_areaFull) the Ctrl+wheel answers over. Written by
// layout() every frame; 16 x 15 = the measured grid in the base game. g_areaRec: the collection's
// derived grid in record px (the P line's origin, its whole cells), the lattice's grid in force.
UtRectF g_areaGrid = {0.0f, 0.0f, 0.0f, 0.0f}, g_areaFull = {0.0f, 0.0f, 0.0f, 0.0f};
int g_areaCols = 16, g_areaRows = 15;
UtRect g_areaRec = {27, 126, 512, 480};
int g_gridArea = 0;   // 1 = the collection's grid is the derived area, larger than the real sack
int g_areaSaid[4] = {0, 0, 0, 0};   // the area said last (cols, rows, full cols, full rows)
int g_gridFailCols = 0, g_gridFailRows = 0;   // the cells the groups could not be packed on
bool g_gridPlateSaid = false;
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
// THE LAYOUT (ut_layout.h): the derived pad and label, the player's [layout] rectangles for this
// caravan window size, and what is in force. layout() is the only writer.
UtLayoutLive g_lay;
// The collection's resolution while the view is OFF (gridApply's `coll`): the grid line resolved as
// the view ON would resolve it; on the real Transfer page only its pad and label are drawn
// (utLayoutRealPage), never its grid.
UtLayoutLive g_layColl;
// the derived set with the pad and the label back in the window (the area keeps the pad's band):
// the layout editor's RESET while the pad and the label are on the screen (else g_lay's serves)
UtLayoutLive g_layWin;
bool g_layWinOk = false;
UtRectF g_areaGridWin = {0.0f, 0.0f, 0.0f, 0.0f};
bool g_areaWinOk = false;
bool g_layOk = false;
bool g_layRealColl = false;   // this frame's real page took g_layColl's pad and label
// THE PAGE KIND (ut_layout.h "the relic pages"): 1 = a relic page is shown (the view ON, the relic
// group's page) - the page on screen takes the relic area, the uniques stay packed on the
// collection's grid. Written by layout() each frame, read by gridApply and the editor.
bool g_relicPage = false;
char g_relicRefusedSaid[96] = "";   // the refused relics_ line said last (once per change)
// The collection's grid in force (record px, frame-local) and the relic pages' area, as the last
// gridApply resolved them.
UtRect g_collRec = {27, 126, 512, 480};
bool g_collRecUser = false;   // g_collRec is the player's grid line (else the derived grid)
UtRelicArea g_relicArea = {0, {27, 126, 512, 480}, 16, 15};
bool g_relicAreaOk = false;
// THE REAL TRANSFER PAGE'S PAD (GD's materials-page memo): the pad and the label stay where the
// collection page has them, drawn over the real cells they cover. Each decision is logged once it
// has held kRealHoldMs (a page switch's first frames never reach the log), again when the pad's or
// the label's rectangle or a covered count changes; kRealLogLines lines per game, then one line
// says the rest are not logged. Game thread only.
struct RealMemo {
    UtRect pad, label;
    int padUnder, labelUnder, labelUser;
    DWORD since;      // when the decision being held began (0 = none)
    int logged;       // 1 = last* is the decision the log holds
    UtRect lastPad, lastLabel;
    int lastPadUnder, lastLabelUnder, lastLabelUser;
    int lines;        // decisions logged in this game
};
RealMemo g_real = {};
const DWORD kRealHoldMs = 250;
const int kRealLogLines = 12;
int g_realKeepSaid = 0;   // bits: the once-per-game DEBUG lines of the kept click and the refusal
int g_padPage = 0;          // the pad's page (the shown group's) while the groups are paged
int g_padNextTarget = 0;    // NEXT's group: the first of the next page
char g_layWarned[kUtLayParts][128];   // the last refusal said per part (once per change)
char g_laySaid[320];                  // the last "layout:" line said
UtRectF g_userLabelR = {0.0f, 0.0f, 0.0f, 0.0f};

// The pad's buttons as the game thread resolved them from the buttons file (the built-in ones -
// one per group, its short name - before the first resolve): defined with the words below.
// Each reads one generation of the buttons: a layout, a draw or a click takes the words in force
// once (btnWords) and hands them to every call.
struct BtnWords;
const BtnWords* btnWords();
int labelSizeOf(const BtnWords* w);                  // [tab] size.label, else plate_label_size
int padNextChars(const BtnWords* w);                 // NEXT's caption, characters
int padButtons(const BtnWords* w);                  // N, file order
int padFirst(const BtnWords* w, int b);             // button b's first group: the group its click selects
int padButtonOf(const BtnWords* w, int g);          // group g's button; -1 = on no button ([unassigned])
unsigned padMask(const BtnWords* w, int b);         // button b's groups as bits (the search marks)
const char* padCaption(const BtnWords* w, int b);   // its caption
int padSize(const BtnWords* w, int b);              // its caption size cap, -1 = none
int padFont(const BtnWords* w, int b);              // its own font (a UtTmFontId), -1 = the role's
const char* padName(const BtnWords* w, int b);      // its name (the label's {group})

void hideAll() {
    for (int i = 0; i < kUtPadMax; ++i) g_btnShown[i] = false;
    g_groundOk = false;
    g_labelOk = false;
}

// The widest button caption (OWN's three letters at least; a caption longer than a short name
// counts as one - it is fitted on its own button): what the caption rule of the pad's cells is
// fitted to.
int padWidestCaption(const BtnWords* w, int buttons) {
    int widest = 3;
    for (int i = 0; i < buttons && i < kUtPadCells; ++i) {
        const int n = utPadFitChars(utTmCodePoints(padCaption(w, i)));
        if (n > widest) widest = n;
    }
    return widest;
}

// One WARN per refused [layout] rectangle and one INFO line per change of the layout in force
// (GD's words), never per frame.
void layoutSay(const UtLayoutLive& lay, const char* const val[kUtLayParts], float s) {
    const UtLayout& L = lay.L;
    for (int p = kUtLayPad; p < kUtLayParts; ++p) {
        char msg[128];
        msg[0] = 0;
        if (lay.in.user[p] && L.why[p] != kUtLayoutOk)
            _snprintf_s(msg, sizeof(msg), _TRUNCATE, "%s_%s=%s|%d|%d|%.3f", utLayoutPartName(p),
                        lay.cover, val[p], L.why[p], L.against[p], s);
        if (strcmp(msg, g_layWarned[p]) != 0) {
            strcpy_s(g_layWarned[p], sizeof(g_layWarned[p]), msg);
            char warn[320];
            if (msg[0] && utLayoutRefusalText(lay, p, val[p], s, warn, sizeof(warn))) logW("%s", warn);
        }
    }
    char said[320];
    utLayoutSaidText(lay, s, said, sizeof(said));
    if (strcmp(said, g_laySaid) != 0) {
        strcpy_s(g_laySaid, sizeof(g_laySaid), said);
        logI("%s", said);
    }
}

// The grid in force (utGridInForce, ut_layout.h): the collection is packed on the player's grid_
// rectangle whenever its line resolves - `coll` is that resolution, made with the view ON or OFF
// alike - else on the derived (measured) grid; the page on screen shows the player's grid only
// while the view is ON (OFF: the real Transfer page's own grid, never moved). Its whole cells
// (utLayoutGridCells under the caravan window's box cap) are drawn from the rectangle's top-left
// at the mod sack's live cell (the engine's cell formula until the sack is built). The live page
// is packed on its cells and the view moves and sizes the collection sack to it. Every frame;
// nothing allocates unless the cells change (the re-pack, ut_live liveSetGrid: only on a change
// of the cell count); each change is said once (ut_live, ut_view).
void gridApply(const PlateGeometry& g, const UtLayoutLive& lay, const UtLayoutLive* coll, int relicLine) {
    UtGridForceIn in = {};
    in.on = 1;   // the collection page's grid; the real page's is picked at the draw (shownGrid)
    in.userSet = coll && coll->L.user[kUtLayGrid] ? 1 : 0;
    if (in.userSet) in.userRect = coll->L.rect[kUtLayGrid];
    liveCaravanWindow(&in.coverW, &in.coverH, nullptr, nullptr);   // the box cap
    in.frame = g_frame;
    in.frameGrid = g_areaGrid;   // the derived grid: the game's area at the real sack's origin
    in.scale = g.scale;
    if (!panelCellPx(&in.cell)) in.cell = utEngineCellPx(g.engineScale);   // the engine's own cell
    in.derivedCols = g_areaCols;   // the real sack's 16 x 15 unless the game's area is larger
    in.derivedRows = g_areaRows;
    in.failCols = g_gridFailCols;
    in.failRows = g_gridFailRows;
    UtGridForce f = utGridInForce(in);
    if (f.packUser) {
        liveSetGrid(f.packCols, f.packRows);
        int lc = 0, lr = 0;
        liveGridCells(&lc, &lr);
        if (lc != f.packCols || lr != f.packRows) {   // a group could not be packed on them (said once)
            g_gridFailCols = in.failCols = f.packCols;
            g_gridFailRows = in.failRows = f.packRows;
            f = utGridInForce(in);
        }
    }
    if (!f.packUser) liveSetGrid(f.packCols, f.packRows);
    // a relic page: the page on screen, the collection sack and the lattice take the relic area -
    // its relics_ rectangle while the layout in force resolved it (`lay`), else the collection's
    // grid in force (`f` itself); the groups stay packed on the collection's grid above
    g_collRec = f.packUser ? in.userRect : lay.in.def[kUtLayGrid];
    g_collRecUser = f.packUser != 0;
    const int relicSet = g_relicPage && relicLine && lay.L.user[kUtLayGrid] ? 1 : 0;
    const UtRect relicRect = relicSet ? lay.L.rect[kUtLayGrid] : g_collRec;
    g_relicArea = utRelicAreaOf(relicSet ? &relicRect : nullptr, g_collRec, f.packCols, f.packRows,
                                in.coverW, in.coverH);
    g_relicAreaOk = true;
    // the relic layout follows the relic area's cells and the two gap settings
    if (g_relicPage)
        liveSetRelicGeometry(g_relicArea.cols, g_relicArea.rows, g_cfg.relicBlockGap,
                             g_cfg.relicBonusGap);
    const UtGridForce d = g_relicPage ? utRelicGridInForce(in, f, relicSet, relicRect) : f;
    const UtRect drawnRec = relicSet ? relicRect : in.userRect;
    g_gridLive = d.drawn;
    g_gridUser = d.drawnUser;
    // the derived grid larger than the real sack (the game's caravan area: 20 x 20 in a 20 x 20
    // window, 20 x 19 while the derived pad keeps its band in the window): the collection sack is
    // sized for it as for a player's grid
    g_gridArea = !d.packUser && (d.packCols != kUtPadGridCols || d.packRows != kUtPadGridRows) ? 1 : 0;
    g_gridCols = d.drawnCols;
    g_gridRows = d.drawnRows;
    g_gridRec = d.drawnUser ? drawnRec : lay.in.def[kUtLayGrid];
    g_gridDefRec = lay.in.def[kUtLayGrid];
    // OFF the want is kept for the next ON (ut_view moves nothing while the view is OFF); the
    // collection sack is sized and placed for a grid of the player's or a derived area larger than
    // the real sack (ut_view viewAccessorGrid: only while the view is ON and the page holds it)
    viewSetGrid(d.packUser != 0 || g_gridArea != 0, d.dx, d.dy, d.packCols, d.packRows);
    // the caravan background's lattice follows the collection's grid in force (the player's, or
    // the game's area larger than the real sack), drawn by ut_cover while the view is ON
    // (a relic page with the grid cover: the lattice erased, utRelicLatticeOf)
    const UtLatticeGrid lat = utRelicLatticeOf(g_relicPage, g_cfg.gridCover != 0,
                                               utLatticeGridOf(d, drawnRec, g_gridArea, g_areaRec));
    coverSetGrid(lat.user != 0, lat.x, lat.y, lat.cols, lat.rows);
    const int user = d.drawnUser;
    if (user && viewOn() && !g_gridPlateSaid) {   // said for the collection page
        g_gridPlateSaid = true;
        logI("layout: a user grid is in force - the caravan window's background is repainted with "
             "the lattice of its cells (layout plates)");
    }
}

// The grid the page on screen shows, picked when it is read (utGridInForce's drawn grid for the
// view's state: the first frame after a toggle never draws on the other page's grid): the
// collection's grid in force while the view is ON, the real Transfer page's own grid while OFF.
// shownColl: the collection's own grid is on screen (a player's grid, or the game's area larger
// than the real sack); shownUser: that grid is the player's.
bool shownUser() { return g_gridUser != 0 && viewOn(); }
bool shownColl() { return (g_gridUser != 0 || g_gridArea != 0) && viewOn(); }
UtRectF shownGrid() { return shownColl() ? g_gridLive : g_frameGrid; }
int shownCols() { return shownColl() ? g_gridCols : kUtPadGridCols; }
int shownRows() { return shownColl() ? g_gridRows : kUtPadGridRows; }
// The layout's grid (the editor's outline and its rectangle): the grid on screen while the view is
// ON; OFF the derived area the real page's pad and label are laid out against (layout(): src.grid),
// the real sack's 16 x 15 in the base game.
UtRectF layGrid() { return viewOn() ? shownGrid() : g_areaGrid; }
int layCols() { return viewOn() ? shownCols() : g_areaCols; }
int layRows() { return viewOn() ? shownRows() : g_areaRows; }

// A refused screen key: one WARN per change (the part stays in the window). "Not placed yet" is
// the normal state before the page was drawn once: DEBUG. A screen key while the screen input is
// not installed: one WARN for the session (the parts stay in the caravan window).
char g_scrWarned[kUtLayParts][128] = {};
bool g_scrInputSaid = false;
void layoutSayScreen(const UtLayoutLive& lay, const char* padScr, const char* labelScr, float s) {
    const UtLayout& L = lay.L;
    for (int p = kUtLayPad; p < kUtLayParts; ++p) {
        const char* v = p == kUtLayPad ? padScr : labelScr;
        char m2[128] = {0};
        if (v && v[0]) {
            if (!lay.in.screenSet[p])
                _snprintf_s(m2, sizeof(m2), _TRUNCATE, "%s_screen=%s is not tl|tr|bl|br,x,y,w,h",
                            utLayoutPartName(p), v);
            else if (L.screenWhy[p] != kUtLayoutOk)
                _snprintf_s(m2, sizeof(m2), _TRUNCATE, "%s_screen=%s %s", utLayoutPartName(p), v,
                            utLayoutWhyText(L.screenWhy[p]));
        }
        if (strcmp(m2, g_scrWarned[p]) == 0) continue;
        strcpy_s(g_scrWarned[p], sizeof(g_scrWarned[p]), m2);
        if (!m2[0]) continue;
        if (lay.in.screenSet[p] && L.screenWhy[p] == kUtLayoutScreenNoView) {
            if (!screenInputReady() && !g_scrInputSaid) {
                g_scrInputSaid = true;
                logW("buttons: the screen input is not installed - pad_screen / label_screen are not "
                     "used this session: the pad and the label stay in the caravan window");
            }
            logD("layout: %s at UI scale %.3f - the %s stays in the window until the page has been "
                 "drawn once", m2, s, utLayoutPartName(p));
        } else {
            logW("layout: %s at UI scale %.3f - the %s stays in the window", m2, s,
                 utLayoutPartName(p));
        }
    }
}

// A screen part is in force (drawn from the Present detour, its clicks consumed by the screen
// input).
bool layScreenParts() {
    return g_layOk && (g_lay.L.screen[kUtLayPad] || g_lay.L.screen[kUtLayLabel]);
}

// The canvas point lies on a screen part in force (its ground and its 1-px frame).
bool onScreenPart(float x, float y) {
    if (!layScreenParts()) return false;
    if (g_lay.L.screen[kUtLayPad] && g_groundOk) {
        const UtScreenFrame f = utScreenFrame(g_ground, 1);
        if (x >= (float)f.ox && x < (float)(f.ox + f.ow) && y >= (float)f.oy && y < (float)(f.oy + f.oh))
            return true;
    }
    if (g_lay.L.screen[kUtLayLabel] && g_labelOk) {
        const UtScreenFrame f = utScreenFrame(g_labelR, 1);
        if (x >= (float)f.ox && x < (float)(f.ox + f.ow) && y >= (float)f.oy && y < (float)(f.oy + f.oh))
            return true;
    }
    return false;
}

// The real Transfer page's pad as the log says it (GD's materials-page rule, one line per change
// once it has held kRealHoldMs): the real sack's cells the pad and the player's label cover (INFO
// when they cover some, DEBUG when the cells are clear). Called by layout() while the view is OFF.
void realPadNote() {
    const UtLayout& L = g_lay.L;
    const int cols = kUtPadGridCols, rows = kUtPadGridRows;   // the game's own sack, never moved
    const int padUnder = g_groundOk ? utRealCellsCovered(g_frameGrid, cols, rows, g_ground) : 0;
    const int labelUser = L.user[kUtLayLabel] && g_labelOk ? 1 : 0;
    const int labelUnder = labelUser ? utRealCellsCovered(g_frameGrid, cols, rows, g_labelR) : 0;
    if (!g_groundOk) return;
    const DWORD now = GetTickCount();
    RealMemo& m = g_real;
    if (!m.since || !utRectEq(m.pad, L.rect[kUtLayPad]) || !utRectEq(m.label, L.rect[kUtLayLabel]) ||
        m.padUnder != padUnder || m.labelUnder != labelUnder || m.labelUser != labelUser) {
        m.pad = L.rect[kUtLayPad];
        m.label = L.rect[kUtLayLabel];
        m.padUnder = padUnder;
        m.labelUnder = labelUnder;
        m.labelUser = labelUser;
        m.since = now ? now : 1;
    }
    if (now - m.since < kRealHoldMs) return;
    if (m.logged && utRectEq(m.lastPad, m.pad) && utRectEq(m.lastLabel, m.label) &&
        m.lastPadUnder == padUnder && m.lastLabelUnder == labelUnder && m.lastLabelUser == labelUser)
        return;
    m.logged = 1;
    m.lastPad = m.pad;
    m.lastLabel = m.label;
    m.lastPadUnder = padUnder;
    m.lastLabelUnder = labelUnder;
    m.lastLabelUser = labelUser;
    if (m.lines > kRealLogLines) return;
    if (++m.lines > kRealLogLines) {
        logI("buttons: the real Transfer page's pad changed %d times in this game - further changes "
             "are not logged until the next one", kRealLogLines);
        return;
    }
    char text[320];
    utRealPadText(g_lay, padUnder, labelUnder, cols * rows, text, sizeof(text));
    if (padUnder > 0 || labelUnder > 0)
        logI("buttons: the real Transfer page: %s - the pad is drawn over them, and a click or a hover "
             "on it is the pad's", text);
    else
        logD("buttons: the real Transfer page: %s - the pad is drawn where it is", text);
}

// The game's own Transfer page (the view OFF): the canvas point lies inside the pad drawn there
// (its ground, between the buttons too), the label of the player's or a screen part's frame. They
// lie over the real cells, so such a point is theirs: a press there never reaches the cell under it
// (utPadMsgDecide's onRealPad, panelPadClaimsPress) and neither does the hover
// (panelRealPadOwnsCursor).
bool realPadAt(float x, float y) {
    if (viewOn() || !g_groundOk || GetTickCount() - g_drawnAt >= 500) return false;
    if (utRectHas(g_ground, x, y)) return true;
    if (g_labelOk && utRectHas(g_labelR, x, y)) return true;
    return onScreenPart(x, y);
}

// THE geometry: draw and click both read g_btnR / g_btnShown, which only this function writes.
void layout(const PlateGeometry& g, bool on) {
    hideAll();
    g_layOk = false;
    const BtnWords* pw = btnWords();   // one generation of the buttons for the whole layout
    if (g_frameVerdict == 1) {
        // the field whenever search=1 and the view is ON, or the real Transfer page is shown with
        // search_transfer=1 - drawn disabled while it cannot take keys, so row 2 never changes its
        // shape
        const bool search = utPadFieldLaidOut(on, g_cfg.search, g_cfg.searchTransfer);
        const int buttons = padButtons(pw);   // row 1: one cell per button of the buttons file
        // the [layout] lines of this caravan window size (the cover key: the game's caravan window,
        // W x H - liveCaravanWindow)
        int coverW = 0, coverH = 0;
        liveCaravanWindow(&coverW, &coverH, nullptr, nullptr);
        char cover[16], padVal[kUtCfgFamilyValueCap], labelVal[kUtCfgFamilyValueCap];
        utCoverKeyFormat(coverW, coverH, cover, sizeof(cover));
        const char* pv = configFamilyGet(g_cfg.layPad, cover);
        const char* lv = configFamilyGet(g_cfg.layLabel, cover);
        // pad_screen / label_screen (copied: the worker publishes g_cfg once a second)
        char padScr[sizeof(g_cfg.padScreen)], labelScr[sizeof(g_cfg.labelScreen)];
        strncpy_s(padScr, sizeof(padScr), g_cfg.padScreen, _TRUNCATE);
        strncpy_s(labelScr, sizeof(labelScr), g_cfg.labelScreen, _TRUNCATE);
        // copied first: the worker publishes g_cfg once a second, a torn value fails the parse
        padVal[0] = labelVal[0] = 0;
        if (pv) strncpy_s(padVal, sizeof(padVal), pv, _TRUNCATE);
        if (lv) strncpy_s(labelVal, sizeof(labelVal), lv, _TRUNCATE);
        // the grid: read with the view ON or OFF (the collection stays packed on it); the page on
        // screen resolves with it only while the view is ON - the real Transfer page resolves with
        // its own (derived) grid, so no pad or label of the player's lies over its live cells
        char gridVal[kUtCfgFamilyValueCap];
        gridVal[0] = 0;
        const char* gv = configFamilyGet(g_cfg.layGrid, cover);
        if (gv) strncpy_s(gridVal, sizeof(gridVal), gv, _TRUNCATE);
        // the relic pages' area (relics_<W>x<H>): the page on screen takes it while a relic page
        // is shown; the collection keeps its grid line (utPageGridKeys)
        char relicsVal[kUtCfgFamilyValueCap];
        relicsVal[0] = 0;
        const char* rv = configFamilyGet(g_cfg.layRelics, cover);
        if (rv) strncpy_s(relicsVal, sizeof(relicsVal), rv, _TRUNCATE);
        g_relicPage = on && utRelicGroupIs(liveGroupId(liveShownGroup()));
        // The game's derived area (uniq-geometry.txt): the collection's grid while no grid line is
        // in force, at the measured grid's origin and the drawn cell. The derived pad and label are
        // laid out against it on both pages, so they keep their place when the view toggles; in the
        // base game it is the measured grid itself.
        {
            UtCaravanGeo geo = utCaravanGeoBase();
            int ac = 16, ar = 15;
            // the cells come from the records alone; the drawn cell is the mod sack's live cell
            // when proven, else the engine's formula (as the player's grid: utGridInForce)
            float cell = 0.0f;
            if (!panelCellPx(&cell)) cell = utEngineCellPx(g.scale);
            // the pad's band below the area is kept while the derived pad or label is laid out in
            // the window; with the pad on the screen and its label too (label_screen, or the
            // screen pad's own box: no label line) the area takes all its rows
            const int padBand = utCaravanPadBandFor(padScr[0] != 0, labelScr[0] != 0, labelVal[0] != 0,
                                                    screenInputReady());
            const bool have = cell > 0.0f && liveCaravanGeometry(&geo) &&
                              utCaravanCollectionCells(geo, padBand, &ac, &ar);
            const int fc = have ? (geo.cols > ac ? geo.cols : ac) : 16;
            const int fr = have ? (geo.rows > ar ? geo.rows : ar) : 15;
            g_areaCols = ac;
            g_areaRows = ar;
            if (have) g_areaRec = UtRect{geo.x0, geo.y0, ac * kUtCaravanCellPx, ar * kUtCaravanCellPx};
            utCaravanAreaRects(g_frameGrid, have ? cell : 0.0f, ac, ar, fc, fr, &g_areaGrid, &g_areaFull);
            // the same area with the pad's band kept (the pad and the label back in the window)
            g_areaWinOk = false;
            if (padBand == 0) {
                int wc = 16, wr = 15;
                UtRectF wFull;
                if (have && utCaravanCollectionCells(geo, kUtCaravanPadBand, &wc, &wr)) {
                    utCaravanAreaRects(g_frameGrid, cell, wc, wr, fc, fr, &g_areaGridWin, &wFull);
                    g_areaWinOk = true;
                }
            }
            if (g_areaSaid[0] != ac || g_areaSaid[1] != ar || g_areaSaid[2] != fc || g_areaSaid[3] != fr) {
                g_areaSaid[0] = ac;
                g_areaSaid[1] = ar;
                g_areaSaid[2] = fc;
                g_areaSaid[3] = fr;
                if (fc != 16 || fr != 15)
                    logI("layout: the game's caravan area is %d x %d cells (uniq-geometry.txt, caravan window "
                         "%dx%d) - the collection's own grid is %d x %d cells at (%.0f,%.0f) %.0fx%.0f px (the "
                         "rows below keep a band of %d record px for the pad - 0 with pad_screen and the label on the screen); the "
                         "game's Transfer page keeps its "
                         "16 x 15 sack; Ctrl+wheel answers over the whole area",
                         fc, fr, geo.windowW, geo.windowH, ac, ar, g_areaGrid.x, g_areaGrid.y, g_areaGrid.w,
                         g_areaGrid.h, padBand);
            }
        }
        UtLayoutSource src;
        src.frame = g_frame;
        src.grid = g_areaGrid;
        src.scale = g.scale;
        src.canvasW = (float)g.canvasW;
        src.canvasH = (float)g.canvasH;
        src.coverW = coverW;
        src.coverH = coverH;
        src.pad = padVal[0] ? padVal : nullptr;
        src.label = labelVal[0] ? labelVal : nullptr;
        UtCollGridKeys keys = utPageGridKeys(on ? 1 : 0, g_relicPage ? 1 : 0, gridVal, relicsVal);
        src.gridKey = keys.page;
        int slotW = 0, slotH = 0;
        if (liveMaxSlot(&slotW, &slotH)) {   // a grid of the player's holds the largest slot
            src.maxCellW = slotW * kUtGridCell;
            src.maxCellH = slotH * kUtGridCell;
        }
        src.groups = buttons > 0 ? buttons : kUtPadDefCells;
        src.widest = padWidestCaption(pw, buttons);
        src.nextChars = padNextChars(pw);
        src.labelPx = utPlateLabelPx(labelSizeOf(pw), g.scale);
        src.search = search;
        src.edit = g_cfg.editButton != 0;   // EDIT in row 2's cell 7 (on both pages), the label after it
        src.padScreen = padScr[0] ? padScr : nullptr;
        src.labelScreen = labelScr[0] ? labelScr : nullptr;
        // a screen part is placed only while its clicks can be consumed (the screen input)
        src.screenKnown = screenInputReady() ? 1 : 0;
        bool layIn = utLayoutFromSource(src, &g_lay);
        // a relics_ line the layout refuses (outside the window, over the pad or the label): the
        // relic page shows the collection's grid in force instead; said once per change
        if (g_relicPage) {
            char refused[96];
            refused[0] = 0;
            int why = kUtLayoutOk;
            if (layIn && keys.relic && !g_lay.L.user[kUtLayGrid]) {
                _snprintf_s(refused, sizeof(refused), _TRUNCATE, "relics_%s=%s", cover, relicsVal);
                why = g_lay.L.why[kUtLayGrid];
                keys = utCollGridKeys(1, gridVal);
                src.gridKey = keys.page;
                layIn = utLayoutFromSource(src, &g_lay);
            }
            if (strcmp(refused, g_relicRefusedSaid) != 0) {
                strcpy_s(g_relicRefusedSaid, sizeof(g_relicRefusedSaid), refused);
                if (refused[0])
                    logW("layout: %s is not in force (%s) - the relic pages show the item grid", refused,
                         utLayoutWhyText(why));
            }
        }
        // the derived set with every part back in the window (the editor's RESET): the area keeps
        // its band, the derived pad and label lie in it
        g_layWinOk = false;
        if (layIn && g_areaWinOk) {
            UtLayoutSource sw = src;
            sw.grid = g_areaGridWin;
            sw.padScreen = nullptr;
            sw.labelScreen = nullptr;
            g_layWinOk = utLayoutFromSource(sw, &g_layWin);
        }
        const UtPadOut& o0 = g_lay.defOut;
        // the band's lines only while the derived pad can be in force (never while the pad is pinned
        // on the screen: no band pad is placed then)
        const int band = g_lay.defBand;
        const bool bandLines = utPadBandLinesWanted(g_lay.L);
        const int bandKey = bandLines ? band : -1000;
        if (bandKey != g_padBandSaid) {   // said once per change (a new frame, an ini edit)
            g_padBandSaid = bandKey;
            // ONE drawn cell - the grid / 16, the mod sack's live cell, the engine's formula
            float cell = 0.0f;
            const bool cellOk = panelCellPx(&cell);
            if (!cellOk) cell = g_frameGrid.w / (float)kUtPadGridCols;
            if (!bandLines) {   // the pad is pinned on the screen: no band pad is placed
            } else if (band > 0) {
                logI("panel: pad in the band below the grid at (%.0f,%.0f) %.0fx%.0f, cell %.0f px, "
                     "scale %.3f - rows %.0f px (pad_h %d record px), gap %.0f px, margin %.0f px, "
                     "pad_y %.0f px (%s), group cells %d record px - inside the frame %.0f..%.0f x "
                     "%.0f..%.0f%s, clear of the grid %.0f..%.0f x %.0f..%.0f",
                     o0.ground.x, o0.ground.y, o0.ground.w, o0.ground.h, cell, g.scale, o0.rowPx,
                     kUtPadDefH, o0.gapPx, o0.marginPx, o0.dyPx, utPadFitText(o0.fit), o0.cellW,
                     g_frame.x, g_frame.x + g_frame.w, g_frame.y, g_frame.y + g_frame.h,
                     band == kUtPadBorder ? " (NOT: over its bottom border)" : "", g_frameGrid.x,
                     g_frameGrid.x + g_frameGrid.w, g_frameGrid.y, g_frameGrid.y + g_frameGrid.h);
                if (o0.fit >= kUtPadFitBelowFloor)
                    logW("panel: the band below the grid is %.1f px at UI scale %.3f - the pad's rows "
                         "are %.0f px, under GD's floor of %d record px (%.0f px): %s; the captions "
                         "use a shorter font",
                         g_frame.y + g_frame.h - (g_frameGrid.y + g_frameGrid.h), g.scale, o0.rowPx,
                         kUtPadRowFloor, utPadRound((float)kUtPadRowFloor * g.scale),
                         utPadFitText(o0.fit));
            } else {
                logW("panel: no room for the pad even over the measured frame's bottom border "
                     "(%.0f,%.0f) %.0fx%.0f below the grid (%.0f,%.0f) %.0fx%.0f on the %dx%d canvas "
                     "(rows %d, gap %d, %d below the grid, scale %.3f) - no derived pad and no label "
                     "while the view is ON",
                     g_frame.x, g_frame.y, g_frame.w, g_frame.h, g_frameGrid.x, g_frameGrid.y,
                     g_frameGrid.w, g_frameGrid.h, g.canvasW, g.canvasH, kUtPadDefH, kUtPadDefGap,
                     kUtPadDefY, g.scale);
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
        const char* val[kUtLayParts] = {src.gridKey, src.pad, src.label};
        // the collection's grid: ON the layout in force; OFF the same resolution as ON would make
        // (the grid line, the ON page's search field), kept apart from the real page's layout
        const UtLayoutLive* coll = layIn ? &g_lay : nullptr;
        if (layIn && keys.own) {
            UtLayoutSource sc = src;
            sc.gridKey = keys.coll;
            sc.search = utPadFieldLaidOut(true, g_cfg.search, g_cfg.searchTransfer);
            coll = utLayoutFromSource(sc, &g_layColl) ? &g_layColl : nullptr;
        }
        // the real Transfer page: its grid stays the game's own sack, its pad and label are the
        // collection page's, whole, over the real cells they cover (with no grid line the two
        // resolutions are one: nothing to take)
        g_layRealColl = layIn && !on && coll == &g_layColl;
        if (g_layRealColl) utLayoutRealPage(&g_lay, g_layColl);
        if (layIn) gridApply(g, g_lay, coll, keys.relic);
        if (layIn) {   // a bad input zeroes the layout: nothing of it is said or outlined
            layoutSay(g_lay, val, g.scale);
            layoutSayScreen(g_lay, padScr, labelScr, g.scale);
            g_layOk = true;
        }
        const UtLayout& L = g_lay.L;
        // the pad in force: the player's rectangle (fitted, paged when the buttons do not fit),
        // or today's pad as utPadLayout placed it (also for a line that is its rectangle) with
        // row 1 cut into one cell per button (15 buttons: its 15 cells exactly)
        UtPadOut o = o0;
        bool padOk = band > 0;
        g_padPage = 0;
        g_padNextTarget = 0;
        const UtPadPages& pg = L.pad.pages;
        const int shownButton = padButtonOf(pw, liveShownGroup());
        if (L.pad.ok) {
            g_padPage = utPadPageOf(pg, shownButton);
            g_padNextTarget = utPadNextTarget(pg, g_padPage);
        }
        if (L.user[kUtLayPad] && !L.padDerived && L.pad.ok) {
            padOk = utPadOutFromFit(L.pad, g_padPage, utPadRound(g_frame.x), utPadRound(g_frame.y),
                                    g.scale, search, src.edit, &o);
        } else if (padOk && (src.groups != kUtPadDefCells || pg.pages > 1)) {
            padOk = L.pad.ok != 0;   // no cut of row 1 fits: no pad
            utPadDerivedCut(L.pad, g_padPage, g.scale, &o);
        }
        if (padOk) {   // the whole pad, ON or OFF - "Transfer" and the group list
            for (int i = 0; i < kUtPadMax; ++i) g_btnR[i] = o.btn[i];
            g_btnShown[kUtPadToggle] = true;
            g_btnShown[kUtPadPrev] = true;
            g_btnShown[kUtPadNext] = true;
            g_btnShown[kUtPadOwn] = true;
            g_btnShown[kUtPadSearch] = search;
            g_btnShown[kUtPadClear] = search;
            g_btnShown[kUtPadPage] = o.btn[kUtPadPage].w > 0.0f;
            g_btnShown[kUtPadEdit] = o.btn[kUtPadEdit].w > 0.0f;   // edit_button=1: ON and OFF alike
            g_fieldTextR = o.fieldText;
            for (int i = 0; i < kUtPadCells; ++i)
                g_btnShown[kUtPadGroup0 + i] = i < buttons && o.btn[kUtPadGroup0 + i].w > 0.0f;
            g_ground = o.ground;
            g_groundOk = true;
            g_labelR = o.label;
            g_labelOk = true;
        }
        if (L.user[kUtLayLabel] && L.label.ok) {   // the label out of the pad, in its own rectangle
            const UtRectF d = utScaledRectF(L.rect[kUtLayLabel], g.scale);
            g_userLabelR = UtRectF{utPadRound(g_frame.x) + d.x, utPadRound(g_frame.y) + d.y, d.w, d.h};
            g_labelR = g_userLabelR;
            g_labelOk = g_groundOk;   // the label is the pad's: drawn only with a pad
        }
        if (!on && g_layOk) realPadNote();
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
DWORD g_realPressAt = 0;        // the last press the real Transfer page's pad claimed: the engine
                                // reads it later, so the cell under it is refused for 400 ms even
                                // when the cursor has left the pad by then (panelRealPadOwnsCursor)
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
        const UtRectF r = utRectInset(utSlotDrawnRect(gr, shownCols(), shownRows(), col, row, w, h), 1.0f);
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
        if (markStyle != 2 && ownedRelicSlot(i)) continue;   // a relic slot: its gray copy, never the veil
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
    const float cell = gr.w / (float)shownCols();
    const float t = utSearchMarkThick(cell);
    const TqColor col = searchMarkColour(!frame);
    const int n = protoCount();
    for (int i = 0; i < n; ++i) {
        const char* rec = nullptr;
        int sc = 0, sr = 0, sw = 0, sh = 0;
        if (!protoAt(i, &rec, nullptr, nullptr, nullptr, nullptr, nullptr) ||
            !protoSlotAt(i, &sc, &sr, &sw, &sh) || sw < 1 || sh < 1 || !searchHighlight(i, rec))
            continue;
        const UtRectF r = utSearchMarkRect(utSlotDrawnRect(gr, shownCols(), shownRows(), sc, sr, sw, sh), !frame);
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
    const float t = utSearchMarkThick(gr.w / (float)shownCols());
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
    // the query's tail that fits the text area, measured in the font it is drawn in
    // (ut_textmeasure.h), the caret's room kept
    const int fi = utTmDrawFontW(kUtTmCaption, q, n);
    const void* font = fi >= 0 ? fontAt(fi) : nullptr;
    if (!font) font = g_font;
    if (!font || !g_tq.CanvasRenderTextW) return;
    int start = n;
    if (fi >= 0) {
        const UtTmFont& f = utTmSet()->font[fi];
        const float room = area.w - 2.0f * pad - (float)utTmGlyphPx(f, (char32_t)'_', fs);
        float used = 0.0f;
        while (start > 0) {
            const float g = (float)utTmGlyphPx(f, (char32_t)(unsigned short)q[start - 1], fs);
            if (used + g > room) break;
            used += g;
            --start;
        }
        if (start < n && (unsigned short)q[start] >= 0xDC00 && (unsigned short)q[start] < 0xE000)
            ++start;   // never half a surrogate pair
    } else {
        start = utSearchFieldTailStart(n, (int)((area.w - 2.0f * pad) / ((float)fs * kUtTmNoTableEm)) - 1);
    }
    wchar_t line[kUtSearchFieldMax + 2];
    int m = 0;
    for (int i = start; i < n; ++i) line[m++] = q[i];
    if (focused && ((GetTickCount() / 500u) & 1u) == 0u) line[m++] = L'_';   // 500 ms
    line[m] = 0;
    if (m > 0)
        g_tq.CanvasRenderTextW(c, (int)(area.x + pad), (int)ty, &kCaption, line, font, fs, 0, 0,
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


// =============================================================================================
// THE BUTTONS FILE - uniquetab-buttons.ini, one per game (ut_buttons.h)
// =============================================================================================
// The main game's beside uniquetab.ini, a mod's in mods\<leaf>\ (the folder of its own ini).
// Written POPULATED the first time the tab opens in that game (the game thread hands the worker
// the group table it opened with); polled once a second by the worker (a stat; a read and a parse
// only after a change of the file or of the game), published by pointer swap; resolved against the
// game's groups on the GAME THREAD (panelButtonsTick, when a new parse is published - never per
// frame), one INFO line; the [tab] words go to the label and the rollover lines; [unassigned] is
// rewritten in place by the worker whenever the set differs. The pad keeps its fixed 15 cells.
// A refused file is one WARN naming its line; the words in force stay while they were this
// file's, else the built-in ones - a file whose buttons do not resolve against the game's groups
// as well (the whole file is refused). Other threads read these objects unlocked, so a replaced one
// goes through a two-deep retire list (ut_retire.h) and is freed two replacements of its kind later
// by the thread that replaces it (the worker for the file's parse, the game thread for the rest).

// The group table the tab opened with, and its game ("" = the main game).
struct BtnTable {
    std::string game;
    std::vector<utb::UtbGroupIn> groups;
    long gen = 0;   // +1 per table recorded: the worker reads the file again when it moves
    bool atSwitch = false;   // recorded at the game switch: the late offer reads it; a missing
                             // file is still written at the tab's first open (its own table)
};
const BtnTable* volatile g_btnTable = nullptr;
volatile LONG g_btnTableGen = 0;

// The worker's last read of the game's file.
struct BtnFile {
    long gen = 0;
    std::string path, game;
    bool exists = false;   // false = no file: the built-in buttons and words
    utb::UtbFile file;
};
const BtnFile* volatile g_btnFile = nullptr;
volatile LONG g_btnFileGen = 0;

// What the game thread resolved: the draw reads only these fixed arrays.
struct BtnWords {
    UtPadLabelFormats formats;
    char mat[64];                    // mat.caption: the Transfer toggle's caption
    bool matBuiltin = true;          // "Transfer": the toggle shortens it to fit (Trans., Tab)
    char own[64];
    char next[64];                   // next.caption: NEXT's caption (">>")
    char edit[64];                   // edit.caption: EDIT's caption ("EDIT")
    int nextChars = kUtPadNextChars; // its characters for the pad's fit (utPadFitChars)
    // [tab]'s look (record px; unset = the design's): size.caption (0 = fitted), size.caption_max
    // (the fitted captions' ceiling, -1 = pad_caption_max of uniquetab.ini), size.label (-1 =
    // plate_label_size), the four colours
    int capFixed = 0;
    int capMax = -1;
    int labelSize = -1;
    bool colorSet[utt::kStyleColorCount];   // false = the design's colour
    TqColor color[utt::kStyleColorCount];
    // the pad keyed by the buttons (utb::utbPadMap): file order; the built-in set without a file
    int buttons = 0;
    int first[kUtPadCells];         // per button: its first group (its click selects it)
    unsigned mask[kUtPadCells];     // per button: its groups as bits (the search marks)
    int size[kUtPadCells];          // per button: its caption size cap, -1 = none
    int font[kUtPadCells];          // per button: its own font (a UtTmFontId), -1 = the role's
    char cap[kUtPadCells][48];      // per button: its caption
    char names[kUtPadCells][160];   // per button: its name ("" = its first group's label)
    int groups = 0;
    int buttonOf[kUtPadCells];      // per group: its button, -1 = on no button
};
const BtnWords* volatile g_btnWords = nullptr;

// The resolve's [unassigned] for the worker's rewrite.
struct BtnResolved {
    long fileGen = 0;
    std::string path;
    std::vector<std::string> unassigned;
};
const BtnResolved* volatile g_btnResolved = nullptr;

// ---- the retired objects (ut_retire.h): one list per kind; the sizes are their heap blocks ----
size_t btnStr(const std::string& s) { return s.capacity() > 15 ? s.capacity() + 1 : 0; }
size_t btnBytes(const BtnTable* t) {
    size_t b = sizeof(*t) + btnStr(t->game) + t->groups.capacity() * sizeof(utb::UtbGroupIn);
    for (const utb::UtbGroupIn& g : t->groups) b += btnStr(g.id) + btnStr(g.tag) + btnStr(g.label);
    return b;
}
size_t btnBytes(const BtnFile* f) { return sizeof(*f) + btnStr(f->path) + btnStr(f->game); }
size_t btnBytes(const BtnWords* w) { return sizeof(*w); }
size_t btnBytes(const BtnResolved* r) {
    size_t b = sizeof(*r) + btnStr(r->path) + r->unassigned.capacity() * sizeof(std::string);
    for (const std::string& u : r->unassigned) b += btnStr(u);
    return b;
}
UtRetireList<const BtnTable> g_retBtnTable;         // game thread
UtRetireList<const BtnFile> g_retBtnFile;           // the worker
UtRetireList<const BtnWords> g_retBtnWords;         // game thread
UtRetireList<const BtnResolved> g_retBtnResolved;   // game thread
unsigned g_retBtnN[4] = {0, 0, 0, 0};               // the replacements of each kind
volatile LONG g_btnRetiredBytes = 0;                // held in the four lists

template <class T>
void btnRetire(UtRetireList<const T>* list, int kind, const T* old, const char* what) {
    if (!old) return;
    const size_t b = btnBytes(old);
    const UtRetired<const T> out = list->retire(old, ++g_retBtnN[kind], b);
    InterlockedExchangeAdd(&g_btnRetiredBytes, (LONG)b);
    if (!out.table) return;
    InterlockedExchangeAdd(&g_btnRetiredBytes, -(LONG)out.bytes);
    logD("buttons: the %s retired at replacement #%u freed (%u bytes)", what, out.when, (unsigned)out.bytes);
    delete out.table;
}

std::string buttonsPathFor(const std::string& game) {
    char path[MAX_PATH];
    const std::string leaf = game.empty() ? std::string("uniquetab-buttons.ini")
                                          : "mods\\" + game + "\\uniquetab-buttons.ini";
    utModPathA(nullptr, leaf.c_str(), path, sizeof(path));
    return path;
}

bool btnReadBytes(const std::string& path, std::string* out, std::string* why) {
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        *why = "it cannot be opened";
        return false;
    }
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart > 256 * 1024) {
        CloseHandle(h);
        *why = "it is larger than 256 KB";
        return false;
    }
    out->assign((size_t)sz.QuadPart, '\0');
    DWORD got = 0;
    const bool ok = out->empty() ||
                    (ReadFile(h, &(*out)[0], (DWORD)out->size(), &got, nullptr) && got == out->size());
    CloseHandle(h);
    if (!ok) *why = "it could not be read";
    return ok;
}

// Whole and atomically (a temporary file, then a replacing rename).
bool btnWriteBytes(const std::string& path, const std::string& bytes, std::string* why) {
    const std::string tmp = path + ".tmp";
    HANDLE h = CreateFileA(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        *why = "cannot create " + tmp;
        return false;
    }
    DWORD put = 0;
    const bool ok = bytes.empty() || (WriteFile(h, bytes.data(), (DWORD)bytes.size(), &put, nullptr) &&
                                      put == bytes.size());
    CloseHandle(h);
    if (!ok || !MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileA(tmp.c_str());
        *why = "cannot write " + path;
        return false;
    }
    return true;
}

utt::UtFileStamp btnStat(const std::string& path) {
    utt::UtFileStamp st;
    st.known = true;
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &a) &&
        !(a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        st.exists = true;
        st.mtime = ((unsigned long long)a.ftLastWriteTime.dwHighDateTime << 32) | a.ftLastWriteTime.dwLowDateTime;
        st.size = ((unsigned long long)a.nFileSizeHigh << 32) | a.nFileSizeLow;
    }
    return st;
}

// The game's group table as the buttons file names it (id, tag, label).
std::vector<utb::UtbGroupIn> btnLiveGroups() {
    std::vector<utb::UtbGroupIn> g;
    const int n = liveGroupCount();
    for (int i = 0; i < n; ++i)
        g.push_back({liveGroupId(i), liveGroupTag(i), utb::utbGroupLabel(liveGroupLabel(i))});
    return g;
}

// The switch's group table. The switch's table step has just read the entered game's
// uniq-groups.txt, and the settings (so the buttons file's path) already follow that game: the table
// is the file's own game's, recorded now, so the late built-in offer runs before the caravan opens.
// Null when the two games differ or the game has no table: the first open of the tab records it.
// Game thread.
const BtnTable* btnSwitchTable() {
    if (liveGroupCount() <= 0) return nullptr;
    char game[96] = "", leaf[96] = "";
    configActiveGame(game, sizeof(game));
    gameNow(leaf, sizeof(leaf), nullptr, nullptr);
    if (strcmp(game, leaf) != 0) return nullptr;
    try {
        BtnTable* n = new BtnTable();
        n->game = game;
        n->groups = btnLiveGroups();
        n->gen = InterlockedIncrement(&g_btnTableGen);
        n->atSwitch = true;
        return n;
    } catch (...) {
        return nullptr;
    }
}

// The ids no file is wrong to name although this game may have no records for them: the built-in
// table's and Other. Such an id on a button holds nothing here.
const std::vector<std::string>& btnKnownIds() {
    static const std::vector<std::string> ids = [] {
        std::vector<std::string> v;
        std::size_t n = 0;
        const utg::BuiltinGroup* t = utg::builtinGroups(&n);
        for (std::size_t i = 0; i < n; ++i) v.push_back(t[i].id);
        v.push_back("other");
        return v;
    }();
    return ids;
}

// The ids a setting switches off in the game in force: the relic page's while its group table
// has no relic group (collect_relics=0 builds none). A buttons file section naming it gives no
// button; the rest of the file stays in force, and the late offer never adds it (no such group).
// No table yet (no group at all): nothing is switched off.
std::vector<std::string> btnOffIds(const std::vector<utb::UtbGroupIn>& groups) {
    if (groups.empty()) return std::vector<std::string>();
    for (const utb::UtbGroupIn& g : groups)
        if (g.id == utg::kRelicGroupId) return std::vector<std::string>();
    return std::vector<std::string>(1, utg::kRelicGroupId);
}

// Worker. The default file for `game`, from `groups`; `carry` (the old ini's rollover words, the
// main game only) goes into its [tab].
bool btnWriteDefault(const std::string& path, const std::string& game, const std::vector<utb::UtbGroupIn>& groups,
                     const UtTooltipCarry* carry) {
    utb::UtbDefaults d = utb::utbBuiltinDefaults();
    std::string said;
    if (carry) {
        if (carry->yes[0]) d.tab["tooltip.yes"] = carry->yes;
        if (carry->no[0]) d.tab["tooltip.no"] = carry->no;
        if (carry->classYes >= 0) d.tab["tooltip.class_yes"] = std::to_string(carry->classYes);
        if (carry->classNo >= 0) d.tab["tooltip.class_no"] = std::to_string(carry->classNo);
        said = "; the rollover words of the old uniquetab.ini are in its [tab]";
    }
    if (!game.empty()) {
        char dir[MAX_PATH];
        utModDirA(nullptr, dir, sizeof(dir));
        const std::string mods = std::string(dir) + "\\mods";
        CreateDirectoryA(mods.c_str(), nullptr);   // mods and mods\<leaf>: an existing folder is fine
        CreateDirectoryA((mods + "\\" + game).c_str(), nullptr);
    }
    std::string why;
    const std::string text = utb::utbCrlf(utb::utbDefaultText(utb::utbViewTitle(game), groups, d));
    if (btnWriteBytes(path, text, &why)) {
        logI("buttons: %s written - %u button(s), one per group of %s%s", path.c_str(), (unsigned)groups.size(),
             utb::utbViewTitle(game).c_str(), said.c_str());
        return true;
    }
    logW("buttons: the buttons file could not be written (%s) - the built-in buttons are used", why.c_str());
    return false;
}

// Worker. Reads and parses the file; publishes it, or keeps the one in force when it was this
// file's (a refused edit). `snap`: the group table the game thread recorded for this file's game
// (null when the table in force is another game's or not recorded yet) - the late built-in offer
// reads only that one. True when it wrote that offer into the file (nothing published: the file is
// read again at the next poll).
bool btnLoad(const std::string& path, const std::string& game, bool exists, bool first, const BtnTable* snap) {
    const BtnFile* was = g_btnFile;
    BtnFile* n = new BtnFile();
    n->path = path;
    n->game = game;
    if (exists) {
        std::string bytes, why;
        utb::UtbFile f;
        if (!btnReadBytes(path, &bytes, &why) || !utb::utbParse(bytes, f, &why)) {
            const bool keep = was && was->path == path && was->exists;
            logW("buttons: %s is IGNORED - %s; %s", path.c_str(), why.c_str(),
                 keep ? "the buttons and words in force stay" : "the built-in buttons and words are used");
            if (keep) {
                delete n;
                return false;
            }
        } else {
            // A built-in group that came after the file was written gets its button ONCE (utbOfferNew:
            // after the last button, byte-preserving; the "added once" line of [unassigned] remembers
            // it, so a deleted section stays deleted). The file is read again at the next poll. Until
            // `snap` is this game's, the worker's [unassigned] rewrite leaves such a group out.
            std::string out;
            std::vector<std::string> added, refused;
            if (snap && snap->game == game && !snap->groups.empty() &&
                utb::utbOfferNew(bytes, f, snap->groups, utb::utbLateBuiltinIds(), &out, &added, &refused)) {
                std::string a, r;
                for (const std::string& id : added) a += (a.empty() ? "" : ", ") + id;
                for (const std::string& id : refused) r += (r.empty() ? "" : ", ") + id;
                if (btnWriteBytes(path, out, &why)) {
                    logI("buttons: %s - a new built-in group got its button once: %s%s%s (delete the section to "
                         "keep it off - it then stays under [unassigned])",
                         path.c_str(), a.empty() ? "none" : a.c_str(),
                         r.empty() ? "" : "; no button, its tag is taken: ", r.c_str());
                    delete n;
                    return true;
                }
                logW("buttons: %s - the new group's button could not be added (%s)", path.c_str(), why.c_str());
            }
            n->file = std::move(f);
            n->exists = true;
        }
    } else if (!first) {
        logI("buttons: %s is gone - the built-in buttons and words are used (it is written again the next "
             "time the tab opens)", path.c_str());
    }
    n->gen = InterlockedIncrement(&g_btnFileGen);
    g_btnFile = n;
    btnRetire(&g_retBtnFile, 1, was, "file's parse");
    return false;
}

// A font key of the buttons file as one of the game's nine fonts (ut_textmeasure.h): -1 when
// unset; a path that is not one of them is said (WARN) and -1 (the role's font draws).
int btnFontOf(const std::string& path, const std::string& key) {
    if (path.empty()) return -1;
    const int i = utTmFontOfPath(path.c_str());
    if (i < 0)
        logW("buttons: %s = %s is not a font of Resources\\Fonts.arc (README \"Alphabets\") - the role's font "
             "draws instead", key.c_str(), path.c_str());
    return i;
}

// One INFO line per font of the game: its family and how many of the letters of the captions and
// the label words in force it has (each letter once); then, per role, GD's line when no font of
// the game has all of the role's letters. Once per buttons file resolved, never per frame.
void btnFontsSay(const BtnWords* w) {
    const UtTmFontSet* set = utTmSet();
    if (!set || !w) return;
    std::u32string caps, labels, all;
    auto add = [](std::u32string* to, const char* s) {
        if (s && *s)
            utTmForEachCodePoint(s, [&](char32_t cp) {
                if (cp >= 0x20 && to->find(cp) == std::u32string::npos) to->push_back(cp);
            });
    };
    for (int b = 0; b < w->buttons; ++b) {
        add(&caps, w->cap[b]);
        add(&labels, w->names[b][0] ? w->names[b] : liveGroupLabel(w->first[b]));   // no name: the group's
    }
    add(&caps, w->mat);
    add(&caps, w->own);
    for (int k = 0; k < w->formats.n; ++k) add(&labels, w->formats.f[k]);
    all = caps;
    for (char32_t cp : labels)
        if (all.find(cp) == std::u32string::npos) all.push_back(cp);
    for (int i = 0; i < kUtTmFontCount; ++i) {
        const UtTmFont& f = set->font[i];
        if (!f.valid) continue;
        unsigned have = 0;
        char32_t miss = 0;
        for (char32_t cp : all) {
            if (utTmCovers(f, cp)) ++have;
            else if (!miss) miss = cp;
        }
        logI("fonts: %s - %s (%s, %u code points): %u of the %u letters of the captions and label words in "
             "force%s%s",
             utTmFontPath(i), f.family.c_str(), f.cff ? "OpenType CFF" : "TrueType", (unsigned)f.adv.size(), have,
             (unsigned)all.size(), miss ? "; lacks " : "", miss ? utt::utTextCodePointName(miss).c_str() : "");
    }
    for (int role = 0; role < kUtTmRoles; ++role) {
        const std::u32string& cps = role == kUtTmCaption ? caps : labels;
        const UtTmPick p = utTmPickBy(*set, utTmRoleFont(role), [&](auto fn) {
            for (char32_t cp : cps) fn(cp);
        });
        const char* who = role == kUtTmCaption ? "the captions" : "the label line";
        if (p.font >= 0 && p.font != utTmRoleFont(role) && p.coversAll) {
            char32_t first = 0;   // the first letter the role's own font lacks
            for (char32_t cp : cps)
                if (!first && cp <= 0xFFFF && !utTmCovers(set->font[utTmRoleFont(role)], cp)) first = cp;
            logI("fonts: %s is used for %s; %s is not in %s", utTmFontPath(p.font), who,
                 utt::utTextCodePointName(first).c_str(), utTmFontPath(utTmRoleFont(role)));
        }
        const std::string fb = utTmFallbackText(p, who);
        if (!fb.empty()) logI("fonts: %s", fb.c_str());
    }
}

// GAME THREAD: the published parse against the game's groups; the words the draw and the
// rollover lines use. One INFO line (or the WARN of a refused id).
std::string g_btnWordsPath;   // game thread: the file the words in force came from ("" = built-in)
void btnResolve(const BtnFile* f) {
    const std::vector<utb::UtbGroupIn> groups = btnLiveGroups();
    BtnWords* w = new BtnWords();
    memset(w->names, 0, sizeof(w->names));
    utt::UtTextContext c;
    c.glyph = utb::utbGlyphDrawable;   // a letter above U+FFFF: the next source (the tag, the group's name)
    utb::UtbSet set = utb::utbDefaultSet(groups);   // no file: the default file's buttons
    bool fromFile = false;
    if (f->exists) {
        std::string why;
        if (utb::utbResolve(f->file, groups, btnKnownIds(), set, &why, btnOffIds(groups))) {
            fromFile = true;
            if (!set.offTags.empty()) {
                std::string tags;
                for (const std::string& t : set.offTags) tags += (tags.empty() ? "[" : ", [") + t + "]";
                logI("buttons: %s - %s names \"%s\", the relic and charm page, which is off in this game%s: "
                     "no button for it, the rest of the file is in force",
                     f->path.c_str(), tags.c_str(), utg::kRelicGroupId,
                     g_cfg.collectRelics ? "" : " (collect_relics=0)");
            }
            c.file = &f->file.words;
            std::string list;
            for (const std::string& id : set.unassigned) list += (list.empty() ? "" : ", ") + id;
            logI("buttons: %s read - %u button(s) over %u group(s), %u on no button%s%s; [tab] sets %d "
                 "word(s); the pad, the label and the rollover lines follow",
                 f->path.c_str(), (unsigned)set.buttons.size(), (unsigned)groups.size(),
                 (unsigned)set.unassigned.size(), list.empty() ? "" : ": ", list.c_str(), f->file.words.keys);
            BtnResolved* r = new BtnResolved();
            r->fileGen = f->gen;
            r->path = f->path;
            r->unassigned = set.unassigned;
            const BtnResolved* wasR = g_btnResolved;
            g_btnResolved = r;
            btnRetire(&g_retBtnResolved, 3, wasR, "resolve's [unassigned]");
        } else {
            const bool keep = !g_btnWordsPath.empty() && g_btnWordsPath == f->path;
            logW("buttons: %s is IGNORED - %s; %s", f->path.c_str(), why.c_str(),
                 keep ? "the buttons and words in force stay" : "the built-in buttons and words are used");
            if (keep) {
                delete w;   // never published
                return;
            }
            set = utb::utbDefaultSet(groups);   // the default file's buttons, as with no file
        }
    }
    std::vector<std::string> fm;
    std::string fskip;
    utt::utTextResolveFormats(c, fm, &fskip);
    w->formats.n = 0;
    for (const std::string& t : fm) {
        if (w->formats.n >= kUtPadLabelFormatsMax) break;
        _snprintf_s(w->formats.f[w->formats.n++], sizeof(w->formats.f[0]), _TRUNCATE, "%s", t.c_str());
    }
    const utt::UtTextPick unk = utt::utTextResolve(c, utt::kTextUnknown, "");
    _snprintf_s(w->formats.unknown, sizeof(w->formats.unknown), _TRUNCATE, "%s", unk.text.c_str());
    const std::string mat = utt::utTextResolve(c, utt::kTextCaption, "vanilla").text;
    _snprintf_s(w->mat, sizeof(w->mat), _TRUNCATE, "%s", mat.c_str());
    w->matBuiltin = mat == "Transfer";
    _snprintf_s(w->own, sizeof(w->own), _TRUNCATE, "%s", utt::utTextResolve(c, utt::kTextCaption, "owned").text.c_str());
    _snprintf_s(w->next, sizeof(w->next), _TRUNCATE, "%s", utt::utTextResolve(c, utt::kTextCaption, "next").text.c_str());
    w->nextChars = utPadFitChars(utTmCodePoints(w->next));
    _snprintf_s(w->edit, sizeof(w->edit), _TRUNCATE, "%s", utt::utTextResolve(c, utt::kTextCaption, "edit").text.c_str());
    // [tab]'s sizes and colours (the built-in buttons and words: none set)
    for (int k = 0; k < utt::kStyleColorCount; ++k) w->colorSet[k] = false;
    if (fromFile) {
        const utt::UtTextStyle& st = f->file.words.style;
        w->capFixed = utt::utStyleCaptionFixed(st);
        w->capMax = utt::utStyleSizeOr(st, utt::kStyleSizeCaptionMax, -1);
        w->labelSize = st.size[utt::kStyleSizeLabel];
        for (int k = 0; k < utt::kStyleColorCount; ++k) {
            float rgb[3] = {0.0f, 0.0f, 0.0f};
            w->colorSet[k] = utt::utStyleColorRgb(st, k, rgb);
            w->color[k] = TqColor{rgb[0], rgb[1], rgb[2], 1.0f};
        }
    }
    // the pad: one cell per button in file order (no usable file: the default file's buttons, the
    // captions and names it writes)
    utb::UtbPad pad = utb::utbResolvePad(c, set, groups);
    const utb::UtbPadMap map = utb::utbPadMap(set, pad);
    w->buttons = map.buttons() < kUtPadCells ? map.buttons() : kUtPadCells;
    for (int b = 0; b < w->buttons; ++b) {
        const std::vector<int>& gs = map.groups[(size_t)b];
        w->first[b] = map.first(b);
        w->mask[b] = 0;
        for (int g : gs)
            if (g >= 0 && g < 32) w->mask[b] |= 1u << (unsigned)g;
        w->size[b] = map.sizes[(size_t)b];
        w->font[b] = fromFile ? btnFontOf(set.buttons[(size_t)b].font, "[" + set.buttons[(size_t)b].tag + "] font")
                              : -1;
        _snprintf_s(w->cap[b], sizeof(w->cap[b]), _TRUNCATE, "%s", map.caps[(size_t)b].c_str());
        // the file's name, or none when the resolve passed it over (padName: the group's own name)
        const std::string& nm = set.buttons[(size_t)b].name;
        _snprintf_s(w->names[b], sizeof(w->names[b]), _TRUNCATE, "%s",
                    map.names[(size_t)b] == nm ? nm.c_str() : "");
    }
    w->groups = (int)groups.size() < kUtPadCells ? (int)groups.size() : kUtPadCells;
    for (int g = 0; g < w->groups; ++g) {
        const int b = set.buttonOfGroup[(size_t)g];
        w->buttonOf[g] = b >= 0 && b < w->buttons ? b : -1;
    }
    liveSetButtons(w->buttonOf, w->groups);   // one page per button (its groups' union)
    // the roles' fonts: [tab] font.caption / font.label, else the ini's font
    const int iniFont = utTmFontOfPath(g_cfg.fontName);
    const int capKey = fromFile ? btnFontOf(f->file.words.style.font[utt::kStyleFontCaption], "font.caption") : -1;
    const int labKey = fromFile ? btnFontOf(f->file.words.style.font[utt::kStyleFontLabel], "font.label") : -1;
    utTmSetRoleFont(kUtTmCaption, capKey >= 0 ? capKey : iniFont);
    utTmSetRoleFont(kUtTmLabel, labKey >= 0 ? labKey : iniFont);
    btnFontsSay(w);
    const utt::UtTextPick yes = utt::utTextResolve(c, utt::kTextTooltipYes, "");
    const utt::UtTextPick no = utt::utTextResolve(c, utt::kTextTooltipNo, "");
    const utt::UtTextPick roll = utt::utTextResolve(c, utt::kTextTooltipRoll, "");
    tooltipTextRefresh(yes.text.c_str(), no.text.c_str(), roll.text.c_str(), fromFile ? f->file.tooltipClass[0] : -1,
                       fromFile ? f->file.tooltipClass[1] : -1);
    // what the resolve passed over (a letter above U+FFFF, a value the key does not take): one WARN
    if (!fskip.empty()) pad.skipped.push_back(fskip);
    for (const utt::UtTextPick* p : {&unk, &yes, &no, &roll})
        if (!p->skipped.empty()) pad.skipped.push_back(p->skipped);
    const std::string warn = utb::utbSkippedWarn(pad.skipped);
    if (!warn.empty()) logW("buttons: %s", warn.c_str());
    g_btnWordsPath = fromFile ? f->path : std::string();
    const BtnWords* wasW = g_btnWords;
    g_btnWords = w;
    btnRetire(&g_retBtnWords, 2, wasW, "words");
}

long g_btnResolvedGen = 0;     // game thread: the parse last resolved
long g_btnSeenGameGen = -1;    // game thread: configGameGeneration when the game was last read
char g_btnTickGame[96] = "";   // game thread: the game in force

// The words in force (the built-in ones before the first resolve).
const BtnWords* btnWords() { return g_btnWords; }

// [tab]'s colour `which` when the buttons file sets it, else the design's `dflt` (its alpha kept).
TqColor styleColor(const BtnWords* w, int which, const TqColor& dflt) {
    if (!w || which < 0 || which >= utt::kStyleColorCount || !w->colorSet[which]) return dflt;
    const TqColor& c = w->color[which];
    return TqColor{c.r, c.g, c.b, dflt.a};
}
// The label's size in record px: [tab] size.label, else plate_label_size.
int labelSizeOf(const BtnWords* w) {
    return w && w->labelSize > 0 ? w->labelSize : g_cfg.plateLabelSize;
}
// NEXT's caption in characters for the pad's fit: next.caption's (">>": 2).
int padNextChars(const BtnWords* w) { return w ? w->nextChars : kUtPadNextChars; }

int padButtons(const BtnWords* w) {
    const int n = w ? w->buttons : liveGroupCount();
    return n < kUtPadCells ? n : kUtPadCells;
}
int padFirst(const BtnWords* w, int b) {
    if (b < 0 || b >= padButtons(w)) return -1;
    return w ? w->first[b] : b;
}
int padButtonOf(const BtnWords* w, int g) {
    if (w) return g >= 0 && g < w->groups ? w->buttonOf[g] : -1;
    return g >= 0 && g < padButtons(w) ? g : -1;
}
unsigned padMask(const BtnWords* w, int b) {
    if (b < 0 || b >= padButtons(w)) return 0u;
    return w ? w->mask[b] : (b < 32 ? 1u << (unsigned)b : 0u);
}
const char* padCaption(const BtnWords* w, int b) {
    if (b < 0 || b >= padButtons(w)) return "";
    return w ? w->cap[b] : utPadShortName(liveGroupLabel(b));
}
int padSize(const BtnWords* w, int b) {
    return w && b >= 0 && b < padButtons(w) ? w->size[b] : -1;
}
int padFont(const BtnWords* w, int b) {
    return w && b >= 0 && b < padButtons(w) ? w->font[b] : -1;
}
const char* padName(const BtnWords* w, int b) {
    if (w && b >= 0 && b < padButtons(w) && w->names[b][0]) return w->names[b];
    return liveGroupLabel(padFirst(w, b));
}
const UtPadLabelFormats& btnBuiltinFormats() {
    static UtPadLabelFormats f = [] {
        UtPadLabelFormats x;
        x.n = 0;
        for (int i = 0; i < utt::kTextDefaultFormats && i < kUtPadLabelFormatsMax; ++i)
            _snprintf_s(x.f[x.n++], sizeof(x.f[0]), _TRUNCATE, "%s", utt::utTextDefaultFormat(i));
        _snprintf_s(x.unknown, sizeof(x.unknown), _TRUNCATE, "%s", utt::utTextBuiltinPlain(utt::kTextUnknown));
        return x;
    }();
    return f;
}

// The pad itself: the ground, the buttons, the lamps and the label, from the rects layout() wrote.
// Drawn by the Transfer page draw's POST (panelPageDrawPost, under the tooltip) or, as the
// fallback, by the Present detour (drawInner).
// layout_outline=1: the grid, the pad and the label rectangles in force with their [layout] numbers
// (record px, frame-local), white while derived, green when they are the player's, and a refused
// rectangle in red with the reason (GD's outline). Quads and the caption font only; nothing is
// hit-tested.
const TqColor kOutDerived = {0.850f, 0.850f, 0.850f, 1.00f};
const TqColor kOutUser = {0.247f, 0.831f, 0.416f, 1.00f};
const TqColor kOutRefused = {0.950f, 0.300f, 0.250f, 1.00f};

void outlineCaption(TqCanvas* c, float x, float y, int fs, const TqColor& col, const char* s) {
    fillRect(c, x - 1.0f, y - 1.0f, utPadTextWidth(s, fs) + 2.0f, (float)fs + 2.0f, kPadBack);
    text(c, x, y, col, s, fs);
}

void drawLayoutOutline(TqCanvas* c) {
    if (!g_layOk) return;
    const float s = g_geo.scale;
    const float t = s >= 1.5f ? 2.0f : 1.0f;
    const int fs = (int)utPadRound(10.0f * s) < 6 ? 6 : (int)utPadRound(10.0f * s);
    const float ox = utPadRound(g_frame.x), oy = utPadRound(g_frame.y);
    const UtLayout& L = g_lay.L;
    for (int p = 0; p < kUtLayParts; ++p) {
        const UtRect& r = L.rect[p];
        const bool user = L.user[p] != 0;
        char line[160];
        UtRectF d;
        if (p == kUtLayGrid) {
            d = layGrid();
        } else if (p == kUtLayPad) {
            if (!g_groundOk) continue;
            d = g_ground;
        } else {
            if (!g_labelOk) continue;
            d = g_labelR;
        }
        outlineRect(c, d.x, d.y, d.w, d.h, t, user ? kOutUser : kOutDerived);
        if (p == kUtLayGrid)
            _snprintf_s(line, sizeof(line), _TRUNCATE, "grid %d,%d,%d,%d (%d x %d)", r.x, r.y,
                        r.w, r.h, layCols(), layRows());
        else if (p == kUtLayPad)
            _snprintf_s(line, sizeof(line), _TRUNCATE, "pad_%s=%d,%d,%d,%d (%s) - %d cells of %d x %d%s",
                        g_lay.cover, r.x, r.y, r.w, r.h, user ? "yours" : "derived", L.pad.cols,
                        L.pad.cellW, L.pad.cellH, L.pad.pages.pages > 1 ? ", paged" : "");
        else
            _snprintf_s(line, sizeof(line), _TRUNCATE, "label_%s=%d,%d,%d,%d (%s) - size %d",
                        g_lay.cover, r.x, r.y, r.w, r.h,
                        user ? "yours" : (L.labelInPad ? "in the pad" : "derived"), L.label.size);
        const float ty = p == kUtLayGrid ? d.y + 2.0f : d.y - (float)fs - 3.0f;
        outlineCaption(c, d.x + 2.0f, ty, fs, user ? kOutUser : kOutDerived, line);
        if (g_lay.in.user[p] && L.why[p] != kUtLayoutOk) {
            const UtRect& u = g_lay.user[p];
            const UtRectF f = utScaledRectF(u, s);
            outlineRect(c, ox + f.x, oy + f.y, f.w, f.h, t, kOutRefused);
            _snprintf_s(line, sizeof(line), _TRUNCATE, "%s_%s=%d,%d,%d,%d refused: it %s%s%s",
                        utLayoutPartName(p), g_lay.cover, u.x, u.y, u.w, u.h,
                        utLayoutWhyText(L.why[p]), L.against[p] >= 0 ? " - the " : "",
                        L.against[p] >= 0 ? utLayoutPartName(L.against[p]) : "");
            outlineCaption(c, ox + f.x + 2.0f, oy + f.y + 2.0f, fs, kOutRefused, line);
        }
    }
}

// ---- the layout editor (ut_layoutedit.h) ----------------------------------------------------------
// Opened by the EDIT button, Ctrl+right-click on the pad or the one-shot layout_edit=1, where the pad is shown,
// never with an item on the cursor (GD's rule: a click of the editor must never drop it). While it
// is open the pad is not drawn: the pad (its cells over a plain ground) and the label (a sample
// line) are drawn as rectangles with handles, the grid with the shown group's cells as ghost lines
// on a collection page (outlined only on the game's own Transfer page), with a SAVE / CANCEL / RESET
// toolbar at the top right of the pad that travels with it. On a collection page the page's items
// are hidden while it is open (the view builds an empty page, ut_view.cpp) and back at every close. SAVE writes pad_ and label_<W>x<H> to
// the settings file in force in one configPersistStr call; a part left derived has its line
// removed. The state lives on the game thread (the pad's draw and the window's messages).
UtEdit g_edit = {};
int g_editLatch = 0;                  // the one-shot key's latch (utEditOneShot)
char g_editCover[16] = {0};           // the window size the editor writes, "565x637"
bool g_editRefusedSaid = false;       // one "SAVE is off" line per edit
int g_editSaidVerdict = -1;           // the last refusal said (one line per refusal, never per frame)
bool g_editDown = false;              // the editor took a LEFT DOWN: its LEFT UP is the editor's too
UtEdPressMark g_editMark = {};        // the last press the editor took: the engine's copy is claimed

const TqColor kEdGround = {0.024f, 0.024f, 0.024f, 1.00f};   // the plain ground, #060606
const TqColor kEdCell = {0.227f, 0.247f, 0.278f, 0.80f};     // the pad's cells
const TqColor kEdGhost = {0.560f, 0.590f, 0.630f, 0.45f};    // the grid's cell lines
const TqColor kEdDim = {0.560f, 0.580f, 0.600f, 1.00f};      // a disabled caption

void editRectText(const UtRect& r, char* out, size_t cap) {
    _snprintf_s(out, cap, _TRUNCATE, "%d,%d,%d,%d", r.x, r.y, r.w, r.h);
}

// The frame's rounded top-left on the canvas: the editor's points are frame-local screen px.
float edOx() { return utPadRound(g_frame.x); }
float edOy() { return utPadRound(g_frame.y); }

// The page the pad is shown on: the collection group, or the real Transfer page.
int editGroup() { return viewOn() ? liveShownGroup() : kUtEdTransferPage; }

// The item grid the editor drives (the panel's grid: panelGridInForce / gridApply). The rectangle
// in force: the player's grid_<W>x<H> while the view is ON, else the derived grid (the engine's
// own Transfer sack). It can be set on the collection page only, once the mod sack's drawn cell is
// known: the REAL Transfer page (the view OFF) always keeps its own grid, so there the editor
// outlines it only and SAVE never writes it.
bool editGridInForce(UtRect* r) {
    if (!g_layOk || !r) return false;
    return panelGridInForce(r, nullptr, nullptr, nullptr);
}
bool editGridCanSet() { return g_layOk && viewOn() && panelCellPx(nullptr); }
// SAVE's grid: SAVE's one configPersistStr call has written grid_<W>x<H> (utEditSavePlanOf: the
// rectangle, or the line removed = the derived grid; the key panelGridSave writes). The page
// follows at the settings read-back: layout() -> gridApply re-packs every group on the new cells.
// A cell count the groups could not be packed on is tried again once after a SAVE.
bool editGridSet(const UtRect* r) {
    (void)r;
    g_gridFailCols = 0;
    g_gridFailRows = 0;
    return true;
}
const int kEdCellRecord = 32;   // the engine's item cell, record px (the grid's snap and floor)

// The one 1-px frame of a screen part, outside its ground (ut_screendraw.h).
void drawScreenFrame(TqCanvas* c, const UtRectF& ground) {
    const UtScreenFrame f = utScreenFrame(ground, 1);
    fillRect(c, (float)f.top[0], (float)f.top[1], (float)f.top[2], (float)f.top[3], kLineTop);
    fillRect(c, (float)f.left[0], (float)f.left[1], (float)f.left[2], (float)f.left[3], kLineTop);
    fillRect(c, (float)f.bottom[0], (float)f.bottom[1], (float)f.bottom[2], (float)f.bottom[3], kLineBot);
    fillRect(c, (float)f.right[0], (float)f.right[1], (float)f.right[2], (float)f.right[3], kLineBot);
}

// The polled cursor, frame-local screen px (the hover's own route).
bool editCursor(float* x, float* y) {
    float cx = 0.0f, cy = 0.0f;
    if (!panelCursorNow(&cx, &cy)) return false;
    *x = cx - edOx();
    *y = cy - edOy();
    return true;
}

bool gameHasFocus() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

void editClosed(int why) {
    g_editDown = false;
    logI("layout editor: closed - %s", utEditWhyText(why));
}

// What the editor's parts are judged against: the layout in force, except on the real Transfer
// page with a grid line set - there the collection's resolution (its grid is the one the pad and
// the label must stay clear of; the real cells may be covered: utEditCheck).
const UtLayoutInput& editBase() {
    if (g_layRealColl) return g_layColl.in;
    // after a RESET that took the in-window set, the parts are judged against it (the derived pad
    // in the band below the grid that keeps its band)
    return g_edit.resetWin && g_layWinOk ? g_layWin.in : g_lay.in;
}

void editRecheck() {
    if (g_layOk) utEditCheck(&g_edit, editBase());
}

bool editOpen(const char* how, DWORD now) {
    const int group = editGroup();
    const int v = utEditOpenVerdict(g_edit.state, hookCursorHolds() ? 0 : 1, g_layOk ? 1 : 0);
    int cw = 0, ch = 0;
    UtEditOpenIn o = {};
    bool opened = false;
    if (v == kUtEdOpenYes && utCoverKeyParse(g_lay.cover, &cw, &ch)) {
        const UtLayout& L = g_lay.L;
        for (int p = 0; p < kUtLayParts; ++p) {
            o.inForce[p] = L.rect[p];
            o.def[p] = g_lay.in.def[p];
        }
        if (!L.pad.ok) o.inForce[kUtLayPad] = UtRect{0, 0, 0, 0};
        o.plate = g_lay.in.plate;
        UtRect gridNow;
        if (editGridInForce(&gridNow)) o.inForce[kUtLayGrid] = gridNow;
        o.maxCellW = o.maxCellH = o.cellW = o.cellH = kEdCellRecord;
        int slotW = 0, slotH = 0;
        if (liveMaxSlot(&slotW, &slotH)) {   // the grid's floor: the largest slot of any group
            o.maxCellW = slotW * kUtGridCell;
            o.maxCellH = slotH * kUtGridCell;
        }
        o.rowGap = 0;
        o.gridMovable = editGridCanSet() ? 1 : 0;
        // a relic page: the grid part is the relic area; with no relics_ line it is the
        // collection's grid in force, which RESET brings back (SAVE then removes the line)
        o.relicArea = g_relicPage ? 1 : 0;
        if (o.relicArea) o.def[kUtLayGrid] = g_collRec;
        // RESET's set: every part back in the window, the grid with its band (one consistent set)
        o.defWinOk = g_layWinOk && utEditUsable(g_layWin.in.def[kUtLayPad]) &&
                             utEditUsable(g_layWin.in.def[kUtLayLabel]) ? 1 : 0;
        for (int p = 0; p < kUtLayParts; ++p) o.defWin[p] = o.defWinOk ? g_layWin.in.def[p] : o.def[p];
        if (o.defWinOk && o.relicArea)
            o.defWin[kUtLayGrid] = g_collRecUser ? g_collRec : g_layWin.in.def[kUtLayGrid];
        for (int p = 0; p < kUtLayParts; ++p) o.screenIn[p] = L.screen[p];
        o.screenKnown = screenInputReady() ? 1 : 0;
        o.screenW = g_geo.canvasW;
        o.screenH = g_geo.canvasH;
        o.originX = edOx();
        o.originY = edOy();
        o.labelInPad = L.labelInPad;
        o.groups = g_lay.in.groups;
        o.gap = g_lay.in.gap;
        o.widest = g_lay.in.widest;
        o.nextChars = g_lay.in.nextChars;
        o.edit = g_lay.in.edit;
        o.scale = g_lay.in.scale;
        o.coverW = cw;
        o.coverH = ch;
        o.group = group;
        o.now = (unsigned)now;
        opened = utEditOpen(&g_edit, o);
    }
    if (!opened) {
        const int said = v == kUtEdOpenYes ? kUtEdOpenNoPad : v;
        if (said != g_editSaidVerdict) {   // one line per refusal, the key's frames included
            g_editSaidVerdict = said;
            logI("layout editor: not opened (%s) - %s", how, utEditOpenVerdictText(said));
        }
        return false;
    }
    g_editSaidVerdict = -1;
    strcpy_s(g_editCover, sizeof(g_editCover), g_lay.cover);
    g_editRefusedSaid = false;
    g_editDown = false;
    editRecheck();
    char gr[48], pd[48], lb[48];
    editRectText(g_edit.rect[kUtLayGrid], gr, sizeof(gr));
    editRectText(g_edit.rect[kUtLayPad], pd, sizeof(pd));
    editRectText(g_edit.rect[kUtLayLabel], lb, sizeof(lb));
    logI("layout editor: open (%s) for the %s window at UI scale %.3f on the %s - %s %s%s, pad "
         "%s%s, label %s%s; drag a rectangle or a handle, then SAVE, CANCEL or RESET beside the pad",
         how, g_editCover, o.scale,
         group == kUtEdTransferPage ? "real Transfer page" : g_edit.relicArea ? "relic page" : "collection",
         g_edit.relicArea ? "relic area" : "grid", gr, g_edit.gridMovable ? "" : " (fixed)", pd, g_edit.scr[kUtLayPad] ? " (on the screen)" : "",
         lb, g_edit.labelInPad ? " (in the pad)" : g_edit.scr[kUtLayLabel] ? " (on the screen)" : "");
    return true;
}

// SAVE: one configPersistStr call (utEditSavePlanOf): pad_<cover> and label_<cover> go to the
// settings file in force (a mod's own mods\<leaf>\uniquetab.ini while one is active - ut_config
// routes them). The log names the file.
void editSave(DWORD now) {
    UtEditSavePlan plan;
    utEditSavePlanOf(g_edit, g_editCover, &plan);
    const char* k[kUtEdSaveKeysMax];
    const char* v[kUtEdSaveKeysMax];
    for (int i = 0; i < plan.n; ++i) {
        k[i] = plan.k[i].key;
        v[i] = plan.k[i].set ? plan.k[i].value : nullptr;
    }
    const bool written = configPersistStr(k, v, plan.n);
    utEditSaved(&g_edit, written ? 1 : 0, (unsigned)now);
    if (written && (g_edit.gridMovable || g_edit.gridReset))   // a RESET on a fixed grid: the derived one
        editGridSet(!g_edit.gridMovable || utEditPartDerived(g_edit, kUtLayGrid) ? nullptr
                                                                                  : &g_edit.rect[kUtLayGrid]);
    char game[96];
    configActiveGame(game, sizeof(game));
    char viewFile[160];
    if (!game[0]) _snprintf_s(viewFile, sizeof(viewFile), _TRUNCATE, "uniquetab.ini");
    else _snprintf_s(viewFile, sizeof(viewFile), _TRUNCATE, "mods\\%s\\uniquetab.ini", game);
    if (!written) {
        logW("layout editor: SAVE could not rewrite %s - the editor stays open (CANCEL leaves it)",
             viewFile);
        return;
    }
    // "key=value, key=(removed: ...)"
    char lines[512] = {0};
    char labelKey[40], relicsKey[40];
    _snprintf_s(labelKey, sizeof(labelKey), _TRUNCATE, "label_%s", g_editCover);
    _snprintf_s(relicsKey, sizeof(relicsKey), _TRUNCATE, "relics_%s", g_editCover);
    for (int i = 0; i < plan.n; ++i) {
        const UtEditSaveKey& s = plan.k[i];
        const size_t len = strlen(lines);
        _snprintf_s(lines + len, sizeof(lines) - len, _TRUNCATE, "%s%s=%s", len ? ", " : "", s.key,
                    s.set ? s.value
                    : s.mainOnly ? "(removed: in the window)"
                    : (!strcmp(s.key, labelKey) && g_edit.labelInPad) ? "(removed: the pad's own box)"
                    : !strcmp(s.key, relicsKey) ? "(removed: the collection's grid)"
                                                                       : "(removed: the derived rectangle)");
    }
    logI("layout editor: SAVE - %s in one rewrite of %s%s; the page follows once the file is read "
         "back",
         lines, viewFile, game[0] ? " (the game's own)" : "");
}

// The one-shot key: consumed (written back to 0) when the editor really opens, wherever the pad is
// shown (a collection group or the real Transfer page).
void editKeyCheck(DWORD now) {
    const int key = g_cfg.layoutEdit;
    if (!utEditOneShot(key, &g_editLatch)) return;
    if (g_edit.state != kUtEdClosed) return;
    if (!editOpen("layout_edit=1", now)) return;
    g_editLatch = 1;
    const char* k[1] = {"layout_edit"};
    const char* v[1] = {"0"};
    if (!configPersistStr(k, v, 1))
        logW("layout editor: layout_edit=1 could not be written back to 0 - set it by hand");
}

void editDrawInner(TqCanvas* c) {
    const UtEdit& e = g_edit;
    const float s = e.scale;
    const float ox = edOx(), oy = edOy();
    const float t = s >= 1.5f ? 2.0f : 1.0f;
    const int fs = (int)utPadRound(10.0f * s) < 6 ? 6 : (int)utPadRound(10.0f * s);
    ensureFont();
    // the pad: a plain ground and its cells (the derived pad as utPadLayout draws it)
    const UtRectF pr = utScaledRectF(e.rect[kUtLayPad], s);
    fillRect(c, ox + pr.x, oy + pr.y, pr.w, pr.h, kEdGround);
    static UtPadOut po;   // the game thread's only; off the stack, never allocated
    bool cells = false;
    if (utEditPadDerived(e) && g_lay.defBand > 0) {
        po = g_lay.defOut;
        cells = true;
    } else {
        const UtPadFit f = utPadFitRect(e.rect[kUtLayPad], e.groups, e.gap, s, e.widest, e.nextChars);
        cells = f.ok && utPadOutFromFit(f, 0, ox, oy, s, g_btnShown[kUtPadSearch], e.edit != 0, &po);
    }
    for (int i = 0; cells && i < kUtPadMax; ++i)
        if (po.btn[i].w > 0.0f) fillRect(c, po.btn[i].x, po.btn[i].y, po.btn[i].w, po.btn[i].h, kEdCell);
    // the label: a sample line at the size it would get
    const BtnWords* pw = btnWords();
    const UtLabelFit lf = utLabelFit(e.rect[kUtLayLabel], utPlateLabelPx(labelSizeOf(pw), s), s);
    if (lf.ok) {
        const int gb = e.group >= 0 ? padButtonOf(pw, e.group) : -1;
        const char* sample = gb >= 0 ? padName(pw, gb)
                             : e.group >= 0 ? liveGroupLabel(e.group) : "Transfer";
        const UtRectF lr = utScaledRectF(e.rect[kUtLayLabel], s);
        text(c, ox + lr.x, oy + lr.y + utPadRound((lr.h - (float)lf.size) * 0.5f),
             styleColor(pw, utt::kStyleColorLabel, kCaption), sample, lf.size);
    }
    // the grid: the shown group's slots as ghost lines inside the edited rectangle (they follow the
    // drag; ut_layoutedit.h utEditGhost) - on a collection page, where the grid can be set; the
    // game's own Transfer page keeps its outline only
    int cw = 0, ch = 0;
    liveCaravanWindow(&cw, &ch, nullptr, nullptr);
    const int gridCap = utLayoutBoxCap(cw, ch);
    if (e.gridMovable) {
        int slotW = 1, slotH = 1;
        liveShownSlot(&slotW, &slotH);   // not known: one cell
        const UtEdGhost gh = utEditGhost(e.rect[kUtLayGrid], gridCap, slotW, slotH);
        if (gh.cols > 0 && gh.rows > 0) {
            const float x0 = utEditGhostAt(gh.cells.x, s, ox), x1 = utEditGhostAt(gh.cells.x + gh.cells.w, s, ox);
            const float y0 = utEditGhostAt(gh.cells.y, s, oy), y1 = utEditGhostAt(gh.cells.y + gh.cells.h, s, oy);
            for (int k = 0; k <= gh.cols; ++k)
                fillRect(c, utEditGhostAt(gh.cells.x + k * gh.pitchW, s, ox), y0, t, y1 - y0, kEdGhost);
            for (int k = 0; k <= gh.rows; ++k)
                fillRect(c, x0, utEditGhostAt(gh.cells.y + k * gh.pitchH, s, oy), x1 - x0, t, kEdGhost);
        }
    }
    // the outlines, the handles of the pad and the label; their numbers go to the block above the
    // grid (the band below it holds the pad)
    const int hs = utEditHandlePx(s);
    char note[kUtLayParts][160];
    const TqColor* noteCol[kUtLayParts];
    for (int p = 0; p < kUtLayParts; ++p) {
        const UtRect& r = e.rect[p];
        const bool bad = e.partWhy[p] != kUtLayoutOk;
        const bool grabbable = p != kUtLayGrid || e.gridMovable;
        const bool user = grabbable && !utEditPartDerived(e, p);
        const TqColor& col = bad ? kOutRefused : user ? kOutUser : kOutDerived;
        noteCol[p] = &col;
        UtRectF d = utScaledRectF(r, s);
        d.x += ox;
        d.y += oy;
        if (p == kUtLayGrid && !e.gridMovable) d = layGrid();   // the grid in force, drawn
        const bool dragged = e.state == kUtEdDrag && e.part == p;
        outlineRect(c, d.x, d.y, d.w, d.h, dragged ? 2.0f * t : t, col);
        if (grabbable) {
            const float xs[3] = {d.x, d.x + d.w * 0.5f, d.x + d.w};
            const float ys[3] = {d.y, d.y + d.h * 0.5f, d.y + d.h};
            for (int j = 0; j < 3; ++j)
                for (int i = 0; i < 3; ++i)
                    if (i != 1 || j != 1)
                        fillRect(c, utPadRound(xs[i] - (float)hs * 0.5f), utPadRound(ys[j] - (float)hs * 0.5f),
                                 (float)hs, (float)hs, col);
        }
        char* tx = note[p];
        char key[48] = {0};
        if (utEditOwnScreen(e, p)) utScreenRectFormat(utEditScreenKey(e, p), key, sizeof(key));
        if (p == kUtLayGrid && !e.gridMovable)
            _snprintf_s(tx, sizeof(note[p]), _TRUNCATE, "grid %d,%d,%d,%d (fixed: %d x %d)", r.x, r.y,
                        r.w, r.h, layCols(), layRows());
        else if (key[0])
            _snprintf_s(tx, sizeof(note[p]), _TRUNCATE, "%s_screen=%s%s%s", utLayoutPartName(p), key,
                        bad ? " - it " : "", bad ? utEditWhyText(e, p) : "");
        else if (p == kUtLayGrid && !bad) {
            const UtGrid gc = utLayoutGridCells(r, gridCap);
            _snprintf_s(tx, sizeof(note[p]), _TRUNCATE, "grid_%s %d,%d,%d,%d (%s) - %d x %d cells of %d x %d",
                        g_editCover, r.x, r.y, r.w, r.h, user ? "user" : "derived", gc.cols, gc.rows,
                        kUtGridCell, kUtGridCell);
        }
        else if (bad)
            _snprintf_s(tx, sizeof(note[p]), _TRUNCATE, "%s_%s %d,%d,%d,%d - it %s%s%s",
                        utLayoutPartName(p), g_editCover, r.x, r.y, r.w, r.h,
                        utEditWhyText(e, p), utEditAgainstName(e, p)[0] ? ": the " : "",
                        utEditAgainstName(e, p));
        else
            _snprintf_s(tx, sizeof(note[p]), _TRUNCATE, "%s_%s %d,%d,%d,%d%s", utLayoutPartName(p),
                        g_editCover, r.x, r.y, r.w, r.h,
                        user ? "" : (p == kUtLayLabel && e.labelInPad) ? " (in the pad)" : " (derived)");
    }
    // the toolbar
    static const char* const kEdTags[kUtEdButtons] = {"SAVE", "CANCEL", "RESET"};
    float cx = -1.0f, cy = -1.0f;
    UtEdHit hot = {kUtEdHitNone, -1, 0, -1};
    const bool haveCur = editCursor(&cx, &cy);
    if (haveCur) hot = utEditHitTest(e, cx, cy);
    for (int b = 0; b < kUtEdButtons; ++b) {
        const UtRectF d0 = utScaledRectF(e.tool[b], s);
        const float x0 = ox + d0.x, y0 = oy + d0.y;
        // CANCEL is never off: it answers in every state (utEditLeftDown)
        const bool off = b != kUtEdCancel && (e.state != kUtEdOpen || (b == kUtEdSave && !e.ok));
        const bool over = (hot.kind == kUtEdHitButton && hot.button == b) ||
                          (b == kUtEdCancel && e.state != kUtEdOpen && haveCur &&
                           utEditToolbarAt(e, cx, cy) == kUtEdCancel);
        fillRect(c, x0, y0, d0.w, d0.h, off ? kBtnDown : over ? kBtnHover : kBtnBg);
        if (d0.h > 2.0f * t) {
            fillRect(c, x0, y0, d0.w, t, kLineTop);
            fillRect(c, x0, y0 + d0.h - t, d0.w, t, kLineBot);
        }
        const int cs = utPadCaptionSize(d0.w, d0.h, 6, s);
        if (cs >= 6)
            text(c, x0 + utPadRound((d0.w - utPadTextWidth(kEdTags[b], cs)) * 0.5f),
                 y0 + utPadRound((d0.h - (float)cs) * 0.5f), off ? kEdDim : kCaption, kEdTags[b], cs);
    }
    // the hint line, right above the grid, and the three parts' numbers above it
    char hint[160];
    if (e.state == kUtEdSaving)
        _snprintf_s(hint, sizeof(hint), _TRUNCATE, "saved - waiting for uniquetab.ini to be read back");
    else if (!e.ok)
        _snprintf_s(hint, sizeof(hint), _TRUNCATE,
                    "SAVE is off while a rectangle is red - move or resize it");
    else if (utEditScreenHint(e))
        _snprintf_s(hint, sizeof(hint), _TRUNCATE, "layout editor - %s", utEditScreenHint(e));
    else
        _snprintf_s(hint, sizeof(hint), _TRUNCATE,
                    "layout editor - drag a rectangle or a handle; SAVE writes the %s layout",
                    g_editCover);
    const float bx = g_frameGrid.x + (float)hs;
    float by = g_frameGrid.y - (float)fs - 3.0f;
    outlineCaption(c, bx, by, fs, kCaption, hint);
    for (int p = kUtLayParts - 1; p >= 0; --p) {
        by -= (float)fs + 3.0f;
        outlineCaption(c, bx, by, fs, *noteCol[p], note[p]);
    }
}

// Once a frame from the pad's draw: the one-shot key, then (open) the tick and the editor's own
// draw. True = the editor drew this frame and the pad does not.
bool editTickDraw(TqCanvas* c) {
    const DWORD now = GetTickCount();
    editKeyCheck(now);
    if (g_edit.state == kUtEdClosed) return false;
    UtEdTick t = {};
    t.now = (unsigned)now;
    t.group = editGroup();
    t.relicArea = g_relicPage ? 1 : 0;
    t.scale = g_layOk ? g_lay.in.scale : 0.0f;
    t.focused = gameHasFocus() ? 1 : 0;
    t.layoutOk = g_layOk ? 1 : 0;
    if (g_layOk) utCoverKeyParse(g_lay.cover, &t.coverW, &t.coverH);
    t.physL = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) ? 1 : 0;
    t.physR = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) ? 1 : 0;
    t.haveCursor = editCursor(&t.cx, &t.cy) ? 1 : 0;
    t.matches = g_layOk && utEditMatches(g_edit, g_lay.L.rect, g_lay.L.screen) ? 1 : 0;
    t.screenKnown = screenInputReady() && g_geo.canvasW > 0 ? 1 : 0;
    t.screenW = g_geo.canvasW;
    t.screenH = g_geo.canvasH;
    t.originX = edOx();
    t.originY = edOy();
    const int was = g_edit.state;
    const int part = g_edit.part;
    const UtRect before = g_edit.start;
    int moved = 0;
    const int why = utEditTick(&g_edit, t, &moved);
    if (why != kUtEdWhyNone) {
        editClosed(why);
        return false;
    }
    if (moved) editRecheck();
    if (was == kUtEdDrag && g_edit.state == kUtEdOpen && part >= 0 && part < kUtLayParts) {
        char a[48], b[48];
        editRectText(before, a, sizeof(a));
        editRectText(g_edit.rect[part], b, sizeof(b));
        logD("layout editor: the %s %s -> %s%s", utLayoutPartName(part), a, b,
             g_edit.ok ? "" : " (a rectangle is refused, SAVE is off)");
    }
    editDrawInner(c);
    return true;
}

// The window's mouse message at canvas (cx, cy), before the pad's own hit test. True = the
// editor's: swallowed, and the engine's own read of the press is claimed (panelPadClaimsPress),
// so nothing of it reaches the Transfer page under the pad.
bool editInput(UINT msg, WPARAM wp, float cx, float cy) {
    const DWORD now = GetTickCount();
    float x = 0.0f, y = 0.0f;
    utEditPointOf(cx, cy, edOx(), edOy(), &x, &y);
    if (g_edit.state == kUtEdClosed) {
        g_editDown = false;
        // Ctrl+right-click on the pad's ground (the pad's own hit test, as its clicks)
        const bool ctrl = (wp & MK_CONTROL) != 0 || (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        // on the pad or the label, in the window or on the screen, on a group or the real page
        if ((msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK) && ctrl && g_groundOk &&
            (utRectHas(g_ground, cx, cy) || (g_labelOk && utRectHas(g_labelR, cx, cy)) ||
             onScreenPart(cx, cy))) {
            editOpen("Ctrl+right-click on the pad", now);
            g_padPressAt = now;
            utEditMarkPress(&g_editMark, now, x, y);
            return true;
        }
        return false;
    }
    if (now - (DWORD)g_edit.lastTick > kUtEdGoneMs) return false;   // not drawn: its tick closes it
    const bool owns = utEditOwnsPoint(g_edit, x, y);
    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) {
        const int inputOk = hookCursorHolds() ? 0 : 1;
        const UtEdPress pr = utEditPress(&g_edit, x, y, inputOk,
                                         (GetAsyncKeyState(VK_LBUTTON) & 0x8000) ? 1 : 0,
                                         (GetAsyncKeyState(VK_RBUTTON) & 0x8000) ? 1 : 0,
                                         GetSystemMetrics(SM_SWAPBUTTON) ? 1 : 0);
        const int act = pr.act;
        if (logWants(UT_LOG_DEBUG)) {   // every press of the open editor, with what it took
            char words[128];
            utEditPressText(pr, inputOk, words, sizeof(words));
            logD("layout editor: LEFT DOWN at %.0f,%.0f of the caravan window (screen px, UI scale "
                 "%.3f; the grid %s) - %s",
                 x, y, g_edit.scale, g_edit.gridMovable ? "can be moved" : "is fixed", words);
        }
        switch (act) {
            case kUtEdActSave:
                editSave(now);
                break;
            case kUtEdActSaveRefused:
                if (!g_editRefusedSaid) {
                    g_editRefusedSaid = true;
                    for (int p = 0; p < kUtLayParts; ++p) {
                        char words[160];
                        if (utEditRefusal(g_edit, p, words, sizeof(words)) <= 0) continue;
                        logI("layout editor: SAVE is off - %s", words);
                        break;
                    }
                }
                break;
            case kUtEdActCancel:
                editClosed(g_edit.why);   // CANCEL, or (while saving) "saved, not read back yet"
                break;
            case kUtEdActReset:
                editRecheck();
                // GD's RESET: all three, also where the grid is fixed (the collection's grid line)
                logI("layout editor: RESET - the derived grid, pad and label are back in the window "
                     "(SAVE removes the three %s keys and pad_screen / label_screen)", g_editCover);
                break;
            default:
                break;
        }
        const bool mine = pr.mine != 0;
        g_editDown = mine;
        if (mine) {
            g_padPressAt = now;
            utEditMarkPress(&g_editMark, now, x, y);   // the screen input claims the engine's copy
        }
        return mine;
    }
    if (msg == WM_LBUTTONUP) {
        const int part = g_edit.part;
        const UtRect before = g_edit.start;
        if (utEditLeftUp(&g_edit, x, y)) {
            editRecheck();
            if (part >= 0 && part < kUtLayParts) {
                char a[48], b[48];
                editRectText(before, a, sizeof(a));
                editRectText(g_edit.rect[part], b, sizeof(b));
                logD("layout editor: the %s %s -> %s%s", utLayoutPartName(part), a, b,
                     g_edit.ok ? "" : " (a rectangle is refused, SAVE is off)");
            }
        }
        const bool mine = g_editDown || owns;
        g_editDown = false;
        if (mine) g_padPressAt = now;
        return mine;
    }
    if (owns) g_padPressAt = now;   // the right button on the editor's parts: swallowed
    if (owns && (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK)) utEditMarkPress(&g_editMark, now, x, y);
    return owns;
}

// What the screen input reads, once a frame from the Present detour after the draw: the open
// editor's grounds of its parts on the screen and the caravan window's rect (a point inside it is
// the window's); else the screen pad's frame and its shown buttons, the screen label's frame. In
// client px (the event's space): canvas px over g_canvasPerClient. Nothing = retracted once.
bool g_scrPublished = false;
void screenPublish() {
    static UtPadScreenRects pub;   // the game thread's only; off the stack, never allocated
    utScreenPubReset(&pub);
    const float kx = g_canvasPerClientX > 0.0f ? 1.0f / g_canvasPerClientX : 1.0f;
    const float ky = g_canvasPerClientY > 0.0f ? 1.0f / g_canvasPerClientY : 1.0f;
    if (g_edit.state != kUtEdClosed) {
        UtRectF pad, label;
        utEditScreenGrounds(g_edit, &pad, &label);
        const UtRectF win = utEditWindowClient(g_edit);
        const float ox = edOx(), oy = edOy();
        if (pad.w > 0.0f) pub.pad = UtPadScreenRect{(pad.x + ox) * kx, (pad.y + oy) * ky, pad.w * kx, pad.h * ky};
        if (label.w > 0.0f)
            pub.label = UtPadScreenRect{(label.x + ox) * kx, (label.y + oy) * ky, label.w * kx, label.h * ky};
        if (win.w > 0.0f) pub.window = UtPadScreenRect{(win.x + ox) * kx, (win.y + oy) * ky, win.w * kx, win.h * ky};
        pub.shown = pad.w > 0.0f || label.w > 0.0f || win.w > 0.0f ? 1 : 0;
    } else if (layScreenParts() && g_groundOk) {
        if (g_lay.L.screen[kUtLayPad]) {
            utScreenPubPad(&pub, utScreenFrame(g_ground, 1), kx, ky);
            for (int i = 0; i < kUtPadMax; ++i)
                if (g_btnShown[i]) utScreenPubButton(&pub, g_btnR[i], i, kx, ky);
        }
        if (g_lay.L.screen[kUtLayLabel] && g_labelOk) utScreenPubLabel(&pub, utScreenFrame(g_labelR, 1), kx, ky);
    }
    if (pub.shown) {
        screenInputPublish(&pub);
        g_scrPublished = true;
    } else if (g_scrPublished) {
        screenInputPublish(nullptr);
        g_scrPublished = false;
    }
}

bool drawPad(TqCanvas* c) {
    if (editTickDraw(c)) return true;   // the layout editor is open: it draws instead of the pad
    const bool on = viewOn();
    if (!g_groundOk) return true;   // a refused frame while ON: no pad, no label
    const float s = g_geo.scale;
    const float t = s >= 1.5f ? 2.0f : 1.0f;
    ensureFont();
    fillRect(c, g_ground.x, g_ground.y, g_ground.w, g_ground.h, kPadBack);
    // a part on the screen: its 1-px frame outside the ground, the label on a ground of its own
    if (g_layOk && g_lay.L.screen[kUtLayPad]) drawScreenFrame(c, g_ground);
    if (g_layOk && g_lay.L.screen[kUtLayLabel] && g_labelOk) {
        fillRect(c, g_labelR.x, g_labelR.y, g_labelR.w, g_labelR.h, kPadBack);
        drawScreenFrame(c, g_labelR);
    }
    const BtnWords* pw = btnWords();   // one generation of the buttons for the whole draw
    // One caption size for the whole pad (GD: fitted to the smallest cell): the group cells'
    // widest short name, OWN's three letters at least; a caption longer than a short name counts
    // as one (utPadFitChars) and is fitted on its own button (utPadCellCaption), so it never
    // shrinks the other buttons or row 2. The Transfer toggle shortens its caption to fit beside
    // its lamp (utPadToggleCaption); the pad's size is never cut for it.
    // the first group cell on the pad (its page's, while the groups are paged)
    UtRectF g0 = g_btnR[kUtPadGroup0];
    for (int i = 0; i < kUtPadCells; ++i)
        if (g_btnShown[kUtPadGroup0 + i]) {
            g0 = g_btnR[kUtPadGroup0 + i];
            break;
        }
    // the widest caption by its measured width (OWN's three letters at least); a caption longer
    // than kUtPadCaptionFitChars counts by its first ones
    char widest[64] = "OWN";
    int widestFont = -1;
    float widestW = utPadTextWidth(widest, 14);
    for (int i = 0; i < kUtPadCells; ++i) {
        if (!g_btnShown[kUtPadGroup0 + i]) continue;
        char head[64];
        utTmCutToWidth(padCaption(pw, i), (float)kUtPadCaptionFitChars, 0, head, sizeof(head),
                       [](const char* t) { return (float)utTmCodePoints(t); });
        const int bf = padFont(pw, i);
        const float wv = utPadTextWidth(head, 14, bf);
        if (wv > widestW) {
            widestW = wv;
            widestFont = bf;
            memcpy(widest, head, sizeof(widest));
        }
    }
    // [tab] size.caption: one fixed size for every caption (bounded by the cell's height, each cut
    // to its own button); else fitted to the widest under size.caption_max (else pad_caption_max)
    const int capFixed = pw ? pw->capFixed : 0;
    const int fs = capFixed > 0 ? utPadCaptionFixed(g0.h, s, capFixed)
                                : utPadCaptionSizeOf(g0.w, g0.h, widest, s, widestFont,
                                                     utPadCaptionCeiling(pw ? pw->capMax : -1,
                                                                         g_cfg.padCaptionMax));
    // the label font = size.label, else plate_label_size, x the UI scale (utPlateLabelPx), cut to
    // the row - the LABEL row's own height rule, not the group caption size
    const int labelFs = utPadLabelFont(utPlateLabelPx(labelSizeOf(pw), s), g_labelR.h);
    // [tab]'s colours over the design's: the captions, the label, the shown group's ring, the
    // search mark's plate (a user's mark keeps the design's hover step, #3a6288 -> #456c93)
    const TqColor capColor = styleColor(pw, utt::kStyleColorCaption, kCaption);
    // a search-marked plate's caption: the player's color.caption when set (GD keeps it there),
    // else the mark's own near-white
    const TqColor markCap = styleColor(pw, utt::kStyleColorCaption, kMarkCaption);
    const TqColor labelColor = styleColor(pw, utt::kStyleColorLabel, kCaption);
    const TqColor ringColor = styleColor(pw, utt::kStyleColorRing, kRing);
    const TqColor markColor = styleColor(pw, utt::kStyleColorMark, kMarkBg);
    auto lift = [](float v, float d) { return v + d > 1.0f ? 1.0f : v + d; };
    const TqColor markHover =
        pw && pw->colorSet[utt::kStyleColorMark]
            ? TqColor{lift(markColor.r, kMarkHover.r - kMarkBg.r), lift(markColor.g, kMarkHover.g - kMarkBg.g),
                      lift(markColor.b, kMarkHover.b - kMarkBg.b), kMarkHover.a}
            : kMarkHover;
    const int shownButton = padButtonOf(pw, liveShownGroup());   // -1: a group on no button
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
        const bool isGroup = utPadIsCell(i);
        const int button = isGroup ? i - kUtPadGroup0 : -1;
        const unsigned bits = isGroup ? padMask(pw, button) : 0u;   // the button's groups
        const bool lit = (i == kUtPadToggle && !on) || (i == kUtPadOwn && ownOn) ||
                         (isGroup && on && button == shownButton);
        // a marked button (any of its groups marked) shows the mark's plate unless it is being
        // pressed; then it is exactly the button it is without the search
        const bool marked = isGroup && (marks & bits) != 0 && g_padMsg.down != i;
        const bool litFace = lit && !marked;
        const TqColor& bg = marked ? (g_hover == i ? markHover : markColor)
                            : litFace
                                ? kTglOnBg
                                : (g_padMsg.down == i ? kBtnDown
                                               : (g_hover == i ? kBtnHover
                                                               : (isToggle ? kTglBg : kBtnBg)));
        fillRect(c, b.x, b.y, b.w, b.h, bg);
        if (b.h > 2.0f * t) {
            fillRect(c, b.x, b.y, b.w, t, litFace ? kTglOnTop : kLineTop);
            fillRect(c, b.x, b.y + b.h - t, b.w, t, litFace ? kTglOnBot : kLineBot);
        }
        if (isGroup && on && button == shownButton)
            outlineRect(c, b.x, b.y, b.w, b.h, t, ringColor);   // GD's inset ring: no size change
        const BtnWords* bw = pw;
        char fitted[64];
        const char* cap = i == kUtPadToggle ? (bw && !bw->matBuiltin ? utPadFitCaption(bw->mat, b.w, b.h, s, fs, fitted,
                                                                                       sizeof(fitted))
                                                                   : utPadToggleCaption(b.w, b.h, s, fs))
                          : i == kUtPadOwn  ? (bw ? bw->own : "OWN")
                          : i == kUtPadEdit ? (bw ? bw->edit : "EDIT")
                          : i == kUtPadPrev ? "<"
                          : i == kUtPadNext ? ">"
                          : i == kUtPadPage ? utPadNextCaption(bw ? bw->next : ">>", b.w, fs, fitted,
                                                               sizeof(fitted))   // NEXT: next.caption
                                            : padCaption(pw, button);
        if (fs < 6) continue;   // too small for any caption: the button stays unlabelled
        // a long caption is fitted on its own button (smaller, else cut), never at the pad's size;
        // a button's own size caps its caption (the buttons file's size, 6..36); size.caption
        // draws every caption at its one size, cut to its button (utPadButtonCaption); EDIT's is
        // fitted on its own cell as a group's is (the pad's size is never fitted to it)
        char cut[64];
        const int bf = isGroup ? padFont(pw, button) : -1;   // the button's own font, -1 = the role's
        const int cfs = utPadButtonCaption(cap, b.w, b.h, s, fs, isGroup || i == kUtPadEdit,
                                           isGroup ? padSize(pw, button) : -1,
                                           capFixed, cut, sizeof(cut), &cap, bf,
                                           utPadCaptionCeiling(pw ? pw->capMax : -1, g_cfg.padCaptionMax));
        if (cfs < 6) continue;   // not one character fits: this button stays unlabelled
        float tx;
        if (i == kUtPadToggle) {   // the lamp; OWN shows its state by its lit face alone
            tx = lamp(c, b, lit, s);
        } else {   // centred on the measured width, never left of the button
            tx = b.x + utPadRound((b.w - utPadTextWidth(cap, cfs, bf)) * 0.5f);
            if (tx < b.x + t) tx = b.x + t;
        }
        // dimmed: every group of the button indexed and none marked
        const bool dim = queryOn && isGroup && !litFace && bits != 0 && (indexedGroups & bits) == bits &&
                         (marks & bits) == 0;
        const float drawn = textIn(c, tx, b.y + utPadRound((b.h - (float)cfs) * 0.5f),
                                   litFace ? kTglOnText : (marked ? markCap : (dim ? kCaptionDim : capColor)),
                                   cap, cfs, kUtTmCaption, bf);
        if (!g_capSaid[i]) {   // the measure against the engine's own float, once per world
            g_capSaid[i] = true;
            const int dfi = utTmDrawFont(kUtTmCaption, cap, bf);
            logD("fonts: caption \"%s\" at %d in %s: measured %.0f px, RenderText returned %.2f", cap, cfs,
                 dfi >= 0 ? utTmFontPath(dfi) : g_cfg.fontName, utPadTextWidth(cap, cfs, bf), drawn);
        }
    }
    if (g_btnShown[kUtPadSearch])   // ON, or OFF with search_transfer=1 (layout decides)
        drawField(c, g_btnR[kUtPadSearch], g_btnR[kUtPadClear], g_fieldTextR, fs, t);
    if (!on && g_labelOk && g_cfg.plateLabel && labelFs >= 6) {   // the real page is shown
        // "the real Transfer page", and with a query "found N" / "reading k/n" over its items
        char found[40], brief[24], line[96];
        found[0] = brief[0] = 0;
        if (g_btnShown[kUtPadSearch]) searchRealLabel(found, sizeof(found), brief, sizeof(brief));
        const int lfs = utPadLabelRealLine(found, brief, g_labelR.w, labelFs, line, sizeof(line));
        textIn(c, g_labelR.x, g_labelR.y + utPadRound((g_labelR.h - (float)lfs) * 0.5f), labelColor,
               line, lfs, kUtTmLabel, -1);
        if (g_cfg.layoutOutline) drawLayoutOutline(c);
        return true;
    }
    int relicFirst = 0, relicEnd = 0, relicBlocks = 0, relicPage = 0, relicPages = 0;
    if (on && g_labelOk && g_cfg.plateLabel && labelFs >= 6 && liveIsRelicGroup(liveShownGroup()) &&
        liveRelicWindow(&relicFirst, &relicEnd, &relicBlocks, &relicPage, &relicPages)) {
        // the relic and charm page: its own form (blocks, the fixed page, bonuses k / N, pieces n),
        // and with a query standing the collection pages' "found N" / "indexing k/n"
        char line[640];
        UtPadRelicLabel r;   // no page name: the pressed Relic button names the page
        r.blocks = relicBlocks;
        r.first = relicBlocks > 0 ? relicFirst + 1 : 0;
        r.last = relicBlocks > 0 ? (relicEnd > relicFirst ? relicEnd : relicFirst + 1) : 0;
        r.page = relicPages > 0 ? relicPage + 1 : 0;
        r.pages = relicPages;
        liveRelicTotals(&r.bonuses, &r.bonusSlots, &r.pieces);
        char found[40], brief[24];
        searchLabel(found, sizeof(found), brief, sizeof(brief));
        r.found = found;
        r.brief = brief;
        const int lfs = utPadLabelRelic(r, g_labelR.w, labelFs, line, sizeof(line));
        textIn(c, g_labelR.x, g_labelR.y + utPadRound((g_labelR.h - (float)lfs) * 0.5f), labelColor,
               line, lfs, kUtTmLabel, -1);
    } else if (on && g_labelOk && g_cfg.plateLabel && labelFs >= 6) {   // the ini key is honoured
        char line[640];
        const int g = liveShownGroup();
        UtPadLabelWords w;
        const BtnWords* bw = pw;
        const int gb = padButtonOf(pw, g);   // the page's button (a group on none: its own label)
        w.group = gb >= 0 ? padName(pw, gb) : liveGroupLabel(g);
        w.known = ownedLabel(&w.owned, &w.total);   // owned / total for the page (its button's groups)
        w.allKnown = ownedLabelAll(&w.allOwned, &w.allTotal);   // the whole collection
        w.notShown = ownedNotShown();   // " - +N not on these pages", counted per journal change
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
        w.first = rows > 0 ? r0 : 0;
        w.last = rows > 0 ? r1 : 0;
        w.rows = rows;
        // "indexing k/2678" or "found N", and the short forms "k/2678" / "=N" for a narrow box;
        // both empty while no query stands
        char found[40], brief[24];
        searchLabel(found, sizeof(found), brief, sizeof(brief));
        w.found = found;
        w.brief = brief;
        // the label templates of the buttons file's [tab] (TQ's four built in)
        const int lfs = utPadLabelTemplated(bw ? bw->formats : btnBuiltinFormats(), w, g_labelR.w,
                                            labelFs, line, sizeof(line));
        textIn(c, g_labelR.x, g_labelR.y + utPadRound((g_labelR.h - (float)lfs) * 0.5f), labelColor,
               line, lfs, kUtTmLabel, -1);
    }
    if (g_cfg.layoutOutline) drawLayoutOutline(c);
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
    in.scale = g.engineScale;   // the measured grid is engine px: ui_scale_pct does not move it
    in.canvasW = (float)g.canvasW;
    in.canvasH = (float)g.canvasH;
    int winW = 0, winH = 0, pageX = 0, pageY = 0;   // the game's caravan window and Transfer page
    liveCaravanWindow(&winW, &winH, &pageX, &pageY);
    in.pageX = (float)pageX;
    in.pageY = (float)pageY;
    in.frameW = (float)winW;
    in.frameH = (float)winH;
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
        // at most one line a second: the ones in between are counted into the next line
        static DWORD s_sameAt = 0;
        static long s_sameQuiet = 0;
        const DWORD now = GetTickCount();
        if (s_sameAt && now - s_sameAt < 1000) {
            ++s_sameQuiet;
        } else {
            char more[64] = "";
            if (s_sameQuiet > 0)
                _snprintf_s(more, sizeof(more), _TRUNCATE, " (and %ld time(s) more since the last such line)",
                            s_sameQuiet);
            s_sameAt = now;
            s_sameQuiet = 0;
            logD("panel: caravan window measured again at (%.0f,%.0f) %.0fx%.0f - unchanged%s", f.x, f.y,
                 f.w, f.h, more);
        }
    } else if (r == kUtFrameOk) {
        g_frame = f;
        g_frameGrid = gr;
        g_frameVerdict = 1;
        g_frameLogged = f;
        g_frameLoggedOk = true;
        logI("panel: caravan window measured at (%.0f,%.0f) %.0fx%.0f - %s (%.1f,%.1f) minus "
             "the Transfer page's record place (%d,%d) x %.3f; "
             "the grid (%.0f,%.0f) %.0fx%.0f (page pos (%.1f,%.1f)) lies inside it, the frame "
             "inside the %dx%d canvas; the records said (%.0f,%.0f) %.0fx%.0f (off by %+.0f,%+.0f)",
             f.x, f.y, f.w, f.h, kFrameSrcText[src], originX, originY, pageX,
             pageY, g.scale, gr.x, gr.y,
             gr.w, gr.h, gridX - originX, gridY - originY, g.canvasW, g.canvasH, g.winX, g.winY,
             g.winW, g.winH, f.x - g.winX, f.y - g.winY);
    } else {
        g_frameVerdict = -1;
        g_frameLoggedOk = false;
        logW("panel: the caravan frame derived from the proven hover is REFUSED (%s) - frame "
             "(%.0f,%.0f) %.0fx%.0f = parent origin (%.1f,%.1f) minus the page's record place "
             "(%d,%d) x %.3f, grid (%.0f,%.0f) %.0fx%.0f, canvas %dx%d - no pad and no label "
             "while the view is ON (the view hotkey, a tab change or closing the caravan turns it "
             "OFF); the view itself is unaffected",
             utFrameWhyText(r), f.x, f.y, f.w, f.h, originX, originY, pageX, pageY,
             g.scale, gr.x, gr.y, gr.w, gr.h, g.canvasW, g.canvasH);
    }
}

// The hover route: the fallback, and the cross-check of a frame the page draw gave.
void panelNoteHover(float originX, float originY, float gridX, float gridY, unsigned cellW,
                    unsigned cellH) {
    // a page the pad does not show is not measured (panelDraw forgets its frame every frame)
    if (!plateTransferVisible()) return;
    float sx = 0.0f, sy = 0.0f;   // a moved collection sack: the frame is measured on the derived
    viewGridShift(&sx, &sy);      // grid's place
    gridX -= sx;
    gridY -= sy;
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
    if (grid) *grid = shownGrid();
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

bool panelGridInForce(UtRect* rec, int* user, int* cols, int* rows) {
    if (g_frameVerdict != 1) return false;
    if (rec) *rec = shownUser() ? g_gridRec : g_gridDefRec;
    if (user) *user = shownUser() ? 1 : 0;
    if (cols) *cols = layCols();
    if (rows) *rows = layRows();
    return true;
}

bool panelRelicArea(UtRelicArea* out) {
    if (g_frameVerdict != 1 || !g_relicAreaOk || !out) return false;
    *out = g_relicArea;
    return true;
}

bool panelGridSave(const UtRect* rec) {
    char cover[16], key[40], val[48];
    int cw = 0, ch = 0;
    liveCaravanWindow(&cw, &ch, nullptr, nullptr);
    utCoverKeyFormat(cw, ch, cover, sizeof(cover));
    _snprintf_s(key, sizeof(key), _TRUNCATE, "grid_%s", cover);
    val[0] = 0;
    if (rec && !utRectFormat(*rec, val, sizeof(val))) return false;
    const char* k[1] = {key};
    const char* v[1] = {rec ? val : nullptr};
    const bool ok = configPersistStr(k, v, 1);
    if (ok)
        logI("layout: %s=%s written - the grid follows at the next settings read", key,
             rec ? val : "(removed: the derived grid)");
    else
        logW("layout: %s could not be written", key);
    return ok;
}

bool panelSlotGrid(UtRectF* grid) {
    return panelFrameGrid(grid) && panelCellVerdict(nullptr) >= 0;
}

const char* panelFrameSourceText() {
    return kFrameSrcText[g_frameSrc == kFrameSrcDraw ? kFrameSrcDraw : kFrameSrcHover];
}

namespace {

// Ctrl+wheel / Ctrl+PgUp / Ctrl+PgDn walk the pad's own order - Transfer, the buttons in the
// buttons file's order (each button's first group: a union is one stop; a group on no button is
// never one), Transfer ... (utPadWheelButton, pure). "From" is the view's state INCLUDING a queued
// request, so a fast wheel does not repeat the same step. Transfer = the real page (view OFF).
bool cycleStep(int dir, int* fromOut, int* toOut) {
    const int n = liveGroupCount();
    const int from = viewPendingOn() ? liveWantedGroup() : kUtCycleTransfer;
    const BtnWords* pw = btnWords();   // one generation of the buttons for the step
    const int to = n < 1 || dir == 0 || padButtons(pw) < 1
                       ? kUtCycleNone
                       : utPadWheelButton(from, dir, n, [pw](int g) { return padButtonOf(pw, g); });
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

void panelButtonsPoll() {
    static utt::UtFileStamp stamp;
    static std::string lastPath;
    static long rewrittenFor = 0;
    static long snapGen = 0;   // the table the file was last read with
    try {
        char game[96];
        configActiveGame(game, sizeof(game));
        const std::string key = game;
        const std::string path = buttonsPathFor(key);
        // the old ini's rollover words (ini_version 21): into the main game's [tab], once, and only
        // with the game's groups known (a file written without them would hold no button). The ini
        // keeps its old version and the four keys until then (configTooltipCarryDone), so a start
        // that never gets here carries them at the next one.
        UtTooltipCarry carry;
        if (liveGroupCount() > 0 && configTakeTooltipCarry(&carry)) {
            const std::string mainPath = buttonsPathFor(std::string());
            bool done = true;
            if (!btnStat(mainPath).exists) {
                done = btnWriteDefault(mainPath, std::string(), btnLiveGroups(), &carry);
            } else {
                logW("buttons: %s exists, so the old uniquetab.ini's rollover words are not written into it - "
                     "set tooltip.yes = %s, tooltip.no = %s, tooltip.class_yes = %d, tooltip.class_no = %d under "
                     "its [tab] (empty / -1 = keep)",
                     mainPath.c_str(), carry.yes, carry.no, carry.classYes, carry.classNo);
            }
            if (done) configTooltipCarryDone();
        }
        // the group table of this file's game, or none (the game thread has not recorded it yet)
        const BtnTable* tb = g_btnTable;
        const BtnTable* snap = tb && tb->game == key ? tb : nullptr;
        utt::UtFileStamp st = btnStat(path);
        if (!st.exists && snap && !snap->atSwitch) {
            btnWriteDefault(path, key, snap->groups, nullptr);
            st = btnStat(path);
        }
        const bool moved = path != lastPath;
        const bool snapMoved = snap && snap->gen != snapGen && st.exists;   // the late offer reads it again
        bool offered = false;   // this poll wrote the late offer: the rewrite waits for its read
        if (moved || snapMoved || utt::utTextChanged(stamp, st)) {
            const bool first = !stamp.known || moved;
            lastPath = path;
            stamp = st;
            if (snap) snapGen = snap->gen;
            offered = btnLoad(path, key, st.exists, first, snap);
        }
        // [unassigned]: what no button holds, rewritten in place (only that section) when it differs.
        // A late built-in group not yet offered (btnLoad offers it once the tab has recorded this
        // game's table) is left out (utbRewriteIds), so the rewrite never lists it before the offer.
        // In the poll that wrote the offer there is no rewrite: the resolve in force is of the file
        // before it, and the next poll reads the offered file (GD: the offer returns, the rewrite
        // runs on the next read).
        const BtnResolved* r = g_btnResolved;
        const BtnFile* f = g_btnFile;
        if (!offered && r && f && r->fileGen == f->gen && r->path == path && r->fileGen != rewrittenFor) {
            rewrittenFor = r->fileGen;
            std::string bytes, out, why;
            const bool read = btnReadBytes(path, &bytes, &why);
            const std::vector<std::string> ids =
                read ? utb::utbRewriteIds(bytes, r->unassigned, utb::utbLateBuiltinIds()) : std::vector<std::string>();
            if (read && utb::utbRewriteUnassigned(bytes, ids, &out)) {
                if (btnWriteBytes(path, out, &why)) {
                    std::string list;
                    for (const std::string& id : ids) list += (list.empty() ? "" : ", ") + id;
                    logI("buttons: [unassigned] of %s rewritten - %u group(s) on no button%s%s", path.c_str(),
                         (unsigned)ids.size(), list.empty() ? "" : ": ", list.c_str());
                    stamp = btnStat(path);   // our own write is not a change to read again
                } else {
                    logW("buttons: [unassigned] of %s could not be rewritten (%s)", path.c_str(), why.c_str());
                }
            }
        }
    } catch (...) {
    }
}

void panelButtonsTick() {
    // the game in force, re-read only when it switched (no lock per frame)
    const long gg = configGameGeneration();
    if (gg != g_btnSeenGameGen) {
        g_btnSeenGameGen = gg;
        configActiveGame(g_btnTickGame, sizeof(g_btnTickGame));
    }
    try {
        // the first open of the tab in a game: the group table its default file is written from
        if (viewOn() && liveGroupCount() > 0) {
            const BtnTable* t = g_btnTable;
            if (!t || t->game != g_btnTickGame || t->atSwitch) {
                BtnTable* n = new BtnTable();
                n->game = g_btnTickGame;
                n->groups = btnLiveGroups();
                n->gen = InterlockedIncrement(&g_btnTableGen);
                g_btnTable = n;
                btnRetire(&g_retBtnTable, 0, t, "group table");
            }
        }
        // a new parse: resolved here, once - and only a parse of the game in force: right after a
        // switch the published parse can still be the previous game's file (the worker reads the
        // new game's file at its next poll), which is never read against this game's groups
        const BtnFile* f = g_btnFile;
        if (f && f->gen != g_btnResolvedGen && liveGroupCount() > 0 && f->game == g_btnTickGame) {
            g_btnResolvedGen = f->gen;
            btnResolve(f);
        }
    } catch (...) {
    }
}

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
    // the polled release of the window procedure's clicks: a held button or a kept click whose UP
    // never came (released off the window, or behind Alt+Tab) ends here (ut_padinput.h)
    if (utPadMsgOpen(g_padMsg))
        utPadMsgPoll(&g_padMsg, (GetAsyncKeyState(VK_LBUTTON) & 0x8000) ? 1 : 0,
                     (GetAsyncKeyState(VK_RBUTTON) & 0x8000) ? 1 : 0, GetSystemMetrics(SM_SWAPBUTTON) ? 1 : 0);
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
        logE("panel: the pad's draw FAULTED - the pad is off for this session");
        return;
    }
    screenPublish();   // the screen parts as drawn this frame (their clicks are consumed)
    if (!padByPage && !g_tq.sigRollDrawRva && !g_rollMissSaid &&
        (g_edit.state != kUtEdClosed || layScreenParts())) {
        g_rollMissSaid = true;
        logI("panel: exe.rolloverDraw not confirmed - the pad and the label on the screen and the "
             "layout editor are drawn from the Present detour (over the tooltips and the item on the "
             "cursor)");
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
        utPadMsgReset(&g_padMsg);   // the page has gone: a swallowed DOWN's UP is not waited for
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
        // the layout editor first: its clicks and drags never reach the page under the pad
        if (editInput(msg, wp, x, y)) return true;
        if (g_edit.state != kUtEdClosed) return false;   // no pad button while it is open
        // A screen part lies over the world: with an item on the cursor a click on it does nothing
        // and is swallowed whole, its UP too (the item stays on the cursor; the GetMouseEvent
        // detour consumes the engine's own read of the same click). The decision, the latch and
        // the held button are ut_padinput.h's utPadMsgDecide (tools\test_padinput.cpp).
        const bool down = msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK ||
                          msg == WM_RBUTTONDOWN || msg == WM_RBUTTONDBLCLK;
        const bool leftMsg = msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK || msg == WM_LBUTTONUP;
        const int b = hit(x, y);   // a SHOWN button's rect only (the parts' ground: the facts below)
        const bool scr = onScreenPart(x, y);
        UtPadMsgFacts facts;
        facts.button = b;
        facts.onScreen = scr ? 1 : 0;
        facts.cursorItem = down && scr && hookCursorHolds() ? 1 : 0;
        // the game's own Transfer page: the pad and the label lie over its cells, so a press
        // anywhere inside them is theirs (GD's materials-page rule)
        facts.onRealPad = realPadAt(x, y) ? 1 : 0;
        const UtPadMsgOut o = utPadMsgDecide(
            &g_padMsg, leftMsg ? (down ? kUtMsgLeftDown : kUtMsgLeftUp) : (down ? kUtMsgRightDown : kUtMsgRightUp),
            facts);
        if (o.claim) {
            g_padPressAt = GetTickCount();   // the engine's own read of this press is claimed
            if (facts.onRealPad) g_realPressAt = g_padPressAt;   // and its pick-up under the pad refused
        }
        if (down && o.swallow && b < 0 && facts.onRealPad && !(g_realKeepSaid & 1)) {
            g_realKeepSaid |= 1;
            logD("buttons: a click on the real Transfer page's pad, on no button, was kept from the cell "
                 "under it");
        }
        // a press anywhere but on the field drops its focus (the press itself goes on as ever);
        // a press on the Transfer toggle, or on a group button while the real page is shown, only
        // switches the page and keeps it (one query for both pages)
        const bool switches = b == kUtPadToggle ||
                              (!viewOn() && utPadIsCell(b));
        if (o.judged && b != kUtPadSearch && b != kUtPadClear && !switches)
            searchFieldBlur("a click outside the field");
        if (o.act >= 0) {
            // An ABSOLUTE click: one button, one action. "Transfer" = the real page (view
            // OFF), a group = the view ON on that group. also while an item is held - the
            // item stays on the cursor and the page changes (the held-item refusal is gone).
            {
                if (b == kUtPadToggle) {
                    viewRequest(kUtViewReqOff);
                } else if (b == kUtPadPrev) {
                    if (viewOn()) liveStepPage(-1);
                } else if (b == kUtPadNext) {
                    if (viewOn()) liveStepPage(+1);
                } else if (b == kUtPadOwn) {
                    liveToggleOwnedOnly();
                } else if (b == kUtPadEdit) {
                    // EDIT: the layout editor opens here, on a collection group or the real
                    // Transfer page; refused with an item on the cursor (one log line), as the
                    // editor's other ways in are. The press was the pad's: nothing reaches the page.
                    editOpen("the EDIT button", GetTickCount());
                } else if (b == kUtPadSearch || b == kUtPadClear) {
                    fieldClick(b == kUtPadClear);   // laid out ON, or OFF with search_transfer=1
                } else if (b == kUtPadPage) {
                    // NEXT: the first button of the next page - an ordinary button click
                    liveSelectGroup(padFirst(btnWords(), g_padNextTarget));
                    viewRequest(kUtViewReqOn);
                } else {
                    liveSelectGroup(padFirst(btnWords(), b - kUtPadGroup0));   // the button's page
                    viewRequest(kUtViewReqOn);
                }
            }
        }
        return o.swallow != 0;
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
        if (g_frameVerdict == 1) {   // the grid on screen and the game's whole derived area
            over = over || utRectHas(shownGrid(), x, y) || utRectHas(g_areaFull, x, y);
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
        // enabled=0: the key is the game's again
        if (g_cfg.enabled && g_cfg.viewHotkey && (int)wp == g_cfg.viewHotkey) {
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
    if (!utResNameOk(kUtSlotArtTexture)) {
        char why[96];
        _snprintf_s(why, sizeof(why), _TRUNCATE, "its name has a part of %zu characters (the engine takes 1..63)",
                    utResLongestPart(kUtSlotArtTexture));
        artFail(why);
        return nullptr;
    }
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
    const int cg = liveShownCellGroup();   // a union: the member whose slot it takes
    if (cg < 0) return nullptr;
    const int k = utSlotArtFor(liveGroupLabel(cg));
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
bool g_graySizeSaid = false, g_grayMissSaid = false, g_grayNameSaid = false;
int g_grayLent = 0, g_grayWanted = 0;   // this page-draw call
int g_grayReadyEver = 0, g_grayLoadedEver = 0, g_grayTriedEver = 0;
DWORD g_grayFirstAt = 0;
char g_grayDir[MAX_PATH] = "";   // the folder, '\\' separators (for the existence test)
char g_grayHeap[192];            // LoadTexture's std::string (heap form: <leaf>/gray/ + 22 chars)
char g_grayLeaf[96] = "";        // the game in force ("" = the start-up game)
char g_grayGameDir[MAX_PATH] = "";   // its gray folder (a custom game's), '\\' separators
bool g_grayGameOk = false;       // a custom game's gray icons are readable
bool g_grayModsSource = false, g_grayModsFailed = false;
char g_grayModsHeap[MAX_PATH + 8];
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
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\%s", g_grayLeaf[0] ? g_grayGameDir : g_grayDir, name);
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        if (!g_grayMissSaid) {
            g_grayMissSaid = true;
            logW("panel: a prototype has no gray icon (gray\\%s is missing) - its slot gets the veil; "
                 "delete catalogue.stamp to write the gray copies again", name);
        }
        return nullptr;
    }
    char eng[160];   // the engine's name: a custom game's through the mods\ source
    if (!utGameGrayName(g_grayLeaf, name, eng, sizeof(eng))) return nullptr;
    if (!utResNameOk(eng)) {
        if (!g_grayNameSaid) {
            g_grayNameSaid = true;
            logW("panel: the gray icon's name \"%s\" has a part of %zu characters - the engine takes 1..63 between '/' or '\\', so it is NOT loaded - its slot keeps its own "
                 "icon under the veil", eng, utResLongestPart(eng));
        }
        return nullptr;
    }
    TqStdString s;
    if (!tqStdStringOver(&s, g_grayHeap, sizeof(g_grayHeap), eng, strlen(eng))) return nullptr;
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

// False = the call faulted (the caller turns the gray icons off with one ERROR).
bool grayUnloadSeh(const TqTexture* t) {
    __try {
        TqEngine* e = *g_tq.ppEngine;
        TqGraphicsEngine* gfx = e ? g_tq.EngineGetGraphicsEngine(e) : nullptr;
        if (gfx) g_tq.GfxUnloadTexture(gfx, t);   // ENGINE CALL: one reference given back
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool g_grayUnloadOff = false;   // an UnloadTexture faulted: no texture is given back any more

// One texture given back. False = not given back (no export, or the call is off after a fault).
bool grayUnload(const TqTexture* t) {
    if (!t || !g_tq.GfxUnloadTexture || g_grayUnloadOff) return false;   // without it the engine keeps it
    utGuardEnter();
    const bool ok = grayUnloadSeh(t);
    utGuardLeave();
    if (ok) return true;
    g_grayUnloadOff = true;
    g_grayOff = true;
    logE("panel: GraphicsEngine::UnloadTexture faulted giving back a gray icon - the gray icons are off "
         "for this session (the veil marks the uncollected records)");
    return false;
}

// A relic page slot's gray copy is keyed by its ICON (the generator's writeGrayRelicIcons): the
// red slot's shard bitmap, a green slot's relic bitmap. Null for a unique's place.
const char* grayRelicIcon(int i, const char* rec) {
    int slot = 0;
    if (!protoSlotKindAt(i, &slot) || slot <= 0) return nullptr;
    const UtRelicList* list = liveRelicList();
    char key[512];
    if (!list || !utOwnedNormaliseKey(rec, key, sizeof(key))) return nullptr;
    const int r = liveRelicFind(key);
    if (r < 0 || r >= (int)list->rows.size()) return nullptr;
    return utRelicGrayIcon(list->rows[(size_t)r], slot);
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
    if (g_grayLeaf[0] && !g_grayGameOk) return;   // this game's icons are not readable (said at the switch)
    g_grayGen = gen;
    g_grayCount = n;
    int loaded = 0, reused = 0;
    for (int i = 0; i < n && i < kUtProtoMax; ++i) {
        g_grayNext[i] = GrayTex{0ull, nullptr, 0};
        const char* rec = nullptr;
        if (!protoAt(i, &rec, nullptr, nullptr, nullptr) || !rec || !*rec) continue;
        int slot = 0;   // a relic slot: its icon's copy (none: no gray copy, never the veil)
        const bool relicSlot = protoSlotKindAt(i, &slot) && slot > 0;
        const char* icon = relicSlot ? grayRelicIcon(i, rec) : nullptr;
        if (relicSlot && !icon) continue;
        const unsigned long long h = utGrayHash(icon ? icon : rec);
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

int grayHeld() {
    int n = 0;
    for (int k = 0; k < g_grayN; ++k) n += g_gray[k].tex ? 1 : 0;
    return n;
}

bool grayDrawn(int i) {
    return i >= 0 && i < kUtProtoMax && g_grayMark[i] != 0 && g_grayMark[i] == g_rrLastCall &&
           rrRecent();
}

// Every held texture is given back (UnloadTexture: the engine frees it once nothing else holds
// it), then the set is forgotten. `why` names the teardown for the line.
void grayForget(const char* why) {
    int held = 0, given = 0;
    for (int k = 0; k < g_grayN; ++k) {
        if (!g_gray[k].tex) continue;
        ++held;
        if (grayUnload(g_gray[k].tex)) ++given;
        g_gray[k].tex = nullptr;
    }
    if (held)
        logI("panel: gray icons - %d of %d held texture(s) given back (GraphicsEngine::UnloadTexture) - %s",
             given, held, why);
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

// WORKER, once per process, before the hooks: the nine fonts of Resources\Fonts.arc through the
// mod's arc reader; each font's code points and advances are kept (ut_textmeasure.h) and its file
// bytes freed at once (override_asia.fnt alone is 23.6 MB). The set is installed for the measure;
// one INFO line, or one WARN and the estimate (0.55 em per letter).
void panelFontsRead() {
    const DWORD t0 = GetTickCount();
    char game[MAX_PATH] = {0};
    if (!utGameDirA(game, sizeof(game))) {
        logW("fonts: the game folder is not known - the text is measured at 0.55 em per letter");
        return;
    }
    const std::string path = std::string(game) + "\\Resources\\Fonts.arc";
    UtTmFontSet* set = nullptr;   // never freed once installed: the measure reads it for the process
    std::string bad;
    try {
        gen::ArcArchive arc;
        std::string err;
        if (!arc.load(path, &err)) {
            logW("fonts: %s could not be read (%s) - the text is measured at 0.55 em per letter", path.c_str(),
                 err.c_str());
            return;
        }
        set = new UtTmFontSet();
        // override_asia.fnt first, while the address space is least cut up; each file inflated into
        // one block of its full size (a block grown part by part holds the old and the new copy at once)
        for (int k = 0; k < kUtTmFontCount; ++k) {
            const int i = k == 0 ? (int)kUtTmAsia : (k <= (int)kUtTmAsia ? k - 1 : k);
            const char* p = utTmFontPath(i);
            const gen::ArcEntry* e = arc.entry(std::string(p + 6));
            std::vector<std::uint8_t> bytes;
            if (e) bytes.reserve(e->dsize);
            const bool ok = e && arc.read(*e, bytes) && utTmFontRead(bytes.data(), bytes.size(), &set->font[i]);
            std::vector<std::uint8_t>().swap(bytes);   // the file's bytes go now
            if (ok) ++set->read;
            else bad += std::string(bad.empty() ? "" : ", ") + p;
        }
    } catch (...) {   // out of memory in the 32-bit process: the game runs on with the estimate
        delete set;
        logW("fonts: %s could not be read (out of memory) - the text is measured at 0.55 em per letter",
             path.c_str());
        return;
    }
    utTmInstall(set);
    if (bad.empty())
        logI("fonts: %d of %d fonts read from %s in %lu ms (their code points and advances; the files freed)",
             set->read, (int)kUtTmFontCount, path.c_str(), (unsigned long)(GetTickCount() - t0));
    else
        logW("fonts: %d of %d fonts read from %s - not read: %s (a string they would draw is measured in "
             "another)", set->read, (int)kUtTmFontCount, path.c_str(), bad.c_str());
}

void panelForgetFonts() {
    g_font = nullptr;
    g_fontTried = false;
    for (int i = 0; i < kUtTmFontCount; ++i) {
        g_fonts[i] = nullptr;
        g_fontsTried[i] = false;
    }
    utTmRefusedClear();   // a font the engine refused in this world is asked for again in the next
    memset(g_capSaid, 0, sizeof(g_capSaid));
    g_geoSaid = false;
    panelForgetSlotArt();   // the slot art is loaded again in the next world, like the font
    grayForget("the world is gone");   // the gray icons too, given back
}

// a click on the pad (a button pressed in the last 400 ms, or the cursor on the pad's ground)
// never drops the held item on the page either: the pad sits in the page's rect below the grid, and
// the engine reads the same button through DirectInput. The ground never meets the grid (viewgate).
bool panelLayoutEditOpen() {
    return utEditHidesItems(g_edit, (unsigned)GetTickCount());   // the page's items hidden meanwhile
}

void panelCaravanClosed() {
    if (g_edit.state == kUtEdClosed) return;
    // the page left the screen with the caravan; a SAVE already written is said as such
    const int why = g_edit.state == kUtEdSaving ? kUtEdWhySaveSlow : kUtEdWhyGone;
    utEditClose(&g_edit, why);
    editClosed(why);
}

bool panelLayoutEditDragging() {
    return g_edit.state == kUtEdDrag && GetTickCount() - (DWORD)g_edit.lastTick <= kUtEdGoneMs;
}

// The screen input's question for a DOWN at a client point (the event's space): is it the
// editor's? Its parts, handles and toolbar wherever they lie, or the press it has just taken.
bool panelLayoutEditClaims(float clientX, float clientY) {
    const float kx = g_canvasPerClientX > 0.0f ? g_canvasPerClientX : 1.0f;
    const float ky = g_canvasPerClientY > 0.0f ? g_canvasPerClientY : 1.0f;
    const DWORD now = GetTickCount();
    return utEditClaimTake(g_edit, now - (DWORD)g_edit.lastTick, &g_editMark, now, clientX * kx - edOx(),
                           clientY * ky - edOy());
}

bool panelLayoutEditPressRecent() {
    return g_editMark.have && GetTickCount() - (DWORD)g_editMark.at <= kUtEdClaimMs;
}

bool panelLayoutEditOwnsCursor() {
    if (g_edit.state == kUtEdClosed) return false;
    float ex = 0.0f, ey = 0.0f;
    const bool known = panelCursorNow(&ex, &ey);
    return utEditOwnsTake(g_edit, plateTransferVisible(), GetTickCount() - (DWORD)g_edit.lastTick, known,
                          ex - edOx(), ey - edOy());
}

bool panelPadClaimsPress() {
    if (!plateTransferVisible()) return false;
    const DWORD now = GetTickCount();
    if (g_padPressAt && now - g_padPressAt < 400) return true;
    if (g_edit.state != kUtEdClosed && now - (DWORD)g_edit.lastTick <= kUtEdGoneMs) {
        // the layout editor's parts are the pad's too (anywhere in the window; the grid while it can be set)
        float ex = 0.0f, ey = 0.0f;
        if (panelCursorNow(&ex, &ey) && utEditOwnsPoint(g_edit, ex - edOx(), ey - edOy())) return true;
    }
    if (!g_groundOk || now - g_drawnAt >= 500) return false;
    float x = 0.0f, y = 0.0f;
    if (!panelCursorNow(&x, &y)) return false;
    return utRectHas(g_ground, x, y) || realPadAt(x, y);   // the real page: the label too
}

bool panelRealPadOwnsCursor() {
    if (viewOn() || !plateTransferVisible()) return false;
    // a press the pad claimed in the last 400 ms: the engine reads it after the message, by then
    // the cursor may be off the pad's edge
    if (g_realPressAt && GetTickCount() - g_realPressAt < 400) return true;
    float x = 0.0f, y = 0.0f;
    if (panelCursorNow(&x, &y) && realPadAt(x, y)) return true;
    return panelLayoutEditOwnsCursor();   // the open editor's parts, placed over the real cells
}

void panelRealPadRefusalSaid() {
    if (g_realKeepSaid & 2) return;
    g_realKeepSaid |= 2;
    logD("buttons: the real Transfer page's pad is over the cell under the cursor - the cell's tooltip "
         "and pick-up were refused (nothing under the cursor)");
}

namespace {
void drawTintRing(void* page, TqCanvas* c, const UtRectF& gr);
void pageDrawVeils(TqCanvas* c, int pass);   // the ON half of panelPageDrawPost
void drawRelicBack(TqCanvas* c, const UtRectF& gr);   // the relic page's ground and block panels
}  // namespace

// the slot plates, drawn by the Transfer page draw's PRE-detour under the items.
// then the tint ring, over the plates and under the items.
void panelPageDrawPre(void* page, void* canvas, const void* origin, int pass) {
    if (!page || page != viewPageWindow()) return;
    viewGeometryCheck(page, pass);       // derived against measured, once per world
    // the relic and charm page shows the caravan background with its lattice erased (grid_cover=1;
    // else the lattice of its area); its block panels go under its plates (drawRelicBack)
    coverTick(page, pass);               // the caravan background with the lattice of the grid in force
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
    const float cw = gr.w / (float)shownCols();   // the drawn cell (the art edge scales by it)
    // the relic page's plates are inset 3 px a side (a 26 px plate in a 32 px cell): the slots
    // stand apart; a dead red cell has no prototype, so no plate (plain ground)
    const bool relicPage = liveIsRelicGroup(liveShownGroup());
    if (relicPage) drawRelicBack(c, gr);   // under the plates and the items
    if (plates) {
        const int n = protoCount();
        const UtSlotArtBox* art = g_cfg.slotPlates == 3 ? slotArtNow() : nullptr;
        int drawnArt = 0;
        for (int i = 0; i < n; ++i) {
            int col = 0, row = 0, w = 0, h = 0;
            if (!protoSlotAt(i, &col, &row, &w, &h) || w < 1 || h < 1) continue;
            const UtRectF sd = utSlotDrawnRect(gr, shownCols(), shownRows(), col, row, w, h);   // the one helper
            const float x = sd.x, y = sd.y, ww = sd.w, hh = sd.h;
            const float s = cw / 32.0f;   // the texture's cell edge: 2 px left / top, 3 px right / bottom
            const float lt = (relicPage ? 3.0f : 2.0f) * s, cut = (relicPage ? 6.0f : 5.0f) * s;
            if (art && drawSlotArt(c, *art, x + lt, y + lt, ww - cut, hh - cut)) {
                ++drawnArt;
                continue;
            }
            UtRectF in;   // the plate stays inside the caravan window
            if (utRectClip(UtRectF{x + lt, y + lt, ww - cut, hh - cut}, g_frame, &in))
                fillRect(c, in.x, in.y, in.w, in.h, kPlateFill);
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
    const float k = g_tintInset * (gr.w / (float)shownCols() / 32.0f);
    const int n = protoCount() < g_tintN ? protoCount() : g_tintN;
    for (int i = 0; i < n; ++i) {
        if (g_tint[i].kind == kUtTintNone || rrCovers(i)) continue;   // slot-wide already
        if (ownedBackgroundSkip(i, routed)) continue;   // bare unless hovered
        const char* rec = nullptr;
        int col = 0, row = 0, w = 0, h = 0, sc = 0, sr = 0, sw = 0, sh = 0;
        unsigned id = 0;
        if (!protoAt(i, &rec, &col, &row, &id, &w, &h) || !protoSlotAt(i, &sc, &sr, &sw, &sh))
            continue;
        const UtRectF slot = utRectInset(utSlotDrawnRect(gr, shownCols(), shownRows(), sc, sr, sw, sh), k);
        const UtRectF item = utRectInset(utSlotDrawnRect(gr, shownCols(), shownRows(), col, row, w, h), k);
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
// The head of the HUD item rollover's Draw: the pad and the label pinned to the screen, and the open
// layout editor, are drawn here - after the caravan window, under the item tooltip and the item on
// the cursor - in a frame whose Transfer page draw already ran, once a frame (utScreenSiteDecide).
// In any other frame the Present detour draws them. The rects and the hover are the last Present's
// layout (panelDraw); the clicks never read this draw.
void panelRolloverDrawPre(void* self, void* canvas, const void* origin, int pass, float alpha) {
    if (self != g_rollSaidFor) {   // its inputs, once per rollover object (one per world)
        g_rollSaidFor = self;
        const float* o = (const float*)origin;
        logI("panel: the HUD item rollover's Draw (TQ.exe+0x%X) self=%p canvas=%p origin=(%.1f,%.1f) "
             "pass=%d alpha=%.2f - the pad and the label on the screen and the layout editor are "
             "drawn at its head, under the tooltips and the item on the cursor",
             g_tq.sigRollDrawRva, self, canvas, o ? o[0] : 0.0f, o ? o[1] : 0.0f, pass, alpha);
    }
    if (InterlockedCompareExchange(&g_padOff, 0, 0) || !g_cfg.groupButtons || !g_groundOk ||
        !plateTransferVisible())
        return;
    TqCanvas* c = canvasNow();
    const unsigned long long frame = hookFrameCount();
    const UtScreenSiteWhy why = utScreenSiteDecide(
        g_edit.state != kUtEdClosed || layScreenParts(), c && (void*)c == canvas,
        g_pageFrame != 0 && g_pageFrame == frame, GetTickCount() - g_layoutAt < 250, g_padByPage);
    if (why != kUtSiteDraw) return;
    if (!padGuarded(c)) {
        InterlockedExchange(&g_padOff, 1);
        hideAll();
        logE("panel: the pad's draw FAULTED (at the head of the HUD item rollover's Draw) - the pad is "
             "off for this session");
        return;
    }
    g_padPageAt = GetTickCount();
    g_drawnAt = g_padPageAt;
    g_padByPage = true;   // Present leaves the pad alone this frame
    if (!g_rollSiteSaid) {
        g_rollSiteSaid = true;
        logI("panel: the pad and the label on the screen (or the layout editor) are drawn at the head "
             "of the HUD item rollover's Draw (TQ.exe+0x%X, pass %d): the item tooltip and the item on "
             "the cursor go over them; the Present detour draws them only in a frame where this did "
             "not", g_tq.sigRollDrawRva, pass);
    }
}

void panelPageDrawPost(void* page, void* canvas, int pass) {
    // ON or OFF (the pad is drawn on the real Transfer page too); the veils ON only
    if (!page || page != viewPageWindow() || InterlockedCompareExchange(&g_padOff, 0, 0) ||
        !plateTransferVisible())
        return;
    g_pageFrame = hookFrameCount();   // the caravan was drawn in this frame (the rollover site reads it)
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
    // a part on the screen, or the open editor (whose parts may leave the window): drawn at the head
    // of the HUD item rollover's Draw (panelRolloverDrawPre), else from the Present detour
    if (g_edit.state != kUtEdClosed || layScreenParts()) return;
    if (!padGuarded(c)) {
        InterlockedExchange(&g_padOff, 1);
        hideAll();
        logE("panel: the pad's draw FAULTED (after the Transfer page draw) - the pad is off for this "
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
// The relic and charm page's counts, at 2 and above: a green slot's items, the red slot's shards.
// Top left of the slot, inside its inset plate: the engine writes a partial piece's level text
// (ItemRelic::GetUIBitmapText, Game.dll 0x1C3B80: only while the level is below the completed one)
// and its stack numbers at the bottom right, and the have-mark takes the top right. Nothing is
// allocated; the states were taken at the page build (ut_live, once per journal change).
void drawRelicCounts(TqCanvas* c) {
    const int n = liveRelicPlaceCount();
    if (n <= 0) return;
    UtRectF gr;
    if (!panelSlotGrid(&gr)) return;
    const float cw = gr.w / (float)shownCols();
    const float s = cw / 32.0f;
    const int edge = utRelicEdgePx(s);   // the dark edge around the digits
    int fs = (int)(cw * 0.36f + 0.5f);
    if (fs < 8) fs = 8;
    for (int i = 0; i < n; ++i) {
        UtRelicPlace pl;
        UtRelicPlaceState st;
        if (!liveRelicPlaceAt(i, &pl, &st) || pl.kind == kUtRelicSlotDead || st.empty || st.count < 2u)
            continue;
        const UtRectF r = utSlotDrawnRect(gr, shownCols(), shownRows(), pl.col, pl.line, 1, 1);
        if (r.x < g_frame.x || r.y < g_frame.y || r.x + r.w > g_frame.x + g_frame.w ||
            r.y + r.h > g_frame.y + g_frame.h)
            continue;   // inside the caravan window only
        char num[16];
        _snprintf_s(num, sizeof(num), _TRUNCATE, "%u", st.count);
        const float x = r.x + utPadRound(4.0f * s), y = r.y + utPadRound(3.0f * s);
        for (int k = 0; k < 9; ++k) {   // the cell's dark flank on all eight sides, then the gold
            if (k == 4) continue;
            text(c, x + (float)((k % 3 - 1) * edge), y + (float)((k / 3 - 1) * edge), kLineBot, num, fs);
        }
        text(c, x, y, kCaption, num, fs);
    }
}

// The relic page's back, drawn in the page draw's PRE under the plates and the items. Its ground is
// the caravan background with the lattice erased (gridApply, grid_cover=1); until that copy is in
// the window's field (the painter's first pass, the swap at a page change) every cell of the area
// that holds no slot (the gap column, the empty lines between blocks, the space right of a short
// row, a dead red cell) is covered in the caravan's ground colour, so no other lattice shows between
// the slots. Then every block's panel (relic_panels). The runs and the panels are taken once per
// prototype set and window, never per frame; nothing is allocated.
const int kRelicGroundMax = 64;   // cells a side the cover takes (a custom game's area of 20 x 20 fits)
unsigned char g_relicOcc[kRelicGroundMax * kRelicGroundMax];
UtRelicPlace g_relicGroundPlaces[kUtProtoMax];
UtRelicRun g_relicRuns[kRelicGroundMax * kRelicGroundMax / 2 + kRelicGroundMax];
int g_relicRunN = 0;
unsigned g_relicRunGen = ~0u;
int g_relicRunCols = -1, g_relicRunRows = -1, g_relicRunPlaces = -1, g_relicRunFirst = -1;
const int kRelicPanelMax = 64;
UtRelicPanel g_relicPanels[kRelicPanelMax];
int g_relicPanelN = 0;

// The block panels: a subtle tint and a thin frame in the caravan's palette - the relic blocks in
// its gold (the lit rim, the lit tab face), the charm blocks in a bronze patina - two tones each,
// alternating between neighbouring blocks (utRelicPanelTone); the frame is lit on its top and left
// edges (the tab's top edge) and dark on its bottom and right ones (the window's cell flank).
const TqColor kRelicPanelFill[4] = {
    {0.847f, 0.776f, 0.565f, 0.11f},    // relic, pale gold: 0xD8C690, the lit rim
    {0.667f, 0.584f, 0.322f, 0.22f},    // relic, deep gold: 0xAA9552, the lit tab face
    {0.553f, 0.722f, 0.647f, 0.11f},    // charm, pale patina: 0x8DB8A5
    {0.290f, 0.502f, 0.459f, 0.24f}};   // charm, deep patina: 0x4A8075
const TqColor kRelicPanelLit[2] = {
    {0.682f, 0.620f, 0.443f, 0.85f},    // relic: 0xAE9E71, the tab's top edge
    {0.502f, 0.667f, 0.600f, 0.85f}};   // charm: 0x80AA99, the patina's light edge
const TqColor kRelicPanelDark = {0.098f, 0.086f, 0.055f, 0.80f};   // 0x19160E, the cell's dark flank

void relicBackTake(int cols, int rows) {
    const int n = liveRelicPlaceCount();
    int first = 0, end = 0, blocks = 0, page = 0, pages = 0;
    if (!liveRelicWindow(&first, &end, &blocks, &page, &pages)) first = 0;
    if (protoGeneration() == g_relicRunGen && cols == g_relicRunCols && rows == g_relicRunRows &&
        n == g_relicRunPlaces && first == g_relicRunFirst)
        return;
    g_relicRunGen = protoGeneration();
    g_relicRunCols = cols;
    g_relicRunRows = rows;
    g_relicRunPlaces = n;
    g_relicRunFirst = first;
    int k = 0;
    for (int i = 0; i < n && k < kUtProtoMax; ++i) {
        UtRelicPlaceState st;
        if (liveRelicPlaceAt(i, &g_relicGroundPlaces[k], &st)) ++k;
    }
    utRelicOccupancy(g_relicGroundPlaces, k, cols, rows, g_relicOcc);
    g_relicRunN = utRelicGroundRuns(g_relicOcc, cols, rows, g_relicRuns,
                                    (int)(sizeof(g_relicRuns) / sizeof(g_relicRuns[0])));
    const UtRelicList* l = liveRelicList();
    g_relicPanelN = l ? utRelicPanelsOf(*l, g_relicGroundPlaces, k, first, g_relicPanels, kRelicPanelMax) : 0;
}

void drawRelicBack(TqCanvas* c, const UtRectF& gr) {
    const int cols = shownCols(), rows = shownRows();
    if (cols < 1 || rows < 1 || cols > kRelicGroundMax || rows > kRelicGroundMax) return;
    relicBackTake(cols, rows);
    if (g_cfg.gridCover && !coverGridShown()) {   // the erased copy is not in the field yet
        for (int i = 0; i < g_relicRunN; ++i) {   // the one helper, on the relic area's cells
            const UtRelicRun& r = g_relicRuns[i];
            const UtRectF cell = utSlotDrawnRect(gr, cols, rows, r.col, r.line, r.w, 1);
            UtRectF in;   // the ground stays inside the caravan window
            if (utRectClip(cell, g_frame, &in)) fillRect(c, in.x, in.y, in.w, in.h, kPadBack);
        }
    }
    if (!g_cfg.relicPanels) return;
    const float t = (float)utRelicEdgePx(gr.w / (float)cols / 32.0f);   // the frame's width
    for (int i = 0; i < g_relicPanelN; ++i) {
        const UtRelicPanel& p = g_relicPanels[i];
        const UtRectF r = utSlotDrawnRect(gr, cols, rows, p.col, p.line, p.cols, p.lines);
        UtRectF in;   // the panel stays inside the caravan window
        if (!utRectClip(r, g_frame, &in) || in.w < 2.0f * t || in.h < 2.0f * t) continue;
        const int tone = utRelicPanelTone(p.part, p.pos);
        const TqColor& lit = kRelicPanelLit[tone >> 1];
        fillRect(c, in.x, in.y, in.w, in.h, kRelicPanelFill[tone]);
        fillRect(c, in.x, in.y, in.w, t, lit);
        fillRect(c, in.x, in.y + t, t, in.h - t, lit);
        fillRect(c, in.x + t, in.y + in.h - t, in.w - t, t, kRelicPanelDark);
        fillRect(c, in.x + in.w - t, in.y + t, t, in.h - 2.0f * t, kRelicPanelDark);
    }
}

// The veils and the grid cover: after the LAST page draw of the frame, over
// the items, under the tooltip. The view is ON (panelPageDrawPost).
void pageDrawVeils(TqCanvas* c, int pass) {
    UtRectF gr;
    if (panelSlotGrid(&gr)) {   // not on a grid the items do not share
        // the cells no slot of the window uses (a relic page's: drawRelicBack, under its plates)
        if (g_cfg.gridCover && !liveIsRelicGroup(liveShownGroup())) {
            UtCellRect lo[3];
            const int nl = liveLeftover(lo, 3);
            for (int i = 0; i < nl; ++i) {   // the one helper, on the grid in force's cells
                const UtRectF r = utSlotDrawnRect(gr, shownCols(), shownRows(), lo[i].col, lo[i].row,
                                                  lo[i].w, lo[i].h);
                UtRectF in;   // the ground stays inside the caravan window
                if (utRectClip(r, g_frame, &in)) fillRect(c, in.x, in.y, in.w, in.h, kPadBack);
            }
        }
    }
    const int markStyle = ownedMarksBegin(g_geo, g_cursorOk, g_cursorX, g_cursorY);
    if (markStyle) drawMarks(c, markStyle);
    drawRelicCounts(c);   // the relic page's counts: over the items, under the tooltip
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

namespace {
bool grayAddSourceGuarded(const TqStdString* s) {
    bool ok = false;
    __try {
        TqEngine* e = *g_tq.ppEngine;
        void* fs = e ? g_tq.EngineGetFileSystem(e) : nullptr;
        ok = fs && g_tq.FsAddSource(fs, 1, s, nullptr, 0, 0, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    return ok;
}

// Once per process: the mod's mods\ folder becomes a directory source of the engine's file
// system, partition 1 like the start-up gray\ folder (Engine::InitializeMod empties partition 0
// only, so neither source is lost at a game switch). A custom game's icons are read by
// "<leaf>/gray/ug<hash>.tex", never by the start-up game's bare name.
bool grayModsSource() {
    if (g_grayModsSource) return true;
    if (g_grayModsFailed) return false;
    if (!g_tq.EngineGetFileSystem || !g_tq.FsAddSource || !g_tq.ppEngine) {
        g_grayModsFailed = true;
        return false;
    }
    char path[MAX_PATH];
    utModPathA(nullptr, "mods", path, sizeof(path));
    char dir[MAX_PATH + 2];
    size_t n = 0;
    for (const char* p = path; *p && n + 2 < sizeof(dir); ++p) dir[n++] = *p == '\\' ? '/' : *p;
    dir[n++] = '/';
    dir[n] = 0;
    TqStdString s;
    if (!tqStdStringOver(&s, g_grayModsHeap, sizeof(g_grayModsHeap), dir, n) ||
        !grayAddSourceGuarded(&s)) {
        g_grayModsFailed = true;
        logW("panel: FileSystem::AddSource refused the mod's mods\\ folder - a custom game's "
             "uncollected records get the veil instead of their gray icons");
        return false;
    }
    g_grayModsSource = true;
    logI("panel: gray icons - the mod's mods\\ folder is a directory source of the engine's file "
         "system (FileSystem::AddSource, partition 1; Engine::InitializeMod empties partition 0 "
         "only); a custom game's icons load by the name <game>/gray/ug<hash>.tex");
    return true;
}
}  // namespace

// The game in force switched (Engine::InitializeMod / Engine::CleanseDatabase; the game thread, no
// world loaded): the gray cache is forgotten (it holds the previous game's textures), the game's
// gray folder is made readable, and the buttons file resolves again over the new group table.
void panelGameChanged() {
    grayForget("the game switched");
    g_real = RealMemo{};   // the real page's pad line is said again in the next game
    g_realKeepSaid = 0;
    g_gridFailCols = 0;   // the next game's groups are tried on its own grid again
    g_gridFailRows = 0;
    g_btnResolvedGen = 0;
    const BtnTable* wasT = g_btnTable;
    g_btnTable = btnSwitchTable();   // this game's own table, else recorded at the first open
    btnRetire(&g_retBtnTable, 0, wasT, "group table");
    char leaf[96] = "";
    gameNow(leaf, sizeof(leaf), nullptr, nullptr);
    _snprintf_s(g_grayLeaf, sizeof(g_grayLeaf), _TRUNCATE, "%s", leaf);
    g_grayGameOk = false;
    g_grayGameDir[0] = 0;
    if (!leaf[0]) return;
    char rel[MAX_PATH];
    if (!utGameRel(leaf, "gray", rel, sizeof(rel))) return;
    utModPathA(nullptr, rel, g_grayGameDir, sizeof(g_grayGameDir));
    char stamp[MAX_PATH + 16];
    _snprintf_s(stamp, sizeof(stamp), _TRUNCATE, "%s\\gray.stamp", g_grayGameDir);
    if (GetFileAttributesA(stamp) == INVALID_FILE_ATTRIBUTES) {
        logW("panel: the %s game's gray icons are not there (mods\\%s\\gray\\gray.stamp is missing) "
             "- its uncollected records get the veil",
             leaf, leaf);
        return;
    }
    if (!grayModsSource()) return;
    g_grayGameOk = true;
    logD("panel: gray icons - the %s game's from mods\\%s\\gray", leaf, leaf);
}

int panelGrayHeld() { return grayHeld(); }

size_t panelRetiredBytes() { return (size_t)(unsigned long)InterlockedCompareExchange(&g_btnRetiredBytes, 0, 0); }

}  // namespace ut
