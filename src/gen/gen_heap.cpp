// gen_heap.cpp - see gen_heap.h. The replacement global operator new / delete of the module that
// links this file, and the build arena's allocator.
#include "gen/gen_heap.h"

#include <intrin.h>

#include <cstdint>
#include <cstring>
#include <new>

namespace {

// ---- a private heap (a pair while no arena is reserved) and the process heap ---------------
volatile LONG g_heapThread = 0;    // the thread bound to the private heap
HANDLE volatile g_heap = nullptr;
const std::size_t kHeader = 8;     // the heap a block came from; keeps HeapAlloc's 8-byte alignment

// ---- the arena ------------------------------------------------------------------------------
// A block's boundary tag; a free block's two list links follow it (in its first 8 bytes).
struct Block {
    std::uint32_t sizeFlags;   // the block's size, tag included (a multiple of 8) | kFree
    std::uint32_t prevSize;    // the size of the block before it; 0 for the first block
    Block* nextFree;
    Block* prevFree;
};
const std::uint32_t kFree = 1;
const std::size_t kTag = 8;              // the tag in front of a block's bytes
const std::size_t kMinBlock = 16;        // a tag and the two links
const unsigned kSlLog2 = 4;              // 16 classes per power of two
const unsigned kSlCount = 1u << kSlLog2;
const std::size_t kSmall = 128;          // below: exact 8-byte classes (first level 0)
const unsigned kFlCount = 25;            // first levels up to 2^30 bytes
const std::size_t kCommitStep = 256 * 1024;
static_assert(sizeof(Block) <= kMinBlock, "a free block's tag and links fit the smallest block (x86)");

struct Arena {
    char* base = nullptr;              // the reserved range [base, base + size)
    std::size_t size = 0;
    char* top = nullptr;               // the blocks fill [base, top)
    char* committed = nullptr;         // [base, committed) is committed
    Block* last = nullptr;             // the block that ends at top
    std::uint32_t flMap = 0;           // a bit per first level with a non-empty list
    std::uint32_t slMap[kFlCount] = {};
    Block* heads[kFlCount][kSlCount] = {};
    std::size_t live = 0, livePeak = 0;
    std::size_t highWater = 0, pairHighWater = 0;
    unsigned pairs = 0, outgrown = 0;
    bool pairOutgrown = false;
};

Arena g_arena;
volatile LONG g_arenaThread = 0;           // the thread a pair binds it to; 0 none; -1 releasing
char* volatile g_arenaLo = nullptr;        // the range as other threads compare against it
volatile LONG g_arenaLen = 0;
volatile LONG g_foreignFrees = 0;
const LONG kReleasing = -1;                // never a thread id (those are multiples of 4)

HANDLE arenaPair() { return (HANDLE)&g_arena; }

std::size_t blockSize(const Block* b) { return b->sizeFlags & ~std::uint32_t(7); }
bool isFree(const Block* b) { return (b->sizeFlags & kFree) != 0; }
Block* nextOf(Block* b) { return (Block*)((char*)b + blockSize(b)); }

unsigned highBit(std::size_t v) {
    unsigned long i = 0;
    _BitScanReverse(&i, (unsigned long)v);
    return (unsigned)i;
}
unsigned lowBit(std::uint32_t v) {
    unsigned long i = 0;
    _BitScanForward(&i, v);
    return (unsigned)i;
}

// The list a free block of `size` bytes is kept in.
void mapping(std::size_t size, unsigned* fl, unsigned* sl) {
    if (size < kSmall) {
        *fl = 0;
        *sl = unsigned(size >> 3);
        return;
    }
    const unsigned m = highBit(size);
    *fl = m - 6;   // 128 = 2^7 is first level 1
    *sl = unsigned(size >> (m - kSlLog2)) - kSlCount;
}

// The first list whose every block holds `size` bytes: the size rounded up to its class's end.
void mappingSearch(std::size_t size, unsigned* fl, unsigned* sl) {
    if (size >= kSmall) size += (std::size_t(1) << (highBit(size) - kSlLog2)) - 1;
    mapping(size, fl, sl);
}

bool inArena(const void* p) {
    return (std::uintptr_t)p - (std::uintptr_t)g_arenaLo < (std::uintptr_t)(std::uint32_t)g_arenaLen;
}

void insertFree(Arena& a, Block* b) {
    unsigned fl, sl;
    mapping(blockSize(b), &fl, &sl);
    Block* h = a.heads[fl][sl];
    b->nextFree = h;
    b->prevFree = nullptr;
    if (h) h->prevFree = b;
    a.heads[fl][sl] = b;
    a.flMap |= 1u << fl;
    a.slMap[fl] |= 1u << sl;
}

void removeFree(Arena& a, Block* b) {
    unsigned fl, sl;
    mapping(blockSize(b), &fl, &sl);
    if (b->prevFree) b->prevFree->nextFree = b->nextFree;
    else a.heads[fl][sl] = b->nextFree;
    if (b->nextFree) b->nextFree->prevFree = b->prevFree;
    if (!a.heads[fl][sl]) {
        a.slMap[fl] &= ~(1u << sl);
        if (!a.slMap[fl]) a.flMap &= ~(1u << fl);
    }
}

Block* findFree(Arena& a, std::size_t need) {
    unsigned fl, sl;
    mappingSearch(need, &fl, &sl);
    if (fl >= kFlCount) return nullptr;
    std::uint32_t slm = a.slMap[fl] & (~0u << sl);
    if (!slm) {
        const std::uint32_t flm = fl + 1 < kFlCount ? a.flMap & (~0u << (fl + 1)) : 0u;
        if (!flm) return nullptr;
        fl = lowBit(flm);
        slm = a.slMap[fl];
    }
    return a.heads[fl][lowBit(slm)];
}

// Commits the pages up to `end` (in steps, never past the range). False when the system refuses.
bool commitTo(Arena& a, char* end) {
    if (end <= a.committed) return true;
    std::size_t want = (std::size_t(end - a.committed) + kCommitStep - 1) & ~(kCommitStep - 1);
    const std::size_t room = std::size_t(a.base + a.size - a.committed);
    if (want > room) want = room;
    if (!VirtualAlloc(a.committed, want, MEM_COMMIT, PAGE_READWRITE)) return false;
    a.committed += want;
    return end <= a.committed;
}

// A block of at least `need` bytes from the top: the free last block grown, or a new one. Null
// when the range is full (pairOutgrown) or the pages cannot be committed.
Block* extend(Arena& a, std::size_t need) {
    Block* b = (Block*)a.top;
    std::size_t have = 0;
    if (a.last && isFree(a.last)) {
        b = a.last;
        have = blockSize(b);
        if (have >= need) {   // big enough, but in a class the search does not take
            removeFree(a, b);
            return b;
        }
    }
    const std::size_t more = need - have;
    if (std::size_t(a.base + a.size - a.top) < more) {
        a.pairOutgrown = true;
        return nullptr;
    }
    if (!commitTo(a, a.top + more)) return nullptr;
    if (have) {
        removeFree(a, b);
    } else {
        b->prevSize = a.last ? std::uint32_t(blockSize(a.last)) : 0u;
        a.last = b;
    }
    b->sizeFlags = std::uint32_t(need);
    a.top += more;
    const std::size_t used = std::size_t(a.top - a.base);
    if (used > a.pairHighWater) a.pairHighWater = used;
    return b;
}

void* arenaAlloc(std::size_t n) {
    Arena& a = g_arena;
    if (n >= a.size) {
        a.pairOutgrown = true;
        return nullptr;
    }
    std::size_t need = ((n + 7) & ~std::size_t(7)) + kTag;
    if (need < kMinBlock) need = kMinBlock;
    Block* b = findFree(a, need);
    if (b) removeFree(a, b);
    else if (!(b = extend(a, need))) return nullptr;
    const std::size_t sz = blockSize(b);
    if (sz - need >= kMinBlock) {   // the rest stays free
        Block* r = (Block*)((char*)b + need);
        r->sizeFlags = std::uint32_t(sz - need) | kFree;
        r->prevSize = std::uint32_t(need);
        if (b == a.last) a.last = r;
        else nextOf(r)->prevSize = std::uint32_t(sz - need);
        insertFree(a, r);
        b->sizeFlags = std::uint32_t(need);
    } else {
        b->sizeFlags = std::uint32_t(sz);
    }
    a.live += blockSize(b);
    if (a.live > a.livePeak) a.livePeak = a.live;
    return (char*)b + kTag;
}

void arenaFree(void* p) {
    Arena& a = g_arena;
    Block* b = (Block*)((char*)p - kTag);
    if (isFree(b)) return;   // freed twice: the block is already in a list
    std::size_t sz = blockSize(b);
    a.live -= sz;
    if (b != a.last) {   // merge the next block when it is free
        Block* nx = nextOf(b);
        if (isFree(nx)) {
            removeFree(a, nx);
            if (nx == a.last) a.last = b;
            sz += blockSize(nx);
        }
    }
    if (b->prevSize) {   // and the block before it
        Block* pv = (Block*)((char*)b - b->prevSize);
        if (isFree(pv)) {
            removeFree(a, pv);
            if (b == a.last) a.last = pv;
            sz += blockSize(pv);
            b = pv;
        }
    }
    b->sizeFlags = std::uint32_t(sz) | kFree;
    if (b != a.last) nextOf(b)->prevSize = std::uint32_t(sz);
    insertFree(a, b);
}

// Every page decommitted and no block left; the range stays reserved.
void arenaReset(Arena& a) {
    if (a.committed > a.base) VirtualFree(a.base, std::size_t(a.committed - a.base), MEM_DECOMMIT);
    a.top = a.committed = a.base;
    a.last = nullptr;
    a.flMap = 0;
    std::memset(a.slMap, 0, sizeof a.slMap);
    std::memset(a.heads, 0, sizeof a.heads);
    a.live = 0;
}

bool boundHere() { return g_arenaThread == (LONG)GetCurrentThreadId(); }

HANDLE heapForThisThread() {
    const HANDLE h = g_heap;
    if (h && (DWORD)InterlockedCompareExchange(&g_heapThread, 0, 0) == GetCurrentThreadId())
        return h;
    return GetProcessHeap();
}

void* allocate(std::size_t size) {
    if (boundHere()) return arenaAlloc(size);
    const HANDLE h = heapForThisThread();
    if (size > (std::size_t)-1 - kHeader) return nullptr;
    void* p = HeapAlloc(h, 0, size + kHeader);
    if (!p) return nullptr;
    *(HANDLE*)p = h;
    return (char*)p + kHeader;
}

void release(void* block) {
    if (!block) return;
    if (inArena(block)) {
        if (boundHere()) arenaFree(block);
        else InterlockedIncrement(&g_foreignFrees);   // refused: the block waits for the reset
        return;
    }
    void* p = (char*)block - kHeader;
    HeapFree(*(HANDLE*)p, 0, p);
}

void* allocateOrThrow(std::size_t size) {
    for (;;) {
        if (void* p = allocate(size ? size : 1)) return p;
        if (boundHere()) throw std::bad_alloc();   // the arena: no handler makes room in it
        std::new_handler handler = std::get_new_handler();
        if (!handler) throw std::bad_alloc();
        handler();
    }
}

}  // namespace

