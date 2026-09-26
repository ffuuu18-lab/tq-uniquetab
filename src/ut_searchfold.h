// ut_searchfold.h - the fold the property search matches with. PURE: no engine, no Windows.
//
// One rule for both sides: the text the engine builds for an item's rollover, and the query.
// Both are folded to UTF-8 by the same functions, and a query matches a line when its fold is a
// substring of the line's fold (Grim Dawn's semantics: one substring, no word logic).
//   * ASCII letters to lower case;
//   * the Latin-1 and Latin Extended-A letters to their base letter (o for the o with a diaeresis,
//     ae for the ae ligature, ss for the sharp s), so "mjolnir" finds the name spelled with the
//     diaeresis;
//   * U+2018 / U+2019 (the curly apostrophes) to ', and U+00A0 (the no-break space) to a space;
//   * Cyrillic capitals to their small letters;
//   * the colour escapes {^x} and ^x (x an ASCII letter) dropped;
//   * every control character to a space, so a folded line never carries a line break of its own
//     and a folded query never carries one either: the '\n' between two lines of a record can
//     never be part of a match, so no match spans two lines.
// ut_textfold.h stays the ASCII fold the catalogue names use; it is Grim Dawn's and unchanged.
#pragma once

#include <stddef.h>

#include <string>
#include <vector>

namespace ut {

// The base-letter spelling of U+00C0..U+017F, or null when the code point is not a letter there
// (the multiplication and division signs).
inline const char* utSearchLatinBase(unsigned cp) {
    static const char* const kLatin1[64] = {
        "a", "a", "a", "a", "a", "a", "ae", "c",       // U+00C0..U+00C7
        "e", "e", "e", "e", "i", "i", "i", "i",        // U+00C8..U+00CF
        "d", "n", "o", "o", "o", "o", "o", nullptr,    // U+00D0..U+00D7
        "o", "u", "u", "u", "u", "y", "th", "ss",      // U+00D8..U+00DF
        "a", "a", "a", "a", "a", "a", "ae", "c",       // U+00E0..U+00E7
        "e", "e", "e", "e", "i", "i", "i", "i",        // U+00E8..U+00EF
        "d", "n", "o", "o", "o", "o", "o", nullptr,    // U+00F0..U+00F7
        "o", "u", "u", "u", "u", "y", "th", "y"};      // U+00F8..U+00FF
    // U+0100..U+017F, one letter each; '1' marks the two ligatures (IJ ij, OE oe).
    static const char kExtA[] =
        "aaaaaa" "cccccccc" "dddd" "eeeeeeeeee" "gggggggg" "hhhh" "iiiiiiiiii" "11" "jj" "kkk"
        "llllllllll" "nnnnnnnnn" "oooooo" "22" "rrrrrr" "ssssssss" "tttttt" "uuuuuuuuuuuu" "ww"
        "yyy" "zzzzzz" "s";
    static_assert(sizeof(kExtA) == 129, "one entry per code point of Latin Extended-A");
    static const char* const kOne[26] = {"a", "b", "c", "d", "e", "f", "g", "h", "i",
                                         "j", "k", "l", "m", "n", "o", "p", "q", "r",
                                         "s", "t", "u", "v", "w", "x", "y", "z"};
    if (cp >= 0xC0 && cp <= 0xFF) return kLatin1[cp - 0xC0];
    if (cp >= 0x100 && cp <= 0x17F) {
        const char c = kExtA[cp - 0x100];
        if (c == '1') return "ij";
        if (c == '2') return "oe";
        return kOne[c - 'a'];
    }
    return nullptr;
}

// Cyrillic capitals to their small letters (the basic block and the paired letters of the
// extended one); every other code point unchanged.
inline unsigned utSearchCyrLower(unsigned cp) {
    if (cp >= 0x0410 && cp <= 0x042F) return cp + 0x20;
    if (cp >= 0x0400 && cp <= 0x040F) return cp + 0x50;
    if (((cp >= 0x0460 && cp <= 0x0481) || (cp >= 0x048A && cp <= 0x04BF) ||
         (cp >= 0x04D0 && cp <= 0x04FF)) &&
        (cp & 1u) == 0)
        return cp + 1;
    if (cp >= 0x04C1 && cp <= 0x04CE && (cp & 1u) == 1) return cp + 1;
    if (cp == 0x04C0) return 0x04CF;
    return cp;
}

inline void utSearchPutUtf8(std::string* out, unsigned cp) {
    if (cp < 0x80) {
        out->push_back((char)cp);
    } else if (cp < 0x800) {
        out->push_back((char)(0xC0 | (cp >> 6)));
        out->push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out->push_back((char)(0xE0 | (cp >> 12)));
        out->push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out->push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out->push_back((char)(0xF0 | (cp >> 18)));
        out->push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out->push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out->push_back((char)(0x80 | (cp & 0x3F)));
    }
}

// One code point, folded onto `out`.
inline void utSearchFoldCp(std::string* out, unsigned cp) {
    if (cp < 0x20 || cp == 0x7F) {
        out->push_back(' ');
        return;
    }
    if (cp < 0x80) {
        out->push_back((char)(cp >= 'A' && cp <= 'Z' ? cp + 0x20 : cp));
        return;
    }
    if (cp == 0xA0) {
        out->push_back(' ');
        return;
    }
    if (cp == 0x2018 || cp == 0x2019) {
        out->push_back('\'');
        return;
    }
    if (const char* b = utSearchLatinBase(cp)) {
        out->append(b);
        return;
    }
    utSearchPutUtf8(out, utSearchCyrLower(cp));
}

inline bool utSearchAsciiAlpha(unsigned c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// `n` UTF-16 units (the engine's wstring text), folded and appended to `out`. A surrogate pair is
// decoded; a lone surrogate becomes U+FFFD.
inline void utSearchFoldWide(const unsigned short* s, size_t n, std::string* out) {
    for (size_t i = 0; i < n; ++i) {
        const unsigned c = s[i];
        if (c == '{' && i + 3 < n && s[i + 1] == '^' && s[i + 3] == '}') {
            i += 3;   // {^x}
            continue;
        }
        if (c == '^' && i + 1 < n && utSearchAsciiAlpha(s[i + 1])) {
            i += 1;   // ^x
            continue;
        }
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < n && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
            utSearchFoldCp(out, 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00));
            ++i;
            continue;
        }
        utSearchFoldCp(out, (c >= 0xD800 && c <= 0xDFFF) ? 0xFFFD : c);
    }
}

