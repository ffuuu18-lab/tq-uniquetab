#pragma once
// The collection's re-fit on a grid of C x R cells (GD's "every group re-fitted"): every group is
// packed on the new cells, all of them or none; a several-group button's view takes the new slot
// counts; the shown window keeps its first visible item (utRowKeep, ut_layout.h). Pure: the live
// table (ut_live.cpp liveSetGrid) calls it and the layout suite runs it over the same packer.

#include <vector>

#include "model/layout.h"
#include "ut_layout.h"   // utRowKeep

namespace ut {

struct UtGridPack {
    std::vector<std::vector<gdut::PackPlacement>> placed;   // per group
    std::vector<gdut::SlotGeometry> slot;                   // per group
    std::vector<int> pages;                                 // per group
    int total = 0;                                          // pages over every group
    int failed = -1;   // the first group that cannot be packed on the cells, -1 = none
};

// items[i] = group i's records (entry index, footprint). True when EVERY group packs on
// cols x rows; false at the first that cannot (out->failed = its index), and then nothing of
// `out` is meant to be applied: the grid in force stays.
inline bool utGridPackAll(const std::vector<std::vector<gdut::PackItem>>& items, int cols, int rows,
                          UtGridPack* out) {
    if (!out) return false;
    out->placed.assign(items.size(), std::vector<gdut::PackPlacement>());
    out->slot.assign(items.size(), gdut::SlotGeometry());
    out->pages.assign(items.size(), 0);
    out->total = 0;
    out->failed = -1;
    if (cols < 1 || rows < 1) {
        out->failed = 0;
        return false;
    }
    for (std::size_t i = 0; i < items.size(); ++i) {
        const int p = gdut::packSlots(items[i], cols, rows, &out->placed[i], &out->slot[i]);
        if (p < 1 && !(p == 0 && items[i].empty())) {   // a group with no record: no page
            out->failed = (int)i;
            return false;
        }
        out->pages[i] = p;
        out->total += p;
    }
    return true;
}

// A several-group button's view on the new cells: its slot (the largest footprint of its groups)
// counted again across and down, and its pages for `records` records.
inline int utGridUnionSlots(gdut::SlotGeometry* s, int cols, int rows, int records) {
    if (!s) return 0;
    s->cols = s->w > 0 ? cols / s->w : 0;
    s->rows = s->h > 0 ? rows / s->h : 0;
    return gdut::pagesFor(*s, records);
}

}  // namespace ut
