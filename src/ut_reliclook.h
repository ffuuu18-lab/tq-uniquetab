// ut_reliclook.h - the relic and charm page's look, pure (tools\test_layout drives it): which slots
// show their item's own colours, which gray copy an empty slot shows, and the ground drawn over the
// cells of the area that hold no slot (every slot stands on an inventory cell of the lattice, the
// space between slots is plain ground).
#pragma once

#include "ut_relicpage.h"

namespace ut {

// A relic page prototype (UtProtoPlace::slot: 1 = the red slot, 2 + i = bonus i; seq = the journal
// row it was built from, 0 = the synthetic item): one built from a row is a slot that holds
// something - it shows the item's own colours; the synthetic item is the empty slot (gray).
inline bool utRelicProtoLit(int protoSlot, unsigned long long seq) {
    return protoSlot > 0 && seq != 0;
}

// The owned-marks style a page slot follows this frame for its gray copy and its background skip
// (ut_owned ownedGrayWanted / ownedBackgroundSkip): `ownedMarks` while the marks are trusted
// (`marksNow`: the inventory walk, the journal set, the measured grid), else 0 (no gray copy, no
// skip). A relic slot is always the gray style, whatever owned_marks says: an empty slot's gray copy
// is how the page tells it from a slot that holds something. Its lit / empty state is its journal
// row alone (utRelicProtoLit), so it needs only the journal set known - between a rebuild and the
// owned recount it keeps its gray copy instead of one frame of colour, and under the OWN filter
// too: OWN keeps the held blocks whole, and their empty slots stay gray as with OWN off.
inline int utRelicMarksStyle(bool marksNow, bool relicSlot, int ownedMarks, bool journalKnown) {
    if (relicSlot) return journalKnown ? 3 : 0;
    return marksNow ? ownedMarks : 0;
}

// The slots that take the relic look (utRelicMarksStyle's `relicSlot`, relic_empty_veil, no veil or
// frame of owned_marks): every relic slot (`relicSlot`: UtProtoPlace::slot > 0) and, on the artifact
// page, its formula cells (`artifactPage`: that page's unique places, slot 0). A formula cell stays a
// unique's place for everything else - lit by its record's journal rows, its count, its take. Under
// OWN a block stays while any of its slots holds something, so its formula can be one the
// collection lacks: the item pages' rule (no marks under OWN, every record shown is owned) would
// show it in colour; this look shows it gray like the block's empty slots. An item page's place never.
inline bool utRelicLook(bool relicSlot, bool artifactPage) {
    return relicSlot || artifactPage;
}

// An empty relic slot's veil (relic_empty_veil=1): the owned_marks=1 veil over a relic slot whose
// gray copy is wanted this frame (`grayWanted`: ut_owned ownedGrayWanted, the gray lend's own rule),
// so a held slot stands out by brightness and not by colour alone (a gray copy has its colour
// icon's luma; a legendary relic has little colour). A slot that holds something, the hovered slot
// and every slot while the journal set is unknown want no gray copy, so no veil; an item page's
// slot never (owned_marks has its veil).
inline bool utRelicEmptyVeiled(int emptyVeil, bool relicSlot, bool grayWanted) {
    return emptyVeil != 0 && relicSlot && grayWanted;
}

// The icon whose gray copy an empty relic slot shows. The generator writes the relic gray copies
// keyed by the ICON path (utGrayHash of the R line's bitmap): the red slot's piece is a partial
// (its shard bitmap), a green slot's a completed relic or charm (its relic bitmap). Null for a
// unique's place or a row with no such icon. An artifact's every slot shows its catalogue gray copy,
// which the generator keys by the RECORD (writeGrayIcons), so its record stands for the icon.
inline const char* utRelicGrayIcon(const UtRelicRow& r, int protoSlot) {
    if (r.artifact) return protoSlot >= 1 ? r.record.c_str() : nullptr;
    const std::string* icon = protoSlot == 1 ? &r.shardBitmap : protoSlot >= 2 ? &r.relicBitmap : nullptr;
    return icon && !icon->empty() ? icon->c_str() : nullptr;
}

// One run of ground: `w` cells from (col, line).
struct UtRelicRun {
    int col, line, w;
};

// The cells of a cols x rows area a slot stands on (`occ`, row-major, 1 = a slot): every place of
// the window but the dead red cells (records of one shard: plain ground, no plate). A place covers
// its size x size cells (2 x 2 on the artifact page).
inline void utRelicOccupancy(const UtRelicPlace* places, int n, int cols, int rows, unsigned char* occ) {
    for (int i = 0; i < cols * rows; ++i) occ[i] = 0;
    for (int i = 0; i < n; ++i) {
        const UtRelicPlace& p = places[i];
        if (p.kind == kUtRelicSlotDead || p.col < 0 || p.line < 0 || p.col >= cols || p.line >= rows) continue;
        const int z = p.size > 1 ? p.size : 1;
        for (int y = p.line; y < p.line + z && y < rows; ++y)
            for (int x = p.col; x < p.col + z && x < cols; ++x) occ[y * cols + x] = 1;
    }
}

// The runs of cells that hold no slot, left to right and top to bottom; how many were written (at
// most `cap`: the rest of the area is then not covered).
inline int utRelicGroundRuns(const unsigned char* occ, int cols, int rows, UtRelicRun* out, int cap) {
    int n = 0;
    for (int line = 0; line < rows; ++line) {
        int col = 0;
        while (col < cols) {
            if (occ[line * cols + col]) {
                ++col;
                continue;
            }
            const int from = col;
            while (col < cols && !occ[line * cols + col]) ++col;
            if (n >= cap) return n;
            out[n++] = UtRelicRun{from, line, col - from};
        }
    }
    return n;
}

// ---- the block panels and the dark edges ---------------------------------------------------------
// One block's panel on the relic page: the cells its places cover, from column 0 to its widest
// line's last slot and from its first line to its last shown one. `pos` = the block's place in the
// layout (the tone alternates by it, so a block keeps its tone while the wheel moves the window);
// `part` = 'R' relic, 'C' charm, 'A' artifact.
struct UtRelicPanel {
    int block;
    char part;
    int pos;
    int col, line, cols, lines;
};

// relic_split: the panel of a window whose block starts another part (-1 = one part in the window).
inline int utRelicPartChange(const UtRelicPanel* p, int n) {
    for (int i = 1; i < n; ++i)
        if (p[i].part != p[i - 1].part) return i;
    return -1;
}

// The panels of a window's places (utRelicWindowPlaces: reading order, dead cells included). The
// places of one block follow each other, so each run of one block is one panel; `firstPos` = the
// window's first block in the layout. Returns how many were written (at most `cap`).
inline int utRelicPanelsOf(const UtRelicList& l, const UtRelicPlace* places, int n, int firstPos,
                           UtRelicPanel* out, int cap) {
    int k = 0;
    int cur = -1;
    for (int i = 0; i < n; ++i) {
        const UtRelicPlace& p = places[i];
        if (p.row < 0 || p.row >= (int)l.rows.size()) continue;
        const int b = l.rows[(size_t)p.row].block;
        if (b < 0 || b >= (int)l.blocks.size()) continue;
        const int z = p.size > 1 ? p.size : 1;
        if (b != cur) {
            if (k >= cap) return k;
            cur = b;
            out[k] = UtRelicPanel{b, l.blocks[(size_t)b].part, firstPos + k, p.col, p.line, z, z};
            ++k;
            continue;
        }
        UtRelicPanel& q = out[k - 1];
        const int x1 = q.col + q.cols > p.col + z ? q.col + q.cols : p.col + z;
        const int y1 = q.line + q.lines > p.line + z ? q.line + q.lines : p.line + z;
        if (p.col < q.col) q.col = p.col;
        if (p.line < q.line) q.line = p.line;
        q.cols = x1 - q.col;
        q.lines = y1 - q.line;
    }
    return k;
}

// A formula cell of the artifact page stands on its cells, and its block's panel takes it in (a
// third formula's line under a row that does not wrap).
inline void utRelicFormulaCell(const UtRelicList& l, const UtRelicFormulaPlace& p, int cols, int rows,
                               unsigned char* occ, UtRelicPanel* panels, int pn) {
    for (int y = p.line < 0 ? 0 : p.line; y < p.line + p.h && y < rows; ++y)
        for (int x = p.col < 0 ? 0 : p.col; x < p.col + p.w && x < cols; ++x) occ[y * cols + x] = 1;
    if (p.row < 0 || p.row >= (int)l.rows.size()) return;
    for (int k = 0; k < pn; ++k) {
        UtRelicPanel& q = panels[k];
        if (q.block != l.rows[(size_t)p.row].block) continue;
        const int x1 = q.col + q.cols > p.col + p.w ? q.col + q.cols : p.col + p.w;
        const int y1 = q.line + q.lines > p.line + p.h ? q.line + q.lines : p.line + p.h;
        if (p.col < q.col) q.col = p.col;
        if (p.line < q.line) q.line = p.line;
        q.cols = x1 - q.col;
        q.lines = y1 - q.line;
    }
}

// A panel's tone (0..3): relic blocks 0 / 1, charm blocks 2 / 3 (another hue), alternating between
// neighbouring blocks by their place in the layout. The artifacts (a page of their own) take the
// relics' gold.
inline int utRelicPanelTone(char part, int pos) {
    return (part == 'C' ? 2 : 0) + (pos & 1);
}

// The width in px of a dark edge at the drawn cell's scale (the cell / 32): the edge around a
// slot's count (all eight sides) and a panel's frame. One pixel up to 1.49, then the scale rounded.
inline int utRelicEdgePx(float scale) {
    const int px = (int)(scale + 0.5f);
    return px < 1 ? 1 : px;
}

}  // namespace ut
