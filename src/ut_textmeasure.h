// ut_textmeasure.h - the width of the tab's text in the game's own fonts, the font that follows the
// letters, and the UTF-8 widening the wide text call needs.
//
// Pure: standard headers only, no Windows, no engine. The start-up read (ut_panel.cpp, on the
// worker, once per process) fills a UtTmFontSet from the nine fonts of Resources\Fonts.arc and
// installs it; the pad, the label and the search field measure through it on the render thread,
// and nothing is allocated per frame; tools/test_text.cpp fills one from the same archive and
// proves the advances.
//
// The measure is the engine's pen rule (GraphicsCanvas::RenderText -> GraphicsFont's line builder
// -> the renderer's font, whose glyph cache is FreeType's): the face is set ONCE at 14 pt and
// 300 dpi (FT_Set_Char_Size(face, 0, 14 << 6, 300, 300); a font whose head.flags bit 3 asks for
// whole pixels per em is scaled at 58 px per em, any other at 3733/64), every glyph is loaded
// hinted and keeps its advance in whole px at that size (FT_PIX_ROUND, then >> 6) divided by 56,
// and a run of text is (int)(floor(size x 0.875 + 0.5) x the sum of those advances), all in single
// precision, truncated - the float RenderText returns. Kerning is left out, so the width is an
// upper bound: the engine adds a pair's kerning only when its glyph cache computed that pair
// (the pair's left glyph was cached after its right one), and the fonts' kerning narrows. The ASCII
// advances are kept per font when it is read; any other code point is looked up in the font's
// sorted table. A code point the font lacks is measured as '?', as GD's measure does. Before a set
// is installed, or for a font that is not one of the nine, a string is measured at 0.55 em per
// code point (kUtTmNoTableEm, the estimate 1.0.1 used for every string).
//
// The font that follows the letters (GD's order, utb::utbDeriveFolder, over Titan Quest's fonts):
// 1. the role's own font when it has every letter;
// 2. the fonts the letters' script names - kana, Hangul or Han: override_asia.fnt; else Cyrillic
//    or Greek: arial.fnt, then dejavuserif.fnt;
// 3. every other font of the game in Fonts.arc's order - the first with them all;
// 4. when none has them all, the one with the most (on a tie the earlier, the role's font first):
//    the text is drawn all the same and the engine's own fallback decides the letters it lacks
//    (said once, utTmFallbackText). Letters above U+FFFF never decide it.
//
// UTF-8: the decoder is ut_textcfg.h's (utt::utf8Decode). A string that is not well-formed UTF-8
// is taken byte by byte as Latin-1, as GD's measure does.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "ut_sfnt.h"
#include "ut_textcfg.h"

