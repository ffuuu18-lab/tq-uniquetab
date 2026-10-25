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
//   9. enabled=0 (GD's master switch) collapses the tab's switches, said once in the log;
//      enabled=1 gives every switch its own value back.
//  11. THE SHORTER FILE (GD's renderer): the template (seven live keys under [general], every
//      other key one commented line under [advanced]), a kept value (a live blob row), a
//      commented default, a stray key, and the 19 -> 20 migration's round trip: every value
//      kept, every moved line logged, render -> parse -> the same values.
//  10. THE PATH RULES (src\ut_paths.cpp), for both placements the loader allows: the .asi beside
//      TQ.exe and the .asi in a scripts\ folder. The mod folder follows the .asi; the game
//      folder is the host exe's own folder and does not move with it.
// Exit code 0 = all of it held.
#include <windows.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>

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

// "\r\nname=" or "\r\n;name=" - a key at the start of a line, live or as the commented row of
// the [advanced] blob, which is how the renderer writes every one of them.
static int countKeyLines(const char* text, const char* name) {
    int n = 0;
    for (int form = 0; form < 2; ++form) {
        char needle[96];
        _snprintf_s(needle, sizeof(needle), _TRUNCATE, form ? "\r\n;%s=" : "\r\n%s=", name);
        for (const char* p = strstr(text, needle); p; p = strstr(p + 1, needle)) ++n;
    }
    return n;
}

// A key's line, "key=value", live ("\r\nkey=value") or as the commented row of the blob
// ("\r\n;key=value": an [advanced] key at its default).
static bool hasKeyLine(const char* text, const char* kv) {
    char a[200], b[200];
    _snprintf_s(a, sizeof(a), _TRUNCATE, "\r\n%s", kv);
    _snprintf_s(b, sizeof(b), _TRUNCATE, "\r\n;%s", kv);
    return text && (strstr(text, a) || strstr(text, b));
}
// Only the live form.
static bool hasLiveLine(const char* text, const char* kv) {
    char a[200];
    _snprintf_s(a, sizeof(a), _TRUNCATE, "\r\n%s", kv);
    return text && strstr(text, a);
}

// The player's edit of one line: "key=old" - live, or the commented row with its ';' removed -
// becomes the live "key=new" (no longer than the old line).
static bool patchKey(char* text, DWORD* len, const char* oldKv, const char* newKv) {
    char a[200], b[200];
    _snprintf_s(b, sizeof(b), _TRUNCATE, "\r\n%s", newKv);
    _snprintf_s(a, sizeof(a), _TRUNCATE, "\r\n;%s", oldKv);
    if (patchShorter(text, len, a, b)) return true;
    _snprintf_s(a, sizeof(a), _TRUNCATE, "\r\n%s", oldKv);
    return patchShorter(text, len, a, b);
}

// A key's lines in a complete file: exactly one; a screen key of [layout] (written only while
// set) at most one.
static bool keyLinesOk(const char* text, const ut::UtCfgKey& k) {
    const int n = countKeyLines(text, k.name);
    return (k.flags & ut::kUtCfgWhenSet) ? n <= 1 : n == 1;
}

// A complete current file: the current ini_version and every key on exactly one line.
static bool completeFile(const char* text) {
    if (!text) return false;
    char ver[40];
    _snprintf_s(ver, sizeof(ver), _TRUNCATE, "\r\nini_version=%d\r\n", UT_INI_VERSION);
    if (!strstr(text, ver)) return false;
    for (int i = 0; i < ut::kUtCfgKeyCount; ++i)
        if (!keyLinesOk(text, ut::kUtCfgKeys[i])) return false;
    return true;
}

