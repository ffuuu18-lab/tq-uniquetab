// ut_owned.cpp - the owned marking (see ut_owned.h). GAME THREAD ONLY, except ownedMarkDirty.
//
// Invariants:
//   * engine memory is only READ, every read inside an SEH guard; the one buffer the engine
//     allocates for the mod (GetObjectList's vector) is freed through MSVCR110 operator delete;
//   * no allocation in the per-frame functions (ownedMarksBegin / ownedMarkRect / ownedLabel);
//   * refuse when unsure: a store that cannot be walked, a self-check that fails or a full table
//     makes the set UNKNOWN - no mark is drawn and the label says "?".

#include "ut_owned.h"

#include <windows.h>

#include <stdio.h>
#include <string.h>

#include <algorithm>

#include "hooks.h"   // the cost meter (probeScan)
#include "tq_runtime.h"
#include "ut_config.h"
#include "ut_gridcheck.h"
#include "ut_live.h"
#include "ut_log.h"
#include "ut_ownedfold.h"
#include "ut_padlayout.h"
#include "ut_panel.h"
#include "ut_proto.h"
#include "ut_search.h"   // utSackMapWalk
#include "ut_store.h"
#include "ut_viewgate.h"   // utBackgroundSkip

namespace ut {
namespace {

const int kIdCap = 8192;       // item ids in the stores + the inventory
const int kOwnedCap = 8192;    // distinct owned records (hashes)
const int kKeyCap = 300;       // a folded record path
const int kGridCols = kUtGridCols, kGridRows = kUtGridRows;

unsigned g_ids[kIdCap];         // the store ids (Transfer, Stash, Relic Vault), sorted
int g_idCount = 0;
// the character's inventory ids are kept apart and join the owned set only when
// every one of them resolves in the object walk to a record path ("records/..."): a wrong base
// under GetInventoryItems would read some other vector, and its ids must not mark anything.
unsigned g_invIds[kIdCap];
int g_invCount = 0;
unsigned long long g_invHash[kOwnedCap];
int g_invHashCount = 0;
bool g_invSaid = false;
unsigned long long g_owned[kOwnedCap];
int g_ownedCount = 0;
bool g_known = false;          // the last refresh succeeded and both self-checks held
bool g_mapOk = false, g_namesOk = false;
// a self-check PROVEN on a non-empty page of this world; an empty page (the OWN filter)
// keeps it (the names check needs prototypes to name; the map walk of an empty sack must be 0).
bool g_mapProven = false, g_namesProven = false;
bool g_namesSaid = false;
volatile LONG g_dirty = 0;
LONG64 g_ticks = 0, g_lastRefresh = -1000;

// The shown page (index = prototype index) and group.
char g_pageOwned[kUtProtoMax];   // COLLECTED - the journal holds rows of its record
char g_pageHave[kUtProtoMax];    // the store set: the player has one somewhere
int g_pageN = 0;
int g_groupOwned = 0, g_groupTotal = 0;
// the whole catalogue's count: recounted when the journal set changes (known or not, its file,
// its row total) or on a refresh, never per frame
int g_allOwned = 0, g_allTotal = 0;
bool g_allKnown = false, g_allSeen = false;
unsigned g_allRows = 0;
char g_allLeaf[272] = "";

// The grid origin (engine UI coordinates = canvas px, see the x check).
int g_gridVerdict = 0;         // 0 undecided, 1 accepted, -1 refused (for this geometry)
float g_gridX = 0.0f, g_gridY = 0.0f;
unsigned g_cw = 0, g_ch = 0;
// the pure check (ut_gridcheck.h) holds the candidate origin and the cursor counters.
UtGridState g_grid = {};
bool g_gridWaitSaid = false;
bool g_gridWaitLongSaid = false;   // the INFO line after kUtGridWaitSay waits
int g_geoW = 0, g_geoH = 0;
float g_geoScale = 0.0f;
bool g_marksNow = false;       // ownedMarksBegin's answer for this frame
int g_unveil = -1;             // the prototype whose slot the cursor is on (not veiled)
// the marks are drawn from the Present detour, AFTER the engine's tooltip, so they
// are hidden while a tooltip is likely open: the cursor is on the measured grid and either on a
// cell a prototype covers (g_occ) or the last proven hover returned an item (g_hoverId).
char g_occ[kGridRows][kGridCols];
volatile LONG g_hoverId = 0;

char g_status[160] = "owned=unknown";

// A VS2012 x86 std::map node (the `msvc.map` row): left, parent, right, color, isnil, then the
// value (pair<const unsigned, RectExt>: the key first).
// In-order walk of the id map, no allocation; inside the caller's __try. -1 = it does not read as
// a VS2012 map (utSackMapWalk's refusals) or the ids do not fit in `out`.
int walkMapKeys(const void* map, unsigned* out, int* n, int cap) {
    return utSackMapWalk(map, kIdCap, [&](const UtSackMapNode& nd) {
        if (*n >= cap) return false;
        out[(*n)++] = nd.key;
        return true;
    });
}

int sackKeys(const TqSack* s, unsigned* out, int* n, int cap) {
    if (!s || !g_tq.SackGetInventory) return -1;
    int r = -1;
    utGuardEnter();
    __try {
        const void* map = g_tq.SackGetInventory(s);
        r = map ? walkMapKeys(map, out, n, cap) : -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        r = -1;
    }
    utGuardLeave();
    return r;
}

// The character's inventory ids (advisory: -1 = not available, the stores still count).
int inventoryIds() {
    if (!g_tq.CharGetInventoryItems || !g_tq.GameGetMainPlayer) return -1;
    TqGameEngine* ge = gameEngine();
    if (!ge) return -1;
    int added = -1;
    utGuardEnter();
    __try {
        TqPlayer* p = g_tq.GameGetMainPlayer(ge);
        const TqU32Vector* v = p ? g_tq.CharGetInventoryItems(p) : nullptr;
        if (v && v->last >= v->first && v->end >= v->last && (v->last - v->first) <= 4096 &&
            (v->first || v->last == v->first)) {
            added = 0;
            for (const unsigned* q = v->first; q < v->last && g_invCount < kIdCap; ++q) {
                if (!*q) continue;
                g_invIds[g_invCount++] = *q;
                ++added;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        added = -1;
    }
    utGuardLeave();
    return added;
}

bool sortedHas(const unsigned* a, int n, unsigned v) { return std::binary_search(a, a + n, v); }

// the primary mark is the journal's ROW COUNT (GD's meaning: collected = bright).
bool collected(const char* record) { return record && storeCount(record) > 0; }

bool ownsKey(const char* record) {
    char key[kKeyCap];
    if (!utOwnedNormaliseKey(record, key, sizeof(key))) return false;
    return std::binary_search(g_owned, g_owned + g_ownedCount, utOwnedRowHash(key, 1));
}

struct WalkOut {
    int objects = 0, named = 0, protoSeen = 0, protoOk = 0;
    int invRecords = 0;   // inventory ids whose object's name folds to "records/..."
    char bad[96] = {0};
};

// The object walk: every object whose id is wanted gets named, folded and hashed; every prototype
// is named too, for the name self-check. Inside one guard; `v` is freed by the caller.
bool walkObjects(TqPtrVector* v, const unsigned* protoIds, const int* protoIdx, int protoN,
                 WalkOut* o) {
    bool ok = false;
    utGuardEnter();
    __try {
        void* om = g_tq.ObjectManagerGet();
        if (om) {
            g_tq.ObjectManagerGetObjectList(om, v);
            if (v->last >= v->first && v->end >= v->last && (v->last - v->first) <= 4000000) {
                const size_t n = (size_t)(v->last - v->first);
                o->objects = (int)n;
                for (size_t i = 0; i < n; ++i) {
                    const void* obj = v->first[i];
                    if (!obj) continue;
                    const unsigned id = g_tq.ObjectGetObjectId(obj);
                    if (!id) continue;
                    const bool inv = sortedHas(g_invIds, g_invCount, id);
                    const bool want = inv || sortedHas(g_ids, g_idCount, id);
                    const unsigned* pp = std::lower_bound(protoIds, protoIds + protoN, id);
                    const bool proto = pp != protoIds + protoN && *pp == id;
                    if (!want && !proto) continue;
                    char key[kKeyCap];
                    if (!utOwnedNormaliseKey(g_tq.ObjectGetObjectName(obj), key, sizeof(key)))
                        continue;
                    if (proto) {
                        ++o->protoSeen;
                        const char* rec = nullptr;
                        char recKey[kKeyCap];
                        if (protoAt(protoIdx[pp - protoIds], &rec, nullptr, nullptr, nullptr) &&
                            utOwnedNormaliseKey(rec, recKey, sizeof(recKey)) &&
                            strcmp(recKey, key) == 0) {
                            ++o->protoOk;
                        } else if (!o->bad[0]) {
                            _snprintf_s(o->bad, sizeof(o->bad), _TRUNCATE, "%s", key);
                        }
                    }
                    if (inv) {   // held apart until the inventory self-check in refresh() has passed
                        if (strncmp(key, "records/", 8) == 0 && g_invHashCount < kOwnedCap) {
                            g_invHash[g_invHashCount++] = utOwnedRowHash(key, 1);
                            ++o->invRecords;
                        }
                    } else if (want && g_ownedCount < kOwnedCap) {
                        g_owned[g_ownedCount++] = utOwnedRowHash(key, 1);
                        ++o->named;
                    }
                }
                ok = true;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    utGuardLeave();
    return ok;
}

void freeEngineVector(TqPtrVector* v) {
    if (!v->first || !g_tq.CrtOperatorDelete) return;
    utGuardEnter();
    __try {
        g_tq.CrtOperatorDelete((void*)v->first);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    utGuardLeave();
    v->first = v->last = v->end = nullptr;
}

// Self-check 1: the map walk returns exactly the mod sack's own prototype ids.
bool checkMapOnModSack() {
    static unsigned keys[kUtProtoMax + 1];
    int n = 0;
    const int pc = protoCount();
    const int r = sackKeys(protoSackBuilt(), keys, &n, kUtProtoMax + 1);
    // an EMPTY page (the OWN filter, a group with nothing owned) proves nothing; it keeps
    // the verdict of a non-empty page of this world if its walk returns no key at all.
    bool ok = pc > 0 ? r == pc : (g_mapProven && r == 0);
    for (int i = 0; ok && i < n; ++i) ok = protoIsId(keys[i]);
    if (ok && pc > 0) g_mapProven = true;
    static int s_last = -1;   // logged on the first result and on every change only
    if ((ok ? 1 : 0) != s_last) {
        s_last = ok ? 1 : 0;
        if (ok) {
            logI("owned: the id-map walk reads the mod sack's %d prototypes exactly", pc);
        } else {
            logW("owned: the id-map walk does NOT read the mod sack right (%d keys for %d "
                 "prototypes) - owned marks off", r, pc);
        }
    }
    return ok;
}

void computePage() {
    g_pageN = protoCount();
    memset(g_occ, 0, sizeof(g_occ));
    for (int i = 0; i < g_pageN && i < kUtProtoMax; ++i) {
        const char* rec = nullptr;
        const bool have = protoAt(i, &rec, nullptr, nullptr, nullptr);
        g_pageOwned[i] = (char)(have && journalSetKnown() && collected(rec));
        g_pageHave[i] = (char)(have && g_known && ownsKey(rec));
        int col = 0, row = 0, fw = 0, fh = 0;   // the cells a prototype's SLOT covers
        if (protoSlotAt(i, &col, &row, &fw, &fh)) {
            for (int r = row; r < row + fh; ++r)
                for (int k = col; k < col + fw; ++k)
                    if (r >= 0 && r < kGridRows && k >= 0 && k < kGridCols) g_occ[r][k] = 1;
        }
    }
    const int g = liveShownGroup();
    g_groupTotal = liveGroupEntries(g) > 0 ? liveGroupEntries(g) : 0;
    g_groupOwned = 0;
    if (journalSetKnown()) {
        for (int k = 0; k < g_groupTotal; ++k)
            if (collected(liveGroupRecord(g, k))) ++g_groupOwned;
    }
}

// The whole catalogue's collected count (the `all` of the label). `force` = a refresh; otherwise
// only when the journal set is not the one it was counted on.
void computeAll(bool force) {
    const bool known = journalSetKnown();
    const unsigned rows = known ? journalCollectedTotal() : 0u;
    const char* leaf = known ? journalSetLeaf() : "";
    if (!leaf) leaf = "";
    if (!force && g_allSeen && known == g_allKnown && rows == g_allRows && !strcmp(leaf, g_allLeaf))
        return;
    g_allSeen = true;
    g_allKnown = known;
    g_allRows = rows;
    _snprintf_s(g_allLeaf, sizeof(g_allLeaf), _TRUNCATE, "%s", leaf);
    int total = 0, owned = 0;
    const int groups = liveGroupCount();
    for (int g = 0; g < groups; ++g) {
        const int n = liveGroupEntries(g);
        for (int k = 0; k < n; ++k) {
            ++total;
            if (known && collected(liveGroupRecord(g, k))) ++owned;
        }
    }
    g_allTotal = total;
    g_allOwned = owned;
}

void refresh() {
    g_lastRefresh = g_ticks;
    InterlockedExchange(&g_dirty, 0);
    g_known = false;
    g_idCount = 0;
    g_ownedCount = 0;
    g_invCount = 0;
    g_invHashCount = 0;
    g_mapOk = checkMapOnModSack();
    TqGameEngine* ge = gameEngine();
    if (!g_mapOk || !ge || !g_tq.ObjectManagerGet || !g_tq.ObjectManagerGetObjectList ||
        !g_tq.ObjectGetObjectId || !g_tq.ObjectGetObjectName || !g_tq.CrtOperatorDelete) {
        _snprintf_s(g_status, sizeof(g_status), _TRUNCATE, "owned=unknown (%s)",
                    g_mapOk ? "an export is missing" : "map walk");
        return;
    }
    const unsigned offs[3] = {g_tq.stashOff, g_tq.transferOff, g_tq.relicOff};
    int per[3] = {-1, -1, -1};
    for (int k = 0; k < 3; ++k) {
        per[k] = offs[k] ? sackKeys((const TqSack*)((unsigned char*)ge + offs[k]), g_ids,
                                    &g_idCount, kIdCap)
                         : -1;
        if (per[k] < 0) {
            logW("owned: store %d (0 stash, 1 transfer, 2 vault) could not be walked - owned "
                 "unknown", k);
            _snprintf_s(g_status, sizeof(g_status), _TRUNCATE, "owned=unknown (store %d)", k);
            return;
        }
    }
    int inv = inventoryIds();
    std::sort(g_ids, g_ids + g_idCount);
    g_idCount = (int)(std::unique(g_ids, g_ids + g_idCount) - g_ids);
    std::sort(g_invIds, g_invIds + g_invCount);
    g_invCount = (int)(std::unique(g_invIds, g_invIds + g_invCount) - g_invIds);
    if (inv > 0) inv = g_invCount;

    // the prototypes, sorted by id, for the name self-check
    static unsigned protoIds[kUtProtoMax];
    static int protoIdx[kUtProtoMax];
    int pn = 0;
    {
        static unsigned long long pairs[kUtProtoMax];
        const int pc = protoCount();
        for (int i = 0; i < pc && pn < kUtProtoMax; ++i) {
            unsigned id = 0;
            if (protoAt(i, nullptr, nullptr, nullptr, &id) && id)
                pairs[pn++] = ((unsigned long long)id << 32) | (unsigned)i;
        }
        std::sort(pairs, pairs + pn);
        for (int i = 0; i < pn; ++i) {
            protoIds[i] = (unsigned)(pairs[i] >> 32);
            protoIdx[i] = (int)(pairs[i] & 0xFFFFFFFFu);
        }
    }
    TqPtrVector v = {nullptr, nullptr, nullptr};
    WalkOut o;
    const bool walked = walkObjects(&v, protoIds, protoIdx, pn, &o);
    freeEngineVector(&v);
    // The inventory counts only when EVERY one of its ids resolved to a record path (and so at
    // least one, since it is not empty); else it is "not available" and the stores alone count.
    if (inv > 0 && walked && o.invRecords == g_invCount) {
        for (int i = 0; i < g_invHashCount && g_ownedCount < kOwnedCap; ++i)
            g_owned[g_ownedCount++] = g_invHash[i];
    } else if (inv > 0) {
        if (!g_invSaid) {
            g_invSaid = true;
            logW("owned: the inventory self-check failed (%d of %d ids resolved to a record path) "
                 "- the inventory is not counted, the stores still are", o.invRecords, g_invCount);
        }
        inv = -1;
    }
    std::sort(g_owned, g_owned + g_ownedCount);
    g_ownedCount = (int)(std::unique(g_owned, g_owned + g_ownedCount) - g_owned);
    g_namesOk = walked && o.protoSeen == pn && pn > 0 && o.protoOk == o.protoSeen;
    if (g_namesOk) g_namesProven = true;
    if (walked && pn == 0 && g_namesProven) g_namesOk = true;   // an empty page
    if (!g_namesOk && !g_namesSaid) {
        g_namesSaid = true;
        logW("owned: the name check failed (walk %d, %d of %d prototypes found, %d named as their "
             "record; first other name \"%s\") - owned marks off",
             walked ? 1 : 0, o.protoSeen, pn, o.protoOk, o.bad);
    }
    g_known = walked && g_namesOk && g_idCount < kIdCap && g_ownedCount < kOwnedCap;
    logI("owned: %s - ids stash %d, transfer %d, vault %d, inventory %d%s; %d objects walked, %d "
         "named, %d distinct records; names checked on %d prototypes",
         g_known ? "known" : "UNKNOWN", per[0], per[1], per[2], inv,
         inv < 0 ? " (not available)" : "", o.objects, o.named, g_ownedCount, o.protoOk);
    _snprintf_s(g_status, sizeof(g_status), _TRUNCATE, "owned=%s ids=%d inv=%d records=%d grid=%d",
                g_known ? "known" : "unknown", g_idCount, inv, g_ownedCount, g_gridVerdict);
}

// Every refresh ends here: the page's flags, then the OWN filter's (ut_live), which asks for a
// rebuild when the filtered page changes.
void refreshAll() {
    const long long t0 = probeNow();
    refresh();
    probeScan(probeNow() - t0);   // the owned scan (the object-list walk) alone
    computePage();
    computeAll(true);
    liveOwnedChanged();
}

}  // namespace

void ownedOnViewOn() {
    refreshAll();
    if (journalSetKnown()) {
        logI("owned: group %s - %d / %d collected (journal rows), %u row(s) in the set", liveGroupLabel(liveShownGroup()),
             g_groupOwned, g_groupTotal, journalCollectedTotal());
    }
}

void ownedPageBuilt() {
    const bool was = g_known;
    if (g_known && !checkMapOnModSack()) g_known = false;
    // a full rebuild destroyed the old page's prototypes; after a row scroll the hovered one
    // may be the same (moved) object - cleared anyway, the next hover sets it again
    InterlockedExchange(&g_hoverId, 0);
    computePage();
    computeAll(false);
    if (was && !g_known) liveOwnedChanged();   // the OWN filter falls back to all
    // the OWN filter waits for a KNOWN set - prove it again on this (non-empty) page.
    if (!g_known && liveOwnedOnly() && g_pageN > 0) InterlockedExchange(&g_dirty, 1);
}

void ownedMarkDirty() { InterlockedExchange(&g_dirty, 1); }

void ownedTick(bool on) {
    ++g_ticks;
    if (!on || !InterlockedCompareExchange(&g_dirty, 0, 0) || g_ticks - g_lastRefresh < 30) return;
    refreshAll();
}

void ownedOnViewOff() {
    g_pageN = 0;
    g_marksNow = false;
    InterlockedExchange(&g_hoverId, 0);
}

void ownedWorldGone() {
    g_known = false;
    g_mapProven = false;     // a new world proves its self-checks again
    g_namesProven = false;
    g_namesOk = false;
    liveOwnedChanged();      // no owned flag survives into the next world
    g_pageN = 0;
    g_idCount = 0;
    g_ownedCount = 0;
    g_invCount = 0;
    g_invHashCount = 0;
    g_marksNow = false;
    InterlockedExchange(&g_hoverId, 0);
}

namespace {

void gridForget() {
    utGridReset(&g_grid);
    g_gridVerdict = 0;
    g_gridWaitSaid = false;
    g_gridWaitLongSaid = false;
}

// the measured caravan frame's grid is the origin proof - fed while the
// check is undecided, so the marks draw from the first frame the collection is shown. The proven
// hovers still run as the cross-check (ut_gridcheck.h). Game thread (the Present / page draw).
int g_feedWhySaid = -1;
// the "fed from" line is INFO for a NEW fed grid only; the same grid fed again
// (each new showing of the page: ut_panel forgets the frame and measures it again) is DEBUG.
bool g_fedSaid = false;
float g_fedSaidX = 0.0f, g_fedSaidY = 0.0f;
unsigned g_fedSaidCw = 0;
void gridFeedFromFrame(const PlateGeometry& g) {
    UtRectF fg = {0.0f, 0.0f, 0.0f, 0.0f}, fr = {0.0f, 0.0f, 0.0f, 0.0f};
    unsigned cw = 0, ch = 0;
    if (!panelFrameGrid(&fg) || !panelFrame(&fr) || !protoCellSize(&cw, &ch)) return;
    UtGridBounds b;
    b.canvasW = (float)g.canvasW;
    b.canvasH = (float)g.canvasH;
    b.winX = fr.x;
    b.winY = fr.y;
    b.winW = fr.w;
    b.winH = fr.h;
    // the frame's grid was measured with the REAL Transfer sack's cell: it must be the mod sack's
    const float gw = (float)(kGridCols * (int)cw), gh = (float)(kGridRows * (int)ch);
    int r = kUtGridFedNotUsable;
    if (utGridAbs(fg.w - gw) <= 1.0f && utGridAbs(fg.h - gh) <= 1.0f)
        r = utGridFeed(&g_grid, fg.x, fg.y, cw, ch, b);
    g_geoW = g.canvasW;   // a later change of either asks again (ownedMarksBegin)
    g_geoH = g.canvasH;
    g_geoScale = g.scale;
    if (r == kUtGridFed) {
        g_gridVerdict = 1;
        g_gridX = g_grid.gridX;
        g_gridY = g_grid.gridY;
        g_cw = g_grid.cw;
        g_ch = g_grid.ch;
        g_feedWhySaid = -1;
        const bool again = g_fedSaid && g_fedSaidCw == g_cw &&
                           !utFrameMoved(g_grid.fedX, g_grid.fedY, g_fedSaidX, g_fedSaidY);
        g_fedSaid = true;
        g_fedSaidX = g_grid.fedX;
        g_fedSaidY = g_grid.fedY;
        g_fedSaidCw = g_cw;
        char line[512];
        _snprintf_s(line, sizeof(line), _TRUNCATE,
                    "owned: grid at (%.1f,%.1f), cell %ux%u, %.0fx%.0f - fed from the measured "
                    "caravan frame (%.0f,%.0f) %.0fx%.0f (%s)%s; the marks draw from this frame, "
                    "every proven hover cross-checks it%s",
                    g_gridX, g_gridY, g_cw, g_ch, gw, gh, fr.x, fr.y, fr.w, fr.h,
                    panelFrameSourceText(),
                    !g_grid.fedAgree ? ""
                    : g_grid.adopted ? ", the hover before it agrees within the frame tolerance "
                                       "(the grid takes its origin)"
                                     : ", the hover before it agrees",
                    again ? " (the same grid, fed again)" : "");
        if (again)
            logD("%s", line);
        else
            logI("%s", line);
    } else if (r == kUtGridRefusedOrigin) {
        g_gridVerdict = -1;
        logW("owned: grid check FAILED (the proven hover's origin (%.1f,%.1f) is not the measured "
             "caravan frame's grid (%.1f,%.1f), %s) - the owned marks are refused (the view is "
             "unaffected)",
             g_grid.candX, g_grid.candY, fg.x, fg.y, panelFrameSourceText());
    } else if (r == kUtGridFedNotUsable && g_feedWhySaid != 1) {
        g_feedWhySaid = 1;
        logD("owned: the measured caravan frame's grid (%.1f,%.1f) %.0fx%.0f is not usable as the "
             "origin proof (the mod sack's live cell %ux%u drawn px, UI scale %.3f, frame "
             "(%.0f,%.0f) %.0fx%.0f, canvas %dx%d) - the proven hovers decide", fg.x, fg.y, fg.w,
             fg.h, cw, ch, g.scale, fr.x, fr.y, fr.w, fr.h, g.canvasW, g.canvasH);
    }
}

}  // namespace

// The decision is ut_gridcheck.h's (pure, tested in
// tools\test_viewgate.cpp); this only gathers the hover, the cursor and the bounds, and logs.
void ownedNoteHover(float gridX, float gridY, float originX, float originY, float x, float y,
                    unsigned id) {
    InterlockedExchange(&g_hoverId, (LONG)id);   // every proven hover; 0 = an empty cell
    {   // the LIVE caravan frame, from the same handler numbers (ut_panel, once per geometry)
        unsigned fcw = 0, fch = 0;
        if (protoCellSize(&fcw, &fch)) panelNoteHover(originX, originY, gridX, gridY, fcw, fch);
    }
    if (g_gridVerdict == 1) {   // the frame followed a move - so do the marks
        UtRectF fg = {0.0f, 0.0f, 0.0f, 0.0f};
        // a fed grid is compared as FED (its origin may have adopted a hover's)
        const float ax = g_grid.fed ? g_grid.fedX : g_gridX, ay = g_grid.fed ? g_grid.fedY : g_gridY;
        if (panelFrameGrid(&fg) && utFrameMoved(fg.x, fg.y, ax, ay)) {
            logI("owned: the measured caravan frame's grid (%.1f,%.1f) is not the accepted grid "
                 "origin (%.1f,%.1f) - the grid check runs again",
                 fg.x, fg.y, g_gridX, g_gridY);
            gridForget();
        }
    }
    // a FED grid keeps taking proven hovers until the cross-check is complete
    const bool crossCheck = g_gridVerdict == 1 && g_grid.fed && !g_grid.confirmed;
    if ((g_gridVerdict != 0 && !crossCheck) || !id) return;
    UtGridHover h = {};
    h.gridX = gridX;
    h.gridY = gridY;
    h.x = x;
    h.y = y;
    h.id = id;
    int k = -1;
    for (int i = 0; i < protoCount(); ++i) {
        unsigned pid = 0;
        if (protoAt(i, nullptr, &h.col, &h.row, &pid, &h.w, &h.h) && pid == id) {
            k = i;
            break;
        }
    }
    PlateGeometry g;
    if (k < 0 || !protoCellSize(&h.cw, &h.ch) || !plateGeometry(&g)) return;   // not decidable yet
    h.cursorRead = panelCursorNow(&h.cursorX, &h.cursorY);   // read at the hover itself
    UtGridBounds b;
    b.canvasW = (float)g.canvasW;
    b.canvasH = (float)g.canvasH;
    // (ratified): the window test is primary again - on the LIVE frame
    // once ut_panel accepted it; the records' rect (and so the cursor proof) only before that.
    UtRectF live = {0.0f, 0.0f, 0.0f, 0.0f};
    const bool liveFrame = panelFrame(&live);
    if (liveFrame) {
        g.winX = live.x;   // the log lines below print the rect the check used
        g.winY = live.y;
        g.winW = live.w;
        g.winH = live.h;
    }
    b.winX = g.winX;
    b.winY = g.winY;
    b.winW = g.winW;
    b.winH = g.winH;
    const char* winWhat = liveFrame ? "the measured caravan frame" : "the records' window rect";
    g_geoW = g.canvasW;   // a later change of either asks again (ownedMarksBegin)
    g_geoH = g.canvasH;
    g_geoScale = g.scale;
    const int r = utGridStep(&g_grid, h, b);
    const int cx = utGridCell(x, h.cw), cy = utGridCell(y, h.ch);
    const float gw = (float)(kGridCols * (int)h.cw), gh = (float)(kGridRows * (int)h.ch);
    if (r == kUtGridCrossAdopted) {   // within the frame tolerance - follow it
        g_gridX = g_grid.gridX;
        g_gridY = g_grid.gridY;
        logI("owned: grid - a proven hover's origin (%.2f,%.2f) is %.2f x %.2f px off the fed grid "
             "(%.2f,%.2f), within the caravan frame's tolerance (%.1f px): the grid takes the "
             "hover's origin (prototype %u at cell (%d,%d)); waiting for another prototype",
             gridX, gridY, gridX - g_grid.fedX, gridY - g_grid.fedY, g_grid.fedX, g_grid.fedY,
             kUtGridFedTol, id, cx, cy);
    } else if (r == kUtGridCrossAgree) {
        logD("owned: grid - a proven hover agrees with the fed grid (%.1f,%.1f): prototype %u at "
             "cell (%d,%d); waiting for another prototype", gridX, gridY, id, cx, cy);
    } else if (r == kUtGridConfirmed) {
        logI("owned: the proven hovers confirm the fed grid (%.1f,%.1f): prototype %u at cell "
             "(%d,%d), prototype %u at cell (%d,%d); the cursor matched %d of %d hovers",
             g_gridX, g_gridY, g_grid.fedFirstId, g_grid.fedFirstCol, g_grid.fedFirstRow, id, cx,
             cy, g_grid.cursorAgree, g_grid.cursorRead);
    } else if (r == kUtGridFirst) {
        logD("owned: grid - the first proven hover gives origin (%.1f,%.1f) = page pos (%.1f,%.1f) "
             "+ parent origin (%.1f,%.1f), prototype %u at cell (%d,%d); waiting for another "
             "prototype", gridX, gridY, gridX - originX, gridY - originY, originX, originY, id, cx,
             cy);
    } else if (r == kUtGridWaitCursor) {   // waiting is never a refusal
        if (!g_gridWaitSaid) {
            g_gridWaitSaid = true;
            logD("owned: grid (%.1f,%.1f) %.0fx%.0f leaves %s (%.0f,%.0f) "
                 "%.0fx%.0f; waiting for the cursor proof: two hovers matching the cursor, %d "
                 "cells apart (%d of %d so far, last %.1f px)",
                 gridX, gridY, gw, gh, winWhat, g.winX, g.winY, g.winW, g.winH, kUtGridSpreadCells,
                 g_grid.cursorAgree, g_grid.cursorRead, g_grid.cursorLast);
        } else if (!g_gridWaitLongSaid && g_grid.waits >= kUtGridWaitSay) {
            g_gridWaitLongSaid = true;
            logI("owned: grid (%.1f,%.1f) %.0fx%.0f leaves %s (%.0f,%.0f) "
                 "%.0fx%.0f and %d hovers on other prototypes brought no cursor proof yet (the "
                 "cursor matched %d of %d hovers, the matches %.0fx%.0f px apart, last %.1f px) - "
                 "no owned marks until two hovers %d cells apart match the cursor; still waiting",
                 gridX, gridY, gw, gh, winWhat, g.winX, g.winY, g.winW, g.winH, g_grid.waits,
                 g_grid.cursorAgree, g_grid.cursorRead, g_grid.agreeMaxX - g_grid.agreeMinX,
                 g_grid.agreeMaxY - g_grid.agreeMinY, g_grid.cursorLast, kUtGridSpreadCells);
        }
    } else if (r == kUtGridAccepted) {
        g_gridVerdict = 1;
        g_gridX = g_grid.gridX;
        g_gridY = g_grid.gridY;
        g_cw = g_grid.cw;
        g_ch = g_grid.ch;
        logI("owned: grid at (%.1f,%.1f), cell %ux%u, %.0fx%.0f - page pos (%.1f,%.1f) + parent "
             "origin (%.1f,%.1f); two proven hovers agree (prototype %u at cell (%d,%d), prototype "
             "%u at cell (%d,%d)); inside the %dx%d canvas; %s (%.0f,%.0f) "
             "%.0fx%.0f: %s; the cursor matched %d of %d hovers, the matches %.0fx%.0f px apart "
             "(last %.1f px)",
             g_gridX, g_gridY, g_cw, g_ch, gw, gh, gridX - originX, gridY - originY, originX,
             originY, g_grid.candId, g_grid.candCol, g_grid.candRow, id, cx, cy, g.canvasW,
             g.canvasH, winWhat, g.winX, g.winY, g.winW, g.winH,
             g_grid.windowOk ? "inside" : "OUTSIDE (the cursor vouched)", g_grid.cursorAgree,
             g_grid.cursorRead, g_grid.agreeMaxX - g_grid.agreeMinX,
             g_grid.agreeMaxY - g_grid.agreeMinY, g_grid.cursorLast);
    } else if (g_grid.verdict < 0) {
        g_gridVerdict = -1;
        const char* why = r == kUtGridRefusedCell ? "check 1: the cell is not the prototype's"
                          : r == kUtGridRefusedOrigin
                              ? (g_grid.fed ? "a proven hover disagrees with the fed grid of the "
                                              "measured caravan frame"
                                            : "two proven hovers give different origins")
                              : "the grid leaves the canvas";
        logW("owned: grid check FAILED (%s) - origin (%.1f,%.1f), point (%.1f,%.1f) -> cell "
             "(%d,%d), prototype %u at (%d,%d) %dx%d; the first hover's origin (%.1f,%.1f) on "
             "prototype %u; %s (%.0f,%.0f) %.0fx%.0f on a %dx%d canvas; the "
             "cursor matched %d of %d hovers (last %.1f px) - the owned marks are refused (the "
             "view is unaffected)",
             why, gridX, gridY, x, y, cx, cy, id, h.col, h.row, h.w, h.h, g_grid.candX,
             g_grid.candY, g_grid.candId, winWhat, g.winX, g.winY, g.winW, g.winH, g.canvasW,
             g.canvasH,
             g_grid.cursorAgree, g_grid.cursorRead, g_grid.cursorLast);
    }
}

int ownedSackKeys(const void* sack, unsigned* out, int cap) {
    int n = 0;
    const int r = sackKeys((const TqSack*)sack, out, &n, cap);
    return (r < 0 || r != n) ? -1 : n;
}

// the hovered slot (g_unveil) from the given cursor, with the marks' trust of the
// last ownedMarksBegin. ownedMarksBegin ends with it; the rect route calls it at the page draw's
// Begin with the cursor the POST reads, so the gray lend and the veil agree in every frame.
void ownedUnveilRefresh(bool cursorOk, float cx, float cy) {
    g_unveil = -1;
    if (g_marksNow && cursorOk && g_cw && g_ch && cx >= g_gridX && cy >= g_gridY) {
        // the veil STAYS on every unowned slot while the cursor is on the
        // grid (hiding them all read as "everything collected"); only the hovered
        // slot is un-veiled, so the item under the cursor shows bright; the tooltip is
        // drawn over dim items.
        const int col = (int)((cx - g_gridX) / (float)g_cw);
        const int row = (int)((cy - g_gridY) / (float)g_ch);
        if (col >= 0 && col < kGridCols && row >= 0 && row < kGridRows)
            g_unveil = protoSlotIndexAtCell(col, row);
    }
}

int ownedMarksBegin(const PlateGeometry& g, bool cursorOk, float cx, float cy) {
    if ((g_gridVerdict != 0 || g_grid.candSet) &&
        (g.canvasW != g_geoW || g.canvasH != g_geoH || g.scale != g_geoScale)) {
        gridForget();   // a new window geometry: measure again on the next hovers
        logI("owned: the canvas or UI scale changed - the grid is measured again");
    }
    if (g_gridVerdict == 1 && g_grid.fed) {   // the fed grid follows the measured frame
        UtRectF fg = {0.0f, 0.0f, 0.0f, 0.0f};
        // INFO when the frame MOVED; a frame that is only gone (each new showing
        // of the page: ut_panel measures it again) is DEBUG. Compared as FED.
        if (!panelFrameGrid(&fg)) {
            logD("owned: the measured caravan frame is gone (a new showing of the page) - the fed "
                 "grid (%.1f,%.1f) is dropped and fed again", g_gridX, g_gridY);
            gridForget();
        } else if (utFrameMoved(fg.x, fg.y, g_grid.fedX, g_grid.fedY)) {
            logI("owned: the measured caravan frame's grid moved to (%.1f,%.1f) - the fed grid "
                 "(%.1f,%.1f) is dropped and fed again", fg.x, fg.y, g_gridX, g_gridY);
            gridForget();
        }
    }
    if (g_gridVerdict == 0) gridFeedFromFrame(g);   // no mouse move needed
    // under the OWN filter every record shown is owned - the veil is moot.
    // bright = collected (journal rows), dim = not; the self-checks (g_known) still prove
    // the page's prototypes before any mark is drawn over them.
    g_marksNow = g_cfg.ownedMarks != 0 && g_known && journalSetKnown() && g_gridVerdict == 1 &&
                 g_pageN > 0 && !liveOwnedOnlyActive();
    ownedUnveilRefresh(cursorOk, cx, cy);
    return g_marksNow ? g_cfg.ownedMarks : 0;
}

bool ownedMarkRect(int i, float* x, float* y, float* w, float* h) {
    if (!g_marksNow || i < 0 || i >= g_pageN || i >= kUtProtoMax || g_pageOwned[i]) return false;
    if (i == g_unveil) return false;   // the hovered slot shows bright
    int col = 0, row = 0, fw = 0, fh = 0;   // the veil covers the whole SLOT
    if (!protoSlotAt(i, &col, &row, &fw, &fh)) return false;
    if (col < 0 || row < 0 || fw < 1 || fh < 1 || col + fw > kGridCols || row + fh > kGridRows)
        return false;
    // the one helper (g_cw = the mod sack's live cell = the drawn cell, fed or proven)
    const UtRectF grid = {g_gridX, g_gridY, (float)(kGridCols * (int)g_cw),
                          (float)(kGridRows * (int)g_ch)};
    const UtRectF r = utSlotDrawnRect(grid, col, row, fw, fh);
    *x = r.x;
    *y = r.y;
    *w = r.w;
    *h = r.h;
    return true;
}

bool ownedGrayWanted(int i) {
    if (i < 0 || i >= g_pageN || i >= kUtProtoMax) return false;
    // the rule is ut_viewgate.h utOwnedGray (the viewgate row runs it too)
    return utOwnedGray(g_pageOwned[i] != 0, i == g_unveil, g_marksNow ? g_cfg.ownedMarks : 0);
}

bool ownedMarksNow() { return g_marksNow; }

bool ownedBackgroundSkip(int i, bool routeLive) {
    if (i < 0 || i >= g_pageN || i >= kUtProtoMax) return false;
    return utBackgroundSkip(g_pageOwned[i] != 0, i == g_unveil, g_marksNow ? g_cfg.ownedMarks : 0,
                            routeLive);
}

bool ownedHaveRect(int i, float* x, float* y, float* w, float* h) {
    if (!g_marksNow || !g_cfg.haveMarks || i < 0 || i >= g_pageN || i >= kUtProtoMax ||
        g_pageOwned[i] || !g_pageHave[i])
        return false;
    int col = 0, row = 0, fw = 0, fh = 0;
    if (!protoSlotAt(i, &col, &row, &fw, &fh)) return false;
    if (col < 0 || row < 0 || fw < 1 || fh < 1 || col + fw > kGridCols || row + fh > kGridRows)
        return false;
    const float side = (float)((int)g_cw / 4 > 4 ? (int)g_cw / 4 : 4);   // 8 px on a 32 px cell
    // the one helper (slot -> drawn rect), and the 2 px inset is record px:
    // x the drawn cell / 32 (= the UI scale), GD's rounding
    const UtRectF grid = {g_gridX, g_gridY, (float)(kGridCols * (int)g_cw),
                          (float)(kGridRows * (int)g_ch)};
    const UtRectF r = utSlotDrawnRect(grid, col, row, fw, fh);
    const float k = utPadRound(2.0f * (float)(int)g_cw / 32.0f);
    *x = r.x + r.w - side - k;
    *y = r.y + k;
    *w = side;
    *h = side;
    return true;
}

bool ownedLabelAll(int* owned, int* total) {
    if (owned) *owned = g_allOwned;
    if (total) *total = g_allTotal;
    return g_allKnown && journalSetKnown();
}

bool ownedLabel(int* owned, int* total) {
    if (owned) *owned = g_groupOwned;
    if (total) *total = g_groupTotal;
    return journalSetKnown();
}

// "owned" is now COLLECTED - the journal's rows; known while a save set is open.
bool ownedKnown() { return journalSetKnown(); }

int ownedRecordState(const char* record) {
    if (!journalSetKnown() || !record) return -1;
    return collected(record) ? 1 : 0;
}

const char* ownedStatus() { return g_status; }

}  // namespace ut
