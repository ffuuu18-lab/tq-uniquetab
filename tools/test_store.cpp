// test_store.cpp - the private table's owning side (src\ut_store.cpp) and the pure decisions of
// src\ut_depositgate.h, offline. NO GAME NEEDED.
//
// TQ port of the Grim Dawn mod's tools\test_store.cpp. GD's central proof
// is kept: the REAL decision functions the shipped build calls are enumerated over EVERY fact
// combination, and every verdict but "journal it" is shown to be reached without the journal
// being touched. Added for TQ: the take decision, the save-set leaf, the store's
// deposit/take/count through a real journal file, and the character-save watch over a fake
// SaveData folder (%UNIQUETAB_SAVEDATA%).
#include <windows.h>

#include <stdio.h>
#include <string.h>

#include <string>

#include "../src/ut_config.h"
#include "../src/ut_depositgate.h"
#include "../src/ut_log.h"
#include "../src/ut_rescue.h"
#include "../src/ut_store.h"
#include "../src/ut_mikind.h"
#include "../src/ut_relicpage.h"

namespace {

int g_pass = 0, g_fail = 0;

void check(bool ok, const char* what) {
    if (ok) {
        ++g_pass;
        printf("  [ok]   %s\n", what);
    } else {
        ++g_fail;
        printf("  [FAIL] %s\n", what);
    }
}

void touch(const char* path, unsigned long long ft) {
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    FILETIME f;
    f.dwLowDateTime = (DWORD)(ft & 0xFFFFFFFFu);
    f.dwHighDateTime = (DWORD)(ft >> 32);
    SetFileTime(h, nullptr, nullptr, &f);
    CloseHandle(h);
}

unsigned long long now() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

ut::UtReplicaCapture cap(const char* base, unsigned seed) {
    ut::UtReplicaCapture c;
    memset(&c, 0, sizeof(c));
    _snprintf_s(c.str[ut::kUtIdBase], ut::kUtIdStrMax, _TRUNCATE, "%s", base);
    c.seed = seed;
    c.stack = 1;
    ut::utJournalKey(base, c.record, sizeof(c.record));
    return c;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: test_store <empty folder under build\\test>\n");
        return 2;
    }
    char dir[MAX_PATH];
    _snprintf_s(dir, sizeof(dir), _TRUNCATE, "%s", argv[1]);
    CreateDirectoryA(dir, nullptr);
    SetEnvironmentVariableA("UNIQUETAB_OUT", dir);
    char save[MAX_PATH];
    _snprintf_s(save, sizeof(save), _TRUNCATE, "%s\\SaveData", dir);
    SetEnvironmentVariableA("UNIQUETAB_SAVEDATA", save);
    wchar_t logPath[MAX_PATH];
    _snwprintf_s(logPath, MAX_PATH, _TRUNCATE, L"%S\\test_store.log", dir);
    ut::logInit(logPath);
    ut::logSetLevel("debug");

    printf("\n1. utDepositDecide over EVERY fact combination\n");
    {
        long total = 0, table = 0, wrongTable = 0, orderBad = 0;
        long stackOneRule = 0;   // combinations the rule (stack == 1 only) would refuse
        const unsigned stacks[3] = {0u, 1u, 2u};
        long haveRefused = 0;   // journal-ready combinations the one-copy rule refused
        for (int caller = 0; caller < 4; ++caller)
            for (unsigned bits = 0; bits < (1u << 10); ++bits)
                for (int si = 0; si < 3; ++si)
                for (unsigned rn = 0; rn < 3u; ++rn) {
                    ut::UtDepositFacts f;
                    f.caller = caller;
                    f.viewOn = (bits & 1u) != 0;
                    f.mpKnown = (bits & 2u) != 0;
                    f.mp = (bits & 4u) != 0;
                    f.tableOwns = (bits & 8u) != 0;
                    f.bindings = (bits & 16u) != 0;
                    f.isItem = (bits & 32u) != 0;
                    f.inCatalogue = (bits & 64u) != 0;
                    f.haveCap = (bits & 128u) != 0;
                    f.capMatches = (bits & 256u) != 0;
                    f.mpCollect = (bits & 512u) != 0;   // GD's mp_collect
                    f.stack = stacks[si];
                    f.rowsNow = rn;   // rows of its record already in the journal
                    const int v = ut::utDepositDecide(f);
                    ++total;
                    // GetNumberInStack answers 0 for a unique (Game.dll 0x8800 `xor eax,eax`);
                    // 0 and 1 are a single item, 2 or more a stack.
                    const bool all = f.viewOn && (caller == 1 || caller == 2) && f.mpKnown &&
                                     (!f.mp || f.mpCollect) && f.tableOwns && f.bindings && f.isItem &&
                                     f.inCatalogue && f.rowsNow == 0u && f.stack <= 1u &&
                                     f.haveCap && f.capMatches;
                    if (v == ut::kUtDepRefuseHave && f.stack <= 1u && f.haveCap && f.capMatches)
                        ++haveRefused;
                    if (v == ut::kUtDepTable) ++table;
                    if (v == ut::kUtDepTable && f.stack != 1u) ++stackOneRule;
                    if ((v == ut::kUtDepTable) != all) ++wrongTable;
                    // the FIRST failing fact names the verdict (the log reads in this order)
                    int want = ut::kUtDepTable;
                    if (!f.viewOn) want = ut::kUtDepRefuseViewOff;
                    else if (!(caller == 1 || caller == 2)) want = ut::kUtDepRefuseCaller;
                    else if (!f.mpKnown) want = ut::kUtDepRefuseMpUnknown;
                    else if (f.mp && !f.mpCollect) want = ut::kUtDepRefuseMultiplayer;
                    else if (!f.tableOwns) want = ut::kUtDepRefuseTableOff;
                    else if (!f.bindings) want = ut::kUtDepRefuseBindings;
                    else if (!f.isItem) want = ut::kUtDepRefuseNotItem;
                    else if (!f.inCatalogue) want = ut::kUtDepRefuseNotCatalogue;
                    else if (f.rowsNow > 0u) want = ut::kUtDepRefuseHave;
                    else if (f.stack >= 2u) want = ut::kUtDepRefuseStack;
                    else if (!f.haveCap || !f.capMatches) want = ut::kUtDepRefuseCapture;
                    if (v != want) ++orderBad;
                }
        char line[160];
        _snprintf_s(line, sizeof(line), _TRUNCATE,
                    "%ld combinations: %ld journal, the rest refused; journal iff every fact holds",
                    total, table);
        // 4 per session shape that allows moves - single player (mp_collect 0 or 1) and
        // multiplayer with mp_collect=1
        check(wrongTable == 0 && table == 12, line);
        check(orderBad == 0, "each refusal names the first failing fact, in the log's order");
        _snprintf_s(line, sizeof(line), _TRUNCATE,
                    "stack 0 (a unique, as the engine reports it) and 1 deposit, 2 refuses; the "
                    "rule (exactly 1) refused %ld of the %ld journal combinations", stackOneRule, table);
        check(stackOneRule == 6, line);
        _snprintf_s(line, sizeof(line), _TRUNCATE,
                    "one copy per record: a record with 1 or 2 rows (pending-in included) is "
                    "refused as 'already in the collection' - %ld otherwise-ready combinations",
                    haveRefused);
        check(haveRefused == 24 &&strcmp(ut::utDepositWhyText(ut::kUtDepRefuseHave),
                                          "already in the collection") == 0, line);
        check(ut::utStackIsSingle(0u) && ut::utStackIsSingle(1u) && !ut::utStackIsSingle(2u) &&
                  !ut::utStackIsSingle(0xFFFFFFFFu),
              "utStackIsSingle: 0 and 1 single, 2 and an unreadable count (0xFFFFFFFF) a stack");
        check(ut::utJournalStack(0u) == 1u && ut::utJournalStack(1u) == 1u &&
                  ut::utJournalStack(2u) == 2u,
              "the journal row's stack is max(1, n)");
        ut::UtDepositFacts f;
        memset(&f, 0, sizeof(f));
        f.caller = ut::kUtDepCallerDrag;
        f.viewOn = f.mpKnown = f.tableOwns = f.bindings = f.isItem = f.inCatalogue = true;
        f.stack = 0;   // what Item::GetNumberInStack answers for a unique
        check(ut::utDepositCapable(f), "the capability slot answers true for a unique (stack 0)");
        f.stack = 1;
        check(ut::utDepositCapable(f), "the capability slot answers before the capture ran");
        f.stack = 2;
        check(!ut::utDepositCapable(f) &&
                  ut::utDepositCapableVerdict(f) == ut::kUtDepRefuseStack,
              "... and refuses a stack of two, naming the stack (the one debug line's reason)");
        f.stack = 0;
        f.rowsNow = 1;
        check(!ut::utDepositCapable(f) &&
                  ut::utDepositCapableVerdict(f) == ut::kUtDepRefuseHave &&
                  !ut::utCapSlotAnswer(true, ut::utDepositCapable(f), true),
              "... and refuses a second copy of a collected record (the drag-drop does "
              "nothing, the item stays on the cursor)");
        f.rowsNow = 0;
        ut::UtTakeFacts tt = {true, true, false, true, true, true, 2u, true};
        check(ut::utTakeDecide(tt) == ut::kUtTakeOk,
              "a record already held twice stays takeable (the rule never refuses a take)");
        f.stack = 0;
        f.inCatalogue = false;
        check(!ut::utDepositCapable(f), "... and refuses a non-catalogue item (a green rare)");
        f.inCatalogue = true;
        f.mp = true;
        check(!ut::utDepositCapable(f), "... and everything in multiplayer with mp_collect=0");
        f.mpCollect = true;
        check(ut::utDepositCapable(f), "... and a unique in multiplayer with mp_collect=1");
        f.mpKnown = false;
        check(!ut::utDepositCapable(f) &&
                  ut::utDepositCapableVerdict(f) == ut::kUtDepRefuseMpUnknown,
              "... and everything while the session state is unknown, even with mp_collect=1");
        f.mpCollect = false;
        check(ut::utDepositCapableVerdict(f) == ut::kUtDepRefuseMpUnknown,
              "unknown with mp_collect=0 is named UNKNOWN too (the ini is not the cause)");
        check(strcmp(ut::utDepositWhyText(ut::kUtDepRefuseMultiplayer),
                     "a multiplayer session is active and mp_collect=0") == 0 &&
                  strstr(ut::utDepositWhyText(ut::kUtDepRefuseMpUnknown), "UNKNOWN") != nullptr &&
                  strstr(ut::utDepositWhyText(ut::kUtDepRefuseMpUnknown), "mp_collect=0") == nullptr,
              "two refusal texts, GD's two reasons (mp_collect=0 / the mode UNKNOWN)");
        f.mpKnown = true;
        f.mpCollect = false;
        f.mp = false;
        f.tableOwns = false;
        check(!ut::utDepositCapable(f), "... and everything with an unwritable journal (the earlier answer)");
    }

