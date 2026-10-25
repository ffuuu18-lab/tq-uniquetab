// ut_caravangeo.h - the caravan geometry a game's generated set carries (uniq-geometry.txt, written
// by the generator: src\gen\ui_geometry.h, whose header has the records and TQ.exe's code behind
// them), parsed at run time, and the check of it against the Transfer page the engine draws.
//
//   P <x0> <y0> <usableW> <usableH> <plateW> <plateH> <plate>   the grid (caravan-window units) and
//                                                              the cover (the Grim Dawn mod's P line)
//   W <windowW> <windowH> <pageX> <pageY> [<bgX> <bgY>]        the caravan window's size, the
//                                                              Transfer page's place in it and the
//                                                              background's place (its
//                                                              BackgroundImage record's
//                                                              bitmapPositionX/Y); a W line of four
//                                                              fields leaves the place unknown
//
// The grid is usableW / 32 x usableH / 32 cells (the catalogue's cell). The measured side is what
// the Transfer page draw places its inventory with (ut_padlayout.h utDrawOrigin: the page's place
// [page+0x1C/+0x20], the inventory's place [widget+0x10/+0x14] x the UI scale) plus the REAL
// Transfer sack's cell count (InventorySack +0x20 / +0x24, written by SetDims). Pure: no engine, no
// allocation.
#pragma once

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

