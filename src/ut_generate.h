// ut_generate.h - the DLL side of src\gen: keeps catalogue.bin and the uniq txt files in the mod
// folder current with the game's own archives before anything reads them.
//
// generateEnsure() is called once from the worker thread, after the exports are resolved and
// before anything reads a generated file or any hook is installed. It resolves the game folder
// and the mod folder through ut_paths.h, takes the text language from the ini, and runs
// gen::ensureOutputsGuarded. A stamp that matches costs one stat per input archive and one info
// line ("catalogue: up to date (stamp matches)"); a first launch or a game update rebuilds the
// files (a few seconds, one info line each way); a failure is one ERROR line and the folder keeps
// the files it already had.
//
// Titan Quest: GD's views (a custom game's own outputs) and the overlay the tab loads
// belong to the page,; the start-up generation is the whole of it here.
#pragma once

#include <windows.h>

namespace ut {

// The generation, into the mod folder. Worker thread, once.
void generateEnsure(HMODULE selfModule);

// True while a generation is running. The frame counter may not advance during those seconds,
// and the stall check must not read that as a crashed game.
bool generateBusy();

// generateEnsure has returned (the gray copies are written, or never will be
// this launch). The panel adds the gray folder to the engine only after it.
bool generateDone();

}  // namespace ut
