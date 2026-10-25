// generate.h - keeps the generated data files in the mod folder current with the game.
//
// ensureOutputs() fingerprints every input archive (size + mtime of Database\database.arz,
// Text\Text_<lang>.arc and the Items / Item / UI .arc of the base game and the four expansion
// resource folders) into <mod>\catalogue.stamp and, when the stamp differs or an output is
// missing, regenerates catalogue.bin, uniq-records.txt, uniq-groups.txt, uniq-excluded.txt,
// uniq-geometry.txt (the caravan geometry, ui_geometry.h) and uniq-relics.txt (relics_gen.h):
// everything is written to temporary names, catalogue.bin is re-read through the model loader,
// and only then are all six renamed into place and the stamp written. On any failure the
// folder keeps the files it had and `error` names the input or rule that failed.
//
// A custom game (GameSpec, GD's view): its .arz over database.arz, its Text.arc tags over
// Text_<lang>.arc, its own archives searched before the game's; the outputs go to the modDir the
// caller passes (the game's folder mods\<leaf>\, ut_folders.h), its gray\ folder with them; the
// stamp carries "game=<leaf>" and the size + mtime of every archive of the game after the base
// game's lines. The start-up game's stamp and outputs are exactly what they were without it.
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
    std::string game;              // GameSpec::name() of the outputs: "" the start-up game
    bool generated = false;        // the six files were (re)written
    bool upToDate = false;         // the stamp matched and every output exists
    std::string reason;            // why a generation ran: no stamp, a missing output, a changed input
    std::string error;             // set when the generation failed
    bool refused = false;          // GenInputs::mayGenerate said no: nothing was generated
    std::string summary;           // counts and milliseconds, for the log
    std::string geometry;          // the caravan geometry's note (ui_geometry.h), for the log
    std::size_t records = 0, groups = 0, excluded = 0;
    long long ms = 0;              // wall time of the generation
    double peakAddedMb = 0.0;      // the rise of the process peak working set over its start
    // the gray icons (gray\ug<hash>.tex, one per record; ut_graytex.h)
    std::size_t grayWritten = 0, grayFailed = 0;
    unsigned long long grayBytes = 0;
    // uniq-relics.txt (relics_gen.h): its R lines, B lines and X lines, and the gray copies of
    // the relicBitmap / shardBitmap icons it names (gray\ug<hash of the icon path>.tex)
    std::size_t relicRows = 0, relicBlocks = 0, relicLeft = 0;
    std::size_t grayRelicWritten = 0, grayRelicFailed = 0;
    std::vector<std::string> warnings;
    // the groups the lists were made with: "the built-in groups (25)" or "groups from <path>: N
    // built-in + M of yours (K built-in group(s) given more rules)", then ", G group(s)" once
    // the lists are written - one INFO line
    std::string groupsLine;
};

// The inputs besides the archives.
struct GenInputs {
    // uniquetab.ini's collect_unobtainable: the records EXCLUDE rule 7 calls unobtainable stay in
    // the catalogue (catalogue_gen.h). A stamp input ("collect_unobtainable=1" only while it is
    // on): a flip regenerates once, and the reason names the key.
    bool collectUnobtainable = false;
    // uniquetab.ini's collect_mi: the monster infrequents (every Rare equipment record) are in the
    // catalogue, after the uniques of their group. A stamp input ("collect_mi=1" only while it is
    // on): a flip regenerates once, and the reason names the key.
    bool collectMi = true;
    // uniquetab.ini's collect_relics: the relics and charms are on the relic page (uniq-relics.txt
    // lists them, uniq-groups.txt has the "relics" G line, their gray icons are written). Off:
    // uniq-relics.txt lists nothing, no relic G line, no relic gray icon; the catalogue is the
    // same. A stamp input ("collect_relics=1" only while it is on): a flip regenerates once, and
    // the reason names the key.
    bool collectRelics = true;
    // uniquetab-groups.ini beside uniquetab.ini ("" = none): the player's groups, merged IN FRONT
    // of the built-in ones (ut_groups_default.h loadGroupsFile). Read only, never written. Its
    // bytes are a stamp input ("groups=builtin:<hash>+user:<hash>"): an edit regenerates once; a
    // file that cannot be read or parsed is one warning and the built-in groups alone.
    std::string groupsPath;
    // Asked once, after the decision to (re)generate and before onStart and any archive read:
    // false refuses the generation (report.refused, report.error says why; nothing is touched).
    // Null = always go. Not a stamp input.
    bool (*mayGenerate)(const GenReport& report, void* ctx, std::string* why) = nullptr;
    void* mayCtx = nullptr;
};

// WHICH database the outputs describe (GD's ViewSpec; Titan Quest calls it the game). The engine
// builds a custom game's in this order: Engine::InitializeMod(name, root) loads the main
// database, then <root><name>\Database\<name>.arz over it, puts <root><name>\Resources in front
// of the base game's resources and loads the game's own strings. So a game carries the base
// layer and its own: its records win, its tags replace the base ones, its archives come first.
// ut_gameprobe.h utGameLocate checks a root and name and fills one.
struct GameSpec {
    std::string root;      // the folder InitializeMod receives
    std::string modName;   // the name InitializeMod receives (its folder and its .arz); "" = start-up

    bool startup() const { return modName.empty(); }
    std::string dir() const;            // <root>\<name> ("" for the start-up game)
    std::string arzPath() const;        // <dir>\Database\<name>.arz ("" for the start-up game)
    std::string resourcesDir() const;   // <dir>\Resources ("" for the start-up game)
    // The game's leaf: the folder mods\<leaf>\ and the journal's set - ut_depositgate.h
    // utSaveSetLeaf + utSaveSetGame, ONE leaf function, so the folder, the journal and the
    // generated set never disagree. "" for the start-up game and for a name that sanitises to
    // nothing (ensureOutputs then refuses the game).
    std::string name() const;
    // What tells two games' files apart: the leaf ("" for the start-up game).
    std::string tag() const { return name(); }
};

// Called once, after the decision to (re)generate and before any archive is read, so the caller
// can say so before the seconds the generation takes. Never called when the stamp matches.
typedef void (*GenStartFn)(const GenReport& report, void* ctx);

// gameDir: the game folder (the one holding TQ.exe). modDir: where the outputs and the stamp go,
// no trailing slash - for a custom game the caller passes that game's folder (mods\<leaf>), so its
// gray\ icons are its own. lang: the Text_<lang>.arc suffix. game: null = the start-up game.
bool ensureOutputs(const std::string& gameDir, const std::string& modDir,
                   const std::string& lang, GenReport& report,
                   GenStartFn onStart = nullptr, void* ctx = nullptr,
                   const GenInputs& in = GenInputs(), const GameSpec* game = nullptr);
bool ensureOutputsGuarded(const std::string& gameDir, const std::string& modDir,
                          const std::string& lang, GenReport& report,
                          GenStartFn onStart = nullptr, void* ctx = nullptr,
                          const GenInputs& in = GenInputs(), const GameSpec* game = nullptr);

// The fingerprint text of the inputs under gameDir (what catalogue.stamp holds), with a custom
// game's archives after the base game's.
std::string inputFingerprint(const std::string& gameDir, const std::string& lang,
                             const GenInputs& in = GenInputs(), const GameSpec* game = nullptr);

// Why a stamp that differs regenerates: a new output format (the mod was updated), a
// collect_unobtainable / collect_mi / collect_relics flip (named), the groups (named), or the game files.
std::string stampMismatch(const std::string& old, const std::string& now);

} // namespace gen
