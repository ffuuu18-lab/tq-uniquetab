// ut_mem.cpp - the memory lines (see ut_mem.h). The engine's counters are read on the game thread
// only, in one guarded frame of pure reads after ObjectManager's singleton getter; a fault turns
// them off for the session with one ERROR line (the rest of the line goes on). Nothing here is
// allocated: the line is built on the stack.
//
// The engine's addresses (Engine.dll, AE 2.10):
//   ObjectManager (the singleton): +0x24 its objects (GetNumObjects 0x1E6D80), +0x80 / +0x84 the
//     vector of objects waiting for deletion, 32 bytes each (GetNumDeletedObjects 0x1E6D70), +0x94
//     the record cache (TableDepot, ObjectManager::ClearCache 0x1E6D90), whose list size at
//     +0x9C is the records held and whose +0x28 (+0xBC) counts the unique files it loaded
//     (TableDepot::DumpStats 0x226A70);
//   the 7 resource managers Engine::Update walks (0x14447B-0x1444BD): [[Engine+0x160]+0x28],
//     +0x2C, +0x30, +0x24, +0x1C; [Engine+0x158]; [Engine+0x38]+0x518 - each one's bytes at +8
//     (BaseResourceManager::GetMemoryUsage 0x11F810 is `mov eax,[ecx+8]`).
// The first read logs every pointer and value it used (INFO, once a session).

#include "ut_mem.h"

#include <windows.h>

#include <psapi.h>

#include "gen/gen_heap.h"
#include "tq_runtime.h"
#include "ut_live.h"
#include "ut_log.h"
#include "ut_panel.h"
#include "ut_proto.h"
#include "ut_search.h"

