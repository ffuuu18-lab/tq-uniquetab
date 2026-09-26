// ut_paths.h - THE one place that decides where the mod's files live.
//
// The mod folder is "<the directory the loaded .asi sits in>\uniquetab". The loader takes an .asi
// from beside the exe or from scripts\ / plugins\ under it, so that is <game>\scripts\uniquetab
// for the placement deploy.bat uses and <game>\uniquetab beside TQ.exe - the mod's files follow
// the .asi. It is created the first time it is asked for. EVERYTHING the mod reads and
// everything it writes goes through this header: the catalogue, the database overlay and the
// record lists on the read side; the ini, the log, the journal, the exports and every diagnostic
// file on the write side.
//
// The ONLY fallback is <Documents>\My Games\Titan Quest - Immortal Throne\uniquetab (the plain
// and the OneDrive-redirected Documents folder), a SIBLING of TQ's SaveData folder and never
// inside it, taken when the folder beside the .asi cannot be created or a test file cannot be
// written into it - which is what a read-only game folder looks like. utModDirWarning() then
// returns one line saying so, and the caller logs it once.
//
// %UNIQUETAB_OUT% overrides both. It is not a fallback: it is how the offline test harnesses give
// each case its own empty folder, and it is read before anything else so a test can never reach a
// real installation.
//
// !!! NEVER call SHGetFolderPathW / SHGetKnownFolderPath / CoInitialize / LoadLibrary from this
// !!! file. utModDir() is FIRST called from DllMain, i.e. UNDER THE LOADER LOCK, from a DLL the
// !!! exe loaded through its own import table. Those APIs live in shell32; importing it adds a
// !!! load-order dependency and resolving it lazily would be a LoadLibrary from DllMain - the
// !!! textbook loader-lock deadlock. Documents is therefore reached through the USERPROFILE
// !!! environment variable and a two-candidate check (the plain folder and the OneDrive-redirected
// !!! one). Anyone who does not know this will suggest the "correct" API; the reason is here so
// !!! they cannot.
#pragma once

#include <windows.h>

#include <stddef.h>

namespace ut {

// THE RESOLVER. The mod folder, without a trailing backslash, resolved once and cached for the
// life of the process. `self` is the module handle of this DLL (nullptr in the offline test
// harnesses, where it means the running exe).
const wchar_t* utModDir(HMODULE self);

// "<the mod folder>\<leaf>".
void utModPathW(HMODULE self, const wchar_t* leaf, wchar_t* out, size_t cap);
void utModPathA(HMODULE self, const char* leaf, char* out, size_t cap);

// The mod folder itself, and a short human reason for which of the two candidates won.
void utModDirA(HMODULE self, char* out, size_t cap);
const char* utModDirWhy();

// THE GAME FOLDER: the HOST EXE's own directory (<game>\TQ.exe -> <game>), the only input the
// generated catalogue will read. Never a fallback, never the mod folder.
// It is read from the exe and not from this module on purpose: an .asi in a scripts\ or plugins\
// folder sits one level deeper than the exe, and the game folder must not move with it.
// False when the exe's own path is not available.
bool utGameDirA(char* out, size_t cap);

// The two pure path rules the two resolvers above are built from. Neither touches the disk, and
// both are exposed so the offline harness can check every .asi placement without an installation.
//   utModDirBesideW   <game>\scripts\uniquetab.asi -> <game>\scripts\uniquetab
//   utGameDirFromExeW <game>\TQ.exe                 -> <game>
bool utModDirBesideW(const wchar_t* modulePath, wchar_t* out, size_t cap);
bool utGameDirFromExeW(const wchar_t* hostExe, char* out, size_t cap);

// One line to log when the Documents fallback was taken, else nullptr. The mod folder is resolved
// in DllMain, before the log file exists, so the warning cannot report itself.
const char* utModDirWarning();

// A file the mod SHIPS (catalogue.bin, uniq-*.arz, uniq-*.txt). Looks in the mod folder and,
// when that is the Documents fallback, beside the .asi as well - the two places the
// resolver above knows about and no other. Writes the winner into `out` and returns whether it
// exists; on a miss `out` holds the primary path, which is what an error line should name.
bool utModFile(HMODULE self, const char* leaf, char* out, size_t cap);

}  // namespace ut
