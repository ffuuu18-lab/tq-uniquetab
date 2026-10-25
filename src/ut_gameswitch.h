// ut_gameswitch.h - the game switch's pure rules: which game's generated files are in force, the
// entry's decision (wait, read, not built), the load floor with its refusal lines, the
// address-space measure, and the names a
// custom game's gray icons are read by. No hook, no log, no engine call: test_viewgate drives the
// state machine offline; ut_generate.cpp holds the one live instance.
//
// The game in force changes at Engine::InitializeMod (a custom game with its own save files - its
// leaf, utSaveSetGame(utSaveSetLeaf(...)), the folder mods\<leaf>\) and at Engine::CleanseDatabase
// (back to the start-up game, leaf ""). One switch = one call of utGameEnter that returns
// kUtGameSwitched; the caller runs the resets (utGameResets, in kUtResetOrder's order) exactly
// once for it. An entry that cannot be completed takes its state from utGameEntryFailed.
#pragma once

#include <windows.h>

#include <stdio.h>
#include <string.h>

namespace ut {

// Every installed custom game is built when the game starts (ut_generate.h generateGamesStart),
// while the address space is clean; an entry only reads the game's files, which needs this much of
// the largest free block (the catalogue, the groups, a few MB).
const unsigned kUtGameLoadMinMb = 64;

struct UtAddrSpace {
    unsigned largestMb;   // the largest free block
    unsigned totalMb;     // every free block together
};

// VirtualQuery over the user address space (64 KB .. 2 GB - 64 KB).
inline UtAddrSpace utAddrSpace() {
    UtAddrSpace r = {0, 0};
    unsigned long long largest = 0, total = 0;
    const unsigned char* p = (const unsigned char*)0x10000;
    const unsigned char* const end = (const unsigned char*)0x7FFF0000;
    MEMORY_BASIC_INFORMATION mbi;
    while (p < end && VirtualQuery(p, &mbi, sizeof(mbi)) == sizeof(mbi)) {
        const unsigned long long size = mbi.RegionSize;
        if (mbi.State == MEM_FREE) {
            total += size;
            if (size > largest) largest = size;
        }
        const unsigned char* next = (const unsigned char*)mbi.BaseAddress + mbi.RegionSize;
        if (next <= p) break;
        p = next;
    }
    r.largestMb = (unsigned)(largest >> 20);
    r.totalMb = (unsigned)(total >> 20);
    return r;
}

// 0 = go; else the threshold the largest free block misses (reading a game's catalogue).
inline unsigned utGameGuardMiss(unsigned largestMb) {
    return largestMb < kUtGameLoadMinMb ? kUtGameLoadMinMb : 0;
}

// The refusal (one WARN): GD's "the tab is off in this world (the <x> view) because ..." line,
// in TQ's word.
inline void utGameRefusalLine(char* out, size_t cap, const char* leaf, unsigned largestMb,
                              unsigned needMb) {
    _snprintf_s(out, cap, _TRUNCATE,
                "live: the tab is off in this game (the %s game) because the largest free address "
                "block is %u MB, below the %u MB reading its catalogue needs - the main game's "
                "files are untouched; the next entry into this game asks again",
                leaf && leaf[0] ? leaf : "?", largestMb, needMb);
}

// What an entry does with a custom game (Engine::InitializeMod, the game thread). No generation
// runs after start-up: the start-up builds still running are waited for, then a game whose set in
// mods\<leaf>\ is current is read (the load floor above), and a game with no current set - a mod
// installed while the game ran, or a build that failed - has the tab off until the next start
// builds it.
enum : int { kUtEntryLoad = 0, kUtEntryWait, kUtEntryNotBuilt, kUtEntryLowMemory };
inline int utGameEntryStep(bool startupBuilding, bool built, unsigned largestMb) {
    if (startupBuilding) return kUtEntryWait;
    if (!built) return kUtEntryNotBuilt;
    if (utGameGuardMiss(largestMb)) return kUtEntryLowMemory;
    return kUtEntryLoad;
}

// The start-up builds' order (ut_generate.cpp): each game found is pending, being built or done.
// The game an entry waits for (`want`, -1 none) is built before any other; once a world is coming
// (`stop`: a custom game's entry, a world up) no other game is started - a game left pending is
// built at the next start. -1 = no game to start now.
enum : int { kUtGamePending = 0, kUtGameBuilding, kUtGameDone };
inline int utGamesNext(const int* state, int n, int want, bool stop) {
    if (want >= 0 && want < n && state[want] == kUtGamePending) return want;
    if (stop) return -1;
    for (int k = 0; k < n; ++k)
        if (state[k] == kUtGamePending) return k;
    return -1;
}
// An entry waits only while ITS game is pending or being built (-1: the list is not made yet, so
// the game may be in it); a game done, or not in the list, is read or refused by its stamp at once.
inline bool utGamesEntryWaits(int state) {
    return state == -1 || state == kUtGamePending || state == kUtGameBuilding;
}

// kUtEntryNotBuilt's WARN: the refusal's sentence, with why the set is not current (the stamp's
// reason) and when it is built.
inline void utGameNotBuiltLine(char* out, size_t cap, const char* leaf, const char* why) {
    _snprintf_s(out, cap, _TRUNCATE,
                "live: the tab is off in this game (the %s game) because its catalogue is not built "
                "(%s) - every installed custom game is built when the game starts, so this one is "
                "built at the next start; the main game's files are untouched",
                leaf && leaf[0] ? leaf : "?", why && why[0] ? why : "no reason recorded");
}

// The game in force.
struct UtGameState {
    char leaf[96];       // "" = the start-up game
    bool off;            // the tab is off in this game (a refused or failed generation)
    unsigned switches;   // +1 per switch: everything built from a game's files is keyed on it
};

enum : int { kUtGameSame = 0, kUtGameSwitched = 1 };

// Engine::InitializeMod(a, b) loads <b><a>/Database/<a>.arz over the main database: the FIRST
// string is the custom game's name (one folder name), the SECOND the folder that holds it (a
// workshop item's folder, with its trailing separator).
struct UtGameCall {
    const char* name;
    const char* folder;
};
inline UtGameCall utGameCallOf(const char* first, const char* second) {
    UtGameCall c = {first ? first : "", second ? second : ""};
    return c;
}

// Is `name` ONE folder name? 0 = yes; 1 = empty; 2 = "." or ".."; 3 = a separator, a drive or a
// control character in it.
inline int utGameNameFault(const char* name) {
    if (!name || !*name) return 1;
    if (!strcmp(name, ".") || !strcmp(name, "..")) return 2;
    for (const char* p = name; *p; ++p)
        if (*p == '\\' || *p == '/' || *p == ':' || (unsigned char)*p < 32) return 3;
    return 0;
}

// The game in force entered again with its tab on (the engine calls InitializeMod twice in a row
// for some games, before the world loads): nothing to do - no switch, no generation, no settings
// change.
inline bool utGameAgain(const char* inForce, bool off, const char* leaf) {
    return !off && !strcmp(inForce ? inForce : "", leaf ? leaf : "");
}

// Enter `leaf` ("" = the start-up game). The same game in the same state is no switch (a second
// InitializeMod of the game in force, or a CleanseDatabase while the start-up game is in force).
inline int utGameEnter(UtGameState* s, const char* leaf, bool off) {
    const char* l = leaf ? leaf : "";
    if (!strcmp(s->leaf, l) && s->off == off) return kUtGameSame;
    _snprintf_s(s->leaf, sizeof(s->leaf), _TRUNCATE, "%s", l);
    s->off = off;
    ++s->switches;
    return kUtGameSwitched;
}

// Something built under switch `builtAt` is stale once the game has switched again.
inline bool utGameStale(unsigned builtAt, unsigned now) { return builtAt != now; }

// The resets of one switch (hooks.cpp gameResets), on the game thread with no world loaded. Every
// step only drops what the old game's files built: the new game's are built when a reader asks,
// and the old group / KIND / union tables are never freed (a reader still holding one stays
// valid). So only one rule is load-bearing: the world goes first when it is still up. After it,
// what points into the old game's records (the prototypes, the owned counts keyed by its ids) goes
// before the tables those came from, then the index built from the tables, the panel's gray cache
// and button table that read them, and last the game's uniquetab.state.
enum : int {
    kUtResetWorld = 0,   // the world-gone teardown, only when the world-gone edge had not fired
    kUtResetProtos,      // the prototype pool
    kUtResetOwned,       // the owned counts
    kUtResetTables,      // the group, KIND and union tables
    kUtResetIndex,       // the search index
    kUtResetPanel,       // the gray cache and folder, the buttons resolve
    kUtResetState,       // the game's uniquetab.state
    kUtResetSteps
};
const int kUtResetOrder[kUtResetSteps] = {kUtResetWorld,  kUtResetProtos, kUtResetOwned,
                                          kUtResetTables, kUtResetIndex,  kUtResetPanel,
                                          kUtResetState};

// One switch's resets: `step(id, ctx)` for each step of kUtResetOrder, once, the world step only
// when `worldUp`. Returns the steps run.
inline int utGameResets(bool worldUp, void (*step)(int id, void* ctx), void* ctx) {
    int n = 0;
    for (int i = 0; i < kUtResetSteps; ++i) {
        if (kUtResetOrder[i] == kUtResetWorld && !worldUp) continue;
        step(kUtResetOrder[i], ctx);
        ++n;
    }
    return n;
}

// A custom game's entry that cannot be completed (the locator refused its folder, or the entry
// threw). `leaf` is the game's leaf when the locator had named it ("" when not), `inForce` the
// game in force. A named game is entered with the tab off (as a refused guard). Without a name:
// while the start-up game is in force nothing changes (the custom game's database starts from the
// main one); while a custom game is in force its files do not belong to the database now loading,
// so the tab goes off over the start-up game's files until the main database loads again.
// True = enter (`out`, off).
inline bool utGameEntryFailed(const char* leaf, const char* inForce, char* out, size_t cap) {
    if (!out || !cap) return false;
    out[0] = 0;
    if (leaf && leaf[0]) {
        _snprintf_s(out, cap, _TRUNCATE, "%s", leaf);
        return true;
    }
    return inForce && inForce[0];
}

// A generated file of the game in force, relative to the mod folder: the start-up game's own
// files ("catalogue.bin"), a custom game's under mods\<leaf>\.
inline bool utGameRel(const char* leaf, const char* file, char* out, size_t cap) {
    if (!out || !cap || !file) return false;
    const int n = leaf && leaf[0] ? _snprintf_s(out, cap, _TRUNCATE, "mods\\%s\\%s", leaf, file)
                                  : _snprintf_s(out, cap, _TRUNCATE, "%s", file);
    return n > 0;
}

// The engine name of a gray icon `name` (ug<hash>.tex). The start-up game's gray\ folder is a
// source of its own (the bare name); a custom game's icons are read through ONE source, the mods\
// folder, as "<leaf>/gray/<name>" - so the same record never shares a name, nor the engine's
// texture cache, between two games, and no source order decides which copy is read.
inline bool utGameGrayName(const char* leaf, const char* name, char* out, size_t cap) {
    if (!out || !cap || !name) return false;
    const int n = leaf && leaf[0] ? _snprintf_s(out, cap, _TRUNCATE, "%s/gray/%s", leaf, name)
                                  : _snprintf_s(out, cap, _TRUNCATE, "%s", name);
    return n > 0;
}

}  // namespace ut