namespace ut {

const int kUtTmFirst = 32;            // the cached range: printable ASCII
const int kUtTmChars = 95;
const int kUtTmMaxSize = 64;          // the largest caption size a measure is asked for
const float kUtTmNoTableEm = 0.55f;   // the estimate without a font table, per code point

// The engine's glyph cache: FT_Set_Char_Size(face, 0, kUtTmBaseChar, kUtTmBaseDpi, kUtTmBaseDpi),
// the advances kept over kUtTmBaseEm, a run scaled by floor(size x 0.875 + 0.5).
const int kUtTmBaseChar = 14 * 64;    // 14 pt in 26.6
const int kUtTmBaseDpi = 300;
const float kUtTmBaseEm = 56.0f;
const float kUtTmSizeScale = 0.875f;

// The nine fonts of Resources\Fonts.arc, in the archive's order. The path is the name LoadFont
// takes and the ini / buttons file spell; the archive entry is the part after "fonts/".
enum UtTmFontId {
    kUtTmAlbertusLight = 0,
    kUtTmAlbertus,
    kUtTmAlbertusCz,
    kUtTmArial,
    kUtTmArialRussian,
    kUtTmDejaVuSerif,
    kUtTmLinLibertine,
    kUtTmAsia,
    kUtTmTimesEe,
    kUtTmFontCount
};

inline const char* utTmFontPath(int i) {
    static const char* const k[kUtTmFontCount] = {
        "fonts/albertus mt light.fnt", "fonts/albertus mt.fnt",    "fonts/albertus_cz.fnt",
        "fonts/arial.fnt",             "fonts/arial_russian.fnt",  "fonts/dejavuserif.fnt",
        "fonts/linlibertine_r.fnt",    "fonts/override_asia.fnt",  "fonts/times_ee.fnt"};
    return i >= 0 && i < kUtTmFontCount ? k[i] : "";
}

// The font a path names (any case, '\' or '/'); -1 for a path that is not one of the nine.
inline int utTmFontOfPath(const char* path) {
    if (!path) return -1;
    for (int i = 0; i < kUtTmFontCount; ++i) {
        const char* a = utTmFontPath(i);
        const char* b = path;
        for (; *a && *b; ++a, ++b) {
            char c = *b;
            if (c == '\\') c = '/';
            if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
            if (c != *a) break;
        }
        if (!*a && !*b) return i;
    }
    return -1;
}

struct UtTmAdvance {
    char32_t cp;
    std::uint16_t adv;   // font units
};

// One font, read once: every code point its cmap maps with its advance, and the ASCII advances
// in px at the engine's base size. Measuring afterwards is arithmetic over it.
struct UtTmFont {
    bool valid = false;
    bool cff = false;                  // OpenType CFF ('OTTO')
    int format = 0;                    // the cmap subtable read (4 or 12)
    int upem = 0;                      // head.unitsPerEm
    std::string family;                // the name table's family (name id 1)
    std::vector<UtTmAdvance> adv;      // sorted by code point
    std::uint16_t qAdv = 0;            // '?': what a code point the font lacks is measured as
    unsigned headFlags = 0;            // head.flags (bit 3: whole pixels per em)
    std::int32_t xScale = 0;           // FreeType's x_scale at the engine's base size (16.16)
    unsigned char have[kUtTmChars] = {};
    std::uint16_t basePx[kUtTmChars] = {};   // the ASCII advances in px at the base size
    std::uint16_t qPx = 0;                    // '?' in px at the base size
};

inline bool utTmAdvOf(const UtTmFont& f, char32_t cp, std::uint16_t* adv) {
    const auto it = std::lower_bound(f.adv.begin(), f.adv.end(), cp,
                                     [](const UtTmAdvance& a, char32_t c) { return a.cp < c; });
    if (it == f.adv.end() || it->cp != cp) return false;
    if (adv) *adv = it->adv;
    return true;
}
inline bool utTmCovers(const UtTmFont& f, char32_t cp) { return f.valid && utTmAdvOf(f, cp, nullptr); }
// FreeType's fixed-point helpers (FT_DivFix, FT_MulFix: rounded, sign apart).
inline std::int64_t utTmFtDivFix(std::int64_t a, std::int64_t b) {
    if (b == 0) return 0;
    const bool neg = (a < 0) != (b < 0);
    const std::int64_t ua = a < 0 ? -a : a, ub = b < 0 ? -b : b;
    const std::int64_t q = (ua * 65536 + ub / 2) / ub;
    return neg ? -q : q;
}
inline std::int64_t utTmFtMulFix(std::int64_t a, std::int64_t b) {
    const bool neg = (a < 0) != (b < 0);
    const std::int64_t ua = a < 0 ? -a : a, ub = b < 0 ? -b : b;
    const std::int64_t r = (ua * ub + 0x8000) >> 16;
    return neg ? -r : r;
}
// x_scale at the base size: 14 pt at 300 dpi = 3733 in 26.6 (58.33 px); a font that asks for
// whole pixels per em (head.flags bit 3) is scaled at (3733 + 32) >> 6 = 58 px.
inline std::int32_t utTmXScale(int upem, unsigned headFlags) {
    const std::int64_t sw = ((std::int64_t)kUtTmBaseChar * kUtTmBaseDpi + 36) / 72;
    return (std::int32_t)utTmFtDivFix((headFlags & 8u) ? ((sw + 32) >> 6) << 6 : sw, upem);
}
// One advance (font units) in whole px at the base size: the hinted advance, FT_PIX_ROUND, >> 6.
inline int utTmBasePx(std::uint32_t adv, std::int32_t xScale) {
    return (int)(((utTmFtMulFix((std::int64_t)adv, xScale) + 32) & ~(std::int64_t)63) >> 6);
}
// The run's scale for a requested size: floor(size x 0.875 + 0.5), single precision.
inline float utTmRunScale(int size) {
    const double v = (double)((float)size * kUtTmSizeScale + 0.5f);
    long long r = (long long)v;   // floor
    if ((double)r > v) --r;
    return (float)r;
}
// The run's width as RenderText returns it: the sum of the glyphs' advances over 56, scaled,
// truncated.
inline float utTmRunWidth(float sum, int size) {
    if (size <= 0) return 0.0f;
    const float w = utTmRunScale(size) * sum;
    return (float)(int)w;
}

// The family name (name id 1): Windows' English record first, then any Windows one, then the
// Macintosh Roman one; letters outside printable ASCII become '?'. "" when there is none.
inline std::string utTmFamily(const unsigned char* d, std::size_t n) {
    std::size_t off = 0, len = 0;
    if (!utSfntTable(d, n, 0x6E616D65u, &off, &len) || len < 6) return std::string();   // 'name'
    const unsigned char* t = d + off;
    const std::size_t count = utSfntBe16(t + 2), strings = utSfntBe16(t + 4);
    if (6 + count * 12 > len || strings > len) return std::string();
    int best = 0;
    std::size_t at = 0, sl = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const unsigned char* r = t + 6 + i * 12;
        const unsigned pid = utSfntBe16(r), eid = utSfntBe16(r + 2), lid = utSfntBe16(r + 4);
        const std::size_t l = utSfntBe16(r + 8), o = utSfntBe16(r + 10);
        if (utSfntBe16(r + 6) != 1 || o > len - strings || l > len - strings - o) continue;
        const int rank = pid == 3 && lid == 0x409 ? 3 : pid == 3 ? 2 : (pid == 1 && eid == 0) ? 1 : 0;
        if (rank > best) {
            best = rank;
            at = strings + o;
            sl = l;
        }
    }
    std::string s;
    for (std::size_t k = 0; best > 0 && k < sl; k += best >= 2 ? 2 : 1) {
        const unsigned u = best >= 2 ? (k + 1 < sl ? utSfntBe16(t + at + k) : 0u) : t[at + k];
        s += (u >= 0x20 && u < 0x7F) ? (char)u : '?';
    }
    return s;
}

