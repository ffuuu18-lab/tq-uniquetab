// arc_reader.h - reader for the game's ARC v1 resource archives (Titan Quest AE: Text_<lang>.arc,
// Items.arc and the expansions' Items.arc / Item.arc).
//
// Layout (little endian; the same 28-byte header, 12-byte parts and 44-byte entries as GD's v3):
//   header 28 B: magic 'ARC\0', version 1, numFileEntries, numDataRecords, recordTableSize,
//                stringTableSize, recordTableOffset
//   data-record table at recordTableOffset: numDataRecords x (partOffset, csize, dsize)
//   string table at recordTableOffset + recordTableSize ('\0'-separated names)
//   file-entry table after the string table: numFileEntries x 44 B
//                (entryType, fileOffset, csize, dsize, hash, fileTime u64, numParts,
//                 firstPart, nameLength, nameOffset)
//   a part is a zlib stream (GD: an LZ4 block); a part whose csize == dsize is stored raw;
//   numParts == 0 means the whole entry is stored raw at fileOffset.
//
// load() keeps the part table, the string table and the entry table - the index - and holds
// the file open; read() fetches and decompresses one entry body at a time. The archive
// itself is never resident: the base game's Items.arc alone is 300 MB.
//
// Bounds-checked like arz_reader; a corrupt archive fails to load, nothing throws.
// tools/tqarc.py is the Python reference this mirrors.
#pragma once

#include "gen/arz_reader.h"          // FileHandle, and normKey this delegates to

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace gen {

struct ArcEntry {
    std::string name;                     // '/' separators, original case
    std::string key;                      // lower-cased name
    std::uint32_t etype = 0, offset = 0, csize = 0, dsize = 0, numParts = 0, firstPart = 0;
    std::uint64_t ftime = 0;
};

class ArcArchive {
public:
    bool load(const std::string& path, std::string* error);
    const std::string& path() const { return m_path; }
    const std::string& tag() const { return m_tag; }
    void setTag(const std::string& t) { m_tag = t; }
    std::size_t size() const { return m_entries.size(); }
    const std::vector<ArcEntry>& entries() const { return m_entries; }
    const ArcEntry* entry(const std::string& name) const;
    // The entry body. limit > 0 stops after the first part that reaches it, so a caller
    // that wants a header does not pay for the rest: out then holds at least limit bytes,
    // or the whole entry when it is shorter.
    bool read(const ArcEntry& e, std::vector<std::uint8_t>& out, std::size_t limit = 0) const;
    static std::string normKey(const std::string& name);

private:
    struct Part { std::uint32_t offset, csize, dsize; };
    static const std::uint32_t kMaxPartBytes = 64u * 1024u * 1024u;   // a larger part is refused
    std::string m_path, m_tag;
    FileHandle m_file;
    std::uint64_t m_fileSize = 0;
    mutable std::vector<std::uint8_t> m_scratch;   // one compressed part
    std::vector<Part> m_parts;
    std::vector<ArcEntry> m_entries;
    std::unordered_map<std::string, std::size_t> m_index;
};

// A resource path names its own archive (TQ; GD instead stacks one archive name over four
// folders, later wins): "[XPack<n>\]<Arc>\<rest>" is the entry <rest> of
// <game>\Resources\[XPack<n>\]<Arc>.arc. The Immortal Throne folder is "xpack", the others
// "XPack2".."XPack4", and Eternal Embers ships its item art as Item.arc - singular, which is what
// its bitmap paths say; an "Items" path whose Items.arc is absent falls back to Item.arc, as
// tools/tqarc.py does. Archives are opened on first use and only their index is kept.
class ResourceArcs {
public:
    void setRoot(const std::string& gameDir) { m_root = gameDir + "\\Resources"; m_arcs.clear(); }
    // The entry bytes for a resource path; `limit` as ArcArchive::read. False when the path maps
    // to no archive, the archive or the entry is absent, or the body does not decode; *error is
    // set only for an archive that exists and does not load (the generation then fails).
    bool read(const std::string& resPath, std::vector<std::uint8_t>& out, std::size_t limit,
              std::string* error) const;
    // "<rel path of the archive>" a resource path resolves to, "" when none does.
    std::string sourceOf(const std::string& resPath) const;
    std::size_t opened() const;          // archives actually loaded so far

private:
    const ArcArchive* archiveFor(const std::string& resPath, std::string* inner,
                                 std::string* error) const;
    std::string m_root;
    mutable std::map<std::string, std::unique_ptr<ArcArchive>> m_arcs;   // null = absent
};

// `tag=Text` lines of one tag file, appended to tags (later lines win). TQ: a file that opens
// with the UTF-16LE BOM FF FE (53 of the 57 in Text_EN.arc) is decoded from UTF-16LE, an
// unpaired surrogate or an odd last byte becoming U+FFFD as Python's "replace" does; any other
// file is GD's case - UTF-8 (a BOM is dropped), else read as latin-1.
void parseTagFile(const std::vector<std::uint8_t>& data,
                  std::unordered_map<std::string, std::string>& tags);

// Every .txt of <game>\Text\Text_<lang>.arc merged in archive order, a later file winning (TQ
// has one text archive for the base game and every expansion).
bool loadTextTags(const std::string& gameDir, const std::string& lang,
                  std::unordered_map<std::string, std::string>& tags, std::string* error);

} // namespace gen
