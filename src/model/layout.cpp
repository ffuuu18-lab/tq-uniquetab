#include "layout.h"
#include "../ut_rowmath.h"   // GD's row arithmetic, verbatim

#include <algorithm>

namespace gdut {

namespace {

char lowerAscii(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

}  // namespace

bool containsNoCase(std::string_view haystack, std::string_view needle) noexcept {
    if (needle.empty()) return true;
    if (needle.size() > haystack.size()) return false;
    const std::size_t last = haystack.size() - needle.size();
    for (std::size_t i = 0; i <= last; ++i) {
        std::size_t j = 0;
        while (j < needle.size() && lowerAscii(haystack[i + j]) == lowerAscii(needle[j])) ++j;
        if (j == needle.size()) return true;
    }
    return false;
}

void GridLayout::setCatalogue(const Catalogue* catalogue) {
    m_catalogue = catalogue;
    m_visible.clear();
    m_slotOf.assign(catalogue ? catalogue->itemCount() : 0, -1);
    m_sections.clear();
    m_columns = 1;
    m_rows = 0;
    m_contentHeight = 0;
    m_scroll = 0;
}

void GridLayout::setMetrics(const Metrics& m) {
    m_metrics = m;
    if (m_metrics.cellSize < 1) m_metrics.cellSize = 1;
    if (m_metrics.gap < 0) m_metrics.gap = 0;
    if (m_metrics.headerHeight < 0) m_metrics.headerHeight = 0;
    rebuildGeometry();
}

bool GridLayout::passes(int index, const ItemView& it, const Filters& f,
                        const Collection* owned) const {
    if (f.requireDefaultVisible && !it.has(ItemFlag::DefaultVisible)) return false;
    if ((f.groupMask & groupBit(it.group)) == 0) return false;
    const std::uint32_t clsBit = 1u << static_cast<int>(it.classification);
    if ((f.classificationMask & clsBit) == 0) return false;
    if (f.owned != OwnedFilter::Any) {
        const bool isOwned = owned ? owned->owned(index) > 0 : false;
        if (f.owned == OwnedFilter::OwnedOnly && !isOwned) return false;
        if (f.owned == OwnedFilter::MissingOnly && isOwned) return false;
    }
    if (!f.nameQuery.empty() && !containsNoCase(it.name, f.nameQuery)) return false;
    return true;
}

void GridLayout::relayout(const Viewport& viewport, const Filters& filters,
                          const Collection* owned) {
    m_viewport = viewport;
    if (m_viewport.w < 0) m_viewport.w = 0;
    if (m_viewport.h < 0) m_viewport.h = 0;

    m_visible.clear();
    m_sections.clear();
    if (!m_catalogue) {
        m_slotOf.clear();
        rebuildGeometry();
        return;
    }
    const std::size_t n = m_catalogue->itemCount();
    m_slotOf.assign(n, -1);
    m_visible.reserve(n);

    // Items are stored sorted by (group, level, name, record), so walking the
    // group ranges in enum order keeps the filtered list in display order and
    // makes every section contiguous.
    for (int g = 0; g < kSlotGroupCount; ++g) {
        const SlotGroup group = static_cast<SlotGroup>(g);
        if ((filters.groupMask & groupBit(group)) == 0) continue;
        const GroupRange range = m_catalogue->groupRange(group);
        if (range.count == 0) continue;
        Section section;
        section.group = group;
        section.firstVisible = static_cast<int>(m_visible.size());
        for (std::uint32_t i = 0; i < range.count; ++i) {
            const int index = static_cast<int>(range.first + i);
            const ItemView& it = m_catalogue->item(static_cast<std::size_t>(index));
            if (!passes(index, it, filters, owned)) continue;
            m_slotOf[static_cast<std::size_t>(index)] = static_cast<int>(m_visible.size());
            m_visible.push_back(index);
        }
        section.count = static_cast<int>(m_visible.size()) - section.firstVisible;
        if (section.count > 0) m_sections.push_back(section);
    }

    rebuildGeometry();
}

void GridLayout::rebuildGeometry() {
    const int step = m_metrics.cellSize + m_metrics.gap;
    m_columns = (m_viewport.w + m_metrics.gap) / step;
    if (m_columns < 1) m_columns = 1;
    m_rows = 0;

    if (sectioned()) {
        // Each group starts on a fresh row under its own header.
        int y = 0;
        for (Section& s : m_sections) {
            s.headerTop = y;
            y += m_metrics.headerHeight + m_metrics.gap;
            s.gridTop = y;
            s.rows = (s.count + m_columns - 1) / m_columns;
            m_rows += s.rows;
            y += s.rows * step;
        }
        m_contentHeight = y > 0 ? y - m_metrics.gap : 0;
    } else {
        // One continuous grid; sections stay as metadata for the renderer.
        const int n = static_cast<int>(m_visible.size());
        m_rows = (n + m_columns - 1) / m_columns;
        m_contentHeight = m_rows > 0 ? m_rows * step - m_metrics.gap : 0;
        for (Section& s : m_sections) {
            const int firstRow = s.firstVisible / m_columns;
            const int lastRow = (s.firstVisible + s.count - 1) / m_columns;
            s.headerTop = firstRow * step;
            s.gridTop = s.headerTop;
            s.rows = lastRow - firstRow + 1;
        }
    }
    scrollTo(m_scroll);
}

int GridLayout::maxScroll() const noexcept {
    const int over = m_contentHeight - m_viewport.h;
    return over > 0 ? over : 0;
}

void GridLayout::scrollTo(int px) {
    const int limit = maxScroll();
    if (px < 0) px = 0;
    if (px > limit) px = limit;
    m_scroll = px;
}

void GridLayout::scrollBy(int px) { scrollTo(m_scroll + px); }

int GridLayout::itemAtSlot(int slot) const noexcept {
    if (slot < 0 || slot >= static_cast<int>(m_visible.size())) return -1;
    return m_visible[static_cast<std::size_t>(slot)];
}

int GridLayout::slotOfItem(int itemIndex) const noexcept {
    if (itemIndex < 0 || static_cast<std::size_t>(itemIndex) >= m_slotOf.size()) return -1;
    return m_slotOf[static_cast<std::size_t>(itemIndex)];
}

VisibleRange GridLayout::visibleRange() const noexcept {
    VisibleRange out;
    if (m_visible.empty() || m_viewport.h <= 0) return out;
    const int step = m_metrics.cellSize + m_metrics.gap;
    const int top = m_scroll;
    const int bottom = m_scroll + m_viewport.h;

    if (!sectioned()) {
        int firstRow = top / step;
        if (firstRow < 0) firstRow = 0;
        if (firstRow >= m_rows) return out;
        int lastRow = (bottom - 1) / step;
        if (lastRow >= m_rows) lastRow = m_rows - 1;
        if (lastRow < firstRow) return out;
        const int n = static_cast<int>(m_visible.size());
        out.first = firstRow * m_columns;
        int end = (lastRow + 1) * m_columns;
        if (end > n) end = n;
        out.count = end - out.first;
        if (out.count < 0) out.count = 0;
        return out;
    }

    int first = -1;
    int end = 0;
    for (const Section& s : m_sections) {
        const int sectionBottom = s.gridTop + s.rows * step - m_metrics.gap;
        if (sectionBottom <= top) continue;          // fully above
        if (s.gridTop >= bottom) break;              // fully below
        int firstRow = (top - s.gridTop) / step;
        if (top <= s.gridTop) firstRow = 0;
        if (firstRow < 0) firstRow = 0;
        if (firstRow >= s.rows) continue;
        int lastRow = (bottom - 1 - s.gridTop) / step;
        if (lastRow >= s.rows) lastRow = s.rows - 1;
        if (lastRow < firstRow) continue;
        const int firstSlot = s.firstVisible + firstRow * m_columns;
        int endSlot = s.firstVisible + (lastRow + 1) * m_columns;
        if (endSlot > s.firstVisible + s.count) endSlot = s.firstVisible + s.count;
        if (first < 0) first = firstSlot;
        end = endSlot;
    }
    if (first < 0) return out;
    out.first = first;
    out.count = end - first;
    if (out.count < 0) out.count = 0;
    return out;
}

Rect GridLayout::cellRectBySlot(int slot) const noexcept {
    Rect r;
    if (slot < 0 || slot >= static_cast<int>(m_visible.size())) return r;
    const int stepSize = m_metrics.cellSize + m_metrics.gap;
    if (!sectioned()) {
        r.x = m_viewport.x + (slot % m_columns) * stepSize;
        r.y = m_viewport.y + (slot / m_columns) * stepSize - m_scroll;
        r.w = m_metrics.cellSize;
        r.h = m_metrics.cellSize;
        return r;
    }
    const Section* found = nullptr;
    for (const Section& s : m_sections) {
        if (slot >= s.firstVisible && slot < s.firstVisible + s.count) {
            found = &s;
            break;
        }
    }
    if (!found) return r;
    const int step = m_metrics.cellSize + m_metrics.gap;
    const int local = slot - found->firstVisible;
    const int row = local / m_columns;
    const int col = local % m_columns;
    r.x = m_viewport.x + col * step;
    r.y = m_viewport.y + found->gridTop + row * step - m_scroll;
    r.w = m_metrics.cellSize;
    r.h = m_metrics.cellSize;
    return r;
}

Rect GridLayout::cellRect(int itemIndex) const noexcept {
    return cellRectBySlot(slotOfItem(itemIndex));
}

Rect GridLayout::headerRect(int sectionIndex) const noexcept {
    Rect r;
    if (m_metrics.headerHeight <= 0) return r;
    if (sectionIndex < 0 || sectionIndex >= static_cast<int>(m_sections.size())) return r;
    const Section& s = m_sections[static_cast<std::size_t>(sectionIndex)];
    r.x = m_viewport.x;
    r.y = m_viewport.y + s.headerTop - m_scroll;
    r.w = m_viewport.w;
    r.h = m_metrics.headerHeight;
    return r;
}

int GridLayout::hitTest(int px, int py) const noexcept {
    if (m_visible.empty()) return -1;
    if (px < m_viewport.x || px >= m_viewport.x + m_viewport.w) return -1;
    if (py < m_viewport.y || py >= m_viewport.y + m_viewport.h) return -1;

    const int step = m_metrics.cellSize + m_metrics.gap;
    const int localX = px - m_viewport.x;
    const int col = localX / step;
    if (col >= m_columns) return -1;                       // right-hand padding
    if (localX - col * step >= m_metrics.cellSize) return -1;   // vertical gap

    const int contentY = py - m_viewport.y + m_scroll;

    if (!sectioned()) {
        if (contentY % step >= m_metrics.cellSize) return -1;   // horizontal gap
        const int row = contentY / step;
        if (row < 0 || row >= m_rows) return -1;
        const int slot = row * m_columns + col;
        if (slot < 0 || slot >= static_cast<int>(m_visible.size())) return -1;
        return m_visible[static_cast<std::size_t>(slot)];
    }

    for (const Section& s : m_sections) {
        if (contentY < s.gridTop) return -1;               // header row or its gap
        const int localY = contentY - s.gridTop;
        const int sectionHeight = s.rows * step;
        if (localY >= sectionHeight) continue;             // next section
        if (localY % step >= m_metrics.cellSize) return -1;      // horizontal gap
        const int row = localY / step;
        const int slot = s.firstVisible + row * m_columns + col;
        if (slot >= s.firstVisible + s.count) return -1;   // ragged last row
        return m_visible[static_cast<std::size_t>(slot)];
    }
    return -1;
}

// ---- the slot packer ------------------------------------------------------------------

bool slotOf(const std::vector<PackItem>& items, int cols, int rows, SlotGeometry* out) {
    SlotGeometry g;
    if (out) *out = g;
    if (items.empty() || cols <= 0 || rows <= 0) return false;
    for (const PackItem& it : items) {
        if (it.w < 1 || it.h < 1 || it.w > cols || it.h > rows) return false;
        g.w = std::max(g.w, it.w);
        g.h = std::max(g.h, it.h);
    }
    g.cols = cols / g.w;
    g.rows = rows / g.h;
    if (out) *out = g;
    return g.cols >= 1 && g.rows >= 1;
}

int packSlots(const std::vector<PackItem>& items, int cols, int rows,
              std::vector<PackPlacement>* out, SlotGeometry* slot) {
    if (out) out->clear();
    if (slot) *slot = SlotGeometry();
    if (cols <= 0 || rows <= 0) return -1;
    if (items.empty()) return 0;
    SlotGeometry g;
    if (!slotOf(items, cols, rows, &g)) return -1;
    if (slot) *slot = g;
    const int per = g.perPage();
    if (out) out->reserve(items.size());
    for (std::size_t i = 0; i < items.size(); ++i) {
        const PackItem& it = items[i];
        PackPlacement p;
        if (!placeInSlot(g, static_cast<int>(i), it.index, it.w, it.h, &p)) {
            if (out) out->clear();
            return -1;
        }
        if (out) out->push_back(p);
    }
    return static_cast<int>((items.size() + static_cast<std::size_t>(per) - 1) /
                            static_cast<std::size_t>(per));
}

// ---- one place by ordinal, one page (the OWN filter) ---------------------------------

bool placeInSlot(const SlotGeometry& g, int ordinal, int index, int fw, int fh,
                 PackPlacement* out) noexcept {
    const int per = g.perPage();
    if (per <= 0 || g.w < 1 || g.h < 1 || ordinal < 0 || fw < 1 || fh < 1 || fw > g.w ||
        fh > g.h || !out)
        return false;
    const int k = ordinal % per;
    PackPlacement p;
    p.index = index;
    p.page = ordinal / per;
    p.slotCol = (k % g.cols) * g.w;
    p.slotRow = (k / g.cols) * g.h;
    p.col = p.slotCol + (g.w - fw) / 2;
    p.row = p.slotRow + (g.h - fh) / 2;
    *out = p;
    return true;
}

int placePage(const SlotGeometry& g, const unsigned char* fw, const unsigned char* fh, int n,
              const unsigned char* show, int page, PackPlacement* out, int cap,
              int* shown) noexcept {
    int ordinal = 0, placed = 0;
    if (g.perPage() > 0 && fw && fh && n > 0) {
        for (int i = 0; i < n; ++i) {
            if (show && !show[i]) continue;
            PackPlacement p;
            const bool ok = placeInSlot(g, ordinal, i, fw[i], fh[i], &p);
            ++ordinal;
            if (ok && p.page == page && out && placed < cap) out[placed++] = p;
        }
    }
    if (shown) *shown = ordinal;
    return placed;
}

int pagesFor(const SlotGeometry& g, int shown) noexcept {
    const int per = g.perPage();
    if (per <= 0) return 0;
    if (shown <= 0) return 1;
    return (shown + per - 1) / per;
}

// ---- the row-offset window -------------------------------------------------------------

int slotRowsFor(const SlotGeometry& g, int shown) noexcept {
    if (g.cols <= 0 || g.rows <= 0 || shown <= 0) return 0;
    return (shown + g.cols - 1) / g.cols;
}

// GD's utMaxRowOf / utClampRow (the Grim Dawn mod's src\ut_rowmath.h, copied verbatim): a slot
// row of the window is GD's box row, `shown` its total, `cols` / `rows` the window's slots.
int maxRowOffset(const SlotGeometry& g, int shown) noexcept {
    return ut::utMaxRowOf(shown, g.cols, g.rows);
}

int clampRowOffset(const SlotGeometry& g, int shown, int offset) noexcept {
    return ut::utClampRow(offset, maxRowOffset(g, shown));
}

int placeWindow(const SlotGeometry& g, const unsigned char* fw, const unsigned char* fh, int n,
                const unsigned char* show, int rowOffset, PackPlacement* out, int cap,
                int* shown) noexcept {
    int ordinal = 0, placed = 0;
    const int per = g.perPage();
    if (per > 0 && fw && fh && n > 0 && rowOffset >= 0) {
        const int first = rowOffset * g.cols;   // the window's first ordinal
        for (int i = 0; i < n; ++i) {
            if (show && !show[i]) continue;
            const int k = ordinal - first;
            ++ordinal;
            if (k < 0 || k >= per) continue;
            PackPlacement p;
            if (placeInSlot(g, k, i, fw[i], fh[i], &p) && out && placed < cap) out[placed++] = p;
        }
    }
    if (shown) *shown = ordinal;
    return placed;
}

int windowLeftover(const SlotGeometry& g, int shown, int rowOffset, int hostCols, int hostRows,
                   CellRect out[3]) noexcept {
    if (!out || hostCols <= 0 || hostRows <= 0) return 0;
    int m = 0;
    const int per = g.perPage();
    int inWin = per > 0 ? shown - rowOffset * g.cols : 0;
    if (inWin < 0) inWin = 0;
    if (inWin > per) inWin = per;
    if (per <= 0 || inWin == 0) {   // nothing shown: every cell is left over
        out[m++] = CellRect{0, 0, hostCols, hostRows};
        return m;
    }
    const int fullRows = inWin / g.cols, rem = inWin % g.cols;
    const int usedW = g.cols * g.w;
    if (usedW < hostCols && fullRows > 0)
        out[m++] = CellRect{usedW, 0, hostCols - usedW, fullRows * g.h};
    if (rem > 0) out[m++] = CellRect{rem * g.w, fullRows * g.h, hostCols - rem * g.w, g.h};
    const int top = (fullRows + (rem > 0 ? 1 : 0)) * g.h;
    if (top < hostRows) out[m++] = CellRect{0, top, hostCols, hostRows - top};
    return m;
}

namespace {

void splitTabs(const std::string& line, std::vector<std::string>* f) {
    f->clear();
    std::size_t a = 0;
    for (;;) {
        const std::size_t t = line.find('\t', a);
        f->push_back(line.substr(a, t == std::string::npos ? std::string::npos : t - a));
        if (t == std::string::npos) break;
        a = t + 1;
    }
}

bool toInt(const std::string& s, int* v) {
    if (s.empty() || s.size() > 9) return false;
    int n = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        n = n * 10 + (c - '0');
    }
    *v = n;
    return true;
}

// the G line's cols / rows / cellW / cellH must be the slot of its entries (a group with no
// entry has no slot; its geometry is not checked).
bool slotMatches(const GroupList& g) {
    if (g.entries.empty()) return true;
    std::vector<PackItem> items;
    items.reserve(g.entries.size());
    for (const GroupEntry& e : g.entries) items.push_back(PackItem{0, e.w, e.h});
    SlotGeometry s;
    if (!slotOf(items, kHostCols, kHostRows, &s)) return false;
    return g.cols == s.cols && g.rows == s.rows && g.cellW == s.w * kCellPx &&
           g.cellH == s.h * kCellPx;
}

}  // namespace

bool parseGroupsText(const std::string& text, std::vector<GroupList>* out, std::string* error) {
    out->clear();
    std::vector<std::string> f;
    std::size_t pos = 0;
    int lineNo = 0;
    auto fail = [&](const char* why) {
        if (error) *error = "uniq-groups.txt line " + std::to_string(lineNo) + ": " + why;
        out->clear();
        return false;
    };
    while (pos < text.size()) {
        std::size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(pos, nl - pos);
        pos = nl + 1;
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        splitTabs(line, &f);
        if (f[0] == "G") {
            int n = 0;
            GroupList g;
            if (f.size() != 8 || !toInt(f[7], &n) || f[2].empty() || !toInt(f[3], &g.cols) ||
                !toInt(f[4], &g.rows) || !toInt(f[5], &g.cellW) || !toInt(f[6], &g.cellH))
                return fail("bad G line");
            if (!out->empty()) {
                if (static_cast<int>(out->back().entries.size()) != out->back().declared)
                    return fail("the previous group's E count differs from its G line");
                if (!slotMatches(out->back()))
                    return fail("the previous group's G geometry is not its entries' slot");
            }
            g.label = f[2];
            g.declared = n;
            out->push_back(g);
        } else if (f[0] == "E") {
            GroupEntry e;
            if (out->empty()) return fail("an E line before any G line");
            if (f.size() != 4 || f[1].empty() || !toInt(f[2], &e.w) || !toInt(f[3], &e.h))
                return fail("bad E line (want: E <record> <w> <h>)");
            if (e.w < 1 || e.w > 2 || e.h < 1 || e.h > 5)
                return fail("footprint outside 1..2 x 1..5");
            e.record = f[1];
            out->back().entries.push_back(e);
        } else {
            return fail("unknown line kind");
        }
    }
    if (out->empty()) {
        if (error) *error = "uniq-groups.txt holds no group";
        return false;
    }
    if (static_cast<int>(out->back().entries.size()) != out->back().declared)
        return fail("the last group's E count differs from its G line");
    if (!slotMatches(out->back()))
        return fail("the last group's G geometry is not its entries' slot");
    return true;
}

}  // namespace gdut
