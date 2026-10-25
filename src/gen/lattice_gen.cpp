// lattice_gen.cpp - see lattice_gen.h.
#include "gen/lattice_gen.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace gen {

namespace {

// One lattice line is the light pixel and two darker pixels on each side.
const int kBand = 2;
const int kStampC = 2 * kBand + 1;   // the 9-slice corner (the crossing of two bands)
const int kStampT = 2 * kBand + 1;   // the strip's thickness (one band)

std::uint32_t rd32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) |
           (std::uint32_t(p[3]) << 24);
}

// the line positions of a grid: one pixel before each cell edge
int lineX(const LatticeGrid& g, int k) { return g.x - 1 + k * g.cellW; }
int lineY(const LatticeGrid& g, int k) { return g.y - 1 + k * g.cellH; }

// The vanilla lattice's bands inside its panel (the rectangle from the first line to the last);
// the outer lines' outer pixels belong to the window's frame and are left alone.
void latticeMask(const LatticeSurface& img, const LatticeGrid& v, std::vector<char>& m) {
    m.assign(std::size_t(img.w) * img.h, 0);
    const int px0 = lineX(v, 0), px1 = lineX(v, v.cols), py0 = lineY(v, 0), py1 = lineY(v, v.rows);
    auto set = [&](int x, int y) {
        if (x >= px0 && x <= px1 && y >= py0 && y <= py1 && x >= 0 && x < img.w && y >= 0 && y < img.h)
            m[std::size_t(y) * img.w + x] = 1;
    };
    for (int k = 0; k <= v.cols; ++k)
        for (int d = -kBand; d <= kBand; ++d)
            for (int y = py0; y <= py1; ++y) set(lineX(v, k) + d, y);
    for (int k = 0; k <= v.rows; ++k)
        for (int d = -kBand; d <= kBand; ++d)
            for (int x = px0; x <= px1; ++x) set(x, lineY(v, k) + d);
}

// The cell the stamp is cut from: the first interior cell whose four lines lie exactly where the
// derived grid puts them (at each edge's middle the line pixel is lighter than the two beside it
// across the line); cell (1,1) when none does. A hand-drawn lattice with a row one pixel taller
// (a custom game's 20 x 20 caravan: its second row is 33 px) would otherwise put every stamped
// frame's bottom line one pixel low.
int lumAt(const LatticeSurface& s, int x, int y) {
    const std::uint8_t* p = s.at(x, y);
    return (int(p[0]) + int(p[1]) + int(p[2])) / 3;
}
bool onLine(const LatticeSurface& s, int x, int y, int dx, int dy) {
    if (x - dx < 0 || y - dy < 0 || x + dx >= s.w || y + dy >= s.h) return false;
    const int c = lumAt(s, x, y);
    return c > lumAt(s, x - dx, y - dy) && c > lumAt(s, x + dx, y + dy);
}
void stampCell(const LatticeSurface& img, const LatticeGrid& v, int* sc, int* sr) {
    *sc = 1;
    *sr = 1;
    for (int r = 1; r + 1 < v.rows; ++r)
        for (int c = 1; c + 1 < v.cols; ++c) {
            const int x0 = lineX(v, c), x1 = lineX(v, c + 1), y0 = lineY(v, r), y1 = lineY(v, r + 1);
            const int mx = (x0 + x1) / 2, my = (y0 + y1) / 2;
            if (onLine(img, x0, my, 1, 0) && onLine(img, x1, my, 1, 0) && onLine(img, mx, y0, 0, 1) &&
                onLine(img, mx, y1, 0, 1)) {
                *sc = c;
                *sr = r;
                return;
            }
        }
}

