// ui_geometry.cpp - see ui_geometry.h.
#include "gen/ui_geometry.h"

#include "gen/arc_reader.h"
#include "gen/arz_reader.h"

#include <cstdio>
#include <cstdint>
#include <vector>

namespace gen {

namespace {

// The HUD records TQ.exe loads (0x134E70: HudType != 0 -> hud_x3.dbr, else hud_x3_new.dbr), in the
// order they are tried here, and the caravan window the base game's HUD names (the last resort).
const char* const kHudRecords[] = {"records/xpack3/ui/hud/hud_x3.dbr",
                                   "records/xpack3/ui/hud/hud_x3_new.dbr"};
const char kCaravanWindow[] = "records/xpack/ui/caravan/caravanwindow.dbr";

// A record path as a field names it ("Records\XPack\UI\...") -> the database's key form.
std::string recordKey(const std::string& field) {
    std::string k = field;
    for (char& c : k) {
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    }
    return k;
}

} // namespace

std::string UiGeometry::line() const {
    char buf[96];
    std::snprintf(buf, sizeof buf, "P\t%d\t%d\t%d\t%d\t%d\t%d\t", x0, y0, usableW, usableH,
                  plateW, plateH);
    return std::string(buf) + plate;
}

std::string UiGeometry::windowLine() const {
    char buf[96];
    if (bgKnown)
        std::snprintf(buf, sizeof buf, "W\t%d\t%d\t%d\t%d\t%d\t%d", windowW, windowH, pageX, pageY, bgX, bgY);
    else
        std::snprintf(buf, sizeof buf, "W\t%d\t%d\t%d\t%d", windowW, windowH, pageX, pageY);
    return buf;
}

std::string UiGeometry::fileText() const {
    return line() + "\r\n" + windowLine() + "\r\n";
}

bool deriveGeometry(const GameData& g, UiGeometry& out, std::string* error) {
    if (!g.db || !g.res) { if (error) *error = "game data not loaded"; return false; }
    // The merged view (the game's layer over the base game, ArzDatabase's own order): a field a
    // game does not override inherits the base record's value.
    auto rec = [&](const std::string& name, ArzRecord& r) { return g.db->get(recordKey(name), r); };

    // The caravan window the game builds: the HUD record's hudCaravanWindow (TQ.exe 0x1235D0 reads it
    // at 0x12851B and hands it to the caravan window's loader at 0x128549).
    std::string hudName, winName, otherWin;
    for (const char* h : kHudRecords) {
        ArzRecord hud;
        if (!rec(h, hud)) continue;
        const std::string w = hud.str("hudCaravanWindow");
        if (w.empty()) continue;
        if (winName.empty()) {
            hudName = h;
            winName = w;
        } else if (recordKey(w) != recordKey(winName)) {
            otherWin = std::string(h) + " names " + w;
        }
    }
    if (winName.empty()) {
        hudName = "no HUD record names one";
        winName = kCaravanWindow;
    }
    ArzRecord win;
    if (!rec(winName, win)) {
        if (error) *error = "the caravan window " + winName + " (" + hudName + ") is not in the database";
        return false;
    }
    const std::string pageName = win.str("TransferWindow");
    ArzRecord page;
    if (pageName.empty() || !rec(pageName, page)) {
        if (error) *error = "the caravan window has no readable TransferWindow record";
        return false;
    }
    const std::string invName = page.str("InventoryDBR");
    ArzRecord inv;
    if (invName.empty() || !rec(invName, inv)) {
        if (error) *error = "the Transfer page has no readable InventoryDBR record";
        return false;
    }
    // The cover: a page without a TransferPlate has none (the engine draws no cover there).
    const std::string cover = page.str("TransferPlate");
    ArzRecord crec;
    if (!cover.empty() && !rec(cover, crec)) {
        if (error) *error = "the Transfer page's TransferPlate " + cover + " is not in the database";
        return false;
    }
    const std::string bitmap = cover.empty() ? std::string() : crec.str("bitmapName");
    int pw = 0, ph = 0;
    if (!cover.empty()) {
        std::vector<std::uint8_t> head;
        std::string err;
        if (bitmap.empty() || !g.res->read(bitmap, head, 64, &err) || !texSize(head, pw, ph)) {
            if (error) *error = "cannot read the pixel size of the cover \"" + bitmap + "\"";
            return false;
        }
    }
    // The background: the caravan window's BackgroundImage record, its place in the window.
    const std::string bgName = win.str("BackgroundImage");
    ArzRecord bg;
    const bool bgKnown = !bgName.empty() && rec(bgName, bg);
    const int pageX = page.i32("WindowLocationX"), pageY = page.i32("WindowLocationY");
    const int invX = inv.i32("inventoryX"), invY = inv.i32("inventoryY");
    const int askW = inv.i32("inventoryXSize"), askH = inv.i32("inventoryYSize");
    const int x0 = pageX + invX, y0 = pageY + invY;
    // The area: the inventory record's whole cells, never less than TQ.exe's real sack.
    const int askCols = askW / kTransferCellPx, askRows = askH / kTransferCellPx;
    const int cols = askCols > kTransferCols ? askCols : kTransferCols;
    const int rows = askRows > kTransferRows ? askRows : kTransferRows;
    const int usableW = cols * kTransferCellPx, usableH = rows * kTransferCellPx;

    out.x0 = x0;
    out.y0 = y0;
    out.usableW = usableW;
    out.usableH = usableH;
    out.plateW = pw;
    out.plateH = ph;
    out.bmpX = cover.empty() ? 0 : crec.i32("bitmapPositionX");
    out.bmpY = cover.empty() ? 0 : crec.i32("bitmapPositionY");
    out.plate = bitmap;
    out.cols = cols;
    out.rows = rows;
    out.windowW = win.i32("windowDefaultExtentX");
    out.windowH = win.i32("windowDefaultExtentY");
    out.pageX = pageX;
    out.pageY = pageY;
    out.bgKnown = bgKnown;
    out.bgX = bgKnown ? bg.i32("bitmapPositionX") : 0;
    out.bgY = bgKnown ? bg.i32("bitmapPositionY") : 0;
    {
        char buf[640];
        char place[64] = "its background's place unknown";
        if (bgKnown) std::snprintf(place, sizeof place, "its background at %d,%d", out.bgX, out.bgY);
        std::snprintf(buf, sizeof buf,
                      "grid %d,%d %dx%d = %d x %d cells of %d from the merged database (the "
                      "Transfer page at %d,%d + its inventory at %d,%d; the inventory record asks "
                      "%dx%d, TQ.exe sizes the sack %d x %d cells%s); cover %dx%d at %d,%d; caravan "
                      "window %dx%d%s, %s",
                      x0, y0, usableW, usableH, out.cols, out.rows, kTransferCellPx, pageX, pageY,
                      invX, invY, askW, askH, kTransferCols, kTransferRows,
                      cols != kTransferCols || rows != kTransferRows
                          ? " - the collection takes the larger area, the game's Transfer page keeps its "
                            "sack"
                          : "",
                      pw, ph, out.bmpX, out.bmpY, out.windowW, out.windowH,
                      out.insideWindow() ? "" : " - the grid leaves the caravan window", place);
        out.note = std::string(buf) + " (" + (bitmap.empty() ? "the page has no cover" : bitmap) +
                   "; the caravan window " + winName + " from " + hudName +
                   (otherWin.empty() ? "" : "; " + otherWin) + "; the background " +
                   (bgName.empty() ? std::string("record: none") : bgName) +
                   (bgName.empty() || bgKnown ? "" : " is not in the database") + ")";
    }
    return true;
}

} // namespace gen
