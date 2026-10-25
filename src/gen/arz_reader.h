// arz_reader.h - reader for the game's ARZ database archive (Titan Quest AE, format version 3).
//
// Layout (little endian; the same layout tools/tqarz.py reads):
//   header 24 B: unk u16, version u16 (3), recordTableStart u32, recordTableSize u32,
//                recordCount u32, stringTableStart u32, stringTableSize u32
//   string table: count u32, then count x (len u32, bytes)
//   record table: recordCount x (nameIdx u32, typeLen u32, type bytes, offset u32,
//                 csize u32, time u64); record data lives at 24 + offset. There is NO dsize:
//                 the entry is 16 bytes after the type, not GD's 20.
//   record data: one zlib stream (csize bytes, 78 5E ...) holding fields of
//                type u16, count u16, nameIdx u32, count x u32
//                type 0 int32, 1 float32, 2 string-table index, 3 bool (int32)
//   footer: 16 B after the string table, 4 x adler32 (file, strings, data, record table).
//           Nothing here reads it; it is tolerated, as GD's reader tolerates GD's.
//
// load() keeps the index and holds the file open; decode() reads and decompresses one record
// body at a time. The file itself is never resident. The index is compact: the string table is
// kept as the file stores it (one block of latin-1 bytes, a u32 offset per string), a record is
// one 16-byte entry (its path's string number, the hash of its key, the body's offset and size),
// and the lookup is an open-addressing table of u32 entry numbers keyed by that hash. A string
// becomes UTF-8 only when it is asked for (str, name, key), so no path is held twice. The
// decompressed size is not stored, so a body is inflated into a buffer that grows with the
// stream and is refused past kMaxRecordBytes (inflate.h): never a size it did not read.
//
// Every read is bounds-checked: a corrupt file makes load() return false with a message,
// nothing here throws. tools/arz.py is the Python reference this mirrors.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace gen {

// An open archive file. Movable, not copyable; closes itself. The readers keep the header
// and the tables in memory and come back here for record bodies, so nothing the size of an
// archive is ever resident. One handle is used by one thread at a time (readAt seeks).
class FileHandle {
public:
    FileHandle() = default;
    ~FileHandle() { close(); }
    FileHandle(FileHandle&& o) noexcept : m_f(o.m_f) { o.m_f = nullptr; }
    FileHandle& operator=(FileHandle&& o) noexcept {
        if (this != &o) { close(); m_f = o.m_f; o.m_f = nullptr; }
        return *this;
    }
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
    bool open(const std::string& path);
    void close();
    bool valid() const { return m_f != nullptr; }
    std::uint64_t size() const;                   // 0 when the handle is closed
    // n bytes at off into out; false on a seek error or a short read.
    bool readAt(std::uint64_t off, std::size_t n, std::uint8_t* out) const;

private:
    std::FILE* m_f = nullptr;
};

// The largest decompressed record body decode() accepts. The biggest in TQ AE 2.10 is a few
// tens of KB; a body that would pass this is corrupt and is refused, never cut.
const std::size_t kMaxRecordBytes = 16u * 1024u * 1024u;

// Bytes of a latin-1 string re-encoded as UTF-8 (the archives are latin-1; the outputs UTF-8).
std::string latin1ToUtf8(const std::uint8_t* p, std::size_t n);

enum : std::uint16_t { kFtInt = 0, kFtFloat = 1, kFtString = 2, kFtBool = 3 };

struct ArzField {
    std::string name;
    std::uint16_t type = 0;
    std::vector<std::uint32_t> raw;       // the u32 words as stored
    std::vector<std::string> strs;        // resolved for type 2, else empty
};

struct ArzRecord {
    std::vector<ArzField> fields;         // in file order; the LAST field of a name wins
    const ArzField* find(const char* name) const;
    std::string str(const char* name) const;     // first string of the field, "" if absent
    std::int32_t i32(const char* name) const;    // first value as int, 0 if absent
};