namespace ut {

const int kUtCaravanCellPx = 32;   // the catalogue's cell, record px

struct UtCaravanGeo {
    bool valid;
    int x0, y0, w, h;         // the grid, caravan-window units
    int plateW, plateH;       // the cover's texture size
    int windowW, windowH;     // the caravan window
    int pageX, pageY;         // the Transfer page in it
    int bgKnown;              // 1 = the W line carries the background's place
    int bgX, bgY;             // the background's place in the window (bitmapPositionX/Y)
    int cols, rows;           // w / 32, h / 32
    char plate[128];          // the cover's bitmapName (cut to fit)
};

// The base game's: 27,126 512 x 480 = 16 x 15 cells, the vanilla cover, the 565 x 637 window, its
// background (backgroundimage.dbr) at 0,0.
inline UtCaravanGeo utCaravanGeoBase() {
    UtCaravanGeo g;
    memset(&g, 0, sizeof(g));
    g.valid = true;
    g.x0 = 27;
    g.y0 = 126;
    g.w = 512;
    g.h = 480;
    g.plateW = 565;
    g.plateH = 521;
    g.windowW = 565;
    g.windowH = 637;
    g.pageX = 0;
    g.pageY = 126;
    g.cols = 16;
    g.rows = 15;
    g.bgKnown = 1;
    strncpy_s(g.plate, sizeof(g.plate), "XPack\\UI\\Caravan\\InventoryTransferCover01.tex", _TRUNCATE);
    return g;
}

// One tab-separated integer field at *p (advanced past it and its tab); false when there is none.
inline bool utCaravanGeoInt(const char*& p, const char* end, int* out) {
    if (p >= end) return false;
    char buf[16];
    size_t n = 0;
    while (p < end && *p != '\t' && *p != '\r' && *p != '\n' && n + 1 < sizeof(buf)) buf[n++] = *p++;
    buf[n] = 0;
    if (n == 0) return false;
    char* stop = nullptr;
    const long v = strtol(buf, &stop, 10);
    if (!stop || *stop || v < -100000 || v > 100000) return false;
    *out = (int)v;
    if (p < end && *p == '\t') ++p;
    return true;
}

// The file's text (n bytes, not necessarily terminated) -> out. False, and out invalid, when the
// P or the W line is missing or malformed, or the grid holds no whole cell. The background's place
// is known only when both of its fields follow the W line's four (an older file has none).
inline bool utCaravanGeoParse(const char* t, size_t n, UtCaravanGeo* out) {
    memset(out, 0, sizeof(*out));
    bool haveP = false, haveW = false;
    const char* end = t + n;
    for (const char* line = t; line < end;) {
        const char* nl = line;
        while (nl < end && *nl != '\n') ++nl;
        const char* le = nl;
        if (le > line && le[-1] == '\r') --le;
        if (le - line >= 2 && line[0] == 'P' && line[1] == '\t') {
            const char* p = line + 2;
            haveP = utCaravanGeoInt(p, le, &out->x0) && utCaravanGeoInt(p, le, &out->y0) &&
                    utCaravanGeoInt(p, le, &out->w) && utCaravanGeoInt(p, le, &out->h) &&
                    utCaravanGeoInt(p, le, &out->plateW) && utCaravanGeoInt(p, le, &out->plateH);
            const size_t pn = (size_t)(le - p) < sizeof(out->plate) - 1 ? (size_t)(le - p)
                                                                         : sizeof(out->plate) - 1;
            if (haveP && p <= le) {
                memcpy(out->plate, p, pn);
                out->plate[pn] = 0;
            }
        } else if (le - line >= 2 && line[0] == 'W' && line[1] == '\t') {
            const char* p = line + 2;
            haveW = utCaravanGeoInt(p, le, &out->windowW) && utCaravanGeoInt(p, le, &out->windowH) &&
                    utCaravanGeoInt(p, le, &out->pageX) && utCaravanGeoInt(p, le, &out->pageY);
            int bx = 0, by = 0;
            out->bgKnown = haveW && utCaravanGeoInt(p, le, &bx) && utCaravanGeoInt(p, le, &by) ? 1 : 0;
            out->bgX = out->bgKnown ? bx : 0;
            out->bgY = out->bgKnown ? by : 0;
        }
        line = nl + 1;
    }
    out->cols = out->w / kUtCaravanCellPx;
    out->rows = out->h / kUtCaravanCellPx;
    out->valid = haveP && haveW && out->cols > 0 && out->rows > 0 && out->windowW > 0 && out->windowH > 0;
    return out->valid;
}

// What the engine shows: the Transfer page draw's numbers and the real Transfer sack.
struct UtCaravanMeasure {
    float pageX, pageY;     // [page+0x1C] / [+0x20], record units
    float invX, invY;       // [widget+0x10] / [+0x14], the UI scale applied
    float uiScale;
    int gridW, gridH;       // the real Transfer sack's cells (InventorySack +0x20 / +0x24)
};

enum { kUtGeoAgree = 1, kUtGeoDiffer = 0, kUtGeoUnusable = -1 };

// The game's own Transfer sack: TQ.exe sets it 16 x 15 cells whatever the records ask (0xC2F50);
// the derived area (the P line) is at least that, at the same origin.
const int kUtCaravanRealCols = 16, kUtCaravanRealRows = 15;

// The measured grid origin (caravan-window units) into *x0 / *y0, and whether it agrees with the
// derived geometry (within one unit for the origin: the widget keeps it scaled) and the real sack
// has TQ.exe's 16 x 15 cells (a larger derived area is the collection's, not the real sack's).
inline int utCaravanGeoAgree(const UtCaravanGeo& d, const UtCaravanMeasure& m, float* x0, float* y0) {
    if (!d.valid || !(m.uiScale > 0.3f && m.uiScale < 4.0f)) return kUtGeoUnusable;
    const float v[4] = {m.pageX, m.pageY, m.invX, m.invY};
    for (int i = 0; i < 4; ++i)
        if (!(v[i] == v[i]) || v[i] < -20000.0f || v[i] > 20000.0f) return kUtGeoUnusable;
    const float mx = m.pageX + m.invX / m.uiScale, my = m.pageY + m.invY / m.uiScale;
    if (x0) *x0 = mx;
    if (y0) *y0 = my;
    const bool at = fabsf(mx - (float)d.x0) <= 1.0f && fabsf(my - (float)d.y0) <= 1.0f;
    const int wantW = d.cols < kUtCaravanRealCols ? d.cols : kUtCaravanRealCols;
    const int wantH = d.rows < kUtCaravanRealRows ? d.rows : kUtCaravanRealRows;
    return at && m.gridW == wantW && m.gridH == wantH ? kUtGeoAgree : kUtGeoDiffer;
}

// ---- the caravan background's place (ut_cover coverTick) ---------------------------------------
// The caravan window's background (the texture its lattice is drawn in) lies at the W line's place
// (bgX, bgY, window units), so a grid at x,y in the window is at x - bgX, y - bgY in the
// background's pixels. kUtBgPlaced: the place is known and the derived grid lies wholly inside a
// texW x texH background at it (*ox, *oy = the place). kUtBgUnknown: the set carries no place (a W
// line of four fields: an older build's, or a window whose BackgroundImage record was not read).
// kUtBgOutside: the derived grid leaves the
// background at that place, or the background has no size. The fields are bounded by the parser
// (utCaravanGeoInt: +-100000), so the arithmetic stays far from the int range.
enum { kUtBgPlaced = 1, kUtBgUnknown = 0, kUtBgOutside = -1 };
inline int utCaravanBgPlace(const UtCaravanGeo& g, int texW, int texH, int* ox, int* oy) {
    *ox = 0;
    *oy = 0;
    if (!g.valid || !g.bgKnown) return kUtBgUnknown;
    const int x = g.x0 - g.bgX, y = g.y0 - g.bgY;
    if (texW <= 0 || texH <= 0 || x < 0 || y < 0 || x + g.w > texW || y + g.h > texH) return kUtBgOutside;
    *ox = g.bgX;
    *oy = g.bgY;
    return kUtBgPlaced;
}

// The two grids the painter is handed, in the background's pixels: the derived grid (where the
// background's own lattice lies) and the grid in force (x, y in window units, cols x rows), each
// less the place ox, oy that utCaravanBgPlace gave.
struct UtCaravanBgGrid {
    int x, y, cols, rows;
};
inline void utCaravanBgGrids(const UtCaravanGeo& g, int ox, int oy, int x, int y, int cols, int rows,
                             UtCaravanBgGrid* vanilla, UtCaravanBgGrid* inForce) {
    vanilla->x = g.x0 - ox;
    vanilla->y = g.y0 - oy;
    vanilla->cols = g.cols;
    vanilla->rows = g.rows;
    inForce->x = x - ox;
    inForce->y = y - oy;
    inForce->cols = cols;
    inForce->rows = rows;
}

// ---- the collection's own grid on the derived area (ut_panel gridApply) -------------------------
// The band the derived pad takes below the grid: the base caravan window's (637 - (126 + 480)).
const int kUtCaravanPadBand = 31;

// The cells the collection's sack takes when no grid line of the player's is in force: the derived
// area's columns, and its rows less those that would take `padBand` record px inside the caravan
// window (the Grim Dawn mod's derived grid leaves its pad band the same way), never fewer than the
// real sack's 16 x 15. `padBand` = kUtCaravanPadBand while the derived pad or label is laid out in
// the window, 0 when both are on the screen (utCaravanPadBandFor). The base game:
// 16 x 15. A 20 x 20 area at y 57 of a 699-high window: 20 x 19 with the band, 20 x 20 without.
// False (and 16 x 15) when the geometry is not valid.
// The band utCaravanCollectionCells keeps, from the keys in force for this window: 0 while the
// screen input works and the pad is pinned to the screen (pad_screen) with its label on the screen
// too - label_screen, or no label_<W>x<H> line (the label is then the screen pad's own box, on the
// screen with it); else kUtCaravanPadBand. The derived grid is laid out as if no label were in the
// window whenever none is.
inline int utCaravanPadBandFor(bool padScreen, bool labelScreen, bool labelLine, bool screenReady) {
    return screenReady && padScreen && (labelScreen || !labelLine) ? 0 : kUtCaravanPadBand;
}

inline bool utCaravanCollectionCells(const UtCaravanGeo& g, int padBand, int* cols, int* rows) {
    *cols = kUtCaravanRealCols;
    *rows = kUtCaravanRealRows;
    if (!g.valid) return false;
    int c = g.cols, r = g.rows;
    const int room = (g.windowH - (padBand > 0 ? padBand : 0) - g.y0) / kUtCaravanCellPx;
    if (g.windowH > 0 && r > room) r = room;
    *cols = c > kUtCaravanRealCols ? c : kUtCaravanRealCols;
    *rows = r > kUtCaravanRealRows ? r : kUtCaravanRealRows;
    return true;
}

// ---- the caravan window in force (ut_live liveCaravanWindow) -----------------------------------
// The caravan window's size and the Transfer page's place in it, record px: the game's (its W line,
// `g`, when `derived`), except an [advanced] caravan_w / caravan_h / page_y (`set`) the player moved
// off its built-in value (`builtin`), which wins. `out` = W, H, page x, page y. Not derived: `set`
// (the page at x 0, TransferWindow's place in every game read). True = the W line was used.
inline bool utCaravanWindowOf(const UtCaravanGeo& g, bool derived, const int set[3],
                              const int builtin[3], int out[4]) {
    const bool d = derived && g.valid && g.windowW > 0 && g.windowH > 0;
    out[0] = d && set[0] == builtin[0] ? g.windowW : set[0];
    out[1] = d && set[1] == builtin[1] ? g.windowH : set[1];
    out[2] = d ? g.pageX : 0;
    out[3] = d && set[2] == builtin[2] ? g.pageY : set[2];
    return d;
}

// ---- the grid in force on the Transfer page's inventory widget (ut_view viewAccessorGrid) ------
// The widget's place (+0x10 / +0x14, record x UI scale, written by its loader TQ.exe 0x10E850) and
// size (+0x18 / +0x1C, rewritten as 16 x 15 cells by every page refresh 0xC2F50) for the grid in
// force. `place` = what the widget holds after the refresh; `applied` = 1 when the place `wrote`
// was written to this widget last, `kept` = the engine's own place kept then. The engine's own
// place is the widget's, unless it still holds the place written last (then the kept one - the
// engine writes it again only when the loader runs again, e.g. a UI scale change). The grid's
// place = own + the shift (dx, dy: drawn px); its size = the live cell x the cells.
inline void utGridWidget(int applied, const float place[2], const float wrote[2], const float kept[2],
                         float dx, float dy, unsigned cw, unsigned ch, int cols, int rows,
                         float own[2], float out[4]) {
    const bool ours = applied && place[0] == wrote[0] && place[1] == wrote[1];
    own[0] = ours ? kept[0] : place[0];
    own[1] = ours ? kept[1] : place[1];
    out[0] = own[0] + dx;
    out[1] = own[1] + dy;
    out[2] = (float)(cw * (unsigned)cols);
    out[3] = (float)(ch * (unsigned)rows);
}

}  // namespace ut
