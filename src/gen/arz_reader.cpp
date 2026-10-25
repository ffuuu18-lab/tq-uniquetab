// arz_reader.cpp - see arz_reader.h.
#include "gen/arz_reader.h"
#include "gen/inflate.h"

#include <cstdio>
#include <cstring>

namespace gen {

namespace {

std::uint16_t rd16(const std::uint8_t* p) { return std::uint16_t(p[0] | (p[1] << 8)); }
std::uint32_t rd32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16)
         | (std::uint32_t(p[3]) << 24);
}
std::uint64_t rd64(const std::uint8_t* p) {
    return std::uint64_t(rd32(p)) | (std::uint64_t(rd32(p + 4)) << 32);
}

} // namespace

// ---------------------------------------------------------------- FileHandle
bool FileHandle::open(const std::string& path) {
    close();
    m_f = std::fopen(path.c_str(), "rb");
    return m_f != nullptr;
}

void FileHandle::close() {
    if (m_f) { std::fclose(m_f); m_f = nullptr; }
}

std::uint64_t FileHandle::size() const {
    if (!m_f) return 0;
    if (_fseeki64(m_f, 0, SEEK_END) != 0) return 0;
    long long n = _ftelli64(m_f);
    return n < 0 ? 0 : std::uint64_t(n);
}

bool FileHandle::readAt(std::uint64_t off, std::size_t n, std::uint8_t* out) const {
    if (!m_f) return false;
    if (n == 0) return true;
    if (_fseeki64(m_f, (long long)off, SEEK_SET) != 0) return false;
    return std::fread(out, 1, n, m_f) == n;
}

std::string latin1ToUtf8(const std::uint8_t* p, std::size_t n) {
    std::string s;
    s.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        std::uint8_t c = p[i];
        if (c < 0x80) {
            s.push_back(char(c));
        } else {
            s.push_back(char(0xC0 | (c >> 6)));
            s.push_back(char(0x80 | (c & 0x3F)));
        }
    }
    return s;
}

// ---------------------------------------------------------------- ArzRecord
const ArzField* ArzRecord::find(const char* name) const {
    for (std::size_t i = fields.size(); i-- > 0;)
        if (fields[i].name == name) return &fields[i];
    return nullptr;
}

std::string ArzRecord::str(const char* name) const {
    const ArzField* f = find(name);
    if (!f || f->raw.empty()) return std::string();
    if (f->type == kFtString) return f->strs.empty() ? std::string() : f->strs[0];
    if (f->type == kFtFloat) {
        float v;
        std::memcpy(&v, &f->raw[0], 4);
        char buf[64];
        std::snprintf(buf, sizeof buf, "%g", double(v));
        return buf;
    }
    return std::to_string(std::int32_t(f->raw[0]));
}

std::int32_t ArzRecord::i32(const char* name) const {
    const ArzField* f = find(name);
    if (!f || f->raw.empty()) return 0;
    if (f->type == kFtString) {
        if (f->strs.empty()) return 0;
        char* end = nullptr;
        long v = std::strtol(f->strs[0].c_str(), &end, 10);
        return (end && *end == 0 && !f->strs[0].empty()) ? std::int32_t(v) : 0;
    }
    if (f->type == kFtFloat) {
        float v;
        std::memcpy(&v, &f->raw[0], 4);
        return std::int32_t(v);
    }
    return std::int32_t(f->raw[0]);
}

// ---------------------------------------------------------------- ArzArchive
namespace {

const std::uint32_t kEmpty = 0xFFFFFFFFu;   // a free slot of an index table
const std::uint32_t kFnvBasis = 2166136261u, kFnvPrime = 16777619u;

std::uint32_t fnvStep(std::uint32_t h, char c) { return (h ^ std::uint8_t(c)) * kFnvPrime; }

// The slots of an index table for n keys: a power of two, at least twice n (half full at most).
std::size_t slotCountFor(std::size_t n) {
    std::size_t c = 16;
    while (c < 2 * n) c <<= 1;
    return c;
}

// Hands f each byte of the UTF-8 form of the latin-1 bytes p[0..n), '\\' given as '/' and, with
// lower, A-Z lower-cased: the bytes of a record's path (lower false) or of its key (lower true).
// f returns false to stop.
template <class F>
void forPathBytes(const char* p, std::size_t n, bool lower, F f) {
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint8_t c = std::uint8_t(p[i]);
        if (c < 0x80) {
            char o = char(c);
            if (o == '\\') o = '/';
            else if (lower && o >= 'A' && o <= 'Z') o = char(o - 'A' + 'a');
            if (!f(o)) return;
        } else {
            if (!f(char(0xC0 | (c >> 6)))) return;
            if (!f(char(0x80 | (c & 0x3F)))) return;
        }
    }
}

} // namespace

