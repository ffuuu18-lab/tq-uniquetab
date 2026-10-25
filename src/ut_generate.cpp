// ut_generate.cpp - see ut_generate.h.
#include "ut_generate.h"

#include "gen/gen_heap.h"
#include "gen/generate.h"
#include "gen/ut_groups_default.h"   // builtinGroupSet: touched before the first private heap
#include "ut_config.h"
#include "ut_folders.h"
#include "ut_gamelist.h"
#include "ut_gameprobe.h"
#include "ut_gameswitch.h"
#include "ut_log.h"
#include "ut_mem.h"
#include "ut_paths.h"
#include "ut_store.h"
#include "ut_threads.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace ut {

namespace {

// The ini value names a file suffix: letters and digits only, up to seven of them, else EN (with
// one WARN when `say`).
std::string textLanguageOf(const char* value, bool say = true) {
    std::string lang;
    for (const char* p = value; *p && lang.size() < 7; ++p) {
        if (!std::isalnum((unsigned char)*p)) { lang.clear(); break; }
        lang += (char)std::toupper((unsigned char)*p);
    }
    if (lang.empty()) {
        if (value[0] && say) logW("config: text_language=%s is not a Text_<lang>.arc suffix - using EN", value);
        lang = "EN";
    }
    return lang;
}

std::string textLanguage() { return textLanguageOf(g_cfg.textLanguage); }

volatile LONG g_generating = 0;   // a generation is running
volatile LONG g_generated = 0;    // generateEnsure has returned

struct StartCtx {
    const char* gameDir;
    const char* modDir;
    const std::string* lang;
};

// Printed before the archives are read, so the log says why the start-up pauses for a few seconds.
void sayStart(const gen::GenReport& report, void* ctx) {
    const StartCtx* c = (const StartCtx*)ctx;
    logI("catalogue: building from the game's database (%s, names from Text_%s.arc) ...",
         report.reason.empty() ? "no reason recorded" : report.reason.c_str(), c->lang->c_str());
    logD("catalogue: game folder \"%s\", output folder \"%s\"", c->gameDir, c->modDir);
    logFlush();
}

// One INFO line after the catalogue line: the records EXCLUDE rule 7 calls unobtainable, read
// back from uniq-excluded.txt (so it is said whether or not this start generated), each with
// the kind of record that holds it without dropping it.
void sayUnobtainable(const char* modDir) {
    const std::string path = std::string(modDir) + "\\uniq-excluded.txt";
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return;
    static const char kTag[] = "\tunobtainable: ";
    static const char kKept[] = " (kept: collect_unobtainable=1)";
    std::string list;
    int n = 0, shown = 0;
    char line[1024];
    while (std::fgets(line, sizeof line, f)) {
        std::string l(line);
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        const std::size_t t = l.find(kTag);
        if (t == std::string::npos) continue;
        std::string kinds = l.substr(t + sizeof kTag - 1);
        const std::size_t k = kinds.find(kKept);
        if (k != std::string::npos) kinds.erase(k);
        ++n;
        const std::string item = l.substr(0, t) + " (" + kinds + ")";
        if (list.size() + item.size() > 1600) continue;   // the log line stays one line
        list += (list.empty() ? "" : "; ") + item;
        ++shown;
    }
    std::fclose(f);
    if (!n) return;
    if (shown < n) list += "; and " + std::to_string(n - shown) + " more in uniq-excluded.txt";
    if (g_cfg.collectUnobtainable)
        logI("catalogue: %d record(s) nobody can obtain are in the catalogue (collect_unobtainable=1): %s",
             n, list.c_str());
    else
        logI("catalogue: %d record(s) nobody can obtain are left out (collect_unobtainable=0; "
             "set it to 1 to collect them): %s", n, list.c_str());
}

// The start-up game's catalogue (generateEnsure, inside its private heap).
void generateMain(HMODULE selfModule) {
    char gameDir[MAX_PATH] = {0};
    char modDir[MAX_PATH] = {0};
    if (!utGameDirA(gameDir, sizeof(gameDir))) {
        logE("catalogue: the game folder could not be resolved from the host exe's own path - "
             "the data files in the mod folder are used as they are");
        InterlockedExchange(&g_generated, 1);
        return;
    }
    utModDirA(selfModule, modDir, sizeof(modDir));
    const std::string lang = textLanguage();

    gen::GenReport report;
    StartCtx ctx = {gameDir, modDir, &lang};
    gen::GenInputs in;
    in.collectUnobtainable = g_cfg.collectUnobtainable != 0;
    in.collectMi = g_cfg.collectMi != 0;
    in.collectRelics = g_cfg.collectRelics != 0;
    // uniquetab-groups.ini beside the main uniquetab.ini: the start-up game's catalogue is
    // generated here, under the main game's settings; every custom game's right after, under its
    // own (generateGamesStart)
    char groupsPath[MAX_PATH] = {0};
    utModPathA(selfModule, "uniquetab-groups.ini", groupsPath, sizeof(groupsPath));
    in.groupsPath = groupsPath;
    InterlockedIncrement(&g_generating);
    const bool ok = gen::ensureOutputsGuarded(gameDir, modDir, lang, report, &sayStart, &ctx, in);
    InterlockedDecrement(&g_generating);
    InterlockedExchange(&g_generated, 1);
    for (const std::string& w : report.warnings) logW("catalogue: %s", w.c_str());
    if (ok && report.upToDate) {
        logI("catalogue: up to date (stamp matches) - catalogue.bin and the uniq-*.txt files in "
             "\"%s\" are current (Text_%s)",
             modDir, lang.c_str());
        if (!report.groupsLine.empty()) logI("catalogue: %s", report.groupsLine.c_str());
        sayUnobtainable(modDir);
        return;
    }
    if (ok) {
        logI("catalogue: %s", report.summary.c_str());
        if (!report.groupsLine.empty()) logI("catalogue: %s", report.groupsLine.c_str());
        sayUnobtainable(modDir);
    } else if (gen::genHeapOutgrown()) {
        logW("catalogue: the start-up game's build needs more than the %u MB build arena - it stopped "
             "and is left for the next start; the files already in \"%s\" are kept",
             (unsigned)(gen::kBuildArenaBytes >> 20), modDir);
    } else {
        logE("catalogue: generation FAILED - %s. The files already in \"%s\" are kept",
             report.error.c_str(), modDir);
    }
}

// ---- the build arena (gen_heap.h) -------------------------------------------------------------
UtAddrSpace g_arenaBefore = {0, 0};   // the address space before the reserve
std::size_t g_arenaPeak = 0;          // the largest pair high-water, and whose build it was
char g_arenaPeakGame[96] = "";

// GenInputs::mayGenerate of the stamp checks: nothing is generated, report.refused says a build
// is due.
bool neverHere(const gen::GenReport&, void*, std::string* why) {
    why->clear();
    return false;
}

// After a pair: the build whose high-water is the largest so far ("" = the start-up game).
void arenaNoteBuild(HANDLE heap, const char* leaf) {
    if (!gen::genHeapIsArena(heap)) return;
    const gen::ArenaInfo a = gen::arenaInfo();
    if (a.pairHighWater > g_arenaPeak) {
        g_arenaPeak = a.pairHighWater;
        _snprintf_s(g_arenaPeakGame, sizeof(g_arenaPeakGame), _TRUNCATE, "%s", leaf[0] ? leaf : "the start-up game");
    }
}

}  // namespace

