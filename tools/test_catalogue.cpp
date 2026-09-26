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
//                                                200 MB budget
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>

#include "gen/arc_reader.h"
#include "gen/arz_reader.h"
#include "gen/catalogue_gen.h"
#include "gen/generate.h"
#include "gen/inflate.h"
#include "model/catalogue.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
std::vector<std::uint8_t> synthArc(std::uint32_t version, const std::vector<std::uint8_t>& part,
                                   std::uint32_t dsize, std::uint32_t entryDsize = 0xFFFFFFFFu) {
    std::vector<std::uint8_t> out(28, 0);
    const std::uint32_t partAt = std::uint32_t(out.size());
    out.insert(out.end(), part.begin(), part.end());
    const std::uint32_t recOff = std::uint32_t(out.size());
    put32(out, partAt); put32(out, std::uint32_t(part.size())); put32(out, dsize);
    const char name[] = "a/b.txt";
    out.insert(out.end(), name, name + sizeof name);   // with its NUL
    const std::uint32_t strSize = sizeof name;
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
    if (!gen::collectItems(g, items, excluded, warnings, &err)) return fail(err.c_str());
    std::vector<std::uint8_t> bin;
    gen::CatalogueStats st;
    if (!gen::packCatalogue(items, bin, &st, &err)) return fail(err.c_str());
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    std::printf("[test] catalogue: %zu items (%zu equipment, %zu artifacts), %zu excluded, %zu sets / "
                "%zu members, %zu strings %zu B, %zu B total, generated in %lld ms\n",
                st.items, st.equipment, st.relics, excluded.size(), st.sets, st.members, st.strings,
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
    const bool ok2 = gen::collectItems(g, items2, excluded2, warnings2, &err);
    g.expansionInstalled[3] = true;
    std::size_t left3 = 0, filtered = 0;
    for (const gen::CatalogueItem& it : items2) if (it.expansion == 3) ++left3;
    for (const gen::ExcludedItem& x : excluded2) if (x.reason == "expansion not installed") ++filtered;
    const bool dlcOk = ok2 && atlantis > 0 && left3 == 0 && items2.size() + atlantis == items.size()
                       && filtered >= atlantis;
    std::printf("[test] %s: DLC filter, Atlantis marked absent: %zu of its %zu records left the catalogue, "
                "%zu candidates excluded as 'expansion not installed', %zu left\n",
                dlcOk ? "ok  " : "FAIL", items.size() - items2.size(), atlantis, filtered, left3);
    return dlcOk ? rc : 1;
}

// ---- --ensure ----------------------------------------------------------------------------------
const char* const kNames[4] = {"catalogue.bin", "uniq-records.txt", "uniq-groups.txt", "uniq-excluded.txt"};

int ensure(const std::string& dataDir, const std::string& modDir) {
    std::string game = gameDir();
    if (game.empty()) return fail("no Titan Quest AE folder: set TQ_GAME_DIR");
    const std::string stamp = modDir + "\\catalogue.stamp";
    for (const char* n : kNames) DeleteFileA((modDir + "\\" + n).c_str());
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
    return rc;
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
                "budget %.0f MB - %s\n",
                ok ? "generated" : "FAILED", (long long)ms, after, before, kPeakBudgetMb,
                ok ? r.summary.c_str() : r.error.c_str());
    if (!ok || !r.generated) return fail("the generation did not run");
    if (after > kPeakBudgetMb) return fail("the generation peaked over the 200 MB budget");
    std::printf("[test] ok  : the generation stays inside the 200 MB peak budget\n");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 4 && std::string(argv[1]) == "--units") return units(argv[2], argv[3]);
    if (argc >= 4 && std::string(argv[1]) == "--ensure") return ensure(argv[2], argv[3]);
    if (argc >= 3 && std::string(argv[1]) == "--peak") return peak(argv[2]);
    if (argc >= 3 && std::string(argv[1]) == "--catalogue")
        return catalogue(argv[2], argc >= 4 ? argv[3] : "");
    std::fprintf(stderr, "usage: test_catalogue --units <scratch dir> <oracle.bin> | --catalogue <oracle.bin> [<out.bin>] | "
                         "--ensure <data dir> <mod dir> | --peak <mod dir>\n");
    return 2;
}
