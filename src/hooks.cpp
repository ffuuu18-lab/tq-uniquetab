// hooks.cpp - the detours.
//
// Rules every detour here obeys:
//   * it always calls the original, exactly once, with the arguments it was given, and returns the
//     original's result unchanged - every detour OBSERVES, none alters anything;
//   * its own body is wrapped in __try/__except so a mistake cannot take the game down;
//   * it never creates, moves, deletes or saves anything, and reads no keyboard state;
//   * where it logs AFTER the original, the thread's last-error value is saved right after the
//     original returns and restored before the detour returns (UT_LE_SAVE / UT_LE_RESTORE), so the
//     caller sees exactly what the original left;
//   * a detour on a function the engine calls EVERY FRAME (PresentSurface, Update, SetCaravanMode,
//     the six page getters) never logs per call: it counts, the worker says the count once a
//     second, and a CHANGE is logged when it happens.
//
// x86: every target is __thiscall (QAE/QBE/UAE/UBE/AAE), so each detour is
// `__fastcall(self, void* edx_unused, args...)` - `this` arrives in ECX exactly as the engine put
// it, EDX is ignored, and the callee cleans the same stack - and its trampoline is called through
// the __thiscall typedef in tq_runtime.h (checked against the mangling there).
//
// 23 detours (Engine::PresentSurface, GameEngine::Update, Engine::LoadMainDatabase,
// Engine::LoadDatabase, NpcCaravan::OnPlayerInteract, GameEngine::CaravanGoodbye,
// GameEngine::SetCaravanMode, the six GetPlayer{Stash,Transfer,RelicVault} getters, the three
// CursorHandlerItemMove::Primary*Activ*, the three AddItemTo*(id) overloads, the three
// RemoveItemFrom*, CursorHandlerItemMove::SetId) + 4 (the Transfer page accessor and
// UIStashInventory::StreamOut in TQ.exe, InventorySack::GetItemUnderPoint and ::Sort) + one
// vftable slot (CursorHandlerItemMove +0x30, IsTransferCapable). The Vec2 AddItemTo* overloads
// are the FILE LOAD path and are NEVER hooked.
//
// CHANGES THE FIRST RULE for the view only: while the collection view is ON (ut_view.cpp),
// the Transfer drop / quick-move / prototype pick-up detours REFUSE without calling their
// original, GetItemUnderPoint returns 0 on the mod sack for the right-click and held pick-up
// paths, Sort refuses on the mod sack, and GetPlayerTransfer hands the mod sack to the page's own
// accessor. Every one of those predicates is FALSE while the view is OFF, so with the view OFF
// every detour is still the pass-through. No refusal ever calls a Primary* original.
//
// the page mouse handler's GetItemUnderPoint (the left-click site) is refused only for a
// click (either of its two bools set) or an unverifiable frame; a PROVEN hover gets the real id so
// the tooltip works. A store event that passed through marks the owned set dirty.

#include "hooks.h"

#include <windows.h>

#include <stdio.h>

#include <intrin.h>   // _ReturnAddress

#include "MinHook.h"
#include "tq_runtime.h"
#include "ut_config.h"
#include "ut_costprobe.h"     // the cost meter
#include "ut_depositgate.h"   // utCapSlotAnswer
#include "ut_log.h"
#include "ut_owned.h"
#include "ut_panel.h"
#include "ut_recon.h"
#include "ut_search.h"   // the key gate
#include "ut_store.h"
#include "ut_tooltip.h"
#include "ut_view.h"
#include "ut_viewgate.h"

#pragma intrinsic(_ReturnAddress)
#pragma intrinsic(_AddressOfReturnAddress)

#define UT_LE_SAVE const DWORD utLastError = GetLastError()
#define UT_LE_RESTORE SetLastError(utLastError)

namespace ut {
namespace {

PfnEngine_Void o_PresentSurface = nullptr;
PfnGameEngine_Update o_GameUpdate = nullptr;
PfnEngine_Void o_LoadMainDatabase = nullptr;
PfnEngine_LoadDatabase o_LoadDatabase = nullptr;
PfnNpcCaravan_OnPlayerInteract o_OnPlayerInteract = nullptr;
PfnGameEngine_Void o_CaravanGoodbye = nullptr;
PfnGameEngine_SetCaravanMode o_SetCaravanMode = nullptr;

volatile LONG64 g_frames = 0;

// ---- the cost meter. Interlocked only (the Present and Update detours run on the game's
// one thread in every log so far, - this stays exact if a tooltip detour ever
// runs elsewhere). Taken (and zeroed) by the 5-second frames line.
struct CostAcc {
    volatile LONG64 sum;
    volatile LONG64 max;
    volatile LONG n;
};
CostAcc g_costPresent = {}, g_costUpdate = {}, g_costRebuild = {}, g_costScan = {};
volatile LONG64 g_presentFrame = 0;   // this frame's Present-side ticks so far
LONG64 g_qpcFreq = 0;                 // set once by the first Present (QueryPerformanceFrequency)
UtCostFirst g_costFirst = {};         // game thread: the once-per-session line

void costAdd(CostAcc& a, LONG64 v) {
    InterlockedExchangeAdd64(&a.sum, v);
    LONG64 m = a.max;
    while (v > m) {
        const LONG64 was = InterlockedCompareExchange64(&a.max, v, m);
        if (was == m) break;
        m = was;
    }
    InterlockedIncrement(&a.n);
}
UtCostWindow costTake(CostAcc& a) {
    UtCostWindow w;
    w.n = InterlockedExchange(&a.n, 0);
    w.sum = InterlockedExchange64(&a.sum, 0);
    w.max = InterlockedExchange64(&a.max, 0);
    return w;
}
LONG64 qpcFreq() {
    if (!g_qpcFreq) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        g_qpcFreq = f.QuadPart;
    }
    return g_qpcFreq;
}
void costFirstSay() {
    logI("cost: the first rebuilds took %.1f / %.1f / %.1f ms, the first owned scan %.1f ms (this "
         "session; the 5-second \"mod:\" figures follow the TRACE frames line)",
         g_costFirst.rebuild[0], g_costFirst.rebuild[1], g_costFirst.rebuild[2], g_costFirst.scan);
}
volatile LONG64 g_updates = 0;
volatile LONG g_caravanOpen = 0;       // OnPlayerInteract -> 1, CaravanGoodbye -> 0
volatile LONG g_caravanOpens = 0;      // how many times it opened this session
volatile LONG g_mainDbSeen = 0;        // the LoadMainDatabase detour fired
volatile LONG g_dbTried = 0;           // the late-load check has run (exactly one tick wins)
volatile LONG g_hooksLive = 0;         // hooksInstall finished
volatile LONG g_world = 0;             // bumped on every world up (main player appears)
volatile LONG g_viewHooksOk = 0;             // every detour and the slot patch went in
HWND g_hwnd = nullptr;                 // the game window, subclassed from the game thread
WNDPROC g_origProc = nullptr;
bool g_procUnicode = false;
int g_subclassTries = 0;

DWORD g_lastFrameLogTick = 0;
LONG64 g_lastFrameLogValue = 0;
bool g_loggedFirstPresent = false;
bool g_loggedFirstUpdate = false;
DWORD g_openTick = 0;

// ---- the COUNTED per-frame calls ---------------------------------------------------------------
// Index: 0..5 the six getters (stash, stash const, transfer, transfer const, relic, relic const),
// 6 SetCaravanMode. The detours only increment; the worker reads and says them once a second.
enum { kCntStash, kCntStashC, kCntTransfer, kCntTransferC, kCntRelic, kCntRelicC, kCntSetMode,
       kCntCount };
const char* const kCntName[kCntCount] = {"GetPlayerStash",       "GetPlayerStash(const)",
                                         "GetPlayerTransfer",    "GetPlayerTransfer(const)",
                                         "GetPlayerRelicVault",  "GetPlayerRelicVault(const)",
                                         "SetCaravanMode"};
volatile LONG g_cnt[kCntCount] = {};
LONG g_cntSaid[kCntCount] = {};          // worker only: the totals at the last per-second line
LONG g_cntAtOpen[kCntCount] = {};        // game thread: the totals when the caravan opened
volatile LONG g_mode = -1;               // the last SetCaravanMode argument
volatile LONG g_getterWorldChecked[6] = {};  // the world number each getter was last compared in

// ---------------------------------------------------------------------------------------------
// Engine::PresentSurface -- the frame tick
// ---------------------------------------------------------------------------------------------
// The body of every detour is wrapped in a C++ try/catch as well as the SEH __try in the detour
// itself, so a std::bad_alloc raised deep inside the mod's own code is swallowed here, where the
// stack is still ours, instead of unwinding into the engine's frame.
void presentBody(TqEngine* self) try {
    const LONG64 frames = InterlockedIncrement64(&g_frames);

    if (!g_loggedFirstPresent) {
        g_loggedFirstPresent = true;
        logD("HOOK Engine::PresentSurface first call: this=%p", (void*)self);
        g_lastFrameLogTick = GetTickCount();
        g_lastFrameLogValue = frames;
    }

    const DWORD now = GetTickCount();
    if (now - g_lastFrameLogTick >= 5000) {
        const LONG64 delta = frames - g_lastFrameLogValue;
        const double secs = (double)(now - g_lastFrameLogTick) / 1000.0;
        // the mod's own cost over the same window, on the same line
        char mod[200];
        const UtCostWindow cp = costTake(g_costPresent), cu = costTake(g_costUpdate),
                           cr = costTake(g_costRebuild), cs = costTake(g_costScan);
        utCostFormat(mod, sizeof(mod), cp, cu, cr, cs, qpcFreq());
        logT("frames: total=%lld  +%lld in %.1fs (%.1f fps); %s", frames, delta, secs,
             secs > 0.0 ? (double)delta / secs : 0.0, mod);
        g_lastFrameLogTick = now;
        g_lastFrameLogValue = frames;
    }
} catch (...) {
}

// ---- the game window's procedure (the shape measured at run time; GD's WindowProc role) ----
// Subclassed from the game thread (the Present detour), so the window found is the one this
// thread pumps. The pad's clicks, the wheel over the page while ON and the view hotkey are
// consumed here; everything else is passed on unchanged. A swallowed message may still reach the
// engine through DirectInput - which is why every refusal is ALSO at the engine level.
LRESULT CALLBACK utWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    // the search field lets the keyboard go when the game loses it (never swallowed)
    if (msg == WM_KILLFOCUS)
        searchFieldBlur("the game window lost the keyboard focus (WM_KILLFOCUS)");
    else if (msg == WM_ACTIVATEAPP && !wp)
        searchFieldBlur("another application was activated (WM_ACTIVATEAPP)");
    if ((msg >= WM_LBUTTONDOWN && msg <= WM_RBUTTONDBLCLK) || msg == WM_MOUSEWHEEL ||
        msg == WM_KEYDOWN) {
        bool mine = false;
        __try {
            mine = panelInput(h, msg, wp, lp);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            mine = false;
        }
        if (mine) return 0;
    }
    if (!g_origProc) return g_procUnicode ? DefWindowProcW(h, msg, wp, lp) : DefWindowProcA(h, msg, wp, lp);
    return g_procUnicode ? CallWindowProcW(g_origProc, h, msg, wp, lp)
                         : CallWindowProcA(g_origProc, h, msg, wp, lp);
}