void generateArenaReserve(HMODULE selfModule) {
    char gameDir[MAX_PATH] = {0};
    if (!utGameDirA(gameDir, sizeof(gameDir))) return;   // the builds say so themselves
    char modDir[MAX_PATH] = {0};
    char groupsPath[MAX_PATH] = {0};
    utModDirA(selfModule, modDir, sizeof(modDir));
    utModPathA(selfModule, "uniquetab-groups.ini", groupsPath, sizeof(groupsPath));
    const DWORD t0 = GetTickCount();
    int checked = 0, due = 0;
    std::string first;   // the first set found due
    try {
        gen::GenInputs in;
        in.collectUnobtainable = g_cfg.collectUnobtainable != 0;
        in.collectMi = g_cfg.collectMi != 0;
        in.collectRelics = g_cfg.collectRelics != 0;
        in.groupsPath = groupsPath;
        in.mayGenerate = &neverHere;
        gen::GenReport r;
        gen::ensureOutputsGuarded(gameDir, modDir, textLanguageOf(g_cfg.textLanguage, false), r, nullptr, nullptr, in);
        ++checked;
        if (r.refused) {
            ++due;
            first = "the start-up game";
        }
        char docs[MAX_PATH] = {0};
        if (const wchar_t* d = storeDocumentsFolder())
            WideCharToMultiByte(CP_ACP, 0, d, -1, docs, sizeof(docs), nullptr, nullptr);
        std::vector<UtGameFound> games;
        std::vector<std::string> skipped;
        utGamesFind(utWorkshopDirOf(gameDir), utCustomMapsDirOf(docs), &games, &skipped);
        for (const UtGameFound& g : games) {
            const std::string rel = utCustomGameRelLeaf(g.leaf);
            if (rel.empty()) continue;   // its build fails at once, with its own line
            UtGenSettings gs;
            configGameGenInputs(g.leaf.c_str(), &gs);
            gen::GenInputs gi = in;
            gi.collectUnobtainable = gs.collectUnobtainable != 0;
            gi.collectMi = gs.collectMi != 0;
            gi.collectRelics = gs.collectRelics != 0;
            gen::GenReport gr;
            gen::ensureOutputsGuarded(gameDir, std::string(modDir) + "\\" + rel, textLanguageOf(gs.textLanguage, false),
                                      gr, nullptr, nullptr, gi, &g.spec);
            ++checked;
            if (gr.refused && !due++) first = "the " + g.leaf + " game";
        }
    } catch (...) {   // out of memory while checking: the builds decide, in the arena
        if (!due++) first = "(the check ran out of memory)";
    }
    const DWORD ms = GetTickCount() - t0;
    if (!due) {
        logD("arena: no start-up build is due (%d set(s) checked in %lu ms) - nothing reserved", checked, ms);
        return;
    }
    g_arenaBefore = utAddrSpace();
    unsigned long err = 0;
    if (!gen::arenaReserve(gen::kBuildArenaBytes, &err)) {
        logW("arena: %u MB could not be reserved (err=%lu) - each start-up build uses a heap of its own, as "
             "without the arena",
             (unsigned)(gen::kBuildArenaBytes >> 20), err);
        return;
    }
    const gen::ArenaInfo a = gen::arenaInfo();
    logD("arena: %u MB reserved at 0x%08X (top-down) for the start-up builds - %d of %d set(s) due (%s "
         "first), checked in %lu ms; before it the largest free address block %u MB, %u MB free in all",
         (unsigned)(a.size >> 20), (unsigned)(uintptr_t)a.base, due, checked, first.c_str(), ms,
         g_arenaBefore.largestMb, g_arenaBefore.totalMb);
}