namespace gen {

bool arenaReserve(std::size_t bytes, unsigned long* err) {
    if (err) *err = 0;
    if (g_arena.base || !bytes || bytes > 0x40000000u) return false;
    bytes = (bytes + 0xFFFF) & ~std::size_t(0xFFFF);
    void* p = VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_TOP_DOWN, PAGE_READWRITE);
    if (!p) {
        if (err) *err = GetLastError();
        return false;
    }
    Arena& a = g_arena;
    arenaReset(a);
    a.base = a.top = a.committed = (char*)p;
    a.size = bytes;
    a.livePeak = a.highWater = a.pairHighWater = 0;
    a.pairs = a.outgrown = 0;
    a.pairOutgrown = false;
    InterlockedExchange(&g_foreignFrees, 0);
    g_arenaLo = a.base;
    InterlockedExchange(&g_arenaLen, (LONG)bytes);
    return true;
}

bool arenaRelease() {
    if (!g_arena.base) return false;
    if (InterlockedCompareExchange(&g_arenaThread, kReleasing, 0) != 0) return false;   // a pair holds it
    InterlockedExchange(&g_arenaLen, 0);   // no block is the arena's from here on
    VirtualFree(g_arena.base, 0, MEM_RELEASE);
    Arena& a = g_arena;
    a.base = a.top = a.committed = nullptr;
    a.size = 0;
    a.last = nullptr;
    a.flMap = 0;
    std::memset(a.slMap, 0, sizeof a.slMap);
    std::memset(a.heads, 0, sizeof a.heads);
    a.live = 0;
    g_arenaLo = nullptr;
    InterlockedExchange(&g_arenaThread, 0);
    return true;
}

