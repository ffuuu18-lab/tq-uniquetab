// ut_gameprobe.h - where a custom game's files are, from what Engine::InitializeMod receives.
//
// InitializeMod(name, root) builds <root><name>\Resources and <root><name>\Database\<name>.arz
// (ut_gameswitch.h utGameCallOf: the name first, the folder second).
// utGameLocate checks the same pair before the generator reads a byte of it (GD's
// customGameFolder rules in Titan Quest's terms): the name is ONE folder name (no separator, no
// drive, not "." or ".."), the root climbs nowhere (no ".." segment), the game's folder is a
// folder under that root and its Database\<name>.arz is a file. The Resources folder may be
// absent: the game then has no archives of its own, and its text and icons are the base game's.
//
// Nothing here compares a game with the base game: every custom game with a folder of its own
// (GameInfo::GetModName set, ut_folders.h) gets its own generated set.
#pragma once

#include "gen/generate.h"   // GameSpec
#include "ut_gameswitch.h"  // utGameNameFault

#include <windows.h>

#include <string>

namespace ut {

inline bool utGameIsDir(const std::string& p) {
    const DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

inline bool utGameIsFile(const std::string& p) {
    const DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// Fills `out` (root, name) when the pair names a custom game's folder with its database; false
// with the reason in *why (one clause, for a WARN line) otherwise, and `out` the start-up game.
inline bool utGameLocate(const std::string& root, const std::string& name, gen::GameSpec& out,
                         std::string* why) {
    out = gen::GameSpec();
    auto refuse = [&](const std::string& w) {
        out = gen::GameSpec();
        if (why) *why = w;
        return false;
    };
    switch (utGameNameFault(name.c_str())) {
        case 1: return refuse("the custom game has no name");
        case 2: return refuse("the custom game name \"" + name + "\" is not a folder name");
        case 3: return refuse("the custom game name \"" + name + "\" is not one folder name");
        default: break;
    }
    if (root.empty()) return refuse("the custom game " + name + " has no folder");
    std::string r = root;
    for (char& c : r)
        if (c == '/') c = '\\';
    for (std::size_t i = 0; i <= r.size();) {
        std::size_t j = r.find('\\', i);
        if (j == std::string::npos) j = r.size();
        if (j - i == 2 && r.compare(i, 2, "..") == 0)
            return refuse("the custom game folder " + root + " climbs (\"..\")");
        i = j + 1;
    }
    out.root = root;
    out.modName = name;
    const std::string dir = out.dir(), arz = out.arzPath();
    if (!utGameIsDir(dir)) return refuse("the custom game folder " + dir + " is not there");
    if (!utGameIsFile(arz)) return refuse("the custom game database " + arz + " is not there");
    if (out.name().empty()) return refuse("the custom game name \"" + name + "\" gives no folder name");
    return true;
}

}  // namespace ut