void generateArenaRelease(const char* why, const char* memPoint) {
    const gen::ArenaInfo a = gen::arenaInfo();
    if (!a.base) {   // none reserved: the memory line of the point alone
        if (memPoint) memSay(memPoint, false);
        return;
    }
    const UtAddrSpace held = utAddrSpace();
    if (!gen::arenaRelease()) {
        logW("arena: not released (%s) - a build still holds it", why);
        if (memPoint) memSay(memPoint, false);   // its arena part says it is still reserved
        return;
    }
    const UtAddrSpace freed = utAddrSpace();
    // the memory line's arena part (ut_mem.h): the one INFO line of the arena
    char tail[512];
    _snprintf_s(tail, sizeof(tail), _TRUNCATE,
                "arena: %u MB at 0x%08X (top-down), high-water %.1f MB (%s), %u build(s), %u outgrew it, %u "
                "free(s) from another thread refused; released (%s): the largest free address block %u -> %u "
                "MB, %u -> %u MB free in all (%u MB / %u MB before the reserve)",
                (unsigned)(a.size >> 20), (unsigned)(uintptr_t)a.base, a.highWater / 1048576.0,
                g_arenaPeakGame[0] ? g_arenaPeakGame : "no build", a.pairs, a.outgrown, a.foreignFrees, why,
                held.largestMb, freed.largestMb, held.totalMb, freed.totalMb, g_arenaBefore.largestMb,
                g_arenaBefore.totalMb);
    memSay(memPoint ? memPoint : "the build arena released", false, tail);
}

void generateEnsure(HMODULE selfModule) {
    // the generator's one function-local static (the built-in groups) is made here, on the
    // process heap, before the first private heap exists (gen_heap.h)
    std::string why;
    utg::builtinGroupSet(&why);
    const HANDLE heap = gen::genHeapBegin();
    try {
        generateMain(selfModule);
    } catch (...) {
        if (gen::genHeapOutgrown())   // the arena was full: even the failure's own report could not be made
            logW("catalogue: the start-up game's build needs more than the %u MB build arena - it stopped "
                 "and is left for the next start; the files already in the mod folder are kept",
                 (unsigned)(gen::kBuildArenaBytes >> 20));
        else
            logE("catalogue: generation FAILED - out of memory. The files already in the mod folder are "
                 "kept");
    }
    arenaNoteBuild(heap, "");
    if (gen::genHeapIsArena(heap))
        logD("catalogue: the start-up game in the build arena: its high-water %.1f MB (%.1f MB in blocks at "
             "the peak)",
             gen::arenaInfo().pairHighWater / 1048576.0, gen::arenaInfo().pairLivePeak / 1048576.0);
    gen::genHeapEnd(heap);
    InterlockedExchange(&g_generated, 1);
}

bool generateBusy() { return InterlockedCompareExchange(&g_generating, 0, 0) != 0; }
bool generateDone() { return InterlockedCompareExchange(&g_generated, 0, 0) != 0; }

