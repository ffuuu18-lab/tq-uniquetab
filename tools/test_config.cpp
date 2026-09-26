// test_config.cpp - the settings file, offline. No game, no engine.
//
//   tools\build_test_config.bat        (needs a vcvars32 shell; the .bat calls it itself)
//
// Everything it expects is DERIVED FROM THE KEY TABLE in src\ut_config.h - there is no magic
// byte count and no second copy of the key list here. It proves, in a temp directory:
//   1. the table and the struct agree on every default (they are written in two places);
//   2. a fresh file is the whole rendered template and names every key exactly once;
//   3. a round trip: a value of each type survives file -> parser -> file;
//   4. a VERSION BUMP KEEPS the player's values, and takes the default for keys the old file did
//      not have - the thing a bump used to destroy;
//   5. a bump drops a key this build no longer has, and says which in the log;
//   6. a value outside its range is clamped and logged, and a word that is not one of the
//      choices is refused;
//   5b. a GRIM DAWN uniquetab.ini (ini_version 36) is migrated by the version rule - the keys
//      this build knows keep their values, the rest are dropped and named - and rewritten;
//   7. the persist write-back (configPersistInt) keeps the rest of the file byte for byte,
//      comments included;
//   8. a RESCUE-NOW file is LEFT ALONE: has no rescue, so nothing may consume it;
//   9. enabled is not a setting in an enabled=0 line is named as unknown and changes nothing.
//  10. THE PATH RULES (src\ut_paths.cpp), for both placements the loader allows: the .asi beside
//      TQ.exe and the .asi in a scripts\ folder. The mod folder follows the .asi; the game
//      folder is the host exe's own folder and does not move with it.
// Exit code 0 = all of it held.
#include <windows.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/ut_config.h"
#include "../src/ut_log.h"
#include "../src/ut_paths.h"

// ---- ut_log stubs, with a transcript the checks can search ---------------------------------
static char g_logBuf[65536];
static size_t g_logLen = 0;

static void logClear() {
    g_logBuf[0] = 0;
    g_logLen = 0;
}

static bool logHas(const char* needle) { return strstr(g_logBuf, needle) != nullptr; }

namespace ut {
volatile long g_logLevel = UT_LOG_TRACE;  // the harness wants every line, whatever the ini says
void logSetLevel(const char*) {}
void logAtV(int level, const char* fmt, va_list ap) {
    char line[2048];
    _vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, ap);
    const char* tags = "EWIDT";
    printf("    [log %c] %s\n", tags[(level < 0 || level > 4) ? 2 : level], line);
    const size_t n = strlen(line);
    if (g_logLen + n + 2 < sizeof(g_logBuf)) {
        memcpy(g_logBuf + g_logLen, line, n);
        g_logLen += n;
        g_logBuf[g_logLen++] = '\n';
        g_logBuf[g_logLen] = 0;
    }
}
void logSetFlushEachLine(int) {}
}  // namespace ut

static int g_fails = 0;
static void check(bool ok, const char* what) {
    printf("  %-62s %s\n", what, ok ? "OK" : "FAIL");
    if (!ok) ++g_fails;
}

static DWORD fileSize(const wchar_t* p) {
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(p, GetFileExInfoStandard, &fad)) return 0;
    return fad.nFileSizeLow;
}

static char* readAll(const wchar_t* p, DWORD* len) {
    *len = 0;
    HANDLE h = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return nullptr;
    const DWORD size = GetFileSize(h, nullptr);
    char* buf = (char*)malloc((size_t)size + 1);
    if (!buf) {
        CloseHandle(h);
        return nullptr;
    }
    DWORD got = 0;
    ReadFile(h, buf, size, &got, nullptr);
    CloseHandle(h);
    buf[got] = 0;
    *len = got;
    return buf;
}

// ut_config.cpp skips the parse when the file's last-write time EQUALS the one it saw last, so
// every write here has to land on a stamp that differs from the one the file already carries -
// whoever wrote it, this harness or configEnsure. The clock cannot give that: it moves in ~15 ms
// steps, so two writes inside one step share a stamp and the reload is skipped (a rare 5-failure
// run of section 3, which is what the old Sleep(40) was guarding against). The stamp is set
// explicitly and forced strictly past both the file's own and the last one written here.
static ULONGLONG g_lastStamp = 0;   // 100 ns units: the stamp this harness last wrote

