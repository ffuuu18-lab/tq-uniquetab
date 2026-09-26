// ut_search.cpp - the property search's game side (see ut_search.h): the capture, the index and
// its schedule, the query, and the one fault latch. GAME THREAD ONLY, except the three getters
// the pad reads (searchMarks, searchQueryStands, searchLabel), which read interlocked words.
//
// The capture of one record, in this order:
//   1. utReplicaBuild: the bare replica (seed 0, no relic), exactly what the page builds for a
//      record the player has not collected;
//   2. Item::CreateItem (protoCreateLoose). A null item or an id of 0: whatever exists is
//      destroyed again and the record is UNINDEXABLE (counted, never retried). A FAULT inside
//      CreateItem or GetObjectId is told apart from a null or a 0, and is a fault (below);
//   3. the item's vftable +0x14C must hold one of the four GetUIDisplayText overrides the tooltip
//      resolved by name; any other value: unindexable (a fault reading the slot: a fault);
//   4. the thread-local capture flag is set, so the tooltip detours this call runs through leave
//      the rollover latch alone (ut_tooltip.cpp latchBody);
//   5. vt[+0x14C](item, mainPlayer, &v) into a mod-owned empty VS2012 vector {0,0,0}. The
//      character MUST be the main player: ItemEquipment's set block reads it with no null test;
//   6. the lines are staged (utSearchStage: pure reads, the only work inside that SEH frame);
//   7. every heap string and then the array go back through MSVCR110's operator delete, the
//      allocator that made them (utSearchFreeStaged);
//   8. the flag is cleared, the item is destroyed (DestroyObjectEx);
//   9. the staged lines are folded into the record's text (mod memory only, outside every frame).
// The item exists only inside this one call on the game thread: no engine Update, draw, network
// tick, reconciliation or take ever sees it, and it is in no sack.
//
// The SEH and lock rule is the tooltip's: an SEH frame here wraps ONE engine call, ONE run of
// pure reads, or one of the real Transfer page's two bounded walks (GetInventory plus the sack
// map walk; GetObjectList plus the id resolve), never anything that takes a mod lock or logs; a
// fault is reported after the frame
// has been left. Any fault turns the search OFF for the session with one ERROR line and it is
// never retried: the page is then exactly what it is with search=0.
//
// The field: a left click on it gives it the focus; while it has the focus the key gate (the
// Display::HandleKeyEvent detour, which runs before every game widget) takes every key PRESS and
// passes every release, so a typed letter never reaches a game hotkey. The focus is dropped by
// Enter, Esc on an empty field, a click outside, the page or the view or the caravan or the world
// going away, the window losing the focus, a fault, and 60 s without a key.

#include "ut_search.h"

#include <windows.h>

#include <stdio.h>

#include <string>
#include <vector>

#include "hooks.h"
#include "tq_runtime.h"
#include "ut_config.h"
#include "ut_live.h"
#include "ut_log.h"
#include "ut_panel.h"
#include "ut_plate.h"
#include "ut_proto.h"
#include "ut_tooltip.h"
#include "ut_view.h"