// ---- the game in force (ut_gameswitch.h) -----------------------------------------------------
namespace {
SRWLOCK g_gameLock = SRWLOCK_INIT;
UtGameState g_game = {{0}, false, 0};

// GenInputs::mayGenerate at an entry: no generation runs in a running game (the custom games are
// built at start-up), so a set the stamp does not call current is refused here, before any
// archive is read; report.reason says why it is not current.
bool neverAtEntry(const gen::GenReport&, void*, std::string* why) {
    why->clear();
    return false;
}

// ---- every installed custom game, built at start-up ----
volatile LONG g_gamesBuilding = 0;   // the start-up builds have not ended (set before their thread)
HANDLE g_gamesDone = nullptr;        // manual reset: set when they end
HMODULE g_gamesSelf = nullptr;
const DWORD kGamesWaitMs = 10 * 60 * 1000;   // an entry waits at most this long for its game

// Each game's start-up build (utGamesNext), published for the entry: made once by the builds'
// thread outside every private heap (the process heap), read by the game thread. Under g_slotLock.
SRWLOCK g_slotLock = SRWLOCK_INIT;
std::vector<std::string> g_slotLeaf;   // the games found, in build order
std::vector<int> g_slotState;          // kUtGamePending / kUtGameBuilding / kUtGameDone
bool g_slotReady = false;              // the two lists are made
int g_slotWant = -1;                   // the game an entry waits for: built next
char g_slotWantLeaf[96] = "";          // named by an entry before the lists were made
bool g_slotStop = false;               // a world is coming: no other game is started
const char* g_slotStopWhy = "";

// A world is coming (`why`, a literal): the builds' thread finishes the game in hand and starts no
// other, except the game an entry waits for.
void gamesStopFor(const char* why) {
    AcquireSRWLockExclusive(&g_slotLock);
    if (!g_slotStop) g_slotStopWhy = why;
    g_slotStop = true;
    ReleaseSRWLockExclusive(&g_slotLock);
}

// The entered game's build state (kUtGameDone when it is not in the list), -1 while the lists are
// not made yet. `want`: a pending game becomes the next one built (named for later when the lists
// are not made yet).
int gamesLeafState(const char* leaf, bool want) {
    AcquireSRWLockExclusive(&g_slotLock);
    int st = g_slotReady ? (int)kUtGameDone : -1;
    if (!g_slotReady && want) _snprintf_s(g_slotWantLeaf, sizeof(g_slotWantLeaf), _TRUNCATE, "%s", leaf);
    for (size_t k = 0; g_slotReady && k < g_slotLeaf.size(); ++k) {
        if (_stricmp(g_slotLeaf[k].c_str(), leaf) != 0) continue;
        st = g_slotState[k];
        if (want && st == kUtGamePending) g_slotWant = (int)k;
        break;
    }
    ReleaseSRWLockExclusive(&g_slotLock);
    return st;
}

struct GamesTally {
    int built;
    int current;
    int failed;
    int left;   // pending when a world came: built at the next start
};

// The start of one game's generation: DEBUG (each game's INFO line comes after it).
void sayGameStart(const gen::GenReport& report, void* ctx) {
    const StartCtx* c = (const StartCtx*)ctx;
    logD("catalogue: building into \"%s\" (%s, names from Text_%s.arc) ...", c->modDir,
         report.reason.empty() ? "no reason recorded" : report.reason.c_str(), c->lang->c_str());
}

// One game, with its own generation inputs; called between genHeapBegin and genHeapEnd, so every
// object of the generation lives in this frame and is gone at its return.
void buildGame(const char* gameDir, const char* modDir, const char* groupsPath,
               const UtGameFound& g, int k, int n, GamesTally* t) {
    UtGenSettings gs;
    const bool own = configGameGenInputs(g.leaf.c_str(), &gs);
    const std::string rel = utCustomGameRelLeaf(g.leaf);
    if (rel.empty() || !utMakeRelDirs(modDir, rel)) {
        ++t->failed;
        logE("catalogue: the %s game (%d of %d) could not be built - mods\\%s could not be made; the "
             "tab is off in it until a start builds it",
             g.leaf.c_str(), k, n, g.leaf.c_str());
        return;
    }
    const std::string dir = std::string(modDir) + "\\" + rel;
    const std::string lang = textLanguageOf(gs.textLanguage);
    gen::GenInputs in;
    in.collectUnobtainable = gs.collectUnobtainable != 0;
    in.collectMi = gs.collectMi != 0;
    in.collectRelics = gs.collectRelics != 0;
    in.groupsPath = groupsPath;
    gen::GenReport report;
    StartCtx ctx = {gameDir, dir.c_str(), &lang};
    // its own thread: the game thread keeps drawing, so the stall check stays on (no g_generating)
    const bool ok = gen::ensureOutputsGuarded(gameDir, dir, lang, report, &sayGameStart, &ctx, in, &g.spec);
    for (const std::string& w : report.warnings) logW("catalogue: the %s game: %s", g.leaf.c_str(), w.c_str());
    if (ok && report.upToDate) {
        ++t->current;
        logI("catalogue: the %s game (%d of %d) is up to date (stamp matches) in mods\\%s",
             g.leaf.c_str(), k, n, g.leaf.c_str());
    } else if (ok) {
        ++t->built;
        logI("catalogue: the %s game (%d of %d, %s %s) is built into mods\\%s with the settings of "
             "%s - %s",
             g.leaf.c_str(), k, n, g.workshop ? "workshop item" : "custom quest", g.spec.dir().c_str(),
             g.leaf.c_str(), own ? "its own uniquetab.ini" : "the main uniquetab.ini",
             report.summary.c_str());
        if (!report.groupsLine.empty()) logD("catalogue: the %s game: %s", g.leaf.c_str(), report.groupsLine.c_str());
    } else if (gen::genHeapOutgrown()) {
        ++t->left;
        logW("catalogue: the %s game (%d of %d) needs more than the %u MB build arena - its build stopped "
             "and is left for the next start (the tab is off in it until then)",
             g.leaf.c_str(), k, n, (unsigned)(gen::kBuildArenaBytes >> 20));
    } else {
        ++t->failed;
        logE("catalogue: the %s game (%d of %d) could not be built - %s; the tab is off in it until a "
             "start builds it",
             g.leaf.c_str(), k, n, report.error.c_str());
    }
}

// One INFO line naming the [view:<key>] blocks of the player's uniquetab-groups.ini that no
// installed game answers to (the main game answers to vanilla, a custom game to its folder name
// under mods\): such a block never applies. Nothing when the file is absent, over 1 MB, cannot be
// read or is refused (the generation's own line says why). Reads the file only.
void sayUnansweredViews(const char* groupsPath, const std::vector<UtGameFound>& games) {
    FILE* f = std::fopen(groupsPath, "rb");
    if (!f) return;
    std::string text;
    char buf[16384];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0 && text.size() <= ((size_t)1 << 20)) text.append(buf, n);
    const bool bad = std::ferror(f) != 0;
    std::fclose(f);
    utg::GroupSet user;
    std::string why;
    if (bad || text.size() > ((size_t)1 << 20) || !utg::parseGroups(text, user, &why)) return;
    std::vector<std::string> answered = utg::viewKeysFor(std::string());
    for (const UtGameFound& g : games)
        for (const std::string& k : utg::viewKeysFor(g.leaf)) answered.push_back(k);
    const std::vector<std::string> keys = utg::unansweredViewKeys(user, answered);
    if (keys.empty()) return;
    std::string list;
    for (const std::string& k : keys) list += (list.empty() ? "[view:" : ", [view:") + k + "]";
    const bool one = keys.size() == 1;
    logI("catalogue: %s in uniquetab-groups.ini - no installed game answers to %s, so the blocks after "
         "%s never apply (the main game answers to vanilla, a custom game to its folder name under mods\\)",
         list.c_str(), one ? "this key" : "these keys", one ? "it" : "them");
}

