// test_catalogue.cpp - offline harness for src\gen: the archive readers and the generator.
// Reads the Titan Quest AE folder (%TQ_GAME_DIR%, else the Steam library folders from the
// registry, as tools\test_bindings.cpp finds it) READ ONLY, and writes only under build\test.
// NO GAME is launched.
//
//   test_catalogue.exe --units <dir> <oracle.bin> the decoder and reader refusals, no game needed:
//                                                zlib streams (stored, fixed, dynamic, and every
//                                                way one can be wrong), a synthetic ARZ whose
//                                                record has csize 0 / a truncated / a corrupt body,
//                                                a synthetic ARC whose part does not inflate, an
//                                                ARC of the wrong version, the UTF-16 tag parser
//   test_catalogue.exe --catalogue <oracle.bin> [<out.bin>]
//                                                generates catalogue.bin from the game folder
//                                                and compares it byte for byte with the oracle
//   test_catalogue.exe --ensure <data dir> <mod dir>
//                                                the DLL's start-up path (gen::ensureOutputs)
//                                                against a scratch mod folder: first launch (all
//                                                four outputs byte for byte against
//                                                <data dir>\oracle), matching stamp, changed
//                                                stamp, missing output, the error path
//   test_catalogue.exe --peak <mod dir>          one first-launch generation in a fresh process:
//                                                the time, and the peak working set against the
//                                                200 MB budget (the peak private commit beside it)
//   test_catalogue.exe --order <dir>             a scratch custom game whose Items.arc holds one
//                                                entry of the base game's: the game's archives
//                                                are searched first, entry by entry
//   test_catalogue.exe --rulefix <dir>           a scratch custom game of fixture records outside
//                                                the item folders: the collection rule's holders,
//                                                template words, reach walk, developer and
//                                                creature folders
//   test_catalogue.exe --zeroarc <dir> <oracle dir>
//                                                a scratch custom game whose Text.arc and Items.arc
//                                                are zeroed: both skipped with one warning each,
//                                                the generation the base game's
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>

#include "gen/arc_reader.h"
#include "gen/arz_reader.h"
#include "gen/catalogue_gen.h"
#include "gen/relics_gen.h"    // relicBlockOf, relicsListText: uniq-relics.txt
#include "gen/gen_heap.h"      // the build arena of the start-up builds
#include "gen/generate.h"
#include "gen/inflate.h"
#include "model/catalogue.h"
#include "gen/ut_groups.h"    // viewKeysFor
#include "ut_depositgate.h"   // utSaveSetLeaf / utSaveSetGame: a game's leaf is the journal's
#include "ut_gameprobe.h"     // utGameLocate
#include "ut_gamelist.h"      // utGamesFind: the custom games the DLL builds at start-up
#include "ut_gameswitch.h"    // utAddrSpace
#include "gen/ut_groups_default.h"   // builtinGroupSet
#include "model/layout.h"     // readGameGroups: the in-game reader of uniq-groups.txt
#include "ut_caravangeo.h"    // utCaravanGeoParse: the P line of uniq-geometry.txt


#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

int fail(const char* what) {
    std::fprintf(stderr, "[test] FAIL: %s\n", what);
    return 1;
}

bool readAll(const std::string& path, std::vector<std::uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? std::size_t(n) : 0);
    bool ok = out.empty() || std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

bool writeAll(const std::string& path, const std::vector<std::uint8_t>& data) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), f) == data.size();
    return (std::fclose(f) == 0) && ok;
}

// ---- the game folder: %TQ_GAME_DIR%, else the Steam library folders from the registry ----------
bool hasTqExe(const std::string& dir) {
    const std::string p = dir + "\\TQ.exe";
    const DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::string gameDir() {
    const char* env = std::getenv("TQ_GAME_DIR");
    if (env && *env) return hasTqExe(env) ? std::string(env) : std::string();
    char steam[MAX_PATH] = {0};
    DWORD cb = sizeof(steam);
    if (RegGetValueA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath", RRF_RT_REG_SZ,
                     nullptr, steam, &cb) != ERROR_SUCCESS) {
        cb = sizeof(steam);
        if (RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Valve\\Steam", "InstallPath",
                         RRF_RT_REG_SZ, nullptr, steam, &cb) != ERROR_SUCCESS)
            return std::string();
    }
    std::vector<std::string> libs;
    libs.push_back(steam);
    std::vector<std::uint8_t> vdf;
    if (readAll(std::string(steam) + "\\steamapps\\libraryfolders.vdf", vdf) && !vdf.empty()) {
        const std::string text((const char*)vdf.data(), vdf.size());
        for (size_t at = text.find("\"path\""); at != std::string::npos;
             at = text.find("\"path\"", at + 6)) {
            const size_t q1 = text.find('"', at + 6);
            const size_t q2 = q1 == std::string::npos ? q1 : text.find('"', q1 + 1);
            if (q2 == std::string::npos) break;
            std::string v = text.substr(q1 + 1, q2 - q1 - 1);
            std::string un;
            for (size_t i = 0; i < v.size(); ++i) {
                if (v[i] == '\\' && i + 1 < v.size() && v[i + 1] == '\\') ++i;
                un += v[i];
            }
            libs.push_back(un);
        }
    }
    for (size_t i = 0; i < libs.size(); ++i) {
        std::string cand = libs[i] + "\\steamapps\\common\\Titan Quest Anniversary Edition";
        for (size_t k = 0; k < cand.size(); ++k)
            if (cand[k] == '/') cand[k] = '\\';
        if (hasTqExe(cand)) return cand;
    }
    return std::string();
}

// Peak working set of this process, in MB. PeakWorkingSetSize only ever grows, so a sample
// taken right after the first generation is that generation's peak - which is why --peak runs
// in a process of its own and --ensure measures its FIRST pass.
double peakWorkingSetMb() {
    PROCESS_MEMORY_COUNTERS pmc;
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc)) return 0.0;
    return double(pmc.PeakWorkingSetSize) / (1024.0 * 1024.0);
}

// Peak private commit of this process (PeakPagefileUsage), in MB: committed private bytes, which
// the 2 GB of a 32-bit process must hold and the system cannot trim as it can the working set.
double peakCommitMb() {
    PROCESS_MEMORY_COUNTERS pmc;
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof pmc)) return 0.0;
    return double(pmc.PeakPagefileUsage) / (1024.0 * 1024.0);
}

const double kPeakBudgetMb = 200.0;

// Prints the first differing offset with 16 bytes of context on both sides.
int compareBytes(const char* what, const std::vector<std::uint8_t>& got,
                 const std::vector<std::uint8_t>& want) {
    std::size_t n = std::min(got.size(), want.size()), at = n;
    for (std::size_t i = 0; i < n; ++i) if (got[i] != want[i]) { at = i; break; }
    if (at == n && got.size() == want.size()) {
        std::printf("[test] %s: byte-identical (%zu B)\n", what, got.size());
        return 0;
    }
    std::printf("[test] FAIL: %s differs at offset %zu (generated %zu B, oracle %zu B)\n",
                what, at, got.size(), want.size());
    std::size_t from = at >= 16 ? at - 16 : 0;
    for (int side = 0; side < 2; ++side) {
        const auto& v = side ? want : got;
        std::printf("  %s:", side ? "oracle   " : "generated");
        for (std::size_t i = from; i < from + 48 && i < v.size(); ++i) std::printf(" %02x", v[i]);
        std::printf("\n            ");
        for (std::size_t i = from; i < from + 48 && i < v.size(); ++i)
            std::printf(" %c ", v[i] >= 32 && v[i] < 127 ? char(v[i]) : '.');
        std::printf("\n");
    }
    return 1;
}

unsigned long long mtimeOf(const std::string& p) {
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExA(p.c_str(), GetFileExInfoStandard, &fa)) return 0;
    return (unsigned long long(fa.ftLastWriteTime.dwHighDateTime) << 32) | fa.ftLastWriteTime.dwLowDateTime;
}

// ---- --units ---------------------------------------------------------------------------------
std::vector<std::uint8_t> hexBytes(const char* h) {
    std::vector<std::uint8_t> v;
    for (; h[0] && h[1]; h += 2) {
        auto nib = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
        v.push_back(std::uint8_t(nib(h[0]) * 16 + nib(h[1])));
    }
    return v;
}

void put32(std::vector<std::uint8_t>& o, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) o.push_back(std::uint8_t(v >> (8 * i)));
}

// A zlib stream that stores `data` in one stored block (always valid, no compression needed).
std::vector<std::uint8_t> zlibStored(const std::vector<std::uint8_t>& data) {
    std::vector<std::uint8_t> z = {0x78, 0x01, 0x01};
    const std::uint16_t n = std::uint16_t(data.size());
    z.push_back(std::uint8_t(n)); z.push_back(std::uint8_t(n >> 8));
    z.push_back(std::uint8_t(~n)); z.push_back(std::uint8_t(~n >> 8));
    z.insert(z.end(), data.begin(), data.end());
    const std::uint32_t a = gen::adler32(1, data.data(), data.size());
    for (int i = 3; i >= 0; --i) z.push_back(std::uint8_t(a >> (8 * i)));
    return z;
}

// A one-record TQ ARZ: strings {record, "Class", "ItemArtifact"}, a body of one string field,
// stored as the given bytes with the given csize (the real body is a zlib stream of the field).
std::vector<std::uint8_t> synthArz(const std::vector<std::uint8_t>& body, std::uint32_t csize) {
    const char* strs[3] = {"records/test/a.dbr", "Class", "ItemArtifact"};
    std::vector<std::uint8_t> out(24, 0);
    const std::uint32_t dataAt = 0;           // the body right after the 24-byte header
    out.insert(out.end(), body.begin(), body.end());
    const std::uint32_t recStart = std::uint32_t(out.size());
    put32(out, 0);                            // nameIdx
    put32(out, 4); out.insert(out.end(), {'T', 'e', 's', 't'});
    put32(out, dataAt); put32(out, csize);
    put32(out, 0); put32(out, 0);             // FILETIME
    const std::uint32_t recSize = std::uint32_t(out.size()) - recStart;
    const std::uint32_t strStart = std::uint32_t(out.size());
    put32(out, 3);
    for (const char* s : strs) {
        put32(out, std::uint32_t(std::strlen(s)));
        out.insert(out.end(), s, s + std::strlen(s));
    }
    const std::uint32_t strSize = std::uint32_t(out.size()) - strStart;
    for (int i = 0; i < 4; ++i) put32(out, 0);   // the adler footer (tolerated, not read)
    std::uint8_t* h = out.data();
    h[0] = 4; h[1] = 0; h[2] = 3; h[3] = 0;
    auto set32 = [&](std::size_t at, std::uint32_t v) { for (int i = 0; i < 4; ++i) h[at + i] = std::uint8_t(v >> (8 * i)); };
    set32(4, recStart); set32(8, recSize); set32(12, 1); set32(16, strStart); set32(20, strSize);
    return out;
}

// A one-entry ARC of the given version whose one part is `part` (csize = part size, dsize given).
// entryDsize: the entry's own dsize when it differs from the part's (the per-entry bound).
// name: the entry's name ('/' separators).
std::vector<std::uint8_t> synthArc(std::uint32_t version, const std::vector<std::uint8_t>& part,
                                   std::uint32_t dsize, std::uint32_t entryDsize = 0xFFFFFFFFu,
                                   const std::string& name = "a/b.txt") {
    std::vector<std::uint8_t> out(28, 0);
    const std::uint32_t partAt = std::uint32_t(out.size());
    out.insert(out.end(), part.begin(), part.end());
    const std::uint32_t recOff = std::uint32_t(out.size());
    put32(out, partAt); put32(out, std::uint32_t(part.size())); put32(out, dsize);
    out.insert(out.end(), name.c_str(), name.c_str() + name.size() + 1);   // with its NUL
    const std::uint32_t strSize = std::uint32_t(name.size() + 1);
    put32(out, 3); put32(out, partAt); put32(out, std::uint32_t(part.size()));
    put32(out, entryDsize == 0xFFFFFFFFu ? dsize : entryDsize);
    put32(out, 0); put32(out, 0); put32(out, 0);       // hash, FILETIME
    put32(out, 1); put32(out, 0); put32(out, strSize - 1); put32(out, 0);
    std::uint8_t* h = out.data();
    auto set32 = [&](std::size_t at, std::uint32_t v) { for (int i = 0; i < 4; ++i) h[at + i] = std::uint8_t(v >> (8 * i)); };
    set32(0, 0x00435241); set32(4, version); set32(8, 1); set32(12, 1); set32(16, 12);
    set32(20, strSize); set32(24, recOff);
    return out;
}

