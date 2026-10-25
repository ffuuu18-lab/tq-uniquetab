// test_journal.cpp - the journal (src\ut_rescue.cpp, format TQ-1), offline. NO GAME NEEDED.
//
// TQ port of the Grim Dawn mod's tools\test_journal.cpp. GD's harness
// rules are kept: the harness runs ENTIRELY inside build\test (the folder in argv[1] becomes
// %UNIQUETAB_OUT% BEFORE the first path call, so the mod-folder resolver can never reach a real
// installation); every case uses its own save-set leaf; nothing launches the game.
//
// What it proves: the format gate (a newer format and a foreign header
// are READ-ONLY and the bytes on disk stay untouched), the atomic write (no .tmp left, the file
// always parses), the folded key, duplicate identities kept as separate rows, a take removes the
// NEWEST row, the crash window (pending in / out, the restore at load, the save observation, the
// cancel of an unsaved take), the round trip of every field, the capture validation, a share-locked
// file, a bad line costing one row with the copy aside, and the CSV export.
#include <windows.h>

#include <stdio.h>
#include <string.h>

#include <string>

#include "../src/ut_depositgate.h"   // utJournalStack
#include "../src/ut_folders.h"       // the one-time move of a mod's journal
#include "../src/ut_log.h"
#include "../src/ut_rescue.h"
#include "../src/ut_relicroute.h"   // the relic / charm choices the takes make
#define UT_PROTO_PURE_ONLY
#include "../src/ut_proto.h"          // the take's row -> replica -> row (utReplicaFromIdentity)

#include <vector>

namespace {

int g_pass = 0, g_fail = 0;
char g_dir[MAX_PATH];

void check(bool ok, const char* what) {
    if (ok) {
        ++g_pass;
        printf("  [ok]   %s\n", what);
    } else {
        ++g_fail;
        printf("  [FAIL] %s\n", what);
    }
}

std::string readAll(const char* path) {
    std::string s;
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || !f) return s;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
    fclose(f);
    return s;
}

void writeAll(const char* path, const std::string& s) {
    FILE* f = nullptr;
    if (fopen_s(&f, path, "wb") != 0 || !f) return;
    fwrite(s.data(), 1, s.size(), f);
    fclose(f);
}

void pathOf(const char* leaf, const char* ext, char* out) {
    _snprintf_s(out, MAX_PATH, _TRUNCATE, "%s\\%s.%s", g_dir, leaf, ext);
}

