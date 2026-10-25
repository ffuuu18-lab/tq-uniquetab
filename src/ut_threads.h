// ut_threads.h - the stack each of the mod's threads reserves.
//
// CreateThread with a stack size of 0 reserves what TQ.exe's header says (SizeOfStackReserve
// 2,531,200 bytes, 2,496 KB once rounded to 64 KB) for every thread, and that address space stays
// taken while the thread lives. The mod's four threads ask for their own reservation
// (STACK_SIZE_PARAM_IS_A_RESERVATION; the first page is committed as before):
//   the worker, which also runs the start-up game's build       1024 KB
//   the custom games' start-up builds                            1024 KB
//   the layout plates' painter                                    512 KB
//   the log flusher                                               128 KB
// A whole build of the largest installed game (the deepest stack the generator makes) runs on a
// 1 MB stack. The painter's deepest call was not measured: 512 KB keeps a wide margin over its
// frames, whose buffers live on the heap.
#pragma once

#include <windows.h>

namespace ut {

const DWORD kUtStackWorker = 1024 * 1024;
const DWORD kUtStackBuilds = 1024 * 1024;
const DWORD kUtStackPainter = 512 * 1024;
const DWORD kUtStackFlusher = 128 * 1024;
const DWORD kUtStackDefault = 2496 * 1024;   // a thread without its own size (TQ.exe's header)

}  // namespace ut
