#include "ut_config.h"
#include "ut_color.h"   // search_mark_color

#include <windows.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ut_log.h"

namespace ut {

UtConfig g_cfg;

// ---- THE TABLE --------------------------------------------------------------------------------
// One row per setting: where it is written, what it is called, what it may hold, where it lands in
// UtConfig, and the one line the file says about it. The parser, the file the mod writes and the
// test suite all read THIS - there is no second list anywhere.
//
// The `def` column repeats the member's own default. tools\test_config.cpp proves the two agree
// for every row, so they cannot drift.

static const char* const kLogLevels[] = {"error", "warn", "info", "debug", "trace", nullptr};

#define UT_OFF(m) offsetof(UtConfig, m)
#define UT_CAP(m) sizeof(UtConfig::m)

// A switch: 0 or 1.
#define UT_BOOL(sec, name, member, def, cmt) \
    {sec, name, kUtCfgBool, def, 0, 1, "", UT_OFF(member), 0, nullptr, cmt}
// A number in an inclusive range.
#define UT_INT(sec, name, member, def, lo, hi, cmt) \
    {sec, name, kUtCfgInt, def, lo, hi, "", UT_OFF(member), 0, nullptr, cmt}
// Text. `choices` is null for free text, or a null-terminated list of the only legal words.
#define UT_STR(sec, name, member, def, choices, cmt) \
    {sec, name, kUtCfgStr, 0, 0, 0, def, UT_OFF(member), UT_CAP(member), choices, cmt}

const UtCfgKey kUtCfgKeys[] = {
    // ---- [general] ---------------------------------------------------------------------------
    UT_STR("general", "log_level", logLevel, "info", kLogLevels,
           "how much goes in the log: error, warn, info, debug, trace"),

    // ---- [collection] ------------------------------------------------------------------------
    UT_BOOL("collection", "mp_collect", mpCollect, 1,
            "1 = the collection works in a MULTIPLAYER game (hosted or joined) exactly as in "
            "single player, for you only; 0 = the view works there but every deposit and take "
            "is refused"),

    // ---- [view] ------------------------------------------------------------------------
    UT_INT("view", "view_hotkey", viewHotkey, 117, 0, 254,
           "the key that switches the Transfer page to the collection and back (a Windows "
           "virtual-key code: 117 = F6; 0 = no key, the button only)"),

    // ---- [display] (GD's keys) ---------------------------------------------------------------
    UT_BOOL("display", "group_buttons", groupButtons, 1,
            "1 = draw the collection buttons beside the caravan and let them be clicked"),
    UT_BOOL("display", "page_hotkeys", pageHotkeys, 1,
            "1 = the wheel over the page scrolls it by one slot row and PageUp/PageDown by a window, Ctrl changes group (Transfer is a stop)"),
    UT_BOOL("display", "plate_label", plateLabel, 1,
            "1 = draw the group name and the slot rows shown over the buttons"),
    UT_INT("display", "owned_marks", ownedMarks, 3, 0, 3,
           "how a record you do not own yet is marked on the collection page: 0 = no mark, 1 = a "
           "dark veil over it, 2 = a thin frame around it, 3 = its icon in gray (the gray copies "
           "are written with the catalogue, gray\\ beside it; the veil where one is missing)"),
    UT_BOOL("display", "have_marks", haveMarks, 0,
            "1 = an uncollected record you already have somewhere (inventory, stash, Transfer, "
            "Relic Vault) gets a small corner tick (GD's \"have one somewhere\" hint). Default 0: "
            "the tooltip line (tooltip_mark) says it instead"),
    UT_BOOL("display", "tooltip_mark", tooltipMark, 1,
            "1 = every item tooltip says whether it is in your collection"),
    UT_INT("display", "slot_plates", slotPlates, 3, 0, 3,
           "the ground under each item of the collection page: 0 = none, 1 = automatic (a dark "
           "plate under the items, or thin frames over them when the plate cannot be drawn), 2 = "
           "thin frames always (use it if the plates do not show), 3 = the equipment window's own "
           "slot ground (its cracked stone and the item type's drawing; 1 when it cannot be loaded)"),
    UT_BOOL("display", "grid_cover", gridCover, 1,
            "1 = the grid cells of the collection page that no slot uses (the rows under the last "
            "slot row, the cells after the last record) are covered in the caravan's ground colour"),
    UT_BOOL("display", "owned_only", ownedOnly, 0,
            "1 = the collection page starts with the OWN filter on (only the records you own); "
            "the OWN button switches it and writes the new value here"),
    UT_BOOL("display", "search", search, 1,
            "1 = the collection page can be searched by item name and properties (\"fire\" finds "
            "Fire Damage and Fire Resistance); 0 = no search at all"),
    UT_BOOL("display", "search_buttons", searchButtons, 1,
            "1 = mark the group buttons that hold a match for your search"),
    UT_INT("display", "search_mark", searchMark, 1, 1, 3,
           "how an item your search finds is marked on the collection page: 1 = a thin frame "
           "round its slot, 2 = a wash under it, 3 = both, in search_mark_color. Nothing is hidden"),
    UT_STR("display", "search_mark_color", searchMarkColor, "gold", nullptr,
           "the colour of that frame and wash: gold, green, white, red, orange, cyan, magenta, "
           "blue, or #rrggbb (red, green and blue in hex, e.g. #ffd700)"),
    UT_BOOL("display", "search_transfer", searchTransfer, 1,
            "1 = the search field is also on the real Transfer page (the collection view off) and "
            "marks the items there that your search finds; 0 = only on the collection page"),
    UT_BOOL("display", "search_lore", searchLore, 0,
            "1 = the search also looks in an item's story text; 0 = names and properties only"),

    // ---- [files] (GD's key) --------------------------------------------------------------
    UT_INT("files", "export_csv", exportCsv, 0, 0, 2,
           "a spreadsheet copy of the journal beside it (tq-uniq-items*.csv), rewritten with every "
           "journal write: 0 = off, 1 = the collection, 2 = also the taken rows no save has seen"),

    // ---- [advanced] --------------------------------------------------------------------------
    UT_INT("advanced", "pad_x", padX, 0, -100, 100,
           "the button pad's shift right (or left, negative) of its centred place under the "
           "grid, in page pixels; clamped to the caravan window's edges"),
    UT_INT("advanced", "pad_y", padY, 4, 0, 40,
           "the button pad's gap below the grid's bottom row, in page pixels"),
    UT_INT("advanced", "pad_h", padH, 14, 12, 20, "height of one button row, in page pixels"),
    UT_INT("advanced", "pad_gap", padGap, 1, 0, 4, "gap between two buttons, in page pixels"),
    UT_INT("advanced", "plate_label_size", plateLabelSize, 13, 8, 32,
           "height of the caption and label text, in page pixels (8..32)"),
    UT_STR("advanced", "font", fontName, "fonts/albertus mt light.fnt", nullptr,
           "the caption font, by its name in Resources\\Fonts.arc"),
    UT_INT("advanced", "caravan_x", caravanX, 10, -2000, 4000,
           "the caravan window's left edge (its record: 10)"),
    UT_INT("advanced", "caravan_y", caravanY, 0, -2000, 2000,
           "the caravan window's offset from the centred position (its record: 0)"),
    UT_INT("advanced", "caravan_w", caravanW, 565, 100, 4000, "the caravan window's width (565)"),
    UT_INT("advanced", "caravan_h", caravanH, 637, 100, 4000, "the caravan window's height (637)"),
    UT_INT("advanced", "page_y", pageY, 126, 0, 2000,
           "the Transfer page's top inside the caravan window (TransferWindow.dbr: 126)"),
    UT_BOOL("advanced", "log_flush_each_line", logFlushEachLine, 0,
            "1 = write every log line at once (slow; for chasing a crash)"),
    UT_BOOL("advanced", "search_prebuild", searchPrebuild, 0,
            "1 = prepare the search's text for every item as soon as the collection is first "
            "shown (a few seconds in the background); 0 = only when a search is first made"),
    UT_STR("advanced", "search_debug_query", searchDebugQuery, "", nullptr,
           "a search applied to the collection page (item names and properties, any case); empty "
           "= none. Read again every second"),
    UT_STR("advanced", "text_language", textLanguage, "EN", nullptr,
           "the suffix of Text\\Text_<lang>.arc the catalogue takes item names from"),
    UT_STR("advanced", "tooltip_text_yes", tooltipTextYes, "", nullptr,
           "the tooltip line for an item you have. Empty = the built-in wording"),
    UT_STR("advanced", "tooltip_text_no", tooltipTextNo, "", nullptr,
           "and the line for one you do not. Empty = the built-in wording"),
    UT_INT("advanced", "tooltip_class_yes", tooltipClassYes, 22, 0, 57,
           "colour of that line, a game text style number (0..57)"),
    UT_INT("advanced", "tooltip_class_no", tooltipClassNo, 24, 0, 57,
           "colour of the other line, a game text style number (0..57)"),
};

const int kUtCfgKeyCount = (int)(sizeof(kUtCfgKeys) / sizeof(kUtCfgKeys[0]));

const UtCfgSection kUtCfgSections[] = {
    {"general", "The basics."},
    {"view", "The collection view on the caravan's Transfer page: drop or quick-move a unique onto "
             "it to deposit, left-click a collected one to take it back."},
    {"display", "What is drawn beside the caravan."},
    {"collection", "Where the collection works: single player always, multiplayer as mp_collect says."},
    {"files", "The journal's exports."},
    {"advanced", "Tuning. A value outside the range in its line is clamped, and the log says so."},
};

const int kUtCfgSectionCount = (int)(sizeof(kUtCfgSections) / sizeof(kUtCfgSections[0]));

namespace {

// The scan's `seen` array is indexed by table row.
const int kMaxKeys = 64;
static_assert(sizeof(kUtCfgKeys) / sizeof(kUtCfgKeys[0]) <= (size_t)kMaxKeys,
              "kMaxKeys must cover the whole key table");

FILETIME g_lastWrite = {0, 0};
bool g_haveStamp = false;
wchar_t g_path[MAX_PATH] = {0};

int* intAt(UtConfig* cfg, size_t off) { return (int*)((char*)cfg + off); }
char* strAt(UtConfig* cfg, size_t off) { return (char*)cfg + off; }
const int* intAt(const UtConfig* cfg, size_t off) { return (const int*)((const char*)cfg + off); }
const char* strAt(const UtConfig* cfg, size_t off) { return (const char*)cfg + off; }

// ---- rendering ---------------------------------------------------------------------------
// The file the mod writes IS the table, so the text and the parser cannot disagree.

struct Out {
    char* p;
    size_t cap;
    size_t n;
    bool bad;