BOOL CALLBACK findWindowCb(HWND h, LPARAM lp) {
    if (!IsWindowVisible(h)) return TRUE;
    RECT rc, brc = {0, 0, 0, 0};
    GetClientRect(h, &rc);
    HWND* best = (HWND*)lp;
    if (*best) GetClientRect(*best, &brc);
    if (!*best || rc.right * rc.bottom > brc.right * brc.bottom) *best = h;
    return TRUE;
}

void trySubclass(LONG64 frame) {
    if (g_hwnd || (g_subclassTries > 0 && (frame % 120) != 0)) return;
    ++g_subclassTries;
    HWND h = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), findWindowCb, (LPARAM)&h);
    if (!h) return;
    g_procUnicode = IsWindowUnicode(h) != FALSE;
    const LONG_PTR prev = g_procUnicode ? SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)utWndProc)
                                        : SetWindowLongPtrA(h, GWLP_WNDPROC, (LONG_PTR)utWndProc);
    if (!prev) {
        logW("input: SetWindowLongPtr failed err=%lu - the pad cannot be clicked", GetLastError());
        return;
    }
    g_origProc = (WNDPROC)prev;
    g_hwnd = h;
    logI("input: the game window %p (%s) is subclassed for the pad, the wheel and the hotkey",
         (void*)h, g_procUnicode ? "unicode" : "ansi");
}

void __fastcall hk_PresentSurface(TqEngine* self, void* /*edx*/) {
    __try {
        // The game thread is the first place that can tell whether the LoadMainDatabase detour
        // was installed too late to catch the load. One interlocked read per frame after that.
        hookLateLoadTick(true);
        presentBody(self);
        trySubclass(InterlockedCompareExchange64(&g_frames, 0, 0));
        panelGrayPrepare(InterlockedCompareExchange(&g_world, 0, 0) == 0);
        const LONG64 t0 = probeNow();
        panelDraw();   // the pad + label, only while the Transfer tab is on screen
        // the frame's Present-side cost = the page draw PRE / POST and the tooltip
        // detours since the last Present + this panelDraw; closed here, one sample per frame
        costAdd(g_costPresent, InterlockedExchange64(&g_presentFrame, 0) + (probeNow() - t0));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // no logging inside the filter path: keep the crash path as small as possible
    }
    if (o_PresentSurface) o_PresentSurface(self);
}

// ---------------------------------------------------------------------------------------------
// GameEngine::Update -- game-thread tick, and the world up / down edges
// ---------------------------------------------------------------------------------------------
// The observable world signals. The first Update that reports a main player after none is a world
// coming up; the first that reports none after one is the world gone. Polled every 30 updates, so
// this costs one exported call twice a second.
void watchForTeardown(TqGameEngine* self, LONG64 n) {
    static bool sawPlayer = false;
    if (!g_tq.GameGetMainPlayer || (n % 30) != 0) return;
    TqPlayer* player = g_tq.GameGetMainPlayer(self);
    const bool have = player != nullptr;
    if (have && !sawPlayer) {
        const LONG w = InterlockedIncrement(&g_world);
        logI("world #%ld is up: main player %p, GameEngine %p", w, (void*)player, (void*)self);
        sawPlayer = true;
        viewOnWorld(true);
    } else if (!have && sawPlayer) {
        sawPlayer = false;
        logI("world #%ld is gone: the first GameEngine::Update with no main player",
             InterlockedCompareExchange(&g_world, 0, 0));
        viewOnWorld(false);   // G4/G6: OFF, the prototypes dropped (the world took them)
        panelForgetFonts();
    }
}

void updateBody(TqGameEngine* self, int deltaMs) try {
    const LONG64 n = InterlockedIncrement64(&g_updates);
    if (!g_loggedFirstUpdate) {
        g_loggedFirstUpdate = true;
        logD("HOOK GameEngine::Update first call: this=%p delta=%d", (void*)self, deltaMs);
    } else if ((n % 600) == 0) {
        logT("GameEngine::Update tick %lld (delta=%d, caravanOpen=%ld)", n, deltaMs,
             InterlockedCompareExchange(&g_caravanOpen, 0, 0));
    }
    watchForTeardown(self, n);
} catch (...) {
}