std::string ArzArchive::normKey(const std::string& name) {
    std::string k = name;
    for (char& c : k) {
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    }
    return k;
}

std::uint32_t ArzArchive::keyHash(const std::string& key) {
    std::uint32_t h = kFnvBasis;
    for (char c : key) h = fnvStep(h, c);
    return h;
}

const char* ArzArchive::raw(std::size_t i, std::size_t* len) const {
    if (i >= m_strOff.size()) { *len = 0; return nullptr; }
    const std::uint32_t at = m_strOff[i];
    *len = rd32((const std::uint8_t*)m_table.data() + at - 4);
    return m_table.data() + at;
}

std::string ArzArchive::str(std::size_t i) const {
    std::size_t n = 0;
    const char* p = raw(i, &n);
    return p ? latin1ToUtf8((const std::uint8_t*)p, n) : std::string();
}

std::string ArzArchive::name(const ArzEntry& e) const {
    std::size_t n = 0;
    const char* p = raw(e.nameIdx, &n);
    std::string out;
    out.reserve(n);
    forPathBytes(p, n, false, [&](char c) { out.push_back(c); return true; });
    return out;
}

void ArzArchive::key(const ArzEntry& e, std::string& out) const {
    std::size_t n = 0;
    const char* p = raw(e.nameIdx, &n);
    out.clear();
    forPathBytes(p, n, true, [&](char c) { out.push_back(c); return true; });
}

std::string ArzArchive::key(const ArzEntry& e) const {
    std::string out;
    key(e, out);
    return out;
}

bool ArzArchive::keyIs(const ArzEntry& e, const std::string& k) const {
    std::size_t n = 0;
    const char* p = raw(e.nameIdx, &n);
    std::size_t at = 0;
    bool same = true;
    forPathBytes(p, n, true, [&](char c) {
        if (at >= k.size() || k[at] != c) { same = false; return false; }
        ++at;
        return true;
    });
    return same && at == k.size();
}

std::size_t ArzArchive::indexBytes() const {
    return m_table.capacity() + m_strOff.capacity() * sizeof(std::uint32_t) +
           m_entries.capacity() * sizeof(ArzEntry) + m_slots.capacity() * sizeof(std::uint32_t);
}

