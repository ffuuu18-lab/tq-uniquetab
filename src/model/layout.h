// layout.h - grid layout for the collection tab.  Pure arithmetic: it decides
// which catalogue items are shown, where each cell lands inside a rectangle,
// and which cell a mouse position is over.  No drawing, no engine types.
//
// The renderer calls relayout() once per filter/viewport/ownership
// change, then per frame walks visibleRange(), asks cellRect() for each slot
// and draws the icon; sections() gives the optional group header rows.

#ifndef GDUT_MODEL_LAYOUT_H
#define GDUT_MODEL_LAYOUT_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "catalogue.h"
#include "collection.h"

namespace gdut {

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    bool empty() const noexcept { return w <= 0 || h <= 0; }
    bool contains(int px, int py) const noexcept {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

using Viewport = Rect;   // the game's transfer grid is x=105 y=4 w=320 h=608

enum class OwnedFilter : std::uint8_t { Any = 0, OwnedOnly = 1, MissingOnly = 2 };

// Bit per SlotGroup.
constexpr std::uint64_t groupBit(SlotGroup g) noexcept {
    return std::uint64_t(1) << static_cast<int>(g);
}
constexpr std::uint64_t kAllGroupsMask = (std::uint64_t(1) << kSlotGroupCount) - 1;
// Helm .. Relic: equipment plus relics, the default display set.
constexpr std::uint64_t kDefaultGroupsMask = (std::uint64_t(1) << kDefaultSlotGroupCount) - 1;

// Bit per Classification.
constexpr std::uint32_t kAllClassificationsMask = (1u << kClassificationCount) - 1;

struct Filters {
    std::uint64_t groupMask = kDefaultGroupsMask;
    std::uint32_t classificationMask = kAllClassificationsMask;
    OwnedFilter   owned = OwnedFilter::Any;
    std::string   nameQuery;          // case-insensitive substring, empty = no filter
    bool          requireDefaultVisible = true;   // drop blueprints/augments/consumables/quest
};

struct Metrics {
    int cellSize = 40;      // square icon cell, pixels
    int gap = 2;            // pixels between cells and between rows
    int headerHeight = 0;   // >0 turns on one header row per non-empty group
};

// One group's block inside the laid-out content.  Content-space y (add the
// viewport origin and subtract the scroll offset for screen coordinates).
struct Section {
    SlotGroup group = SlotGroup::Other;
    int firstVisible = 0;   // index into the filtered list
    int count = 0;
    int headerTop = 0;      // content y of the header row (== gridTop if no headers)
    int gridTop = 0;        // content y of the first cell row
    int rows = 0;
};

struct VisibleRange {
    int first = 0;   // index into the filtered list
    int count = 0;
};

// ASCII case-insensitive substring test (item names are ASCII in 1.3.0.8).
bool containsNoCase(std::string_view haystack, std::string_view needle) noexcept;

class GridLayout {
public:
    GridLayout() = default;

    void setCatalogue(const Catalogue* catalogue);
    const Catalogue* catalogue() const noexcept { return m_catalogue; }

    void setMetrics(const Metrics& m);
    const Metrics& metrics() const noexcept { return m_metrics; }

    // With headerHeight > 0 every group starts on a fresh row under a header
    // row; with headerHeight == 0 the groups run together in one continuous
    // grid (sections() is then metadata only).
    bool sectioned() const noexcept { return m_metrics.headerHeight > 0; }

    // Rebuilds the filtered list and the geometry.  `owned` may be null when
    // no ownership filter is used; if it is null and a filter asks for owned or
    // missing items, everything is treated as not owned.  Keeps the scroll
    // offset where it can (clamped to the new content height).
    void relayout(const Viewport& viewport, const Filters& filters,
                  const Collection* owned = nullptr);

    // --- geometry ---------------------------------------------------------
    int columns() const noexcept { return m_columns; }
    int rows() const noexcept { return m_rows; }
    int contentHeight() const noexcept { return m_contentHeight; }
    int maxScroll() const noexcept;
    int scrollOffset() const noexcept { return m_scroll; }
    void scrollBy(int px);
    void scrollTo(int px);
    const Viewport& viewport() const noexcept { return m_viewport; }

    // --- filtered list ----------------------------------------------------
    int visibleItemCount() const noexcept { return static_cast<int>(m_visible.size()); }
    // slot -> catalogue item index (-1 when out of range)
    int itemAtSlot(int slot) const noexcept;
    // catalogue item index -> slot in the filtered list (-1 when filtered out)
    int slotOfItem(int itemIndex) const noexcept;
    const std::vector<int>& visibleItems() const noexcept { return m_visible; }
    const std::vector<Section>& sections() const noexcept { return m_sections; }

    // Slots whose cell intersects the viewport at the current scroll offset.
    VisibleRange visibleRange() const noexcept;

    // Screen rect of a catalogue item's cell.  Empty rect when the item is
    // filtered out; otherwise the rect even if it is scrolled off-screen.
    Rect cellRect(int itemIndex) const noexcept;
    // Same, addressed by slot in the filtered list.
    Rect cellRectBySlot(int slot) const noexcept;
    // Screen rect of a section's header row (empty when headers are off).
    Rect headerRect(int sectionIndex) const noexcept;

    // Catalogue item index under a screen pixel, or -1 (gaps, header rows,
    // padding and anything outside the viewport all give -1).
    int hitTest(int px, int py) const noexcept;

private:
    bool passes(int index, const ItemView& it, const Filters& f, const Collection* owned) const;
    void rebuildGeometry();

    const Catalogue* m_catalogue = nullptr;
    Metrics m_metrics;
    Viewport m_viewport;
    std::vector<int> m_visible;     // slot -> catalogue index
    std::vector<int> m_slotOf;      // catalogue index -> slot, -1
    std::vector<Section> m_sections;
    int m_columns = 1;
    int m_rows = 0;
    int m_contentHeight = 0;
    int m_scroll = 0;
};

// ============================================================================================
// (Titan Quest): the SLOT PACKER for the Transfer page.
//
// TQ inventories are footprint-true: an item covers w x h cells (1..2 x 1..5 over the catalogue),
// and the host page is the Transfer sack, forced to 16 x 15 cells on every refresh (measured). The
// collection page therefore shows ONE GROUP at a time, as many 16 x 15 pages as it needs.
//
// ("one box per catalogue item", as GD): every record
// of a group gets the SAME slot, the group's largest footprint (max w, max h over its records). A
// page holds floor(16 / w) x floor(15 / h) slots, filled in reading order (left to right, top to
// bottom, from the top-left cell) in list order; each record's footprint sits CENTRED in its slot
// (offset floor((w - fw) / 2), floor((h - fh) / 2) cells). the earlier first-fit packer is gone (no GD
// test used it).
//
// Pure: no engine, no Windows. The DLL (ut_live.cpp) and tests\model_test.cpp run this same
// code over the same uniq-groups.txt, so the page counts the design notes list are the game's.
// ============================================================================================

struct PackItem {
    int index = 0;   // the caller's handle (the entry index inside its group)
    int w = 1;       // footprint in cells
    int h = 1;
};

// A group's slot: its size in cells and how many fit on a page.
struct SlotGeometry {
    int w = 0, h = 0;          // the largest footprint of the group
    int cols = 0, rows = 0;    // slots per page across / down
    int perPage() const noexcept { return cols * rows; }
};

struct PackPlacement {
    int index = 0;
    int page = 0;
    int col = 0;       // the footprint's top-left cell (centred inside its slot)
    int row = 0;
    int slotCol = 0;   // the slot's top-left cell (slot size = the group's SlotGeometry)
    int slotRow = 0;
};

inline constexpr int kHostCols = 16;   // the Transfer sack, forced by its page accessor
inline constexpr int kHostRows = 15;
inline constexpr int kCellPx = 32;     // uniq-groups.txt's G line gives the slot in 32 px cells

// The slot of `items` on a cols x rows page. False when there is no item or a footprint is not
// legal (w/h < 1, or larger than the page).
bool slotOf(const std::vector<PackItem>& items, int cols, int rows, SlotGeometry* out);

// Packs `items` in order into uniform slots (see above). Returns the page count (0 for no items),
// or -1 when a footprint is not legal - nothing is placed then. `slot` (optional) gets the slot.
int packSlots(const std::vector<PackItem>& items, int cols, int rows,
              std::vector<PackPlacement>* out, SlotGeometry* slot = nullptr);

// the place of the record shown as number `ordinal` (0-based, in list order among the
// records SHOWN) in slot geometry `g`: page = ordinal / perPage, the slot in reading order, the
// footprint fw x fh centred in it. packSlots is this over every record; the OWN filter is this
// over the owned ones only. False when `g` is empty, the ordinal negative or the footprint
// larger than the slot.
bool placeInSlot(const SlotGeometry& g, int ordinal, int index, int fw, int fh,
                 PackPlacement* out) noexcept;

// (GD's owned-only view, "the OWN filter"): the records of ONE group shown on `page`, in
// list order, in the GROUP's slot `g` (never the subset's: the boxes keep their size when the
// filter is on). `show` null = every record; else only the records whose flag is non-zero are
// shown, packed into consecutive slots. Writes at most `cap` placements for `page` into `out`
// and returns how many; `*shown` (optional) gets the number of records shown over ALL pages.
// No allocation: the DLL calls it at every page build.
int placePage(const SlotGeometry& g, const unsigned char* fw, const unsigned char* fh, int n,
              const unsigned char* show, int page, PackPlacement* out, int cap,
              int* shown = nullptr) noexcept;

// Pages for `shown` records in geometry `g`: ceil(shown / perPage), and at least 1 (an empty
// owned-only view is ONE empty page, never zero pages). 0 when `g` is empty.
int pagesFor(const SlotGeometry& g, int shown) noexcept;

// The page window, so that scrolling moves by slot rows and non-last pages always take the
// full tab grid: the view shows a WINDOW of `g.rows` slot rows starting at slot row
// `rowOffset` of the group's records laid out in reading order (`g.cols` slots a row). The wheel
// moves the offset by one slot row, `<` `>` / PgUp / PgDn by a window. The offset is clamped to
// 0 .. max(0, slotRows - g.rows), so the window stays FULL whenever the group has at least g.rows
// slot rows (the last window overlaps the one before it); a shorter group shows from row 0.
//
// slot rows needed for `shown` records: ceil(shown / cols); 0 for none or an empty geometry.
int slotRowsFor(const SlotGeometry& g, int shown) noexcept;
// The largest legal offset: max(0, slotRowsFor - g.rows).
int maxRowOffset(const SlotGeometry& g, int shown) noexcept;
// `offset` clamped to 0 .. maxRowOffset.
int clampRowOffset(const SlotGeometry& g, int shown, int offset) noexcept;
// placePage for a window: the shown records whose slot row is inside [rowOffset, rowOffset +
// g.rows), placed as if the window were page 0 (slotRow counts from the window's top). The offset
// is used as given (clamp it first). Same contract as placePage otherwise; `page` is 0.
int placeWindow(const SlotGeometry& g, const unsigned char* fw, const unsigned char* fh, int n,
                const unsigned char* show, int rowOffset, PackPlacement* out, int cap,
                int* shown = nullptr) noexcept;

// A rectangle of host cells (col, row, w, h), e.g. a leftover area of the 16 x 15 grid.
struct CellRect {
    int col = 0, row = 0, w = 0, h = 0;
};
// The cells of a hostCols x hostRows grid that NO slot of the window holding a record uses: at
// most three disjoint rectangles, in this order - the strip right of the full slot rows when
// cols x w < hostCols, the cells after the last record of a partial last row (to the right edge,
// one slot high), and every cell row under the last used slot row (the whole width). An empty
// window gives the whole grid; a window whose slots fill the grid gives none. Returns the count.
int windowLeftover(const SlotGeometry& g, int shown, int rowOffset, int hostCols, int hostRows,
                   CellRect out[3]) noexcept;

// One group of uniq-groups.txt as writes it: `G <i> <label> <cols> <rows> <cw> <ch> <n>`
// then n lines `E <record> <w> <h>` (tab separated, CRLF or LF). the G line's geometry IS
// the slot - cols x rows slots per page, each cw x ch pixels (32 px cells) - and it must equal the
// slot the E lines give (their largest footprint); the parser refuses a group where it does not.
struct GroupEntry {
    std::string record;
    int w = 1;
    int h = 1;
};
struct GroupList {
    std::string label;
    int declared = 0;               // the G line's count
    int cols = 0, rows = 0;         // the G line's slots per page across / down
    int cellW = 0, cellH = 0;       // the G line's slot size in pixels
    std::vector<GroupEntry> entries;
};

// Parses the whole text. False (and *error) on a malformed line, an E before any G, a footprint
// outside 1..2 x 1..5, a group whose E count differs from its G line, or a G line whose geometry is
// not its entries' slot: refuse, never guess.
bool parseGroupsText(const std::string& text, std::vector<GroupList>* out,
                     std::string* error = nullptr);

}  // namespace gdut

#endif  // GDUT_MODEL_LAYOUT_H
