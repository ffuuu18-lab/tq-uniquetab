// ut_graytex.h - the gray copy of a catalogue icon.
//
// TQ has no desaturating 2D shader: the one "saturation" in Engine.dll is a constant of
// Shaders/filter.ssh's bloom bright-pass (technique extractHot, GraphicsCanvas::HotBlurFrameBuffer
// 0x15D7F0), drawn with Begin / Render("HotBlur") / End on render surfaces, not through the canvas.
// The game's own gray looks are separate pre-made textures (grayBitmap, ...Greyed..., ...Gray...).
// So the catalogue generator writes a gray copy of every catalogue icon (this file's utGrayTex, in
// place on the TEX bytes it read from the archives) as gray\ug<16 hex>.tex beside the catalogue,
// and ut_panel hands the engine's own icon draw (TQ.exe 0x10ADE0, the texture at [widget+0x3C])
// the gray copy for an uncollected prototype.
//
// Pure: no engine, no allocation. Used by src\gen\generate.cpp, ut_panel.cpp and the tests
// (test_viewgate: the luminance on known pixels, a DXT1 block, the names; tools\graytex_check.py:
// the written files decode through tools\tqtex.py).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ut {

// Rec. 601 luma, rounded: (299 R + 587 G + 114 B + 500) / 1000.
inline uint8_t utGrayLuma(unsigned r, unsigned g, unsigned b) {
    return (uint8_t)((299u * r + 587u * g + 114u * b + 500u) / 1000u);
}

// A 5:6:5 colour to its gray 5:6:5 (the channels expanded to 8 bits the D3D way first).
inline uint16_t utGray565(uint16_t c) {
    const unsigned r5 = (c >> 11) & 31u, g6 = (c >> 5) & 63u, b5 = c & 31u;
    const unsigned y = utGrayLuma((r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2));
    const unsigned y5 = (y * 31u + 127u) / 255u, y6 = (y * 63u + 127u) / 255u;
    return (uint16_t)((y5 << 11) | (y6 << 5) | y5);
}

// One DXT colour block (8 bytes) in place. The palette is linear in the two endpoints and so is
// the luma, so gray endpoints with the same indices give the gray of every texel. DXT1's mode is
// the endpoints' order (c0 > c1: four colours; else three and a transparent index 3): when the gray
// endpoints flip that order they are swapped and the indices remapped (four colours: 0<->1, 2<->3;
// three: 0<->1); two equal gray endpoints in the four-colour mode become index 0 everywhere (one
// opaque gray). DXT3 / DXT5 colour blocks are always four-colour: the endpoints only.
inline void utGrayDxtColourBlock(uint8_t* b, bool dxt1) {
    const uint16_t c0 = (uint16_t)(b[0] | (b[1] << 8)), c1 = (uint16_t)(b[2] | (b[3] << 8));
    uint16_t g0 = utGray565(c0), g1 = utGray565(c1);
    uint32_t idx = (uint32_t)b[4] | ((uint32_t)b[5] << 8) | ((uint32_t)b[6] << 16) | ((uint32_t)b[7] << 24);
    if (dxt1) {
        const bool four = c0 > c1;
        if (four && g0 == g1) {
            idx = 0;
        } else if ((four && g0 < g1) || (!four && g0 > g1)) {
            const uint16_t t = g0;
            g0 = g1;
            g1 = t;
            uint32_t out = 0;
            for (int k = 0; k < 16; ++k) {
                uint32_t v = (idx >> (2 * k)) & 3u;
                if (four) v ^= 1u;                 // 0<->1, 2<->3
                else if (v < 2) v ^= 1u;           // 0<->1; the midpoint and the transparent stay
                out |= v << (2 * k);
            }
            idx = out;
        }
    }
    b[0] = (uint8_t)g0;
    b[1] = (uint8_t)(g0 >> 8);
    b[2] = (uint8_t)g1;
    b[3] = (uint8_t)(g1 >> 8);
    b[4] = (uint8_t)idx;
    b[5] = (uint8_t)(idx >> 8);
    b[6] = (uint8_t)(idx >> 16);
    b[7] = (uint8_t)(idx >> 24);
}

enum { kUtGrayNone = 0, kUtGrayRaw32, kUtGrayRaw24, kUtGrayDxt1, kUtGrayDxt3, kUtGrayDxt5 };

