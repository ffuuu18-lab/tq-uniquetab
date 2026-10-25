// ut_view.cpp - the collection view on the Transfer page: the substitution, the one OFF path, the
// save assert and the refusals. See ut_view.h for the guard map and ut_viewgate.h for the state
// machine (the pure half, enumerated offline by tools\test_viewgate.cpp).
//
// Invariants:
//   * the only engine memory this file WRITES is the Transfer page's cached sack member
//     (sub-window + the decoded 0x88 + 0x60), and only ever to put the REAL sack back
//     (viewForceOff, viewOnStreamOut, viewDetach) - never the mod sack: the substitution happens
//     by returning the mod sack from the getter to the page's own accessor, which stores it;
//   * every OFF goes through viewForceOff; every refusal predicate is false while OFF;
//   * nothing here logs per frame: the per-frame paths only count (the refusal counters).

#include "ut_view.h"
#include "ut_cover.h"   // the caravan background swap, restored at every OFF

#include <windows.h>

#include <stdio.h>

#include "hooks.h"
#include "ut_bindings.h"
#include "ut_config.h"
#include "ut_generate.h"   // gameTabOff
#include "ut_live.h"
#include "ut_mikind.h"
#include "ut_log.h"
#include "ut_owned.h"
#include "ut_panel.h"
#include "ut_proto.h"
#include "ut_recon.h"
#include "ut_search.h"
#include "ut_depositgate.h"
#include "ut_store.h"
#include "ut_viewgate.h"

