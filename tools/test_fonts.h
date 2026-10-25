// test_fonts.h - the offline suites' copy of the game's fonts: the nine fonts of
// Resources\Fonts.arc read into a ut::UtTmFontSet through the mod's own arc reader, exactly as the
// mod's start-up read does (ut_panel.cpp panelFontsRead). The game folder is %TQ_GAME_DIR% (the
// build_test_*.bat files set it from tools\tqpath.py when it is unset); it is only READ.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "../src/gen/arc_reader.h"
#include "../src/ut_textmeasure.h"

inline std::string tfFontsArc() {
    const char* g = std::getenv("TQ_GAME_DIR");
    return g && *g ? std::string(g) + "\\Resources\\Fonts.arc" : std::string();
}

// The raw bytes of one font (by its path, fonts/<name>.fnt); empty when it cannot be read.
inline std::vector<std::uint8_t> tfFontBytes(const char* path) {
    std::vector<std::uint8_t> out;
    gen::ArcArchive arc;
    std::string err;
    const std::string p = tfFontsArc();
    if (p.empty() || !arc.load(p, &err)) return out;
    const gen::ArcEntry* e = arc.entry(std::string(path + 6));
    if (e) arc.read(*e, out);
    return out;
}

// The nine fonts read as the mod reads them; true when all nine were.
inline bool tfReadSet(ut::UtTmFontSet* set) {
    gen::ArcArchive arc;
    std::string err;
    const std::string p = tfFontsArc();
    if (p.empty() || !arc.load(p, &err)) return false;
    set->read = 0;
    for (int i = 0; i < ut::kUtTmFontCount; ++i) {
        const gen::ArcEntry* e = arc.entry(std::string(ut::utTmFontPath(i) + 6));
        std::vector<std::uint8_t> b;
        if (e && arc.read(*e, b) && ut::utTmFontRead(b.data(), b.size(), &set->font[i])) ++set->read;
    }
    return set->read == ut::kUtTmFontCount;
}