ArenaInfo arenaInfo() {
    ArenaInfo i;
    // a read from another thread can meet the release half done: no range, no figures
    if (!InterlockedCompareExchange(&g_arenaLen, 0, 0)) return i;
    const Arena& a = g_arena;
    char* const base = a.base;
    char* const committed = a.committed;
    if (!base) return i;
    i.base = base;
    i.size = a.size;
    i.highWater = a.highWater > a.pairHighWater ? a.highWater : a.pairHighWater;
    i.pairHighWater = a.pairHighWater;
    i.pairLivePeak = a.livePeak;
    i.committed = committed >= base ? std::size_t(committed - base) : 0;
    i.pairs = a.pairs;
    i.outgrown = a.outgrown;
    i.foreignFrees = (unsigned)g_foreignFrees;
    i.bound = g_arenaThread > 0;
    return i;
}

HANDLE genHeapBegin() {
    if (g_arena.base &&
        InterlockedCompareExchange(&g_arenaThread, (LONG)GetCurrentThreadId(), 0) == 0) {
        if (g_arena.base) {
            Arena& a = g_arena;
            a.pairHighWater = 0;
            a.livePeak = 0;
            a.pairOutgrown = false;
            ++a.pairs;
            return arenaPair();
        }
        InterlockedExchange(&g_arenaThread, 0);   // released meanwhile
    }
    const HANDLE h = HeapCreate(0, 0, 0);
    if (!h) return nullptr;
    g_heap = h;
    InterlockedExchange(&g_heapThread, (LONG)GetCurrentThreadId());
    return h;
}