namespace ut {
namespace {

// vftable +0x14C: GetUIDisplayText(Character const*, vector<GameTextLine>&) - ItemEquipment
// 0x1C0090, ItemArtifact 0x1BA540, ItemRelic 0x1C3C00 (Game.dll, AE 2.10).
const unsigned kSlotDisplayText = 0x14C;
// The two fallbacks, written and switched OFF. They are turned on only if the capture's DEBUG
// dump shows they are needed:
//   * the name: vftable +0xFC GetGameDescription(bool, bool) -> wstring, added as one more line
//     when the rollover carries no name line;
//   * the requirements: vftable +0x15C GetUIRequirementText(Character const*, vector&) into a
//     second vector, and every captured line equal to one of its lines dropped - for the case
//     that the requirement lines carry a class other than 0x11.
const bool kNameFallback = false;
const bool kRequirementSubtraction = false;
const unsigned kSlotGameDescription = 0xFC;
const unsigned kSlotRequirementText = 0x15C;

const double kBackgroundBudgetMs = 1.5;   // per Update

typedef void(__thiscall* PfnDisplayText)(void* item, const void* character, void* lines);
typedef void*(__thiscall* PfnGameDescription)(void* item, void* ret, bool a, bool b);

// ---- the index ------------------------------------------------------------------------------
enum : unsigned char { kPending = 0, kIndexed = 1, kUnindexable = 2 };

struct Index {
    std::vector<std::string> text;       // per record (the groups one after another)
    std::vector<unsigned char> state;
    std::vector<int> base;               // first record of each group
    std::vector<int> done;               // records of each group processed
    std::vector<double> groupMs;
    int total = 0;
    int processed = 0;
    int next = 0;                        // the background's next record
};
Index* g_ix = nullptr;

bool g_started = false;
volatile LONG g_off = 0;
unsigned g_dumpSaid = 0;   // bit b: the first capture of rollover builder b has been dumped
bool g_infoSaid = false;
int g_unindexableSaid = 0;
// the numbers of the INFO line
unsigned g_linesKept = 0, g_dropReq = 0, g_dropLore = 0, g_dropDir = 0, g_unindexable = 0;
double g_totalMs = 0.0, g_maxUs = 0.0;
char g_maxRec[160] = "";
size_t g_bytes = 0;

// the query: the field's text; the ini key search_debug_query fills the field when it changes
char g_raw[sizeof(UtConfig::searchDebugQuery)] = "";   // the ini key's last value
bool g_rawInit = false;
LONG g_fieldSeen = -1;          // the field generation the needle was built from
std::string* g_needle = nullptr;
bool g_active = false;          // a query stands and the search is on
bool g_queryLinePending = false;

// the words the pad reads (any thread)
volatile LONG g_labelOn = 0;
volatile LONG g_labelDone = 0;
volatile LONG g_labelTotal = 0;

// the field: its text under its own lock (never taken inside an SEH frame), the rest interlocked
SRWLOCK g_fieldLock = SRWLOCK_INIT;
UtSearchField g_field = {{0}, 0};
volatile LONG g_fieldGen = 0;     // +1 per change of the text
volatile LONG g_focus = 0;        // the field has the focus: the key gate takes the presses
volatile LONG g_lastKey = 0;      // GetTickCount of the focus or of the last key it took
volatile LONG g_wantStart = 0;    // a focus asked for the index (the tick starts it)
volatile LONG g_gateFault = 0;    // the key gate could not read an event: the tick goes off
bool g_keySaid = false;           // the first focused press is said once (DEBUG)
bool g_stateSaid = false;         // the first focused event that is not a press, once (DEBUG)
bool g_offClickSaid = false;      // a click on the disabled field, once (INFO)
bool g_repeatSaid = false;        // the first focused event neither a press nor a release (DEBUG)

// the real Transfer page (the view OFF, search_transfer=1): its items' text, keyed by (id, the
// item's pointer, its record) for the session; when the sack's id set changes only the items still
// there under the same key keep their text; all of it goes when the world unloads or the main
// player changes. The draw reads the matched rects under g_realLock; the label reads interlocked words.
struct RealItem {
    unsigned id;
    const void* item;
    std::string record;
    std::string text;
    unsigned char state;   // kPending / kIndexed / kUnindexable
    bool match;
};
struct RealCache {
    std::vector<RealItem> items;   // ascending ids, as the walk returns them
    std::vector<unsigned> ids;     // the id set the cache was built on
    size_t next = 0;
    int done = 0;                  // read or unindexable
    bool ready = false;            // resolved for the id set in `ids`
    double ms = 0.0;               // the walk, the resolve and the reads of this id set
    std::string needle;            // the query the matches are for
    const void* player = nullptr;  // the main player the pointers were resolved under
    bool matchesDirty = true;
    bool querySaid = false;        // the DEBUG line for `needle`
};
RealCache* g_real = nullptr;
bool g_realSaid = false;           // the INFO line: the first time the cache is complete
bool g_realMissingSaid = false;    // the INFO line: a binding the real page's read needs is missing
const int kRealNameCap = 260;
UtSackEntry g_realWalk[kUtSearchRealMax];           // this Update's walk (game thread)
const void* g_realObjs[kUtSearchRealMax];
char g_realNames[kUtSearchRealMax][kRealNameCap];   // the resolve's copies of the record names
SRWLOCK g_realLock = SRWLOCK_INIT;
UtSackEntry g_realMarks[kUtSearchRealMax];          // the matched items' rects (g_realLock)
int g_realMarkN = 0;
volatile LONG g_realReady = 0, g_realDone = 0, g_realTotal = 0, g_realFound = 0;
UtBackRepeat g_back = {false, 0, 0, 0, false};   // Backspace held (under g_fieldLock)

bool ensureIndex() {
    if (g_ix) return true;
    if (!liveActive()) return false;
    try {
        Index* ix = new Index();
        const int groups = liveGroupCount();
        for (int g = 0; g < groups; ++g) {
            ix->base.push_back(ix->total);
            ix->done.push_back(0);
            ix->groupMs.push_back(0.0);
            ix->total += liveGroupEntries(g) > 0 ? liveGroupEntries(g) : 0;
        }
        ix->text.resize((size_t)ix->total);
        ix->state.assign((size_t)ix->total, kPending);
        g_needle = new std::string();
        g_ix = ix;
        InterlockedExchange(&g_labelTotal, ix->total);
        return true;
    } catch (...) {
        return false;
    }
}

bool off() { return InterlockedCompareExchange(&g_off, 0, 0) != 0; }

bool groupComplete(int g) {
    return g_ix && g >= 0 && g < (int)g_ix->done.size() && g_ix->done[(size_t)g] >= liveGroupEntries(g);
}

unsigned indexedMask() {
    unsigned m = 0;
    for (int g = 0; g < liveGroupCount() && g < 32; ++g) {
        if (groupComplete(g)) m |= 1u << (unsigned)g;
    }
    return m;
}

// ---- the engine calls, each in its own frame -------------------------------------------------
// Null = no player (the menu, a world change). `faulted`: the call faulted, which is not "no
// player" - the caller turns the search off.
const void* mainPlayer(bool* faulted) {
    *faulted = false;
    TqGameEngine* ge = gameEngine();
    if (!ge || !g_tq.GameGetMainPlayer) return nullptr;
    const void* p = nullptr;
    utGuardEnter();
    __try {
        p = g_tq.GameGetMainPlayer(ge);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        p = nullptr;
        *faulted = true;
    }
    utGuardLeave();
    return p;
}

bool readSlotSeh(const void* item, unsigned slot, void** fn) {
    __try {
        const void* const* vt = *(const void* const* const*)item;
        *fn = (void*)vt[slot / sizeof(void*)];
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *fn = nullptr;
        return false;
    }
}

// True when `a` and `b` are code of one loaded module: the two fallback slots are called only
// when they point into the module that holds the item's known +0x14C builder.
bool sameModule(const void* a, const void* b) {
    const DWORD f =
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    HMODULE ma = nullptr, mb = nullptr;
    return a && b && GetModuleHandleExA(f, (LPCSTR)a, &ma) && GetModuleHandleExA(f, (LPCSTR)b, &mb) &&
           ma && ma == mb;
}

bool callTextSeh(PfnDisplayText fn, void* item, const void* player, void* vec) {
    __try {
        fn(item, player, vec);   // ENGINE CALL: the item's own rollover builder
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool callDescriptionSeh(PfnGameDescription fn, void* item, void* ret) {
    __try {
        fn(item, ret, false, false);   // ENGINE CALL: the one-line name (fallback, off)
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// -2 = a fault while reading, -1 = not a GameTextLine vector, else the lines staged.
int stageSeh(const void* vec, UtSearchStage* st) {
    __try {
        return utSearchStage(vec, st);   // pure reads, nothing else
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        st->count = 0;
        st->heapCount = 0;
        st->buffer = nullptr;
        return -2;
    }
}

void crtDelete(void* p) { g_tq.CrtOperatorDelete(p); }

bool freeSeh(const UtSearchStage* st) {
    __try {
        utSearchFreeStaged(st, &crtDelete);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ---- the fault latch --------------------------------------------------------------------------
void applyToPage();
void realPublish(bool on);
double msSince(const LARGE_INTEGER& t0);

// The page is never laid out by the search, so going off only drops the highlight, the marks and
// the field's focus (the second argument, the page build's own call, changes nothing now).
void goOff(const char* where, bool = false) {
    if (InterlockedExchange(&g_off, 1)) return;
    logE("search: OFF ***** %s faulted - the index and the field are off for this session",
         where ? where : "?");
    g_active = false;
    InterlockedExchange(&g_labelOn, 0);
    applyToPage();   // no highlight and no mark: the page is what it is with search=0
    realPublish(false);   // nor on the real Transfer page
    searchFieldBlur("the search went off (a fault)");
}

// ---- one record ------------------------------------------------------------------------------
enum { kCapOk = 0, kCapUnindexable = 1, kCapFault = 2 };

struct Capture {
    const char* why;       // an unindexable record's reason
    const char* where;     // a fault's place
};

UtSearchStage g_stage;      // ~70 KB each: game thread only, never on the stack
UtSearchStage g_stageReq;

// One raw line for the DEBUG dump: printable UTF-8, quotes kept.
void appendRaw(std::string* out, const UtSearchStage& st, unsigned i) {
    const unsigned short* s = st.text + st.start[i];
    for (unsigned k = 0; k < st.len[i]; ++k) {
        const unsigned c = s[k];
        if (c < 0x20 || c == 0x7F) out->push_back('?');
        else utSearchPutUtf8(out, (c >= 0xD800 && c <= 0xDFFF) ? 0xFFFD : c);
    }
}

const char* const kBuilderName[4] = {"Item", "ItemEquipment", "ItemArtifact", "ItemRelic"};

// The DEBUG capture dump: the first capture of each rollover builder (at most four lines), so the
// ItemEquipment one, which carries the requirement and lore lines, is always among them.
void dumpOnce(const char* record, int builder, const UtSearchStage& st) {
    if (builder < 0 || builder > 3 || !logWants(UT_LOG_DEBUG)) return;
    if (g_dumpSaid & (1u << (unsigned)builder)) return;
    g_dumpSaid |= 1u << (unsigned)builder;
    std::string d;
    for (unsigned i = 0; i < st.count && d.size() < 1700; ++i) {
        char head[24];
        _snprintf_s(head, sizeof(head), _TRUNCATE, "%s[0x%02X] \"", i ? " | " : "", st.cls[i]);
        d += head;
        appendRaw(&d, st, i);
        d += "\"";
    }
    if (d.size() > 1700) d.resize(1700);
    logD("search: capture %s (%s): %s", record, kBuilderName[builder], d.c_str());
}

// The requirement subtraction (off): drops every staged line of `st` whose text equals a line of
// `req`, by marking its class as the requirements class.
void subtractRequirements(UtSearchStage* st, const UtSearchStage& req) {
    for (unsigned i = 0; i < st->count; ++i) {
        for (unsigned j = 0; j < req.count; ++j) {
            if (st->len[i] == req.len[j] &&
                memcmp(st->text + st->start[i], req.text + req.start[j],
                       st->len[i] * sizeof(unsigned short)) == 0) {
                st->cls[i] = kUtSearchClsRequirements;
                break;
            }
        }
    }
}

// The text of one LIVE item - a throw-away prototype (captureOne) or an item in the real Transfer
// sack: the +0x14C slot must hold a known GetUIDisplayText; the capture flag is set; the builder
// runs with the main player into a mod-owned empty vector; the lines are staged and freed through
// the CRT's operator delete; the flag is cleared; the staged lines are folded into `out`. The item
// itself is never changed, moved or destroyed here.
int captureItem(TqItem* item, const void* player, const char* record, std::string* out,
                UtSearchCounts* c, Capture* cap) {
    cap->why = "";
    cap->where = "";
    void* fn = nullptr;
    utGuardEnter();
    const bool slotRead = readSlotSeh(item, kSlotDisplayText, &fn);
    utGuardLeave();
    if (!slotRead) {   // a live item's vftable: a fault reading it is never a property of the record
        cap->where = "the item's +0x14C slot read";
        return kCapFault;
    }
    const int builder = tooltipTextBuilderIndex(fn);
    if (builder < 0) {
        cap->why = "its +0x14C slot is not a known GetUIDisplayText";
        return kCapUnindexable;
    }
    TqPtrVector v = {nullptr, nullptr, nullptr};
    TqPtrVector r = {nullptr, nullptr, nullptr};
    tooltipSearchCapture(true);
    utGuardEnter();
    bool called = callTextSeh((PfnDisplayText)fn, item, player, &v);
    utGuardLeave();
    if (!called) {
        tooltipSearchCapture(false);
        cap->where = "the GetUIDisplayText call";
        return kCapFault;
    }
    utGuardEnter();
    const int n = stageSeh(&v, &g_stage);
    utGuardLeave();
    if (n < 0) {
        tooltipSearchCapture(false);
        cap->where = n == -2 ? "the line read" : "the line vector's shape";
        // the vector is left as it is: nothing is freed on a doubt
        return kCapFault;
    }
    utGuardEnter();
    const bool freed = freeSeh(&g_stage);
    utGuardLeave();
    if (!freed) {
        tooltipSearchCapture(false);
        cap->where = "the line free";
        return kCapFault;
    }
    if constexpr (kRequirementSubtraction) {
        void* rf = nullptr;
        utGuardEnter();
        bool ok = readSlotSeh(item, kSlotRequirementText, &rf);
        utGuardLeave();
        ok = ok && sameModule(rf, fn);
        if (ok) {
            utGuardEnter();
            ok = callTextSeh((PfnDisplayText)rf, item, player, &r);
            utGuardLeave();
        }
        int rn = -1;
        if (ok) {
            utGuardEnter();
            rn = stageSeh(&r, &g_stageReq);
            utGuardLeave();
        }
        if (!ok || rn < 0) {
            tooltipSearchCapture(false);
            cap->where = "the requirement lines";
            return kCapFault;
        }
        utGuardEnter();
        ok = freeSeh(&g_stageReq);
        utGuardLeave();
        if (!ok) {
            tooltipSearchCapture(false);
            cap->where = "the requirement lines' free";
            return kCapFault;
        }
        subtractRequirements(&g_stage, g_stageReq);
    }
    (void)r;
    bool hasName = false;
    for (unsigned i = 0; i < g_stage.count; ++i) {
        if (g_stage.cls[i] >= 0x02 && g_stage.cls[i] <= 0x0D) hasName = true;
    }
    if constexpr (kNameFallback) {
        if (!hasName) {
            void* df = nullptr;
            unsigned char ws[0x18] = {};   // a VS2012 wstring the callee constructs
            utGuardEnter();
            bool ok = readSlotSeh(item, kSlotGameDescription, &df);
            utGuardLeave();
            ok = ok && sameModule(df, fn);
            if (ok) {
                utGuardEnter();
                ok = callDescriptionSeh((PfnGameDescription)df, item, ws);
                utGuardLeave();
            }
            if (!ok) {
                tooltipSearchCapture(false);
                cap->where = "the GetGameDescription call";
                return kCapFault;
            }
            // Stage it as one more line of class 0x02, then free its heap text if it has one.
            unsigned char line[kUtSearchLineSize] = {};
            const unsigned nameCls = 0x02;
            memcpy(line, &nameCls, 4);
            memcpy(line + 0x04, ws, 0x18);
            const unsigned char* lv[3] = {line, line + kUtSearchLineSize, line + kUtSearchLineSize};
            utGuardEnter();
            const int nn = stageSeh(lv, &g_stageReq);
            utGuardLeave();
            if (nn < 0) {   // the string is left as it is: nothing is freed on a doubt
                tooltipSearchCapture(false);
                cap->where = nn == -2 ? "the GetGameDescription string's read"
                                      : "the GetGameDescription string's shape";
                return kCapFault;
            }
            if (nn == 1 && g_stage.count < kUtSearchMaxLines &&
                g_stage.used + g_stageReq.len[0] <= kUtSearchStageChars) {
                const unsigned at = g_stage.count++;
                g_stage.cls[at] = nameCls;
                g_stage.start[at] = g_stage.used;
                g_stage.len[at] = g_stageReq.len[0];
                memcpy(g_stage.text + g_stage.used, g_stageReq.text,
                       g_stageReq.len[0] * sizeof(unsigned short));
                g_stage.used += g_stageReq.len[0];
            }
            g_stageReq.buffer = nullptr;   // the line is the mod's; only its heap text is freed
            utGuardEnter();
            const bool nameFreed = freeSeh(&g_stageReq);
            utGuardLeave();
            if (!nameFreed) {
                tooltipSearchCapture(false);
                cap->where = "the GetGameDescription string's free";
                return kCapFault;
            }
        }
    }
    (void)hasName;
    tooltipSearchCapture(false);
    // ---- from here on: mod memory only ----------------------------------------------------------
    dumpOnce(record, builder, g_stage);
    if (g_stage.count == 0) {
        cap->why = "its rollover has no lines";
        return kCapUnindexable;
    }
    utSearchFoldStage(&g_stage, g_cfg.searchLore != 0, out, c);
    return kCapOk;
}

int captureOne(const char* record, const void* player, std::string* out, UtSearchCounts* c,
               Capture* cap) {
    static UtReplica rep;   // ~2 KB
    cap->why = "";
    cap->where = "";
    if (!utReplicaBuild(&rep, record)) {
        cap->why = "its record path is not one a replica can carry";
        return kCapUnindexable;
    }
    unsigned id = 0;
    const char* createFault = nullptr;
    TqItem* item = protoCreateLoose(rep, &id, &createFault);
    if (createFault) {   // a fault inside CreateItem or GetObjectId: never a property of the record
        if (item) protoDestroyLoose(item);
        cap->where = createFault;
        return kCapFault;
    }
    if (!item || !id) {
        if (item && !protoDestroyLoose(item)) {
            cap->where = "the throw-away item's destroy";
            return kCapFault;
        }
        cap->why = "CreateItem gave no item";
        return kCapUnindexable;
    }
    const int rc = captureItem(item, player, record, out, c, cap);
    if (rc == kCapFault) {   // the capture's own fault is the one reported
        protoDestroyLoose(item);
        return kCapFault;
    }
    if (!protoDestroyLoose(item)) {
        out->clear();
        cap->where = "the throw-away item's destroy";
        return kCapFault;
    }
    return rc;
}

// ---- the real Transfer page ------------------------------------------------------------------
// -2 = a fault, -1 = the map is not a VS2012 map, else the entries read (GetInventory is the one
// engine call; the walk is pure reads).
int realWalkSeh(const TqSack* sack, UtSackEntry* out) {
    __try {
        const void* map = g_tq.SackGetInventory(sack);
        return map ? utSackMapRead(map, out, kUtSearchRealMax) : -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -2;
    }
}

// ONE ObjectManager::GetObjectList walk: every object whose id the walk holds gets its pointer
// and its record name (copied into mod memory). 1 = done, 0 = no object manager yet, -1 = the
// list's shape, -2 = a fault. Nothing is logged or locked inside; `v` is freed by the caller.
int realResolveSeh(TqPtrVector* v, const UtSackEntry* e, int n, int* found) {
    __try {
        void* om = g_tq.ObjectManagerGet();
        if (!om) return 0;
        g_tq.ObjectManagerGetObjectList(om, v);
        if (!(v->last >= v->first && v->end >= v->last && (v->last - v->first) <= 4000000))
            return -1;
        const size_t count = (size_t)(v->last - v->first);
        for (size_t i = 0; i < count; ++i) {
            const void* obj = v->first[i];
            if (!obj) continue;
            const unsigned id = g_tq.ObjectGetObjectId(obj);
            if (!id) continue;
            int lo = 0, hi = n;   // the walk is ascending
            while (lo < hi) {
                const int mid = (lo + hi) / 2;
                if (e[mid].id < id) lo = mid + 1;
                else hi = mid;
            }
            if (lo >= n || e[lo].id != id || g_realObjs[lo]) continue;
            g_realObjs[lo] = obj;
            const char* name = g_tq.ObjectGetObjectName(obj);
            int k = 0;
            for (; name && name[k] && k < kRealNameCap - 1; ++k) g_realNames[lo][k] = name[k];
            g_realNames[lo][k] = 0;
            ++*found;
        }
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -2;
    }
}

bool realFreeSeh(TqPtrVector* v) {
    __try {
        if (v->first) g_tq.CrtOperatorDelete((void*)v->first);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The draw's list and the label's words follow the cache.
void realPublish(bool on) {
    int m = 0;
    AcquireSRWLockExclusive(&g_realLock);
    if (on && g_real && g_real->ready) {
        for (size_t i = 0; i < g_real->items.size() && m < kUtSearchRealMax; ++i) {
            if (!g_real->items[i].match) continue;
            g_realMarks[m++] = g_realWalk[i];
        }
    }
    g_realMarkN = m;
    ReleaseSRWLockExclusive(&g_realLock);
    const bool ready = on && g_real && g_real->ready;
    InterlockedExchange(&g_realReady, ready ? 1 : 0);
    InterlockedExchange(&g_realDone, ready ? g_real->done : 0);
    InterlockedExchange(&g_realTotal, ready ? (LONG)g_real->items.size() : 0);
    InterlockedExchange(&g_realFound, m);
}

void realDrop() {
    if (!g_real) return;
    g_real->items.clear();
    g_real->ids.clear();
    g_real->next = 0;
    g_real->done = 0;
    g_real->ready = false;
    g_real->ms = 0.0;
    g_real->matchesDirty = true;
    g_real->querySaid = false;
}

// Once per Update (game thread): while the real Transfer page is shown with a query standing or
// the field focused, its items are read - the map walk every Update (the rects follow a sort),
// the ids resolved with one object walk whenever the id set changes, and the items' text captured
// 1.5 ms per Update at most. True = it captured something this Update (the background index
// waits). Nothing is read while the view is ON, without a world, or with search_transfer=0.
bool realTick(bool worldUp) {
    if (!worldUp) realDrop();   // the item pointers go with the world
    const bool focused = InterlockedCompareExchange(&g_focus, 0, 0) != 0;
    const bool shown = g_cfg.search && g_cfg.searchTransfer && worldUp && !off() && !viewOn() &&
                       (g_active || focused) && hookCaravanOpen() && plateTransferVisible();
    const char* missing = !g_tq.SackGetInventory              ? "InventorySack::GetInventory"
                          : !g_tq.transferOff                  ? "the Transfer sack's offset"
                          : !g_tq.ObjectManagerGet             ? "the ObjectManager"
                          : !g_tq.ObjectManagerGetObjectList   ? "ObjectManager::GetObjectList"
                          : !g_tq.ObjectGetObjectId            ? "Object::GetObjectId"
                          : !g_tq.ObjectGetObjectName          ? "Object::GetObjectName"
                          : !g_tq.CrtOperatorDelete            ? "the CRT operator delete"
                          : !g_tq.GameGetMainPlayer            ? "GameEngine::GetMainPlayer"
                                                               : nullptr;
    if (shown && missing && !g_realMissingSaid) {
        g_realMissingSaid = true;
        logI("search: the Transfer page's items are not read - %s is not bound", missing);
    }
    if (!shown || missing) {   // the label then shows the page's name alone
        realPublish(false);
        return false;
    }
    bool playerFault = false;
    const void* player = mainPlayer(&playerFault);
    if (playerFault) {
        goOff("the GameGetMainPlayer call");
        return false;
    }
    TqGameEngine* ge = gameEngine();
    if (!player || !ge) {   // a world change: nothing is read, the cache waits
        realPublish(false);
        return false;
    }
    if (!g_real) {
        try {
            g_real = new RealCache();
        } catch (...) {
            g_real = nullptr;
        }
        if (!g_real) {
            goOff("the Transfer page's cache allocation");
            return false;
        }
    }
    if (g_real->player != player) {   // another character or a reloaded world: the pointers go
        realDrop();
        g_real->player = player;
    }
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    const TqSack* sack = (const TqSack*)((unsigned char*)ge + g_tq.transferOff);
    utGuardEnter();
    const int n = realWalkSeh(sack, g_realWalk);
    utGuardLeave();
    if (n < 0) {
        goOff(n == -2 ? "the Transfer sack's item map read" : "the Transfer sack's item map shape");
        return false;
    }
    bool built = false;
    if (!g_real->ready ||
        !utSackSameIds(g_realWalk, n, g_real->ids.data(), (int)g_real->ids.size())) {
        // a deposit, a take, a pick-up, a sort into new ids: resolve again; an item still there
        // (the same id, pointer and record) keeps its text, the others are read again
        std::vector<RealItem> kept;
        kept.swap(g_real->items);
        realDrop();
        for (int i = 0; i < n; ++i) g_realObjs[i] = nullptr;
        TqPtrVector v = {nullptr, nullptr, nullptr};
        int found = 0;
        utGuardEnter();
        const int r = realResolveSeh(&v, g_realWalk, n, &found);
        utGuardLeave();
        utGuardEnter();
        const bool freed = realFreeSeh(&v);
        utGuardLeave();
        if (r < 0 || !freed) {
            goOff(r == -2   ? "the Transfer page's object walk"
                  : r == -1 ? "the Transfer page's object list shape"
                            : "the Transfer page's object list free");
            return false;
        }
        if (r == 0) {   // no object manager yet: again next Update
            realPublish(false);
            return false;
        }
        try {
            g_real->items.resize((size_t)n);
            g_real->ids.resize((size_t)n);
            size_t k = 0;   // both ascending
            for (int i = 0; i < n; ++i) {
                RealItem& it = g_real->items[(size_t)i];
                it.id = g_realWalk[i].id;
                it.item = g_realObjs[i];
                it.record = g_realObjs[i] ? g_realNames[i] : "";
                it.text.clear();
                it.state = g_realObjs[i] ? kPending : kUnindexable;
                it.match = false;
                g_real->ids[(size_t)i] = g_realWalk[i].id;
                while (k < kept.size() && kept[k].id < it.id) ++k;
                if (it.item && k < kept.size() && kept[k].id == it.id && kept[k].item == it.item &&
                    kept[k].state != kPending && kept[k].record == it.record) {
                    it.text.swap(kept[k].text);
                    it.state = kept[k].state;
                }
                if (it.state != kPending) ++g_real->done;
            }
        } catch (...) {
            realDrop();
            goOff("the Transfer page's cache allocation");
            return false;
        }
        g_real->ready = true;
        built = true;
        (void)found;
    }
    // the reads, 1.5 ms per Update at most (at least one item)
    bool worked = false;
    while (g_real->next < g_real->items.size()) {
        RealItem& it = g_real->items[g_real->next++];
        if (it.state != kPending) continue;
        std::string text;
        UtSearchCounts c = {};
        Capture cap = {"", ""};
        const int rc = captureItem((TqItem*)it.item, player, it.record.c_str(), &text, &c, &cap);
        if (rc == kCapFault) {
            realDrop();
            goOff(cap.where);
            return false;
        }
        it.text.swap(text);
        it.state = rc == kCapOk ? kIndexed : kUnindexable;
        ++g_real->done;
        g_real->matchesDirty = true;
        worked = true;
        if (msSince(t0) >= kBackgroundBudgetMs) break;
    }
    if (worked || built) g_real->ms += msSince(t0);
    const bool complete = g_real->done >= (int)g_real->items.size();
    if (complete && (worked || built) && !g_realSaid) {
        g_realSaid = true;
        logI("search: the Transfer page's items are read - %d item(s) in %.1f ms",
             (int)g_real->items.size(), g_real->ms);
    }
    // the matches: the same fold and substring test as the collection's
    if (g_real->matchesDirty || g_real->needle != *g_needle) {
        if (g_real->needle != *g_needle) {
            g_real->needle = *g_needle;
            g_real->querySaid = false;
        }
        g_real->matchesDirty = false;
        for (size_t i = 0; i < g_real->items.size(); ++i) {
            RealItem& it = g_real->items[i];
            it.match = g_active && it.state == kIndexed && utSearchHit(it.text, *g_needle);
        }
    }
    realPublish(true);
    if (complete && g_active && !g_real->querySaid) {
        g_real->querySaid = true;
        logD("search: \"%s\" - %ld of %d Transfer item(s)", g_needle->c_str(),
             InterlockedCompareExchange(&g_realFound, 0, 0), (int)g_real->items.size());
    }
    return worked;
}

// ---- the schedule ----------------------------------------------------------------------------
double msSince(const LARGE_INTEGER& t0) {
    LARGE_INTEGER t1, f;
    QueryPerformanceCounter(&t1);
    QueryPerformanceFrequency(&f);
    return f.QuadPart ? (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)f.QuadPart : 0.0;
}

bool matchOf(int idx) {
    return g_ix->state[(size_t)idx] == kIndexed && g_needle &&
           utSearchHit(g_ix->text[(size_t)idx], *g_needle);
}

void logQueryLine() {
    if (!g_active || !logWants(UT_LOG_DEBUG)) return;
    int groups = 0;
    const int m = liveSearchFound(&groups);
    int of = 0;
    const int shown = liveSearchShownIn(liveShownGroup(), &of);
    logD("search: \"%s\" - %d match(es) in %d group(s) (mask 0x%04X), shown group %s: %d of %d "
         "(OWN %s)",
         g_needle->c_str(), m, groups, liveSearchMarks(), liveGroupLabel(liveShownGroup()), shown,
         of, liveOwnedOnlyActive() ? "on" : "off");
}

void applyToPage() { liveSearchApply(g_active && !off(), off() ? 0u : indexedMask()); }

void sayIndexBuilt() {
    if (g_infoSaid || !g_ix || g_ix->processed < g_ix->total) return;
    g_infoSaid = true;
    logI("search: index built - %d records (%u unindexable), %u lines kept, %u dropped (0x11 x%u, "
         "0x0E x%u, 0x1C x%u), %u KB, %.0f ms total (mean %.0f us, max %.0f us at %s)",
         g_ix->total, g_unindexable, g_linesKept, g_dropReq + g_dropLore + g_dropDir, g_dropReq,
         g_dropLore, g_dropDir, (unsigned)((g_bytes + 1023) / 1024), g_totalMs,
         g_ix->total ? g_totalMs * 1000.0 / (double)g_ix->total : 0.0, g_maxUs,
         g_maxRec[0] ? g_maxRec : "-");
}

// Indexes record k of group g. False = the search went OFF. `inBuild`: the page build asked.
bool indexRecord(int g, int k, const void* player, const char* how, bool inBuild) {
    const int idx = g_ix->base[(size_t)g] + k;
    if (g_ix->state[(size_t)idx] != kPending) return true;
    const char* record = liveGroupRecord(g, k);
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    std::string text;
    UtSearchCounts c = {};
    Capture cap = {"", ""};
    const int rc = record ? captureOne(record, player, &text, &c, &cap) : kCapUnindexable;
    if (!record) cap.why = "no record at this place";
    if (rc == kCapFault) {
        goOff(cap.where, inBuild);
        return false;
    }
    const double ms = msSince(t0);
    g_totalMs += ms;
    g_ix->groupMs[(size_t)g] += ms;
    if (ms * 1000.0 > g_maxUs) {
        g_maxUs = ms * 1000.0;
        _snprintf_s(g_maxRec, sizeof(g_maxRec), _TRUNCATE, "%s", record ? record : "?");
    }
    if (rc == kCapOk) {
        g_ix->text[(size_t)idx].swap(text);
        g_ix->state[(size_t)idx] = kIndexed;
        g_linesKept += c.kept;
        g_dropReq += c.dropReq;
        g_dropLore += c.dropLore;
        g_dropDir += c.dropDir;
        g_bytes += g_ix->text[(size_t)idx].size() + 1;
    } else {
        g_ix->state[(size_t)idx] = kUnindexable;
        ++g_unindexable;
        if (g_unindexableSaid < 8) {
            ++g_unindexableSaid;
            logD("search: %s is unindexable - %s", record ? record : "?", cap.why);
        }
    }
    liveSearchSetMatch(g, k, matchOf(idx));
    ++g_ix->processed;
    InterlockedExchange(&g_labelDone, g_ix->processed);
    if (++g_ix->done[(size_t)g] >= liveGroupEntries(g)) {
        logD("search: group %s indexed - %d in %.1f ms (%s)", liveGroupLabel(g),
             liveGroupEntries(g), g_ix->groupMs[(size_t)g], how);
        applyToPage();   // its mark may light now
        sayIndexBuilt();
        if (g_ix->processed >= g_ix->total) logQueryLine();
    }
    return true;
}

bool indexGroup(int g, const void* player, const char* how) {
    const int n = liveGroupEntries(g);
    for (int k = 0; k < n; ++k) {
        if (!indexRecord(g, k, player, how, true)) return false;
    }
    return true;
}

// The field's text into `out` (at least kUtSearchFieldMax + 1); returns its length.
int fieldCopy(unsigned short* out) {
    AcquireSRWLockShared(&g_fieldLock);
    const int n = g_field.len;
    memcpy(out, g_field.text, sizeof(g_field.text));
    ReleaseSRWLockShared(&g_fieldLock);
    return n;
}

// The ini key's text (UTF-8) replaces the field's.
void fieldSetUtf8(const char* s) {
    std::vector<unsigned short> w;
    try {
        utSearchUtf8ToWide(s, &w);
    } catch (...) {
        w.clear();
    }
    AcquireSRWLockExclusive(&g_fieldLock);
    utSearchFieldClear(&g_field);
    if (!w.empty()) utSearchFieldType(&g_field, w.data(), (int)w.size());
    ReleaseSRWLockExclusive(&g_fieldLock);
    InterlockedIncrement(&g_fieldGen);
}

std::string wideUtf8(const unsigned short* s, int n) {
    std::string out;
    for (int i = 0; i < n && s[i]; ++i) {
        if (s[i] < 0x20) out += '?';
        else utSearchPutUtf8(&out, s[i]);
    }
    return out;
}

// One key event's fields, read inside its own SEH frame (pure reads and the engine's GetText,
// which is `lea eax,[ecx+0x24]; ret`).
struct KeyRead {
    int button, state, n;
    unsigned short text[16];
};
bool readKeySeh(const void* ev, KeyRead* k) {
    __try {
        const unsigned char* e = (const unsigned char*)ev;
        k->button = *(const int*)(e + 0x0C);
        k->state = *(const int*)(e + 0x10);
        k->n = 0;
        if (k->state != kUtKeyStateRelease && g_tq.ButtonEventGetText) {
            const unsigned short* t = (const unsigned short*)g_tq.ButtonEventGetText(ev);
            for (int i = 0; t && i < 15 && t[i]; ++i) k->text[k->n++] = t[i];
        }
        k->text[k->n] = 0;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The paths that end the field's visibility drop its focus, every Update.
void fieldTick(bool worldUp) {
    if (!InterlockedCompareExchange(&g_focus, 0, 0)) return;
    const char* why = nullptr;
    if (off()) why = "the search is off (a fault)";
    else if (!g_cfg.search) why = "search=0 in uniquetab.ini";
    else if (!worldUp) why = "the world was unloaded";
    else if (!hookCaravanOpen()) why = "the caravan was closed";
    else if (!viewOn() && !g_cfg.searchTransfer) why = "the view was turned OFF (search_transfer=0)";
    else if (!plateTransferVisible()) why = "the Transfer page is not shown";
    else if (!panelSearchFieldShown()) why = "the field is not drawn (the pad is not live)";
    else if (utSearchFieldIdle(GetTickCount(), (unsigned)InterlockedCompareExchange(&g_lastKey, 0, 0)))
        why = "60 s without a key (the watchdog)";
    if (why) {
        searchFieldBlur(why);
        return;
    }
    // Backspace held: the repeat the engine does not send (400 ms, then one every 40 ms)
    // (the key's own state ends it too, should its release event never arrive)
    const DWORD now = GetTickCount();
    const bool backDown = (GetAsyncKeyState(VK_BACK) & 0x8000) != 0;
    bool changed = false;
    AcquireSRWLockExclusive(&g_fieldLock);
    utBackRepeatPoll(&g_back, backDown);
    if (utBackRepeatStep(&g_back, (unsigned)now)) {
        changed = utSearchFieldErase(&g_field);
        if (!changed || g_field.len == 0) utBackRepeatRelease(&g_back);
    }
    ReleaseSRWLockExclusive(&g_fieldLock);
    if (changed) {
        InterlockedExchange(&g_lastKey, (LONG)now);
        InterlockedIncrement(&g_fieldGen);
    }
}

bool haveBindings() {
    return g_tq.CrtOperatorDelete && g_tq.ItemCreateItem && g_tq.ObjectManagerGet &&
           g_tq.ObjectManagerDestroyObjectEx && g_tq.ObjectGetObjectId && g_tq.GameGetMainPlayer;
}

bool startIndex(const char* why, bool inBuild) {
    if (g_started) return true;
    if (!haveBindings()) {
        goOff("a missing engine binding (CreateItem, DestroyObjectEx or operator delete)", inBuild);
        return false;
    }
    g_started = true;
    logD("search: the index starts (%s) - %d records; the shown group now, the rest %.1f ms "
         "per Update",
         why, g_ix->total, kBackgroundBudgetMs);
    return true;
}

// The wanted group indexed now, whole (a focus or a query: its highlight is there at once).
bool syncGroup(int group, const char* how, bool inBuild) {
    if (groupComplete(group)) return true;
    bool playerFault = false;
    const void* player = mainPlayer(&playerFault);
    if (playerFault) {
        goOff("the GameGetMainPlayer call", inBuild);
        return false;
    }
    if (!player) return true;   // the menu, a world change: the background finishes it later
    return indexGroup(group, player, how);
}

}  // namespace

void searchTick(bool worldUp) {
    if (InterlockedExchange(&g_gateFault, 0)) goOff("the key gate's event read");
    fieldTick(worldUp);   // the blur paths, before anything returns
    if (!liveActive() || off()) return;
    if (!g_cfg.search && !g_active) return;   // search=0: inert (a standing query is let go below)
    if (!ensureIndex()) return;
    // the ini key fills the field when its value changes (configReload reads it once a second)
    if (!g_rawInit || strcmp(g_raw, g_cfg.searchDebugQuery) != 0) {
        const bool first = !g_rawInit;
        g_rawInit = true;
        _snprintf_s(g_raw, sizeof(g_raw), _TRUNCATE, "%s", g_cfg.searchDebugQuery);
        if (!first || g_raw[0]) fieldSetUtf8(g_raw);
    }
    // the query: the field's text, folded (a change highlights again; the page is not rebuilt)
    const LONG gen = InterlockedCompareExchange(&g_fieldGen, 0, 0);
    bool startSync = false;
    const char* syncHow = "sync at a query";
    if (gen != g_fieldSeen || g_active != (g_cfg.search != 0 && !g_needle->empty())) {
        g_fieldSeen = gen;
        unsigned short buf[kUtSearchFieldMax + 1];
        const int n = fieldCopy(buf);
        std::string needle;
        try {
            needle = utSearchNeedleWide(buf, (size_t)n);
        } catch (...) {
            needle.clear();
        }
        const bool active = g_cfg.search != 0 && !needle.empty();
        const bool changed = active != g_active || (active && needle != *g_needle);
        g_needle->swap(needle);
        if (changed) {
            g_active = active;
            InterlockedExchange(&g_labelOn, active ? 1 : 0);
            for (int g = 0; g < liveGroupCount(); ++g) {
                for (int k = 0; k < liveGroupEntries(g); ++k)
                    liveSearchSetMatch(g, k, active && matchOf(g_ix->base[(size_t)g] + k));
            }
            applyToPage();   // the highlight and the marks follow; nothing is laid out again
            if (!active) {
                logD("search: no query - nothing is highlighted");
            } else {
                if (!g_started && !startIndex("the first query", false)) return;
                startSync = true;
                g_queryLinePending = !groupComplete(liveWantedGroup());
                if (!g_queryLinePending) logQueryLine();
            }
        }
    }
    // the real Transfer page (the view OFF): its items, read with the same query
    const bool realBusy = realTick(worldUp);
    // the field's first focus starts the index (search_prebuild aside) and indexes the shown group
    if (InterlockedExchange(&g_wantStart, 0) && g_cfg.search) {
        if (!g_started && !startIndex("the field's first focus", false)) return;
        if (!startSync) syncHow = g_ix->processed == 0 ? "sync at focus" : "sync at a focus";
        startSync = true;
    }
    if (startSync && g_started && worldUp) {
        if (!syncGroup(liveWantedGroup(), syncHow, false)) return;
        if (g_queryLinePending && groupComplete(liveWantedGroup())) {
            g_queryLinePending = false;
            applyToPage();
            logQueryLine();
        }
    }
    // the background: every other group, 1.5 ms per Update at most (not while the real page reads)
    if (realBusy || !g_started || !g_cfg.search || !worldUp || g_ix->processed >= g_ix->total)
        return;
    if (!haveBindings()) {
        goOff("a missing engine binding (CreateItem, DestroyObjectEx or operator delete)");
        return;
    }
    bool playerFault = false;
    const void* player = mainPlayer(&playerFault);
    if (playerFault) {
        goOff("the GameGetMainPlayer call");
        return;
    }
    if (!player) return;   // the menu, a world change: paused, the cache is kept
    LARGE_INTEGER t0;
    QueryPerformanceCounter(&t0);
    while (g_ix->next < g_ix->total) {
        int g = 0;
        while (g + 1 < (int)g_ix->base.size() && g_ix->base[(size_t)g + 1] <= g_ix->next) ++g;
        const int k = g_ix->next - g_ix->base[(size_t)g];
        ++g_ix->next;
        if (g_ix->state[(size_t)(g_ix->base[(size_t)g] + k)] != kPending) continue;
        if (!indexRecord(g, k, player, "background", false)) return;
        if (msSince(t0) >= kBackgroundBudgetMs) break;
    }
}

void searchBeforePage(int group) {
    if (!liveActive() || off() || !g_cfg.search) return;
    if (!ensureIndex()) return;
    if (!g_started) {
        if (!g_active && !g_cfg.searchPrebuild) return;
        if (!startIndex(g_active ? "the first query" : "search_prebuild=1, the first view-ON", true))
            return;
    }
    if (!g_active) return;   // search_prebuild alone: the background builds it
    if (!syncGroup(group, g_ix->processed == 0 ? "sync at the first query" : "sync at a switch",
                   true))
        return;
    if (g_queryLinePending && groupComplete(group)) {
        g_queryLinePending = false;
        applyToPage();
        logQueryLine();
    }
}

unsigned searchMarks() {
    if (!InterlockedCompareExchange(&g_labelOn, 0, 0) || !g_cfg.searchButtons) return 0;
    return liveSearchMarks();
}

bool searchQueryStands() { return InterlockedCompareExchange(&g_labelOn, 0, 0) != 0; }

bool searchFieldWanted() { return g_cfg.search != 0 && !off() && hookKeyGateLive() && liveActive(); }

bool searchFieldFocus() {
    if (!searchFieldWanted()) return false;
    InterlockedExchange(&g_lastKey, (LONG)GetTickCount());
    if (InterlockedExchange(&g_focus, 1)) return true;   // it had it already
    InterlockedExchange(&g_wantStart, 1);   // the next Update starts the index and syncs the group
    logI("search: the field has the focus - type to search; Enter keeps the query, Esc clears it");
    return true;
}

void searchFieldOffClick() {
    if (g_offClickSaid) return;
    g_offClickSaid = true;
    const char* why = !g_cfg.search   ? "search=0 in uniquetab.ini"
                      : off()         ? "the search went off after a fault (the ERROR line above)"
                      : !hookKeyGateLive()
                          ? "the key gate is not installed (search was 0 when the game started, or "
                            "its hook failed)"
                      : !liveActive() ? "the collection is not active"
                                      : "it cannot take the focus now";
    logI("search: the field is off - %s", why);
}

bool searchFieldClearQuery() {
    AcquireSRWLockExclusive(&g_fieldLock);
    const bool changed = utSearchFieldEscape(&g_field);   // Esc on a non-empty field
    ReleaseSRWLockExclusive(&g_fieldLock);
    if (!changed) return false;
    InterlockedIncrement(&g_fieldGen);   // the tick recomputes the highlight and the group marks
    if (InterlockedCompareExchange(&g_focus, 0, 0))
        InterlockedExchange(&g_lastKey, (LONG)GetTickCount());
    logD("search: the query cleared by the clear button");
    return true;
}

void searchFieldBlur(const char* why) {
    if (!InterlockedExchange(&g_focus, 0)) return;
    AcquireSRWLockExclusive(&g_fieldLock);
    utBackRepeatRelease(&g_back);   // a held Backspace stops with the focus
    ReleaseSRWLockExclusive(&g_fieldLock);
    unsigned short buf[kUtSearchFieldMax + 1];
    const int n = fieldCopy(buf);
    std::string q;
    try {
        q = wideUtf8(buf, n);
    } catch (...) {
        q.clear();
    }
    logI("search: the field lost the focus - %s (the field holds \"%s\")", why ? why : "?",
         q.c_str());
}

bool searchFieldFocused() { return InterlockedCompareExchange(&g_focus, 0, 0) != 0; }

int searchFieldText(wchar_t* out, int cap) {
    if (!out || cap < 1) return 0;
    unsigned short buf[kUtSearchFieldMax + 1];
    int n = fieldCopy(buf);
    if (n > cap - 1) n = cap - 1;
    for (int i = 0; i < n; ++i) out[i] = (wchar_t)buf[i];
    out[n] = 0;
    return n;
}

bool searchKeyGate(const void* ev) {
    if (!InterlockedCompareExchange(&g_focus, 0, 0)) return false;   // unfocused: nothing at all
    if (!ev || off()) {
        searchFieldBlur(off() ? "the search is off (a fault)" : "a key event with no object");
        return false;
    }
    KeyRead k = {};
    if (!readKeySeh(ev, &k)) {
        InterlockedExchange(&g_gateFault, 1);   // the next Update turns the search off
        searchFieldBlur("a key event could not be read (a fault)");
        return false;   // the event goes on to the game
    }
    // the Update may not run (a loading screen): the page and the watchdog are checked here too
    const DWORD now = GetTickCount();
    const char* why = !viewOn() && !g_cfg.searchTransfer
                          ? "the view was turned OFF (search_transfer=0)"
                      : !plateTransferVisible()   ? "the Transfer page is not shown"
                      : utSearchFieldIdle(now, (unsigned)InterlockedCompareExchange(&g_lastKey, 0, 0))
                          ? "60 s without a key (the watchdog)"
                          : nullptr;
    if (why) {
        searchFieldBlur(why);
        return false;
    }
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const int act = utSearchKeyAction(true, k.state, k.button, ctrl);
    if (act == kUtKeyPass) {   // a release: the game's held-key bookkeeping sees it
        if (k.button == kUtKeyBack) {   // Backspace let go: the repeat stops
            AcquireSRWLockExclusive(&g_fieldLock);
            utBackRepeatRelease(&g_back);
            ReleaseSRWLockExclusive(&g_fieldLock);
        }
        if (!g_stateSaid) {   // only state 0 (a press) is known: the first other state is said
            g_stateSaid = true;
            logD("search: the first key event in the field that is not a press - button 0x%02X, "
                 "state %d (passed on to the game)",
                 (unsigned)k.button & 0xFFu, k.state);
        }
        return false;
    }
    InterlockedExchange(&g_lastKey, (LONG)now);
    if (k.state != kUtKeyStatePress && !g_repeatSaid) {   // neither a press nor a release
        g_repeatSaid = true;
        logD("search: a key event in the field with state %d - button 0x%02X (neither a press nor "
             "a release: taken by the field, not passed on to the game)",
             k.state, (unsigned)k.button & 0xFFu);
    }
    if (!g_keySaid) {
        g_keySaid = true;
        std::string t;
        try {
            t = wideUtf8(k.text, k.n);
        } catch (...) {
            t.clear();
        }
        logD("search: the first key in the field - button 0x%02X, GetText \"%s\" (%d character(s))",
             (unsigned)k.button & 0xFFu, t.c_str(), k.n);
    }
    bool changed = false, blur = false;
    const char* blurWhy = nullptr;
    AcquireSRWLockExclusive(&g_fieldLock);
    switch (act) {
    case kUtKeyEscape:
        changed = utSearchFieldEscape(&g_field);
        blur = !changed;
        blurWhy = "Esc on an empty field";
        break;
    case kUtKeyErase:
        changed = utSearchFieldErase(&g_field);
        break;
    case kUtKeyClear:
        changed = utSearchFieldClear(&g_field);
        break;
    case kUtKeyEnter:
        blur = true;
        blurWhy = "Enter (the query is kept)";
        break;
    case kUtKeySwallow:   // Esc or Return neither pressed nor released: nothing
        break;
    default:
        changed = utSearchFieldType(&g_field, k.text, k.n) > 0;
        break;
    }
    // Backspace held repeats from here (a repeat event from the engine starts the timer again);
    // any other key, Ctrl+Backspace or an empty field stops it
    if (act == kUtKeyErase && g_field.len > 0) utBackRepeatPress(&g_back, (unsigned)now);
    else if (act != kUtKeySwallow) utBackRepeatRelease(&g_back);
    ReleaseSRWLockExclusive(&g_fieldLock);
    if (changed) InterlockedIncrement(&g_fieldGen);
    if (blur) searchFieldBlur(blurWhy);
    return true;   // every press is the field's while it has the focus
}

int searchRealMarks(UtSackEntry* out, int cap) {
    if (!out || cap <= 0 || !InterlockedCompareExchange(&g_labelOn, 0, 0)) return 0;
    AcquireSRWLockShared(&g_realLock);
    const int n = g_realMarkN < cap ? g_realMarkN : cap;
    for (int i = 0; i < n; ++i) out[i] = g_realMarks[i];
    ReleaseSRWLockShared(&g_realLock);
    return n;
}

void searchRealLabel(char* out, size_t cap, char* brief, size_t briefCap) {
    utSearchRealWords(InterlockedCompareExchange(&g_labelOn, 0, 0) != 0,
                      InterlockedCompareExchange(&g_realReady, 0, 0) != 0,
                      (int)InterlockedCompareExchange(&g_realDone, 0, 0),
                      (int)InterlockedCompareExchange(&g_realTotal, 0, 0),
                      (int)InterlockedCompareExchange(&g_realFound, 0, 0), out, cap, brief, briefCap);
}

bool searchHighlight(int i, const char* record) {
    return g_active && !off() && liveSearchHighlight(i, record);
}

void searchLabel(char* out, size_t cap, char* brief, size_t briefCap) {
    if (brief && briefCap) brief[0] = 0;
    if (!out || !cap) return;
    out[0] = 0;
    if (!InterlockedCompareExchange(&g_labelOn, 0, 0)) return;
    const LONG done = InterlockedCompareExchange(&g_labelDone, 0, 0);
    const LONG total = InterlockedCompareExchange(&g_labelTotal, 0, 0);
    if (done < total) {
        _snprintf_s(out, cap, _TRUNCATE, "indexing %ld/%ld", done, total);
        if (brief && briefCap) _snprintf_s(brief, briefCap, _TRUNCATE, "%ld/%ld", done, total);
    } else {
        const int n = liveSearchFound(nullptr);
        _snprintf_s(out, cap, _TRUNCATE, "found %d", n);
        if (brief && briefCap) _snprintf_s(brief, briefCap, _TRUNCATE, "=%d", n);
    }
}

}  // namespace ut