// GD's eraseLines: take the nearest clean pixel sideways, then diagonally. Each direction reads a
// SNAPSHOT of the image as it was when that direction started and never samples a band pixel or
// one outside the panel's interior (the window's frame).
void eraseLines(LatticeSurface& img, const std::vector<char>& mask, const LatticeGrid& v) {
    static const int kDirs[14][2] = {{0, 5}, {0, -5}, {5, 0}, {-5, 0}, {5, 5}, {5, -5},
                                     {-5, 5}, {-5, -5}, {0, 9}, {0, -9}, {9, 0}, {-9, 0},
                                     {13, 0}, {-13, 0}};
    const int px0 = lineX(v, 0), px1 = lineX(v, v.cols), py0 = lineY(v, 0), py1 = lineY(v, v.rows);
    std::vector<char> todo = mask;
    std::vector<char> forbidden(mask.size());
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x) {
            const std::size_t i = std::size_t(y) * img.w + x;
            const bool inside = x > px0 && x < px1 && y > py0 && y < py1;
            forbidden[i] = (mask[i] || !inside) ? 1 : 0;
        }
    std::vector<std::uint8_t> snap;
    for (int d = 0; d < 14; ++d) {
        if (std::find(todo.begin(), todo.end(), 1) == todo.end()) break;
        const int dy = kDirs[d][0], dx = kDirs[d][1];
        snap = img.px;
        for (int y = 0; y < img.h; ++y) {
            const int sy = y + dy;
            if (sy < 0 || sy >= img.h) continue;
            for (int x = 0; x < img.w; ++x) {
                const std::size_t i = std::size_t(y) * img.w + x;
                if (!todo[i]) continue;
                const int sx = x + dx;
                if (sx < 0 || sx >= img.w) continue;
                const std::size_t j = std::size_t(sy) * img.w + sx;
                if (forbidden[j]) continue;
                std::memcpy(img.px.data() + i * img.step, snap.data() + j * img.step, img.step);
                todo[i] = 0;
            }
        }
    }
}

// GD's 9-slice stamp, cut from the band around one real vanilla cell. Every pixel of a corner or a
// strip is a band pixel, so the whole piece is carried (GD's alpha test kept only the gold line,
// whose frame is drawn on a ground of another tone).
struct Stamp {
    LatticeSurface tl, tr, bl, br, top, bot, left, right;
};

void cut(const LatticeSurface& src, int x, int y, int w, int h, LatticeSurface& out) {
    out.w = w;
    out.h = h;
    out.step = src.step;
    out.px.assign(std::size_t(w) * h * src.step, 0);
    for (int j = 0; j < h; ++j)
        std::memcpy(out.at(0, j), src.at(x, y + j), std::size_t(w) * src.step);
}

bool makeStamp(const LatticeSurface& donor, int x, int y, int w, int h, Stamp& st) {
    const int c = kStampC, t = kStampT;
    if (x < 0 || y < 0 || x + w > donor.w || y + h > donor.h || w < 2 * c + 1 || h < 2 * c + 1)
        return false;
    cut(donor, x, y, c, c, st.tl);
    cut(donor, x + w - c, y, c, c, st.tr);
    cut(donor, x, y + h - c, c, c, st.bl);
    cut(donor, x + w - c, y + h - c, c, c, st.br);
    cut(donor, x + c, y, w - 2 * c, t, st.top);
    cut(donor, x + c, y + h - t, w - 2 * c, t, st.bot);
    cut(donor, x, y + c, t, h - 2 * c, st.left);
    cut(donor, x + w - t, y + c, t, h - 2 * c, st.right);
    return true;
}

// tileW/tileH > 0 repeat the strip to that length; anything off the texture is cut.
void blit(LatticeSurface& dst, const LatticeSurface& src, int px, int py, int tileW, int tileH) {
    const int w = tileW > 0 ? tileW : src.w;
    const int h = tileH > 0 ? tileH : src.h;
    for (int j = 0; j < h; ++j) {
        const int dy = py + j;
        if (dy < 0 || dy >= dst.h) continue;
        for (int i = 0; i < w; ++i) {
            const int dx = px + i;
            if (dx < 0 || dx >= dst.w) continue;
            std::memcpy(dst.at(dx, dy), src.at(i % src.w, j % src.h), dst.step);
        }
    }
}

// One cell's frame: the bands of its four lines, (x, y) = the left line's and the top line's
// first band pixel, w x h = cell + one band on each side.
bool drawFrame(LatticeSurface& dst, const Stamp& st, int x, int y, int w, int h) {
    const int c = kStampC, t = kStampT;
    if (w < 2 * c + 1 || h < 2 * c + 1) return false;
    if (x + w <= 0 || y + h <= 0 || x >= dst.w || y >= dst.h) return false;
    blit(dst, st.tl, x, y, 0, 0);
    blit(dst, st.tr, x + w - c, y, 0, 0);
    blit(dst, st.bl, x, y + h - c, 0, 0);
    blit(dst, st.br, x + w - c, y + h - c, 0, 0);
    blit(dst, st.top, x + c, y, w - 2 * c, 0);
    blit(dst, st.bot, x + c, y + h - t, w - 2 * c, 0);
    blit(dst, st.left, x, y + c, 0, h - 2 * c);
    blit(dst, st.right, x + w - t, y + c, 0, h - 2 * c);
    return true;
}

