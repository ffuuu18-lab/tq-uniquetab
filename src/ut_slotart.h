// ut_slotart.h -, pure (no Windows, no engine): the collection slot's LOOK.
//
//  1. The rect route. The engine draws each item widget from its FOOTPRINT rect
//     (TQ.exe 0x10A9B0: the tint and the border texture over [widget+0x10..+0x1C] inset by k, then
//     the icon at vt+0x0C from the same rect). A collection slot is the group's LARGEST footprint and
//     every item sits at the slot's top-left cell, so a 1-wide sword in a 2-wide slot is drawn 16 px
//     left of the slot's centre with a tint and a border that frame only its own 1 x 4 cells.
//     utWidgetSlotRects turns the widget's footprint rect (in the widget's own units) into
//       * the SLOT rect: what the background (tint + border) is drawn over, and
//       * the CENTRED rect: the footprint shifted by ((slotW - itemW) / 2, (slotH - itemH) / 2)
//         cells, whole pixels; zero when the item fills the slot.
//  2. The slot art. The equipment window's item boxes carry no bitmap: the cracked
//     stone ground with the item type's drawing is baked into CharacterWindow01.tex. kUtSlotArt is
//     each collection group's box (tools\ui_slotart.py prints it from the game's database and
//     writes data\oracle\slotart.txt, which test_viewgate compares with this table), and
//     utSlotArtFit picks the texture rect: the whole box, stretched, when its aspect is within 35 %
//     of the slot's; otherwise the box's centred sub-rect at the slot's aspect (a uniform scale,
//     never a squash - the 69 x 124 hand art on the 1 x 5 spear and 1 x 4 bow slots).
#pragma once
#include "ut_padlayout.h"   // UtRectF, utPadFinite