    printf("\n1c. collect_pristine_only (GD's key): the parts beyond the record\n");
    {
        const char* names[6] = {"prefix", "suffix", "relic", "relicBonus", "relic2", "relicBonus2"};
        const char* clean[6] = {"", "", "", "", "", ""};
        const char* relic[6] = {"", "", "records/item/relics/relic_x.dbr", "", "", ""};
        const char* affix[6] = {"records/item/lootmagicalaffixes/prefix/p.dbr", "", "", "", "", ""};
        char why[128];
        check(ut::utPartsPristine(clean, names, 6, why, sizeof(why)) && !why[0], "no part: pristine");
        check(!ut::utPartsPristine(relic, names, 6, why, sizeof(why)) &&
                  !strcmp(why, "relic = \"records/item/relics/relic_x.dbr\""),
              "a socketed relic: not pristine, named GD's way");
        check(!ut::utPartsPristine(affix, names, 6, why, sizeof(why)) && !strncmp(why, "prefix = ", 9),
              "an affix: not pristine");
        ut::UtDepositFacts f;
        memset(&f, 0, sizeof(f));
        f.caller = ut::kUtDepCallerQuick;
        f.viewOn = f.mpKnown = f.tableOwns = f.bindings = f.isItem = f.inCatalogue = true;
        f.haveCap = f.capMatches = true;
        f.notPristine = true;
        check(ut::utDepositDecide(f) == ut::kUtDepTable, "collect_pristine_only=0: a relic-socketed unique goes in");
        f.pristineOnly = true;
        check(ut::utDepositDecide(f) == ut::kUtDepRefuseNotPristine &&
                  strstr(ut::utDepositWhyText(ut::kUtDepRefuseNotPristine), "not pristine") != nullptr,
              "collect_pristine_only=1: refused, the item is not pristine");
        check(ut::utDepositCapable(f), "the per-frame capability does not read the parts (the drop decides)");
        f.notPristine = false;
        check(ut::utDepositDecide(f) == ut::kUtDepTable, "collect_pristine_only=1: a pristine unique goes in");
        f.notPristine = true;
        f.rowsNow = 1;
        check(ut::utDepositDecide(f) == ut::kUtDepRefuseHave, "the earlier refusals come first");
    }