std::uint32_t fnv1a(std::uint32_t h, const std::string& s) {
    for (unsigned char ch : s) {
        h ^= ch;
        h *= 16777619u;
    }
    return h;
}

}  // namespace

bool latticeDecodeTex(const std::vector<std::uint8_t>& blob, LatticeSurface& out, std::string* why) {
    char buf[128];
    if (blob.size() < 4 || blob[0] != 'T' || blob[1] != 'E' || blob[2] != 'X' ||
        (blob[3] != 1 && blob[3] != 2)) {
        if (why) *why = "not a TQ .tex (TEX 1 / TEX 2)";
        return false;
    }
    const std::size_t off = blob[3] == 1 ? 12 : 13;
    if (blob.size() < off + 4 + 124) {
        if (why) *why = "short file";
        return false;
    }
    const std::uint8_t* p = blob.data() + off;
    if (p[0] != 'D' || p[1] != 'D' || p[2] != 'S' || (p[3] != ' ' && p[3] != 'R')) {
        if (why) *why = "not a DDS / DDSR surface";
        return false;
    }
    const std::uint32_t hsize = rd32(p + 4), hh = rd32(p + 12), ww = rd32(p + 16), mips = rd32(p + 28);
    const std::uint32_t fourCC = rd32(p + 4 + 72 + 8), bpp = rd32(p + 4 + 72 + 12);
    const std::uint32_t rm = rd32(p + 4 + 72 + 16), gm = rd32(p + 4 + 72 + 20), bm = rd32(p + 4 + 72 + 24);
    if (hsize != 124) {
        if (why) { std::snprintf(buf, sizeof buf, "unexpected DDS header size %u", hsize); *why = buf; }
        return false;
    }
    if (fourCC != 0 || (bpp != 24 && bpp != 32)) {
        if (why) {
            std::snprintf(buf, sizeof buf, "only uncompressed 24/32-bit surfaces are handled (fourCC=%u bpp=%u)",
                          fourCC, bpp);
            *why = buf;
        }
        return false;
    }
    if (!((rm == 0 && gm == 0 && bm == 0) || (rm == 0xFF0000u && gm == 0xFF00u && bm == 0xFFu))) {
        if (why) *why = "a channel order other than B, G, R (, A)";
        return false;
    }
    if (mips > 1) {
        if (why) { std::snprintf(buf, sizeof buf, "mipmapped source (%u)", mips); *why = buf; }
        return false;
    }
    if (ww == 0 || ww > 4096 || hh == 0 || hh > 4096) {
        if (why) *why = "implausible surface size";
        return false;
    }
    const std::size_t step = bpp / 8;
    const std::size_t need = std::size_t(ww) * hh * step;
    const std::size_t at = off + 4 + hsize;
    if (blob.size() - at < need) {
        if (why) { std::snprintf(buf, sizeof buf, "short surface: %zu of %zu bytes", blob.size() - at, need); *why = buf; }
        return false;
    }
    out.w = int(ww);
    out.h = int(hh);
    out.step = int(step);
    out.off = at;
    out.px.assign(blob.begin() + std::ptrdiff_t(at), blob.begin() + std::ptrdiff_t(at + need));
    return true;
}