    void put(const char* s) {
        const size_t len = strlen(s);
        if (bad || !p || n + len + 1 > cap) {
            bad = true;
            return;
        }
        memcpy(p + n, s, len);
        n += len;
        p[n] = 0;
    }

    void line(const char* fmt, ...) {
        char tmp[512];
        va_list ap;
        va_start(ap, fmt);
        const int k = _vsnprintf_s(tmp, sizeof(tmp), _TRUNCATE, fmt, ap);
        va_end(ap);
        if (k < 0) {
            bad = true;
            return;
        }
        put(tmp);
        put("\r\n");
    }
};

// "name=value" padded out to the comment column, then "; comment". One line, always.
void renderKey(Out* o, const UtCfgKey& k, const UtConfig& src) {
    char head[352];
    if (k.type == kUtCfgStr) {
        _snprintf_s(head, sizeof(head), _TRUNCATE, "%s=%s", k.name, strAt(&src, k.off));
    } else {
        _snprintf_s(head, sizeof(head), _TRUNCATE, "%s=%d", k.name, *intAt(&src, k.off));
    }
    const size_t kComment = 26;  // the column every comment starts in
    char pad[40];
    size_t p = 0;
    const size_t hn = strlen(head);
    while (p + 1 < sizeof(pad) && hn + p < kComment) pad[p++] = ' ';
    if (p == 0) pad[p++] = ' ';
    pad[p] = 0;
    o->line("%s%s; %s", head, pad, k.comment);
}

}  // namespace

size_t configRender(char* out, size_t cap, const UtConfig& src) {
    Out o = {out, cap, 0, false};
    o.put("");
    o.line("; ============================================================================");
    o.line("; Unique Collection Tab - your settings.");
    o.line(";");
    o.line("; Edit this file while the game is running: it is re-read once a second. Lines");
    o.line("; that start with ';' are comments, and so is anything after a ';' on a line.");
    o.line("; The [blocks] are for reading only - a setting is found by its name.");
    o.line("; Delete this file to get the defaults back. When the mod updates it for a new");
    o.line("; version YOUR VALUES ARE KEPT: only the layout and any new setting change.");
    o.line("; ============================================================================");
    o.line("ini_version=%d", UT_INI_VERSION);
    for (int s = 0; s < kUtCfgSectionCount; ++s) {
        o.line("");
        o.line("[%s]", kUtCfgSections[s].name);
        // A blurb may be several lines; each one is written as its own comment.
        for (const char* b = kUtCfgSections[s].blurb; *b;) {
            const char* nl = strchr(b, '\n');
            const size_t len = nl ? (size_t)(nl - b) : strlen(b);
            char one[200];
            if (len >= sizeof(one)) return 0;
            memcpy(one, b, len);
            one[len] = 0;
            o.line("; %s", one);
            b = nl ? nl + 1 : b + len;
        }
        for (int i = 0; i < kUtCfgKeyCount; ++i) {
            if (strcmp(kUtCfgKeys[i].section, kUtCfgSections[s].name) != 0) continue;
            renderKey(&o, kUtCfgKeys[i], src);
        }
    }
    return o.bad ? 0 : o.n;
}

size_t configTemplateBytes() {
    static size_t cached = 0;
    if (!cached) {
        char buf[16384];
        const UtConfig defaults;
        cached = configRender(buf, sizeof(buf), defaults);
    }
    return cached;
}

namespace {

bool writeIni(const wchar_t* path, const UtConfig& src, bool overwrite) {
    char buf[16384];
    const size_t n = configRender(buf, sizeof(buf), src);
    if (!n) {
        logE("config: the settings file could not be rendered - it is not written");
        return false;
    }
    HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                           overwrite ? CREATE_ALWAYS : CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = WriteFile(h, buf, (DWORD)n, &written, nullptr);
    CloseHandle(h);
    return ok && written == (DWORD)n;
}

// ---- parsing -----------------------------------------------------------------------------

struct UtIniScan {
    bool seen[kMaxKeys];  // which table rows the file actually had
    int known;            // how many distinct rows that is
    int unknown;          // lines naming a key this build does not have
    char unknownNames[200];
    int iniVersionSeen;
};

void scanInit(UtIniScan* s) {
    memset(s, 0, sizeof(*s));
    s->iniVersionSeen = -1;
}

int findKey(const char* name) {
    for (int i = 0; i < kUtCfgKeyCount; ++i)
        if (!strcmp(kUtCfgKeys[i].name, name)) return i;
    return -1;
}

// Trailing blanks off a value the caller has already cut at the comment and trimmed on the left.
size_t trimmedLen(const char* value) {
    size_t n = strlen(value);
    while (n > 0 && (value[n - 1] == ' ' || value[n - 1] == '\t' || value[n - 1] == '\r' ||
                     value[n - 1] == '\n')) {
        --n;
    }
    return n;
}

// One key, validated. A number outside its range is CLAMPED and the log says so; a word that is
// not one of the choices is refused and the default kept. Neither is written back to the file -
// the file keeps whatever the user typed, so a silly value never sticks.
void applyOne(const UtCfgKey& k, const char* value, UtConfig* cfg) {
    if (k.type == kUtCfgStr) {
        size_t n = trimmedLen(value);
        char tmp[300];
        if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
        memcpy(tmp, value, n);
        tmp[n] = 0;
        if (k.choices && tmp[0]) {
            bool ok = false;
            for (const char* const* c = k.choices; *c && !ok; ++c)
                if (!_stricmp(tmp, *c)) {
                    _snprintf_s(tmp, sizeof(tmp), _TRUNCATE, "%s", *c);
                    ok = true;
                }
            if (!ok) {
                logW("config: %s=%s is not one of the words it accepts - using \"%s\"", k.name,
                     tmp, k.defStr);
                _snprintf_s(tmp, sizeof(tmp), _TRUNCATE, "%s", k.defStr);
            }
        }
        char* dst = strAt(cfg, k.off);
        _snprintf_s(dst, k.cap, _TRUNCATE, "%s", tmp);
        return;
    }
    const int raw = atoi(value);
    int v = raw;
    if (v < k.lo) v = k.lo;
    if (v > k.hi) v = k.hi;
    if (v != raw) {
        logW("config: %s=%d is outside %d..%d - using %d", k.name, raw, k.lo, k.hi, v);
    }
    *intAt(cfg, k.off) = v;
}

// Parses `buf` in place into `parsed`, recording what was there in `scan`. Values after a ';' or
// '#' on the same line are comments (the one exception: the '#' that starts a #rrggbb value of
// search_mark_color); a "[section]" line is skipped (a key is found by name).
// Returns the number of distinct known keys applied.
int parseInto(char* buf, UtConfig* parsed, UtIniScan* scan) {
    scanInit(scan);
    char* line = buf;
    while (line && *line) {
        char* next = strpbrk(line, "\r\n");
        if (next) {
            *next = 0;
            ++next;
            while (*next == '\r' || *next == '\n') ++next;
        }
        while (*line == ' ' || *line == '\t') ++line;
        if (*line && *line != ';' && *line != '#' && *line != '[') {
            char* eq = strchr(line, '=');
            if (eq) {
                *eq = 0;
                char* key = line;
                char* value = eq + 1;
                for (char* p = eq - 1; p >= key && (*p == ' ' || *p == '\t'); --p) *p = 0;
                while (*value == ' ' || *value == '\t') ++value;
                // a '#' that starts the value of search_mark_color is part of it (a colour,
                // #rrggbb); anywhere else, and after that first character, a ';' or '#' starts a
                // comment
                const bool colourHash = *value == '#' && !_stricmp(key, "search_mark_color");
                char* cut = strpbrk(colourHash ? value + 1 : value, ";#");
                if (cut) *cut = 0;
                if (!_stricmp(key, "ini_version")) {
                    parsed->iniVersion = atoi(value);
                    scan->iniVersionSeen = parsed->iniVersion;
                } else {
                    const int idx = findKey(key);
                    if (idx >= 0) {
                        applyOne(kUtCfgKeys[idx], value, parsed);
                        if (!scan->seen[idx]) {
                            scan->seen[idx] = true;
                            ++scan->known;
                        }
                    } else if (*key) {
                        ++scan->unknown;
                        const size_t at = strlen(scan->unknownNames);
                        if (at + strlen(key) + 3 < sizeof(scan->unknownNames)) {
                            _snprintf_s(scan->unknownNames + at, sizeof(scan->unknownNames) - at,
                                        _TRUNCATE, "%s%s", at ? ", " : "", key);
                        }
                    }
                }
            }
        }
        line = next;
    }
    return scan->known;
}

// ---- the master switch ----------------------------------------------------------------------
// enabled=0 is not a code path of its own: it is the configuration "everything off", which every
// one of these switches already supports on its own. journal=0 is what stops a deposit - under
// the private table a row the mod may not write is a deposit the mod refuses - so nothing new is
// collected, while the journal file, its exports and rescue=1 are untouched.
int g_enabledSaid = -1;

void applyMasterSwitch(UtConfig* c) {
    if (c->enabled != g_enabledSaid) {
        g_enabledSaid = c->enabled;
        if (!c->enabled) {
            logI("config: enabled=0 - no tab, no marks, nothing new is collected");
            logD("what is already stored is untouched, and rescue=1 still hands it back");
        }
    }
    if (c->enabled) return;
    c->journal = 0;
    c->livePages = 0;
    c->pageHotkeys = 0;
    c->groupButtons = 0;
    c->plateSwap = 0;
    c->plateLabel = 0;
    c->ownedMarks = 0;
    c->slotPlates = 0;
    c->gridCover = 0;
    c->searchButtons = 0;
    c->searchButtonsUnowned = 0;
    c->search = 0;
    c->comparePopup = 0;
    c->tooltipMark = 0;
}

// ---- reading the file ------------------------------------------------------------------------

const unsigned kMaxIniBytes = 1u << 20;  // our own few-KB file; anything bigger is not ours

// Reads all of `h` into a malloc'd NUL-terminated buffer (caller frees). Null on any failure.
char* readAllA(HANDLE h, DWORD* outLen) {
    *outLen = 0;
    LARGE_INTEGER li;
    li.QuadPart = 0;
    if (!GetFileSizeEx(h, &li)) return nullptr;
    if (li.QuadPart < 0 || (unsigned long long)li.QuadPart > kMaxIniBytes) return nullptr;
    const DWORD want = (DWORD)li.QuadPart;
    char* buf = (char*)malloc((size_t)want + 2);
    if (!buf) return nullptr;
    DWORD got = 0;
    if (want && !ReadFile(h, buf, want, &got, nullptr)) {
        free(buf);
        return nullptr;
    }
    if (got > want) got = want;
    buf[got] = 0;
    *outLen = got;
    return buf;
}

bool g_selfCheckDone = false;

// Logged exactly once per process, on the first file that was actually read. `migrated` says the
// file has just been rewritten in full, so "what is missing" is a statement about the OLD file
// and there is nothing left for the reader to do about it.
void configSelfCheck(DWORD fileBytes, const UtIniScan& scan, int iniVersion, bool migrated) {
    if (g_selfCheckDone) return;
    g_selfCheckDone = true;
    if (migrated) {
        logI("config: %d of %d settings kept from the old file, the rest defaulted "
             "(rewritten in full, %u bytes, ini_version=%d)",
             scan.known, kUtCfgKeyCount, (unsigned)configTemplateBytes(), iniVersion);
        return;
    }
    logI("config: %d of %d settings are in the file (%u bytes, a full one is %u; ini_version=%d)",
         scan.known, kUtCfgKeyCount, (unsigned)fileBytes, (unsigned)configTemplateBytes(),
         iniVersion);
    if (scan.known < kUtCfgKeyCount) {
        logI("config: %d setting(s) are not in the file and use their default - delete the file "
             "to get a complete one back",
             kUtCfgKeyCount - scan.known);
    }
}

// True when `line` (already trimmed at the left) assigns exactly `key`.
bool lineIsKey(const char* line, const char* key) {
    const size_t n = strlen(key);
    if (_strnicmp(line, key, n) != 0) return false;
    const char* p = line + n;
    while (*p == ' ' || *p == '\t') ++p;
    return *p == '=';
}

}  // namespace

bool configReload(const wchar_t* path) {
    if (path && path != g_path) wcsncpy_s(g_path, path, _TRUNCATE);

    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const UtConfig defaults;
        writeIni(path, defaults, false);
        g_haveStamp = false;
        return false;
    }