    printf("\n1d. relics and charms: the gate per kind, the routing, the takes' choices\n");
    {
        using namespace ut;
        // the gate per kind: any number of rows of one relic; a unique still once
        UtDepositFacts f;
        memset(&f, 0, sizeof(f));
        f.caller = kUtDepCallerDrag;
        f.viewOn = f.mpKnown = f.tableOwns = f.bindings = f.isItem = f.inCatalogue = true;
        f.haveCap = f.capMatches = true;
        f.relicKind = true;
        f.relicRoute = kUtRelicRouteRed;
        bool many = true;
        for (unsigned rows = 0; rows < 3; ++rows) {
            f.rowsNow = rows;
            many = many && utDepositDecide(f) == kUtDepTable;
        }
        check(many, "a relic: the 1st, 2nd and 3rd row of one record are accepted");
        f.relicKind = false;
        f.rowsNow = 1;
        check(utDepositDecide(f) == kUtDepRefuseHave, "a unique with a row: still refused (once)");
        f.rowsNow = 0;
        f.relicRoute = 0;
        check(utDepositDecide(f) == kUtDepTable, "a unique: relicRoute is not read");
        f.relicKind = true;
        f.relicRoute = kUtRelicRouteRefuseBonus;
        check(utDepositDecide(f) == kUtDepRefuseRelicBonus &&
                  !strcmp(utDepositWhyText(kUtDepRefuseRelicBonus),
                          "a completion bonus its record's table does not list"),
              "a relic routed 'bonus not in the table': refused, its words");
        f.relicRoute = kUtRelicRouteRefuseShards;
        check(utDepositDecide(f) == kUtDepRefuseRelicShards &&
                  !strcmp(utDepositWhyText(kUtDepRefuseRelicShards),
                          "a shard count its record does not allow"),
              "a relic routed 'shard count not allowed': refused, its words");
        f.relicRoute = kUtRelicRouteNone;
        check(utDepositDecide(f) == kUtDepRefuseCapture, "a relic not routed: refused as a capture");
        check(utDepositCapable(f), "the per-frame capability routes at the drop (a relic is capable)");
        f.relicRoute = kUtRelicRouteGreen;
        f.inCatalogue = false;
        check(utDepositDecide(f) == kUtDepRefuseNotCatalogue, "a record the relic list lacks: not a unique of the collection");
        f.inCatalogue = true;
        {   // collect_relics=0: uniq-relics.txt lists nothing, so a relic is no relic of the list and
            // no record of the catalogue - refused as any record outside the collection
            UtRelicList none;
            std::string rerr;
            const bool read = utRelicListRead(std::string(), &none, &rerr);
            UtDepositFacts g = f;
            g.relicKind = read && utRelicFind(none, "records/item/relics/01_act1_aegisofathena.dbr") >= 0;
            g.inCatalogue = g.relicKind;   // the catalogue never holds a relic record
            g.relicRoute = 0;
            g.rowsNow = 0;
            check(read && none.rows.empty() && utDepositDecide(g) == kUtDepRefuseNotCatalogue &&
                      !strcmp(utDepositWhyText(kUtDepRefuseNotCatalogue), "not a unique of the collection"),
                  "collect_relics=0 (an empty relic list): a relic is refused - not a unique of the collection");
        }
        f.stack = 2;
        check(utDepositDecide(f) == kUtDepRefuseStack, "a stack is refused before the routing");
        f.stack = 0;
        check(kUtDepRefuseRelicBonus == kUtDepRefuseNotPristine + 1 &&
                  kUtDepRefuseRelicShards == kUtDepRefuseNotPristine + 2 && kUtDepRefuseNotPristine == 12,
              "the two refusals are appended (no verdict renumbered)");

        // the pristine test per kind: a relic's own relic / relicBonus strings are its identity
        const char* names[6] = {"prefix", "suffix", "relic", "relicBonus", "relic2", "relicBonus2"};
        const char* own[6] = {"", "", "records/item/relics/r.dbr", "records/item/relics/bonus/b.dbr", "", ""};
        const char* second[6] = {"", "", "", "records/item/relics/bonus/b.dbr", "records/x.dbr", ""};
        const char* affixed[6] = {"", "records/item/lootmagicalaffixes/suffix/s.dbr", "", "", "", ""};
        char why[128];
        check(kUtIdRelic - kUtIdPrefix == kUtPartRelic && kUtIdRelicBonus - kUtIdPrefix == kUtPartRelicBonus,
              "the part indices are the replica's relic / relicBonus");
        check(utPartsPristineKind(own, names, 6, true, why, sizeof(why)) && !why[0],
              "a relic with its own bonus: pristine");
        check(!utPartsPristineKind(own, names, 6, false, why, sizeof(why)) && !strncmp(why, "relic = ", 8),
              "the same strings on a unique: not pristine (a socketed relic)");
        check(!utPartsPristineKind(second, names, 6, true, why, sizeof(why)) && !strncmp(why, "relic2 = ", 9),
              "a relic with relic2 set: not pristine");
        check(!utPartsPristineKind(affixed, names, 6, true, why, sizeof(why)) && !strncmp(why, "suffix = ", 9),
              "a relic with a suffix: not pristine");

        // the routing (N = 3, a table of two bonuses; the comparison folds)
        const char* table[2] = {"records/xpack/item/relics/bonus/B_One.dbr",
                                "records\\xpack\\item\\relics\\bonus\\b_two.dbr"};
        int bi = 7;
        check(utRelicRoute(1, 3, "", table, 2, &bi) == kUtRelicRouteRed && bi == -1 &&
                  utRelicRoute(2, 3, nullptr, table, 2, &bi) == kUtRelicRouteRed,
              "route: 1/3 and 2/3 without a bonus -> red");
        check(utRelicRoute(3, 3, "RECORDS\\XPACK\\ITEM\\RELICS\\BONUS\\B_TWO.DBR", table, 2, &bi) ==
                      kUtRelicRouteGreen && bi == 1,
              "route: 3/3 with a bonus in the table (case and slashes folded) -> green, entry 2");
        check(utRelicRoute(3, 3, "records/xpack/item/relics/bonus/b_one.dbr", table, 2, &bi) ==
                      kUtRelicRouteGreen && bi == 0,
              "route: 3/3, entry 1");
        check(utRelicRoute(3, 3, "records/xpack/item/relics/bonus/b_zero.dbr", table, 2, &bi) ==
                      kUtRelicRouteRefuseBonus && bi == -1,
              "route: a bonus the table does not list (a weight-0 entry is not in it) -> refused");
        check(utRelicRoute(4, 3, "", table, 2, nullptr) == kUtRelicRouteRefuseShards &&
                  utRelicRoute(4, 3, table[0], table, 2, nullptr) == kUtRelicRouteRefuseShards &&
                  utRelicRoute(2, 3, table[0], table, 2, nullptr) == kUtRelicRouteRefuseShards &&
                  utRelicRoute(3, 3, "", table, 2, nullptr) == kUtRelicRouteRefuseShards &&
                  utRelicRoute(1, 0, "", table, 2, nullptr) == kUtRelicRouteRefuseShards,
              "route: var1 > N, a partial with a bonus, a completed without one, N 0 -> refused");
        check(utRelicRoute(1, 1, "", table, 2, nullptr) == kUtRelicRouteRefuseShards &&
                  utRelicRoute(1, 1, table[1], table, 2, &bi) == kUtRelicRouteGreen && bi == 1,
              "route: a 1-shard record (a dead red cell) - only the completed item goes in");
        // var1 0 is ONE shard (a dropped piece; the game's InitializeItem sets no level for 0)
        check(utRelicShardsOf(0) == 1 && utRelicShardsOf(1) == 1 && utRelicShardsOf(2) == 2 &&
                  utRelicShardsOf(5) == 5,
              "shards of a piece: var1 0 and 1 are one shard, else var1");
        check(utRelicRoute(0, 3, "", table, 2, &bi) == kUtRelicRouteRed && bi == -1 &&
                  utRelicRoute(0, 3, nullptr, table, 2, nullptr) == kUtRelicRouteRed,
              "route: var1 0 of 3 without a bonus (01_act4_ironwillofajax as dropped) -> red");
        check(utRelicRoute(0, 5, "", table, 2, nullptr) == kUtRelicRouteRed &&
                  utRelicRoute(4, 5, "", table, 2, nullptr) == kUtRelicRouteRed,
              "route: var1 0 of 5 (01_act3_pengclaw as dropped) and a 4/5 piece -> red");
        check(utRelicRoute(0, 3, table[0], table, 2, nullptr) == kUtRelicRouteRefuseShards,
              "route: var1 0 of 3 WITH a bonus (a partial with one) -> refused");
        check(utRelicRoute(0, 1, "", table, 2, nullptr) == kUtRelicRouteRefuseShards &&
                  utRelicRoute(0, 1, table[0], table, 2, &bi) == kUtRelicRouteGreen && bi == 0,
              "route: var1 0 of a 1-shard record is its one shard: completed, green with its bonus");
        check(utRelicFoldSame("A\\b/C.DBR", "a/B\\c.dbr") && !utRelicFoldSame("a/b.dbr", "a/b.db") &&
                  utRelicFoldSame(nullptr, ""),
              "the folded comparison: case and slashes alike, nothing else");

        // the takes' choices on mixed rows
        UtRelicRowFacts rows[9];
        memset(rows, 0, sizeof(rows));
        auto row = [&](int i, unsigned long long seq, unsigned long long dep, unsigned v1, int pend,
                       bool unres, const char* bonus) {
            rows[i].seq = seq;
            rows[i].deposited = dep;
            rows[i].var1 = v1;
            rows[i].pending = pend;
            rows[i].unresolved = unres;
            utRelicFold(bonus, rows[i].bonus, sizeof(rows[i].bonus));
        };
        const char* B1 = "records/b/one.dbr";
        const char* B2 = "records/b/two.dbr";
        row(0, 1, 100, 3, kUtRelicPendIn, false, B1);      // the oldest of B1, but pending in
        row(1, 2, 200, 3, kUtRelicPendNone, false, B1);    // settled
        row(2, 3, 150, 3, kUtRelicPendOut, false, B1);     // out: never shown, never counted
        row(3, 4, 300, 3, kUtRelicPendNone, false, "Records\\B\\Two.DBR");
        row(4, 5, 110, 2, kUtRelicPendNone, false, "");    // a settled 2/3
        row(5, 6, 120, 2, kUtRelicPendIn, false, "");      // a 2/3 pending in
        row(6, 7, 400, 1, kUtRelicPendIn, false, "");      // a 1-shard row pending in
        row(7, 8, 130, 1, kUtRelicPendOut, false, "");     // a shard out
        row(8, 9, 90, 2, kUtRelicPendNone, true, "");      // unresolved: never a source
        check(utRelicGreenChoice(rows, 9, B1) == 1, "green: the settled row before the older one pending in");
        check(utRelicGreenChoice(rows, 9, "RECORDS/B/TWO.DBR") == 3 && utRelicGreenChoice(rows, 9, "records/b/x.dbr") == -1 &&
                  utRelicGreenChoice(rows, 9, "") == -1,
              "green: the slot's own bonus (folded); an empty slot -> -1");
        check(utRelicGreenCount(rows, 9, B1) == 2 && utRelicGreenCount(rows, 9, B2) == 1,
              "green count: rows not pending out");
        check(utRelicRedShards(rows, 9) == 2 + 2 + 1 + 2, "red count: the shards of the partial rows not pending out");
        bool split = true;
        check(utRelicShardSource(rows, 9, &split) == 6 && !split,
              "shard source: a 1-shard row first (taken whole), even pending in");
        rows[6].pending = kUtRelicPendOut;
        check(utRelicShardSource(rows, 9, &split) == 4 && split,
              "shard source: else the oldest SETTLED row, split (the unresolved older one skipped)");
        rows[4].pending = kUtRelicPendOut;
        check(utRelicShardSource(rows, 9, &split) == 5 && split, "shard source: else the oldest row pending in, split");
        rows[5].pending = kUtRelicPendOut;
        check(utRelicShardSource(rows, 9, &split) == -1 && !split, "shard source: nothing left -> -1 (refused)");
        row(0, 21, 500, 1, kUtRelicPendIn, false, "");
        row(1, 22, 600, 1, kUtRelicPendNone, false, "");
        row(2, 23, 50, 1, kUtRelicPendNone, false, "");
        check(utRelicShardSource(rows, 3, &split) == 2 && !split,
              "shard source: among 1-shard rows, settled before pending in, the oldest first");
        row(0, 31, 70, 0, kUtRelicPendNone, false, "");
        check(utRelicShardSource(rows, 1, &split) == 0 && !split,
              "shard source: a row of var1 0 is one shard - handed out whole, never split");
        check(utRelicRedShards(rows, 1) == 1, "red count: a row of var1 0 counts one shard");
        check(utRelicProtoVar1(0) == 0 && utRelicProtoVar1(1) == 1 && utRelicProtoVar1(2) == 1 &&
                  utRelicProtoVar1(4) == 1,
              "the red prototype: a one-shard row as stored (var1 0 stays 0), a split source as var1 1");
        // the logged pieces: two dropped 0/3 pieces, a 4/5 and a dropped 0/5 of another record
        row(0, 51, 10, 0, kUtRelicPendNone, false, "");   // 01_act4_ironwillofajax, var1 0
        row(1, 52, 20, 0, kUtRelicPendIn, false, "");     // the same record, pending in
        row(2, 53, 30, 2, kUtRelicPendNone, false, "");   // a 2/3 settled
        check(utRelicRedShards(rows, 3) == 1 + 1 + 2, "red count: 0/3 + 0/3 + 2/3 = 4 shards");
        check(utRelicShardSource(rows, 3, &split) == 0 && !split,
              "shard source: the settled var1 0 row first (a whole take), before the split of 2/3");
        rows[0].pending = kUtRelicPendOut;
        check(utRelicShardSource(rows, 3, &split) == 1 && !split && utRelicRedShards(rows, 3) == 3,
              "shard source: then the var1 0 row pending in (one shard, whole) before the split of "
              "the settled 2/3; 3 shards left");
        row(0, 61, 10, 4, kUtRelicPendNone, false, "");   // 01_act3_pengclaw 4/5
        row(1, 62, 20, 0, kUtRelicPendNone, false, "");   // 01_act3_pengclaw 0/5
        check(utRelicRedShards(rows, 2) == 5 && utRelicShardSource(rows, 2, &split) == 1 && !split,
              "pengclaw: 4/5 + 0/5 = 5 shards; the take hands out the 0/5 row whole");
        row(0, 41, 70, 2, kUtRelicPendNone, false, "");
        row(1, 40, 70, 2, kUtRelicPendNone, false, "");
        check(utRelicShardSource(rows, 2, &split) == 1 && split, "shard source: equal times go by row order");

        // the relic log line and the CSV cells
        char tail[96], line[400];
        utRelicRouteTail(kUtRelicRouteGreen, "records/b/one.dbr", tail, sizeof(tail));
        utRelicDebugLine(line, sizeof(line), "records/item/relics/r.dbr", 3, 3, "", "records/b/one.dbr", 77, tail);
        check(!strcmp(line, "relic: records/item/relics/r.dbr var1=3 of 3 relic='' relicBonus='records/b/one.dbr' "
                            "seed=77 -> green records/b/one.dbr"),
              "the relic line of a deposit (green)");
        utRelicRouteTail(kUtRelicRouteRed, "", tail, sizeof(tail));
        check(!strcmp(tail, "red"), "... red");
        utRelicRouteTail(kUtRelicRouteRefuseShards, "", tail, sizeof(tail));
        check(!strcmp(tail, "refused (a shard count its record does not allow)"), "... refused");
        utRelicShardTail(12, 1, tail, sizeof(tail));
        utRelicDebugLine(line, sizeof(line), "records/item/relics/r.dbr", 1, 3, "", "", 77, tail);
        check(!strcmp(line, "relic: records/item/relics/r.dbr var1=1 of 3 relic='' relicBonus='' seed=77 -> "
                            "one shard of row 12, 1 left"),
              "the relic line of a shard take");
        char cell[32];
        utRelicShardsCell(2, 3, cell, sizeof(cell));
        check(!strcmp(utRelicTierWord(1), "Normal") && !strcmp(utRelicTierWord(2), "Epic") &&
                  !strcmp(utRelicTierWord(3), "Legendary") && !utRelicTierWord(0)[0] && !strcmp(cell, "2 / 3"),
              "the CSV cells: tier Normal / Epic / Legendary, shards \"2 / 3\"");
        utRelicShardsCell(1, 0, cell, sizeof(cell));
        check(!cell[0], "the CSV shards cell is empty for a record without a shard count");
        utRelicShardsCell(0, 3, cell, sizeof(cell));
        check(!strcmp(cell, "1 / 3"), "the CSV shards cell of a var1 0 row: \"1 / 3\"");
        utRelicRefusalFacts(line, sizeof(line), 4, 3, "", 13134);
        check(!strcmp(line, "var1=4 of 3 relicBonus='' seed=13134"),
              "a refused relic's facts for its one REFUSED line");
    }