namespace ut {

// ---- 1. the rect route -------------------------------------------------------------------------

inline float utSlotRound(float v) {
    const float f = (float)(long long)v;   // truncation toward zero
    const float r = v - f;
    if (r >= 0.5f) return f + 1.0f;
    if (r < -0.5f) return f - 1.0f;
    return f;
}

// foot = the widget's rect (x, y, w, h), item / slot = cells (top-left col, row; w, h), scale =
// the widget's draw scale [+0x78] (the engine draws w * scale, h * scale from x, y in DRAWN px).
// False (nothing to write) when the rect is not a plausible footprint of that item: not finite, a
// cell under 4 or over 256 units, the two cell sizes more than 1 % apart, the item not inside its
// slot, or a scale outside 0.25 .. 4. drawnCell = the page's ONE drawn cell (the measured
// grid / 16, canvas px; 0 = not known, no check): the widget's drawn cell (its footprint w / item
// cells x scale) must agree within 1 px, else refused - the two unit systems never mix here.
// a NEGATIVE (or NaN) drawnCell = the grid / 16 and the mod sack's live cell
// DISAGREE (panelCellVerdict -1): refused, never "no check".
inline bool utWidgetSlotRects(const UtRectF& foot, int itemCol, int itemRow, int itemW, int itemH,
                              int slotCol, int slotRow, int slotW, int slotH, float scale,
                              UtRectF* slot, UtRectF* centred, float drawnCell = 0.0f) {
    if (!slot || !centred || itemW < 1 || itemH < 1 || slotW < 1 || slotH < 1) return false;
    if (!(drawnCell >= 0.0f)) return false;   // the units disagree - refused
    if (!utPadFinite(foot.x) || !utPadFinite(foot.y) || !utPadFinite(foot.w) ||
        !utPadFinite(foot.h) || !utPadFinite(scale) || scale < 0.25f || scale > 4.0f)
        return false;
    if (itemCol < slotCol || itemRow < slotRow || itemCol + itemW > slotCol + slotW ||
        itemRow + itemH > slotRow + slotH)
        return false;
    const float cw = foot.w / (float)itemW, ch = foot.h / (float)itemH;
    if (!(cw >= 4.0f && cw <= 256.0f && ch >= 4.0f && ch <= 256.0f)) return false;
    const float d = cw > ch ? cw - ch : ch - cw;
    if (d > 0.01f * (cw > ch ? cw : ch)) return false;
    if (drawnCell > 0.0f) {   // the widget's DRAWN cell is the page's drawn cell
        const float dw = cw * scale - drawnCell, dh = ch * scale - drawnCell;
        if (!(dw <= 1.0f && dw >= -1.0f && dh <= 1.0f && dh >= -1.0f)) return false;
    }
    // x, y are unscaled pixels but a cell is DRAWN cw * scale px wide (TQ.exe 0x10A9B0): the way back
    // to the slot's top-left cell is in drawn px.
    slot->x = foot.x - (float)(itemCol - slotCol) * cw * scale;
    slot->y = foot.y - (float)(itemRow - slotRow) * ch * scale;
    slot->w = (float)slotW * cw;
    slot->h = (float)slotH * ch;
    // The icon is drawn w * scale wide from x: centre the DRAWN icon in the DRAWN slot.
    centred->x = slot->x + utSlotRound((slot->w - foot.w) * scale * 0.5f);
    centred->y = slot->y + utSlotRound((slot->h - foot.h) * scale * 0.5f);
    centred->w = foot.w;
    centred->h = foot.h;
    return true;
}

// a widget rect (x, y DRAWN, w, h x scale) as the engine draws it: canvas px from the origin.
inline UtRectF utWidgetDrawn(const UtRectF& r, float scale) {
    const UtRectF o = {r.x, r.y, r.w * scale, r.h * scale};
    return o;
}

// ---- 2. the slot art ---------------------------------------------------------------------------

struct UtSlotArtBox {
    const char* group;   // the collection group's label (uniq-groups.txt)
    const char* box;     // characterwindow.dbr's field
    int x, y, w, h;      // itemX, itemY, itemXSize, itemYSize (CharacterWindow01.tex pixels)
};
// CharacterWindowImage.dbr: bitmapName, placed at (0, 0) of the 660 x 637 window; the texture's
// own size (the file's; the engine may load it at another, GraphicsTexture::GetWidth says).
static const char kUtSlotArtTexture[] = "XPack2\\UI\\Character\\CharacterWindow01.tex";
const int kUtSlotArtTexW = 660, kUtSlotArtTexH = 637;
static const UtSlotArtBox kUtSlotArt[15] = {
    {"Helms", "equipHead", 163, 23, 69, 67},
    {"Torso", "equipUpperBody", 15, 222, 70, 104},
    {"Arms", "equipForearm", 312, 227, 69, 70},
    {"Legs", "equipLowerBody", 312, 361, 69, 67},
    {"Amulets", "equipNeck", 312, 312, 69, 33},
    {"Rings", "equipFinger1", 106, 57, 35, 33},
    {"Shields", "equipHandLeft", 312, 67, 69, 124},
    {"Axes", "equipHandRight", 15, 67, 69, 124},
    {"Maces", "equipHandRight", 15, 67, 69, 124},
    {"Staves", "equipHandRight", 15, 67, 69, 124},
    {"Swords", "equipHandRight", 15, 67, 69, 124},
    {"Throwing", "equipHandRight", 15, 67, 69, 124},
    {"Spears", "equipHandRight", 15, 67, 69, 124},
    {"Bows", "equipHandRight", 15, 67, 69, 124},
    {"Artifacts", "equipArtifact", 16, 359, 69, 70},
};

inline int utSlotArtFor(const char* label) {
    if (!label) return -1;
    for (int i = 0; i < 15; ++i) {
        const char* a = kUtSlotArt[i].group;
        const char* b = label;
        while (*a && *a == *b) ++a, ++b;
        if (!*a && !*b) return i;
    }
    return -1;
}

enum { kUtArtStretch = 1, kUtArtCover = 2 };

// The texture rect (in the box's texture pixels) for a slot of slotW x slotH (any unit): the whole
// box when the aspects are within 35 % (kUtArtStretch), else the box's centred sub-rect at the
// slot's aspect (kUtArtCover). 0 = nothing (a bad box or slot).
inline int utSlotArtFit(const UtSlotArtBox& b, float slotW, float slotH, UtRectF* tex) {
    if (!tex || b.w < 1 || b.h < 1 || !(slotW > 0.0f) || !(slotH > 0.0f)) return 0;
    const float boxA = (float)b.w / (float)b.h, slotA = slotW / slotH;
    const float q = slotA / boxA;
    if (q >= 1.0f / 1.35f && q <= 1.35f) {
        *tex = UtRectF{(float)b.x, (float)b.y, (float)b.w, (float)b.h};
        return kUtArtStretch;
    }
    if (slotA < boxA) {   // the slot is narrower: a centred column of the box, full height
        const float w = (float)b.h * slotA;
        *tex = UtRectF{(float)b.x + ((float)b.w - w) * 0.5f, (float)b.y, w, (float)b.h};
    } else {              // the slot is flatter: a centred band of the box, full width
        const float h = (float)b.w / slotA;
        *tex = UtRectF{(float)b.x, (float)b.y + ((float)b.h - h) * 0.5f, (float)b.w, h};
    }
    return kUtArtCover;
}

}  // namespace ut