void genHeapEnd(HANDLE heap) {
    if (!heap) return;
    if (heap == arenaPair()) {
        if (!boundHere()) return;
        Arena& a = g_arena;
        if (a.pairHighWater > a.highWater) a.highWater = a.pairHighWater;
        if (a.pairOutgrown) ++a.outgrown;
        arenaReset(a);
        InterlockedExchange(&g_arenaThread, 0);
        return;
    }
    InterlockedExchange(&g_heapThread, 0);
    g_heap = nullptr;
    HeapDestroy(heap);
}

bool genHeapOutgrown() { return g_arena.pairOutgrown; }

bool genHeapIsArena(HANDLE heap) { return heap && heap == arenaPair(); }

SIZE_T genHeapBlockSize(const void* block) {
    if (!block) return 0;
    if (inArena(block)) return blockSize((const Block*)((const char*)block - kTag)) - kTag;
    const void* p = (const char*)block - kHeader;
    const SIZE_T n = HeapSize(*(const HANDLE*)p, 0, p);
    return n == (SIZE_T)-1 ? 0 : n - kHeader;
}

}  // namespace gen

void* operator new(std::size_t size) { return allocateOrThrow(size); }
void* operator new[](std::size_t size) { return allocateOrThrow(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return allocate(size ? size : 1); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    return allocate(size ? size : 1);
}
void operator delete(void* block) noexcept { release(block); }
void operator delete[](void* block) noexcept { release(block); }
void operator delete(void* block, std::size_t) noexcept { release(block); }
void operator delete[](void* block, std::size_t) noexcept { release(block); }
void operator delete(void* block, const std::nothrow_t&) noexcept { release(block); }
void operator delete[](void* block, const std::nothrow_t&) noexcept { release(block); }