    printf("\n2. utTakeDecide\n");
    {
        long ok = 0, total = 0;
        for (unsigned bits = 0; bits < (1u << 8); ++bits)
            for (unsigned rows = 0; rows < 3; ++rows) {
                ut::UtTakeFacts t;
                t.viewOn = (bits & 1u) != 0;
                t.mpKnown = (bits & 2u) != 0;
                t.mp = (bits & 4u) != 0;
                t.tableOwns = (bits & 8u) != 0;
                t.isPrototype = (bits & 16u) != 0;
                t.fromRow = (bits & 32u) != 0;
                t.rowIsNewest = (bits & 64u) != 0;
                t.mpCollect = (bits & 128u) != 0;
                t.rows = rows;
                ++total;
                const bool all = t.viewOn && t.mpKnown && (!t.mp || t.mpCollect) && t.tableOwns &&
                                 t.isPrototype &&
                                 t.fromRow && rows > 0 && t.rowIsNewest;
                if ((ut::utTakeDecide(t) == ut::kUtTakeOk) == all) ++ok;
            }
        check(ok == total, "a take only for a newest-row prototype, rows > 0, writable, and not "
                           "MP unless mp_collect=1");
        ut::UtTakeFacts m = {true, true, true, true, true, true, 1u, true, true};
        const int inMp = ut::utTakeDecide(m);
        m.mpCollect = false;
        const int inMp0 = ut::utTakeDecide(m);
        m.mpCollect = true;
        m.mpKnown = false;
        const int unknown = ut::utTakeDecide(m);
        check(inMp == ut::kUtTakeOk && inMp0 == ut::kUtTakeRefuseMultiplayer &&
                  unknown == ut::kUtTakeRefuseMultiplayer,
              "a take in multiplayer: mp_collect=1 takes, 0 refuses, an unknown state refuses");
        check(ut::utMpMoves(true, false, false) == ut::kUtMpMovesOk &&
                  ut::utMpMoves(true, false, true) == ut::kUtMpMovesOk &&
                  ut::utMpMoves(true, true, true) == ut::kUtMpMovesOk &&
                  ut::utMpMoves(true, true, false) == ut::kUtMpMovesBarred &&
                  ut::utMpMoves(false, false, true) == ut::kUtMpMovesUnknown &&
                  ut::utMpMoves(false, true, false) == ut::kUtMpMovesUnknown,
              "utMpMoves: single / multiplayer / unknown x mp_collect 0 / 1 (GD's rule; unknown "
              "refuses both)");
        ut::UtTakeFacts t = {true, true, false, true, true, false, 0, true};
        check(ut::utTakeDecide(t) == ut::kUtTakeRefuseBare, "an uncollected (bare) prototype: refused");
    }

