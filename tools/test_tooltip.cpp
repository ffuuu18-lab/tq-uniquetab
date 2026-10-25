// test_tooltip.cpp - the OFFLINE proof of the tooltip line. NO GAME, NO ENGINE.
// Ported from GD's tools\test_tooltip.cpp (the swap); the compare-byte case is gone with the
// compare byte (ut_tooltip.h), and TQ adds the latch rule, the memo, the texts, the classes, the
// line read-back and the fault latch.
//
//   tools\build_test_tooltip.bat        (finds vcvars32 itself, like build.bat: x86, as the mod)
//
// It links the REAL src\ut_tooltip.cpp and src\ut_config.cpp - so the code under test is the code
// the game runs - with stubbed catalogue / journal / record lookups, and proves:
//
//   1. tooltipSwapBuild over an N-line VS2012 vector ({first,last,end}, 0x20-byte GameTextLine)
//      produces exactly N+1 records, the first N byte for byte identical to the engine's, the
//      mod's static line last, and a {first,last,end} triple whose span is (N+1) * 0x20; a stub
//      standing in for the trampoline sees that array and nothing else; the engine's own vector
//      object is never written;
//   2. every refusal (empty, over-long, not a multiple of 0x20, end < begin, an unreadable pointer
//      that FAULTS inside the mod's SEH) allocates nothing, and the guard bracket stays balanced;
//   3. nothing leaks: the borrowed-array balance is 0 after 5,000 build/free pairs;
//   4. the latch rule: the exact fingerprint, the prefix one, and every near miss;
//   5. the memo: one catalogue + journal question per record per 250 ms, case-insensitive, a
//      non-catalogue record answered "no line" without asking the journal; and no line
//      and NO memo while the journal's save set is not open;
//   6. the classes: the registered 0..57 are taken, anything else falls back to the default;
//   7. the read-back of a built line (class, VS2012 wstring in both its SSO and heap forms, the
//      bool), and every mismatch refused;
//   8. the texts from the ini (a set one, an empty one = the built-in wording), and the FAULT
//      LATCH: with no Game.dll in this process tooltipInit turns the line OFF with ONE capitalised
//      ERROR line, a second init says so again without re-arming, and tooltipInstall installs
//      nothing (0 of 6).
//
//   9. the REAL detour bodies (`hk_*`, `latchBody`, `pendingLine`) driven through a
//      test-only seam (UT_TOOLTIP_TEST_SEAM, never in the mod) with stub trampolines: the
//      ItemEquipment detour overwrites the base's partial latch; an artifact gets the line; a
//      non-catalogue record gets none; rule (b) (the first ToString consumes the latch, matching or
//      not); tooltip_mark=0 clears the latch; a closed journal set latches nothing; the `__finally`
//      frees the borrowed array when the trampoline RAISES; a faulting name read is swallowed.
//
// WHAT IS NOT COVERED (said here so nobody reads more into a pass than is in it): the engine's own
// trampolines, the MinHook install and the rollover's pixels run only in the game.
//
// Exit code 0 = all of it held.
#include <windows.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MinHook.h"

#include "../src/tq_exports.h"
#include "../src/tq_runtime.h"
#include "../src/ut_config.h"
#include "../src/ut_log.h"
#include "../src/ut_depositgate.h"   // UtRollKey
#include "../src/ut_tooltip.h"
#include "../src/ut_proto.h"       // utVsStringSet (the hovered relic's replica)
#include "../src/ut_relicpage.h"   // the relic list the rollover line asks

// ---- stubs: everything ut_tooltip.cpp calls that is not ut_config.cpp ------------------------
static char g_logBuf[65536];
static size_t g_logLen = 0;
static bool logHas(const char* needle) { return strstr(g_logBuf, needle) != nullptr; }
static int logCount(const char* needle) {
    int n = 0;
    for (const char* p = strstr(g_logBuf, needle); p; p = strstr(p + 1, needle)) ++n;
    return n;
}

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

// The deliberate guarded-read bracket (tq_runtime's utGuardEnter/Leave). Counted, so the test can assert
// it is balanced even when the read inside it FAULTS - an unbalanced depth would make dllmain's
// vectored handler go quiet for the rest of the session.
long g_guardDepth = 0;
long g_guardMax = 0;
void utGuardEnter() {
    ++g_guardDepth;
    if (g_guardDepth > g_guardMax) g_guardMax = g_guardDepth;
}
void utGuardLeave() { --g_guardDepth; }

// The record, the catalogue and the journal. Counted, so the memo can be proven.
int g_liveAsks = 0;
int g_storeAsks = 0;
// TQ's record read calls Object::GetObjectName through g_tq; section 9 points it at a stub that
// returns a FakeItem's name. The journal's save set: open, unless a row closes it.
TqRuntime g_tq;
bool g_setKnown = true;
bool journalSetKnown() { return g_setKnown; }
// the cost meter (hooks.cpp in the mod) - the detours stamp their own time
long long probeNow() { return 0; }
void probePresentAdd(long long) {}
bool liveHasRecord(const char* folded) {
    ++g_liveAsks;
    return !_stricmp(folded, "records/items/a/unique_a.dbr") ||
           !_stricmp(folded, "records/items/b/unique_b.dbr") ||
           !_stricmp(folded, "records/items/m/mi_l_m.dbr");
}
unsigned int storeCount(const char* record) {
    ++g_storeAsks;
    return (!_stricmp(record, "records/items/a/unique_a.dbr") ||
            !_stricmp(record, "records/items/m/mi_l_m.dbr")) ? 1u : 0u;
}
// The KIND (ut_live's table) and the stored roll (ut_store's journal read): one held monster
// infrequent, mi_l_m, whose row holds prefix ar_one, no suffix, seed 77. Counted, for the memo.
int g_rollAsks = 0;
int liveRecordKind(const char* record) {
    return !_stricmp(record, "records/items/m/mi_l_m.dbr") ? 1 : 0;
}
bool storeRollOf(const char* record, UtRollKey* out) {
    ++g_rollAsks;
    utRollClear(out);
    if (_stricmp(record, "records/items/m/mi_l_m.dbr") != 0) return false;
    return utRollSet("records/affix/ar_one.dbr", "", 77u, out);
}
// The capture's vtable test (ut_proto's isItemObject): a FakeMi is an Item unless a case says not.
bool g_isItem = true;
bool protoIsItem(const void*) { return g_isItem; }
// The relic page's list (ut_live) and the record's journal facts (ut_rescue): one relic of three
// shards and two bonuses; the facts are the case's.
UtRelicList g_relicList;
UtRelicRowFacts g_relicFacts[8];
size_t g_relicFactN = 0;
const UtRelicList* liveRelicList() { return g_relicList.rows.empty() ? nullptr : &g_relicList; }
int liveRelicFind(const char* folded) {
    return !g_relicList.rows.empty() && !_stricmp(folded, "records/items/relic/r1.dbr") ? 0 : -1;
}
size_t journalRowsOf(const char*, UtRelicRowFacts* out, size_t cap) {
    for (size_t i = 0; i < g_relicFactN && i < cap; ++i) out[i] = g_relicFacts[i];
    return g_relicFactN;
}
}  // namespace ut