bool ArzArchive::load(const std::string& path, std::string* error) {
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
    std::uint8_t hdr[24];
    if (n < 24 || !m_file.readAt(0, 24, hdr)) { if (error) *error = path + ": too short"; return false; }
    std::uint16_t version = rd16(hdr + 2);
    std::uint32_t recStart = rd32(hdr + 4), recSize = rd32(hdr + 8), recCount = rd32(hdr + 12);
    std::uint32_t strStart = rd32(hdr + 16), strSize = rd32(hdr + 20);
    if (version != 3) { if (error) *error = path + ": ARZ version is not 3"; return false; }
    if (std::uint64_t(recStart) + recSize > n || std::uint64_t(strStart) + strSize > n
        || strSize < 4) {
        if (error) *error = path + ": table offsets outside the file";
        return false;
    }
    // the string table stays as the file stores it; each string is found by its offset
    {
        m_table.resize(strSize);
        if (!m_file.readAt(strStart, strSize, (std::uint8_t*)m_table.data())) {
            if (error) *error = path + ": cannot read the string table";
            return false;
        }
        const std::uint8_t* b = (const std::uint8_t*)m_table.data();
        std::size_t p = 0, end = strSize;
        std::uint32_t count = rd32(b);
        p += 4;
        m_strOff.reserve(count < (end - p) / 4 ? count : (end - p) / 4);
        for (std::uint32_t i = 0; i < count; ++i) {
            if (p + 4 > end) { if (error) *error = path + ": string table truncated"; return false; }
            std::uint32_t len = rd32(b + p);
            p += 4;
            if (len > end - p) { if (error) *error = path + ": string table truncated"; return false; }
            m_strOff.push_back(std::uint32_t(p));
            p += len;
        }
    }
    // the record table: read whole, kept as one compact entry per record
    {
        std::vector<std::uint8_t> table(recSize);
        if (!m_file.readAt(recStart, recSize, table.data())) {
            if (error) *error = path + ": cannot read the record table";
            return false;
        }
        const std::uint8_t* b = table.data();
        std::size_t p = 0, end = recSize;
        m_entries.reserve(recCount < recSize / 24 ? recCount : recSize / 24);
        for (std::uint32_t i = 0; i < recCount; ++i) {
            if (p + 8 > end) { if (error) *error = path + ": record table truncated"; return false; }
            std::uint32_t nameIdx = rd32(b + p), typeLen = rd32(b + p + 4);
            p += 8;
            if (typeLen > end - p || end - p - typeLen < 16) {
                if (error) *error = path + ": record table truncated";
                return false;
            }
            ArzEntry e;
            p += typeLen;                  // the record's type: not kept
            e.offset = rd32(b + p);
            e.csize = rd32(b + p + 4);
            p += 16;                       // TQ: no dsize between csize and the time
            if (nameIdx >= m_strOff.size()) { if (error) *error = path + ": record name index"; return false; }
            if (std::uint64_t(24) + e.offset + e.csize > n) {
                if (error) *error = path + ": record data outside the file";
                return false;
            }
            e.nameIdx = nameIdx;
            std::size_t len = 0;
            const char* s = raw(nameIdx, &len);
            std::uint32_t h = kFnvBasis;
            forPathBytes(s, len, true, [&](char c) { h = fnvStep(h, c); return true; });
            e.hash = h;
            m_entries.push_back(e);
        }
    }
    // the lookup: of two entries with one key, the later one
    m_slots.assign(slotCountFor(m_entries.size()), kEmpty);
    const std::size_t mask = m_slots.size() - 1;
    std::string k;
    for (std::size_t i = 0; i < m_entries.size(); ++i) {
        const ArzEntry& e = m_entries[i];
        key(e, k);
        for (std::size_t at = e.hash & mask;; at = (at + 1) & mask) {
            const std::uint32_t s = m_slots[at];
            if (s == kEmpty || (m_entries[s].hash == e.hash && keyIs(m_entries[s], k))) {
                m_slots[at] = std::uint32_t(i);
                break;
            }
        }
    }
    return true;
}

const ArzEntry* ArzArchive::entry(const std::string& name) const {
    if (m_slots.empty()) return nullptr;
    const std::string k = normKey(name);
    const std::uint32_t h = keyHash(k);
    const std::size_t mask = m_slots.size() - 1;
    for (std::size_t at = h & mask;; at = (at + 1) & mask) {
        const std::uint32_t s = m_slots[at];
        if (s == kEmpty) return nullptr;
        if (m_entries[s].hash == h && keyIs(m_entries[s], k)) return &m_entries[s];
    }
}

bool ArzArchive::decode(const ArzEntry& e, ArzRecord& out, std::string* why) const {
    out.fields.clear();
    if (std::uint64_t(24) + e.offset + e.csize > m_fileSize) {
        if (why) *why = "the body lies outside the file";
        return false;
    }
    m_scratch.resize(e.csize);
    if (!m_file.readAt(std::uint64_t(24) + e.offset, e.csize, m_scratch.data())) {
        if (why) *why = "a short read";
        return false;
    }
    // No stored size to trust: the body grows as the stream inflates, capped, and a stream
    // that is truncated, malformed or fails its adler32 is refused (inflate.h).
    m_body.clear();
    if (!zlibInflate(m_scratch.data(), e.csize, m_body, kMaxRecordBytes, why)) return false;
    const std::vector<std::uint8_t>& raw = m_body;
    std::size_t p = 0, n = raw.size();
    while (p + 8 <= n) {
        ArzField f;
        f.type = rd16(raw.data() + p);
        std::uint16_t count = rd16(raw.data() + p + 2);
        std::uint32_t nameIdx = rd32(raw.data() + p + 4);
        p += 8;
        std::size_t end = p + std::size_t(count) * 4;
        if (end > n) {
            if (why) *why = "a field runs past the end of the body";
            return false;
        }
        f.name = nameIdx < m_strOff.size() ? str(nameIdx) : "?" + std::to_string(nameIdx);
        f.raw.reserve(count);
        for (std::uint16_t i = 0; i < count; ++i) f.raw.push_back(rd32(raw.data() + p + i * 4));
        if (f.type == kFtString) {
            f.strs.reserve(count);
            for (std::uint32_t idx : f.raw)
                f.strs.push_back(str(idx));
        }
        p = end;
        out.fields.push_back(std::move(f));
    }
    return true;
}