// Reads one font out of its bytes (a bare sfnt, TrueType or OpenType CFF): the cmap, head's
// unitsPerEm, hhea's numberOfHMetrics and hmtx's advances (a glyph past the long metrics takes the
// last one). False - and `f` invalid - for anything else, a truncated file or one whose tables
// point outside it. The caller may free the bytes afterwards: nothing points into them.
inline bool utTmFontRead(const unsigned char* d, std::size_t n, UtTmFont* f) {
    if (!f) return false;
    f->valid = false;
    f->adv.clear();
    f->family.clear();
    std::size_t base = 0, face = 0, ho = 0, hl = 0, hh = 0, hhl = 0, mo = 0, ml = 0;
    if (!utSfntFace(d, n, &base, &face)) return false;
    if (!utSfntTable(d, n, 0x68656164u, &ho, &hl) || hl < 54) return false;     // 'head'
    if (!utSfntTable(d, n, 0x68686561u, &hh, &hhl) || hhl < 36) return false;   // 'hhea'
    if (!utSfntTable(d, n, 0x686D7478u, &mo, &ml)) return false;                // 'hmtx'
    const int upem = (int)utSfntBe16(d + ho + 18);
    const unsigned headFlags = utSfntBe16(d + ho + 16);
    if (upem < 16 || upem > 16384) return false;
    const std::size_t nhm = utSfntBe16(d + hh + 34);
    if (nhm == 0 || nhm > ml / 4) return false;
    std::vector<UtSfntMap> map;
    int format = 0;
    if (!utSfntGlyphMap(d, n, &map, &format)) return false;
    f->adv.reserve(map.size());
    for (const UtSfntMap& m : map) {
        const std::size_t g = m.glyph < nhm ? m.glyph : nhm - 1;
        f->adv.push_back({m.cp, (std::uint16_t)utSfntBe16(d + mo + 4 * g)});
    }
    f->cff = utSfntBe32(d + base + face) == 0x4F54544Fu;
    f->format = format;
    f->upem = upem;
    f->headFlags = headFlags;
    f->xScale = utTmXScale(upem, headFlags);
    f->family = utTmFamily(d, n);
    std::uint16_t q = 0;
    if (!utTmAdvOf(*f, (char32_t)'?', &q)) q = (std::uint16_t)(upem / 2);   // no '?': half an em
    f->qAdv = q;
    f->qPx = (std::uint16_t)utTmBasePx(q, f->xScale);
    for (int c = 0; c < kUtTmChars; ++c) {
        std::uint16_t a = q;
        f->have[c] = utTmAdvOf(*f, (char32_t)(kUtTmFirst + c), &a) ? 1 : 0;
        if (!f->have[c]) a = q;
        f->basePx[c] = (std::uint16_t)utTmBasePx(a, f->xScale);
    }
    f->valid = true;
    return true;
}

