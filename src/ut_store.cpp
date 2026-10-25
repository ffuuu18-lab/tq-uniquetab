// ut_store.cpp - the private table's owning side (TQ port of GD's ut_store.cpp; see ut_store.h).
//
// Invariants kept from GD:
//   * a `true` from storeOnDeposit means the row is ON DISK; a false MUST refuse the deposit;
//   * storeTableOwns() is the one question every path asks before it chooses a path;
//   * nothing here writes anything but the mod's own journal (ut_rescue.cpp): the character save
//     folder is only LISTED and its files' last-write times read.
#include "ut_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>

#include "ut_config.h"
#include "ut_depositgate.h"
#include "ut_folders.h"
#include "ut_log.h"
#include "ut_paths.h"

namespace ut {
namespace {

volatile LONG g_lastCheck = 0;   // GetTickCount of the last save check
volatile LONG g_saves = 0;       // save checks that settled at least one row
// 1 = the watch sees a Player.chr, 0 = it does not (takes refused), -1 = not yet
// looked. Written by whichever thread lists the folder; read by the take gate.
volatile LONG g_watch = -1;
volatile LONG g_watchSaid = -1;  // the state the last INFO line reported
char g_status[160] = "store=idle";

// The Documents known folder (a redirected or localised Documents). Resolved at
// run time through shell32's own export (never under the loader lock: the first caller is a world
// load, the Goodbye detour or the worker), cached after the first success.
wchar_t g_docs[MAX_PATH];
volatile LONG g_docsTried = 0;

const wchar_t* documentsFolder() {
    if (InterlockedCompareExchange(&g_docsTried, 1, 0) != 0) return g_docs[0] ? g_docs : nullptr;
    typedef HRESULT(WINAPI * PfnKnown)(const GUID&, DWORD, HANDLE, PWSTR*);
    typedef void(WINAPI * PfnFree)(LPVOID);
    static const GUID kDocuments = {0xFDD39AD0, 0x238F, 0x46AF,
                                    {0xAD, 0xB4, 0x6C, 0x85, 0x48, 0x03, 0x69, 0xC7}};
    HMODULE sh = LoadLibraryW(L"shell32.dll");
    HMODULE ole = LoadLibraryW(L"ole32.dll");
    PfnKnown known = sh ? (PfnKnown)GetProcAddress(sh, "SHGetKnownFolderPath") : nullptr;
    PfnFree freeMem = ole ? (PfnFree)GetProcAddress(ole, "CoTaskMemFree") : nullptr;
    PWSTR path = nullptr;
    if (known && known(kDocuments, 0, nullptr, &path) == S_OK && path) {
        if (wcslen(path) < MAX_PATH - 64) wcscpy_s(g_docs, MAX_PATH, path);
        if (freeMem) freeMem(path);   // (a leak of one string when ole32 is missing, never a crash)
    }
    return g_docs[0] ? g_docs : nullptr;   // the modules stay loaded: the game holds them anyway
}

bool newerIn(const wchar_t* pattern, unsigned long long* best) {
    // pattern = "<SaveData>\\Main" etc.: every "_*" folder's Player.chr.
    wchar_t glob[MAX_PATH];
    _snwprintf_s(glob, MAX_PATH, _TRUNCATE, L"%s\\*", pattern);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(glob, &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool any = false;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
        wchar_t chr[MAX_PATH];
        _snwprintf_s(chr, MAX_PATH, _TRUNCATE, L"%s\\%s\\Player.chr", pattern, fd.cFileName);
        WIN32_FILE_ATTRIBUTE_DATA a;
        if (!GetFileAttributesExW(chr, GetFileExInfoStandard, &a)) continue;
        const unsigned long long t =
            ((unsigned long long)a.ftLastWriteTime.dwHighDateTime << 32) |
            a.ftLastWriteTime.dwLowDateTime;
        if (t > *best) *best = t;
        any = true;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return any;
}

}  // namespace

bool storeNewestCharacterSave(unsigned long long* fileTime) {
    if (!fileTime) return false;
    *fileTime = 0;
    static const wchar_t* const kTail = L"My Games\\Titan Quest - Immortal Throne\\SaveData";
    wchar_t roots[3][MAX_PATH];
    int nRoots = 0;
    wchar_t env[MAX_PATH];
    const DWORD envLen = GetEnvironmentVariableW(L"UNIQUETAB_SAVEDATA", env, MAX_PATH);
    if (envLen > 0 && envLen < MAX_PATH) {
        _snwprintf_s(roots[nRoots++], MAX_PATH, _TRUNCATE, L"%s", env);
    } else {
        // the Documents KNOWN FOLDER first (redirected, another drive, a localised
        // OneDrive name), then the two USERPROFILE guesses as a fallback.
        const wchar_t* docs = documentsFolder();
        if (docs) _snwprintf_s(roots[nRoots++], MAX_PATH, _TRUNCATE, L"%s\\%s", docs, kTail);
        wchar_t home[MAX_PATH];
        const DWORD homeLen = GetEnvironmentVariableW(L"USERPROFILE", home, MAX_PATH);
        if (homeLen > 0 && homeLen < MAX_PATH) {
            _snwprintf_s(roots[nRoots++], MAX_PATH, _TRUNCATE, L"%s\\Documents\\%s", home, kTail);
            _snwprintf_s(roots[nRoots++], MAX_PATH, _TRUNCATE, L"%s\\OneDrive\\Documents\\%s",
                         home, kTail);
        }
    }
    bool any = false;
    for (int i = 0; i < nRoots; ++i) {
        wchar_t sub[MAX_PATH];
        _snwprintf_s(sub, MAX_PATH, _TRUNCATE, L"%s\\Main", roots[i]);
        any = newerIn(sub, fileTime) || any;
        _snwprintf_s(sub, MAX_PATH, _TRUNCATE, L"%s\\User", roots[i]);
        any = newerIn(sub, fileTime) || any;
    }
    return any && *fileTime != 0;
}

unsigned long long storeProcessStart() {
    FILETIME created, exited, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 0;
    return ((unsigned long long)created.dwHighDateTime << 32) | created.dwLowDateTime;
}

namespace {
// One INFO line per change of the watch state.
void noteWatch(bool seen) {
    const LONG now = seen ? 1 : 0;
    InterlockedExchange(&g_watch, now);
    if (InterlockedExchange(&g_watchSaid, now) == now) return;
    if (!seen) {
        logI("journal: no character save (SaveData\\Main|User\\*\\Player.chr) is visible under "
             "the Documents folder - TAKES are refused (a take could not be settled and would "
             "come back at the next load); deposits stay possible");
    } else {
        logD("journal: the character save folder is visible - takes are possible");
    }
}
}  // namespace

bool storeTakeAllowed() {
    return storeTableOwns() && InterlockedCompareExchange(&g_watch, 0, 0) == 1;
}

namespace {
HMODULE g_self = nullptr;

// The per-game log (ut_log.h): mods\<leaf>\uniquetab.log for a mod, no tee for the main game.
void teeFollow(const std::string& game) {
    if (game.empty()) {
        logTeeClose();
        return;
    }
    char dirA[MAX_PATH], pathA[MAX_PATH];
    utModDirA(g_self, dirA, sizeof(dirA));
    _snprintf_s(pathA, sizeof(pathA), _TRUNCATE, "%s\\%s\\uniquetab.log", dirA,
                utCustomGameRelLeaf(game).c_str());
    wchar_t path[MAX_PATH];
    if (!MultiByteToWideChar(CP_ACP, 0, pathA, -1, path, MAX_PATH)) {
        logTeeClose();
        return;
    }
    if (logTeeOpen(path, game.c_str())) {
        logD("log: the %s game's lines also go to \"%s\"", game.c_str(), pathA);
    } else {
        logW("log: \"%s\" could not be opened - the %s game's lines are in this log only", pathA,
             game.c_str());
    }
}

// THE GAME SWITCH, at the world load that chooses the save set: the game's log, then its
// settings and state (configSetGame) - "" = the main game's root files.
void followGame(const std::string& game) {
    teeFollow(game);
    configSetGame(game.c_str());
}
// The game Engine::InitializeMod entered ("" = the start-up game, after Engine::CleanseDatabase);
// `g_enteredSeen` = the game switch has run at least once (its detours are in).
char g_entered[96] = "";
bool g_enteredSeen = false;
}  // namespace

void storeFollowGame(const char* game) { followGame(game ? game : ""); }

void storeNoteEntered(const char* game) {
    _snprintf_s(g_entered, sizeof(g_entered), _TRUNCATE, "%s", game ? game : "");
    g_enteredSeen = true;
}

void storeInit(HMODULE selfModule) {
    g_self = selfModule;
    // journal_dir is handed over HERE and nowhere else (GD's rule): journalInit reads it once and
    // the journals stay where it put them until the next start.
    journalInit(selfModule, g_cfg.journalDir);
    journalSetCsvExport(g_cfg.exportCsv);
    // THE ONE-TIME MOVE (ut_folders.h): every mod's journal still in the root moves into its
    // folder, all or nothing per set, before the first world opens one - in the mod folder, then
    // in journal_dir when that is another folder.
    char base[MAX_PATH];
    utModDirA(selfModule, base, sizeof(base));
    utJournalMoveAll(base);
    const char* jd = journalFolder();
    if (jd && jd[0] && _stricmp(jd, base) != 0) utJournalMoveAll(jd);
}

bool storeSelectSet(bool modKnown, const char* modName, bool questKnown, bool inMainQuest) {
    char leaf[96];
    if (!utSaveSetLeaf(modKnown, modName, questKnown, inMainQuest, leaf, sizeof(leaf))) {
        followGame(std::string());
        logI("journal: the save set of this world is UNKNOWN (mod name %s, main quest %s) - "
             "deposits and takes are refused, the page shows nothing collected",
             modKnown ? (modName && modName[0] ? "unusable" : "none") : "unreadable",
             questKnown ? (inMainQuest ? "yes" : "no") : "unreadable");
        journalOpenSet(nullptr);
        return false;
    }
    const std::string game = utGameOfSet(leaf);
    // ONE leaf: the name Engine::InitializeMod received and GetModName at the world load must give
    // the same game; when they do not, the world's set decides the settings and the journal.
    if (g_enteredSeen && strcmp(g_entered, game.c_str()) != 0)
        logW("game: this world's save set is the %s game's, but Engine::InitializeMod entered the %s "
             "game - the settings and the journal follow the world; the catalogue stays the "
             "entered game's until the next switch",
             game.empty() ? "main" : game.c_str(), g_entered[0] ? g_entered : "main");
    followGame(game);
    std::string rel;
    if (!game.empty()) {
        // the journals' folder: journal_dir when it is set, else the mod folder (journalInit)
        char base[MAX_PATH];
        _snprintf_s(base, sizeof(base), _TRUNCATE, "%s", journalFolder());
        if (!base[0]) utModDirA(g_self, base, sizeof(base));
        // the move once more for a set still in the root (a file was held at start); never for
        // the set open now, whose rows are in memory and written back to where they were read
        if (!(journalSetKnown() && !strcmp(journalSetLeaf(), leaf)))
            utJournalMoveLog(leaf, utJournalMoveSet(base, leaf), "at the world load");
        rel = utJournalSetRel(base, leaf);
    }
    // the newest character save and this process's start go INTO the open, so the
    // pending rows are settled (bounded) BEFORE the load restores any "out" row.
    unsigned long long saveTime = 0;
    noteWatch(storeNewestCharacterSave(&saveTime));
    // what the save's time cannot settle waits for the container check (ut_recon.cpp).
    journalOpenSet(leaf, saveTime, storeProcessStart(), true, rel.c_str());
    storeSaveCheck(true);   // this process's own rows against the newest save (the runtime rule)
    return journalUsable();
}

void storeOnWorldTeardown() { storeSaveCheck(true); }

bool storeTableOwns() { return journalUsable(); }

unsigned int storeCount(const char* record) {
    char key[256];
    if (!record || !utJournalKey(record, key, sizeof(key))) return 0;
    return journalRows(key);
}

bool storeRollOf(const char* record, UtRollKey* out) {
    if (out) utRollClear(out);
    char key[256];
    if (!out || !record || !utJournalKey(record, key, sizeof(key))) return false;
    // ~2 KB: kept off the engine's stack (the tooltip asks from inside a rollover)
    UtReplicaCapture* c = (UtReplicaCapture*)malloc(sizeof(UtReplicaCapture));
    if (!c) return false;
    unsigned long long seq = 0;
    const bool got = journalNewest(key, c, &seq);
    if (got) utRollSet(c->str[kUtIdPrefix], c->str[kUtIdSuffix], c->seed, out);
    free(c);
    return got;
}

bool storeOnDeposit(const UtReplicaCapture& cap, unsigned long long* seqOut) {
    if (!storeTableOwns()) {
        logW("deposit REFUSED: %s - the item has not been touched",
             !journalSetKnown() ? "no save set is known for this world"
                                : "the journal is read-only this session");
        return false;
    }
    bool rolledBack = false;
    const bool ok = journalDepositCommit(cap, seqOut, &rolledBack);
    if (!ok && rolledBack) logD("deposit: the row was put back out of the journal (rolled back)");
    return ok;
}

bool storeOnTake(const char* record, unsigned long long seq) {
    char key[256];
    if (!record || !utJournalKey(record, key, sizeof(key)) || !storeTakeAllowed()) return false;
    bool rolledBack = false;
    return journalTakeCommit(key, seq, &rolledBack);
}

bool storeOnTakeRow(const char* record, unsigned long long seq) {
    char key[256];
    if (!record || !utJournalKey(record, key, sizeof(key)) || !storeTakeAllowed()) return false;
    bool rolledBack = false;
    return journalTakeRow(key, seq, &rolledBack);
}

bool storeOnTakeShard(const char* record, unsigned long long seq, unsigned long long* outSeq,
                      unsigned* left) {
    if (outSeq) *outSeq = 0;
    if (left) *left = 0;
    char key[256];
    if (!record || !utJournalKey(record, key, sizeof(key)) || !storeTakeAllowed()) return false;
    bool rolledBack = false;
    return journalTakeShard(key, seq, outSeq, left, &rolledBack);
}

void storeSaveCheck(bool force) {
    const DWORD now = GetTickCount();
    if (!force && now - (DWORD)InterlockedCompareExchange(&g_lastCheck, 0, 0) < 1000) return;
    InterlockedExchange(&g_lastCheck, (LONG)now);
    if (!journalSetKnown()) return;
    const bool pending = journalPendingIn() != 0 || journalPendingOut() != 0;
    // The folder is listed while rows are pending, and while the watch is blind (a new
    // character's first save turns takes on); otherwise nothing is touched.
    if (!pending && InterlockedCompareExchange(&g_watch, 0, 0) == 1) return;
    unsigned long long t = 0;
    const bool seen = storeNewestCharacterSave(&t);
    noteWatch(seen);
    if (!seen || !pending) return;
    const int n = journalSaveObserved(t);
    if (n > 0) {
        InterlockedIncrement(&g_saves);
        logD("journal: a character save was seen - %d pending row(s) settled", n);
    }
}

void storeWorkerTick() {
    journalSetCsvExport(g_cfg.exportCsv);
    storeSaveCheck(false);
    journalService();
    _snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
                "journal=%s rows=%u collected=%u pending(in=%u out=%u) writes=%ld",
                journalSetKnown() ? (journalUsable() ? journalSetLeaf() : "read-only") : "none",
                (unsigned int)journalCount(), journalCollectedTotal(),
                (unsigned int)journalPendingIn(), (unsigned int)journalPendingOut(),
                journalWrites());
}

const char* storeStatus() { return g_status; }

const wchar_t* storeDocumentsFolder() { return documentsFolder(); }

}  // namespace ut
