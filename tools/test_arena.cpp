// test_arena.cpp - the build arena (src\gen\gen_heap.cpp), offline: the reserve (top-down, nothing
// committed), a pair's blocks inside the range and another thread's outside it, the allocator
// (reuse, splitting, merging, a random churn checked byte for byte, every block merged back at
// the end), the reset (decommitted; the next pair starts at the base again), the outgrow path
// (std::bad_alloc, genHeapOutgrown, the next pair unharmed), a free from another thread (refused,
// the block intact until the reset), the release (one piece; refused while a pair holds it) and
// the private heap of a pair without an arena. No game; writes nothing.
//
//   test_arena.exe
#include "gen/gen_heap.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <vector>

namespace {

int g_fail = 0, g_pass = 0;

void check(bool ok, const std::string& what) {
    std::printf("[test] %s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (ok) ++g_pass;
    else ++g_fail;
}

bool inRange(const void* p, const gen::ArenaInfo& a) {
    return (std::uintptr_t)p - (std::uintptr_t)a.base < a.size;
}

// The state of the pages at p (MEM_COMMIT, MEM_RESERVE or MEM_FREE) and its region's size.
DWORD stateAt(const void* p, SIZE_T* regionSize = nullptr) {
    MEMORY_BASIC_INFORMATION m{};
    if (!VirtualQuery(p, &m, sizeof m)) return 0;
    if (regionSize) *regionSize = m.RegionSize;
    return m.State;
}

double mb(std::size_t b) { return double(b) / 1048576.0; }

struct Foreign {
    char* block;   // an arena block the other thread frees
    char* own;     // the block the other thread makes
};

DWORD WINAPI freeElsewhere(LPVOID p) {
    Foreign* f = (Foreign*)p;
    delete[] f->block;
    f->own = new char[64];
    return 0;
}

std::uint32_t g_rng = 12345;
std::uint32_t rnd() {
    g_rng = g_rng * 1664525u + 1013904223u;
    return g_rng >> 8;
}

std::size_t churnSize() {
    const std::uint32_t r = rnd() % 1000;
    if (r < 900) return 1 + rnd() % 512;           // most blocks are small
    if (r < 995) return 512 + rnd() % (64 * 1024);
    return 512 * 1024 + rnd() % (2 * 1024 * 1024);  // and a few are large
}

void fill(char* p, std::size_t n, std::uint32_t seed) {
    for (std::size_t i = 0; i < n; ++i) p[i] = char((seed + i * 31u) & 0xFF);
}

bool same(const char* p, std::size_t n, std::uint32_t seed) {
    for (std::size_t i = 0; i < n; ++i)
        if (p[i] != char((seed + i * 31u) & 0xFF)) return false;
    return true;
}

}  // namespace

int main() {
    const std::size_t kSize = 64u * 1024u * 1024u;   // the suite's own arena
    // 1 no arena: a pair gets a private heap, as before
    {
        const HANDLE h = gen::genHeapBegin();
        check(h != nullptr && gen::arenaInfo().base == nullptr, "no arena reserved: a pair gets a private heap");
        std::vector<int>* v = new std::vector<int>(1000, 7);
        check(gen::genHeapBlockSize(v) >= sizeof(*v), "a private-heap block's size");
        delete v;
        gen::genHeapEnd(h);
        check(!gen::arenaRelease(), "arenaRelease with no arena: false");
    }

    // 2 the reserve: top-down, reserved, nothing committed
    void* bottomUp = VirtualAlloc(nullptr, kSize, MEM_RESERVE, PAGE_READWRITE);   // where bottom-up lands
    unsigned long err = 0;
    check(gen::arenaReserve(kSize, &err), "arenaReserve(64 MB) (err=" + std::to_string(err) + ")");
    gen::ArenaInfo a = gen::arenaInfo();
    check(a.base != nullptr && a.size == kSize && a.committed == 0 && !a.bound,
          "the range: a base, 64 MB, nothing committed, no pair");
    SIZE_T region = 0;
    check(stateAt(a.base, &region) == MEM_RESERVE && region == kSize, "VirtualQuery: one reserved region of 64 MB");
    check(bottomUp != nullptr && a.base > bottomUp,
          "MEM_TOP_DOWN: the arena lies above a bottom-up reservation of the same size");
    if (bottomUp) VirtualFree(bottomUp, 0, MEM_RELEASE);
    check(!gen::arenaReserve(kSize), "a second reserve is refused");
    std::printf("[measure] arena base=0x%08X size_mb=%.0f\n", (unsigned)(std::uintptr_t)a.base, mb(a.size));

    // 3 a pair: the blocks, reuse, split, merge, another thread
    char* before = new char[100];   // the process heap's: made before the pair, freed inside it
    HANDLE pair = gen::genHeapBegin();
    a = gen::arenaInfo();
    check(pair != nullptr && a.bound && a.pairs == 1, "genHeapBegin binds the arena to this thread");
    delete[] before;
    check(!inRange(before, a), "a process-heap block made before the pair is freed to its heap inside it");
    char* p1 = new char[100];
    check(inRange(p1, a) && (std::uintptr_t)p1 % 8 == 0, "a block from the arena, 8-byte aligned");
    check((std::uintptr_t)p1 == (std::uintptr_t)a.base + 8, "the first block starts at the base (after its tag)");
    check(gen::genHeapBlockSize(p1) >= 100 && gen::genHeapBlockSize(p1) < 116, "genHeapBlockSize of an arena block");
    char* p2 = new char[200];
    char* p3 = new char[300];
    check(p2 > p1 && p3 > p2, "blocks are cut upward from the top");
    delete[] p2;
    char* p4 = new char[200];
    check(p4 == p2, "a freed block is taken again for the same size");
    {   // split: a big free block serves two small ones
        char* big = new char[4096];
        char* guard = new char[16];
        delete[] big;
        char* s1 = new char[100];
        char* s2 = new char[100];
        check(s1 == big && s2 == big + 112, "a big free block is split for smaller ones");
        delete[] s1;
        delete[] s2;
        char* again = new char[4000];
        check(again == big, "the split pieces merge back into the big block when freed");
        delete[] again;
        delete[] guard;
    }
    {   // merge: three neighbours freed in the order first, last, middle become one block
        char* x = new char[1000];
        char* y = new char[1000];
        char* z = new char[1000];
        char* guard = new char[16];
        delete[] x;
        delete[] z;
        delete[] y;
        char* xyz = new char[2900];
        check(xyz == x, "three freed neighbours merge (a 2900-byte block where the first was)");
        delete[] xyz;
        delete[] guard;
    }
    {   // another thread: its blocks come from the process heap; its free of an arena block is refused
        Foreign f = {new char[256], nullptr};
        fill(f.block, 256, 77);
        HANDLE th = CreateThread(nullptr, 64 * 1024, freeElsewhere, &f, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
        if (th) {
            WaitForSingleObject(th, INFINITE);
            CloseHandle(th);
        }
        a = gen::arenaInfo();
        check(th && a.foreignFrees == 1 && same(f.block, 256, 77),
              "an arena block freed by another thread is refused (counted; its bytes intact until the reset)");
        check(f.own && !inRange(f.own, a), "another thread's block during the pair comes from the process heap");
        delete[] f.own;
        delete[] f.block;   // the owner's free of it is taken
    }
    // the churn: random sizes and frees, every block checked byte for byte before it is freed
    {
        const int kSlots = 4000, kOps = 300000;
        std::vector<char*> blk(kSlots, nullptr);
        std::vector<std::size_t> len(kSlots, 0);
        std::vector<std::uint32_t> seed(kSlots, 0);
        bool intact = true, inside = true;
        const auto t0 = std::chrono::steady_clock::now();
        for (int op = 0; op < kOps; ++op) {
            const int k = int(rnd() % kSlots);
            if (blk[k]) {
                intact = intact && same(blk[k], len[k], seed[k]);
                delete[] blk[k];
                blk[k] = nullptr;
            } else {
                len[k] = churnSize();
                seed[k] = rnd();
                blk[k] = new char[len[k]];
                inside = inside && inRange(blk[k], gen::arenaInfo());
                fill(blk[k], len[k], seed[k]);
            }
        }
        for (int k = 0; k < kSlots; ++k) {
            if (!blk[k]) continue;
            intact = intact && same(blk[k], len[k], seed[k]);
            delete[] blk[k];
        }
        const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - t0).count();
        a = gen::arenaInfo();
        check(inside && intact, "the churn: 300000 random allocations and frees, every block in the range and intact");
        std::printf("[measure] churn ms=%lld live_peak_mb=%.1f high_water_mb=%.1f committed_mb=%.1f\n", ms,
                    mb(a.pairLivePeak), mb(a.pairHighWater), mb(a.committed));
        check(a.pairHighWater < a.pairLivePeak + a.pairLivePeak / 2 + 4 * 1024 * 1024,
              "the churn's high-water stays within 1.5 x its live peak + 4 MB");
    }
    {   // everything is free again: one block from the base holds all the used part
        a = gen::arenaInfo();
        const std::size_t used = a.pairHighWater;
        // p1, p3 and p4 are still held at the base; free them and the whole used part is one block
        delete[] p1;
        delete[] p3;
        delete[] p4;
        char* whole = new char[used - 8];
        // read before check's message (a string of the arena itself) is made
        const bool merged = (std::uintptr_t)whole == (std::uintptr_t)a.base + 8 &&
                            gen::arenaInfo().pairHighWater == used;
        check(merged, "every freed block merged: one block from the base holds the whole used part");
        delete[] whole;
    }
    check(!gen::arenaRelease(), "arenaRelease while a pair holds the arena: refused");
    gen::genHeapEnd(pair);
    a = gen::arenaInfo();
    check(!a.bound && a.committed == 0 && stateAt(a.base, &region) == MEM_RESERVE && region == kSize,
          "genHeapEnd: unbound, every page decommitted, the range still reserved");
    check(a.highWater > 0 && a.outgrown == 0 && !gen::genHeapOutgrown(), "the high-water is kept, nothing outgrew");

    // 4 the outgrow path
    {
        pair = gen::genHeapBegin();
        bool threw = false;
        try {
            char* huge = new char[kSize];
            delete[] huge;
        } catch (const std::bad_alloc&) {
            threw = true;
        }
        check(threw && gen::genHeapOutgrown(), "a block larger than the arena: std::bad_alloc, genHeapOutgrown");
        std::vector<char*> mbs;
        mbs.reserve(128);
        threw = false;
        try {
            for (int i = 0; i < 128; ++i) mbs.push_back(new char[1024 * 1024]);
        } catch (const std::bad_alloc&) {
            threw = true;
        }
        check(threw && mbs.size() >= 60 && mbs.size() < 64,
              "1 MB blocks until the range is full: std::bad_alloc after " + std::to_string(mbs.size()));
        for (char* m : mbs) delete[] m;
        mbs.clear();
        char* after = new char[32 * 1024 * 1024];
        check(after != nullptr && inRange(after, gen::arenaInfo()), "after the frees the arena serves 32 MB again");
        delete[] after;
        gen::genHeapEnd(pair);
        a = gen::arenaInfo();
        check(a.outgrown == 1 && a.committed == 0, "the pair is counted as outgrown; its pages are decommitted");
    }

    // 5 the reset: the next pair starts at the base, not outgrown
    {
        pair = gen::genHeapBegin();
        check(!gen::genHeapOutgrown(), "a new pair is not outgrown");
        std::string* s = new std::string(1000, 'x');
        a = gen::arenaInfo();
        check((std::uintptr_t)s == (std::uintptr_t)a.base + 8 && a.pairs == 3, "the next pair starts at the base again");
        delete s;
        // the speed of the allocator against a private heap: 2,000,000 small blocks made and freed
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 2000000; ++i) delete new std::string(40 + (i % 64), 'y');
        const long long arenaMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::steady_clock::now() - t0).count();
        gen::genHeapEnd(pair);
        check(gen::arenaRelease(), "arenaRelease after the pair: the whole range in one VirtualFree");
        check(stateAt(a.base) == MEM_FREE && gen::arenaInfo().base == nullptr && !gen::arenaRelease(),
              "released: the range is free, no arena, a second release is refused");
        const HANDLE h = gen::genHeapBegin();
        const auto t1 = std::chrono::steady_clock::now();
        for (int i = 0; i < 2000000; ++i) delete new std::string(40 + (i % 64), 'y');
        const long long heapMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - t1).count();
        std::string* t = new std::string(1000, 'z');
        check(h != nullptr && !inRange(t, a), "after the release a pair gets a private heap again");
        delete t;
        gen::genHeapEnd(h);
        std::printf("[measure] 2000000 small blocks made and freed: arena %lld ms, private heap %lld ms\n", arenaMs,
                    heapMs);
    }

    std::printf("[test] arena: %d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) std::printf("[test] FAILURES\n");
    else std::printf("[test] ALL PASS\n");
    return g_fail ? 1 : 0;
}
