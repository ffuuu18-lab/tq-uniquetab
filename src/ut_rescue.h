// ut_rescue.h - the journal: the mod's own record of every deposited item, one row per stored COPY
// with its full identity, and its CSV export. Pure file I/O; not one engine call lives in here.
//
// TQ PORT of the Grim Dawn mod's ut_rescue.h (HANDOFF.md section 7 has the TQ format).
// What stayed GD's, verbatim in shape: the JSON Lines file with a header
// line carrying `format`, the atomic whole-file write (tmp + FlushFileBuffers + MoveFileEx
// WRITE_THROUGH), the flat hand-rolled reader where a bad line costs that line only, the format
// gate (a HIGHER number = READ-ONLY for the session), "a file that is there and cannot be used is
// never treated as empty", the copy-aside of a file that did not parse cleanly, the deposit
// commit that puts the previous state back on any failure, the CSV export with its mode latch.
//
// What TQ forced (format TQ-1, `tq-uniq-items*.jsonl`):
//   * ONE ROW PER STORED COPY. A GD reagent is a stack of identical copies (record -> count); a TQ
//     unique is an individual - seed, affixes, relics - and the identity is not even unique (a
//     15-bit seed: two copies can be byte-identical), so the table is a LIST of rows and every
//     question the mod asks ("is it collected", "may it be taken", "how many") COUNTS ROWS.
//   * the identity is the ItemReplicaInfo's ten fields, NAMED (the TQ replica is 0xBC bytes with a
//     layout measured in the game): the seven strings byte-exact, seed / var1 / var2,
//     the +0xB8 byte, and the stack. No raw blob, no slot scan, no identity overlay.
//   * the key is the baseName FOLDED (lower case, '\' -> '/': GD's utOwnedNormaliseKey), the raw
//     baseName is kept byte-exact for Item::CreateItem.
//   * a row carries a `pending` state for the crash window between the journal write and the
//     next Player.chr save: "in" (deposited, the character file on disk still holds the item) and
//     "out" (taken, the character file on disk does not hold it yet). journalSaveObserved clears
//     them; an "out" row found at a load is RESTORED (the take never reached a save: the item is
//     back in the collection - a duplicate at worst, never a loss).
//   * one file per SAVE SET, chosen at world load: journalOpenSet. No set = nothing usable.
//   * no GDS export (no GD Stash for TQ), no binary migration, no reconcile/prune, no rescue.
//   * a pending row the load cannot settle by a save's time is settled against the
//     character's OWN containers (ut_recon.cpp): utPendingVerdict below decides, by replica
//     identity (utReplicaSame), and the journal applies it once (journalApplyVerdicts).
#pragma once

#include <windows.h>

#include <stddef.h>

#include "ut_ownedfold.h"

