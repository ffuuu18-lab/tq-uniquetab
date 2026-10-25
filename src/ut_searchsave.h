// ut_searchsave.h - the search index's text kept per game (uniq-search.txt beside the game's
// uniq-groups.txt): its key, the file's form, the write and the read. No hook, no log, no engine
// call: test_search drives it offline; ut_search.cpp holds the one live use.
//
// The file, UTF-8, one row per line:
//   uniq-search 1
//   key stamp=<16 hex> lang=<text_language> db=0x<8 hex> lore=<0|1> version=<mod version>
//   R <record path>\t<the record's folded text>      (an indexed record)
//   U <record path>                                    (an unindexable record)
// The folded text joins its lines with '\n'; in the file '\\', '\n', '\r' and '\t' are written as
// the two characters \\, \n, \r and \t. The key's five parts are the inputs of the text: the
// game's catalogue.stamp (its record list and the generator's settings), the language setting,
// the loaded database's checksum, search_lore (which keeps or drops the lore lines) and the mod's
// version (the fold's rules). A file whose key differs in any part is not used, and the reason
// names the part.
#pragma once

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

namespace ut {

const char* const kUtSearchSaveMagic = "uniq-search 1";
const char* const kUtSearchSaveFile = "uniq-search.txt";

struct UtSearchKey {
    unsigned long long stamp = 0;   // FNV-1a 64 of the game's catalogue.stamp bytes
    std::string lang;               // text_language
    unsigned db = 0;                // Engine::GetDatabaseArchiveChecksum
    int lore = 0;                   // search_lore
    std::string version;            // the mod's version
};

struct UtSearchSaved {
    std::string record;
    bool indexed = false;   // false: unindexable
    std::string text;       // the folded text (indexed only)
};

inline unsigned long long utSearchFnv(const char* p, size_t n,
                                      unsigned long long h = 0xcbf29ce484222325ull) {
    for (size_t i = 0; i < n; ++i) {
        h ^= (unsigned char)p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

inline std::string utSearchKeyLine(const UtSearchKey& k) {
    char buf[160];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "key stamp=%016llx lang=%s db=0x%08X lore=%d version=%s",
                k.stamp, k.lang.c_str(), k.db, k.lore ? 1 : 0, k.version.c_str());
    return buf;
}

inline void utSearchEscape(const std::string& s, std::string* out) {
    for (char c : s) {
        switch (c) {
            case '\\': *out += "\\\\"; break;
            case '\n': *out += "\\n"; break;
            case '\r': *out += "\\r"; break;
            case '\t': *out += "\\t"; break;
            default: out->push_back(c); break;
        }
    }
}

// False = a backslash followed by anything else, or a lone one at the end (a damaged file).
inline bool utSearchUnescape(const char* s, size_t n, std::string* out) {
    out->clear();
    out->reserve(n);
    for (size_t i = 0; i < n; ++i) {
        if (s[i] != '\\') {
            out->push_back(s[i]);
            continue;
        }
        if (++i >= n) return false;
        switch (s[i]) {
            case '\\': out->push_back('\\'); break;
            case 'n': out->push_back('\n'); break;
            case 'r': out->push_back('\r'); break;
            case 't': out->push_back('\t'); break;
            default: return false;
        }
    }
    return true;
}

// The file's first two lines.
inline std::string utSearchSaveHead(const UtSearchKey& k) {
    return std::string(kUtSearchSaveMagic) + "\n" + utSearchKeyLine(k) + "\n";
}

// One row onto `out` (`text` is read only when `indexed`). A record path carries no tab or
// newline; one that does is not written (false).
inline bool utSearchSaveRow(std::string* out, const char* record, bool indexed, const std::string& text) {
    if (!record || !record[0] || strpbrk(record, "\t\r\n")) return false;
    *out += indexed ? "R " : "U ";
    *out += record;
    if (indexed) {
        *out += '\t';
        utSearchEscape(text, out);
    }
    *out += '\n';
    return true;
}

// The whole file's bytes.
inline std::string utSearchSaveText(const UtSearchKey& k, const std::vector<UtSearchSaved>& rows) {
    std::string out = utSearchSaveHead(k);
    for (const UtSearchSaved& r : rows) utSearchSaveRow(&out, r.record.c_str(), r.indexed, r.text);
    return out;
}

// Which part of the key differs ("" = none): the first that does, in the key's order.
inline std::string utSearchKeyDiff(const std::string& fileKey, const UtSearchKey& now) {
    const char* const parts[5] = {"stamp=", "lang=", "db=", "lore=", "version="};
    const char* const says[5] = {"the game's catalogue.stamp changed", "text_language changed",
                                 "the database checksum changed", "search_lore changed",
                                 "the mod's version changed"};
    const std::string want = utSearchKeyLine(now);
    if (fileKey == want) return std::string();
    for (int i = 0; i < 5; ++i) {
        const size_t a = fileKey.find(parts[i]);
        const size_t b = want.find(parts[i]);
        if (a == std::string::npos) return "its key line is not one this version writes";
        const size_t ae = fileKey.find(' ', a), be = want.find(' ', b);
        if (fileKey.compare(a, ae == std::string::npos ? std::string::npos : ae - a, want, b,
                            be == std::string::npos ? std::string::npos : be - b) != 0)
            return says[i];
    }
    return "its key line is not one this version writes";
}

// Reads the file's bytes against the key in force. False: `why` says the reason (another key, a
// damaged row); `rows` holds the file's rows sorted by record (utSearchFind).
inline bool utSearchLoadText(const std::string& bytes, const UtSearchKey& now,
                             std::vector<UtSearchSaved>* rows, std::string* why) {
    rows->clear();
    why->clear();
    size_t at = 0;
    auto nextLine = [&](size_t* s, size_t* e) {
        if (at >= bytes.size()) return false;
        *s = at;
        size_t nl = bytes.find('\n', at);
        if (nl == std::string::npos) nl = bytes.size();
        *e = nl;
        at = nl + 1;
        return true;
    };
    size_t s = 0, e = 0;
    if (!nextLine(&s, &e) || bytes.compare(s, e - s, kUtSearchSaveMagic) != 0) {
        *why = "its first line is not \"uniq-search 1\"";
        return false;
    }
    if (!nextLine(&s, &e)) {
        *why = "it has no key line";
        return false;
    }
    const std::string diff = utSearchKeyDiff(bytes.substr(s, e - s), now);
    if (!diff.empty()) {
        *why = diff;
        return false;
    }
    int line = 2;
    while (nextLine(&s, &e)) {
        ++line;
        if (e == s) continue;
        UtSearchSaved r;
        const char kind = bytes[s];
        if ((kind != 'R' && kind != 'U') || e - s < 3 || bytes[s + 1] != ' ') {
            *why = "line " + std::to_string(line) + " is not a row";
            rows->clear();
            return false;
        }
        const size_t tab = bytes.find('\t', s + 2);
        const size_t recEnd = (tab == std::string::npos || tab > e) ? e : tab;
        r.record.assign(bytes, s + 2, recEnd - (s + 2));
        r.indexed = kind == 'R';
        if (r.record.empty() || (r.indexed && recEnd == e) || (!r.indexed && recEnd != e) ||
            (r.indexed && !utSearchUnescape(bytes.data() + recEnd + 1, e - recEnd - 1, &r.text))) {
            *why = "line " + std::to_string(line) + " is damaged";
            rows->clear();
            return false;
        }
        rows->push_back(std::move(r));
    }
    std::sort(rows->begin(), rows->end(),
              [](const UtSearchSaved& a, const UtSearchSaved& b) { return a.record < b.record; });
    return true;
}

// The row of `record` in rows sorted by utSearchLoadText, or null.
inline const UtSearchSaved* utSearchFind(const std::vector<UtSearchSaved>& rows, const char* record) {
    if (!record) return nullptr;
    size_t lo = 0, hi = rows.size();
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (strcmp(rows[mid].record.c_str(), record) < 0) lo = mid + 1;
        else hi = mid;
    }
    return lo < rows.size() && rows[lo].record == record ? &rows[lo] : nullptr;
}
inline UtSearchSaved* utSearchFind(std::vector<UtSearchSaved>& rows, const char* record) {
    return const_cast<UtSearchSaved*>(utSearchFind(static_cast<const std::vector<UtSearchSaved>&>(rows), record));
}

// The text of index slot `slot` from its saved row `row` (a row of `rows`, an indexed one): the
// first slot that takes a row moves its text out, so the file's text is not held twice; a record
// the group table lists twice finds the same row again (utSearchFind gives the first of equal
// records) and gets a copy of what the first slot took. `takenBy`: one entry per row, -1 until a
// slot takes it.
inline void utSearchTakeText(std::vector<UtSearchSaved>& rows, std::vector<int>& takenBy,
                             const UtSearchSaved* row, std::vector<std::string>& slots, int slot) {
    const size_t i = (size_t)(row - rows.data());
    if (takenBy[i] < 0) {
        slots[(size_t)slot].swap(rows[i].text);
        takenBy[i] = slot;
    } else {
        slots[(size_t)slot] = slots[(size_t)takenBy[i]];
    }
}

}  // namespace ut
