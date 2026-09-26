// test_search.cpp - the property search's pure half, offline. NO GAME NEEDED.
//   tools\build_test_search.bat
//
// It includes the SAME headers the mod compiles (src\ut_search.h, src\ut_searchfold.h): the read
// and free of a captured line vector over a FAKE engine vector (inline and heap strings, the
// small-string edge at capacity 7 / 8, malformed shapes refused, every free counted through an
// injected deleter), the keep-class rule, the fold, the matching, the page's show mask with OWN on
// and off, and the group marks.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "ut_search.h"
#include "ut_color.h"
#include "ut_padlayout.h"

static int g_fails = 0;
static void check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) ++g_fails;
}

// ---- a fake engine vector ----------------------------------------------------------------------
static const size_t kLine = 0x20;
struct FakeVec {
    unsigned char* first;
    unsigned char* last;
    unsigned char* end;
};
static_assert(sizeof(FakeVec) == 12, "VS2012 x86 std::vector is three 4-byte pointers");

static int g_freed = 0;
static std::vector<void*> g_live;   // what the fake engine allocated and still owns
static void* fakeAlloc(size_t n) {
    void* p = malloc(n);
    g_live.push_back(p);
    return p;
}
static void countingDelete(void* p) {
    ++g_freed;
    for (size_t i = 0; i < g_live.size(); ++i) {
        if (g_live[i] == p) {
            g_live.erase(g_live.begin() + (long)i);
            free(p);
            return;
        }
    }
    printf("  [FAIL] a pointer the fake engine never allocated was freed\n");
    ++g_fails;
}

// One line: `text` (UTF-16) in the buffer while it fits capacity 7, else on the fake heap with
// capacity max(8, len) - or the capacity the case forces.
static void setLine(unsigned char* line, unsigned cls, const wchar_t* text, int forceCap = -1) {
    memset(line, 0, kLine);
    memcpy(line, &cls, 4);
    const unsigned len = (unsigned)wcslen(text);
    unsigned cap = forceCap >= 0 ? (unsigned)forceCap : (len < 8 ? 7u : len);
    if (cap < 8) {
        memcpy(line + 0x04, text, len * 2);
    } else {
        unsigned short* p = (unsigned short*)fakeAlloc((cap + 1) * 2);
        memcpy(p, text, len * 2);
        p[len] = 0;
        memcpy(line + 0x04, &p, sizeof(p));
    }
    memcpy(line + 0x14, &len, 4);
    memcpy(line + 0x18, &cap, 4);
}

static FakeVec makeVec(size_t n) {
    FakeVec v;
    v.first = (unsigned char*)fakeAlloc(n * kLine);
    v.last = v.first + n * kLine;
    v.end = v.last;
    return v;
}

static std::string foldW(const wchar_t* w) {
    std::string out;
    ut::utSearchFoldWide((const unsigned short*)w, wcslen(w), &out);
    return out;
}

static ut::UtSearchStage g_stage;   // ~70 KB: not on the stack