// The builds: every game found, the one an entry waits for first, none started once a world is
// coming; one line names the games left for the next start.
void gamesRun() {
    char gameDir[MAX_PATH] = {0};
    char modDir[MAX_PATH] = {0};
    char groupsPath[MAX_PATH] = {0};
    GamesTally t = {0, 0, 0, 0};
    int found = 0;
    const UtAddrSpace before = utAddrSpace();
    const DWORD t0 = GetTickCount();
    if (utGameDirA(gameDir, sizeof(gameDir))) {
        utModDirA(g_gamesSelf, modDir, sizeof(modDir));
        utModPathA(g_gamesSelf, "uniquetab-groups.ini", groupsPath, sizeof(groupsPath));
        char docs[MAX_PATH] = {0};
        if (const wchar_t* d = storeDocumentsFolder())
            WideCharToMultiByte(CP_ACP, 0, d, -1, docs, sizeof(docs), nullptr, nullptr);
        const std::string workshop = utWorkshopDirOf(gameDir);
        const std::string local = utCustomMapsDirOf(docs);
        std::vector<UtGameFound> games;
        std::vector<std::string> skipped;
        utGamesFind(workshop, local, &games, &skipped);
        found = (int)games.size();
        int inWorkshop = 0;
        for (const UtGameFound& g : games) inWorkshop += g.workshop ? 1 : 0;
        logI("catalogue: custom games: %d found (%d in the workshop folder \"%s\", %d in the custom "
             "quest folder \"%s\") - each one whose set in mods\\<game> is missing or stale is built "
             "now, one at a time, in the background; the largest free address block %u MB, %u MB "
             "free in all",
             found, inWorkshop, workshop.empty() ? "(none: the game is not in a Steam library)" : workshop.c_str(),
             found - inWorkshop, local.empty() ? "(none: no Documents folder)" : local.c_str(),
             before.largestMb, before.totalMb);
        for (const std::string& sk : skipped)
            logW("catalogue: the custom game folder %s is skipped - one game, one set", sk.c_str());
        logFlush();
        // the generator's one function-local static, made on the process heap before any private one
        std::string why;
        utg::builtinGroupSet(&why);
        try {   // an INFO line only: a throw while it is made (out of memory) stops no build
            sayUnansweredViews(groupsPath, games);
        } catch (...) {
        }
        // the two lists are made outside the lock and swapped in under it: a throw while they are
        // made (out of memory) never leaves the lock held
        std::vector<std::string> leaves;
        std::vector<int> states;
        leaves.reserve(games.size());
        states.reserve(games.size());
        for (const UtGameFound& g : games) {
            leaves.push_back(g.leaf);
            states.push_back(kUtGamePending);
        }
        AcquireSRWLockExclusive(&g_slotLock);
        g_slotLeaf.swap(leaves);
        g_slotState.swap(states);
        for (size_t k = 0; g_slotWantLeaf[0] && k < g_slotLeaf.size(); ++k)
            if (!_stricmp(g_slotLeaf[k].c_str(), g_slotWantLeaf)) g_slotWant = (int)k;
        g_slotReady = true;
        ReleaseSRWLockExclusive(&g_slotLock);
        for (;;) {
            AcquireSRWLockExclusive(&g_slotLock);
            const int k = utGamesNext(g_slotState.data(), (int)g_slotState.size(), g_slotWant, g_slotStop);
            if (k >= 0) g_slotState[k] = kUtGameBuilding;
            ReleaseSRWLockExclusive(&g_slotLock);
            if (k < 0) break;
            const HANDLE heap = gen::genHeapBegin();
            try {
                buildGame(gameDir, modDir, groupsPath, games[k], k + 1, found, &t);
            } catch (...) {
                if (gen::genHeapOutgrown()) {   // the arena was full: even the failure's report could not be made
                    ++t.left;
                    logW("catalogue: the %s game (%d of %d) needs more than the %u MB build arena - its build "
                         "stopped and is left for the next start (the tab is off in it until then)",
                         games[k].leaf.c_str(), k + 1, found, (unsigned)(gen::kBuildArenaBytes >> 20));
                } else {
                    ++t.failed;
                    logE("catalogue: the %s game (%d of %d) could not be built - out of memory; the tab is "
                         "off in it until a start builds it",
                         games[k].leaf.c_str(), k + 1, found);
                }
            }
            arenaNoteBuild(heap, games[k].leaf.c_str());
            const bool inArena = gen::genHeapIsArena(heap);
            const gen::ArenaInfo ai = gen::arenaInfo();
            gen::genHeapEnd(heap);
            const UtAddrSpace a = utAddrSpace();
            char how[96];
            if (inArena)
                _snprintf_s(how, sizeof(how), _TRUNCATE, "the build arena reset, its high-water %.1f MB",
                            ai.pairHighWater / 1048576.0);
            else
                _snprintf_s(how, sizeof(how), _TRUNCATE, "its heap %s", heap ? "released" : "was the process heap");
            logD("catalogue: after the %s game (%s): the largest free address block %u MB, %u MB free in all",
                 games[k].leaf.c_str(), how, a.largestMb, a.totalMb);
            logFlush();
            AcquireSRWLockExclusive(&g_slotLock);
            g_slotState[k] = kUtGameDone;
            ReleaseSRWLockExclusive(&g_slotLock);
        }
        char names[400] = "";
        size_t at = 0;
        const char* stopWhy = "";
        AcquireSRWLockShared(&g_slotLock);
        for (size_t k = 0; k < g_slotState.size(); ++k) {
            if (g_slotState[k] != kUtGamePending) continue;
            ++t.left;
            if (at < sizeof(names) - 1) {
                const int w = _snprintf_s(names + at, sizeof(names) - at, _TRUNCATE, "%s%s", at ? ", " : "",
                                          g_slotLeaf[k].c_str());
                at = w < 0 ? sizeof(names) - 1 : at + (size_t)w;
            }
        }
        stopWhy = g_slotStopWhy;
        ReleaseSRWLockShared(&g_slotLock);
        if (t.left)
            logI("catalogue: custom games: %s - the start-up builds stopped after the game in hand; %d "
                 "left for the next start (the tab is off in them until then): %s",
                 stopWhy, t.left, names);
    } else {
        logE("catalogue: custom games: the game folder could not be resolved - none is built at start");
    }
    const UtAddrSpace after = utAddrSpace();
    logI("catalogue: custom games at start: %d found - %d built, %d up to date, %d failed, %d left for "
         "the next start, %.1f s; the largest free address block %u -> %u MB, %u -> %u MB free in all "
         "(%s)",
         found, t.built, t.current, t.failed, t.left, (GetTickCount() - t0) / 1000.0, before.largestMb,
         after.largestMb, before.totalMb, after.totalMb,
         gen::arenaInfo().base ? "each game generated in the build arena, reset after it"
                               : "each game generated in a heap of its own, released after it");
}