    printf("\n3. the save set\n");
    {
        char leaf[96];
        check(ut::utSaveSetLeaf(true, "", true, true, leaf, sizeof(leaf)) &&
                  strcmp(leaf, "tq-uniq-items") == 0, "main campaign -> tq-uniq-items");
        check(ut::utSaveSetLeaf(true, "", true, false, leaf, sizeof(leaf)) &&
                  strcmp(leaf, "tq-uniq-items-custom") == 0, "custom, no mod -> -custom");
        check(ut::utSaveSetLeaf(true, "My Quest: Part 2!", false, false, leaf, sizeof(leaf)) &&
                  strncmp(leaf, "tq-uniq-items-my_quest_part_2-", 30) == 0 && strlen(leaf) == 38,
              "a mod name wins (sanitised + a hash of what was lost) even with the quest unknown");
        {   // names that sanitise alike never share a journal; case alone does
            char a[96], b[96], c[96], d[96], e[96];
            const bool built = ut::utSaveSetLeaf(true, "My Mod", true, true, a, sizeof(a)) &&
                               ut::utSaveSetLeaf(true, "my-mod", true, true, b, sizeof(b)) &&
                               ut::utSaveSetLeaf(true, "my_mod", true, true, c, sizeof(c)) &&
                               ut::utSaveSetLeaf(true, "MyMod", true, true, d, sizeof(d)) &&
                               ut::utSaveSetLeaf(true, "mymod", true, true, e, sizeof(e));
            check(built && strcmp(a, b) != 0 && strcmp(a, c) != 0 && strcmp(b, c) != 0,
                  "'My Mod' / 'my-mod' / 'my_mod' -> three different journals");
            check(built && strcmp(d, e) == 0 && strcmp(d, "tq-uniq-items-mymod") == 0,
                  "'MyMod' = 'mymod' (a folder name ignores case), no hash when nothing was lost");
        }
        check(ut::utSaveSetLeaf(true, "Custom", true, true, leaf, sizeof(leaf)) &&
                  strcmp(leaf, "tq-uniq-items-mod_custom") == 0, "a mod called Custom never collides");
        check(!ut::utSaveSetLeaf(false, "", true, true, leaf, sizeof(leaf)), "mod name unreadable -> UNKNOWN");
        check(!ut::utSaveSetLeaf(true, "", false, true, leaf, sizeof(leaf)), "quest unreadable -> UNKNOWN");
        check(!ut::utSaveSetLeaf(true, "???", true, true, leaf, sizeof(leaf)), "a name with nothing usable -> UNKNOWN");
        char longName[200];
        memset(longName, 'a', sizeof(longName));
        longName[199] = 0;
        check(ut::utSaveSetLeaf(true, longName, true, true, leaf, sizeof(leaf)) &&
                  strlen(leaf) == 14 + 48 + 9 && leaf[14 + 48] == '-',
              "a long mod name is cut to 48 characters + a hash of the whole name");
        char longName2[200];
        memcpy(longName2, longName, sizeof(longName2));
        longName2[150] = 'b';
        char leaf2[96];
        check(ut::utSaveSetLeaf(true, longName2, true, true, leaf2, sizeof(leaf2)) &&
                  strcmp(leaf, leaf2) != 0, "two long names sharing 48 characters differ");
    }

