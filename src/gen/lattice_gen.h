// lattice_gen.h - paints the caravan window's background with the cell lattice at the grid in
// force (GD's plate painter, src/gen/plate_gen.*, ported to the TQ caravan).
//
// WHERE THE LATTICE IS. The 16 x 15 lattice a player sees on the Transfer page is baked into the
// caravan window's own background, the BackgroundImage widget of the caravan window record
// (records/xpack/ui/caravan/backgroundimage.dbr: XPack\UI\Caravan\CaravanWindow01.tex, 565 x 637,
// bitmapPosition 0,0). The Transfer page's TransferPlate (InventoryTransferCover01.tex) holds no
// 16 x 15 lattice. So the painted texture is a copy of that background with the vanilla lattice
// erased and a lattice stamped where the grid in force lies; it keeps the background's own size
// and surface format, so the widget's frozen rectangle draws it unchanged.
//
// THE VANILLA LATTICE, measured on CaravanWindow01.tex (AE 2.10): a 1 px light line one pixel
// before each cell edge - x = 26 + 32 k (k = 0..16), y = 125 + 32 k (k = 0..15) for the grid at
// 27,126 - flanked by two darker pixels on each side, so one line is a 5 px band (line +-2). The
// painter takes the vanilla grid (its place and cells) and finds the bands from it; the erasure
// copies the nearest clean pixel of the panel into each band pixel (GD's eraseLines, the same 14
// directions), and the stamp is a 9-slice of the band around one vanilla interior cell (GD's
// Stamp / drawFrame with the corner and the strip both 5 px: every strip pixel is a band pixel).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gen {

// A grid on the background, in its pixels: the top-left of cell (0,0) and the cell count.
struct LatticeGrid {
    int x = 0, y = 0;
    int cols = 0, rows = 0;
    int cellW = 32, cellH = 32;
};

// An uncompressed TQ .tex surface (TEX 1 / TEX 2 around DDS / DDSR, 24 or 32 bpp, one mip) in
// its stored channel order. `off` = where the pixels start in the file the surface came from.
struct LatticeSurface {
    int w = 0, h = 0, step = 0;   // step = bytes per pixel (3 or 4)
    std::size_t off = 0;
    std::vector<std::uint8_t> px;
    std::uint8_t* at(int x, int y) { return &px[(std::size_t(y) * w + x) * step]; }
    const std::uint8_t* at(int x, int y) const { return &px[(std::size_t(y) * w + x) * step]; }
};

// False with the reason for a compressed, mipmapped, short or unknown surface.
bool latticeDecodeTex(const std::vector<std::uint8_t>& blob, LatticeSurface& out, std::string* why);

struct LatticeReport {
    int erased = 0;    // band pixels of the vanilla lattice replaced
    int frames = 0;    // cells of the grid in force stamped (cut to the texture where they cross it)
};

// The background `blob` with the vanilla grid's lattice erased and the cells of `want` stamped;
// `out` = the same file with only its pixels changed (the TEX and DDS headers kept byte for byte).
// A `want` of 0 x 0 cells erases the lattice and stamps nothing (the relic page: its slots draw
// their own plates, so the ground between them is plain).
// False with the reason when the surface is not one this reads or the vanilla grid does not lie
// inside it with one interior cell to cut the stamp from.
bool latticePaint(const std::vector<std::uint8_t>& blob, const LatticeGrid& vanilla,
                  const LatticeGrid& want, std::vector<std::uint8_t>& out, LatticeReport* rep,
                  std::string* why);

// The eight hex digits a painted background is named by: FNV-1a 32 over every input of the paint
// (the background's resource name and size, the vanilla grid, the grid in force).
std::string latticeHash(const std::string& texName, int texW, int texH, const LatticeGrid& vanilla,
                        const LatticeGrid& want);

// The leaf name (no folder, no .tex), GD's plateLeafName for a user grid's set:
// "uniq_plate_32x32_565x637_L1a2b3c4d" for the start-up game, "..._565x637_<game>_L..." for a
// custom game, <game> = eight hex digits of FNV-1a 32 over its tag. The engine takes a name part
// of 63 characters at most (ut_resname.h); a tag of any length keeps the name well under that,
// and two games with the same background never share a name in the plates folder or in the
// engine's texture cache.
std::string latticeLeafName(int cellW, int cellH, int texW, int texH, const std::string& tag,
                            const std::string& hash);

}  // namespace gen
