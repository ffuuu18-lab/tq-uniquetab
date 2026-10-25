// ut_mem.h - the memory lines: one INFO line "mem: <point> | ..." at the points that change the
// process's memory (the DLL load, the start-up builds' end, every world up and gone, every game
// switch both ways and the point before a custom game's database loads, the search index's start,
// its end and each whole-group sync, the caravan's close), so two sittings - the tab ON and
// enabled=0 - compare field by field.
//
// The fields, in the line's order:
//   space   - the user address space (VirtualQuery): the largest free block, all free blocks, and
//             what is not free by type: image, mapped, private committed, private reserved only;
//   process - GetProcessMemoryInfo: PrivateUsage and WorkingSetSize;
//   heap    - HeapSummary(GetProcessHeap()): reserved, committed, allocated (the engine's
//             MSVCR110 and this DLL's CRT both allocate from it);
//   engine  - read on the game thread only, pure reads under one guarded frame: the record cache's
//             size, the objects, the objects waiting for deletion, and the 7 resource managers'
//             bytes (the ones Engine::Update walks);
//   mod     - the search index's text, the live prototypes, the gray icons held, the retired
//             tables still held;
//   arena   - while the start-up builds' arena is reserved (gen_heap.h, any thread): its size,
//             base, the high-water of its builds so far and what is committed now. Its release
//             is a line of this form too: "mem: <point> | space: ... | arena: <size> at <base>
//             (top-down), high-water ..., released (<why>): the largest free address block
//             before -> after the release, the free total before -> after (and both before the
//             reserve)".
// The search's lines carry the change since the index started.
//
// The pure half (the walk, the line, the delta) is here; test_viewgate checks it. The rest is
// ut_mem.cpp.
#pragma once

#include <windows.h>

#include <stdio.h>

