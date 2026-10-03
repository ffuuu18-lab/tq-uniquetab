// ut_proto.cpp - the mod sack and the display prototypes (see ut_proto.h). GAME THREAD ONLY.
//
// Invariants:
//   * the only sack this file ever adds to, removes from or sizes is the MOD sack it built;
//   * every prototype it creates is either in the mod sack and listed in g_live, or destroyed
//     again before the function returns (a failed add never leaves an orphan behind);
//   * every engine call is inside its own SEH guard; a fault is reported to the view
//     (viewFault), which forces the view OFF and latches it for the process.

#include "ut_proto.h"

#include <windows.h>

#include <string.h>

#include <algorithm>
#include "hooks.h"
#include "ut_log.h"
#include "ut_owned.h"
#include "ut_ownedfold.h"
#include "ut_depositgate.h"   // utJournalStack
#include "ut_viewgate.h"
#include "ut_store.h"
#include "ut_view.h"

namespace ut {
namespace {

typedef UtProtoLive Live;   // the shift's (ut_protoshift.h) entry; item is the TqItem*
Live g_live[kUtProtoMax];
int g_liveCount = 0;
unsigned g_liveGen = 0;   // bumped whenever the prototype list changes

TqSack* g_sack = nullptr;
bool g_sackTried = false;
// every mod sack this process built stays RECOGNISED. An empty sack survives a
// world teardown (kept, its cell re-checked at the next use); one that still listed prototypes
// when the world took them is retired here (its map is stale) and a fresh one is built at the
// next ON. With the list full no new sack is built (refuse when unsure).
enum { kRetiredMax = 16 };
TqSack* g_retired[kRetiredMax];
int g_retiredCount = 0;
bool g_sackFull = false;
bool g_recheckCell = false;
unsigned g_keepId = 0;      // the prototype of a journalled take - never destroyed
bool g_sackStale = false;   // its map may still list a forgotten taken object: retire it
// protoShift ran the teardown's foreign sweep on this sack and did NOT build the
// window (not applied after the sweep, or aborted): the protoDestroyAll that follows in buildPage
// skips its own sweep. ONE sweep per teardown - a second one would read a foreign id whose
// Transfer return succeeded but whose RemoveItem failed as foreign again and return it twice.
bool g_shiftSwept = false;
// the taken ids a failed hand-out left in the CURRENT sack's map (they are on the
// cursor): the sweep forgets them - never destroyed, never auto-placed - until that sack retires.
UtForgotten g_forgot = {};
unsigned g_cellW = 0, g_cellH = 0;
volatile DWORD g_placingTid = 0;   // the thread inside the mod's own add/remove (0 none)

// `faulted` (may be null): set when the call faulted, told apart from a genuine id of 0.
unsigned objectId(const void* obj, bool* faulted = nullptr) {
    unsigned id = 0;
    if (faulted) *faulted = false;
    if (!obj || !g_tq.ObjectGetObjectId) return 0;
    utGuardEnter();
    __try {
        id = g_tq.ObjectGetObjectId(obj);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        id = 0;
        if (faulted) *faulted = true;
    }
    utGuardLeave();
    return id;
}

bool destroyObject(TqItem* item, int line) {
    bool ok = false;
    utGuardEnter();
    __try {
        void* om = g_tq.ObjectManagerGet();
        if (om) {
            g_tq.ObjectManagerDestroyObjectEx(om, item, "ut_proto.cpp", line);
            ok = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    utGuardLeave();
    return ok;
}

// `faulted` (may be null): set when CreateItem faulted, told apart from a genuine null item.
TqItem* createItem(const UtReplica& rep, bool* faulted = nullptr) {
    TqItem* item = nullptr;
    if (faulted) *faulted = false;
    utGuardEnter();
    __try {
        item = g_tq.ItemCreateItem(rep.bytes);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        item = nullptr;
        if (faulted) *faulted = true;
    }
    utGuardLeave();
    return item;
}

// the mod sack's LIVE cell (sack px = drawn px). The sack is an options listener (its ctor
// registers it): InventorySack::OnUIScaleChange (Game.dll 0x1B6220) sets its cell again with
// 0x198C40 = floorf(32 x Engine::GetUIScale + 0.5f) and rescales every item rect by new / old - so
// the cell cached at the build (g_cellW) went stale at any UI scale change (measured: 32
// cached, 44 live at 1.375; new prototypes were placed at col x 32 in a 44 px sack - overlapping).
bool liveCell(TqSack* s, unsigned* w, unsigned* h) {
    unsigned cw = 0, ch = 0;
    if (!s || !g_tq.SackGetCellWidth || !g_tq.SackGetCellHeight) return false;
    utGuardEnter();
    __try {
        cw = g_tq.SackGetCellWidth(s);
        ch = g_tq.SackGetCellHeight(s);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        cw = 0;
    }
    utGuardLeave();
    if (cw < 4 || cw > 256 || ch < 4 || ch > 256) return false;
    if (w) *w = cw;
    if (h) *h = ch;
    return true;
}

bool addToSack(TqSack* s, TqItem* item, unsigned id, int col, int row) {
    bool ok = false;
    TqVec2 at;
    at.x = (float)(col * (int)g_cellW);   // sack pixels: the load path multiplies the saved
    at.y = (float)(row * (int)g_cellH);   // cell offsets by the cell size
    utGuardEnter();
    g_placingTid = GetCurrentThreadId();   // the AddItem guard lets the mod's own through
    __try {
        // the last argument is the engine's "silent" flag: false makes AddItem call the item's
        // PlayDropSound (Item vtable +0x130), so every window rebuild would play every item's drop
        // sound; a prototype is placed silently. Real deposits and takes keep their own sounds.
        ok = g_tq.SackAddItemVec(s, &at, item, 1) && g_tq.SackContainsItem(s, id);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    g_placingTid = 0;
    utGuardLeave();
    return ok;
}

bool removeFromSack(TqSack* s, unsigned id) {
    bool ok = false;
    utGuardEnter();
    g_placingTid = GetCurrentThreadId();   // the mod's own removal (not a logged event)
    __try {
        ok = g_tq.SackRemoveItem(s, id);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    g_placingTid = 0;
    utGuardLeave();
    return ok;
}

// The mod sack, built once. The object registers itself with the options manager in its ctor,
// so its storage must outlive this DLL's image: the process heap, never freed.
bool buildSack() {
    if (!g_tq.SackCtor || !g_tq.SackVftable || !g_tq.SackSetDims || !g_tq.SackGetCellWidth ||
        !g_tq.SackGetCellHeight || !g_tq.SackGetGridWidth || !g_tq.SackGetGridHeight) {
        logW("view: the mod sack cannot be built - an InventorySack export is missing");
        return false;
    }
    TqGameEngine* ge = gameEngine();
    if (!ge || !g_tq.transferOff) return false;
    TqSack* real = (TqSack*)((unsigned char*)ge + g_tq.transferOff);
    void* mem = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 0x100);   // the object is 0x38
    if (!mem) return false;
    bool built = false;
    unsigned cw = 0, ch = 0, rcw = 0, rch = 0, gw = 0, gh = 0;
    utGuardEnter();
    __try {
        g_tq.SackCtor(mem);
        built = true;
        cw = g_tq.SackGetCellWidth((TqSack*)mem);
        ch = g_tq.SackGetCellHeight((TqSack*)mem);
        rcw = g_tq.SackGetCellWidth(real);
        rch = g_tq.SackGetCellHeight(real);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        cw = 0;
    }
    utGuardLeave();
    const void* vt = nullptr;
    safeRead(mem, &vt, sizeof(vt));
    if (!built || vt != g_tq.SackVftable) {
        logE("view: InventorySack ctor %s (vftable %p, expected %p) - the view stays unavailable",
             built ? "ran but the object does not look like a sack" : "FAULTED", vt,
             g_tq.SackVftable);
        return false;   // the block is left allocated: a half-built listener is never freed
    }
    if (cw < 8 || cw > 256 || ch < 8 || ch > 256 || cw != rcw || ch != rch) {
        logE("view: the mod sack's cell is %ux%u, the real Transfer sack's %ux%u - refused", cw, ch,
             rcw, rch);
        return false;
    }
    utGuardEnter();
    __try {
        g_tq.SackSetDims((TqSack*)mem, (int)(cw * 16), (int)(ch * 15), 0);
        gw = g_tq.SackGetGridWidth((TqSack*)mem);
        gh = g_tq.SackGetGridHeight((TqSack*)mem);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        gw = 0;
    }
    utGuardLeave();
    if (gw != 16 || gh != 15) {
        logE("view: SetDims gave the mod sack %ux%u cells, not 16x15 - refused", gw, gh);
        return false;
    }
    g_cellW = cw;
    g_cellH = ch;
    g_sack = (TqSack*)mem;
    logI("view: mod sack built at %p (16x15 cells of %ux%u px, the real Transfer sack's cell)",
         mem, cw, ch);
    return true;
}

}  // namespace

// Retire the current sack: it stays recognised (protoIsModSack), a fresh one is built later.
static void retireSack() {
    if (!g_sack) return;
    if (g_retiredCount >= kRetiredMax) {
        g_sackFull = true;   // keep g_sack as it is: still recognised, never built on again
        logW("view: %d mod sacks retired - no new one is built this session", g_retiredCount);
        return;
    }
    g_retired[g_retiredCount++] = g_sack;
    g_sack = nullptr;
    g_sackTried = false;
    utForgottenClear(g_forgot);   // its forgotten takes stay in the retired map, never swept again
}

// A kept (empty) sack after a world change: its cell must still be the real Transfer sack's.
static bool cellStillMatches(TqSack* s) {
    TqGameEngine* ge = gameEngine();
    if (!ge || !g_tq.transferOff) return false;
    TqSack* real = (TqSack*)((unsigned char*)ge + g_tq.transferOff);
    bool same = false;
    utGuardEnter();
    __try {
        same = g_tq.SackGetCellWidth(s) == g_tq.SackGetCellWidth(real) &&
               g_tq.SackGetCellHeight(s) == g_tq.SackGetCellHeight(real);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        same = false;
    }
    utGuardLeave();
    return same;
}

TqSack* protoSack() {
    if (g_sackFull) return nullptr;
    if (g_sackStale && g_sack) {
        g_sackStale = false;
        retireSack();
        if (g_sackFull) return nullptr;
    }
    if (g_sack && g_recheckCell) {
        g_recheckCell = false;
        if (!cellStillMatches(g_sack)) {
            logI("view: the kept mod sack's cell no longer matches the real Transfer sack - retired");
            retireSack();
            if (g_sackFull) return nullptr;
        }
    }
    if (g_sack) return g_sack;
    if (g_sackTried) return nullptr;
    g_sackTried = true;
    if (!buildSack()) viewFault("the mod sack could not be built");
    return g_sack;
}

namespace {
// One prototype for place `p` into the sack `s` (protoBuild's body, factored out so the
// row-scroll shift creates its entering ones with the SAME code). False = refused, and nothing is
// left behind: a created object that could not be placed is removed and destroyed again.
bool buildOne(TqSack* s, const UtProtoPlace& p, Live* out) {
    // a record with journal rows is built from its NEWEST row's identity (bright,
    // takeable); a record without is the bare prototype (dim). A row whose identity cannot be
    // built falls back to the bare prototype and is NOT takeable (seq 0): the take would hand
    // out something other than the row.
    static UtReplica rep;   // ~2 KB: not on the detour's stack
    static UtReplicaCapture row;
    unsigned long long seq = 0;
    char key[256];
    if (utJournalKey(p.record, key, sizeof(key)) && journalRows(key) > 0 &&
        journalNewest(key, &row, &seq) &&
        utReplicaFromIdentity(&rep, row)) {
        // built from the row
    } else {
        if (seq) {
            logW("view: the stored identity of %s could not be built - shown bare, not "
                 "takeable", p.record);
        }
        seq = 0;
        if (!utReplicaBuild(&rep, p.record)) return false;
    }
    TqItem* item = createItem(rep);
    const unsigned id = item ? objectId(item) : 0;
    if (!item || !id) {
        if (item) destroyObject(item, __LINE__);
        return false;
    }
    if (!addToSack(s, item, id, p.col, p.row)) {
        removeFromSack(s, id);   // harmless when the add never happened
        destroyObject(item, __LINE__);
        return false;
    }
    out->item = item;
    out->id = id;
    out->record = p.record;
    utShiftCopyPlace(*out, p);
    out->seq = seq;
    return true;
}

// the journal row a fresh build of `record` would start from (0 = bare) - buildOne's
// test without the copy. A row whose identity cannot be built reads as that row here, so the
// shift re-creates it (bare again, as the full build would) instead of keeping it: never wrong.
unsigned long long placeSeq(const char* record) {
    unsigned long long seq = 0;
    char key[256];
    if (utJournalKey(record, key, sizeof(key)) && journalRows(key) > 0 &&
        journalNewest(key, nullptr, &seq))
        return seq;
    return 0;
}

// The shift's engine calls, each inside its own SEH guard (the helpers above).
struct ShiftOps {
    TqSack* s;
    const UtProtoPlace* places;
    bool remove(unsigned id) { return removeFromSack(s, id); }
    bool add(void* item, unsigned id, int col, int row) {
        return addToSack(s, (TqItem*)item, id, col, row);
    }
    bool destroy(void* item) { return destroyObject((TqItem*)item, __LINE__); }
    bool create(int i, UtProtoLive* out) { return buildOne(s, places[i], out); }
};
}  // namespace

int protoBuild(const UtProtoPlace* places, int n, int* failed) {
    ++g_liveGen;
    int bad = 0;
    TqSack* s = protoSack();
    if (!s) {
        if (failed) *failed = n;
        return 0;
    }
    if (g_liveCount == 0) {   // an empty sack is placed on its LIVE cell (the UI scale's)
        unsigned lw = 0, lh = 0;
        if (liveCell(s, &lw, &lh) && (lw != g_cellW || lh != g_cellH)) {
            logI("view: the mod sack's cell is now %ux%u px (was %ux%u: the UI scale changed - "
                 "InventorySack::OnUIScaleChange); the prototypes are placed on it",
                 lw, lh, g_cellW, g_cellH);
            g_cellW = lw;
            g_cellH = lh;
        }
    }
    for (int i = 0; i < n && g_liveCount < kUtProtoMax; ++i) {
        if (!buildOne(s, places[i], &g_live[g_liveCount])) {
            ++bad;
            continue;
        }
        ++g_liveCount;
    }
    if (failed) *failed = bad;
    return g_liveCount;
}

int protoShift(const UtProtoPlace* places, int n, UtShiftCounts* out) {
    UtShiftCounts none = {};
    if (out) *out = none;
    // A journalled take (the keep slot) or a forgotten one keeps the proven teardown path: the
    // full rebuild forgets it exactly as decided. So does a sack that must retire.
    g_shiftSwept = false;
    // the refusal guards are the pure utShiftAllowed (ut_protoshift.h, tested)
    UtShiftFacts f = {};
    f.sack = g_sack != nullptr;
    f.sackFull = g_sackFull;
    f.sackStale = g_sackStale;
    f.recheckCell = g_recheckCell;
    f.keepId = g_keepId;
    f.forgotten = g_forgot.n;
    f.forgotOverflow = g_forgot.overflow;
    f.liveCount = g_liveCount;
    f.places = places ? n : 0;
    // (a tested fact): prototypes placed on another cell than the sack's
    // live one are rebuilt whole
    f.cellStale = protoPlacedCellStale();
    if (!utShiftAllowed(f)) return kUtShiftNotApplied;
    protoSweepForeign(false);   // the teardown's sweep, as protoDestroyAll runs it
    g_shiftSwept = true;        // until the window is built, the teardown's sweep ran
    if (g_sackStale || !g_sack) return kUtShiftNotApplied;
    static unsigned long long seq[kUtProtoMax];
    for (int i = 0; i < n; ++i) seq[i] = placeSeq(places[i].record);
    static Live scratch[kUtProtoMax];
    ShiftOps ops = {g_sack, places};
    UtShiftCounts c;
    const int r = utProtoShift(g_live, &g_liveCount, places, seq, n, ops, &c, &g_liveGen, scratch);
    if (r == kUtShiftDone) g_shiftSwept = false;   // built: the next teardown sweeps afresh
    if (out) *out = c;
    return r;
}

TqSack* protoSackBuilt() { return g_sack; }

int protoForgetAll() {
    protoSweepForeign(true);   // a foreign id is reported, never destroyed
    g_shiftSwept = false;
    const int n = g_liveCount;
    g_liveCount = 0;
    ++g_liveGen;
    if (n > 0) {
        retireSack();          // its map lists objects the world took: never built on again
    } else if (g_sack) {
        g_recheckCell = true;  // empty: kept across worlds (no leak, no new listener)
    }
    return n;
}

bool protoIsModSack(const void* s) {
    if (!s) return false;
    if (s == (const void*)g_sack) return true;
    for (int i = 0; i < g_retiredCount; ++i)
        if (s == (const void*)g_retired[i]) return true;
    return false;
}

int protoDestroyAll() {
    // foreign ids go back to the Transfer first - unless the shift that this teardown
    // follows has just swept the same sack (one sweep per teardown)
    const bool swept = g_shiftSwept;
    g_shiftSwept = false;
    if (!swept) protoSweepForeign(false);
    int n = 0;
    for (int i = g_liveCount - 1; i >= 0; --i) {
        const bool removed = g_sack && removeFromSack(g_sack, g_live[i].id);
        if (utProtoTeardownAction(g_live[i].id, g_keepId) == kUtProtoForget) {
            // the journal says this copy is the player's: forgotten, never destroyed.
            if (!removed) g_sackStale = true;
            logW("view: the prototype of a journalled take (id %u) is forgotten at the teardown, "
                 "never destroyed - it belongs to the cursor", g_live[i].id);
            continue;
        }
        if (destroyObject((TqItem*)g_live[i].item, __LINE__)) ++n;
    }
    g_liveCount = 0;
    ++g_liveGen;
    g_keepId = 0;
    if (g_sackStale && g_sack) {
        g_sackStale = false;
        retireSack();
    }
    return n;
}

void protoKeep(unsigned id) { g_keepId = id; }

TqItem* protoCreateLoose(const UtReplica& rep, unsigned* id, const char** fault) {
    bool faulted = false;
    if (fault) *fault = nullptr;
    if (id) *id = 0;
    TqItem* item = createItem(rep, &faulted);
    if (faulted) {
        if (fault) *fault = "the throw-away item's CreateItem";
        return nullptr;
    }
    if (item && id) {
        *id = objectId(item, &faulted);
        if (faulted && fault) *fault = "the throw-away item's GetObjectId";
    }
    return item;
}

bool protoDestroyLoose(TqItem* item) { return item && destroyObject(item, __LINE__); }

bool protoForgetTaken(unsigned id) {
    if (!id) return false;
    for (int i = 0; i < g_liveCount; ++i) {
        if (g_live[i].id != id) continue;
        for (int k = i + 1; k < g_liveCount; ++k) g_live[k - 1] = g_live[k];
        --g_liveCount;   // NOT destroyed: the cursor holds it and the journal says "taken"
        ++g_liveGen;
        g_sackStale = true;   // the removal failed: the sack's map may still list it
        // the id stays FORGOTTEN (off the keep slot, onto g_forgot) until the
        // sack is retired, so the teardown's sweep never reads it as foreign (a clone into the
        // real Transfer = a duplicate).
        utForgetTaken(g_forgot, g_keepId, id);
        return true;
    }
    return false;
}

bool protoIsId(unsigned id) {
    if (!id) return false;
    for (int i = 0; i < g_liveCount; ++i)
        if (g_live[i].id == id) return true;
    return false;
}

int protoCount() { return g_liveCount; }
unsigned protoGeneration() { return g_liveGen; }

bool protoAt(int i, const char** record, int* col, int* row, unsigned* id, int* w, int* h) {
    if (i < 0 || i >= g_liveCount) return false;
    if (w) *w = g_live[i].w;
    if (h) *h = g_live[i].h;
    if (record) *record = g_live[i].record;
    if (col) *col = g_live[i].col;
    if (row) *row = g_live[i].row;
    if (id) *id = g_live[i].id;
    return true;
}

bool protoSlotAt(int i, int* col, int* row, int* w, int* h) {
    if (i < 0 || i >= g_liveCount) return false;
    if (col) *col = g_live[i].slotCol;
    if (row) *row = g_live[i].slotRow;
    if (w) *w = g_live[i].slotW;
    if (h) *h = g_live[i].slotH;
    return true;
}

bool protoLookup(unsigned id, const char** record, unsigned long long* seq) {
    if (!id) return false;
    for (int i = 0; i < g_liveCount; ++i) {
        if (g_live[i].id != id) continue;
        if (record) *record = g_live[i].record;
        if (seq) *seq = g_live[i].seq;
        return true;
    }
    return false;
}

bool protoAtSeq(int i, unsigned long long* seq) {
    if (i < 0 || i >= g_liveCount) return false;
    if (seq) *seq = g_live[i].seq;
    return true;
}

bool protoHandOut(unsigned id) {
    if (!id || !g_sack) return false;
    for (int i = 0; i < g_liveCount; ++i) {
        if (g_live[i].id != id) continue;
        if (!removeFromSack(g_sack, id)) return false;   // the MOD sack, never a real one
        for (int k = i + 1; k < g_liveCount; ++k) g_live[k - 1] = g_live[k];
        --g_liveCount;   // forgotten, NOT destroyed: the object is the player's item now
        ++g_liveGen;
        if (g_keepId == id) g_keepId = 0;
        return true;
    }
    return false;
}

namespace {
void freeEngineVector(TqPtrVector* v) {
    if (v->first && g_tq.CrtOperatorDelete) {
        __try {
            g_tq.CrtOperatorDelete((void*)v->first);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    v->first = v->last = v->end = nullptr;
}

bool isItemObject(const void* obj) {
    const void* vt = nullptr;
    const void* fn = nullptr;
    if (!obj || g_tq.itemSlotReplica < 0 || !g_tq.ItemGetItemReplicaInfo) return false;
    return safeRead(obj, &vt, sizeof(vt)) && vt &&
           safeRead((const unsigned char*)vt + g_tq.itemSlotReplica, &fn, sizeof(fn)) &&
           fn == (const void*)g_tq.ItemGetItemReplicaInfo;
}

// Object::GetObjectName's text, folded (the record path the object was built from).
void objectNameFolded(const void* obj, char* out, unsigned cap) {
    out[0] = 0;
    const char* objName = nullptr;
    if (!g_tq.ObjectGetObjectName) return;
    utGuardEnter();
    __try {
        objName = g_tq.ObjectGetObjectName(obj);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        objName = nullptr;
    }
    utGuardLeave();
    if (!objName) return;
    char raw[300];
    size_t i = 0;
    for (; i + 1 < sizeof(raw); ++i) {
        if (!safeRead(objName + i, &raw[i], 1)) break;
        if (!raw[i]) break;
    }
    raw[i] = 0;
    utOwnedNormaliseKey(raw, out, cap);
}
}  // namespace

void protoObjectRecord(const void* obj, char* out, unsigned cap) {
    if (!out || !cap) return;
    out[0] = 0;
    if (obj) objectNameFolded(obj, out, cap);
}

bool protoFindObjects(unsigned itemId, unsigned ctrlId, const void** item, const void** ctrl) {
    if (item) *item = nullptr;
    if (ctrl) *ctrl = nullptr;
    if (!g_tq.ObjectManagerGet || !g_tq.ObjectManagerGetObjectList || !g_tq.ObjectGetObjectId)
        return false;
    TqPtrVector v = {nullptr, nullptr, nullptr};
    bool listed = false;
    utGuardEnter();
    __try {
        void* om = g_tq.ObjectManagerGet();
        if (om) {
            g_tq.ObjectManagerGetObjectList(om, &v);
            listed = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        listed = false;
    }
    utGuardLeave();
    if (!listed) {
        freeEngineVector(&v);
        return false;
    }
    const size_t n = v.first && v.last >= v.first ? (size_t)(v.last - v.first) : 0;
    for (size_t k = 0; k < n && n < 4000000; ++k) {
        const void* obj = nullptr;
        if (!safeRead(v.first + k, &obj, sizeof(obj)) || !obj) continue;
        const unsigned id = objectId(obj);
        if (!id) continue;
        if (item && id == itemId && isItemObject(obj)) *item = obj;
        if (ctrl && ctrlId && id == ctrlId) {
            const void* vt = nullptr;
            if (g_tq.ControllerPlayerVftable && safeRead(obj, &vt, sizeof(vt)) &&
                vt == g_tq.ControllerPlayerVftable)
                *ctrl = obj;
        }
    }
    utGuardEnter();
    freeEngineVector(&v);
    utGuardLeave();
    return true;
}

bool protoFindItems(const unsigned* ids, int n, const void** objs) {
    if (!ids || !objs || n < 0) return false;
    for (int i = 0; i < n; ++i) objs[i] = nullptr;
    // without the Item test nothing can be compared - that is "not read", never
    // "none there"
    if (!g_tq.ObjectManagerGet || !g_tq.ObjectManagerGetObjectList || !g_tq.ObjectGetObjectId ||
        g_tq.itemSlotReplica < 0 || !g_tq.ItemGetItemReplicaInfo)
        return false;
    TqPtrVector v = {nullptr, nullptr, nullptr};
    bool listed = false;
    utGuardEnter();
    __try {
        void* om = g_tq.ObjectManagerGet();
        if (om) {
            g_tq.ObjectManagerGetObjectList(om, &v);
            listed = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        listed = false;
    }
    utGuardLeave();
    if (listed) {
        const size_t cnt = v.first && v.last >= v.first ? (size_t)(v.last - v.first) : 0;
        if (cnt >= 4000000) listed = false;   // over the bound = nothing compared
        for (size_t k = 0; listed && k < cnt; ++k) {
            const void* obj = nullptr;
            if (!safeRead(v.first + k, &obj, sizeof(obj)) || !obj) continue;
            const unsigned id = objectId(obj);
            if (!id) continue;
            const unsigned* at = std::lower_bound(ids, ids + n, id);
            if (at != ids + n && *at == id && isItemObject(obj)) objs[at - ids] = obj;
        }
    }
    utGuardEnter();
    freeEngineVector(&v);
    utGuardLeave();
    return listed;
}

// Item::GetNumberInStack is virtual (vftable slot `item.stackSlot`,
// +0x1B8). The export is the BASE body (`xor eax,eax / ret`, Game.dll 0x8800), so calling it
// directly answers 0 for a potion stack too: the count is read through the object's own vftable,
// the way the engine does (CreateAndStackIds 0x152858 `call [edx+0x1B8]`). The object passed the
// Item test (isItemObject) before it gets here. A fault answers 0xFFFFFFFF (a stack: refused).
// with the ADVISORY slot not decoded the count is NOT readable - the export is the
// folded `33 C0 C3` stub (Game.dll 0x8800, shared by 12 exports), so it is not called: the answer is
// 0 (a unique counts as one item), said ONCE, and the deposit lines print the count as "?".
bool protoStackReadable() { return g_tq.itemSlotStack >= 0; }

unsigned protoStackCount(const void* item) {
    if (!item) return 0xFFFFFFFFu;
    if (!protoStackReadable()) {
        static bool said = false;
        if (!said) {
            said = true;
            logI("view: the stack count is not readable (item.stackSlot not decoded) - a unique counts "
                 "as one item; the deposit lines print the count as \"?\"");
        }
        return 0;
    }
    unsigned n = 0xFFFFFFFFu;
    utGuardEnter();
    __try {
        const unsigned char* vt = *(const unsigned char* const*)item;
        const PfnItem_GetNumberInStack fn =
            *(const PfnItem_GetNumberInStack*)(vt + g_tq.itemSlotStack);
        if (fn) n = fn((const TqItem*)item);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        n = 0xFFFFFFFFu;
    }
    utGuardLeave();
    return n;
}

bool protoCapture(const void* item, UtReplicaCapture* out, char* name, unsigned nameCap,
                  unsigned* stack) {
    if (name && nameCap) name[0] = 0;
    if (!item || !out || !isItemObject(item)) return false;
    memset(out, 0, sizeof(*out));
    // The measured buffer: the 0xBC replica, seven EMPTY strings (the engine assigns into
    // them), a canary to 0x100 that proves the engine wrote nothing past this build's layout.
    const unsigned kBuf = 0x100;
    const unsigned char kCanary = 0xA5;
    unsigned char rep[kBuf];
    memset(rep, 0, kUtReplicaSize);
    memset(rep + kUtReplicaSize, kCanary, kBuf - kUtReplicaSize);
    for (int k = 0; k < 7; ++k) {
        const unsigned cap = 15;
        memcpy(rep + kUtReplicaStr[k] + 0x14, &cap, 4);   // SSO empty: size 0, capacity 15
    }
    bool called = false;
    unsigned n = 0;
    utGuardEnter();
    __try {
        g_tq.ItemGetItemReplicaInfo((const TqItem*)item, rep);
        called = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    utGuardLeave();
    if (called) n = protoStackCount(item);   // the object's own slot; 0 = a single item
    if (!called) {
        logW("deposit: GetItemReplicaInfo FAULTED - the replica's strings are leaked, not freed");
        return false;
    }
    for (unsigned k = kUtReplicaSize; k < kBuf; ++k) {
        if (rep[k] != kCanary) {
            logW("deposit: GetItemReplicaInfo wrote past 0x%X (+0x%X) - the capture is refused and "
                 "its strings are leaked", kUtReplicaSize, k);
            return false;
        }
    }
    bool ok = true;
    for (int k = 0; k < 7; ++k) {
        const unsigned char* at = rep + kUtReplicaStr[k];
        unsigned len = 0;
        memcpy(&len, at + 0x10, 4);
        if (len >= kUtIdStrMax || !safeStdString(at, out->str[k], kUtIdStrMax) ||
            strlen(out->str[k]) != len)
            ok = false;
        // safeStdString shows a non-printable byte as '?'; the journal must store
        // the bytes EXACTLY, so any byte outside 0x20..0x7E in the raw string refuses the capture.
        if (ok) {
            unsigned res = 0;
            memcpy(&res, at + 0x14, 4);
            const unsigned char* raw = at;
            if (res >= 16u) memcpy(&raw, at, sizeof(raw));
            bool printable = raw != nullptr;
            utGuardEnter();
            __try {
                for (unsigned i = 0; printable && i < len; ++i)
                    if (raw[i] < 0x20 || raw[i] > 0x7E) printable = false;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                printable = false;
            }
            utGuardLeave();
            if (!printable) ok = false;
        }
    }
    memcpy(&out->seed, rep + kUtReplicaSeed, 4);
    memcpy(&out->var1, rep + kUtReplicaVar1, 4);
    memcpy(&out->var2, rep + kUtReplicaVar2, 4);
    out->b8 = rep[kUtReplicaB8];
    out->stack = utJournalStack(n);   // the row says 1 for a single copy
    if (stack) *stack = n;             // the raw count: the caller's gate decides
    // The engine's heap strings (capacity >= 16) belong to MSVCR110's heap: freed there, once.
    for (int k = 0; k < 7; ++k) {
        const unsigned char* at = rep + kUtReplicaStr[k];
        unsigned res = 0;
        void* heap = nullptr;
        memcpy(&res, at + 0x14, 4);
        memcpy(&heap, at, sizeof(heap));
        if (res >= 16u && heap && g_tq.CrtOperatorDelete) {
            utGuardEnter();
            __try {
                g_tq.CrtOperatorDelete(heap);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
            utGuardLeave();
        }
    }
    if (name && nameCap) objectNameFolded(item, name, nameCap);
    return ok && utJournalKey(out->str[kUtIdBase], out->record, sizeof(out->record));
}

// -------------------------------------------------------------------------------------------
bool protoPlacing() {
    const DWORD t = g_placingTid;
    return t != 0 && t == GetCurrentThreadId();
}

int protoSlotIndexAtCell(int col, int row) {
    UtSlotRect r[kUtProtoMax];
    for (int i = 0; i < g_liveCount; ++i) {
        r[i].col = g_live[i].slotCol;
        r[i].row = g_live[i].slotRow;
        r[i].w = g_live[i].slotW;
        r[i].h = g_live[i].slotH;
    }
    return utSlotIndexAt(r, g_liveCount, col, row);
}

unsigned protoIdAtSlotCell(int col, int row) {
    const int i = protoSlotIndexAtCell(col, row);
    return i >= 0 ? g_live[i].id : 0u;
}

int protoSweepForeign(bool worldGone) {
    if (!g_sack) return 0;
    enum { kCap = 512 };
    static unsigned keys[kCap];
    static unsigned protos[kUtProtoMax];
    const int n = ownedSackKeys(g_sack, keys, kCap);
    if (n < 0) {
        g_sackStale = true;   // cannot prove it holds only prototypes: retired, never destroyed
        logW("view: the collection sack's map could not be walked before its teardown - the sack "
             "is retired (never destroyed), only the prototypes the mod listed are touched");
        return 0;
    }
    for (int i = 0; i < g_liveCount; ++i) protos[i] = g_live[i].id;
    int foreign = 0;
    for (int k = 0; k < n; ++k) {
        if (utTeardownAction(keys[k], protos, g_liveCount, g_keepId, &g_forgot) != kUtTearReturn) continue;
        ++foreign;
        char rec[200] = "record unknown";
        if (!worldGone) {
            const void* obj = nullptr;
            const void* ctrl = nullptr;
            if (protoFindObjects(keys[k], 0, &obj, &ctrl) && obj) protoObjectRecord(obj, rec, sizeof(rec));
        }
        if (!worldGone && hookTransferAutoPlace(keys[k])) {
            if (!removeFromSack(g_sack, keys[k])) g_sackStale = true;
            logE("view: a foreign item (id %u, %s) was found in the collection sack - returned to "
                 "the Transfer", keys[k], rec);
        } else {
            g_sackStale = true;
            logE("view: a foreign item (id %u, %s) was found in the collection sack and could NOT be "
                 "returned to the Transfer (%s) - it stays in the collection sack, which is retired "
                 "and never destroyed",
                 keys[k], rec, worldGone ? "the world is gone" : "the Transfer refused it");
        }
    }
    return foreign;
}

const void* protoItem(int i) { return (i >= 0 && i < g_liveCount) ? g_live[i].item : nullptr; }

// the LIVE cell of the mod sack (drawn px), never the one cached at the build.
bool protoCellSize(unsigned* w, unsigned* h) {
    if (!g_sack) return false;
    return liveCell(g_sack, w, h);
}

bool protoPlacedCell(unsigned* w, unsigned* h) {
    if (!g_sack || !g_cellW || !g_cellH) return false;
    if (w) *w = g_cellW;
    if (h) *h = g_cellH;
    return true;
}

bool protoPlacedCellStale() {
    if (!g_sack || g_liveCount <= 0) return false;
    unsigned lw = 0, lh = 0;
    if (!liveCell(g_sack, &lw, &lh)) return false;   // unreadable: no evidence of a change
    return lw != g_cellW || lh != g_cellH;
}

}  // namespace ut