// One code point's advance in px at the base size ('?' for one the font lacks).
inline int utTmBaseGlyphPx(const UtTmFont& f, char32_t cp) {
    if (cp >= (char32_t)kUtTmFirst && cp < (char32_t)(kUtTmFirst + kUtTmChars)) return f.basePx[cp - kUtTmFirst];
    std::uint16_t a = f.qAdv;
    if (cp > 0xFFFF || !utTmAdvOf(f, cp, &a)) return f.qPx;
    return utTmBasePx(a, f.xScale);
}

// Adds one code point to a run's sum as the engine does (each glyph's advance over 56, single
// precision). One above U+FFFF reaches the engine as two UTF-16 units, each a glyph of its own
// that no font has: two '?'.
inline void utTmRunAdd(const UtTmFont& f, char32_t cp, float* sum) {
    const float g = (float)utTmBaseGlyphPx(f, cp) / kUtTmBaseEm;
    *sum += g;
    if (cp > 0xFFFF) *sum += g;
}

// One code point drawn alone at `size`: its width as RenderText returns it.
inline int utTmGlyphPx(const UtTmFont& f, char32_t cp, int size) {
    if (size <= 0) return 0;
    float sum = 0.0f;
    utTmRunAdd(f, cp, &sum);
    return (int)utTmRunWidth(sum, size);
}

// Calls fn(cp) for every code point of `s`: UTF-8 when the whole string is well-formed, else every
// byte as Latin-1. Returns false when `s` was not UTF-8.
template <typename Fn>
inline bool utTmForEachCodePoint(const char* s, Fn fn) {
    const unsigned char* p = (const unsigned char*)s;
    const std::size_t n = std::strlen(s);
    bool utf8 = true;
    for (std::size_t i = 0; i < n;) {
        const int len = utt::utf8Decode(p + i, n - i, nullptr);
        if (len <= 0) {
            utf8 = false;
            break;
        }
        i += (std::size_t)len;
    }
    for (std::size_t i = 0; i < n;) {
        char32_t cp = p[i];
        int len = 1;
        if (utf8) len = utt::utf8Decode(p + i, n - i, &cp);
        fn(cp);
        i += (std::size_t)len;
    }
    return utf8;
}

// The same over UTF-16 (the search field's text): a surrogate pair is one code point.
template <typename Fn>
inline void utTmForEachCodePointW(const wchar_t* s, int n, Fn fn) {
    for (int i = 0; s && i < n && s[i]; ++i) {
        char32_t cp = (char32_t)(unsigned short)s[i];
        if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < n && (unsigned short)s[i + 1] >= 0xDC00 &&
            (unsigned short)s[i + 1] < 0xE000) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + ((char32_t)(unsigned short)s[i + 1] - 0xDC00);
            ++i;
        }
        fn(cp);
    }
}

inline int utTmCodePoints(const char* s) {
    int k = 0;
    if (s && *s) utTmForEachCodePoint(s, [&](char32_t) { ++k; });
    return k;
}