inline uint32_t utGrayRd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// A whole TQ .tex in place: 'TEX' 1 (the surface at 12) or 'TEX' 2 (at 13), the surface 'DDS ' or
// TQ's 'DDSR', every mip level. Uncompressed 32 / 24 bpp (masks 0 = the D3D order B, G, R, A, or
// the standard 0xFF0000 / 0xFF00 / 0xFF masks) -> each texel's R = G = B = its luma, alpha kept;
// DXT1 / DXT3 / DXT5 -> every colour block (utGrayDxtColourBlock), the alpha blocks kept. Answers
// the kind (kUtGray*), kUtGrayNone when the bytes are not a surface this can gray (then nothing is
// changed). All 1,479 distinct catalogue icons of AE 2.10 are DDSR 32 bpp with one mip.
inline int utGrayTex(uint8_t* d, size_t n) {
    if (!d || n < 4 || d[0] != 'T' || d[1] != 'E' || d[2] != 'X') return kUtGrayNone;
    const size_t off = d[3] == 1 ? 12 : d[3] == 2 ? 13 : 0;
    if (!off || n < off + 4 + 124) return kUtGrayNone;
    const uint8_t* s = d + off;
    if (s[0] != 'D' || s[1] != 'D' || s[2] != 'S' || (s[3] != ' ' && s[3] != 'R')) return kUtGrayNone;
    const uint32_t hsize = utGrayRd32(s + 4), h = utGrayRd32(s + 12), w = utGrayRd32(s + 16);
    uint32_t mips = utGrayRd32(s + 28);
    const uint32_t fourcc = utGrayRd32(s + 84), bpp = utGrayRd32(s + 88);
    const uint32_t rm = utGrayRd32(s + 92), gm = utGrayRd32(s + 96), bm = utGrayRd32(s + 100);
    const uint32_t caps2 = utGrayRd32(s + 4 + 108);
    if (hsize != 124 || !w || !h || w > 4096 || h > 4096 || (caps2 & 0x200u)) return kUtGrayNone;   // no cube map
    if (mips < 1) mips = 1;
    if (mips > 16) return kUtGrayNone;
    int kind = kUtGrayNone;
    if (fourcc == 0x31545844u) kind = kUtGrayDxt1;         // 'DXT1'
    else if (fourcc == 0x33545844u) kind = kUtGrayDxt3;    // 'DXT3'
    else if (fourcc == 0x35545844u) kind = kUtGrayDxt5;    // 'DXT5'
    else if (fourcc == 0 && (bpp == 32 || bpp == 24)) {
        const bool d3dOrder = (rm == 0 && gm == 0 && bm == 0) ||
                              (rm == 0xFF0000u && gm == 0xFF00u && bm == 0xFFu);
        if (!d3dOrder) return kUtGrayNone;
        kind = bpp == 32 ? kUtGrayRaw32 : kUtGrayRaw24;
    } else {
        return kUtGrayNone;
    }
    // the sizes first: nothing is changed unless every level is inside the file
    size_t at = off + 4 + hsize, need = 0;
    for (uint32_t m = 0, lw = w, lh = h; m < mips; ++m) {
        size_t sz;
        if (kind == kUtGrayRaw32 || kind == kUtGrayRaw24) {
            sz = (size_t)lw * lh * (bpp / 8);
        } else {
            sz = (size_t)((lw + 3) / 4) * ((lh + 3) / 4) * (kind == kUtGrayDxt1 ? 8u : 16u);
        }
        need += sz;
        lw = lw > 1 ? lw / 2 : 1;
        lh = lh > 1 ? lh / 2 : 1;
    }
    if (at + need > n) return kUtGrayNone;
    uint8_t* p = d + at;
    uint8_t* const end = p + need;
    if (kind == kUtGrayRaw32 || kind == kUtGrayRaw24) {
        const size_t step = bpp / 8;
        for (; p + step <= end; p += step) {   // B, G, R (, A)
            const uint8_t y = utGrayLuma(p[2], p[1], p[0]);
            p[0] = p[1] = p[2] = y;
        }
    } else {
        const size_t block = kind == kUtGrayDxt1 ? 8u : 16u, colour = kind == kUtGrayDxt1 ? 0u : 8u;
        for (; p + block <= end; p += block) utGrayDxtColourBlock(p + colour, kind == kUtGrayDxt1);
    }
    return kind;
}

// The gray file of a catalogue record: "ug" + the 64-bit FNV-1a of the record path (lower case,
// '\' as '/') in 16 lower-case hex digits + ".tex" - 22 characters, one file per record, the same
// name in the generator (the catalogue's record) and at run time (the prototype's record).
inline uint64_t utGrayHash(const char* record) {
    uint64_t h = 14695981039346656037ull;
    for (const char* p = record ? record : ""; *p; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c == '\\') c = '/';
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c - 'A' + 'a');
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

const size_t kUtGrayNameLen = 22;
inline bool utGrayNameOf(uint64_t h, char* out, size_t cap) {
    if (!out || cap < kUtGrayNameLen + 1) return false;
    static const char hex[] = "0123456789abcdef";
    out[0] = 'u';
    out[1] = 'g';
    for (int k = 0; k < 16; ++k) out[2 + k] = hex[(h >> (60 - 4 * k)) & 15u];
    out[18] = '.';
    out[19] = 't';
    out[20] = 'e';
    out[21] = 'x';
    out[22] = 0;
    return true;
}
inline bool utGrayName(const char* record, char* out, size_t cap) {
    return utGrayNameOf(utGrayHash(record), out, cap);
}

}  // namespace ut
