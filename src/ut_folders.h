// ut_folders.h - where a mod's files live, and the one-way move of its journal into that folder.
//
// TQ PORT of the Grim Dawn mod's ut_folders.h (GD's layout without its basemods\ variant: Titan
// Quest has the main game and one game per mod). The word on TQ is the GAME: the main game, or
// the mod a world was loaded with.
//
// THE LAYOUT. The main game's files - and the custom-quest save set that has no mod name,
// tq-uniq-items-custom - stay in the mod folder's root, exactly as before. A mod has ONE folder,
// <mod folder>\mods\<leaf>\, where <leaf> is the journal's own name without "tq-uniq-items-"
// (utSaveSetGame, ut_depositgate.h: ONE function names both, so the folder and the journal can
// never disagree):
//
//   mods\<leaf>\   the mod's journal (tq-uniq-items-<leaf>.jsonl, its .csv and its .bad- copies),
//                  its own complete uniquetab.ini, its uniquetab.state and its uniquetab.log
//   <journal_dir>\mods\<leaf>\   the mod's journal set instead, when journal_dir is set (the
//                  main game's sets in <journal_dir>\ itself); the rest stays in the mod folder
//
// THE MOVE. Builds before this one kept a mod's journal in the root. utJournalMoveSet moves one
// such set into mods\<leaf>\ - a rename on the same volume per file, so nothing is copied,
// merged or overwritten - ALL OR NOTHING: the .csv and the .bad- copies first, the .jsonl (the
// only file that holds rows) LAST; a target that already exists refuses the whole set before
// anything moves (both are kept, a WARN names the file in the way and the journal the mod
// reads); a folder that cannot be created moves nothing; a rename that fails (a file held open
// by another program) moves the already moved files back to the root, with a WARN, and the set
// is tried again at the next start. A set whose .jsonl is still in the root is read from the
// root (utJournalSetRel), so a set is never split between two journals. One-way: an older build
// does not look in mods\.
#pragma once

#include <windows.h>

#include <string.h>
#include <wchar.h>

#include <algorithm>
#include <string>
#include <vector>

#include "ut_depositgate.h"   // utSaveSetGame
#include "ut_log.h"

namespace ut {

// The mods' folder under the mod folder.
#define UT_CUSTOM_GAMES_DIR "mods"

// A leaf the mod would have named a folder: 1..63 of [A-Za-z0-9._+-], and not "." or "..".
inline bool utFolderLeafOk(const std::string& leaf) {
    if (leaf.empty() || leaf.size() >= 64 || leaf == "." || leaf == "..") return false;
    for (char c : leaf) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                        c == '.' || c == '_' || c == '+' || c == '-';
        if (!ok) return false;
    }
    return true;
}

// The game of a save set: the mod's leaf for "tq-uniq-items-<leaf>", "" for the main game's
// sets (tq-uniq-items, tq-uniq-items-custom), an unknown set, or a leaf no folder may carry.
inline std::string utGameOfSet(const char* setLeaf) {
    char game[80];
    if (!utSaveSetGame(setLeaf, game, sizeof(game))) return std::string();
    const std::string g(game);
    return utFolderLeafOk(g) ? g : std::string();
}

// "mods\<leaf>" for a bare leaf, "" for an illegal one (and for the main game's "").
inline std::string utCustomGameRelLeaf(const std::string& leaf) {
    return utFolderLeafOk(leaf) ? std::string(UT_CUSTOM_GAMES_DIR "\\") + leaf : std::string();
}

