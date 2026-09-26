// arc_reader.cpp - see arc_reader.h.
#include "gen/arc_reader.h"
#include "gen/arz_reader.h"
#include "gen/inflate.h"

#include <cstdio>
#include <cstring>

namespace gen {

namespace {

std::uint32_t rd32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16)
         | (std::uint32_t(p[3]) << 24);
}
std::uint64_t rd64(const std::uint8_t* p) {
    return std::uint64_t(rd32(p)) | (std::uint64_t(rd32(p + 4)) << 32);
}

const std::uint32_t kArcMagic = 0x00435241;   // "ARC\0"

// True when the whole buffer is well-formed UTF-8 (the same test Python's decode makes).
bool validUtf8(const std::uint8_t* p, std::size_t n) {
    std::size_t i = 0;
    while (i < n) {
        std::uint8_t c = p[i];
        if (c < 0x80) { ++i; continue; }
        std::size_t len;
        std::uint32_t cp;
        if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; if (c < 0xC2) return false; }
        else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; if (c > 0xF4) return false; }
        else return false;
        if (i + len > n) return false;
        for (std::size_t k = 1; k < len; ++k) {
            if ((p[i + k] & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (p[i + k] & 0x3F);
        }
        if ((len == 3 && cp < 0x800) || (len == 4 && (cp < 0x10000 || cp > 0x10FFFF))
            || (cp >= 0xD800 && cp <= 0xDFFF))
            return false;
        i += len;
    }
    return true;
}

bool isSpaceByte(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'
        || (c >= 0x1C && c <= 0x1F);
}

// Python's str.strip() on the ASCII whitespace plus NBSP / NEL in their UTF-8 spelling.
std::string stripUtf8(const std::string& s) {
    std::size_t a = 0, b = s.size();
    for (;;) {
        if (a < b && isSpaceByte(std::uint8_t(s[a]))) { ++a; continue; }
        if (a + 2 <= b && std::uint8_t(s[a]) == 0xC2
            && (std::uint8_t(s[a + 1]) == 0xA0 || std::uint8_t(s[a + 1]) == 0x85)) { a += 2; continue; }
        break;
    }
    for (;;) {
        if (b > a && isSpaceByte(std::uint8_t(s[b - 1]))) { --b; continue; }
        if (b >= a + 2 && std::uint8_t(s[b - 2]) == 0xC2
            && (std::uint8_t(s[b - 1]) == 0xA0 || std::uint8_t(s[b - 1]) == 0x85)) { b -= 2; continue; }
        break;
    }
    return s.substr(a, b - a);
}