// Width in px of `s` (UTF-8) at `size` in font `f`.
inline float utTmFontWidth(const UtTmFont& f, const char* s, int size) {
    if (!s || !*s || size <= 0) return 0.0f;
    float sum = 0.0f;
    utTmForEachCodePoint(s, [&](char32_t cp) { utTmRunAdd(f, cp, &sum); });
    return utTmRunWidth(sum, size);
}
inline float utTmFontWidthW(const UtTmFont& f, const wchar_t* s, int n, int size) {
    if (!s || size <= 0) return 0.0f;
    float sum = 0.0f;
    utTmForEachCodePointW(s, n, [&](char32_t cp) { utTmRunAdd(f, cp, &sum); });
    return utTmRunWidth(sum, size);
}

// ---- the set in force ---------------------------------------------------------------------------
struct UtTmFontSet {
    UtTmFont font[kUtTmFontCount];
    int read = 0;   // how many of the nine were read
};
inline const UtTmFontSet*& utTmSetSlot() {
    static const UtTmFontSet* p = nullptr;
    return p;
}
// Installed once the set is complete (the start-up read; a test); nullptr = the estimate.
inline void utTmInstall(const UtTmFontSet* s) { utTmSetSlot() = s; }
inline const UtTmFontSet* utTmSet() { return utTmSetSlot(); }

// The fonts the engine refused to load in this world (bit i = font i): measured as fonts without a
// table, so a string is measured in the font that draws it. Set by the draw, cleared per world.
inline unsigned& utTmRefusedSlot() {
    static unsigned m = 0;
    return m;
}
inline void utTmRefuse(int i) {
    if (i >= 0 && i < kUtTmFontCount) utTmRefusedSlot() |= 1u << i;
}
inline void utTmRefusedClear() { utTmRefusedSlot() = 0; }
// Font `i` of `set` has a table and was not refused.
inline bool utTmUsable(const UtTmFontSet& set, int i) {
    return i >= 0 && i < kUtTmFontCount && set.font[i].valid && !(utTmRefusedSlot() & (1u << i));
}

// The two roles of the buttons file: the captions (font.caption) and the label line (font.label).
// Each role's font is one of the nine, or -1 for a path that is not (then the estimate measures).
enum UtTmRole { kUtTmCaption = 0, kUtTmLabel, kUtTmRoles };
inline int& utTmRoleSlot(int role) {
    static int r[kUtTmRoles] = {kUtTmAlbertusLight, kUtTmAlbertusLight};
    return r[role == kUtTmLabel ? 1 : 0];
}
inline void utTmSetRoleFont(int role, int font) { utTmRoleSlot(role) = font; }
inline int utTmRoleFont(int role) { return utTmRoleSlot(role); }

// ---- the font that follows the letters ---------------------------------------------------------
struct UtTmPick {
    int font = -1;          // -1: no table for the role's font
    bool coversAll = true;
    char32_t missing = 0;   // the first letter it lacks
    int lacking = 0;        // how many of the letters it lacks
};

// Kana, Hangul, Han (and the CJK punctuation, Bopomofo and fullwidth forms): GD's three ranges.
inline bool utTmIsCjk(char32_t cp) {
    return (cp >= 0x3040 && cp <= 0x30FF) || (cp >= 0x31F0 && cp <= 0x31FF) || (cp >= 0xFF65 && cp <= 0xFF9F) ||
           (cp >= 0x1100 && cp <= 0x11FF) || (cp >= 0x3130 && cp <= 0x318F) || (cp >= 0xA960 && cp <= 0xA97F) ||
           (cp >= 0xAC00 && cp <= 0xD7FF) || (cp >= 0xFFA0 && cp <= 0xFFDC) || (cp >= 0x3000 && cp <= 0x303F) ||
           (cp >= 0x3100 && cp <= 0x312F) || (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x4E00 && cp <= 0x9FFF) ||
           (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFF00 && cp <= 0xFF60);
}

