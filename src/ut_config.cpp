#include "ut_config.h"
#include "ut_color.h"   // search_mark_color

#include <windows.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>

#include "ut_folders.h"   // a mod's own folder
#include "ut_layout.h"    // the [layout] rectangles: their text
#include "ut_log.h"

namespace ut {

UtConfig g_cfg;

// The file's search value before enabled=0 collapses it (applyMasterSwitch): the search key gate
// is installed at start from this, so enabled=1 later brings the field back without a restart.
int g_searchInFile = 1;

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
#define UT_BOOL(sec, name, member, def, fl, cmt) \
    {sec, name, kUtCfgBool, def, 0, 1, "", UT_OFF(member), 0, nullptr, cmt, fl}
// A number in an inclusive range.
#define UT_INT(sec, name, member, def, lo, hi, fl, cmt) \
    {sec, name, kUtCfgInt, def, lo, hi, "", UT_OFF(member), 0, nullptr, cmt, fl}
// Text. `choices` is null for free text, or a null-terminated list of the only legal words.
#define UT_STR(sec, name, member, def, choices, fl, cmt) \
    {sec, name, kUtCfgStr, 0, 0, 0, def, UT_OFF(member), UT_CAP(member), choices, cmt, fl}

#define UT_H kUtCfgHidden

const UtCfgKey kUtCfgKeys[] = {
    // ---- [general] - the eight keys the file shows --------------------------------------------
    UT_BOOL("general", "enabled", enabled, 1, 0,
            "0 = the mod goes quiet: no tab, nothing new collected. Nothing is lost"),
    UT_BOOL("general", "collect_mi", collectMi, 1, 0,
            "1 = monster infrequents are collected, one roll per item; 0 = they are not, and the "
            "ones you stored stay in the collection, hidden until it is 1 again (from the next start)"),
    UT_BOOL("general", "collect_relics", collectRelics, 1, 0,
            "1 = relics and charms have their own page, one shard at a time; 0 = no relic page: "
            "they are not collected, and the ones you stored stay in the collection, hidden until "
            "it is 1 again (from the next start)"),
    UT_BOOL("general", "collect_unobtainable", collectUnobtainable, 0, 0,
            "1 = also collect the records no monster, chest, merchant or quest gives (gear worn by "
            "statues and quest people, a boss weapon he never drops, set pieces no loot table "
            "names); 0 = they are left out, and the ones you stored stay in the collection, hidden "
            "until it is 1 again (from the next start)"),
    UT_BOOL("general", "tooltip_mark", tooltipMark, 1, 0,
            "1 = every item tooltip says whether it is in your collection"),
    UT_STR("general", "log_level", logLevel, "info", kLogLevels, 0,
           "how much goes in the log: error, warn, info, debug, trace"),
    UT_INT("general", "view_hotkey", viewHotkey, 117, 0, 254, 0,
           "the key that switches the Transfer page to the collection and back (a Windows "
           "virtual-key code: 117 = F6; 0 = no key, the button only)"),
    UT_BOOL("general", "search", search, 1, 0,
            "1 = the collection page can be searched by item name and properties (\"fire\" finds "
            "Fire Damage and Fire Resistance); 0 = no search at all (if the game started with 0, "
            "a 1 works from the next start)"),

    // ---- [advanced] - the commented blob, one row each ---------------------------------------
    UT_INT("advanced", "ui_scale_pct", uiScalePct, 0, 0, 300, UT_H,
           "0 = follow the game's UI scale, else force one in percent (50..300)"),
    UT_BOOL("advanced", "mp_collect", mpCollect, 1, UT_H,
            "1 = deposits and takes work in a multiplayer game too (hosted or joined), for you only"),
    UT_BOOL("advanced", "collect_pristine_only", collectPristineOnly, 0, UT_H,
            "1 = refuse items with affixes, relics or charms"),
    UT_BOOL("advanced", "group_buttons", groupButtons, 1, UT_H,
            "1 = draw the pad of group buttons under the grid and let it be clicked"),
    UT_BOOL("advanced", "page_hotkeys", pageHotkeys, 1, UT_H,
            "1 = the wheel scrolls rows, PageUp/PageDown turn pages, Ctrl changes group"),
    UT_BOOL("advanced", "plate_label", plateLabel, 1, UT_H,
            "1 = draw the group name and the slot rows shown over the buttons"),
    UT_INT("advanced", "owned_marks", ownedMarks, 3, 0, 3, UT_H,
           "a record you do not own: 0 no mark, 1 a dark veil, 2 a thin frame, 3 its icon in gray"),
    UT_BOOL("advanced", "have_marks", haveMarks, 0, UT_H,
            "1 = a corner tick on an uncollected record you carry somewhere"),
    UT_INT("advanced", "slot_plates", slotPlates, 3, 0, 3, UT_H,
           "the ground under a slot: 0 none, 1 a dark plate, 2 thin frames, 3 the slot art"),
    UT_BOOL("advanced", "grid_cover", gridCover, 1, UT_H,
            "1 = cover the grid cells no slot uses in the caravan's ground colour"),
    UT_BOOL("advanced", "search_buttons", searchButtons, 1, UT_H,
            "1 = mark the group buttons that hold a match for your search"),
    UT_INT("advanced", "search_mark", searchMark, 1, 1, 3, UT_H,
           "a found item: 1 a thin frame round its slot, 2 a wash under it, 3 both"),
    UT_STR("advanced", "search_mark_color", searchMarkColor, "gold", nullptr, UT_H,
           "that colour: gold, green, white, red, orange, cyan, magenta, blue or #rrggbb"),
    UT_BOOL("advanced", "search_transfer", searchTransfer, 1, UT_H,
            "1 = the search field on the real Transfer page too; 0 = the collection page only"),
    UT_BOOL("advanced", "search_lore", searchLore, 0, UT_H,
            "1 = the search also looks in an item's story text"),
    UT_BOOL("advanced", "search_prebuild", searchPrebuild, 0, UT_H,
            "1 = prepare the search's text when the collection is first shown"),
    UT_STR("advanced", "search_debug_query", searchDebugQuery, "", nullptr, UT_H,
           "a search applied to the collection page, read again every second; empty = none"),
    UT_STR("advanced", "journal_dir", journalDir, "", nullptr, UT_H,
           "where the collection file is kept (read at start-up). Empty = the mod's own folder"),
    UT_INT("advanced", "export_csv", exportCsv, 0, 0, 2, UT_H,
           "write tq-uniq-items*.csv for a spreadsheet: 0 off, 1 the collection, 2 all"),
    UT_BOOL("advanced", "layout_outline", layoutOutline, 0, UT_H,
            "1 = outline the grid, the buttons and the label with their [layout] numbers"),
    UT_BOOL("advanced", "layout_edit", layoutEdit, 0, UT_H,
            "1 = open the in-game layout editor where the buttons are shown (sets itself to 0)"),
    UT_BOOL("advanced", "edit_button", editButton, 1, UT_H,
            "1 = an EDIT button among the buttons opens the layout editor; 0 = no button (Ctrl+right-click still opens it)"),
    UT_INT("advanced", "relic_block_gap", relicBlockGap, 1, 0, 2, UT_H,
           "the relic pages: empty lines between two blocks (0..2)"),
    UT_INT("advanced", "relic_bonus_gap", relicBonusGap, 1, 0, 1, UT_H,
           "the relic pages: empty columns between the shard slot and the first bonus (0..1)"),
    UT_BOOL("advanced", "relic_panels", relicPanels, 1, UT_H,
            "the relic pages: 1 = every block stands on its own framed panel; 0 = no panels"),
    // A screen key of [layout] (pad_screen, label_screen): text checked by utScreenRectParse, written
    // only while set.
    UT_STR("layout", "pad_screen", padScreen, "", nullptr, kUtCfgWhenSet,
           "the buttons pinned to a screen corner: tl|tr|bl|br,x,y,w,h in page pixels"),
    UT_STR("layout", "label_screen", labelScreen, "", nullptr, kUtCfgWhenSet,
           "the label pinned to a screen corner: tl|tr|bl|br,x,y,w,h in page pixels"),
    UT_INT("advanced", "plate_label_size", plateLabelSize, 13, 8, 32, UT_H,
           "height of the label text, in page pixels (8..32)"),
    UT_INT("advanced", "pad_caption_max", padCaptionMax, 14, 6, 36, UT_H,
           "largest caption size on the buttons; the button size limits it too (6..36)"),
    UT_STR("advanced", "font", fontName, "fonts/albertus mt light.fnt", nullptr, UT_H,
           "the caption font, by its name in Resources\\Fonts.arc"),
    UT_INT("advanced", "caravan_x", caravanX, 10, -2000, 4000, UT_H,
           "the caravan window's left edge (its record: 10)"),
    UT_INT("advanced", "caravan_y", caravanY, 0, -2000, 2000, UT_H,
           "the caravan window's offset from the centred position (its record: 0)"),
    UT_INT("advanced", "caravan_w", caravanW, 565, 100, 4000, UT_H,
           "the caravan window's width (565)"),
    UT_INT("advanced", "caravan_h", caravanH, 637, 100, 4000, UT_H,
           "the caravan window's height (637)"),
    UT_INT("advanced", "page_y", pageY, 126, 0, 2000, UT_H,
           "the Transfer page's top inside the caravan window (TransferWindow.dbr: 126)"),
    UT_BOOL("advanced", "log_flush_each_line", logFlushEachLine, 0, UT_H,
            "1 = write every log line at once (slow; for chasing a crash)"),
    UT_STR("advanced", "text_language", textLanguage, "EN", nullptr, UT_H,
           "the suffix of Text\\Text_<lang>.arc the catalogue takes item names from"),
};

const int kUtCfgKeyCount = (int)(sizeof(kUtCfgKeys) / sizeof(kUtCfgKeys[0]));

const UtCfgSection kUtCfgSections[] = {
    {"general", ""},
    {"layout",
     "Where the buttons and the label sit. The in-game editor writes these (the EDIT button, or\n"
     "Ctrl+right-click the buttons); by hand: x,y,w,h in page pixels from the caravan window's\n"
     "top-left, per window size (565x637 is the game's own). Delete a line to get the default."},
    {"advanced",
     "Every other setting, commented out at its default. Remove the ';' in front of one to\n"
     "change it; the line keeps its place. A value outside its range is clamped."},
};

const int kUtCfgSectionCount = (int)(sizeof(kUtCfgSections) / sizeof(kUtCfgSections[0]));

// ---- THE FAMILIES -----------------------------------------------------------------------------
// The [layout] rectangles: the suffix is a caravan window size, the value four whole numbers. Both
// checks are format only; whether a rectangle fits the page is decided where it is used, against
// the live page (utLayoutResolve), because this file is re-read once a second.

namespace {
bool coverSuffixOk(const char* suffix) { return utCoverKeyParse(suffix, nullptr, nullptr); }
const char* rectValueOk(const char* value) {
    UtRect r;
    return utRectParse(value, &r) ? nullptr : "not x,y,w,h (four whole numbers, w and h from 1)";
}
}  // namespace

const UtCfgFamily kUtCfgFamilies[] = {
    {"layout", "grid_", UT_OFF(layGrid), coverSuffixOk, rectValueOk, "565x637=27,126,512,480",
     "the item grid: whole cells from the top-left, the rest stays empty"},
    {"layout", "pad_", UT_OFF(layPad), coverSuffixOk, rectValueOk, "565x637=33,606,500,31",
     "the buttons: the group cells are fitted to it, paged when they do not all fit"},
    {"layout", "label_", UT_OFF(layLabel), coverSuffixOk, rectValueOk, "565x637=270,622,260,12",
     "the label line: its height limits plate_label_size"},
    {"layout", "relics_", UT_OFF(layRelics), coverSuffixOk, rectValueOk, "565x637=27,126,512,480",
     "the relic and charm pages' area: whole cells from the top-left; no line = the item grid"},
};

const int kUtCfgFamilyCount = (int)(sizeof(kUtCfgFamilies) / sizeof(kUtCfgFamilies[0]));

const UtCfgFamily* configFamilyOf(const char* key, const char** suffix) {
    if (!key) return nullptr;
    for (int i = 0; i < kUtCfgFamilyCount; ++i) {
        const UtCfgFamily& f = kUtCfgFamilies[i];
        const size_t n = strlen(f.prefix);
        if (_strnicmp(key, f.prefix, n) != 0) continue;
        if (!f.suffixOk || !f.suffixOk(key + n)) continue;
        if (suffix) *suffix = key + n;
        return &f;
    }
    return nullptr;
}

const char* configFamilyGet(const UtCfgFamilyStore& s, const char* suffix) {
    if (!suffix) return nullptr;
    for (int i = 0; i < s.count && i < kUtCfgFamilySlots; ++i)
        if (!strcmp(s.e[i].suffix, suffix)) return s.e[i].value;
    return nullptr;
}

bool configFamilySet(UtCfgFamilyStore* s, const char* suffix, const char* value) {
    if (!s || !suffix || !value) return false;
    if (strlen(suffix) >= (size_t)kUtCfgFamilySuffixCap) return false;
    if (strlen(value) >= (size_t)kUtCfgFamilyValueCap) return false;
    int at = -1;
    for (int i = 0; i < s->count && i < kUtCfgFamilySlots; ++i)
        if (!strcmp(s->e[i].suffix, suffix)) at = i;
    if (at < 0) {
        if (s->count >= kUtCfgFamilySlots) return false;
        at = s->count++;
    }
    UtCfgFamilyEntry& e = s->e[at];
    memset(&e, 0, sizeof(e));
    memcpy(e.suffix, suffix, strlen(suffix));
    memcpy(e.value, value, strlen(value));
    return true;
}

const char* configFamilyValue(const UtConfig& c, const char* key) {
    const char* suffix = nullptr;
    const UtCfgFamily* f = configFamilyOf(key, &suffix);
    if (!f) return nullptr;
    return configFamilyGet(*(const UtCfgFamilyStore*)((const char*)&c + f->off), suffix);
}

// journal_dir is read at start-up, before any custom game is known, and it is where the games'
// journals are: only the main file sets it. Every other key, search included, is a game's own
// (search's key gate is decided once at start, configSearchGateAtStart).
bool configMainOnlyKey(const char* key) { return key && !_stricmp(key, "journal_dir"); }

bool configIsStateKey(const char* key) {
    return key && (!_stricmp(key, "uniq_group") || !_stricmp(key, "uniq_row") ||
                   !_stricmp(key, "owned_only"));
}

namespace {
std::wstring widen(const char* s) {
    std::wstring w;
    for (const char* p = s; p && *p; ++p) w += (wchar_t)(unsigned char)*p;
    return w;
}
}  // namespace

void configGameFiles(const wchar_t* mainIni, const char* game, std::wstring* ini,
                     std::wstring* state) {
    if (ini) ini->clear();
    if (state) state->clear();
    if (!mainIni || !mainIni[0]) return;
    std::wstring dir(mainIni);
    const size_t slash = dir.find_last_of(L"\\/");
    dir = slash == std::wstring::npos ? std::wstring(L".") : dir.substr(0, slash);
    // mods\<leaf>\ for a mod (ut_folders.h); the root for the main game
    const std::string rel = utCustomGameRelLeaf(game ? game : "");
    if (rel.empty()) {
        if (state) *state = dir + L"\\uniquetab.state";
        return;
    }
    if (ini) *ini = dir + L"\\" + widen(rel.c_str()) + L"\\uniquetab.ini";
    if (state) *state = dir + L"\\" + widen(rel.c_str()) + L"\\uniquetab.state";
}

namespace {

// The scan's `seen` array is indexed by table row.
const int kMaxKeys = 64;
static_assert(sizeof(kUtCfgKeys) / sizeof(kUtCfgKeys[0]) <= (size_t)kMaxKeys,
              "kMaxKeys must cover the whole key table");

// THE SETTINGS STATE. Everything here is under g_applyLock (the worker's reload, the game
// thread's game switch and the persists never interleave); g_cfg is written under it as well and
// read without it, as before.
FILETIME g_lastWrite = {0, 0};
bool g_haveStamp = false;
wchar_t g_path[MAX_PATH] = {0};
UtConfig g_base;                 // the main file as read: no game file, no state, no master switch
bool g_haveBase = false;
char g_game[80] = {0};           // the game g_cfg is derived for: "" = the main game, else a mod's leaf
bool g_gameFilesReady = false;   // the two paths below are computed for g_path and g_game
// The active mod's own file (empty path = the main game): its stamp and the WHOLE configuration
// it describes (a key it does not name is the compiled default, never the main file's value).
// g_ovHave = it was read, and it is what g_cfg is derived from.
wchar_t g_ovPath[MAX_PATH] = {0};
FILETIME g_ovStamp = {0, 0};
bool g_ovHaveStamp = false;
bool g_ovHave = false;
UtConfig g_ovVals;
// The active game's state file and its three values.
wchar_t g_statePath[MAX_PATH] = {0};
int g_stGroup = -1, g_stRow = 0, g_stOwned = 0;
SRWLOCK g_applyLock = SRWLOCK_INIT;
volatile LONG g_cfgGen = 0;
volatile LONG g_gameGen = 0;

int* intAt(UtConfig* cfg, size_t off) { return (int*)((char*)cfg + off); }
char* strAt(UtConfig* cfg, size_t off) { return (char*)cfg + off; }
const int* intAt(const UtConfig* cfg, size_t off) { return (const int*)((const char*)cfg + off); }
const char* strAt(const UtConfig* cfg, size_t off) { return (const char*)cfg + off; }

bool keyHidden(const UtCfgKey& k) { return (k.flags & kUtCfgHidden) != 0; }
bool keyWhenSet(const UtCfgKey& k) { return (k.flags & kUtCfgWhenSet) != 0; }

// The visible keys a file is expected to show: not the hidden rows.
int visibleKeyCount() {
    int n = 0;
    for (int i = 0; i < kUtCfgKeyCount; ++i)
        if (!keyHidden(kUtCfgKeys[i]) && !keyWhenSet(kUtCfgKeys[i])) ++n;
    return n;
}

// A row's value as the file writes it.
void valueText(const UtCfgKey& k, const UtConfig& src, char* out, size_t cap) {
    if (k.type == kUtCfgStr)
        _snprintf_s(out, cap, _TRUNCATE, "%s", strAt(&src, k.off));
    else
        _snprintf_s(out, cap, _TRUNCATE, "%d", *intAt(&src, k.off));
}
void defaultText(const UtCfgKey& k, char* out, size_t cap) {
    if (k.type == kUtCfgStr)
        _snprintf_s(out, cap, _TRUNCATE, "%s", k.defStr);
    else
        _snprintf_s(out, cap, _TRUNCATE, "%d", k.def);
}

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

// "name=value" padded with blanks out to the comment column, then "; comment". One line.
void renderKey(Out* o, const UtCfgKey& k, const UtConfig& src) {
    char head[352], val[300];
    valueText(k, src, val, sizeof(val));
    _snprintf_s(head, sizeof(head), _TRUNCATE, "%s=%s", k.name, val);
    const size_t kComment = 26;  // the column every comment starts in
    char pad[40];
    size_t p = 0;
    const size_t hn = strlen(head);
    while (p + 1 < sizeof(pad) && hn + p < kComment) pad[p++] = ' ';
    if (p == 0) pad[p++] = ' ';
    pad[p] = 0;
    o->line("%s%s; %s", head, pad, k.comment);
}

// THE BLOB. Its rows are aligned with TABS (stops every 8 columns) on one column: the first stop
// past the longest commented default row, so the template never moves; a longer live row gets one
// tab.
size_t tabbedTo(char* pad, size_t cap, size_t from, size_t col) {
    size_t n = 0, at = from;
    do {
        if (n + 1 >= cap) break;
        pad[n++] = '\t';
        at = (at / 8 + 1) * 8;
    } while (at < col);
    pad[n] = 0;
    return n;
}

size_t blobColumn() {
    size_t m = 0;
    for (int i = 0; i < kUtCfgFamilyCount; ++i) {   // a family's commented example row
        const UtCfgFamily& f = kUtCfgFamilies[i];
        const size_t len = 1 + strlen(f.prefix) + strlen(f.example);
        if (len > m) m = len;
    }
    for (int i = 0; i < kUtCfgKeyCount; ++i) {
        const UtCfgKey& k = kUtCfgKeys[i];
        if (!keyHidden(k)) continue;
        char d[300];
        defaultText(k, d, sizeof(d));
        const size_t len = 1 + strlen(k.name) + 1 + strlen(d);
        if (len > m) m = len;
    }
    return (m / 8 + 1) * 8;
}

// The column `upto` bytes of `s` reach on screen, tabs stopping every 8.
size_t visualCol(const char* s, size_t upto) {
    size_t col = 0;
    for (size_t i = 0; i < upto && s[i]; ++i) col = s[i] == '\t' ? (col / 8 + 1) * 8 : col + 1;
    return col;
}

void blobRow(Out* o, const char* head, const char* comment, size_t col) {
    char pad[16];
    tabbedTo(pad, sizeof(pad), strlen(head), col);
    o->line("%s%s; %s", head, pad, comment);
}

// The family lines of `section`, one window size at a time: the suffixes in the order they first
// appear, and for each one its pad_ and label_ line - so the lines of one size stay together.
void renderLayoutLines(Out* o, const UtConfig& src, const char* section) {
    const char* order[kUtCfgFamilySlots * 4];
    int n = 0;
    for (int i = 0; i < kUtCfgFamilyCount; ++i) {
        const UtCfgFamily& f = kUtCfgFamilies[i];
        if (strcmp(f.section, section) != 0) continue;
        const UtCfgFamilyStore& st = *(const UtCfgFamilyStore*)((const char*)&src + f.off);
        for (int j = 0; j < st.count && j < kUtCfgFamilySlots; ++j) {
            bool have = false;
            for (int q = 0; q < n && !have; ++q) have = !strcmp(order[q], st.e[j].suffix);
            if (!have && n < (int)(sizeof(order) / sizeof(order[0]))) order[n++] = st.e[j].suffix;
        }
    }
    for (int q = 0; q < n; ++q)
        for (int i = 0; i < kUtCfgFamilyCount; ++i) {
            const UtCfgFamily& f = kUtCfgFamilies[i];
            if (strcmp(f.section, section) != 0) continue;
            const char* v =
                configFamilyGet(*(const UtCfgFamilyStore*)((const char*)&src + f.off), order[q]);
            if (v) o->line("%s%s=%s", f.prefix, order[q], v);
        }
}

// A section's comment lines; a blurb may be several lines, each written as its own comment.
bool renderBlurb(Out* o, const char* blurb) {
    for (const char* b = blurb; b && *b;) {
        const char* nl = strchr(b, '\n');
        const size_t len = nl ? (size_t)(nl - b) : strlen(b);
        char one[200];
        if (len >= sizeof(one)) return false;
        memcpy(one, b, len);
        one[len] = 0;
        o->line("; %s", one);
        b = nl ? nl + 1 : b + len;
    }
    return true;
}

}  // namespace

namespace {

// journal_dir's line in a mod's own file.
const char* const kMainOnlyNote =
    "read at start-up from the main uniquetab.ini only - set it there";

// ONE RENDERER for both files: the main uniquetab.ini (`game` null) and a mod's own
// mods\<leaf>\uniquetab.ini. They differ in the header and in journal_dir, which a mod's file
// shows as a commented line naming its one home; every other line is the same template.
size_t renderSettings(char* out, size_t cap, const UtConfig& src, const char* game) {
    Out o = {out, cap, 0, false};
    o.put("");
    o.line("; ============================================================================");
    if (game) {
        o.line("; Unique Collection Tab - your settings for the custom game \"%s\".", game);
        o.line("; While this game is played this file is the only one read: nothing comes from");
        o.line("; the main uniquetab.ini, except journal_dir. It was copied from the main file's");
        o.line("; values once, when it was made. Delete it to copy them again.");
        o.line("; Edit it while the game runs: it is re-read once a second. Anything after a");
        o.line("; ';' is a comment.");
    } else {
        o.line("; Unique Collection Tab - your settings.");
        o.line("; Edit it while the game runs: it is re-read once a second. Anything after a");
        o.line("; ';' is a comment. Delete this file to get the defaults back.");
    }
    o.line("; More settings are at the end, commented out: README.md, section \"Settings\".");
    o.line("; ============================================================================");
    o.line("ini_version=%d", UT_INI_VERSION);
    const size_t col = blobColumn();
    for (int s = 0; s < kUtCfgSectionCount; ++s) {
        const char* name = kUtCfgSections[s].name;
        o.line("");
        o.line("[%s]", name);
        if (!renderBlurb(&o, kUtCfgSections[s].blurb)) return 0;
        for (int i = 0; i < kUtCfgKeyCount; ++i) {
            const UtCfgKey& k = kUtCfgKeys[i];
            if (strcmp(k.section, name) != 0) continue;
            if (game && configMainOnlyKey(k.name)) {
                // never a value here: the line says where it is set (parseInto refuses one)
                char head[64];
                _snprintf_s(head, sizeof(head), _TRUNCATE, ";%s=", k.name);
                blobRow(&o, head, kMainOnlyNote, col);
                continue;
            }
            if (!keyHidden(k)) {
                if (keyWhenSet(k)) {   // a screen key: its line only while set
                    char val[300], def[300];
                    valueText(k, src, val, sizeof(val));
                    defaultText(k, def, sizeof(def));
                    if (!strcmp(val, def)) continue;
                }
                renderKey(&o, k, src);
                continue;
            }
            // A hidden row: commented at its default, LIVE (the ';' gone) when the value differs.
            char val[300], def[300], head[352];
            valueText(k, src, val, sizeof(val));
            defaultText(k, def, sizeof(def));
            _snprintf_s(head, sizeof(head), _TRUNCATE, "%s%s=%s", strcmp(val, def) ? "" : ";",
                        k.name, val);
            blobRow(&o, head, k.comment, col);
        }
        renderLayoutLines(&o, src, name);
        // A family with no line at all: its commented example row (GD's), at the derived numbers
        // of the game's own window, so "delete a line to get the default" shows what that is.
        for (int i = 0; i < kUtCfgFamilyCount; ++i) {
            const UtCfgFamily& f = kUtCfgFamilies[i];
            if (strcmp(f.section, name) != 0) continue;
            const UtCfgFamilyStore& st = *(const UtCfgFamilyStore*)((const char*)&src + f.off);
            if (st.count != 0) continue;
            char head[160];
            _snprintf_s(head, sizeof(head), _TRUNCATE, ";%s%s", f.prefix, f.example);
            blobRow(&o, head, f.comment, col);
        }
    }
    return o.bad ? 0 : o.n;
}

}  // namespace

size_t configRender(char* out, size_t cap, const UtConfig& src) {
    return renderSettings(out, cap, src, nullptr);
}

size_t configRenderGame(char* out, size_t cap, const char* game, const UtConfig& src) {
    return renderSettings(out, cap, src, game ? game : "");
}

size_t configRenderState(char* out, size_t cap, const UtConfig& src) {
    Out o = {out, cap, 0, false};
    o.put("");
    o.line("; uniquetab.state - written by the mod: where the collection was left in this game.");
    o.line("; Not a setting: delete it to start on the first group.");
    o.line("uniq_group=%d", src.uniqGroup);
    o.line("uniq_row=%d", src.uniqRow);
    o.line("owned_only=%d", src.ownedOnly);
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

bool fileExists(const wchar_t* path) {
    const DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// Every missing folder above `path` (a mod's mods\<leaf>\).
void makeParents(const wchar_t* path) {
    wchar_t dir[MAX_PATH];
    lstrcpynW(dir, path, MAX_PATH);
    for (wchar_t* p = dir; *p; ++p) {
        if ((*p == L'\\' || *p == L'/') && p != dir && p[-1] != L':') {
            const wchar_t saved = *p;
            *p = 0;
            CreateDirectoryW(dir, nullptr);
            *p = saved;
        }
    }
}

// `buf` into `path` whole: a temp file beside it, moved over the original (a reader never sees
// half a file). One retry: the worker's once-a-second read may hold the file for a moment.
bool writeAtomic(const wchar_t* path, const char* buf, size_t n) {
    wchar_t tmp[MAX_PATH];
    _snwprintf_s(tmp, _TRUNCATE, L"%s.tmp", path);
    HANDLE h = CreateFileW(tmp, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = WriteFile(h, buf, (DWORD)n, &written, nullptr);
    CloseHandle(h);
    if (!ok || written != (DWORD)n) {
        DeleteFileW(tmp);
        return false;
    }
    if (!MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING)) {
        Sleep(30);
        if (!MoveFileExW(tmp, path, MOVEFILE_REPLACE_EXISTING)) {
            DeleteFileW(tmp);
            return false;
        }
    }
    return true;
}

char* readAllA(HANDLE h, DWORD* outLen);

// A whole small file (a game's ini, a state file), NUL-terminated (caller frees); null when it
// is not there or cannot be read. `ft` gets its last-write time.
char* readFileW(const wchar_t* path, DWORD* len, FILETIME* ft) {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return nullptr;
    if (ft) GetFileTime(h, nullptr, nullptr, ft);
    char* buf = readAllA(h, len);
    CloseHandle(h);
    return buf;
}

// A mod's own file: created (never over an existing one) or rewritten whole.
bool writeGameFile(const wchar_t* path, const char* key, const UtConfig& src, bool overwrite) {
    char buf[16384];
    const size_t n = configRenderGame(buf, sizeof(buf), key, src);
    if (!n) return false;
    makeParents(path);
    if (overwrite) return writeAtomic(path, buf, n);
    HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = WriteFile(h, buf, (DWORD)n, &written, nullptr);
    CloseHandle(h);
    return ok && written == (DWORD)n;
}

bool writeStateFile(const wchar_t* path, int group, int row, int owned) {
    UtConfig s;
    s.uniqGroup = group;
    s.uniqRow = row;
    s.ownedOnly = owned;
    char buf[512];
    const size_t n = configRenderState(buf, sizeof(buf), s);
    if (!n) return false;
    makeParents(path);
    return writeAtomic(path, buf, n);
}

// search_mark_color must name a colour; anything else is the default, said once per read.
void checkColour(UtConfig* c) {
    if (utColorParse(c->searchMarkColor, nullptr, nullptr, nullptr)) return;
    logW("config: search_mark_color=%s is not a colour name (gold, green, white, red, orange, "
         "cyan, magenta, blue) or #rrggbb - using %s", c->searchMarkColor, kUtColorDefault);
    _snprintf_s(c->searchMarkColor, sizeof(c->searchMarkColor), _TRUNCATE, "%s", kUtColorDefault);
}

// ---- parsing -----------------------------------------------------------------------------

struct UtIniScan {
    bool seen[kMaxKeys];  // which table rows the file actually had
    char where[kMaxKeys][24];  // the [block] each of them stood under ("" = none)
    int known;            // how many distinct VISIBLE rows that is (a hidden one is missing by design)
    int knownAll;         // how many distinct rows of either kind
    int unknown;          // lines naming a key this build does not have
    char unknownNames[200];
    int iniVersionSeen;
    int stateSeen;        // bit 0 uniq_group, 1 uniq_row, 2 owned_only
    int stateVals[3];
    int notHere;          // a game's own file: journal_dir, which only the main file sets
    bool movedSeen[4];    // the rollover keys that moved to the buttons file (kMovedKeys)
    char moved[4][96];
    int family;           // keyed lines kept ([layout])
    int refused;          // keyed lines whose value was refused, or that did not fit
    bool retiredSeen[4];  // the pad keys retired at ini_version 22 (kRetiredKeys)
    char retired[4][24];
};

// THE KEYS THAT WERE RETIRED (ini_version 22): the pad's place and size are its rectangle now.
const char* const kRetiredKeys[4] = {"pad_x", "pad_y", "pad_h", "pad_gap"};
int retiredIndex(const char* key) {
    for (int i = 0; i < 4; ++i)
        if (!_stricmp(key, kRetiredKeys[i])) return i;
    return -1;
}
// "pad_y=8, pad_h=14" - the retired lines the file had, with their values; 0 = none.
int retiredList(const UtIniScan& scan, char* out, size_t cap) {
    int n = 0;
    out[0] = 0;
    for (int i = 0; i < 4; ++i) {
        if (!scan.retiredSeen[i]) continue;
        const size_t at = strlen(out);
        _snprintf_s(out + at, cap - at, _TRUNCATE, "%s%s=%s", n ? ", " : "", kRetiredKeys[i],
                    scan.retired[i]);
        ++n;
    }
    return n;
}
// A file of this version (or a mod's own file) naming one: one WARN for all of them.
void sayRetired(const UtIniScan& scan, const char* file) {
    char list[160];
    if (retiredList(scan, list, sizeof(list)) == 0) return;
    logW("config: %s in %s - not read any more (retired at ini_version 22): the pad's place and "
         "size are a pad_<W>x<H> line under [layout] now (README, \"Move the tab\")",
         list, file);
}

// THE KEYS THAT MOVED (ini_version 21): the rollover words and their text styles, now under [tab]
// of uniquetab-buttons.ini. `def` is the old default (a value equal to it is not carried).
struct MovedKey {
    const char* name;
    const char* def;
    const char* home;
};
const MovedKey kMovedKeys[4] = {
    {"tooltip_text_yes", "", "tooltip.yes"},
    {"tooltip_text_no", "", "tooltip.no"},
    {"tooltip_class_yes", "22", "tooltip.class_yes"},
    {"tooltip_class_no", "24", "tooltip.class_no"},
};
int movedIndex(const char* key) {
    for (int i = 0; i < 4; ++i)
        if (!_stricmp(key, kMovedKeys[i].name)) return i;
    return -1;
}
bool movedIsDefault(int i, const char* v) {
    if (i >= 2) return atoi(v) == atoi(kMovedKeys[i].def);
    return !v[0];
}

// A line naming a moved key where it is not carried (a file of this version, a mod's own file):
// one WARN per key whose value is not its old default, naming its new home (GD's rule).
void sayMoved(const UtIniScan& scan, const char* file, const char* buttons) {
    for (int i = 0; i < 4; ++i) {
        if (!scan.movedSeen[i] || movedIsDefault(i, scan.moved[i])) continue;
        logW("config: %s=%s is not read from %s any more - its value belongs in %s under [tab] of "
             "%s; set it there",
             kMovedKeys[i].name, scan.moved[i], file, kMovedKeys[i].home, buttons);
    }
}

// The carry, under g_applyLock: what the last migration of the main file found.
UtTooltipCarry g_carry = {{0}, {0}, -1, -1};
bool g_carryPending = false;
// The main file left at its old version (and its four keys) while a carry is pending: rewritten
// by configTooltipCarryDone once the words are in the buttons file.
bool g_carryHeld = false;

enum ParseMode {
    kParseMain = 0,   // uniquetab.ini
    kParseGame,       // a mod's own file: every key but search
    kParseState       // uniquetab.state: the three state keys
};

int stateIndex(const char* key) {
    if (!_stricmp(key, "uniq_group")) return 0;
    if (!_stricmp(key, "uniq_row")) return 1;
    if (!_stricmp(key, "owned_only")) return 2;
    return -1;
}

void scanInit(UtIniScan* s) {
    memset(s, 0, sizeof(*s));
    s->iniVersionSeen = -1;
}

int findKey(const char* name) {
    for (int i = 0; i < kUtCfgKeyCount; ++i)
        if (!strcmp(kUtCfgKeys[i].name, name)) return i;
    return -1;
}

// "[name]" (blanks around it allowed) -> true and the name in `out`.
bool sectionHeader(const char* trimmed, char* out, size_t cap) {
    if (*trimmed != '[') return false;
    const char* close = strchr(trimmed, ']');
    if (!close) return false;
    const size_t n = (size_t)(close - trimmed - 1);
    if (n + 1 > cap) return false;
    memcpy(out, trimmed + 1, n);
    out[n] = 0;
    return true;
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

// One family line. A value the family's check refuses, or one that does not fit the store, is
// not kept (one WARN); an empty value removes the line (no rectangle: the derived place).
void applyFamily(const UtCfgFamily& f, const char* key, const char* value, UtConfig* cfg,
                 UtIniScan* scan) {
    UtCfgFamilyStore* st = (UtCfgFamilyStore*)((char*)cfg + f.off);
    const char* suffix = key + strlen(f.prefix);
    size_t n = trimmedLen(value);
    char tmp[256];
    if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
    memcpy(tmp, value, n);
    tmp[n] = 0;
    if (!tmp[0]) {
        for (int i = 0; i < st->count && i < kUtCfgFamilySlots; ++i) {
            if (strcmp(st->e[i].suffix, suffix) != 0) continue;
            for (int j = i; j + 1 < st->count && j + 1 < kUtCfgFamilySlots; ++j)
                st->e[j] = st->e[j + 1];
            --st->count;
            memset(&st->e[st->count], 0, sizeof(st->e[0]));
            break;
        }
        ++scan->family;
        return;
    }
    const char* why = f.valueWhy ? f.valueWhy(tmp) : nullptr;
    if (why) {
        logW("config: %s=%s is refused - %s; the line is not used", key, tmp, why);
        ++scan->refused;
        return;
    }
    if (!configFamilySet(st, suffix, tmp)) {
        logW("config: %s=%s is not kept - %s<size> holds at most %d lines of at most %d characters",
             key, tmp, f.prefix, (int)kUtCfgFamilySlots, (int)kUtCfgFamilyValueCap - 1);
        ++scan->refused;
        return;
    }
    ++scan->family;
}

// Parses `buf` in place into `parsed`, recording what was there in `scan`. Values after a ';' or
// '#' on the same line are comments (the one exception: the '#' that starts a #rrggbb value of
// search_mark_color); a "[section]" line only notes the block the next keys stand under (a key is
// found by name). Returns the number of distinct known keys applied.
int parseInto(char* buf, int mode, UtConfig* parsed, UtIniScan* scan) {
    scanInit(scan);
    char block[24] = {0};
    char* line = buf;
    while (line && *line) {
        char* next = strpbrk(line, "\r\n");
        if (next) {
            *next = 0;
            ++next;
            while (*next == '\r' || *next == '\n') ++next;
        }
        while (*line == ' ' || *line == '\t') ++line;
        if (*line == '[' && !sectionHeader(line, block, sizeof(block))) block[0] = 0;
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
                const int st = stateIndex(key);
                if (mode == kParseState) {
                    if (st >= 0) {
                        scan->stateSeen |= 1 << st;
                        scan->stateVals[st] = atoi(value);
                    }
                } else if (!_stricmp(key, "ini_version")) {
                    parsed->iniVersion = atoi(value);
                    scan->iniVersionSeen = parsed->iniVersion;
                } else {
                    const int idx = findKey(key);
                    if (idx >= 0 && mode == kParseGame && configMainOnlyKey(kUtCfgKeys[idx].name)) {
                        ++scan->notHere;
                    } else if (idx >= 0) {
                        applyOne(kUtCfgKeys[idx], value, parsed);
                        if (!scan->seen[idx]) {
                            scan->seen[idx] = true;
                            _snprintf_s(scan->where[idx], sizeof(scan->where[idx]), _TRUNCATE,
                                        "%s", block);
                            ++scan->knownAll;
                            if (!keyHidden(kUtCfgKeys[idx]) && !keyWhenSet(kUtCfgKeys[idx]))
                                ++scan->known;
                        }
                    } else if (st >= 0) {
                        scan->stateSeen |= 1 << st;
                        scan->stateVals[st] = atoi(value);
                    } else if (movedIndex(key) >= 0) {
                        const int mi = movedIndex(key);
                        char* e = value + strlen(value);
                        while (e > value && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
                        scan->movedSeen[mi] = true;
                        _snprintf_s(scan->moved[mi], sizeof(scan->moved[mi]), _TRUNCATE, "%s", value);
                    } else if (const UtCfgFamily* fam = configFamilyOf(key, nullptr)) {
                        applyFamily(*fam, key, value, parsed, scan);
                    } else if (retiredIndex(key) >= 0) {
                        const int ri = retiredIndex(key);
                        char* e = value + strlen(value);
                        while (e > value && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
                        scan->retiredSeen[ri] = true;
                        _snprintf_s(scan->retired[ri], sizeof(scan->retired[ri]), _TRUNCATE, "%s",
                                    value);
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
    return scan->knownAll;
}

// ---- the master switch ----------------------------------------------------------------------
// enabled=0 is not a code path of its own: it is the configuration "everything off", which every
// one of these switches already supports on its own, plus the view gate: the collection view
// cannot be switched on (a shown one goes off), and a deposit or a quick move needs the view on,
// so nothing new is collected, while the journal file and its exports are untouched.
// The file's search value is kept in g_searchInFile before the collapse: the key gate is
// installed at start from it and passes every key through while the search is off.
int g_enabledSaid = -1;

void applyMasterSwitch(UtConfig* c) {
    g_searchInFile = c->search;
    if (c->enabled != g_enabledSaid) {
        g_enabledSaid = c->enabled;
        if (!c->enabled) {
            logI("config: enabled=0 - no tab, no marks, nothing new is collected");
            logD("what is already stored is untouched");
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

// Logged exactly once per process, on the first file that was actually read. It counts the
// VISIBLE keys only: a hidden key is missing by design. `migrated` says the file has just been
// rewritten in full, so "what is missing" is a statement about the OLD file.
void configSelfCheck(DWORD fileBytes, const UtIniScan& scan, int iniVersion, bool migrated) {
    if (g_selfCheckDone) return;
    g_selfCheckDone = true;
    const int visible = visibleKeyCount();
    if (migrated) {
        logI("config: %d of %d settings kept from the old file, the rest defaulted "
             "(rewritten in full, %u bytes, ini_version=%d)",
             scan.knownAll, kUtCfgKeyCount, (unsigned)configTemplateBytes(), iniVersion);
        return;
    }
    logI("config: %d of %d settings are in the file (%u bytes, a fresh one is %u; ini_version=%d)",
         scan.known, visible, (unsigned)fileBytes, (unsigned)configTemplateBytes(), iniVersion);
    if (scan.known < visible) {
        logI("config: %d setting(s) are not in the file and use their default - delete the file "
             "to get a complete one back",
             visible - scan.known);
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

// Adds ", name" (or "name=value") to a list, while it fits.
void addName(char* list, size_t cap, const char* name, const char* value) {
    const size_t at = strlen(list);
    _snprintf_s(list + at, cap - at, _TRUNCATE, "%s%s%s%s", at ? ", " : "", name,
                value ? "=" : "", value ? value : "");
}

// THE LINES A MIGRATION MOVES. A key the old file had under another [block] than the one it is
// written under now is named with its value, one log line per old block and new block. The
// [advanced] rows that hold their default are named once more: they come back commented.
void logMoves(const UtIniScan& scan, const UtConfig& parsed) {
    for (int s = 0; s < kUtCfgSectionCount; ++s) {
        const char* to = kUtCfgSections[s].name;
        for (int i = 0; i < kUtCfgKeyCount; ++i) {
            if (!scan.seen[i] || strcmp(kUtCfgKeys[i].section, to) != 0) continue;
            const char* from = scan.where[i];
            if (!_stricmp(from, to)) continue;
            bool first = true;   // the first key of an (old block, new block) pair says the line
            for (int j = 0; j < i && first; ++j)
                if (scan.seen[j] && !strcmp(kUtCfgKeys[j].section, to) &&
                    !_stricmp(scan.where[j], from))
                    first = false;
            if (!first) continue;
            char names[1024] = {0};
            int n = 0;
            for (int j = i; j < kUtCfgKeyCount; ++j) {
                if (!scan.seen[j] || strcmp(kUtCfgKeys[j].section, to) != 0 ||
                    _stricmp(scan.where[j], from) != 0)
                    continue;
                char val[300];
                valueText(kUtCfgKeys[j], parsed, val, sizeof(val));
                addName(names, sizeof(names), kUtCfgKeys[j].name, val);
                ++n;
            }
            if (from[0])
                logI("config: %d line(s) moved from [%s] to [%s]: %s", n, from, to, names);
            else
                logI("config: %d line(s) moved to [%s]: %s", n, to, names);
        }
    }
    char names[1024] = {0};
    int n = 0;
    for (int i = 0; i < kUtCfgKeyCount; ++i) {
        const UtCfgKey& k = kUtCfgKeys[i];
        if (!scan.seen[i] || !keyHidden(k)) continue;
        char val[300], def[300];
        valueText(k, parsed, val, sizeof(val));
        defaultText(k, def, sizeof(def));
        if (strcmp(val, def) != 0) continue;
        addName(names, sizeof(names), k.name, nullptr);
        ++n;
    }
    if (n > 0) {
        logI("config: %d line(s) hold their default and are commented under [advanced] now "
             "(remove the ';' to change one): %s", n, names);
    }
}


// ---- the games: a mod's own file and the state file ------------------------------------------

// THE SEED: what a mod's own file is written with the first time it is entered, and what a game
// file from another build is completed from (once): the main file's values. journal_dir is never
// a game file's value (its line there is rendered commented).
UtConfig seedFrom(const UtConfig& main) {
    UtConfig s = main;
    s.iniVersion = UT_INI_VERSION;
    s.layoutEdit = 0;
    return s;
}
UtConfig seedFromMain() { return seedFrom(g_haveBase ? g_base : UtConfig()); }

// The ini_version that added a row of the commented [advanced] blob or of [general]; 0 = a row
// every numbered game file already has (live, or commented at its default). seedForGame reads it
// only for the hidden rows; a [general] row (collect_relics) is listed for the record: a game file
// with no line for it takes the main file's value through seedFrom.
int keySince(const char* name) {
    static const struct {
        const char* name;
        int ver;
    } kSince[] = {{"layout_outline", 22},
                  {"ui_scale_pct", 23},
                  {"collect_pristine_only", 23},
                  {"pad_caption_max", 23},
                  {"journal_dir", 23},
                  {"edit_button", 24},
                  {"relic_block_gap", 25},
                  {"relic_bonus_gap", 25},
                  {"relic_panels", 26},
                  {"collect_relics", 27}};
    for (const auto& s : kSince)
        if (!_stricmp(name, s.name)) return s.ver;
    return 0;
}

// One key's value from `src` into `dst`.
void copyKeyValue(const UtCfgKey& k, UtConfig* dst, const UtConfig& src) {
    const size_t n = k.type == kUtCfgStr ? k.cap : sizeof(int);
    memcpy((char*)dst + k.off, (const char*)&src + k.off, n);
}

// The seed a game's own file from another build is parsed over, by the ini_version it names
// (`ver`, -1 = none). No ini_version: the main file's values for every key (seedFrom). A numbered
// version is a complete file, where a commented [advanced] row, a screen key with no line and a
// [layout] family with no line MEAN "not set in this game": those keep their defaults, so the
// main file's values fill only the visible keys the file has no line for and the [advanced] rows
// that version did not have yet.
UtConfig seedForGame(const UtConfig& main, int ver) {
    UtConfig s = seedFrom(main);
    if (ver < 0) return s;
    const UtConfig def;
    for (int i = 0; i < kUtCfgKeyCount; ++i) {
        const UtCfgKey& k = kUtCfgKeys[i];
        if (keyWhenSet(k) || (keyHidden(k) && keySince(k.name) <= ver)) copyKeyValue(k, &s, def);
    }
    for (int i = 0; i < kUtCfgFamilyCount; ++i)
        memcpy((char*)&s + kUtCfgFamilies[i].off, (const char*)&def + kUtCfgFamilies[i].off,
               sizeof(UtCfgFamilyStore));
    return s;
}

// g_cfg from its pieces. Under g_applyLock. True when it changed. While a mod's own file is read,
// it is the WHOLE configuration - the main file is not consulted, except for journal_dir (read
// once at start-up from the main file; g_cfg always carries the main file's value).
bool publishLocked() {
    UtConfig next = g_ovHave ? g_ovVals : g_base;
    memcpy(next.journalDir, g_base.journalDir, sizeof(next.journalDir));
    next.uniqGroup = g_stGroup;
    next.uniqRow = g_stRow;
    next.ownedOnly = g_stOwned;
    applyMasterSwitch(&next);
    const bool changed = memcmp(&next, &g_cfg, sizeof(UtConfig)) != 0;
    g_cfg = next;
    if (changed) InterlockedIncrement(&g_cfgGen);
    return changed;
}

// The ini_version a settings file's text names, -1 when it has no such line.
int sniffVersion(const char* buf) {
    for (const char* p = buf; p && *p;) {
        const char* t = p;
        while (*t == ' ' || *t == '\t') ++t;
        if (lineIsKey(t, "ini_version")) return atoi(strchr(t, '=') + 1);
        const char* e = strpbrk(p, "\r\n");
        if (!e) break;
        p = e + 1;
    }
    return -1;
}

// The stamp of the active game's own file as it is now (after the mod rewrote it).
void restampOverlayLocked() {
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (GetFileAttributesExW(g_ovPath, GetFileExInfoStandard, &fa)) {
        g_ovStamp = fa.ftLastWriteTime;
        g_ovHaveStamp = true;
    } else {
        g_ovHaveStamp = false;
    }
}

// Reads the active game's own file into g_ov*. Under g_applyLock. A key the file does not name
// is the compiled default. A file from another build (another ini_version, or none) is migrated
// once: every line it has is kept, the keys it has no say on come from the main file's values
// (seedForGame: a numbered version keeps its commented rows, screen keys and [layout] lines as
// they are), and the file is rewritten whole in the current template - one INFO line.
void loadOverlayLocked() {
    g_ovHave = false;
    g_ovVals = UtConfig();
    if (!g_ovPath[0]) return;
    DWORD len = 0;
    FILETIME ft = {0, 0};
    char* buf = readFileW(g_ovPath, &len, &ft);
    if (!buf) {
        g_ovHaveStamp = false;
        return;
    }
    g_ovStamp = ft;
    g_ovHaveStamp = true;
    const int ver = sniffVersion(buf);
    const bool old = ver != UT_INI_VERSION;
    UtConfig parsed = old ? seedForGame(g_haveBase ? g_base : UtConfig(), ver) : UtConfig();
    UtIniScan scan;
    parseInto(buf, kParseGame, &parsed, &scan);
    free(buf);
    checkColour(&parsed);
    if (scan.notHere > 0)
        logW("config: journal_dir is read at start-up from the main uniquetab.ini - the line in "
             "\"%S\" is ignored", g_ovPath);
    if (scan.stateSeen)
        logW("config: uniq_group/uniq_row/owned_only are not settings - the mod keeps them in "
             "uniquetab.state; the line(s) in \"%S\" are ignored", g_ovPath);
    {
        char where[96], home[128];
        _snprintf_s(where, sizeof(where), _TRUNCATE, "mods\\%s\\uniquetab.ini", g_game);
        _snprintf_s(home, sizeof(home), _TRUNCATE, "mods\\%s\\uniquetab-buttons.ini", g_game);
        sayMoved(scan, where, home);
        sayRetired(scan, where);
    }
    // A current file written while search was read from the main file only has no search line:
    // it is completed ONCE from the main file's value (the seed's rule) and rewritten.
    const int searchIdx = findKey("search");
    if (!old && searchIdx >= 0 && !scan.seen[searchIdx]) {
        parsed.search = g_base.search;
        if (writeGameFile(g_ovPath, g_game, parsed, true)) restampOverlayLocked();
        logI("config: mods\\%s\\uniquetab.ini gains search=%d, copied once from the main "
             "uniquetab.ini - a game's own file carries search now",
             g_game, parsed.search);
    }
    if (old) {
        parsed.iniVersion = UT_INI_VERSION;
        const bool wrote = writeGameFile(g_ovPath, g_game, parsed, true);
        char from[32];
        if (ver < 0) _snprintf_s(from, sizeof(from), _TRUNCATE, "no ini_version");
        else _snprintf_s(from, sizeof(from), _TRUNCATE, "ini_version=%d", ver);
        logI("config: mods\\%s\\uniquetab.ini (%s) migrated to a complete file, ini_version=%d - "
             "%d setting(s) kept, %s copied once from the main uniquetab.ini",
             g_game, from, UT_INI_VERSION, scan.knownAll,
             ver < 0 ? "every other value" : "the keys that version did not have");
        if (scan.unknown > 0)
            logI("config: %d line(s) of mods\\%s\\uniquetab.ini this build does not have were "
                 "dropped: %s", scan.unknown, g_game, scan.unknownNames);
        if (wrote)
            restampOverlayLocked();
        else
            logW("config: \"%S\" could NOT be rewritten - the migrated values are in use for this "
                 "run only", g_ovPath);
    } else if (scan.unknown > 0) {
        logW("config: \"%S\": %d line(s) name a setting this build does not have and are "
             "ignored: %s", g_ovPath, scan.unknown, scan.unknownNames);
    }
    g_ovVals = parsed;
    g_ovHave = true;
}

void loadStateLocked() {
    g_stGroup = -1;
    g_stRow = 0;
    g_stOwned = 0;
    if (!g_statePath[0]) return;
    DWORD len = 0;
    char* buf = readFileW(g_statePath, &len, nullptr);
    if (!buf) return;
    UtConfig unused;
    UtIniScan scan;
    parseInto(buf, kParseState, &unused, &scan);
    free(buf);
    if (scan.stateSeen & 1) g_stGroup = scan.stateVals[0] < -1 ? -1 : (scan.stateVals[0] > 255 ? 255 : scan.stateVals[0]);
    if (scan.stateSeen & 2) g_stRow = scan.stateVals[1] < 0 ? 0 : (scan.stateVals[1] > 65535 ? 65535 : scan.stateVals[1]);
    if (scan.stateSeen & 4) g_stOwned = scan.stateVals[2] ? 1 : 0;
}

// The game's own file, written when absent: the whole template with the main file's values in
// force (THE SEED - once; nothing is inherited afterwards). True when it was created now.
bool ensureGameFileLocked(const wchar_t* path, const char* key) {
    if (fileExists(path)) return false;
    if (!writeGameFile(path, key, seedFromMain(), false)) {
        logW("config: \"%S\" could not be written - the custom game %s uses the main settings",
             path, key);
        return false;
    }
    return true;
}

// The paths for g_path + g_game, the game's own file (created when absent) and the state, read.
// Under g_applyLock. True when the game's own file was created now.
bool refreshGameFilesLocked() {
    std::wstring ini, state;
    configGameFiles(g_path, g_game, &ini, &state);
    wcsncpy_s(g_ovPath, ini.c_str(), _TRUNCATE);
    wcsncpy_s(g_statePath, state.c_str(), _TRUNCATE);
    g_ovHaveStamp = false;
    bool created = false;
    if (g_ovPath[0]) created = ensureGameFileLocked(g_ovPath, g_game);
    loadOverlayLocked();
    loadStateLocked();
    g_gameFilesReady = true;
    return created;
}

// The state lines of the main file (owned_only from a file before the state file existed, or
// GD's uniq_group / uniq_row) seed the main game's uniquetab.state when it does not exist yet.
// True when it was written now. `quiet` = say nothing when it already exists.
bool seedStateLocked(const UtIniScan& scan, bool quiet) {
    if (!scan.stateSeen) return false;
    std::wstring ini, state;
    configGameFiles(g_path, "", &ini, &state);
    if (state.empty()) return false;
    if (fileExists(state.c_str())) {
        if (!quiet)
            logI("config: uniq_group/uniq_row/owned_only left uniquetab.ini; uniquetab.state "
                 "already exists and is kept");
        return false;
    }
    const int group = (scan.stateSeen & 1) ? scan.stateVals[0] : -1;
    const int row = (scan.stateSeen & 2) ? scan.stateVals[1] : 0;
    const int owned = (scan.stateSeen & 4) ? (scan.stateVals[2] ? 1 : 0) : 0;
    if (writeStateFile(state.c_str(), group, row, owned)) {
        logI("config: uniq_group, uniq_row and owned_only moved to uniquetab.state (group=%d "
             "row=%d owned_only=%d) - they are not settings",
             group, row, owned);
        return true;
    }
    logW("config: \"%S\" could not be written - the OWN filter starts off", state.c_str());
    return false;
}

// The main file's migration (ini_version 21): the rollover keys' values that are not their old
// defaults are kept for [tab] of the main game's buttons file (configTakeTooltipCarry); one INFO
// line. A Grim Dawn file (a version above this one) numbers other text styles: its two class
// values are not carried.
void carryMovedLocked(const UtIniScan& scan, int from) {
    UtTooltipCarry k = {{0}, {0}, -1, -1};
    char said[400] = {0};
    int n = 0, seen = 0;
    for (int i = 0; i < 4; ++i) {
        if (!scan.movedSeen[i]) continue;
        ++seen;
        if (movedIsDefault(i, scan.moved[i])) continue;
        if (i >= 2 && from > UT_INI_VERSION) continue;
        if (i == 0) _snprintf_s(k.yes, sizeof(k.yes), _TRUNCATE, "%s", scan.moved[i]);
        if (i == 1) _snprintf_s(k.no, sizeof(k.no), _TRUNCATE, "%s", scan.moved[i]);
        if (i == 2) k.classYes = atoi(scan.moved[i]);
        if (i == 3) k.classNo = atoi(scan.moved[i]);
        const size_t at = strlen(said);
        _snprintf_s(said + at, sizeof(said) - at, _TRUNCATE, "%s%s=%s -> %s", n ? ", " : "",
                    kMovedKeys[i].name, scan.moved[i], kMovedKeys[i].home);
        ++n;
    }
    if (!seen) return;
    if (n) {
        g_carry = k;
        g_carryPending = true;
        logI("config: the rollover keys moved to [tab] of uniquetab-buttons.ini - carried there once: %s",
             said);
    } else {
        logI("config: the rollover keys (tooltip_text_yes/no, tooltip_class_yes/no) moved to [tab] of "
             "uniquetab-buttons.ini - this file had their defaults, nothing to carry");
    }
}

// The main file, parsed into g_base; `h` is closed here. Under g_applyLock. False = not read.
bool loadMainLocked(HANDLE h, const wchar_t* path) {
    DWORD got = 0;
    char* buf = readAllA(h, &got);
    CloseHandle(h);
    if (!buf) return false;

    UtConfig parsed;  // starts from the defaults, so a key the file does not name takes its own
    UtIniScan scan;
    parseInto(buf, kParseMain, &parsed, &scan);
    free(buf);
    checkColour(&parsed);

    // A VERSION BUMP MERGES. `parsed` already holds the player's value for every key that still
    // exists and the default for every key that is new, so the whole migration is: say what
    // happened, write the same values out in the current layout, and carry on. Nothing is reset.
    const bool migrated = parsed.iniVersion != UT_INI_VERSION;
    if (migrated) {
        const int from = parsed.iniVersion;
        parsed.iniVersion = UT_INI_VERSION;
        {   // the four pad keys are retired: their values are named once, never silently dropped
            char list[160];
            if (retiredList(scan, list, sizeof(list)) > 0)
                logI("config: pad_x, pad_y, pad_h and pad_gap are retired (ini_version 22) - the pad "
                     "and the label are rectangles now (pad_<W>x<H>, label_<W>x<H> under [layout]); "
                     "the file had %s, not carried: the pad takes its derived place until a "
                     "pad_<W>x<H> line is set", list);
        }
        carryMovedLocked(scan, from);
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
        seedStateLocked(scan, false);   // owned_only (and GD's two state keys) leave the file
        logMoves(scan, parsed);
        // A pending carry holds the rewrite: the file keeps its version and the four keys until
        // their words are in the buttons file (configTooltipCarryDone), so a start that stops
        // before the buttons file is written loses nothing - the next one migrates again.
        g_carryHeld = g_carryPending;
        const bool wrote = g_carryHeld || writeIni(path, parsed, true);
        if (!g_carryHeld) g_haveStamp = false;
        logI("config: migrated from ini_version=%d to %d - %d setting(s) kept, %d new one(s) "
             "defaulted",
             from, UT_INI_VERSION, scan.knownAll, kUtCfgKeyCount - scan.knownAll);
        if (g_carryHeld) {
            logI("config: \"%S\" is rewritten once the rollover words are in uniquetab-buttons.ini - "
                 "until then it keeps ini_version=%d and those keys", path, from);
        }
        if (!wrote) {
            logW("config: \"%S\" could NOT be rewritten - the migrated values are in use for this "
                 "run only", path);
        }
        if (g_gameFilesReady) loadStateLocked();   // the state may have just been seeded
    } else {
        g_carryHeld = false;   // a file of this version holds nothing back
        // Same version, so the file is NOT rewritten - a misspelled key is the player's line to
        // fix, and a mod that silently swallowed it would leave them editing a setting that does
        // not exist. Said once per edit: the file is only re-parsed when it changes.
        if (scan.unknown > 0) {
            logW("config: %d line(s) name a setting this build does not have and are ignored: %s",
                 scan.unknown, scan.unknownNames);
        }
        sayMoved(scan, "uniquetab.ini", "uniquetab-buttons.ini");
        sayRetired(scan, "uniquetab.ini");
        if (scan.stateSeen) {
            if (seedStateLocked(scan, true) && g_gameFilesReady) loadStateLocked();
            logW("config: uniq_group/uniq_row/owned_only are not settings - the mod keeps them in "
                 "uniquetab.state; the line(s) here are ignored");
        }
    }

    configSelfCheck(got, scan, parsed.iniVersion, migrated);
    g_base = parsed;
    g_haveBase = true;
    return true;
}

}  // namespace

bool configReload(const wchar_t* path) {
    if (path && path != g_path && wcscmp(path, g_path) != 0) {
        AcquireSRWLockExclusive(&g_applyLock);
        wcsncpy_s(g_path, path, _TRUNCATE);
        g_gameFilesReady = false;   // the game's files sit beside this one
        g_haveStamp = false;
        ReleaseSRWLockExclusive(&g_applyLock);
    }

    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const UtConfig defaults;
        writeIni(path, defaults, false);
        g_haveStamp = false;
        return false;
    }

    AcquireSRWLockExclusive(&g_applyLock);
    // Skip the parse when nothing changed, so this can run every second for free: the main file's
    // stamp, and the active mod's own file (polled the same way).
    FILETIME ft = {0, 0};
    const bool haveFt = GetFileTime(h, nullptr, nullptr, &ft) != 0;
    const bool mainChanged = !(haveFt && g_haveStamp && ft.dwLowDateTime == g_lastWrite.dwLowDateTime &&
                               ft.dwHighDateTime == g_lastWrite.dwHighDateTime);
    bool ovChanged = false;
    if (g_gameFilesReady && g_ovPath[0]) {
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (GetFileAttributesExW(g_ovPath, GetFileExInfoStandard, &fa)) {
            ovChanged = !g_ovHaveStamp ||
                        fa.ftLastWriteTime.dwLowDateTime != g_ovStamp.dwLowDateTime ||
                        fa.ftLastWriteTime.dwHighDateTime != g_ovStamp.dwHighDateTime;
        } else {
            ovChanged = true;   // deleted while the game runs: seeded again below
        }
    }
    if (!mainChanged && !ovChanged && g_gameFilesReady) {
        ReleaseSRWLockExclusive(&g_applyLock);
        CloseHandle(h);
        return true;
    }
    if (mainChanged) {
        g_lastWrite = ft;
        g_haveStamp = true;
        if (!loadMainLocked(h, path)) {
            ReleaseSRWLockExclusive(&g_applyLock);
            return false;
        }
    } else {
        CloseHandle(h);
    }
    if (!g_gameFilesReady) {
        refreshGameFilesLocked();
    } else if (ovChanged) {
        // Deleted while the game runs: written again from the main file's values (the seed), as
        // on a first entry - "delete it to copy them again".
        const bool gone = !fileExists(g_ovPath);
        if (gone) ensureGameFileLocked(g_ovPath, g_game);
        loadOverlayLocked();
        if (gone)
            logI("config: mods\\%s\\uniquetab.ini was deleted - %s", g_game,
                 g_ovHave ? "written again from the main uniquetab.ini's values"
                          : "it could not be written again, the main settings apply");
        else
            logI("config: the custom game %s's own settings were re-read", g_game);
    }
    const bool changed = publishLocked();
    ReleaseSRWLockExclusive(&g_applyLock);
    // The two settings the logger itself owns, mapped once per reload: the threshold the level
    // wrappers test on every line, and the per-line flush.
    logSetLevel(g_cfg.logLevel);
    logSetFlushEachLine(g_cfg.logFlushEachLine);
    if (changed) {
        logI("config: enabled=%d log_level=%s mp_collect=%d log_flush_each_line=%d", g_cfg.enabled,
             g_cfg.logLevel, g_cfg.mpCollect, g_cfg.logFlushEachLine);
    }
    return true;
}

namespace {

// The [section] a key is written under in a settings file: its table row's, its family's, or null.
const char* sectionOfKey(const char* key) {
    const int idx = findKey(key);
    if (idx >= 0) return kUtCfgKeys[idx].section;
    const UtCfgFamily* f = configFamilyOf(key, nullptr);
    return f ? f->section : nullptr;
}

// The one line-rewriting engine the persist path uses: read the file, replace the assignment of
// every key in `keys` with `values[i]` (a NULL value deletes the line), insert the keys the file
// does not have at the end of their own [section] (at the end of the file when it has none),
// write a temp file next to it and MoveFileEx it over the original, so a reader never sees half a
// file. The blob's rules: a hidden key's commented row `;key=...` is its line when no live line
// exists, and a hidden key written at its default is commented again. `path` is the main file or
// the active mod's own file.
bool persistKeys(const wchar_t* path, const char* const* keys, const char* const* values, int n) {
    if (!path || !path[0] || n <= 0 || n > 8) return false;

    DWORD got = 0;
    char* in = nullptr;
    {
        HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        in = readAllA(h, &got);
        CloseHandle(h);
        if (!in) return false;
    }

    // Which keys the file already assigns (live), and which have only a commented blob row.
    const UtCfgKey* hidden[8];
    bool live[8] = {false, false, false, false, false, false, false, false};
    bool row[8] = {false, false, false, false, false, false, false, false};
    bool rowUsed[8] = {false, false, false, false, false, false, false, false};
    for (int i = 0; i < n; ++i) {
        const int idx = findKey(keys[i]);
        hidden[i] = (idx >= 0 && keyHidden(kUtCfgKeys[idx])) ? &kUtCfgKeys[idx] : nullptr;
    }
    for (const char* p = in; p && *p;) {
        const char* t = p;
        while (*t == ' ' || *t == '\t') ++t;
        for (int i = 0; i < n; ++i) {
            if (lineIsKey(t, keys[i])) live[i] = true;
            if (hidden[i] && t[0] == ';' && lineIsKey(t + 1, keys[i])) row[i] = true;
        }
        const char* e = strpbrk(p, "\r\n");
        if (!e) break;
        p = e + ((e[0] == '\r' && e[1] == '\n') ? 2 : 1);
    }
    bool pending[8];
    for (int i = 0; i < n; ++i) pending[i] = !live[i] && !row[i] && values[i] != nullptr;
    // ";key=value" for a hidden key at its default.
    auto commented = [&](int i) {
        if (!hidden[i]) return false;
        char def[300];
        defaultText(*hidden[i], def, sizeof(def));
        return strcmp(values[i] ? values[i] : "", def) == 0;
    };

    // Rebuild the file line by line: only the listed assignments change. Every line either
    // survives byte for byte, becomes "key=value", or goes; so input + n inserted lines is an
    // upper bound - and if anything ever did not fit we write NOTHING.
    size_t extra = 1024;
    for (int i = 0; i < n; ++i) extra += strlen(keys[i]) + (values[i] ? strlen(values[i]) : 0) + 256;
    const size_t outCap = (size_t)got + extra;
    char* out = (char*)malloc(outCap);
    if (!out) {
        free(in);
        return false;
    }
    size_t o = 0;
    size_t removed = 0;          // bytes of deleted lines, for the "shorter" guard below
    bool overflow = false;
    char section[64] = {0};      // the [section] the rewrite is in
    size_t sectionEnd = 0;       // `out` offset just past its last non-blank line
    bool inSection = false;

    // Inserts every pending key of `section` at `sectionEnd`, moving the blank lines after it.
    auto flushSection = [&]() {
        if (!inSection || overflow) return;
        for (int i = 0; i < n && !overflow; ++i) {
            if (!pending[i]) continue;
            const char* sec = sectionOfKey(keys[i]);
            if (!sec || strcmp(sec, section) != 0) continue;
            char line[512];
            const int len = _snprintf_s(line, sizeof(line), _TRUNCATE, "%s%s=%s\r\n",
                                        commented(i) ? ";" : "", keys[i], values[i]);
            if (len < 0 || o + (size_t)len + 1 > outCap) {
                overflow = true;
                break;
            }
            memmove(out + sectionEnd + len, out + sectionEnd, o - sectionEnd);
            memcpy(out + sectionEnd, line, (size_t)len);
            sectionEnd += (size_t)len;
            o += (size_t)len;
            pending[i] = false;
        }
    };

    char* line = in;
    while (line && *line && !overflow) {
        // Exactly one line ending is consumed, so blank separator lines survive the rewrite.
        char* next = strpbrk(line, "\r\n");
        size_t rawLen = next ? (size_t)(next - line) : strlen(line);
        if (next) {
            const bool crlf = (next[0] == '\r' && next[1] == '\n');
            rawLen += crlf ? 2 : 1;
            *next = 0;
            next += crlf ? 2 : 1;
        }
        const char* trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') ++trimmed;
        char headName[64];
        if (sectionHeader(trimmed, headName, sizeof(headName))) {
            flushSection();
            _snprintf_s(section, sizeof(section), _TRUNCATE, "%s", headName);
            inSection = true;
        }
        int hit = -1;
        bool hitRow = false;
        for (int i = 0; i < n && hit < 0; ++i) {
            if (lineIsKey(trimmed, keys[i])) {
                hit = i;
            } else if (hidden[i] && !live[i] && !rowUsed[i] && trimmed[0] == ';' &&
                       lineIsKey(trimmed + 1, keys[i])) {
                hit = i;
                hitRow = true;
            }
        }
        if (hit >= 0 && !values[hit]) {
            removed += rawLen;   // a NULL value deletes the line
            line = next;
            continue;
        }
        int wrote;
        if (hit >= 0) {
            if (hitRow) rowUsed[hit] = true;
            // Keep the line's trailing comment, at its original column where it still fits: the
            // file documents itself and persisting a key must not delete its own explanation. A
            // row aligned with tabs stays aligned with tabs.
            const char* eq = strchr(trimmed, '=');
            const char* cmt = eq ? strpbrk(eq, ";#") : nullptr;
            char head[160];
            const int hn = _snprintf_s(head, sizeof(head), _TRUNCATE, "%s%s=%s",
                                       commented(hit) ? ";" : "", keys[hit], values[hit]);
            if (hn < 0) { overflow = true; break; }
            if (cmt) {
                bool tabs = false;
                for (const char* q = cmt; q > eq && (q[-1] == ' ' || q[-1] == '\t'); --q)
                    if (q[-1] == '\t') tabs = true;
                char pad[160];
                if (tabs) {
                    // A blob row goes back to the blob's own column, however it was edited.
                    const size_t col = hidden[hit] ? blobColumn()
                                                   : visualCol(trimmed, (size_t)(cmt - trimmed));
                    tabbedTo(pad, sizeof(pad), (size_t)hn, col);
                } else {
                    const size_t col = (size_t)(cmt - trimmed);
                    size_t p = 0;
                    while (p + 1 < sizeof(pad) && (size_t)hn + p < col) pad[p++] = ' ';
                    if (p == 0) pad[p++] = ' ';
                    pad[p] = 0;
                }
                wrote = _snprintf_s(out + o, outCap - o, _TRUNCATE, "%s%s%s\r\n", head, pad, cmt);
            } else {
                wrote = _snprintf_s(out + o, outCap - o, _TRUNCATE, "%s\r\n", head);
            }
        } else {
            wrote = _snprintf_s(out + o, outCap - o, _TRUNCATE, "%s\r\n", line);
        }
        if (wrote < 0) { overflow = true; break; }
        o += (size_t)wrote;
        if (*trimmed) sectionEnd = o;   // blank lines stay after anything inserted
        line = next;
    }
    flushSection();
    for (int i = 0; i < n && !overflow; ++i) {
        if (!pending[i]) continue;
        const int wrote = _snprintf_s(out + o, outCap - o, _TRUNCATE, "%s%s=%s\r\n",
                                      commented(i) ? ";" : "", keys[i], values[i]);
        if (wrote < 0) { overflow = true; break; }
        o += (size_t)wrote;
    }
    free(in);
    // A rewrite may differ from the input only by the values it changed and the lines it deleted.
    // If it ever came out materially shorter than that, something went wrong - write NOTHING
    // rather than replace the file with a truncated one.
    if (overflow || o + removed + 512 < (size_t)got) {
        logW("config: persist REFUSED - the rewrite would shorten \"%S\" (%u -> %u bytes)", path,
             (unsigned)got, (unsigned)o);
        free(out);
        return false;
    }

    wchar_t tmpPath[MAX_PATH];
    _snwprintf_s(tmpPath, _TRUNCATE, L"%s.tmp", path);
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
    if (!MoveFileExW(tmpPath, path, MOVEFILE_REPLACE_EXISTING)) {
        Sleep(30);
        if (!MoveFileExW(tmpPath, path, MOVEFILE_REPLACE_EXISTING)) {
            DeleteFileW(tmpPath);
            return false;
        }
    }
    // force the next reload to parse what was just written
    if (!_wcsicmp(path, g_path)) g_haveStamp = false;
    if (g_ovPath[0] && !_wcsicmp(path, g_ovPath)) g_ovHaveStamp = false;
    return true;
}

// The state keys: g_cfg at once, and the active game's uniquetab.state rewritten whole (it is the
// mod's own file). Under g_applyLock.
bool persistStateLocked(const char* key, int value) {
    const int st = stateIndex(key);
    if (st == 0) g_stGroup = value < -1 ? -1 : (value > 255 ? 255 : value);
    if (st == 1) g_stRow = value < 0 ? 0 : (value > 65535 ? 65535 : value);
    if (st == 2) g_stOwned = value ? 1 : 0;
    g_cfg.uniqGroup = g_stGroup;
    g_cfg.uniqRow = g_stRow;
    g_cfg.ownedOnly = g_stOwned;
    if (!g_gameFilesReady) {
        std::wstring ini, state;
        configGameFiles(g_path, g_game, &ini, &state);
        wcsncpy_s(g_statePath, state.c_str(), _TRUNCATE);
    }
    if (!g_statePath[0]) return false;
    return writeStateFile(g_statePath, g_stGroup, g_stRow, g_stOwned);
}

// configPersistStr's router. Under g_applyLock. Every key goes to the file whose values are in
// force: a mod's own file while one is active (written again from the seed first when it was
// deleted), else uniquetab.ini - except journal_dir, which only the main file holds, and the
// state keys, which go to the game's state file.
bool persistRoutedLocked(const char* const* keys, const char* const* values, int n) {
    if (n <= 0 || n > 8) return false;
    for (int i = 0; i < n; ++i) {
        if (!keys[i] || !*keys[i]) return false;
        if (values[i] && (strchr(values[i], '\r') || strchr(values[i], '\n'))) return false;
    }
    bool custom = g_gameFilesReady && g_ovPath[0];
    if (custom && !fileExists(g_ovPath)) ensureGameFileLocked(g_ovPath, g_game);
    if (custom && !fileExists(g_ovPath)) custom = false;   // not writable: the main is in force
    const char* mk[8];
    const char* mv[8];
    const char* gk[8];
    const char* gv[8];
    int mn = 0, gn = 0;
    bool ok = true;
    for (int i = 0; i < n; ++i) {
        if (configIsStateKey(keys[i])) {
            ok = persistStateLocked(keys[i], values[i] ? atoi(values[i]) : 0) && ok;
            continue;
        }
        if (custom && !configMainOnlyKey(keys[i])) {
            gk[gn] = keys[i];
            gv[gn] = values[i];
            ++gn;
        } else {
            mk[mn] = keys[i];
            mv[mn] = values[i];
            ++mn;
        }
    }
    if (gn > 0) ok = persistKeys(g_ovPath, gk, gv, gn) && ok;
    if (mn > 0) ok = persistKeys(g_path, mk, mv, mn) && ok;
    return ok;
}

}  // namespace

bool configPersistInt(const char* key, int value) {
    if (!key || !*key) return false;
    char text[16];
    _snprintf_s(text, sizeof(text), _TRUNCATE, "%d", value);
    const char* keys[1] = {key};
    const char* values[1] = {text};
    AcquireSRWLockExclusive(&g_applyLock);
    const bool ok = persistRoutedLocked(keys, values, 1);
    ReleaseSRWLockExclusive(&g_applyLock);
    return ok;
}

bool configPersistStr(const char* const* keys, const char* const* values, int n) {
    if (!keys || !values) return false;
    AcquireSRWLockExclusive(&g_applyLock);
    const bool ok = persistRoutedLocked(keys, values, n);
    ReleaseSRWLockExclusive(&g_applyLock);
    return ok;
}

long configGeneration() { return InterlockedCompareExchange(&g_cfgGen, 0, 0); }
long configGameGeneration() { return InterlockedCompareExchange(&g_gameGen, 0, 0); }

bool configSetGame(const char* game) {
    const char* name = game ? game : "";
    AcquireSRWLockExclusive(&g_applyLock);
    if (!strcmp(g_game, name)) {
        ReleaseSRWLockExclusive(&g_applyLock);
        return false;
    }
    const bool hadOwn = g_ovHave;
    _snprintf_s(g_game, sizeof(g_game), _TRUNCATE, "%s", name);
    InterlockedIncrement(&g_gameGen);
    if (!g_path[0]) {   // no file read yet: the first reload reads the game's files
        g_gameFilesReady = false;
        ReleaseSRWLockExclusive(&g_applyLock);
        return true;
    }
    const bool created = refreshGameFilesLocked();
    if (g_haveBase) publishLocked();
    const bool own = g_ovHave;
    ReleaseSRWLockExclusive(&g_applyLock);
    // The logger's two settings follow the game in force at once (configReload maps them only
    // when a file changes): a mod's log_level is that mod's.
    logSetLevel(g_cfg.logLevel);
    logSetFlushEachLine(g_cfg.logFlushEachLine);
    if (created) {
        logI("config: the custom game %s has its own settings now - mods\\%s\\uniquetab.ini, "
             "a complete file with the main uniquetab.ini's values (enabled=%d collect_mi=%d "
             "log_level=%s); while it is played only that file is read",
             name, name, g_cfg.enabled, g_cfg.collectMi, g_cfg.logLevel);
    } else if (own) {
        logI("config: the %s game's own settings are in force - mods\\%s\\uniquetab.ini "
             "(enabled=%d collect_mi=%d log_level=%s); the main uniquetab.ini is not read while "
             "it is played",
             name, name, g_cfg.enabled, g_cfg.collectMi, g_cfg.logLevel);
    } else if (hadOwn) {
        logI("config: the main game uses the main settings (uniquetab.ini)");
    }
    return true;
}

bool configTakeTooltipCarry(UtTooltipCarry* out) {
    if (!out) return false;
    AcquireSRWLockExclusive(&g_applyLock);
    const bool had = g_carryPending;
    if (had) *out = g_carry;
    g_carryPending = false;
    ReleaseSRWLockExclusive(&g_applyLock);
    return had;
}

void configTooltipCarryDone() {
    AcquireSRWLockExclusive(&g_applyLock);
    if (g_carryHeld && g_haveBase && g_path[0]) {
        g_carryHeld = false;
        if (writeIni(g_path, g_base, true)) {
            g_haveStamp = false;
            logI("config: \"%S\" rewritten at ini_version=%d - the rollover keys left it", g_path,
                 UT_INI_VERSION);
        } else {
            logW("config: \"%S\" could NOT be rewritten - it keeps its old ini_version until the next "
                 "start", g_path);
        }
    }
    ReleaseSRWLockExclusive(&g_applyLock);
}

void configActiveGame(char* out, size_t cap) {
    if (!out || !cap) return;
    AcquireSRWLockShared(&g_applyLock);
    _snprintf_s(out, cap, _TRUNCATE, "%s", g_game);
    ReleaseSRWLockShared(&g_applyLock);
}

namespace {
// The value of a live `key=` line in a settings file's text, -1 when it has none.
int liveKeyValue(const char* buf, const char* key) {
    for (const char* p = buf; p && *p;) {
        const char* t = p;
        while (*t == ' ' || *t == '\t') ++t;
        if (lineIsKey(t, key)) return atoi(strchr(t, '=') + 1);
        const char* e = strpbrk(p, "\r\n");
        if (!e) break;
        p = e + 1;
    }
    return -1;
}
}  // namespace

bool configSearchGateAtStart(char* games, size_t cap) {
    if (games && cap) games[0] = 0;
    wchar_t dir[MAX_PATH];
    AcquireSRWLockShared(&g_applyLock);
    bool on = g_haveBase ? g_base.search != 0 : true;
    wcsncpy_s(dir, g_path, _TRUNCATE);
    ReleaseSRWLockShared(&g_applyLock);
    wchar_t* slash = wcsrchr(dir, L'\\');
    if (!slash) return on;
    *slash = 0;
    wchar_t pattern[MAX_PATH];
    _snwprintf_s(pattern, MAX_PATH, _TRUNCATE, L"%s\\" L"mods" L"\\*", dir);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return on;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
        wchar_t ini[MAX_PATH];
        _snwprintf_s(ini, MAX_PATH, _TRUNCATE, L"%s\\" L"mods" L"\\%s\\uniquetab.ini", dir,
                     fd.cFileName);
        DWORD len = 0;
        char* buf = readFileW(ini, &len, nullptr);
        if (!buf) continue;
        const int v = liveKeyValue(buf, "search");
        free(buf);
        if (v <= 0) continue;
        on = true;
        if (games && cap) {
            char name[MAX_PATH];
            WideCharToMultiByte(CP_ACP, 0, fd.cFileName, -1, name, sizeof(name), nullptr, nullptr);
            const size_t used = strlen(games);
            _snprintf_s(games + used, cap - used, _TRUNCATE, "%smods\\%s\\uniquetab.ini",
                        used ? ", " : "", name);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return on;
}

bool configGameGenInputs(const char* game, UtGenSettings* out) {
    if (!out) return false;
    std::wstring ini;
    UtConfig main;
    AcquireSRWLockShared(&g_applyLock);
    if (g_haveBase) main = g_base;
    configGameFiles(g_path, game, &ini, nullptr);
    ReleaseSRWLockShared(&g_applyLock);
    UtConfig use = main;
    bool own = false;
    if (!ini.empty()) {
        DWORD len = 0;
        if (char* buf = readFileW(ini.c_str(), &len, nullptr)) {
            const int ver = sniffVersion(buf);
            UtConfig parsed = ver != UT_INI_VERSION ? seedForGame(main, ver) : UtConfig();
            UtIniScan scan;
            parseInto(buf, kParseGame, &parsed, &scan);
            free(buf);
            use = parsed;
            own = true;
        }
    }
    _snprintf_s(out->textLanguage, sizeof(out->textLanguage), _TRUNCATE, "%s", use.textLanguage);
    out->collectMi = use.collectMi;
    out->collectRelics = use.collectRelics;
    out->collectUnobtainable = use.collectUnobtainable;
    return own;
}

}  // namespace ut
