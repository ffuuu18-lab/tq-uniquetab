// ut_coverstep.h - the caravan background swap's decisions (ut_cover.cpp), pure: no engine, no
// Windows, so tools\test_lattice drives them.
//
// The two textures and who holds them: the engine's background (`orig`) is held by the caravan
// window's field (caravan+0x1308 +0x34), or by the mod while its copy is in; the mod's painted copy
// (`mine`) is held by the mod, or by the field while it is in. Whatever the field holds is unloaded
// by the window's destructor (TQ.exe 0xB6D70) or by the engine's own reload (slot +0x88), so the
// mod unloads only what no field holds.
#pragma once

namespace ut {

struct UtCoverIn {
    // carried between frames
    bool installed;     // the field holds the mod's copy
    bool haveMine;      // a painted copy is loaded and held by the mod
    bool mineIsKey;     // that copy is the one the grid in force names
    bool keyed;         // the grid in force has a name (the painter was asked)
    bool keyFailed;     // that name was refused or cannot be shown (said once)
    // this frame
    bool sameCaravan;   // the page's caravan window is the one written, in TQ.exe's shape
    bool fieldIsMine;   // the field holds the mod's copy
    bool want;          // the view ON on the Transfer page, a grid in force off the derived one
    bool sizeOk;        // the background's pixels are the caravan window's units
    bool painted;       // the painter's pass for the name is done
};

enum : unsigned {
    kUtCoverRestore = 1u,       // the engine's background written back (the field compared first)
    kUtCoverEngineWrote = 2u,   // the engine replaced the mod's copy: the held background given back, swap off
    kUtCoverDropMine = 4u,      // the held copy given back on a later frame (this frame may have drawn it)
    kUtCoverLoad = 8u,          // LoadTexture of the named copy
    kUtCoverInstall = 16u,      // the mod's copy written into the field
    kUtCoverSwap = 32u,         // the named copy loaded and written over the one in the field; the
                                // one it replaces given back on a later frame (no frame of the
                                // window's own lattice between two painted copies)
};

// One frame of the Transfer page's draw. A new grid in force while a copy is in keeps that copy in
// the field until the painter's pass for the new name is done, then writes the new copy over it
// (kUtCoverSwap): the window's own lattice is never shown between two painted copies (a page change
// between the item grid and the relic area, an edit of either). A name that cannot be shown takes
// the copy out and gives it back (the window's own background then). An OFF -> ON with the same
// grid writes the held copy again.
inline unsigned utCoverStep(const UtCoverIn& in) {
    if (in.installed && !in.sameCaravan) return kUtCoverRestore;
    if (in.installed && !in.fieldIsMine) return kUtCoverEngineWrote;
    if (!in.want || !in.sizeOk) return in.installed ? kUtCoverRestore : 0u;
    if (in.haveMine && !in.mineIsKey) {
        if (!in.installed || !in.keyed || in.keyFailed) return (in.installed ? kUtCoverRestore : 0u) | kUtCoverDropMine;
        return in.painted ? kUtCoverSwap : 0u;
    }
    if (in.installed || !in.keyed || in.keyFailed) return 0u;
    if (!in.haveMine) return in.painted ? (kUtCoverLoad | kUtCoverInstall) : 0u;
    return kUtCoverInstall;
}

// What a restore leaves, the field read again first: `fieldRead` false = the caravan window is
// gone (its destructor unloaded what the field held, the mod's copy); `fieldIsMine` false = the
// engine wrote its own texture (its reload unloaded the mod's copy); `writeOk` = the engine's
// background was written back (tried only when the field was read and holds the mod's copy).
struct UtCoverBack {
    bool unloadOrig;   // the engine's background is given back by the mod: no field holds it
    bool keepMine;     // the mod still holds its copy: the field no longer does
    bool off;          // the swap is off for the session
};

inline UtCoverBack utCoverRestoreOf(bool fieldRead, bool fieldIsMine, bool writeOk) {
    UtCoverBack r = {false, false, false};
    if (!fieldRead) {
        r.unloadOrig = true;
    } else if (!fieldIsMine) {
        r.unloadOrig = true;
        r.off = true;
    } else if (!writeOk) {   // the field keeps the mod's copy: the window's destructor unloads it
        r.unloadOrig = true;
        r.off = true;
    } else {
        r.keepMine = true;
    }
    return r;
}

}  // namespace ut