// MinHook is never reached with a live target (tooltipInit refuses first), but ut_tooltip.cpp
// references these, so the link needs them.
extern "C" {
MH_STATUS WINAPI MH_CreateHook(LPVOID, LPVOID, LPVOID*) { return MH_ERROR_NOT_INITIALIZED; }
MH_STATUS WINAPI MH_EnableHook(LPVOID) { return MH_ERROR_NOT_INITIALIZED; }
MH_STATUS WINAPI MH_DisableHook(LPVOID) { return MH_ERROR_NOT_INITIALIZED; }
MH_STATUS WINAPI MH_RemoveHook(LPVOID) { return MH_ERROR_NOT_INITIALIZED; }
const char* WINAPI MH_StatusToString(MH_STATUS) { return "stub"; }
}

// ---- the fixture -----------------------------------------------------------------------------
static const size_t kLine = 0x20;   // sizeof(GAME::GameTextLine), TQ x86

// VS2012 std::vector<GameTextLine> as GameTextLineToString reads it: {first, last, end}.
struct FakeVec {
    const unsigned char* begin;
    const unsigned char* end;
    const unsigned char* cap;
};
static_assert(sizeof(FakeVec) == sizeof(ut::UtTooltipSwap), "the borrowed triple is a vector");
#if defined(_M_IX86)
static_assert(sizeof(FakeVec) == 12, "VS2012 x86 std::vector is three 4-byte pointers");
#endif

static int g_fails = 0;
static void check(bool ok, const char* what) {
    printf("  %-66s %s\n", what, ok ? "OK" : "FAIL");
    if (!ok) ++g_fails;
}

// Fills `n` 0x20-byte records with a per-record pattern that no memcpy bug could reproduce by
// accident: byte i of record r is (r * 7 + i * 13 + 1) & 0xFF, with the class word at +0 set to
// a plausible GameTextClass so the record also looks like a real line.
static unsigned char* makeLines(size_t n) {
    unsigned char* buf = (unsigned char*)malloc(n * kLine);
    for (size_t r = 0; r < n; ++r) {
        for (size_t i = 0; i < kLine; ++i) {
            buf[r * kLine + i] = (unsigned char)((r * 7 + i * 13 + 1) & 0xFF);
        }
        const unsigned int cls = (r == 0) ? 0x05u : 0x0Fu;
        memcpy(buf + r * kLine, &cls, 4);
    }
    return buf;
}

// The mod's prepared line, as a recognisable 0x20-byte blob.
static void makeExtra(unsigned char* out, unsigned int cls) {
    memset(out, 0xA5, kLine);
    memcpy(out, &cls, 4);
}

// The stubbed TRAMPOLINE: this is what the engine's GameTextLineToString would be. It records
// what it was handed so the test can assert the borrowed triple, not just the array.
static const unsigned char* g_sawBegin = nullptr;
static const unsigned char* g_sawEnd = nullptr;
static size_t g_sawCount = 0;
static void stubTrampoline(const void* vec) {
    const FakeVec* v = (const FakeVec*)vec;
    g_sawBegin = v->begin;
    g_sawEnd = v->end;
    g_sawCount = (size_t)(v->end - v->begin) / kLine;
}

static bool caseBuild(size_t n) {
    unsigned char* lines = makeLines(n);
    unsigned char extra[kLine];
    makeExtra(extra, 0x16);
    FakeVec vec;
    vec.begin = lines;
    vec.end = lines + n * kLine;
    vec.cap = vec.end;
    // A copy of the engine's vector object and of its lines, to prove neither is written.
    FakeVec before = vec;
    unsigned char* mirror = (unsigned char*)malloc(n * kLine);
    memcpy(mirror, lines, n * kLine);

    unsigned char* owned = nullptr;
    ut::UtTooltipSwap borrowed;
    memset(&borrowed, 0, sizeof(borrowed));
    bool ok = ut::tooltipSwapBuild(&vec, extra, &owned, &borrowed);
    if (!ok) {
        free(lines);
        free(mirror);
        return false;
    }
    stubTrampoline(&borrowed);

    bool good = true;
    good = good && owned != nullptr && borrowed.begin == owned;
    good = good && (size_t)(borrowed.end - borrowed.begin) == (n + 1) * kLine;
    good = good && borrowed.cap == borrowed.end;
    good = good && g_sawCount == n + 1;
    good = good && g_sawBegin == owned && g_sawEnd == borrowed.end;
    // the engine's N, byte for byte
    good = good && memcmp(owned, mirror, n * kLine) == 0;
    // the mod's static line, last, byte for byte
    good = good && memcmp(owned + n * kLine, extra, kLine) == 0;
    // the engine's own vector object and its storage are untouched
    good = good && memcmp(&before, &vec, sizeof(vec)) == 0;
    good = good && memcmp(lines, mirror, n * kLine) == 0;
    good = good && ut::tooltipSwapAllocBalance() == 1;

    ut::tooltipSwapFree(owned);
    good = good && ut::tooltipSwapAllocBalance() == 0;
    free(lines);
    free(mirror);
    return good;
}

static bool refuses(const void* vec, const char* label) {
    unsigned char extra[kLine];
    makeExtra(extra, 0x16);
    unsigned char* owned = (unsigned char*)(size_t)0xDEADBEEF;  // must be nulled by the callee
    ut::UtTooltipSwap borrowed;
    memset(&borrowed, 0xEE, sizeof(borrowed));
    const bool built = ut::tooltipSwapBuild(vec, extra, &owned, &borrowed);
    const bool ok = !built && owned == nullptr && ut::tooltipSwapAllocBalance() == 0;
    printf("      %-40s built=%d owned=%p balance=%ld\n", label, built ? 1 : 0, (void*)owned,
           ut::tooltipSwapAllocBalance());
    return ok;
}

// A GameTextLine as the engine's ctor leaves it (Game.dll 0x1ABCE0): class, VS2012 wstring
// (SSO below 8 characters, else a heap pointer), the bool.
static void makeLine(unsigned char* line, unsigned cls, const unsigned short* text, size_t len,
                     unsigned char flag) {
    memset(line, 0, kLine);
    memcpy(line, &cls, 4);
    if (len < 8) {
        memcpy(line + 0x04, text, (len + 1) * 2);
        const size_t res = 7;
        memcpy(line + 0x18, &res, 4);
    } else {
        memcpy(line + 0x04, &text, sizeof(text));
        memcpy(line + 0x18, &len, 4);
    }
    memcpy(line + 0x14, &len, 4);
    line[0x1C] = flag;
}

// ---- section 9: the detour bodies driven with stub trampolines -------------------
struct FakeItem {
    const char* name;
};
static unsigned char g_rollBuf[16 * kLine];