    printf("\n4. the store over a real journal\n");
    {
        ut::g_cfg.exportCsv = 0;
        ut::storeInit(nullptr);
        check(!ut::storeTableOwns(), "no set before a world");
        unsigned long long seq = 0;
        const char* base = "records\\item\\equipmentring\\u_n_ring_01.dbr";
        check(!ut::storeOnDeposit(cap(base, 1), &seq), "a deposit without a set is refused");
        check(!ut::storeSelectSet(true, "", false, false), "an unknown set: moves refused");
        char p[MAX_PATH];
        _snprintf_s(p, sizeof(p), _TRUNCATE, "%s\\tq-uniq-items.jsonl", dir);
        DeleteFileA(p);
        check(ut::storeSelectSet(true, "", true, true) && ut::storeTableOwns(), "the main set opens");
        check(ut::storeOnDeposit(cap(base, 1), &seq) && ut::storeOnDeposit(cap(base, 2), &seq),
              "two deposits");
        check(ut::storeCount("Records/Item/EquipmentRing/U_N_Ring_01.dbr") == 2,
              "storeCount folds any spelling: 2 rows");
        check(!ut::storeOnTake(base, seq - 1), "a take of the older row is refused");
        check(!ut::storeTakeAllowed() && !ut::storeOnTake(base, seq) && ut::storeCount(base) == 2,
              "no Player.chr visible -> takes refused (deposits stay)");
        {
            char d[MAX_PATH], f[MAX_PATH];
            CreateDirectoryA(save, nullptr);
            _snprintf_s(d, sizeof(d), _TRUNCATE, "%s\\Main", save);
            CreateDirectoryA(d, nullptr);
            _snprintf_s(d, sizeof(d), _TRUNCATE, "%s\\Main\\_Hero", save);
            CreateDirectoryA(d, nullptr);
            _snprintf_s(f, sizeof(f), _TRUNCATE, "%s\\Player.chr", d);
            touch(f, now() - 600000000ull);
        }
        ut::storeSaveCheck(true);
        check(ut::storeTakeAllowed(), "a Player.chr appears -> takes allowed");
        check(ut::journalPendingIn() == 2, "... and that OLDER save settled nothing");
        check(ut::storeOnTake(base, seq) && ut::storeCount(base) == 1, "the newest is taken: 1 row");
        // the game switch leaves the journal per set alone: the world's save set decides it
        ut::storeNoteEntered("othermod");
        check(ut::storeSelectSet(true, "mymod", true, false) &&
                  !strcmp(ut::journalSetLeaf(), "tq-uniq-items-mymod"),
              "InitializeMod entered another game: the world's set (mymod) opens, a WARN says so");
        ut::storeNoteEntered("mymod");
        check(ut::storeSelectSet(true, "mymod", true, false) &&
                  !strcmp(ut::journalSetLeaf(), "tq-uniq-items-mymod"),
              "entered and loaded agree: the same set");
        ut::storeNoteEntered("");
        check(ut::storeSelectSet(true, "", true, true) && !strcmp(ut::journalSetLeaf(), "tq-uniq-items") &&
                  ut::storeCount(base) == 1,
              "back to the main game: the main set, its row still there");
    }

    printf("\n5. the character-save watch (read only)\n");
    {
        char main[MAX_PATH], chr[MAX_PATH], chr2[MAX_PATH];
        CreateDirectoryA(save, nullptr);
        _snprintf_s(main, sizeof(main), _TRUNCATE, "%s\\Main", save);
        CreateDirectoryA(main, nullptr);
        _snprintf_s(chr, sizeof(chr), _TRUNCATE, "%s\\Main\\_Hero", save);
        CreateDirectoryA(chr, nullptr);
        _snprintf_s(chr2, sizeof(chr2), _TRUNCATE, "%s\\User", save);
        CreateDirectoryA(chr2, nullptr);
        _snprintf_s(chr2, sizeof(chr2), _TRUNCATE, "%s\\User\\_Other", save);
        CreateDirectoryA(chr2, nullptr);
        char f1[MAX_PATH], f2[MAX_PATH];
        _snprintf_s(f1, sizeof(f1), _TRUNCATE, "%s\\Player.chr", chr);
        _snprintf_s(f2, sizeof(f2), _TRUNCATE, "%s\\Player.chr", chr2);
        const unsigned long long old = now() - 600000000ull;   // a minute ago
        touch(f1, old);
        touch(f2, old - 10000000ull);
        unsigned long long t = 0;
        check(ut::storeNewestCharacterSave(&t) && t == old, "the newest Player.chr of Main and User");
        check(ut::journalPendingIn() == 1, "one row pending in (the deposit above, not the taken one)");
        ut::storeSaveCheck(true);
        check(ut::journalPendingIn() == 1, "a save OLDER than the row settles nothing");
        touch(f2, now() + 10000000ull);
        ut::storeSaveCheck(true);
        check(ut::journalPendingIn() == 0, "a newer character save settles it");
    }

    printf("\n6. the right-click take's settle (no cursor: touched at the hand-out)\n");
    {
        unsigned long long seq = 0;
        const char* rc = "records\\item\\equipmentring\\u_n_ring_02.dbr";
        check(ut::storeCount(rc) == 0 && ut::storeOnDeposit(cap(rc, 7), &seq) &&
                  ut::storeCount(rc) == 1,
              "a first copy deposits (the one-copy rule never refuses a first deposit)");
        ut::journalSaveObserved(now() + 10000000ull);
        check(ut::journalPendingIn() == 0, "a newer save settles the deposit");
        const size_t out0 = ut::journalPendingOut();
        check(ut::storeOnTake(rc, seq) && ut::storeCount(rc) == 0 &&
                  ut::journalPendingOut() == out0 + 1,
              "the right-click take journals the row out first (viewTakeRightClick, G7)");
        check(ut::journalTouchOut(seq), "the hand-out touches the out row (viewTakeRemove)");
        ut::journalSaveObserved(now() - 10000000ull);
        check(ut::journalPendingOut() == out0 + 1, "a save OLDER than the hand-out settles nothing");
        ut::journalSaveObserved(now() + 20000000ull);
        check(ut::journalPendingOut() == 0 && ut::storeCount(rc) == 0,
              "a newer save settles it: the item is in the character file, no row, no duplicate");
    }

