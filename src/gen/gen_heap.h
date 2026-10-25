// gen_heap.h - the start-up builds' memory: one reserved range, given back whole when they end.
//
// The generator's records, strings and tables are many small blocks. On the process heap they
// grow its segments, and a heap segment stays reserved after its blocks are freed: what one
// generation needed would stay taken from TQ.exe's 2 GB for the rest of the run. And a heap of
// its own per build (HeapCreate) places its segments bottom-up in the lowest holes that fit, the
// holes the engine's own bottom-up growth takes next, so the space a build gives back comes back
// cut up. So the builds share ONE range, the build arena: reserved top-down (MEM_TOP_DOWN) when
// the DLL loads and a build is due, committed as a build grows, decommitted and reset after each
// game, and released in one piece when the builds end (ut_generate.h). Released at the top of the
// address space, it merges back into the free space the engine has not reached.
//
// The binding is per thread: between genHeapBegin and genHeapEnd, every operator new of THIS
// thread is served from the arena (or, when none is reserved, from a private heap that
// genHeapEnd destroys); every other thread, and this one outside the pair, uses the process
// heap. A process-heap block carries its heap in an 8-byte header, so it is freed to it whichever
// thread frees it; an arena block is known by its address. An arena block freed by another
// thread is refused (counted, and left to the reset): the arena is one thread's at a time.
// Nothing allocated inside the pair may outlive it: the caller keeps every object of the
// generation inside the pair, and touches the generator's function-local statics (the built-in
// groups) before the first pair.
//
// The arena's allocator: blocks with an 8-byte boundary tag (size and the previous block's size),
// free blocks kept in segregated lists (a first level per power of two, 16 classes each, exact
// 8-byte classes under 128 bytes; two bitmaps find the first fitting list), a freed block merged
// with its free neighbours at once, and new blocks cut from the top of the used part, committing
// pages as it rises. A build that needs more than the range holds gets std::bad_alloc (the
// generation fails, as on the process heap when memory runs out) and genHeapOutgrown says why.
#pragma once

#include <windows.h>

#include <cstddef>

namespace gen {

// The build arena's size: the largest of the 13 installed custom games' builds reaches 75.8 MB of
// the range at its high-water (74.6 MB in live blocks); a base-size game 37-50 MB.
const std::size_t kBuildArenaBytes = 128u * 1024u * 1024u;

// What the arena holds and has held. Sizes in bytes.
struct ArenaInfo {
    const void* base = nullptr;       // the range; null when none is reserved
    std::size_t size = 0;             // its size
    std::size_t highWater = 0;        // the most of the range one pair has used, since the reserve
    std::size_t pairHighWater = 0;    // the last pair's (the current pair's while one is open)
    std::size_t pairLivePeak = 0;     // the last pair's peak of bytes in live blocks (with tags)
    std::size_t committed = 0;        // committed now
    unsigned pairs = 0;               // pairs served since the reserve
    unsigned outgrown = 0;            // pairs that asked for more than the range holds
    unsigned foreignFrees = 0;        // arena blocks freed by another thread: refused
    bool bound = false;               // a pair holds it now
};

// Reserves the arena: `bytes` (rounded up to 64 KB) of address space, MEM_RESERVE | MEM_TOP_DOWN,
// nothing committed. False when one is reserved already or VirtualAlloc refuses (`err` says why).
bool arenaReserve(std::size_t bytes, unsigned long* err = nullptr);
// Gives the whole range back in one VirtualFree. False when none is reserved or a pair holds it.
bool arenaRelease();
ArenaInfo arenaInfo();

// Binds this thread's operator new to the arena when one is reserved and free, else to a new
// growable private heap. Null when neither could be had (the process heap is used, as without
// the pair).
HANDLE genHeapBegin();
// Unbinds this thread. The arena: every page decommitted and the allocator reset (the range stays
// reserved); a private heap: destroyed. Null: nothing to do.
void genHeapEnd(HANDLE heap);
// The current (or last) pair's arena refused a block because the range was full.
bool genHeapOutgrown();
// The pair genHeapBegin returned is the arena's (not a private heap's).
bool genHeapIsArena(HANDLE heap);
// The usable size of a block operator new returned (what _msize gives for a malloc block).
SIZE_T genHeapBlockSize(const void* block);

}  // namespace gen