// Creates `base`\`rel` level by level (`base` itself must exist). True when the folder is there.
inline bool utMakeRelDirs(const std::string& base, const std::string& rel) {
    std::string path = base;
    size_t from = 0;
    while (from <= rel.size()) {
        size_t to = rel.find('\\', from);
        if (to == std::string::npos) to = rel.size();
        if (to > from) {
            path += "\\" + rel.substr(from, to - from);
            CreateDirectoryA(path.c_str(), nullptr);
        }
        from = to + 1;
    }
    const DWORD a = GetFileAttributesA(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

// journal_dir as journalInit reads it (GD's): trailing slashes, spaces and quotes cut, a leading
// quote cut; "" unless it names an existing folder. The journals are under it (a mod's under
// <journal_dir>\mods\<leaf>\); every other file stays in the mod folder.
inline std::string utJournalDirResolved(const char* raw) {
    std::string jd(raw ? raw : "");
    while (!jd.empty() && (jd.back() == '\\' || jd.back() == '/' || jd.back() == ' ' || jd.back() == '"'))
        jd.pop_back();
    if (!jd.empty() && jd[0] == '"') jd.erase(0, 1);
    if (jd.empty()) return jd;
    const DWORD a = GetFileAttributesA(jd.c_str());
    return (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) ? jd : std::string();
}

// ---- the one-way move ------------------------------------------------------------------------

enum UtJournalMoveResult {
    kUtMoveNothing = 0,   // no .jsonl of the set in the root: nothing to move
    kUtMoveDone,          // every file of the set is in mods\<leaf>\ now
    kUtMoveTaken,         // a target exists: nothing moved, both kept
    kUtMoveRolledBack,    // a rename failed: the moved files went back, the set stays in the root
    kUtMoveSplit,         // a rename failed AND a file could not be moved back (named in `why`)
    kUtMoveNoFolder       // mods\<leaf>\ could not be created: nothing moved
};

struct UtJournalMove {
    int result = kUtMoveNothing;
    std::vector<std::string> files;   // the set's file names, in the order they are renamed
    std::string why;                  // the refusal or the failure, in words
    std::string blocker;              // kUtMoveTaken: the file in the way, "mods\<leaf>\<name>"
    std::string readRel;              // kUtMoveTaken: the folder the set is read from (utJournalSetRel)
};

namespace folders_detail {

inline bool fileThere(const std::string& p) {
    const DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
}
inline bool anyThere(const std::string& p) {
    return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES;
}
// The files of `dir` matching `pattern` (no folders), sorted.
inline std::vector<std::string> filesLike(const std::string& dir, const std::string& pattern) {
    std::vector<std::string> out;
    WIN32_FIND_DATAA fd;
    const HANDLE h = FindFirstFileA((dir + "\\" + pattern).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        out.push_back(fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end());
    return out;
}

// The rename every step of the move uses: MoveFileExA (never replacing an existing file), or
// g_moveFileHook when one is set. Nothing in the mod sets it; the journal tests fail chosen
// renames (a move back included) with it.
inline bool (*g_moveFileHook)(const char* from, const char* to) = nullptr;
inline bool moveFile(const std::string& from, const std::string& to) {
    if (g_moveFileHook) return g_moveFileHook(from.c_str(), to.c_str());
    return MoveFileExA(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH) != 0;
}

}  // namespace folders_detail

// The folder a set's files are read from and written to, relative to `base`: "" (the root) for
// the main game's sets, and for a mod's set whose .jsonl is still in the root while mods\<leaf>\
// has none (the move has not happened: the set is never split); "mods\<leaf>" otherwise.
inline std::string utJournalSetRel(const std::string& base, const char* setLeaf) {
    namespace fd = folders_detail;
    const std::string rel = utCustomGameRelLeaf(utGameOfSet(setLeaf));
    if (rel.empty()) return rel;
    const std::string stem(setLeaf);
    if (fd::fileThere(base + "\\" + stem + ".jsonl") &&
        !fd::fileThere(base + "\\" + rel + "\\" + stem + ".jsonl"))
        return std::string();
    return rel;
}

// Moves the save set `setLeaf` (a mod's: tq-uniq-items-<leaf>) from `base` into
// `base`\mods\<leaf>\ by the rules above. Pure file system; the caller logs.
inline UtJournalMove utJournalMoveSet(const std::string& base, const char* setLeaf) {
    namespace fd = folders_detail;
    UtJournalMove r;
    const std::string game = utGameOfSet(setLeaf);
    if (base.empty() || game.empty()) return r;
    const std::string stem(setLeaf);
    if (!fd::fileThere(base + "\\" + stem + ".jsonl")) return r;
    // The .csv, the .bad- copies (sorted), the .jsonl LAST: until the last rename the rows are
    // in the root, whole.
    if (fd::fileThere(base + "\\" + stem + ".csv")) r.files.push_back(stem + ".csv");
    for (const std::string& b : fd::filesLike(base, stem + ".jsonl.bad-*")) r.files.push_back(b);
    r.files.push_back(stem + ".jsonl");
    const std::string rel = utCustomGameRelLeaf(game);
    const std::string dst = base + "\\" + rel;
    for (const std::string& f : r.files) {
        if (fd::anyThere(dst + "\\" + f)) {
            r.result = kUtMoveTaken;
            r.blocker = rel + "\\" + f;
            r.why = r.blocker + " already exists";
            r.readRel = utJournalSetRel(base, setLeaf);
            return r;
        }
    }
    if (!utMakeRelDirs(base, rel)) {
        r.result = kUtMoveNoFolder;
        r.why = "the folder " + rel + " could not be created (error " +
                std::to_string((unsigned long)GetLastError()) + ")";
        return r;
    }
    for (size_t i = 0; i < r.files.size(); ++i) {
        const std::string from = base + "\\" + r.files[i];
        const std::string to = dst + "\\" + r.files[i];
        if (fd::moveFile(from, to)) continue;
        r.why = r.files[i] + " could not be moved (error " +
                std::to_string((unsigned long)GetLastError()) + ")";
        r.result = kUtMoveRolledBack;
        for (size_t k = i; k-- > 0;) {   // back to the root, newest first
            const std::string back = base + "\\" + r.files[k];
            if (!fd::moveFile(dst + "\\" + r.files[k], back)) {
                r.result = kUtMoveSplit;
                r.why += "; " + rel + "\\" + r.files[k] + " could NOT be moved back to the root";
            }
        }
        return r;
    }
    r.result = kUtMoveDone;
    return r;
}

// The one log line (or none) for a move's result. `when` says where it ran ("at start",
// "at the world load").
inline void utJournalMoveLog(const char* setLeaf, const UtJournalMove& r, const char* when) {
    const std::string rel = utCustomGameRelLeaf(utGameOfSet(setLeaf));
    std::string list;
    for (const std::string& f : r.files) list += (list.empty() ? "" : ", ") + f;
    if (r.result == kUtMoveDone) {
        logI("files: the journal %s moved into %s\\ (%s) - %zu file(s): %s", setLeaf, rel.c_str(),
             when, r.files.size(), list.c_str());
    } else if (r.result == kUtMoveTaken && !r.readRel.empty()) {
        logW("files: the journal %s was NOT moved into %s\\: %s - both are kept, nothing was "
             "merged, and the mod uses %s\\%s.jsonl. Move or delete one of them by hand",
             setLeaf, rel.c_str(), r.why.c_str(), r.readRel.c_str(), setLeaf);
    } else if (r.result == kUtMoveTaken) {
        logW("files: the journal %s was NOT moved into %s\\: %s - nothing moved, nothing was "
             "merged, and the mod reads the root's %s.jsonl, which holds the rows (%s\\ holds no "
             "journal). Move or delete %s by hand and the next start moves the set",
             setLeaf, rel.c_str(), r.why.c_str(), setLeaf, rel.c_str(), r.blocker.c_str());
    } else if (r.result == kUtMoveNoFolder) {
        logW("files: the journal %s could not be moved into %s\\: %s - nothing moved, the set "
             "stays in the root and is read from there; the move is tried again at the next "
             "start. A file in the way of that folder, or a folder the game may not write to, "
             "stops it",
             setLeaf, rel.c_str(), r.why.c_str());
    } else if (r.result == kUtMoveRolledBack) {
        logW("files: the journal %s could not be moved into %s\\: %s - the files already moved "
             "went back, the set stays in the root and is read from there; the move is tried "
             "again at the next start. A program holding one of them open stops it: close it "
             "and restart the game",
             setLeaf, rel.c_str(), r.why.c_str());
    } else if (r.result == kUtMoveSplit) {
        logW("files: the journal %s could not be moved into %s\\: %s. The rows are in the root's "
             "%s.jsonl, which did not move; move the named file back to the root by hand",
             setLeaf, rel.c_str(), r.why.c_str(), setLeaf);
    }
}

// The start-up step (WORKER THREAD, before the first world): every mod's set still in the root
// moves into its folder - in the mod folder, then in journal_dir when it is another folder (GD's
// utFoldersMigrate: both bases). Silent when there is none - every start after the first.
inline void utJournalMoveAll(const std::string& base) {
    if (base.empty()) return;
    for (const std::string& f : folders_detail::filesLike(base, "tq-uniq-items-*.jsonl")) {
        if (f.size() <= 6 || _stricmp(f.c_str() + f.size() - 6, ".jsonl") != 0) continue;
        const std::string stem = f.substr(0, f.size() - 6);
        if (utGameOfSet(stem.c_str()).empty()) continue;   // the main game's sets stay
        utJournalMoveLog(stem.c_str(), utJournalMoveSet(base, stem.c_str()), "at start");
    }
}

}  // namespace ut