    printf("\n7. a monster infrequent's stored roll, named (GD's utRollDescribe words)\n");
    {
        ut::UtRollKey ra;
        ut::utRollSet("records/xpack/item/lootmagicalaffixes/prefix/default/prefix_ar_01.dbr",
                      "Records\\XPack\\Item\\LootMagicalAffixes\\Suffix\\suffix_dmg_fire_05.dbr",
                      2115467035u, &ra);
        char line[640];
        ut::utRollDescribe(ra, "Machae's Helm", 32, line, sizeof(line));
        check(strcmp(line, "\"Machae's Helm\" level 32: prefix prefix_ar_01, suffix "
                           "suffix_dmg_fire_05, seed 2115467035") == 0,
              "name, item level, both affixes by their record's leaf, the seed");
        ut::UtRollKey bare;
        ut::utRollSet("", "", 7u, &bare);
        ut::utRollDescribe(bare, "", 0, line, sizeof(line));
        check(strcmp(line, "no prefix, no suffix, seed 7") == 0,
              "an empty affix reads no prefix / no suffix; no name and no level: no head");
        ut::utRollDescribe(ra, "", 32, line, sizeof(line));
        check(strncmp(line, "level 32: prefix prefix_ar_01", 29) == 0, "a level without a name");
        ut::UtRollKey unk;
        ut::utRollClear(&unk);
        ut::utRollDescribe(unk, "X", 0, line, sizeof(line));
        check(strcmp(line, "\"X\": roll not readable") == 0, "an unknown roll says so");

        ut::UtRollKey rb;
        ut::utRollSet("RECORDS\\XPACK\\ITEM\\LOOTMAGICALAFFIXES\\PREFIX\\DEFAULT\\PREFIX_AR_01.DBR",
                      "records/xpack/item/lootmagicalaffixes/suffix/suffix_dmg_fire_05.dbr",
                      2115467035u, &rb);
        check(ut::utRollSame(ra, rb), "case and separators do not make another roll");
        rb.seed = 2115467036u;
        check(!ut::utRollSame(ra, rb), "another seed is another roll");
        ut::utRollSet("", ra.suffix, ra.seed, &rb);
        check(!ut::utRollSame(ra, rb), "no prefix against a prefix is another roll");
        check(!ut::utRollSame(unk, unk), "an unknown roll never matches, not even itself");

        char why[640];
        ut::utRollDescribe(ra, "Machae's Helm", 32, line, sizeof(line));
        ut::utDepositWhyRoll(line, why, sizeof(why));
        check(strcmp(why, "a monster infrequent keeps ONE roll per record and this box already "
                          "holds it - \"Machae's Helm\" level 32: prefix prefix_ar_01, suffix "
                          "suffix_dmg_fire_05, seed 2115467035. Take that one out first to store "
                          "this roll") == 0 &&
                  strstr(why, "max_per_record") == nullptr,
              "the refusal's words: GD's, naming the stored roll, without max_per_record");
        check(strcmp(ut::utDepositWhyText(ut::kUtDepRefuseHave), "already in the collection") == 0,
              "the verdict's own text is unchanged (a unique's refusal reads as before)");
        ut::utDepositWhyRoll("", why, sizeof(why));
        check(strstr(why, "already holds it - roll not readable.") != nullptr,
              "no roll text: the reason says it is not readable");

        unsigned long long seq = 0;
        const char* mi = "records\\xpack\\item\\equipmentarmor\\helm\\mi_l_machaemelee.dbr";
        ut::UtReplicaCapture c = cap(mi, 2115467035u);
        _snprintf_s(c.str[ut::kUtIdPrefix], ut::kUtIdStrMax, _TRUNCATE, "%s", ra.prefix);
        _snprintf_s(c.str[ut::kUtIdSuffix], ut::kUtIdStrMax, _TRUNCATE, "%s", ra.suffix);
        ut::UtRollKey got;
        check(!ut::storeRollOf(mi, &got) && !got.known, "no row: no stored roll");
        check(ut::storeOnDeposit(c, &seq) &&
                  ut::storeRollOf("RECORDS/XPACK/ITEM/EQUIPMENTARMOR/HELM/MI_L_MACHAEMELEE.DBR",
                                  &got) &&
                  ut::utRollSame(got, ra) && got.seed == 2115467035u &&
                  strcmp(got.suffix, ra.suffix) == 0,
              "the deposited row's roll comes back through storeRollOf (any spelling of the record)");
    }