// ---------------------------------------------------------------- ArzDatabase
std::vector<std::pair<std::string, std::string>> ArzDatabase::loadOrder() {
    return {
        {"database", "Database\\database.arz"},
    };
}

bool ArzDatabase::loadGame(const std::string& gameDir, std::string* error,
                           const std::string& gameArz) {
    m_archives.clear();
    m_winners.clear();
    m_slots.clear();
    std::vector<ArzArchive> loaded;
    loaded.reserve(2);
    for (const auto& lo : loadOrder()) {
        std::string full = gameDir + "\\" + lo.second;
        FILE* f = std::fopen(full.c_str(), "rb");
        if (!f) continue;
        std::fclose(f);
        ArzArchive a;
        a.setTag(lo.first);
        if (!a.load(full, error)) return false;
        loaded.push_back(std::move(a));
    }
    if (loaded.empty()) { if (error) *error = "no .arz archive under " + gameDir; return false; }
    if (!gameArz.empty()) {   // a custom game's own layer, over the main database
        ArzArchive a;
        a.setTag("game");
        if (!a.load(gameArz, error)) return false;
        loaded.push_back(std::move(a));
    }
    m_archives = std::move(loaded);
    std::size_t most = 0;
    for (const ArzArchive& a : m_archives) most = a.recordCount() > most ? a.recordCount() : most;
    m_slots.assign(slotCountFor(most), kEmpty);
    std::string k;
    for (std::size_t ai = 0; ai < m_archives.size(); ++ai) {
        const ArzArchive& a = m_archives[ai];
        for (const ArzEntry& e : a.entries()) {
            if (2 * (m_winners.size() + 1) > m_slots.size()) {   // half full: twice the slots
                m_slots.assign(m_slots.size() * 2, kEmpty);
                const std::size_t m = m_slots.size() - 1;
                for (std::size_t w = 0; w < m_winners.size(); ++w) {
                    std::size_t at = m_winners[w].second->hash & m;
                    while (m_slots[at] != kEmpty) at = (at + 1) & m;
                    m_slots[at] = std::uint32_t(w);
                }
            }
            a.key(e, k);
            const std::size_t mask = m_slots.size() - 1;
            for (std::size_t at = e.hash & mask;; at = (at + 1) & mask) {
                const std::uint32_t w = m_slots[at];
                if (w == kEmpty) {
                    m_slots[at] = std::uint32_t(m_winners.size());
                    m_winners.emplace_back(ai, &e);
                    break;
                }
                const std::pair<std::size_t, const ArzEntry*>& was = m_winners[w];
                if (was.second->hash == e.hash && m_archives[was.first].keyIs(*was.second, k)) {
                    m_winners[w] = {ai, &e};
                    break;
                }
            }
        }
    }
    return true;
}

bool ArzDatabase::lookup(const std::string& name, std::size_t* archive, const ArzEntry** entry) const {
    if (m_slots.empty()) return false;
    const std::string k = ArzArchive::normKey(name);
    const std::uint32_t h = ArzArchive::keyHash(k);
    const std::size_t mask = m_slots.size() - 1;
    for (std::size_t at = h & mask;; at = (at + 1) & mask) {
        const std::uint32_t w = m_slots[at];
        if (w == kEmpty) return false;
        const std::pair<std::size_t, const ArzEntry*>& win = m_winners[w];
        if (win.second->hash == h && m_archives[win.first].keyIs(*win.second, k)) {
            if (archive) *archive = win.first;
            if (entry) *entry = win.second;
            return true;
        }
    }
}

bool ArzDatabase::get(const std::string& name, ArzRecord& out) const {
    std::size_t ai;
    const ArzEntry* e;
    if (!lookup(name, &ai, &e)) return false;
    return m_archives[ai].decode(*e, out);
}

std::size_t ArzDatabase::indexBytes() const {
    std::size_t n = m_winners.capacity() * sizeof(m_winners[0]) + m_slots.capacity() * sizeof(std::uint32_t);
    for (const ArzArchive& a : m_archives) n += a.indexBytes();
    return n;
}

} // namespace gen