namespace ut {
void viewSelectSet();
void viewTakeTick();
void viewGridRestore(const char* why);
namespace {

void holdRelease(const char* why);   // (defined with the take state below)

UtViewState g_vs;                  // game thread only
volatile LONG g_on = 0;            // mirror of g_vs.on for any thread
volatile LONG g_available = 0;     // G8, decided once by viewInit
volatile LONG g_toggleReq = 0;     // queued by the input
char g_unavailable[160] = "not initialised";

void* volatile g_subWindow = nullptr;   // the Transfer page's sub-window (accessor `this`)
const void* volatile g_subVft = nullptr;  // its vftable, read at every accessor entry
bool g_goneSaid = false;
volatile LONG g_inAccessor = 0;
volatile LONG g_memberVerified = 0;     // this open: member == real sack and kind == 1, seen live
volatile LONG g_worldGen = 0;          // every world up / down (viewOnWorld)
const unsigned kUiKindOff = 0x148;      // LITERAL ui.stashKind

volatile LONG g_refDrop = 0, g_refAdd = 0, g_refSetId = 0, g_refRemove = 0, g_refUnder = 0,
              g_refSort = 0;
volatile LONG g_passHover = 0, g_refUnsure = 0;
volatile LONG g_refSackAdd = 0, g_refQuickCaller = 0;
// the verdict of the FIRST mouse-handler call on the mod sack (0 none yet, 1 its
// frame verified, 2 it did not) and why not (UtFrameWhy), written by the GetItemUnderPoint detour
// (no log there) and logged once by the view tick - so "no tooltips" always says why.
volatile LONG g_firstFrame = 0, g_firstFrameWhy = 0, g_lastFrameWhy = 0;
bool g_firstFrameSaid = false;
enum UtFrameWhy {
    kWhyNone = 0, kWhyNoSignature, kWhyEbpRead, kWhyShape, kWhyArgsRead, kWhySack, kWhyPoint
};
const char* const kFrameWhy[] = {
    "-", "exe.leftClickHandler(.frame) not resolved", "the saved EBP could not be read",
    "the frame shape differs (EBP - return slot != the decoded distance: is "
    "hk_GetItemUnderPoint's own frame still push ebp / mov ebp,esp?)",
    "the handler's arguments could not be read", "the page's cached sack is not this sack",
    "the handler's point arithmetic does not reproduce (x, y)"};
LONG64 g_ticks = 0;
char g_status[360] = "view=off";
const char* volatile g_mpShape = "?";   // the last poll's utMpShapeText (the heartbeat's mp=)

TqSack* realTransfer() {
    TqGameEngine* ge = gameEngine();
    return (ge && g_tq.transferOff) ? (TqSack*)((unsigned char*)ge + g_tq.transferOff) : nullptr;
}

TqSack* realByKind(int kind) {
    TqGameEngine* ge = gameEngine();
    if (!ge) return nullptr;
    const unsigned off = kind == 0 ? g_tq.stashOff : kind == 2 ? g_tq.relicOff : g_tq.transferOff;
    return off ? (TqSack*)((unsigned char*)ge + off) : nullptr;
}

TqSack** memberAddr(void* subWindow) {
    if (!subWindow || !g_tq.pageUiOff || !g_tq.pageSackOff) return nullptr;
    return (TqSack**)((unsigned char*)subWindow + g_tq.pageUiOff + g_tq.pageSackOff);
}

// A guarded pointer write: VirtualQuery says it is committed, SEH catches the rest.
bool writePtr(TqSack** at, TqSack* v) {
    if (!at || !readable(at, sizeof(*at))) return false;
    bool ok = false;
    utGuardEnter();
    __try {
        *at = v;
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    utGuardLeave();
    return ok;
}

// Re-point the Transfer page's member at the real sack when it holds A mod sack (the current one
// or one a world teardown retired - protoIsModSack, a). Returns 1 re-pointed, 0
// nothing to do, -1 it held a mod sack and the write FAILED (the view is then faulted: refuse
// forever; G3 still recognises the sack and skips the save), -2 the page object is gone: its first
// dword is no longer the vftable the accessor ran on, so nothing is written into memory the HUD
// may have freed (b; a later page gets the real sack from the getter, the view is OFF).
int repoint() {
    TqSack** at = memberAddr(g_subWindow);
    if (!at) return 0;
    TqSack* cur = nullptr;
    if (!safeRead(at, &cur, sizeof(cur))) return 0;
    if (!protoIsModSack(cur)) return 0;
    const void* vt = nullptr;
    if (!safeRead(g_subWindow, &vt, sizeof(vt)) || !vt || vt != g_subVft) {
        if (!g_goneSaid) {
            g_goneSaid = true;
            logW("view: the Transfer page %p no longer reads as the page (vftable %p, was %p) - its "
                 "member is not written; the StreamOut assert (G3) still guards the save",
                 g_subWindow, vt, g_subVft);
        }
        return -2;
    }
    TqSack* real = realTransfer();
    if (!real || !writePtr(at, real)) return -1;
    return 1;
}

// also says the role (server / client: Engine::IsNetworkServer / IsNetworkClient) for the
// heartbeat's mp= field; either pointer may be null.
bool isMultiplayer(bool* known, bool* server = nullptr, bool* client = nullptr) {
    bool mp = false, srv = false, cli = false;
    *known = false;
    TqEngine* e = engine();
    if (e) {
        utGuardEnter();
        __try {
            if (g_tq.EngineGetGameInfo && g_tq.GameInfoGetIsMultiPlayer) {
                TqGameInfo* gi = g_tq.EngineGetGameInfo(e);
                if (gi) {
                    mp = g_tq.GameInfoGetIsMultiPlayer(gi);
                    *known = true;
                }
            }
            if (g_tq.EngineIsNetworkServer && g_tq.EngineIsNetworkServer(e)) srv = true;
            if (g_tq.EngineIsNetworkClient && g_tq.EngineIsNetworkClient(e)) cli = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            *known = false;
        }
        utGuardLeave();
    }
    if (srv || cli) mp = true;
    if (server) *server = srv;
    if (client) *client = cli;
    return mp;
}

void event(int ev, int arg) {
    const UtViewStep st = utViewApply(&g_vs, ev, arg);
    if (st.turnedOff) viewForceOff(utViewWhyText(st.why));
}

// read the session state, feed a CHANGE to the state machine (G6: OFF once), and say it.
// Answers whether the state is KNOWN. Game thread.
bool mpNote() {
    bool known = false, server = false, client = false;
    const bool mp = isMultiplayer(&known, &server, &client);
    const char* shape = utMpShapeText(known, mp, server, client);
    const char* was = g_mpShape;
    g_mpShape = shape;
    const int arg = known ? (mp ? 1 : 0) : 2;
    const int prev = g_vs.mpUnknown ? 2 : (g_vs.mp ? 1 : 0);
    if (arg != prev) {
        if (arg == 1) {
            logI("view: multiplayer session (mp=%s, was mp=%s) - the view works; deposits and takes "
                 "%s (mp_collect=%d)",
                 shape, was,
                 g_cfg.mpCollect ? "work as in single player, for this player only" : "are refused",
                 g_cfg.mpCollect ? 1 : 0);
        } else {
            logD("view: session state mp=%s (was mp=%s)%s", shape, was,
                 arg == 2 ? " - unknown: the view and every move are refused until it reads again"
                          : "");
        }
        event(kUtViewEvMpChanged, arg);
    }
    return known;
}

// 1 = the last buildPage built an EMPTY page on purpose (OWN, nothing owned), so
// the getter keeps the mod sack on the page. Game thread (buildPage and the accessor share it).
volatile LONG g_emptyByDesign = 0;
// 1 = the last buildPage built the layout editor's empty page: the items hidden while the editor
// is open (ut_layoutedit.h utEditHidesItems), the normal page built again at its close. Game thread.
volatile LONG g_editHid = 0;

// Build the wanted page into the mod sack. Game thread, view ON. `navigation`: the view was already
// showing a window, which this one replaces - timed together.
// inside the SAME group (a row scroll, a window step, a same-window refresh) the prototypes
// that stay are MOVED (protoShift: the same objects re-positioned, only the entering row created,
// only the leaving row destroyed); a group switch - or anything the shift refuses - stays
// destroy-all + create-all. ONE INFO line per rebuilt window, with the input's latency.
bool buildPage(bool navigation = false) {
    // a standing search query: the wanted group is indexed first (once), outside the build's timing
    searchBeforePage(liveWantedGroup());
    LARGE_INTEGER t0 = {}, t1 = {}, qf = {};
    QueryPerformanceCounter(&t0);
    const int prevGroup = liveShownGroup(), prevRow = liveShownPage();
    const bool hadPage = navigation && protoCount() > 0;
    static UtProtoPlace places[kUtProtoMax];
    const int wanted = livePagePlaces(places, kUtProtoMax);   // the wanted window becomes the shown one
    // the layout editor open on the page: its items are hidden - an empty page of the same window
    const bool hide = panelLayoutEditOpen();
    const int n = hide ? 0 : wanted;
    // OWN, a group with nothing owned; or the editor's empty page of a window that has entries
    const bool emptyOk = n == 0 && (liveEmptyPageOk() || (hide && wanted > 0));
    InterlockedExchange(&g_emptyByDesign, 0);   // set again below on success
    if (n <= 0 && !emptyOk) {
        if (navigation) protoDestroyAll();   // the old window never outlives a failed build
        logW("view: the page has no entries (group table missing?) - OFF");
        return false;
    }
    const bool sameGroup = hadPage && liveShownGroup() == prevGroup;
    UtShiftCounts sc = {};
    int shift = kUtShiftNotApplied;
    if (sameGroup && n > 0) shift = protoShift(places, n, &sc);
    int placed = 0, failed = 0, destroyed = 0;
    LARGE_INTEGER tc = {};   // the full path: the destroy ends / the create starts here
    if (shift == kUtShiftDone) {
        placed = protoCount();
        failed = sc.failed;
    } else {
        if (shift == kUtShiftAborted) {
            logW("view: the row scroll could not move the window's prototypes (%d moved, %d destroyed "
                 "before a RemoveItem / AddItem failed) - the window is rebuilt whole",
                 sc.moved, sc.destroyed);
        }
        if (navigation) destroyed = protoDestroyAll();
        QueryPerformanceCounter(&tc);
        placed = protoBuild(places, n, &failed);
    }
    QueryPerformanceCounter(&t1);
    QueryPerformanceFrequency(&qf);
    const double ms = qf.QuadPart ? (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)qf.QuadPart : 0.0;
    probeRebuild(t1.QuadPart - t0.QuadPart);   // the cost meter's rebuild count / max
    const char* label = liveGroupLabel(liveShownGroup());
    const int r0 = liveShownPage() + 1, rows = liveShownRows();
    const int r1 = r0 - 1 + liveWindowRows() < rows ? r0 - 1 + liveWindowRows() : rows;
    char span[32];   // an empty (OWN) group logs "rows 0 / 0"
    if (rows > 0) _snprintf_s(span, sizeof(span), _TRUNCATE, "%d-%d", r0, r1);
    else _snprintf_s(span, sizeof(span), _TRUNCATE, "0");
    // the input this window answers - the wheel ticks it covers (coalesced since the last
    // Update) and the latency from the first one (QPC at the WM_MOUSEWHEEL) to the end of the rebuild
    int ticks = 0;
    long long q0 = 0;
    char input[128] = "";
    if (liveRebuildInput(&ticks, &q0) && qf.QuadPart) {
        const double lat = (double)(t1.QuadPart - q0) * 1000.0 / (double)qf.QuadPart;
        if (ticks > 0 && sameGroup) {
            _snprintf_s(input, sizeof(input), _TRUNCATE, "; wheel %d tick(s) -> %+d row(s), "
                        "tick-to-window %.1f ms", ticks, liveShownPage() - prevRow, lat);
        } else {
            _snprintf_s(input, sizeof(input), _TRUNCATE, "; input-to-window %.1f ms", lat);
        }
    }
    const char* own = hide ? " (hidden while the layout editor is open)"
                      : liveOwnedOnlyActive() ? " (OWN: the records you own)" : "";
    if (failed) {
        logW("view: page %s rows %s / %d: %d of %d prototypes placed, %d refused by the engine",
             label, span, rows, placed, n, failed);
    } else if (shift == kUtShiftDone) {
        logI("view: page %s rows %s / %d: %d prototypes%s - moved %d, created %d, destroyed %d in "
             "%.1f ms (%d unchanged%s)",
             label, span, rows, placed, own, sc.moved, sc.created, sc.destroyed, ms, sc.stayed,
             input);
    } else {
        // the create's share, so a user log says whether creation dominates a group switch
        const double createMs =
            qf.QuadPart ? (double)(t1.QuadPart - tc.QuadPart) * 1000.0 / (double)qf.QuadPart : 0.0;
        logI("view: page %s rows %s / %d: %d prototypes%s - rebuilt in %.1f ms (%s%d destroyed + %d "
             "created, the create %.1f ms%s)",
             label, span, rows, placed, own, ms, sameGroup ? "whole, " : "", destroyed, placed,
             createMs, input);
    }
    // An empty page counts only once the mod sack exists (it is what the getter then hands out).
    const bool ok = (placed > 0 || (emptyOk && protoSackBuilt() != nullptr)) && failed == 0;
    InterlockedExchange(&g_emptyByDesign, ok && placed == 0 && emptyOk ? 1 : 0);
    InterlockedExchange(&g_editHid, ok && hide ? 1 : 0);
    return ok;
}

}  // namespace

void viewInit(bool hooksOk) {
    // G8: every binding the view reads, every export it calls, every detour it relies on.
    static const char* const kRows[] = {
        "exe.transferPageAccessor", "exe.stashStreamOut", "exe.rightClickUnderPoint",
        "exe.heldPickupUnderPoint", "exe.leftClickUnderPoint", "page.cachedSack",
        "exe.rightClickUnderPoint.ret", "exe.heldPickupUnderPoint.ret",
        "exe.leftClickUnderPoint.ret", "gameEngine.transferSack", "gameEngine.caravanMode",
        "cursor.itemId", "cursor.capabilitySlots"};
    const char* missing = nullptr;
    for (const char* r : kRows) {
        if (!bindingsRowOk(r)) {
            missing = r;
            break;
        }
    }
    struct {
        const void* p;
        const char* name;
    } ex[] = {{(const void*)g_tq.SackCtor, "InventorySack::InventorySack"},
              {g_tq.SackVftable, "InventorySack vftable"},
              {(const void*)g_tq.SackAddItemVec, "InventorySack::AddItem(Vec2)"},
              {(const void*)g_tq.SackRemoveItem, "InventorySack::RemoveItem"},
              {(const void*)g_tq.SackContainsItem, "InventorySack::ContainsItem"},
              {(const void*)g_tq.SackSetDims, "InventorySack::SetDims"},
              {(const void*)g_tq.SackGetCellWidth, "InventorySack::GetCellWidth"},
              {(const void*)g_tq.SackGetGridWidth, "InventorySack::GetGridWidth"},
              {(const void*)g_tq.SackGetItemUnderPoint, "InventorySack::GetItemUnderPoint"},
              {(const void*)g_tq.SackSort, "InventorySack::Sort"},
              {(const void*)g_tq.ItemCreateItem, "Item::CreateItem"},
              {(const void*)g_tq.ObjectGetObjectId, "Object::GetObjectId"},
              {(const void*)g_tq.ObjectManagerGet, "ObjectManager::Get"},
              {(const void*)g_tq.ObjectManagerDestroyObjectEx, "ObjectManager::DestroyObjectEx"},
              {(const void*)g_tq.EngineGetGameInfo, "Engine::GetGameInfo"},
              {(const void*)g_tq.GameInfoGetIsMultiPlayer, "GameInfo::GetIsMultiPlayer"}};
    char why[160] = {0};
    if (missing) {
        _snprintf_s(why, sizeof(why), _TRUNCATE, "binding %s not confirmed", missing);
    } else {
        for (const auto& e : ex) {
            if (!e.p) {
                _snprintf_s(why, sizeof(why), _TRUNCATE, "export %s missing", e.name);
                break;
            }
        }
    }
    if (!why[0] && !hooksOk)
        _snprintf_s(why, sizeof(why), _TRUNCATE, "a detour or the capability slot is missing");
    if (why[0]) {
        _snprintf_s(g_unavailable, sizeof(g_unavailable), _TRUNCATE, "%s", why);
        g_vs.bindingsOk = false;
        InterlockedExchange(&g_available, 0);
        logI("view: unavailable - %s (the Transfer page stays vanilla; the toggle does nothing)",
             why);
        return;
    }
    g_vs.bindingsOk = true;
    InterlockedExchange(&g_available, 1);
    _snprintf_s(g_unavailable, sizeof(g_unavailable), _TRUNCATE, "%s", "");
    // the start-up game's group table is this game's data, not the session's: a set that did not
    // load turns the tab off in this game only (the WARN above it says why)
    if (!liveActive() && !gameTabOff()) viewOnGameTable(false, false);
    logI("view: available - member at sub-window+0x%X+0x%X, right-click / held pick-up / left-click "
         "refused at TQ.exe+0x%X / +0x%X / +0x%X, %d groups on %d pages",
         g_tq.pageUiOff, g_tq.pageSackOff,
         (unsigned)((const unsigned char*)g_tq.retRightClick - (const unsigned char*)g_tq.exe),
         (unsigned)((const unsigned char*)g_tq.retHeldPickup - (const unsigned char*)g_tq.exe),
         (unsigned)((const unsigned char*)g_tq.retLeftClick - (const unsigned char*)g_tq.exe),
         liveGroupCount(), livePagesTotal());
    // (the READY block): what mp_collect does now (GD's rule).
    logI("view: multiplayer - mp_collect=%d: in a hosted or joined game %s; a change of the session "
         "state turns the view off once, an unknown state refuses the view and every move",
         g_cfg.mpCollect ? 1 : 0,
         g_cfg.mpCollect
             ? "the view, deposits (drag-drop, quick-move) and takes (left-click, right-click) work "
               "as in single player, for this player only"
             : "the view works (display and navigation) and every deposit and take is refused");
}

bool viewAvailable() { return InterlockedCompareExchange(&g_available, 0, 0) != 0; }
bool viewOn() { return InterlockedCompareExchange(&g_on, 0, 0) != 0; }

void viewForceOff(const char* reason, bool worldGone) {
    const bool wasOn = viewOn() || g_vs.on;
    g_vs.on = false;
    InterlockedExchange(&g_on, 0);   // the refusals stop at once...
    const int rp = repoint();        // ...G2: the member holds the real sack again...
    viewGridRestore(reason);         // ...and the inventory its own place
    coverRestore(reason, worldGone); // ...and the caravan window its own background
    // ...G5: no prototype survives the view (after a world teardown they are only dropped).
    const int n = worldGone ? protoForgetAll() : protoDestroyAll();
    ownedOnViewOff();
    if (rp == -1) {
        g_vs.faulted = true;
        logE("view: OFF (%s) - the page member held the mod sack and could NOT be re-pointed; "
             "the StreamOut assert (G3) stays armed; the view is disabled for this session",
             reason ? reason : "?");
        return;
    }
    if (wasOn || rp > 0 || n > 0) {
        logI("view: OFF (%s) - member %s, %d prototype(s) destroyed", reason ? reason : "?",
             rp > 0    ? "re-pointed to the real Transfer sack"
             : rp == -2 ? "not written (the page object is gone)"
                        : "already the real sack",
             n);
    }
}

// A DATA fault: this game's generated set. The tab is off in THIS game - the toggle is refused
// (kUtViewWhyRefusedGameData) until the next game switch (viewOnGameTable) - and the engine-fault
// latch (g_vs.faulted, viewFault) is not touched. `said`: the cause is already one WARN (the
// live load, the page build), so this line is INFO; else this line is the one WARN.
void viewDataFault(const char* where, bool said) {
    if (!g_vs.gameOff) {
        if (said)
            logI("view: the tab is off in this game until the next game switch (%s)", where);
        else
            logW("view: %s - the tab is off in this game until the next game switch", where);
    }
    event(kUtViewEvDataFault, 0);
}

void viewOnGameTable(bool loaded, bool tabOff) {
    const bool was = g_vs.gameOff;
    event(kUtViewEvGameSwitch, 0);
    if (was) logI("view: the game switched - the previous game's data fault is forgotten");
    if (!loaded && !tabOff) viewDataFault("its group table did not load - the WARN above says why", true);
}

void viewFault(const char* where) {
    if (!g_vs.faulted) logW("view: fault - %s; the view is disabled for this session", where);
    event(kUtViewEvFault, 0);
    g_vs.faulted = true;
    viewForceOff(where);
}

// for the __except blocks of the PRE detours - a view fault there must still latch
// and re-point, and a second throw inside must not escape into the engine.
void viewFaultSafe(const char* where) {
    __try {
        viewFault(where);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_vs.faulted = true;
        g_vs.on = false;
        InterlockedExchange(&g_on, 0);
        InterlockedExchange(&g_available, 0);
    }
}

void viewRequestToggle() { InterlockedExchange(&g_toggleReq, kUtViewReqToggle); }
void viewRequest(int want) { InterlockedExchange(&g_toggleReq, want); }
bool viewPendingOn() {
    const LONG req = InterlockedCompareExchange(&g_toggleReq, 0, 0);
    const bool on = viewOn();
    if (req == kUtViewReqOn) return true;
    if (req == kUtViewReqOff) return false;
    if (req == kUtViewReqToggle) return !on;
    return on;
}

// The queued request, resolved against the state. no held-item refusal any
// more - a switch with an item on the cursor changes the page and the item stays on the cursor (a
// held REAL item is in no sack; a held TAKEN item is the kept take, which the teardown forgets).
static bool takeSwitchRequest() {
    const LONG req = InterlockedExchange(&g_toggleReq, 0);
    if (!req) return false;
    if (req == kUtViewReqOn) return !g_vs.on;
    if (req == kUtViewReqOff) return g_vs.on;
    return true;
}

void viewTick() {
    ++g_ticks;
    panelButtonsTick();   // the buttons file: the first open's table, a new read resolved
    // G6: the session state, polled twice a second (and at every toggle request below). a
    // CHANGE (single / multiplayer / unknown) turns the view OFF once; multiplayer itself does not.
    if ((g_ticks % 30) == 0 && viewAvailable()) mpNote();
    // enabled=0 (the ini's master switch): the view is not available - a shown one goes off at
    // once, and a request still queued is refused and said.
    if (!g_cfg.enabled) {
        if (g_vs.on) viewForceOff("enabled=0 in uniquetab.ini", false);
        if (takeSwitchRequest()) logI("view: toggle refused - enabled=0 in uniquetab.ini");
    } else if (takeSwitchRequest()) {
        if (g_vs.on) {
            event(kUtViewEvToggle, 0);
        } else if (gameTabOff()) {
            logI("view: toggle refused - the tab is off in this game (the WARN at its entry says why)");
        } else if (!viewAvailable()) {
            logI("view: toggle refused - the view is unavailable (%s)", g_unavailable);
        } else {
            if (!mpNote()) {
                logI("view: toggle refused - multiplayer state unknown (refuse when unsure)");
            } else {
                const int why = utViewRefusal(g_vs);
                if (why != kUtViewWhyOk) {
                    logI("view: toggle refused - %s", utViewWhyText(why));
                } else if (!InterlockedCompareExchange(&g_memberVerified, 0, 0)) {
                    logI("view: toggle refused - the page's cached sack has not been verified "
                         "live yet in this open (show the Transfer tab for one frame)");
                } else if (!protoSack()) {
                    logI("view: toggle refused - the mod sack is not usable");
                } else {
                    const UtViewStep st = utViewApply(&g_vs, kUtViewEvToggle, 0);
                    if (st.turnedOn) {
                        liveDropDirty();   // this build is the page; no second one
                        if (!buildPage()) {
                            viewDataFault("the first page could not be built", true);
                        } else {
                            InterlockedExchange(&g_on, 1);
                            const bool movesMp =
                                utMpMovesAllowed(true, g_vs.mp, g_cfg.mpCollect != 0);
                            logI("view: ON - the Transfer page shows the collection (%s)%s",
                                 !storeTableOwns()
                                     ? "display only: the journal cannot take moves"
                                     : !movesMp
                                           ? "display only in multiplayer: mp_collect=0 refuses "
                                             "deposits and takes"
                                           : "drop or quick-move a unique to deposit it, left-click "
                                             "a collected one to take it",
                                 g_vs.mp ? (movesMp ? " - multiplayer (mp_collect=1): the same "
                                                      "moves as in single player"
                                                    : " - multiplayer")
                                         : "");
                            ownedOnViewOn();   // the owned set + its self-checks
                        }
                    }
                }
            }
        }
    }
    // a pick-up press armed a take that no SetId consumed: disarm. A journalled take whose
    // RemoveItemFromTransfer never came: the prototype leaves the mod sack now (it is on the
    // cursor), so it is never in two places and never destroyed by the next rebuild.
    viewTakeTick();
    // Navigation: rebuild the page's prototypes (the member keeps pointing at the mod sack).
    // no settle - at most one rebuild per Update, every input since the last one in it
    // a UI scale change re-cells the mod sack (InventorySack::OnUIScaleChange): the window
    // is rebuilt whole on the new cell (the shift refuses a stale cell), the same Update.
    // the property search: the ini's query (a change recomputes the highlight and the group
    // marks; the page is never rebuilt for it) and the background index
    searchTick(g_vs.world);
    const bool dirty = liveTakeDirty();
    const bool cellMoved = g_vs.on && protoPlacedCellStale();
    if (cellMoved) {
        unsigned pw = 0, ph = 0, lw = 0, lh = 0;
        protoPlacedCell(&pw, &ph);
        protoCellSize(&lw, &lh);
        logI("view: the mod sack's cell went from %ux%u to %ux%u px (the UI scale changed) - the "
             "window is rebuilt on the new cell", pw, ph, lw, lh);
    }
    // the layout editor opened or closed on the collection page: its items hidden / back
    const bool editFlip = g_vs.on && panelLayoutEditOpen() != (InterlockedCompareExchange(&g_editHid, 0, 0) != 0);
    if ((dirty || cellMoved || editFlip) && g_vs.on) {
        if (!buildPage(true)) {   // the destroy is timed with the build
            viewDataFault("a page could not be built", true);
        } else {
            ownedPageBuilt();
        }
    }
    ownedTick(g_vs.on);
    livePersistOwnedOnly();   // the OWN button's new value, written back once (GD's rule)
    if (!g_firstFrameSaid && InterlockedCompareExchange(&g_firstFrame, 0, 0) != 0) {
        g_firstFrameSaid = true;
        const LONG why = InterlockedCompareExchange(&g_firstFrameWhy, 0, 0);
        if (InterlockedCompareExchange(&g_firstFrame, 0, 0) == 1) {
            logI("view: the page mouse handler's frame VERIFIED at its first call on the collection "
                 "- hovers are proven, prototype tooltips work, clicks stay refused");
        } else {
            logW("view: the page mouse handler's frame did NOT verify at its first call (%s) - "
                 "every call is refused: no tooltips on prototypes, the door unchanged",
                 kFrameWhy[why >= 0 && why <= kWhyPoint ? why : 0]);
        }
    }
    if (!logWants(UT_LOG_TRACE)) return;   // only the TRACE heartbeat reads it
    _snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
                "view=%s mp=%s group=%d row=%d/%d protos=%d refused(drop=%ld add=%ld setid=%ld "
                "remove=%ld under=%ld unsure=%ld sort=%ld sackadd=%ld caller=%ld) "
                "hover=%ld mi=%d %s",
                g_vs.on ? "on" : (viewAvailable() ? "off" : "unavailable"), g_mpShape,
                liveShownGroup(),
                liveShownPage() + 1, liveShownRows(), protoCount(), g_refDrop,
                g_refAdd, g_refSetId, g_refRemove, g_refUnder, g_refUnsure, g_refSort,
                g_refSackAdd, g_refQuickCaller, g_passHover, ownedHeldMi(), ownedStatus());
}

void viewOnModeChanged(int mode) {
    if (mode == g_vs.mode) return;   // every frame: one compare
    event(kUtViewEvModeChanged, mode);
}

void viewOnOpen() {
    InterlockedExchange(&g_memberVerified, 0);
    event(kUtViewEvOpen, 0);
    viewForceOff("caravan open");   // G4: the load path must see the real sack, whatever state
}

void viewOnGoodbye() {
    event(kUtViewEvGoodbye, 0);
    viewForceOff("caravan close");  // G2: idempotent; re-points even if the state says OFF
    InterlockedExchange(&g_memberVerified, 0);
}

void viewOnReopen() {
    // the open's event once more after the goodbye that ran inside it: the state is open again
    // with the view OFF (both PREs turned it off), the live check runs again at this open
    event(kUtViewEvOpen, 0);
    InterlockedExchange(&g_memberVerified, 0);
}

unsigned viewWorldGeneration() { return (unsigned)InterlockedCompareExchange(&g_worldGen, 0, 0); }

void viewOnWorld(bool up) {
    InterlockedIncrement(&g_worldGen);   // the tint's controller cache key
    const UtViewStep st = utViewApply(&g_vs, up ? kUtViewEvWorldUp : kUtViewEvWorldDown, 0);
    (void)st;
    // Every world change is an OFF, and a world that went away takes its objects with it: the
    // prototypes are dropped, never destroyed twice.
    viewForceOff(up ? "world up" : "world down", !up);
    if (up) {
        viewSelectSet();   // the journal of this world's save set
        reconOnWorld();    // its unresolved pending rows against the inventory / equipment
    } else {
        holdRelease("world down");   // only a later save settles it
        storeOnWorldTeardown();
    }
    if (!up) {
        ownedWorldGone();
        g_subWindow = nullptr;
        InterlockedExchange(&g_memberVerified, 0);
    }
}

void viewAccessorEnter(void* subWindow) {
    g_subWindow = subWindow;
    if (subWindow) g_subVft = *(const void* const*)subWindow;   // `this` of a live call
    InterlockedExchange(&g_inAccessor, 1);
}

void viewAccessorLeave(void* subWindow) {
    InterlockedExchange(&g_inAccessor, 0);
    if (InterlockedCompareExchange(&g_memberVerified, 0, 0) || g_vs.on || !hookCaravanOpen())
        return;
    // The live check, once per open while OFF: the member the accessor just stored must be the
    // real Transfer sack at gGameEngine + the decoded offset, and the kind literal must read 1.
    TqSack** at = memberAddr(subWindow);
    TqSack* cur = nullptr;
    int kind = -1;
    const bool readOk =
        at && safeRead(at, &cur, sizeof(cur)) &&
        safeRead((unsigned char*)subWindow + g_tq.pageUiOff + kUiKindOff, &kind, sizeof(kind));
    TqSack* real = realTransfer();
    if (readOk && cur && cur == real && kind == 1) {
        InterlockedExchange(&g_memberVerified, 1);
        static bool said = false;
        if (!said) {
            said = true;
            logI("view: live check - the Transfer page caches the real sack (GameEngine+0x%X) at "
                 "sub-window+0x%X, kind %d",
                 g_tq.transferOff, g_tq.pageUiOff + g_tq.pageSackOff, kind);
        }
    } else {
        static bool warned = false;
        if (!warned) {
            warned = true;
            logW("view: live check FAILED - member %p, real sack %p, kind %d: the view is refused",
                 (void*)cur, (void*)real, kind);
        }
        event(kUtViewEvBindingsIncomplete, 0);
        InterlockedExchange(&g_available, 0);
        _snprintf_s(g_unavailable, sizeof(g_unavailable), _TRUNCATE, "%s",
                    "the live member check failed");
    }
}

// ---- the grid in force: the collection sack moved and sized ----------------------------------
// TQ.exe 0xC2F50 (the Transfer page refresh, every frame the page is shown) SetDims the sack in
// the member to 16 x 15 cells and writes the inventory widget's size (+0x18 / +0x1C); its place
// (+0x10 / +0x14, record x UI scale) is written once, by the widget's loader 0x10E850. While the
// view is ON with the player's grid in force the refresh's POST re-dims the collection sack and
// writes the widget's place and size; the engine's own place is kept and put back at every OFF
// and whenever the view stops asking. The REAL Transfer page is never moved.
namespace {
struct GridWant {
    int user;
    float dx, dy;      // the grid in force's top-left against the derived grid's, drawn px
    int cols, rows;
};
GridWant g_gridWant = {0, 0.0f, 0.0f, 16, 15};   // game thread (ut_panel's layout)
struct GridDone {
    int applied;
    unsigned char* ui;   // the inventory widget written
    float orig[4];       // the engine's own place and size
    float wrote[4];
    float dx, dy;        // the shift written (0 while the engine's own place is in force)
    int cols, rows;
    unsigned cw, ch;
};
GridDone g_gridDone = {};
GridDone g_gridSaid = {};
bool g_gridOff = false;   // a fault: the grid is not moved again in this session

bool writeFloats(unsigned char* at, const float* v, int n) {
    if (!at || !v || n <= 0 || !readable(at, sizeof(float) * (size_t)n)) return false;
    bool ok = false;
    utGuardEnter();
    __try {
        for (int i = 0; i < n; ++i) ((float*)at)[i] = v[i];
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    utGuardLeave();
    return ok;
}
}  // namespace

void viewSetGrid(bool user, float dx, float dy, int cols, int rows) {
    g_gridWant.user = user ? 1 : 0;
    g_gridWant.dx = user ? dx : 0.0f;
    g_gridWant.dy = user ? dy : 0.0f;
    g_gridWant.cols = user ? cols : 16;
    g_gridWant.rows = user ? rows : 15;
    protoSetGrid(g_gridWant.cols, g_gridWant.rows);
}

void viewGridShift(float* dx, float* dy) {
    if (dx) *dx = g_gridDone.applied ? g_gridDone.dx : 0.0f;
    if (dy) *dy = g_gridDone.applied ? g_gridDone.dy : 0.0f;
}

// The engine's own place and size back in the widget, written only while the page object is the
// one the accessor last saw (its vftable) and the place is still the one written.
void viewGridRestore(const char* why) {
    if (!g_gridDone.applied) return;
    unsigned char* ui = g_gridDone.ui;
    g_gridDone.applied = 0;
    g_gridDone.dx = g_gridDone.dy = 0.0f;
    void* sub = g_subWindow;
    const void* vt = nullptr;
    float cur[2] = {0.0f, 0.0f};
    if (!sub || ui != (unsigned char*)sub + g_tq.pageUiOff || !safeRead(sub, &vt, sizeof(vt)) ||
        vt != g_subVft || !safeRead(ui + 0x10, cur, sizeof(cur))) {
        logI("view: the Transfer page's inventory place is not written back (%s) - the page object "
             "is gone", why ? why : "?");
        return;
    }
    if (cur[0] != g_gridDone.wrote[0] || cur[1] != g_gridDone.wrote[1]) {
        logI("view: the Transfer page's inventory place is not written back (%s) - the engine wrote "
             "its own (%.1f,%.1f) since", why ? why : "?", cur[0], cur[1]);
        return;
    }
    const bool ok = writeFloats(ui + 0x10, g_gridDone.orig, 4);
    if (!ok) g_gridOff = true;
    logI("view: the Transfer page's inventory back at its own place %.1f,%.1f %.0fx%.0f (%s)%s",
         g_gridDone.orig[0], g_gridDone.orig[1], g_gridDone.orig[2], g_gridDone.orig[3],
         why ? why : "?", ok ? "" : " - the WRITE FAILED; the grid is not moved again in this session");
}

void viewAccessorGrid(void* subWindow) {
    if (g_gridOff || !subWindow || subWindow != g_subWindow || !g_tq.pageUiOff || !g_tq.pageSackOff)
        return;
    unsigned char* ui = (unsigned char*)subWindow + g_tq.pageUiOff;
    TqSack* cur = nullptr;
    float place[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    if (!safeRead(ui + g_tq.pageSackOff, &cur, sizeof(cur)) || !safeRead(ui + 0x10, place, sizeof(place))) {
        g_gridOff = true;
        logE("view: the Transfer page's inventory could not be read after its refresh - the grid "
             "is not moved again in this session");
        return;
    }
    if (!g_vs.on || !g_gridWant.user || !cur || cur != protoSackBuilt()) {
        if (g_gridDone.applied) viewGridRestore(!g_vs.on ? "the view is OFF" : "the derived grid");
        return;
    }
    unsigned cw = 0, ch = 0;
    if (!protoGridReapply(cur, &cw, &ch)) return;   // the engine call: not guarded
    // the engine's own place: what the widget holds unless it is the place written last
    float own[2], v[4];
    utGridWidget(g_gridDone.applied && g_gridDone.ui == ui, place, g_gridDone.wrote, g_gridDone.orig,
                 g_gridWant.dx, g_gridWant.dy, cw, ch, g_gridWant.cols, g_gridWant.rows, own, v);
    g_gridDone.orig[0] = own[0];
    g_gridDone.orig[1] = own[1];
    g_gridDone.orig[2] = place[2];   // the size the refresh has just written (16 x 15 cells)
    g_gridDone.orig[3] = place[3];
    if (!writeFloats(ui + 0x10, v, 4)) {
        g_gridOff = true;
        logE("view: the Transfer page's inventory could not be written - the grid is not moved again "
             "in this session");
        return;
    }
    g_gridDone.applied = 1;
    g_gridDone.ui = ui;
    for (int i = 0; i < 4; ++i) g_gridDone.wrote[i] = v[i];
    g_gridDone.dx = g_gridWant.dx;
    g_gridDone.dy = g_gridWant.dy;
    g_gridDone.cols = g_gridWant.cols;
    g_gridDone.rows = g_gridWant.rows;
    g_gridDone.cw = cw;
    g_gridDone.ch = ch;
    if (g_gridSaid.applied && g_gridSaid.dx == g_gridDone.dx && g_gridSaid.dy == g_gridDone.dy &&
        g_gridSaid.cols == g_gridDone.cols && g_gridSaid.rows == g_gridDone.rows &&
        g_gridSaid.cw == cw && g_gridSaid.ch == ch && g_gridSaid.orig[0] == g_gridDone.orig[0] &&
        g_gridSaid.orig[1] == g_gridDone.orig[1])
        return;
    g_gridSaid = g_gridDone;
    logI("view: the collection sack is moved for the grid in force - the Transfer page's inventory "
         "at %.1f,%.1f %.0fx%.0f (its own place %.1f,%.1f + %.1f,%.1f px), %d x %d cells of %ux%u "
         "px; the page refresh had written %.0fx%.0f (16 x 15)",
         v[0], v[1], v[2], v[3], g_gridDone.orig[0], g_gridDone.orig[1], g_gridDone.dx, g_gridDone.dy,
         g_gridDone.cols, g_gridDone.rows, cw, ch, place[2], place[3]);
}

TqSack* viewGetterResult(TqSack* real) {
    if (!utViewSubstitute(InterlockedCompareExchange(&g_inAccessor, 0, 0) != 0, g_vs.on,
                          g_vs.mode, g_vs.world, g_vs.mpUnknown))
        return real;
    TqSack* mod = protoSackBuilt();
    const int n = protoCount();
    // an empty OWN page keeps the MOD sack on the page (ut_viewgate.h).
    // the layout editor's empty page too (its items hidden while it is open)
    const bool emptyByDesign =
        n == 0 && InterlockedCompareExchange(&g_emptyByDesign, 0, 0) != 0 &&
        (liveEmptyPageOk() || InterlockedCompareExchange(&g_editHid, 0, 0) != 0);
    return utViewGetterPicksMod(mod != nullptr, n, emptyByDesign) ? mod : real;
}

void viewOnStreamOut(void* ui) {
    if (!ui || !g_tq.pageSackOff) return;
    TqSack** at = (TqSack**)((unsigned char*)ui + g_tq.pageSackOff);
    TqSack* cur = nullptr;
    if (!safeRead(at, &cur, sizeof(cur)) || !protoIsModSack(cur)) return;
    int kind = -1;
    safeRead((unsigned char*)ui + kUiKindOff, &kind, sizeof(kind));
    TqSack* real = realByKind(kind == 0 || kind == 2 ? kind : 1);
    const bool fixed = real && writePtr(at, real);
    logE("view: StreamOut found the MOD sack in a page (kind %d) - %s before the write (G3); the "
         "view is disabled for this session",
         kind, fixed ? "re-pointed to the real sack" : "COULD NOT re-point");
    event(kUtViewEvStreamOutModSack, 0);
    g_vs.faulted = true;
    viewForceOff("StreamOut assert");
}

// after viewOnStreamOut (or its fault), the member is read AGAIN. A mod sack still
// there means the re-point failed: the original must not run (it would write the mod sack - empty
// after protoDestroyAll - over the real file). The caller then returns false, the engine's own
// "could not open the file" result (TQ.exe 0xBFAD6 `xor bl,bl`), so the file on disk keeps the
// content of the last good close.
bool viewStreamOutMustSkip(void* ui) {
    if (!ui || !g_tq.pageSackOff) return false;
    TqSack* cur = nullptr;
    if (!safeRead((unsigned char*)ui + g_tq.pageSackOff, &cur, sizeof(cur))) return false;
    if (!protoIsModSack(cur)) return false;
    g_vs.faulted = true;
    g_vs.on = false;
    InterlockedExchange(&g_on, 0);
    InterlockedExchange(&g_available, 0);
    _snprintf_s(g_unavailable, sizeof(g_unavailable), _TRUNCATE, "%s",
                "the StreamOut assert had to skip a save");
    logE("view: StreamOut SKIPPED - the page member still holds a mod sack (%p) after the re-point; "
         "the engine is told the write failed, so the file on disk keeps the content of the last "
         "good close (changes made on this caravan page since then are NOT saved); the view is "
         "disabled for this session",
         (void*)cur);
    return true;
}

bool viewRefuseTransferDrop() {
    if (!viewOn()) return false;
    InterlockedIncrement(&g_refDrop);
    return true;
}

bool viewRefuseRemove(unsigned id) {
    if (!protoIsId(id)) return false;   // no prototype exists while OFF
    InterlockedIncrement(&g_refRemove);
    return true;
}



namespace {

// read the page mouse handler's own frame at its GetItemUnderPoint
// call. `retSlot[-1]` is the EBP our detour's frame saved = the handler's EBP (MinHook enters the
// detour by a jmp, so nothing between the two touches EBP). Every read is guarded; the frame is
// accepted only when its shape is exact (the decoded distance), the handler's `this` holds this
// very sack at the cached member, and its own point arithmetic reproduces (x, y).
int readHandlerFrame(const TqSack* s, const void* const* retSlot, float x, float y,
                     UtHoverFrame* hv, int* gesture) {
    *gesture = kUtGestUnknown;
    g_lastFrameWhy = kWhyNoSignature;
    if (!g_tq.leftFrameDist || !retSlot || !g_subWindow || !g_tq.pageUiOff || !g_tq.pageSackOff)
        return kUtFrameUnknown;
    unsigned ebp = 0;
    g_lastFrameWhy = kWhyEbpRead;
    if (!safeRead(retSlot - 1, &ebp, sizeof(ebp))) return kUtFrameUnknown;
    g_lastFrameWhy = kWhyShape;
    if (!utUnderFrameShape(ebp, (unsigned)(uintptr_t)retSlot, g_tq.leftFrameDist))
        return kUtFrameUnknown;
    struct {
        unsigned b1, b2;          // the two bools as pushed (the handler reads the low byte)
        const float* mouse;
        const float* origin;
    } a;
    float m[2], o[2], pos[2];
    const TqSack* cached = nullptr;
    const unsigned char* ui = (const unsigned char*)g_subWindow + g_tq.pageUiOff;
    g_lastFrameWhy = kWhyArgsRead;
    if (!safeRead((const void*)(uintptr_t)(ebp + 8), &a, sizeof(a)) ||
        !safeRead(a.mouse, m, sizeof(m)) || !safeRead(a.origin, o, sizeof(o)) ||
        !safeRead(ui + 0x10, pos, sizeof(pos)) ||   // `F3 0F 10 5B 10` / `53 14` (+0x3C / +0x41)
        !safeRead(ui + g_tq.pageSackOff, &cached, sizeof(cached)))
        return kUtFrameUnknown;
    g_lastFrameWhy = kWhySack;
    if (cached != s) return kUtFrameUnknown;
    g_lastFrameWhy = kWhyPoint;
    if (!utUnderPointMatches(x, y, m[0], m[1], pos[0], pos[1], o[0], o[1])) return kUtFrameUnknown;
    g_lastFrameWhy = kWhyNone;
    const int k =
        utUnderFrameKind(true, (unsigned char)(a.b1 & 0xFF), (unsigned char)(a.b2 & 0xFF));
    *gesture = utUnderFrameGesture(true, (unsigned char)(a.b1 & 0xFF), (unsigned char)(a.b2 & 0xFF));
    if (k == kUtFrameHover && hv) {
        hv->valid = true;
        hv->gridX = pos[0] + o[0];
        hv->gridY = pos[1] + o[1];
        hv->originX = o[0];
        hv->originY = o[1];
    }
    return k;
}

}  // namespace

bool viewRefuseUnderPoint(const TqSack* s, const void* ret, const void* const* retSlot, float x,
                          float y, UtHoverFrame* hv, bool* takeCheck, bool* takeRight) {
    if (hv) {
        hv->valid = false;
        hv->realSack = false;
        hv->slotWide = false;
    }
    if (takeCheck) *takeCheck = false;
    if (takeRight) *takeRight = false;
    // Any mod sack (a 0 from an empty one is harmless); a real sack is never touched.
    if (!s || !protoIsModSack(s)) {
        // With the view OFF the pad needs the caravan frame too - the page
        // mouse handler passes the same parent origin for the REAL Transfer sack (the earlier reading,
        // not conditioned on a prototype hover). Only read, never refused.
        if (hv && s && ret == g_tq.retLeftClick && !g_vs.on && s == realTransfer()) {
            int gesture = kUtGestUnknown;
            if (readHandlerFrame(s, retSlot, x, y, hv, &gesture) == kUtFrameHover && hv->valid) {
                hv->realSack = true;
            } else {
                hv->valid = false;
            }
        }
        return false;
    }
    const int site = ret == g_tq.retRightClick   ? kUtUnderRightClick
                     : ret == g_tq.retHeldPickup ? kUtUnderHeldPickup
                     : ret == g_tq.retLeftClick  ? kUtUnderMouseHandler
                                                 : kUtUnderOther;
    if (site == kUtUnderOther) return false;
    int gesture = kUtGestUnknown;
    const int frame = site == kUtUnderMouseHandler ? readHandlerFrame(s, retSlot, x, y, hv, &gesture)
                                                   : kUtFrameUnknown;
    if (site == kUtUnderMouseHandler && !g_firstFrame) {   // the first call's verdict, once
        g_firstFrameWhy = frame == kUtFrameUnknown ? g_lastFrameWhy : kWhyNone;
        InterlockedExchange(&g_firstFrame, frame == kUtFrameUnknown ? 2 : 1);
    }
    // a verified pick-up press while takes are possible is decided after the original
    // (viewTakeFilterUnder); every other gesture keeps the earlier answer exactly.
    const bool takesPossible = takeCheck && viewOn() &&
                               utMpMovesAllowed(!g_vs.mpUnknown, g_vs.mp, g_cfg.mpCollect != 0) &&
                               storeTableOwns();
    const int d = utUnderPointDecide(site, true, gesture, takesPossible);
    if (d == kUtUnderTakeIfCollected || d == kUtUnderTakeRightClick) {
        if (hv) {
            hv->valid = false;
            hv->slotWide = utSlotWideAllowed(site, true, gesture, d);
        }
        *takeCheck = true;
        if (takeRight) *takeRight = d == kUtUnderTakeRightClick;
        return false;
    }
    if (d == kUtUnderPass && !utUnderPointRefuse(site, true, frame)) {
        InterlockedIncrement(&g_passHover);   // the tooltip: the only branch that takes nothing
        if (hv) hv->slotWide = utSlotWideAllowed(site, true, gesture, d);
        return false;
    }
    if (hv) hv->valid = false;
    // a right-click refused HERE says which fact was false - an event
    // (the button handler's call, the mouse handler's verified b2), never a hover or a frame.
    // "the view is OFF" is a defensive first alternative that cannot print in
    // practice (a sack that is not a mod sack returned above, and the mod sack exists only while
    // the view is built): the line names the multiplayer rule or the table. Its absence proves nothing.
    if (site == kUtUnderRightClick || (site == kUtUnderMouseHandler && gesture == kUtGestRight)) {
        const bool mpOk = utMpMovesAllowed(!g_vs.mpUnknown, g_vs.mp, g_cfg.mpCollect != 0);
        logD("view: right-click at (%.1f,%.1f) -> site refused: takes not possible (%s), raw id -, "
             "slot id -, verdict -", x, y,
             !viewOn() ? "the view is OFF" : !mpOk ? "the multiplayer rule" : !storeTableOwns()
                 ? "the journal table is not owned" : "?");
    }
    InterlockedIncrement(site == kUtUnderMouseHandler && frame == kUtFrameUnknown ? &g_refUnsure
                                                                                 : &g_refUnder);
    return true;
}

void viewNoteHover(const UtHoverFrame& hv, float x, float y, unsigned id) {
    if (!hv.valid) return;
    if (hv.realSack) {   // the frame only (the owned grid check needs prototypes)
        TqSack* real = realTransfer();
        unsigned cw = 0, ch = 0;
        utGuardEnter();
        __try {
            if (real && g_tq.SackGetCellWidth && g_tq.SackGetCellHeight) {
                cw = g_tq.SackGetCellWidth(real);
                ch = g_tq.SackGetCellHeight(real);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            cw = ch = 0;
        }
        utGuardLeave();
        if (cw >= 8 && cw <= 256 && ch >= 8 && ch <= 256)
            panelNoteHover(hv.originX, hv.originY, hv.gridX, hv.gridY, cw, ch);
        return;
    }
    if (viewOn()) ownedNoteHover(hv.gridX, hv.gridY, hv.originX, hv.originY, x, y, id);
}

bool viewRefuseSort(const TqSack* s) {
    if (!s || !protoIsModSack(s)) return false;
    InterlockedIncrement(&g_refSort);
    return true;
}

bool viewIsModSack(const void* s) { return protoIsModSack(s); }

bool viewRealPageSite(const TqSack* s, const void* ret) {
    if (!s || g_vs.on || protoIsModSack(s) || s != realTransfer()) return false;
    return ret == g_tq.retRightClick || ret == g_tq.retHeldPickup || ret == g_tq.retLeftClick;
}

bool viewRefuseTransferAdd() {
    if (!viewOn()) return false;
    InterlockedIncrement(&g_refAdd);
    return true;
}

void viewDetach() {
    // DllMain: the member must not keep a pointer into a sack whose owner is going away. A plain
    // guarded write; nothing is freed and no engine function is called under the loader lock.
    InterlockedExchange(&g_on, 0);
    repoint();
}


// =============================================================================================
// the moves. G7 - JOURNAL FIRST: every deposit is on disk before the engine consumes the
// original, every take is on disk before the prototype reaches the cursor. A journal that cannot
// be written refuses every move (one INFO line per gesture kind); the display stays.
// =============================================================================================
namespace {

volatile LONG g_depDrop = 0, g_depQuick = 0, g_takes = 0, g_moveRefused = 0;
unsigned g_takeArmedId = 0;   // a verified pick-up press found a takeable prototype (this gesture)
unsigned g_takeId = 0;        // its take is journalled; RemoveItemFromTransfer hands it out
bool g_takeRight = false;     // ... by the right-click (into the inventory, no cursor)
unsigned long long g_takeSeq = 0;   // ... the journal row it was built from
// the handler holding the last taken item, while it holds it.
TqCursorItemMove* g_holdHandler = nullptr;
unsigned g_holdId = 0;
unsigned long long g_holdSeq = 0;

// Still on that cursor? The handler's dtor (Game.dll 0x153C90) writes the BASE vftable (0x38D450)
// over CursorHandlerItemMove's (0x38D620) before the block is freed, so a destroyed or reused
// handler fails the vftable test; a live one must still carry the taken id.
bool holdStill() {
    if (!g_holdHandler || !g_holdId || !g_tq.CursorVftable) return false;
    bool still = false;
    utGuardEnter();
    __try {
        still = *(const void* const*)g_holdHandler == g_tq.CursorVftable;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        still = false;
    }
    utGuardLeave();
    return still && cursorItemId(g_holdHandler) == g_holdId;
}

// The taken item left the cursor (or can no longer be followed): only a save written after NOW
// settles its "out" row.
void holdRelease(const char* why) {
    if (!g_holdSeq) return;
    const bool touched = journalTouchOut(g_holdSeq);
    journalSetHeldSeq(0);
    logD("journal: the taken item (id %u) left the cursor (%s)%s", g_holdId, why,
         touched ? " - its row settles at the next character save" : "");
    g_holdHandler = nullptr;
    g_holdId = 0;
    g_holdSeq = 0;
}
unsigned g_capId = 0;         // the capability cache: the cursor id it was computed for ...
bool g_capItem = false, g_capCat = false;   // ... and its item facts
unsigned g_capStack = 0;
unsigned g_capRows = 0;       // its record's journal rows (one copy per record)
char g_capName[256] = "";     // its record, for the one line per cursor id below
unsigned g_capSaidId = 0;     // the cursor id whose "not accepted" line was written
int g_lastRefusalSaid = -1;   // one INFO line per refusal reason until another one is said

// the stack count as the lines print it - "?" when item.stackSlot is not decoded
// (protoStackCount then answers 0 without a measurement; a constant must not read like one).
const char* stackText(unsigned n, char* buf, unsigned cap) {
    if (!protoStackReadable()) return "? (item.stackSlot not decoded)";
    _snprintf_s(buf, cap, _TRUNCATE, "%u", n);
    return buf;
}

// kUtDepRefuseHave of a monster infrequent: the reason names the roll the collection holds
// (record, item level, prefix, suffix, seed). "" for every other record.
const char* miRollWhy(const char* record, char* buf, size_t cap) {
    return utMiRollWhy(liveMiTable(), record, buf, cap);
}

// `detail` carries the raw facts (Item::GetNumberInStack, the stacked-id vector).
unsigned g_haveSaidId = 0;    // the item id whose "already in the collection" line was said
void sayRefusal(const char* gesture, int why, const char* record, const char* detail,
                unsigned itemId = 0) {
    InterlockedIncrement(&g_moveRefused);
    if (why == kUtDepRefuseHave) {   // INFO once per item id (the user tries it again)
        if (itemId && itemId == g_haveSaidId) {
            logD("%s REFUSED (%s)%s%s [%s]", gesture, utDepositWhyText(why), record ? ": " : "",
                 record ? record : "", detail ? detail : "");
            return;
        }
        g_haveSaidId = itemId;
        g_lastRefusalSaid = why;
        char roll[640];
        miRollWhy(record, roll, sizeof(roll));
        logI("%s REFUSED - %s: %s [%s, %u row(s) of it]%s%s; the item was not touched", gesture,
             utDepositWhyText(why), record ? record : "?", detail ? detail : "",
             record ? storeCount(record) : 0u, roll[0] ? " - " : "", roll);
        return;
    }
    if (why == g_lastRefusalSaid) {
        logD("%s REFUSED (%s)%s%s [%s]", gesture, utDepositWhyText(why), record ? ": " : "",
             record ? record : "", detail ? detail : "");
        return;
    }
    g_lastRefusalSaid = why;
    logI("%s REFUSED - %s%s%s [%s]; the item was not touched", gesture, utDepositWhyText(why),
         record && record[0] ? ": " : "", record ? record : "", detail ? detail : "");
}

bool depositBindings(bool drag) {
    const bool common = g_tq.ItemGetItemReplicaInfo && g_tq.itemSlotReplica >= 0 &&
                        g_tq.ItemGetNumberInStack && g_tq.ObjectGetObjectName &&
                        g_tq.ObjectManagerGetObjectList && g_tq.CrtOperatorDelete &&
                        liveActive();
    if (!drag) return common;
    return common && g_tq.CtrlSendRemoveItem && g_tq.CharGetControllerId &&
           g_tq.ControllerPlayerVftable && g_tq.cursorPlayerOff && g_tq.ctrlIdOff &&
           g_tq.GameGetMainPlayer;
}

// The handler's player, proved to be the main player, and its controller id.
bool handlerController(const TqCursorItemMove* handler, unsigned* ctrlId) {
    *ctrlId = 0;
    TqGameEngine* ge = gameEngine();
    if (!ge || !handler || !g_tq.cursorPlayerOff) return false;
    const void* player = nullptr;
    if (!safeRead((const unsigned char*)handler + g_tq.cursorPlayerOff, &player, sizeof(player)) ||
        !player)
        return false;
    const void* mainPlayer = nullptr;
    unsigned id = 0;
    utGuardEnter();
    __try {
        mainPlayer = g_tq.GameGetMainPlayer(ge);
        if (mainPlayer == player) id = g_tq.CharGetControllerId(player);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        id = 0;
    }
    utGuardLeave();
    *ctrlId = id;
    return id != 0;
}

// The deposit facts every gesture shares (the item's own facts are filled by the caller).
void baseFacts(UtDepositFacts* f, int caller) {
    memset(f, 0, sizeof(*f));
    f->caller = caller;
    f->viewOn = viewOn() && g_vs.mode == 1 && g_vs.world;
    bool known = false;
    f->mp = isMultiplayer(&known);
    f->mpKnown = known;
    f->mpCollect = g_cfg.mpCollect != 0;   // GD's rule (utMpMoves)
    f->pristineOnly = g_cfg.collectPristineOnly != 0;   // GD's collect_pristine_only
    f->tableOwns = storeTableOwns();
    f->bindings = depositBindings(caller == kUtDepCallerDrag);
}

// collect_pristine_only: the captured identity's parts beyond its record (utPartsPristine).
// `why` names the first one found; "" when the item is pristine or nothing was captured.
void pristineFacts(UtDepositFacts* f, const UtReplicaCapture& cap, char* why, size_t whyCap) {
    why[0] = 0;
    if (!f->haveCap) return;
    const char* parts[kUtIdStrCount - 1];
    const char* names[kUtIdStrCount - 1];
    for (int i = kUtIdPrefix; i < kUtIdStrCount; ++i) {
        parts[i - 1] = cap.str[i];
        names[i - 1] = kUtIdStrKey[i];
    }
    f->notPristine =
        !utPartsPristineKind(parts, names, kUtIdStrCount - 1, f->relicKind, why, whyCap);
}

// One walk: the object of `id`, its folded record, its stack, whether the record is collectable.
void itemFacts(unsigned id, unsigned ctrlId, const void** item, const void** ctrl, char* name,
               unsigned nameCap, bool* isItem, bool* inCat, unsigned* stack) {
    *isItem = false;
    *inCat = false;
    *stack = 0;
    name[0] = 0;
    if (!protoFindObjects(id, ctrlId, item, ctrl) || !*item) return;
    *isItem = true;
    *stack = protoStackCount(*item);   // through the object's own vftable slot
    protoObjectRecord(*item, name, nameCap);
    *inCat = name[0] && (liveHasRecord(name) || liveRelicFind(name) >= 0);
}

// A relic or charm of the relic list (any number of rows; routed by its capture). Sets the
// gate's kind and, once the capture is read, its route. An accepted route gets the DEBUG relic
// line; a refused one gets none - its facts go into `refusal` for the deposit's ONE REFUSED line
// (as a refused unique has one line).
bool relicFacts(UtDepositFacts* f, const char* name) {
    f->relicKind = name[0] && liveRelicFind(name) >= 0;
    return f->relicKind;
}

void relicRouteFacts(UtDepositFacts* f, const UtReplicaCapture& cap, const char* name,
                     char* refusal, size_t refusalCap) {
    if (refusal && refusalCap) refusal[0] = 0;
    if (!f->relicKind || !f->haveCap) return;
    unsigned shards = 0;
    const char* const* table = nullptr;
    int tableN = 0, bonusIndex = -1;
    if (!liveRelicFacts(name, &shards, nullptr, &table, &tableN)) return;
    f->relicRoute = utRelicRoute(cap.var1, shards, cap.str[kUtIdRelicBonus], table, tableN,
                                 &bonusIndex);
    if (!utRelicRouteAccepted(f->relicRoute)) {
        utRelicRefusalFacts(refusal, refusalCap, cap.var1, shards, cap.str[kUtIdRelicBonus],
                            cap.seed);
        return;
    }
    char tail[320], line[1024];
    utRelicRouteTail(f->relicRoute, bonusIndex >= 0 && table ? table[bonusIndex] : "", tail,
                     sizeof(tail));
    utRelicDebugLine(line, sizeof(line), name, cap.var1, shards, cap.str[kUtIdRelic],
                     cap.str[kUtIdRelicBonus], cap.seed, tail);
    logD("%s", line);
}

// The take written to the journal before the prototype leaves (G7). A unique's place: today's
// take of its row. A relic place: the green slot's row whole (journalTakeRow), the red slot's ONE
// shard (journalTakeShard: `*held` = the row of the shard handed out, which the cursor then
// holds). A stale relic page is refused and rebuilt. The red take's DEBUG relic line reads the
// prototype back (protoCapture) before it is handed out.
bool takeOnDisk(unsigned id, const char* record, unsigned long long seq, unsigned long long* held) {
    int slot = 0;
    const bool haveSlot = protoSlotOf(id, &slot);
    const UtRelicTakeCall call = utRelicTakeCallOf(haveSlot, slot);
    *held = seq;
    if (call == kUtRelicTakeUnique) return storeOnTake(record, seq);
    bool taken = false;
    if (call == kUtRelicTakeShard) {
        unsigned long long outSeq = 0;
        unsigned left = 0;
        taken = storeOnTakeShard(record, seq, &outSeq, &left);
        *held = utRelicTakeHeld(call, taken, seq, outSeq);
        if (taken) {
            static UtReplicaCapture cap;   // ~2 KB: kept off the engine's stack
            memset(&cap, 0, sizeof(cap));
            const void* item = nullptr;
            char capName[256], tail[96], line[1024];
            unsigned st = 0, shards = 0;
            if (protoFindObjects(id, 0, &item, nullptr) && item &&
                protoCapture(item, &cap, capName, sizeof(capName), &st)) {
                liveRelicFacts(record, &shards, nullptr, nullptr, nullptr);
                utRelicShardTail(seq, left, tail, sizeof(tail));
                utRelicDebugLine(line, sizeof(line), record, cap.var1, shards, cap.str[kUtIdRelic],
                                 cap.str[kUtIdRelicBonus], cap.seed, tail);
                logD("%s (row %llu handed out)", line, outSeq);
            } else {
                logD("relic: %s - one shard of row %llu, %u left (the prototype could not be read back)",
                     record, seq, left);
            }
        }
    } else {
        taken = storeOnTakeRow(record, seq);
        if (taken) logD("relic: %s - row %llu taken whole (green slot %d)", record, seq, slot - 1);
    }
    if (utRelicTakeRebuild(call, taken)) liveMarkDirty();   // a stale relic page: rebuilt
    return taken;
}

void afterMove() {
    g_capId = 0;         // the capability's cached rows follow the journal
    liveMarkDirty();     // the page is rebuilt from the journal: bright / next copy / bare
    ownedMarkDirty();    // the marks and the label follow the rows
}

}  // namespace

bool viewCapable(const TqCursorItemMove* handler) {
    if (!viewOn()) return false;
    const unsigned id = cursorItemId(handler);
    if (!id) return false;
    if (id != g_capId) {
        g_capId = id;
        const void* item = nullptr;
        char name[256];
        itemFacts(id, 0, &item, nullptr, name, sizeof(name), &g_capItem, &g_capCat, &g_capStack);
        memcpy(g_capName, name, sizeof(g_capName));
        g_capRows = g_capCat ? storeCount(g_capName) : 0u;   // once per cursor id / move
    }
    UtDepositFacts f;
    baseFacts(&f, kUtDepCallerDrag);
    f.isItem = g_capItem;
    f.inCatalogue = g_capCat;
    f.stack = g_capStack;
    f.rowsNow = g_capRows;   // one copy per record (no per-frame journal read)
    if (g_capCat) relicFacts(&f, g_capName);   // a binary search of the list, no journal read
    const int v = utDepositCapableVerdict(f);
    // the user could not tell why nothing happened - ONE debug line per cursor id naming
    // the fact that failed (the slot is asked every frame; the line is not).
    if (v != kUtDepTable && id != g_capSaidId) {
        g_capSaidId = id;
        char sn[16];
        char roll[640];
        if (v == kUtDepRefuseHave) miRollWhy(g_capName, roll, sizeof(roll));
        else roll[0] = 0;
        logD("view: the collection page does not accept item %u%s%s as a deposit - %s%s%s "
             "(Item::GetNumberInStack %s)",
             id, g_capName[0] ? " " : "", g_capName, utDepositWhyText(v), roll[0] ? " - " : "",
             roll, stackText(g_capStack, sn, sizeof(sn)));
    }
    return v == kUtDepTable;
}

int viewDepositDrop(TqCursorItemMove* handler) {
    if (!viewOn()) return kUtMovePass;
    UtDepositFacts f;
    baseFacts(&f, kUtDepCallerDrag);
    const unsigned id = cursorItemId(handler);
    unsigned ctrlId = 0;
    const void* item = nullptr;
    const void* ctrl = nullptr;
    char name[256] = {0};
    static UtReplicaCapture cap;   // ~2 KB: kept off the engine's stack
    memset(&cap, 0, sizeof(cap));
    char relicWhy[400] = "";
    if (f.viewOn && f.bindings && id) {
        if (!handlerController(handler, &ctrlId)) f.bindings = false;
        itemFacts(id, ctrlId, &item, &ctrl, name, sizeof(name), &f.isItem, &f.inCatalogue,
                  &f.stack);
        if (!ctrl) f.bindings = false;
        if (f.isItem && f.inCatalogue) f.rowsNow = storeCount(name);
        if (f.isItem && f.inCatalogue) relicFacts(&f, name);
        if (f.isItem && f.inCatalogue && (!f.rowsNow || f.relicKind) && utStackIsSingle(f.stack)) {
            char capName[256];
            unsigned st = 0;
            f.haveCap = protoCapture(item, &cap, capName, sizeof(capName), &st);
            f.capMatches = f.haveCap && strcmp(cap.record, name) == 0 && utStackIsSingle(st);
            relicRouteFacts(&f, cap, name, relicWhy, sizeof(relicWhy));
        }
    }
    char partWhy[128];
    pristineFacts(&f, cap, partWhy, sizeof(partWhy));
    const unsigned rawStack = f.stack;
    // the engine's own drop also moves the handler's stacked ids (Game.dll 0x152623 /
    // 0x152710); the mod's SetId(0) would just empty that vector. confirmed the layout by
    // disassembly: CreateAndStackIds (0x1527D0) pushes each id with `lea ecx,[ebx+0x28] / call
    // 0x9520` (VS2012 vector<unsigned>::push_back: +0 first, +4 last, +8 end), SetId copies +0x28
    // into +0x2C (clear). So [+0x28] != [+0x2C] = ids are stacked on the cursor: refused as a stack.
    unsigned idsFirst = 0, idsLast = 0;
    bool idsRead = false;
    utGuardEnter();
    __try {
        const unsigned char* h = (const unsigned char*)handler;
        idsFirst = *(const unsigned*)(h + 0x28);
        idsLast = *(const unsigned*)(h + 0x2C);
        idsRead = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        idsRead = false;
    }
    utGuardLeave();
    if (utStackIsSingle(f.stack) && (!idsRead || idsFirst != idsLast)) f.stack = 2u;
    char detail[600], sn[16];
    _snprintf_s(detail, sizeof(detail), _TRUNCATE,
                "%s%sItem::GetNumberInStack %s, stacked ids [+0x28]=%08X [+0x2C]=%08X%s", relicWhy,
                relicWhy[0] ? ", " : "", stackText(rawStack, sn, sizeof(sn)), idsFirst, idsLast,
                idsRead ? "" : " (unreadable)");
    const int v = utDepositDecide(f);
    if (utDepositIsRefusal(v)) {
        sayRefusal("deposit (drag-drop)", v, name, v == kUtDepRefuseNotPristine ? partWhy : detail,
                   id);
        return kUtMoveRefuse;
    }
    unsigned long long seq = 0;
    if (!storeOnDeposit(cap, &seq)) {   // G7: the row is on disk, or nothing happens
        InterlockedIncrement(&g_moveRefused);
        return kUtMoveRefuse;
    }
    // The engine's own disposal (PrimaryTransferActivate 0x1526D3..0x1526F0): the handler
    // player's controller queues the removal of the cursor original. Nothing else is called.
    bool sent = false;
    utGuardEnter();
    __try {
        g_tq.CtrlSendRemoveItem((void*)ctrl, id);
        sent = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        sent = false;
    }
    utGuardLeave();
    if (!sent) {
        // The row stays (a duplicate is the safe side of an unknown: the removal may be queued).
        logE("journal: deposit %s is on disk but SendRemoveItemFromInventory FAULTED - the row "
             "is kept (check the cursor and the inventory for a duplicate); the view is disabled",
             cap.record);
        viewFault("the deposit's disposal faulted");
        return kUtMoveRefuse;
    }
    InterlockedIncrement(&g_depDrop);
    g_lastRefusalSaid = -1;
    g_capId = 0;
    logI("journal: deposit %s (drag-drop, seed %u, %s) - row %llu, %u row(s) of it now",
         cap.record, cap.seed, detail, seq, storeCount(cap.record));
    afterMove();
    return kUtMoveDone;
}

int viewDepositQuick(unsigned id) {
    if (!viewOn()) return kUtMovePass;
    UtDepositFacts f;
    baseFacts(&f, kUtDepCallerQuick);
    const void* item = nullptr;
    char name[256] = {0};
    static UtReplicaCapture cap;
    memset(&cap, 0, sizeof(cap));
    char relicWhy[400] = "";
    if (f.viewOn && f.bindings && id) {
        itemFacts(id, 0, &item, nullptr, name, sizeof(name), &f.isItem, &f.inCatalogue, &f.stack);
        if (f.isItem && f.inCatalogue) f.rowsNow = storeCount(name);
        if (f.isItem && f.inCatalogue) relicFacts(&f, name);
        if (f.isItem && f.inCatalogue && (!f.rowsNow || f.relicKind) && utStackIsSingle(f.stack)) {
            char capName[256];
            unsigned st = 0;
            f.haveCap = protoCapture(item, &cap, capName, sizeof(capName), &st);
            f.capMatches = f.haveCap && strcmp(cap.record, name) == 0 && utStackIsSingle(st);
            relicRouteFacts(&f, cap, name, relicWhy, sizeof(relicWhy));
        }
    }
    char partWhy[128];
    pristineFacts(&f, cap, partWhy, sizeof(partWhy));
    // A refusal here leaves the item where it is and nothing else: the hook returns false to the
    // quick-move's primary call (TQ.exe 0x107A6C), whose false branch (0x107C05) reports the click
    // handled, so the caller (0x107461) never runs its own right-click (the item's secondary cursor
    // handler - for a relic, the socketing cursor). The same for every kind of item.
    char detail[512], sn[16];
    _snprintf_s(detail, sizeof(detail), _TRUNCATE, "%s%sItem::GetNumberInStack %s", relicWhy,
                relicWhy[0] ? ", " : "", stackText(f.stack, sn, sizeof(sn)));
    const int v = utDepositDecide(f);
    if (utDepositIsRefusal(v)) {
        sayRefusal("deposit (quick-move)", v, name, v == kUtDepRefuseNotPristine ? partWhy : detail,
                   id);
        return kUtMoveRefuse;
    }
    unsigned long long seq = 0;
    if (!storeOnDeposit(cap, &seq)) {
        InterlockedIncrement(&g_moveRefused);
        return kUtMoveRefuse;
    }
    InterlockedIncrement(&g_depQuick);
    g_lastRefusalSaid = -1;
    logI("journal: deposit %s (quick-move, seed %u, %s) - row %llu, %u row(s) of it now; the "
         "game removes the original from the inventory", cap.record, cap.seed, detail, seq,
         storeCount(cap.record));
    afterMove();
    return kUtMoveDone;
}

namespace {

// the page the right-click served is the Transfer page holding THIS mod sack: the page
// object (sub-window + pageUiOff) caches 's' and its mode field is 1. The right-click's own switch
// reads the mode at TQ.exe 0xBFDF3 ("8B 86 48 01 00 00" mov eax,[esi+0x148]: 0 stash ->
// RemoveItemFromStash, 1 -> RemoveItemFromTransfer, 2 -> RemoveItemFromRelicVault); the mouse
// handler reads the same field at 0xBFFE8 ("8B 83 48 01 00 00 / 83 F8 01").
const unsigned kPageModeOff = 0x148;
bool rightClickOnTransferPage(const TqSack* s) {
    if (!g_subWindow || !g_tq.pageUiOff || !g_tq.pageSackOff || !s) return false;
    const unsigned char* ui = (const unsigned char*)g_subWindow + g_tq.pageUiOff;
    const TqSack* cached = nullptr;
    int mode = -1;
    if (!safeRead(ui + g_tq.pageSackOff, &cached, sizeof(cached)) ||
        !safeRead(ui + kPageModeOff, &mode, sizeof(mode)))
        return false;
    return cached == s && mode == 1;
}

// room for the item in the main player's inventory, asked the way
// PlayerInventoryCtrl::AddItem (Game.dll 0x21A450) places it: its sacks, each with
// InventorySack::IsSpaceForItem (const: FindNextPosition, nothing written).
// 1 room, 0 full, -1 unknown (refused: refuse when unsure).
// `engineCheck` = also ask Player::IsInventorySpaceAvailable(item) - the check the page
// mouse handler's right-click (TQ.exe 0xC0650) makes before it gives the item: a take journalled
// on a "yes" the engine then turns into a "no" would never see its RemoveItemFromTransfer.
int inventoryRoomFor(unsigned id, int* asked, bool engineCheck, bool* engineSaidNo) {
    *asked = 0;
    if (engineSaidNo) *engineSaidNo = false;
    TqGameEngine* ge = gameEngine();
    if (!ge || !g_tq.GameGetMainPlayer || !g_tq.CharGetControllerId || !g_tq.CtrlGetInventoryCtrl ||
        !g_tq.InvCtrlGetNumberOfSacks || !g_tq.InvCtrlGetSack || !g_tq.SackIsSpaceForItem)
        return -1;
    unsigned ctrlId = 0;
    const TqPlayer* player = nullptr;
    utGuardEnter();
    __try {
        player = g_tq.GameGetMainPlayer(ge);
        if (player) ctrlId = g_tq.CharGetControllerId(player);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ctrlId = 0;
    }
    utGuardLeave();
    const void* item = nullptr;
    const void* ctrl = nullptr;
    if (!ctrlId || !protoFindObjects(id, ctrlId, &item, &ctrl) || !item || !ctrl) return -1;
    int room = -1;
    int n1 = 0;
    utGuardEnter();
    __try {
        void* inv = g_tq.CtrlGetInventoryCtrl((void*)ctrl);
        const unsigned n = inv ? g_tq.InvCtrlGetNumberOfSacks(inv) : 0u;
        if (inv && n >= 1u && n <= 16u) {
            room = 0;
            for (unsigned i = 0; i < n && room == 0; ++i) {
                const TqSack* sack = g_tq.InvCtrlGetSack(inv, (int)i);
                ++n1;
                if (sack && g_tq.SackIsSpaceForItem(sack, (const TqItem*)item)) room = 1;
            }
        }
        if (room == 1 && engineCheck) {   // the engine's own answer, or refused
            if (!g_tq.PlayerIsInventorySpaceAvailable || !player) {
                room = -1;
            } else if (!g_tq.PlayerIsInventorySpaceAvailable(player, (const TqItem*)item)) {
                room = 0;
                if (engineSaidNo) *engineSaidNo = true;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        room = -1;
    }
    utGuardLeave();
    *asked = n1;
    return room;
}

// the engine's own "inventory full" sound on the main player - what the mouse
// right-click's engine branch plays when its room check says no (TQ.exe 0xC0650 -> 0xC067A,
// Player::PlayInventoryFullSound). The mod's refusal returns 0, so that branch never runs and the
// click was silent in game. The sound only: the engine's dialog is not reproduced (no invented UI).
// A missing export or a fault stays silent, as before. GD plays the same export (ut_reagent.cpp).
void playInventoryFullSound() {
    TqGameEngine* ge = gameEngine();
    if (!ge || !g_tq.GameGetMainPlayer || !g_tq.PlayerPlayInventoryFullSound) return;
    utGuardEnter();
    __try {
        const TqPlayer* player = g_tq.GameGetMainPlayer(ge);
        if (player) g_tq.PlayerPlayInventoryFullSound(const_cast<TqPlayer*>(player));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    utGuardLeave();
}

bool g_rightRuleSaid = false;
bool g_rightPageSaid = false;   // the "not the Transfer page" refusal, INFO once

// The take facts every take gesture shares (the left-click pick, the right-click).
int takeDecideFor(unsigned id, const char** record, unsigned long long* seq) {
    UtTakeFacts t;
    memset(&t, 0, sizeof(t));
    t.viewOn = viewOn();
    bool known = false;
    t.mp = isMultiplayer(&known);
    t.mpKnown = known;
    t.mpCollect = g_cfg.mpCollect != 0;
    t.tableOwns = storeTakeAllowed();   // and the save watch sees a Player.chr
    unsigned long long newest = 0;
    *record = nullptr;
    *seq = 0;
    t.isPrototype = id && protoLookup(id, record, seq);
    t.fromRow = *seq != 0;
    if (t.isPrototype && *record) {
        char key[256];
        t.rows = storeCount(*record);
        int slot = 0;
        if (protoSlotOf(id, &slot) && slot > 0)
            t.rowIsNewest = true;   // a relic place: the take re-checks its slot's choice
        else
            t.rowIsNewest = utJournalKey(*record, key, sizeof(key)) &&
                            journalNewest(key, nullptr, &newest) && newest == *seq;
    }
    return utTakeDecide(t);
}

}  // namespace

// the take verdict's name for the right-click trace (utTakeDecide, ut_depositgate.h)
static const char* takeVerdictName(int v) {
    switch (v) {
    case kUtTakeOk: return "kUtTakeOk";
    case kUtTakeRefuseViewOff: return "kUtTakeRefuseViewOff";
    case kUtTakeRefuseMultiplayer: return "kUtTakeRefuseMultiplayer";
    case kUtTakeRefuseTableOff: return "kUtTakeRefuseTableOff";
    case kUtTakeRefuseNotPrototype: return "kUtTakeRefuseNotPrototype";
    case kUtTakeRefuseBare: return "kUtTakeRefuseBare";
    case kUtTakeRefuseStale: return "kUtTakeRefuseStale";
    default: return "?";
    }
}

unsigned viewTakeRightClick(const TqSack* s, unsigned id, unsigned rawId, float x, float y,
                            bool viaMouse) {
    g_takeArmedId = 0;
    const char* record = nullptr;
    unsigned long long seq = 0;
    const int verdict = takeDecideFor(id, &record, &seq);
    // one DEBUG line per right-click on a mod sack - an event, never a frame
    logD("view: right-click at (%.1f,%.1f) -> site ok (%s), raw id %u, slot id %u, verdict %s", x, y,
         viaMouse ? "the page mouse handler's b2, TQ.exe+0xC00CE" : "the button handler, TQ.exe+0xBFDD5",
         rawId, rawId ? 0u : id, takeVerdictName(verdict));
    if (verdict != kUtTakeOk || !record) {
        InterlockedIncrement(&g_refUnder);   // a dim record, the view OFF, MP: nothing
        return 0;
    }
    char rec[256];
    _snprintf_s(rec, sizeof(rec), _TRUNCATE, "%s", record);
    if (!rightClickOnTransferPage(s)) {
        InterlockedIncrement(&g_refUnder);
        const char* fmt = "take (right-click) REFUSED - the right-click's page is not the Transfer "
                          "page holding the collection (its mode field +0x148 is not 1, or it "
                          "caches another sack): %s; the item stays in the collection%s";
        if (!g_rightPageSaid) {
            g_rightPageSaid = true;
            logI(fmt, rec, " (said once; later refusals of this kind are DEBUG)");
        } else {
            logD(fmt, rec, "");
        }
        return 0;
    }
    if (!g_rightRuleSaid) {
        g_rightRuleSaid = true;
        logI("view: right-click takes check the inventory's room UP FRONT (InventorySack::"
             "IsSpaceForItem on the sacks PlayerInventoryCtrl::AddItem tries) - a full inventory "
             "refuses the take and the item stays in the collection (the engine itself would drop "
             "it on the ground: Player::GiveItemToCharacter -> SendDropItemRandom); the MOUSE "
             "right-click also asks Player::IsInventorySpaceAvailable, as its engine branch does "
             "before it gives the item (TQ.exe 0xC0650)");
    }
    int asked = 0;
    bool engineNo = false;
    const int room = inventoryRoomFor(id, &asked, viaMouse, &engineNo);
    if (room != 1) {
        InterlockedIncrement(&g_moveRefused);
        logI("take (right-click) REFUSED - %s for %s (%d sack(s) asked); the item stays in the "
             "collection",
             room == 0 ? (engineNo ? "no room in the inventory (Player::IsInventorySpaceAvailable)"
                                   : "no room in the inventory")
                       : "the inventory's room is unknown",
             rec, asked);
        if (room == 0 && viaMouse) playInventoryFullSound();   // not silent
        return 0;
    }
    unsigned long long held = seq;
    if (!takeOnDisk(id, rec, seq, &held)) {   // G7: the row is marked out on disk, or nothing happens
        InterlockedIncrement(&g_moveRefused);
        return 0;
    }
    // RemoveItemFromTransfer hands it out after GiveItemToCharacter: TQ.exe 0xC0265 on the mouse
    // right-click (b2; it reads the id the detour returned from [page+0x110]), 0xBFE6E on the button
    // handler's
    g_takeId = id;
    g_takeSeq = held;   // a relic shard: the row of the shard handed out
    g_takeRight = true;
    protoKeep(id);   // never destroyed from now on
    return id;
}

unsigned viewTakeFilterUnder(unsigned id) {
    g_takeArmedId = 0;
    UtTakeFacts t;
    memset(&t, 0, sizeof(t));
    t.viewOn = viewOn();
    bool known = false;
    t.mp = isMultiplayer(&known);
    t.mpKnown = known;
    t.mpCollect = g_cfg.mpCollect != 0;
    t.tableOwns = storeTakeAllowed();   // and the save watch sees a Player.chr
    const char* record = nullptr;
    unsigned long long seq = 0, newest = 0;
    t.isPrototype = id && protoLookup(id, &record, &seq);
    t.fromRow = seq != 0;
    if (t.isPrototype && record) {
        char key[256];
        t.rows = storeCount(record);
        int slot = 0;
        if (protoSlotOf(id, &slot) && slot > 0) {
            t.rowIsNewest = true;   // a relic place: the take re-checks its slot's choice
        } else {
            t.rowIsNewest = utJournalKey(record, key, sizeof(key)) &&
                            journalNewest(key, nullptr, &newest) && newest == seq;
        }
    }
    const int v = utTakeDecide(t);
    if (v != kUtTakeOk) {
        InterlockedIncrement(&g_refUnder);
        return 0;   // the earlier answer: nothing under the cursor
    }
    g_takeArmedId = id;
    return id;
}

unsigned viewFilterSetId(unsigned id) {
    if (!protoIsId(id)) return id;
    if (id && id == g_takeArmedId) {
        g_takeArmedId = 0;
        const char* record = nullptr;
        unsigned long long seq = 0;
        unsigned long long held = 0;
        if (protoLookup(id, &record, &seq) && seq && viewOn() && takeOnDisk(id, record, seq, &held)) {
            g_takeId = id;   // G7: the take is on disk; the cursor may have the prototype now
            g_takeSeq = held;   // a relic shard: the row of the shard handed out
            protoKeep(id);   // from now on this object is never destroyed
            return id;
        }
    }
    InterlockedIncrement(&g_refSetId);
    return 0;
}

int viewTakeRemove(unsigned id) {
    if (!protoIsId(id)) return kUtMovePass;   // not a prototype (none exists while OFF)
    if (!id || id != g_takeId) {
        InterlockedIncrement(&g_refRemove);
        return kUtMoveRefuse;
    }
    g_takeId = 0;
    const bool right = g_takeRight;   // cleared on every path, the fault's too
    g_takeRight = false;
    const char* record = nullptr;
    unsigned long long seq = 0;
    protoLookup(id, &record, &seq);
    char rec[256];
    _snprintf_s(rec, sizeof(rec), _TRUNCATE, "%s", record ? record : "?");
    if (!protoHandOut(id)) {
        // forgotten BEFORE the fault's teardown and kept on the
        // forgotten list, so the teardown neither destroys it nor auto-places a clone of it into
        // the real Transfer; the sack whose map may still list it is retired by that teardown.
        protoForgetTaken(id);
        logE("journal: take %s%s is on disk but the prototype could not leave the mod sack - the "
             "item %s (never destroyed, never returned); the view is disabled and its teardown "
             "retires the mod sack", right ? "(right-click) " : "", rec,
             right ? "is in the inventory (Player::GiveItemToCharacter)" : "stays on the cursor");
        viewFault("a taken prototype could not leave the mod sack");
        return kUtMoveDone;
    }
    InterlockedIncrement(&g_takes);
    if (right) {   // the engine already gave the object to the player (0xC0530)
        const bool touched = journalTouchOut(seq);   // only a save written after NOW settles it
        logI("journal: take (right-click) %s - row %llu into the inventory "
             "(Player::GiveItemToCharacter), %u row(s) of it left%s", rec, seq, storeCount(rec),
             touched ? "; the row settles at the next character save" : "");
    } else {
        logI("journal: take %s - row %llu handed to the cursor, %u row(s) of it left", rec, seq,
             storeCount(rec));
    }
    afterMove();
    return kUtMoveDone;
}

// The world's save set, read once per world up on the game thread.
void viewSelectSet() {
    TqEngine* e = engine();
    TqGameEngine* ge = gameEngine();
    bool modKnown = false, questKnown = false, inMain = false;
    char mod[128] = {0};
    TqStdString s;
    memset(&s, 0, sizeof(s));
    bool got = false;
    utGuardEnter();
    __try {
        if (e && g_tq.EngineGetGameInfo && g_tq.GameInfoGetModName) {
            TqGameInfo* gi = g_tq.EngineGetGameInfo(e);
            if (gi) {
                g_tq.GameInfoGetModName(gi, &s, 1);   // the Transfer save path's own call
                got = true;
            }
        }
        if (ge && g_tq.GameGetMainPlayer && g_tq.PlayerIsInMainQuest) {
            const TqPlayer* p = g_tq.GameGetMainPlayer(ge);
            if (p) {
                inMain = g_tq.PlayerIsInMainQuest(p);
                questKnown = true;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        got = false;
        questKnown = false;
    }
    utGuardLeave();
    if (got) {
        modKnown = s.size == 0 || safeStdString(&s, mod, sizeof(mod));
        if (s.res >= 16u && s.ptr && g_tq.CrtOperatorDelete) {   // the engine's heap: MSVCR110
            utGuardEnter();
            __try {
                g_tq.CrtOperatorDelete(s.ptr);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
            utGuardLeave();
        }
    }
    storeSelectSet(modKnown, mod, questKnown, inMain);
    liveFollowGame();   // the game's uniquetab.state, when the set chose another game
}

void viewTakeOnCursor(TqCursorItemMove* handler, unsigned id) {
    if (!handler || !id || id != g_takeId || !g_takeSeq) return;
    holdRelease("another take");   // the pick-up needs an empty cursor: the last one was placed
    g_holdHandler = handler;
    g_holdId = id;
    g_holdSeq = g_takeSeq;
    journalSetHeldSeq(g_holdSeq);
}

unsigned viewTakeRightOnFault(unsigned id) {
    if (!id || id != g_takeId || !g_takeRight) return 0u;
    // journalled (the row is out on disk) and armed: the engine must give it to the player, or
    // the tick would hand the prototype out of the mod sack to nobody
    logW("journal: take (right-click) id %u faulted after its journal - the engine gets the id "
         "(Player::GiveItemToCharacter, then RemoveItemFromTransfer hands it out)", id);
    return id;
}

unsigned viewSetIdOnFault(unsigned id) {
    if (id && id == g_takeId) return id;   // journalled: the cursor must get it
    return protoIsId(id) ? 0u : id;
}

void viewTakeTick() {
    // the taken item left the cursor -> its row may settle from now on.
    if (g_holdSeq && !holdStill()) holdRelease("placed");
    g_takeArmedId = 0;
    if (g_takeId) {
        const unsigned id = g_takeId;
        g_takeId = 0;
        g_takeRight = false;
        if (protoIsId(id)) {
            logW("journal: a journalled take (id %u) saw no RemoveItemFromTransfer - the prototype "
                 "leaves the mod sack now", id);
            if (!protoHandOut(id)) {
                protoForgetTaken(id);   // never destroyed
                viewFault("a taken prototype could not leave the mod sack");
            }
            afterMove();
        }
    }
}

// =============================================================================================
// the lost-item guards, the slot-wide answer.
// =============================================================================================
namespace {
bool g_quickRowSaid = false;
}  // namespace

bool viewSackAddRefused(const TqSack* s, const TqItem* item, const void* ret, bool vec) {
    if (utSackAddDecide(protoIsModSack(s), protoPlacing()) == kUtSackAddPass) return false;
    InterlockedIncrement(&g_refSackAdd);
    char where[64] = "?", rec[200] = "?";
    fmtAddr(ret, where, sizeof(where));
    unsigned id = 0;
    utGuardEnter();
    __try {
        if (item && g_tq.ObjectGetObjectId) id = g_tq.ObjectGetObjectId(item);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        id = 0;
    }
    utGuardLeave();
    if (item) protoObjectRecord(item, rec, sizeof(rec));
    logW("view: REFUSED an engine add into the collection sack %p - InventorySack::AddItem(%s) from "
         "%s, item id %u (%s); the item was not added (false, the original never ran)",
         (const void*)s, vec ? "Vec2, Item*, bool" : "Item*, bool", where, id, rec);
    return true;
}

void viewSackRemoveNoted(const TqSack* s, unsigned id, bool result, const void* ret) {
    if (protoPlacing()) return;   // the mod's own (a hand-out, a rebuild, a return): not an event
    char where[64] = "?";
    fmtAddr(ret, where, sizeof(where));
    const char* record = nullptr;
    const bool proto = protoLookup(id, &record, nullptr);
    logD("view: InventorySack::RemoveItem(%u) on the collection sack %p from %s -> %d (%s%s)", id,
         (const void*)s, where, result ? 1 : 0, proto ? "prototype " : "NOT a prototype",
         proto && record ? record : "");
}

int viewQuickCaller(const void* ret, unsigned id) {
    (void)id;
    const bool confirmed = g_tq.retQuickMove != nullptr;
    const int d = utQuickCallerDecide(confirmed, confirmed && ret == g_tq.retQuickMove);
    if (d == kUtQuickPrimary) return d;
    InterlockedIncrement(&g_refQuickCaller);
    if (d == kUtQuickRefuse && !g_quickRowSaid) {
        g_quickRowSaid = true;
        logI("deposit (quick-move) REFUSED - the quick-move's call site is not confirmed "
             "(binding exe.quickMoveAdd.ret); the item was not touched");
    }
    return d;
}

void viewQuickVanillaNoted(const void* ret, unsigned id, bool added) {
    char where[64] = "?";
    fmtAddr(ret, where, sizeof(where));
    logE("view: GameEngine::AddItemToTransfer(item id %u) from %s while the collection is shown - "
         "not the quick-move's primary call (TQ.exe+0x%X), so NOT a deposit and nothing journalled: "
         "handed to the engine's own AddItemToTransfer -> %s",
         id, where,
         (unsigned)((const unsigned char*)g_tq.retQuickMove - (const unsigned char*)g_tq.exe),
         added ? "a copy is on the REAL Transfer page (the caller disposes of the original, as "
                 "vanilla does)"
               : "the engine refused it (a full Transfer?) - the caller disposes of it, as vanilla "
                 "does");
}

// the point is in sack px = drawn px (the engine's GetItemUnderPoint, Game.dll 0x1B6580,
// compares it with each item's sack rect: no division, no scaling), so the cell it is divided by
// is the mod sack's LIVE cell (protoCellSize), the drawn cell - never a cell cached at the build.
// while the prototypes still sit on the cell they were placed on (a UI scale
// change, until viewTick's rebuild) no id is handed out - the engine's own lookup answers.
unsigned viewSlotIdAt(const TqSack* s, float x, float y) {
    if (!s || s != protoSackBuilt() || !viewOn() || protoPlacedCellStale()) return 0;
    unsigned cw = 0, ch = 0;
    if (!protoCellSize(&cw, &ch) || !cw || !ch) return 0;
    if (!(x >= 0.0f && y >= 0.0f && x < 1.0e6f && y < 1.0e6f)) return 0;   // NaN-safe
    return protoIdAtSlotCell((int)(x / (float)cw), (int)(y / (float)ch));
}

void* viewPageWindow() { return g_subWindow; }

// the numbers the Transfer page draw places its inventory with, read in its
// PRE-detour (every read under SEH, nothing written); the arithmetic is utDrawOrigin (pure, tested
// in the viewgate suite). The cell size is the REAL Transfer sack's (the mod sack copies it).
int viewDrawOrigin(void* page, const void* origin, int pass, float* pageX, float* pageY,
                   float* gridX, float* gridY, unsigned* cellW, unsigned* cellH) {
    if (!page || page != g_subWindow || !g_tq.pageUiOff || !origin) return kUtDrawBadInput;
    UtDrawOriginIn in;
    memset(&in, 0, sizeof(in));
    in.pass = pass;
    TqEngine* e = engine();
    TqSack* real = realTransfer();
    unsigned cw = 0, ch = 0;
    bool read = false;
    utGuardEnter();
    __try {
        const float* o = (const float*)origin;
        const unsigned char* pg = (const unsigned char*)page;
        const unsigned char* ui = pg + g_tq.pageUiOff;
        in.originX = o[0];
        in.originY = o[1];
        in.posX = *(const float*)(pg + 0x1C);   // 0xC31AE / 0xC31B5
        in.posY = *(const float*)(pg + 0x20);
        // less the shift of a moved collection sack: the frame is the page's, wherever the grid
        // in force sits (viewAccessorGrid)
        in.invX = *(const float*)(ui + 0x10) - g_gridDone.dx;   // the handler's `F3 0F 10 5B 10`
        in.invY = *(const float*)(ui + 0x14) - g_gridDone.dy;   // / `53 14`
        if (e && g_tq.EngineGetUIScale) in.uiScale = g_tq.EngineGetUIScale(e);
        TqGraphicsEngine* gfx =
            (e && g_tq.EngineGetGraphicsEngine) ? g_tq.EngineGetGraphicsEngine(e) : nullptr;
        if (gfx && g_tq.GfxIsDownsizing) {
            in.downsizing = g_tq.GfxIsDownsizing(gfx);
            in.downsizingKnown = true;
        }
        if (real && g_tq.SackGetCellWidth && g_tq.SackGetCellHeight) {
            cw = g_tq.SackGetCellWidth(real);
            ch = g_tq.SackGetCellHeight(real);
        }
        read = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        read = false;
    }
    utGuardLeave();
    if (!read || !(cw >= 8 && cw <= 256 && ch >= 8 && ch <= 256)) return kUtDrawBadInput;
    if (cellW) *cellW = cw;
    if (cellH) *cellH = ch;
    return utDrawOrigin(in, pageX, pageY, gridX, gridY);
}

// The Transfer page's cover (TransferPlate, read by the page's loader TQ.exe 0xC2C50 into the
// widget at page+0xE2C: TQ.exe's bitmap class, vftable rva 0x21C998). Its GraphicsTexture* sits at
// widget+0x34 - the loader 0xB61F0 reads the texture's rect from it (0xB657D GetRect) and the
// destructor 0xB6D70 hands whatever pointer is there to GraphicsEngine::UnloadTexture (0xB6DCB);
// The floats at widget+0x40 / +0x44 (written at 0xB64A8 / 0xB64BC) are logged as read: in the game
// they are not the bitmap's place (0.0,0.0 and 0.0,4.6e27 have been read there), so nothing uses
// them. One INFO line per world, pure reads under SEH, nothing written.
static void viewCoverRead(const void* page) {
    const void* vt = nullptr;
    const void* tex = nullptr;
    float bx = 0.0f, by = 0.0f;
    bool read = false;
    utGuardEnter();
    __try {
        const unsigned char* w = (const unsigned char*)page + 0xE2C;
        vt = *(const void* const*)w;
        tex = *(const void* const*)(w + 0x34);
        bx = *(const float*)(w + 0x40);
        by = *(const float*)(w + 0x44);
        read = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        read = false;
    }
    utGuardLeave();
    if (!read) {
        logW("view: the Transfer page's cover could not be read (page+0xE2C) - no cover line in this world");
        return;
    }
    const unsigned char* exe = (const unsigned char*)GetModuleHandleA(nullptr);
    const bool shape = exe && vt == (const void*)(exe + 0x21C998);
    logI("view: the Transfer page's cover at page+0xE2C: vftable %p (%s TQ.exe's bitmap class +0x21C998), "
         "texture [+0x34] %p, the widget's +0x40/+0x44 %.1f,%.1f",
         vt, shape ? "=" : "NOT", tex, bx, by);
}

// The caravan geometry, derived (the game's uniq-geometry.txt, liveCaravanGeometry) against
// measured (the numbers the Transfer page draw places its inventory with, and the REAL Transfer
// sack's cell count: InventorySack +0x20 / +0x24, which SetDims writes - Game.dll 0x1B5790), one
// line at the first Transfer page draw of each world. The widget's size [widget+0x18 / +0x1C] is
// logged too: TQ.exe writes cellW x 16 / cellW x 15 there whenever the page is shown (0xC2F50).
// Pure reads under SEH; the engine getters outside the guarded frame; nothing allocated. Said
// whether the tab is on or off in the game: with no derived geometry (a game whose set carries no
// uniq-geometry.txt) the line gives the measured place only.
void viewGeometryCheck(void* page, int pass) {
    static unsigned s_saidWorld = 0xFFFFFFFFu;
    const unsigned world = viewWorldGeneration();
    if (s_saidWorld == world || pass == 1) return;
    if (!page || page != g_subWindow || !g_tq.pageUiOff) return;
    UtCaravanGeo d;
    const bool derived = liveCaravanGeometry(&d);
    if (!derived) d = utCaravanGeoBase();   // the measured numbers only (utCaravanGeoAgree's mx, my)
    static unsigned s_coverWorld = 0xFFFFFFFFu;
    if (s_coverWorld != world) {   // the cover's inputs, once per world
        s_coverWorld = world;
        viewCoverRead(page);
    }
    UtCaravanMeasure m;
    memset(&m, 0, sizeof(m));
    TqSack* real = realTransfer();
    float ww = 0.0f, wh = 0.0f;
    bool read = false;
    utGuardEnter();
    __try {
        const unsigned char* pg = (const unsigned char*)page;
        const unsigned char* ui = pg + g_tq.pageUiOff;
        m.pageX = *(const float*)(pg + 0x1C);
        m.pageY = *(const float*)(pg + 0x20);
        m.invX = *(const float*)(ui + 0x10) - g_gridDone.dx;   // the engine's own place
        m.invY = *(const float*)(ui + 0x14) - g_gridDone.dy;
        ww = *(const float*)(ui + 0x18);
        wh = *(const float*)(ui + 0x1C);
        if (real) {
            m.gridW = *(const int*)((const unsigned char*)real + 0x20);
            m.gridH = *(const int*)((const unsigned char*)real + 0x24);
        }
        read = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        read = false;
    }
    utGuardLeave();
    if (!read) {
        s_saidWorld = world;
        logE("view: the caravan geometry could not be measured (a fault reading the Transfer page) - "
             "not measured again in this world");
        return;
    }
    TqEngine* e = engine();
    bool downsizing = true;
    if (e && g_tq.EngineGetUIScale) m.uiScale = g_tq.EngineGetUIScale(e);
    TqGraphicsEngine* gfx =
        (e && g_tq.EngineGetGraphicsEngine) ? g_tq.EngineGetGraphicsEngine(e) : nullptr;
    if (gfx && g_tq.GfxIsDownsizing) downsizing = g_tq.GfxIsDownsizing(gfx);
    if (downsizing || !real) {
        // Said once per world with the reason; the inventory place is scaled while downsized.
        s_saidWorld = world;
        if (!derived)
            logI("view: caravan geometry - derived: none (this game's set has no uniq-geometry.txt); "
                 "not measured in this world: %s",
                 !real ? "the real Transfer sack is not known"
                 : (gfx && g_tq.GfxIsDownsizing) ? "the UI is downsized (the inventory place is scaled)"
                                                 : "GraphicsEngine::IsDownsizing is not bound");
        else
        logI("view: caravan geometry - derived %d,%d %dx%d = %d x %d cells (the game's database); "
             "not measured in this world: %s", d.x0, d.y0, d.w, d.h, d.cols, d.rows,
             !real ? "the real Transfer sack is not known"
             : (gfx && g_tq.GfxIsDownsizing) ? "the UI is downsized (the inventory place is scaled)"
                                             : "GraphicsEngine::IsDownsizing is not bound");
        return;
    }
    float mx = 0.0f, my = 0.0f;
    const int r = utCaravanGeoAgree(d, m, &mx, &my);
    if (r == kUtGeoUnusable) return;
    s_saidWorld = world;
    if (!derived)
        logI("view: caravan geometry - derived: none (this game's set has no uniq-geometry.txt); "
             "measured %.0f,%.0f (the Transfer page at %.0f,%.0f + its inventory at %.1f,%.1f / UI "
             "scale %.3f), the Transfer sack %d x %d cells, its widget %.0fx%.0f",
             mx, my, m.pageX, m.pageY, m.invX, m.invY, m.uiScale, m.gridW, m.gridH, ww, wh);
    else if (r == kUtGeoAgree)
        logI("view: caravan geometry - derived %d,%d %dx%d = %d x %d cells (the game's database); "
             "measured %.0f,%.0f (the Transfer page at %.0f,%.0f + its inventory at %.1f,%.1f / UI "
             "scale %.3f), the Transfer sack %d x %d cells, its widget %.0fx%.0f - they agree",
             d.x0, d.y0, d.w, d.h, d.cols, d.rows, mx, my, m.pageX, m.pageY, m.invX, m.invY, m.uiScale,
             m.gridW, m.gridH, ww, wh);
    else
        logW("view: caravan geometry - derived %d,%d %dx%d = %d x %d cells (the game's database); "
             "measured %.0f,%.0f (the Transfer page at %.0f,%.0f + its inventory at %.1f,%.1f / UI "
             "scale %.3f), the Transfer sack %d x %d cells, its widget %.0fx%.0f - they DIFFER; the "
             "measured place is the one in force (the grid, the pad and the layout editor are laid on "
             "the engine's Transfer sack, the database's grid place is not used)",
             d.x0, d.y0, d.w, d.h, d.cols, d.rows, mx, my, m.pageX, m.pageY, m.invX, m.invY, m.uiScale,
             m.gridW, m.gridH, ww, wh);
}

const char* viewStatus() { return g_status; }

}  // namespace ut