void __fastcall hk_GameUpdate(TqGameEngine* self, void* /*edx*/, int deltaMs) {
    __try {
        updateBody(self, deltaMs);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    if (o_GameUpdate) o_GameUpdate(self, deltaMs);
    const LONG64 t0 = probeNow();
    __try {
        viewTick();   // a queued toggle, a page rebuild, the multiplayer poll
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        viewFault("an exception in the view tick");
    }
    costAdd(g_costUpdate, probeNow() - t0);   // the game side, one sample per Update
}

// ---------------------------------------------------------------------------------------------
// Engine::LoadMainDatabase / Engine::LoadDatabase -- the database, OBSERVED ONLY
// ---------------------------------------------------------------------------------------------
// The GD overlay goes in here (after the checksum is computed, so multiplayer stays neutral).
// has no overlay: the detour only proves it fires and says what the checksum was either side.
unsigned readChecksum(TqEngine* e) {
    unsigned sum = 0;
    if (!e || !g_tq.EngineGetDatabaseArchiveChecksum) return 0;
    __try {
        sum = g_tq.EngineGetDatabaseArchiveChecksum(e);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        sum = 0;
    }
    return sum;
}

void __fastcall hk_LoadMainDatabase(TqEngine* self, void* /*edx*/) {
    unsigned before = 0;
    __try {
        InterlockedExchange(&g_mainDbSeen, 1);
        before = readChecksum(self);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    if (o_LoadMainDatabase) o_LoadMainDatabase(self);
    UT_LE_SAVE;
    __try {
        logI("Engine::LoadMainDatabase via detour: checksum 0x%08X -> 0x%08X (no overlay)",
             before, readChecksum(self));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    UT_LE_RESTORE;
}

bool __fastcall hk_LoadDatabase(TqEngine* self, void* /*edx*/, const TqStdString* path) {
    const bool r = o_LoadDatabase ? o_LoadDatabase(self, path) : false;
    UT_LE_SAVE;
    __try {
        char text[260];
        if (!safeStdString(path, text, sizeof(text))) text[0] = 0;
        logD("Engine::LoadDatabase(\"%s\") -> %d", text[0] ? text : "(unreadable)", r ? 1 : 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    UT_LE_RESTORE;
    return r;
}

// ---------------------------------------------------------------------------------------------
// NpcCaravan::OnPlayerInteract / GameEngine::CaravanGoodbye / SetCaravanMode -- the caravan
// ---------------------------------------------------------------------------------------------
LONG readMode(TqGameEngine* ge) {
    LONG m = -1;
    if (ge && g_tq.modeOff) safeRead((const unsigned char*)ge + g_tq.modeOff, &m, sizeof(m));
    return m;
}

void __fastcall hk_OnPlayerInteract(TqNpcCaravan* self, void* /*edx*/, unsigned id, Bool32 a,
                                    Bool32 b) {
    // G4: the view is OFF before the original runs (its load path resizes the cached member).
    __try {
        viewOnOpen();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        viewFaultSafe("an exception in the caravan-open OFF (G4)");
    }
    // Set FIRST: the calls the original makes inside (the page build) belong to the open caravan.
    const LONG was = InterlockedExchange(&g_caravanOpen, 1);
    if (o_OnPlayerInteract) o_OnPlayerInteract(self, id, a, b);
    UT_LE_SAVE;
    __try {
        if (!was) {
            InterlockedIncrement(&g_caravanOpens);
            g_openTick = GetTickCount();
            for (int i = 0; i < kCntCount; ++i) g_cntAtOpen[i] = g_cnt[i];
            logI("caravan open: NpcCaravan::OnPlayerInteract(npc=%p, player id=%u) mode=%ld",
                 (void*)self, id, readMode(gameEngine()));
            reconOnCaravan();   // the unresolved pending rows against every container
        } else {
            logD("NpcCaravan::OnPlayerInteract(npc=%p, id=%u) while already open", (void*)self, id);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    UT_LE_RESTORE;
}

void __fastcall hk_CaravanGoodbye(TqGameEngine* self, void* /*edx*/) {
    // G2, PRE: the three saves follow this call 8-25 ms later with NO page refresh in between
    // (measured), so the member is re-pointed and the prototypes destroyed BEFORE the original runs.
    __try {
        viewOnGoodbye();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        viewFaultSafe("an exception in the caravan-close OFF (G2)");
    }
    if (o_CaravanGoodbye) o_CaravanGoodbye(self);
    UT_LE_SAVE;
    __try {
        // the engine wrote Player.chr just BEFORE this call (measured): settle the journal's
        // pending rows against it now (a read-only look at the save folder).
        storeSaveCheck(true);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    __try {
        const LONG was = InterlockedExchange(&g_caravanOpen, 0);
        const DWORD ms = was ? GetTickCount() - g_openTick : 0;
        LONG d[kCntCount];
        for (int i = 0; i < kCntCount; ++i) d[i] = g_cnt[i] - (was ? g_cntAtOpen[i] : g_cnt[i]);
        logI("caravan close: GameEngine::CaravanGoodbye after %lu ms open - SetCaravanMode x%ld, "
             "getters stash x%ld transfer x%ld relic x%ld (const x%ld/%ld/%ld)",
             ms, d[kCntSetMode], d[kCntStash], d[kCntTransfer], d[kCntRelic], d[kCntStashC],
             d[kCntTransferC], d[kCntRelicC]);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    UT_LE_RESTORE;
}

// Called EVERY FRAME while the caravan is up (the 3-way switch re-sets the current mode): counted,
// and logged only when the value changes.
void __fastcall hk_SetCaravanMode(TqGameEngine* self, void* /*edx*/, int mode) {
    __try {
        viewOnModeChanged(mode);   // G2: a tab change turns the view OFF (one compare per frame)
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        viewFaultSafe("an exception in the tab-change OFF (G2)");
    }
    if (o_SetCaravanMode) o_SetCaravanMode(self, mode);
    UT_LE_SAVE;
    __try {
        InterlockedIncrement(&g_cnt[kCntSetMode]);
        const LONG before = InterlockedExchange(&g_mode, mode);
        if (before != mode) {
            // A mode that flickered every frame would turn "logged on change" into a line per
            // frame: past 10 change lines in one second the rest of that second is only counted
            // (the worker's per-second line still carries the total).
            static DWORD s_second = 0;
            static int s_lines = 0;
            const DWORD now = GetTickCount();
            if (now - s_second >= 1000) {
                s_second = now;
                s_lines = 0;
            }
            if (++s_lines <= 10) logI("caravan mode %ld -> %d", before, mode);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    UT_LE_RESTORE;
}

// ---------------------------------------------------------------------------------------------
// The six page getters -- the ACTIVE tab's getter runs every frame: COUNTED, never printed
// ---------------------------------------------------------------------------------------------
// Once per world per getter, the sack it returns is compared with gGameEngine + the decoded
// offset: that is the substitution point of design v2, and proves the arithmetic live.
void getterNote(int which, TqGameEngine* ge, TqSack* r) {
    InterlockedIncrement(&g_cnt[which]);
    const LONG w = InterlockedCompareExchange(&g_world, 0, 0);
    if (InterlockedExchange(&g_getterWorldChecked[which], w) == w) return;
    const unsigned off = which < 2 ? g_tq.stashOff : which < 4 ? g_tq.transferOff : g_tq.relicOff;
    TqGameEngine* global = gameEngine();
    const bool match = off && r == (TqSack*)((unsigned char*)ge + off);
    if (match && ge == global) {
        logI("getter %s: returns GameEngine+0x%X (the decoded offset) on gGameEngine - world #%ld",
             kCntName[which], off, w);
    } else {
        logW("getter %s: returned %p, GameEngine %p + 0x%X = %p, gGameEngine %p - world #%ld",
             kCntName[which], (void*)r, (void*)ge, off, (void*)((unsigned char*)ge + off),
             (void*)global, w);
    }
}

// FILTER is the returned value: `r` everywhere, except the non-const GetPlayerTransfer, which
// hands the MOD sack to the Transfer page's own accessor while the view is ON (G1).
#define UT_GETTER(NAME, WHICH, FILTER)                            \
    PfnGameEngine_GetSack o_##NAME = nullptr;                     \
    TqSack* __fastcall hk_##NAME(TqGameEngine* ge, void*) {       \
        TqSack* r = o_##NAME(ge);                                 \
        UT_LE_SAVE;                                               \
        __try {                                                   \
            getterNote(WHICH, ge, r);                             \
            r = FILTER;                                           \
        } __except (EXCEPTION_EXECUTE_HANDLER) {                  \
        }                                                         \
        UT_LE_RESTORE;                                            \
        return r;                                                 \
    }
UT_GETTER(GetPlayerStash, kCntStash, r)
UT_GETTER(GetPlayerStashC, kCntStashC, r)
UT_GETTER(GetPlayerTransfer, kCntTransfer, viewGetterResult(r))
UT_GETTER(GetPlayerTransferC, kCntTransferC, r)
UT_GETTER(GetPlayerRelicVault, kCntRelic, r)
UT_GETTER(GetPlayerRelicVaultC, kCntRelicC, r)
#undef UT_GETTER

// ---------------------------------------------------------------------------------------------
// The drop gate and the three stores -- one debug line per EVENT, with the cursor id
// ---------------------------------------------------------------------------------------------
// REFUSE: evaluated BEFORE the original; true = return false WITHOUT the original (the cursor
// keeps its item: `[this+0x24]` untouched). Only the Transfer one can ever be true (view ON).
#define UT_PRIMARY(NAME, PRETTY, REFUSE)                                                        \
    PfnCursor_Primary o_##NAME = nullptr;                                                       \
    bool __fastcall hk_##NAME(TqCursorItemMove* self, void*, const TqVec2* pos) {               \
        const unsigned id = cursorItemId(self);                                                 \
        bool refuse = false;                                                                    \
        __try {                                                                                 \
            refuse = REFUSE;                                                                    \
        } __except (EXCEPTION_EXECUTE_HANDLER) {                                                \
            refuse = true; /* unsure while it could matter: the view is faulted below */        \
        }                                                                                       \
        if (refuse) return false;                                                               \
        const bool r = o_##NAME(self, pos);                                                     \
        UT_LE_SAVE;                                                                             \
        if (r) ownedMarkDirty(); /* a store changed: the owned set is refreshed */       \
        __try {                                                                                 \
            logD("drop: CursorHandlerItemMove::" PRETTY "(cursor=%p, item id=%u) -> %d, cursor " \
                 "now %u",                                                                      \
                 (void*)self, id, r ? 1 : 0, cursorItemId(self));                               \
        } __except (EXCEPTION_EXECUTE_HANDLER) {                                                \
        }                                                                                       \
        UT_LE_RESTORE;                                                                          \
        return r;                                                                               \
    }
UT_PRIMARY(PrimaryStashActivate, "PrimaryStashActivate", false)
UT_PRIMARY(PrimaryRelicVaultActive, "PrimaryRelicVaultActive", false)
#undef UT_PRIMARY

PfnCursor_SetId o_SetId = nullptr;   // the deposit clears the handler with the ORIGINAL

// the Transfer drop. OFF = the pass-through. ON = the deposit (ut_view.cpp
// viewDepositDrop): refused -> false WITHOUT the original (the cursor keeps the item); deposited
// -> the row is on disk and the original was disposed of exactly as the engine's own body does
// after its add (SendRemoveItemFromInventory on the handler player's controller), then the
// handler is cleared with the ORIGINAL SetId(0) and true is returned. The original is NEVER
// called while ON (it would add to the REAL Transfer sack).
PfnCursor_Primary o_PrimaryTransferActivate = nullptr;
bool __fastcall hk_PrimaryTransferActivate(TqCursorItemMove* self, void*, const TqVec2* pos) {
    const unsigned id = cursorItemId(self);
    // a click on the pad is never a drop on the page (no deposit while ON, no vanilla drop
    // while OFF): the item stays on the cursor.
    bool padClick = false;
    __try {
        padClick = panelPadClaimsPress();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        padClick = false;
    }
    if (padClick) {
        UT_LE_SAVE;
        __try {
            logI("panel: a click on the pad never drops the held item on the page - "
                 "PrimaryTransferActivate refused, item %u stays on the cursor", id);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        UT_LE_RESTORE;
        return false;
    }
    int move = kUtMovePass;
    __try {
        move = viewDepositDrop(self);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        move = viewOn() ? kUtMoveRefuse : kUtMovePass;   // unsure while it could matter: refuse
        viewFaultSafe("an exception in the drag-drop deposit");
    }
    if (move == kUtMoveRefuse) return false;
    if (move == kUtMoveDone) {
        __try {
            if (o_SetId) o_SetId(self, 0);   // [handler+0x24] = 0: the cursor is complete
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        return true;
    }
    const bool r = o_PrimaryTransferActivate(self, pos);
    UT_LE_SAVE;
    if (r) ownedMarkDirty();
    __try {
        logD("drop: CursorHandlerItemMove::PrimaryTransferActivate(cursor=%p, item id=%u) -> %d, "
             "cursor now %u", (void*)self, id, r ? 1 : 0, cursorItemId(self));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    UT_LE_RESTORE;
    return r;
}

#define UT_ADDID(NAME, PRETTY, REFUSE)                                                         \
    PfnGameEngine_AddItemId o_##NAME = nullptr;                                                \
    bool __fastcall hk_##NAME(TqGameEngine* ge, void*, unsigned id, Bool32 b) {                \
        bool refuse = false;                                                                   \
        __try {                                                                                \
            refuse = REFUSE;                                                                   \
        } __except (EXCEPTION_EXECUTE_HANDLER) {                                               \
            refuse = true;                                                                     \
        }                                                                                      \
        if (refuse) return false; /* the TQ.exe caller keeps the item */                      \
        const bool r = o_##NAME(ge, id, b);                                                    \
        UT_LE_SAVE;                                                                            \
        if (r) ownedMarkDirty();                                                               \
        __try {                                                                                \
            logD("store: GameEngine::" PRETTY "(item id=%u, %u) -> %d", id, b & 0xFF, r ? 1 : 0); \
        } __except (EXCEPTION_EXECUTE_HANDLER) {                                               \
        }                                                                                      \
        UT_LE_RESTORE;                                                                         \
        return r;                                                                              \
    }
UT_ADDID(AddItemToStashId, "AddItemToStash", false)
UT_ADDID(AddItemToRelicVaultId, "AdditemToRelicVault", false)
#undef UT_ADDID

// the inventory quick-move onto the Transfer page. ON = the deposit (viewDepositQuick):
// deposited -> true WITHOUT the original (TQ.exe 0x107A6C: the caller then runs
// PlayerInventoryCtrl::RemoveItem and SendRemoveItemFromInventory itself); refused -> false (the
// caller keeps the item). OFF = the pass-through.
PfnGameEngine_AddItemId o_AddItemToTransferId = nullptr;
bool __fastcall hk_AddItemToTransferId(TqGameEngine* ge, void*, unsigned id, Bool32 b) {
    // while ON only the quick-move's PRIMARY call (TQ.exe 0x107A6C, decoded
    // row exe.quickMoveAdd.ret) may deposit. any other caller (the stacked-extras
    // loop 0x107AED, which ignores the result and disposes of its item) gets the ORIGINAL - the
    // engine's own add into the REAL Transfer, never the mod sack - with an ERROR: never a
    // deposit, never a loss. The call site unknown: refused (the primary call keeps its item).
    const void* caller = _ReturnAddress();
    if (viewOn()) {
        int qc = kUtQuickRefuse;
        __try {
            qc = viewQuickCaller(caller, id);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            qc = kUtQuickRefuse;
        }
        if (qc == kUtQuickRefuse) return false;
        if (qc == kUtQuickVanilla) {
            const bool rv = o_AddItemToTransferId(ge, id, b);
            UT_LE_SAVE;
            if (rv) ownedMarkDirty();
            __try {
                viewQuickVanillaNoted(caller, id, rv);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
            UT_LE_RESTORE;
            return rv;
        }
    }
    int move = kUtMovePass;
    __try {
        move = viewDepositQuick(id);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        move = viewOn() ? kUtMoveRefuse : kUtMovePass;
        viewFaultSafe("an exception in the quick-move deposit");
    }
    if (move == kUtMoveRefuse) return false;
    if (move == kUtMoveDone) return true;
    const bool r = o_AddItemToTransferId(ge, id, b);
    UT_LE_SAVE;
    if (r) ownedMarkDirty();
    __try {
        logD("store: GameEngine::AddItemToTransfer(item id=%u, %u) -> %d", id, b & 0xFF, r ? 1 : 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    UT_LE_RESTORE;
    return r;
}

// the engine's own auto-place into the REAL Transfer sack, through the ORIGINAL (the
// trampoline once the detour is in, never the deposit detour itself).
bool transferAutoPlaceImpl(unsigned id) {
    PfnGameEngine_AddItemId f =
        o_AddItemToTransferId ? o_AddItemToTransferId : g_tq.GameAddItemToTransferId;
    TqGameEngine* ge = gameEngine();
    if (!f || !ge || !id) return false;
    bool r = false;
    utGuardEnter();
    __try {
        r = f(ge, id, 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        r = false;
    }
    utGuardLeave();
    if (r) ownedMarkDirty();
    return r;
}

#define UT_REMOVE(NAME, PRETTY, REFUSE)                                                     \
    PfnGameEngine_RemoveItem o_##NAME = nullptr;                                            \
    bool __fastcall hk_##NAME(TqGameEngine* ge, void*, unsigned id) {                       \
        bool refuse = false;                                                                \
        __try {                                                                             \
            refuse = REFUSE;                                                                \
        } __except (EXCEPTION_EXECUTE_HANDLER) {                                            \
            refuse = true;                                                                  \
        }                                                                                   \
        if (refuse) return false; /* a prototype never leaves the mod sack (G5) */         \
        const bool r = o_##NAME(ge, id);                                                    \
        UT_LE_SAVE;                                                                         \
        if (r) ownedMarkDirty();                                                            \
        __try {                                                                             \
            /* id 0 (a click on an empty or refused cell) is not an event: no line */ \
            if (id || r) logD("store: GameEngine::" PRETTY "(item id=%u) -> %d", id, r ? 1 : 0); \
        } __except (EXCEPTION_EXECUTE_HANDLER) {                                            \
        }                                                                                   \
        UT_LE_RESTORE;                                                                      \
        return r;                                                                           \
    }
UT_REMOVE(RemoveItemFromStash, "RemoveItemFromStash", false)
UT_REMOVE(RemoveItemFromRelicVault, "RemoveItemFromRelicVault", false)
#undef UT_REMOVE

// the take's last step. A prototype id with a JOURNALLED take (the SetId of this same click
// wrote it) leaves the MOD sack (InventorySack::RemoveItem on the mod sack) and true is returned;
// the original is never called for a prototype. A prototype without a journalled take is refused
// exactly as. Anything else (a real item, or the view OFF) is the pass-through.
PfnGameEngine_RemoveItem o_RemoveItemFromTransfer = nullptr;
bool __fastcall hk_RemoveItemFromTransfer(TqGameEngine* ge, void*, unsigned id) {
    int move = kUtMovePass;
    __try {
        move = viewTakeRemove(id);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        move = kUtMoveRefuse;   // a prototype never reaches the original (G5)
        viewFaultSafe("an exception in the take");
    }
    if (move == kUtMoveRefuse) return false;
    if (move == kUtMoveDone) return true;
    const bool r = o_RemoveItemFromTransfer(ge, id);
    UT_LE_SAVE;
    if (r) ownedMarkDirty();
    __try {
        if (id || r) logD("store: GameEngine::RemoveItemFromTransfer(item id=%u) -> %d", id, r ? 1 : 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    UT_LE_RESTORE;
    return r;
}
void __fastcall hk_SetId(TqCursorItemMove* self, void* /*edx*/, unsigned id) {
    const unsigned was = cursorItemId(self);
    // A prototype id never reaches a cursor: the original runs with 0 (the cursor stays empty).
    unsigned pass = id;
    __try {
        pass = viewFilterSetId(id);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // a prototype reaches the cursor only when its take is journalled.
        __try {
            pass = viewSetIdOnFault(id);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            pass = viewOn() ? 0u : id;
        }
    }
    id = pass;
    if (o_SetId) o_SetId(self, id);
    __try {
        if (id) viewTakeOnCursor(self, id);   // follow a taken item on its cursor
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    UT_LE_SAVE;
    __try {
        if (was != id) logD("cursor: CursorHandlerItemMove::SetId(%u) on %p (was %u)", id, (void*)self, was);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    UT_LE_RESTORE;
}

// ---- the Transfer page accessor (TQ.exe, counted signature) ------------------------------
// PRE: "in the Transfer page refresh" + the sub-window `this`; POST: the live member check.
PfnExe_PageAccessor o_TransferAccessor = nullptr;
unsigned __fastcall hk_TransferAccessor(void* self, void* /*edx*/, unsigned arg) {
    __try {
        viewAccessorEnter(self);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    const unsigned r = o_TransferAccessor(self, arg);
    UT_LE_SAVE;
    __try {
        viewAccessorLeave(self);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        viewFault("an exception after the Transfer page accessor");
    }
    UT_LE_RESTORE;
    return r;
}

// ---- UIStashInventory::StreamOut (TQ.exe, counted signature) - G3, PRE ---------------------
PfnExe_StreamOut o_StreamOut = nullptr;
unsigned __fastcall hk_StreamOut(void* ui, void* /*edx*/, const TqStdString* path) {
    __try {
        viewOnStreamOut(ui);   // the mod sack in the member -> the real sack, ERROR, latch
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        viewFaultSafe("an exception in the StreamOut assert (G3)");
    }
    // read the member AGAIN. Still a mod sack (the re-point failed, or the assert
    // threw) -> the original never runs; false is the engine's own "could not open" result.
    bool skip = false;
    __try {
        skip = viewStreamOutMustSkip(ui);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        skip = true;   // refuse when unsure: the file on disk holds the last good close
    }
    if (skip) return 0;
    return o_StreamOut(ui, path);
}

// ---- InventorySack::GetItemUnderPoint - the page-class refusals ---------------------
// Called per mouse event for the hover as well: pointer compares, a few guarded reads, no log.
// the page mouse handler (TQ.exe 0xBFED0) is ALSO its hover handler, so its call is refused
// only when one of its click bools is set (or its frame cannot be verified): a proven hover gets
// the real id and the tooltip works. The frame is read through the EBP this
// function's own frame saved right below its return address - which is why this read happens HERE
// (a __try function: MSVC always gives it an EBP frame, and build.bat has /Oy-).
PfnSack_GetItemUnderPoint o_GetItemUnderPoint = nullptr;
unsigned __fastcall hk_GetItemUnderPoint(const TqSack* s, void* /*edx*/, float x, float y) {
    const void* ret = _ReturnAddress();
    const void* const* retSlot = (const void* const*)_AddressOfReturnAddress();
    UtHoverFrame hover;
    hover.valid = false;
    hover.realSack = false;
    hover.slotWide = false;
    bool refuse = false;
    bool takeCheck = false;
    bool takeRight = false;   // the right-click's take
    __try {
        refuse = viewRefuseUnderPoint(s, ret, retSlot, x, y, &hover, &takeCheck, &takeRight);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        takeCheck = false;
        takeRight = false;
        // refuse when unsure - but only at the three page-class sites (pointer compares)
        refuse = ret == g_tq.retRightClick || ret == g_tq.retHeldPickup || ret == g_tq.retLeftClick;
        hover.valid = false;
        hover.slotWide = false;
    }
    if (refuse) return 0;   // "nothing under the cursor": no clone, no cursor, no removal
    unsigned id = o_GetItemUnderPoint(s, x, y);
    if (takeCheck) {
        // a verified pick-up press on the collection: the id passes only for a takeable
        // prototype (built from a journal row, rows > 0, the journal writable), else 0.
        __try {
            const unsigned raw = id;   // the trace says which lookup answered
            if (!id && hover.slotWide) id = viewSlotIdAt(s, x, y);   // the whole slot
            // the right-click journals its take here (the engine gives the object to the
            // player before its RemoveItemFromTransfer); the left-click's SetId journals it.
            // the MOUSE right-click is the page mouse handler's b2 (its return address)
            id = takeRight ? viewTakeRightClick(s, id, raw, x, y, ret == g_tq.retLeftClick)
                           : viewTakeFilterUnder(id);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            id = takeRight ? viewTakeRightOnFault(id) : 0;   // journalled + armed
        }
        return id;
    }
    if (hover.valid) {
        UT_LE_SAVE;
        __try {
            viewNoteHover(hover, x, y, id);   // the owned marks' grid check (once): the TRUE id
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        UT_LE_RESTORE;
        // a proven hover on an empty cell of a slot answers with the slot's prototype
        // (the tooltip and the highlight cover the whole slot). Mod sack and view ON only.
        if (!id && hover.slotWide) {
            __try {
                id = viewSlotIdAt(s, x, y);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                id = 0;
            }
        }
    }
    return id;
}

// ---- InventorySack::AddItem, both overloads - the mod sack's guard ----
// Every sack but a mod sack (current or retired: pointer compares only, no read) is a pure
// pass-through, so the file load (the Vec2 overload into the REAL sacks) is never touched. An add
// into a mod sack that is not the mod's own prototype placement (ut_proto's placing flag, this
// thread) is REFUSED: false without the original, one WARN with the caller, the id and record.
PfnSack_AddItem o_SackAddItem = nullptr;
bool __fastcall hk_SackAddItem(TqSack* s, void* /*edx*/, TqItem* item, Bool32 b) {
    if (viewIsModSack(s)) {
        const void* caller = _ReturnAddress();
        bool refuse = true;
        UT_LE_SAVE;
        __try {
            refuse = viewSackAddRefused(s, item, caller, false);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            refuse = true;   // unsure: the mod's own placement fails safe (its item is destroyed)
        }
        UT_LE_RESTORE;
        if (refuse) return false;
    }
    return o_SackAddItem(s, item, b);
}
PfnSack_AddItemVec o_SackAddItemVec = nullptr;
bool __fastcall hk_SackAddItemVec(TqSack* s, void* /*edx*/, const TqVec2* at, TqItem* item,
                                  Bool32 b) {
    if (viewIsModSack(s)) {
        const void* caller = _ReturnAddress();
        bool refuse = true;
        UT_LE_SAVE;
        __try {
            refuse = viewSackAddRefused(s, item, caller, true);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            refuse = true;
        }
        UT_LE_RESTORE;
        if (refuse) return false;
    }
    return o_SackAddItemVec(s, at, item, b);
}
// 1(d): every RemoveItem on a mod sack that is not the mod's own is a DEBUG line (never refused).
PfnSack_RemoveItem o_SackRemoveItem = nullptr;
bool __fastcall hk_SackRemoveItem(TqSack* s, void* /*edx*/, unsigned id) {
    const void* caller = _ReturnAddress();
    const bool r = o_SackRemoveItem(s, id);
    if (viewIsModSack(s)) {
        UT_LE_SAVE;
        __try {
            viewSackRemoveNoted(s, id, r, caller);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        UT_LE_RESTORE;
    }
    return r;
}

// ---- the Transfer page draw (TQ.exe 0xC31A0, counted signature) - PRE ------------
// The slot plates are drawn BEFORE the original, which then draws the inventory (its ground and
// the items) over them. Quads only; the original runs exactly once, whatever happens here.
PfnExe_PageDraw o_PageDraw = nullptr;
void pageDrawPost(void* page, void* canvas, int pass) {
    UT_LE_SAVE;
    __try {
        panelPageDrawPost(page, canvas, pass);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    UT_LE_RESTORE;
}
void __fastcall hk_PageDraw(void* page, void* /*edx*/, void* canvas, const TqVec2* origin, int pass,
                            float alpha) {
    UT_LE_SAVE;
    LONG64 t0 = probeNow();
    __try {
        panelPageDrawPre(page, canvas, origin, pass);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    __try {
        panelRectRouteBegin(page);   // the rect route is live only inside the original
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    probePresentAdd(probeNow() - t0);   // the PRE (the slot ground, the tint refresh, the route's Begin)
    UT_LE_RESTORE;
    __try {
        o_PageDraw(page, canvas, origin, pass, alpha);   // exactly once
    } __finally {
        // an exception unwinding past the page draw gives every widget the
        // route touched its own rect and icon back now, before a view rebuild can free one
        // that holds a gray texture (the next Begin writes the line)
        if (AbnormalTermination()) panelRectRouteUnwound();
    }
    const DWORD leAfter = GetLastError();
    t0 = probeNow();
    __try {
        panelRectRouteEnd();   // every widget the route touched is back at its footprint
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    SetLastError(leAfter);
    pageDrawPost(page, canvas, pass);                 // the veils, under the tooltip
    probePresentAdd(probeNow() - t0);
}

// ---- an item widget's background (TQ.exe 0x10A9B0, counted signature) -----------------
// For a PROTOTYPE's widget inside the Transfer page draw (panelRectRouteBegin / End): the slot rect
// for the engine's tint and border, then the centred footprint for the icon it draws next. Every
// other widget goes straight to the original. The original runs exactly once - or,, not at
// all for an UNCOLLECTED prototype whose slot is not hovered (the token's skip flag, ut_viewgate.h
// utBgToken): no red, no class tint, no rarity border. 0x10A9B0 / 0x10A850 / 0x10AA60 only draw
// (every store is to their own stack; the widget is only read), so nothing the engine reads later
// is left unset. A fault in Pre = -1 = the original runs.
PfnExe_ItemBackground o_ItemBackground = nullptr;
void __fastcall hk_ItemBackground(void* widget, void* /*edx*/, void* canvas, const TqVec2* origin,
                                  const TqColor* colour, float inset, Bool32 met, Bool32 one) {
    UT_LE_SAVE;
    int token = -1;
    __try {
        token = panelItemBackgroundPre(widget);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        token = -1;
    }
    UT_LE_RESTORE;
    if (!ut::utBgTokenSkip(token)) o_ItemBackground(widget, canvas, origin, colour, inset, met, one);
    const int save = ut::utBgTokenSave(token);
    if (save < 0) return;
    const DWORD leAfter = GetLastError();
    __try {
        panelItemBackgroundPost(save);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    SetLastError(leAfter);
}

// ---- InventorySack::Sort - never on the mod sack ------------------------------------------
PfnSack_Sort o_Sort = nullptr;
bool __fastcall hk_Sort(TqSack* s, void* /*edx*/, unsigned arg) {
    bool refuse = false;
    __try {
        refuse = viewRefuseSort(s);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        refuse = false;
    }
    if (refuse) return false;
    return o_Sort(s, arg);
}

// ---- CursorHandlerItemMove vftable slot +0x30 (IsTransferCapable) --------------------------
// The slot holds the COMDAT-folded `mov al,1; ret` stub shared by 265 slots, so the stub itself is
// never touched: the ONE slot is patched with a thunk that answers false
// while the view is ON (no drop on the page, no green highlight) and asks the stub otherwise.
const unsigned kCapTransferSlot = 0x30;
PfnCursor_Capable g_capOrig = nullptr;
bool __fastcall capTransferThunk(const TqCursorItemMove* self, void* /*edx*/) {
    // while ON the page accepts a drop only of an item the deposit would take (a
    // single unique answers GetNumberInStack 0, which is a single item - utCapSlotAnswer).
    const bool on = viewOn();
    bool capable = false;
    if (on) {
        __try {
            capable = viewCapable(self);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            capable = false;
        }
    }
    return utCapSlotAnswer(on, capable, on ? false : (g_capOrig ? g_capOrig(self) : true));
}

bool patchCapSlot() {
    if (!g_tq.CursorVftable || !g_tq.CursorIsTransferCapable) return false;
    unsigned char* slot = (unsigned char*)g_tq.CursorVftable + kCapTransferSlot;
    void* cur = nullptr;
    if (!safeRead(slot, &cur, sizeof(cur)) || cur != (void*)g_tq.CursorIsTransferCapable) {
        logW("view: CursorHandlerItemMove slot +0x%X holds %p, not IsTransferCapable %p - not "
             "patched", kCapTransferSlot, cur, (void*)g_tq.CursorIsTransferCapable);
        return false;
    }
    DWORD old = 0, dummy = 0;
    if (!VirtualProtect(slot, 4, PAGE_READWRITE, &old)) {
        logW("view: VirtualProtect on the cursor vftable failed err=%lu", GetLastError());
        return false;
    }
    g_capOrig = (PfnCursor_Capable)cur;
    InterlockedExchange((volatile LONG*)slot, (LONG)(uintptr_t)&capTransferThunk);
    VirtualProtect(slot, 4, old, &dummy);
    FlushInstructionCache(GetCurrentProcess(), slot, 4);
    logD("  slot CursorHandlerItemMove+0x%X (IsTransferCapable) -> the view thunk", kCapTransferSlot);
    return true;
}

// ---- the search field's key gate (Engine.dll Display::HandleKeyEvent, exported) ---------------
// Every keyboard event enters the game here - its one caller is Engine::ProcessUserInput - before
// any game widget, so a press the field takes never reaches a hotkey or the chat line. While the
// field has no focus the gate returns at once. Installed only with search=1.
PfnDisplay_HandleKeyEvent o_HandleKeyEvent = nullptr;
volatile LONG g_keyGateLive = 0;
void __fastcall hk_HandleKeyEvent(void* self, void* /*edx*/, const void* ev) {
    if (searchKeyGate(ev)) return;   // a press the field took
    o_HandleKeyEvent(self, ev);
}

const char* mhText(MH_STATUS s) {
    const char* t = MH_StatusToString(s);
    return t ? t : "?";
}

bool createOne(const char* pretty, void* target, void* detour, void** original) {
    if (!target) {
        logE("  hook %-40s SKIPPED (target not resolved)", pretty);
        return false;
    }
    MH_STATUS s = MH_CreateHook(target, detour, original);
    if (s != MH_OK) {
        logE("  hook %-40s MH_CreateHook FAILED: %s", pretty, mhText(s));
        return false;
    }
    s = MH_EnableHook(target);
    if (s != MH_OK) {
        logE("  hook %-40s MH_EnableHook FAILED: %s", pretty, mhText(s));
        return false;
    }
    logD("  hook %-40s installed at %p (trampoline %p)", pretty, target, *original);
    return true;
}

bool g_initialised = false;

}  // namespace

void hookLateLoadTick(bool gameThread) {
    // THE LATE-LOAD CHECK (GD reagentLateLoadTick, without the overlay). A dinput8 loader maps
    // this DLL after the engine has read its database, so the LoadMainDatabase detour cannot fire:
    // a non-zero checksum once the hooks are live says so. Exactly one tick - the frame tick or
    // the worker's - says it, once.
    if (!InterlockedCompareExchange(&g_hooksLive, 0, 0)) return;
    if (InterlockedCompareExchange(&g_dbTried, 0, 0)) return;
    if (InterlockedCompareExchange(&g_mainDbSeen, 0, 0)) {
        InterlockedExchange(&g_dbTried, 1);
        return;
    }
    const unsigned sum = readChecksum(engine());
    if (!sum) return;  // the load has not happened yet: the detour will see it
    if (InterlockedExchange(&g_dbTried, 1)) return;
    logI("LoadMainDatabase detour never fired (checksum 0x%08X already set) - the database was "
         "loaded before this DLL; seen from the %s (no overlay)",
         sum, gameThread ? "game thread" : "worker");
}

bool hookTransferAutoPlace(unsigned id) { return transferAutoPlaceImpl(id); }

bool hooksInstall() {
    MH_STATUS s = MH_Initialize();
    if (s != MH_OK && s != MH_ERROR_ALREADY_INITIALIZED) {
        logE("MH_Initialize FAILED: %s", mhText(s));
        return false;
    }
    g_initialised = true;
    logD("installing detours (MinHook, x86):");

    struct One {
        const char* pretty;
        void* target;
        void* detour;
        void** original;
    };
    const One all[] = {
        {"Engine::PresentSurface", (void*)g_tq.EnginePresentSurface, (void*)&hk_PresentSurface,
         (void**)&o_PresentSurface},
        {"GameEngine::Update", (void*)g_tq.GameUpdate, (void*)&hk_GameUpdate,
         (void**)&o_GameUpdate},
        {"Engine::LoadMainDatabase", (void*)g_tq.EngineLoadMainDatabase,
         (void*)&hk_LoadMainDatabase, (void**)&o_LoadMainDatabase},
        {"Engine::LoadDatabase", (void*)g_tq.EngineLoadDatabase, (void*)&hk_LoadDatabase,
         (void**)&o_LoadDatabase},
        {"NpcCaravan::OnPlayerInteract", (void*)g_tq.NpcCaravanOnPlayerInteract,
         (void*)&hk_OnPlayerInteract, (void**)&o_OnPlayerInteract},
        {"GameEngine::CaravanGoodbye", (void*)g_tq.GameCaravanGoodbye, (void*)&hk_CaravanGoodbye,
         (void**)&o_CaravanGoodbye},
        {"GameEngine::SetCaravanMode", (void*)g_tq.GameSetCaravanMode, (void*)&hk_SetCaravanMode,
         (void**)&o_SetCaravanMode},
        {"GameEngine::GetPlayerStash", (void*)g_tq.GameGetPlayerStash, (void*)&hk_GetPlayerStash,
         (void**)&o_GetPlayerStash},
        {"GameEngine::GetPlayerStash const", (void*)g_tq.GameGetPlayerStashC,
         (void*)&hk_GetPlayerStashC, (void**)&o_GetPlayerStashC},
        {"GameEngine::GetPlayerTransfer", (void*)g_tq.GameGetPlayerTransfer,
         (void*)&hk_GetPlayerTransfer, (void**)&o_GetPlayerTransfer},
        {"GameEngine::GetPlayerTransfer const", (void*)g_tq.GameGetPlayerTransferC,
         (void*)&hk_GetPlayerTransferC, (void**)&o_GetPlayerTransferC},
        {"GameEngine::GetPlayerRelicVault", (void*)g_tq.GameGetPlayerRelicVault,
         (void*)&hk_GetPlayerRelicVault, (void**)&o_GetPlayerRelicVault},
        {"GameEngine::GetPlayerRelicVault const", (void*)g_tq.GameGetPlayerRelicVaultC,
         (void*)&hk_GetPlayerRelicVaultC, (void**)&o_GetPlayerRelicVaultC},
        {"CursorHandlerItemMove::PrimaryStashActivate", (void*)g_tq.CursorPrimaryStashActivate,
         (void*)&hk_PrimaryStashActivate, (void**)&o_PrimaryStashActivate},
        {"CursorHandlerItemMove::PrimaryTransferActivate",
         (void*)g_tq.CursorPrimaryTransferActivate, (void*)&hk_PrimaryTransferActivate,
         (void**)&o_PrimaryTransferActivate},
        {"CursorHandlerItemMove::PrimaryRelicVaultActive",
         (void*)g_tq.CursorPrimaryRelicVaultActive, (void*)&hk_PrimaryRelicVaultActive,
         (void**)&o_PrimaryRelicVaultActive},
        {"GameEngine::AddItemToStash(id)", (void*)g_tq.GameAddItemToStashId,
         (void*)&hk_AddItemToStashId, (void**)&o_AddItemToStashId},
        {"GameEngine::AddItemToTransfer(id)", (void*)g_tq.GameAddItemToTransferId,
         (void*)&hk_AddItemToTransferId, (void**)&o_AddItemToTransferId},
        {"GameEngine::AdditemToRelicVault(id)", (void*)g_tq.GameAddItemToRelicVaultId,
         (void*)&hk_AddItemToRelicVaultId, (void**)&o_AddItemToRelicVaultId},
        {"GameEngine::RemoveItemFromStash", (void*)g_tq.GameRemoveItemFromStash,
         (void*)&hk_RemoveItemFromStash, (void**)&o_RemoveItemFromStash},
        {"GameEngine::RemoveItemFromTransfer", (void*)g_tq.GameRemoveItemFromTransfer,
         (void*)&hk_RemoveItemFromTransfer, (void**)&o_RemoveItemFromTransfer},
        {"GameEngine::RemoveItemFromRelicVault", (void*)g_tq.GameRemoveItemFromRelicVault,
         (void*)&hk_RemoveItemFromRelicVault, (void**)&o_RemoveItemFromRelicVault},
        {"CursorHandlerItemMove::SetId", (void*)g_tq.CursorSetId, (void*)&hk_SetId,
         (void**)&o_SetId},
        // the lost-item guards (the two AddItem ones are REQUIRED for the view) and the
        // plates. the ActivateWorld detour is gone (the world drop is vanilla behaviour; the
        // pad lives inside the measured caravan frame, which the engine's world never gets).
        {"InventorySack::AddItem(Item*)", (void*)g_tq.SackAddItem, (void*)&hk_SackAddItem,
         (void**)&o_SackAddItem},
        {"InventorySack::AddItem(Vec2)", (void*)g_tq.SackAddItemVec, (void*)&hk_SackAddItemVec,
         (void**)&o_SackAddItemVec},
        {"InventorySack::RemoveItem", (void*)g_tq.SackRemoveItem, (void*)&hk_SackRemoveItem,
         (void**)&o_SackRemoveItem},
        {"TQ.exe Transfer page draw",
         g_tq.sigPageDrawRva ? (void*)((unsigned char*)g_tq.exe + g_tq.sigPageDrawRva) : nullptr,
         (void*)&hk_PageDraw, (void**)&o_PageDraw},
        // the view's four (a target of null - an unconfirmed signature - is SKIPPED, and
        // the view is then unavailable; the mod itself runs on).
        {"TQ.exe Transfer page accessor",
         g_tq.pageUiOff ? (void*)((unsigned char*)g_tq.exe + g_tq.sigTransferPageRva) : nullptr,
         (void*)&hk_TransferAccessor, (void**)&o_TransferAccessor},
        {"TQ.exe UIStashInventory::StreamOut",
         g_tq.sigStreamOutRva ? (void*)((unsigned char*)g_tq.exe + g_tq.sigStreamOutRva) : nullptr,
         (void*)&hk_StreamOut, (void**)&o_StreamOut},
        {"InventorySack::GetItemUnderPoint", (void*)g_tq.SackGetItemUnderPoint,
         (void*)&hk_GetItemUnderPoint, (void**)&o_GetItemUnderPoint},
        {"InventorySack::Sort", (void*)g_tq.SackSort, (void*)&hk_Sort, (void**)&o_Sort},
    };
    const int kViewFirst = (int)(sizeof(all) / sizeof(all[0])) - 4;
    const int kGuardFirst = kViewFirst - 4;   // AddItem x2 (required), RemoveItem, draw
    const int wanted = (int)(sizeof(all) / sizeof(all[0]));
    int ok = 0, viewHooks = 0, guards = 0;
    for (int i = 0; i < wanted; ++i) {
        const bool one = createOne(all[i].pretty, all[i].target, all[i].detour, all[i].original);
        ok += one ? 1 : 0;
        if (one && i >= kViewFirst) ++viewHooks;
        if (one && i >= kGuardFirst && i < kGuardFirst + 2) ++guards;
    }
    const bool slot = patchCapSlot();
    InterlockedExchange(&g_viewHooksOk, (viewHooks == 4 && slot && guards == 2) ? 1 : 0);
    logI("view detours: %d of 4 + the IsTransferCapable slot %s; sack guards %d of 2 "
         "(InventorySack::AddItem x2)%s; slot plates %s; the caravan frame %s",
         viewHooks, slot ? "patched" : "NOT patched", guards,
         guards == 2 ? "" : " - the view is unavailable without them",
         o_PageDraw ? "from the page draw (TQ.exe PRE-detour)" : "as frames (no page-draw detour)",
         o_PageDraw ? "from the page draw (the hover as the fallback)"
                    : "from the first hover over the grid (no page-draw detour)");

    // the collection tooltip line (GD ut_tooltip) - five detours, ALL-OR-NOTHING in their
    // own group, and COUNTED here: a missing one makes this line a WARN.
    int tipWanted = 0;
    ok += tooltipInstall(&tipWanted);
    // the rect route (items centred in their slot, the tint and border over the slot). It
    // needs the page draw's bracket too; without either the items keep their footprint.
    const bool bg = createOne("TQ.exe item widget background",
                              g_tq.sigItemBackgroundRva
                                  ? (void*)((unsigned char*)g_tq.exe + g_tq.sigItemBackgroundRva)
                                  : nullptr,
                              (void*)&hk_ItemBackground, (void**)&o_ItemBackground);
    ok += bg ? 1 : 0;
    logI("rect route: %s", bg && o_PageDraw ? "the item widget background (TQ.exe PRE/POST-detour) "
                                              "inside the page draw - items centred, tint and border slot-wide"
                           : "OFF - items keep their footprint (the ring tints the slot)");
    // the search field's key gate: search=0 installs nothing
    int gateWanted = 0;
    if (g_cfg.search) {
        gateWanted = 1;
        const bool gate = createOne("Display::HandleKeyEvent (the search field)",
                                    (void*)g_tq.DisplayHandleKeyEvent, (void*)&hk_HandleKeyEvent,
                                    (void**)&o_HandleKeyEvent);
        ok += gate ? 1 : 0;
        const bool live = gate && g_tq.ButtonEventGetText != nullptr;
        InterlockedExchange(&g_keyGateLive, live ? 1 : 0);
        logI("search: %s", live ? "the key gate is installed (Display::HandleKeyEvent) - the field "
                                   "takes the keys only while it has the focus"
                            : "the key gate is NOT available - the field is drawn disabled and reads 'search off' (the "
                              "search_debug_query key still highlights)");
    } else {
        logD("search: search=0 - no key gate");
    }
    const int wantedAll = wanted + tipWanted + 1 + gateWanted;
    if (ok == wantedAll) {
        logI("detours installed: %d of %d", ok, wantedAll);
    } else {
        logW("detours installed: %d of %d - the missing ones are features this run does not have",
             ok, wantedAll);
    }
    InterlockedExchange(&g_hooksLive, 1);
    return ok > 0;
}

// This is NOT called from DllMain. MH_DisableHook/MH_Uninitialize
// suspend every thread, which under the loader lock is a textbook DllMain deadlock, and after an
// unmap a still-live detour jumps into unmapped code. The hooks stay in place until process exit.
// Kept for a future explicit, non-DllMain shutdown path.
void hooksRemove() {
    if (!g_initialised) return;
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    g_initialised = false;
    logD("detours removed");
}

void hookSayCounts() {
    // WORKER, once a second: the per-frame calls, as one line per second in which any happened.
    LONG d[kCntCount];
    bool any = false;
    for (int i = 0; i < kCntCount; ++i) {
        const LONG now = InterlockedCompareExchange(&g_cnt[i], 0, 0);
        d[i] = now - g_cntSaid[i];
        g_cntSaid[i] = now;
        if (d[i]) any = true;
    }
    if (!any || !logWants(UT_LOG_DEBUG)) return;
    logD("per second: SetCaravanMode %ld, getters stash %ld transfer %ld relic %ld (const "
         "%ld/%ld/%ld), mode %ld, caravan %s",
         d[kCntSetMode], d[kCntStash], d[kCntTransfer], d[kCntRelic], d[kCntStashC],
         d[kCntTransferC], d[kCntRelicC], InterlockedCompareExchange(&g_mode, 0, 0),
         InterlockedCompareExchange(&g_caravanOpen, 0, 0) ? "open" : "closed");
}

long long probeNow() {
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    return q.QuadPart;
}
void probePresentAdd(long long ticks) { InterlockedExchangeAdd64(&g_presentFrame, ticks); }
void probeRebuild(long long ticks) {
    costAdd(g_costRebuild, ticks);
    if (utCostFirstRebuild(g_costFirst, utCostMs(ticks, qpcFreq()))) costFirstSay();
}
void probeScan(long long ticks) {
    costAdd(g_costScan, ticks);
    if (utCostFirstScan(g_costFirst, utCostMs(ticks, qpcFreq()))) costFirstSay();
}

unsigned long long hookFrameCount() {
    return (unsigned long long)InterlockedCompareExchange64(&g_frames, 0, 0);
}
unsigned long long hookUpdateCount() {
    return (unsigned long long)InterlockedCompareExchange64(&g_updates, 0, 0);
}
bool hookCaravanOpen() {
    return InterlockedCompareExchange(&g_caravanOpen, 0, 0) != 0;
}
int hookCaravanMode() { return (int)InterlockedCompareExchange(&g_mode, 0, 0); }
HWND hookGameWindow() { return g_hwnd; }
bool hooksViewOk() { return InterlockedCompareExchange(&g_viewHooksOk, 0, 0) != 0; }
bool hookKeyGateLive() { return InterlockedCompareExchange(&g_keyGateLive, 0, 0) != 0; }
long hookCaravanOpens() {
    return InterlockedCompareExchange(&g_caravanOpens, 0, 0);
}

}  // namespace ut