    // Skip the parse when nothing changed, so this can run every second for free.
    FILETIME ft = {0, 0};
    if (GetFileTime(h, nullptr, nullptr, &ft) && g_haveStamp &&
        ft.dwLowDateTime == g_lastWrite.dwLowDateTime &&
        ft.dwHighDateTime == g_lastWrite.dwHighDateTime) {
        CloseHandle(h);
        return true;
    }
    g_lastWrite = ft;
    g_haveStamp = true;

    DWORD got = 0;
    char* buf = readAllA(h, &got);
    CloseHandle(h);
    if (!buf) return false;

    UtConfig parsed;  // starts from the defaults, so a key the file does not name takes its own
    UtIniScan scan;
    parseInto(buf, &parsed, &scan);
    free(buf);
    if (!utColorParse(parsed.searchMarkColor, nullptr, nullptr, nullptr)) {
        logW("config: search_mark_color=%s is not a colour name (gold, green, white, red, orange, "
             "cyan, magenta, blue) or #rrggbb - using %s", parsed.searchMarkColor, kUtColorDefault);
        _snprintf_s(parsed.searchMarkColor, sizeof(parsed.searchMarkColor), _TRUNCATE, "%s",
                    kUtColorDefault);
    }

    // A VERSION BUMP MERGES. `parsed` already holds the player's value for every key that still
    // exists and the default for every key that is new, so the whole migration is: say what
    // happened, write the same values out in the current layout, and carry on. Nothing is reset.
    const bool migrated = parsed.iniVersion != UT_INI_VERSION;
    if (migrated) {
        const int from = parsed.iniVersion;
        parsed.iniVersion = UT_INI_VERSION;
        if (from < 5 || from > UT_INI_VERSION) {
            // the four pad keys changed their MEANING (the column beside the window, or
            // GD's plate) - a kept value would misplace the pad, so they take the new defaults.
            const UtConfig d;
            parsed.padX = d.padX;
            parsed.padY = d.padY;
            parsed.padH = d.padH;
            parsed.padGap = d.padGap;
            logI("config: pad_x, pad_y, pad_h and pad_gap now place the pad INSIDE the caravan "
                 "window (ini_version 5) - their old values were reset to %d, %d, %d, %d",
                 d.padX, d.padY, d.padH, d.padGap);
        }
        if (from > UT_INI_VERSION) {
            // a GRIM DAWN file's tooltip_class_yes / _no number GD's 84 text styles; TQ has
            // 58 others under the same numbers, so a kept value would be a different colour.
            const UtConfig d;
            parsed.tooltipClassYes = d.tooltipClassYes;
            parsed.tooltipClassNo = d.tooltipClassNo;
        }
        if (from < UT_HAVE_MARKS_OFF_SINCE && parsed.haveMarks == 1) {
            // have_marks' default went from 1 to 0 (the tooltip line replaces the tick). A
            // file from before that still reading the OLD default is flipped once; a 0 stays 0,
            // and a 1 set in a file of this version or later is never touched again.
            parsed.haveMarks = 0;
            logI("config: have_marks=1 was the old default and is now 0 (ini_version %d) - the "
                 "tooltip line says \"In your collection\" instead; set have_marks=1 to get the "
                 "corner tick back", UT_HAVE_MARKS_OFF_SINCE);
        }
        if (from < UT_SLOT_ART_SINCE && parsed.slotPlates == 1) {
            // slot_plates' default went from 1 (the flat plate) to 3 (the equipment
            // window's slot art). A file from before that still reading the OLD default moves
            // once; a 0 or a 2 stays, and a 1 set in a file of this version is never touched.
            parsed.slotPlates = 3;
            logI("config: slot_plates=1 was the old default and is now 3 (ini_version %d) - the "
                 "equipment window's slot art; set slot_plates=1 to get the flat plate back",
                 UT_SLOT_ART_SINCE);
        }
        if (from < UT_GRAY_ICONS_SINCE && parsed.ownedMarks == 1) {
            // owned_marks' default went from 1 (the veil) to 3 (the icon in gray). A file
            // from before that still reading the OLD default moves once; a 0 or a 2 stays, and a
            // 1 set in a file of this version is never touched.
            parsed.ownedMarks = 3;
            logI("config: owned_marks=1 was the old default and is now 3 (ini_version %d) - the "
                 "icons of the records you do not own are drawn in gray; set owned_marks=1 to get "
                 "the dark veil back", UT_GRAY_ICONS_SINCE);
        }
        if (from < UT_SEARCH_FRAME_SINCE && parsed.searchMark == 3) {
            // search_mark's default went from 3 (the frame and the wash) to 1 (a thin frame). A
            // file from before that still reading the OLD default moves once; a 1 or a 2 stays,
            // and a 3 set in a file of this version is never touched.
            parsed.searchMark = 1;
            logI("config: search_mark=3 was the old default and is now 1 (ini_version %d) - a thin "
                 "frame in search_mark_color; set search_mark=3 to get the wash back as well",
                 UT_SEARCH_FRAME_SINCE);
        }
        if (scan.unknown > 0) {
            logI("config: %d setting(s) this build no longer has were dropped: %s",
                 scan.unknown, scan.unknownNames);
        }
        const bool wrote = writeIni(path, parsed, true);
        g_haveStamp = false;
        logI("config: migrated from ini_version=%d to %d - %d setting(s) kept, %d new one(s) "
             "defaulted",
             from, UT_INI_VERSION, scan.known, kUtCfgKeyCount - scan.known);
        if (!wrote) {
            logW("config: \"%S\" could NOT be rewritten - the migrated values are in use for this "
                 "run only", path);
        }
    } else if (scan.unknown > 0) {
        // Same version, so the file is NOT rewritten - a misspelled key is the player's line to
        // fix, and a mod that silently swallowed it would leave them editing a setting that does
        // not exist. Said once per edit: the file is only re-parsed when it changes.
        logW("config: %d line(s) name a setting this build does not have and are ignored: %s",
             scan.unknown, scan.unknownNames);
    }