DWORD WINAPI gamesMain(LPVOID) {
    try {
        gamesRun();
    } catch (...) {
        logE("catalogue: custom games: the start-up builds stopped (out of memory) - a game not built "
             "has the tab off until a start builds it");
    }
    char why[96];
    AcquireSRWLockExclusive(&g_slotLock);
    g_slotReady = true;   // an entry still waiting reads its game's state: none is built any more
    for (size_t k = 0; k < g_slotState.size(); ++k)
        if (g_slotState[k] == kUtGameBuilding) g_slotState[k] = kUtGameDone;
    if (g_slotStop)
        _snprintf_s(why, sizeof(why), _TRUNCATE, "the start-up builds stopped: %s", g_slotStopWhy);
    else
        _snprintf_s(why, sizeof(why), _TRUNCATE, "the start-up builds ended");
    ReleaseSRWLockExclusive(&g_slotLock);
    // before the entry waiting for its game goes on to load the world; one memory line with the
    // arena's release in it (or without it when none was reserved)
    generateArenaRelease(why, "the start-up builds ended");
    InterlockedExchange(&g_gamesBuilding, 0);
    SetEvent(g_gamesDone);
    logFlush();
    return 0;
}

// An entry waits while its own game's start-up build is pending or running (said once); the
// builds' thread builds it next and starts no other after it (gamesStopFor).
void gamesWait(const char* leaf) {
    if (!InterlockedCompareExchange(&g_gamesBuilding, 0, 0) || !g_gamesDone) return;
    logI("game: the %s game is still being built at start-up - its entry waits for it (the other "
         "games' builds stop after the game in hand)",
         leaf);
    logFlush();
    const DWORD t0 = GetTickCount();
    for (;;) {
        // with the stop set no game starts after the wanted one, so the builds' end (and the arena's
        // release) follows its build within milliseconds: the entry waits for the end itself
        AcquireSRWLockShared(&g_slotLock);
        const bool stopped = g_slotStop;
        ReleaseSRWLockShared(&g_slotLock);
        if (WaitForSingleObject(g_gamesDone, 100) == WAIT_OBJECT_0 ||
            (!stopped && !utGamesEntryWaits(gamesLeafState(leaf, true)))) {
            logD("game: the entry waited %lu ms for the %s game's start-up build", GetTickCount() - t0, leaf);
            return;
        }
        if (GetTickCount() - t0 >= kGamesWaitMs) break;
    }
    logW("game: the %s game's start-up build did not end within %lu s - its entry goes on with the "
         "set it has",
         leaf, kGamesWaitMs / 1000);
}

void sayAddrSpace(const char* when, const char* leaf) {
    const UtAddrSpace a = utAddrSpace();
    logI("game: %s the %s game's catalogue: the largest free address block %u MB, %u MB free in "
         "all (TQ.exe has 2 GB)",
         when, leaf, a.largestMb, a.totalMb);
}
}  // namespace

bool utGameFile(HMODULE selfModule, const char* leaf, char* out, size_t cap) {
    char game[96];
    AcquireSRWLockShared(&g_gameLock);
    _snprintf_s(game, sizeof(game), _TRUNCATE, "%s", g_game.leaf);
    ReleaseSRWLockShared(&g_gameLock);
    if (!game[0]) return utModFile(selfModule, leaf, out, cap);
    char rel[MAX_PATH];
    if (!utGameRel(game, leaf, rel, sizeof(rel))) return false;
    utModPathA(selfModule, rel, out, cap);
    return GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES;
}

void gameNow(char* leaf, size_t cap, unsigned* switches, bool* off) {
    AcquireSRWLockShared(&g_gameLock);
    if (leaf && cap) _snprintf_s(leaf, cap, _TRUNCATE, "%s", g_game.leaf);
    if (switches) *switches = g_game.switches;
    if (off) *off = g_game.off;
    ReleaseSRWLockShared(&g_gameLock);
}

