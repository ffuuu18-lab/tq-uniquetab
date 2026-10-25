// ut_gamelist.h - every installed custom game, found where the engine finds it.
//
// TQ.exe (Anniversary Edition 2.10) builds the pair it gives Engine::InitializeMod(name, folder)
// right before the call at 0x445563 (the same pattern before 0x471FD6): the folder is
// the subscribed Steam workshop item's install folder for that name (the lookup at 0x47AE70), and
// when there is none, the shell's My Documents folder (0x5C1240, the ::{450D8FBA-...} namespace
// item) + "/My Games/Titan Quest - Immortal Throne/" (0x448100) + "CustomMaps/". The engine then
// reads <folder><name>/Database/<name>.arz (ut_gameprobe.h). Steam installs a workshop item of
// the game under the game's own library: <library>\steamapps\workshop\content\<app id>\<item>\,
// with the custom game's folder <name>\ inside it.
//
// So the games are: every <name> folder of every item under the workshop content folder of the
// library that holds the game, then every <name> folder of the local CustomMaps folder, each kept
// when utGameLocate accepts it (its Database\<name>.arz is there). Two folders of one leaf
// (GameSpec::name) are one game: the first is kept, the workshop's before the local one, as the
// engine asks the workshop first. Read only: nothing here writes.
#pragma once

#include "gen/generate.h"   // GameSpec
#include "ut_gameprobe.h"   // utGameLocate

#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

namespace ut {

// Titan Quest Anniversary Edition's Steam app id: the workshop content folder's name.
const char* const kUtSteamAppId = "475150";

inline std::string utGameListTrim(const std::string& path) {
    std::string p = path;
    for (char& c : p)
        if (c == '/') c = '\\';
    while (p.size() > 3 && p.back() == '\\') p.pop_back();
    return p;
}

// <library>\steamapps\common\<game> -> <library>\steamapps\workshop\content\475150; "" when the
// game folder is not in a Steam library's steamapps\common.
inline std::string utWorkshopDirOf(const std::string& gameDir) {
    const std::string g = utGameListTrim(gameDir);
    const std::size_t game = g.rfind('\\');
    if (game == std::string::npos || game == 0) return std::string();
    const std::string common = g.substr(0, game);
    const std::size_t c = common.rfind('\\');
    if (c == std::string::npos || _stricmp(common.c_str() + c + 1, "common") != 0)
        return std::string();
    const std::string steamapps = common.substr(0, c);
    const std::size_t s = steamapps.rfind('\\');
    if (s == std::string::npos || _stricmp(steamapps.c_str() + s + 1, "steamapps") != 0)
        return std::string();
    return steamapps + "\\workshop\\content\\" + kUtSteamAppId;
}

// The local custom-quest folder under the Documents folder (with its trailing separator, as the
// engine passes it); "" without a Documents folder.
inline std::string utCustomMapsDirOf(const std::string& documents) {
    if (documents.empty()) return std::string();
    return utGameListTrim(documents) + "\\My Games\\Titan Quest - Immortal Throne\\CustomMaps\\";
}

// The sub-folders of `dir`, sorted (case-insensitive), without "." and "..".
inline std::vector<std::string> utGameListFolders(const std::string& dir) {
    std::vector<std::string> out;
    if (dir.empty()) return out;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((utGameListTrim(dir) + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        const std::string n = fd.cFileName;
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && n != "." && n != "..")
            out.push_back(n);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        return _stricmp(a.c_str(), b.c_str()) < 0;
    });
    return out;
}

struct UtGameFound {
    gen::GameSpec spec;   // the (folder, name) pair Engine::InitializeMod is given for it
    std::string leaf;     // GameSpec::name(): mods\<leaf>\, the same leaf the entry computes
    bool workshop;        // a workshop item (else the local CustomMaps folder)
};

// Every custom game under the two folders ("" = none there), in the order above. `skipped` (may
// be null) gets one clause per folder of a leaf already found.
inline void utGamesFind(const std::string& workshopDir, const std::string& customMapsDir,
                        std::vector<UtGameFound>* out, std::vector<std::string>* skipped) {
    if (!out) return;
    out->clear();
    auto add = [&](const std::string& root, const std::string& name, bool workshop) {
        UtGameFound g;
        if (!utGameLocate(root, name, g.spec, nullptr)) return;   // no Database\<name>.arz
        g.leaf = g.spec.name();
        g.workshop = workshop;
        for (const UtGameFound& f : *out)
            if (f.leaf == g.leaf) {
                if (skipped)
                    skipped->push_back(g.spec.dir() + " (the " + g.leaf + " game is " +
                                       f.spec.dir() + ")");
                return;
            }
        out->push_back(g);
    };
    if (!workshopDir.empty()) {
        const std::string base = utGameListTrim(workshopDir);
        for (const std::string& item : utGameListFolders(base)) {
            const std::string root = base + "\\" + item + "\\";
            for (const std::string& name : utGameListFolders(root)) add(root, name, true);
        }
    }
    if (!customMapsDir.empty()) {
        const std::string root = utGameListTrim(customMapsDir) + "\\";
        for (const std::string& name : utGameListFolders(root)) add(root, name, false);
    }
}

}  // namespace ut
