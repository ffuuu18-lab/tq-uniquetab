// generate.cpp - see generate.h.
#include "gen/generate.h"
#include "gen/catalogue_gen.h"
#include "gen/arc_reader.h"   // the gray pass reads whole icons
#include "model/catalogue.h"
#include "ut_graytex.h"   // the gray copy of each icon (pure)

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <new>

namespace gen {

namespace {

const char* const kStampName = "catalogue.stamp";
const char* const kOutputs[4] = {"catalogue.bin", "uniq-records.txt", "uniq-groups.txt", "uniq-excluded.txt"};

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
    for (int i = 0; i < 4; ++i) DeleteFileA(tmpName(modDir, i).c_str());
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

} // namespace

std::string inputFingerprint(const std::string& gameDir, const std::string& lang) {
    // TQ: one database, one text archive, and the item / UI archives of the base game and each
    // expansion folder. Eternal Embers names its item archive Item.arc, so both spellings are
    // fingerprinted everywhere; the absent one is a stable "missing" line. The UI archives are
    // not read but are fingerprinted as GD's stamp does (the page,, reads them).
    static const char* const kSub[5] = {"", "xpack\\", "XPack2\\", "XPack3\\", "XPack4\\"};
    std::vector<std::string> rel;
    rel.push_back("Database\\database.arz");
    rel.push_back("Text\\Text_" + lang + ".arc");
    for (const char* s : kSub) {
        rel.push_back(std::string("Resources\\") + s + "Items.arc");
        rel.push_back(std::string("Resources\\") + s + "Item.arc");
        rel.push_back(std::string("Resources\\") + s + "UI.arc");
    }
    // The first line is the OUTPUT format: bump it whenever the generated files change for the
    // same inputs, so an installation that already has a stamp regenerates once with the new
    // build instead of keeping text the DLL no longer expects. Titan Quest restarts the count
    // at 1 (catalogue.bin v2, uniq-records / uniq-groups / uniq-excluded).
    std::string fp = "GDUT-STAMP 2\nlang=" + lang + "\n";
    for (const std::string& r : rel) fp += stampLine(gameDir + "\\" + r, r);
    return fp;
}

namespace {
// the gray copy of every catalogue icon, gray\ug<hash>.tex
// beside the catalogue (ut_graytex.h: R = G = B = the luma, alpha kept; the same TEX layout and
// size). Only files named ug<16 hex>.tex inside the mod's own gray\ folder are ever written; a
// failure is a warning (ut_panel's veil stands in for that record), never the catalogue's.
const char* const kGrayDir = "gray";
const char* const kGrayStamp = "gray.stamp";
// appended to catalogue.stamp when gray\gray.stamp could not be written (the
// folder cannot be made): the catalogue still counts as current and the gray check is skipped.
const char* const kGrayFailedTag = "\ngray: the gray folder could not be written\n";

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
}  // namespace

bool ensureOutputs(const std::string& gameDir, const std::string& modDir,
                   const std::string& lang, GenReport& report,
                   GenStartFn onStart, void* ctx) {
    report = GenReport();
    std::string fp = inputFingerprint(gameDir, lang);
    std::string stampPath = modDir + "\\" + kStampName;
    std::string old;
    for (const char* o : kOutputs) {
        if (!fileExists(modDir + "\\" + o)) { report.reason = std::string(o) + " is missing"; break; }
    }
    if (report.reason.empty()) {
        if (!readText(stampPath, old)) report.reason = "no catalogue.stamp yet (first launch)";
        else if (old != fp && old != fp + kGrayFailedTag) report.reason = "catalogue.stamp does not match the game files (the game was updated)";
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
    if (onStart) onStart(report, ctx);

    // Before the first archive is opened, so the log can say what the generation itself
    // added rather than what the whole process happens to be holding.
    double wsBefore = 0.0;
    workingSetMb(&wsBefore, nullptr);

    auto t0 = std::chrono::steady_clock::now();
    deleteTemporaries(modDir);
    std::vector<CatalogueItem> items;
    std::vector<ExcludedItem> excluded;
    std::string err;
    {
        // the archives are open only for the collection pass; their indexes are gone before
        // anything is written
        GameData g;
        if (!g.load(gameDir, lang, &err)) { report.error = "reading the game archives: " + err; return false; }
        if (!collectItems(g, items, excluded, report.warnings, &err)) {
            report.error = "the collection rule: " + err;
            return false;
        }
        writeGrayIcons(g, items, modDir, report);   // while the archives are open
    }
    std::vector<std::uint8_t> bin;
    CatalogueStats st;
    if (!packCatalogue(items, bin, &st, &err)) { report.error = "packing catalogue.bin: " + err; return false; }
    ListsOutput lists;
    buildLists(items, excluded, lists);
    // temporary names first; nothing in the folder changes until all four are good
    TmpGuard guard(modDir);
    const void* data[4] = {bin.data(), lists.records.data(), lists.groups.data(), lists.excluded.data()};
    std::size_t size[4] = {bin.size(), lists.records.size(), lists.groups.size(), lists.excluded.size()};
    for (int i = 0; i < 4; ++i) {
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
    for (int i = 0; i < 4; ++i) {
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
    report.excluded = excluded.size();
    report.ms = (long long)ms;
    char line[256];
    std::snprintf(line, sizeof line, "%zu records, %zu groups, built in %lld ms", st.items,
                  lists.groupCount, (long long)ms);
    report.summary = line;
    double wsAfter = 0.0, peakMb = 0.0;
    if (workingSetMb(&wsAfter, &peakMb) && peakMb > 0.0) {
        const double added = peakMb > wsBefore ? peakMb - wsBefore : 0.0;
        report.peakAddedMb = added;
        std::snprintf(line, sizeof line, " (peak +%.0f MB, process peak %.0f MB)", added, peakMb);
        report.summary += line;
    }
    std::snprintf(line, sizeof line, "; %zu excluded (uniq-excluded.txt), %zu sets", excluded.size(),
                  st.sets);
    report.summary += line;
    std::snprintf(line, sizeof line, "; %zu gray icons (%.1f MB) in gray\\%s", report.grayWritten,
                  (double)report.grayBytes / 1048576.0,
                  report.grayFailed ? " - some records have none, see the warnings" : "");
    report.summary += line;
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
};

bool runCatching(const GenArgs& a, GenReport& report) {
    try {
        return ensureOutputs(*a.gameDir, *a.modDir, *a.lang, report, a.onStart, a.ctx);
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
                          GenStartFn onStart, void* ctx) {
    GenArgs a = {&gameDir, &modDir, &lang, onStart, ctx};
    return runGuarded(a, report);
}

} // namespace gen
