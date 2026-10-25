// ut_store.h - the collection outside the game's save: THE PRIVATE TABLE, the owning side.
//
// TQ PORT of the Grim Dawn mod's ut_store.h. GD's rule is kept word for
// word: THERE IS NO SECOND CONTAINER. The journal's own row list IS the table (ut_rescue.h), the
// file is the authority, and everything here is a thin, locked read of it plus the choke points'
// all-or-nothing writes. What TQ dropped: the display cache (TQ's prototypes live in ut_proto, one
// page at a time), the reagent-map census, the takeover, the rescue. What TQ added: the save set
// chosen at world load (ut_depositgate.h utSaveSetLeaf) and the character-save watch that settles
// the journal's pending rows.
//
// THREADING. storeSelectSet / storeOnDeposit / storeOnTake / storeSaveCheck(true) run on the GAME
// thread (the detours and the Update tick); storeWorkerTick on the worker; the reads from either.
#pragma once

#include <windows.h>

#include "ut_depositgate.h"   // UtRollKey
#include "ut_rescue.h"

namespace ut {

// Worker start: the journal's folder, and the one-time move of the mods' journals (ut_folders.h).
void storeInit(HMODULE selfModule);

// World load (game thread): decide the set, bring its GAME's files into force (a mod's own
// settings, state and log in mods\<leaf>\, the main game's in the root - ut_folders.h) and open
// its journal there. `modKnown` / `questKnown` say
// whether the two engine reads succeeded. Returns the journal's usability afterwards.
bool storeSelectSet(bool modKnown, const char* modName, bool questKnown, bool inMainQuest);
// The game switch (Engine::InitializeMod, the game thread): `game`'s settings and log in force
// before its world loads (the world load keeps the main and the custom-without-mod sets),
// and the game it entered, which the world load checks against its own save set (one WARN).
void storeFollowGame(const char* game);
void storeNoteEntered(const char* game);
// World teardown: nothing is closed (the next world reopens or keeps the set), but a pending
// character save is looked for once more.
void storeOnWorldTeardown();

// True while the table OWNS the collection: a save set is open and writable (GD's name).
bool storeTableOwns();
// Rows of `record` (any spelling: it is folded here). 0 without a set.
unsigned int storeCount(const char* record);

// The roll the collection holds for `record` (any spelling): the newest row's prefix, suffix and
// seed (journalNewest, under the journal's lock - never inside an SEH frame). False = no row.
bool storeRollOf(const char* record, UtRollKey* out);
// The deposit / take choke points (GD's names): all-or-nothing, on disk before they return true.
bool storeOnDeposit(const UtReplicaCapture& cap, unsigned long long* seqOut);
bool storeOnTake(const char* record, unsigned long long seq);
// The relic and charm takes (several rows of one record): a whole take of the row its slot hands
// out now (journalTakeRow), and the red slot's take of ONE shard (journalTakeShard: *outSeq = the
// row of the shard handed out, *left = the source's shards after it). Same gate as storeOnTake.
bool storeOnTakeRow(const char* record, unsigned long long seq);
bool storeOnTakeShard(const char* record, unsigned long long seq, unsigned long long* outSeq,
                      unsigned* left);

// The crash window: look at the newest character save (Player.chr under the TQ SaveData folder,
// read only) and settle every pending row written before it. `force` = look now (CaravanGoodbye:
// the engine writes Player.chr just BEFORE it, as measured); otherwise at most once a second.
void storeSaveCheck(bool force);
// The newest Player.chr last-write time under SaveData\Main\*\ and SaveData\User\*\ (the
// USERPROFILE Documents and OneDrive\Documents candidates, or %UNIQUETAB_SAVEDATA% in the offline
// harness). False = none found.
bool storeNewestCharacterSave(unsigned long long* fileTime);
// the Documents known folder is looked at first. The TAKE gate: the table owns the
// collection AND the watch sees a Player.chr (a take the watch can never settle is refused).
bool storeTakeAllowed();
// This process's creation time (FILETIME units), 0 = unknown.
unsigned long long storeProcessStart();

// Worker: the export_csv latch, the journal's lazy writes, the save check.
void storeWorkerTick();

const char* storeStatus();

// The Documents known folder (null when it cannot be resolved). Never under the loader lock.
const wchar_t* storeDocumentsFolder();

}  // namespace ut
