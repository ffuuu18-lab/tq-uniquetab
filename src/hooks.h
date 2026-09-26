// hooks.h - MinHook detours on the engine entry points the skeleton observes.
#pragma once

#include <windows.h>

namespace ut {

// MH_Initialize + MH_CreateHook + MH_EnableHook for every resolved target. Logs each step.
bool hooksInstall();

// MH_DisableHook(MH_ALL_HOOKS) + MH_Uninitialize.
void hooksRemove();

// The late-load check: says ONCE, from whichever tick sees it first, that the LoadMainDatabase
// detour missed the load (the database checksum was already set). `gameThread` names the caller.
void hookLateLoadTick(bool gameThread);

// WORKER, once a second: one debug line with the per-frame calls counted in the last second.
void hookSayCounts();

// Counters the worker thread logs, so the smoke test can assert on them.
unsigned long long hookFrameCount();
unsigned long long hookUpdateCount();
bool hookCaravanOpen();
// the last SetCaravanMode argument (-1 before the first), and the game's own window (null
// until the Present detour subclassed it).
int hookCaravanMode();
HWND hookGameWindow();
// every view detour (the accessor, StreamOut, GetItemUnderPoint, Sort) and the
// IsTransferCapable slot patch went in - the view's G8 asks this.
bool hooksViewOk();
// The search field's key gate (Display::HandleKeyEvent) is installed and ButtonEvent::GetText is
// bound: without both the field is never drawn and never takes the focus.
bool hookKeyGateLive();
long hookCaravanOpens();

// GameEngine::AddItemToTransfer(id, false)'s ORIGINAL (never the deposit detour): the
// engine's own auto-place into the REAL Transfer sack. Game thread, SEH-guarded.
bool hookTransferAutoPlace(unsigned id);

// ---- the cost meter (ut_costprobe.h) - the mod's own time, QPC only, no allocation, no
// per-frame log. The Present side is summed per frame and closed at each Engine::PresentSurface;
// the game side is viewTick per GameEngine::Update. The 5-second `frames:` line carries the window.
long long probeNow();                   // QueryPerformanceCounter
void probePresentAdd(long long ticks);  // a Present-side stretch (the tooltip detours)
void probeRebuild(long long ticks);     // one window rebuilt / shifted (ut_view buildPage)
void probeScan(long long ticks);        // one owned scan (ut_owned refresh)

}  // namespace ut