// the Resources folder of the game last entered (gameBeforeEntry), for the lattice painter
SRWLOCK g_resLock = SRWLOCK_INIT;
char g_resLeaf[96] = "";
char g_resDir[MAX_PATH] = "";

bool gameResourcesOf(const char* leaf, char* out, size_t cap) {
    if (!out || !cap) return false;
    out[0] = 0;
    if (!leaf || !leaf[0]) return true;
    AcquireSRWLockShared(&g_resLock);
    const bool ok = strcmp(leaf, g_resLeaf) == 0;
    if (ok) _snprintf_s(out, cap, _TRUNCATE, "%s", g_resDir);
    ReleaseSRWLockShared(&g_resLock);
    return ok;
}

bool gameTabOff() {
    bool off = false;
    gameNow(nullptr, 0, nullptr, &off);
    return off;
}

bool gameBeforeEntry(HMODULE selfModule, const char* root, const char* name, UtGameEntry* e) {
    memset(e, 0, sizeof(*e));
    gen::GameSpec spec;
    std::string why;
    if (!utGameLocate(root ? root : "", name ? name : "", spec, &why)) {
        char now[96];
        gameNow(now, sizeof(now), nullptr, nullptr);
        if (!utGameEntryFailed("", now, e->leaf, sizeof(e->leaf))) {
            logW("game: Engine::InitializeMod(\"%s\", \"%s\"): %s - the collection keeps the files in "
                 "force", name ? name : "", root ? root : "", why.c_str());
            return false;
        }
        logW("game: Engine::InitializeMod(\"%s\", \"%s\"): %s - the %s game's files do not belong to "
             "this database, so the tab is off until the main database loads again",
             name ? name : "", root ? root : "", why.c_str(), now);
        e->ok = e->off = true;
        return true;
    }
    const std::string leaf = spec.name();
    _snprintf_s(e->leaf, sizeof(e->leaf), _TRUNCATE, "%s", leaf.c_str());
    // the folder is made outside the lock and copied in under it: a throw while it is made (out of
    // memory) never leaves the lock held
    const std::string resDir = spec.resourcesDir();
    AcquireSRWLockExclusive(&g_resLock);
    _snprintf_s(g_resLeaf, sizeof(g_resLeaf), _TRUNCATE, "%s", e->leaf);
    _snprintf_s(g_resDir, sizeof(g_resDir), _TRUNCATE, "%s", resDir.c_str());
    ReleaseSRWLockExclusive(&g_resLock);
    {
        char now[96];
        bool off = false;
        gameNow(now, sizeof(now), nullptr, &off);
        if (utGameAgain(now, off, e->leaf)) {   // the game in force entered again: nothing changes
            logI("game: Engine::InitializeMod(\"%s\") - the %s game is already in force: no switch, "
                 "no generation",
                 name, e->leaf);
            return false;
        }
    }
    char gameDir[MAX_PATH] = {0};
    char modDir[MAX_PATH] = {0};
    if (!utGameDirA(gameDir, sizeof(gameDir))) {
        logE("game: the game folder could not be resolved - the %s game's catalogue cannot be "
             "built; the tab is off in this game",
             e->leaf);
        e->ok = e->off = true;
        return true;
    }
    utModDirA(selfModule, modDir, sizeof(modDir));
    const std::string rel = utCustomGameRelLeaf(leaf);
    const std::string dir = std::string(modDir) + "\\" + rel;
    if (rel.empty() || !utMakeRelDirs(modDir, rel)) {
        logW("game: the %s game has no folder of its own (mods\\%s could not be made) - the tab is "
             "off in this game",
             e->leaf, e->leaf);
        e->ok = e->off = true;
        return true;
    }
    // A world is coming: the start-up builds start no other game than this one (if it is still to
    // be built); the game thread waits only while ITS build is pending or running (no world is
    // loaded yet; the flag keeps the stall check quiet). First, so the other games' build lines
    // stay in the main log.
    if (gamesStartupBusy()) {
        const int st = gamesLeafState(e->leaf, true);
        gamesStopFor("a custom game's entry");
        InterlockedIncrement(&g_generating);
        if (utGameEntryStep(utGamesEntryWaits(st), true, kUtGameLoadMinMb) == kUtEntryWait) gamesWait(e->leaf);
        InterlockedDecrement(&g_generating);
    }
    // From here the game's lines go to its own log as well, and ITS uniquetab.ini (seeded once,
    // complete) is in force before the stamp check reads a setting (text_language, collect_mi,
    // collect_relics, collect_unobtainable).
    storeFollowGame(e->leaf);
    storeNoteEntered(e->leaf);
    logI("catalogue: the game is %s - a custom game's database is loading from \"%s\"", e->leaf,
         spec.dir().c_str());
    sayAddrSpace("before", e->leaf);
    {   // the whole memory line at the point the game's database starts to load (game thread, no world)
        char point[160];
        _snprintf_s(point, sizeof(point), _TRUNCATE, "before the %s game's database", e->leaf);
        memSay(point, true);
    }
    // Is the game's set current? The same stamp the start-up build wrote, with the settings now in
    // force (the game's own file); nothing is generated here.
    const std::string lang = textLanguage();
    gen::GenReport report;
    gen::GenInputs in;
    in.collectUnobtainable = g_cfg.collectUnobtainable != 0;
    in.collectMi = g_cfg.collectMi != 0;
    in.collectRelics = g_cfg.collectRelics != 0;
    char groupsPath[MAX_PATH] = {0};   // ONE groups file for every game; [view:<leaf>] blocks
    utModPathA(selfModule, "uniquetab-groups.ini", groupsPath, sizeof(groupsPath));
    in.groupsPath = groupsPath;
    in.mayGenerate = &neverAtEntry;
    const bool ok = gen::ensureOutputsGuarded(gameDir, dir, lang, report, nullptr, nullptr, in, &spec);
    for (const std::string& w : report.warnings) logW("catalogue: %s", w.c_str());
    const UtAddrSpace at = utAddrSpace();
    char line[400];
    switch (utGameEntryStep(false, ok && report.upToDate, at.largestMb)) {
        case kUtEntryNotBuilt:
            utGameNotBuiltLine(line, sizeof(line), e->leaf,
                               report.refused || report.error.empty() ? report.reason.c_str()
                                                                      : report.error.c_str());
            logW("%s", line);
            e->ok = e->off = true;
            return true;
        case kUtEntryLowMemory:
            utGameRefusalLine(line, sizeof(line), e->leaf, at.largestMb, utGameGuardMiss(at.largestMb));
            logW("%s", line);
            e->ok = e->off = true;
            return true;
        default: break;
    }
    logI("catalogue: the %s game's set in \"%s\" is current (stamp matches) - read, not generated",
         e->leaf, dir.c_str());
    sayUnobtainable(dir.c_str());
    e->ok = true;
    return true;
}