int main() {
    printf("1. the vector read and free over a fake engine vector\n");
    {
        FakeVec v = makeVec(4);
        setLine(v.first + 0 * kLine, 0x06, L"Mjolnir");                 // 7 chars: inline, cap 7
        setLine(v.first + 1 * kLine, 0x10, L"+30% Fire Resistance");    // heap
        setLine(v.first + 2 * kLine, 0x11, L"Required Level: 40");      // heap, dropped
        setLine(v.first + 3 * kLine, 0x0F, L"Sword", 8);                // SSO edge: cap 8 = heap
        const size_t before = g_live.size();
        const int n = ut::utSearchStage(&v, &g_stage);
        check(n == 4 && g_stage.count == 4, "four lines staged");
        check(g_stage.heapCount == 3 && g_stage.buffer == v.first,
              "capacity 7 is inline, capacity 8 is on the heap: three heap strings, one array");
        check(g_stage.len[0] == 7 && g_stage.cls[1] == 0x10 && g_stage.len[3] == 5,
              "the sizes and classes are read from +0x14 / +0x00");
        ut::UtSearchCounts c = {};
        std::string text;
        ut::utSearchFoldStage(&g_stage, false, &text, &c);
        check(text == "mjolnir\n+30% fire resistance\nsword", "the kept lines, folded, joined by \\n");
        check(c.kept == 3 && c.dropReq == 1 && c.dropLore == 0 && c.dropDir == 0,
              "the requirement line is dropped and counted");
        g_freed = 0;
        const int freed = ut::utSearchFreeStaged(&g_stage, &countingDelete);
        check(freed == 4 && g_freed == 4, "four frees: the three heap strings, then the array");
        check(g_live.size() == before - 4 && g_live.empty(),
              "nothing the fake engine allocated is left behind");
    }
    {
        FakeVec empty = {nullptr, nullptr, nullptr};
        check(ut::utSearchStage(&empty, &g_stage) == 0 && !g_stage.buffer,
              "an empty vector {0,0,0}: nothing staged, nothing to free");
        g_freed = 0;
        ut::utSearchFreeStaged(&g_stage, &countingDelete);
        check(g_freed == 0, "and nothing is freed");
        FakeVec noLines;
        noLines.first = (unsigned char*)fakeAlloc(4 * kLine);
        noLines.last = noLines.first;
        noLines.end = noLines.first + 4 * kLine;
        check(ut::utSearchStage(&noLines, &g_stage) == 0 && g_stage.buffer == noLines.first,
              "an array with no lines yet: nothing staged, the array itself is freed");
        ut::utSearchFreeStaged(&g_stage, &countingDelete);
        check(g_live.empty(), "the array went back");
    }
    {
        g_freed = 0;
        FakeVec odd = makeVec(2);
        setLine(odd.first, 0x10, L"a");
        setLine(odd.first + kLine, 0x10, L"b");
        odd.last = odd.first + kLine + 4;   // not a multiple of 0x20
        check(ut::utSearchStage(&odd, &g_stage) == -1 && !g_stage.buffer && g_stage.heapCount == 0,
              "a length that is not a multiple of 0x20 is refused, nothing staged");
        FakeVec back = odd;
        back.last = back.first - kLine;
        check(ut::utSearchStage(&back, &g_stage) == -1, "last before first is refused");
        FakeVec endBefore = odd;
        endBefore.last = odd.first + kLine;
        endBefore.end = odd.first;
        check(ut::utSearchStage(&endBefore, &g_stage) == -1, "end before last is refused");
        FakeVec nullFirst = {nullptr, odd.first, odd.first};
        check(ut::utSearchStage(&nullFirst, &g_stage) == -1, "a null first with a last is refused");
        unsigned char* big = (unsigned char*)malloc(513 * kLine);
        memset(big, 0, 513 * kLine);
        for (int i = 0; i < 513; ++i) setLine(big + i * kLine, 0x10, L"x");
        FakeVec huge = {big, big + 513 * kLine, big + 513 * kLine};
        check(ut::utSearchStage(&huge, &g_stage) == -1, "513 lines is refused (1-512)");
        huge.last = big + 512 * kLine;
        huge.end = huge.last;
        check(ut::utSearchStage(&huge, &g_stage) == 512, "512 lines is taken");
        free(big);
        FakeVec bad = makeVec(2);
        setLine(bad.first, 0x10, L"fine");
        setLine(bad.first + kLine, 0x10, L"xy", 6);   // capacity 6: no VS2012 wstring has it
        check(ut::utSearchStage(&bad, &g_stage) == -1 && g_stage.heapCount == 0,
              "a capacity under 7 is refused (the whole vector, not just the line)");
        setLine(bad.first + kLine, 0x10, L"xy");
        unsigned big2 = 9;
        memcpy(bad.first + kLine + 0x14, &big2, 4);   // size 9 > capacity 7
        check(ut::utSearchStage(&bad, &g_stage) == -1, "a size over the capacity is refused");
        setLine(bad.first + kLine, 0x10, L"a heap one here");
        void* nul = nullptr;
        void* was = nullptr;
        memcpy(&was, bad.first + kLine + 0x04, sizeof(was));
        memcpy(bad.first + kLine + 0x04, &nul, sizeof(nul));
        check(ut::utSearchStage(&bad, &g_stage) == -1, "a heap string with a null pointer is refused");
        check(g_freed == 0, "a refused vector frees nothing");
        memcpy(bad.first + kLine + 0x04, &was, sizeof(was));
        check(ut::utSearchStage(&bad, &g_stage) == 2, "repaired, it reads");
        ut::utSearchFreeStaged(&g_stage, &countingDelete);
        ut::utSearchStage(&odd, &g_stage);   // refused; its memory is the harness's to drop
        while (!g_live.empty()) countingDelete(g_live.back());
    }

    printf("\n2. the keep-class rule\n");
    {
        check(!ut::utSearchKeepClass(0x11, false) && !ut::utSearchKeepClass(0x11, true),
              "0x11 ItemRequirements is out, with or without search_lore");
        check(!ut::utSearchKeepClass(0x1C, false) && !ut::utSearchKeepClass(0x1C, true),
              "0x1C ItemDirections is out");
        check(!ut::utSearchKeepClass(0x0E, false) && ut::utSearchKeepClass(0x0E, true),
              "0x0E ItemDescription (the lore) is out unless search_lore=1");
        bool rest = true;
        for (unsigned c = 0; c < 0x40; ++c) {
            if (c == 0x0E || c == 0x11 || c == 0x1C) continue;
            if (!ut::utSearchKeepClass(c, false)) rest = false;
        }
        check(rest && ut::utSearchKeepClass(0x1234, false),
              "every other class is kept, including one the table does not know");
    }

    printf("\n3. the fold\n");
    {
        check(foldW(L"Mj\x00F6lnir") == "mjolnir", "Mj\xC3\xB6lnir -> mjolnir");
        check(foldW(L"Summoner\x2019s Trinket") == "summoner's trinket",
              "a curly apostrophe -> '");
        check(foldW(L"Summoner\x2018s") == "summoner's", "U+2018 too");
        check(foldW(L"12\x00A0" L"Fire") == "12 fire", "a no-break space -> a space");
        check(foldW(L"\x0421\x0422\x0420\x0415\x041B\x0410") ==
                  std::string("\xD1\x81\xD1\x82\xD1\x80\xD0\xB5\xD0\xBB\xD0\xB0"),
              "Cyrillic capitals -> small letters");
        check(foldW(L"\x0401\x0404") == std::string("\xD1\x91\xD1\x94"), "and the 0x400 row");
        check(foldW(L"{^y}Fire{^n} Damage") == "fire damage", "{^x} escapes are dropped");
        check(foldW(L"^yFire ^wDamage") == "fire damage", "^x escapes are dropped");
        check(foldW(L"\x00C6gis \x00DF \x0152uvre \x0133") == "aegis ss oeuvre ij",
              "the ligatures and the sharp s spell out");
        check(foldW(L"\x0141\x00F3" L"d\x017A \x010C\x0161") == "lodz cs", "Latin Extended-A letters");
        check(foldW(L"a\nb\tc") == "a b c", "a control character is a space");
        check(foldW(L"100 \x00D7 2") == std::string("100 \xC3\x97 2"),
              "a sign that is not a letter stays as it is");
        check(ut::utSearchNeedleUtf8("  Mj\xC3\xB6lnir ") == "mjolnir",
              "the query (UTF-8): folded the same way and trimmed");
        check(ut::utSearchNeedleUtf8("Mj\xF6lnir") == "mjolnir",
              "a Latin-1 byte in the ini is taken as its letter");
        check(ut::utSearchNeedleUtf8("   ").empty() && ut::utSearchNeedleUtf8("").empty(),
              "blank = no query");
        check(ut::utSearchNeedleUtf8("\xE2\x80\x99") == "'", "a UTF-8 curly apostrophe folds too");
    }

    printf("\n4. matching\n");
    {
        std::string rec;
        rec = foldW(L"Stonebreaker") + "\n" + foldW(L"+30% Fire Resistance") + "\n" +
              foldW(L"12-18 Fire Damage") + "\n" + foldW(L"+15 Strength");
        check(ut::utSearchHit(rec, ut::utSearchNeedleUtf8("fire")),
              "\"fire\" hits \"+30% Fire Resistance\" / \"12-18 Fire Damage\"");
        const std::string res = ut::utSearchNeedleUtf8("fire resistance");
        check(ut::utSearchHit(rec, res), "\"fire resistance\" hits its line");
        const std::string dmgOnly = foldW(L"12-18 Fire Damage");
        check(!ut::utSearchHit(dmgOnly, res), "and not the damage line");
        check(!ut::utSearchHit(rec, ut::utSearchNeedleUtf8("resistance 12")),
              "no match across two lines");
        check(!ut::utSearchHit(rec, ut::utSearchNeedleUtf8("damage +15")),
              "no match across two lines (the other boundary)");
        check(ut::utSearchHit(rec, ut::utSearchNeedleUtf8("STONE")), "case does not matter");
        check(ut::utSearchHit(rec, std::string()), "no query hits everything");
    }

    printf("\n5. the highlight (nothing is hidden) and the count\n");
    {
        const unsigned char owned[6] = {1, 0, 1, 0, 1, 0};
        const unsigned char match[6] = {1, 1, 0, 0, 1, 1};
        int lit = 0;
        for (int k = 0; k < 6; ++k) lit += ut::utSearchHighlight(match, 6, k, true) ? 1 : 0;
        check(lit == 4 && ut::utSearchHighlight(match, 6, 0, true) &&
                  !ut::utSearchHighlight(match, 6, 2, true) && ut::utSearchHighlight(match, 6, 5, true),
              "a query: the four matches are highlighted, the other two are not");
        bool any = false;
        for (int k = 0; k < 6; ++k) any = any || ut::utSearchHighlight(match, 6, k, false);
        check(!any, "no query: nothing is highlighted");
        check(!ut::utSearchHighlight(match, 6, -1, true) && !ut::utSearchHighlight(match, 6, 6, true) &&
                  !ut::utSearchHighlight(nullptr, 6, 0, true),
              "an index off the group (a prototype with no place) or no flags: not highlighted");
        check(ut::utSearchCount(owned, match, 6, false) == 4, "the count, OWN off: the four matches");
        check(ut::utSearchCount(owned, match, 6, true) == 2,
              "the count, OWN on: the owned matches (0 and 4) - the label's found N");
        const unsigned char none[6] = {0, 0, 0, 0, 0, 0};
        check(ut::utSearchCount(owned, none, 6, false) == 0 && ut::utSearchCount(owned, nullptr, 6, false) == 0,
              "no match: a count of 0 (the group still lays out whole)");
        // a real ItemEquipment capture from the game: the name line ends in a raw ^n
        const std::string name = foldW(L"Agamemnon\x2019s Death Mask^n");
        check(name == "agamemnon's death mask" && ut::utSearchHit(name, ut::utSearchNeedleUtf8("mask")) &&
                  ut::utSearchHit(name, ut::utSearchNeedleUtf8("death mask")),
              "the captured name 'Agamemnon's Death Mask^n' folds to 'agamemnon's death mask': "
              "^n is gone, 'mask' hits at the word end");
        check(foldW(L"{^y}Fire Damage^w") == "fire damage", "{^x} before and ^x after: both gone");
        // the same line with the exact bytes the log captured (an ASCII apostrophe, 0x27)
        check(foldW(L"Agamemnon's Death Mask^n") == "agamemnon's death mask",
              "the captured line's exact bytes (ASCII 0x27) fold to 'agamemnon's death mask'");
        {   // the shown page's prototype -> group index map: the record pointer decides
            const char a[] = "records/a.dbr", b[] = "records/b.dbr", c[] = "records/c.dbr";
            const char same[] = "records/b.dbr";   // the same text in another buffer
            const char* rec[3] = {a, b, c};
            const int kk[3] = {4, 7, 9};
            check(ut::utSearchPlaceIndex(rec, kk, 3, 0, a) == 4 &&
                      ut::utSearchPlaceIndex(rec, kk, 3, 1, b) == 7,
                  "place map: prototype i is place i - its group index");
            check(ut::utSearchPlaceIndex(rec, kk, 3, 1, c) == 9 &&
                      ut::utSearchPlaceIndex(rec, kk, 3, 0, b) == 7,
                  "place map: a prototype that failed to build shifts the rest - found by the record");
            check(ut::utSearchPlaceIndex(rec, kk, 3, 1, same) == -1 &&
                      ut::utSearchPlaceIndex(rec, kk, 3, 7, same) == -1 &&
                      ut::utSearchPlaceIndex(rec, kk, 0, 0, a) == -1 &&
                      ut::utSearchPlaceIndex(nullptr, kk, 3, 0, a) == -1 &&
                      ut::utSearchPlaceIndex(rec, kk, 3, 0, nullptr) == -1,
                  "place map: a stale prototype (another group's record, even with the same text) or "
                  "no places: -1, never marked");
        }
    }

    printf("\n5b. the field: the key gate's decisions and the edits\n");
    {
        using namespace ut;
        check(utSearchKeyAction(false, 0, 0x17, false) == kUtKeyPass &&
                  utSearchKeyAction(false, 0, kUtKeyEsc, false) == kUtKeyPass,
              "unfocused: every key passes (i, Esc)");
        check(utSearchKeyAction(true, 1, 0x17, false) == kUtKeyPass &&
                  utSearchKeyAction(true, 1, kUtKeyEsc, false) == kUtKeyPass &&
                  utSearchKeyAction(true, 1, kUtKeyReturn, false) == kUtKeyPass,
              "focused: every RELEASE passes (the game's held-key bookkeeping)");
        check(utSearchKeyAction(true, 0, kUtKeyEsc, false) == kUtKeyEscape &&
                  utSearchKeyAction(true, 0, kUtKeyBack, false) == kUtKeyErase &&
                  utSearchKeyAction(true, 0, kUtKeyBack, true) == kUtKeyClear &&
                  utSearchKeyAction(true, 0, kUtKeyReturn, false) == kUtKeyEnter &&
                  utSearchKeyAction(true, 0, 0x17, false) == kUtKeyText &&
                  utSearchKeyAction(true, 0, 0x0F, false) == kUtKeyText,
              "focused presses: Esc, Back, Ctrl+Back = clear, Return, i and Tab are text");
        UtSearchField f = {{0}, 0};
        const unsigned short fire[] = {'f', 'i', 'r', 'e', 0};
        check(utSearchFieldType(&f, fire, 4) == 4 && f.len == 4 && f.text[4] == 0, "typing fire");
        const unsigned short ctl[] = {0x08, 0x1B, 0x7F, 0x7F, 0};
        check(utSearchFieldType(&f, ctl, 4) == 0 && f.len == 4,
              "control characters (a Ctrl combination) and DEL are dropped");
        check(utSearchFieldType(&f, nullptr, 3) == 0 && f.len == 4, "a press with no text adds nothing");
        const unsigned short cyr[] = {0x043E, 0x0433, 0};
        check(utSearchFieldType(&f, cyr, 2) == 2 && f.len == 6 && f.text[5] == 0x0433,
              "a non-ASCII character is kept as typed");
        check(utSearchFieldErase(&f) && f.len == 5 && f.text[5] == 0, "Back erases the last character");
        check(utSearchFieldEscape(&f) && f.len == 0 && f.text[0] == 0,
              "Esc on a non-empty field clears it and keeps the focus");
        check(!utSearchFieldEscape(&f), "Esc on an empty field: blur");
        check(!utSearchFieldErase(&f) && !utSearchFieldClear(&f), "Back / Ctrl+Back on an empty field: nothing");
        unsigned short many[40];
        for (int i = 0; i < 40; ++i) many[i] = (unsigned short)('a' + i % 26);
        check(utSearchFieldType(&f, many, 40) == kUtSearchFieldMax && f.len == kUtSearchFieldMax &&
                  f.text[kUtSearchFieldMax] == 0 && utSearchFieldType(&f, many, 1) == 0,
              "the maximum is 32 characters");
        check(utSearchFieldClear(&f) && f.len == 0, "Ctrl+Back clears the field");
        check(utSearchFieldTailStart(32, 20) == 12 && utSearchFieldTailStart(5, 20) == 0 &&
                  utSearchFieldTailStart(5, 0) == 4,
              "a long query shows its tail (at least one character)");
        check(!utSearchFieldIdle(1000u, 1000u) && !utSearchFieldIdle(60999u, 1000u) &&
                  utSearchFieldIdle(61000u, 1000u) && utSearchFieldIdle(59000u, 0xFFFF0000u),
              "the watchdog: 60 s without a key, across the GetTickCount wrap");
        // a state that is neither a press (0) nor a release (1): the field's, never the game's
        check(utSearchKeyAction(true, 2, kUtKeyBack, false) == kUtKeyErase &&
                  utSearchKeyAction(true, 2, kUtKeyBack, true) == kUtKeyClear &&
                  utSearchKeyAction(true, 2, 0x17, false) == kUtKeyText &&
                  utSearchKeyAction(true, 7, 0x21, false) == kUtKeyText &&
                  utSearchKeyAction(true, 2, kUtKeyEsc, false) == kUtKeySwallow &&
                  utSearchKeyAction(true, 2, kUtKeyReturn, false) == kUtKeySwallow &&
                  utSearchKeyAction(true, -1, kUtKeyEsc, false) == kUtKeySwallow &&
                  utSearchKeyAction(false, 2, kUtKeyBack, false) == kUtKeyPass,
              "focused, state 2 (or any other): Back erases, text types, Esc / Return do nothing; none passes");
    }

    printf("\n5c. Backspace held: 400 ms, then one erase every 40 ms\n");
    {
        using namespace ut;
        check(!utSearchRepeatDue(1000u, 1000u, 1000u, 0) && !utSearchRepeatDue(1399u, 1000u, 1000u, 0) &&
                  utSearchRepeatDue(1400u, 1000u, 1000u, 0),
              "nothing before 400 ms after the press; the first repeat at 400 ms");
        check(!utSearchRepeatDue(1439u, 1000u, 1400u, 1) && utSearchRepeatDue(1440u, 1000u, 1400u, 1) &&
                  !utSearchRepeatDue(1479u, 1000u, 1440u, 2) && utSearchRepeatDue(1480u, 1000u, 1440u, 2),
              "then one every 40 ms");
        check(!utSearchRepeatDue(0x0000008Fu, 0xFFFFFF00u, 0xFFFFFF00u, 0) &&
                  utSearchRepeatDue(0x00000090u, 0xFFFFFF00u, 0xFFFFFF00u, 0) &&
                  !utSearchRepeatDue(0x000000B7u, 0xFFFFFF00u, 0x00000090u, 1) &&
                  utSearchRepeatDue(0x000000B8u, 0xFFFFFF00u, 0x00000090u, 1),
              "across the GetTickCount wrap (unsigned differences)");
        // the stepper as the Update drives it: a 16 ms frame, held 1 s, then released
        UtBackRepeat r = {false, 0, 0, 0, false};
        UtSearchField f = {{0}, 0};
        unsigned short many[40];
        for (int i = 0; i < 40; ++i) many[i] = (unsigned short)('a' + i % 26);
        utSearchFieldType(&f, many, 30);
        const unsigned t0 = 0xFFFFFE00u;   // across the wrap too
        utSearchFieldErase(&f);            // the press's own erase (the gate)
        utBackRepeatPress(&r, t0);
        int erased = 0, early = 0;
        for (unsigned t = 0; t <= 1000; t += 16) {
            if (utBackRepeatStep(&r, t0 + t)) {
                if (t < 400) ++early;
                if (utSearchFieldErase(&f)) ++erased;
                if (f.len == 0) utBackRepeatRelease(&r);
            }
        }
        char what[160];
        _snprintf_s(what, sizeof(what), _TRUNCATE,
                    "held 1 s at 16 ms frames: %d repeat erase(s), none before 400 ms (%d early)", erased, early);
        check(early == 0 && erased >= 12 && erased <= 16 && f.len == 29 - erased, what);
        utBackRepeatRelease(&r);
        int after = 0;
        for (unsigned t = 1016; t <= 3000; t += 16)
            if (utBackRepeatStep(&r, t0 + t)) ++after;
        check(after == 0 && !r.held, "none after the release");
        // an empty field stops it
        utBackRepeatPress(&r, 0u);
        UtSearchField g = {{0}, 0};
        const unsigned short ab[] = {'a', 'b', 0};
        utSearchFieldType(&g, ab, 2);
        int steps = 0;
        for (unsigned t = 0; t <= 2000; t += 10) {
            if (utBackRepeatStep(&r, t)) {
                ++steps;
                if (!utSearchFieldErase(&g) || g.len == 0) utBackRepeatRelease(&r);
            }
        }
        check(steps == 2 && g.len == 0 && !r.held, "an empty field stops the repeat (2 erases for 2 characters)");
        // a repeat event from the engine restarts the timer: nothing for 400 ms again
        utBackRepeatPress(&r, 5000u);
        check(utBackRepeatStep(&r, 5400u) && !utBackRepeatStep(&r, 5420u), "the stepper: one at 400 ms, then waits 40 ms");
        utBackRepeatPress(&r, 5420u);
        check(!utBackRepeatStep(&r, 5460u) && !utBackRepeatStep(&r, 5819u) && utBackRepeatStep(&r, 5820u),
              "a repeat event resets the timer (the next erase 400 ms after it)");
    }

    printf("\n6. the group marks\n");
    {
        const unsigned char owned[4] = {0, 1, 0, 0};
        const unsigned char match[4] = {1, 0, 0, 0};
        check(ut::utSearchGroupMarked(owned, match, 4, false, true), "a match, OWN off: marked");
        check(!ut::utSearchGroupMarked(owned, match, 4, true, true),
              "the only match is not owned, OWN on: NOT marked");
        check(!ut::utSearchGroupMarked(owned, match, 4, false, false),
              "a group still being indexed is never marked");
        const unsigned char match2[4] = {0, 1, 0, 0};
        check(ut::utSearchGroupMarked(owned, match2, 4, true, true), "an owned match, OWN on: marked");
        const unsigned char none[4] = {0, 0, 0, 0};
        check(!ut::utSearchGroupMarked(owned, none, 4, false, true), "no match: not marked");
    }

    printf("\n7. the search mark: its colour (search_mark_color) and its geometry\n");
    {
        struct Want {
            const char* t;
            unsigned char r, g, b;
        };
        static const Want kOk[] = {
            {"gold", 0xff, 0xd7, 0x00},   {"green", 0x3c, 0xf0, 0x3c},   {"white", 0xf0, 0xf0, 0xf0},
            {"red", 0xff, 0x3c, 0x3c},    {"orange", 0xff, 0x9a, 0x1e},  {"cyan", 0x40, 0xe0, 0xff},
            {"magenta", 0xff, 0x50, 0xff}, {"blue", 0x45, 0x6c, 0x93},   {"GOLD", 0xff, 0xd7, 0x00},
            {"Green", 0x3c, 0xf0, 0x3c},  {"#3cf03c", 0x3c, 0xf0, 0x3c}, {"#3CF03C", 0x3c, 0xf0, 0x3c},
            {"#aBcDeF", 0xab, 0xcd, 0xef}, {"#000000", 0, 0, 0},          {"#FFFFFF", 0xff, 0xff, 0xff}};
        bool allOk = ut::kUtColorNameCount == 8;
        for (const Want& k : kOk) {
            unsigned char r = 7, g = 7, b = 7;
            const bool ok = ut::utColorParse(k.t, &r, &g, &b) && r == k.r && g == k.g && b == k.b;
            if (!ok) printf("  not parsed right: \"%s\"\n", k.t);
            allOk = allOk && ok;
        }
        check(allOk, "every name of the table (any case) and #rrggbb in both cases give their colour");
        static const char* const kBad[] = {"",        "purple",   "gold ",   " gold",    "golden",
                                           "gol",     "#",        "#12345",  "#1234567", "#gg0000",
                                           "#12 456", "ffd700",   "#-12345", "0xffd700", "#ffd70",
                                           "##ffd70", "#ffd700;", "blue2"};
        unsigned char r = 9, g = 9, b = 9;
        bool noneOk = !ut::utColorParse(nullptr, &r, &g, &b);
        for (const char* t : kBad) {
            if (ut::utColorParse(t, &r, &g, &b)) {
                printf("  accepted: \"%s\"\n", t);
                noneOk = false;
            }
        }
        check(noneOk && r == 9 && g == 9 && b == 9,
              "malformed input (empty, unknown name, short / long / non-hex, spaces) is refused, the outputs untouched");
        unsigned char dr = 0, dg = 0, db = 0;
        check(ut::utColorParse(ut::kUtColorDefault, &dr, &dg, &db) && dr == 0xff && dg == 0xd7 && db == 0x00,
              "the default is gold, #ffd700");
        check(ut::utColorParse("#456c93", nullptr, nullptr, nullptr) && ut::utColorParse("blue", &dr, &dg, &db) &&
                  dr == 0x45 && dg == 0x6c && db == 0x93,
              "blue is the first look (#456c93); null outputs give only the verdict");
        const ut::UtRectF slot = {100.0f, 50.0f, 64.0f, 96.0f};
        const ut::UtRectF fr = ut::utSearchMarkRect(slot, false), wa = ut::utSearchMarkRect(slot, true);
        check(fr.x == 100.0f && fr.y == 50.0f && fr.w == 64.0f && fr.h == 96.0f,
              "the frame lies on the slot's outermost pixel ring (inset 0)");
        check(wa.x == 101.0f && wa.y == 51.0f && wa.w == 62.0f && wa.h == 94.0f, "the wash fills the slot inset by 1 px");
        check(ut::utSearchMarkThick(26.0f) == 1.0f && ut::utSearchMarkThick(32.0f) == 1.0f &&
                  ut::utSearchMarkThick(44.0f) == 1.0f && ut::utSearchMarkThick(47.0f) == 1.0f &&
                  ut::utSearchMarkThick(48.0f) == 2.0f && ut::utSearchMarkThick(64.0f) == 2.0f,
              "the frame: 1 px below a 48 px cell (UI scale under 1.5), 2 px from there");
    }

    printf("\n8. the real Transfer page: the item map read over a fake VS2012 map, the label\n");
    {
        using namespace ut;
        check(sizeof(UtSackMapNode) == 0x24 && offsetof(UtSackMapNode, key) == 0x10 &&
                  offsetof(UtSackMapNode, isnil) == 0x0D && offsetof(UtSackMapNode, x) == 0x14 &&
                  offsetof(UtSackMapNode, y) == 0x18 && offsetof(UtSackMapNode, w) == 0x1C &&
                  offsetof(UtSackMapNode, h) == 0x20,
              "the node: links +0/+4/+8, isnil +0x0D, the key +0x10, RectExt x/y/w/h +0x14..+0x20");
        // a balanced tree of 7 ids (40 at the root) under the nil head, as VS2012 builds it
        static UtSackMapNode head, nd[7];
        const unsigned ids[7] = {10, 20, 30, 40, 50, 60, 70};
        auto build = [&]() {
            memset(&head, 0, sizeof(head));
            memset(nd, 0, sizeof(nd));
            head.isnil = 1;
            for (int i = 0; i < 7; ++i) {
                nd[i].key = ids[i];
                nd[i].x = (float)(i * 32);
                nd[i].y = (float)(i * 16);
                nd[i].w = 32.0f;
                nd[i].h = 64.0f;
                nd[i].left = nd[i].right = &head;
            }
            // 40 -> (20 -> 10, 30), (60 -> 50, 70)
            nd[3].parent = &head;
            nd[3].left = &nd[1];
            nd[3].right = &nd[5];
            nd[1].parent = &nd[3];
            nd[1].left = &nd[0];
            nd[1].right = &nd[2];
            nd[5].parent = &nd[3];
            nd[5].left = &nd[4];
            nd[5].right = &nd[6];
            nd[0].parent = nd[2].parent = &nd[1];
            nd[4].parent = nd[6].parent = &nd[5];
            head.parent = &nd[3];
            head.left = &nd[0];
            head.right = &nd[6];
        };
        build();
        struct FakeMap {
            const UtSackMapNode* head;
            unsigned size;
        } map = {&head, 7};
        UtSackEntry e[16];
        int n = utSackMapRead(&map, e, 16);
        bool inOrder = n == 7;
        for (int i = 0; inOrder && i < 7; ++i)
            inOrder = e[i].id == ids[i] && e[i].x == (float)(i * 32) && e[i].y == (float)(i * 16) &&
                      e[i].w == 32.0f && e[i].h == 64.0f;
        check(inOrder, "7 items read in id order, each with its RectExt x / y / w / h");
        const unsigned same[7] = {10, 20, 30, 40, 50, 60, 70}, other[7] = {10, 20, 30, 40, 50, 60, 71};
        check(utSackSameIds(e, n, same, 7) && !utSackSameIds(e, n, other, 7) && !utSackSameIds(e, n, same, 6),
              "the id set compare: the same set, one id changed, one id fewer");
        {   // the walk alone (the owned-items reader keeps only the ids)
            unsigned keys[16];
            int got = 0;
            const int w = utSackMapWalk(&map, 8192, [&](const UtSackMapNode& x) {
                keys[got++] = x.key;
                return true;
            });
            bool keysOk = w == 7 && got == 7;
            for (int i = 0; keysOk && i < 7; ++i) keysOk = keys[i] == ids[i];
            check(keysOk, "the walk alone: 7 ids in order");
            int visits = 0;
            check(utSackMapWalk(&map, 8192, [&](const UtSackMapNode&) { return ++visits < 3; }) == -1 &&
                      visits == 3,
                  "a visitor that stops (the ids do not fit) refuses the walk");
        }
        check(utSackMapRead(&map, e, 6) == -1, "more entries than the cap: refused");
        map.size = 6;
        check(utSackMapRead(&map, e, 16) == -1, "a count that is not the stored size: refused");
        map.size = 8;
        check(utSackMapRead(&map, e, 16) == -1, "a stored size larger than the tree: refused");
        map.size = 7;
        head.isnil = 0;
        check(utSackMapRead(&map, e, 16) == -1, "a head that is not the nil node: refused");
        build();
        nd[2].right = &nd[1];   // a cycle: 30's right leads back up into the tree
        check(utSackMapRead(&map, e, 16) == -1, "a cycle is refused (the step bound or the count)");
        build();
        nd[4].parent = nullptr;
        check(utSackMapRead(&map, e, 16) == -1, "a null link is refused");
        build();
        UtSackMapNode lone;
        memset(&lone, 0, sizeof(lone));
        lone.isnil = 1;
        lone.left = lone.right = lone.parent = &lone;
        FakeMap empty = {&lone, 0};
        check(utSackMapRead(&empty, e, 16) == 0, "an empty sack: 0 items");
        FakeMap nohead = {nullptr, 0};
        check(utSackMapRead(&nohead, e, 16) == -1 && utSackMapRead(nullptr, e, 16) == -1,
              "no head / no map: refused");
        // the label's words and its line
        char f[40], b[24], ln[96];
        utSearchRealWords(false, true, 3, 9, 2, f, sizeof(f), b, sizeof(b));
        check(f[0] == 0 && b[0] == 0, "no query: no words");
        utSearchRealWords(true, false, 0, 0, 0, f, sizeof(f), b, sizeof(b));
        check(f[0] == 0 && b[0] == 0, "a query while no read is under way: no words (the page's name alone)");
        utSearchRealWords(true, true, 3, 40, 1, f, sizeof(f), b, sizeof(b));
        check(!strcmp(f, "reading 3/40") && !strcmp(b, "3/40"), "while the items are read: reading 3/40 (3/40)");
        utSearchRealWords(true, true, 40, 40, 9, f, sizeof(f), b, sizeof(b));
        check(!strcmp(f, "found 9") && !strcmp(b, "=9"), "then: found 9 (=9)");
        {   // the key's own state as a second stop for the Backspace repeat
            UtBackRepeat r = {false, 0, 0, 0, false};
            utBackRepeatPoll(&r, true);
            check(!r.held, "the poll does nothing while Backspace is not held");
            utBackRepeatPress(&r, 1000u);
            utBackRepeatPoll(&r, false);
            check(r.held, "an up before any down leaves the repeat to the events");
            utBackRepeatPoll(&r, true);
            utBackRepeatPoll(&r, true);
            check(r.held && r.seenDown, "held while the poll sees the key down");
            utBackRepeatPoll(&r, false);
            check(!r.held && !utBackRepeatStep(&r, 5000u), "an up after a down ends the repeat (a lost release)");
            utBackRepeatPress(&r, 6000u);
            utBackRepeatPoll(&r, false);
            check(r.held, "a new press starts over: its first up needs a down first");
        }
        int lf = utPadLabelRealLine("", "", 260.0f, 13, ln, sizeof(ln));
        check(lf == 13 && !strcmp(ln, "the real Transfer page"), "no query: \"the real Transfer page\" at the label's size");
        lf = utPadLabelRealLine("found 9", "=9", 260.0f, 13, ln, sizeof(ln));
        check(lf == 13 && !strcmp(ln, "the real Transfer page  found 9"), "a query in a 260 px box at 13: the full form");
        lf = utPadLabelRealLine("found 9", "=9", 120.0f, 13, ln, sizeof(ln));
        check(lf == 13 && !strcmp(ln, "Transfer found 9"), "120 px: \"Transfer found 9\"");
        lf = utPadLabelRealLine("found 9", "=9", 90.0f, 13, ln, sizeof(ln));
        check(lf == 13 && !strcmp(ln, "Transfer  =9"), "90 px: \"Transfer  =9\"");
        lf = utPadLabelRealLine("found 9", "=9", 40.0f, 13, ln, sizeof(ln));
        check(lf == 6 && !strcmp(ln, "Transfer  =9"), "40 px: the last form at 6");
        lf = utPadLabelRealLine("reading 3/40", "3/40", 160.0f, 13, ln, sizeof(ln));
        check(lf == 13 && !strcmp(ln, "Transfer reading 3/40"), "reading, 160 px: \"Transfer reading 3/40\"");
        lf = utPadLabelRealLine("", "", 100.0f, 13, ln, sizeof(ln));
        check(lf == 13 && !strcmp(ln, "Transfer"), "no query, 100 px: \"Transfer\"");
        check(utPadFieldLaidOut(true, 1, 0) && utPadFieldLaidOut(true, 1, 1) && utPadFieldLaidOut(false, 1, 1) &&
                  !utPadFieldLaidOut(false, 1, 0) && !utPadFieldLaidOut(true, 0, 1) && !utPadFieldLaidOut(false, 0, 1),
              "the field is laid out with search=1: ON always, OFF with search_transfer=1 only");
    }

    printf("\n%s (%d failure(s))\n", g_fails ? "FAILED" : "PASSED", g_fails);
    return g_fails ? 1 : 0;
}