static void writeAll(const wchar_t* p, const char* text, DWORD len) {
    ULONGLONG prev = 0;
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (GetFileAttributesExW(p, GetFileExInfoStandard, &fa))
        prev = ((ULONGLONG)fa.ftLastWriteTime.dwHighDateTime << 32) | fa.ftLastWriteTime.dwLowDateTime;
    HANDLE h = CreateFileW(p, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD w = 0;
    WriteFile(h, text, len, &w, nullptr);
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    ULONGLONG t = ((ULONGLONG)now.dwHighDateTime << 32) | now.dwLowDateTime;
    const ULONGLONG second = 10000000ULL;
    if (t <= prev) t = prev + second;
    if (t <= g_lastStamp) t = g_lastStamp + second;
    g_lastStamp = t;
    FILETIME ft;
    ft.dwLowDateTime = (DWORD)t;
    ft.dwHighDateTime = (DWORD)(t >> 32);
    SetFileTime(h, nullptr, nullptr, &ft);   // the system stops stamping this handle itself
    CloseHandle(h);
}

static void writeText(const wchar_t* p, const char* text) {
    writeAll(p, text, (DWORD)strlen(text));
}

// Replaces the first `find` with `repl` IN PLACE. Both must be the same length, so the rest of
// the file - and every offset in it - is untouched.
static bool patch(char* text, const char* find, const char* repl) {
    if (strlen(find) != strlen(repl)) return false;
    char* at = strstr(text, find);
    if (!at) return false;
    memcpy(at, repl, strlen(repl));
    return true;
}

// The merge: ini_version went from one digit to two (9 -> 10), so "one version old" is a
// SHORTER string - replace it and move the rest of the text up (repl no longer than find).
static bool patchShorter(char* text, DWORD* len, const char* find, const char* repl) {
    const size_t f = strlen(find), r = strlen(repl);
    if (r > f) return false;
    char* at = strstr(text, find);
    if (!at) return false;
    memcpy(at, repl, r);
    memmove(at + r, at + f, strlen(at + f) + 1);
    *len -= (DWORD)(f - r);
    return true;
}

// The value of one table row, out of a UtConfig.
static int intOf(const UtConfig& c, const ut::UtCfgKey& k) {
    return *(const int*)((const char*)&c + k.off);
}
static const char* strOf(const UtConfig& c, const ut::UtCfgKey& k) {
    return (const char*)&c + k.off;
}

// "\r\nname=" - a key at the start of a line, which is how the renderer writes every one of them.
static int countKeyLines(const char* text, const char* name) {
    char needle[96];
    _snprintf_s(needle, sizeof(needle), _TRUNCATE, "\r\n%s=", name);
    int n = 0;
    for (const char* p = strstr(text, needle); p; p = strstr(p + 1, needle)) ++n;
    return n;
}

int main() {
    // A folder of this run's own: two harnesses started at once, or one left behind by a killed
    // run, must never read each other's ini.
    wchar_t dir[MAX_PATH];
    GetTempPathW(MAX_PATH, dir);
    wchar_t leaf[64];
    _snwprintf_s(leaf, _TRUNCATE, L"ut-config-test-%lu-%llu", GetCurrentProcessId(),
                 (unsigned long long)GetTickCount64());
    wcscat_s(dir, leaf);
    if (!CreateDirectoryW(dir, nullptr)) {
        printf("cannot create %S (error %lu)\n", dir, GetLastError());
        return 2;
    }
    wchar_t ini[MAX_PATH], trigger[MAX_PATH], triggerTxt[MAX_PATH];
    _snwprintf_s(ini, _TRUNCATE, L"%s\\uniquetab.ini", dir);
    _snwprintf_s(trigger, _TRUNCATE, L"%s\\RESCUE-NOW", dir);
    _snwprintf_s(triggerTxt, _TRUNCATE, L"%s\\RESCUE-NOW.txt", dir);
    DeleteFileW(ini);
    DeleteFileW(trigger);
    DeleteFileW(triggerTxt);
    printf("settings file = %S\n", ini);
    printf("%d keys in %d sections, a full file is %u bytes\n\n", ut::kUtCfgKeyCount,
           ut::kUtCfgSectionCount, (unsigned)ut::configTemplateBytes());

    // ---- 1. the table and the struct agree ---------------------------------------------------
    printf("1. the table's defaults are the struct's defaults\n");
    {
        const UtConfig d;
        int bad = 0;
        for (int i = 0; i < ut::kUtCfgKeyCount; ++i) {
            const ut::UtCfgKey& k = ut::kUtCfgKeys[i];
            if (k.type == ut::kUtCfgStr) {
                if (strcmp(strOf(d, k), k.defStr) != 0) {
                    printf("    %s: table \"%s\" vs struct \"%s\"\n", k.name, k.defStr,
                           strOf(d, k));
                    ++bad;
                }
            } else {
                if (intOf(d, k) != k.def) {
                    printf("    %s: table %d vs struct %d\n", k.name, k.def, intOf(d, k));
                    ++bad;
                }
            }
            if (k.type != ut::kUtCfgStr && (k.def < k.lo || k.def > k.hi)) {
                printf("    %s: the default %d is outside its own range %d..%d\n", k.name, k.def,
                       k.lo, k.hi);
                ++bad;
            }
            for (int j = 0; j < i; ++j)
                if (!strcmp(ut::kUtCfgKeys[j].name, k.name)) {
                    printf("    %s: the table names it twice\n", k.name);
                    ++bad;
                }
        }
        check(bad == 0, "every row matches its member, and no name is used twice");
    }

    // ---- 2. a fresh file ---------------------------------------------------------------------
    printf("\n2. the first run writes the whole file\n");
    logClear();
    ut::configReload(ini);  // no file yet -> writes it, returns false
    const DWORD tpl = fileSize(ini);
    printf("    on disk = %u bytes\n", tpl);
    check(tpl == (DWORD)ut::configTemplateBytes(), "the file is exactly what the table renders");
    check(ut::configReload(ini), "the second reload parses it");
    check(ut::g_cfg.iniVersion == UT_INI_VERSION, "ini_version matches UT_INI_VERSION");
    {
        DWORD len = 0;
        char* text = readAll(ini, &len);
        int missing = 0, dupes = 0;
        for (int i = 0; i < ut::kUtCfgKeyCount; ++i) {
            const int n = countKeyLines(text, ut::kUtCfgKeys[i].name);
            if (n == 0) {
                printf("    %s is not in the file\n", ut::kUtCfgKeys[i].name);
                ++missing;
            } else if (n > 1) {
                printf("    %s is in the file %d times\n", ut::kUtCfgKeys[i].name, n);
                ++dupes;
            }
        }
        check(missing == 0 && dupes == 0, "every key is on exactly one line of its own");
        int sections = 0;
        for (int s = 0; s < ut::kUtCfgSectionCount; ++s) {
            char head[64];
            _snprintf_s(head, sizeof(head), _TRUNCATE, "\r\n[%s]\r\n", ut::kUtCfgSections[s].name);
            if (strstr(text, head)) ++sections;
        }
        check(sections == ut::kUtCfgSectionCount, "every section has its own [block] header");
        check(strstr(text, "; Unique Collection Tab") != nullptr, "the file says what it is");
        free(text);
    }
    {
        const UtConfig d;
        int bad = 0;
        for (int i = 0; i < ut::kUtCfgKeyCount; ++i) {
            const ut::UtCfgKey& k = ut::kUtCfgKeys[i];
            if (k.type == ut::kUtCfgStr) {
                if (strcmp(strOf(ut::g_cfg, k), strOf(d, k)) != 0) ++bad;
            } else if (intOf(ut::g_cfg, k) != intOf(d, k)) {
                ++bad;
            }
        }
        check(bad == 0, "a fresh file parses back to the defaults, key for key");
    }

    // ---- 3. a round trip ---------------------------------------------------------------------
    printf("\n3. a round trip through the file\n");
    {
        char edited[512];
        _snprintf_s(edited, sizeof(edited), _TRUNCATE,
                    "ini_version=%d\r\n"
                    "[collection]\r\n"
                    "  mp_collect = 0   ; a comment, and blanks around the '='\r\n"
                    "log_level=warn   ; and one after a word\r\n",
                    UT_INI_VERSION);
        writeText(ini, edited);
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.mpCollect == 0, "mp_collect=0 came back");
        check(!strcmp(ut::g_cfg.logLevel, "warn"),
              "log_level=warn came back and lost the comment and the padding");
        check(!logHas("migrated"), "a current file is parsed, not migrated");
        char again[16384];
        const size_t n = ut::configRender(again, sizeof(again), ut::g_cfg);
        check(n > 0 && strstr(again, "mp_collect=0") && strstr(again, "log_level=warn"),
              "rendering what was parsed gives the same two values back");
    }

    // ---- 4. a version bump KEEPS the player's values -------------------------------------------
    printf("\n4. a version bump keeps what you changed\n");
    {
        DWORD len = 0;
        char* text = readAll(ini, &len);
        char older[32];
        _snprintf_s(older, sizeof(older), _TRUNCATE, "ini_version=%d", UT_INI_VERSION - 1);
        char current[32];
        _snprintf_s(current, sizeof(current), _TRUNCATE, "ini_version=%d", UT_INI_VERSION);
        check(patchShorter(text, &len, current, older), "pretend the file is one version old");
        writeAll(ini, text, len);
        free(text);
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.mpCollect == 0, "mp_collect=0 SURVIVED the bump");
        check(!strcmp(ut::g_cfg.logLevel, "warn"), "log_level=warn survived the bump");
        check(ut::g_cfg.logFlushEachLine == 0,
              "the key the old file did not have took its default");
        check(ut::g_cfg.iniVersion == UT_INI_VERSION, "the version is the current one now");
        check(logHas("migrated"), "the log says it migrated the file");
        text = readAll(ini, &len);
        check(strstr(text, current) != nullptr, "the file carries the new version");
        check(strstr(text, "mp_collect=0") != nullptr, "and the kept value");
        check(len == (DWORD)ut::configTemplateBytes(), "and is a complete file again");
        free(text);
    }

    // ---- 5. a bump from a file that predates most of the keys --------------------------------
    printf("\n5. a bump from an old, short file\n");
    {
        char old[512];
        _snprintf_s(old, sizeof(old), _TRUNCATE,
                    "ini_version=%d\r\n"
                    "mp_collect=0\r\n"
                    "drop_trace=1\r\n"
                    "tab_col_x=417\r\n",
                    UT_INI_VERSION - 1);
        writeText(ini, old);
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.mpCollect == 0, "the one setting it had was kept");
        check(!strcmp(ut::g_cfg.logLevel, "info") && ut::g_cfg.logFlushEachLine == 0,
              "every key the old file lacked took its default");
        check(logHas("drop_trace") && logHas("tab_col_x"),
              "the log names both keys this build does not have");
        check(logHas("2 setting(s) this build no longer has"), "and says how many were dropped");
        DWORD len = 0;
        char* text = readAll(ini, &len);
        check(strstr(text, "drop_trace") == nullptr && strstr(text, "tab_col_x") == nullptr,
              "they are gone from the rewritten file");
        check(len == (DWORD)ut::configTemplateBytes(), "which is a complete file");
        check(strstr(text, "mp_collect=0") != nullptr, "holding the value that was kept");
        free(text);
    }

    // ---- 5w. slot_plates=3 (the equipment-slot art) is the default; the old default 1
    // is moved ONCE, a 0 / 2 stays, and a 1 in a current file is the player's -------------------
    printf("\n5w. slot_plates=3 by default (the equipment window's slot art), migrated once\n");
    {
        const UtConfig d;
        check(d.slotPlates == 3, "the default: slot_plates=3");
        check(UT_INI_VERSION >= 11 && UT_SLOT_ART_SINCE == 11, "slot_plates=3 since ini_version 11");
        char old[512];
        _snprintf_s(old, sizeof(old), _TRUNCATE,
                    "ini_version=10\r\n[display]\r\nslot_plates=1\r\ngrid_cover=0\r\n");
        writeText(ini, old);
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.slotPlates == 3 && ut::g_cfg.gridCover == 0,
              "ini_version 10 reading slot_plates=1 (the old default) -> 3, the rest kept");
        char mig[64];
        _snprintf_s(mig, sizeof(mig), _TRUNCATE, "migrated from ini_version=10 to %d", UT_INI_VERSION);
        check(logHas("slot_plates=1 was the old default") && logHas(mig),
              "and the log says so");
        DWORD len = 0;
        char* text = readAll(ini, &len);
        char cur[32];
        _snprintf_s(cur, sizeof(cur), _TRUNCATE, "ini_version=%d", UT_INI_VERSION);
        check(text && strstr(text, "\r\nslot_plates=3") != nullptr && strstr(text, cur) != nullptr,
              "the rewritten file says slot_plates=3 and the current ini_version");
        check(text && patch(text, "\r\nslot_plates=3", "\r\nslot_plates=1"),
              "the user sets slot_plates=1 (the flat plate) in the current file");
        if (text) {
            writeAll(ini, text, len);
            free(text);
        }
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.slotPlates == 1 && !logHas("slot_plates=1 was the old default") &&
                  !logHas("migrated"),
              "a 1 set in a current file stays 1, nothing migrated again");
        for (int v = 0; v <= 2; v += 2) {
            _snprintf_s(old, sizeof(old), _TRUNCATE, "ini_version=10\r\n[display]\r\nslot_plates=%d\r\n", v);
            writeText(ini, old);
            logClear();
            ut::configReload(ini);
            check(ut::g_cfg.slotPlates == v && !logHas("slot_plates=1 was the old default"),
                  v == 0 ? "ini_version 10 with slot_plates=0 (the player's) stays 0"
                         : "ini_version 10 with slot_plates=2 (the player's) stays 2");
        }
        _snprintf_s(old, sizeof(old), _TRUNCATE, "ini_version=11\r\n[display]\r\nslot_plates=4\r\n");
        writeText(ini, old);
        ut::configReload(ini);
        check(ut::g_cfg.slotPlates >= 0 && ut::g_cfg.slotPlates <= 3,
              "slot_plates=4 is out of range (0..3) and is not taken");
    }

    // ---- 5x. owned_marks=3 (the icons in gray) is the default; the old default 1 is
    // moved ONCE, a 0 / 2 stays, and a 1 in a current file is the player's -----------------------
    printf("\n5x. owned_marks=3 by default (the uncollected icons in gray), migrated once\n");
    {
        const UtConfig d;
        check(d.ownedMarks == 3, "the default: owned_marks=3");
        check(UT_INI_VERSION == 17 && UT_GRAY_ICONS_SINCE == 13, "ini_version 17, the gray default since 13");
        char old[512];
        _snprintf_s(old, sizeof(old), _TRUNCATE,
                    "ini_version=11\r\n[display]\r\nowned_marks=1\r\nslot_plates=2\r\n");
        writeText(ini, old);
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.ownedMarks == 3 && ut::g_cfg.slotPlates == 2,
              "ini_version 11 reading owned_marks=1 (the old default) -> 3, the rest kept");
        check(logHas("owned_marks=1 was the old default") && logHas("migrated from ini_version=11 to 17"),
              "and the log says so");
        DWORD len = 0;
        char* text = readAll(ini, &len);
        check(text && strstr(text, "\r\nowned_marks=3") != nullptr && strstr(text, "ini_version=17") != nullptr,
              "the rewritten file says owned_marks=3 and ini_version=17");
        check(text && patch(text, "\r\nowned_marks=3", "\r\nowned_marks=1"),
              "the user sets owned_marks=1 (the veil) in the current file");
        if (text) {
            writeAll(ini, text, len);
            free(text);
        }
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.ownedMarks == 1 && !logHas("owned_marks=1 was the old default") &&
                  !logHas("migrated"),
              "a 1 set in a current file stays 1, nothing migrated again");
        _snprintf_s(old, sizeof(old), _TRUNCATE, "ini_version=12\r\n[display]\r\nowned_marks=1\r\n");
        writeText(ini, old);
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.ownedMarks == 3 && logHas("owned_marks=1 was the old default") &&
                  logHas("migrated from ini_version=12 to 17"),
              "the merge rule: a branch's ini_version 12 with owned_marks=1 is moved to 3 once (12 is before the gray default)");
        for (int v = 0; v <= 2; v += 2) {
            _snprintf_s(old, sizeof(old), _TRUNCATE, "ini_version=11\r\n[display]\r\nowned_marks=%d\r\n", v);
            writeText(ini, old);
            logClear();
            ut::configReload(ini);
            check(ut::g_cfg.ownedMarks == v && !logHas("owned_marks=1 was the old default"),
                  v == 0 ? "ini_version 11 with owned_marks=0 (the player's) stays 0"
                         : "ini_version 11 with owned_marks=2 (the player's) stays 2");
        }
        _snprintf_s(old, sizeof(old), _TRUNCATE, "ini_version=13\r\n[display]\r\nowned_marks=4\r\n");
        writeText(ini, old);
        ut::configReload(ini);
        check(ut::g_cfg.ownedMarks >= 0 && ut::g_cfg.ownedMarks <= 3,
              "owned_marks=4 is out of range (0..3) and is not taken");
    }

    // ---- 5y. ini_version 12 (13 after the merge) changes no value; it rewrites mp_collect's comment ----
    printf("\n5y. an ini_version 11 file (the old mp_collect comment) is rewritten, every value kept\n");
    {
        check(UT_INI_VERSION == 17, "ini_version 17 (13: the comment bump merged with 12; 14: the search keys)");
        writeText(ini,
                  "ini_version=11\r\n[collection]\r\n"
                  "; read and kept (GD's key); in this version deposits and takes are ALWAYS refused "
                  "while you play multiplayer, whatever it says\r\nmp_collect=0\r\n"
                  "[display]\r\nslot_plates=1\r\nhave_marks=1\r\ngrid_cover=0\r\n");
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.iniVersion == 17 && logHas("migrated from ini_version=11 to 17"),
              "ini_version 11 is migrated to 17, and the log says so");
        check(ut::g_cfg.mpCollect == 0 && ut::g_cfg.slotPlates == 1 && ut::g_cfg.haveMarks == 1 &&
                  ut::g_cfg.gridCover == 0,
              "every value kept: mp_collect=0, slot_plates=1 and have_marks=1 (the player's), grid_cover=0");
        check(!logHas("slot_plates=1 was the old default") && !logHas("have_marks=1 was the old default"),
              "and no default is moved (11 is past both of those migrations)");
        DWORD len = 0;
        char* text = readAll(ini, &len);
        check(text && strstr(text, "ALWAYS refused") == nullptr &&
                  strstr(text, "; 1 = the collection works in a MULTIPLAYER game (hosted or joined)") != nullptr &&
                  strstr(text, "\r\nmp_collect=0") != nullptr && strstr(text, "ini_version=17") != nullptr,
              "the file now carries the new mp_collect comment, the kept value and ini_version=17");
        free(text);
        logClear();
        ut::configReload(ini);
        check(!logHas("migrated") && ut::g_cfg.mpCollect == 0, "read again: nothing migrated, the value stays");
        writeText(ini,
                  "ini_version=12\r\n[collection]\r\n"
                  "; read and kept (GD's key); in this version deposits and takes are ALWAYS refused "
                  "while you play multiplayer, whatever it says\r\nmp_collect=1\r\n"
                  "[display]\r\nowned_marks=2\r\n");
        logClear();
        ut::configReload(ini);
        text = readAll(ini, &len);
        check(ut::g_cfg.iniVersion == 17 && logHas("migrated from ini_version=12 to 17") &&
                  ut::g_cfg.mpCollect == 1 && ut::g_cfg.ownedMarks == 2 && text &&
                  strstr(text, "ALWAYS refused") == nullptr,
              "the merge: a branch ini_version 12 (the old comment) gets the new comment too, values kept");
        free(text);
    }

    // ---- 5a. have_marks is off by default; the old default 1 is flipped ONCE -----------
    printf("\n5a. have_marks off by default (the tooltip line replaces it), migrated once\n");
    {
        const UtConfig d;
        check(d.haveMarks == 0 && d.tooltipMark == 1, "the defaults: have_marks=0, tooltip_mark=1");
        check(d.tooltipClassYes == 22 && d.tooltipClassNo == 24 && !d.tooltipTextYes[0] &&
                  !d.tooltipTextNo[0],
              "tooltip_class_yes=22, tooltip_class_no=24, both texts empty (built-in wording)");
        char old[512];
        _snprintf_s(old, sizeof(old), _TRUNCATE,
                    "ini_version=7\r\n[display]\r\nhave_marks=1\r\nowned_marks=2\r\n");
        writeText(ini, old);
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.haveMarks == 0, "ini_version 7 reading have_marks=1 (the old default) -> 0");
        check(ut::g_cfg.ownedMarks == 2, "every other value is kept");
        check(logHas("have_marks=1 was the old default"), "and the log says so, once");
        DWORD len = 0;
        char* text = readAll(ini, &len);
        check(text && strstr(text, "\r\nhave_marks=0") != nullptr,
              "the rewritten, current file says have_marks=0");
        check(text && strstr(text, "\r\ntooltip_mark=1") != nullptr &&
                  strstr(text, "\r\ntooltip_class_yes=22") != nullptr &&
                  strstr(text, "\r\ntooltip_class_no=24") != nullptr,
              "and carries GD's tooltip keys with TQ's defaults");
        check(text && patch(text, "\r\nhave_marks=0", "\r\nhave_marks=1"),
              "the user sets have_marks=1 in the current file");
        if (text) {
            writeAll(ini, text, len);
            free(text);
        }
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.haveMarks == 1, "a 1 set in a current file stays 1");
        check(!logHas("old default") && !logHas("migrated"), "and nothing is migrated again");

        _snprintf_s(old, sizeof(old), _TRUNCATE, "ini_version=7\r\n[display]\r\nhave_marks=0\r\n");
        writeText(ini, old);
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.haveMarks == 0 && !logHas("old default"),
              "ini_version 7 with have_marks=0 (the player's) -> stays 0, nothing said");

        _snprintf_s(old, sizeof(old), _TRUNCATE, "ini_version=8\r\n[display]\r\nhave_marks=1\r\n");
        writeText(ini, old);
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.haveMarks == 0 && logHas("old default"),
              "ini_version 8 (the earlier branch) reading 1 -> flipped as well");

        // The merge (ini_version 10): 8 is the earlier grid_cover file, 9 the branch's.
        _snprintf_s(old, sizeof(old), _TRUNCATE,
                    "ini_version=8\r\n[display]\r\nhave_marks=1\r\ngrid_cover=0\r\n");
        writeText(ini, old);
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.haveMarks == 0 && ut::g_cfg.gridCover == 0 && ut::g_cfg.tooltipMark == 1,
              "merge: a file (8) keeps grid_cover=0, its have_marks=1 is flipped, the "
              "tooltip keys take their defaults");

        _snprintf_s(old, sizeof(old), _TRUNCATE,
                    "ini_version=9\r\n[display]\r\nhave_marks=1\r\ntooltip_mark=0\r\n");
        writeText(ini, old);
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.haveMarks == 1 && ut::g_cfg.tooltipMark == 0 && !logHas("old default"),
              "merge: a branch file (9, written with the default 0) keeps have_marks=1 and "
              "tooltip_mark=0, nothing flipped");
        check(ut::g_cfg.gridCover == 1 && ut::g_cfg.iniVersion == UT_INI_VERSION &&
                  logHas("migrated from ini_version=9"),
              "merge: and it gains grid_cover=1 (the default) and the current ini_version");
        {
            DWORD len9 = 0;
            char* text9 = readAll(ini, &len9);
            check(text9 && strstr(text9, "\r\ngrid_cover=1") != nullptr &&
                      strstr(text9, "\r\nhave_marks=1") != nullptr,
                  "merge: the rewritten file carries grid_cover=1 and the player's have_marks=1");
            free(text9);
        }

        _snprintf_s(old, sizeof(old), _TRUNCATE, "ini_version=5\r\nmp_collect=0\r\n");
        writeText(ini, old);
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.haveMarks == 0 && ut::g_cfg.mpCollect == 0,
              "a file from before the key (5) -> the new default 0");
    }

    // ---- 5z. the property search's keys (ini_version 16) ---------------------------------------
    printf("\n5z. the search keys: the defaults, the values read, a file from 13 gains them\n");
    {
        const UtConfig d;
        check(d.search == 1 && d.searchButtons == 1 && d.searchLore == 0 && d.searchPrebuild == 0 &&
                  d.searchDebugQuery[0] == 0,
              "the defaults: search=1, search_buttons=1, search_lore=0, search_prebuild=0, "
              "search_debug_query empty");
        char cur[512];
        _snprintf_s(cur, sizeof(cur), _TRUNCATE,
                    "ini_version=%d\r\n[display]\r\nsearch=0\r\nsearch_buttons=0\r\n"
                    "search_lore=1\r\n[advanced]\r\nsearch_prebuild=1\r\n"
                    "search_debug_query=Fire Resistance\r\n",
                    UT_INI_VERSION);
        writeText(ini, cur);
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.search == 0 && ut::g_cfg.searchButtons == 0 && ut::g_cfg.searchLore == 1 &&
                  ut::g_cfg.searchPrebuild == 1,
              "search=0, search_buttons=0, search_lore=1 and search_prebuild=1 are read");
        check(!strcmp(ut::g_cfg.searchDebugQuery, "Fire Resistance"),
              "search_debug_query is read as text, spaces inside kept");
        _snprintf_s(cur, sizeof(cur), _TRUNCATE,
                    "ini_version=%d\r\n[display]\r\nsearch=7\r\n[advanced]\r\n"
                    "search_debug_query=\r\n",
                    UT_INI_VERSION);
        writeText(ini, cur);
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.search == 1 && logHas("search=7 is outside 0..1"),
              "search=7 is clamped to its range, and the log says so");
        check(ut::g_cfg.searchDebugQuery[0] == 0, "an empty search_debug_query = no query");
        writeText(ini, "ini_version=13\r\n[display]\r\nowned_marks=2\r\n");
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.iniVersion == 17 && logHas("migrated from ini_version=13 to 17") &&
                  ut::g_cfg.ownedMarks == 2 && ut::g_cfg.search == 1 && ut::g_cfg.searchButtons == 1 &&
                  ut::g_cfg.searchLore == 0 && ut::g_cfg.searchPrebuild == 0 &&
                  ut::g_cfg.searchDebugQuery[0] == 0,
              "a file from 13 is migrated: its values kept, the five search keys at their defaults");
        DWORD len = 0;
        char* text = readAll(ini, &len);
        check(text && strstr(text, "\r\nsearch=1") != nullptr &&
                  strstr(text, "\r\nsearch_buttons=1") != nullptr &&
                  strstr(text, "\r\nsearch_lore=0") != nullptr &&
                  strstr(text, "\r\nsearch_prebuild=0") != nullptr &&
                  strstr(text, "\r\nsearch_debug_query=") != nullptr,
              "and the rewritten file carries all five");
        free(text);
        // search_mark (ini_version 16): the highlight's look, 1 frame / 2 wash / 3 both
        check(d.searchMark == 1, "search_mark defaults to 1 (a thin frame)");
        _snprintf_s(cur, sizeof(cur), _TRUNCATE, "ini_version=%d\r\n[display]\r\nsearch_mark=1\r\n",
                    UT_INI_VERSION);
        writeText(ini, cur);
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.searchMark == 1, "search_mark=1 is read");
        _snprintf_s(cur, sizeof(cur), _TRUNCATE, "ini_version=%d\r\n[display]\r\nsearch_mark=0\r\n",
                    UT_INI_VERSION);
        writeText(ini, cur);
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.searchMark == 1 && logHas("search_mark=0 is outside 1..3"),
              "search_mark=0 is clamped to 1, and the log says so");
        writeText(ini, "ini_version=14\r\n[display]\r\nsearch_buttons=0\r\n");
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.iniVersion == 17 && logHas("migrated from ini_version=14 to 17") &&
                  ut::g_cfg.searchButtons == 0 && ut::g_cfg.searchMark == 1,
              "a file from 14 is migrated: its values kept, search_mark at its default");
        text = readAll(ini, &len);
        check(text && strstr(text, "\r\nsearch_mark=1") != nullptr, "and the rewritten file carries it");
        free(text);
    }

    // ---- 5za. search_mark's default 1 and search_mark_color (ini_version 16) -------------------
    printf("\n5za. search_mark=1 by default, search_mark_color, a 15 file migrated once\n");
    {
        const UtConfig d;
        check(d.searchMark == 1 && !strcmp(d.searchMarkColor, "gold") && UT_INI_VERSION == 17 &&
                  UT_SEARCH_FRAME_SINCE == 16,
              "the defaults: search_mark=1 (a thin frame), search_mark_color=gold; ini_version 17");
        char cur[512];
        // a name (any case), hex in both cases, a comment after the value: the '#' that starts the
        // value is kept
        static const char* const good[][2] = {
            {"green", "green"}, {"Blue", "Blue"}, {"#3CF03C", "#3CF03C"}, {"#ffd700 ; gold", "#ffd700"},
            {"#456c93 # the old blue", "#456c93"}, {"magenta", "magenta"}};
        for (const auto& gv : good) {
            _snprintf_s(cur, sizeof(cur), _TRUNCATE, "ini_version=%d\r\n[display]\r\nsearch_mark_color=%s\r\n",
                        UT_INI_VERSION, gv[0]);
            writeText(ini, cur);
            logClear();
            ut::configReload(ini);
            char what[160];
            _snprintf_s(what, sizeof(what), _TRUNCATE, "search_mark_color=%s is read as \"%s\"", gv[0], gv[1]);
            check(!strcmp(ut::g_cfg.searchMarkColor, gv[1]) && !logHas("is not a colour name"), what);
        }
        static const char* const bad[] = {"purple", "#12345", "#1234567", "#gg0000", "ffd700", "# ffd700", ""};
        for (const char* bv : bad) {
            _snprintf_s(cur, sizeof(cur), _TRUNCATE, "ini_version=%d\r\n[display]\r\nsearch_mark_color=%s\r\n",
                        UT_INI_VERSION, bv);
            writeText(ini, cur);
            logClear();
            ut::configReload(ini);
            char what[160];
            _snprintf_s(what, sizeof(what), _TRUNCATE, "search_mark_color=%s is refused: gold, and the log says so", bv);
            check(!strcmp(ut::g_cfg.searchMarkColor, "gold") && logHas("is not a colour name"), what);
        }
        // only search_mark_color keeps a leading '#': for every other key it starts a comment, as
        // before (a string key and an int key)
        _snprintf_s(cur, sizeof(cur), _TRUNCATE,
                    "ini_version=%d\r\n[display]\r\nsearch_mark=#2\r\n[advanced]\r\nsearch_debug_query=#fire\r\n",
                    UT_INI_VERSION);
        writeText(ini, cur);
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.searchMark != 2 && ut::g_cfg.searchDebugQuery[0] == 0,
              "search_mark=#2 and search_debug_query=#fire are comments: the '#' is kept for the colour only");
        _snprintf_s(cur, sizeof(cur), _TRUNCATE, "ini_version=%d\r\n[display]\r\nsearch_mark=2\r\n", UT_INI_VERSION);
        writeText(ini, cur);
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.searchMark == 2 && !logHas("migrated"), "search_mark=2 in a current file is read");
        // a 15 file: the old default 3 moves to 1 once, every other value is kept
        writeText(ini,
                  "ini_version=15\r\n[display]\r\nsearch_mark=3\r\nsearch_buttons=0\r\nowned_marks=2\r\n"
                  "search_lore=1\r\n[advanced]\r\npad_y=8\r\nplate_label_size=16\r\nsearch_debug_query=fire\r\n");
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.iniVersion == 17 && ut::g_cfg.searchMark == 1 &&
                  logHas("search_mark=3 was the old default") && logHas("migrated from ini_version=15 to 17"),
              "a 15 file reading search_mark=3 (the old default) -> 1, and the log says so");
        check(ut::g_cfg.searchButtons == 0 && ut::g_cfg.ownedMarks == 2 && ut::g_cfg.searchLore == 1 &&
                  ut::g_cfg.padY == 8 && ut::g_cfg.plateLabelSize == 16 &&
                  !strcmp(ut::g_cfg.searchDebugQuery, "fire") && !strcmp(ut::g_cfg.searchMarkColor, "gold"),
              "every other value of the 15 file is kept; search_mark_color comes at gold");
        DWORD len = 0;
        char* text = readAll(ini, &len);
        check(text && strstr(text, "\r\nsearch_mark=1") != nullptr &&
                  strstr(text, "\r\nsearch_mark_color=gold") != nullptr && strstr(text, "ini_version=17") != nullptr,
              "the rewritten file says search_mark=1, search_mark_color=gold and ini_version=17");
        check(text && patch(text, "\r\nsearch_mark=1", "\r\nsearch_mark=3"),
              "the user sets search_mark=3 (the frame and the wash) in the current file");
        if (text) {
            writeAll(ini, text, len);
            free(text);
        }
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.searchMark == 3 && !logHas("was the old default") && !logHas("migrated"),
              "a 3 set in a current file stays 3, nothing migrated again");
        for (int v = 1; v <= 2; ++v) {
            _snprintf_s(cur, sizeof(cur), _TRUNCATE, "ini_version=15\r\n[display]\r\nsearch_mark=%d\r\n", v);
            writeText(ini, cur);
            logClear();
            ut::configReload(ini);
            check(ut::g_cfg.searchMark == v && !logHas("search_mark=3 was the old default") &&
                      logHas("migrated from ini_version=15 to 17"),
                  v == 1 ? "a 15 file with search_mark=1 (the player's) stays 1"
                         : "a 15 file with search_mark=2 (the player's) stays 2");
        }
    }

    // ---- 5zb. search_transfer (ini_version 17): the field on the real Transfer page ------------
    printf("\n5zb. search_transfer=1 by default, read, and a 16 file migrated keeping every value\n");
    {
        const UtConfig d;
        check(d.searchTransfer == 1 && UT_INI_VERSION == 17,
              "the default: search_transfer=1 (the field on the real Transfer page too); ini_version 17");
        char cur[256];
        _snprintf_s(cur, sizeof(cur), _TRUNCATE, "ini_version=%d\r\n[display]\r\nsearch_transfer=0\r\n",
                    UT_INI_VERSION);
        writeText(ini, cur);
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.searchTransfer == 0 && !logHas("migrated"), "search_transfer=0 in a current file is read");
        // a 16 file: every value kept, the key gained at its default
        writeText(ini,
                  "ini_version=16\r\n[display]\r\nsearch_mark=3\r\nsearch_mark_color=#40e0ff\r\n"
                  "search_buttons=0\r\nsearch_lore=1\r\n[advanced]\r\npad_y=8\r\nsearch_debug_query=fire\r\n");
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.iniVersion == 17 && logHas("migrated from ini_version=16 to 17") &&
                  !logHas("was the old default") && ut::g_cfg.searchTransfer == 1,
              "a 16 file is migrated to 17 and gains search_transfer=1");
        check(ut::g_cfg.searchMark == 3 && !strcmp(ut::g_cfg.searchMarkColor, "#40e0ff") &&
                  ut::g_cfg.searchButtons == 0 && ut::g_cfg.searchLore == 1 && ut::g_cfg.padY == 8 &&
                  !strcmp(ut::g_cfg.searchDebugQuery, "fire"),
              "every value of the 16 file is kept (a 3 set in a 16 file is the player's)");
        DWORD len = 0;
        char* text = readAll(ini, &len);
        check(text && strstr(text, "\r\nsearch_transfer=1") != nullptr && strstr(text, "ini_version=17") != nullptr &&
                  strstr(text, "\r\nsearch_mark=3") != nullptr,
              "the rewritten file says search_transfer=1, search_mark=3 and ini_version=17");
        if (text) free(text);
        writeText(ini, "ini_version=16\r\n[display]\r\nsearch_transfer=0\r\n");
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.searchTransfer == 0 && logHas("migrated from ini_version=16 to 17"),
              "a 0 already in a 16 file (a hand edit) is kept");
    }

    // ---- 5b. a Grim Dawn uniquetab.ini copied across -----------------------------------------
    // The complete key set of the GD release, at its own ini_version (36).
    // Eleven of its keys are settings here (mp_collect, log_flush_each_line, text_language, since
    // GD's pad/label keys page_hotkeys, plate_label, plate_label_size, group_buttons, pad_y,
    // pad_h, pad_gap, and owned_only; tooltip_mark, tooltip_text_yes/_no,
    // tooltip_class_yes/_no); the rest are not. the four pad keys are reset on migration
    // (their meaning changed); so are the two tooltip classes (GD's numbers name GD's styles).
    printf("\n5b. a Grim Dawn settings file is never taken for this one\n");
    {
        char old[2048];
        _snprintf_s(old, sizeof(old), _TRUNCATE,
                    "ini_version=36\r\n"
                    "db_load=1\r\n"
                    "uniq_page=-1\r\n"
                    "page_hotkeys=1\r\n"
                    "live_pages=1\r\n"
                    "uniq_group=-1\r\n"
                    "uniq_row=0\r\n"
                    "owned_only=1\r\n"
                    "search_buttons=1\r\n"
                    "search_buttons_unowned=1\r\n"
                    "live_cache_max=4096\r\n"
                    "plate_swap=1\r\n"
                    "plate_label=1\r\n"
                    "plate_label_size=13\r\n"
                    "group_buttons=1\r\n"
                    "group_buttons_observe=0\r\n"
                    "pad_y=25\r\n"
                    "pad_h=14\r\n"
                    "pad_gap=1\r\n"
                    "button_icons=0\r\n"
                    "compare_popup=1\r\n"
                    "tooltip_mark=1\r\n"
                    "tooltip_text_yes=\r\n"
                    "tooltip_text_no=Nope\r\n"
                    "tooltip_class_yes=44\r\n"
                    "tooltip_class_no=74\r\n"
                    "plate_count_ms=1000\r\n"
                    "journal=1\r\n"
                    "journal_dir=\r\n"
                    "journal_prune=0\r\n"
                    "rescue=0\r\n"
                    "export_gds=1\r\n"
                    "export_csv=2\r\n"
                    "take_watch=1\r\n"
                    "take_watch_ms=400\r\n"
                    "max_per_record=9\r\n"
                    "collect_pristine_only=0\r\n"
                    "collect_quick_pass=1\r\n"
                    "log_flush_each_line=0\r\n"
                    "identity=1\r\n"
                    "identity_window_ms=250\r\n"
                    "identity_require_callsite=0\r\n"
                    "soulbound_collect=1\r\n"
                    "mp_collect=1\r\n"
                    "text_language=de\r\n");
        writeText(ini, old);
        logClear();
        ut::configReload(ini);
        char want36[128];
        _snprintf_s(want36, sizeof(want36), _TRUNCATE,
                    "migrated from ini_version=36 to %d - 18 setting(s) kept, 20 new one(s) defaulted",
                    UT_INI_VERSION);
        check(logHas(want36),
              "18 of its keys are settings here (GD's pad/label keys; owned_only; "
              "search_buttons; export_csv; the five tooltip keys), 20 are new (slot_plates; "
              "grid_cover; the other search keys, search_transfer among them)");
        check(ut::g_cfg.searchButtons == 1, "search_buttons (GD's key) keeps the value the GD file had");
        check(!strcmp(ut::g_cfg.tooltipTextNo, "Nope") && ut::g_cfg.tooltipMark == 1,
              "GD's tooltip_text_no / tooltip_mark keep the values the GD file had");
        check(ut::g_cfg.tooltipClassYes == 22 && ut::g_cfg.tooltipClassNo == 24,
              "GD's tooltip_class_yes=44 / _no=74 (GD's style numbers) are RESET to TQ's");
        check(ut::g_cfg.ownedOnly == 1, "owned_only (GD's key) keeps the value the GD file had");
        check(logHas("now place the pad INSIDE the caravan window") && ut::g_cfg.padY == 4 &&
                  ut::g_cfg.padH == 14 && ut::g_cfg.padGap == 1 && ut::g_cfg.padX == 0,
              "GD's pad_y=25 (its plate) is RESET to the new default, said in the log");
        check(!strcmp(ut::g_cfg.textLanguage, "de"),
              "text_language (GD's key) keeps the value the GD file had");
        check(logHas("26 setting(s) this build no longer has"), "the other 26 are dropped");
        check(ut::g_cfg.mpCollect == 1 && !strcmp(ut::g_cfg.logLevel, "info"),
              "the kept one holds its value, the new one its default");
        DWORD len = 0;
        char* text = readAll(ini, &len);
        check(strstr(text, "max_per_record") == nullptr,
              "the rewritten file carries none of the Grim Dawn keys");
        check(len == (DWORD)ut::configTemplateBytes(), "the file is a complete one");
        free(text);
        logClear();
        ut::configReload(ini);
        check(!logHas("migrated"),
              "and from then on it is parsed as this mod's own");
    }

    // ---- 6. bad values -----------------------------------------------------------------------
    printf("\n6. a value outside its range, and a word that is not a choice\n");
    {
        DWORD len = 0;
        char* text = readAll(ini, &len);
        check(patch(text, "mp_collect=1", "mp_collect=7"), "write mp_collect=7 (a switch: 0..1)");
        check(patch(text, "log_level=info", "log_level=lou1"), "write a log_level that is not one");
        writeAll(ini, text, len);
        free(text);
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.mpCollect == 1, "mp_collect was clamped to the top of its range");
        check(logHas("mp_collect=7 is outside 0..1"), "and the log says so");
        check(!strcmp(ut::g_cfg.logLevel, "info"), "the unknown log_level fell back to the default");
        check(logHas("log_level=lou1 is not one of the words"), "and the log says that too");
        text = readAll(ini, &len);
        check(strstr(text, "mp_collect=7") != nullptr,
              "the file still says what the user typed - a clamp is not written back");
        free(text);
    }

    // ---- 6b. a misspelled key at the current version -----------------------------------------
    printf("\n6b. a line that names nothing\n");
    {
        DWORD before = 0;
        char* text = readAll(ini, &before);
        check(patch(text, "log_flush_each_line=0", "log_flush_eahc_line=0"), "misspell log_flush_each_line");
        writeAll(ini, text, before);
        free(text);
        logClear();
        ut::configReload(ini);
        check(logHas("name a setting this build does not have and are ignored: log_flush_eahc_line"),
              "the log names the line instead of swallowing it");
        check(ut::g_cfg.logFlushEachLine == 0, "the real setting kept its default");
        DWORD after = 0;
        text = readAll(ini, &after);
        check(after == before && strstr(text, "log_flush_eahc_line=0") != nullptr,
              "the file is left alone - the typo is the player's line to fix");
        free(text);
        text = readAll(ini, &after);
        check(patch(text, "log_flush_eahc_line=0", "log_flush_each_line=0"), "spell it properly again");
        writeAll(ini, text, after);
        free(text);
        logClear();
        ut::configReload(ini);
        check(!logHas("does not have"), "and then the log says nothing about it");
    }

    // ---- 7. the persist write-back ------------------------------------------------------------
    printf("\n7. a persisted key rewrites its own line and nothing else\n");
    {
        DWORD before = 0;
        char* text = readAll(ini, &before);
        free(text);
        check(ut::configPersistInt("mp_collect", 0), "mp_collect=0 is written back");
        DWORD after = 0;
        text = readAll(ini, &after);
        check(after == before, "the file is exactly as long as it was");
        check(strstr(text, "mp_collect=0") != nullptr, "mp_collect is 0 in the file");
        check(strstr(text, "; 1 = the collection works in a MULTIPLAYER game (hosted or joined) exactly as in") != nullptr,
              "the rewritten line kept its own comment (GD's mp_collect rule)");
        free(text);
        ut::configReload(ini);
        check(ut::g_cfg.mpCollect == 0, "and it reads back as 0");
        check(ut::configPersistInt("log_flush_each_line", 1), "another key persists the same way");
        ut::configReload(ini);
        check(ut::g_cfg.logFlushEachLine == 1 && ut::g_cfg.mpCollect == 0,
              "log_flush_each_line=1 is read back, and the other key is untouched");
        ut::configPersistInt("log_flush_each_line", 0);
    }

    // ---- 8. RESCUE-NOW is not consumed ---------------------------------------------------------
    printf("\n8. a RESCUE-NOW file is left alone (no rescue)\n");
    {
        writeAll(trigger, "go", 2);
        ut::configReload(ini);
        check(GetFileAttributesW(trigger) != INVALID_FILE_ATTRIBUTES, "the file is still there");
        check(ut::g_cfg.rescue == 0, "and nothing was armed");
        DeleteFileW(trigger);
    }

    // ---- 9. enabled is not a setting ---------------------------------------------------
    printf("\n9. enabled=0 names nothing\n");
    {
        char back[128];
        _snprintf_s(back, sizeof(back), _TRUNCATE, "ini_version=%d\r\nenabled=0\r\n",
                    UT_INI_VERSION);
        writeText(ini, back);
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.enabled == 1, "the fixed default stays 1");
        check(logHas("ignored: enabled"), "the log names the line as unknown");
    }

    // ---- 10. the path rules ------------------------------------------------------------------
    // Pure string arithmetic, no disk: every case is a path the loader can really produce.
    printf("\n10. the game folder and the mod folder, for both .asi placements\n");
    {
        char game[MAX_PATH] = {0};
        wchar_t mod[MAX_PATH] = {0};

        // The .asi beside the exe: <game>\uniquetab.asi.
        check(ut::utGameDirFromExeW(L"\\games\\Titan Quest AE\\TQ.exe", game, sizeof(game)) &&
                  strcmp(game, "\\games\\Titan Quest AE") == 0,
              "the game folder is the exe's own folder");
        check(ut::utModDirBesideW(L"\\games\\Titan Quest AE\\uniquetab.asi", mod, MAX_PATH) &&
                  lstrcmpW(mod, L"\\games\\Titan Quest AE\\uniquetab") == 0,
              "beside the exe: the mod folder is <game>\\uniquetab");

        // The plugin placement deploy.bat uses: <game>\scripts\uniquetab.asi. The mod folder
        // moves with the .asi; the game folder is read from the exe, so it does NOT.
        check(ut::utModDirBesideW(L"\\games\\Titan Quest AE\\scripts\\uniquetab.asi", mod,
                                  MAX_PATH) &&
                  lstrcmpW(mod, L"\\games\\Titan Quest AE\\scripts\\uniquetab") == 0,
              "in scripts\\: the mod folder is scripts\\uniquetab");
        check(ut::utGameDirFromExeW(L"\\games\\Titan Quest AE\\TQ.exe", game, sizeof(game)) &&
                  strcmp(game, "\\games\\Titan Quest AE") == 0,
              "the game folder is the same for either placement");

        // The live resolver reads the host exe, never this module: the answer is the harness
        // exe's own folder, whatever handle a caller might have passed in before.
        {
            wchar_t self[MAX_PATH] = {0};
            char expect[MAX_PATH] = {0};
            GetModuleFileNameW(nullptr, self, MAX_PATH);
            check(ut::utGameDirFromExeW(self, expect, sizeof(expect)) &&
                      ut::utGameDirA(game, sizeof(game)) && strcmp(game, expect) == 0,
                  "utGameDirA resolves the running exe's own folder");
        }

        // A path with nothing left to strip is refused rather than answered with a root.
        check(!ut::utGameDirFromExeW(L"TQ.exe", game, sizeof(game)),
              "a bare file name has no game folder");
        check(!ut::utModDirBesideW(nullptr, mod, MAX_PATH) &&
                  !ut::utGameDirFromExeW(nullptr, game, sizeof(game)),
              "a null path is refused by both rules");
    }

    // The run's own folder goes with it; nothing but this harness's files is in it.
    DeleteFileW(ini);
    DeleteFileW(trigger);
    DeleteFileW(triggerTxt);
    if (!RemoveDirectoryW(dir)) printf("note: %S was left behind\n", dir);

    // plate_label_size is record px, scaled by the UI scale and clamped
    check(ut::utPlateLabelPx(13, 1.0f) == 13 && ut::utPlateLabelPx(13, 1.375f) == 18 &&
              ut::utPlateLabelPx(13, 1.6875f) == 22 && ut::utPlateLabelPx(13, 0.8f) == 10,
          "plate_label_size 13 -> 13 / 18 / 22 / 10 px at UI scale 1 / 1.375 / 1.6875 / 0.8");
    check(ut::utPlateLabelPx(4, 1.0f) == 8 && ut::utPlateLabelPx(40, 2.0f) == 64 &&
              ut::utPlateLabelPx(8, 0.7f) == 6 && ut::utPlateLabelPx(8, 0.31f) == 6,
          "plate_label_size is clamped to 8..32 record px and never drawn under 6 px");
    {
        volatile float zero = 0.0f;
        const float nan = zero / zero;
        check(ut::utPlateLabelPx(13, nan) == 13 && ut::utPlateLabelPx(13, 9.0f) == 13 &&
                  ut::utPlateLabelPx(13, 0.0f) == 13,
              "an unreadable or absurd UI scale counts as 1");
    }

    printf("\n%s (%d failure(s))\n", g_fails ? "FAILED" : "PASSED", g_fails);
    return g_fails ? 1 : 0;
}