// Every row of the table holds the same value in both.
static int differingKeys(const UtConfig& a, const UtConfig& b, bool say) {
    int bad = 0;
    for (int i = 0; i < ut::kUtCfgKeyCount; ++i) {
        const ut::UtCfgKey& k = ut::kUtCfgKeys[i];
        const bool same = k.type == ut::kUtCfgStr ? !strcmp(strOf(a, k), strOf(b, k))
                                                   : intOf(a, k) == intOf(b, k);
        if (!same) {
            ++bad;
            if (say) printf("    %s differs\n", k.name);
        }
    }
    return bad;
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
    wchar_t ini[MAX_PATH], trigger[MAX_PATH], triggerTxt[MAX_PATH], state[MAX_PATH];
    _snwprintf_s(ini, _TRUNCATE, L"%s\\uniquetab.ini", dir);
    _snwprintf_s(state, _TRUNCATE, L"%s\\uniquetab.state", dir);
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
            if (n == 0 && !(ut::kUtCfgKeys[i].flags & ut::kUtCfgWhenSet)) {
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
        check(completeFile(text), "and is a complete file again");
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
        check(completeFile(text), "which is a complete file");
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
        check(text && hasKeyLine(text, "slot_plates=3") && strstr(text, cur) != nullptr,
              "the rewritten file says slot_plates=3 and the current ini_version");
        check(text && patchKey(text, &len, "slot_plates=3", "slot_plates=1"),
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
        check(UT_INI_VERSION == 27 && UT_GRAY_ICONS_SINCE == 13, "ini_version 25, the gray default since 13");
        char old[512];
        _snprintf_s(old, sizeof(old), _TRUNCATE,
                    "ini_version=11\r\n[display]\r\nowned_marks=1\r\nslot_plates=2\r\n");
        writeText(ini, old);
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.ownedMarks == 3 && ut::g_cfg.slotPlates == 2,
              "ini_version 11 reading owned_marks=1 (the old default) -> 3, the rest kept");
        check(logHas("owned_marks=1 was the old default") && logHas("migrated from ini_version=11 to 27"),
              "and the log says so");
        DWORD len = 0;
        char* text = readAll(ini, &len);
        check(text && hasKeyLine(text, "owned_marks=3") && strstr(text, "ini_version=27") != nullptr,
              "the rewritten file says owned_marks=3 and ini_version=27");
        check(text && patchKey(text, &len, "owned_marks=3", "owned_marks=1"),
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
                  logHas("migrated from ini_version=12 to 27"),
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
        check(UT_INI_VERSION == 27, "ini_version 25 (13: the comment bump merged with 12; 14: the search keys)");
        writeText(ini,
                  "ini_version=11\r\n[collection]\r\n"
                  "; read and kept (GD's key); in this version deposits and takes are ALWAYS refused "
                  "while you play multiplayer, whatever it says\r\nmp_collect=0\r\n"
                  "[display]\r\nslot_plates=1\r\nhave_marks=1\r\ngrid_cover=0\r\n");
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.iniVersion == 27 && logHas("migrated from ini_version=11 to 27"),
              "ini_version 11 is migrated to 24, and the log says so");
        check(ut::g_cfg.mpCollect == 0 && ut::g_cfg.slotPlates == 1 && ut::g_cfg.haveMarks == 1 &&
                  ut::g_cfg.gridCover == 0,
              "every value kept: mp_collect=0, slot_plates=1 and have_marks=1 (the player's), grid_cover=0");
        check(!logHas("slot_plates=1 was the old default") && !logHas("have_marks=1 was the old default"),
              "and no default is moved (11 is past both of those migrations)");
        DWORD len = 0;
        char* text = readAll(ini, &len);
        check(text && strstr(text, "ALWAYS refused") == nullptr &&
                  strstr(text, "; 1 = deposits and takes work in a multiplayer game too (hosted or joined)") != nullptr &&
                  hasKeyLine(text, "mp_collect=0") && strstr(text, "ini_version=27") != nullptr,
              "the file now carries the new mp_collect comment, the kept value and ini_version=27");
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
        check(ut::g_cfg.iniVersion == 27 && logHas("migrated from ini_version=12 to 27") &&
                  ut::g_cfg.mpCollect == 1 && ut::g_cfg.ownedMarks == 2 && text &&
                  strstr(text, "ALWAYS refused") == nullptr,
              "the merge: a branch ini_version 12 (the old comment) gets the new comment too, values kept");
        free(text);
    }

    // ---- 5a. have_marks is off by default; the old default 1 is flipped ONCE -----------
    printf("\n5z. the rollover keys leave the file for [tab] of uniquetab-buttons.ini (ini_version 21)\n");
    {
        writeText(ini, "ini_version=20\r\n[general]\r\ntooltip_mark=0\r\n[advanced]\r\n"
                       "tooltip_text_yes=Got it ; mine\r\ntooltip_text_no=\r\ntooltip_class_yes=22\r\n"
                       "tooltip_class_no=30\r\n");
        logClear();
        ut::configReload(ini);
        check(logHas("carried there once: tooltip_text_yes=Got it -> tooltip.yes, "
                     "tooltip_class_no=30 -> tooltip.class_no"),
              "20 -> 21: one INFO line names the two values that are not their old defaults");
        {
            DWORD hl = 0;
            char* held = readAll(ini, &hl);
            check(held && !strncmp(held, "ini_version=20\r\n", 16) && strstr(held, "tooltip_text_yes=Got it") &&
                      logHas("until then it keeps ini_version=20 and those keys"),
                  "held: the file keeps ini_version=20 and the four keys until the words are in the buttons file");
            free(held);
        }
        ut::UtTooltipCarry k;
        check(ut::configTakeTooltipCarry(&k) && !strcmp(k.yes, "Got it") && !k.no[0] &&
                  k.classYes == -1 && k.classNo == 30,
              "the carry holds exactly them (\"\" / -1 = the old default, not carried)");
        check(!ut::configTakeTooltipCarry(&k), "taken once");
        logClear();
        ut::configTooltipCarryDone();
        check(logHas("rewritten at ini_version=27 - the rollover keys left it"),
              "the words written: configTooltipCarryDone rewrites the file at 21");
        logClear();
        ut::configTooltipCarryDone();
        check(!logHas("rewritten at ini_version"), "and only once");
        check(ut::g_cfg.tooltipMark == 0, "every other value is kept");
        DWORD len = 0;
        char* text = readAll(ini, &len);
        check(text && !strstr(text, "tooltip_text") && !strstr(text, "tooltip_class") &&
                  hasKeyLine(text, "ini_version=27"),
              "the rewritten file has no rollover key and says ini_version=27");
        free(text);
        writeText(ini, "ini_version=27\r\n[general]\r\ntooltip_text_no=Nope\r\ntooltip_class_yes=22\r\n");
        logClear();
        ut::configReload(ini);
        check(logHas("tooltip_text_no=Nope is not read from uniquetab.ini any more - its value belongs in "
                     "tooltip.no under [tab] of uniquetab-buttons.ini") &&
                  !logHas("tooltip_class_yes=22 is not read"),
              "a 22 file naming one: one WARN for a value that is not its old default, none for a default");
        check(!ut::configTakeTooltipCarry(&k), "and nothing is carried from a file of this version");
        writeText(ini, "ini_version=20\r\n[general]\r\ntooltip_text_yes=\r\n");
        logClear();
        ut::configReload(ini);
        check(logHas("this file had their defaults, nothing to carry") && !ut::configTakeTooltipCarry(&k),
              "a 20 file with the defaults: said once, nothing carried");
    }

    printf("\n5a. have_marks off by default (the tooltip line replaces it), migrated once\n");
    {
        const UtConfig d;
        check(d.haveMarks == 0 && d.tooltipMark == 1, "the defaults: have_marks=0, tooltip_mark=1");
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
        check(text && hasKeyLine(text, "have_marks=0"),
              "the rewritten, current file says have_marks=0");
        check(text && hasKeyLine(text, "tooltip_mark=1") && !strstr(text, "tooltip_class_yes") &&
                  !strstr(text, "tooltip_text_yes"),
              "and carries tooltip_mark; the rollover keys left the file (ini_version 21)");
        check(text && patchKey(text, &len, "have_marks=0", "have_marks=1"),
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
            check(text9 && hasKeyLine(text9, "grid_cover=1") &&
                      hasKeyLine(text9, "have_marks=1"),
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
        check(ut::g_cfg.iniVersion == 27 && logHas("migrated from ini_version=13 to 27") &&
                  ut::g_cfg.ownedMarks == 2 && ut::g_cfg.search == 1 && ut::g_cfg.searchButtons == 1 &&
                  ut::g_cfg.searchLore == 0 && ut::g_cfg.searchPrebuild == 0 &&
                  ut::g_cfg.searchDebugQuery[0] == 0,
              "a file from 13 is migrated: its values kept, the five search keys at their defaults");
        DWORD len = 0;
        char* text = readAll(ini, &len);
        check(text && hasKeyLine(text, "search=1") &&
                  hasKeyLine(text, "search_buttons=1") &&
                  hasKeyLine(text, "search_lore=0") &&
                  hasKeyLine(text, "search_prebuild=0") &&
                  hasKeyLine(text, "search_debug_query="),
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
        check(ut::g_cfg.iniVersion == 27 && logHas("migrated from ini_version=14 to 27") &&
                  ut::g_cfg.searchButtons == 0 && ut::g_cfg.searchMark == 1,
              "a file from 14 is migrated: its values kept, search_mark at its default");
        text = readAll(ini, &len);
        check(text && hasKeyLine(text, "search_mark=1"), "and the rewritten file carries it");
        free(text);
    }

    // ---- 5za. search_mark's default 1 and search_mark_color (ini_version 16) -------------------
    printf("\n5za. search_mark=1 by default, search_mark_color, a 15 file migrated once\n");
    {
        const UtConfig d;
        check(d.collectMi == 1 && d.collectUnobtainable == 0 && d.searchMark == 1 && !strcmp(d.searchMarkColor, "gold") && UT_INI_VERSION == 27 &&
                  UT_SEARCH_FRAME_SINCE == 16,
              "the defaults: collect_mi=1, collect_unobtainable=0, search_mark=1 (a thin frame), search_mark_color=gold; ini_version 25");
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
        check(ut::g_cfg.iniVersion == 27 && ut::g_cfg.searchMark == 1 &&
                  logHas("search_mark=3 was the old default") && logHas("migrated from ini_version=15 to 27"),
              "a 15 file reading search_mark=3 (the old default) -> 1, and the log says so");
        check(ut::g_cfg.searchButtons == 0 && ut::g_cfg.ownedMarks == 2 && ut::g_cfg.searchLore == 1 &&
                  logHas("the file had pad_y=8, not carried") && ut::g_cfg.plateLabelSize == 16 &&
                  !strcmp(ut::g_cfg.searchDebugQuery, "fire") && !strcmp(ut::g_cfg.searchMarkColor, "gold"),
              "every other value of the 15 file is kept; search_mark_color comes at gold");
        DWORD len = 0;
        char* text = readAll(ini, &len);
        check(text && hasKeyLine(text, "search_mark=1") &&
                  hasKeyLine(text, "search_mark_color=gold") && strstr(text, "ini_version=27") != nullptr,
              "the rewritten file says search_mark=1, search_mark_color=gold and ini_version=27");
        check(text && patchKey(text, &len, "search_mark=1", "search_mark=3"),
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
                      logHas("migrated from ini_version=15 to 27"),
                  v == 1 ? "a 15 file with search_mark=1 (the player's) stays 1"
                         : "a 15 file with search_mark=2 (the player's) stays 2");
        }
    }

    // ---- 5zb. search_transfer (ini_version 20): the field on the real Transfer page ------------
    printf("\n5zb. search_transfer=1 by default, read, and a 16 file migrated keeping every value\n");
    {
        const UtConfig d;
        check(d.searchTransfer == 1 && UT_INI_VERSION == 27,
              "the default: search_transfer=1 (the field on the real Transfer page too); ini_version 25");
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
        check(ut::g_cfg.iniVersion == 27 && logHas("migrated from ini_version=16 to 27") &&
                  !logHas("was the old default") && ut::g_cfg.searchTransfer == 1,
              "a 16 file is migrated to 24 and gains search_transfer=1");
        check(ut::g_cfg.searchMark == 3 && !strcmp(ut::g_cfg.searchMarkColor, "#40e0ff") &&
                  ut::g_cfg.searchButtons == 0 && ut::g_cfg.searchLore == 1 &&
                  !strcmp(ut::g_cfg.searchDebugQuery, "fire"),
              "every value of the 16 file is kept (a 3 set in a 16 file is the player's)");
        DWORD len = 0;
        char* text = readAll(ini, &len);
        check(text && hasKeyLine(text, "search_transfer=1") && strstr(text, "ini_version=27") != nullptr &&
                  hasKeyLine(text, "search_mark=3"),
              "the rewritten file says search_transfer=1, search_mark=3 and ini_version=27");
        if (text) free(text);
        writeText(ini, "ini_version=16\r\n[display]\r\nsearch_transfer=0\r\n");
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.searchTransfer == 0 && logHas("migrated from ini_version=16 to 27"),
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
                    "migrated from ini_version=36 to %d - 12 setting(s) kept, 33 new one(s) defaulted",
                    UT_INI_VERSION);
        check(logHas(want36),
              "10 of its keys are settings here (GD's label keys; "
              "search_buttons; export_csv; tooltip_mark), 30 are new (enabled; edit_button; relic_block_gap; relic_bonus_gap; collect_mi; collect_relics; collect_unobtainable; slot_plates; "
              "grid_cover; the other search keys, search_transfer among them; pad_screen, label_screen)");
        check(ut::g_cfg.searchButtons == 1, "search_buttons (GD's key) keeps the value the GD file had");
        check(ut::g_cfg.tooltipMark == 1, "GD's tooltip_mark keeps the value the GD file had");
        {
            ut::UtTooltipCarry k;
            check(ut::configTakeTooltipCarry(&k) && !strcmp(k.no, "Nope") && !k.yes[0] &&
                      k.classYes == -1 && k.classNo == -1,
                  "GD's tooltip_text_no=Nope is carried to [tab] of the buttons file; GD's "
                  "tooltip_class_yes=44 / _no=74 (GD's style numbers) are not");
            check(!ut::configTakeTooltipCarry(&k), "the carry is taken once");
            ut::configTooltipCarryDone();   // the poll's part: the words are in the buttons file
        }
        check(ut::g_cfg.ownedOnly == 1 && logHas("moved to uniquetab.state (group=-1 row=0 owned_only=1)"),
              "owned_only (GD's state key) keeps the value the GD file had: it seeds uniquetab.state");
        check(logHas("pad_x, pad_y, pad_h and pad_gap are retired (ini_version 22)") &&
                  logHas("the file had pad_y=25, pad_h=14, pad_gap=1, not carried"),
              "GD's pad_y=25, pad_h, pad_gap are retired: named with their values in the log, never used");
        check(!strcmp(ut::g_cfg.textLanguage, "de"),
              "text_language (GD's key) keeps the value the GD file had");
        check(logHas("22 setting(s) this build no longer has"),
              "the other 22 are dropped (GD's uniq_group / uniq_row are state lines, not settings; "
              "journal_dir and collect_pristine_only are keys again since 23)");
        check(ut::g_cfg.mpCollect == 1 && !strcmp(ut::g_cfg.logLevel, "info"),
              "the kept one holds its value, the new one its default");
        DWORD len = 0;
        char* text = readAll(ini, &len);
        check(strstr(text, "max_per_record") == nullptr,
              "the rewritten file carries none of the Grim Dawn keys");
        check(completeFile(text), "the file is a complete one");
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
        check(patchKey(text, &len, "mp_collect=1", "mp_collect=7"), "write mp_collect=7 (a switch: 0..1)");
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
        check(patchKey(text, &before, "log_flush_each_line=0", "log_flush_eahc_line=0"), "misspell log_flush_each_line");
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
        check(strstr(text, "; 1 = deposits and takes work in a multiplayer game too (hosted or joined), for you only") != nullptr,
              "the rewritten line kept its own comment (GD's mp_collect rule)");
        free(text);
        ut::configReload(ini);
        check(ut::g_cfg.mpCollect == 0, "and it reads back as 0");
        check(ut::configPersistInt("log_flush_each_line", 1), "another key persists the same way");
        ut::configReload(ini);
        check(ut::g_cfg.logFlushEachLine == 1 && ut::g_cfg.mpCollect == 0,
              "log_flush_each_line=1 is read back, and the other key is untouched");
        ut::configPersistInt("log_flush_each_line", 0);
        // the OWN button: owned_only is the game's state, written to uniquetab.state
        writeText(ini, "");
        DeleteFileW(ini);
        DeleteFileW(state);
        ut::configReload(ini);   // a fresh file
        DWORD fresh = 0;
        text = readAll(ini, &fresh);
        free(text);
        check(ut::configPersistInt("owned_only", 1), "owned_only=1 is written back (the OWN button)");
        text = readAll(ini, &after);
        check(after == fresh && countKeyLines(text, "owned_only") == 0,
              "the settings file is untouched and names no owned_only");
        free(text);
        DWORD slen = 0;
        text = readAll(state, &slen);
        check(text && hasLiveLine(text, "owned_only=1") && hasLiveLine(text, "uniq_group=-1"),
              "uniquetab.state holds owned_only=1 (and the group and row)");
        free(text);
        ut::configReload(ini);
        check(ut::g_cfg.ownedOnly == 1, "and it reads back as 1");
        check(ut::configPersistInt("owned_only", 0), "owned_only=0 is written back");
        text = readAll(ini, &after);
        check(after == fresh && after == (DWORD)ut::configTemplateBytes(),
              "the settings file is still the template, byte for byte");
        free(text);
        text = readAll(state, &slen);
        check(text && hasLiveLine(text, "owned_only=0"), "and the state file says owned_only=0");
        free(text);
        DeleteFileW(state);
    }

    // ---- 8. RESCUE-NOW is not consumed ---------------------------------------------------------
    printf("\n8. a RESCUE-NOW file is left alone (no rescue)\n");
    {
        writeAll(trigger, "go", 2);
        ut::configReload(ini);
        check(GetFileAttributesW(trigger) != INVALID_FILE_ATTRIBUTES, "the file is still there");
        DeleteFileW(trigger);
    }

    // ---- 9. enabled (GD's master switch) ---------------------------------------------------
    printf("\n9. enabled=0 is the configuration everything-off, said once\n");
    {
        char back[128];
        _snprintf_s(back, sizeof(back), _TRUNCATE, "ini_version=%d\r\n[general]\r\nenabled=0\r\n",
                    UT_INI_VERSION);
        writeText(ini, back);
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.enabled == 0 && ut::g_cfg.journal == 0 && ut::g_cfg.livePages == 0 &&
                  ut::g_cfg.groupButtons == 0 && ut::g_cfg.pageHotkeys == 0 &&
                  ut::g_cfg.plateLabel == 0 && ut::g_cfg.ownedMarks == 0 &&
                  ut::g_cfg.slotPlates == 0 && ut::g_cfg.gridCover == 0 &&
                  ut::g_cfg.tooltipMark == 0 && ut::g_cfg.search == 0 &&
                  ut::g_cfg.searchButtons == 0 && ut::g_cfg.comparePopup == 0,
              "enabled=0: no pad, no marks, no tooltip line, no search, no deposit (journal=0)");
        check(logHas("config: enabled=0 - no tab, no marks, nothing new is collected"),
              "the log says so");
        check(!logHas("ignored: enabled"), "enabled is a setting, not an unknown line");
        writeText(ini, back);
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.enabled == 0 && !logHas("enabled=0 - no tab"), "said once, not at every read");
        _snprintf_s(back, sizeof(back), _TRUNCATE, "ini_version=%d\r\n[general]\r\nenabled=1\r\n",
                    UT_INI_VERSION);
        writeText(ini, back);
        logClear();
        ut::configReload(ini);
        const UtConfig d;
        check(ut::g_cfg.enabled == 1 && ut::g_cfg.journal == 1 && ut::g_cfg.groupButtons == 1 &&
                  ut::g_cfg.tooltipMark == 1 && ut::g_cfg.search == 1 &&
                  ut::g_cfg.ownedMarks == d.ownedMarks && ut::g_cfg.slotPlates == d.slotPlates,
              "enabled=1 gives every switch its own value back");
    }

    // ---- 11. the shorter file (GD's renderer) ------------------------------------------------
    printf("\n11. the shorter file: eight keys on top, every other one commented line\n");
    {
        static const char* const kTop[] = {"enabled", "collect_mi", "collect_relics", "collect_unobtainable",
                                           "tooltip_mark", "log_level", "view_hotkey", "search"};
        const int nTop = (int)(sizeof(kTop) / sizeof(kTop[0]));
        static char buf[16384];
        const UtConfig d;
        const size_t n = ut::configRender(buf, sizeof(buf), d);
        check(n > 0 && n == ut::configTemplateBytes(), "the template renders");
        // the template: [general] holds exactly the eight, live, in this order; [advanced] the rest
        const char* gen = strstr(buf, "\r\n[general]\r\n");
        const char* lay = strstr(buf, "\r\n[layout]\r\n");
        const char* adv = strstr(buf, "\r\n[advanced]\r\n");
        check(gen && lay && adv && gen < lay && lay < adv && ut::kUtCfgSectionCount == 3,
              "three blocks: [general], [layout] (no line until one is set), then [advanced] at the end");
        int top = 0, order = 0;
        const char* last = gen;
        for (int i = 0; i < nTop && gen && adv; ++i) {
            char kv[64];
            _snprintf_s(kv, sizeof(kv), _TRUNCATE, "\r\n%s=", kTop[i]);
            const char* at = strstr(gen, kv);
            if (at && at < adv) ++top;
            if (at && at > last) ++order;
            if (at) last = at;
        }
        check(top == nTop && order == nTop, "[general] has the eight keys live, in the order of the top set");
        int hidden = 0, liveHidden = 0, bad = 0, whenSet = 0;
        for (int i = 0; i < ut::kUtCfgKeyCount; ++i) {
            const ut::UtCfgKey& k = ut::kUtCfgKeys[i];
            if (k.flags & ut::kUtCfgWhenSet) {   // a screen key of [layout]: no line at its default
                char row[96];
                _snprintf_s(row, sizeof(row), _TRUNCATE, "%s=", k.name);
                if (strcmp(k.section, "layout") != 0 || strstr(buf, row)) {
                    printf("    %s: in the wrong place\n", k.name);
                    ++bad;
                }
                ++whenSet;
                continue;
            }
            bool isTop = false;
            for (int j = 0; j < nTop; ++j) isTop = isTop || !strcmp(kTop[j], k.name);
            const bool h = (k.flags & ut::kUtCfgHidden) != 0;
            if (h == isTop || strcmp(k.section, h ? "advanced" : "general") != 0) {
                printf("    %s: in the wrong place\n", k.name);
                ++bad;
            }
            if (!h) continue;
            ++hidden;
            char row[96];
            _snprintf_s(row, sizeof(row), _TRUNCATE, "\r\n;%s=", k.name);
            const char* at = strstr(buf, row);
            if (!at || at < adv) ++bad;
            _snprintf_s(row, sizeof(row), _TRUNCATE, "\r\n%s=", k.name);
            if (strstr(buf, row)) ++liveHidden;
        }
        printf("    %d visible key(s), %d commented row(s), %u bytes\n", nTop, hidden, (unsigned)n);
        check(bad == 0 && liveHidden == 0 && whenSet == 2 && hidden == ut::kUtCfgKeyCount - nTop - whenSet,
              "every other key is ONE commented line under [advanced], at its default; pad_screen and "
              "label_screen are [layout] keys with no line until set");
        {   // a set screen key: one live line under [layout], before [advanced]
            static char sb[16384];
            UtConfig sc;
            strcpy_s(sc.padScreen, sizeof(sc.padScreen), "tr,10,20,100,40");
            const size_t sn = ut::configRender(sb, sizeof(sb), sc);
            const char* sl = strstr(sb, "\r\n[layout]\r\n");
            const char* sa = strstr(sb, "\r\n[advanced]\r\n");
            const char* sp = strstr(sb, "\r\npad_screen=tr,10,20,100,40");
            check(sn > 0 && sl && sa && sp && sl < sp && sp < sa && !strstr(sb, "label_screen="),
                  "pad_screen set: its one live line under [layout] (label_screen, unset, has none)");
        }
        check(strstr(buf, "More settings are at the end, commented out") != nullptr &&
                  strstr(buf, "; Every other setting, commented out at its default. Remove the ';' in front of one to") != nullptr,
              "the header and the [advanced] blurb say how to change one");
        check(strstr(buf, "max_per_record") == nullptr, "no max_per_record (TQ has no counterpart)");
        // a commented default: the template parses back to the defaults (section 2) - and one
        // more time here, from the rendered bytes
        DeleteFileW(ini);
        ut::configReload(ini);
        ut::configReload(ini);
        check(differingKeys(ut::g_cfg, d, true) == 0, "the commented rows read back as their defaults");
        // a kept value: an [advanced] key off its default is written LIVE in its place
        UtConfig v;
        v.plateLabelSize = 16;
        _snprintf_s(v.searchMarkColor, sizeof(v.searchMarkColor), _TRUNCATE, "#40e0ff");
        static char buf2[16384];
        const size_t n2 = ut::configRender(buf2, sizeof(buf2), v);
        check(n2 > 0 && hasLiveLine(buf2, "plate_label_size=16\t") && !strstr(buf2, ";plate_label_size=") &&
                  hasLiveLine(buf2, "search_mark_color=#40e0ff\t") && countKeyLines(buf2, "plate_label_size") == 1,
              "a kept value is the row with its ';' gone, aligned with tabs like the rest");
        writeAll(ini, buf2, (DWORD)n2);
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.plateLabelSize == 16 && !strcmp(ut::g_cfg.searchMarkColor, "#40e0ff") &&
                  differingKeys(ut::g_cfg, v, true) == 0 && !logHas("migrated"),
              "and it reads back: the colour's '#' is the value, the tab and the comment are not");
        // a stray key: at the current version it is named and the file is left alone
        {
            static char buf3[16400];
            memcpy(buf3, buf2, n2 + 1);
            strcat_s(buf3, "stray_key=1\r\n");
            writeText(ini, buf3);
            logClear();
            ut::configReload(ini);
            DWORD len = 0;
            char* text = readAll(ini, &len);
            check(logHas("ignored: stray_key") && text && strstr(text, "stray_key=1") != nullptr &&
                      ut::g_cfg.plateLabelSize == 16,
                  "a stray key in a current file: named in the log, the file left alone");
            free(text);
        }
        // the migration 19 -> 20: a complete ini_version 19 file, as the 19 renderer laid it out
        // (its six blocks), every second key off its default
        struct OldKey { const char* name; const char* block; };
        static const OldKey k19[] = {
            {"collect_mi", "general"}, {"collect_unobtainable", "general"}, {"log_level", "general"},
            {"mp_collect", "collection"}, {"view_hotkey", "view"},
            {"group_buttons", "display"}, {"page_hotkeys", "display"}, {"plate_label", "display"},
            {"owned_marks", "display"}, {"have_marks", "display"}, {"tooltip_mark", "display"},
            {"slot_plates", "display"}, {"grid_cover", "display"},
            {"search", "display"}, {"search_buttons", "display"}, {"search_mark", "display"},
            {"search_mark_color", "display"}, {"search_transfer", "display"}, {"search_lore", "display"},
            {"export_csv", "files"},
            {"pad_x", "advanced"}, {"pad_y", "advanced"}, {"pad_h", "advanced"}, {"pad_gap", "advanced"},
            {"plate_label_size", "advanced"}, {"font", "advanced"}, {"caravan_x", "advanced"},
            {"caravan_y", "advanced"}, {"caravan_w", "advanced"}, {"caravan_h", "advanced"},
            {"page_y", "advanced"}, {"log_flush_each_line", "advanced"}, {"search_prebuild", "advanced"},
            {"search_debug_query", "advanced"}, {"text_language", "advanced"}};
        const int n19 = (int)(sizeof(k19) / sizeof(k19[0]));
        check(n19 == ut::kUtCfgKeyCount - 14 + 4,
              "the 19 file names every key but the new enabled, layout_outline, layout_edit, edit_button, relic_block_gap, relic_bonus_gap, relic_panels, collect_relics, pad_screen and label_screen, "
              "the four keys of 23, and the four retired pad keys");
        DeleteFileW(state);
        UtConfig want;   // what the migration must keep
        static char old[16384];
        int at = _snprintf_s(old, sizeof(old), _TRUNCATE, "; the 19 layout\r\nini_version=19\r\n");
        const char* block = "";
        int off = 0;
        for (int i = 0; i < n19; ++i) {
            int idx = -1;
            for (int j = 0; j < ut::kUtCfgKeyCount; ++j)
                if (!strcmp(ut::kUtCfgKeys[j].name, k19[i].name)) idx = j;
            if (idx < 0) continue;
            const ut::UtCfgKey& k = ut::kUtCfgKeys[idx];
            if (strcmp(block, k19[i].block) != 0) {
                block = k19[i].block;
                at += _snprintf_s(old + at, sizeof(old) - at, _TRUNCATE, "\r\n[%s]\r\n; a blurb\r\n", block);
            }
            char val[96];
            if (k.type == ut::kUtCfgStr) {
                const char* s = (i % 2) ? strOf(want, k)
                                : !strcmp(k.name, "log_level")         ? "debug"
                                : !strcmp(k.name, "search_mark_color") ? "green"
                                : !strcmp(k.name, "text_language")     ? "DE"
                                                                       : "fire and ice";
                _snprintf_s(val, sizeof(val), _TRUNCATE, "%s", s);
                _snprintf_s((char*)&want + k.off, k.cap, _TRUNCATE, "%s", s);
            } else {
                int x = intOf(want, k);
                if (i % 2 == 0) x = x < k.hi ? x + 1 : x - 1;
                *(int*)((char*)&want + k.off) = x;
                _snprintf_s(val, sizeof(val), _TRUNCATE, "%d", x);
                if (x != k.def) ++off;
            }
            at += _snprintf_s(old + at, sizeof(old) - at, _TRUNCATE, "%s=%s     ; its old comment\r\n",
                              k.name, val);
        }
        // owned_only, a key of the 19 file, is the OWN filter's state now
        at += _snprintf_s(old + at, sizeof(old) - at, _TRUNCATE, "owned_only=1     ; its old comment\r\n");
        writeText(ini, old);
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.iniVersion == 27 && logHas("migrated from ini_version=19 to 27 - 31 setting(s) kept, 14 new one(s) defaulted"),
              "a 19 file is migrated to 25: 31 kept, enabled, layout_outline, layout_edit, edit_button, the two relic gaps, pad_screen, label_screen "
              "and the four keys of 23 new");
        check(ut::g_cfg.ownedOnly == 1 && logHas("moved to uniquetab.state (group=-1 row=0 owned_only=1)"),
              "its owned_only=1 moved to uniquetab.state and is in force");
        {
            DWORD l2 = 0;
            char* t2 = readAll(ini, &l2);
            check(t2 && countKeyLines(t2, "owned_only") == 0, "the rewritten 20 file names no owned_only");
            free(t2);
        }
        DeleteFileW(state);
        check(differingKeys(ut::g_cfg, want, true) == 0, "EVERY value of the 19 file is kept");
        check(logHas("line(s) moved from [view] to [general]: view_hotkey=118") &&
                  logHas("line(s) moved from [display] to [general]: tooltip_mark=") &&
                  logHas("search=") &&
                  logHas("line(s) moved from [collection] to [advanced]: mp_collect=") &&
                  logHas("line(s) moved from [display] to [advanced]: group_buttons=") &&
                  logHas("line(s) moved from [files] to [advanced]: export_csv="),
              "every moved line is logged, with its value, per old and new block");
        check(logHas("hold their default and are commented under [advanced] now"),
              "the rows at their default are named as the lines that come back commented");
        DWORD len = 0;
        char* text = readAll(ini, &len);
        int liveOff = 0, commentedDef = 0;
        for (int i = 0; i < ut::kUtCfgKeyCount && text; ++i) {
            const ut::UtCfgKey& k = ut::kUtCfgKeys[i];
            if (!(k.flags & ut::kUtCfgHidden)) continue;
            char vt[300], kv[340];
            if (k.type == ut::kUtCfgStr) _snprintf_s(vt, sizeof(vt), _TRUNCATE, "%s", strOf(want, k));
            else _snprintf_s(vt, sizeof(vt), _TRUNCATE, "%d", intOf(want, k));
            const bool isDef = k.type == ut::kUtCfgStr ? !strcmp(vt, k.defStr) : intOf(want, k) == k.def;
            _snprintf_s(kv, sizeof(kv), _TRUNCATE, "%s=%s\t", k.name, vt);
            if (!isDef && hasLiveLine(text, kv)) ++liveOff;
            if (isDef && hasKeyLine(text, kv) && !hasLiveLine(text, kv)) ++commentedDef;
        }
        int hiddenRows = 0, hiddenOff = 0;
        for (int i = 0; i < ut::kUtCfgKeyCount; ++i) {
            const ut::UtCfgKey& k = ut::kUtCfgKeys[i];
            if (!(k.flags & ut::kUtCfgHidden)) continue;
            ++hiddenRows;
            const bool isDef = k.type == ut::kUtCfgStr ? !strcmp(strOf(want, k), k.defStr) : intOf(want, k) == k.def;
            if (!isDef) ++hiddenOff;
        }
        printf("    %d [advanced] row(s): %d live (kept off their default), %d commented\n", hiddenRows,
               liveOff, commentedDef);
        check(completeFile(text) && liveOff == hiddenOff && commentedDef == hiddenRows - hiddenOff,
              "the rewritten file: one line per key, the kept values live, the defaults commented");
        check(text && !strstr(text, "[display]") && !strstr(text, "[collection]") && !strstr(text, "[view]") &&
                  !strstr(text, "[files]"),
              "and only the two blocks");
        // the round trip: render -> parse -> the same values, and the file is the renderer's fixed point
        const UtConfig first = ut::g_cfg;
        logClear();
        ut::configReload(ini);   // the stamp: the rewrite is re-read
        writeAll(ini, text, len);
        ut::configReload(ini);
        check(!logHas("migrated") && differingKeys(ut::g_cfg, first, true) == 0,
              "the rewritten file parses to the same values, nothing migrated again");
        static char again[16384];
        const size_t na = ut::configRender(again, sizeof(again), ut::g_cfg);
        check(text && na == len && !memcmp(again, text, len), "and renders to the same bytes");
        free(text);
        (void)off;
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

    // ---- 12. a mod's own files ---------------------------------------------------------------
    printf("\n12. a mod's own settings: seeded once, published, seeded again; search per game, its gate at start\n");
    {
        wchar_t gameIni[MAX_PATH], gameState[MAX_PATH], gameDir[MAX_PATH], modsDir[MAX_PATH];
        _snwprintf_s(modsDir, _TRUNCATE, L"%s\\mods", dir);
        _snwprintf_s(gameDir, _TRUNCATE, L"%s\\mods\\mymod", dir);
        _snwprintf_s(gameIni, _TRUNCATE, L"%s\\uniquetab.ini", gameDir);
        _snwprintf_s(gameState, _TRUNCATE, L"%s\\uniquetab.state", gameDir);
        {
            std::wstring gi, gs;
            ut::configGameFiles(ini, "mymod", &gi, &gs);
            check(gi == gameIni && gs == gameState, "a mod's files: mods\\<leaf>\\uniquetab.ini and .state");
            ut::configGameFiles(ini, "", &gi, &gs);
            check(gi.empty() && gs == state, "the main game: no own file, the root's uniquetab.state");
            ut::configGameFiles(ini, "bad leaf!", &gi, &gs);
            check(gi.empty() && gs == state, "a leaf no folder may carry is the main game");
        }
        DeleteFileW(ini);
        DeleteFileW(state);
        ut::configReload(ini);   // the template
        DWORD len = 0;
        char* text = readAll(ini, &len);
        check(patchKey(text, &len, "tooltip_mark=1", "tooltip_mark=0") &&
                  patchKey(text, &len, "owned_marks=3", "owned_marks=2"),
              "the main file: tooltip_mark=0, owned_marks=2");
        writeAll(ini, text, len);
        free(text);
        ut::configReload(ini);
        check(ut::g_cfg.tooltipMark == 0 && ut::g_cfg.ownedMarks == 2, "the main values are in force");
        const long gen0 = ut::configGameGeneration();
        logClear();
        check(ut::configSetGame("mymod") && ut::configGameGeneration() == gen0 + 1,
              "entering the mod switches the game");
        text = readAll(gameIni, &len);
        check(text != nullptr && logHas("the custom game mymod has its own settings now"),
              "its own file was written at the first entry (THE SEED), and the log says so");
        bool complete = text != nullptr;
        for (int i = 0; complete && i < ut::kUtCfgKeyCount; ++i)
            if (!keyLinesOk(text, ut::kUtCfgKeys[i])) complete = false;
        check(complete && strstr(text, "your settings for the custom game \"mymod\""),
              "a complete file: every key on one line of its own, its own header");
        check(hasLiveLine(text, "tooltip_mark=0") && hasLiveLine(text, "owned_marks=2"),
              "seeded from the main file's values, not the defaults");
        check(hasLiveLine(text, "search=1") && strstr(text, "\r\n;journal_dir=") &&
                  !hasLiveLine(text, "journal_dir=") &&
                  strstr(text, "read at start-up from the main uniquetab.ini only - set it there"),
              "search is a live line of the game's own file (seeded from the main file's value); "
              "journal_dir is a commented line naming the main file");
        check(ut::g_cfg.tooltipMark == 0 && ut::g_cfg.ownedMarks == 2, "the seeded values are in force");
        // the game's file changes a key: in force at once, the main file untouched
        check(patchKey(text, &len, "tooltip_mark=0", "tooltip_mark=1"), "the mod's file: tooltip_mark=1");
        writeAll(gameIni, text, len);
        free(text);
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.tooltipMark == 1 && logHas("the custom game mymod's own settings were re-read"),
              "the mod's own value is published on the next read");
        text = readAll(ini, &len);
        check(hasLiveLine(text, "tooltip_mark=0"), "the main file still says tooltip_mark=0");
        // nothing is inherited after the seed: a main edit does not reach the mod
        check(patchKey(text, &len, "owned_marks=2", "owned_marks=1"), "the main file: owned_marks=1");
        writeAll(ini, text, len);
        free(text);
        ut::configReload(ini);
        check(ut::g_cfg.ownedMarks == 2, "the mod keeps owned_marks=2: nothing is inherited after the seed");
        // search: the game's own value; the key gate is decided at start from the main
        // file OR any game's file
        text = readAll(ini, &len);
        check(patchKey(text, &len, "search=1", "search=0"), "the main file: search=0");
        writeAll(ini, text, len);
        free(text);
        ut::configReload(ini);
        check(ut::g_cfg.search == 1 && ut::g_searchInFile == 1,
              "the mod keeps its own search=1 under a main search=0 (nothing inherited)");
        {
            char games[256];
            check(ut::configSearchGateAtStart(games, sizeof(games)) && strstr(games, "mods\\mymod\\uniquetab.ini"),
                  "the gate: main search=0, the mod's file search=1 -> installed, the file named");
        }
        text = readAll(gameIni, &len);
        check(patchKey(text, &len, "search=1", "search=0"), "the mod's file: search=0");
        writeAll(gameIni, text, len);
        free(text);
        ut::configReload(ini);
        check(ut::g_cfg.search == 0 && ut::g_searchInFile == 0, "the mod's search=0 is in force in the mod");
        {
            char games[256];
            check(!ut::configSearchGateAtStart(games, sizeof(games)) && !games[0],
                  "the gate: search=0 in every file -> not installed");
        }
        // a file written while search was main-only (no search line) is completed once
        text = readAll(gameIni, &len);
        {
            std::string t(text, len);
            const size_t at = t.find("\r\nsearch=0");
            const size_t end = at == std::string::npos ? at : t.find("\r\n", at + 2);
            if (end != std::string::npos) t.erase(at + 2, end - at);
            writeAll(gameIni, t.c_str(), (DWORD)t.size());
        }
        free(text);
        logClear();
        ut::configReload(ini);
        text = readAll(gameIni, &len);
        check(ut::g_cfg.search == 0 && hasLiveLine(text, "search=0") && logHas("gains search=0"),
              "a game file with no search line gains the main file's value once, rewritten");
        free(text);
        // the persist goes to the file in force; the state to the game's state file
        check(ut::configPersistInt("mp_collect", 0), "mp_collect=0 is persisted while the mod is played");
        text = readAll(gameIni, &len);
        check(hasLiveLine(text, "mp_collect=0"), "into the mod's own file");
        free(text);
        text = readAll(ini, &len);
        check(!hasLiveLine(text, "mp_collect=0"), "not into the main file");
        free(text);
        check(ut::configPersistInt("owned_only", 1) && ut::configPersistInt("uniq_group", 3),
              "the OWN switch and the group are persisted");
        text = readAll(gameState, &len);
        check(text && hasLiveLine(text, "owned_only=1") && hasLiveLine(text, "uniq_group=3"),
              "into the mod's own uniquetab.state");
        free(text);
        check(GetFileAttributesW(state) == INVALID_FILE_ATTRIBUTES, "the main game's state is untouched");
        ut::configReload(ini);
        check(ut::g_cfg.ownedOnly == 1 && ut::g_cfg.uniqGroup == 3 && ut::g_cfg.mpCollect == 0,
              "and read back");
        // back to the main game, and into the mod again
        logClear();
        check(ut::configSetGame("") && ut::configGameGeneration() == gen0 + 2, "the main game again");
        check(ut::g_cfg.tooltipMark == 0 && ut::g_cfg.ownedMarks == 1 && ut::g_cfg.mpCollect == 1 &&
                  ut::g_cfg.ownedOnly == 0 && ut::g_cfg.uniqGroup == -1 &&
                  logHas("the main game uses the main settings"),
              "the main values and the main state are in force");
        check(!ut::configSetGame(""), "the same game again changes nothing");
        logClear();
        check(ut::configSetGame("mymod") && logHas("the mymod game's own settings are in force"),
              "into the mod again: its own file, not seeded again");
        check(ut::g_cfg.tooltipMark == 1 && ut::g_cfg.ownedOnly == 1 && ut::g_cfg.uniqGroup == 3,
              "its values and its state come back");
        // deleted while the mod is played: seeded again from the main file's values
        DeleteFileW(gameIni);
        logClear();
        ut::configReload(ini);
        text = readAll(gameIni, &len);
        check(text && logHas("mods\\mymod\\uniquetab.ini was deleted - written again"),
              "a deleted mod file is written again at the next read");
        check(text && hasLiveLine(text, "tooltip_mark=0") && hasLiveLine(text, "owned_marks=1") &&
                  ut::g_cfg.tooltipMark == 0 && ut::g_cfg.ownedMarks == 1,
              "from the main file's values as they are now (the seed), and in force");
        free(text);
        ut::configSetGame("");
        DeleteFileW(gameIni);
        DeleteFileW(gameState);
        RemoveDirectoryW(gameDir);
        RemoveDirectoryW(modsDir);
        DeleteFileW(state);
    }

    printf("\n22. [layout]: the pad's and the label's rectangles, the retired pad keys (ini_version 22)\n");
    {
        DeleteFileW(state);
        writeText(ini, "ini_version=21\r\n[general]\r\ntooltip_mark=0\r\n[advanced]\r\npad_x=-3\r\npad_y=8\r\n"
                       "pad_h=16 ; mine\r\npad_gap=2\r\nplate_label_size=15\r\n");
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.iniVersion == 27 && logHas("migrated from ini_version=21 to 27") &&
                  logHas("the file had pad_x=-3, pad_y=8, pad_h=16, pad_gap=2, not carried") &&
                  !logHas("no longer has were dropped"),
              "21 -> 22: the four retired keys named with their values in one INFO line, none silently dropped");
        DWORD len = 0;
        char* text = readAll(ini, &len);
        check(text && !strstr(text, "pad_y=") && !strstr(text, "pad_gap=") && hasKeyLine(text, "tooltip_mark=0") &&
                  strstr(text, "plate_label_size=15") && strstr(text, "\r\n[layout]\r\n") &&
                  ut::g_cfg.tooltipMark == 0 && ut::g_cfg.plateLabelSize == 15,
              "the rewritten file: every other value kept, the pad keys gone, a [layout] block");
        free(text);
        writeText(ini, "ini_version=27\r\n[general]\r\n[layout]\r\npad_565x637=27,40,512,40\r\n"
                       "label_565x637 = 27,22,512,16 ; above the pad\r\npad_640x480=1,2,3\r\n"
                       "label_800x600=\r\npad_x=5\r\n[advanced]\r\nlayout_outline=1\r\n");
        logClear();
        ut::configReload(ini);
        const char* pv = ut::configFamilyValue(ut::g_cfg, "pad_565x637");
        const char* lv = ut::configFamilyValue(ut::g_cfg, "label_565x637");
        check(pv && !strcmp(pv, "27,40,512,40") && lv && !strcmp(lv, "27,22,512,16") &&
                  ut::g_cfg.layoutOutline == 1 && !logHas("migrated"),
              "pad_565x637 and label_565x637 are read (the comment and the blanks are not the value); layout_outline=1");
        check(logHas("pad_640x480=1,2,3 is refused - not x,y,w,h") &&
                  ut::configFamilyValue(ut::g_cfg, "pad_640x480") == nullptr,
              "a value that is not x,y,w,h: refused with a WARN, no line kept");
        check(logHas("pad_x=5 in uniquetab.ini - not read any more (retired at ini_version 22)"),
              "a retired key in a 22 file: one WARN, never read");
        static char rb[16384];
        const size_t rn = ut::configRender(rb, sizeof(rb), ut::g_cfg);
        const char* secLay = strstr(rb, "\r\n[layout]\r\n");
        const char* secAdv = strstr(rb, "\r\n[advanced]\r\n");
        const char* linePad = strstr(rb, "\r\npad_565x637=27,40,512,40\r\n");
        const char* lineLabel = strstr(rb, "\r\nlabel_565x637=27,22,512,16\r\n");
        check(rn > 0 && secLay && secAdv && linePad && lineLabel && secLay < linePad && linePad < lineLabel && lineLabel < secAdv && hasLiveLine(rb, "layout_outline=1\t"),
              "the renderer writes the [layout] lines under [layout], the pad's line first; layout_outline live");
        UtConfig seed = ut::g_cfg;
        check(ut::configFamilyValue(seed, "pad_565x637") != nullptr,
              "a copy of the values carries the lines (a mod's own file is seeded from them)");
        // the format checks of the family
        check(ut::configFamilyOf("pad_565x637", nullptr) && ut::configFamilyOf("label_1x1", nullptr) &&
                  !ut::configFamilyOf("pad_x", nullptr) && !ut::configFamilyOf("pad_565X637", nullptr) &&
                  ut::configFamilyOf("grid_565x637", nullptr),
              "grid_<W>x<H>, pad_<W>x<H> and label_<W>x<H> are the families; pad_x and a capital X are not");
        // the rendered file: a family with no line shows GD's commented example row, a family with
        // a line does not
        check(!strstr(rb, "\r\n;pad_565x637=") && !strstr(rb, "\r\n;label_565x637="),
              "a family with a line: no example row");
        static char rd[16384];
        UtConfig fresh;
        const size_t dn = ut::configRender(rd, sizeof(rd), fresh);
        const char* dLay = strstr(rd, "\r\n[layout]\r\n");
        const char* dAdv = strstr(rd, "\r\n[advanced]\r\n");
        const char* exPad = strstr(rd, "\r\n;pad_565x637=33,606,500,31\t");
        const char* exLabel = strstr(rd, "\r\n;label_565x637=270,622,260,12\t");
        check(dn > 0 && dLay && dAdv && exPad && exLabel && dLay < exPad && exPad < exLabel && exLabel < dAdv &&
                  strstr(exPad, "; the buttons: the group cells are fitted to it") &&
                  strstr(rd, "The in-game editor writes these (the EDIT button, or"),
              "an empty family: GD's commented example row under [layout], with the family's comment");
        // the editor's SAVE: the line inserted UNDER [layout] (GD's insertion), not at the file's end;
        // a NULL value removes a line
        writeText(ini, "ini_version=27\r\n[general]\r\ntooltip_mark=0\r\n\r\n[layout]\r\n; a blurb\r\n"
                       ";pad_565x637=33,606,500,31\t; the example\r\n\r\n[advanced]\r\n;layout_outline=0\r\n");
        ut::configReload(ini);
        {
            const char* k[2] = {"pad_565x637", "label_565x637"};
            const char* v[2] = {"33,40,500,31", nullptr};
            check(ut::configPersistStr(k, v, 2), "SAVE's two keys are written in one rewrite");
        }
        DWORD sl = 0;
        char* st = readAll(ini, &sl);
        const char* sLay = st ? strstr(st, "\r\n[layout]\r\n") : nullptr;
        const char* sAdv = st ? strstr(st, "\r\n[advanced]\r\n") : nullptr;
        const char* sPad = st ? strstr(st, "\r\npad_565x637=33,40,500,31\r\n") : nullptr;
        check(sLay && sAdv && sPad && sLay < sPad && sPad < sAdv && strstr(st, "\t; the example\r\n\r\npad_565x637=") == nullptr &&
                  strstr(st, "; the example\r\npad_565x637=33,40,500,31\r\n\r\n[advanced]") && !strstr(st, "label_565x637="),
              "a new family line lands at the end of [layout] (before its blank line), not at the file's end");
        free(st);
        ut::configReload(ini);
        const char* pv2 = ut::configFamilyValue(ut::g_cfg, "pad_565x637");
        check(pv2 && !strcmp(pv2, "33,40,500,31") && ut::configFamilyValue(ut::g_cfg, "label_565x637") == nullptr,
              "read back: the pad's line in force, no label line");
        {
            const char* k[1] = {"pad_565x637"};
            const char* v[1] = {nullptr};
            check(ut::configPersistStr(k, v, 1), "a NULL value is written (RESET + SAVE)");
        }
        st = readAll(ini, &sl);
        check(st && !strstr(st, "\r\npad_565x637=") && strstr(st, "\r\n;pad_565x637=33,606,500,31\t; the example\r\n"),
              "the line is removed, the commented example row stays");
        free(st);
        ut::configReload(ini);
        check(ut::configFamilyValue(ut::g_cfg, "pad_565x637") == nullptr, "read back: the derived pad");
        // layout_edit: the one-shot key, a hidden [advanced] row, written back to 0 by the editor
        writeText(ini, "ini_version=27\r\n[general]\r\n[layout]\r\n[advanced]\r\nlayout_edit=1\r\n");
        ut::configReload(ini);
        check(ut::g_cfg.layoutEdit == 1, "layout_edit=1 is read");
        {
            const char* k[1] = {"layout_edit"};
            const char* v[1] = {"0"};
            check(ut::configPersistStr(k, v, 1), "the editor writes it back to 0");
        }
        st = readAll(ini, &sl);
        check(st && strstr(st, "\r\n;layout_edit=0\r\n") && !strstr(st, "\r\nlayout_edit="),
              "at its default the line is commented again");
        free(st);
        ut::configReload(ini);
        check(ut::g_cfg.layoutEdit == 0, "read back: 0");
        writeText(ini, "ini_version=27\r\n[general]\r\n");
        ut::configReload(ini);
    }


    printf("\n23. GD's last settings: ui_scale_pct, collect_pristine_only, pad_caption_max, journal_dir (ini_version 23)\n");
    {
        // the four rows: [advanced], hidden, GD's ranges and defaults
        struct Want {
            const char* name;
            int type, def, lo, hi;
        } want[4] = {{"ui_scale_pct", ut::kUtCfgInt, 0, 0, 300},
                     {"collect_pristine_only", ut::kUtCfgBool, 0, 0, 1},
                     {"pad_caption_max", ut::kUtCfgInt, 14, 6, 36},
                     {"journal_dir", ut::kUtCfgStr, 0, 0, 0}};
        int rows = 0;
        for (int w = 0; w < 4; ++w)
            for (int i = 0; i < ut::kUtCfgKeyCount; ++i) {
                const ut::UtCfgKey& k = ut::kUtCfgKeys[i];
                if (strcmp(k.name, want[w].name) != 0) continue;
                if (!strcmp(k.section, "advanced") && (k.flags & ut::kUtCfgHidden) && k.type == want[w].type &&
                    (k.type == ut::kUtCfgStr ? !k.defStr[0]
                                             : k.def == want[w].def && k.lo == want[w].lo && k.hi == want[w].hi))
                    ++rows;
            }
        check(rows == 4, "the four keys are [advanced] rows of the blob with GD's ranges and defaults");
        check(ut::configMainOnlyKey("journal_dir") && ut::configMainOnlyKey("JOURNAL_DIR") &&
                  !ut::configMainOnlyKey("search") && !ut::configMainOnlyKey("pad_caption_max"),
              "journal_dir is the one main-only key");
        check(ut::utUiScaleOf(0, 1.25f) == 1.25f && ut::utUiScaleOf(150, 1.0f) == 1.5f &&
                  ut::utUiScaleOf(20, 1.0f) == 0.5f && ut::utUiScaleOf(300, 1.0f) == 3.0f &&
                  ut::utUiScaleOf(999, 1.0f) == 3.0f && ut::utUiScaleOf(-5, 0.75f) == 0.75f,
              "ui_scale_pct: 0 follows the measured scale, else the percentage clamped to 50..300");

        // a 22 file like the player's: screen keys, a grid line, the commented example rows and a
        // commented search_debug_query - carried to 23 with every line kept
        DeleteFileW(state);
        writeText(ini, "ini_version=22\r\n[general]\r\nenabled=1                 ; 0 = the mod goes quiet\r\n"
                       "collect_mi=1              ; mi\r\ncollect_unobtainable=0    ; un\r\n"
                       "tooltip_mark=1            ; tt\r\nlog_level=trace           ; ll\r\n"
                       "view_hotkey=117           ; vk\r\nsearch=1                  ; se\r\n\r\n"
                       "[layout]\r\n; a blurb\r\n"
                       ";pad_565x637=33,606,500,31\t\t; the buttons: the group cells are fitted to it\r\n"
                       ";label_565x637=270,622,260,12\t\t; the label line\r\n"
                       "pad_screen=bl,242,107,631,57\r\nlabel_screen=br,1066,66,259,89\r\n"
                       "grid_565x637=0,112,544,512\r\n\r\n[advanced]\r\n; a blurb\r\n"
                       ";mp_collect=1\t\t\t\t; mp\r\n;search_debug_query=\t\t\t; a search applied\r\n"
                       ";export_csv=0\t\t\t\t; csv\r\n;plate_label_size=13\t\t\t; size\r\n"
                       ";font=fonts/albertus mt light.fnt\t; the caption font\r\n"
                       ";text_language=EN\t\t\t; lang\r\n");
        logClear();
        ut::configReload(ini);
        check(ut::g_cfg.iniVersion == 27 && logHas("migrated from ini_version=22 to 27") &&
                  !logHas("no longer has were dropped") && !logHas("retired"),
              "22 -> 25: migrated, nothing dropped, nothing named retired");
        DWORD len = 0;
        char* text = readAll(ini, &len);
        check(text && strstr(text, "\r\nini_version=27\r\n") && completeFile(text) &&
                  hasLiveLine(text, "pad_screen=bl,242,107,631,57") &&
                  hasLiveLine(text, "label_screen=br,1066,66,259,89") &&
                  hasLiveLine(text, "grid_565x637=0,112,544,512\r\n") &&
                  hasLiveLine(text, "log_level=trace") && hasLiveLine(text, "view_hotkey=117"),
              "the rewritten file: ini_version=27, complete, the screen keys, the grid line and every value kept");
        check(text && strstr(text, "\r\n;search_debug_query=") && !hasLiveLine(text, "search_debug_query=") &&
                  strstr(text, "\r\n;pad_565x637=33,606,500,31") && strstr(text, "\r\n;label_565x637=270,622,260,12"),
              "the commented rows stay commented (search_debug_query, the two example rows)");
        check(text && strstr(text, "\r\n;ui_scale_pct=0") && strstr(text, "\r\n;collect_pristine_only=0") &&
                  strstr(text, "\r\n;pad_caption_max=14") && strstr(text, "\r\n;journal_dir=") &&
                  !hasLiveLine(text, "journal_dir="),
              "the four new keys are commented at their defaults");
        check(!strcmp(ut::g_cfg.padScreen, "bl,242,107,631,57") && !strcmp(ut::g_cfg.labelScreen, "br,1066,66,259,89") &&
                  ut::configFamilyValue(ut::g_cfg, "grid_565x637") &&
                  !strcmp(ut::configFamilyValue(ut::g_cfg, "grid_565x637"), "0,112,544,512") &&
                  ut::g_cfg.uiScalePct == 0 && ut::g_cfg.collectPristineOnly == 0 && ut::g_cfg.padCaptionMax == 14 &&
                  !ut::g_cfg.journalDir[0],
              "in force: the screen keys, the grid line, the four keys at their defaults");
        // read back: a current file, no second migration
        logClear();
        free(text);
        writeText(ini, "ini_version=27\r\n[general]\r\n[advanced]\r\nui_scale_pct=150\r\ncollect_pristine_only=1\r\n"
                       "pad_caption_max=20\r\njournal_dir=my saves\\tq   ; mine\r\n");
        ut::configReload(ini);
        check(!logHas("migrated") && ut::g_cfg.uiScalePct == 150 && ut::g_cfg.collectPristineOnly == 1 &&
                  ut::g_cfg.padCaptionMax == 20 && !strcmp(ut::g_cfg.journalDir, "my saves\\tq"),
              "each key read live: ui_scale_pct=150, collect_pristine_only=1, pad_caption_max=20, journal_dir (comment cut)");
        static char r23[16384];
        const size_t n23 = ut::configRender(r23, sizeof(r23), ut::g_cfg);
        check(n23 > 0 && hasLiveLine(r23, "ui_scale_pct=150\t") && hasLiveLine(r23, "collect_pristine_only=1\t") &&
                  hasLiveLine(r23, "pad_caption_max=20\t") && hasLiveLine(r23, "journal_dir=my saves\\tq\t"),
              "the renderer writes a set key live, its comment beside it");
        writeText(ini, "ini_version=27\r\n[advanced]\r\nui_scale_pct=400\r\npad_caption_max=3\r\n");
        ut::configReload(ini);
        check(ut::g_cfg.uiScalePct == 300 && ut::g_cfg.padCaptionMax == 6,
              "out of range: ui_scale_pct=400 -> 300, pad_caption_max=3 -> 6 (clamped)");

        // journal_dir in a mod's own file: a commented line naming the main file; a live line there
        // is ignored with one WARN, and the main file's value stays in force
        writeText(ini, "ini_version=27\r\n[advanced]\r\njournal_dir=my saves\\tq\r\n");
        ut::configReload(ini);
        wchar_t gIni[MAX_PATH], gState[MAX_PATH], gDir[MAX_PATH], gMods[MAX_PATH];
        _snwprintf_s(gMods, _TRUNCATE, L"%s\\mods", dir);
        _snwprintf_s(gDir, _TRUNCATE, L"%s\\mods\\jdmod", dir);
        _snwprintf_s(gIni, _TRUNCATE, L"%s\\uniquetab.ini", gDir);
        _snwprintf_s(gState, _TRUNCATE, L"%s\\uniquetab.state", gDir);
        ut::configSetGame("jdmod");
        text = readAll(gIni, &len);
        check(text && strstr(text, "\r\n;journal_dir=\t") && !hasLiveLine(text, "journal_dir=") &&
                  strstr(text, "the main uniquetab.ini, except journal_dir"),
              "a mod's own file: journal_dir commented, its header names the exception");
        check(!strcmp(ut::g_cfg.journalDir, "my saves\\tq"), "in the mod: the main file's journal_dir is in force");
        {
            std::string t(text ? text : "", text ? len : 0);
            t += "journal_dir=elsewhere\r\n";
            writeAll(gIni, t.c_str(), (DWORD)t.size());
        }
        free(text);
        logClear();
        ut::configReload(ini);
        check(logHas("journal_dir is read at start-up from the main uniquetab.ini - the line in") &&
                  !strcmp(ut::g_cfg.journalDir, "my saves\\tq"),
              "a live journal_dir line in a mod's file: one WARN, ignored");
        ut::configSetGame("");
        DeleteFileW(gIni);
        DeleteFileW(gState);
        RemoveDirectoryW(gDir);

        // a mod's own 22 file with a commented grid row, no label_screen and a commented [advanced]
        // row; the main file has all three live and two of the new keys set: after the step the
        // mod's file keeps its own layout and rows, and only the new keys come from the main file
        writeText(ini, "ini_version=27\r\n[layout]\r\nlabel_screen=br,1066,66,259,89\r\n"
                       "grid_565x637=0,112,544,512\r\n[advanced]\r\nexport_csv=1\r\nui_scale_pct=150\r\n"
                       "pad_caption_max=20\r\n");
        ut::configReload(ini);
        _snwprintf_s(gDir, _TRUNCATE, L"%s\\mods\\h1mod", dir);
        _snwprintf_s(gIni, _TRUNCATE, L"%s\\uniquetab.ini", gDir);
        _snwprintf_s(gState, _TRUNCATE, L"%s\\uniquetab.state", gDir);
        CreateDirectoryW(gMods, nullptr);
        CreateDirectoryW(gDir, nullptr);
        writeText(gIni, "ini_version=22\r\n[general]\r\nenabled=1\r\nlog_level=debug\r\n\r\n[layout]\r\n"
                        ";grid_565x637=27,126,512,480\t\t; the item grid\r\n"
                        "pad_screen=bl,10,10,300,40\r\n\r\n[advanced]\r\n;export_csv=0\t\t\t\t; csv\r\n");
        logClear();
        ut::configSetGame("h1mod");
        check(logHas("(ini_version=22) migrated to a complete file, ini_version=27") &&
                  logHas("the keys that version did not have copied once from the main uniquetab.ini"),
              "a mod's own 22 file: migrated, only the keys 22 did not have copied from the main file");
        text = readAll(gIni, &len);
        check(text && strstr(text, "\r\n;grid_565x637=") && !hasLiveLine(text, "grid_565x637=") &&
                  !hasLiveLine(text, "label_screen=") && hasLiveLine(text, "pad_screen=bl,10,10,300,40") &&
                  strstr(text, "\r\n;export_csv=0") && !hasLiveLine(text, "export_csv=") &&
                  hasLiveLine(text, "log_level=debug") && hasLiveLine(text, "ui_scale_pct=150") &&
                  hasLiveLine(text, "pad_caption_max=20"),
              "the mod's file keeps its commented grid row, its absent label_screen, its own pad_screen and "
              "commented export_csv; ui_scale_pct and pad_caption_max come from the main file");
        check(!ut::configFamilyValue(ut::g_cfg, "grid_565x637") && !ut::g_cfg.labelScreen[0] &&
                  !strcmp(ut::g_cfg.padScreen, "bl,10,10,300,40") && ut::g_cfg.exportCsv == 0 &&
                  ut::g_cfg.uiScalePct == 150 && ut::g_cfg.padCaptionMax == 20 && ut::g_cfg.collectPristineOnly == 0,
              "in force in the mod: the derived grid and label, its own pad_screen, export_csv 0, the new keys from the main file");
        free(text);
        ut::configSetGame("");
        DeleteFileW(gIni);
        DeleteFileW(gState);
        RemoveDirectoryW(gDir);
        RemoveDirectoryW(gMods);
        writeText(ini, "ini_version=27\r\n[general]\r\n");
        ut::configReload(ini);
    }

    printf("\n24. edit_button (ini_version 24): the EDIT button on the pad\n");
    {
        const UtConfig dc;
        int row = -1;
        for (int i = 0; i < ut::kUtCfgKeyCount; ++i)
            if (!strcmp(ut::kUtCfgKeys[i].name, "edit_button")) row = i;
        check(UT_INI_VERSION == 27 && dc.editButton == 1 && row >= 0 && !strcmp(ut::kUtCfgKeys[row].section, "advanced") &&
                  ut::kUtCfgKeys[row].def == 1 && ut::kUtCfgKeys[row].type == ut::kUtCfgBool,
              "edit_button: [advanced], a 0/1 row, default 1; ini_version 24");
        // a 23 file: migrated, every line kept, edit_button gained commented at its default
        writeText(ini, "ini_version=23\r\n[general]\r\nenabled=1\r\n\r\n[layout]\r\npad_565x637=27,560,512,40\r\n"
                       "\r\n[advanced]\r\n;export_csv=0\r\nui_scale_pct=150\r\n");
        logClear();
        ut::configReload(ini);
        DWORD len = 0;
        char* text = readAll(ini, &len);
        check(ut::g_cfg.iniVersion == 27 && logHas("migrated from ini_version=23 to 27") && ut::g_cfg.editButton == 1 &&
                  ut::g_cfg.uiScalePct == 150 && text && strstr(text, "\r\nini_version=27\r\n") &&
                  strstr(text, "\r\n;edit_button=1") && !hasLiveLine(text, "edit_button=") &&
                  hasLiveLine(text, "pad_565x637=27,560,512,40") && hasLiveLine(text, "ui_scale_pct=150"),
              "23 -> 25: migrated, every value and line kept, ;edit_button=1 gained (commented at its default)");
        free(text);
        writeText(ini, "ini_version=27\r\n[general]\r\n[advanced]\r\nedit_button=0\r\n");
        logClear();
        ut::configReload(ini);
        text = readAll(ini, &len);
        check(ut::g_cfg.editButton == 0 && text && hasLiveLine(text, "edit_button=0"),
              "edit_button=0 is read and kept live (no button; Ctrl+right-click still opens the editor)");
        free(text);
        // a mod's own 23 file has no edit_button row: it takes the main file's value; its own
        // commented rows stay its own
        wchar_t eIni[MAX_PATH], eState[MAX_PATH], eDir[MAX_PATH], eMods[MAX_PATH];
        _snwprintf_s(eMods, _TRUNCATE, L"%s\\mods", dir);
        _snwprintf_s(eDir, _TRUNCATE, L"%s\\mods\\e1mod", dir);
        _snwprintf_s(eIni, _TRUNCATE, L"%s\\uniquetab.ini", eDir);
        _snwprintf_s(eState, _TRUNCATE, L"%s\\uniquetab.state", eDir);
        CreateDirectoryW(eMods, nullptr);
        CreateDirectoryW(eDir, nullptr);
        writeText(eIni, "ini_version=23\r\n[general]\r\nenabled=1\r\n\r\n[advanced]\r\n;export_csv=0\t\t; csv\r\n");
        logClear();
        ut::configSetGame("e1mod");
        text = readAll(eIni, &len);
        check(logHas("(ini_version=23) migrated to a complete file, ini_version=27") && ut::g_cfg.editButton == 0 &&
                  ut::g_cfg.exportCsv == 0 && text && hasLiveLine(text, "edit_button=0") &&
                  strstr(text, "\r\n;export_csv=0") && !hasLiveLine(text, "export_csv="),
              "a mod's own 23 file takes the main file's edit_button=0 (the key it did not have); its commented "
              "export_csv stays commented");
        free(text);
        ut::configSetGame("");
        DeleteFileW(eIni);
        DeleteFileW(eState);
        RemoveDirectoryW(eDir);
        RemoveDirectoryW(eMods);
        writeText(ini, "ini_version=27\r\n[general]\r\n");
        ut::configReload(ini);
    }

    printf("\n25. the relic pages (ini_version 25): relics_<W>x<H>, relic_block_gap, relic_bonus_gap\n");
    {
        const UtConfig dc;
        int rb = -1, rg = -1;
        for (int i = 0; i < ut::kUtCfgKeyCount; ++i) {
            if (!strcmp(ut::kUtCfgKeys[i].name, "relic_block_gap")) rb = i;
            if (!strcmp(ut::kUtCfgKeys[i].name, "relic_bonus_gap")) rg = i;
        }
        check(UT_INI_VERSION == 27 && rb >= 0 && rg >= 0 && dc.relicBlockGap == 1 && dc.relicBonusGap == 1 &&
                  !strcmp(ut::kUtCfgKeys[rb].section, "advanced") && ut::kUtCfgKeys[rb].type == ut::kUtCfgInt &&
                  ut::kUtCfgKeys[rb].def == 1 && ut::kUtCfgKeys[rb].lo == 0 && ut::kUtCfgKeys[rb].hi == 2 &&
                  !strcmp(ut::kUtCfgKeys[rg].section, "advanced") && ut::kUtCfgKeys[rg].type == ut::kUtCfgInt &&
                  ut::kUtCfgKeys[rg].def == 1 && ut::kUtCfgKeys[rg].lo == 0 && ut::kUtCfgKeys[rg].hi == 1,
              "relic_block_gap 0..2 and relic_bonus_gap 0..1: [advanced] rows, default 1; ini_version 25");
        const char* suf = nullptr;
        const ut::UtCfgFamily* fam = ut::configFamilyOf("relics_565x637", &suf);
        check(fam && !strcmp(fam->prefix, "relics_") && !strcmp(fam->section, "layout") && suf &&
                  !strcmp(suf, "565x637") && dc.layRelics.count == 0 &&
                  ut::configFamilyOf("relics_565", nullptr) == nullptr,
              "relics_<W>x<H> is a [layout] family, no line by default (a key without a window size is not one)");
        // a 24 file: migrated, every value and line kept, the new rows commented at their defaults
        writeText(ini, "ini_version=24\r\n[general]\r\nenabled=1\r\n\r\n[layout]\r\ngrid_565x637=24,124,512,480\r\n"
                       "\r\n[advanced]\r\n;export_csv=0\r\nedit_button=0\r\n");
        logClear();
        ut::configReload(ini);
        DWORD len = 0;
        char* text = readAll(ini, &len);
        check(ut::g_cfg.iniVersion == 27 && logHas("migrated from ini_version=24 to 27") &&
                  ut::g_cfg.relicBlockGap == 1 && ut::g_cfg.relicBonusGap == 1 && ut::g_cfg.editButton == 0 &&
                  ut::g_cfg.layRelics.count == 0 && text && strstr(text, "\r\nini_version=27\r\n") &&
                  strstr(text, "\r\n;relic_block_gap=1") && strstr(text, "\r\n;relic_bonus_gap=1") &&
                  strstr(text, "\r\n;relics_565x637=27,126,512,480") && !hasLiveLine(text, "relic_block_gap=") &&
                  !hasLiveLine(text, "relic_bonus_gap=") && !hasLiveLine(text, "relics_") &&
                  hasLiveLine(text, "grid_565x637=24,124,512,480") && hasLiveLine(text, "edit_button=0") &&
                  strstr(text, "\r\n;export_csv=0") && !hasLiveLine(text, "export_csv="),
              "24 -> 25: migrated, every value and line kept, the relic rows gained commented at their defaults "
              "(no new live line)");
        free(text);
        // the relic area per window size, the two gaps read and clamped
        writeText(ini, "ini_version=27\r\n[general]\r\n[layout]\r\nrelics_565x637=40,150,480,416\r\n"
                       "relics_660x699=0,59,640,640\r\n[advanced]\r\nrelic_block_gap=5\r\nrelic_bonus_gap=0\r\n");
        logClear();
        ut::configReload(ini);
        text = readAll(ini, &len);
        const char* rv = ut::configFamilyGet(ut::g_cfg.layRelics, "565x637");
        const char* rw = ut::configFamilyGet(ut::g_cfg.layRelics, "660x699");
        check(rv && !strcmp(rv, "40,150,480,416") && rw && !strcmp(rw, "0,59,640,640") &&
                  ut::g_cfg.relicBlockGap == 2 && ut::g_cfg.relicBonusGap == 0 && text &&
                  hasLiveLine(text, "relics_565x637=40,150,480,416") && hasLiveLine(text, "relics_660x699=0,59,640,640") &&
                  hasLiveLine(text, "relic_bonus_gap=0"),
              "relics_<W>x<H> is read per window size and kept live; relic_block_gap=5 is clamped to 2, "
              "relic_bonus_gap=0 is read");
        free(text);
        // a mod's own 24 file takes the main file's two gaps (the keys it did not have) and keeps its
        // own [layout]: the main file's relics_ line is not its own
        writeText(ini, "ini_version=27\r\n[general]\r\n[layout]\r\nrelics_565x637=40,150,480,416\r\n"
                       "[advanced]\r\nrelic_block_gap=2\r\n");
        ut::configReload(ini);
        wchar_t rIni[MAX_PATH], rState[MAX_PATH], rDir[MAX_PATH], rMods[MAX_PATH];
        _snwprintf_s(rMods, _TRUNCATE, L"%s\\mods", dir);
        _snwprintf_s(rDir, _TRUNCATE, L"%s\\mods\\r1mod", dir);
        _snwprintf_s(rIni, _TRUNCATE, L"%s\\uniquetab.ini", rDir);
        _snwprintf_s(rState, _TRUNCATE, L"%s\\uniquetab.state", rDir);
        CreateDirectoryW(rMods, nullptr);
        CreateDirectoryW(rDir, nullptr);
        writeText(rIni, "ini_version=24\r\n[general]\r\nenabled=1\r\n\r\n[layout]\r\ngrid_565x637=11,91,544,448\r\n"
                        "\r\n[advanced]\r\n;export_csv=0\r\n");
        logClear();
        ut::configSetGame("r1mod");
        text = readAll(rIni, &len);
        check(logHas("(ini_version=24) migrated to a complete file, ini_version=27") && ut::g_cfg.relicBlockGap == 2 &&
                  ut::g_cfg.relicBonusGap == 1 && ut::g_cfg.layRelics.count == 0 && text &&
                  hasLiveLine(text, "relic_block_gap=2") && strstr(text, "\r\n;relic_bonus_gap=1") &&
                  !hasLiveLine(text, "relics_") && hasLiveLine(text, "grid_565x637=11,91,544,448"),
              "a mod's own 24 file takes the main file's relic_block_gap=2 (the key it did not have) and keeps its "
              "own [layout]: no relics_ line");
        free(text);
        ut::configSetGame("");
        DeleteFileW(rIni);
        DeleteFileW(rState);
        RemoveDirectoryW(rDir);
        RemoveDirectoryW(rMods);
        writeText(ini, "ini_version=27\r\n[general]\r\n");
        ut::configReload(ini);
    }

    // The player's own files, when TQ_TEST_INI_DIR names the mod's folder (its uniquetab.ini and every
    // mods\<leaf>\uniquetab.ini): each is COPIED here and migrated as the game would, and must keep
    // every live line it had (key and value), gain no live line, and say the current ini_version.
    printf("\n26. the relic panels (ini_version 26): relic_panels\n");
    {
        const UtConfig dc;
        int rp = -1;
        for (int i = 0; i < ut::kUtCfgKeyCount; ++i)
            if (!strcmp(ut::kUtCfgKeys[i].name, "relic_panels")) rp = i;
        check(UT_INI_VERSION >= 26 && rp >= 0 && dc.relicPanels == 1 &&
                  !strcmp(ut::kUtCfgKeys[rp].section, "advanced") && ut::kUtCfgKeys[rp].def == 1,
              "relic_panels: an [advanced] row, default 1 (every block on its own panel); ini_version 26");
    }

    printf("\n27. the relic page on or off (ini_version 27): collect_relics, as collect_mi\n");
    {
        const UtConfig dc;
        int cr = -1, cm = -1;
        for (int i = 0; i < ut::kUtCfgKeyCount; ++i) {
            if (!strcmp(ut::kUtCfgKeys[i].name, "collect_relics")) cr = i;
            if (!strcmp(ut::kUtCfgKeys[i].name, "collect_mi")) cm = i;
        }
        check(UT_INI_VERSION == 27 && cr >= 0 && cr == cm + 1 && dc.collectRelics == 1 &&
                  !strcmp(ut::kUtCfgKeys[cr].section, "general") && ut::kUtCfgKeys[cr].def == 1 &&
                  ut::kUtCfgKeys[cr].flags == ut::kUtCfgKeys[cm].flags &&
                  strstr(ut::kUtCfgKeys[cr].comment, "1 = relics and charms have their own page") != nullptr &&
                  strstr(ut::kUtCfgKeys[cr].comment, "0 = no relic page") != nullptr &&
                  strstr(ut::kUtCfgKeys[cr].comment, "hidden until it is 1 again (from the next start)") != nullptr,
              "collect_relics: a [general] row right after collect_mi, default 1, collect_mi's help style; ini_version 27");
        // the main file from 26: every value kept, the key gained live at its default
        writeText(ini, "ini_version=26\r\n[general]\r\ncollect_mi=0\r\nsearch=0\r\n");
        logClear();
        ut::configReload(ini);
        DWORD len = 0;
        char* text = readAll(ini, &len);
        check(ut::g_cfg.collectRelics == 1 && ut::g_cfg.collectMi == 0 && ut::g_cfg.search == 0 && text &&
                  hasLiveLine(text, "collect_relics=1") && hasLiveLine(text, "collect_mi=0") &&
                  hasLiveLine(text, "search=0") && hasKeyLine(text, "ini_version=27"),
              "a main file from 26: collect_mi=0 and search=0 kept, collect_relics=1 gained live, ini_version=27");
        const char* gl = text ? strstr(text, "collect_mi=0") : nullptr;
        const char* rl = text ? strstr(text, "collect_relics=1") : nullptr;
        const char* ul = text ? strstr(text, "collect_unobtainable=") : nullptr;
        check(gl && rl && ul && gl < rl && rl < ul, "its line stands between collect_mi and collect_unobtainable");
        free(text);
        // the main file says 0; a game's own file from 26 has no line: the main file's 0 at its entry
        writeText(ini, "ini_version=27\r\n[general]\r\ncollect_relics=0\r\n");
        ut::configReload(ini);
        check(ut::g_cfg.collectRelics == 0, "collect_relics=0 in the main file is in force");
        wchar_t gMods[MAX_PATH], gDir[MAX_PATH], gIni[MAX_PATH], gState[MAX_PATH];
        _snwprintf_s(gMods, _TRUNCATE, L"%s\\mods", dir);
        _snwprintf_s(gDir, _TRUNCATE, L"%s\\mods\\relicgame", dir);
        _snwprintf_s(gIni, _TRUNCATE, L"%s\\uniquetab.ini", gDir);
        _snwprintf_s(gState, _TRUNCATE, L"%s\\uniquetab.state", gDir);
        CreateDirectoryW(gMods, nullptr);
        CreateDirectoryW(gDir, nullptr);
        writeText(gIni, "ini_version=26\r\n[general]\r\ncollect_mi=1\r\ntooltip_mark=0\r\n");
        ut::UtGenSettings gs;
        memset(&gs, 0, sizeof(gs));
        gs.collectRelics = 7;
        check(ut::configGameGenInputs("relicgame", &gs) && gs.collectRelics == 0 && gs.collectMi == 1,
              "the start-up build reads a game file from 26 as its entry does: collect_relics = the main file's 0");
        logClear();
        ut::configSetGame("relicgame");
        text = readAll(gIni, &len);
        check(ut::g_cfg.collectRelics == 0 && ut::g_cfg.tooltipMark == 0 && text &&
                  hasLiveLine(text, "collect_relics=0") && hasLiveLine(text, "tooltip_mark=0") &&
                  hasKeyLine(text, "ini_version=27"),
              "its entry: the main file's collect_relics=0 taken (the key it did not have), its own values kept, ini_version=27");
        free(text);
        ut::configSetGame("");
        // a game's own file at 27 keeps its own value whatever the main file says
        writeText(gIni, "ini_version=27\r\n[general]\r\ncollect_relics=1\r\n");
        memset(&gs, 0, sizeof(gs));
        check(ut::configGameGenInputs("relicgame", &gs) && gs.collectRelics == 1,
              "a game file at 27 with collect_relics=1: its own 1 while the main file says 0");
        ut::configSetGame("relicgame");
        check(ut::g_cfg.collectRelics == 1, "and in force at its entry");
        ut::configSetGame("");
        DeleteFileW(gIni);
        DeleteFileW(gState);
        RemoveDirectoryW(gDir);
        RemoveDirectoryW(gMods);
        writeText(ini, "ini_version=27\r\n[general]\r\n");
        ut::configReload(ini);
    }

    printf("\n25b. the player's own files (TQ_TEST_INI_DIR: copies migrated here, the originals only read)\n");
    {
        wchar_t from[MAX_PATH];
        const DWORD got = GetEnvironmentVariableW(L"TQ_TEST_INI_DIR", from, MAX_PATH);
        if (!got || got >= MAX_PATH) {
            printf("  skipped: TQ_TEST_INI_DIR is not set\n");
        } else {
            char want[32];
            _snprintf_s(want, _TRUNCATE, "ini_version=%d", UT_INI_VERSION);
            // one file: the live lines before and after, compared both ways
            auto sameLive = [&](const char* before, const char* after, const char* what) {
                int missing = 0, added = 0, live = 0;
                for (int pass = 0; pass < 2; ++pass) {
                    const char* a = pass == 0 ? before : after;
                    const char* b = pass == 0 ? after : before;
                    for (const char* p = a; p && *p;) {
                        const char* e = strstr(p, "\n");
                        const size_t n = e ? (size_t)(e - p) : strlen(p);
                        char line[512];
                        _snprintf_s(line, _TRUNCATE, "%.*s", (int)n, p);
                        p = e ? e + 1 : p + n;
                        size_t k = 0;   // the key=value part: up to its blanks, a ';' or the line end
                        while (line[k] && line[k] != ';' && line[k] != '\r' && line[k] != '\t') ++k;
                        while (k > 0 && line[k - 1] == ' ') --k;
                        line[k] = 0;
                        if (!line[0] || line[0] == ';' || line[0] == '[' || !strchr(line, '=')) continue;
                        if (!strncmp(line, "ini_version=", 12)) continue;
                        // the key ini_version 27 adds to [general]: gained by a file from before 27
                        if (pass == 1 && !strncmp(line, "collect_relics=", 15) && !hasLiveLine(b, line)) {
                            printf("  %s: gained %s\n", what, line);
                            continue;
                        }
                        if (pass == 0) ++live;
                        if (!hasLiveLine(b, line) && !(b && !strncmp(b, line, strlen(line)))) {
                            if (pass == 0) ++missing;
                            else ++added;
                            printf("  %s: %s %s\n", what, pass == 0 ? "LOST" : "NEW LIVE LINE", line);
                        }
                    }
                }
                printf("  %s: %d live line(s) kept, %d lost, %d new\n", what, live - missing, missing, added);
                return missing == 0 && added == 0 && after && strstr(after, want) != nullptr &&
                       (hasLiveLine(after, "collect_relics=1") || hasLiveLine(after, "collect_relics=0"));
            };
            wchar_t src[MAX_PATH];
            _snwprintf_s(src, _TRUNCATE, L"%s\\uniquetab.ini", from);
            DWORD n0 = 0, n1 = 0;
            char* before = readAll(src, &n0);
            check(before != nullptr, "the player's main uniquetab.ini is read");
            if (before) {
                writeAll(ini, before, n0);
                logClear();
                ut::configReload(ini);
                char* after = readAll(ini, &n1);
                check(sameLive(before, after, "main"),
                      "the player's main file (a copy): every live line kept, no new live line, the current ini_version");
                free(after);
                free(before);
            }
            wchar_t mask[MAX_PATH];
            _snwprintf_s(mask, _TRUNCATE, L"%s\\mods\\*", from);
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW(mask, &fd);
            int games = 0;
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
                    wchar_t gSrc[MAX_PATH], gMods[MAX_PATH], gDir[MAX_PATH], gIni[MAX_PATH], gState[MAX_PATH];
                    _snwprintf_s(gSrc, _TRUNCATE, L"%s\\mods\\%s\\uniquetab.ini", from, fd.cFileName);
                    DWORD g0 = 0, g1 = 0;
                    char* gBefore = readAll(gSrc, &g0);
                    if (!gBefore) continue;
                    _snwprintf_s(gMods, _TRUNCATE, L"%s\\mods", dir);
                    _snwprintf_s(gDir, _TRUNCATE, L"%s\\mods\\%s", dir, fd.cFileName);
                    _snwprintf_s(gIni, _TRUNCATE, L"%s\\uniquetab.ini", gDir);
                    _snwprintf_s(gState, _TRUNCATE, L"%s\\uniquetab.state", gDir);
                    CreateDirectoryW(gMods, nullptr);
                    CreateDirectoryW(gDir, nullptr);
                    writeAll(gIni, gBefore, g0);
                    char leaf8[MAX_PATH];
                    WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, leaf8, sizeof(leaf8), nullptr, nullptr);
                    logClear();
                    ut::configSetGame(leaf8);
                    char* gAfter = readAll(gIni, &g1);
                    char what[300];
                    _snprintf_s(what, _TRUNCATE, "the player's mods\\%s file (a copy): every live line kept, no "
                                "new live line, the current ini_version", leaf8);
                    check(sameLive(gBefore, gAfter, leaf8), what);
                    ++games;
                    ut::configSetGame("");
                    free(gAfter);
                    free(gBefore);
                    DeleteFileW(gIni);
                    DeleteFileW(gState);
                    RemoveDirectoryW(gDir);
                    RemoveDirectoryW(gMods);
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
            printf("  %d game file(s) of the player's migrated\n", games);
            writeText(ini, "ini_version=27\r\n[general]\r\n");
            ut::configReload(ini);
        }
    }

    // The run's own folder goes with it; nothing but this harness's files is in it.
    DeleteFileW(state);
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