void clean(const char* leaf) {
    char p[MAX_PATH];
    pathOf(leaf, "jsonl", p);
    DeleteFileA(p);
    pathOf(leaf, "csv", p);
    DeleteFileA(p);
    char glob[MAX_PATH];
    _snprintf_s(glob, sizeof(glob), _TRUNCATE, "%s\\%s.jsonl.bad-*", g_dir, leaf);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(glob, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            char q[MAX_PATH];
            _snprintf_s(q, sizeof(q), _TRUNCATE, "%s\\%s", g_dir, fd.cFileName);
            DeleteFileA(q);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
}

int countBad(const char* leaf) {
    char glob[MAX_PATH];
    _snprintf_s(glob, sizeof(glob), _TRUNCATE, "%s\\%s.jsonl.bad-*", g_dir, leaf);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(glob, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int n = 0;
    do ++n; while (FindNextFileA(h, &fd));
    FindClose(h);
    return n;
}

bool exists(const char* p) { return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES; }

ut::UtReplicaCapture cap(const char* base, unsigned seed, const char* prefix = "",
                         const char* relic = "") {
    ut::UtReplicaCapture c;
    memset(&c, 0, sizeof(c));
    _snprintf_s(c.str[ut::kUtIdBase], ut::kUtIdStrMax, _TRUNCATE, "%s", base);
    _snprintf_s(c.str[ut::kUtIdPrefix], ut::kUtIdStrMax, _TRUNCATE, "%s", prefix);
    _snprintf_s(c.str[ut::kUtIdRelic], ut::kUtIdStrMax, _TRUNCATE, "%s", relic);
    c.seed = seed;
    c.var1 = 0;
    c.var2 = 0;
    c.b8 = 0;
    c.stack = 1;
    ut::utJournalKey(base, c.record, sizeof(c.record));
    return c;
}

bool sameCap(const ut::UtReplicaCapture& a, const ut::UtReplicaCapture& b) {
    for (int k = 0; k < ut::kUtIdStrCount; ++k)
        if (strcmp(a.str[k], b.str[k]) != 0) return false;
    return strcmp(a.record, b.record) == 0 && a.seed == b.seed && a.var1 == b.var1 &&
           a.var2 == b.var2 && a.b8 == b.b8 && a.stack == b.stack;
}

unsigned long long now() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

const char* kRing = "records\\item\\equipmentring\\u_n_ring_01.dbr";
const char* kRingKey = "records/item/equipmentring/u_n_ring_01.dbr";

// ---- the all-items file (tools\make_all_items.py) --------------------------------------------
// The generator's seed: FNV-1a (32 bit) over the folded key, folded into 1..32767.
unsigned allItemsSeed(const char* key) {
    unsigned h = 0x811C9DC5u;
    for (const unsigned char* p = (const unsigned char*)key; *p; ++p) h = (h ^ *p) * 0x01000193u;
    return (h % 0x7FFFu) + 1u;
}

// The JSON text appendRowLine writes for one string (the same escaping, for the ASCII the reader
// accepts: printable only, so only '"' and '\\' are escaped).
std::string jsonText(const char* s) {
    std::string o = "\"";
    for (; *s; ++s) {
        if (*s == '"' || *s == '\\') o.push_back('\\');
        o.push_back(*s);
    }
    return o + "\"";
}

// The identity part of a row line, exactly as appendRowLine writes it (stack .. b8).
std::string identityText(const ut::UtReplicaCapture& c) {
    char num[160];
    std::string o;
    _snprintf_s(num, sizeof(num), _TRUNCATE, "\"stack\":%u", c.stack);
    o += num;
    for (int k = 0; k < ut::kUtIdStrCount; ++k)
        o += "," + jsonText(ut::kUtIdStrKey[k]) + ":" + jsonText(c.str[k]);
    _snprintf_s(num, sizeof(num), _TRUNCATE, ",\"seed\":%u,\"var1\":%u,\"var2\":%u,\"b8\":%u", c.seed,
                c.var1, c.var2, c.b8);
    return o + num;
}

// A generated row line, byte for byte what make_all_items.py writes for `base` (backslashes).
std::string allItemsRow(const char* base, const char* stamp) {
    char key[260];
    ut::utJournalKey(base, key, sizeof(key));
    char num[96];
    _snprintf_s(num, sizeof(num), _TRUNCATE, ",\"seed\":%u,\"var1\":0,\"var2\":0,\"b8\":0}\n",
                allItemsSeed(key));
    return "{\"record\":" + jsonText(key) + ",\"deposited\":\"" + stamp + "\",\"stack\":1,\"base\":" +
           jsonText(base) + ",\"prefix\":\"\",\"suffix\":\"\",\"relic\":\"\",\"relicBonus\":\"\"," +
           "\"relic2\":\"\",\"relicBonus2\":\"\"" + num;
}

// The take's path for the NEWEST row of `key`: row -> replica bytes (utReplicaFromIdentity, what
// the take hands Item::CreateItem) -> identity read back out of the bytes -> the same row.
bool takeRoundTrip(const char* key, ut::UtReplicaCapture* row) {
    unsigned long long seq = 0;
    if (!ut::journalNewest(key, row, &seq)) return false;
    ut::UtReplica rep;
    ut::UtReplicaCapture back;
    if (!ut::utReplicaFromIdentity(&rep, *row) || !ut::utReplicaReadIdentity(rep.bytes, &back))
        return false;
    back.stack = row->stack;   // the stack is the row's, not the replica's
    return sameCap(*row, back) && strcmp(back.record, key) == 0;
}

// The raw text of a string value `"name":"..."` in a flat line (escapes undone), or "".
std::string fieldText(const std::string& line, const char* name) {
    const std::string pat = std::string("\"") + name + "\":\"";
    size_t p = line.find(pat);
    if (p == std::string::npos) return std::string();
    std::string v;
    for (p += pat.size(); p < line.size() && line[p] != '"'; ++p) {
        if (line[p] == '\\' && p + 1 < line.size()) ++p;
        v.push_back(line[p]);
    }
    return v;
}

unsigned headerNumber(const std::string& line, const char* name) {
    const std::string pat = std::string("\"") + name + "\":";
    const size_t p = line.find(pat);
    return p == std::string::npos ? 0xFFFFFFFFu
                                  : (unsigned)strtoul(line.c_str() + p + pat.size(), nullptr, 10);
}

// test_journal <dir> --load <file>: a COPY of the file is opened as a save set in <dir> through the
// mod's own reader; the rows, collected, pending and dropped counts against the file's own, and
// the take's round trip for the newest row of every record (identity fields exact against the
// file's bytes). Exit 0 only when nothing was dropped and every record round-trips.
int loadMode(const char* file) {
    const std::string text = readAll(file);
    if (text.empty()) {
        printf("load: %s is missing or empty\n", file);
        return 1;
    }
    std::vector<std::string> lines;
    for (size_t at = 0; at < text.size();) {
        size_t nl = text.find('\n', at);
        if (nl == std::string::npos) nl = text.size();
        std::string l = text.substr(at, nl - at);
        while (!l.empty() && l.back() == '\r') l.pop_back();
        if (l.find_first_not_of(" \t") != std::string::npos) lines.push_back(l);
        at = nl + 1;
    }
    const size_t fileRows = lines.empty() ? 0 : lines.size() - 1;
    const char* leaf = "tq-uniq-items-load";
    clean(leaf);
    char path[MAX_PATH];
    pathOf(leaf, "jsonl", path);
    writeAll(path, text);
    const bool opened = ut::journalOpenSet(leaf) && ut::journalUsable();
    const size_t rows = ut::journalCount();
    const unsigned collected = ut::journalCollectedTotal();
    const size_t pin = ut::journalPendingIn(), pout = ut::journalPendingOut();
    printf("load: %s\n", file);
    printf("  file    %u rows; header entries %u collected %u pendingIn %u pendingOut %u\n",
           (unsigned)fileRows, headerNumber(lines[0], "entries"), headerNumber(lines[0], "collected"),
           headerNumber(lines[0], "pendingIn"), headerNumber(lines[0], "pendingOut"));
    printf("  read    %s; %u rows, %u collected, %u pending in, %u pending out\n",
           opened ? "opened" : "NOT OPENED", (unsigned)rows, collected, (unsigned)pin, (unsigned)pout);
    // Per record: the lines naming it, the newest (last) one, and whether the reader kept them all.
    std::vector<std::string> keys;
    std::vector<size_t> newest, nLines;
    size_t firstDropped = 0, unnamed = 0;
    for (size_t i = 1; i < lines.size(); ++i) {
        const std::string k = fieldText(lines[i], "record");
        if (k.empty()) {
            if (!firstDropped) firstDropped = i + 1;
            ++unnamed;
            continue;
        }
        size_t j = 0;
        while (j < keys.size() && keys[j] != k) ++j;   // a few thousand keys: linear is fine
        if (j == keys.size()) {
            keys.push_back(k);
            newest.push_back(i);
            nLines.push_back(0);
        }
        newest[j] = i;
        ++nLines[j];
    }
    size_t same = 0, shortKeys = 0;
    for (size_t j = 0; j < keys.size(); ++j) {
        if (ut::journalRows(keys[j].c_str()) < nLines[j]) {
            ++shortKeys;
            if (!firstDropped || newest[j] + 1 < firstDropped) firstDropped = newest[j] + 1;
        }
        ut::UtReplicaCapture row;
        if (takeRoundTrip(keys[j].c_str(), &row) &&
            lines[newest[j]].find("," + identityText(row)) != std::string::npos)
            ++same;
    }
    const size_t dropped = fileRows - (rows < fileRows ? rows : fileRows);
    printf("  dropped %u%s", (unsigned)dropped, (dropped || shortKeys || unnamed) ? "" : "\n");
    if (dropped || shortKeys || unnamed)
        printf(" - the first at line %u: %.160s\n", (unsigned)firstDropped,
               firstDropped ? lines[firstDropped - 1].c_str() : "?");
    printf("  records %u; the take's round trip (row -> replica -> row, identity exact against the "
           "file's bytes): %u of %u\n",
           (unsigned)keys.size(), (unsigned)same, (unsigned)keys.size());
    ut::journalOpenSet(nullptr);
    const bool ok = opened && rows == fileRows && dropped == 0 && shortKeys == 0 && unnamed == 0 &&
                    headerNumber(lines[0], "entries") == rows &&
                    headerNumber(lines[0], "collected") == collected && same == keys.size();
    printf("%s\n", ok ? "LOAD OK" : "LOAD FAILED");
    return ok ? 0 : 1;
}

}  // namespace

// The seeding tool's output (tools\seed_relic_rows.py) through the reader: every row of the file
// kept (no copy aside), and each named record's partial rows settled and counted in shards by the
// red slot's own count. `kv` = key, shards, key, shards, ...
int shardsMode(const char* file, int n, char** kv) {
    const std::string text = readAll(file);
    if (text.empty()) {
        printf("shards: %s is missing or empty\n", file);
        return 1;
    }
    size_t lines = 0;
    for (size_t at = 0; at < text.size();) {
        size_t nl = text.find('\n', at);
        if (nl == std::string::npos) nl = text.size();
        if (text.find_first_not_of(" \t\r", at) < nl) ++lines;
        at = nl + 1;
    }
    const size_t fileRows = lines ? lines - 1 : 0;
    const char* leaf = "tq-uniq-items-seed";
    clean(leaf);
    char path[MAX_PATH];
    pathOf(leaf, "jsonl", path);
    writeAll(path, text);
    int fails = 0;
    const bool opened = ut::journalOpenSet(leaf) && ut::journalUsable();
    const std::string head = text.substr(0, text.find('\n'));
    const bool whole = opened && ut::journalCount() == fileRows &&
                       headerNumber(head, "entries") == (unsigned)fileRows;
    printf("  %s %s: %u rows in the file, header entries %u, %u read\n", whole ? "PASS" : "FAIL", file,
           (unsigned)fileRows, headerNumber(head, "entries"), (unsigned)ut::journalCount());
    if (!whole) ++fails;
    static ut::UtRelicRowFacts fx[512];
    for (int i = 0; i + 1 < n; i += 2) {
        const size_t got = ut::journalRowsOf(kv[i], fx, 512);
        const size_t have = got < 512 ? got : 512;
        unsigned settled = 0;
        for (size_t j = 0; j < have; ++j)
            if (fx[j].pending == ut::kUtRelicPendNone && !fx[j].bonus[0]) ++settled;
        const unsigned shards = ut::utRelicRedShards(fx, (int)have);
        const unsigned want = (unsigned)strtoul(kv[i + 1], nullptr, 10);
        const bool good = got > 0 && settled == (unsigned)got && shards == want;
        printf("  %s %s: %u row(s), %u settled partial, red count %u shard(s) (expected %u)\n",
               good ? "PASS" : "FAIL", kv[i], (unsigned)got, settled, shards, want);
        if (!good) ++fails;
    }
    if (fails) printf("SHARDS FAILED (%d)\n", fails);
    else printf("SHARDS PASSED\n");
    return fails ? 1 : 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: test_journal <empty folder under build\\test>\n");
        return 2;
    }
    _snprintf_s(g_dir, sizeof(g_dir), _TRUNCATE, "%s", argv[1]);
    CreateDirectoryA(g_dir, nullptr);
    SetEnvironmentVariableA("UNIQUETAB_OUT", g_dir);   // BEFORE any path call
    wchar_t logPath[MAX_PATH];
    _snwprintf_s(logPath, MAX_PATH, _TRUNCATE, L"%S\\test_journal.log", g_dir);
    ut::logInit(logPath);
    ut::logSetLevel("debug");
    if (argc >= 6 && strcmp(argv[2], "--shards") == 0) {
        const bool init = ut::journalInit(nullptr);
        const int rc = init ? shardsMode(argv[3], argc - 4, argv + 4) : 1;
        ut::logShutdown();
        return rc;
    }
    if (argc >= 4 && strcmp(argv[2], "--load") == 0) {
        const bool init = ut::journalInit(nullptr);
        const int rc = init ? loadMode(argv[3]) : 1;
        ut::logShutdown();
        return rc;
    }
    check(ut::journalInit(nullptr), "journalInit resolves the folder and opens no file");

    printf("\nA. no save set: nothing is usable, nothing is written\n");
    check(!ut::journalSetKnown() && !ut::journalUsable(), "no set -> not usable");
    {
        unsigned long long seq = 0;
        bool rb = false;
        check(!ut::journalDepositCommit(cap(kRing, 5), &seq, &rb), "a deposit is refused");
        check(ut::journalRows(kRingKey) == 0 && ut::journalCount() == 0, "no row appeared");
    }
    check(!ut::journalOpenSet("Bad Leaf!"), "an unsafe set name is refused (unknown set)");
    check(!ut::journalUsable(), "and nothing is usable");

    printf("\nB. a first deposit: the file, its header, the pending row\n");
    clean("tq-uniq-items-ta");
    char path[MAX_PATH];
    pathOf("tq-uniq-items-ta", "jsonl", path);
    check(ut::journalOpenSet("tq-uniq-items-ta") && ut::journalUsable(), "set opened, usable");
    check(!exists(path), "no file before the first write");
    {
        unsigned long long seq = 0;
        bool rb = true;
        const bool ok = ut::journalDepositCommit(cap(kRing, 12345, "records\\item\\lootmagicalaffixes\\prefix\\a.dbr"), &seq, &rb);
        check(ok && seq != 0 && !rb, "deposit committed");
        const std::string t = readAll(path);
        check(t.find("{\"journal\":\"titan quest uniquetab\",\"format\":1,") == 0,
              "line 1 names Titan Quest and format 1");
        check(t.find("\"set\":\"tq-uniq-items-ta\"") != std::string::npos, "the header names the set");
        check(t.find("\"record\":\"records/item/equipmentring/u_n_ring_01.dbr\"") != std::string::npos,
              "the row's key is the folded base");
        check(t.find("\"base\":\"records\\\\item\\\\equipmentring\\\\u_n_ring_01.dbr\"") != std::string::npos,
              "the raw base is kept byte-exact (backslashes escaped)");
        check(t.find("\"pending\":\"in\"") != std::string::npos, "the row is pending (no save seen)");
        char tmp[MAX_PATH];
        _snprintf_s(tmp, sizeof(tmp), _TRUNCATE, "%s.tmp", path);
        check(!exists(tmp), "atomic write: no .tmp left behind");
        check(ut::journalRows(kRingKey) == 1 && ut::journalCollectedTotal() == 1, "rows = 1");
    }

    printf("\nC. the folded key: case and path separators\n");
    {
        check(ut::journalRows("records/item/equipmentring/u_n_ring_01.dbr") == 1, "the folded key finds it");
        char k[256];
        check(ut::utJournalKey("Records\\Item\\EquipmentRing\\U_N_Ring_01.DBR", k, sizeof(k)) &&
                  strcmp(k, kRingKey) == 0, "any spelling folds to the same key");
        unsigned long long seq = 0;
        bool rb = false;
        check(ut::journalDepositCommit(cap("Records\\Item\\EquipmentRing\\U_N_Ring_01.DBR", 7), &seq, &rb),
              "a deposit spelled differently");
        check(ut::journalRows(kRingKey) == 2, "... counts under the same key (2 rows)");
        const std::string t = readAll(path);
        check(t.find("\"base\":\"Records\\\\Item\\\\EquipmentRing\\\\U_N_Ring_01.DBR\"") != std::string::npos,
              "and its raw base keeps its own spelling");
    }

    printf("\nD. duplicate identities are separate rows\n");
    {
        unsigned long long s1 = 0, s2 = 0;
        bool rb = false;
        check(ut::journalDepositCommit(cap(kRing, 999), &s1, &rb) &&
                  ut::journalDepositCommit(cap(kRing, 999), &s2, &rb) && s1 != s2,
              "the same identity deposited twice");
        check(ut::journalRows(kRingKey) == 4, "4 rows, never merged");
        check(ut::journalOpenSet("tq-uniq-items-tb") && ut::journalOpenSet("tq-uniq-items-ta"),
              "switch set and back (re-read from disk)");
        check(ut::journalRows(kRingKey) == 4 && ut::journalCount() == 4, "still 4 rows after the re-read");
    }

    printf("\nE. a take removes the NEWEST row; a stale take is refused\n");
    {
        ut::UtReplicaCapture n;
        unsigned long long seq = 0;
        check(ut::journalNewest(kRingKey, &n, &seq) && n.seed == 999, "the newest row is the last deposit");
        bool rb = false;
        check(!ut::journalTakeCommit(kRingKey, seq - 1, &rb), "a take of an older row is refused (stale page)");
        check(ut::journalRows(kRingKey) == 4, "... nothing changed");
        check(ut::journalTakeCommit(kRingKey, seq, &rb), "the take of the newest row");
        check(ut::journalRows(kRingKey) == 3, "rows 4 -> 3");
        check(ut::journalCount() == 3, "an UNSAVED deposit taken back is erased (both states agree)");
        unsigned long long seq2 = 0;
        check(ut::journalNewest(kRingKey, &n, &seq2) && n.seed == 999 && seq2 != seq,
              "the next newest is the other seed-999 copy");
    }

    printf("\nF. the crash window: a save settles; an unsaved take is restored at load\n");
    {
        check(ut::journalPendingIn() == 3, "3 rows pending in");
        check(ut::journalSaveObserved(now() + 10) == 3, "a character save after them settles all 3");
        check(ut::journalPendingIn() == 0, "none pending");
        ut::journalFlushNow();
        ut::UtReplicaCapture n;
        unsigned long long seq = 0;
        ut::journalNewest(kRingKey, &n, &seq);
        bool rb = false;
        check(ut::journalTakeCommit(kRingKey, seq, &rb), "take a SAVED row");
        check(ut::journalRows(kRingKey) == 2 && ut::journalCount() == 3 && ut::journalPendingOut() == 1,
              "it stops counting but stays in the file as \"out\"");
        check(readAll(path).find("\"pending\":\"out\"") != std::string::npos, "on disk: pending out + taken");
        // the crash: reopen with no save observed
        check(ut::journalOpenSet("tq-uniq-items-tb") && ut::journalOpenSet("tq-uniq-items-ta"),
              "reload (a crash before the character save)");
        check(ut::journalRows(kRingKey) == 3 && ut::journalPendingOut() == 0,
              "the take is RESTORED: a duplicate at worst, never a loss");
        ut::journalNewest(kRingKey, &n, &seq);
        check(ut::journalTakeCommit(kRingKey, seq, &rb), "take it again");
        check(ut::journalSaveObserved(now() - 100000000ull) == 0, "a save OLDER than the take settles nothing");
        check(ut::journalSaveObserved(now() + 10) == 1 && ut::journalCount() == 2,
              "a newer save drops the taken row for good");
    }

    printf("\nG. a deposit of the identity of an unsaved take cancels the take\n");
    {
        ut::UtReplicaCapture n;
        unsigned long long seq = 0;
        ut::journalNewest(kRingKey, &n, &seq);
        bool rb = false;
        check(ut::journalTakeCommit(kRingKey, seq, &rb) && ut::journalPendingOut() == 1, "take a saved row");
        unsigned long long s2 = 0;
        check(ut::journalDepositCommit(n, &s2, &rb) && s2 == seq, "deposit the same identity back");
        check(ut::journalCount() == 2 && ut::journalPendingOut() == 0 && ut::journalPendingIn() == 0,
              "no new row: the take is cancelled, the row settled again");
    }

    printf("\nH. every field survives the round trip\n");
    {
        clean("tq-uniq-items-tc");
        check(ut::journalOpenSet("tq-uniq-items-tc"), "set tc");
        ut::UtReplicaCapture c = cap("records\\xpack\\item\\u \"q\"\\x.dbr", 0xFFFFFFFFu,
                                     "records\\p\\a,b.dbr", "records\\relic\\r.dbr");
        _snprintf_s(c.str[ut::kUtIdSuffix], ut::kUtIdStrMax, _TRUNCATE, "records\\s\\{x}.dbr");
        _snprintf_s(c.str[ut::kUtIdRelicBonus], ut::kUtIdStrMax, _TRUNCATE, "bonus/1");
        _snprintf_s(c.str[ut::kUtIdRelic2], ut::kUtIdStrMax, _TRUNCATE, "relic2");
        _snprintf_s(c.str[ut::kUtIdRelicBonus2], ut::kUtIdStrMax, _TRUNCATE, "bonus2 \\\\ end");
        c.var1 = 0x12345678u;
        c.var2 = 0x80000001u;
        c.b8 = 255;
        unsigned long long seq = 0;
        bool rb = false;
        check(ut::journalDepositCommit(c, &seq, &rb), "deposit with quotes, commas, braces, max u32, b8 255");
        check(ut::journalOpenSet("tq-uniq-items-ta") && ut::journalOpenSet("tq-uniq-items-tc"), "re-read");
        ut::UtReplicaCapture back;
        check(ut::journalNewest(c.record, &back, nullptr) && sameCap(back, c),
              "the row read back equals the capture, field for field");
    }

    printf("\nI. the capture's own check refuses a row that must not be written\n");
    {
        char why[160];
        ut::UtReplicaCapture c = cap(kRing, 1);
        check(ut::utCaptureValid(c, why, sizeof(why)), "a good capture passes");
        _snprintf_s(c.record, sizeof(c.record), _TRUNCATE, "records/item/other.dbr");
        check(!ut::utCaptureValid(c, why, sizeof(why)), "a key that is not the folded base is refused");
        c = cap(kRing, 1);
        c.str[ut::kUtIdPrefix][0] = '\x01';
        check(!ut::utCaptureValid(c, why, sizeof(why)), "a non-printable string is refused");
        c = cap(kRing, 1);
        c.b8 = 256;
        check(!ut::utCaptureValid(c, why, sizeof(why)), "b8 > 255 is refused");
        c = cap("", 1);
        check(!ut::utCaptureValid(c, why, sizeof(why)), "an empty base is refused");
        const size_t before = ut::journalCount();
        unsigned long long seq = 0;
        bool rb = false;
        c = cap(kRing, 1);
        c.stack = 0;
        check(!ut::journalDepositCommit(c, &seq, &rb) && ut::journalCount() == before,
              "journalDepositCommit refuses it and adds nothing");
        // the capture writes utJournalStack(GetNumberInStack) - a unique's 0 becomes 1.
        check(ut::utJournalStack(0u) == 1u, "the capture's stack for a unique (0) is 1 in the row");
    }

    printf("\nJ. the format gate: a newer file is READ-ONLY and never overwritten\n");
    {
        clean("tq-uniq-items-td");
        char p[MAX_PATH];
        pathOf("tq-uniq-items-td", "jsonl", p);
        const std::string newer =
            "{\"journal\":\"titan quest uniquetab\",\"format\":2,\"entries\":1}\n"
            "{\"record\":\"records/item/equipmentring/u_n_ring_01.dbr\",\"stack\":1,"
            "\"base\":\"records\\\\item\\\\equipmentring\\\\u_n_ring_01.dbr\",\"prefix\":\"\","
            "\"suffix\":\"\",\"relic\":\"\",\"relicBonus\":\"\",\"relic2\":\"\",\"relicBonus2\":\"\","
            "\"seed\":1,\"var1\":0,\"var2\":0,\"b8\":0,\"futureField\":\"x\"}\n";
        writeAll(p, newer);
        check(ut::journalOpenSet("tq-uniq-items-td"), "the set opens");
        check(ut::journalReadOnly() && !ut::journalUsable(), "format 2 > 1: READ-ONLY this session");
        check(ut::journalRows(kRingKey) == 1, "what could be read is shown (display stays)");
        unsigned long long seq = 0;
        bool rb = false;
        check(!ut::journalDepositCommit(cap(kRing, 3), &seq, &rb), "a deposit is refused");
        ut::UtReplicaCapture n;
        ut::journalNewest(kRingKey, &n, &seq);
        check(!ut::journalTakeCommit(kRingKey, seq, &rb), "a take is refused");
        check(!ut::journalFlushNow(), "no write at all");
        check(readAll(p) == newer, "the file's bytes are untouched");
    }

    printf("\nK. a foreign journal (GD's header) is never read as ours\n");
    {
        clean("tq-uniq-items-te");
        char p[MAX_PATH];
        pathOf("tq-uniq-items-te", "jsonl", p);
        const std::string gd = "{\"journal\":\"grim dawn uniquetab\",\"format\":4,\"entries\":0}\n";
        writeAll(p, gd);
        check(ut::journalOpenSet("tq-uniq-items-te") && ut::journalReadOnly(), "READ-ONLY");
        check(ut::journalCount() == 0 && readAll(p) == gd, "nothing read, the bytes untouched");
    }

    printf("\nL. a file that is there and cannot be read is never treated as empty\n");
    {
        clean("tq-uniq-items-tf");
        char p[MAX_PATH];
        pathOf("tq-uniq-items-tf", "jsonl", p);
        writeAll(p, "{\"journal\":\"titan quest uniquetab\",\"format\":1,\"entries\":0}\n");
        HANDLE lock = CreateFileA(p, GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        check(lock != INVALID_HANDLE_VALUE, "an exclusive handle holds the file");
        check(ut::journalOpenSet("tq-uniq-items-tf") && ut::journalReadOnly() && !ut::journalUsable(),
              "share-locked -> READ-ONLY, moves refused");
        if (lock != INVALID_HANDLE_VALUE) CloseHandle(lock);
        std::string empty = "";
        writeAll(p, empty);
        check(ut::journalOpenSet("tq-uniq-items-ta") && ut::journalOpenSet("tq-uniq-items-tf") &&
                  ut::journalReadOnly(), "a zero-byte file -> READ-ONLY too");
    }

    printf("\nM. a bad line costs that row only, and the file is copied aside first\n");
    {
        clean("tq-uniq-items-tg");
        char p[MAX_PATH];
        pathOf("tq-uniq-items-tg", "jsonl", p);
        const std::string row =
            "{\"record\":\"records/item/equipmentring/u_n_ring_01.dbr\",\"stack\":1,"
            "\"base\":\"records\\\\item\\\\equipmentring\\\\u_n_ring_01.dbr\",\"prefix\":\"\","
            "\"suffix\":\"\",\"relic\":\"\",\"relicBonus\":\"\",\"relic2\":\"\",\"relicBonus2\":\"\","
            "\"seed\":1,\"var1\":0,\"var2\":0,\"b8\":0}\n";
        const std::string wrongKey =
            "{\"record\":\"records/item/other.dbr\",\"stack\":1,"
            "\"base\":\"records\\\\item\\\\equipmentring\\\\u_n_ring_01.dbr\",\"prefix\":\"\","
            "\"suffix\":\"\",\"relic\":\"\",\"relicBonus\":\"\",\"relic2\":\"\",\"relicBonus2\":\"\","
            "\"seed\":1,\"var1\":0,\"var2\":0,\"b8\":0}\n";
        writeAll(p, "{\"journal\":\"titan quest uniquetab\",\"format\":1,\"entries\":4}\n" + row +
                        "{not json\n" + wrongKey + row);
        check(ut::journalOpenSet("tq-uniq-items-tg") && !ut::journalReadOnly(), "the file opens writable");
        check(ut::journalCount() == 2, "2 good rows kept, the broken and the mis-keyed line dropped");
        unsigned long long seq = 0;
        bool rb = false;
        check(ut::journalDepositCommit(cap(kRing, 2), &seq, &rb), "the next deposit writes");
        check(countBad("tq-uniq-items-tg") == 1, "the original was copied aside as .bad-<stamp> first");
    }

    printf("\nM2. a row that says \"stack\":0 is read as one copy (never dropped)\n");
    {
        clean("tq-uniq-items-tz");
        char p[MAX_PATH];
        pathOf("tq-uniq-items-tz", "jsonl", p);
        writeAll(p, "{\"journal\":\"titan quest uniquetab\",\"format\":1,\"entries\":1}\n"
                    "{\"record\":\"records/item/equipmentring/u_n_ring_01.dbr\",\"stack\":0,"
                    "\"base\":\"records\\\\item\\\\equipmentring\\\\u_n_ring_01.dbr\",\"prefix\":\"\","
                    "\"suffix\":\"\",\"relic\":\"\",\"relicBonus\":\"\",\"relic2\":\"\",\"relicBonus2\":\"\","
                    "\"seed\":1,\"var1\":0,\"var2\":0,\"b8\":0}\n");
        check(ut::journalOpenSet("tq-uniq-items-tz") && ut::journalRows(kRingKey) == 1 &&
                  ut::journalCount() == 1,
              "the stack-0 row is kept (utJournalStack: one copy)");
    }

    printf("\nN. the CSV export\n");
    {
        clean("tq-uniq-items-th");
        check(ut::journalOpenSet("tq-uniq-items-th"), "set th");
        ut::journalSetCsvExport(1);
        unsigned long long seq = 0;
        bool rb = false;
        check(ut::journalDepositCommit(cap(kRing, 42, "a,b"), &seq, &rb), "deposit");
        char c[MAX_PATH];
        pathOf("tq-uniq-items-th", "csv", c);
        const std::string t = readAll(c);
        check(t.find("record,collected,deposited,stack,base,") == 0, "the CSV header");
        check(t.find("records/item/equipmentring/u_n_ring_01.dbr,unsaved,") != std::string::npos,
              "one row, marked unsaved");
        check(t.find("\"a,b\"") != std::string::npos, "RFC 4180 quoting");
        ut::journalSetCsvExport(0);
    }

    printf("\nN. the load settles against a save bounded by the process start; "
           "a take held on the cursor is never settled\n");
    {
        const unsigned long long S = 1000000000ull;   // 100 s in FILETIME units
        check(!ut::utLoadSettles(10 * S, 0, 20 * S) && !ut::utLoadSettles(10 * S, 15 * S, 0) &&
                  !ut::utLoadSettles(10 * S, 9 * S, 20 * S),
              "utLoadSettles: no save / unknown start / an older save -> never");
        check(ut::utLoadSettles(10 * S, 15 * S, 20 * S), "an earlier process's row, saved before this start -> settled");
        check(!ut::utLoadSettles(10 * S, 25 * S, 20 * S), "an earlier process's row, a save after this start -> NOT settled");
        check(ut::utLoadSettles(21 * S, 25 * S, 20 * S), "this process's row, a newer save -> settled");

        clean("tq-uniq-items-ti");
        check(ut::journalOpenSet("tq-uniq-items-ti"), "set ti");
        bool rb = false;
        unsigned long long s = 0;
        check(ut::journalDepositCommit(cap(kRing, 7), &s, &rb) &&
                  ut::journalDepositCommit(cap(kRing, 8), &s, &rb) &&
                  ut::journalDepositCommit(cap(kRing, 9), &s, &rb),
              "three deposits");
        check(ut::journalSaveObserved(now() + 10) == 3, "a save settles them");
        ut::UtReplicaCapture n;
        unsigned long long seq = 0;

        // (a) an earlier process: take, a save after it, then this process starts
        ut::journalNewest(kRingKey, &n, &seq);
        check(ut::journalTakeCommit(kRingKey, seq, &rb) && ut::journalPendingOut() == 1, "(a) take a saved row");
        unsigned long long t = now();
        ut::journalFlushNow();
        check(ut::journalOpenSet("tq-uniq-items-tb") &&
                  ut::journalOpenSet("tq-uniq-items-ti", t + 20, t + 40),
              "(a) reload: the save (t+20) is older than this process (t+40)");
        check(ut::journalCount() == 2 && ut::journalRows(kRingKey) == 2 && ut::journalPendingOut() == 0,
              "(a) the take is SETTLED at load - no silent duplicate");

        // (b) an earlier process: take, crash, this process starts and saves before the load
        ut::journalNewest(kRingKey, &n, &seq);
        check(ut::journalTakeCommit(kRingKey, seq, &rb), "(b) take a saved row");
        t = now();
        ut::journalFlushNow();
        check(ut::journalOpenSet("tq-uniq-items-tb") &&
                  ut::journalOpenSet("tq-uniq-items-ti", t + 20, t + 10),
              "(b) reload: the only newer save (t+20) was written by THIS process (start t+10)");
        check(ut::journalCount() == 2 && ut::journalRows(kRingKey) == 2 && ut::journalPendingOut() == 0,
              "(b) the take is RESTORED - a duplicate at worst, never a loss");

        // (c) this process: take, world switch A -> B -> A, a save in between
        ut::journalNewest(kRingKey, &n, &seq);
        check(ut::journalTakeCommit(kRingKey, seq, &rb), "(c) take a saved row");
        t = now();
        ut::journalFlushNow();
        check(ut::journalOpenSet("tq-uniq-items-tb") &&
                  ut::journalOpenSet("tq-uniq-items-ti", t + 20, t - 100 * S),
              "(c) reload in the same process (started long before the take), a newer save");
        check(ut::journalCount() == 1 && ut::journalRows(kRingKey) == 1 && ut::journalPendingOut() == 0,
              "(c) this process's take is settled by its newer save");

        // a take held on the cursor is not settled; after it leaves, only a LATER save settles
        ut::journalNewest(kRingKey, &n, &seq);
        check(ut::journalTakeCommit(kRingKey, seq, &rb), "(d) take the last row");
        ut::journalSetHeldSeq(seq);
        check(ut::journalSaveObserved(now() + 10) == 0 && ut::journalPendingOut() == 1,
              "(d) a save while the item is on the cursor settles nothing");
        Sleep(30);
        const unsigned long long whileHeld = now();
        Sleep(30);
        ut::journalSetHeldSeq(0);
        check(ut::journalTouchOut(seq), "(d) the item left the cursor: the taken time moves to now");
        check(!ut::journalTouchOut(seq + 1000), "(d) no such out row -> false");
        check(ut::journalSaveObserved(whileHeld) == 0 && ut::journalPendingOut() == 1,
              "(d) a save written while it was held still settles nothing");
        check(ut::journalSaveObserved(now() + 10) == 1 && ut::journalCount() == 0,
              "(d) a save after it left the cursor settles the take");
        ut::journalFlushNow();
    }

    printf("\nW. the pending rows settled against the character's containers\n");
    {
        using namespace ut;
        const std::string::size_type npos = std::string::npos;
        // the pure verdict: the 2 x 3 table + unknown
        check(utPendingVerdict(1, kUtFoundPresent) == kUtVerdictErase,
              "in  + present        -> ERASE (the removal never persisted: the item is back)");
        check(utPendingVerdict(1, kUtFoundAbsent) == kUtVerdictSettle,
              "in  + absent         -> SETTLE (the removal persisted: collected)");
        check(utPendingVerdict(1, kUtFoundAbsentSoFar) == kUtVerdictKeep,
              "in  + absent so far  -> keep (the caravan sacks are not searched yet)");
        check(utPendingVerdict(2, kUtFoundPresent) == kUtVerdictErase,
              "out + present        -> ERASE (the take persisted: removed for good)");
        check(utPendingVerdict(2, kUtFoundAbsent) == kUtVerdictRestore,
              "out + absent         -> RESTORE (the take never persisted)");
        check(utPendingVerdict(2, kUtFoundAbsentSoFar) == kUtVerdictKeep,
              "out + absent so far  -> keep");
        check(utPendingVerdict(1, kUtFoundUnknown) == kUtVerdictKeep &&
                  utPendingVerdict(2, kUtFoundUnknown) == kUtVerdictKeep,
              "unknown              -> keep (settle nothing)");
        check(utPendingVerdict(0, kUtFoundPresent) == kUtVerdictKeep &&
                  utPendingVerdict(3, kUtFoundAbsent) == kUtVerdictKeep,
              "a row that is not pending -> keep");

        // the replica compare (the ten fields) on fixtures
        const UtReplicaCapture a =
            cap(kRing, 7, "records/item/lootmagicalaffixes/prefix/p1.dbr", "records/item/relics/r1.dbr");
        UtReplicaCapture b = a;
        check(utReplicaSame(a, b), "compare: the same replica -> a match");
        b.seed = 8;
        check(!utReplicaSame(a, b), "compare: the seed off by one -> no match");
        b = a;
        _snprintf_s(b.str[kUtIdSuffix], kUtIdStrMax, _TRUNCATE, "%s",
                    "records/item/lootmagicalaffixes/suffix/s2.dbr");
        check(!utReplicaSame(a, b), "compare: a different suffix -> no match");
        b = a;
        b.var2 = 1;
        check(!utReplicaSame(a, b), "compare: var2 differs -> no match");
        b = a;
        _snprintf_s(b.str[kUtIdRelic], kUtIdStrMax, _TRUNCATE, "%s", "records/item/relics/R1.dbr");
        check(!utReplicaSame(a, b), "compare: the relic differs in case only -> no match (byte-exact)");
        const UtReplicaCapture* two[2] = {&a, &a};
        int f2[2] = {0, 0};
        const int m0 = utMatchLive(two, f2, 2, a);
        if (m0 >= 0) f2[m0] = 1;
        const int m1 = utMatchLive(two, f2, 2, a);
        if (m1 >= 0) f2[m1] = 1;
        check(m0 == 0 && m1 == 1 && utMatchLive(two, f2, 2, a) == -1,
              "one live item proves ONE of two byte-identical rows");

        // the file: four verdicts + keep, in one deferred load
        const char* L = "tq-uniq-items-tw";
        clean(L);
        char wpath[MAX_PATH];
        pathOf(L, "jsonl", wpath);
        check(journalOpenSet(L), "set tw");
        bool rb = false;
        unsigned long long s = 0;
        for (unsigned k = 1; k <= 4; ++k) journalDepositCommit(cap(kRing, k), &s, &rb);
        check(journalSaveObserved(now() + 10) == 4, "four rows (seeds 1..4), saved");
        UtReplicaCapture nn;
        unsigned long long seq = 0;
        journalNewest(kRingKey, &nn, &seq);
        const bool t4 = nn.seed == 4 && journalTakeCommit(kRingKey, seq, &rb);
        journalNewest(kRingKey, &nn, &seq);
        const bool t3 = nn.seed == 3 && journalTakeCommit(kRingKey, seq, &rb);
        check(t4 && t3, "take seeds 4 and 3 (pending out)");
        for (unsigned k = 5; k <= 7; ++k) journalDepositCommit(cap(kRing, k), &s, &rb);
        check(journalPendingIn() == 3 && journalPendingOut() == 2 && journalCount() == 7,
              "deposit seeds 5, 6, 7 (pending in): 7 rows, 3 in, 2 out");
        journalFlushNow();
        check(journalOpenSet("tq-uniq-items-tb") && journalOpenSet(L, 0, 0, true),
              "the crash: a DEFERRED reload with no save seen");
        check(journalUnresolved() == 5 && journalPendingIn() == 3 && journalPendingOut() == 0 &&
                  journalRows(kRingKey) == 7,
              "5 unresolved; the 2 outs are RESTORED at the open, the 3 ins wait");
        check(journalSaveObserved(100) == 0 && journalUnresolved() == 5,
              "a save older than the rows leaves the unresolved rows alone");
        UtPendingRow pr[8];
        const size_t np = journalUnresolvedRows(pr, 8);
        check(np == 5, "the 5 rows are handed to the container check");
        UtVerdictItem v[8];
        unsigned long long seq7 = 0;
        for (size_t i = 0; i < np; ++i) {
            const unsigned sd = pr[i].id.seed;
            // found: 5 (in) and 4 (out) in the inventory; 7 not yet (no caravan); 6, 3 nowhere
            const int f = sd == 5 || sd == 4 ? kUtFoundPresent
                          : sd == 7          ? kUtFoundAbsentSoFar
                                             : kUtFoundAbsent;
            v[i].seq = pr[i].seq;
            v[i].verdict = utPendingVerdict(pr[i].state, f);
            v[i].where = f == kUtFoundPresent ? "inventory" : nullptr;
            if (sd == 7) seq7 = pr[i].seq;
        }
        const long w0 = journalWrites();
        check(journalApplyVerdicts(v, np) == 4,
              "apply: 5 erased, 4 erased, 6 settled, 3 restored (4 changed), 7 kept");
        check(journalWrites() == w0 + 1, "the journal is written ONCE for the settle");
        check(journalCount() == 5 && journalRows(kRingKey) == 5 && journalPendingIn() == 1 &&
                  journalPendingOut() == 0 && journalUnresolved() == 1,
              "after: 5 rows (1 2 3 6 7), 1 pending in (7, still unresolved), none out");
        std::string t = readAll(wpath);
        check(t.find("\"entries\":5,\"collected\":5,\"pendingIn\":1,\"pendingOut\":0") != npos &&
                  t.find("\"written\":\"") != npos,
              "on disk: the header says entries 5, collected 5, pendingIn 1, pendingOut 0, written");
        check(t.find("\"seed\":5,") == npos && t.find("\"seed\":4,") == npos &&
                  t.find("\"seed\":3,") != npos && t.find("\"seed\":6,") != npos &&
                  t.find("\"pending\":\"out\"") == npos,
              "on disk: 5 and 4 are gone, 3 (restored) and 6 (settled) are there, no out row");

        // unknown: keep, nothing written
        UtVerdictItem k1 = {seq7, utPendingVerdict(1, kUtFoundUnknown), nullptr};
        const long w1 = journalWrites();
        check(journalApplyVerdicts(&k1, 1) == 0 && journalWrites() == w1 && journalUnresolved() == 1,
              "unknown: settles nothing, no write");
        UtVerdictItem bad = {seq7, kUtVerdictRestore, nullptr};
        check(journalApplyVerdicts(&bad, 1) == 0 && journalPendingIn() == 1,
              "a verdict that does not fit the row's state (restore on an in row): nothing");
        // the fallback: today's rule
        check(journalUnresolvedFallback("test") == 1 && journalUnresolved() == 0 &&
                  journalPendingIn() == 1,
              "fallback: the in row is reported (WARN) and left pending, as today");
        check(journalSaveObserved(now() + 10) == 1 && journalPendingIn() == 0,
              "... and the next save settles it, as today");
        check(journalApplyVerdicts(v, np) == 0 && journalCount() == 5,
              "verdicts for rows that are no longer unresolved: nothing");
        journalNewest(kRingKey, &nn, &seq);
        check(journalTakeCommit(kRingKey, seq, &rb), "take the newest row (seed 7)");
        journalFlushNow();
        check(journalOpenSet("tq-uniq-items-tb") && journalOpenSet(L, 0, 0, true) &&
                  journalUnresolved() == 1 && journalRows(kRingKey) == 5,
              "a crash, a deferred reload: 1 unresolved out row, restored at the open");
        check(journalUnresolvedFallback("test") == 1 && journalRows(kRingKey) == 5 &&
                  journalPendingOut() == 0 && journalUnresolved() == 0,
              "fallback: the out row is RESTORED (today's rule, WARN)");
        journalFlushNow();
        t = readAll(wpath);
        check(t.find("\"entries\":5,\"collected\":5,\"pendingIn\":0,\"pendingOut\":0") != npos,
              "on disk: entries 5, collected 5, nothing pending");
    }

    printf("\nX. a deferred row meets a save of the next session (no caravan open)\n");
    {
        using namespace ut;
        // the repro: S1 takes, dies; S2 defers, saves, quits; S3 must keep the row
        const char* L = "tq-uniq-items-tx";
        clean(L);
        bool rb = false;
        unsigned long long s = 0;
        check(journalOpenSet(L), "S1: set tx");
        journalDepositCommit(cap(kRing, 11), &s, &rb);
        check(journalSaveObserved(now() + 10) == 1, "S1: seed 11 deposited and saved");
        UtReplicaCapture nn;
        unsigned long long seq = 0;
        journalNewest(kRingKey, &nn, &seq);
        check(journalTakeCommit(kRingKey, seq, &rb) && journalPendingOut() == 1,
              "S1: taken (pending out), then the crash");
        journalFlushNow();
        const unsigned long long p2 = now() + 1000;
        check(journalOpenSet("tq-uniq-items-tb") && journalOpenSet(L, 0, p2, true) &&
                  journalUnresolved() == 1 && journalPendingOut() == 0 && journalRows(kRingKey) == 1,
              "S2: deferred load - the out row is RESTORED at the open (unresolved)");
        journalFlushNow();   // the worker's write of the restore
        const unsigned long long save2 = p2 + 1000;
        check(journalSaveObserved(save2) == 0, "S2: a character save; no caravan open; exit");
        journalFlushNow();
        const unsigned long long p3 = save2 + 1000;
        check(journalOpenSet("tq-uniq-items-tb") && journalOpenSet(L, save2, p3, true),
              "S3: deferred load, newest save = S2's");
        printf("  S3 after open: count=%d rows=%d pendingOut=%d unresolved=%d\n",
               (int)journalCount(), (int)journalRows(kRingKey), (int)journalPendingOut(),
               (int)journalUnresolved());
        check(journalCount() == 1 && journalRows(kRingKey) == 1 && journalPendingOut() == 0 &&
                  journalUnresolved() == 0,
              "S3: the row survives, in the collection (never a loss)");

        // the in-row mirror: S1 deposits, dies; S2 defers, then a save of S2 settles with a WARN
        const char* Ly = "tq-uniq-items-ty";
        clean(Ly);
        check(journalOpenSet(Ly), "S1: set ty");
        journalDepositCommit(cap(kRing, 12), &s, &rb);
        journalDepositCommit(cap(kRing, 13), &s, &rb);
        journalFlushNow();
        const unsigned long long q2 = now() + 1000;
        check(journalOpenSet("tq-uniq-items-tb") && journalOpenSet(Ly, 0, q2, true) &&
                  journalUnresolved() == 2 && journalPendingIn() == 2,
              "S2: deferred load, 2 unresolved in rows");
        check(journalSaveObserved(q2 + 1000) == 2 && journalUnresolved() == 0 &&
                  journalPendingIn() == 0 && journalRows(kRingKey) == 2,
              "S2: a character save - today's WARN first, then the save settles both (as)");

        // The verdicts and the fallback of one caravan pass write the journal ONCE
        journalNewest(kRingKey, &nn, &seq);
        check(nn.seed == 13 && journalTakeCommit(kRingKey, seq, &rb), "take seed 13 (pending out)");
        journalDepositCommit(cap(kRing, 14), &s, &rb);
        journalFlushNow();
        check(journalOpenSet("tq-uniq-items-tb") && journalOpenSet(Ly, 0, 0, true) &&
                  journalUnresolved() == 2 && journalRows(kRingKey) == 3,
              "a crash, a deferred reload: 13 (out, restored) and 14 (in) unresolved");
        journalFlushNow();
        UtPendingRow py[4];
        const size_t ny = journalUnresolvedRows(py, 4);
        UtVerdictItem vy[4];
        for (size_t i = 0; i < ny; ++i) {
            vy[i].seq = py[i].seq;
            vy[i].verdict = py[i].id.seed == 13 ? utPendingVerdict(py[i].state, kUtFoundPresent)
                                                : utPendingVerdict(py[i].state, kUtFoundUnknown);
            vy[i].where = "inventory";
        }
        const long wy = journalWrites();
        check(ny == 2 && journalApplyVerdicts(vy, ny, "test") == 1 && journalWrites() == wy + 1,
              "13 found (erased), 14 unknown (the fallback): ONE write");
        check(journalUnresolved() == 0 && journalPendingIn() == 1 && journalPendingOut() == 0 &&
                  journalRows(kRingKey) == 2 && journalCount() == 2,
              "after: 12 and 14 (14 reported, left to the next save), 13 gone");
        const long wz = journalWrites();
        check(journalApplyVerdicts(nullptr, 0, "test") == 0 && journalWrites() == wz,
              "a fallback with nothing unresolved: no write");
    }

    printf("\nZ. a generated row (tools\\make_all_items.py): read, re-written byte for byte, taken\n");
    {
        using namespace ut;
        const char* stamp = "2026-01-02T03:04:05.0000000Z";
        const char* b1 = "records\\item\\equipmentring\\u_n_ring_01.dbr";
        const char* b2 = "records\\xpack\\item\\equipmentweapon\\sword\\u_e_blade's_edge.dbr";
        const std::string r1 = allItemsRow(b1, stamp), r2 = allItemsRow(b2, stamp);
        clean("tq-uniq-items-tz");
        char pz[MAX_PATH];
        pathOf("tq-uniq-items-tz", "jsonl", pz);
        writeAll(pz, std::string("{\"journal\":\"titan quest uniquetab\",\"format\":1,\"written\":\"") +
                         stamp + "\",\"set\":\"tq-uniq-items-tz\",\"entries\":2,\"collected\":2," +
                         "\"pendingIn\":0,\"pendingOut\":0}\n" + r1 + r2);
        check(journalOpenSet("tq-uniq-items-tz") && journalUsable() && journalCount() == 2 &&
                  journalCollectedTotal() == 2 && journalPendingIn() == 0 && journalPendingOut() == 0,
              "two generated rows: 2 rows, 2 collected, nothing pending, nothing dropped");
        const unsigned s1 = allItemsSeed(kRingKey);
        check(s1 >= 1u && s1 <= 32767u && allItemsSeed("records/a.dbr") != allItemsSeed("records/b.dbr"),
              "the generator's seed is in 1..32767 and follows the record");
        UtReplicaCapture row;
        check(takeRoundTrip(kRingKey, &row) && row.seed == s1 && row.var1 == 0 && row.var2 == 0 &&
                  row.b8 == 0 && strcmp(row.str[kUtIdBase], b1) == 0 && !row.str[kUtIdPrefix][0] &&
                  !row.str[kUtIdRelicBonus2][0],
              "the take's row -> replica -> row is exact (base, six empty strings, seed, var1/var2/b8 0)");
        UtReplica proto, fromRow;
        UtReplicaCapture pa, pb;
        // bytes +0x04..+0x07 are the long base's pointer (each replica's own buffer): its TEXT is
        // compared instead, through the identity read back out of both.
        check(utReplicaBuild(&proto, "records/item/equipmentring/u_n_ring_01.dbr") &&
                  utReplicaFromIdentity(&fromRow, row) && utReplicaReadIdentity(proto.bytes, &pa) &&
                  utReplicaReadIdentity(fromRow.bytes, &pb) && pa.seed == 0 && pb.seed == s1 &&
                  (pa.seed = s1, sameCap(pa, pb)) && memcmp(proto.bytes, fromRow.bytes, 4) == 0 &&
                  memcmp(proto.bytes + 8, fromRow.bytes + 8, kUtReplicaSeed - 8) == 0 &&
                  memcmp(proto.bytes + kUtReplicaSeed + 4, fromRow.bytes + kUtReplicaSeed + 4,
                         kUtReplicaSize - kUtReplicaSeed - 4) == 0,
              "its replica IS the page prototype's (utReplicaBuild) apart from the seed");
        unsigned long long sq = 0;
        bool rb = true;
        check(journalDepositCommit(cap("records\\item\\equipmentring\\u_n_ring_02.dbr", 9), &sq, &rb) &&
                  !rb,
              "a deposit rewrites the file");
        const std::string tz = readAll(pz);
        check(tz.find("\n" + r1) != std::string::npos && tz.find("\n" + r2) != std::string::npos,
              "the mod's writer re-emits both generated rows byte for byte (stamp included)");
        journalOpenSet(nullptr);
    }

    printf("\nO. a mod's journal moves into mods\\<leaf>\\ once: all or nothing, the .jsonl last\n");
    {
        using namespace ut;
        char game[80];
        check(!utSaveSetGame("tq-uniq-items", game, sizeof(game)) &&
                  !utSaveSetGame("tq-uniq-items-custom", game, sizeof(game)),
              "the main game's two sets have no folder of their own (the root files)");
        char leaf[96];
        check(utSaveSetLeaf(true, "MYMOD", true, false, leaf, sizeof(leaf)) &&
                  utSaveSetGame(leaf, game, sizeof(game)) && !strcmp(game, "mymod") &&
                  utGameOfSet(leaf) == "mymod",
              "a mod's set names its folder: tq-uniq-items-mymod -> mods\\mymod (ONE leaf function)");
        check(utSaveSetLeaf(true, "My Mod", true, true, leaf, sizeof(leaf)) &&
                  utGameOfSet(leaf) == std::string(leaf + 14) &&
                  utSaveSetLeaf(true, "custom", true, true, leaf, sizeof(leaf)) &&
                  utGameOfSet(leaf) == "mod_custom",
              "a lossy name keeps its hash in the folder; a mod named custom is mod_custom");
        const std::string base(g_dir);
        auto put = [&](const char* name, const char* text) {
            writeAll((base + "\\" + name).c_str(), text);
        };
        auto at = [&](const std::string& rel) { return readAll((base + "\\" + rel).c_str()); };
        auto there = [&](const std::string& rel) {
            return GetFileAttributesA((base + "\\" + rel).c_str()) != INVALID_FILE_ATTRIBUTES;
        };

        // 1. the rename: a real journal with a row, its csv and a .bad- copy
        clean("tq-uniq-items-mv1");
        check(journalOpenSet("tq-uniq-items-mv1"), "a mod's set opened in the root (an older build's place)");
        unsigned long long seq = 0;
        bool rb = false;
        check(journalDepositCommit(cap(kRing, 5), &seq, &rb), "one row deposited");
        journalOpenSet(nullptr);
        const std::string rows1 = at("tq-uniq-items-mv1.jsonl");
        put("tq-uniq-items-mv1.csv", "record\r\n");
        put("tq-uniq-items-mv1.jsonl.bad-x", "bad\n");
        UtJournalMove m = utJournalMoveSet(base, "tq-uniq-items-mv1");
        check(m.result == kUtMoveDone && m.files.size() == 3 && m.files.back() == "tq-uniq-items-mv1.jsonl",
              "moved: the csv and the .bad- copy first, the .jsonl LAST");
        check(!there("tq-uniq-items-mv1.jsonl") && !there("tq-uniq-items-mv1.csv") &&
                  !there("tq-uniq-items-mv1.jsonl.bad-x") &&
                  at("mods\\mv1\\tq-uniq-items-mv1.jsonl") == rows1 &&
                  at("mods\\mv1\\tq-uniq-items-mv1.csv") == "record\r\n" &&
                  at("mods\\mv1\\tq-uniq-items-mv1.jsonl.bad-x") == "bad\n",
              "nothing is left in the root; every file is in mods\\mv1\\ byte for byte");
        check(utJournalSetRel(base, "tq-uniq-items-mv1") == "mods\\mv1", "the set is read from mods\\mv1 now");
        check(journalOpenSet("tq-uniq-items-mv1", 0, 0, false, "mods\\mv1") && journalUsable() &&
                  journalRows(kRingKey) == 1 && !strncmp(journalPath() + strlen(g_dir), "\\mods\\mv1\\", 10),
              "and opens there with its row");
        journalOpenSet(nullptr);
        check(utJournalMoveSet(base, "tq-uniq-items-mv1").result == kUtMoveNothing,
              "a second start finds nothing to move");

        // 2. a taken target: nothing moves, nothing merges, both are kept
        put("tq-uniq-items-mv2.jsonl", "root rows\n");
        put("tq-uniq-items-mv2.csv", "root csv\n");
        CreateDirectoryA((base + "\\mods").c_str(), nullptr);
        CreateDirectoryA((base + "\\mods\\mv2").c_str(), nullptr);
        put("mods\\mv2\\tq-uniq-items-mv2.jsonl", "folder rows\n");
        m = utJournalMoveSet(base, "tq-uniq-items-mv2");
        check(m.result == kUtMoveTaken && at("tq-uniq-items-mv2.jsonl") == "root rows\n" &&
                  at("tq-uniq-items-mv2.csv") == "root csv\n" &&
                  at("mods\\mv2\\tq-uniq-items-mv2.jsonl") == "folder rows\n" &&
                  !there("mods\\mv2\\tq-uniq-items-mv2.csv"),
              "a taken target: refused before anything moved, both files kept as they were");
        check(utJournalSetRel(base, "tq-uniq-items-mv2") == "mods\\mv2", "the mod uses mods\\mv2 (GD's rule)");
        utJournalMoveLog("tq-uniq-items-mv2", m, "at start");

        // 3. a held file: the .jsonl open in another program (no delete sharing)
        put("tq-uniq-items-mv3.jsonl", "rows 3\n");
        put("tq-uniq-items-mv3.csv", "csv 3\n");
        HANDLE held = CreateFileA((base + "\\tq-uniq-items-mv3.jsonl").c_str(), GENERIC_READ,
                                  FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        m = utJournalMoveSet(base, "tq-uniq-items-mv3");
        check(held != INVALID_HANDLE_VALUE && m.result == kUtMoveRolledBack &&
                  at("tq-uniq-items-mv3.jsonl") == "rows 3\n" && at("tq-uniq-items-mv3.csv") == "csv 3\n" &&
                  !there("mods\\mv3\\tq-uniq-items-mv3.csv") && !there("mods\\mv3\\tq-uniq-items-mv3.jsonl"),
              "a held .jsonl: the moved csv went BACK, the whole set is in the root");
        check(utJournalSetRel(base, "tq-uniq-items-mv3") == "", "and is read from the root (never split)");
        utJournalMoveLog("tq-uniq-items-mv3", m, "at start");
        CloseHandle(held);
        m = utJournalMoveSet(base, "tq-uniq-items-mv3");
        check(m.result == kUtMoveDone && at("mods\\mv3\\tq-uniq-items-mv3.jsonl") == "rows 3\n" &&
                  !there("tq-uniq-items-mv3.jsonl"),
              "the next start (the file released) moves the set");

        // 4. a split set rolled back: a .bad- copy in the middle is held
        put("tq-uniq-items-mv4.jsonl", "rows 4\n");
        put("tq-uniq-items-mv4.csv", "csv 4\n");
        put("tq-uniq-items-mv4.jsonl.bad-1", "bad 1\n");
        put("tq-uniq-items-mv4.jsonl.bad-2", "bad 2\n");
        held = CreateFileA((base + "\\tq-uniq-items-mv4.jsonl.bad-2").c_str(), GENERIC_READ,
                           FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        m = utJournalMoveSet(base, "tq-uniq-items-mv4");
        check(m.result == kUtMoveRolledBack && m.why.find("bad-2") != std::string::npos &&
                  at("tq-uniq-items-mv4.csv") == "csv 4\n" && at("tq-uniq-items-mv4.jsonl.bad-1") == "bad 1\n" &&
                  at("tq-uniq-items-mv4.jsonl") == "rows 4\n" &&
                  !there("mods\\mv4\\tq-uniq-items-mv4.csv") && !there("mods\\mv4\\tq-uniq-items-mv4.jsonl.bad-1"),
              "two files moved, the third failed: both rolled back, the .jsonl never left the root");
        CloseHandle(held);

        // 5. the start pass: only the mods' sets move
        put("tq-uniq-items.jsonl", "main\n");
        put("tq-uniq-items-custom.jsonl", "custom\n");
        put("tq-uniq-items-mv5.jsonl", "rows 5\n");
        utJournalMoveAll(base);
        check(at("tq-uniq-items.jsonl") == "main\n" && at("tq-uniq-items-custom.jsonl") == "custom\n" &&
                  !there("mods\\custom") && at("mods\\mv5\\tq-uniq-items-mv5.jsonl") == "rows 5\n" &&
                  at("mods\\mv4\\tq-uniq-items-mv4.jsonl") == "rows 4\n",
              "at start: the main game's and the custom set stay in the root, every mod's set moves");
        check(at("tq-uniq-items-mv2.jsonl") == "root rows\n", "the taken one is still left alone");

        // 6. a split set: the .jsonl's rename fails AND the .csv cannot be moved back
        put("tq-uniq-items-mv6.jsonl", "rows 6\n");
        put("tq-uniq-items-mv6.csv", "csv 6\n");
        put("tq-uniq-items-mv6.jsonl.bad-1", "bad 6\n");
        folders_detail::g_moveFileHook = [](const char* from, const char* to) -> bool {
            const std::string f(from);
            const bool jsonl = f.size() > 6 && f.compare(f.size() - 6, 6, ".jsonl") == 0;
            const bool csvBack = f.find("\\mods\\mv6\\") != std::string::npos &&
                                 f.compare(f.size() - 4, 4, ".csv") == 0;
            if (jsonl || csvBack) {
                SetLastError(jsonl ? ERROR_SHARING_VIOLATION : ERROR_ACCESS_DENIED);
                return false;
            }
            return MoveFileExA(from, to, MOVEFILE_WRITE_THROUGH) != 0;
        };
        m = utJournalMoveSet(base, "tq-uniq-items-mv6");
        folders_detail::g_moveFileHook = nullptr;
        check(m.result == kUtMoveSplit && m.why.find("mv6.csv could NOT be moved back") != std::string::npos &&
                  at("tq-uniq-items-mv6.jsonl") == "rows 6\n" && at("tq-uniq-items-mv6.jsonl.bad-1") == "bad 6\n" &&
                  at("mods\\mv6\\tq-uniq-items-mv6.csv") == "csv 6\n" && !there("tq-uniq-items-mv6.csv") &&
                  !there("mods\\mv6\\tq-uniq-items-mv6.jsonl"),
              "a split set: the .jsonl never left the root, only the named .csv is in mods\\mv6\\");
        check(utJournalSetRel(base, "tq-uniq-items-mv6") == "", "a split set is read from the root");
        utJournalMoveLog("tq-uniq-items-mv6", m, "at start");
        m = utJournalMoveSet(base, "tq-uniq-items-mv6");
        check(m.result == kUtMoveDone && m.files.size() == 2 && at("mods\\mv6\\tq-uniq-items-mv6.jsonl") == "rows 6\n" &&
                  at("mods\\mv6\\tq-uniq-items-mv6.csv") == "csv 6\n" &&
                  at("mods\\mv6\\tq-uniq-items-mv6.jsonl.bad-1") == "bad 6\n" && !there("tq-uniq-items-mv6.jsonl"),
              "the next start heals a split set: everything in mods\\mv6\\");

        // 7. only a .csv in the way (a split remnant, then a session that wrote the root's .csv)
        put("tq-uniq-items-mv7.jsonl", "rows 7\n");
        put("tq-uniq-items-mv7.csv", "new csv 7\n");
        CreateDirectoryA((base + "\\mods\\mv7").c_str(), nullptr);
        put("mods\\mv7\\tq-uniq-items-mv7.csv", "old csv 7\n");
        m = utJournalMoveSet(base, "tq-uniq-items-mv7");
        check(m.result == kUtMoveTaken && m.blocker == "mods\\mv7\\tq-uniq-items-mv7.csv" && m.readRel == "" &&
                  utJournalSetRel(base, "tq-uniq-items-mv7") == "" && at("tq-uniq-items-mv7.jsonl") == "rows 7\n" &&
                  at("mods\\mv7\\tq-uniq-items-mv7.csv") == "old csv 7\n",
              "a .csv in the way: refused, and the WARN names the root's journal as the one read");
        utJournalMoveLog("tq-uniq-items-mv7", m, "at start");
        DeleteFileA((base + "\\mods\\mv7\\tq-uniq-items-mv7.csv").c_str());
        check(utJournalMoveSet(base, "tq-uniq-items-mv7").result == kUtMoveDone &&
                  at("mods\\mv7\\tq-uniq-items-mv7.jsonl") == "rows 7\n" &&
                  at("mods\\mv7\\tq-uniq-items-mv7.csv") == "new csv 7\n",
              "the named file removed, the next start moves the set");

        // 8. a crash between two renames: the .csv and .bad-1 in mods\mv8\, .bad-2 and the .jsonl in the root
        CreateDirectoryA((base + "\\mods\\mv8").c_str(), nullptr);
        put("mods\\mv8\\tq-uniq-items-mv8.csv", "csv 8\n");
        put("mods\\mv8\\tq-uniq-items-mv8.jsonl.bad-1", "bad 8a\n");
        put("tq-uniq-items-mv8.jsonl.bad-2", "bad 8b\n");
        put("tq-uniq-items-mv8.jsonl", "rows 8\n");
        check(utJournalSetRel(base, "tq-uniq-items-mv8") == "", "a crash remnant is read from the root");
        m = utJournalMoveSet(base, "tq-uniq-items-mv8");
        check(m.result == kUtMoveDone && m.files.size() == 2 && at("mods\\mv8\\tq-uniq-items-mv8.jsonl") == "rows 8\n" &&
                  at("mods\\mv8\\tq-uniq-items-mv8.jsonl.bad-2") == "bad 8b\n" &&
                  at("mods\\mv8\\tq-uniq-items-mv8.csv") == "csv 8\n" &&
                  utJournalSetRel(base, "tq-uniq-items-mv8") == "mods\\mv8",
              "the next start heals a crash remnant: everything in mods\\mv8\\");

        // 9. the folder cannot be created (a FILE named mods\mv9)
        put("tq-uniq-items-mv9.jsonl", "rows 9\n");
        put("mods\\mv9", "a file\n");
        m = utJournalMoveSet(base, "tq-uniq-items-mv9");
        check(m.result == kUtMoveNoFolder && at("tq-uniq-items-mv9.jsonl") == "rows 9\n" &&
                  utJournalSetRel(base, "tq-uniq-items-mv9") == "",
              "no folder: nothing moved, read from the root");
        utJournalMoveLog("tq-uniq-items-mv9", m, "at start");

        // 10. a path that would not fit MAX_PATH is refused, never written under a cut name
        check(!journalOpenSet("tq-uniq-items-mvl", 0, 0, false, std::string(240, 'x').c_str()) &&
                  !journalUsable() && !there(std::string(240, 'x')),
              "a set whose longest path passes MAX_PATH is refused");
        const std::string logText = readAll((base + "\\test_journal.log").c_str());
        (void)logText;
    }

    ut::journalOpenSet(nullptr);
    ut::logShutdown();
    {
        const std::string logText = readAll((std::string(g_dir) + "\\test_journal.log").c_str());
        check(logText.find("the journal tq-uniq-items-mv5 moved into mods\\mv5\\ (at start)") != std::string::npos &&
                  logText.find("was NOT moved into mods\\mv2\\") != std::string::npos &&
                  logText.find("the files already moved went back") != std::string::npos,
              "the log says what moved, what was refused and what went back");
        check(logText.find("the mod uses mods\\mv2\\tq-uniq-items-mv2.jsonl") != std::string::npos &&
                  logText.find("the mod reads the root's tq-uniq-items-mv7.jsonl, which holds the rows") !=
                      std::string::npos &&
                  logText.find("Move or delete mods\\mv7\\tq-uniq-items-mv7.csv by hand") != std::string::npos,
              "a refused move names the journal in use and the file in the way");
        check(logText.find("could NOT be moved back to the root") != std::string::npos &&
                  logText.find("the folder mods\\mv9 could not be created") != std::string::npos &&
                  logText.find("A file in the way of that folder") != std::string::npos &&
                  logText.find("would need paths of up to") != std::string::npos,
              "the log says what split, which folder could not be made and which path was too long");
    }
    {   // journal_dir as the mod reads it (utJournalDirResolved): the same rule as the uninstall guard's
        const std::string base = ut::utJournalDirResolved(g_dir);
        const std::string q = std::string("\"") + g_dir;
        char file[MAX_PATH];
        _snprintf_s(file, sizeof(file), _TRUNCATE, "%s\\jd-a-file.txt", g_dir);
        if (FILE* f = fopen(file, "wb")) fclose(f);
        check(!base.empty() && base == g_dir, "journal_dir: an existing folder is taken as it is");
        check(ut::utJournalDirResolved((std::string(g_dir) + "\\").c_str()) == base &&
                  ut::utJournalDirResolved((std::string(g_dir) + "/ \\ ").c_str()) == base &&
                  ut::utJournalDirResolved((q + "\"").c_str()) == base &&
                  ut::utJournalDirResolved(q.c_str()) == base &&
                  ut::utJournalDirResolved((q + "\\\"  ").c_str()) == base,
              "journal_dir: trailing slashes, spaces and quotes cut, a leading quote alone cut");
        check(ut::utJournalDirResolved(nullptr).empty() && ut::utJournalDirResolved("").empty() &&
                  ut::utJournalDirResolved("\" \\").empty() &&
                  ut::utJournalDirResolved((std::string(g_dir) + "\\no-such-folder").c_str()).empty() &&
                  ut::utJournalDirResolved(file).empty(),
              "journal_dir: empty, a missing folder or a file gives \"\" (the mod folder)");
        DeleteFileA(file);
    }
    printf("\nR. relic and charm rows: takes by row, one shard per take, the crash window\n");
    {
        using namespace ut;
        const std::string::size_type npos = std::string::npos;
        const char* kRelic = "records\\xpack\\item\\relics\\01_act4_test.dbr";
        const char* kRelicKey = "records/xpack/item/relics/01_act4_test.dbr";
        const char* kBonus = "records\\xpack\\item\\relics\\bonus\\b_one.dbr";
        auto rcap = [&](unsigned seed, unsigned var1, const char* bonus) {
            UtReplicaCapture c = cap(kRelic, seed);
            c.var1 = var1;
            _snprintf_s(c.str[kUtIdRelicBonus], kUtIdStrMax, _TRUNCATE, "%s", bonus);
            return c;
        };
        UtRelicRowFacts fx[16];
        auto facts = [&]() { return (int)journalRowsOf(kRelicKey, fx, 16); };
        auto redShards = [&]() { return utRelicRedShards(fx, facts()); };
        auto lineOf = [&](const std::string& t, const char* needle) {
            const size_t at = t.find(needle);
            if (at == npos) return std::string();
            const size_t b = t.rfind('\n', at);
            const size_t e = t.find('\n', at);
            return t.substr(b == npos ? 0 : b + 1, e == npos ? npos : e - (b == npos ? 0 : b + 1));
        };
        bool rb = false;
        char p[MAX_PATH];

        // R1. the green take by row: the oldest, a stale page refused
        const char* Lg = "tq-uniq-items-rl1";
        clean(Lg);
        check(journalOpenSet(Lg), "set rl1");
        unsigned long long g1 = 0, g2 = 0, g3 = 0, pp = 0;
        journalDepositCommit(rcap(1, 3, kBonus), &g1, &rb);
        journalDepositCommit(rcap(2, 3, kBonus), &g2, &rb);
        journalDepositCommit(rcap(3, 3, kBonus), &g3, &rb);
        check(journalRows(kRelicKey) == 3 && journalSaveObserved(now() + 10) == 3,
              "three copies of one completed relic, saved");
        check(!journalTakeRow(kRelicKey, g3, &rb) && journalPendingOut() == 0 && journalCount() == 3,
              "a take naming the newest copy: refused (stale page), nothing changed");
        check(journalTakeRow(kRelicKey, g1, &rb) && journalPendingOut() == 1 && journalRows(kRelicKey) == 2,
              "a take naming the OLDEST copy: taken (pending out)");
        int n = facts();
        check(n == 3 && fx[utRelicGreenChoice(fx, n, kBonus)].seq == g2 && utRelicGreenCount(fx, n, kBonus) == 2,
              "the slot now hands out the next oldest and counts 2 (the out row left out)");
        check(!journalTakeRow(kRelicKey, g1, &rb) && !journalTakeRow(kRelicKey, 999999, &rb),
              "the row taken already, or no such row: refused");
        journalDepositCommit(rcap(4, 2, ""), &pp, &rb);
        check(!journalTakeRow(kRelicKey, pp, &rb) && journalCount() == 4,
              "a whole take of a 2-shard partial: refused (the red slot splits it)");
        UtReplicaCapture got;
        int pend = -1;
        check(journalRowBySeq(g2, &got, &pend) && got.seed == 2 && got.var1 == 3 && pend == 0 &&
                  !strcmp(got.str[kUtIdRelicBonus], kBonus) && !journalRowBySeq(999999, &got, &pend),
              "journalRowBySeq: the row's identity and state; an unknown seq -> false");

        // R1b. a dropped piece reads var1 0 = ONE shard: counted 1, handed out whole, byte-exact
        {
            const char* kSetZero = "tq-uniq-items-rl0";
            clean(kSetZero);
            check(journalOpenSet(kSetZero), "set rl0");
            unsigned long long z = 0, two = 0, zout = 0;
            unsigned zleft = 9;
            journalDepositCommit(rcap(13134, 0, ""), &z, &rb);
            journalDepositCommit(rcap(25, 2, ""), &two, &rb);
            check(journalSaveObserved(now() + 10) == 2 && redShards() == 1 + 2,
                  "a var1 0 piece and a 2/3 piece, saved: the red count is 3 shards");
            bool sp0 = true;
            int n0 = facts();
            const int src0 = utRelicShardSource(fx, n0, &sp0);
            check(src0 >= 0 && fx[src0].seq == z && !sp0,
                  "the shard source is the var1 0 row, handed out whole (not split)");
            check(journalTakeShard(kRelicKey, z, &zout, &zleft, &rb) && zout == z && zleft == 0u &&
                      journalCount() == 2 && journalPendingOut() == 1 && redShards() == 2,
                  "the take: the var1 0 row itself goes out, no row appended; 2 shards stay");
            UtReplicaCapture g0;
            int pend0 = -1;
            check(journalRowBySeq(z, &g0, &pend0) && g0.var1 == 0u && g0.seed == 13134u && pend0 == 2 &&
                      !g0.str[kUtIdRelicBonus][0],
                  "the row handed out keeps var1 0 and its seed (the item the slot hands out, byte-exact)");
            char p0[MAX_PATH];
            pathOf(kSetZero, "jsonl", p0);
            const std::string t0 = readAll(p0);
            const std::string lo = lineOf(t0, "\"pending\":\"out\"");
            check(!lo.empty() && lo.find("\"seed\":13134,\"var1\":0,") != npos,
                  "on disk: the out row is the stored var1 0 row");
        }

        // R2. a split from a SETTLED row: the rows and the bytes
        unsigned long long sp = 0, ss = 0;
        unsigned left = 0;
        auto settledSplit = [&](const char* L) {
            clean(L);
            bool ok = journalOpenSet(L);
            ok = ok && journalDepositCommit(rcap(7, 2, ""), &sp, &rb) && journalSaveObserved(now() + 10) == 1;
            return ok && journalTakeShard(kRelicKey, sp, &ss, &left, &rb) && ss != sp && left == 1u;
        };
        check(settledSplit("tq-uniq-items-rl2"), "a settled 2/3 piece: one shard taken (split), 1 left");
        check(!journalTakeShard(kRelicKey, ss, nullptr, nullptr, &rb), "a shard take naming the out row: refused");
        n = facts();
        check(n == 2 && fx[0].seq == sp && fx[0].var1 == 1 && fx[0].pending == 0 && fx[1].seq == ss &&
                  fx[1].var1 == 1 && fx[1].pending == 2 && redShards() == 1,
              "rows: the source keeps its seq with 1 shard (settled), the shard row is out; red count 1");
        pathOf("tq-uniq-items-rl2", "jsonl", p);
        std::string t = readAll(p);
        std::string lr = lineOf(t, "\"pending\":\"out\"");
        const size_t firstRow = t.find('\n') + 1;
        std::string ls = t.substr(firstRow, t.find('\n', firstRow) - firstRow);
        check(!lr.empty() && lr.find("\"seed\":7,\"var1\":1,") != npos && lr.find("\"taken\":\"") != npos &&
                  ls.find("\"seed\":7,\"var1\":1,") != npos && ls.find("\"pending\"") == npos &&
                  fieldText(ls, "deposited") == fieldText(lr, "deposited") && t.find("depositedVar1") == npos,
              "on disk: the source 1 shard settled, the shard row var1 1 out, the same deposit time and seed");
        UtReplicaCapture src, shard;
        check(journalRowBySeq(sp, &src, nullptr) && journalRowBySeq(ss, &shard, nullptr) &&
                  utReplicaSame(src, shard),
              "the shard row's identity is the source's with var1 1 (the item handed out)");
        unsigned long long whole = 0;
        check(journalTakeShard(kRelicKey, sp, &whole, &left, &rb) && whole == sp && left == 0u &&
                  journalPendingOut() == 2 && redShards() == 0,
              "the next shard: the 1-shard source is taken WHOLE (no split)");

        // R3. the settled source's events (design table 5.3, every row exact)
        check(settledSplit("tq-uniq-items-rl3") && journalSaveObserved(now() + 10) == 1 &&
                  journalCount() == 1 && redShards() == 1,
              "a save after the take: the shard row erased, the source untouched (1 + the save's 1)");
        check(settledSplit("tq-uniq-items-rl4"), "split again (rl4)");
        journalSetHeldSeq(ss);
        check(journalSaveObserved(now() + 10) == 0 && journalPendingOut() == 1,
              "a save while the shard is on the cursor: the shard row kept");
        journalSetHeldSeq(0);
        check(journalTouchOut(ss) && journalSaveObserved(now() + 10) == 1 && journalCount() == 1,
              "it leaves the cursor, the next save erases it");
        check(settledSplit("tq-uniq-items-rl5"), "split again (rl5)");
        unsigned long long back = 0;
        check(journalDepositCommit(rcap(7, 1, ""), &back, &rb) && back == ss && journalPendingOut() == 0 &&
                  journalCount() == 2 && redShards() == 2,
              "the shard dropped back before a save: the take is cancelled, 1 + 1 shards in two rows");
        check(settledSplit("tq-uniq-items-rl6"), "split again (rl6)");
        journalFlushNow();
        check(journalOpenSet("tq-uniq-items-tb") && journalOpenSet("tq-uniq-items-rl6", 1, now() + 1000) &&
                  journalCount() == 2 && journalPendingOut() == 0 && redShards() == 2,
              "a crash, a load by save times: the shard row RESTORED - 2 shards, exact");
        for (int present = 0; present < 2; ++present) {
            const char* L = present ? "tq-uniq-items-rl7" : "tq-uniq-items-rl8";
            check(settledSplit(L), "split again (deferred)");
            journalFlushNow();
            journalOpenSet("tq-uniq-items-tb");
            journalOpenSet(L, 0, 0, true);
            UtPendingRow pr[4];
            const size_t np = journalUnresolvedRows(pr, 4);
            UtVerdictItem v = {np ? pr[0].seq : 0, np ? utPendingVerdict(pr[0].state,
                                                                        present ? kUtFoundPresent : kUtFoundAbsent)
                                                      : 0, present ? "inventory" : nullptr};
            check(np == 1 && pr[0].state == 2 && pr[0].id.var1 == 1u && pr[0].id.seed == 7u &&
                      journalApplyVerdicts(&v, 1) == 1 && journalCount() == (present ? 1u : 2u) &&
                      journalUnresolved() == 0,
                  present ? "deferred load, the shard found: its row erased (1 shard left)"
                          : "deferred load, the shard nowhere: its row restored (2 shards)");
        }

        // R4. a split from a row PENDING IN (no save since its deposit)
        auto inSplit = [&](const char* L) {
            clean(L);
            bool ok = journalOpenSet(L) && journalDepositCommit(rcap(9, 3, ""), &sp, &rb);
            return ok && journalTakeShard(kRelicKey, sp, &ss, &left, &rb) && ss != sp && left == 2u;
        };
        check(inSplit("tq-uniq-items-ri1"), "a 3-shard piece pending in: one shard taken (split), 2 left");
        pathOf("tq-uniq-items-ri1", "jsonl", p);
        t = readAll(p);
        check(t.find("\"var1\":2,\"var2\":0,\"b8\":0,\"pending\":\"in\",\"depositedVar1\":3}") != npos &&
                  t.find("\"var1\":1,\"var2\":0,\"b8\":0,\"pending\":\"out\",\"taken\":\"") != npos,
              "on disk: the source 2 shards pending in with depositedVar1 3, the shard row out");
        check(journalTakeShard(kRelicKey, sp, &ss, &left, &rb) && left == 1u &&
                  readAll(p).find("\"pending\":\"in\",\"depositedVar1\":3}") != npos,
              "a second split keeps depositedVar1 3 (the count the character file holds)");
        check(journalSaveObserved(now() + 10) == 3 && journalFlushNow() && journalCount() == 1 &&
                  redShards() == 1 && readAll(p).find("depositedVar1") == npos,
              "a save after the takes: the source settles (the key goes), both shard rows erased");
        check(inSplit("tq-uniq-items-ri2"), "split again (ri2)");
        journalSetHeldSeq(ss);
        check(journalSaveObserved(now() + 10) == 1 && journalPendingIn() == 0 && journalPendingOut() == 1,
              "a save while the shard is on the cursor: the source settles, the shard row kept");
        journalSetHeldSeq(0);
        check(inSplit("tq-uniq-items-ri3") && journalDepositCommit(rcap(9, 1, ""), &back, &rb) && back == ss &&
                  journalPendingOut() == 0 && journalPendingIn() == 1 && redShards() == 3,
              "the shard dropped back before a save: cancelled; the source still pending in");
        check(inSplit("tq-uniq-items-ri4"), "split again (ri4)");
        journalFlushNow();
        journalOpenSet("tq-uniq-items-tb");
        journalOpenSet("tq-uniq-items-ri4", 0, 0, true);
        {
            UtPendingRow pr[4];
            const size_t np = journalUnresolvedRows(pr, 4);
            int ri = -1, si = -1;
            for (size_t i = 0; i < np; ++i) (pr[i].state == 1 ? ri : si) = (int)i;
            check(np == 2 && ri >= 0 && si >= 0 && pr[ri].id.var1 == 3u && pr[si].id.var1 == 1u,
                  "a crash, a deferred load: the source is looked for with depositedVar1 3, the shard with 1");
            UtVerdictItem v[2] = {{pr[ri >= 0 ? ri : 0].seq, utPendingVerdict(1, kUtFoundPresent), "inventory"},
                                  {pr[si >= 0 ? si : 0].seq, utPendingVerdict(2, kUtFoundAbsent), nullptr}};
            check(journalApplyVerdicts(v, 2) == 2 && journalCount() == 1 && redShards() == 1,
                  "the 3/5 piece is in the bags: the source erased, the shard restored (the taken shard "
                  "is the named duplicate, never a loss)");
        }
        check(inSplit("tq-uniq-items-ri5"), "split again (ri5)");
        journalFlushNow();
        journalOpenSet("tq-uniq-items-tb");
        journalOpenSet("tq-uniq-items-ri5", 0, 0, true);
        check(journalUnresolvedFallback("test") == 2 && journalCount() == 2 && journalPendingIn() == 1 &&
                  journalPendingOut() == 0,
              "the check cannot run: today's rule - the shard restored, the source reported");
        journalFlushNow();
        pathOf("tq-uniq-items-ri5", "jsonl", p);
        check(readAll(p).find("\"pending\":\"in\",\"depositedVar1\":3}") != npos,
              "depositedVar1 round-trips through a reload and a write");
        check(journalSaveObserved(now() + 10) == 1 && journalPendingIn() == 0 && redShards() == 3,
              "the next save settles the source: 2 + 1 shards (all 3 duplicated at worst - today's case)");

        // R5. a failed write puts the pre-image back
        clean("tq-uniq-items-rf");
        check(journalOpenSet("tq-uniq-items-rf") && journalDepositCommit(rcap(5, 3, ""), &sp, &rb),
              "set rf: one 3-shard piece pending in");
        pathOf("tq-uniq-items-rf", "jsonl", p);
        const std::string before = readAll(p);
        HANDLE lock = CreateFileA(p, GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        rb = false;
        const bool took = journalTakeShard(kRelicKey, sp, &ss, &left, &rb);
        if (lock != INVALID_HANDLE_VALUE) CloseHandle(lock);
        n = facts();
        check(lock != INVALID_HANDLE_VALUE && !took && rb && ss == 0 && n == 1 && fx[0].var1 == 3 &&
                  fx[0].pending == 1 && readAll(p) == before,
              "the file locked: the split fails, rolled back - one row of 3 shards, the bytes unchanged");
        journalFlushNow();

        // R6. identical rows against the containers: k = 3 rows of one identity, m live items
        for (int state = 1; state <= 2; ++state) {
            for (int m = 0; m <= 3; ++m) {
                char L[64];
                _snprintf_s(L, sizeof(L), _TRUNCATE, "tq-uniq-items-rk%d%d", state, m);
                clean(L);
                journalOpenSet(L);
                for (int k = 0; k < 3; ++k) journalDepositCommit(rcap(11, 1, ""), &sp, &rb);
                if (state == 2) {
                    journalSaveObserved(now() + 10);
                    for (int k = 0; k < 3; ++k) {
                        n = facts();
                        bool sx = false;
                        const int s = utRelicShardSource(fx, n, &sx);
                        if (s >= 0) journalTakeShard(kRelicKey, fx[s].seq, &ss, &left, &rb);
                    }
                }
                journalFlushNow();
                journalOpenSet("tq-uniq-items-tb");
                journalOpenSet(L, 0, 0, true);
                UtPendingRow pr[4];
                const size_t np = journalUnresolvedRows(pr, 4);
                const UtReplicaCapture* ids[4];
                int found[4] = {0, 0, 0, 0};
                for (size_t i = 0; i < np; ++i) ids[i] = &pr[i].id;
                const UtReplicaCapture live = rcap(11, 1, "");
                for (int j = 0; j < m; ++j) {
                    const int hit = utMatchLive(ids, found, (int)np, live);
                    if (hit >= 0) found[hit] = 1;
                }
                UtVerdictItem v[4];
                for (size_t i = 0; i < np; ++i) {
                    v[i].seq = pr[i].seq;
                    v[i].verdict = utPendingVerdict(pr[i].state, found[i] ? kUtFoundPresent : kUtFoundAbsent);
                    v[i].where = found[i] ? "inventory" : nullptr;
                }
                const int applied = journalApplyVerdicts(v, np);
                char what[160];
                _snprintf_s(what, sizeof(what), _TRUNCATE,
                            "3 identical rows pending %s against %d live item(s): %d erased, %d %s", state == 1 ? "in" : "out",
                            m, m, 3 - m, state == 1 ? "settled" : "restored");
                check(np == 3 && applied == 3 && journalCount() == (size_t)(3 - m) && journalUnresolved() == 0 &&
                          journalPendingIn() == 0 && journalPendingOut() == 0,
                      what);
            }
        }

        // R7. the downgrade: a journal this build wrote, read by a reader that skips depositedVar1
        clean("tq-uniq-items-rd");
        check(journalOpenSet("tq-uniq-items-rd") && journalDepositCommit(rcap(21, 3, ""), &sp, &rb) &&
                  journalTakeShard(kRelicKey, sp, &ss, &left, &rb) && journalDepositCommit(rcap(22, 2, ""), &pp, &rb),
              "set rd: a split row pending in (depositedVar1 3), its shard out, a 2/3 piece");
        pathOf("tq-uniq-items-rd", "jsonl", p);
        std::string fixture = readAll(p);
        {   // the 2/3 piece is made settled in the text: its "pending":"in" is the file's last one
            const size_t lastIn = fixture.rfind(",\"pending\":\"in\"}");
            if (lastIn != npos) fixture.erase(lastIn, strlen(",\"pending\":\"in\""));
        }
        const std::string key = ",\"depositedVar1\":3";
        std::string stripped = fixture;
        const size_t kat = stripped.find(key);
        if (kat != npos) stripped.erase(kat, key.size());
        check(kat != npos && fixture.find("\"seed\":22,\"var1\":2,\"var2\":0,\"b8\":0}") != npos,
              "the fixture: a settled 2/3, a split row pending in with depositedVar1, a shard row out");
        std::string written[2];
        const char* leaves[2] = {"tq-uniq-items-rdn", "tq-uniq-items-rdo"};
        for (int k = 0; k < 2; ++k) {
            clean(leaves[k]);
            pathOf(leaves[k], "jsonl", p);
            writeAll(p, k == 0 ? fixture : stripped);
            journalOpenSet("tq-uniq-items-tb");
            journalOpenSet(leaves[k], 0, 0, true);
            n = facts();
            journalFlushNow();
            check(journalCount() == 3 && n == 3 && countBad(leaves[k]) == 0,
                  k == 0 ? "this build reads the three rows" : "the key stripped (today's reader): the same three rows");
            const std::string w = readAll(p);
            written[k] = w.substr(w.find('\n') + 1);
        }
        std::string expect = written[0];
        const size_t wk = expect.find(key);
        if (wk != npos) expect.erase(wk, key.size());
        check(wk != npos && written[1] == expect,
              "written back: every row byte-equal except the key (the out row restored by the same load rule)");
        stripped = fixture;
        if (kat != npos) stripped.replace(kat, key.size(), ",\"depositedVar1\":-1");
        clean("tq-uniq-items-rdb");
        pathOf("tq-uniq-items-rdb", "jsonl", p);
        writeAll(p, stripped);
        journalOpenSet("tq-uniq-items-rdb", 0, 0, true);
        journalFlushNow();
        check(journalCount() == 3 && readAll(p).find("depositedVar1") == npos,
              "a depositedVar1 that is not a plain number: the row kept, the key dropped");
        journalOpenSet(nullptr);
    }

    printf("\n%s - %d passed, %d failed\n", g_fail ? "JOURNAL TESTS FAILED" : "ALL PASS", g_pass,
           g_fail);
    return g_fail ? 1 : 0;
}