static const char* __fastcall stubObjectName(const void* obj, void* /*edx*/) {
    return ((const FakeItem*)obj)->name;
}
static void rollReset(FakeVec* v) {
    memset(g_rollBuf, 0, sizeof(g_rollBuf));
    v->begin = g_rollBuf;
    v->end = g_rollBuf;
    v->cap = g_rollBuf + sizeof(g_rollBuf);
}
static unsigned lineCount(const FakeVec& v) { return (unsigned)((size_t)(v.end - v.begin) / kLine); }
static void rollAppend(void* lines, unsigned int cls) {
    FakeVec* v = (FakeVec*)lines;
    if (v->end + kLine > v->cap) return;
    unsigned char* line = (unsigned char*)v->end;
    memset(line, (int)(0x40 + cls), kLine);   // a per-class pattern, the class word at +0
    *(unsigned int*)line = cls;
    v->end += kLine;
}
// Item::GetUIDisplayText: three lines (the name, the type, the damage).
static void __fastcall stubBaseText(void*, void*, const void*, void* lines) {
    rollAppend(lines, 0x01);
    rollAppend(lines, 0x05);
    rollAppend(lines, 0x07);
}
// ItemEquipment::GetUIDisplayText CALLS the (detoured) base, then appends its own two lines -
// Game.dll 0x1C00D5, the reason the base detour's latch sees a PARTIAL vector.
static void __fastcall stubEquipText(void* item, void*, const void* character, void* lines) {
    ut::tooltipTestBaseText(item, character, lines);
    rollAppend(lines, 0x18);
    rollAppend(lines, 0x09);
}
static void __fastcall stubArtifactText(void*, void*, const void*, void* lines) {
    rollAppend(lines, 0x03);
    rollAppend(lines, 0x0B);
    rollAppend(lines, 0x0C);
    rollAppend(lines, 0x0D);
}
static void __fastcall stubRelicText(void*, void*, const void*, void* lines) {
    rollAppend(lines, 0x04);
    rollAppend(lines, 0x18);
}
// ItemArtifactFormula::GetUIDisplayText: its own lines, with the CRAFTED artifact's (detoured)
// text builder run on the SAME vector part-way through - the worst case the depth covers.
static const void* g_crafted = nullptr;
static int g_innerDepth = -1;
static void __fastcall stubFormulaText(void*, void*, const void* character, void* lines) {
    rollAppend(lines, 0x0B);
    rollAppend(lines, 0x2A);
    g_innerDepth = ut::tooltipTestFormulaDepth();
    if (g_crafted) ut::tooltipTestArtifactText(const_cast<void*>(g_crafted), character, lines);
    rollAppend(lines, 0x2C);
}
// GameTextLineToString's trampoline: records what it was handed, and RAISES when asked to (an
// engine exception unwinding through the swap).
static const void* g_seenVec = nullptr;
static unsigned g_seenCount = 0;
static int g_seenLast = -1;  // 1 = COLLECTED, 2 = NOT COLLECTED, 3 = ANOTHER ROLL, 0 = an engine line
static bool g_raise = false;
// the relic case copies the lines and their texts while the borrowed array is alive
static unsigned char (*g_copyLines)[kLine] = nullptr;
static unsigned short (*g_copyText)[64] = nullptr;
static void __cdecl stubToString(const void* lines, void*) {
    const FakeVec* v = (const FakeVec*)lines;
    g_seenVec = lines;
    g_seenCount = lineCount(*v);
    for (unsigned i = 0; g_copyLines && i < g_seenCount && i < 8; ++i) {
        const unsigned char* l = v->begin + i * kLine;
        memcpy(g_copyLines[i], l, kLine);
        size_t len = 0, res = 0;
        memcpy(&len, l + 0x14, 4);
        memcpy(&res, l + 0x18, 4);
        const unsigned short* t = (const unsigned short*)(l + 0x04);
        if (res >= 8) memcpy(&t, l + 0x04, sizeof(t));
        size_t k = 0;
        for (; len < 64 && k < len; ++k) g_copyText[i][k] = t[k];
        g_copyText[i][k] = 0;
    }
    g_seenLast = 0;
    if (g_seenCount) {
        const unsigned char* last = v->end - kLine;
        if (!memcmp(last, ut::tooltipTestLine(true), kLine)) g_seenLast = 1;
        else if (!memcmp(last, ut::tooltipTestLine(false), kLine)) g_seenLast = 2;
        else if (!memcmp(last, ut::tooltipTestRollLine(), kLine)) g_seenLast = 3;
    }
    if (g_raise) RaiseException(0xE0540001, 0, 0, nullptr);
}
// The hovered monster infrequent: the name first (stubObjectName reads it as a FakeItem), then
// what Item::GetPrefixName / GetSuffixName and the replica's seed give.
struct FakeMi {
    const char* name;
    const char* prefix;
    const char* suffix;
    unsigned seed;
    unsigned char replica[0xBC];   // the item's own replica: the seed at +0x7C (placeSeed)
};
static void placeSeed(FakeMi* m) { memcpy(m->replica + 0x7C, &m->seed, 4); }
static int g_affixCalls = 0;
static const char* __fastcall stubPrefix(void* obj, void*) {
    ++g_affixCalls;
    return ((const FakeMi*)obj)->prefix;
}
static const char* __fastcall stubSuffix(void* obj, void*) {
    return ((const FakeMi*)obj)->suffix;
}
static int g_replicaCalls = 0;
static void __fastcall stubReplica(const void*, void*, void*) {
    ++g_replicaCalls;   // bound, but the roll read never calls it: the seed is a plain read
}

static void seenReset() {
    g_seenVec = nullptr;
    g_seenCount = 0;
    g_seenLast = -1;
}
// No C++ object lives here, so this frame may catch the raise.
static bool toStringRaising(const void* lines) {
    char out[32];
    __try {
        ut::tooltipTestToString(lines, out);
        return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return true;
    }
}

