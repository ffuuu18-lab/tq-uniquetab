#pragma once

#include "ut_padinput.h"   // the published rectangles (pure)

namespace ut {

// The screen input: the front input device's GetMouseEvent detour that consumes a click on a
// screen part (ut_padinput.h's press machine) and writes the screen input log. GAME THREAD, at
// every world up: resets the per-world line budget, the event-space proof and any press; while
// GetMouseEvent is not yet confirmed and a device exists, confirms it by its bytes (the late
// binding row), then installs the two detours. A miss turns the screen input off alone: the screen
// parts are then not placed (they stay in the caravan window).
void screenProbeOnWorld();

// True when the consuming detour is installed and not turned off: a screen part may be placed.
bool screenInputReady();

// The rectangles the screen parts were drawn at this frame (client px); null or shown == 0
// retracts them. The Present detour's draw calls it; the detour reads a consistent copy.
void screenInputPublish(const UtPadScreenRects* r);

}  // namespace ut