int units(const std::string& scratch, const std::string& oracle) {
    int rc = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("[test] %s: %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) rc = 1;
    };
    std::string why;
    std::vector<std::uint8_t> out;

    // the three block types
    const std::vector<std::uint8_t> hello = {'h', 'e', 'l', 'l', 'o'};
    std::vector<std::uint8_t> st = zlibStored(hello);
    check(gen::zlibInflate(st.data(), st.size(), out, 100, &why) && out == hello, "a stored block inflates");
    std::vector<std::uint8_t> fx = hexBytes("78014b4c4a4e44450a19a93939f9c82400f9f30d81");
    out.clear();
    const char* fxWant = "abcabcabcabcabcabc hello hello hello";
    check(gen::zlibInflate(fx.data(), fx.size(), out, 100, &why)
              && std::string(out.begin(), out.end()) == fxWant, "a fixed-Huffman block inflates");
    std::vector<std::uint8_t> dy = hexBytes(
        "78da85d3b10ac2301485e15df01df208494ed324838b539df5053a441dac1549f1f5958263fa6f17ee816ffaeb"
        "783bd532596b0f977b31c7f963e6aba9bf73589e75bfabffbf5bffe75719df8d855f1743794c8d8180e8900844f4"
        "4044241211799b709608e780701e0821d1111180e891884424203211de02e1dd36e13d1222a2032220d11311814848"
        "642004750beb16d52da85b58b7a86e41ddc2ba4575ab51f717f72da785");
    std::string dyWant;
    const char* kinds[3] = {"Bow", "Spear", "Helm"};
    for (int i = 0; i < 40; ++i) {
        char line[64];
        std::snprintf(line, sizeof line, "tagItem%03d=The %s of the Hunt\r\n", i, kinds[i % 3]);
        dyWant += line;
    }
    out.clear();
    check(gen::zlibInflate(dy.data(), dy.size(), out, 1 << 20, &why)
              && std::string(out.begin(), out.end()) == dyWant, "a dynamic-Huffman block inflates");
    // appending: a second stream lands after the first, and its distances cannot reach into it
    out.assign({'x', 'y'});
    check(gen::zlibInflate(st.data(), st.size(), out, 100, &why) && out.size() == 7 && out[2] == 'h',
          "a stream appends to what the buffer already holds");

    // every way to be wrong is a refusal, and the buffer is left as it was
    auto refused = [&](std::vector<std::uint8_t> z, std::size_t cap, const char* what) {
        out.assign({'k'});
        const bool ok = gen::zlibInflate(z.data(), z.size(), out, cap, &why);
        std::printf("[test]       (%s)\n", ok ? "accepted" : why.c_str());
        check(!ok && out.size() == 1 && out[0] == 'k', what);
    };
    refused(std::vector<std::uint8_t>(), 100, "an empty stream (csize 0) is refused");
    refused(std::vector<std::uint8_t>(dy.begin(), dy.end() - 9), 1 << 20, "a truncated stream is refused");
    { auto z = dy; z[z.size() - 1] ^= 0x55; refused(z, 1 << 20, "a stream whose adler32 does not match is refused"); }
    { auto z = dy; z[40] ^= 0xFF; refused(z, 1 << 20, "a corrupt dynamic block is refused"); }
    refused(dy, 1000, "a stream that would pass its size cap is refused, never cut");
    { auto z = st; z[1] = 0x20; refused(z, 100, "a zlib stream that needs a preset dictionary is refused"); }
    { auto z = st; z[0] = 0x79; refused(z, 100, "a header that is not DEFLATE is refused"); }
    { auto z = st; z[5] ^= 1; refused(z, 100, "a stored block whose length check fails is refused"); }
    { auto z = st; z[2] = 0x07; refused(z, 100, "the reserved block type is refused"); }

    // the ARZ reader: a record with csize 0, a truncated body, a corrupt body - refused, not guessed
    {
        std::vector<std::uint8_t> field = {2, 0, 1, 0, 1, 0, 0, 0, 2, 0, 0, 0};   // Class = ItemArtifact
        std::vector<std::uint8_t> body = zlibStored(field);
        struct Case { const char* what; std::vector<std::uint8_t> bytes; std::uint32_t csize; bool good; };
        std::vector<std::uint8_t> corrupt = body;
        corrupt[body.size() - 2] ^= 0x10;
        const Case cases[] = {
            {"a well-formed record decodes (Class = ItemArtifact)", body, std::uint32_t(body.size()), true},
            {"a record with csize 0 is refused", body, 0, false},
            {"a record whose body is truncated is refused", body, std::uint32_t(body.size() - 3), false},
            {"a record whose body is corrupt is refused", corrupt, std::uint32_t(corrupt.size()), false},
        };
        for (const Case& c : cases) {
            const std::string p = scratch + "\\synth.arz";
            writeAll(p, synthArz(c.bytes, c.csize));
            gen::ArzArchive a;
            std::string err;
            gen::ArzRecord rec;
            const bool loaded = a.load(p, &err);
            const bool dec = loaded && a.recordCount() == 1 && a.decode(a.entries()[0], rec, &why);
            if (c.good) check(dec && rec.str("Class") == "ItemArtifact", c.what);
            else {
                std::printf("[test]       (%s)\n", loaded ? why.c_str() : err.c_str());
                check(loaded && !dec, c.what);
            }
        }
    }
    // the ARC reader: v1 loads, a part that does not inflate is refused, v3 (GD's) is refused
    {
        const std::string p = scratch + "\\synth.arc";
        std::vector<std::uint8_t> text = {'k', '=', 'v', '\r', '\n'};
        std::vector<std::uint8_t> part = zlibStored(text);
        writeAll(p, synthArc(1, part, std::uint32_t(text.size())));
        gen::ArcArchive a;
        std::string err;
        std::vector<std::uint8_t> got;
        check(a.load(p, &err) && a.size() == 1 && a.read(a.entries()[0], got) && got == text,
              "an ARC v1 entry with a zlib part reads back");
        std::vector<std::uint8_t> bad = part;
        bad[bad.size() - 1] ^= 0x01;              // the adler32
        writeAll(p, synthArc(1, bad, std::uint32_t(text.size())));
        gen::ArcArchive b;
        check(b.load(p, &err) && !b.read(b.entries()[0], got), "an ARC part that fails to inflate is refused");
        writeAll(p, synthArc(1, part, std::uint32_t(text.size() + 1)));
        gen::ArcArchive c;
        check(c.load(p, &err) && !c.read(c.entries()[0], got),
              "an ARC part that inflates short of its dsize is refused");
        writeAll(p, synthArc(3, part, std::uint32_t(text.size())));
        gen::ArcArchive d;
        check(!d.load(p, &err), "an ARC of GD's version 3 is refused");
        writeAll(p, synthArc(1, part, std::uint32_t(text.size()), std::uint32_t(text.size() - 1)));
        gen::ArcArchive over;
        check(over.load(p, &err) && !over.read(over.entries()[0], got),
              "an ARC entry whose parts inflate past the entry's dsize is refused");
        writeAll(p, synthArc(1, part, std::uint32_t(text.size()), std::uint32_t(text.size() + 5)));
        gen::ArcArchive under;
        check(under.load(p, &err) && !under.read(under.entries()[0], got),
              "an ARC entry whose parts come short of the entry's dsize is refused");
    }
    // the tag parser: UTF-16LE with its BOM, and GD's single-byte case
    {
        std::unordered_map<std::string, std::string> tags;
        const char16_t u[] = u"tagA=Ares\r\n// x=y\r\ntagB=Château Ω\r\n tagC =  sp \r\n";
        std::vector<std::uint8_t> d = {0xFF, 0xFE};
        for (const char16_t* q = u; *q; ++q) { d.push_back(std::uint8_t(*q)); d.push_back(std::uint8_t(*q >> 8)); }
        gen::parseTagFile(d, tags);
        check(tags["tagA"] == "Ares" && tags["tagB"] == "Ch\xc3\xa2teau \xce\xa9" && tags["tagC"] == "  sp "
                  && tags.find("// x") == tags.end(),
              "a UTF-16LE tag file parses (BOM, comments, a key stripped, a value kept as written)");
        std::vector<std::uint8_t> one = {'t', 'a', 'g', 'D', '=', 0xE9, 't', 0xE9, '\r', '\n'};
        gen::parseTagFile(one, tags);
        check(tags["tagD"] == "\xc3\xa9t\xc3\xa9", "a single-byte tag file: 0xE9 is e-acute (cp1252 = latin-1 there)");
        std::vector<std::uint8_t> win = {'t', 'a', 'g', 'E', '=', 0x85, 'x', 0x92, 0x81, '\r', '\n'};
        gen::parseTagFile(win, tags);
        check(tags["tagE"] == "\xe2\x80\xa6" "x\xe2\x80\x99\xc2\x81",
              "a single-byte tag file is cp1252: 0x85 an ellipsis (no line break), 0x92 a quote, 0x81 kept");
        std::vector<std::uint8_t> odd = {0xFF, 0xFE, 'k', 0, '=', 0, 'v', 0, 0x00, 0xD8, 'w', 0, 'x'};
        tags.clear();
        gen::parseTagFile(odd, tags);
        check(tags["k"] == "v\xef\xbf\xbdw\xef\xbf\xbd", "an unpaired surrogate and an odd last byte are U+FFFD");
    }
    // the model refuses GD's v1 catalogue.bin and a v2 footprint out of range
    {
        std::vector<std::uint8_t> bin;
        if (readAll(oracle, bin) && bin.size() > 80) {
            gdut::Catalogue c;
            std::string err;
            check(c.loadFromMemory(bin, &err) && c.formatVersion() == 2, "the oracle catalogue.bin loads as v2");
            std::vector<std::uint8_t> v1 = bin;
            v1[4] = 1;
            check(!c.loadFromMemory(v1, &err), "a v1 (GD) catalogue.bin is refused by the version check");
            std::vector<std::uint8_t> big = bin;
            const std::uint32_t itemOff = bin[24] | (bin[25] << 8) | (bin[26] << 16) | (bin[27] << 24);
            big[itemOff + 24] = 3;                // footW of the first item
            check(!c.loadFromMemory(big, &err), "a footprint outside 1..2 x 1..5 is refused");
        } else {
            check(false, "data\\oracle\\catalogue.bin is readable");
        }
    }

    // a custom game: its leaf is the journal's (one leaf function), and the locator's rules
    {
        gen::GameSpec s;
        check(s.startup() && s.name().empty() && s.dir().empty() && s.arzPath().empty() &&
                  s.resourcesDir().empty(), "the start-up game has no leaf, folder or archives");
        const char* names[4] = {"My Mod", "SomeGame", "custom", "A.B"};
        bool same = true;
        for (const char* n : names) {
            s.root = "games/";
            s.modName = n;
            char set[96], leaf[96];
            same = same && ut::utSaveSetLeaf(true, n, false, false, set, sizeof set) &&
                   ut::utSaveSetGame(set, leaf, sizeof leaf) && s.name() == leaf && s.tag() == leaf;
        }
        check(same, "GameSpec::name() is utSaveSetGame(utSaveSetLeaf(name)) - the journal's leaf");
        s.modName = "My Mod";
        check(s.name() == "my_mod-0e607ba7", "\"My Mod\" -> my_mod-0e607ba7 (a lost byte adds the hash)");
        s.modName = "custom";
        check(s.name() == "mod_custom", "\"custom\" -> mod_custom");
        s.modName = "SomeGame";
        check(s.name() == "somegame" && s.dir() == "games\\SomeGame" &&
                  s.arzPath() == "games\\SomeGame\\Database\\SomeGame.arz" &&
                  s.resourcesDir() == "games\\SomeGame\\Resources",
              "the folder, the .arz and the Resources folder as InitializeMod builds them");
        s.root = "games";
        check(s.dir() == "games\\SomeGame", "a root without a trailing separator gets one");
        s.modName = "---";
        check(s.name().empty(), "a name that sanitises to nothing has no leaf");

        char full[MAX_PATH] = {0};   // the scratch path as the engine names a folder: no ".." in it
        GetFullPathNameA(scratch.c_str(), MAX_PATH, full, nullptr);
        const std::string base = std::string(full) + "\\games";
        const std::string dir = base + "\\Gm";
        CreateDirectoryA(base.c_str(), nullptr);
        CreateDirectoryA(dir.c_str(), nullptr);
        CreateDirectoryA((dir + "\\Database").c_str(), nullptr);
        DeleteFileA((dir + "\\Database\\Gm.arz").c_str());
        gen::GameSpec g;
        check(!ut::utGameLocate(base, "Gm", g, &why) && g.startup() && why.find("Gm.arz") != std::string::npos,
              "a game folder without Database\\<name>.arz is refused");
        writeAll(dir + "\\Database\\Gm.arz", std::vector<std::uint8_t>(16, 0));
        check(ut::utGameLocate(base, "Gm", g, &why) && g.name() == "gm" && g.modName == "Gm",
              "a folder with Database\\<name>.arz is located (the Resources folder may be absent)");
        check(!ut::utGameLocate(base, "..", g, &why) && !ut::utGameLocate(base, "a\\b", g, &why) &&
                  !ut::utGameLocate(base, "c:x", g, &why) && !ut::utGameLocate(base, "", g, &why),
              "a name that is not one folder name is refused");
        check(!ut::utGameLocate(base + "\\..\\games", "Gm", g, &why) && why.find("climbs") != std::string::npos,
              "a root with a \"..\" segment is refused");
        check(!ut::utGameLocate(base, "Absent", g, &why) && why.find("is not there") != std::string::npos,
              "an absent game folder is refused");
        check(!ut::utGameLocate(std::string(), "Gm", g, &why), "no root is refused");
        check(utg::viewKeysFor(std::string()) == std::vector<std::string>{"vanilla"} &&
                  utg::viewKeysFor("gm") == std::vector<std::string>{"gm"},
              "the start-up game answers to the view key vanilla, a game to its leaf");
    }
    return rc;
}

