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
// (`marksNow`: the inventory walk, the journal set, the measured grid). A relic slot's lit / empty
// state is its journal row alone (utRelicProtoLit), so it also follows `ownedMarks` with only the
// journal set known - between a rebuild and the owned recount it keeps its gray copy instead of
// one frame of colour - and under the OWN filter too (the marks are not trusted there): OWN keeps
// the held blocks whole, and their empty slots stay gray exactly as with OWN off, only the slots
// that hold something show their colours. Else 0 (no gray copy, no skip).
inline int utRelicMarksStyle(bool marksNow, bool relicSlot, int ownedMarks, bool journalKnown) {
    if (marksNow) return ownedMarks;
    return relicSlot && journalKnown ? ownedMarks : 0;
}

// The icon whose gray copy an empty relic slot shows. The generator writes the relic gray copies
// keyed by the ICON path (utGrayHash of the R line's bitmap): the red slot's piece is a partial
// (its shard bitmap), a green slot's a completed relic or charm (its relic bitmap). Null for a
// unique's place or a row with no such icon.
inline const char* utRelicGrayIcon(const UtRelicRow& r, int protoSlot) {
    const std::string* icon = protoSlot == 1 ? &r.shardBitmap : protoSlot >= 2 ? &r.relicBitmap : nullptr;
    return icon && !icon->empty() ? icon->c_str() : nullptr;
}

// One run of ground: `w` cells from (col, line).
struct UtRelicRun {
    int col, line, w;
};

// The cells of a cols x rows area a slot stands on (`occ`, row-major, 1 = a slot): every place of
// the window but the dead red cells (records of one shard: plain ground, no plate).
inline void utRelicOccupancy(const UtRelicPlace* places, int n, int cols, int rows, unsigned char* occ) {
    for (int i = 0; i < cols * rows; ++i) occ[i] = 0;
    for (int i = 0; i < n; ++i) {
        const UtRelicPlace& p = places[i];
        if (p.kind == kUtRelicSlotDead || p.col < 0 || p.line < 0 || p.col >= cols || p.line >= rows) continue;
        occ[p.line * cols + p.col] = 1;
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
// `part` = 'R' relic, 'C' charm.
struct UtRelicPanel {
    int block;
    char part;
    int pos;
    int col, line, cols, lines;
};

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
        if (b != cur) {
            if (k >= cap) return k;
            cur = b;
            out[k] = UtRelicPanel{b, l.blocks[(size_t)b].part, firstPos + k, p.col, p.line, 1, 1};
            ++k;
            continue;
        }
        UtRelicPanel& q = out[k - 1];
        const int x1 = q.col + q.cols > p.col + 1 ? q.col + q.cols : p.col + 1;
        const int y1 = q.line + q.lines > p.line + 1 ? q.line + q.lines : p.line + 1;
        if (p.col < q.col) q.col = p.col;
        if (p.line < q.line) q.line = p.line;
        q.cols = x1 - q.col;
        q.lines = y1 - q.line;
    }
    return k;
}

// A panel's tone (0..3): relic blocks 0 / 1, charm blocks 2 / 3 (another hue), alternating between
// neighbouring blocks by their place in the layout.
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