namespace ut {

// The seven strings of the replica, in replica order (+0x04 .. +0x9C).
enum UtIdString {
    kUtIdBase = 0, kUtIdPrefix, kUtIdSuffix, kUtIdRelic, kUtIdRelicBonus, kUtIdRelic2,
    kUtIdRelicBonus2, kUtIdStrCount
};
const unsigned kUtIdStrMax = 260;   // one string with its NUL; a longer one is refused
extern const char* const kUtIdStrKey[kUtIdStrCount];   // the journal's key for each string

// What a deposit captures (GD's name). Fixed size on purpose: it is filled inside an engine frame.
struct UtReplicaCapture {
    char record[256];                       // the KEY: str[kUtIdBase] folded
    char str[kUtIdStrCount][kUtIdStrMax];   // byte-exact
    unsigned int seed;                      // replica +0x7C
    unsigned int var1;                      // replica +0x80
    unsigned int var2;                      // replica +0xB4
    unsigned int b8;                        // replica +0xB8 (one byte, 0..255)
    unsigned int stack;                     // Item stack count (1 for every unique)
};

// The journal format this build writes and is willing to write BACK (TQ-1). A file with a higher
// number is loaded as far as it goes and then the journal is READ-ONLY for the session.
#define UT_JOURNAL_FORMAT 1
// Line 1's "journal" value. A file that names anything else (a GD journal) is never read as ours.
#define UT_JOURNAL_NAME "titan quest uniquetab"

// The key fold, and a capture's own check: every string printable ASCII, the base non-empty,
// b8 <= 255, and `record` equal to the fold of the base. False = the row must not be written.
// does a character save written at `saveTime` settle a pending row written at
// `rowTime`, judged by a LOAD in a process created at `processStart` (FILETIME units, 0 = unknown)?
//   * a row of THIS process (rowTime >= processStart): any newer save settles it (the runtime rule);
//   * a row of an EARLIER process: only a save older than this process's start - a save this
//     process wrote came from a character file that never saw the row's move, so it proves nothing
//     (a crash after the move must never be settled by the current load).
// Unknown times settle nothing: the row is restored / reported, a duplicate at worst.
inline bool utLoadSettles(unsigned long long rowTime, unsigned long long saveTime,
                          unsigned long long processStart) {
    if (!rowTime || !saveTime || !processStart || saveTime <= rowTime) return false;
    if (rowTime >= processStart) return true;
    return saveTime < processStart;
}

// ---- the pending rows against the character's containers (pure) -------------------------
// Where a pending row's item was looked for:
//   kUtFoundUnknown       an export missing, a fault, a store not walkable: nothing is proven
//   kUtFoundPresent       an item with the row's replica identity IS in one of the containers
//   kUtFoundAbsent        it is in NONE of them, and every container was searched (the inventory
//                         sacks, the equipment, and the Stash / Transfer / Relic Vault)
//   kUtFoundAbsentSoFar   not in the inventory / equipment, the caravan sacks not searched yet
enum UtFound { kUtFoundUnknown = 0, kUtFoundPresent, kUtFoundAbsent, kUtFoundAbsentSoFar };
enum UtVerdict {
    kUtVerdictKeep = 0,   // settle nothing: the row stays pending (today's rule applies later)
    kUtVerdictErase,      // the row goes: "in" = the removal never persisted (the item is back);
                          //               "out" = the take persisted (the item is the player's)
    kUtVerdictSettle,     // "in" and the item is nowhere: the removal persisted - collected
    kUtVerdictRestore     // "out" and the item is nowhere: the take never persisted - collected
};
// `state`: 1 "in" (deposited), 2 "out" (taken); anything else keeps.
inline int utPendingVerdict(int state, int found) {
    if (state != 1 && state != 2) return kUtVerdictKeep;
    if (found == kUtFoundPresent) return kUtVerdictErase;
    if (found == kUtFoundAbsent) return state == 1 ? kUtVerdictSettle : kUtVerdictRestore;
    return kUtVerdictKeep;   // unknown, or the caravan sacks are still to be searched
}
// The replica identity the deposit captured, EXACT: the seven strings byte for byte, seed, var1,
// var2 (the ten identity fields; the key is the fold of the base, so it follows).
inline bool utReplicaSame(const UtReplicaCapture& a, const UtReplicaCapture& b) {
    for (int k = 0; k < kUtIdStrCount; ++k) {
        const char* p = a.str[k];
        const char* q = b.str[k];
        unsigned i = 0;
        for (; i < kUtIdStrMax && p[i] == q[i] && p[i]; ++i) {
        }
        if (i >= kUtIdStrMax || p[i] != q[i]) return false;
    }
    return a.seed == b.seed && a.var1 == b.var1 && a.var2 == b.var2;
}
// One live item against the pending rows: it proves AT MOST ONE row (two byte-identical rows need
// two items). `found[i]` != 0 = row i already has its item. Returns the row matched, or -1.
inline int utMatchLive(const UtReplicaCapture* const* rows, const int* found, int n,
                       const UtReplicaCapture& live) {
    for (int i = 0; i < n; ++i)
        if (!found[i] && rows[i] && utReplicaSame(*rows[i], live)) return i;
    return -1;
}

inline bool utJournalKey(const char* baseName, char* out, size_t cap) {
    return utOwnedNormaliseKey(baseName, out, cap);   // GD's fold: lower case, '\\' -> '/'
}
bool utCaptureValid(const UtReplicaCapture& cap, char* why, size_t whyCap);

// Resolves the mod folder (%UNIQUETAB_OUT% first). Opens NO file: the set is not known before a
// world exists. Safe to call twice.
bool journalInit(HMODULE selfModule);

// At world load (game thread): flush and close the open set, then open `leaf` ("tq-uniq-items"
// or "tq-uniq-items-<mod>" or "tq-uniq-items-custom"; ".jsonl" / ".csv" are appended). An empty
// or null leaf = the UNKNOWN set: everything is closed and every move is refused. Returns true
// when the set is open (READ-ONLY included). The pending report of the file is logged here.
// `saveTime` = the newest character save's last-write time (0 = none seen) and
// `processStart` = this process's creation time (0 = unknown). Pending rows are settled against
// them BEFORE the load restores anything (utLoadSettles); the defaults settle nothing (every
// "out" row is restored - a duplicate at worst).
// `defer`: the rows a save's time cannot settle wait, UNRESOLVED, for the container check
// (journalApplyVerdicts) or its fallback (journalUnresolvedFallback, today's rule).
// an "out" row is RESTORED at the open all the same (on disk: no stale "pending out" may meet a
// save of this session); only a find (present) erases it later. An unresolved "in" row still
// pending at a character save gets today's WARN, then that save settles it (journalSaveObserved).
// false = today's rule at the open (the offline tests).
bool journalOpenSet(const char* leaf, unsigned long long saveTime = 0,
                    unsigned long long processStart = 0, bool defer = false);
bool journalSetKnown();
const char* journalSetLeaf();

// True when the file on disk could not be used or carries a newer format: nothing is written.
bool journalReadOnly();
// A set is open, has a path and is not read-only: deposits and takes may be journalled.
bool journalUsable();

HANDLE journalEvent();
// Worker thread: writes the file when a non-critical change is pending (a pending flag cleared),
// or the CSV when only the export mode changed.
void journalService();
// The FULL write, callable from any thread; false = READ-ONLY or the atomic write failed.
bool journalFlushNow();

// ---- the table: counted in ROWS -------------------------------------------------------------
// Rows of `key` in the collection (taken-pending rows are not). 0 without a set. No allocation.
unsigned int journalRows(const char* key);
unsigned int journalCollectedTotal();   // every row in the collection
size_t journalCount();                  // every row in the file (taken-pending included)
// The NEWEST row of `key` in the collection (the prototype is built from it; a take removes it).
// `seq` identifies the row for journalTakeCommit. False = none.
bool journalNewest(const char* key, UtReplicaCapture* out, unsigned long long* seq);

// THE DEPOSIT'S ONE ALL-OR-NOTHING STEP (GD's journalDepositCommit): refuse if not usable,
// append the row ("pending":"in"), write the whole file atomically on this thread, and on any
// failure take the row out again (*rolledBack) and return false. TRUE means the row is ON DISK.
bool journalDepositCommit(const UtReplicaCapture& cap, unsigned long long* seqOut,
                          bool* rolledBack);
// THE TAKE'S step: row `seq` must be the newest in-collection row of `key`; it is marked taken
// ("pending":"out", it no longer counts) and the file is written. On failure the mark is undone.
bool journalTakeCommit(const char* key, unsigned long long seq, bool* rolledBack);

// ---- the crash window -----------------------------------------------------------------------
// A character save was observed with the given last-write time (FILETIME ticks): every "in" row
// written before it loses its pending mark, every "out" row taken before it is dropped (the item
// is in the character file now). Returns how many rows changed; the change is written by the
// worker (journalService). 0 = nothing to do.
int journalSaveObserved(unsigned long long saveWriteTime);
// the "out" row `seq` belongs to a take whose item is still ON THE CURSOR: no save
// settles it (the file watch proves a save, not that the save holds a cursor item). 0 = none.
void journalSetHeldSeq(unsigned long long seq);
// ... and the item left the cursor: its "out" row's taken time becomes NOW, so only a character
// save written after this moment settles it. False when no "out" row has that seq.
bool journalTouchOut(unsigned long long seq);
size_t journalPendingIn();
size_t journalPendingOut();

// ---- the unresolved rows (a load's pending rows, waiting for the container check) ------
// Session state only: the file keeps "pending"; the save watch (journalSaveObserved) does not
// touch an unresolved row (a save of THIS process proves nothing about an earlier one's move).
struct UtPendingRow {
    unsigned long long seq;
    int state;                 // 1 "in", 2 "out"
    UtReplicaCapture id;
};
size_t journalUnresolved();
// Copies up to `cap` unresolved rows (file order). Returns how many were copied.
size_t journalUnresolvedRows(UtPendingRow* out, size_t cap);
// One verdict per row (`seq`); `where` names the container ("inventory", "equipment", "caravan
// stash", "transfer stash", "relic vault"; null when absent). Keep leaves the row unresolved.
// ONE line per decided row: INFO for a find (present), today's WARN wording for "absent"
// (absence is judged against the LOADED character only; the rows do not record
// their character). An unresolved "out" row was restored at the open: Erase
// removes it, Restore only resolves it. `fallbackWhy` non-null (the caravan pass): the rows still
// unresolved after the verdicts get today's rule IN THE SAME WRITE. The journal
// is written ONCE (atomic) when anything changed. Returns the number of rows decided; -1 when the
// write failed (the change stays and the worker retries).
struct UtVerdictItem {
    unsigned long long seq;
    int verdict;
    const char* where;
};
int journalApplyVerdicts(const UtVerdictItem* v, size_t n, const char* fallbackWhy = nullptr);
// The container check could not decide (`why`): TODAY'S rule for every unresolved row - an "out"
// row is RESTORED (WARN), an "in" row is reported (WARN) and left to the next save. Returns the
// rows it touched.
int journalUnresolvedFallback(const char* why);

// ---- the CSV export (`export_csv`) ----------------------------------------------------------
// `tq-uniq-items*.csv` beside the journal, written by the same atomic writer after every
// successful journal write. 0 off, 1 the collection's rows, 2 the taken-pending rows as well.
// PASS THE MODE, NOT A BOOL (GD's deleted overload).
void journalSetCsvExport(int mode);
void journalSetCsvExport(bool on) = delete;

const char* journalPath();
const char* journalCsvPath();
long journalWrites();
long journalCsvWrites();

}  // namespace ut