// ---- --catalogue -------------------------------------------------------------------------------
int catalogue(const std::string& oracle, const std::string& outPath) {
    std::string game = gameDir();
    if (game.empty()) return fail("no Titan Quest AE folder: set TQ_GAME_DIR");
    auto t0 = std::chrono::steady_clock::now();
    gen::GameData g;
    std::string err;
    if (!g.load(game, "EN", &err)) return fail(err.c_str());
    std::vector<gen::CatalogueItem> items;
    std::vector<gen::ExcludedItem> excluded;
    std::vector<std::string> warnings;
    // the shipped default: collect_mi=1, collect_unobtainable=0
    if (!gen::collectItems(g, items, excluded, warnings, &err, false, true)) return fail(err.c_str());
    std::vector<std::uint8_t> bin;
    gen::CatalogueStats st;
    if (!gen::packCatalogue(items, bin, &st, &err)) return fail(err.c_str());
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    std::printf("[test] catalogue: %zu items (%zu equipment, %zu artifacts, %zu monster infrequents), "
                "%zu excluded, %zu sets / %zu members, %zu strings %zu B, %zu B total, generated in %lld ms\n",
                st.items, st.equipment, st.relics, st.mi, excluded.size(), st.sets, st.members, st.strings,
                st.stringBytes, st.bytes, (long long)ms);
    for (const std::string& w : warnings) std::printf("[test] warning: %s\n", w.c_str());
    if (!outPath.empty()) writeAll(outPath, bin);
    std::vector<std::uint8_t> want;
    if (!readAll(oracle, want)) return fail("cannot read the oracle catalogue.bin");
    const int rc = compareBytes("catalogue.bin", bin, want);

    // The DLC filter, which an install with every expansion never runs: the
    // same data with Atlantis (expansion 3) marked absent must move exactly its records out, each
    // with the reason.
    std::size_t atlantis = 0;
    for (const gen::CatalogueItem& it : items) if (it.expansion == 3) ++atlantis;
    g.expansionInstalled[3] = false;
    std::vector<gen::CatalogueItem> items2;
    std::vector<gen::ExcludedItem> excluded2;
    std::vector<std::string> warnings2;
    const bool ok2 = gen::collectItems(g, items2, excluded2, warnings2, &err, false, true);
    g.expansionInstalled[3] = true;
    std::size_t left3 = 0, filtered = 0;
    for (const gen::CatalogueItem& it : items2) if (it.expansion == 3) ++left3;
    for (const gen::ExcludedItem& x : excluded2) if (x.reason == "expansion not installed") ++filtered;
    const bool dlcOk = ok2 && atlantis > 0 && left3 == 0 && items2.size() + atlantis == items.size()
                       && filtered >= atlantis;
    std::printf("[test] %s: DLC filter, Atlantis marked absent: %zu of its %zu records left the catalogue, "
                "%zu candidates excluded as 'expansion not installed', %zu left\n",
                dlcOk ? "ok  " : "FAIL", items.size() - items2.size(), atlantis, filtered, left3);

    // EXCLUDE rule 7 both ways: by default the 14 records nobody can obtain (AE 2.10: six uniques
    // and eight monster infrequents, seven of them only in loot tables nothing reaches) are out,
    // each listed with its holder kinds; collect_unobtainable=1 puts exactly those back in, still
    // listed and marked kept, and every other line of the list stays as it was.
    std::size_t unobt = 0;
    for (const gen::ExcludedItem& x : excluded) {
        if (x.reason.compare(0, 14, "unobtainable: ") == 0 && !x.kept) ++unobt;
        std::printf("[test]       excluded %s: %s\n", x.record.c_str(), x.reason.c_str());
    }
    std::vector<gen::CatalogueItem> items3;
    std::vector<gen::ExcludedItem> excluded3;
    std::vector<std::string> warnings3;
    const bool ok3 = gen::collectItems(g, items3, excluded3, warnings3, &err, true, true);
    std::size_t kept = 0, same = 0;
    for (std::size_t i = 0; i < excluded3.size() && i < excluded.size(); ++i) {
        const gen::ExcludedItem& x = excluded3[i];
        if (x.kept && x.reason == excluded[i].reason + " (kept: collect_unobtainable=1)") ++kept;
        else if (!x.kept && x.reason == excluded[i].reason && x.record == excluded[i].record) ++same;
    }
    const bool flagOk = ok3 && unobt == 14 && kept == unobt && same + kept == excluded.size() &&
                        excluded3.size() == excluded.size() && items3.size() == items.size() + unobt;
    std::printf("[test] %s: collect_unobtainable: 0 leaves %zu unobtainable record(s) out (%zu items); "
                "1 keeps %zu of them (%zu items), the other %zu list lines unchanged\n",
                flagOk ? "ok  " : "FAIL", unobt, items.size(), kept, items3.size(), same);

    // The monster infrequents (collect_mi): the KIND mark is set exactly on the Rare equipment
    // records, never on a unique - and, independently of that expression, never on a record of
    // data\oracle\nomi\uniq-records.txt (the Python reference's uniques-only cut), with every
    // record outside that list marked; catalogue.bin carries it as flag bit 8 and sorts each group's
    // uniques first; collect_mi=0 gives the catalogue without them, byte for byte data\oracle\nomi
    // (the uniques alone, as before the monster infrequents).
    std::size_t miItems = 0, kindBad = 0, binMi = 0, binBad = 0, orderBad = 0;
    std::set<std::string> miRecords;
    for (const gen::CatalogueItem& it : items) {
        if (it.isMi) { ++miItems; miRecords.insert(it.record); }
        if (it.isMi != (it.isEquipment && it.classification == "Rare")) ++kindBad;
    }
    std::size_t nomiCount = 0, miInNomi = 0, uniqOut = 0;
    {
        std::vector<std::uint8_t> raw;
        std::set<std::string> nomiSet;
        const std::string list =
            oracle.substr(0, oracle.find_last_of("\\/") + 1) + "nomi\\uniq-records.txt";
        if (readAll(list, raw)) {
            std::string line;
            for (std::uint8_t ch : raw) {
                if (ch == '\n') {
                    if (!line.empty()) nomiSet.insert(line);
                    line.clear();
                } else if (ch != '\r') {
                    line.push_back(static_cast<char>(ch));
                }
            }
            if (!line.empty()) nomiSet.insert(line);
        }
        nomiCount = nomiSet.size();
        for (const gen::CatalogueItem& it : items) {
            const bool inNomi = nomiSet.count(it.record) != 0;
            if (it.isMi && inNomi) ++miInNomi;
            if (!it.isMi && !inNomi) ++uniqOut;
        }
    }
    {
        gdut::Catalogue c;
        if (c.loadFromMemory(bin, &err)) {
            for (std::size_t i = 0; i < c.itemCount(); ++i) {
                const gdut::ItemView e = c.item(i);
                const bool mi = gdut::catIsMi(e);
                binMi += mi ? 1 : 0;
                if (mi != (miRecords.count(std::string(e.record)) != 0)) ++binBad;
                if (i > 0) {
                    const gdut::ItemView p = c.item(i - 1);
                    if (p.group == e.group && gdut::catIsMi(p) && !mi) ++orderBad;
                }
            }
        } else {
            ++binBad;
        }
    }
    const bool kindOk = miItems == 1099 && st.mi == miItems && kindBad == 0 && binMi == miItems &&
                        binBad == 0 && orderBad == 0 && nomiCount != 0 && miInNomi == 0 &&
                        uniqOut == 0 && items.size() - nomiCount == miItems;
    std::printf("[test] %s: collect_mi=1: %zu monster infrequents in the catalogue (KIND mark on %zu "
                "packed entries, %zu wrong, %zu unique after an MI in its group; against the %zu "
                "records of data\\oracle\\nomi: %zu MI in that list, %zu unique outside it)\n",
                kindOk ? "ok  " : "FAIL", miItems, binMi, kindBad + binBad, orderBad, nomiCount,
                miInNomi, uniqOut);
    // The arcane formulae (Class ItemArtifactFormula): every one in the group blueprint with the
    // blueprint flag (catalogue.json's isBlueprint, catalogue.bin's flag bit 1) whatever its
    // classification, and the flag nowhere else; never default-visible; the sixteenth G line,
    // then the relic page's G line last (count 0, no E line after it).
    std::size_t bpItems = 0, bpBad = 0, binBp = 0, binBpBad = 0, gLines = 0, bpCommon = 0;
    std::string lastG;
    bool lastGEndsFile = false;
    for (const gen::CatalogueItem& it : items) {
        const bool f = it.cls == "ItemArtifactFormula";
        if (f) ++bpItems;
        if (f && it.classification == "Common") ++bpCommon;
        if (it.isBlueprint != f || f != (it.slot == "blueprint")) ++bpBad;
    }
    {
        gdut::Catalogue c;
        if (c.loadFromMemory(bin, &err)) {
            for (std::size_t i = 0; i < c.itemCount(); ++i) {
                const gdut::ItemView e = c.item(i);
                const bool b = e.has(gdut::ItemFlag::Blueprint);
                binBp += b ? 1 : 0;
                if (b != (e.group == gdut::SlotGroup::Blueprint) ||
                    (b && (e.has(gdut::ItemFlag::DefaultVisible) || e.footW != 2 || e.footH != 1)))
                    ++binBpBad;
            }
        } else {
            ++binBpBad;
        }
        std::vector<std::uint8_t> raw;
        if (readAll(oracle.substr(0, oracle.find_last_of("\\/") + 1) + "uniq-groups.txt", raw))
            for (std::size_t i = 0; i < raw.size(); ++i)
                if (raw[i] == 'G' && (i == 0 || raw[i - 1] == '\n') && i + 1 < raw.size() &&
                    raw[i + 1] == '\t') {
                    ++gLines;
                    std::size_t e = i;
                    while (e < raw.size() && raw[e] != '\r' && raw[e] != '\n') ++e;
                    lastG.assign(raw.begin() + std::ptrdiff_t(i), raw.begin() + std::ptrdiff_t(e));
                    lastGEndsFile = e + 2 == raw.size() && raw[e] == '\r';
                }
    }
    const bool relicLast = lastGEndsFile &&
                           lastG == std::string("G\t16\t") + utg::kRelicGroupLabel + "\t16\t15\t32\t32\t0\t" +
                                        utg::kRelicGroupId + "\t" + utg::kRelicGroupTag;
    const bool bpOk = bpItems == 93 && bpCommon == 76 && bpBad == 0 && binBp == bpItems &&
                      binBpBad == 0 && gLines == 17 && relicLast && items.size() == 2774;
    std::printf("[test] %s: arcane formulae: %zu in the catalogue (%zu Common packed as Epic), the "
                "blueprint flag on %zu packed entries, %zu wrong; %zu groups, then %s; %zu records\n",
                bpOk ? "ok  " : "FAIL", bpItems, bpCommon, binBp, bpBad + binBpBad,
                gLines - (relicLast ? 1 : 0),
                relicLast ? "the relic G line last (count 0, no E line)" : "NO relic G line last",
                items.size());
    std::vector<gen::CatalogueItem> items4;
    std::vector<gen::ExcludedItem> excluded4;
    std::vector<std::string> warnings4;
    std::vector<std::uint8_t> bin4, want4;
    gen::CatalogueStats st4;
    const bool ok4 = gen::collectItems(g, items4, excluded4, warnings4, &err, false, false) &&
                     gen::packCatalogue(items4, bin4, &st4, &err);
    const std::string nomi = oracle.substr(0, oracle.find_last_of("\\/") + 1) + "nomi\\catalogue.bin";
    const bool nomiOk = ok4 && st4.mi == 0 && items4.size() + miItems == items.size() &&
                        readAll(nomi, want4) && bin4 == want4;
    std::printf("[test] %s: collect_mi=0: %zu items, no monster infrequent, catalogue.bin byte-identical "
                "to %s\n", nomiOk ? "ok  " : "FAIL", items4.size(), nomi.c_str());
    return dlcOk && flagOk && kindOk && bpOk && nomiOk ? rc : 1;
}

// ---- uniq-relics.txt ---------------------------------------------------------------------------
// The relic and charm list's lines (relics_gen.h) counted, for the base game's checks.
struct RelicFileCounts {
    std::size_t relicB = 0, charmB = 0, r = 0, badR = 0, bonuses = 0, x = 0, xNerthus = 0, xFootprint = 0;
    std::size_t nerthusRows = 0, scarabRows = 0;
    bool scarabRelic = false, orderRelic = false;
};
RelicFileCounts relicFileCounts(const std::string& text) {
    RelicFileCounts c;
    std::size_t p = 0;
    while (p < text.size()) {
        std::size_t e = text.find("\r\n", p);
        if (e == std::string::npos) e = text.size();
        const std::string line = text.substr(p, e - p);
        p = e + 2;
        std::vector<std::string> f;
        for (std::size_t a = 0;;) {
            const std::size_t t = line.find('\t', a);
            f.push_back(line.substr(a, t == std::string::npos ? std::string::npos : t - a));
            if (t == std::string::npos) break;
            a = t + 1;
        }
        if (f[0] == "B" && f.size() == 5) {
            const bool relic = f[3] == "R";
            ++(relic ? c.relicB : c.charmB);
            const std::size_t rows = std::strtoul(f[4].c_str(), nullptr, 10);
            if (f[2] == "records/xpack2/item/relics/act5_nerthusmistletoe") c.nerthusRows = rows;
            if (f[2] == "records/item/hcdungeon/05_x4_goldenscarab_charm") {
                c.scarabRows = rows;
                c.scarabRelic = relic;
            }
            if (f[2] == "records/item/hcdungeon/x4_essenceoforder_charm") c.orderRelic = relic;
        } else if (f[0] == "R" && f.size() >= 7) {
            ++c.r;
            const std::size_t n = std::strtoul(f[6].c_str(), nullptr, 10);
            c.bonuses += n;
            if (f.size() != 7 + n) ++c.badR;
        } else if (f[0] == "X" && f.size() == 3) {
            ++c.x;
            if (f[1].find("_act5_nerthusmistletoe.dbr") != std::string::npos && f[2].compare(0, 14, "unobtainable: ") == 0)
                ++c.xNerthus;
            if (f[2].compare(0, 10, "footprint ") == 0) ++c.xFootprint;
        } else {
            ++c.badR;
        }
    }
    return c;
}

// The blocks, the tiers and the order of uniq-relics.txt (relics_gen.h), no game needed.
int relicUnits() {
    int rc = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("[test] %s: %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) rc = 1;
    };
    std::string id;
    check(gen::relicBlockOf("records/item/relics/02_act1_aegisofathena.dbr", &id) == 2 &&
              id == "records/item/relics/act1_aegisofathena",
          "relics: 0N_<stem> is tier N of the block folder/<stem>");
    check(gen::relicBlockOf("records/item/hcdungeon/05_x4_goldenscarab_l_charm.dbr", &id) == 3 &&
              id == "records/item/hcdungeon/05_x4_goldenscarab_charm",
          "relics: <stem>_l_<rest> is tier 3 of the block <stem><rest> (the Golden Scarab)");
    check(gen::relicBlockOf("records/item/formulaitems/relic/f_relic_003_d2.dbr", &id) == 2 &&
              id == "records/item/formulaitems/relic/f_relic_003",
          "relics: <stem>_d2 is tier 2 of the block <stem>");
    check(gen::relicBlockOf("records/a/x_n_y_e.dbr", &id) == 2 && id == "records/a/x_n_y",
          "relics: the last _n / _e / _l mark decides (the longest stem)");
    check(gen::relicBlockOf("records/xpack4/item/charms/x4_aspectofchi.dbr", &id) == 0 &&
              id == "records/xpack4/item/charms/x4_aspectofchi",
          "relics: a basename with no tier mark is tier 0, a block of its own");
    check(gen::relicBlockOf("records/a/04_x.dbr", &id) == 0 && id == "records/a/04_x",
          "relics: 04_ is no tier mark");
    check(gen::relicExpansionOf("records/item/hcdungeon/05_x4_goldenscarab_n_charm.dbr") == 4 &&
              gen::relicExpansionOf("records/xpack2/item/relics/01_act5_nerthusmistletoe.dbr") == 2 &&
              gen::relicExpansionOf("records/xpack/item/relics/01_act4_x.dbr") == 1 &&
              gen::relicExpansionOf("records/item/relics/01_act1_aegisofathena.dbr") == 0,
          "relics: x4_ (after a 0N_) is Eternal Embers, else the path's expansion");
    std::vector<gen::RelicItem> rs(4);
    auto mk = [](gen::RelicItem& r, const char* rec, const char* cls, int lvl, int shards,
                 std::vector<std::string> b) {
        r.item.record = rec;
        r.item.cls = cls;
        r.item.levelRequirement = lvl;
        r.item.bitmap = "i.tex";
        r.shardBitmap = "s.tex";
        r.shards = shards;
        r.bonuses = std::move(b);
    };
    mk(rs[0], "records/c/01_bat.dbr", "ItemCharm", 1, 5, {"B1"});
    mk(rs[1], "Records/R/02_Aegis.dbr", "ItemRelic", 30, 3, {"B2", "Records\\B3"});
    mk(rs[2], "records/r/01_aegis.dbr", "ItemRelic", 10, 3, {"B4"});
    mk(rs[3], "records/r/x4_ember.dbr", "ItemRelic", 1, 3, {});
    const std::vector<gen::ExcludedItem> xs = {
        {"records/r/03_aegis.dbr", "unobtainable: only in loot tables nothing reaches", false}};
    gen::RelicListCounts n;
    const std::string text = gen::relicsListText(rs, xs, &n);
    check(text == "B\t1\trecords/r/aegis\tR\t2\r\n"
                  "R\trecords/r/01_aegis.dbr\t1\t3\ti.tex\ts.tex\t1\tB4\r\n"
                  "R\tRecords/R/02_Aegis.dbr\t2\t3\ti.tex\ts.tex\t2\tB2\tRecords\\B3\r\n"
                  "B\t2\trecords/r/x4_ember\tR\t1\r\n"
                  "R\trecords/r/x4_ember.dbr\t0\t3\ti.tex\ts.tex\t0\r\n"
                  "B\t3\trecords/c/bat\tC\t1\r\n"
                  "R\trecords/c/01_bat.dbr\t1\t5\ti.tex\ts.tex\t1\tB1\r\n"
                  "X\trecords/r/03_aegis.dbr\tunobtainable: only in loot tables nothing reaches\r\n",
          "relics: relic blocks before charm blocks, by expansion, then level; tiers in order; X lines last");
    check(n.relicBlocks == 2 && n.charmBlocks == 1 && n.oneLineBlocks == 2 && n.rows == 4 && n.bonuses == 4 &&
              n.left == 1,
          "relics: the counts of the list");
    {   // a record with no tier mark beside tiered records of its stem: one block, the tier-0 row first
        std::vector<gen::RelicItem> ms(2);
        mk(ms[0], "records/m/01_foo.dbr", "ItemRelic", 5, 3, {"B1"});
        mk(ms[1], "records/m/foo.dbr", "ItemRelic", 1, 3, {"B2"});
        gen::RelicListCounts mn;
        check(gen::relicsListText(ms, {}, &mn) == "B\t1\trecords/m/foo\tR\t2\r\n"
                                                  "R\trecords/m/foo.dbr\t0\t3\ti.tex\ts.tex\t1\tB2\r\n"
                                                  "R\trecords/m/01_foo.dbr\t1\t3\ti.tex\ts.tex\t1\tB1\r\n" &&
                  mn.relicBlocks == 1 && mn.oneLineBlocks == 0 && mn.rows == 2,
              "relics: a tier-0 record joins the block of its stem's tiered records, its row first");
    }
    return rc;
}

// ---- --ensure ----------------------------------------------------------------------------------
const char* const kNames[4] = {"catalogue.bin", "uniq-records.txt", "uniq-groups.txt", "uniq-excluded.txt"};
// The fifth output, the caravan geometry (src\gen\ui_geometry.h): the base game derives the grid the
// mod has always measured, 27,126 512 x 480 = 16 x 15 cells, under the vanilla cover.
const char* const kGeometryName = "uniq-geometry.txt";
const char kBaseGeometry[] = "P\t27\t126\t512\t480\t565\t521\tXPack\\UI\\Caravan\\InventoryTransferCover01.tex\r\n"
                             "W\t565\t637\t0\t126\t0\t0\r\n";

