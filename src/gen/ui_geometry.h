// ui_geometry.h - the caravan's Transfer grid, derived from the database the engine merges instead
// of held as constants, so a game that moves the Transfer page's grid is followed without a
// per-game profile. Port of the Grim Dawn mod's ui_geometry (its UiGeometry, deriveGeometry and P
// line); tools\build_catalogue.py derive_geometry is the Python twin (byte-identical outputs).
//
// WHICH records are read, and why, from TQ.exe's own code (AE 2.10):
//   the HUD record         hudCaravanWindow: the caravan window the game builds. TQ.exe 0x134E70
//                          names the HUD record: Options::GetInt(0xC9) - the option HudType
//                          (Engine.dll's option table, 0x1E8C9A) - not 0 -> records\xpack3\ui\hud\
//                          hud_x3.dbr, 0 -> hud_x3_new.dbr; the HUD's loader 0x1235D0 loads it
//                          (ObjectManager::LoadTableFile, 0x1236AF), reads hudCaravanWindow
//                          (0x12851B) and hands it to the caravan window's loader (0x128549).
//                          hud_x3.dbr is read first, then hud_x3_new.dbr (the note names the
//                          other when the two differ); none -> xpack\ui\caravan\caravanwindow.dbr.
//   the caravan window     windowDefaultExtentX/Y: the caravan window's size (565 x 637), and
//                          TransferWindow: the Transfer page's record.
//   TransferWindow.dbr     WindowLocationX/Y: the page's place in the window, stored as floats at
//                          page+0x1C / +0x20 (TQ.exe 0xC2B3E..0xC2B74); InventoryDBR: the sack
//                          widget's record, loaded into page+0x88 (0xC2BD0, vtable slot +8 =
//                          0x10E850); TransferPlate: the cover (0xC2C50).
//   InventoryDBR           inventoryX/Y: the sack's place in the page (0x10E883 / 0x10E89D, x the
//                          UI scale into widget+0x10 / +0x14). inventoryXSize/YSize are read too
//                          (0x10E8B7 / 0x10E8D1) but do NOT size the Transfer sack: whenever the
//                          page is shown (0xC2F50, its vtable slot +0x20) TQ.exe sets
//                          InventorySack::SetDims(sack, cellW x 16, cellW x 15) - the constants
//                          16.0 / 15.0 at .rdata 0x2234AC / 0x2234A8 (0xC2FA1..0xC2FD6) - and
//                          writes the same two products over the widget's size. SetDims (Game.dll
//                          0x1B5790) keeps w / cellW and h / cellH as the grid, so the Transfer
//                          sack is 16 x 15 cells in every game the stock TQ.exe runs.
//   the cover's texture    its real pixel size from the .tex header (the plate the page draws); a
//                          page without a TransferPlate has no cover (P line: 0 x 0, no name).
//   BackgroundImage        the caravan window's background (its loader 0xC5F59 loads the record
//                          into caravan+0x1308, the texture the window's lattice is drawn in):
//                          bitmapPositionX/Y, its place in the window in window units, the
//                          units of every other place here: the base game's 0,0 puts the lattice
//                          of CaravanWindow01.tex (lines at 26 + 32 k, 125 + 32 k) on the derived
//                          grid 27,126; a 660 x 699 caravan's 0,11 puts its texture's 11,46 on
//                          the derived 11,57. The UI scale multiplies the place and the grid
//                          alike, so the record's value is used as it is. A window without a
//                          readable BackgroundImage: no place (the W line keeps its four fields,
//                          the background is not repainted).
//
// So a game moves the game's own Transfer sack (its origin = the page's place + the inventory's
// place) but cannot resize it: that sack is TQ.exe's 16 x 15. The rectangle written here is the
// area the collection's own sack uses: the inventory record's whole cells (inventoryXSize/YSize /
// 32), never fewer than TQ.exe's 16 x 15, at the same origin. The base game derives 27,126
// 512 x 480 = 16 x 15 (its record asks 512 x 160), which is the grid the mod has always measured;
// a game whose caravan is built for 20 x 20 derives 20 x 20; a game whose grid leaves the caravan
// window says so in the note.
#pragma once

#include "gen/catalogue_gen.h"

#include <string>

namespace gen {

// TQ.exe's Transfer sack (0xC2F50): 16 x 15 cells whatever the records say.
const int kTransferCols = 16, kTransferRows = 15;
// The catalogue's cell, in record px (the footprints are counted in these).
const int kTransferCellPx = 32;

struct UiGeometry {
    int x0 = 27, y0 = 126;             // the grid origin, caravan-window units
    int usableW = 512, usableH = 480;  // the rectangle the grid is laid out in (cols x rows cells)
    int plateW = 565, plateH = 521;    // the cover texture's real pixel size
    int bmpX = 0, bmpY = 1;            // the cover's bitmapPositionX/Y in the page
    std::string plate = "XPack\\UI\\Caravan\\InventoryTransferCover01.tex";   // its bitmapName
    int cols = kTransferCols, rows = kTransferRows;
    int windowW = 565, windowH = 637;  // caravanwindow.dbr windowDefaultExtentX/Y
    int pageX = 0, pageY = 126;        // TransferWindow.dbr WindowLocationX/Y
    bool bgKnown = true;               // the window's BackgroundImage record was read
    int bgX = 0, bgY = 0;              // its bitmapPositionX/Y (backgroundimage.dbr: 0,0)
    std::string note;                  // one line for the log

    // The P line (the Grim Dawn mod's: x0, y0, usableW, usableH, plateW, plateH, plate) and the W
    // line (the caravan window's size, the Transfer page's place in it and, when bgKnown, the
    // background's place in it), without line endings.
    std::string line() const;
    std::string windowLine() const;
    // uniq-geometry.txt: the two lines, CRLF.
    std::string fileText() const;
    // True when the grid lies wholly inside the caravan window.
    bool insideWindow() const {
        return x0 >= 0 && y0 >= 0 && x0 + usableW <= windowW && y0 + usableH <= windowH;
    }
};

// Reads the merged view out of g and fills out. False with a reason when a record or the cover
// texture cannot be read; the caller then keeps the vanilla defaults above.
bool deriveGeometry(const GameData& g, UiGeometry& out, std::string* error);

} // namespace gen
