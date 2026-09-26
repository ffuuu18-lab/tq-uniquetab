// generate.h - keeps the generated data files in the mod folder current with the game.
//
// ensureOutputs() fingerprints every input archive (size + mtime of Database\database.arz,
// Text\Text_<lang>.arc and the Items / Item / UI .arc of the base game and the four expansion
// resource folders) into <mod>\catalogue.stamp and, when the stamp differs or an output is
// missing, regenerates catalogue.bin, uniq-records.txt, uniq-groups.txt and uniq-excluded.txt:
// everything is written to temporary names, catalogue.bin is re-read through the model loader,
// and only then are all four renamed into place and the stamp written. On any failure the
// folder keeps the files it had and `error` names the input or rule that failed.
//
// Titan Quest: GD's fifth and sixth outputs - the page overlay uniq-pages.arz and the
// painted cover plates - and the per-view outputs belong to the page, which decides; they
// are not generated here, and uniq-pages.txt with them.
//
// ensureOutputsGuarded() is the same call under SEH and a C++ catch: a corrupt archive turns
// into an error string, never into a crash of the process that loaded the DLL.
#pragma once

#include <string>
#include <vector>

namespace gen {

struct GenReport {
    bool generated = false;        // the four files were (re)written
    bool upToDate = false;         // the stamp matched and every output exists
    std::string reason;            // why a generation ran: no stamp, a missing output, a changed input
    std::string error;             // set when the generation failed
    std::string summary;           // counts and milliseconds, for the log
    std::size_t records = 0, groups = 0, excluded = 0;
    long long ms = 0;              // wall time of the generation
    double peakAddedMb = 0.0;      // the rise of the process peak working set over its start
    // the gray icons (gray\ug<hash>.tex, one per record; ut_graytex.h)
    std::size_t grayWritten = 0, grayFailed = 0;
    unsigned long long grayBytes = 0;
    std::vector<std::string> warnings;
};

// Called once, after the decision to (re)generate and before any archive is read, so the caller
// can say so before the seconds the generation takes. Never called when the stamp matches.
typedef void (*GenStartFn)(const GenReport& report, void* ctx);

// gameDir: the game folder (the one holding TQ.exe). modDir: where the outputs and the stamp go,
// no trailing slash. lang: the Text_<lang>.arc suffix.
bool ensureOutputs(const std::string& gameDir, const std::string& modDir,
                   const std::string& lang, GenReport& report,
                   GenStartFn onStart = nullptr, void* ctx = nullptr);
bool ensureOutputsGuarded(const std::string& gameDir, const std::string& modDir,
                          const std::string& lang, GenReport& report,
                          GenStartFn onStart = nullptr, void* ctx = nullptr);

// The fingerprint text of the inputs under gameDir (what catalogue.stamp holds).
std::string inputFingerprint(const std::string& gameDir, const std::string& lang);

} // namespace gen