// uniq-geometry.txt -> its P and W fields (P: x0 y0 usableW usableH plateW plateH; W: windowW windowH
// pageX pageY [bgX bgY]); false when a line is missing or short. *bg = the W line has the
// background's place.
bool readGeometry(const std::string& path, int p[6], int w[6], bool* bg) {
    std::vector<std::uint8_t> raw;
    if (!readAll(path, raw)) return false;
    const std::string t(raw.begin(), raw.end());
    bool haveP = false, haveW = false;
    for (std::size_t at = 0; at < t.size();) {
        std::size_t nl = t.find('\n', at);
        if (nl == std::string::npos) nl = t.size();
        const std::string line = t.substr(at, nl - at);
        if (line.compare(0, 2, "P\t") == 0)
            haveP = std::sscanf(line.c_str() + 2, "%d\t%d\t%d\t%d\t%d\t%d", &p[0], &p[1], &p[2], &p[3], &p[4],
                                &p[5]) == 6;
        if (line.compare(0, 2, "W\t") == 0) {
            const int n = std::sscanf(line.c_str() + 2, "%d\t%d\t%d\t%d\t%d\t%d", &w[0], &w[1], &w[2], &w[3],
                                      &w[4], &w[5]);
            haveW = n >= 4;
            *bg = n == 6;
        }
        at = nl + 1;
    }
    return haveP && haveW;
}