// UTF-8 text (the ini's query) to UTF-16 units. A byte that does not start a well-formed sequence
// is taken as its Latin-1 code point, so a file saved in a Western code page still folds.
inline void utSearchUtf8ToWide(const char* s, std::vector<unsigned short>* out) {
    const unsigned char* p = (const unsigned char*)s;
    while (p && *p) {
        unsigned cp = *p;
        int more = cp >= 0xF0 && cp <= 0xF4 ? 3 : cp >= 0xE0 && cp <= 0xEF ? 2
                                                : cp >= 0xC2 && cp <= 0xDF ? 1 : 0;
        if (cp < 0x80 || more == 0) {
            out->push_back((unsigned short)cp);
            ++p;
            continue;
        }
        unsigned v = cp & (more == 3 ? 0x07u : more == 2 ? 0x0Fu : 0x1Fu);
        int k = 1;
        for (; k <= more; ++k) {
            if ((p[k] & 0xC0) != 0x80) break;
            v = (v << 6) | (p[k] & 0x3Fu);
        }
        if (k <= more) {   // cut short: the lead byte alone, as Latin-1
            out->push_back((unsigned short)cp);
            ++p;
            continue;
        }
        if (v >= 0x10000) {
            v -= 0x10000;
            out->push_back((unsigned short)(0xD800 + (v >> 10)));
            out->push_back((unsigned short)(0xDC00 + (v & 0x3FF)));
        } else {
            out->push_back((unsigned short)v);
        }
        p += more + 1;
    }
}

// The query: folded like a line, then trimmed of spaces at both ends. Empty = no query.
inline std::string utSearchNeedleWide(const unsigned short* s, size_t n) {
    std::string f;
    utSearchFoldWide(s, n, &f);
    size_t a = 0, b = f.size();
    while (a < b && f[a] == ' ') ++a;
    while (b > a && f[b - 1] == ' ') --b;
    return f.substr(a, b - a);
}

inline std::string utSearchNeedleUtf8(const char* s) {
    std::vector<unsigned short> w;
    utSearchUtf8ToWide(s, &w);
    return utSearchNeedleWide(w.empty() ? nullptr : w.data(), w.size());
}

// A record's folded text (its lines joined by '\n') against a folded, trimmed needle.
inline bool utSearchHit(const std::string& text, const std::string& needle) {
    return needle.empty() || text.find(needle) != std::string::npos;
}

}  // namespace ut
