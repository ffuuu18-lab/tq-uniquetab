// ut_generate.h - the DLL side of src\gen: keeps catalogue.bin and the uniq txt files in the mod
// folder current with the game's own archives before anything reads them.
//
// generateEnsure() is called once from the worker thread, after the exports are resolved and
// before anything reads a generated file or any hook is installed. It resolves the game folder
// and the mod folder through ut_paths.h, takes the text language from the ini, and runs
// gen::ensureOutputsGuarded. A stamp that matches costs one stat per input archive and one info
// line ("catalogue: up to date (stamp matches)"); a first launch or a game update rebuilds the
// files (a few seconds, one info line each way); a failure is one ERROR line and the folder keeps
// the files it already had.
//
// Titan Quest: the custom games' sets (GD's views) are generated at start-up as well
// (generateGamesStart), never inside a running game.
#pragma once

#include <windows.h>

namespace ut {

// The build arena (gen_heap.h), at DLL load: the worker's first step after the settings. The
// stamp of the start-up game and of every installed custom game is checked as the builds will
// check it, with no archive read; when one is stale (a build is due) the arena is reserved now,
// top-down, before the engine fills the address space, and the builds below allocate inside it.
// Nothing is reserved when nothing needs building. Worker thread, once.
void generateArenaReserve(HMODULE selfModule);
// Gives the arena back in one piece (nothing when none is reserved) and says so in one INFO line,
// a memory line (ut_mem.h) whose arena part has its base, size and high-water (and which build
// reached it), and the largest free address block and the free total before and after the
// release (and before the reserve). `memPoint`: the line's point ("the start-up builds ended"),
// said even when no arena was reserved; null: "the build arena released", and nothing when none
// was. Called when the start-up builds end (or stop for a world) and on every way out before
// them. `why`: a literal.
void generateArenaRelease(const char* why, const char* memPoint = nullptr);

// The generation, into the mod folder. Worker thread, once.
void generateEnsure(HMODULE selfModule);

// Every installed custom game (ut_gamelist.h: the workshop items of the game's Steam library and
// the local CustomMaps folder) whose set in mods\<leaf>\ is missing or stale is generated on a
// thread of its own, started here right after the start-up game's catalogue: one game at a time,
// each with its own generation inputs (mods\<leaf>\uniquetab.ini when it exists, else the main
// file's), each in the build arena reset after it (or, when none is reserved, a private heap
// released after it; gen_heap.h); one INFO line per game and one summary with the address space
// before and after; the arena is released when they end. Worker thread, once.
void generateGamesStart(HMODULE selfModule);
// True until the start-up builds have ended. An entry waits only for its own game's build, which
// is then built next (gameBeforeEntry).
bool gamesStartupBusy();
// A world is up (hooks.cpp, the game thread): the start-up builds finish the game in hand and
// start no other; one INFO line names the games left for the next start.
void gamesOnWorld();

// True while a generation is running. The frame counter may not advance during those seconds,
// and the stall check must not read that as a crashed game.
bool generateBusy();

// generateEnsure has returned (the gray copies are written, or never will be
// this launch). The panel adds the gray folder to the engine only after it.
bool generateDone();

// ---- the game in force (ut_gameswitch.h): Engine::InitializeMod / Engine::CleanseDatabase ----
// catalogue.bin and uniq-groups.txt are resolved here: the start-up game's in the mod folder
// (utModFile, as before), a custom game's in mods\<leaf>\. True = the file exists. The gray folder
// (utGameRel) and uniq-excluded.txt (read from the game's folder at its entry) follow the same
// rule without it; catalogue.stamp is read by the generator only.
bool utGameFile(HMODULE selfModule, const char* leaf, char* out, size_t cap);
// The game in force: its leaf ("" = the start-up game), the switch count, the tab off there.
void gameNow(char* leaf, size_t cap, unsigned* switches, bool* off);
bool gameTabOff();
// The Resources folder of the custom game `leaf` ("" for the start-up game), noted at its entry;
// false when `leaf` is not the game last entered. Any thread.
bool gameResourcesOf(const char* leaf, char* out, size_t cap);

struct UtGameEntry {
    char leaf[96];
    bool ok;    // a switch is due after the engine's call
    bool off;   // the tab is off in that game (the guard refused, or the generation failed)
};
// Engine::InitializeMod BEFORE the original (the game thread, no world loaded): the game's folder,
// its settings in force, the wait for the start-up builds, the stamp (nothing is generated here: a
// set that is not current turns the tab off until the next start builds it), the load floor. False = nothing to
// switch (an unusable name while the start-up game is in force, or the game already in force); an
// unusable name while a custom game is in force turns the tab off (utGameEntryFailed).
bool gameBeforeEntry(HMODULE selfModule, const char* root, const char* name, UtGameEntry* e);
// AFTER the original: the game in force changes; true = a switch (the caller runs the resets once).
bool gameAfterEntry(const UtGameEntry& e, bool engineOk);
// gameBeforeEntry threw (out of memory?): utGameEntryFailed decides - the game it had named goes
// off; with none named, a custom game in force turns the tab off until the main database loads.
// One ERROR. True = a switch is due (gameAfterEntry).
bool gameEntryFailed(UtGameEntry* e);
// Engine::CleanseDatabase: back to the start-up game; true = a switch (the resets once).
bool gameEnterStartup();

}  // namespace ut