int ensure(const std::string& dataDir, const std::string& modDir) {
    std::string game = gameDir();
    if (game.empty()) return fail("no Titan Quest AE folder: set TQ_GAME_DIR");
    const std::string stamp = modDir + "\\catalogue.stamp";
    for (const char* n : kNames) DeleteFileA((modDir + "\\" + n).c_str());
    DeleteFileA((modDir + "\\" + kGeometryName).c_str());
    DeleteFileA(stamp.c_str());
    int rc = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("[test] %s: %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) rc = 1;
    };
    auto noTemporaries = [&]() {
        for (const char* n : kNames)
            if (GetFileAttributesA((modDir + "\\" + n + ".tmp").c_str()) != INVALID_FILE_ATTRIBUTES) return false;
        return true;
    };
    gen::GenReport r;

    // a: first launch
    auto t0 = std::chrono::steady_clock::now();
    bool ok = gen::ensureOutputsGuarded(game, modDir, "EN", r);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    double peakMb = peakWorkingSetMb();
    std::printf("[test] ensure (first launch): %s, reason \"%s\", %lld ms wall, peak %.1f MB - %s%s\n",
                ok ? "generated" : "FAILED", r.reason.c_str(), (long long)ms, peakMb,
                r.summary.c_str(), r.error.c_str());
    for (const std::string& w : r.warnings) std::printf("[test] warning: %s\n", w.c_str());
    check(ok && r.generated && r.reason == "catalogue.bin is missing", "a first launch generates, reason = the missing catalogue");
    check(peakMb > 0.0 && peakMb <= kPeakBudgetMb, "the generation stays inside the peak-memory budget");
    for (int i = 0; i < 4; ++i) {
        std::vector<std::uint8_t> got, want;
        if (!readAll(modDir + "\\" + kNames[i], got) || !readAll(dataDir + "\\oracle\\" + kNames[i], want)) {
            check(false, kNames[i]);
            continue;
        }
        if (compareBytes(kNames[i], got, want)) rc = 1;
    }
    {   // uniq-relics.txt: the oracle's bytes and the base game's relic and charm counts
        std::vector<std::uint8_t> got, want;
        const bool read = readAll(modDir + "\\uniq-relics.txt", got) &&
                          readAll(dataDir + "\\oracle\\uniq-relics.txt", want);
        check(read, "uniq-relics.txt written, the oracle's copy read");
        if (read && compareBytes("uniq-relics.txt", got, want)) rc = 1;
        const RelicFileCounts c = relicFileCounts(std::string(got.begin(), got.end()));
        std::printf("[test] relics: %zu blocks (%zu relic, %zu charm), %zu rows, %zu bonus slots, %zu X lines; "
                    "%zu gray relic icons, %zu failed\n", c.relicB + c.charmB, c.relicB, c.charmB, c.r,
                    c.bonuses, c.x, r.grayRelicWritten, r.grayRelicFailed);
        check(c.relicB == 53 && c.charmB == 40 && c.badR == 0, "relics: 93 B lines, 53 relic and 40 charm blocks");
        check(c.r == 253 && c.bonuses == 1971, "relics: 253 R lines, 1,971 bonus slots");
        check(c.x == 2 && c.xNerthus == 2 && c.xFootprint == 0,
              "relics: two X lines, the Epic and Legendary Power of Nerthus (unobtainable); every icon 1 x 1");
        check(c.nerthusRows == 1, "relics: the Power of Nerthus is a one-line block (its Normal tier)");
        check(c.scarabRows == 3 && c.scarabRelic,
              "relics: the Golden Scarab's three records joined by the _n / _e / _l rule, in the relic part");
        check(c.orderRelic, "relics: the Essence of Order in the relic part (the class decides)");
        check(r.relicRows == 253 && r.relicBlocks == 93 && r.relicLeft == 2, "relics: the report counts the list");
        check(r.grayRelicWritten > 250 && r.grayRelicFailed == 0,
              "relics: a gray copy of every relicBitmap and shardBitmap the list names");
    }
    {   // the caravan geometry: the base game derives exactly the grid the mod has always measured
        std::vector<std::uint8_t> got;
        const bool read = readAll(modDir + "\\" + kGeometryName, got);
        const std::string text(got.begin(), got.end());
        std::printf("[test] geometry: %s\n", r.geometry.c_str());
        check(read && text == kBaseGeometry,
              "uniq-geometry.txt: the base game derives 27,126 512 x 480 (16 x 15) under the vanilla cover");
        check(r.geometry.compare(0, 41, "grid 27,126 512x480 = 16 x 15 cells of 32") == 0 &&
                  r.geometry.find("leaves the caravan window") == std::string::npos,
              "the geometry note names the base grid, inside the caravan window");
        check(GetFileAttributesA((modDir + "\\" + kGeometryName + ".tmp").c_str()) == INVALID_FILE_ATTRIBUTES,
              "no uniq-geometry.txt.tmp left behind");
    }
    check(GetFileAttributesA(stamp.c_str()) != INVALID_FILE_ATTRIBUTES, "catalogue.stamp written");
    check(noTemporaries(), "no .tmp left behind");
    {   // one gray copy per record in gray\, every texel R = G = B (the 32 bpp icons)
        std::printf("[test] gray icons: %zu written (%.1f MB), %zu failed\n", r.grayWritten,
                    (double)r.grayBytes / 1048576.0, r.grayFailed);
        check(r.grayWritten == r.records && r.grayFailed == 0 && r.records > 1000,
              "a gray copy of every record's icon");
        check(GetFileAttributesA((modDir + "\\gray\\gray.stamp").c_str()) != INVALID_FILE_ATTRIBUTES,
              "gray\\gray.stamp written");
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA((modDir + "\\gray\\ug*.tex").c_str(), &fd);
        std::size_t files = 0, grayFiles = 0;
        if (h != INVALID_HANDLE_VALUE) {
            do {
                ++files;
                std::vector<std::uint8_t> d;
                if (files > 50 || !readAll(modDir + "\\gray\\" + fd.cFileName, d) || d.size() < 16 + 128) continue;
                const std::size_t off = d[3] == 1 ? 12 : 13, at = off + 4 + 124;
                bool gray = d.size() > at;
                for (std::size_t k = at; gray && k + 4 <= d.size(); k += 4) gray = d[k] == d[k + 1] && d[k + 1] == d[k + 2];
                if (gray) ++grayFiles;
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
        std::printf("[test] gray icons: %zu ug*.tex file(s) in gray\\, %zu of the first 50 read gray\n", files,
                    grayFiles);
        check(files >= r.records && grayFiles == 50, "the files are there, and every texel read is gray");
    }
    unsigned long long mt[4];
    for (int i = 0; i < 4; ++i) mt[i] = mtimeOf(modDir + "\\" + kNames[i]);

    // b: second launch - the stamp matches
    t0 = std::chrono::steady_clock::now();
    ok = gen::ensureOutputsGuarded(game, modDir, "EN", r);
    ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    std::printf("[test] ensure (second launch): upToDate=%d in %lld ms\n", int(r.upToDate), (long long)ms);
    check(ok && r.upToDate && !r.generated, "a matching stamp does nothing");
    bool untouched = true;
    for (int i = 0; i < 4; ++i) untouched = untouched && mtimeOf(modDir + "\\" + kNames[i]) == mt[i];
    check(untouched, "the four files were not rewritten");

    {   // a gray folder that cannot be made (here a FILE named gray) is recorded in
        // catalogue.stamp, and the next launch does not regenerate for it
        const std::string l3Dir = modDir + "\\gray", l3Moved = modDir + "\\gray-moved";
        const bool l3MovedOk = MoveFileA(l3Dir.c_str(), l3Moved.c_str()) != 0;
        { FILE* f = std::fopen(l3Dir.c_str(), "wb"); if (f) { std::fputs("not a folder\n", f); std::fclose(f); } }
        { FILE* f = std::fopen(stamp.c_str(), "wb"); if (f) { std::fputs("GDUT-STAMP 0\nlang=EN\nold\n", f); std::fclose(f); } }
        ok = gen::ensureOutputsGuarded(game, modDir, "EN", r);
        std::vector<std::uint8_t> l3Stamp;
        readAll(stamp, l3Stamp);
        const std::string l3Text(l3Stamp.begin(), l3Stamp.end());
        std::printf("[test] gray is a file - generated=%d, %zu gray written, %zu failed, stamp %s the tag\n",
                    int(r.generated), r.grayWritten, r.grayFailed,
                    l3Text.find("gray: the gray folder could not be written") != std::string::npos ? "carries" : "LACKS");
        check(l3MovedOk && ok && r.generated &&
                  l3Text.find("gray: the gray folder could not be written") != std::string::npos,
              "a gray folder that cannot be made is recorded in catalogue.stamp");
        ok = gen::ensureOutputsGuarded(game, modDir, "EN", r);
        check(ok && r.upToDate && !r.generated,
              "... and the next launch does not regenerate for it");
        DeleteFileA(l3Dir.c_str());
        check(MoveFileA(l3Moved.c_str(), l3Dir.c_str()) != 0, "the gray folder is put back");
    }

    // c: a stamp that differs (a game update)
    { FILE* f = std::fopen(stamp.c_str(), "wb"); if (f) { std::fputs("GDUT-STAMP 0\nlang=EN\nold\n", f); std::fclose(f); } }
    ok = gen::ensureOutputsGuarded(game, modDir, "EN", r);
    check(ok && r.generated && r.reason.find("does not match") != std::string::npos, "a changed stamp regenerates");

    // d: one output missing
    DeleteFileA((modDir + "\\uniq-excluded.txt").c_str());
    ok = gen::ensureOutputsGuarded(game, modDir, "EN", r);
    check(ok && r.generated && r.reason == "uniq-excluded.txt is missing", "a missing output regenerates");
    for (int i = 0; i < 4; ++i) mt[i] = mtimeOf(modDir + "\\" + kNames[i]);
    std::vector<std::uint8_t> stampBefore;
    readAll(stamp, stampBefore);

    // e: a game folder without archives - the error path keeps everything
    ok = gen::ensureOutputsGuarded(modDir, modDir, "EN", r);
    std::printf("[test] ensure (no archives): %s\n", r.error.c_str());
    check(!ok && !r.generated && !r.error.empty(), "a game folder without archives is an error");
    untouched = true;
    for (int i = 0; i < 4; ++i) untouched = untouched && mtimeOf(modDir + "\\" + kNames[i]) == mt[i];
    std::vector<std::uint8_t> stampAfter;
    readAll(stamp, stampAfter);
    check(untouched && stampAfter == stampBefore, "the files and the stamp are kept on failure");
    check(noTemporaries(), "no .tmp left behind by the failure");

    // f: a language whose text archive does not exist is an error, not a guess
    DeleteFileA(stamp.c_str());
    ok = gen::ensureOutputsGuarded(game, modDir, "XX", r);
    std::printf("[test] ensure (Text_XX): %s\n", r.error.c_str());
    check(!ok && r.error.find("Text_XX") != std::string::npos, "a missing Text_<lang>.arc is an error");

    // g: collect_unobtainable flips: each regenerates once and the reason names the key; back at 0
    //    the four files are the oracle's again
    ok = gen::ensureOutputsGuarded(game, modDir, "EN", r);
    check(ok && r.generated, "the default set is generated again");
    gen::GenInputs on;
    on.collectUnobtainable = true;
    ok = gen::ensureOutputsGuarded(game, modDir, "EN", r, nullptr, nullptr, on);
    std::printf("[test] ensure (collect_unobtainable=1): reason \"%s\"\n", r.reason.c_str());
    check(ok && r.generated && r.reason.compare(0, 33, "collect_unobtainable changed to 1") == 0,
          "collect_unobtainable=1 regenerates, and the reason names the key");
    {
        std::vector<std::uint8_t> exc, recs;
        readAll(modDir + "\\uniq-excluded.txt", exc);
        readAll(modDir + "\\uniq-records.txt", recs);
        const std::string e(exc.begin(), exc.end());
        std::size_t keptLines = 0, lines = 0;
        for (std::size_t p = e.find("(kept: collect_unobtainable=1)"); p != std::string::npos;
             p = e.find("(kept: collect_unobtainable=1)", p + 1)) ++keptLines;
        for (std::uint8_t c : recs) if (c == '\n') ++lines;
        std::printf("[test] ensure (collect_unobtainable=1): %zu records, %zu kept lines\n", lines, keptLines);
        check(keptLines == 14 && lines == 2788,
              "collect_unobtainable=1: the 14 are in uniq-records.txt and listed as kept");
    }
    ok = gen::ensureOutputsGuarded(game, modDir, "EN", r, nullptr, nullptr, on);
    check(ok && r.upToDate && !r.generated, "collect_unobtainable=1 again: the stamp matches");
    ok = gen::ensureOutputsGuarded(game, modDir, "EN", r);
    std::printf("[test] ensure (collect_unobtainable=0): reason \"%s\"\n", r.reason.c_str());
    check(ok && r.generated && r.reason.compare(0, 33, "collect_unobtainable changed to 0") == 0,
          "back to collect_unobtainable=0 regenerates, and the reason names the key");
    for (const char* n : kNames) {
        std::vector<std::uint8_t> got, want;
        readAll(modDir + "\\" + n, got);
        readAll(dataDir + "\\oracle\\" + n, want);
        check(!got.empty() && got == want, (std::string("back at 0: ") + n + " is the oracle's again").c_str());
    }
    check(gen::stampMismatch("GDUT-STAMP 3\nlang=EN\ncollect_unobtainable=1\na 1 2\n",
                             "GDUT-STAMP 3\nlang=EN\na 1 3\n") ==
              "collect_unobtainable changed to 0 (the records nobody can obtain are left out of the "
              "catalogue), and the game files changed",
          "a flip with a game update names both");
    check(gen::stampMismatch(std::string("GDUT-STAMP 3\nlang=EN\na 1 2\n") +
                                 "\ngray: the gray folder could not be written\n",
                             "GDUT-STAMP 3\nlang=EN\ncollect_unobtainable=1\na 1 2\n") ==
              "collect_unobtainable changed to 1 (the records nobody can obtain are put back in the "
              "catalogue)",
          "a flip after a gray-folder failure names only the key");

    // h: collect_mi flips: 0 regenerates once, the reason names the key, and the four files are
    //    data\oracle\nomi's (the uniques alone); back at 1 they are the oracle's again
    gen::GenInputs noMi;
    noMi.collectMi = false;
    ok = gen::ensureOutputsGuarded(game, modDir, "EN", r, nullptr, nullptr, noMi);
    std::printf("[test] ensure (collect_mi=0): reason \"%s\" - %s\n", r.reason.c_str(), r.summary.c_str());
    check(ok && r.generated && r.reason.compare(0, 23, "collect_mi changed to 0") == 0,
          "collect_mi=0 regenerates, and the reason names the key");
    for (const char* n : kNames) {
        std::vector<std::uint8_t> got, want;
        readAll(modDir + "\\" + n, got);
        readAll(dataDir + "\\oracle\\nomi\\" + n, want);
        check(!got.empty() && got == want, (std::string("collect_mi=0: ") + n + " is data\\oracle\\nomi's").c_str());
    }
    ok = gen::ensureOutputsGuarded(game, modDir, "EN", r, nullptr, nullptr, noMi);
    check(ok && r.upToDate && !r.generated, "collect_mi=0 again: the stamp matches");
    ok = gen::ensureOutputsGuarded(game, modDir, "EN", r);
    std::printf("[test] ensure (collect_mi=1): reason \"%s\" - %s\n", r.reason.c_str(), r.summary.c_str());
    check(ok && r.generated && r.reason.compare(0, 23, "collect_mi changed to 1") == 0 &&
              r.summary.find("(collect_mi=1: 1099 monster infrequent record(s) in the catalogue)") != std::string::npos,
          "back to collect_mi=1 regenerates, the reason names the key, the summary counts the MIs");
    for (const char* n : kNames) {
        std::vector<std::uint8_t> got, want;
        readAll(modDir + "\\" + n, got);
        readAll(dataDir + "\\oracle\\" + n, want);
        check(!got.empty() && got == want, (std::string("back at collect_mi=1: ") + n + " is the oracle's again").c_str());
    }
    // h2: collect_relics flips: 0 regenerates once (the reason names the key): uniq-relics.txt lists
    //     nothing, uniq-groups.txt has no relic G line, no gray relic icon is written, and the
    //     collection's files are the oracle's; back at 1 the oracle's uniq-relics.txt and G line
    gen::GenInputs noRel;
    noRel.collectRelics = false;
    ok = gen::ensureOutputsGuarded(game, modDir, "EN", r, nullptr, nullptr, noRel);
    std::printf("[test] ensure (collect_relics=0): reason \"%s\" - %s\n", r.reason.c_str(), r.summary.c_str());
    check(ok && r.generated && r.reason.compare(0, 27, "collect_relics changed to 0") == 0 &&
              r.summary.find("(collect_relics=0: no relic page)") != std::string::npos,
          "collect_relics=0 regenerates, the reason names the key, the summary says it");
    check(r.relicRows == 0 && r.relicBlocks == 0 && r.relicLeft == 0 && r.grayRelicWritten == 0 &&
              r.grayRelicFailed == 0,
          "collect_relics=0: no relic row, no relic gray icon written");
    {
        std::vector<std::uint8_t> got, want;
        check(readAll(modDir + "\\uniq-relics.txt", got) && got.empty(), "collect_relics=0: uniq-relics.txt lists nothing");
        readAll(modDir + "\\uniq-groups.txt", got);
        readAll(dataDir + "\\oracle\\uniq-groups.txt", want);
        const std::string g(got.begin(), got.end()), w(want.begin(), want.end());
        const std::size_t relG = w.rfind("\nG\t");
        const std::string wNoRel = relG == std::string::npos ? w : w.substr(0, relG + 1);
        check(relG != std::string::npos && w.find("\trelics\tREL\r\n", relG) != std::string::npos && g == wNoRel &&
                  g.find("\trelics\t") == std::string::npos,
              "collect_relics=0: uniq-groups.txt is the oracle's without its last line, the relic G line");
        for (const char* n : {"catalogue.bin", "uniq-records.txt", "uniq-excluded.txt"}) {
            readAll(modDir + "\\" + n, got);
            readAll(dataDir + "\\oracle\\" + n, want);
            check(!got.empty() && got == want, (std::string("collect_relics=0: ") + n + " is the oracle's").c_str());
        }
    }
    ok = gen::ensureOutputsGuarded(game, modDir, "EN", r, nullptr, nullptr, noRel);
    check(ok && r.upToDate && !r.generated, "collect_relics=0 again: the stamp matches");
    ok = gen::ensureOutputsGuarded(game, modDir, "EN", r);
    std::printf("[test] ensure (collect_relics=1): reason \"%s\" - %s\n", r.reason.c_str(), r.summary.c_str());
    check(ok && r.generated && r.reason.compare(0, 27, "collect_relics changed to 1") == 0 && r.relicRows == 253 &&
              r.summary.find("collect_relics=0") == std::string::npos,
          "back to collect_relics=1 regenerates, the reason names the key, the 253 relic rows again");
    for (const char* n : {"catalogue.bin", "uniq-records.txt", "uniq-groups.txt", "uniq-excluded.txt", "uniq-relics.txt"}) {
        std::vector<std::uint8_t> got, want;
        readAll(modDir + "\\" + n, got);
        readAll(dataDir + "\\oracle\\" + n, want);
        check(!got.empty() && got == want, (std::string("back at collect_relics=1: ") + n + " is the oracle's again").c_str());
    }
    check(gen::stampMismatch("GDUT-STAMP 13\nlang=EN\ncollect_mi=1\ncollect_relics=1\na 1 2\n",
                             "GDUT-STAMP 13\nlang=EN\ncollect_mi=1\na 1 2\n") ==
              "collect_relics changed to 0 (no relic page: the relics and charms are left out)",
          "the stamp names a collect_relics flip");
    check(gen::stampMismatch("GDUT-STAMP 4\nlang=EN\ncollect_mi=1\na 1 2\n",
                             "GDUT-STAMP 4\nlang=EN\ncollect_unobtainable=1\na 1 2\n") ==
              "collect_mi changed to 0 (the monster infrequents are left out of the catalogue), and "
              "collect_unobtainable changed to 1 (the records nobody can obtain are put back in the "
              "catalogue)",
          "two flips at once name both keys");
    check(gen::stampMismatch("GDUT-STAMP 7\nlang=EN\ngroups=builtin:1\na 1 2\n",
                             "GDUT-STAMP 7\nlang=EN\ngroups=builtin:1+user:2\na 1 2\n") ==
              "the groups changed (uniquetab-groups.ini or the built-in groups)",
          "a changed groups line names the groups");

    // i: the player's uniquetab-groups.ini (README "Your own groups", example 2): read, never written;
    //    its page comes first; an edit regenerates once; a refused file is one warning and the
    //    built-in groups (the oracle's files again); no file, the oracle's files
    {
        const std::string gpath = modDir + "\\uniquetab-groups.ini";
        const std::string mine = "; mine\r\n[group embers]\r\nlabel = Eternal Embers\r\ntag   = EMB\r\n"
                                 "path  = records/xpack4/**\r\n";
        { FILE* f = std::fopen(gpath.c_str(), "wb"); if (f) { std::fputs(mine.c_str(), f); std::fclose(f); } }
        gen::GenInputs user;
        user.groupsPath = gpath;
        ok = gen::ensureOutputsGuarded(game, modDir, "EN", r, nullptr, nullptr, user);
        std::printf("[test] ensure (a user groups file): reason \"%s\" - %s\n", r.reason.c_str(), r.groupsLine.c_str());
        check(ok && r.generated && r.reason == "the groups changed (uniquetab-groups.ini or the built-in groups)",
              "a user groups file regenerates once, and the reason names the groups");
        check(r.groupsLine == "groups from " + gpath + ": 26 built-in + 1 of yours (0 built-in group(s) given more rules), 17 group(s)",
              "the groups line: 26 built-in (a custom game's own group among them) + 1 of yours, 17 groups");
        std::vector<std::uint8_t> g;
        readAll(modDir + "\\uniq-groups.txt", g);
        const std::string gt(g.begin(), g.end());
        check(gt.rfind("G\t0\tEternal Embers\t", 0) == 0 && gt.find("\tembers\tEMB\r\n") != std::string::npos,
              "the player's page is the first group: G 0 Eternal Embers ... embers EMB");
        std::vector<std::uint8_t> bin, binWant;
        readAll(modDir + "\\catalogue.bin", bin);
        readAll(dataDir + "\\oracle\\catalogue.bin", binWant);
        check(!bin.empty() && bin == binWant, "catalogue.bin does not depend on the groups");
        std::vector<std::uint8_t> back;
        readAll(gpath, back);
        check(std::string(back.begin(), back.end()) == mine, "the generator never writes the player's file");
        ok = gen::ensureOutputsGuarded(game, modDir, "EN", r, nullptr, nullptr, user);
        check(ok && r.upToDate && !r.generated, "the same file again: the stamp matches");
        { FILE* f = std::fopen(gpath.c_str(), "wb"); if (f) { std::fputs("[group mine]\nclass = X\ntag = MAT\n", f); std::fclose(f); } }
        ok = gen::ensureOutputsGuarded(game, modDir, "EN", r, nullptr, nullptr, user);
        const std::string warn = r.warnings.empty() ? std::string() : r.warnings[0];
        std::printf("[test] ensure (a refused groups file): %s\n", warn.c_str());
        check(ok && r.generated && warn == "uniquetab-groups.ini is ignored - " + gpath +
                  ": line 3: the tag MAT is the pad's own; the built-in groups are used",
              "a refused file is ONE warning naming the file and the line, and the built-in groups");
        for (const char* n : kNames) {
            std::vector<std::uint8_t> got, want;
            readAll(modDir + "\\" + n, got);
            readAll(dataDir + "\\oracle\\" + n, want);
            check(!got.empty() && got == want, (std::string("refused groups file: ") + n + " is the oracle's").c_str());
        }
        ok = gen::ensureOutputsGuarded(game, modDir, "EN", r, nullptr, nullptr, user);
        check(ok && r.upToDate && r.warnings.size() == 1, "the refused file again: up to date, the warning said again");
        std::remove(gpath.c_str());
        ok = gen::ensureOutputsGuarded(game, modDir, "EN", r, nullptr, nullptr, user);
        check(ok && r.upToDate && r.warnings.empty() && r.groupsLine == "the built-in groups (26)",
              "the file removed: up to date (the stamp is the built-in one), no warning");
    }
    return rc;
}

// ---- --ensure-flag -----------------------------------------------------------------------------
// collect_unobtainable=1 into a fresh folder, for tools\build_test_catalogue.bat to compare with
// tools\build_catalogue.py --collect-unobtainable.
int ensureFlag(const std::string& modDir) {
    std::string game = gameDir();
    if (game.empty()) return fail("no Titan Quest AE folder: set TQ_GAME_DIR");
    for (const char* n : kNames) DeleteFileA((modDir + "\\" + n).c_str());
    DeleteFileA((modDir + "\\catalogue.stamp").c_str());
    gen::GenReport r;
    gen::GenInputs on;
    on.collectUnobtainable = true;
    const bool ok = gen::ensureOutputsGuarded(game, modDir, "EN", r, nullptr, nullptr, on);
    std::printf("[test] %s: collect_unobtainable=1 generated into %s - %s\n", ok && r.generated ? "ok  " : "FAIL",
                modDir.c_str(), ok ? r.summary.c_str() : r.error.c_str());
    std::vector<std::uint8_t> got;
    const RelicFileCounts c = readAll(modDir + "\\uniq-relics.txt", got)
                                  ? relicFileCounts(std::string(got.begin(), got.end()))
                                  : RelicFileCounts();
    const bool relicsOk = c.nerthusRows == 3 && c.x == 2 && c.r == 255 && c.relicB + c.charmB == 93;
    std::printf("[test] %s: collect_unobtainable=1: the Power of Nerthus is a three-line block, its two "
                "kept records still X lines (%zu rows)\n", relicsOk ? "ok  " : "FAIL", c.r);
    return ok && r.generated && relicsOk ? 0 : 1;
}

// ---- --peak ------------------------------------------------------------------------------------
int peak(const std::string& modDir) {
    std::string game = gameDir();
    if (game.empty()) return fail("no Titan Quest AE folder: set TQ_GAME_DIR");
    for (const char* n : kNames) DeleteFileA((modDir + "\\" + n).c_str());
    DeleteFileA((modDir + "\\catalogue.stamp").c_str());
    const double before = peakWorkingSetMb();
    gen::GenReport r;
    auto t0 = std::chrono::steady_clock::now();
    const bool ok = gen::ensureOutputsGuarded(game, modDir, "EN", r);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    const double after = peakWorkingSetMb();
    std::printf("[test] peak: %s in %lld ms, process peak %.1f MB (%.1f MB before the generation), "
                "peak commit %.1f MB, budget %.0f MB - %s\n",
                ok ? "generated" : "FAILED", (long long)ms, after, before, peakCommitMb(), kPeakBudgetMb,
                ok ? r.summary.c_str() : r.error.c_str());
    if (!ok || !r.generated) return fail("the generation did not run");
    if (after > kPeakBudgetMb) return fail("the generation peaked over the 200 MB budget");
    std::printf("[test] ok  : the generation stays inside the 200 MB peak budget\n");
    return 0;
}

// ---- --game-view -------------------------------------------------------------------------------
// One custom game's set into outDir, in a process of its own (the peak is this generation's alone):
// tools\test_gameview.py runs it once per game found under TQ_TEST_MODS_DIR and compares the four
// files with tools\build_catalogue.py --game-mod. Prints the [measure] lines it tabulates.
int gameView(const std::string& root, const std::string& name, const std::string& outDir) {
    std::string game = gameDir();
    if (game.empty()) return fail("no Titan Quest AE folder: set TQ_GAME_DIR");
    gen::GameSpec spec;
    std::string why;
    if (!ut::utGameLocate(root, name, spec, &why)) return fail(why.c_str());
    int rc = 0;
    auto check = [&](bool ok, const std::string& what) {
        std::printf("[test] %s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
        if (!ok) rc = 1;
    };
    CreateDirectoryA(outDir.c_str(), nullptr);
    for (const char* n : kNames) DeleteFileA((outDir + "\\" + n).c_str());
    DeleteFileA((outDir + "\\" + kGeometryName).c_str());
    DeleteFileA((outDir + "\\catalogue.stamp").c_str());
    gen::GenReport r;
    gen::GenInputs in;   // collect_mi=1, the default
    auto t0 = std::chrono::steady_clock::now();
    const bool ok = gen::ensureOutputsGuarded(game, outDir, "EN", r, nullptr, nullptr, in, &spec);
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - t0).count();
    const double peak = peakWorkingSetMb();
    const double commit = peakCommitMb();
    std::printf("[test] %s: game %s generated into %s - %s\n", ok && r.generated ? "ok  " : "FAIL",
                r.game.c_str(), outDir.c_str(), ok ? r.summary.c_str() : r.error.c_str());
    if (!ok || !r.generated) return 1;
    for (const std::string& w : r.warnings) std::printf("[test]       warning: %s\n", w.c_str());

    std::vector<std::uint8_t> raw;
    readAll(outDir + "\\catalogue.stamp", raw);
    const std::string stamp(raw.begin(), raw.end());
    const std::string fp = gen::inputFingerprint(game, "EN", in, &spec);
    check(stamp.compare(0, fp.size(), fp) == 0, "catalogue.stamp is the game's fingerprint");
    std::string arzKey = "database\\" + spec.modName + ".arz ";   // its path inside the game folder
    for (char& c : arzKey)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    check(fp.find("\ngame=" + r.game + "\n") != std::string::npos && fp.find(arzKey) != std::string::npos &&
              fp.find("Database\\database.arz ") != std::string::npos,
          "the fingerprint carries the leaf, the game's .arz and the base game's archives");
    check(gen::inputFingerprint(game, "EN", in).find("game=") == std::string::npos,
          "the start-up game's fingerprint has no game line");
    gen::GenReport again;
    check(gen::ensureOutputs(game, outDir, "EN", again, nullptr, nullptr, in, &spec) && again.upToDate,
          "the second call is up to date (the stamp matches)");

    {   // the caravan geometry the game derives (uniq-geometry.txt), for the harness's table
        int p[6] = {0}, w[6] = {0};
        bool bg = false;
        const bool read = readGeometry(outDir + "\\" + kGeometryName, p, w, &bg);
        std::printf("[test]       geometry: %s\n", r.geometry.c_str());
        check(read && p[2] >= 16 * 32 && p[3] >= 15 * 32 && p[2] % 32 == 0 && p[3] % 32 == 0 && w[0] > 0 &&
                  w[1] > 0,
              "uniq-geometry.txt holds a P and a W line, the area whole cells and at least TQ.exe's 16 x 15 sack "
              "(the caravan window the HUD record names)");
        const bool inside = p[0] >= 0 && p[1] >= 0 && p[0] + p[2] <= w[0] && p[1] + p[3] <= w[1];
        // the background's place: its BackgroundImage record's bitmapPosition - every installed game's
        // background is the window's size, 0,0 in a 565 x 637 window, 0,11 in a 660 x 699 one
        const bool wide = w[0] == 660 && w[1] == 699;
        const int wantX = 0, wantY = wide ? 11 : 0;
        const bool covers = p[0] - w[4] >= 0 && p[1] - w[5] >= 0 && p[0] - w[4] + p[2] <= w[0] &&
                            p[1] - w[5] + p[3] <= w[1];
        check(read && bg && w[4] == wantX && w[5] == wantY && covers,
              "uniq-geometry.txt's W line carries the caravan background's place (its BackgroundImage record), "
              "and the derived grid lies inside a window-sized background there");
        std::printf("[measure] geometry x0=%d y0=%d w=%d h=%d cols=%d rows=%d plate_w=%d plate_h=%d window_w=%d "
                    "window_h=%d page_x=%d page_y=%d inside=%d bg_x=%d bg_y=%d\n",
                    p[0], p[1], p[2], p[3], p[2] / 32, p[3] / 32, p[4], p[5], w[0], w[1], w[2], w[3],
                    inside ? 1 : 0, bg ? w[4] : -1, bg ? w[5] : -1);
    }
    // the groups and their sizes, from uniq-groups.txt's G lines
    readAll(outDir + "\\uniq-groups.txt", raw);
    const std::string groups(raw.begin(), raw.end());
    std::size_t paged = 0;
    std::vector<std::string> gl;
    for (std::size_t at = 0; at < groups.size();) {
        std::size_t nl = groups.find('\n', at);
        if (nl == std::string::npos) nl = groups.size();
        std::string line = groups.substr(at, nl - at);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.compare(0, 2, "G\t") == 0) gl.push_back(line);
        if (line.compare(0, 2, "E\t") == 0) ++paged;
        at = nl + 1;
    }
    std::printf("[measure] game leaf=%s before_exclude=%zu records=%zu excluded=%zu paged=%zu groups=%zu ms=%lld "
                "peak_added_mb=%.1f process_peak_mb=%.1f process_commit_mb=%.1f gray=%zu gray_mb=%.1f "
                "gray_failed=%zu\n",
                r.game.c_str(), r.records + r.excluded, r.records, r.excluded, paged, r.groups, ms,
                r.peakAddedMb, peak, commit, r.grayWritten, double(r.grayBytes) / 1048576.0, r.grayFailed);
    for (const std::string& g : gl) {
        std::vector<std::string> f;
        for (std::size_t a = 0; a <= g.size();) {
            std::size_t t = g.find('\t', a);
            if (t == std::string::npos) t = g.size();
            f.push_back(g.substr(a, t - a));
            a = t + 1;
        }
        if (f.size() >= 10)
            std::printf("[measure] group id=%s tag=%s count=%s label=%s\n", f[8].c_str(), f[9].c_str(),
                        f[7].c_str(), f[2].c_str());
        // a game's own group from the template's [view:<leaf>] block (its id = <leaf>.<name>)
        if (f.size() >= 10 && f[8].compare(0, r.game.size() + 1, r.game + ".") == 0) {
            std::printf("[measure] the game's own group %s (%s): %s records\n", f[8].c_str(), f[2].c_str(),
                        f[7].c_str());
            check(std::atoi(f[7].c_str()) > 0, "the game's own group from the template has records");
        }
    }
    // the search index at this record count, as test_search measures it: one folded std::string
    // and one state byte per paged record, the text the in-game mean (432 KB over 1588 records)
    {
        const size_t meanText = (432u * 1024u) / 1588u - 1u;
        const std::vector<std::string> ixText(paged ? paged : 1, std::string(meanText, 'x'));
        size_t heap = 0;
        for (const std::string& t : ixText) heap += t.capacity() > 15 ? gen::genHeapBlockSize(t.data()) + 8 : 0;
        const double per = double(sizeof(std::string) + 1) + double(heap) / double(ixText.size());
        std::printf("[measure] index bytes_per_record=%.0f total_kb=%.0f records=%zu\n", per,
                    per * double(paged) / 1024.0, paged);
    }
    // the owned tables (ut_owned.cpp): g_ids + g_invIds (unsigned) at kIdCap, g_invHash + g_owned
    // (unsigned long long) at kOwnedCap
    for (int cap : {8192, 32768})
        std::printf("[measure] owned cap=%d bytes=%zu\n", cap,
                    (2 * sizeof(unsigned) + 2 * sizeof(unsigned long long)) * size_t(cap));
    return rc;
}

// ---- --order -----------------------------------------------------------------------------------
// A custom game's archives are searched before the base game's, entry by entry (arc_reader.h
// ResourceArcs). The game-view passes cannot tell the order when no installed game re-skins a base
// icon under the same path, so this builds a scratch game whose Resources\Items.arc holds ONE
// entry of the base game's Items.arc, with other bytes. That entry must resolve to the game's
// archive and read the game's bytes; a second entry of the base Items.arc, which the game's
// archive lacks, must resolve to the base archive (an archive-level lookup would lose every base
// icon of a game that ships an Items.arc of its own). The "[order]" line hands the two paths and
// the folder to tools\test_gameview.py, which asks tools\tqarc.py the same questions.
int order(const std::string& scratch) {
    std::string game = gameDir();
    if (game.empty()) return fail("no Titan Quest AE folder: set TQ_GAME_DIR");
    int rc = 0;
    auto check = [&](bool ok, const std::string& what) {
        std::printf("[test] %s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
        if (!ok) rc = 1;
    };
    gen::ArcArchive base;
    std::string err;
    if (!base.load(game + "\\Resources\\Items.arc", &err) || base.size() < 2)
        return fail("the base game's Resources\\Items.arc does not load with two entries");
    const std::string one = base.entries()[0].name, two = base.entries()[1].name;
    char full[MAX_PATH] = {0};
    GetFullPathNameA(scratch.c_str(), MAX_PATH, full, nullptr);
    const std::string res = std::string(full) + "\\Resources";
    CreateDirectoryA(full, nullptr);
    CreateDirectoryA(res.c_str(), nullptr);
    const std::string text = "the game's own bytes";
    const std::vector<std::uint8_t> mine(text.begin(), text.end());
    if (!writeAll(res + "\\Items.arc",
                  synthArc(1, zlibStored(mine), std::uint32_t(mine.size()), 0xFFFFFFFFu, one)))
        return fail("the scratch game's Items.arc was not written");
    const std::string both = "Items\\" + one, baseOnly = "Items\\" + two;
    gen::ResourceArcs plain, rs;
    plain.setRoot(game);
    rs.setRoot(game, res);
    const std::string baseTag = plain.sourceOf(both);
    check(!baseTag.empty() && plain.sourceOf(baseOnly) == baseTag,
          "the start-up game resolves both entries to its Items.arc (" + baseTag + ")");
    check(rs.sourceOf(both).compare(0, 5, "game:") == 0,
          "an entry both hold resolves to the game's archive first (" + rs.sourceOf(both) + ")");
    std::vector<std::uint8_t> got;
    check(rs.read(both, got, 0, &err) && got == mine, "and reads the game's bytes, not the base game's");
    check(rs.sourceOf(baseOnly) == baseTag,
          "an entry only the base holds falls back to the base archive (entry by entry)");
    got.clear();
    check(rs.read(baseOnly, got, 0, &err) && !got.empty() && got != mine, "and reads the base game's bytes");
    std::printf("[order]\t%s\t%s\t%s\n", both.c_str(), baseOnly.c_str(), res.c_str());
    return rc;
}

// ---- --zeroarc ---------------------------------------------------------------------------------
// A custom game's archive that cannot be read is skipped, never fatal (arc_reader.h ResourceArcs,
// GD's zeroarcmod): a scratch game "zeroarcmod" (a one-record database of its own) ships a
// Resources\Text.arc and a Resources\Items.arc whose 4096 bytes are all zero. Its tags are the
// base game's, every icon falls through to the base Items.arc, each archive is named once however
// often it is asked for, and the game generates with exactly two warnings naming them - its
// catalogue.bin the base game's byte for byte (data\oracle). The "[zeroarc]" line hands the
// game's root and the C++ outputs to build_test_catalogue.bat, which asks build_catalogue.py the
// same (the twin skips the same two archives and writes the same files).
int zeroArc(const std::string& scratch, const std::string& oracleDir) {
    std::string game = gameDir();
    if (game.empty()) return fail("no Titan Quest AE folder: set TQ_GAME_DIR");
    int rc = 0;
    auto check = [&](bool ok, const std::string& what) {
        std::printf("[test] %s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
        if (!ok) rc = 1;
    };
    char full[MAX_PATH] = {0};
    GetFullPathNameA(scratch.c_str(), MAX_PATH, full, nullptr);
    const std::string root = full, dir = root + "\\zeroarcmod", res = dir + "\\Resources";
    const std::string out = root + "\\cpp";
    CreateDirectoryA(root.c_str(), nullptr);
    CreateDirectoryA(dir.c_str(), nullptr);
    CreateDirectoryA((dir + "\\Database").c_str(), nullptr);
    CreateDirectoryA(res.c_str(), nullptr);
    CreateDirectoryA(out.c_str(), nullptr);
    const std::vector<std::uint8_t> field = {2, 0, 1, 0, 1, 0, 0, 0, 2, 0, 0, 0};   // Class = ItemArtifact
    const std::vector<std::uint8_t> body = zlibStored(field);
    const std::vector<std::uint8_t> zeros(4096, 0);
    if (!writeAll(dir + "\\Database\\zeroarcmod.arz", synthArz(body, std::uint32_t(body.size()))) ||
        !writeAll(res + "\\Text.arc", zeros) || !writeAll(res + "\\Items.arc", zeros))
        return fail("the scratch game zeroarcmod was not written");

    // the archive lookup: the zeroed Items.arc is skipped and named once, the base archive answers
    gen::ArcArchive base;
    std::string err;
    if (!base.load(game + "\\Resources\\Items.arc", &err) || base.size() < 2)
        return fail("the base game's Resources\\Items.arc does not load with two entries");
    const std::string one = "Items\\" + base.entries()[0].name, two = "Items\\" + base.entries()[1].name;
    gen::ResourceArcs plain, rs;
    plain.setRoot(game);
    rs.setRoot(game, res);
    std::vector<std::uint8_t> got, want;
    err.clear();
    const bool read1 = rs.read(one, got, 0, &err);
    check(read1 && err.empty() && plain.read(one, want, 0, &err) && got == want,
          "an entry the zeroed game archive would hold reads from the base game's Items.arc, no error");
    check(rs.read(two, got, 0, &err) && rs.sourceOf(two) == plain.sourceOf(one) && rs.skipped().size() == 1 &&
              rs.skipped()[0].find("zeroarcmod\\Resources\\Items.arc") != std::string::npos,
          "the zeroed archive is named once however often it is asked for (" +
              (rs.skipped().empty() ? std::string("none") : rs.skipped()[0]) + ")");

    // the text: the game's zeroed Text.arc is skipped whole, the base text stands
    std::unordered_map<std::string, std::string> baseTags, gameTags;
    std::vector<std::string> skipped;
    const bool loadedTags = gen::loadTextTags(game, "EN", baseTags, &err) &&
                            gen::loadTextTags(game, "EN", gameTags, &err, res, &skipped);
    check(loadedTags && !gameTags.empty() && gameTags == baseTags && skipped.size() == 1 &&
              skipped[0].find("zeroarcmod\\Resources\\Text.arc") != std::string::npos,
          "the zeroed Text.arc is skipped and named; the tags are the base game's (" +
              std::to_string(gameTags.size()) + ")");

    // a translation: Text_EN.arc first and Text_<lang>.arc over it (GD 21fac7e) - every tag the
    // translation has keeps its text, every tag only English has keeps the English one
    {
        std::unordered_map<std::string, std::string> pure, merged;
        gen::ArcArchive ru;
        std::vector<std::uint8_t> data;
        bool read = ru.load(game + "\\Text\\Text_RU.arc", &err);
        for (std::size_t i = 0; read && i < ru.size(); ++i) {
            const gen::ArcEntry& e = ru.entries()[i];
            if (e.key.size() < 4 || e.key.compare(e.key.size() - 4, 4, ".txt") != 0) continue;
            read = ru.read(e, data);
            if (read && !data.empty()) gen::parseTagFile(data, pure);
        }
        if (!read) {
            std::printf("[test] skip: Text_RU.arc is not installed or does not read - the translation case did not run\n");
        } else {
            const bool loaded = gen::loadTextTags(game, "RU", merged, &err);
            std::size_t fromEn = 0, wrong = 0;
            for (const auto& kv : baseTags) {
                const auto m = merged.find(kv.first);
                const auto p = pure.find(kv.first);
                if (m == merged.end()) { ++wrong; continue; }
                if (p == pure.end()) { ++fromEn; if (m->second != kv.second) ++wrong; }
            }
            for (const auto& kv : pure) {
                const auto m = merged.find(kv.first);
                if (m == merged.end() || m->second != kv.second) ++wrong;
            }
            std::printf("[measure] text_language=RU tags=%zu translated=%zu english_kept=%zu\n", merged.size(),
                        pure.size(), fromEn);
            check(loaded && wrong == 0 && merged.size() >= baseTags.size(),
                  "text_language=RU: the translation over Text_EN.arc - " + std::to_string(fromEn) +
                      " tag(s) only English has keep their English text, the translated ones theirs");
        }
    }

    // the whole generation, as at start-up
    gen::GameSpec spec;
    std::string why;
    if (!ut::utGameLocate(root, "zeroarcmod", spec, &why)) return fail(why.c_str());
    for (const char* n : kNames) DeleteFileA((out + "\\" + n).c_str());
    DeleteFileA((out + "\\" + kGeometryName).c_str());
    DeleteFileA((out + "\\catalogue.stamp").c_str());
    gen::GenReport r;
    gen::GenInputs in;   // collect_mi=1, the default
    const bool ok = gen::ensureOutputs(game, out, "EN", r, nullptr, nullptr, in, &spec);
    check(ok && r.generated, "the game generates (" + (ok ? r.summary : r.error) + ")");
    int named = 0, skips = 0;
    for (const std::string& w : r.warnings) {
        std::printf("[test]       warning: %s\n", w.c_str());
        if (w.find("skips an unreadable archive") == std::string::npos) continue;
        ++skips;
        if (w.find("zeroarcmod\\Resources\\Text.arc") != std::string::npos ||
            w.find("zeroarcmod\\Resources\\Items.arc") != std::string::npos)
            ++named;
    }
    check(skips == 2 && named == 2, "two warnings, each naming its archive (Text.arc, Items.arc)");
    std::vector<std::uint8_t> bin, ref;
    check(readAll(out + "\\catalogue.bin", bin) && readAll(oracleDir + "\\catalogue.bin", ref) && bin == ref,
          "its catalogue.bin is the base game's byte for byte (every icon and name found through the skip)");
    std::printf("[zeroarc]\t%s\t%s\n", dir.c_str(), out.c_str());
    return rc;
}

// ---- --rulefix --------------------------------------------------------------------------------
// The collection rule on a scratch custom game "rulefixmod" whose database holds fixture records
// outside the item folders (records/rulefix/, records/sandbox/rulefix/), each an artifact with the
// name and icon of a base game's artifact: one held by a loot table a monster holds (collected),
// one held by nothing ("unreferenced"), one whose FileDescription says "Test" held by the table
// (collected: something hands it out) and one held by nothing (not a candidate, in no list), one
// whose FileDescription only contains "test" inside a word ("gigantestorso", held by nothing:
// listed "unreferenced", no template), one held only through a master table nothing names
// ("unobtainable: only in loot tables nothing reaches"), one in a developer folder held by the
// table (collected: the walk reaches it) and one held only by a loot table in a developer folder
// (dropped: "developer folder sandbox"), and one in a creature folder held by the table (not a
// candidate). The "[rulefix]" line hands the game's root and the C++ outputs to
// build_test_catalogue.bat, which asks build_catalogue.py the same (byte for byte).
struct FxField { const char* name; std::string value; };
struct FxRecord { std::string key; std::vector<FxField> fields; };

// A TQ ARZ of string fields only: each record's body one stored zlib stream, one string table.
std::vector<std::uint8_t> synthArzMany(const std::vector<FxRecord>& recs) {
    std::vector<std::string> strs;
    std::unordered_map<std::string, std::uint32_t> ids;
    auto sid = [&](const std::string& s) -> std::uint32_t {
        const auto it = ids.find(s);
        if (it != ids.end()) return it->second;
        ids.emplace(s, std::uint32_t(strs.size()));
        strs.push_back(s);
        return std::uint32_t(strs.size() - 1);
    };
    std::vector<std::uint8_t> out(24, 0);
    std::vector<std::uint32_t> at, cs;
    for (const FxRecord& r : recs) {
        std::vector<std::uint8_t> f;
        for (const FxField& fl : r.fields) {
            f.push_back(2); f.push_back(0); f.push_back(1); f.push_back(0);   // a string, one value
            put32(f, sid(fl.name));
            put32(f, sid(fl.value));
        }
        const std::vector<std::uint8_t> z = zlibStored(f);
        at.push_back(std::uint32_t(out.size()) - 24);
        cs.push_back(std::uint32_t(z.size()));
        out.insert(out.end(), z.begin(), z.end());
    }
    const std::uint32_t recStart = std::uint32_t(out.size());
    for (std::size_t i = 0; i < recs.size(); ++i) {
        put32(out, sid(recs[i].key));
        put32(out, 4); out.insert(out.end(), {'T', 'e', 's', 't'});
        put32(out, at[i]); put32(out, cs[i]);
        put32(out, 0); put32(out, 0);             // FILETIME
    }
    const std::uint32_t recSize = std::uint32_t(out.size()) - recStart;
    const std::uint32_t strStart = std::uint32_t(out.size());
    put32(out, std::uint32_t(strs.size()));
    for (const std::string& s : strs) {
        put32(out, std::uint32_t(s.size()));
        out.insert(out.end(), s.begin(), s.end());
    }
    const std::uint32_t strSize = std::uint32_t(out.size()) - strStart;
    for (int i = 0; i < 4; ++i) put32(out, 0);   // the adler footer (tolerated, not read)
    std::uint8_t* h = out.data();
    h[0] = 4; h[1] = 0; h[2] = 3; h[3] = 0;
    auto set32 = [&](std::size_t p, std::uint32_t v) { for (int i = 0; i < 4; ++i) h[p + i] = std::uint8_t(v >> (8 * i)); };
    set32(4, recStart); set32(8, recSize); set32(12, std::uint32_t(recs.size())); set32(16, strStart); set32(20, strSize);
    return out;
}

// The first tab field of every line of a list file, mapped to the rest of the line.
std::unordered_map<std::string, std::string> listLines(const std::string& path) {
    std::unordered_map<std::string, std::string> m;
    std::vector<std::uint8_t> raw;
    if (!readAll(path, raw)) return m;
    std::string s(raw.begin(), raw.end()), line;
    std::size_t p = 0;
    while (p < s.size()) {
        std::size_t e = s.find('\n', p);
        if (e == std::string::npos) e = s.size();
        line = s.substr(p, e - p);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t t = line.find('\t');
        if (t != std::string::npos) m[line.substr(0, t)] = line.substr(t + 1);
        else if (!line.empty()) m[line] = std::string();     // uniq-records.txt: the path alone
        p = e + 1;
    }
    return m;
}

int ruleFix(const std::string& scratch) {
    std::string game = gameDir();
    if (game.empty()) return fail("no Titan Quest AE folder: set TQ_GAME_DIR");
    int rc = 0;
    auto check = [&](bool ok, const std::string& what) {
        std::printf("[test] %s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
        if (!ok) rc = 1;
    };
    char full[MAX_PATH] = {0};
    GetFullPathNameA(scratch.c_str(), MAX_PATH, full, nullptr);
    const std::string root = full, dir = root + "\\rulefixmod", out = root + "\\cpp";
    CreateDirectoryA(root.c_str(), nullptr);
    CreateDirectoryA(dir.c_str(), nullptr);
    CreateDirectoryA((dir + "\\Database").c_str(), nullptr);
    CreateDirectoryA(out.c_str(), nullptr);

    // the name and icon of a base game's artifact
    gen::GameData base;
    std::string err;
    if (!base.load(game, "EN", &err)) return fail(("the base game does not load: " + err).c_str());
    gen::ArzRecord src;
    if (!base.db->get("records/xpack/item/artifacts/n_la_amberflask.dbr", src) || src.str("description").empty() ||
        src.str("artifactBitmap").empty())
        return fail("the base game's records/xpack/item/artifacts/n_la_amberflask.dbr has no description / artifactBitmap");
    const std::string tag = src.str("description"), bmp = src.str("artifactBitmap");
    auto artifact = [&](const std::string& key, const std::string& fd) {
        return FxRecord{key, {{"Class", "ItemArtifact"}, {"itemClassification", "Legendary"},
                              {"description", tag}, {"artifactBitmap", bmp}, {"FileDescription", fd}}};
    };
    const std::vector<FxRecord> recs = {
        {"records/rulefix/monster.dbr", {{"Class", "Monster"}, {"lootName1", "records/rulefix/table.dbr"},
                                         {"lootName2", "records/sandbox/rulefix/devtable.dbr"}}},
        {"records/rulefix/table.dbr", {{"Class", "LootItemTable_FixedWeight"},
                                       {"lootName1", "records/rulefix/held.dbr"},
                                       {"lootName2", "records/rulefix/testheld.dbr"},
                                       {"lootName3", "records/sandbox/rulefix/jewel.dbr"},
                                       {"lootName4", "records/rulefix/creatures/worn.dbr"}}},
        {"records/rulefix/master.dbr", {{"Class", "LootMasterTable"}, {"lootName1", "records/rulefix/deadtable.dbr"}}},
        {"records/rulefix/deadtable.dbr", {{"Class", "LootItemTable_FixedWeight"}, {"lootName1", "records/rulefix/dead.dbr"}}},
        {"records/sandbox/rulefix/devtable.dbr", {{"Class", "LootItemTable_FixedWeight"},
                                                  {"lootName1", "records/sandbox/rulefix/devonly.dbr"}}},
        artifact("records/rulefix/held.dbr", "a real item outside the item folders"),
        artifact("records/rulefix/alone.dbr", "held by nothing"),
        artifact("records/rulefix/testheld.dbr", "Test - Arcane Formula"),
        artifact("records/rulefix/testalone.dbr", "Test - Arcane Formula"),
        artifact("records/rulefix/gigantestorso.dbr", "M_GigantesTorso01B.tex"),
        artifact("records/rulefix/dead.dbr", "held only through a master table nothing names"),
        artifact("records/sandbox/rulefix/jewel.dbr", "a developer folder, held by a real table"),
        artifact("records/sandbox/rulefix/devonly.dbr", "a developer folder, held by a developer table"),
        artifact("records/rulefix/creatures/worn.dbr", "a creature folder"),
    };
    if (!writeAll(dir + "\\Database\\rulefixmod.arz", synthArzMany(recs)))
        return fail("the scratch game rulefixmod was not written");

    gen::GameSpec spec;
    std::string why;
    if (!ut::utGameLocate(root, "rulefixmod", spec, &why)) return fail(why.c_str());
    for (const char* n : kNames) DeleteFileA((out + "\\" + n).c_str());
    DeleteFileA((out + "\\" + kGeometryName).c_str());
    DeleteFileA((out + "\\catalogue.stamp").c_str());
    gen::GenReport r;
    gen::GenInputs in;   // collect_mi=1, the default
    const bool ok = gen::ensureOutputs(game, out, "EN", r, nullptr, nullptr, in, &spec);
    check(ok && r.generated, "the fixture game generates (" + (ok ? r.summary : r.error) + ")");
    const auto rec = listLines(out + "\\uniq-records.txt");
    const auto exc = listLines(out + "\\uniq-excluded.txt");
    auto excIs = [&](const char* k, const std::string& why2) {
        const auto it = exc.find(k);
        return it != exc.end() && it->second == why2 && !rec.count(k);
    };
    const std::string unref = "unreferenced (no loot table, merchant, container, formula, quest or monster holds it)";
    check(rec.count("records/rulefix/held.dbr") && !exc.count("records/rulefix/held.dbr"),
          "a record outside the item folders that a monster's loot table holds is collected");
    check(excIs("records/rulefix/alone.dbr", unref), "one held by nothing is dropped, unreferenced");
    check(rec.count("records/rulefix/testheld.dbr") != 0,
          "a \"Test\" FileDescription the table holds is collected (something hands it out)");
    check(!rec.count("records/rulefix/testalone.dbr") && !exc.count("records/rulefix/testalone.dbr"),
          "a \"Test\" FileDescription nothing holds is no candidate (in neither list)");
    check(excIs("records/rulefix/gigantestorso.dbr", unref),
          "\"test\" inside a word (GigantesTorso) is no template word: a candidate, dropped as unreferenced");
    check(excIs("records/rulefix/dead.dbr", "unobtainable: only in loot tables nothing reaches"),
          "one held only through a master table nothing names is unobtainable (the reach walk)");
    check(rec.count("records/sandbox/rulefix/jewel.dbr") != 0,
          "a developer folder's record a real table holds is collected (the walk reaches it)");
    check(excIs("records/sandbox/rulefix/devonly.dbr", "developer folder sandbox"),
          "a developer folder's record only a developer table holds stays a developer folder's");
    check(!rec.count("records/rulefix/creatures/worn.dbr") && !exc.count("records/rulefix/creatures/worn.dbr"),
          "a creature folder's record is no candidate");
    std::printf("[rulefix]\t%s\t%s\n", dir.c_str(), out.c_str());
    return rc;
}

// ---- --startup ---------------------------------------------------------------------------------
// The start-up builds as the DLL runs them (ut_generate.cpp): every custom game under the workshop
// content folder (ut_gamelist.h utGamesFind), each generated into <out>\mods\<leaf> one at a time,
// each in the build arena the DLL reserves (gen_heap.h kBuildArenaBytes, top-down; reset after each
// game, released in one piece after the last; --process-heap: on the process heap, for the
// comparison): the arena's high-water and live peak per game, the largest of them against its
// size (no build may outgrow it), the address space before, after each and after the release;
// then the same pass again: every game up to date. The folder rules on spelled-out paths first.
int startup(const std::string& workshopDir, const std::string& outDirArg, bool processHeap) {
    // the folder without its ".." steps: utGameLocate refuses a root that climbs, as the entry does
    char full[MAX_PATH] = {0};
    const std::string outDir = GetFullPathNameA(outDirArg.c_str(), MAX_PATH, full, nullptr) ? full : outDirArg;
    std::string game = gameDir();
    if (game.empty()) return fail("no Titan Quest AE folder: set TQ_GAME_DIR");
    int rc = 0;
    auto check = [&](bool ok, const std::string& what) {
        std::printf("[test] %s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
        if (!ok) rc = 1;
    };
    check(ut::utWorkshopDirOf("lib\\steamapps\\common\\Titan Quest Anniversary Edition") ==
                  "lib\\steamapps\\workshop\\content\\475150" &&
              ut::utWorkshopDirOf("lib/SteamApps/Common/TQAE/") == "lib\\SteamApps\\workshop\\content\\475150" &&
              ut::utWorkshopDirOf("games\\Titan Quest Anniversary Edition").empty(),
          "the workshop folder is the game's own library's (steamapps\\common\\<game> -> "
          "steamapps\\workshop\\content\\475150); none outside a Steam library");
    check(ut::utCustomMapsDirOf("home\\") == "home\\My Games\\Titan Quest - Immortal Throne\\CustomMaps\\" &&
              ut::utCustomMapsDirOf("").empty(),
          "the custom quest folder is <Documents>\\My Games\\Titan Quest - Immortal Throne\\CustomMaps\\ "
          "(TQ.exe's own path)");
    const std::string derived = ut::utWorkshopDirOf(game);
    std::printf("[test]       the game folder's workshop folder: %s\n", derived.empty() ? "(none)" : derived.c_str());

    std::vector<ut::UtGameFound> games;
    std::vector<std::string> skipped;
    ut::utGamesFind(workshopDir, "", &games, &skipped);
    for (const std::string& sk : skipped) std::printf("[test]       skipped: %s\n", sk.c_str());
    check(!games.empty(), "the custom games under " + workshopDir + ": " + std::to_string(games.size()));
    if (games.empty()) return 1;
    {   // the engine's spelling of the same folder (its separators, another case) gives the same
        // leaf and the same stamp: what the start-up build writes is what the entry reads
        const gen::GameSpec& s0 = games[0].spec;
        gen::GameSpec eng;
        eng.root = s0.root;
        for (char& c : eng.root) c = c == '\\' ? '/' : (char)((c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c);
        eng.modName = s0.modName;
        for (char& c : eng.modName) c = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
        gen::GenInputs in;
        check(eng.name() == games[0].leaf &&
                  gen::inputFingerprint(game, "EN", in, &eng) == gen::inputFingerprint(game, "EN", in, &s0),
              "the engine's spelling of " + s0.dir() + " gives the leaf " + games[0].leaf +
                  " and the same stamp");
    }
    const std::string mods = outDir + "\\mods";
    CreateDirectoryA(outDir.c_str(), nullptr);
    CreateDirectoryA(mods.c_str(), nullptr);
    {   // the local custom quest folder (an empty database is enough to be found): its game comes
        // after the workshop's, and a folder of a leaf the workshop has already is skipped
        const std::string cm = ut::utCustomMapsDirOf(outDir + "\\home");
        std::string at = outDir + "\\home";
        CreateDirectoryA(at.c_str(), nullptr);
        for (const char* part : {"My Games", "Titan Quest - Immortal Throne", "CustomMaps"}) {
            at += std::string("\\") + part;
            CreateDirectoryA(at.c_str(), nullptr);
        }
        for (const std::string& name : {std::string("LocalQuest"), games[0].spec.modName}) {
            CreateDirectoryA((cm + name).c_str(), nullptr);
            CreateDirectoryA((cm + name + "\\Database").c_str(), nullptr);
            FILE* f = std::fopen((cm + name + "\\Database\\" + name + ".arz").c_str(), "wb");
            if (f) std::fclose(f);
        }
        std::vector<ut::UtGameFound> both;
        std::vector<std::string> sk2;
        ut::utGamesFind(workshopDir, cm, &both, &sk2);
        check(both.size() == games.size() + 1 && !both.back().workshop &&
                  both.back().spec.modName == "LocalQuest" && both.back().spec.root == cm &&
                  both.back().leaf == "localquest" && sk2.size() == skipped.size() + 1,
              "the custom quest folder: its own game after the workshop's (" + std::to_string(both.size()) +
                  " found), the copy of " + games[0].leaf + " skipped (" + (sk2.empty() ? "" : sk2.back()) + ")");
    }
    std::string why;
    utg::builtinGroupSet(&why);   // the generator's function-local static, before the arena
    const ut::UtAddrSpace before = ut::utAddrSpace();
    unsigned long arenaErr = 0;
    const bool arena = !processHeap && gen::arenaReserve(gen::kBuildArenaBytes, &arenaErr);
    if (!processHeap)
        check(arena, "the build arena is reserved: " + std::to_string(gen::kBuildArenaBytes >> 20) +
                         " MB, MEM_TOP_DOWN (err=" + std::to_string(arenaErr) + ")");
    std::printf("[measure] startup before largest_mb=%u total_mb=%u heap=%s arena_base=0x%08X arena_mb=%u\n",
                before.largestMb, before.totalMb, processHeap ? "process" : "arena",
                (unsigned)(std::uintptr_t)gen::arenaInfo().base, (unsigned)(gen::arenaInfo().size >> 20));
    std::size_t arenaPeak = 0;
    std::string arenaPeakGame;
    {   // the start-up game first, inside the same arena, as the game builds it (its first pair)
        const std::string dir = outDir + "\\startup";
        CreateDirectoryA(dir.c_str(), nullptr);
        DeleteFileA((dir + "\\catalogue.stamp").c_str());
        const HANDLE heap = processHeap ? nullptr : gen::genHeapBegin();
        bool ok = false;
        {
            gen::GenReport r;
            gen::GenInputs in;
            ok = gen::ensureOutputsGuarded(game, dir, "EN", r, nullptr, nullptr, in) && r.generated;
            if (!ok) std::printf("[test]       the start-up game: %s\n", r.error.c_str());
        }
        const bool outgrown = arena && gen::genHeapOutgrown();
        gen::genHeapEnd(heap);
        const gen::ArenaInfo ai = gen::arenaInfo();
        if (arena && ai.pairHighWater > arenaPeak) {
            arenaPeak = ai.pairHighWater;
            arenaPeakGame = "the start-up game";
        }
        std::printf("[measure] startup game=(the start-up game) ok=%d arena_high_mb=%.1f arena_live_peak_mb=%.1f "
                    "outgrown=%d\n",
                    ok ? 1 : 0, arena ? ai.pairHighWater / 1048576.0 : 0.0,
                    arena ? ai.pairLivePeak / 1048576.0 : 0.0, outgrown ? 1 : 0);
        check(ok && !outgrown, "the start-up game is built first, in the arena's first pair, as in the game");
    }
    int built = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (std::size_t k = 0; k < games.size(); ++k) {
        const ut::UtGameFound& g = games[k];
        const std::string dir = mods + "\\" + g.leaf;
        CreateDirectoryA(dir.c_str(), nullptr);
        DeleteFileA((dir + "\\catalogue.stamp").c_str());
        const HANDLE heap = processHeap ? nullptr : gen::genHeapBegin();
        bool ok = false;
        long long ms = 0;
        {
            gen::GenReport r;
            gen::GenInputs in;
            auto g0 = std::chrono::steady_clock::now();
            ok = gen::ensureOutputsGuarded(game, dir, "EN", r, nullptr, nullptr, in, &g.spec) && r.generated;
            ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - g0).count();
            if (!ok) std::printf("[test]       %s: %s\n", g.leaf.c_str(), r.error.c_str());
        }
        const bool outgrown = arena && gen::genHeapOutgrown();
        gen::genHeapEnd(heap);
        built += ok ? 1 : 0;
        const ut::UtAddrSpace a = ut::utAddrSpace();
        const gen::ArenaInfo ai = gen::arenaInfo();
        if (arena && ai.pairHighWater > arenaPeak) {
            arenaPeak = ai.pairHighWater;
            arenaPeakGame = g.leaf;
        }
        std::printf("[measure] startup game=%s ok=%d ms=%lld largest_mb=%u total_mb=%u arena_high_mb=%.1f "
                    "arena_live_peak_mb=%.1f arena_committed_after_mb=%.1f outgrown=%d\n",
                    g.leaf.c_str(), ok ? 1 : 0, ms, a.largestMb, a.totalMb, arena ? ai.pairHighWater / 1048576.0 : 0.0,
                    arena ? ai.pairLivePeak / 1048576.0 : 0.0, arena ? ai.committed / 1048576.0 : 0.0,
                    outgrown ? 1 : 0);
    }
    const long long allMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - t0).count();
    if (arena) {
        const gen::ArenaInfo ai = gen::arenaInfo();
        const ut::UtAddrSpace held = ut::utAddrSpace();
        const bool released = gen::arenaRelease();
        const ut::UtAddrSpace freed = ut::utAddrSpace();
        std::printf("[measure] arena %u MB at 0x%08X (top-down); build peak %.1f MB (%s); %u build(s), %u "
                    "outgrew it, %u free(s) from another thread; released: largest %u -> %u MB, free %u -> %u MB\n",
                    (unsigned)(ai.size >> 20), (unsigned)(std::uintptr_t)ai.base, arenaPeak / 1048576.0,
                    arenaPeakGame.c_str(), ai.pairs, ai.outgrown, ai.foreignFrees, held.largestMb, freed.largestMb,
                    held.totalMb, freed.totalMb);
        check(released && gen::arenaInfo().base == nullptr, "the arena is released in one piece after the last build");
        check(ai.outgrown == 0 && ai.highWater == arenaPeak && arenaPeak < ai.size,
              "no build outgrew the arena: the largest high-water " + std::to_string(arenaPeak >> 20) + " MB (" +
                  arenaPeakGame + ") of " + std::to_string(ai.size >> 20) + " MB");
        check(ai.foreignFrees == 0, "no arena block was freed by another thread");
    }
    const ut::UtAddrSpace after = ut::utAddrSpace();
    std::printf("[measure] startup after largest_mb=%u total_mb=%u games=%zu built=%d s=%.1f heap=%s\n",
                after.largestMb, after.totalMb, games.size(), built, allMs / 1000.0,
                processHeap ? "process" : "arena");
    check(built == (int)games.size(), "every custom game is built, one at a time: " + std::to_string(built) +
                                          " of " + std::to_string(games.size()));
    char d[200];
    _snprintf_s(d, sizeof(d), _TRUNCATE, "the largest free block %u -> %u MB, %u -> %u MB free in all",
                before.largestMb, after.largestMb, before.totalMb, after.totalMb);
    if (!processHeap)
        check(after.totalMb + 8 >= before.totalMb && after.largestMb + 8 >= before.largestMb,
              std::string("the builds leave no reservation behind (8 MB slack): ") + d);
    else
        std::printf("[test]       on the process heap: %s\n", d);
    int current = 0;
    auto t1 = std::chrono::steady_clock::now();
    for (const ut::UtGameFound& g : games) {
        gen::GenReport r;
        gen::GenInputs in;
        current += gen::ensureOutputs(game, mods + "\\" + g.leaf, "EN", r, nullptr, nullptr, in, &g.spec) &&
                           r.upToDate
                       ? 1
                       : 0;
    }
    const long long againMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - t1).count();
    check(current == (int)games.size(), "the next start finds every game up to date (" +
                                            std::to_string(current) + " of " + std::to_string(games.size()) +
                                            ", " + std::to_string(againMs) + " ms)");
    return rc;
}

bool slurpText(const std::string& path, std::string* out) {
    out->clear();
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char buf[65536];
    std::size_t k;
    while ((k = std::fread(buf, 1, sizeof buf, f)) > 0) out->append(buf, k);
    std::fclose(f);
    return !out->empty();
}

// --reader <base set dir> [<mods dir>]: every generated set through the in-game reader, as
// ut_live.cpp liveInit reads it - the P line of uniq-geometry.txt (utCaravanGeoParse) gives the
// game's area, and uniq-groups.txt is read on it (gdut::readGameGroups: each G line checked on the
// area, each group packed on it, the packed slot equal to the G line's). The base set first, then
// every sub-folder of <mods dir> (the start-up sets of the custom games). Each line also says
// whether the base game's 16 x 15 would read the set.
int reader(const std::string& baseDir, const std::string& modsDir) {
    int rc = 0, sets = 0, wanted = 0;
    auto check = [&](bool ok, const std::string& what) {
        std::printf("[test] %s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
        if (!ok) rc = 1;
    };
    std::vector<std::string> dirs{baseDir};
    if (!modsDir.empty()) {
        WIN32_FIND_DATAA fd;
        const HANDLE h = FindFirstFileA((modsDir + "\\*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != '.')
                    dirs.push_back(modsDir + "\\" + fd.cFileName);
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    }
    for (const std::string& dir : dirs) {
        ++wanted;
        const std::size_t cut = dir.find_last_of("\\/");
        const std::string leaf = cut == std::string::npos ? dir : dir.substr(cut + 1);
        std::string geoText, groupsText;
        ut::UtCaravanGeo geo = {};
        if (!slurpText(dir + "\\uniq-geometry.txt", &geoText) ||
            !ut::utCaravanGeoParse(geoText.data(), geoText.size(), &geo)) {
            check(false, "reader " + leaf + ": uniq-geometry.txt is missing or has no valid P / W lines");
            continue;
        }
        if (!slurpText(dir + "\\uniq-groups.txt", &groupsText)) {
            check(false, "reader " + leaf + ": uniq-groups.txt is missing or empty");
            continue;
        }
        std::vector<gdut::GroupList> lists;
        std::vector<gdut::PackedGroup> packed;
        std::string err;
        const bool ok = gdut::readGameGroups(groupsText, geo.cols, geo.rows, &lists, &packed, &err);
        std::size_t records = 0;
        int pages = 0;
        bool slotsAgree = ok && !lists.empty() && packed.size() == lists.size();
        for (std::size_t g = 0; slotsAgree && g < lists.size(); ++g) {
            const gdut::GroupList& l = lists[g];
            const gdut::PackedGroup& p = packed[g];
            records += l.entries.size();
            pages += p.pages;
            if (!l.entries.empty() &&
                (p.slot.cols != l.cols || p.slot.rows != l.rows || p.slot.w * gdut::kCellPx != l.cellW ||
                 p.slot.h * gdut::kCellPx != l.cellH || p.placed.size() != l.entries.size()))
                slotsAgree = false;
        }
        // the relic page's G line is there exactly when the game's uniq-relics.txt has an R line
        std::string relicsText;
        const bool relicRows = slurpText(dir + "\\uniq-relics.txt", &relicsText) &&
                               (relicsText.compare(0, 2, "R\t") == 0 ||
                                relicsText.find("\nR\t") != std::string::npos);
        const bool relicG = groupsText.find(std::string("\t") + utg::kRelicGroupId + "\t" +
                                            utg::kRelicGroupTag + "\r\n") != std::string::npos;
        check(relicRows == relicG, "reader " + leaf + ": the relic G line " +
                                       (relicG ? "is there" : "is not there") + " and uniq-relics.txt has " +
                                       (relicRows ? "relic rows" : "no relic row"));
        std::vector<gdut::GroupList> base;
        std::string baseErr;
        const bool baseReads = gdut::parseGroupsText(groupsText, &base, &baseErr);
        const std::string area = std::to_string(geo.cols) + " x " + std::to_string(geo.rows);
        check(ok && slotsAgree,
              "reader " + leaf + ": " +
                  (ok ? std::to_string(lists.size()) + " groups, " + std::to_string(records) + " records on " +
                            std::to_string(pages) + " pages of its own " + area +
                            " cells (P line), each packed slot the G line's" +
                            (slotsAgree ? "" : " - NOT")
                      : err + " on its own " + area) +
                  "; the base game's 16 x 15 " + (baseReads ? "reads it too" : "refuses it (" + baseErr + ")"));
        if (ok && slotsAgree) ++sets;
    }
    check(sets == wanted, "the in-game reader read " + std::to_string(sets) + " of " +
                              std::to_string(wanted) + " generated sets on their own areas");
    return rc;
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == "--reader") return reader(argv[2], argc >= 4 ? argv[3] : "");
    if (argc >= 3 && std::string(argv[1]) == "--order") return order(argv[2]);
    if (argc >= 4 && std::string(argv[1]) == "--zeroarc") return zeroArc(argv[2], argv[3]);
    if (argc >= 3 && std::string(argv[1]) == "--rulefix") return ruleFix(argv[2]);
    if (argc >= 5 && std::string(argv[1]) == "--game-view") return gameView(argv[2], argv[3], argv[4]);
    if (argc >= 4 && std::string(argv[1]) == "--startup")
        return startup(argv[2], argv[3], argc >= 5 && std::string(argv[4]) == "--process-heap");
    if (argc >= 4 && std::string(argv[1]) == "--units") {
        const int a = units(argv[2], argv[3]);
        const int b = relicUnits();
        return a ? a : b;
    }
    if (argc >= 4 && std::string(argv[1]) == "--ensure") return ensure(argv[2], argv[3]);
    if (argc >= 3 && std::string(argv[1]) == "--peak") return peak(argv[2]);
    if (argc >= 3 && std::string(argv[1]) == "--ensure-flag") return ensureFlag(argv[2]);
    if (argc >= 3 && std::string(argv[1]) == "--catalogue")
        return catalogue(argv[2], argc >= 4 ? argv[3] : "");
    std::fprintf(stderr, "usage: test_catalogue --units <scratch dir> <oracle.bin> | --catalogue <oracle.bin> [<out.bin>] | "
                         "--ensure <data dir> <mod dir> | --peak <mod dir> | --ensure-flag <mod dir> | "
                         "--game-view <root> <name> <out dir> | --startup <workshop dir> <out dir> [--process-heap] | "
                         "--order <scratch dir> | --zeroarc <scratch dir> <oracle dir> | --rulefix <scratch dir> | "
                         "--reader <base set dir> [<mods dir>]\n");
    return 2;
}
