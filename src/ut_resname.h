// ut_resname.h - the names the mod hands the engine by name (pure, no engine).
//
// Engine.dll's resource manager splits a resource name at '/' and '\' and copies a part into a
// 64-byte stack buffer behind a range check (Engine.dll 0x14EE40, run for every source of its list
// from GraphicsEngine::LoadTexture): a part of 64 characters or more ends the process with
// __fastfail (FAST_FAIL_RANGE_CHECK_FAILURE) - no exception handler sees it. So every name the mod
// hands the engine (a texture, a font) passes utResNameOk right before the call; a name it refuses
// is never handed over: one WARN names it and that feature stays off for it. A directory source's
// folder is not checked: that function only compares a source's own names with the requested
// name's parts (it never copies them), and FileSystem::AddSource with the mod's arguments does not
// scan the folder.
#pragma once

#include <stddef.h>
#include <string.h>

namespace ut {

// The longest part of a name the engine's 64-byte buffer holds (its terminating zero is the 64th).
const size_t kUtResPartMax = 63;

// The first part of name[0..len) the engine must not see: an empty one (two separators in a row,
// a leading or trailing separator, an empty name) or one longer than kUtResPartMax bytes. True with
// *at and *partLen set to it; false when every part between '/' or '\' is 1..63 bytes.
inline bool utResNameBadPart(const char* name, size_t len, size_t* at, size_t* partLen) {
    size_t from = 0;
    for (size_t i = 0; i <= len; ++i) {
        if (i < len && name[i] != '/' && name[i] != '\\') continue;
        const size_t n = i - from;
        if (n == 0 || n > kUtResPartMax) {
            if (at) *at = from;
            if (partLen) *partLen = n;
            return true;
        }
        from = i + 1;
    }
    return false;
}

// True when the engine may be handed name[0..len): every part between '/' or '\' is 1..63 bytes.
inline bool utResNameOk(const char* name, size_t len) {
    return name && !utResNameBadPart(name, len, nullptr, nullptr);
}

inline bool utResNameOk(const char* name) { return name && utResNameOk(name, strlen(name)); }

// The longest part of `name` (0 for an empty name), for the WARN that names a refused name.
inline size_t utResLongestPart(const char* name) {
    size_t best = 0, n = 0;
    for (const char* p = name ? name : ""; ; ++p) {
        if (*p == 0 || *p == '/' || *p == '\\') {
            if (n > best) best = n;
            n = 0;
            if (*p == 0) break;
        } else {
            ++n;
        }
    }
    return best;
}

}  // namespace ut
