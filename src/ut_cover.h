// ut_cover.h - the lattice follows the grid: the caravan window's background swapped for a copy
// painted with the cell lattice at the grid in force (GD's plate swap, ut_plate.cpp window+0x4B0,
// ported to the TQ caravan window).
//
// THE ENGINE (TQ.exe AE 2.10, read statically):
//   * the 16 x 15 lattice is in the caravan window's BackgroundImage (CaravanWindow01.tex), the
//     widget at caravan+0x1308 (loaded at 0xC5F9C with "BackgroundImage"; its vftable written at
//     0xC79EE: TQ.exe's bitmap class, rva 0x21C998). The Transfer page's own cover (page+0xE2C,
//     TransferPlate = InventoryTransferCover01.tex) carries no 16 x 15 lattice.
//   * the Transfer page's constructor 0xC33B0 stores its owner, the caravan window, at page+0xF1C
//     (0xC34D1); the caravan window holds its Transfer page at +0x1370 (0xC7BC6) and its vftable is
//     rva 0x21D438 (0xC787A).
//   * the bitmap's texture pointer is at widget+0x34. Its ONLY writer after the constructor is the
//     class's slot +0x88 (0xB6F20): when the field holds a texture whose file name differs from the
//     one asked for, it calls GraphicsEngine::UnloadTexture on it, then stores LoadTexture(name).
//     The record loader (slot +0x08, 0xB61F0) calls it at 0xB62ED; the scale slot +0x18 (0xB7070,
//     the UI-scale arithmetic) never touches +0x34. The destructor (0xB6D70) unloads whatever the
//     field holds.
//   * GraphicsEngine::UnloadTexture (Engine.dll 0x158EA0 -> 0x120130) finds the texture by name
//     and decrements a count; the resource is purged only at zero. A texture loaded twice is one
//     object with a count of two.
// THE RULES (GD's): the mod's texture is in the field only while the view is ON on the Transfer
// page with a grid in force (the player's, or the collection's area) that differs from the
// derived one; the engine's own pointer goes back at every view OFF (the tab, caravan close,
// caravan open, world up / down, the toggle), compared with the mod's pointer before the write. When the field holds anything else, the engine wrote it
// (its slot +0x88 unloaded the mod's texture): nothing is written, the engine's first pointer, the
// one the mod held, is given back with one UnloadTexture, and the swap is off for the session.
// A new grid in force while the swap is in: the engine's pointer goes back first, the previous
// copy is given back on a later frame (this frame drew it), then the new copy is loaded and
// written; a failed write-back leaves the copy to the window's destructor (ut_coverstep.h, the
// decisions tools\test_lattice drives). A refused paint is said once per session; a painted copy
// carries its background's fingerprint (<leaf>.src) and is painted again when the bytes change.
// Every write is under SEH; a fault turns the swap off with one ERROR.
#pragma once

namespace ut {

// ut_panel's grid in force (game thread, every frame; ut_layout.h utLatticeGridOf): the player's
// rectangle in caravan-window units and its cells, or the game's derived area when it is larger
// than the real sack (its origin and the collection's cells), or user = false for the derived grid.
void coverSetGrid(bool user, int x, int y, int cols, int rows);

// The painted copy for the grid in force is in the caravan window's field (game thread): false
// while the painter works on it, before a swap, and with the window's own background in place.
bool coverGridShown();

// The Transfer page draw's PRE (game thread): reads the caravan window's background, logs its
// inputs once per world, installs or keeps the painted copy, asks the painter for a missing one.
void coverTick(void* page, int pass);

// Every view OFF (viewForceOff, game thread): the engine's background back, compared first.
// worldGone: the world's objects are gone - the mod's loaded copy is given back too.
void coverRestore(const char* why, bool worldGone);

// Present, before the first world: the mod's plates\ folder becomes a directory source of the
// engine's file system (FileSystem::AddSource, partition 1, as the gray icons' folder), so a
// painted background loads by name.
void coverPrepare(bool noWorldYet);

}  // namespace ut