    printf("\n8. the KIND table over the oracle's catalogue.bin, and the refusal's words built on it\n");
    {
        const char* oracleDir = argc > 2 ? argv[2] : ".";
        char binPath[520], jsonPath[520];
        _snprintf_s(binPath, sizeof(binPath), _TRUNCATE, "%s\\catalogue.bin", oracleDir);
        _snprintf_s(jsonPath, sizeof(jsonPath), _TRUNCATE, "%s\\catalogue.json", oracleDir);
        std::string miErr;
        check(ut::utMiLoad("no-such-folder\\catalogue.bin", &miErr) == nullptr && !miErr.empty(),
              "an unreadable catalogue.bin: no table, and the reason");
        check(ut::utMiKind(nullptr, "records/xpack/item/equipmentarmor/helm/mi_l_machaemelee.dbr") ==
                      -1 &&
                  ut::utMiCount(nullptr) == -1,
              "no table: the KIND is -1 (unknown) and the count -1");
        ut::UtMiTable* miTab = ut::utMiLoad(binPath, &miErr);
        check(miTab != nullptr, "the oracle's catalogue.bin loads");
        std::string miJson;
        FILE* jf = nullptr;
        if (fopen_s(&jf, jsonPath, "rb") == 0 && jf) {
            char chunk[4096];
            for (;;) {
                const size_t got = fread(chunk, 1, sizeof(chunk), jf);
                if (!got) break;
                miJson.append(chunk, got);
            }
            fclose(jf);
        }
        int miExpect = 0;
        for (size_t at = miJson.find("\"isMi\": true"); at != std::string::npos;
             at = miJson.find("\"isMi\": true", at + 1))
            ++miExpect;
        printf("  (the table holds %d, catalogue.json marks %d)\n", ut::utMiCount(miTab), miExpect);
        check(miExpect > 0 && ut::utMiCount(miTab) == miExpect,
              "the table holds every record the oracle's catalogue.json marks isMi, and no other");

        const char* helmRec = "records/xpack/item/equipmentarmor/helm/mi_l_machaemelee.dbr";
        char miName[192];
        int miLevel = -1;
        check(ut::utMiKind(miTab, helmRec) == 1 &&
                  ut::utMiFacts(miTab, helmRec, miName, sizeof(miName), &miLevel) &&
                  strcmp(miName, "Alala") == 0 && miLevel == 69,
              "a monster infrequent: KIND 1, its catalogue name and item level");
        check(ut::utMiKind(miTab, "RECORDS\\XPACK\\ITEM\\EQUIPMENTARMOR\\HELM\\MI_L_MACHAEMELEE.DBR") == 1 &&
                  ut::utMiKind(miTab, "Records\\XPack\\Item\\EquipmentArmor\\Helm\\mi_l_machaemelee.dbr") == 1,
              "any spelling of it (upper case, backslashes): KIND 1");
        check(ut::utMiKind(miTab, "records/item/equipmentamulet/u_e_amaranthine.dbr") == 0 &&
                  !ut::utMiFacts(miTab, "records/item/equipmentamulet/u_e_amaranthine.dbr", miName,
                                 sizeof(miName), &miLevel) &&
                  miName[0] == 0 && miLevel == 0,
              "a unique: KIND 0, no facts");
        check(ut::utMiKind(miTab, "records/item/none/not_a_record.dbr") == 0 &&
                  ut::utMiKind(miTab, "") == 0 && ut::utMiKind(miTab, nullptr) == 0,
              "not a catalogue record, empty, null: KIND 0");

        char miWhy[640];
        ut::utMiRollWhy(miTab, "Records\\XPack\\Item\\EquipmentArmor\\Helm\\MI_L_MachaeMelee.dbr", miWhy,
                        sizeof(miWhy));
        check(strcmp(miWhy, "a monster infrequent keeps ONE roll per record and this box already holds "
                            "it - \"Alala\" level 69: prefix prefix_ar_01, suffix suffix_dmg_fire_05, "
                            "seed 2115467035. Take that one out first to store this roll") == 0,
              "the assembled reason: the catalogue's name and level, the newest row's roll");
        check(ut::utMiRollWhy(miTab, "records/item/equipmentamulet/u_e_amaranthine.dbr", miWhy,
                              sizeof(miWhy))[0] == 0 &&
                  ut::utMiRollWhy(nullptr, helmRec, miWhy, sizeof(miWhy))[0] == 0,
              "a unique, or no table: no roll words (the refusal reads as before)");

        const char* torsoRec = "records/xpack/item/equipmentarmor/torso/mi_l_machaemelee.dbr";
        ut::utMiRollWhy(miTab, torsoRec, miWhy, sizeof(miWhy));
        check(strcmp(miWhy, "a monster infrequent keeps ONE roll per record and this box already holds "
                            "it - \"Homados\" level 69: roll not readable. Take that one out first "
                            "to store this roll") == 0,
              "a monster infrequent without a row: the roll is not readable");

        unsigned long long seqA = 0, seqB = 0;
        ut::UtReplicaCapture capA = cap(torsoRec, 11u);
        _snprintf_s(capA.str[ut::kUtIdPrefix], ut::kUtIdStrMax, _TRUNCATE, "%s",
                    "records/xpack/item/lootmagicalaffixes/prefix/default/prefix_ar_02.dbr");
        ut::UtRollKey rollGot;
        check(ut::storeOnDeposit(capA, &seqA) && ut::storeRollOf(torsoRec, &rollGot) && rollGot.seed == 11u,
              "one roll deposited: it is the stored roll");
        check(ut::storeTakeAllowed() && ut::storeOnTake(torsoRec, seqA) &&
                  !ut::storeRollOf(torsoRec, &rollGot),
              "taken out: no stored roll");
        ut::UtReplicaCapture capB = cap(torsoRec, 22u);
        _snprintf_s(capB.str[ut::kUtIdSuffix], ut::kUtIdStrMax, _TRUNCATE, "%s",
                    "records/xpack/item/lootmagicalaffixes/suffix/suffix_dmg_cold_03.dbr");
        check(ut::storeOnDeposit(capB, &seqB) && ut::storeRollOf(torsoRec, &rollGot) &&
                  rollGot.seed == 22u && rollGot.prefix[0] == 0,
              "another roll deposited after the take is the stored roll now");
        ut::utMiRollWhy(miTab, torsoRec, miWhy, sizeof(miWhy));
        check(strstr(miWhy, "\"Homados\" level 69: no prefix, suffix suffix_dmg_cold_03, seed 22. ") !=
                  nullptr,
              "and the reason names that roll");

        // an arcane formula: the replica is the record path and its seed, every other string
        // empty; the row goes through the same journal path as any other class
        const char* formulaRec = "records/xpack/item/artifacts/arcaneformulae/l_da_eyeofra_formula.dbr";
        unsigned long long seqF = 0, seqBack = 0;
        ut::UtReplicaCapture capF = cap(formulaRec, 7u);
        ut::UtReplicaCapture backF;
        memset(&backF, 0, sizeof(backF));
        char whyF[200] = "";
        check(ut::utCaptureValid(capF, whyF, sizeof(whyF)) && ut::storeOnDeposit(capF, &seqF) &&
                  ut::journalNewest(capF.record, &backF, &seqBack) && seqBack == seqF &&
                  ut::utReplicaSame(backF, capF),
              "an arcane formula's row round-trips: deposited, read back as the same replica");
        check(ut::storeTakeAllowed() && ut::storeOnTake(formulaRec, seqF),
              "and the formula is taken out through the same path");
    }
    {   // the relic takes through the store: three rows of one record, a shard and a whole take
        const char* relRec = "Records\\XPack\\Item\\Relics\\01_Act4_Test.dbr";
        ut::UtReplicaCapture p = cap(relRec, 5u);
        p.var1 = 2;   // a 2/3 piece
        ut::UtReplicaCapture g = cap(relRec, 6u);
        g.var1 = 3;
        _snprintf_s(g.str[ut::kUtIdRelicBonus], ut::kUtIdStrMax, _TRUNCATE, "%s", "records/b/one.dbr");
        unsigned long long sp = 0, sg = 0, sg2 = 0, shard = 0;
        check(ut::storeOnDeposit(p, &sp) && ut::storeOnDeposit(g, &sg) && ut::storeOnDeposit(g, &sg2) &&
                  ut::storeCount(relRec) == 3,
              "three rows of one relic record go in (a partial and two copies of one roll)");
        unsigned left = 9;
        check(!ut::storeOnTakeShard(relRec, sg, &shard, &left) && shard == 0,
              "a shard take naming a green row: refused (stale)");
        check(ut::storeOnTakeShard(relRec, sp, &shard, &left) && shard != 0 && shard != sp && left == 1u,
              "a shard of the 2/3 row: split, one shard handed out, 1 left");
        check(!ut::storeOnTakeRow(relRec, sg2) && ut::storeOnTakeRow(relRec, sg),
              "a whole green take: the newest copy refused (stale), the oldest taken");
    }

    ut::journalOpenSet(nullptr);
    ut::logShutdown();
    printf("\n%s - %d passed, %d failed\n", g_fail ? "STORE TESTS FAILED" : "ALL PASS", g_pass,
           g_fail);
    return g_fail ? 1 : 0;
}