bool gameAfterEntry(const UtGameEntry& e, bool engineOk) {
    if (!e.ok) return false;
    AcquireSRWLockExclusive(&g_gameLock);
    const int step = utGameEnter(&g_game, e.leaf, e.off);
    const unsigned n = g_game.switches;
    ReleaseSRWLockExclusive(&g_gameLock);
    if (step == kUtGameSame) return false;
    if (e.off && !e.leaf[0])
        logI("game: the collection follows no game's files (switch #%u) - the tab is off until the "
             "main database loads again",
             n);
    else if (e.off)
        logI("game: the collection follows the %s game (switch #%u) - the tab is off in this game",
             e.leaf, n);
    else
        logI("game: the collection follows the %s game (switch #%u) - its catalogue, groups and "
             "gray icons from mods\\%s",
             e.leaf, n, e.leaf);
    if (!engineOk)
        logW("game: Engine::InitializeMod returned false for the %s game - its files stay in force "
             "until the main database loads again",
             e.leaf);
    return true;
}

bool gameEntryFailed(UtGameEntry* e) {
    char now[96];
    gameNow(now, sizeof(now), nullptr, nullptr);
    char leaf[96];
    if (!utGameEntryFailed(e->leaf, now, leaf, sizeof(leaf))) {
        logE("game: the custom game's entry failed (out of memory?) - the collection keeps the "
             "files in force");
        return false;
    }
    if (leaf[0])
        logE("game: the %s game's entry failed (out of memory?) - the tab is off in this game", leaf);
    else
        logE("game: the custom game's entry failed (out of memory?) - the %s game's files do not "
             "belong to this database, so the tab is off until the main database loads again",
             now);
    _snprintf_s(e->leaf, sizeof(e->leaf), _TRUNCATE, "%s", leaf);
    e->ok = e->off = true;
    return true;
}

bool gameEnterStartup() {
    char was[96];
    AcquireSRWLockExclusive(&g_gameLock);
    _snprintf_s(was, sizeof(was), _TRUNCATE, "%s", g_game.leaf);
    const int step = utGameEnter(&g_game, "", false);
    ReleaseSRWLockExclusive(&g_gameLock);
    if (step == kUtGameSame) return false;
    logI("catalogue: the game is the main game again - the main database is loading, so the %s "
         "game's records are gone with it",
         was[0] ? was : "?");
    storeNoteEntered("");
    storeFollowGame("");   // the custom game's log ends with the line above; the main settings
    return true;
}

void generateGamesStart(HMODULE selfModule) {
    g_gamesSelf = selfModule;
    g_gamesDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_gamesDone) {
        logE("catalogue: custom games: no event for the start-up builds (err=%lu) - none is built at "
             "start", GetLastError());
        generateArenaRelease("no custom game is built at start", "the start-up builds ended");
        return;
    }
    InterlockedExchange(&g_gamesBuilding, 1);   // before the thread: an entry always sees it
    HANDLE th = CreateThread(nullptr, kUtStackBuilds, gamesMain, nullptr, STACK_SIZE_PARAM_IS_A_RESERVATION,
                             nullptr);
    if (!th) {
        logE("catalogue: custom games: the start-up builds' thread could not start (err=%lu) - none "
             "is built at start", GetLastError());
        generateArenaRelease("no custom game is built at start", "the start-up builds ended");
        InterlockedExchange(&g_gamesBuilding, 0);
        SetEvent(g_gamesDone);
        return;
    }
    SetThreadPriority(th, THREAD_PRIORITY_BELOW_NORMAL);   // the menu stays smooth
    CloseHandle(th);
}

bool gamesStartupBusy() { return InterlockedCompareExchange(&g_gamesBuilding, 0, 0) != 0; }

void gamesOnWorld() {
    if (gamesStartupBusy()) gamesStopFor("a world is up");
}

}  // namespace ut