// One record of an archive. Its path and key are read from the archive's string table
// (ArzArchive::name / key / keyIs).
struct ArzEntry {
    std::uint32_t nameIdx = 0;            // the path's string number
    std::uint32_t hash = 0;               // ArzArchive::keyHash of the key
    std::uint32_t offset = 0, csize = 0;  // the body; TQ stores no decompressed size
};

class ArzArchive {
public:
    bool load(const std::string& path, std::string* error);
    const std::string& path() const { return m_path; }
    const std::string& tag() const { return m_tag; }
    void setTag(const std::string& t) { m_tag = t; }
    std::size_t recordCount() const { return m_entries.size(); }
    std::size_t stringCount() const { return m_strOff.size(); }
    const std::vector<ArzEntry>& entries() const { return m_entries; }
    // String i of the table as UTF-8; "" past the end.
    std::string str(std::size_t i) const;
    // String i's stored (latin-1) bytes and their count; null and 0 past the end.
    const char* raw(std::size_t i, std::size_t* len) const;
    // The record's path: '/' separators, original case, UTF-8.
    std::string name(const ArzEntry& e) const;
    // normKey(name(e)), written into out (whose capacity is reused).
    void key(const ArzEntry& e, std::string& out) const;
    std::string key(const ArzEntry& e) const;
    // key(e) == k, compared byte by byte without building the key.
    bool keyIs(const ArzEntry& e, const std::string& k) const;
    // Entry by key ("records/..." any case, '\\' or '/'); nullptr when absent. Of two entries
    // with one key, the later one.
    const ArzEntry* entry(const std::string& name) const;
    // Decodes one entry. False when the body is corrupt (`why`, if given, says how).
    bool decode(const ArzEntry& e, ArzRecord& out, std::string* why = nullptr) const;
    static std::string normKey(const std::string& name);
    // FNV-1a (32 bits) of a key's bytes: the hash every index table here uses.
    static std::uint32_t keyHash(const std::string& key);
    // What the index holds, in bytes (the vectors' capacities).
    std::size_t indexBytes() const;

private:
    std::string m_path, m_tag;
    FileHandle m_file;
    std::uint64_t m_fileSize = 0;
    mutable std::vector<std::uint8_t> m_scratch;   // one compressed record body
    mutable std::vector<std::uint8_t> m_body;      // the same body, inflated
    std::vector<char> m_table;                     // the string table as stored
    std::vector<std::uint32_t> m_strOff;           // string i's bytes in m_table; its length is the u32 before
    std::vector<ArzEntry> m_entries;
    std::vector<std::uint32_t> m_slots;            // entry numbers by key hash; a power of two long
};

// Titan Quest AE ships ONE database: Database\database.arz holds the base game and every
// expansion (the xpack records live under records/xpack*/). The stacking stays, for one archive.
class ArzDatabase {
public:
    // Loads the archives that exist under gameDir in load order; false when none loads. gameArz
    // (a custom game's <root><name>\Database\<name>.arz, "" = none) is loaded LAST, tag "game",
    // as Engine::InitializeMod loads it over the main database: its records win, and a record
    // only it has comes after the base game's (winners() keeps the first-seen order).
    bool loadGame(const std::string& gameDir, std::string* error,
                  const std::string& gameArz = std::string());
    const std::vector<ArzArchive>& archives() const { return m_archives; }
    std::size_t size() const { return m_winners.size(); }
    // The winning archive index and entry for a key, or false.
    bool lookup(const std::string& name, std::size_t* archive, const ArzEntry** entry) const;
    bool get(const std::string& name, ArzRecord& out) const;
    // Every winning (archive index, entry) in first-seen order.
    const std::vector<std::pair<std::size_t, const ArzEntry*>>& winners() const { return m_winners; }
    // What the index holds, in bytes: every archive's and the winners' table.
    std::size_t indexBytes() const;
    // The relative paths this reader looks for, in load order.
    static std::vector<std::pair<std::string, std::string>> loadOrder();

private:
    std::vector<ArzArchive> m_archives;
    std::vector<std::pair<std::size_t, const ArzEntry*>> m_winners;
    std::vector<std::uint32_t> m_slots;   // winner numbers by key hash; a power of two long
};

} // namespace gen
