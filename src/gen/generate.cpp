// generate.cpp - see generate.h.
#include "gen/generate.h"
#include "gen/catalogue_gen.h"
#include "gen/ui_geometry.h"   // the caravan geometry: uniq-geometry.txt
#include "gen/ut_groups.h"
#include "gen/ut_groups_default.h"
#include "gen/arc_reader.h"   // the gray pass reads whole icons
#include "model/catalogue.h"
#include "ut_graytex.h"   // the gray copy of each icon (pure)
#include "gen/relics_gen.h"   // uniq-relics.txt
#include "ut_depositgate.h"   // utSaveSetLeaf / utSaveSetGame: a game's leaf

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <new>
#include <set>

namespace gen {

// ---------------------------------------------------------------- GameSpec
std::string GameSpec::dir() const {
    if (startup()) return std::string();
    std::string d = root;
    for (char& c : d)
        if (c == '/') c = '\\';
    if (!d.empty() && d.back() != '\\') d += '\\';
    return d + modName;
}

std::string GameSpec::arzPath() const {
    return startup() ? std::string() : dir() + "\\Database\\" + modName + ".arz";
}

std::string GameSpec::resourcesDir() const {
    return startup() ? std::string() : dir() + "\\Resources";
}

std::string GameSpec::name() const {
    if (startup()) return std::string();
    char set[96], leaf[96];
    if (!ut::utSaveSetLeaf(true, modName.c_str(), false, false, set, sizeof set)) return std::string();
    if (!ut::utSaveSetGame(set, leaf, sizeof leaf)) return std::string();
    return leaf;
}

namespace {

const char* const kStampName = "catalogue.stamp";
const int kOutputCount = 6;
const char* const kOutputs[kOutputCount] = {"catalogue.bin", "uniq-records.txt", "uniq-groups.txt",
                                            "uniq-excluded.txt", "uniq-geometry.txt",
                                            "uniq-relics.txt"};

// The process's working set in MB, current and peak, 0 when the call is not there.
// K32GetProcessMemoryInfo lives in kernel32; resolving it at run time keeps the DLL from
// importing psapi.
//
// BOTH numbers are the WHOLE PROCESS. Inside the game that is mostly the engine: the generation
// runs on the start-up worker while the engine is loading its own database, so the raw peak
// there reads several hundred megabytes and says nothing about this code. What the log carries
// is therefore the RISE over the working set measured just before the generation started, which
// is the generator's own cost, plus the process peak for scale.
bool workingSetMb(double* current, double* peak) {
    typedef BOOL(WINAPI * Fn)(HANDLE, PROCESS_MEMORY_COUNTERS*, DWORD);
    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    FARPROC raw = k32 ? GetProcAddress(k32, "K32GetProcessMemoryInfo") : nullptr;
    Fn fn = reinterpret_cast<Fn>(reinterpret_cast<void*>(raw));
    PROCESS_MEMORY_COUNTERS pmc;
    if (!fn || !fn(GetCurrentProcess(), &pmc, sizeof pmc)) return false;
    if (current) *current = double(pmc.WorkingSetSize) / (1024.0 * 1024.0);
    if (peak) *peak = double(pmc.PeakWorkingSetSize) / (1024.0 * 1024.0);
    return true;
}

bool fileExists(const std::string& p) {
    DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool readText(const std::string& p, std::string& out) {
    FILE* f = std::fopen(p.c_str(), "rb");
    if (!f) return false;
    char buf[4096];
    std::size_t n;
    out.clear();
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

bool writeBytes(const std::string& p, const void* data, std::size_t n) {
    FILE* f = std::fopen(p.c_str(), "wb");
    if (!f) return false;
    bool ok = n == 0 || std::fwrite(data, 1, n, f) == n;
    ok = (std::fclose(f) == 0) && ok;
    return ok;
}

std::string tmpName(const std::string& modDir, int i) {
    return modDir + "\\" + kOutputs[i] + ".tmp";
}

// The temporary names, whether this run or a crashed earlier one left them.
void deleteTemporaries(const std::string& modDir) {
    for (int i = 0; i < kOutputCount; ++i) DeleteFileA(tmpName(modDir, i).c_str());
}

// Deletes the temporaries on every exit that is not the rename into place.
struct TmpGuard {
    const std::string& modDir;
    bool keep = false;
    explicit TmpGuard(const std::string& d) : modDir(d) {}
    ~TmpGuard() {
        if (keep) return;
        deleteTemporaries(modDir);
    }
};

// size + mtime of one file, or "missing".
std::string stampLine(const std::string& path, const std::string& label) {
    WIN32_FILE_ATTRIBUTE_DATA fa;
    char line[512];
    if (GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fa)) {
        unsigned long long size = (unsigned long long(fa.nFileSizeHigh) << 32) | fa.nFileSizeLow;
        unsigned long long mt = (unsigned long long(fa.ftLastWriteTime.dwHighDateTime) << 32)
                              | fa.ftLastWriteTime.dwLowDateTime;
        std::snprintf(line, sizeof line, "%s %llu %llu\n", label.c_str(), size, mt);
    } else {
        std::snprintf(line, sizeof line, "%s missing\n", label.c_str());
    }
    return line;
}

// appended to catalogue.stamp when gray\gray.stamp could not be written (the
// folder cannot be made): the catalogue still counts as current and the gray check is skipped.
const char* const kGrayFailedTag = "\ngray: the gray folder could not be written\n";

// FNV-1a 64 of a text, as 16 hex digits (the stamp's groups line).
std::string stampHash(const std::string& bytes) {
    unsigned long long h = 1469598103934665603ULL;
    for (unsigned char c : bytes) { h ^= c; h *= 1099511628211ULL; }
    char buf[24];
    std::snprintf(buf, sizeof buf, "%016llx", h);
    return buf;
}

// The stamp's groups line: the built-in template's hash, and the user file's bytes' hash while
// one is loaded (a refused or absent file leaves the built-in set alone).
std::string groupsStampLine(const utg::GroupsLoad& load) {
    std::string l = "groups=builtin:" + stampHash(utg::defaultGroupsIni());
    if (load.from == utg::GroupsFile::Loaded) l += "+user:" + stampHash(load.bytes);
    return l + "\n";
}

// The line of `text` that starts with `key`, with its newline; "" when there is none.
std::string stampKeyLine(const std::string& text, const char* key) {
    std::size_t at = 0;
    const std::string k = key;
    while (at < text.size()) {
        std::size_t nl = text.find('\n', at);
        if (nl == std::string::npos) nl = text.size() - 1;
        if (text.compare(at, k.size(), k) == 0) return text.substr(at, nl + 1 - at);
        at = nl + 1;
    }
    return std::string();
}

} // namespace

std::string stampMismatch(const std::string& old, const std::string& now) {
    const std::string was = old.substr(0, old.find('\n'));
    const std::string is = now.substr(0, now.find('\n'));
    if (was != is)
        return "catalogue.stamp does not match: it is \"" + was + "\" and this build writes \"" +
               is + "\" (the mod was updated)";
    // the generation inputs, each a stamp line only while it is on; a flip names its key
    struct Flag { const char* line; const char* key; const char* on; const char* off; };
    static const Flag kFlags[] = {
        {"collect_mi=1\n", "collect_mi", "the monster infrequents are collected",
         "the monster infrequents are left out of the catalogue"},
        {"collect_relics=1\n", "collect_relics", "the relics and charms have their page",
         "no relic page: the relics and charms are left out"},
        {"collect_unobtainable=1\n", "collect_unobtainable",
         "the records nobody can obtain are put back in the catalogue",
         "the records nobody can obtain are left out of the catalogue"},
    };
    std::string o = old, n = now;
    const std::string tag = kGrayFailedTag;   // a gray-failure tag on the old stamp is no game change
    if (o.size() >= tag.size() && o.compare(o.size() - tag.size(), tag.size(), tag) == 0)
        o.erase(o.size() - tag.size());
    std::string why;
    for (const Flag& f : kFlags) {
        const std::size_t len = std::string(f.line).size();
        const std::size_t po = o.find(f.line), pn = n.find(f.line);
        if ((po == std::string::npos) == (pn == std::string::npos)) continue;
        if (po != std::string::npos) o.erase(po, len);
        if (pn != std::string::npos) n.erase(pn, len);
        if (!why.empty()) why += ", and ";
        why += std::string(f.key) + " changed to " + (pn != std::string::npos ? "1" : "0") + " (" +
               (pn != std::string::npos ? f.on : f.off) + ")";
    }
    // the groups: the built-in template or the player's uniquetab-groups.ini changed
    const std::string go = stampKeyLine(o, "groups="), gn = stampKeyLine(n, "groups=");
    if (go != gn) {
        if (!go.empty()) o.erase(o.find(go), go.size());
        if (!gn.empty()) n.erase(n.find(gn), gn.size());
        if (!why.empty()) why += ", and ";
        why += "the groups changed (uniquetab-groups.ini or the built-in groups)";
    }
    if (!why.empty()) {
        if (o != n) why += ", and the game files changed";
        return why;
    }
    return "catalogue.stamp does not match the game files (the game was updated)";
}

namespace {

// Every archive a custom game brings, as Engine::InitializeMod reads them: its .arz, then every
// .arc of its Resources folder and of the folders in it (an expansion folder), each list sorted -
// so the fingerprint follows the game's own files the way it follows the base game's (GD's
// customLayerFiles).
void gameLayerFiles(const GameSpec& v, std::vector<std::string>& abs) {
    abs.push_back(v.arzPath());
    std::vector<std::string> dirs{v.resourcesDir()};
    {
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA((v.resourcesDir() + "\\*").c_str(), &fd);
        std::vector<std::string> sub;
        if (h != INVALID_HANDLE_VALUE) {
            do {
                const std::string n = fd.cFileName;
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && n != "." && n != "..")
                    sub.push_back(v.resourcesDir() + "\\" + n);
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
        std::sort(sub.begin(), sub.end());
        dirs.insert(dirs.end(), sub.begin(), sub.end());
    }
    for (const std::string& dir : dirs) {
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA((dir + "\\*.arc").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        std::vector<std::string> found;
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                found.push_back(dir + "\\" + fd.cFileName);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
        std::sort(found.begin(), found.end());
        for (const std::string& f : found) abs.push_back(f);
    }
}

std::string fingerprintOf(const std::string& gameDir, const std::string& lang,
                          const GenInputs& in, const std::string& groupsLine, const GameSpec& v) {
    // TQ: one database, one text archive, and the item / UI archives of the base game and each
    // expansion folder. Eternal Embers names its item archive Item.arc, so both spellings are
    // fingerprinted everywhere; the absent one is a stable "missing" line. The UI archives are
    // not read but are fingerprinted as GD's stamp does (the page,, reads them).
    static const char* const kSub[5] = {"", "xpack\\", "XPack2\\", "XPack3\\", "XPack4\\"};
    std::vector<std::string> rel;
    rel.push_back("Database\\database.arz");
    // a translation reads Text_EN.arc under it (arc_reader.h loadTextTags): its line regenerates
    // a set made before that once, and leaves the English stamp as it was
    if (_stricmp(lang.c_str(), "EN") != 0) rel.push_back("Text\\Text_EN.arc");
    rel.push_back("Text\\Text_" + lang + ".arc");
    for (const char* s : kSub) {
        rel.push_back(std::string("Resources\\") + s + "Items.arc");
        rel.push_back(std::string("Resources\\") + s + "Item.arc");
        rel.push_back(std::string("Resources\\") + s + "UI.arc");
    }
    // The first line is the OUTPUT format: bump it whenever the generated files change for the
    // same inputs, so an installation that already has a stamp regenerates once with the new
    // build instead of keeping text the DLL no longer expects. Titan Quest restarts the count
    // at 1 (catalogue.bin v2, uniq-records / uniq-groups / uniq-excluded); 3 is EXCLUDE rule 7,
    // 4 the monster infrequents, 5 the group ids and tags on uniq-groups.txt's G line, 6 the
    // arcane formulae (the group blueprint / BLU), 7 the groups as data (the groups line: the
    // built-in template's hash and the player's uniquetab-groups.ini).
    // 8 the caravan geometry (uniq-geometry.txt, ui_geometry.h; the pages are laid out on its grid).
    // 9 one display name's tiers in item-level order on the page (normal < epic < legendary).
    // 10 the caravan background's place (its BackgroundImage record's bitmapPositionX/Y) on
    // uniq-geometry.txt's W line.
    // 11 the collection rule on every layer (no path rule, whole template words, the reach walk).
    // 12 the relic and charm list (uniq-relics.txt, relics_gen.h) and the gray copies of its icons.
    // 13 the relic page's group (uniq-groups.txt's G line "relics", no E line).
    std::string fp = "GDUT-STAMP 13\nlang=" + lang + "\n";
    // a custom game: its leaf (the start-up game's stamp has no such line, so it reads as before)
    if (!v.startup()) fp += "game=" + v.name() + "\n";
    fp += groupsLine;
    // collect_mi, collect_relics, collect_unobtainable: a line only while it is on; a flip either
    // way regenerates once.
    if (in.collectMi) fp += "collect_mi=1\n";
    if (in.collectRelics) fp += "collect_relics=1\n";
    if (in.collectUnobtainable) fp += "collect_unobtainable=1\n";
    for (const std::string& r : rel) fp += stampLine(gameDir + "\\" + r, r);
    if (!v.startup()) {   // the game's own archives after the base game's
        // each named by its path inside the game's folder, in lower case: the stamp written at
        // start-up (the folder as found on disk) and the one read at the entry (the folder as the
        // engine spells it) agree whatever the folder's spelling
        std::vector<std::string> abs;
        gameLayerFiles(v, abs);
        const std::string dir = v.dir();
        for (const std::string& a : abs) {
            std::string key = a.size() > dir.size() && _strnicmp(a.c_str(), dir.c_str(), dir.size()) == 0
                                  ? a.substr(dir.size() + 1)
                                  : a;
            for (char& c : key)
                if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            fp += stampLine(a, key);
        }
    }
    return fp;
}

} // namespace

std::string inputFingerprint(const std::string& gameDir, const std::string& lang,
                             const GenInputs& in, const GameSpec* game) {
    utg::GroupSet set;
    utg::GroupsLoad load;
    utg::loadGroupsFile(in.groupsPath, set, &load);
    return fingerprintOf(gameDir, lang, in, groupsStampLine(load), game ? *game : GameSpec());
}

namespace {
// the gray copy of every catalogue icon, gray\ug<hash>.tex
// beside the catalogue (ut_graytex.h: R = G = B = the luma, alpha kept; the same TEX layout and
// size). Only files named ug<16 hex>.tex inside the mod's own gray\ folder are ever written; a
// failure is a warning (ut_panel's veil stands in for that record), never the catalogue's.
// The file is named by the RECORD PATH alone, so a record a custom game overrides with another
// icon would collide with the base game's copy: each game writes into its own folder's gray\
// (modDir is that game's folder), and the two never meet.
const char* const kGrayDir = "gray";
const char* const kGrayStamp = "gray.stamp";
void writeGrayIcons(const GameData& g, const std::vector<CatalogueItem>& items,
                    const std::string& modDir, GenReport& report) {
    const std::string dir = modDir + "\\" + kGrayDir;
    if (!CreateDirectoryA(dir.c_str(), nullptr)) {
        const DWORD e = GetLastError();
        if (e != ERROR_ALREADY_EXISTS) {
            report.grayFailed = items.size();
            report.warnings.push_back("gray icons: the folder " + dir + " cannot be made (error " +
                                      std::to_string(e) + ") - owned_marks=3 draws the veil");
            return;
        }
    }
    std::vector<std::uint8_t> data;
    std::string first;
    for (const CatalogueItem& it : items) {
        char name[32];
        std::string err;
        data.clear();
        const char* why = nullptr;
        if (it.bitmap.empty() || !ut::utGrayName(it.record.c_str(), name, sizeof name)) {
            why = "no icon";
        } else if (!g.res->read(it.bitmap, data, 0, &err) || data.empty()) {
            why = "the icon does not read from the archives";
        } else if (ut::utGrayTex(data.data(), data.size()) == ut::kUtGrayNone) {
            why = "the icon is not a surface the gray copy knows (DDS / DDSR 32 / 24 bpp, DXT1/3/5)";
        } else if (!writeBytes(dir + "\\" + name, data.data(), data.size())) {
            why = "the gray file cannot be written";
        }
        if (why) {
            if (first.empty()) first = it.record + ": " + why;
            ++report.grayFailed;
            continue;
        }
        ++report.grayWritten;
        report.grayBytes += data.size();
    }
    if (report.grayFailed)
        report.warnings.push_back("gray icons: " + std::to_string(report.grayFailed) +
                                  " record(s) without a gray copy (the veil stands in), the first " +
                                  first);
}

// the gray copies of the relic and charm icons uniq-relics.txt names (relicBitmap and
// shardBitmap), gray\ug<hash>.tex named by the ICON path (utGrayName over the path as the R line
// spells it; the hash folds case and slashes): a record has two icons, and one icon can serve
// several records. Written into the folder writeGrayIcons made; a failure is a warning.
void writeGrayRelicIcons(const GameData& g, const std::vector<RelicItem>& relics,
                         const std::string& modDir, GenReport& report) {
    const std::string dir = modDir + "\\" + kGrayDir;
    std::set<std::string> done;
    std::vector<std::uint8_t> data;
    std::string first;
    for (const RelicItem& r : relics) {
        for (const std::string* icon : {&r.item.bitmap, &r.shardBitmap}) {
            if (icon->empty() || !done.insert(*icon).second) continue;
            char name[32];
            std::string err;
            data.clear();
            const char* why = nullptr;
            if (!ut::utGrayName(icon->c_str(), name, sizeof name)) {
                why = "no gray name";
            } else if (!g.res->read(*icon, data, 0, &err) || data.empty()) {
                why = "the icon does not read from the archives";
            } else if (ut::utGrayTex(data.data(), data.size()) == ut::kUtGrayNone) {
                why = "the icon is not a surface the gray copy knows (DDS / DDSR 32 / 24 bpp, DXT1/3/5)";
            } else if (!writeBytes(dir + "\\" + name, data.data(), data.size())) {
                why = "the gray file cannot be written";
            }
            if (why) {
                if (first.empty()) first = *icon + ": " + why;
                ++report.grayRelicFailed;
                continue;
            }
            ++report.grayRelicWritten;
        }
    }
    if (report.grayRelicFailed)
        report.warnings.push_back("gray icons: " + std::to_string(report.grayRelicFailed) +
                                  " relic or charm icon(s) without a gray copy, the first " + first);
}
}  // namespace

bool ensureOutputs(const std::string& gameDir, const std::string& modDir,
                   const std::string& lang, GenReport& report,
                   GenStartFn onStart, void* ctx, const GenInputs& in, const GameSpec* game) {
    report = GenReport();
    const GameSpec startup;
    const GameSpec& v = game ? *game : startup;
    report.game = v.name();
    if (!v.startup() && report.game.empty()) {
        report.error = "the custom game \"" + v.modName + "\" has no name a folder can carry";
        return false;
    }
    // The groups: the built-in set (ut_groups_default.h, compiled in, never written) with the
    // user's optional uniquetab-groups.ini merged IN FRONT of it (utg::mergeGroupSets: its blocks
    // are tried first). A file that cannot be read or parsed is one warning naming it and its
    // line, and the built-in set alone. The built-in text's hash and the player's bytes are stamp
    // inputs, so a new build's groups or an edit regenerate once.
    utg::GroupSet groupSet;
    utg::GroupsLoad groupsLoad;
    utg::loadGroupsFile(in.groupsPath, groupSet, &groupsLoad);
    if (groupsLoad.from == utg::GroupsFile::Refused)
        report.warnings.push_back("uniquetab-groups.ini is ignored - " + groupsLoad.why +
                                  "; the built-in groups are used");
    {
        const utg::MergeStats& ms = groupsLoad.stats;
        if (groupsLoad.from == utg::GroupsFile::Loaded) {
            report.groupsLine = "groups from " + in.groupsPath + ": " + std::to_string(ms.builtin) +
                                " built-in + " + std::to_string(ms.yours) + " of yours (" +
                                std::to_string(ms.extended) + " built-in group(s) given more rules)";
        } else {
            report.groupsLine = "the built-in groups (" + std::to_string(ms.builtin) + ")";
        }
    }
    std::string fp = fingerprintOf(gameDir, lang, in, groupsStampLine(groupsLoad), v);
    std::string stampPath = modDir + "\\" + kStampName;
    std::string old;
    for (const char* o : kOutputs) {
        if (!fileExists(modDir + "\\" + o)) { report.reason = std::string(o) + " is missing"; break; }
    }
    if (report.reason.empty()) {
        if (!readText(stampPath, old)) report.reason = "no catalogue.stamp yet (first launch)";
        else if (old != fp && old != fp + kGrayFailedTag) report.reason = stampMismatch(old, fp);
    }
    // not when catalogue.stamp says the gray folder could not be written
    if (report.reason.empty() && old != fp + kGrayFailedTag) {   // the gray icons' own stamp
        std::string gs;
        if (!readText(modDir + "\\" + kGrayDir + "\\" + kGrayStamp, gs) || gs != fp)
            report.reason = "the gray icons (owned_marks=3) are missing or older than the catalogue";
    }
    if (report.reason.empty()) {
        report.upToDate = true;
        return true;
    }
    if (in.mayGenerate) {
        std::string why;
        if (!in.mayGenerate(report, in.mayCtx, &why)) {
            report.refused = true;
            report.error = why.empty() ? "the generation was refused" : why;
            return false;
        }
    }
    if (onStart) onStart(report, ctx);

    // Before the first archive is opened, so the log can say what the generation itself
    // added rather than what the whole process happens to be holding.
    double wsBefore = 0.0;
    workingSetMb(&wsBefore, nullptr);

    auto t0 = std::chrono::steady_clock::now();
    deleteTemporaries(modDir);
    std::vector<CatalogueItem> items;
    std::vector<ExcludedItem> excluded;
    std::vector<RelicItem> relics;
    std::vector<ExcludedItem> relicLeft;
    std::string err;
    UiGeometry geo;
    {
        // the archives are open only for the collection pass; their indexes are gone before
        // anything is written
        GameData g;
        if (!g.load(gameDir, lang, &err, v.arzPath(), v.resourcesDir())) {
            report.error = "reading the game archives: " + err;
            return false;
        }
        if (!collectItems(g, items, excluded, report.warnings, &err, in.collectUnobtainable,
                          in.collectMi, &relics, &relicLeft)) {
            report.error = "the collection rule: " + err;
            return false;
        }
        if (!in.collectRelics) {   // no relic page: no row, no G line, no gray relic icon
            relics.clear();
            relicLeft.clear();
        }
        writeGrayIcons(g, items, modDir, report);   // while the archives are open
        writeGrayRelicIcons(g, relics, modDir, report);
        // the caravan geometry through the same merged view; the base game's when it cannot be read
        std::string gerr;
        if (!deriveGeometry(g, geo, &gerr)) {
            geo = UiGeometry();
            report.warnings.push_back("the caravan geometry cannot be derived (" + gerr +
                                      ") - the base game's grid is used");
            geo.note = "grid 27,126 512x480 = 16 x 15 cells of 32 (the base game's; the game's "
                       "records could not be read)";
        }
        report.geometry = geo.note;
        // every custom game's archive that could not be read, once each (arc_reader.h ResourceArcs)
        for (const std::string& s : g.res->skipped())
            report.warnings.push_back("skips an unreadable archive (" + s + "); the rest of its archives are read");
    }
    std::vector<std::uint8_t> bin;
    CatalogueStats st;
    if (!packCatalogue(items, bin, &st, &err)) { report.error = "packing catalogue.bin: " + err; return false; }
    ListsOutput lists;
    // the keys the groups' [view:<key>] blocks answer to: a custom game's leaf (the player's
    // [view:<leaf>] blocks apply in that game only), and collect_mi while the flag is on - the
    // monster-infrequent blocks ([view:collect_mi], ut_groups_default.h): off, no MI is in the
    // catalogue and no block pages one
    std::vector<std::string> viewKeys = utg::viewKeysFor(v.name());
    if (in.collectMi) viewKeys.push_back(utg::kCollectMiViewKey);
    buildLists(items, excluded, lists, groupSet, viewKeys, geo.cols, geo.rows);
    const std::string geoText = geo.fileText();
    RelicListCounts rc;
    const std::string relicsText = relicsListText(relics, relicLeft, &rc);
    report.relicRows = rc.rows;
    report.relicBlocks = rc.relicBlocks + rc.charmBlocks;
    report.relicLeft = rc.left;
    if (rc.rows > 0) {   // the relic page's group: one G line after the collection's, no E line
        utg::GLine gl;
        gl.index = int(lists.groupCount);
        gl.label = utg::kRelicGroupLabel;
        gl.cols = geo.cols; gl.rows = geo.rows; gl.cellW = 32; gl.cellH = 32; gl.count = 0;
        gl.id = utg::kRelicGroupId;
        gl.tag = utg::kRelicGroupTag;
        lists.groups += utg::formatGLine(gl) + "\r\n";
    }
    // temporary names first; nothing in the folder changes until all six are good
    TmpGuard guard(modDir);
    const void* data[kOutputCount] = {bin.data(), lists.records.data(), lists.groups.data(),
                                      lists.excluded.data(), geoText.data(), relicsText.data()};
    std::size_t size[kOutputCount] = {bin.size(), lists.records.size(), lists.groups.size(),
                                      lists.excluded.size(), geoText.size(), relicsText.size()};
    for (int i = 0; i < kOutputCount; ++i) {
        if (!writeBytes(tmpName(modDir, i), data[i], size[i])) {
            report.error = "writing " + tmpName(modDir, i);
            return false;
        }
    }
    {
        gdut::Catalogue check;
        if (!check.loadFromFile(tmpName(modDir, 0), &err) || check.itemCount() != st.items) {
            report.error = "the generated catalogue.bin does not load: " + err;
            return false;
        }
    }
    for (int i = 0; i < kOutputCount; ++i) {
        std::string src = tmpName(modDir, i);
        std::string dst = modDir + "\\" + kOutputs[i];
        if (!MoveFileExA(src.c_str(), dst.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            report.error = "renaming " + src + " into place (error " +
                           std::to_string(GetLastError()) + ")";
            return false;
        }
    }
    guard.keep = true;
    // the gray stamp is written after every attempt (a record without a gray copy gets the
    // veil). when it cannot be written (the folder cannot be made), catalogue.stamp
    // records that (kGrayFailedTag), so a failing folder never makes every launch regenerate.
    const bool grayStamped =
        writeBytes(modDir + "\\" + kGrayDir + "\\" + kGrayStamp, fp.data(), fp.size());
    const std::string stampText = grayStamped ? fp : fp + kGrayFailedTag;
    writeBytes(stampPath, stampText.data(), stampText.size());   // a lost stamp: one regeneration

    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    report.records = st.items;
    report.groups = lists.groupCount;
    std::size_t keptCount = 0;   // rule 7 rows listed in uniq-excluded.txt but kept in the catalogue
    for (const ExcludedItem& x : excluded) keptCount += x.kept ? 1 : 0;
    const std::size_t dropped = excluded.size() - keptCount;
    report.excluded = dropped;
    report.ms = (long long)ms;
    char line[256];
    std::snprintf(line, sizeof line, "%zu records, %zu groups, built in %lld ms", st.items,
                  lists.groupCount, (long long)ms);
    report.summary = line;
    if (in.collectMi) {
        std::snprintf(line, sizeof line,
                      " (collect_mi=1: %zu monster infrequent record(s) in the catalogue)", st.mi);
        report.summary += line;
    }
    if (!in.collectRelics) report.summary += " (collect_relics=0: no relic page)";
    double wsAfter = 0.0, peakMb = 0.0;
    if (workingSetMb(&wsAfter, &peakMb) && peakMb > 0.0) {
        const double added = peakMb > wsBefore ? peakMb - wsBefore : 0.0;
        report.peakAddedMb = added;
        std::snprintf(line, sizeof line, " (peak +%.0f MB, process peak %.0f MB)", added, peakMb);
        report.summary += line;
    }
    if (keptCount)
        std::snprintf(line, sizeof line, "; %zu excluded, %zu kept (uniq-excluded.txt), %zu sets",
                      dropped, keptCount, st.sets);
    else
        std::snprintf(line, sizeof line, "; %zu excluded (uniq-excluded.txt), %zu sets", dropped,
                      st.sets);
    report.summary += line;
    std::snprintf(line, sizeof line, "; %zu gray icons (%.1f MB) in gray\\%s", report.grayWritten,
                  (double)report.grayBytes / 1048576.0,
                  report.grayFailed ? " - some records have none, see the warnings" : "");
    report.summary += line;
    report.groupsLine += ", " + std::to_string(lists.groupCount) + " group(s)";
    report.generated = true;
    return true;
}

namespace {

struct GenArgs {
    const std::string* gameDir;
    const std::string* modDir;
    const std::string* lang;
    GenStartFn onStart;
    void* ctx;
    const GenInputs* in;
    const GameSpec* game;
};

bool runCatching(const GenArgs& a, GenReport& report) {
    try {
        return ensureOutputs(*a.gameDir, *a.modDir, *a.lang, report, a.onStart, a.ctx, *a.in, a.game);
    } catch (const std::bad_alloc&) {
        // names memory, so the next report from a small machine diagnoses itself
        report.error = "out of memory while reading the archives";
    } catch (const std::exception& e) {
        report.error = std::string("an exception: ") + e.what();
    } catch (...) {
        report.error = "an exception";
    }
    return false;
}

void noteFault(const GenArgs& a, GenReport& report, unsigned long code) {
    char line[96];
    std::snprintf(line, sizeof line, "a fault (exception 0x%08lX) while reading the archives", code);
    report.error = line;
    report.generated = false;
    deleteTemporaries(*a.modDir);
}

// No object with a destructor may live in the frame that holds __try (C2712).
bool runGuarded(const GenArgs& a, GenReport& report) {
    __try {
        return runCatching(a, report);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        noteFault(a, report, GetExceptionCode());
    }
    return false;
}

} // namespace

bool ensureOutputsGuarded(const std::string& gameDir, const std::string& modDir,
                          const std::string& lang, GenReport& report,
                          GenStartFn onStart, void* ctx, const GenInputs& in,
                          const GameSpec* game) {
    GenArgs a = {&gameDir, &modDir, &lang, onStart, ctx, &in, game};
    return runGuarded(a, report);
}

} // namespace gen