// The rule above over the code points `each` walks (each(fn) calls fn(cp) per code point).
template <typename Each>
inline UtTmPick utTmPickBy(const UtTmFontSet& set, int roleFont, Each each) {
    UtTmPick p;
    if (!utTmUsable(set, roleFont)) return p;
    auto measure = [&](int font, UtTmPick* d) {
        d->font = font;
        d->coversAll = true;
        d->missing = 0;
        d->lacking = 0;
        each([&](char32_t cp) {
            if (cp < 0x20 || cp > 0xFFFF || utTmCovers(set.font[font], cp)) return;
            if (d->coversAll) d->missing = cp;
            d->coversAll = false;
            ++d->lacking;
        });
    };
    measure(roleFont, &p);
    if (p.coversAll) return p;
    bool cjk = false, cyrGreek = false;
    each([&](char32_t cp) {
        if (cp < 0x20 || cp > 0xFFFF || utTmCovers(set.font[roleFont], cp)) return;
        if (utTmIsCjk(cp)) cjk = true;
        else if (cp >= 0x0370 && cp <= 0x04FF) cyrGreek = true;
    });
    int order[kUtTmFontCount + 3];
    int n = 0;
    if (cjk) order[n++] = kUtTmAsia;
    if (cyrGreek) {
        order[n++] = kUtTmArial;
        order[n++] = kUtTmDejaVuSerif;
    }
    for (int i = 0; i < kUtTmFontCount; ++i) {
        bool dup = false;
        for (int k = 0; k < n; ++k) dup = dup || order[k] == i;
        if (!dup) order[n++] = i;
    }
    UtTmPick best = p;
    for (int k = 0; k < n; ++k) {
        if (order[k] == roleFont || !utTmUsable(set, order[k])) continue;
        UtTmPick d;
        measure(order[k], &d);
        if (d.coversAll) return d;
        if (d.lacking < best.lacking) best = d;
    }
    return best;
}
inline UtTmPick utTmPickFont(const UtTmFontSet& set, int roleFont, const char* s) {
    return utTmPickBy(set, roleFont, [&](auto fn) {
        if (s && *s) utTmForEachCodePoint(s, fn);
    });
}
inline UtTmPick utTmPickFontW(const UtTmFontSet& set, int roleFont, const wchar_t* s, int n) {
    return utTmPickBy(set, roleFont, [&](auto fn) { utTmForEachCodePointW(s, n, fn); });
}

// The font `s` is drawn in: `font` when >= 0 (a button's own), else the role's, followed to the
// font its letters need; -1 = no table (the caller draws in the role's loaded font).
inline int utTmDrawFont(int role, const char* s, int font = -1) {
    const UtTmFontSet* set = utTmSet();
    const int rf = font >= 0 ? font : utTmRoleFont(role);
    if (!set || !utTmUsable(*set, rf)) return -1;
    return utTmPickFont(*set, rf, s).font;
}
inline int utTmDrawFontW(int role, const wchar_t* s, int n, int font = -1) {
    const UtTmFontSet* set = utTmSet();
    const int rf = font >= 0 ? font : utTmRoleFont(role);
    if (!set || !utTmUsable(*set, rf)) return -1;
    return utTmPickFontW(*set, rf, s, n).font;
}

// Width in px of `s` (UTF-8) at `size` in the font that draws it (utTmDrawFont); without a table
// the estimate.
inline float utTmWidth(int role, const char* s, int size, int font = -1) {
    if (!s || !*s || size <= 0) return 0.0f;
    const int f = utTmDrawFont(role, s, font);
    if (f < 0) return kUtTmNoTableEm * (float)size * (float)utTmCodePoints(s);
    return utTmFontWidth(utTmSet()->font[f], s, size);
}
inline float utTmWidthW(int role, const wchar_t* s, int n, int size, int font = -1) {
    if (!s || size <= 0) return 0.0f;
    const int f = utTmDrawFontW(role, s, n, font);
    if (f < 0) {
        int k = 0;
        utTmForEachCodePointW(s, n, [&](char32_t) { ++k; });
        return kUtTmNoTableEm * (float)size * (float)k;
    }
    return utTmFontWidthW(utTmSet()->font[f], s, n, size);
}