int main() {
    printf("the tooltip line, offline (TQ x86: 0x20-byte GameTextLine, VS2012 vector)\n\n");
    printf("1. the shape of the borrowed array\n");
    check(ut::tooltipSwapAllocBalance() == 0, "balance is 0 before anything is built");
    check(caseBuild(1), "N=1   -> 2 records, original first, static line last");
    check(caseBuild(20), "N=20  -> 21 records, original first, static line last");
    check(caseBuild(37), "N=37  -> 38 records, original first, static line last");
    check(caseBuild(512), "N=512 -> 513 records (the cap is inclusive)");

    printf("\n2. refusals allocate nothing\n");
    unsigned char* lines = makeLines(4);
    FakeVec v;
    bool all = true;

    v.begin = lines;
    v.end = lines;
    v.cap = lines;
    all = refuses(&v, "empty vector (N = 0)") && all;

    v.begin = lines;
    v.end = lines + 4 * kLine + 7;  // not a whole number of records
    v.cap = v.end;
    all = refuses(&v, "span is not a multiple of 0x20") && all;

    v.begin = lines;
    v.end = lines + 2 * kLine + 0x10;  // a GD-sized (0x40) record count would land here
    v.cap = v.end;
    all = refuses(&v, "half a record (0x10 past a whole one)") && all;

    v.begin = lines + 4 * kLine;
    v.end = lines;  // end before begin
    v.cap = lines;
    all = refuses(&v, "end < begin") && all;

    v.begin = lines;
    v.end = lines + 513 * kLine;  // over the 512-line cap
    v.cap = v.end;
    all = refuses(&v, "N = 513, past the sanity cap") && all;

    all = refuses((const void*)(size_t)0x10, "an UNREADABLE vector pointer (SEH)") && all;
    all = refuses(nullptr, "a null vector") && all;
    check(all, "every refusal returned false, nulled the owner and allocated nothing");
    check(ut::g_guardDepth == 0, "the guard bracket is balanced, faults included");
    check(ut::g_guardMax > 0, "the guard bracket was actually raised around the engine reads");
    free(lines);

    printf("\n3. nothing leaks over 5,000 build/free pairs\n");
    {
        unsigned char extra[kLine];
        makeExtra(extra, 0x18);
        unsigned char* src = makeLines(30);
        FakeVec big;
        big.begin = src;
        big.end = src + 30 * kLine;
        big.cap = big.end;
        long peak = 0;
        for (int i = 0; i < 5000; ++i) {
            unsigned char* owned = nullptr;
            ut::UtTooltipSwap b;
            memset(&b, 0, sizeof(b));
            if (!ut::tooltipSwapBuild(&big, extra, &owned, &b)) {
                check(false, "a build failed");
                break;
            }
            if (ut::tooltipSwapAllocBalance() > peak) peak = ut::tooltipSwapAllocBalance();
            ut::tooltipSwapFree(owned);
        }
        printf("      peak balance during the loop = %ld\n", peak);
        check(peak == 1, "at most one borrowed array is ever alive");
        check(ut::tooltipSwapAllocBalance() == 0, "balance is 0 again after 5,000 pairs");
        check(ut::g_guardDepth == 0, "the guard bracket is still balanced");
        free(src);
    }

    printf("\n4. the latch rule (count, first class, last class)\n");
    check(ut::tooltipLatchMatch(12, 0x05, 0x10, 12, 0x05, 0x10, false, 0) == 1,
          "the same vector handed straight through -> EXACT");
    check(ut::tooltipLatchMatch(12, 0x05, 0x10, 14, 0x05, 0x11, true, 0x10) == 2,
          "two lines appended after it, the latched last at 11 -> PREFIX");
    check(ut::tooltipLatchMatch(12, 0x05, 0x10, 14, 0x05, 0x11, true, 0x0F) == 0,
          "more lines but another class at the prefix end -> no match");
    check(ut::tooltipLatchMatch(12, 0x05, 0x10, 14, 0x05, 0x11, false, 0x10) == 0,
          "more lines but the prefix end unreadable -> no match");
    check(ut::tooltipLatchMatch(12, 0x05, 0x10, 12, 0x06, 0x10, false, 0) == 0,
          "another first class (another item's name) -> no match");
    check(ut::tooltipLatchMatch(12, 0x05, 0x10, 12, 0x05, 0x11, false, 0) == 0,
          "the same count, another last class -> no match");
    check(ut::tooltipLatchMatch(12, 0x05, 0x10, 3, 0x05, 0x10, false, 0) == 0,
          "FEWER lines (a vendor price text, say) -> no match");
    check(ut::tooltipLatchMatch(0, 0x05, 0x10, 12, 0x05, 0x10, false, 0) == 0,
          "an empty latch never matches");

    printf("\n5. the memo in front of the catalogue and the journal\n");
    {
        bool memo = true;
        int s = ut::tooltipCollectedState("records/items/a/unique_a.dbr", &memo);
        check(s == 1 && !memo && ut::g_liveAsks == 1 && ut::g_storeAsks == 1,
              "a collected catalogue record -> 1, asked once");
        s = ut::tooltipCollectedState("RECORDS/ITEMS/A/UNIQUE_A.DBR", &memo);
        check(s == 1 && memo && ut::g_liveAsks == 1 && ut::g_storeAsks == 1,
              "asked again at once (any case) -> the memo, nothing asked");
        s = ut::tooltipCollectedState("records/items/b/unique_b.dbr", &memo);
        check(s == 2 && !memo && ut::g_storeAsks == 2, "another record, not collected -> 2, asked");
        s = ut::tooltipCollectedState("records/items/c/common_c.dbr", &memo);
        check(s == 3 && !memo && ut::g_liveAsks == 3 && ut::g_storeAsks == 2,
              "not a catalogue record -> 3 (no line), the journal never asked");
        s = ut::tooltipCollectedState("records/items/c/common_c.dbr", &memo);
        check(s == 3 && memo && ut::g_liveAsks == 3, "and that answer is memoised too");
        Sleep(300);
        s = ut::tooltipCollectedState("records/items/c/common_c.dbr", &memo);
        check(s == 3 && !memo && ut::g_liveAsks == 4, "300 ms later the memo is stale -> asked");
        // the journal's save set not open (a world still loading, or a set that could
        // not be opened) - storeCount would answer 0 for every record.
        s = ut::tooltipCollectedState("records/items/a/unique_a.dbr", &memo);
        check(s == 1 && !memo, "unique_a asked fresh with the set open -> 1");
        const int live0 = ut::g_liveAsks, store0 = ut::g_storeAsks;
        ut::g_setKnown = false;
        s = ut::tooltipCollectedState("records/items/a/unique_a.dbr", &memo);
        check(s == 3 && !memo && ut::g_liveAsks == live0 && ut::g_storeAsks == store0,
              "the set CLOSED: a collected record -> 3 (no line), even over a fresh memo");
        s = ut::tooltipCollectedState("records/items/b/unique_b.dbr", &memo);
        check(s == 3 && !memo && ut::g_storeAsks == store0,
              "and an uncollected one -> 3, never a false 'Not in your collection'");
        ut::g_setKnown = true;
        s = ut::tooltipCollectedState("records/items/a/unique_a.dbr", &memo);
        check(s == 1 && !memo && ut::g_storeAsks == store0 + 1,
              "the set open again -> asked at once (the closed answer was NOT memoised)");
    }

    printf("\n6. the text classes (0..57 registered, TQ's defaults 0x16 / 0x18)\n");
    check(ut::tooltipPickClass(22, 0x16, "tooltip_class_yes") == 0x16, "22 -> 0x16 ItemSetBonuses");
    check(ut::tooltipPickClass(24, 0x18, "tooltip_class_no") == 0x18, "24 -> 0x18 ItemRelicNumber");
    check(ut::tooltipPickClass(0, 0x16, "tooltip_class_yes") == 0x00, "0 (Default) is registered");
    check(ut::tooltipPickClass(57, 0x16, "tooltip_class_yes") == 0x39, "57 (PetBonusNext) is the last");
    check(ut::tooltipPickClass(58, 0x16, "tooltip_class_yes") == 0x16,
          "58 is not registered -> the default");
    check(ut::tooltipPickClass(-1, 0x18, "tooltip_class_no") == 0x18, "-1 -> the default");
    check(ut::tooltipPickClass(84, 0x18, "tooltip_class_no") == 0x18,
          "84 (a GD class number) -> the default");
    check(logHas("is not a text style this game registers (0..57)"), "and the refusal is logged");

    printf("\n7. the read-back of a built GameTextLine (0x20 bytes)\n");
    {
        static const unsigned short kLong[] = {'I', 'n', ' ', 'y', 'o', 'u', 'r', ' ', 'c', 'o',
                                               'l', 'l', 'e', 'c', 't', 'i', 'o', 'n', 0};
        static const unsigned short kShort[] = {'O', 'w', 'n', 'e', 'd', 0};
        unsigned char line[kLine];
        makeLine(line, 0x16, kLong, 18, 0);
        check(ut::tooltipVerifyLine(line, 0x16, 18), "heap form (18 chars): class, size, bool read back");
        makeLine(line, 0x18, kShort, 5, 0);
        check(ut::tooltipVerifyLine(line, 0x18, 5), "SSO form (5 chars, capacity 7) reads back");
        check(!ut::tooltipVerifyLine(line, 0x16, 5), "another class -> refused");
        check(!ut::tooltipVerifyLine(line, 0x18, 6), "another length -> refused");
        makeLine(line, 0x18, kShort, 5, 1);
        check(!ut::tooltipVerifyLine(line, 0x18, 5), "the +0x1C bool set -> refused");
        check(!ut::tooltipVerifyLine((const unsigned char*)(size_t)0x10, 0x18, 5),
              "an unreadable line -> refused inside the mod's SEH");
        check(ut::g_guardDepth == 0, "the guard bracket is balanced");
    }

    printf("\n8. the words from [tab] of the buttons file, and the FAULT LATCH (no Game.dll in this process)\n");
    {
        check(!strcmp(ut::tooltipTextRoll(), " (another roll)"), "tooltip.roll built in: \" (another roll)\"");
        ut::tooltipTextRefresh("Collected!", nullptr, " (noch ein Wurf)", 30, -1);
        check(!strcmp(ut::tooltipTextYes(), "Collected!") && ut::tooltipClassYes() == 30,
              "tooltip.yes / tooltip.class_yes from the file are kept before init");
        check(!strcmp(ut::tooltipTextRoll(), " (noch ein Wurf)"), "tooltip.roll from the file is kept");
        check(ut::tooltipClassNo() == 0x18, "tooltip.class_no unset (-1) -> the default 0x18");
        ut::tooltipTextRefresh("Collected!", "", nullptr, 84, -1);
        check(ut::tooltipClassYes() == 0x16 && logHas("tooltip.class_yes=84 is not a text style"),
              "an unregistered style -> the default, said once");
        check(!strcmp(ut::tooltipTextRoll(), " (another roll)"), "tooltip.roll unset -> the built-in again");
        ut::tooltipTextRefresh("Collected!", "", nullptr, 30, -1);
        check(!ut::tooltipIsOff(), "the line is armed before init");
        check(!ut::tooltipInit(nullptr), "tooltipInit refuses without Game.dll");
        check(!strcmp(ut::tooltipTextYes(), "Collected!"), "tooltip.yes from the buttons file survives init");
        check(!strcmp(ut::tooltipTextNo(), "Not in your collection"),
              "an empty tooltip.no -> the built-in wording");
        check(ut::tooltipIsOff(), "the session latch is OFF");
        check(logCount("tooltip: OFF *****") == 1, "ONE capitalised OFF line");
        check(!ut::tooltipInit(nullptr), "a second init answers OFF again");
        check(logCount("tooltip: OFF *****") == 1, "and does not say it twice");
        int total = -1;
        check(ut::tooltipInstall(&total) == 0 && total == 6, "tooltipInstall: 0 of 6 installed");
        check(logHas("tooltip: no detour is installed - Game.dll is not loaded"),
              "and names why");
        check(strstr(ut::tooltipStatus(), "tooltip: OFF hooks=0") != nullptr, "tooltipStatus says OFF");
    }

    printf("\n9. the detours driven offline with stub trampolines\n");
    {
        ut::g_tq.ObjectGetObjectName =
            reinterpret_cast<decltype(ut::g_tq.ObjectGetObjectName)>(&stubObjectName);
        ut::tooltipTestArm((void*)&stubBaseText, (void*)&stubEquipText, (void*)&stubArtifactText,
                           (void*)&stubRelicText, (void*)&stubToString, 0x16, 0x18);
        check(!ut::tooltipIsOff(), "armed again for the drive (the seam resets section 8's latch)");
        FakeItem itemA = {"Records\\Items\\A\\Unique_A.dbr"};  // collected (the journal stub)
        FakeItem itemB = {"records/items/b/unique_b.dbr"};         // in the catalogue, not collected
        FakeItem itemC = {"records/items/c/common_c.dbr"};         // not a catalogue record
        FakeItem itemF = {(const char*)(size_t)0x10};              // a name that FAULTS when read
        FakeVec rv;
        char out[32];

        rollReset(&rv);
        ut::tooltipTestEquipText(&itemA, nullptr, &rv);
        check(lineCount(rv) == 5u, "equipment: the base's 3 lines, then its own 2");
        check(ut::tooltipTestLatchCount() == 5u,
              "the ItemEquipment detour ran LAST: the latch holds the complete 5, not the base's 3");
        {   // the property search's capture runs the same detours with the flag set
            unsigned char capBuf[16 * kLine];
            memset(capBuf, 0, sizeof(capBuf));
            FakeVec cv;
            cv.begin = capBuf;
            cv.end = capBuf;
            cv.cap = capBuf + sizeof(capBuf);
            ut::tooltipSearchCapture(true);
            ut::tooltipTestRelicText(&itemB, nullptr, &cv);   // a catalogue record, 2 lines
            ut::tooltipTestEquipText(&itemA, nullptr, &cv);
            ut::tooltipSearchCapture(false);
            check(lineCount(cv) == 7u && ut::tooltipTestLatchCount() == 5u,
                  "a search capture (the flag set) leaves the pending latch intact: still the 5");
        }
        seenReset();
        ut::tooltipTestToString(&rv, out);
        check(g_seenVec != &rv && g_seenCount == 6u && g_seenLast == 1,
              "ToString got a borrowed 6-line vector ending in the COLLECTED line (folded name)");
        check(lineCount(rv) == 5u && ut::tooltipTestLatchCount() == 0u,
              "the engine's vector still holds its 5 lines, and the latch was consumed");
        check(ut::tooltipSwapAllocBalance() == 0 && ut::g_guardDepth == 0,
              "nothing borrowed is alive, the guard bracket is balanced");

        rollReset(&rv);
        ut::tooltipTestArtifactText(&itemB, nullptr, &rv);
        seenReset();
        ut::tooltipTestToString(&rv, out);
        check(g_seenVec != &rv && g_seenCount == 5u && g_seenLast == 2,
              "an ARTIFACT not in the collection: its 4 lines + the NOT COLLECTED line");

        // an ARCANE FORMULA whose builder runs the crafted artifact's hooked text builder inside
        ut::tooltipTestArmFormula((void*)&stubFormulaText);
        check(ut::tooltipTextBuilderIndex((void*)&stubFormulaText) == 4 &&
                  ut::tooltipTextBuilderKnown((void*)&stubFormulaText),
              "the property search knows the formula's builder: index 4 (ItemArtifactFormula), so a "
              "formula is indexable");
        check(ut::tooltipTextBuilderIndex((void*)&stubBaseText) != 4 &&
                  ut::tooltipTextBuilderIndex(nullptr) == -1,
              "no other address is the formula's builder; null is none");
        g_crafted = &itemB;   // the crafted artifact: a catalogue record, NOT collected
        rollReset(&rv);
        const long supBefore = ut::tooltipInnerSuppressed();
        ut::tooltipTestFormulaText(&itemA, nullptr, &rv);   // the formula itself: collected
        check(g_innerDepth == 1 && ut::tooltipTestFormulaDepth() == 0 && lineCount(rv) == 7u &&
                  ut::tooltipTestLatchCount() == 7u,
              "formula: the nested artifact text ran at depth 1 and latched nothing; the depth is "
              "back at 0 and the formula's own record is latched over the complete 7 lines");
        check(ut::tooltipInnerSuppressed() == supBefore + 1,
              "formula: the crafted artifact's latch inside it was suppressed (once)");
        seenReset();
        ut::tooltipTestToString(&rv, out);
        check(g_seenVec != &rv && g_seenCount == 8u && g_seenLast == 1,
              "formula: ToString shows ITS line (COLLECTED), never the artifact's NOT COLLECTED");
        rollReset(&rv);
        ut::tooltipTestEquipText(&itemA, nullptr, &rv);
        check(ut::tooltipTestLatchCount() == 5u, "an equipment latch is pending");
        rollReset(&rv);
        ut::tooltipTestFormulaText(&itemC, nullptr, &rv);   // a formula that is no catalogue record
        check(ut::tooltipTestLatchCount() == 0u && ut::tooltipTestFormulaDepth() == 0,
              "a formula outside the catalogue: the pending latch is dropped first, the artifact's "
              "latch suppressed, nothing latched");
        seenReset();
        ut::tooltipTestToString(&rv, out);
        check(g_seenVec == &rv && g_seenCount == 7u && g_seenLast == 0,
              "and the artifact's line never appears on it: the engine's own 7 lines");
        rollReset(&rv);
        ut::tooltipTestArtifactText(&itemB, nullptr, &rv);
        check(ut::tooltipTestLatchCount() == 4u,
              "outside a formula the artifact latches its own record again (the depth is 0)");
        g_crafted = nullptr;
        seenReset();
        ut::tooltipTestToString(&rv, out);   // consumed: the drive below starts with no latch

        rollReset(&rv);
        ut::tooltipTestRelicText(&itemC, nullptr, &rv);
        check(ut::tooltipTestLatchCount() == 0u, "not a catalogue record: nothing is latched");
        seenReset();
        ut::tooltipTestToString(&rv, out);
        check(g_seenVec == &rv && g_seenCount == 2u && g_seenLast == 0,
              "and ToString gets the engine's own vector, untouched");

        rollReset(&rv);
        ut::tooltipTestBaseText(&itemA, nullptr, &rv);
        check(ut::tooltipTestLatchCount() == 3u, "the base detour alone latches its 3 lines");
        unsigned char other[kLine];
        memset(other, 0, sizeof(other));
        *(unsigned int*)other = 0x33;
        FakeVec ov = {other, other + kLine, other + kLine};
        seenReset();
        ut::tooltipTestToString(&ov, out);
        check(g_seenVec == &ov && g_seenCount == 1u && g_seenLast == 0,
              "another vector converted first (a price text): no line on it");
        seenReset();
        ut::tooltipTestToString(&rv, out);
        check(g_seenVec == &rv && g_seenCount == 3u,
              "rule (b): that first ToString CONSUMED the latch - the item's vector gets none");

        rollReset(&rv);
        ut::tooltipTestBaseText(&itemA, nullptr, &rv);
        check(ut::tooltipTestLatchCount() == 3u, "latched with tooltip_mark=1");
        ut::g_cfg.tooltipMark = 0;
        rollReset(&rv);
        ut::tooltipTestBaseText(&itemA, nullptr, &rv);
        check(ut::tooltipTestLatchCount() == 0u, "tooltip_mark=0: the next rollover CLEARS the latch");
        ut::g_cfg.tooltipMark = 1;
        seenReset();
        ut::tooltipTestToString(&rv, out);
        check(g_seenVec == &rv && g_seenCount == 3u, "and nothing is added once the mark is back");

        ut::g_setKnown = false;
        rollReset(&rv);
        ut::tooltipTestBaseText(&itemA, nullptr, &rv);
        check(ut::tooltipTestLatchCount() == 0u, "the journal set CLOSED: a collected unique latches nothing");
        rollReset(&rv);
        ut::tooltipTestBaseText(&itemB, nullptr, &rv);
        check(ut::tooltipTestLatchCount() == 0u, "and neither does an uncollected one");
        ut::g_setKnown = true;
        rollReset(&rv);
        ut::tooltipTestBaseText(&itemA, nullptr, &rv);
        seenReset();
        ut::tooltipTestToString(&rv, out);
        check(g_seenLast == 1, "the set open again: the very next hover has its line");

        rollReset(&rv);
        ut::tooltipTestBaseText(&itemA, nullptr, &rv);
        g_raise = true;
        seenReset();
        const bool raised = toStringRaising(&rv);
        g_raise = false;
        check(raised && g_seenCount == 4u, "the trampoline RAISED while converting the borrowed 4 lines");
        check(ut::tooltipSwapAllocBalance() == 0, "the __finally freed the borrowed array as it unwound");

        const int faults0 = logCount("tooltip: FAULT");
        rollReset(&rv);
        ut::tooltipTestBaseText(&itemF, nullptr, &rv);
        check(ut::tooltipTestLatchCount() == 0u && !ut::tooltipIsOff() && lineCount(rv) == 3u,
              "a name that FAULTS: no latch, the engine's 3 lines intact, the line stays armed");
        check(ut::g_guardDepth == 0 && logCount("tooltip: FAULT") == faults0,
              "swallowed by the read's own frame (GD's readRecordSeh): no FAULT line, bracket balanced");
    }

    printf("\n10. the \"(another roll)\" line: a held monster infrequent hovered with another roll\n");
    {
        ut::g_tq.ItemGetPrefixName = reinterpret_cast<decltype(ut::g_tq.ItemGetPrefixName)>(&stubPrefix);
        ut::g_tq.ItemGetSuffixName = reinterpret_cast<decltype(ut::g_tq.ItemGetSuffixName)>(&stubSuffix);
        ut::g_tq.ItemGetItemReplicaInfo =
            reinterpret_cast<decltype(ut::g_tq.ItemGetItemReplicaInfo)>(&stubReplica);
        ut::tooltipTestArm((void*)&stubBaseText, (void*)&stubEquipText, (void*)&stubArtifactText,
                           (void*)&stubRelicText, (void*)&stubToString, 0x16, 0x18);
        ut::tooltipTestRollReplica((unsigned)offsetof(FakeMi, replica));
        FakeMi same = {"records/items/m/mi_l_m.dbr", "records/affix/ar_one.dbr", "", 77u};
        FakeMi seed = {"records/items/m/mi_l_m.dbr", "Records\\Affix\\AR_ONE.dbr", "", 78u};
        FakeMi pre = {"Records\\Items\\M\\MI_L_M.dbr", "records/affix/ar_two.dbr", "", 77u};
        FakeMi spelled = {"records/items/m/mi_l_m.dbr", "RECORDS\\AFFIX\\AR_ONE.DBR", "", 77u};
        FakeMi uniq = {"records/items/a/unique_a.dbr", "records/affix/ar_nine.dbr", "", 5u};
        FakeMi bare = {"records/items/b/unique_b.dbr", "", "", 1u};
        FakeMi* placed[] = {&same, &seed, &pre, &spelled, &uniq, &bare};
        for (FakeMi* m : placed) placeSeed(m);
        FakeVec rv;
        char out[32];
        ut::g_rollAsks = 0;
        g_affixCalls = 0;
        g_replicaCalls = 0;

        rollReset(&rv);
        ut::tooltipTestArtifactText(&same, nullptr, &rv);
        seenReset();
        ut::tooltipTestToString(&rv, out);
        check(g_seenCount == 5u && g_seenLast == 1,
              "the STORED roll hovered (prefix, suffix and seed equal): the plain COLLECTED line");
        const int affixBefore = g_affixCalls;
        ut::g_isItem = false;
        rollReset(&rv);
        ut::tooltipTestArtifactText(&seed, nullptr, &rv);
        seenReset();
        ut::tooltipTestToString(&rv, out);
        ut::g_isItem = true;
        check(g_seenCount == 5u && g_seenLast == 1 && g_affixCalls == affixBefore,
              "another roll on an object that fails the vtable test: the plain line, no roll read");
        rollReset(&rv);
        ut::tooltipTestArtifactText(&seed, nullptr, &rv);
        seenReset();
        ut::tooltipTestToString(&rv, out);
        check(g_seenCount == 5u && g_seenLast == 3,
              "another SEED (the prefix spelled otherwise): \"In your collection (another roll)\"");
        rollReset(&rv);
        ut::tooltipTestArtifactText(&pre, nullptr, &rv);
        seenReset();
        ut::tooltipTestToString(&rv, out);
        check(g_seenLast == 3, "another PREFIX (the record spelled otherwise): (another roll)");
        rollReset(&rv);
        ut::tooltipTestArtifactText(&spelled, nullptr, &rv);
        seenReset();
        ut::tooltipTestToString(&rv, out);
        check(g_seenLast == 1, "the stored roll in upper case with backslashes: the same roll, the plain line");
        check(ut::g_rollAsks == 1 && g_affixCalls == 4,
              "the stored roll was asked ONCE for four hovers (memoised 250 ms); the hovered roll each time");
        const int calls0 = g_affixCalls;
        rollReset(&rv);
        ut::tooltipTestArtifactText(&uniq, nullptr, &rv);
        seenReset();
        ut::tooltipTestToString(&rv, out);
        check(g_seenLast == 1 && g_affixCalls == calls0,
              "a held UNIQUE: the plain COLLECTED line, its roll never read (no change for uniques)");
        rollReset(&rv);
        ut::tooltipTestArtifactText(&bare, nullptr, &rv);
        seenReset();
        ut::tooltipTestToString(&rv, out);
        check(g_seenLast == 2 && g_affixCalls == calls0,
              "a record not in the collection: the NOT COLLECTED line, no roll read");
        check(strstr(ut::tooltipStatus(), "anotherRoll=2") != nullptr,
              "the status counts the two (another roll) lines (GD's anotherRoll=)");
        const int asks0 = ut::g_rollAsks;
        rollReset(&rv);
        ut::tooltipTestEquipText(&seed, nullptr, &rv);
        seenReset();
        ut::tooltipTestToString(&rv, out);
        check(g_seenLast == 3,
              "another roll through the EQUIPMENT hook (a monster infrequent is ItemEquipment): (another roll)");
        rollReset(&rv);
        ut::tooltipTestEquipText(&same, nullptr, &rv);
        seenReset();
        ut::tooltipTestToString(&rv, out);
        check(g_seenLast == 1 && ut::g_rollAsks == asks0 + 1,
              "the stored roll through the equipment hook: the plain line; the stored roll asked once for both");
        Sleep(300);
        rollReset(&rv);
        ut::tooltipTestEquipText(&same, nullptr, &rv);
        seenReset();
        ut::tooltipTestToString(&rv, out);
        check(g_seenLast == 1 && ut::g_rollAsks == asks0 + 2,
              "after 250 ms the memo has expired: the stored roll is asked again");

        FakeMi bad = {"records/items/m/mi_l_m.dbr", (const char*)(size_t)0x10, "", 77u};
        placeSeed(&bad);
        const int offs0 = logCount("(another roll)\" line is OFF");
        rollReset(&rv);
        ut::tooltipTestArtifactText(&bad, nullptr, &rv);
        seenReset();
        ut::tooltipTestToString(&rv, out);
        check(g_seenLast == 1 && ut::g_guardDepth == 0,
              "a roll read that FAULTS: swallowed in its own frame, the plain collected line");
        check(logCount("(another roll)\" line is OFF") == offs0 + 1 && !ut::tooltipIsOff(),
              "the roll line is OFF with ONE error line; the collection line stays armed");
        const int calls1 = g_affixCalls;
        rollReset(&rv);
        ut::tooltipTestArtifactText(&seed, nullptr, &rv);
        seenReset();
        ut::tooltipTestToString(&rv, out);
        check(g_seenLast == 1 && g_affixCalls == calls1 &&
                  logCount("(another roll)\" line is OFF") == offs0 + 1,
              "never retried: another roll now reads the plain line, no getter is called again");
        check(g_replicaCalls == 0,
              "the seed is a PURE read (the DWORD at the replica's +0x7C): Item::GetItemReplicaInfo is never called");

        ut::tooltipTestArm((void*)&stubBaseText, (void*)&stubEquipText, (void*)&stubArtifactText,
                           (void*)&stubRelicText, (void*)&stubToString, 0x16, 0x18);
        // the seed's address lands in the null page: the read faults after both getters answered
        ut::tooltipTestRollReplica(0x10u - 0x7Cu - (unsigned)(size_t)&same);
        const int offs1 = logCount("(another roll)\" line is OFF");
        const int calls2 = g_affixCalls;
        rollReset(&rv);
        ut::tooltipTestArtifactText(&same, nullptr, &rv);
        seenReset();
        ut::tooltipTestToString(&rv, out);
        check(g_seenLast == 1 && ut::g_guardDepth == 0 && g_affixCalls == calls2 + 1,
              "a SEED read that faults (after the getters): swallowed in its own frame, the plain line");
        check(logCount("(another roll)\" line is OFF") == offs1 + 1 && !ut::tooltipIsOff() &&
                  g_replicaCalls == 0,
              "the seed's fault: ONE error line, the collection line stays armed, no engine call made");
        ut::tooltipTestRollReplica((unsigned)offsetof(FakeMi, replica));
    }

    printf("\n11. a relic or charm: the line speaks of its slot on the relic page\n");
    {
        ut::UtRelicRow row;
        row.record = row.folded = "records/items/relic/r1.dbr";
        row.shards = 3;
        row.bonuses = {"records/b/one.dbr", "records/b/two.dbr"};
        row.bonusFolded = row.bonuses;
        ut::g_relicList.rows.push_back(row);
        ut::g_relicList.byFolded.push_back(0);
        auto fact = [](unsigned long long seq, unsigned long long at, unsigned var1, int pending,
                       const char* bonus) {
            ut::UtRelicRowFacts f;
            memset(&f, 0, sizeof(f));
            f.seq = seq;
            f.deposited = at;
            f.var1 = var1;
            f.pending = pending;
            ut::utRelicFold(bonus, f.bonus, sizeof(f.bonus));
            return f;
        };
        ut::tooltipTestArm((void*)&stubBaseText, (void*)&stubEquipText, (void*)&stubArtifactText,
                           (void*)&stubRelicText, (void*)&stubToString, 0x16, 0x18);
        ut::tooltipTestRollReplica((unsigned)offsetof(FakeMi, replica));
        ut::g_tq.ObjectGetObjectName =
            reinterpret_cast<decltype(ut::g_tq.ObjectGetObjectName)>(&stubObjectName);
        static FakeMi relic;
        memset(&relic, 0, sizeof(relic));
        relic.name = "records/items/relic/r1.dbr";
        relic.prefix = relic.suffix = "";
        static char heap[7][260];
        auto setPiece = [&](unsigned var1, const char* bonus) {
            memcpy(relic.replica + 0x80, &var1, 4);
            ut::utVsStringSet(relic.replica + 0x64, bonus, (unsigned)strlen(bonus), heap[4]);
        };
        char out[32];
        FakeVec rv;
        // a completed relic whose bonus the collection holds, then one of another bonus
        ut::g_relicFacts[0] = fact(1, 10, 3, 0, "records/b/one.dbr");
        ut::g_relicFactN = 1;
        setPiece(3, "Records\\B\\One.dbr");
        rollReset(&rv);
        ut::tooltipTestRelicText(&relic, nullptr, &rv);
        seenReset();
        ut::tooltipTestToString(&rv, out);
        check(g_seenCount == 3 && g_seenLast == 1, "a completed relic of a held (record, bonus): In your collection");
        Sleep(300);   // past the memo
        setPiece(3, "records/b/two.dbr");
        rollReset(&rv);
        ut::tooltipTestRelicText(&relic, nullptr, &rv);
        seenReset();
        ut::tooltipTestToString(&rv, out);
        check(g_seenCount == 3 && g_seenLast == 3, "only another bonus of the record held: (another roll)");
        Sleep(300);
        ut::g_relicFactN = 0;
        rollReset(&rv);
        ut::tooltipTestRelicText(&relic, nullptr, &rv);
        seenReset();
        ut::tooltipTestToString(&rv, out);
        check(g_seenCount == 3 && g_seenLast == 2, "nothing of the record held: Not in your collection");
        // a partial piece (1 / 3), the engine's own ratio line in the vector, two pieces stored
        Sleep(300);
        ut::g_relicFacts[0] = fact(1, 20, 2, 0, "");
        ut::g_relicFacts[1] = fact(2, 10, 0, 0, "");
        ut::g_relicFacts[2] = fact(3, 5, 1, 2, "");   // pending out: not counted
        ut::g_relicFactN = 3;
        setPiece(1, "");
        rollReset(&rv);
        static const unsigned short ratio[] = {'R', 'e', 'l', 'i', 'c', ' ', 'S', 'h', 'a', 'r', 'd', ' ', '-',
                                               ' ', '1', ' ', '/', ' ', '3', 0};
        makeLine(g_rollBuf, 0x18, ratio, 19, 0);
        rv.end += kLine;
        ut::tooltipTestRelicText(&relic, nullptr, &rv);
        static unsigned char seen[8][kLine];
        static unsigned short seenText[8][64];
        g_copyLines = seen;
        g_copyText = seenText;
        seenReset();
        ut::tooltipTestToString(&rv, out);
        g_copyLines = nullptr;
        auto textIs = [&](int i, const char* want) {
            for (int k = 0;; ++k) {
                if ((unsigned short)(unsigned char)want[k] != seenText[i][k]) return false;
                if (!want[k]) return true;
            }
        };
        char want[160];
        _snprintf_s(want, sizeof(want), _TRUNCATE, "%s (pieces 3)", ut::tooltipTextYes());
        check(g_seenCount == 4 && textIs(3, want) && strlen(want) > 12 &&
                  *(const unsigned*)seen[3] == *(const unsigned*)ut::tooltipTestLine(true),
              "a partial: ONE line - the collected words and the stored shards (2 + 1; the out row not counted)");
        check(ut::tooltipSwapAllocBalance() == 0, "the relic line's borrowed array is given back");
        Sleep(300);
        ut::g_relicFactN = 1;   // the only piece is the 2 / 3 one
        rollReset(&rv);
        ut::tooltipTestRelicText(&relic, nullptr, &rv);   // no ratio line in the vector
        g_copyLines = seen;
        seenReset();
        ut::tooltipTestToString(&rv, out);
        g_copyLines = nullptr;
        _snprintf_s(want, sizeof(want), _TRUNCATE, "%s (pieces 2)", ut::tooltipTextYes());
        check(g_seenCount == 3 && textIs(2, want), "one stored piece of two shards: the same one line, (pieces 2)");
        ut::g_relicList.rows.clear();
        ut::g_relicList.byFolded.clear();
    }

    printf("\n%s (%d failure(s))\n", g_fails ? "FAILED" : "PASSED", g_fails);
    return g_fails ? 1 : 0;
}