namespace ut {
namespace {

const unsigned kMgrInner[5] = {0x28, 0x2C, 0x30, 0x24, 0x1C};   // of [Engine+0x160]

struct EngineRead {
    const void* om;
    const void* engine;
    const void* mgr[7];
    unsigned usage[7];
    unsigned records, objects, pending, loads;
};

volatile LONG g_engineOff = 0;   // a read faulted: off for the session
bool g_inputsSaid = false;       // game thread
const void* g_om = nullptr;      // game thread: the singleton, for the pending sample
UtMemSnap g_base;                // game thread: memIndexStart's snapshot
bool g_baseSet = false;
unsigned g_pendingPeak = 0;

// 1 = read, 0 = no engine or no ObjectManager yet, -1 = a fault. `om`: the singleton, got by the
// caller outside this frame (pure reads only in here).
int engineReadSeh(const void* om, EngineRead* r) {
    __try {
        const unsigned char* e = (const unsigned char*)*g_tq.ppEngine;
        r->om = om;
        r->engine = e;
        if (!om || !e) return 0;
        const unsigned char* o = (const unsigned char*)om;
        r->objects = *(const unsigned*)(o + 0x24);
        r->pending = (unsigned)((*(const unsigned char* const*)(o + 0x84) -
                                 *(const unsigned char* const*)(o + 0x80)) >> 5);
        r->records = *(const unsigned*)(o + 0x9C);
        r->loads = *(const unsigned*)(o + 0x94 + 0x28);
        const unsigned char* inner = *(const unsigned char* const*)(e + 0x160);
        for (int i = 0; i < 5; ++i)
            r->mgr[i] = inner ? *(const void* const*)(inner + kMgrInner[i]) : nullptr;
        r->mgr[5] = *(const void* const*)(e + 0x158);
        const unsigned char* p38 = *(const unsigned char* const*)(e + 0x38);
        r->mgr[6] = p38 ? p38 + 0x518 : nullptr;
        for (int i = 0; i < 7; ++i)
            r->usage[i] = r->mgr[i] ? *(const unsigned*)((const unsigned char*)r->mgr[i] + 8) : 0;
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

int pendingSeh(const void* om, unsigned* out) {
    __try {
        const unsigned char* o = (const unsigned char*)om;
        *out = (unsigned)((*(const unsigned char* const*)(o + 0x84) - *(const unsigned char* const*)(o + 0x80)) >> 5);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

void engineOff(const char* what) {
    if (InterlockedExchange(&g_engineOff, 1)) return;
    logE("mem: %s faulted - the engine's counters are off in the memory lines for this session", what);
}

void readEngine(UtMemSnap* s) {
    if (InterlockedCompareExchange(&g_engineOff, 0, 0)) {
        s->engine = kUtMemEngineOff;
        return;
    }
    if (!g_tq.ObjectManagerGet || !g_tq.ppEngine) {
        s->engine = kUtMemEngineNone;
        return;
    }
    // the singleton's getter is an engine call: made outside the guarded frame, once (it never
    // changes), so a fault in it is never turned into "counters off"
    if (!g_om) g_om = g_tq.ObjectManagerGet();
    EngineRead r = {};
    utGuardEnter();
    const int rc = engineReadSeh(g_om, &r);
    utGuardLeave();
    if (rc < 0) {
        engineOff("the engine counters' read");
        s->engine = kUtMemEngineOff;
        return;
    }
    if (rc == 0) {
        s->engine = kUtMemEngineNone;
        return;
    }
    g_om = r.om;
    s->engine = kUtMemEngineRead;
    s->records = r.records;
    s->objects = r.objects;
    s->pending = r.pending;
    s->resources = 0;
    for (int i = 0; i < 7; ++i) s->resources += r.usage[i];
    if (!g_inputsSaid) {
        g_inputsSaid = true;
        logI("mem: the engine counters' inputs - ObjectManager %p: records [+0x9C] %u, unique files loaded "
             "[+0xBC] %u, objects [+0x24] %u, pending ([+0x84]-[+0x80])/32 %u; Engine %p: resource managers "
             "[[+0x160]+0x28] %p %u B, +0x2C %p %u B, +0x30 %p %u B, +0x24 %p %u B, +0x1C %p %u B, [+0x158] "
             "%p %u B, [+0x38]+0x518 %p %u B (each one's [+8])",
             r.om, r.records, r.loads, r.objects, r.pending, r.engine, r.mgr[0], r.usage[0], r.mgr[1],
             r.usage[1], r.mgr[2], r.usage[2], r.mgr[3], r.usage[3], r.mgr[4], r.usage[4], r.mgr[5],
             r.usage[5], r.mgr[6], r.usage[6]);
    }
}

void readProcess(UtMemSnap* s) {
    typedef BOOL(WINAPI * FnPmc)(HANDLE, PROCESS_MEMORY_COUNTERS*, DWORD);
    struct HeapSummaryT {
        DWORD cb;
        SIZE_T cbAllocated, cbCommitted, cbReserved, cbMaxReserve;
    };
    typedef BOOL(WINAPI * FnHs)(HANDLE, DWORD, HeapSummaryT*);
    static FnPmc pmc = nullptr;
    static FnHs hs = nullptr;
    static bool resolved = false;
    if (!resolved) {   // kernel32's own exports (Windows 7 and later), resolved once
        HMODULE k32 = GetModuleHandleA("kernel32.dll");
        pmc = reinterpret_cast<FnPmc>(
            reinterpret_cast<void*>(k32 ? GetProcAddress(k32, "K32GetProcessMemoryInfo") : nullptr));
        hs = reinterpret_cast<FnHs>(reinterpret_cast<void*>(k32 ? GetProcAddress(k32, "HeapSummary") : nullptr));
        resolved = true;
    }
    PROCESS_MEMORY_COUNTERS_EX c = {};
    c.cb = sizeof(c);
    if (pmc && pmc(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&c, sizeof(c))) {
        s->proc = true;
        s->privateUsage = c.PrivateUsage;
        s->workingSet = c.WorkingSetSize;
    }
    HeapSummaryT h = {};
    h.cb = sizeof(h);
    if (hs && hs(GetProcessHeap(), 0, &h)) {
        s->heap = true;
        s->heapReserved = h.cbReserved;
        s->heapCommitted = h.cbCommitted;
        s->heapAllocated = h.cbAllocated;
    }
}

void snap(UtMemSnap* s, bool gameThread) {
    s->space = utMemSpace();
    readProcess(s);
    const gen::ArenaInfo a = gen::arenaInfo();   // the build arena while it is reserved
    if (a.base) {
        s->arenaBase = (unsigned long long)(uintptr_t)a.base;
        s->arenaSize = a.size;
        s->arenaHighWater = a.highWater;
        s->arenaCommitted = a.committed;
    }
    if (!gameThread) return;
    readEngine(s);
    s->mod = true;
    s->indexBytes = searchIndexBytes();
    s->protos = protoCount();
    s->gray = panelGrayHeld();
    s->retired = liveRetiredBytes() + panelRetiredBytes();
}

}  // namespace

void memSay(const char* point, bool gameThread, const char* tail) {
    UtMemSnap s;
    snap(&s, gameThread);
    char line[1024];
    utMemLine(line, sizeof(line), point, s);
    if (tail && tail[0]) logI("mem: %s | %s", line, tail);
    else logI("mem: %s", line);
}

void memIndexStart() {
    g_base = UtMemSnap();
    snap(&g_base, true);
    g_baseSet = true;
    g_pendingPeak = g_base.engine == kUtMemEngineRead ? g_base.pending : 0;
    char line[1024];
    utMemLine(line, sizeof(line), "search: the index starts", g_base);
    logI("mem: %s", line);
}

void memIndexSay(const char* point) {
    UtMemSnap s;
    snap(&s, true);
    if (s.engine == kUtMemEngineRead && s.pending > g_pendingPeak) g_pendingPeak = s.pending;
    char line[1024], delta[256];
    utMemLine(line, sizeof(line), point, s);
    if (g_baseSet) utMemDelta(delta, sizeof(delta), s, g_base, g_pendingPeak);
    else _snprintf_s(delta, sizeof(delta), _TRUNCATE, "no start reading");
    logI("mem: %s | %s", line, delta);
}

void memPendingSample() {
    if (!g_om || InterlockedCompareExchange(&g_engineOff, 0, 0)) return;
    unsigned n = 0;
    utGuardEnter();
    const int rc = pendingSeh(g_om, &n);
    utGuardLeave();
    if (rc < 0) {
        engineOff("the pending objects' read");
        return;
    }
    if (n > g_pendingPeak) g_pendingPeak = n;
}

}  // namespace ut