namespace ut {

struct UtMemSpace {
    unsigned long long largest = 0, freeTotal = 0;   // MEM_FREE
    unsigned long long image = 0, mapped = 0;        // MEM_IMAGE, MEM_MAPPED (committed or reserved)
    unsigned long long privCommit = 0, privReserve = 0;   // MEM_PRIVATE committed / reserved only
};

// VirtualQuery over the user address space (64 KB .. 2 GB - 64 KB), as utAddrSpace.
inline UtMemSpace utMemSpace() {
    UtMemSpace r;
    const unsigned char* p = (const unsigned char*)0x10000;
    const unsigned char* const end = (const unsigned char*)0x7FFF0000;
    MEMORY_BASIC_INFORMATION mbi;
    while (p < end && VirtualQuery(p, &mbi, sizeof(mbi)) == sizeof(mbi)) {
        const unsigned long long size = mbi.RegionSize;
        if (mbi.State == MEM_FREE) {
            r.freeTotal += size;
            if (size > r.largest) r.largest = size;
        } else if (mbi.Type == MEM_IMAGE) {
            r.image += size;
        } else if (mbi.Type == MEM_MAPPED) {
            r.mapped += size;
        } else if (mbi.Type == MEM_PRIVATE) {
            if (mbi.State == MEM_COMMIT) r.privCommit += size;
            else r.privReserve += size;
        }
        const unsigned char* next = (const unsigned char*)mbi.BaseAddress + mbi.RegionSize;
        if (next <= p) break;
        p = next;
    }
    return r;
}

enum : int {
    kUtMemEngineNotHere = 0,   // not the game thread: not read
    kUtMemEngineRead = 1,
    kUtMemEngineOff = -1,      // a read faulted earlier: off for the session
    kUtMemEngineNone = -2,     // no engine or no ObjectManager yet
};

struct UtMemSnap {
    UtMemSpace space;
    bool proc = false;
    unsigned long long privateUsage = 0, workingSet = 0;
    bool heap = false;
    unsigned long long heapReserved = 0, heapCommitted = 0, heapAllocated = 0;
    int engine = kUtMemEngineNotHere;
    unsigned records = 0, objects = 0, pending = 0;
    unsigned long long resources = 0;
    bool mod = false;   // the mod's figures are read on the game thread only
    unsigned long long indexBytes = 0, retired = 0;
    int protos = 0, gray = 0;
    // the build arena while it is reserved (arenaSize 0: none)
    unsigned long long arenaBase = 0, arenaSize = 0, arenaHighWater = 0, arenaCommitted = 0;
};

inline double utMemMb(unsigned long long b) { return (double)b / 1048576.0; }

// The line after "mem: ".
inline void utMemLine(char* out, size_t cap, const char* point, const UtMemSnap& s) {
    if (!out || !cap) return;
    int n = _snprintf_s(out, cap, _TRUNCATE,
                        "%s | space: largest %.0f MB, free %.0f MB, image %.1f MB, mapped %.1f MB, private "
                        "%.1f MB committed + %.1f MB reserved",
                        point ? point : "?", utMemMb(s.space.largest), utMemMb(s.space.freeTotal),
                        utMemMb(s.space.image), utMemMb(s.space.mapped), utMemMb(s.space.privCommit),
                        utMemMb(s.space.privReserve));
    size_t at = n < 0 ? cap - 1 : (size_t)n;
    auto add = [&](const char* fmt, auto... a) {
        if (at >= cap - 1) return;
        const int k = _snprintf_s(out + at, cap - at, _TRUNCATE, fmt, a...);
        at = k < 0 ? cap - 1 : at + (size_t)k;
    };
    if (s.proc)
        add(" | process: private %.1f MB, working set %.1f MB", utMemMb(s.privateUsage), utMemMb(s.workingSet));
    else
        add(" | process: not read");
    if (s.heap)
        add(" | heap: reserved %.1f MB, committed %.1f MB, allocated %.1f MB", utMemMb(s.heapReserved),
            utMemMb(s.heapCommitted), utMemMb(s.heapAllocated));
    else
        add(" | heap: not read");
    switch (s.engine) {
        case kUtMemEngineRead:
            add(" | engine: records %u, objects %u, pending %u, resources %.1f MB", s.records, s.objects,
                s.pending, utMemMb(s.resources));
            break;
        case kUtMemEngineOff: add(" | engine: off (a read faulted)"); break;
        case kUtMemEngineNone: add(" | engine: none yet"); break;
        default: add(" | engine: not read on this thread"); break;
    }
    if (s.mod)
        add(" | mod: index %.0f KB, prototypes %d, gray %d, retired tables %.2f MB",
            (double)s.indexBytes / 1024.0, s.protos, s.gray, utMemMb(s.retired));
    if (s.arenaSize)
        add(" | arena: %.0f MB at 0x%08llX (top-down), high-water %.1f MB, committed %.1f MB",
            utMemMb(s.arenaSize), s.arenaBase, utMemMb(s.arenaHighWater), utMemMb(s.arenaCommitted));
}

// The change since `base`: "since the index started: records +N, heap allocated +X MB, reserved
// +Y MB, resources +Z MB, pending peak P" (the engine's parts only when both read it).
inline void utMemDelta(char* out, size_t cap, const UtMemSnap& now, const UtMemSnap& base,
                       unsigned pendingPeak) {
    if (!out || !cap) return;
    const bool eng = now.engine == kUtMemEngineRead && base.engine == kUtMemEngineRead;
    const bool heap = now.heap && base.heap;
    char records[48] = "records ?", resources[48] = "resources ?";
    if (eng) {
        _snprintf_s(records, sizeof(records), _TRUNCATE, "records %+lld",
                    (long long)now.records - (long long)base.records);
        _snprintf_s(resources, sizeof(resources), _TRUNCATE, "resources %+.1f MB",
                    utMemMb(now.resources) - utMemMb(base.resources));
    }
    if (heap)
        _snprintf_s(out, cap, _TRUNCATE,
                    "since the index started: %s, heap allocated %+.1f MB, reserved %+.1f MB, %s, pending "
                    "peak %u",
                    records, utMemMb(now.heapAllocated) - utMemMb(base.heapAllocated),
                    utMemMb(now.heapReserved) - utMemMb(base.heapReserved), resources, pendingPeak);
    else
        _snprintf_s(out, cap, _TRUNCATE, "since the index started: %s, heap ?, %s, pending peak %u",
                    records, resources, pendingPeak);
}

// ---- ut_mem.cpp ------------------------------------------------------------------------------
// One "mem:" INFO line. `gameThread`: the engine and the mod's figures are read too. `tail`, when
// given, is the line's last part (the arena's release).
void memSay(const char* point, bool gameThread, const char* tail = nullptr);
// The search index starts (game thread): the base of the deltas, the pending peak reset.
void memIndexStart();
// The search's line (game thread): memSay's fields and the change since memIndexStart.
void memIndexSay(const char* point);
// While the index runs (game thread, once per Update): the objects waiting for deletion, for the
// peak. One pure read of a cached pointer; nothing is allocated.
void memPendingSample();

}  // namespace ut