// The width of `n` characters of a caption not known yet (the layout's fit reserves room for it):
// n x the role font's mean advance over A-Z, a-z and 0-9 at `size`; without a table the estimate.
inline float utTmCharsWidth(int role, int n, int size, int font = -1) {
    if (n <= 0 || size <= 0) return 0.0f;
    const UtTmFontSet* set = utTmSet();
    const int rf = font >= 0 ? font : utTmRoleFont(role);
    if (!set || !utTmUsable(*set, rf))
        return kUtTmNoTableEm * (float)size * (float)n;
    float sum = 0.0f;
    static const char kLetters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    for (const char* p = kLetters; *p; ++p) utTmRunAdd(set->font[rf], (char32_t)*p, &sum);
    return (float)n * utTmRunScale(size) * sum / 62.0f;
}

// GD's one line for a role drawn in a font that lacks some of its letters (utbDerivedFallbackText's
// words, the engine's fallback for Windows'): "" when it lacks none. `role` is "the captions" or
// "the label line".
inline std::string utTmFallbackText(const UtTmPick& p, const std::string& role) {
    if (p.coversAll || p.font < 0 || !p.lacking || !p.missing) return std::string();
    std::string s = std::string(utTmFontPath(p.font)) + " is used for " + role + "; " +
                    utt::utTextCodePointName(p.missing);
    if (p.lacking > 1) s += " and " + std::to_string(p.lacking - 1) + " more";
    s += p.lacking > 1 ? " are" : " is";
    return s + " drawn by the engine's own fallback font";
}

// ---- UTF-8 for the wide call -------------------------------------------------------------------

// `s` as UTF-16 into out[cap] (cap >= 1; always terminated), cut at the last whole code point that
// fits. Well-formed UTF-8 is decoded (a code point above U+FFFF becomes a surrogate pair); any other
// string is widened byte by byte (Latin-1), as before. Returns the units written.
inline std::size_t utWidenUtf8(const char* s, unsigned short* out, std::size_t cap,
                               bool* wasUtf8 = nullptr) {
    if (wasUtf8) *wasUtf8 = true;
    if (!out || cap == 0) return 0;
    out[0] = 0;
    if (!s) return 0;
    std::size_t n = 0;
    bool full = false;
    const bool utf8 = utTmForEachCodePoint(s, [&](char32_t cp) {
        if (full) return;
        if (cp >= 0x10000) {
            if (n + 2 > cap - 1) {
                full = true;
                return;
            }
            const char32_t v = cp - 0x10000;
            out[n++] = (unsigned short)(0xD800 + (v >> 10));
            out[n++] = (unsigned short)(0xDC00 + (v & 0x3FF));
        } else {
            if (n + 1 > cap - 1) {
                full = true;
                return;
            }
            out[n++] = (unsigned short)cp;
        }
    });
    out[n] = 0;
    if (wasUtf8) *wasUtf8 = utf8;
    return n;
}

// True when every byte of `s` is printable ASCII (the only text the char* call draws right).
inline bool utTmIsPlainAscii(const char* s) {
    for (const unsigned char* p = (const unsigned char*)s; p && *p; ++p) {
        if (*p < 0x20 || *p > 0x7E) return false;
    }
    return true;
}

// Cuts `s` into buf[cap] at the most whole code points from its start whose width (`widthOf`)
// fits `room` - at least `atLeast` (0 or 1) - never inside a UTF-8 sequence. Nothing is allocated.
// Returns the code points kept.
template <typename WidthOf>
inline int utTmCutToWidth(const char* s, float room, int atLeast, char* buf, std::size_t cap, WidthOf widthOf) {
    if (!buf || cap < 2) return 0;
    buf[0] = 0;
    if (!s) return 0;
    std::size_t k = 0, kept = 0;
    int chars = 0, keptChars = 0;
    while (s[k] && k + 1 < cap) {
        std::size_t e = k + 1;
        while (s[e] && ((unsigned char)s[e] & 0xC0) == 0x80 && e - k < 4) ++e;
        if (e + 1 > cap) break;
        for (std::size_t j = k; j < e; ++j) buf[j] = s[j];
        buf[e] = 0;
        ++chars;
        if (widthOf(buf) > room && chars > atLeast) break;
        kept = e;
        keptChars = chars;
        k = e;
    }
    buf[kept] = 0;
    return keptChars;
}

}  // namespace ut