void putUtf8(std::string& s, std::uint32_t cp) {
    if (cp < 0x80) {
        s.push_back(char(cp));
    } else if (cp < 0x800) {
        s.push_back(char(0xC0 | (cp >> 6)));
        s.push_back(char(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        s.push_back(char(0xE0 | (cp >> 12)));
        s.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
        s.push_back(char(0x80 | (cp & 0x3F)));
    } else {
        s.push_back(char(0xF0 | (cp >> 18)));
        s.push_back(char(0x80 | ((cp >> 12) & 0x3F)));
        s.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
        s.push_back(char(0x80 | (cp & 0x3F)));
    }
}

// Python's bytes.decode("utf-16-le", "replace"): a lone surrogate unit is one U+FFFD, a high
// surrogate with nothing after it (and any odd byte left over) is one U+FFFD, an odd last byte
// is one U+FFFD.
std::string utf16leToUtf8(const std::uint8_t* p, std::size_t n) {
    std::string s;
    s.reserve(n);
    std::size_t i = 0;
    while (i + 1 < n) {
        const std::uint32_t u = std::uint32_t(p[i]) | (std::uint32_t(p[i + 1]) << 8);
        if (u < 0xD800 || u > 0xDFFF) { putUtf8(s, u); i += 2; continue; }
        if (u >= 0xDC00) { putUtf8(s, 0xFFFD); i += 2; continue; }       // a lone low surrogate
        if (i + 3 >= n) { putUtf8(s, 0xFFFD); return s; }                // a high one at the end
        const std::uint32_t v = std::uint32_t(p[i + 2]) | (std::uint32_t(p[i + 3]) << 8);
        if (v >= 0xDC00 && v <= 0xDFFF) {
            putUtf8(s, 0x10000 + ((u - 0xD800) << 10) + (v - 0xDC00));
            i += 4;
        } else {
            putUtf8(s, 0xFFFD);                                          // unpaired high
            i += 2;
        }
    }
    if (i < n) putUtf8(s, 0xFFFD);                                       // an odd last byte
    return s;
}

// Length of the line separator at p (Python's str.splitlines set), 0 when there is none.
std::size_t lineBreakLen(const std::uint8_t* p, std::size_t n) {
    std::uint8_t c = p[0];
    if (c == '\r') return (n > 1 && p[1] == '\n') ? 2 : 1;
    if (c == '\n' || c == '\v' || c == '\f' || c == 0x1C || c == 0x1D || c == 0x1E) return 1;
    if (c == 0xC2 && n > 1 && p[1] == 0x85) return 2;                                 // NEL
    if (c == 0xE2 && n > 2 && p[1] == 0x80 && (p[2] == 0xA8 || p[2] == 0xA9)) return 3; // LS, PS
    return 0;
}

} // namespace

// ---------------------------------------------------------------- ArcArchive
std::string ArcArchive::normKey(const std::string& name) { return ArzArchive::normKey(name); }

bool ArcArchive::load(const std::string& path, std::string* error) {
    m_path = path;
    if (m_tag.empty()) {
        std::size_t s = path.find_last_of("/\\");
        std::string base = s == std::string::npos ? path : path.substr(s + 1);
        std::size_t d = base.find_last_of('.');
        m_tag = d == std::string::npos ? base : base.substr(0, d);
    }
    if (!m_file.open(path)) { if (error) *error = "cannot read " + path; return false; }
    m_fileSize = m_file.size();
    const std::uint64_t n = m_fileSize;
    std::uint8_t hdr[28];
    if (n < 28 || !m_file.readAt(0, 28, hdr)) { if (error) *error = path + ": too short"; return false; }
    std::uint32_t magic = rd32(hdr), version = rd32(hdr + 4), nFiles = rd32(hdr + 8), nRecs = rd32(hdr + 12);
    std::uint32_t recSize = rd32(hdr + 16), strSize = rd32(hdr + 20), recOff = rd32(hdr + 24);
    if (magic != kArcMagic || version != 1) { if (error) *error = path + ": not an ARC v1"; return false; }
    std::uint64_t strOff = std::uint64_t(recOff) + recSize;
    std::uint64_t entOff = strOff + strSize;
    if (std::uint64_t(nRecs) * 12 > recSize || entOff + std::uint64_t(nFiles) * 44 > n) {
        if (error) *error = path + ": tables outside the file";
        return false;
    }
    // the three tables, contiguous from recOff, are the index and stay; the entry bodies
    // between the header and recOff - all but a few MB of the archive - never come in
    const std::size_t tblLen = std::size_t(recSize) + strSize + std::size_t(nFiles) * 44;
    std::vector<std::uint8_t> tbl(tblLen);
    if (!m_file.readAt(recOff, tblLen, tbl.data())) {
        if (error) *error = path + ": cannot read the tables";
        return false;
    }
    const std::uint8_t* const b = tbl.data();              // b[0] is the byte at recOff
    const std::size_t strAt = recSize, entAt = std::size_t(recSize) + strSize;
    m_parts.reserve(nRecs);
    for (std::uint32_t i = 0; i < nRecs; ++i) {
        const std::uint8_t* p = b + 12 * i;
        Part pt{rd32(p), rd32(p + 4), rd32(p + 8)};
        if (std::uint64_t(pt.offset) + pt.csize > n) { if (error) *error = path + ": part outside the file"; return false; }
        m_parts.push_back(pt);
    }
    m_entries.reserve(nFiles);
    for (std::uint32_t i = 0; i < nFiles; ++i) {
        const std::uint8_t* p = b + entAt + 44 * i;
        ArcEntry e;
        e.etype = rd32(p);
        e.offset = rd32(p + 4);
        e.csize = rd32(p + 8);
        e.dsize = rd32(p + 12);
        e.ftime = rd64(p + 20);
        e.numParts = rd32(p + 28);
        e.firstPart = rd32(p + 32);
        std::uint32_t nlen = rd32(p + 36), noff = rd32(p + 40);
        if (std::uint64_t(noff) + nlen > strSize) { if (error) *error = path + ": entry name outside the string table"; return false; }
        if (e.numParts == 0) {
            if (std::uint64_t(e.offset) + e.dsize > n) { if (error) *error = path + ": entry data outside the file"; return false; }
        } else if (std::uint64_t(e.firstPart) + e.numParts > m_parts.size()) {
            if (error) *error = path + ": entry parts outside the part table";
            return false;
        }
        e.name = latin1ToUtf8(b + strAt + noff, nlen);
        for (char& c : e.name) if (c == '\\') c = '/';
        e.key = normKey(e.name);
        m_index[e.key] = m_entries.size();
        m_entries.push_back(std::move(e));
    }
    return true;
}

const ArcEntry* ArcArchive::entry(const std::string& name) const {
    auto it = m_index.find(normKey(name));
    return it == m_index.end() ? nullptr : &m_entries[it->second];
}

bool ArcArchive::read(const ArcEntry& e, std::vector<std::uint8_t>& out, std::size_t limit) const {
    out.clear();
    if (e.numParts == 0) {
        if (std::uint64_t(e.offset) + e.dsize > m_fileSize) return false;
        std::size_t want = (limit && limit < e.dsize) ? limit : e.dsize;
        out.resize(want);
        return m_file.readAt(e.offset, want, out.data());
    }
    if (std::uint64_t(e.firstPart) + e.numParts > m_parts.size()) return false;
    for (std::uint32_t i = e.firstPart; i < e.firstPart + e.numParts; ++i) {
        if (limit && out.size() >= limit) break;
        const Part& pt = m_parts[i];
        if (std::uint64_t(pt.offset) + pt.csize > m_fileSize) return false;
        std::size_t at = out.size();
        // the entry's own dsize bounds the whole output, not only each part (a malformed entry fails closed)
        if (std::uint64_t(at) + pt.dsize > e.dsize) return false;
        if (pt.csize == pt.dsize) {
            out.resize(at + pt.csize);
            if (!m_file.readAt(pt.offset, pt.csize, out.data() + at)) return false;
        } else {
            // a zlib stream that must inflate to exactly the part's dsize: capped at it, and a
            // short one is refused as surely as a corrupt one - never guessed at or padded
            if (pt.dsize > kMaxPartBytes) return false;
            m_scratch.resize(pt.csize);
            if (!m_file.readAt(pt.offset, pt.csize, m_scratch.data())) return false;
            if (!zlibInflate(m_scratch.data(), pt.csize, out, pt.dsize)) return false;
            if (out.size() != at + pt.dsize) return false;
        }
    }
    // a whole read comes to exactly the entry's dsize (a capped read stops early by design)
    return limit != 0 || out.size() == e.dsize;
}

// ---------------------------------------------------------------- ResourceArcs
namespace {

bool isXPackFolder(const std::string& lowerPart) {
    if (lowerPart.compare(0, 5, "xpack") != 0) return false;
    for (std::size_t i = 5; i < lowerPart.size(); ++i)
        if (lowerPart[i] < '0' || lowerPart[i] > '9') return false;
    return true;
}

bool fileThere(const std::string& p) {
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

} // namespace

const ArcArchive* ResourceArcs::archiveFor(const std::string& resPath, std::string* inner,
                                           std::string* error) const {
    const std::string key = ArcArchive::normKey(resPath);     // '/' separators, lower case
    std::size_t s1 = key.find('/');
    if (s1 == std::string::npos || m_root.empty()) return nullptr;
    std::string folder, arcName;
    std::size_t restAt;
    if (isXPackFolder(key.substr(0, s1))) {
        const std::size_t s2 = key.find('/', s1 + 1);
        if (s2 == std::string::npos) return nullptr;
        folder = resPath.substr(0, s1);
        arcName = resPath.substr(s1 + 1, s2 - s1 - 1);
        restAt = s2 + 1;
    } else {
        arcName = resPath.substr(0, s1);
        restAt = s1 + 1;
    }
    if (inner) *inner = key.substr(restAt);
    const std::string dir = m_root + (folder.empty() ? std::string() : "\\" + folder);
    std::vector<std::string> cands{arcName + ".arc"};
    if (ArcArchive::normKey(arcName) == "items") cands.push_back("Item.arc");
    for (const std::string& c : cands) {
        const std::string rel = folder.empty() ? c : folder + "\\" + c;
        const std::string ck = ArcArchive::normKey(rel);
        auto it = m_arcs.find(ck);
        if (it == m_arcs.end()) {
            std::unique_ptr<ArcArchive> a;
            const std::string full = dir + "\\" + c;
            if (fileThere(full)) {
                a.reset(new ArcArchive);
                a->setTag(ck);
                std::string err;
                if (!a->load(full, &err)) {
                    if (error) *error = err;
                    return nullptr;
                }
            }
            it = m_arcs.emplace(ck, std::move(a)).first;
        }
        if (it->second) return it->second.get();
    }
    return nullptr;
}

bool ResourceArcs::read(const std::string& resPath, std::vector<std::uint8_t>& out,
                        std::size_t limit, std::string* error) const {
    out.clear();
    std::string inner;
    const ArcArchive* a = archiveFor(resPath, &inner, error);
    if (!a) return false;
    const ArcEntry* e = a->entry(inner);
    return e && a->read(*e, out, limit);
}

std::string ResourceArcs::sourceOf(const std::string& resPath) const {
    std::string inner;
    const ArcArchive* a = archiveFor(resPath, &inner, nullptr);
    return (a && a->entry(inner)) ? a->tag() : std::string();
}

std::size_t ResourceArcs::opened() const {
    std::size_t n = 0;
    for (const auto& kv : m_arcs) if (kv.second) ++n;
    return n;
}

// ---------------------------------------------------------------- tag files
namespace {

// TQ: a single-byte tag file is Windows-1252 (x2sidequest.txt holds 0x85 and 0x92), not latin-1:
// 0x80..0x9F take their cp1252 characters, and the five bytes cp1252 leaves undefined keep their
// own code point, as latin-1 does. tools/tqarc.py decode_cp1252 is the twin.
const std::uint16_t kCp1252High[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
};

std::string cp1252ToUtf8(const std::uint8_t* p, std::size_t n) {
    std::string s;
    s.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        std::uint32_t c = p[i];
        if (c >= 0x80 && c < 0xA0) c = kCp1252High[c - 0x80];
        if (c < 0x80) {
            s.push_back(char(c));
        } else if (c < 0x800) {
            s.push_back(char(0xC0 | (c >> 6)));
            s.push_back(char(0x80 | (c & 0x3F)));
        } else {
            s.push_back(char(0xE0 | (c >> 12)));
            s.push_back(char(0x80 | ((c >> 6) & 0x3F)));
            s.push_back(char(0x80 | (c & 0x3F)));
        }
    }
    return s;
}

} // namespace

void parseTagFile(const std::vector<std::uint8_t>& data,
                  std::unordered_map<std::string, std::string>& tags) {
    std::vector<std::uint8_t> conv;
    const std::uint8_t* p = data.data();
    std::size_t n = data.size();
    bool utf16 = false;
    if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE) {
        // TQ: UTF-16LE with its BOM -> UTF-8, then the same line rules as every other file
        std::string s = utf16leToUtf8(p + 2, n - 2);
        conv.assign(s.begin(), s.end());
        p = conv.data();
        n = conv.size();
        utf16 = true;
    } else if (n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) { p += 3; n -= 3; }
    if (!utf16 && !validUtf8(p, n)) {
        // Python's fallback reads the WHOLE file (BOM included) as single-byte text - TQ: cp1252
        std::string s = cp1252ToUtf8(data.data(), data.size());
        conv.assign(s.begin(), s.end());
        p = conv.data();
        n = conv.size();
    }
    std::size_t i = 0;
    while (i < n) {
        std::size_t j = i, brk = 0;
        while (j < n && (brk = lineBreakLen(p + j, n - j)) == 0) ++j;
        std::string line(reinterpret_cast<const char*>(p + i), j - i);
        i = j + brk;
        if (line.empty() || line.compare(0, 2, "//") == 0 || line[0] == '#') continue;
        std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = stripUtf8(line.substr(0, eq));
        if (k.empty() || k[0] == '<') continue;
        tags[k] = line.substr(eq + 1);
    }
}

bool loadTextTags(const std::string& gameDir, const std::string& lang,
                  std::unordered_map<std::string, std::string>& tags, std::string* error) {
    ArcArchive a;
    a.setTag("text");
    if (!a.load(gameDir + "\\Text\\Text_" + lang + ".arc", error)) return false;
    std::vector<std::uint8_t> data;
    for (const ArcEntry& e : a.entries()) {
        if (e.key.size() < 4 || e.key.compare(e.key.size() - 4, 4, ".txt") != 0) continue;
        if (!a.read(e, data)) {
            if (error) *error = a.path() + ": cannot read " + e.name;
            return false;
        }
        if (!data.empty()) parseTagFile(data, tags);
    }
    return true;
}

} // namespace gen