bool latticePaint(const std::vector<std::uint8_t>& blob, const LatticeGrid& vanilla,
                  const LatticeGrid& want, std::vector<std::uint8_t>& out, LatticeReport* rep,
                  std::string* why) {
    LatticeSurface img;
    if (!latticeDecodeTex(blob, img, why)) return false;
    const LatticeGrid& v = vanilla;
    const bool eraseOnly = want.cols == 0 && want.rows == 0;   // the relic page: no cell stamped
    if (v.cols < 3 || v.rows < 3 || v.cellW < 2 * kStampC + 1 || v.cellH < 2 * kStampC + 1 ||
        ((want.cols < 1 || want.rows < 1) && !eraseOnly) || want.cellW < 2 * kStampC + 1 ||
        want.cellH < 2 * kStampC + 1) {
        if (why) *why = "a grid too small to carry a lattice";
        return false;
    }
    if (lineX(v, 0) - kBand < 0 || lineY(v, 0) - kBand < 0 || lineX(v, v.cols) + kBand >= img.w ||
        lineY(v, v.rows) + kBand >= img.h) {
        if (why) *why = "the vanilla grid does not lie inside the background";
        return false;
    }
    // the vanilla grid must carry a lattice: its line pixels well lighter than its cells' insides
    // (CaravanWindow01.tex: about 140 against 45). A background drawn another way is not painted.
    long lineSum = 0, cellSum = 0, lineN = 0, cellN = 0;
    for (int r = 0; r < v.rows; ++r)
        for (int c = 0; c < v.cols; ++c) {
            const std::uint8_t* q = img.at(lineX(v, c) + v.cellW / 2 + 1, lineY(v, r) + v.cellH / 2 + 1);
            cellSum += (q[0] + q[1] + q[2]) / 3;
            ++cellN;
            const std::uint8_t* l = img.at(lineX(v, c), lineY(v, r) + v.cellH / 2 + 1);
            const std::uint8_t* t = img.at(lineX(v, c) + v.cellW / 2 + 1, lineY(v, r));
            lineSum += (l[0] + l[1] + l[2]) / 3 + (t[0] + t[1] + t[2]) / 3;
            lineN += 2;
        }
    if (lineSum / lineN < cellSum / cellN + 40) {
        if (why) {
            char buf[160];
            std::snprintf(buf, sizeof buf, "the background has no lattice at the derived grid (its lines %ld, "
                          "its cells %ld)", lineSum / lineN, cellSum / cellN);
            *why = buf;
        }
        return false;
    }
    // the stamp: the band around one vanilla cell on the derived lines (stampCell; (1,1) in the
    // base game), cut before the erasure
    Stamp st;
    int stc = 1, str = 1;
    stampCell(img, v, &stc, &str);
    const int sx = lineX(v, stc) - kBand, sy = lineY(v, str) - kBand;
    if (!makeStamp(img, sx, sy, v.cellW + 2 * kBand + 1, v.cellH + 2 * kBand + 1, st)) {
        if (why) *why = "the stamp cell is not inside the background";
        return false;
    }
    std::vector<char> mask;
    latticeMask(img, v, mask);
    int erased = 0;
    for (char c : mask) erased += c ? 1 : 0;
    eraseLines(img, mask, v);
    // every cell of the grid in force gets its frame (adjacent frames share their bands)
    int frames = 0;
    for (int r = 0; r < want.rows; ++r)
        for (int c = 0; c < want.cols; ++c)
            if (drawFrame(img, st, lineX(want, c) - kBand, lineY(want, r) - kBand,
                          want.cellW + 2 * kBand + 1, want.cellH + 2 * kBand + 1))
                ++frames;
    out = blob;
    std::memcpy(out.data() + img.off, img.px.data(), img.px.size());
    if (rep) {
        rep->erased = erased;
        rep->frames = frames;
    }
    return true;
}

std::string latticeHash(const std::string& texName, int texW, int texH, const LatticeGrid& v,
                        const LatticeGrid& g) {
    std::string low;
    for (char ch : texName) low += (ch >= 'A' && ch <= 'Z') ? char(ch - 'A' + 'a') : (ch == '/' ? '\\' : ch);
    char buf[160];
    std::snprintf(buf, sizeof buf, "|%dx%d|%d,%d,%dx%d,%dx%d|%d,%d,%dx%d,%dx%d|lattice 1", texW, texH, v.x, v.y,
                  v.cols, v.rows, v.cellW, v.cellH, g.x, g.y, g.cols, g.rows, g.cellW, g.cellH);
    const std::uint32_t h = fnv1a(fnv1a(2166136261u, low), buf);
    char hex[12];
    std::snprintf(hex, sizeof hex, "%08x", h);
    return hex;
}

std::string latticeLeafName(int cellW, int cellH, int texW, int texH, const std::string& tag,
                            const std::string& hash) {
    char game[12] = "";   // a custom game: eight hex digits of FNV-1a 32 over its tag
    if (!tag.empty()) std::snprintf(game, sizeof game, "_%08x", fnv1a(2166136261u, tag));
    char name[192];
    std::snprintf(name, sizeof name, "uniq_plate_%dx%d_%dx%d%s_L%s", cellW, cellH, texW, texH, game,
                  hash.c_str());
    return name;
}

}  // namespace gen