    configSelfCheck(got, scan, parsed.iniVersion, migrated);

    applyMasterSwitch(&parsed);
    const bool changed = memcmp(&parsed, &g_cfg, sizeof(UtConfig)) != 0;
    g_cfg = parsed;
    // The two settings the logger itself owns, mapped once per reload: the threshold the level
    // wrappers test on every line, and the per-line flush.
    logSetLevel(g_cfg.logLevel);
    logSetFlushEachLine(g_cfg.logFlushEachLine);
    if (changed) {
        logI("config: log_level=%s mp_collect=%d log_flush_each_line=%d", g_cfg.logLevel,
             g_cfg.mpCollect, g_cfg.logFlushEachLine);
    }
    return true;
}

namespace {

// The one line-rewriting engine the persist path uses: read the file, replace the assignment of
// every key in `keys` (appending the ones that were not there), write a temp file next to it and
// MoveFileEx it over the original, so a reader never sees half a file.
bool persistKeys(const char* const* keys, const int* values, int n) {
    if (!g_path[0] || n <= 0 || n > 8) return false;

    DWORD got = 0;
    char* in = nullptr;
    {
        HANDLE h = CreateFileW(g_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        in = readAllA(h, &got);
        CloseHandle(h);
        if (!in) return false;
    }

    bool seen[8] = {false, false, false, false, false, false, false, false};

    // Rebuild the file line by line: only the listed assignments change. The output buffer is
    // sized from the INPUT, and every line either survives byte for byte or becomes "key=value",
    // so input + n short lines is an upper bound with room to spare - and if anything ever did not
    // fit we write NOTHING.
    const size_t outCap = (size_t)got + (size_t)n * 256 + 1024;
    char* out = (char*)malloc(outCap);
    if (!out) {
        free(in);
        return false;
    }
    size_t o = 0;
    bool overflow = false;
    char* line = in;
    while (line && *line) {
        // Exactly one line ending is consumed, so blank separator lines survive the rewrite.
        char* next = strpbrk(line, "\r\n");
        if (next) {
            const bool crlf = (next[0] == '\r' && next[1] == '\n');
            *next = 0;
            next += crlf ? 2 : 1;
        }
        const char* trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') ++trimmed;
        int hit = -1;
        for (int i = 0; i < n; ++i)
            if (lineIsKey(trimmed, keys[i])) { hit = i; break; }
        int wrote;
        if (hit >= 0) {
            seen[hit] = true;
            // Keep the line's trailing comment, at its original column where it still fits: the
            // file documents itself and persisting a key must not delete its own explanation.
            const char* eq = strchr(trimmed, '=');
            const char* cmt = eq ? strpbrk(eq, ";#") : nullptr;
            char head[160];
            const int hn =
                _snprintf_s(head, sizeof(head), _TRUNCATE, "%s=%d", keys[hit], values[hit]);
            if (hn < 0) { overflow = true; break; }
            if (cmt) {
                const size_t col = (size_t)(cmt - trimmed);
                char pad[160];
                size_t p = 0;
                while (p + 1 < sizeof(pad) && (size_t)hn + p < col) pad[p++] = ' ';
                if (p == 0) pad[p++] = ' ';
                pad[p] = 0;
                wrote = _snprintf_s(out + o, outCap - o, _TRUNCATE, "%s%s%s\r\n", head, pad, cmt);
            } else {
                wrote = _snprintf_s(out + o, outCap - o, _TRUNCATE, "%s\r\n", head);
            }
        } else {
            wrote = _snprintf_s(out + o, outCap - o, _TRUNCATE, "%s\r\n", line);
        }
        if (wrote < 0) { overflow = true; break; }
        o += (size_t)wrote;
        line = next;
    }
    for (int i = 0; i < n && !overflow; ++i) {
        if (seen[i]) continue;
        const int wrote =
            _snprintf_s(out + o, outCap - o, _TRUNCATE, "%s=%d\r\n", keys[i], values[i]);
        if (wrote < 0) { overflow = true; break; }
        o += (size_t)wrote;
    }
    free(in);
    // A rewrite may differ from the input only by the digits of the values it changed. If it ever
    // came out materially shorter, something went wrong - write NOTHING rather than replace the
    // file with a truncated one.
    if (overflow || o + 512 < (size_t)got) {
        logW("config: persist REFUSED - the rewrite would shorten \"%S\" (%u -> %u bytes)", g_path,
             (unsigned)got, (unsigned)o);
        free(out);
        return false;
    }

    wchar_t tmpPath[MAX_PATH];
    _snwprintf_s(tmpPath, _TRUNCATE, L"%s.tmp", g_path);
    HANDLE h = CreateFileW(tmpPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        free(out);
        return false;
    }
    DWORD written = 0;
    const BOOL wrote = WriteFile(h, out, (DWORD)o, &written, nullptr);
    CloseHandle(h);
    free(out);
    if (!wrote || written != (DWORD)o) {
        DeleteFileW(tmpPath);
        return false;
    }
    // The worker thread re-reads this file once a second; if the replace lands in that window, one
    // retry is enough (the reader closes the handle immediately).
    if (!MoveFileExW(tmpPath, g_path, MOVEFILE_REPLACE_EXISTING)) {
        Sleep(30);
        if (!MoveFileExW(tmpPath, g_path, MOVEFILE_REPLACE_EXISTING)) {
            DeleteFileW(tmpPath);
            return false;
        }
    }
    g_haveStamp = false;  // force the next reload to parse what we just wrote
    return true;
}

}  // namespace

bool configPersistInt(const char* key, int value) {
    if (!key || !*key) return false;
    const char* keys[1] = {key};
    const int values[1] = {value};
    return persistKeys(keys, values, 1);
}

}  // namespace ut
