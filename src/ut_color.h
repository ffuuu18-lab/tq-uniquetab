// ut_color.h - a colour named in uniquetab.ini (search_mark_color): a name from a fixed table or
// "#rrggbb". Pure (no engine, no Windows, no allocation); tools\test_search.cpp tests every name,
// hex in both cases and the malformed inputs.
#pragma once

namespace ut {

struct UtColorName {
    const char* name;
    unsigned char r, g, b;
};

// The named colours. "blue" is GD's hovered-plate blue, the search mark's first look.
const UtColorName kUtColorNames[] = {
    {"gold", 0xff, 0xd7, 0x00},   {"green", 0x3c, 0xf0, 0x3c}, {"white", 0xf0, 0xf0, 0xf0},
    {"red", 0xff, 0x3c, 0x3c},    {"orange", 0xff, 0x9a, 0x1e}, {"cyan", 0x40, 0xe0, 0xff},
    {"magenta", 0xff, 0x50, 0xff}, {"blue", 0x45, 0x6c, 0x93},
};
const int kUtColorNameCount = (int)(sizeof(kUtColorNames) / sizeof(kUtColorNames[0]));
// What a key that names no colour falls back to.
const char* const kUtColorDefault = "gold";

inline int utColorHexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

inline char utColorLower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

// `text` -> its colour: a name of the table (any case) or '#' and exactly six hex digits (any
// case). False (and the outputs untouched) for anything else - an empty or null text, an unknown
// name, a short or long hex, a character that is not a hex digit, a space inside.
inline bool utColorParse(const char* text, unsigned char* r, unsigned char* g, unsigned char* b) {
    if (!text || !text[0]) return false;
    unsigned char v[3] = {0, 0, 0};
    if (text[0] == '#') {
        for (int i = 0; i < 3; ++i) {
            const int hi = utColorHexDigit(text[1 + 2 * i]);
            if (hi < 0) return false;
            const int lo = utColorHexDigit(text[2 + 2 * i]);
            if (lo < 0) return false;
            v[i] = (unsigned char)(hi * 16 + lo);
        }
        if (text[7] != 0) return false;
    } else {
        int found = -1;
        for (int k = 0; k < kUtColorNameCount && found < 0; ++k) {
            const char* a = kUtColorNames[k].name;
            const char* t = text;
            while (*a && *t && *a == utColorLower(*t)) ++a, ++t;
            if (!*a && !*t) found = k;
        }
        if (found < 0) return false;
        v[0] = kUtColorNames[found].r;
        v[1] = kUtColorNames[found].g;
        v[2] = kUtColorNames[found].b;
    }
    if (r) *r = v[0];
    if (g) *g = v[1];
    if (b) *b = v[2];
    return true;
}

}  // namespace ut
