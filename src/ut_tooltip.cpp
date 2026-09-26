// ut_tooltip.cpp - the item rollover: the "In your collection" tooltip line.
// Ported from the Grim Dawn mod's ut_tooltip.cpp; see ut_tooltip.h for what it is.
// Only what Titan Quest forces was changed; each change says so ("TQ:").
//
// EVERY address and every constant below was verified offline against the on-disk AE 2.10
// Game.dll / TQ.exe (HANDOFF.md section 5). The short version:
//
//   * TQ: `GameTextLine` is 0x20 bytes: +0x00 u32 GameTextClass, +0x04 basic_string<unsigned
//     short> (VS2012: 16-byte buffer, size at +0x14, capacity at +0x18), +0x1C bool. Decoded from
//     the exported ctor's own bytes (Game.dll 0x1ABCE0, `ret 0xC` - __thiscall, three stack
//     arguments; GD's GraphicsTexture* / float tail does not exist): `mov [esi],eax`,
//     `mov [ecx+0x14],7` + `mov [ecx+0x10],0` + `mov word [ecx],ax` with ecx = this+4 (a _Tidy
//     init of the string in place - it never READS the destination, so a zeroed buffer is a legal
//     `this`), the assign call (0x3B70), `mov [esi+0x1c],al`. Item::GetUIDisplayText builds its
//     own lines inline with exactly that shape (0x1B77E1..0x1B7812: class, the string, the bool,
//     then vector<GameTextLine>::push_back at 0x3CE0).
//
//   * TQ: `GameTextLineToString` (Game.dll 0x1A8F80, __cdecl) touches the vector object at
//     `[vec]` and `[vec+4]` ONLY (`mov esi,[eax]` 0x1A8FC7, `cmp esi,[eax+4]` 0x1A8FC9 / 0x1A91CA,
//     `add esi,0x20` 0x1A91C7) and writes nothing. That, and the fact that the caller keeps the
//     line vector until the call returns, is what makes a BORROWED {first,last,end} triple over a
//     mod-owned array safe. The mod never pushes into the engine's own vector: it may be at
//     capacity, and whatever is pushed the ENGINE destroys.
//
// RULE 9. Both detour bodies contain an engine call - their own trampoline - and neither is
// wrapped in a swallowing frame. All mod work happens OUTSIDE the trampoline call; the borrowed
// array is released in a `__finally`, which is cleanup, not a handler, so an engine C++ exception
// keeps unwinding exactly as it would have. The `__except(EXCEPTION_EXECUTE_HANDLER)` frames in
// this file protect MOD-SIDE READS of engine memory only (the vector shape, the line copy, the
// read-back), and each of them raises utGuardEnter's depth so dllmain's vectored handler stays
// silent for a fault the mod owns.
//
// RULE 7 (liveness) is satisfied structurally on the tooltip path: the detour runs inside the
// item's own method, so `this` is alive by construction - there is no stored object id to
// re-check and no window between a check and a use.
//
// MULTIPLAYER: nothing here leaves the machine. Both functions are pure local UI text, no packet
// type, no replication, no shared object mutated - so this is deliberately NOT behind mpBarred().

#include "ut_tooltip.h"

#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MinHook.h"
#include "hooks.h"   // the cost meter's Present-side stamps
#include "tq_exports.h"
#include "tq_runtime.h"
#include "ut_config.h"
#include "ut_live.h"
#include "ut_log.h"
#include "ut_ownedfold.h"
#include "ut_proto.h"
#include "ut_rescue.h"
#include "ut_store.h"

namespace ut {
namespace {

// ---- the text classes, with their evidence -----------------------------------------------------
// TQ: GD's 84-class table does not exist here. `GameEngine::GetGameTextStyleName` (Game.dll
// 0x191250) looks the class up in the map at GameEngine+0x27D4; the loader at Game.dll 0x1A9380
// fills it from records/game/gameengine.dbr, reading one style FIELD per class in a fixed order
// and inserting it under the next key, 0 first (the key stored before each insert, 1, 2, 3 ...,
// follows the field it belongs to). 58 fields are read, so the classes are 0x00..0x39 with no
// gap. The colours are `fontColorRed/Green/Blue` of the style record each field names (TQ's
// field names; GD's were fontColor0.R/G/B), measured offline from the shipped database.arz.
// Evidence that the key order is right: Item::GetUIDisplayText's own lines are class 0x0E
// (0x1B77E1), "ItemDescription" -> DescBaseStats.dbr, the white body style.
//
// The two defaults (GD chose a mild green and a warm orange at body size; TQ has both):
//   0x16 ItemSetBonuses  -> DescSetBonuses.dbr   (0.25, 1.00, 0.25) = RGB (64,255,64), size 14.
//        COLLECTED. The green the game already uses for set bonuses, at stat-line size.
//   0x18 ItemRelicNumber -> DescRelicNumber.dbr  (1.00, 0.75, 0.00) = RGB (255,191,0), size 14.
//        NOT COLLECTED. A warm amber at body size (GD had to take a title-size orange).
// TQ: GD's two INELIGIBLE classes (dropped by GD's item-box filter pass) have no counterpart: both
// TQ rollover sites hand the vector GetUIDisplayText filled straight to GameTextLineToString
// (TQ.exe 0x10A6C3 -> 0x10A6DE and 0x1934C1 -> 0x1934D0), no filter in between.
struct TextClassInfo {
    unsigned int cls;
    const char* style;
    int r, g, b;    // the style record's fontColor, 0..255
};

const TextClassInfo kTextClasses[] = {
    {0x00, "Default", 255, 255, 255},
    {0x01, "ItemBanner", 153, 153, 153},
    {0x02, "ItemNameCommon", 255, 255, 255},
    {0x03, "ItemNameMagical", 255, 245, 43},
    {0x04, "ItemNameRare", 64, 255, 64},
    {0x05, "ItemNameEpic", 0, 163, 255},
    {0x06, "ItemNameLegendary", 217, 5, 255},
    {0x07, "ItemNameBroken", 153, 153, 153},
    {0x08, "ItemNamePotion", 255, 0, 0},
    {0x09, "ItemNameRelic", 255, 173, 0},
    {0x0A, "ItemNameQuest", 217, 5, 255},
    {0x0B, "ItemNameArtifactFormula", 0, 255, 217},
    {0x0C, "ItemNameArtifact", 0, 255, 217},
    {0x0D, "ItemNameScroll", 0, 255, 168},
    {0x0E, "ItemDescription", 255, 255, 255},
    {0x0F, "ItemBaseStats", 255, 255, 255},
    {0x10, "ItemBonuses", 0, 163, 255},
    {0x11, "ItemRequirements", 179, 179, 179},
    {0x12, "ItemSetName", 64, 255, 64},
    {0x13, "ItemSetDescription", 224, 224, 224},
    {0x14, "ItemSetComponentNotAvailable", 153, 153, 153},
    {0x15, "ItemSetComponentAvailable", 255, 245, 43},
    {0x16, "ItemSetBonuses", 64, 255, 64},
    {0x17, "ItemRelicName", 255, 173, 0},
    {0x18, "ItemRelicNumber", 255, 191, 0},
    {0x19, "ItemRelicDescription", 224, 224, 224},
    {0x1A, "ItemRelicBonus", 0, 163, 255},
    {0x1B, "ItemRelicCompleteTitle", 255, 191, 0},
    {0x1C, "ItemDirections", 255, 255, 255},
    {0x1D, "ItemSkillHeading", 255, 255, 255},
    {0x1E, "ItemSkillName", 0, 163, 255},
    {0x1F, "SkillName", 255, 255, 255},
    {0x20, "SkillDescription", 224, 224, 224},
    {0x21, "SkillLevelTitles", 255, 245, 43},
    {0x22, "SkillStatsCurrent", 255, 255, 255},
    {0x23, "SkillStatsNext", 153, 153, 153},
    {0x24, "SkillRequirements", 64, 255, 64},
    {0x25, "SkillRequirementsNotMet", 255, 0, 0},
    {0x26, "PetSkillNameCurrent", 0, 163, 255},
    {0x27, "PetSkillNameNext", 153, 153, 153},
    {0x28, "PetSkillStatsCurrent", 0, 163, 255},
    {0x29, "PetSkillStatsNext", 153, 153, 153},
    {0x2A, "ArtifactFormulaTitle", 255, 255, 255},
    {0x2B, "ArtifactFormulaDescription", 255, 255, 255},
    {0x2C, "ArtifactFormulaReagents", 255, 191, 0},
    {0x2D, "ArtifactFormulaReagentAvailable", 255, 245, 43},
    {0x2E, "ArtifactFormulaReagentNotAvailable", 153, 153, 153},
    {0x2F, "ArtifactFormulaCostAvailable", 64, 255, 64},
    {0x30, "ArtifactFormulaCostNotAvailable", 255, 0, 0},
    {0x31, "ArtifactTitle", 255, 255, 255},
    {0x32, "ArtifactDescription", 255, 255, 255},
    {0x33, "ArtifactClass", 153, 153, 153},
    {0x34, "ArtifactBonusTitle", 255, 191, 0},
    {0x35, "ArtifactBonus", 0, 163, 255},
    {0x36, "PetBonusTitle", 255, 191, 0},
    {0x37, "PetBonus", 0, 163, 255},
    {0x38, "PetBonusNextTitle", 153, 153, 153},
    {0x39, "PetBonusNext", 153, 153, 153},
};
const int kTextClassCount = (int)(sizeof(kTextClasses) / sizeof(kTextClasses[0]));

const unsigned int kClassCollectedDefault = 0x16;     // ItemSetBonuses, green
const unsigned int kClassNotCollectedDefault = 0x18;  // ItemRelicNumber, warm amber

// The classes actually used, latched at start-up - the prepared lines become engine-owned
// strings, so these can no more follow a live ini edit than the two texts can.
unsigned int g_classYes = kClassCollectedDefault;
unsigned int g_classNo = kClassNotCollectedDefault;

const TextClassInfo* classInfo(unsigned int cls) {
    for (int i = 0; i < kTextClassCount; ++i) {
        if (kTextClasses[i].cls == cls) return &kTextClasses[i];
    }
    return nullptr;
}

const char* const kDefaultTextYes = "In your collection";
const char* const kDefaultTextNo = "Not in your collection";

const size_t kLineSize = 0x20;   // TQ: sizeof(GAME::GameTextLine) on x86 (GD: 0x40)
const size_t kMaxLines = 512;    // a real tooltip is 20-40 lines; anything else is not one
const DWORD kLatchMaxAgeMs = 1000;

// ---- VS2012 std::wstring, built or read by hand ------------------------------------------------
// TQ: the engine is VS2012 (MSVCP110). 16-byte SSO buffer, then size, then capacity - on x86 24
// bytes; the pointer form is used once capacity >= 8 wide characters.
struct MsvcWString {
    union {
        unsigned short buf[8];
        const unsigned short* ptr;
    } bx;
    size_t size;
    size_t res;
};
#if defined(_M_IX86)
static_assert(sizeof(MsvcWString) == 0x18, "VS2012 x86 basic_string<unsigned short> is 0x18 bytes");
#endif

// ---- the resolved exports ---------------------------------------------------------------------
// TQ: __thiscall members (`this` in ECX, callee cleans); GD's trailing `bool detailed` is gone.
typedef void(__thiscall* PfnItem_GetUIDisplayText)(void* item, const void* character, void* lines);
typedef void(__cdecl* PfnGameTextLineToString)(const void* lines, void* out);
// (GameTextLine* this, GameTextClass, const wstring&, bool) - `ret 0xC`.
typedef void(__thiscall* PfnGameTextLine_Ctor)(void* self, unsigned int cls, const void* wstr,
                                               bool flag);
static_assert(TQ_TIP_ITEM_GETUIDISPLAYTEXT_CC == TQCC_THISCALL, "Item::GetUIDisplayText");
static_assert(TQ_TIP_ITEMEQUIPMENT_GETUIDISPLAYTEXT_CC == TQCC_THISCALL, "ItemEquipment");
static_assert(TQ_TIP_ITEMARTIFACT_GETUIDISPLAYTEXT_CC == TQCC_THISCALL, "ItemArtifact");
static_assert(TQ_TIP_ITEMRELIC_GETUIDISPLAYTEXT_CC == TQCC_THISCALL, "ItemRelic");
static_assert(TQ_TIP_GAMETEXTLINETOSTRING_CC == TQCC_CDECL, "GameTextLineToString");
static_assert(TQ_TIP_GAMETEXTLINE_CTOR_CC == TQCC_THISCALL, "GameTextLine ctor");

// The raw addresses GetProcAddress handed back, kept as void* so the alias check and MinHook see
// exactly what the export table holds and no function-pointer cast is ever needed on that path.
void* r_ItemText = nullptr;
void* r_EquipText = nullptr;
void* r_ArtifactText = nullptr;
void* r_RelicText = nullptr;
void* r_ToString = nullptr;
void* r_LineCtor = nullptr;

PfnGameTextLine_Ctor p_LineCtor = nullptr;

PfnItem_GetUIDisplayText o_ItemText = nullptr;
PfnItem_GetUIDisplayText o_EquipText = nullptr;
PfnItem_GetUIDisplayText o_ArtifactText = nullptr;
PfnItem_GetUIDisplayText o_RelicText = nullptr;
PfnGameTextLineToString o_ToString = nullptr;

// ---- session state ----------------------------------------------------------------------------
// THE fault latch. 1 = the "already collected" mark is off for the rest of the process. Set by a
// missing export, a refused (folded) export, a failed install, a line that did not verify, or the
// first fault in the swap. Mod-owned volatile LONG, never a g_cfg field - configReload replaces
// g_cfg once a second.
volatile LONG g_tooltipOff = 0;

volatile LONG g_hooksOn = 0;
volatile LONG g_linesReady = 0;
volatile LONG g_swaps = 0;        // tooltips that got the extra line
volatile LONG g_latches = 0;      // collectible items whose tooltip was latched
volatile LONG g_allocBalance = 0; // borrowed arrays alive right now; 0 at rest, always
volatile LONG g_faults = 0;
volatile LONG g_refusedShape = 0; // vectors the swap refused because their shape made no sense
char g_offWhy[192] = "";

// The two prepared lines. Built ONCE per process by the engine's own exported constructor, never
// destroyed and never freed - one bounded allocation of a few dozen bytes per state, which is not
// a leak that grows.
unsigned char g_lineYes[kLineSize];
unsigned char g_lineNo[kLineSize];

char g_textYes[96] = "";
char g_textNo[96] = "";

char g_status[320] = "tooltip: not initialised";

// ---- little helpers ---------------------------------------------------------------------------
void* proc(HMODULE m, const char* name, const char* pretty) {
    void* p = m ? (void*)GetProcAddress(m, name) : nullptr;
    logT("  tooltip export %-34s %s", pretty, p ? "ok" : "MISSING");
    return p;
}

// MSVC's identical-COMDAT folding makes hundreds of exports
// share one address, and detouring such an address changes every one of them. Exactly one entry
// of the module's own export address table may point at a target we are about to hook. (None of
// these six is folded on AE 2.10; the rule is unconditional, so it is asked anyway.)
int exportAliasCount(HMODULE mod, const void* addr) {
    if (!mod || !addr) return -1;
    __try {
        const unsigned char* base = (const unsigned char*)mod;
        const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return -1;
        // TQ: the native header (IMAGE_NT_HEADERS32 on this x86 build; GD read the 64-bit one).
        const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return -1;
        const IMAGE_DATA_DIRECTORY& d =
            nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (!d.VirtualAddress || !d.Size) return -1;
        const IMAGE_EXPORT_DIRECTORY* ex = (const IMAGE_EXPORT_DIRECTORY*)(base + d.VirtualAddress);
        const DWORD* fn = (const DWORD*)(base + ex->AddressOfFunctions);
        const DWORD want = (DWORD)((const unsigned char*)addr - base);
        int n = 0;
        for (DWORD i = 0; i < ex->NumberOfFunctions; ++i) {
            if (fn[i] == want) ++n;
        }
        return n;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// THE capitalised line the menu test asserts is ABSENT. Every "the mark cannot run" path ends
// here, exactly once, and the feature never re-arms in this process.
void disable(const char* why) {
    if (InterlockedExchange(&g_tooltipOff, 1)) return;
    _snprintf_s(g_offWhy, sizeof(g_offWhy), _TRUNCATE, "%s", why ? why : "?");
    logE("tooltip: OFF ***** the \"already collected\" line is off for this session ***** - %s",
         g_offWhy);
    logD("every item rollover is the vanilla one again; nothing persistent was touched");
}

void faulted(const char* where) {
    InterlockedIncrement(&g_faults);
    logW("tooltip: FAULT in %s - the mark disables itself now", where ? where : "?");
    disable("a fault inside the mod's own tooltip code");
}

// ---- reading engine memory (MOD-SIDE reads: a swallowing frame is the right shape here) --------
// TQ: the record. GD read ItemReplicaInfo's base-record string straight out of the Item with one
// SEH memcpy (readRecordSeh); TQ asks Object::GetObjectName (a trivial getter that takes no lock)
// and copies the name with ONE bounded loop inside ONE frame, then folds it to the catalogue's
// spelling (lower case, '/'). An earlier version went through ut_proto's objectNameFolded,
// whose per-byte safeRead cost a VirtualQuery per character - on every GetUIDisplayText of every
// item, every frame. A fault is swallowed like GD's: no record, so no line (the line stays armed).
bool readRecordSeh(const void* item, char* raw, size_t cap) {
    __try {
        const char* name = g_tq.ObjectGetObjectName(item);
        size_t i = 0;
        if (name) {
            for (; i + 1 < cap && name[i]; ++i) raw[i] = name[i];
        }
        raw[i] = 0;
        return i != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        raw[0] = 0;
        return false;
    }
}

bool readRecord(const void* item, char* out, size_t cap) {
    if (out && cap) out[0] = 0;
    if (!item || !out || cap < 8 || !g_tq.ObjectGetObjectName) return false;
    char raw[300];
    utGuardEnter();
    const bool ok = readRecordSeh(item, raw, sizeof(raw));
    utGuardLeave();
    if (!ok) return false;
    utOwnedNormaliseKey(raw, out, cap);
    return out[0] != 0;
}

// VS2012 std::vector<GameTextLine> is {first, last, end}; GameTextLineToString reads the first
// two words and nothing else.
bool readVecSeh(const void* vec, const unsigned char** begin, const unsigned char** end) {
    __try {
        const unsigned char* const* p = (const unsigned char* const*)vec;
        *begin = p[0];
        *end = p[1];
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *begin = nullptr;
        *end = nullptr;
        return false;
    }
}

bool readClassSeh(const unsigned char* line, unsigned int* out) {
    __try {
        *out = *(const unsigned int*)line;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *out = 0;
        return false;
    }
}

// How many lines the vector holds, where they start, and the class of its first and last one.
// That is the fingerprint the swap matches the latch against (see pendingLine()).
bool readVecShape(const void* vec, const unsigned char** first, unsigned int* count,
                  unsigned int* firstCls, unsigned int* lastCls) {
    *first = nullptr;
    *count = 0;
    *firstCls = 0;
    *lastCls = 0;
    const unsigned char* begin = nullptr;
    const unsigned char* end = nullptr;
    utGuardEnter();
    bool ok = readVecSeh(vec, &begin, &end);
    if (ok) {
        if (!begin || end < begin || ((size_t)(end - begin) % kLineSize) != 0) {
            ok = false;
        } else {
            const size_t n = (size_t)(end - begin) / kLineSize;
            if (n == 0 || n > kMaxLines) {
                ok = false;
            } else {
                ok = readClassSeh(begin, firstCls) &&
                     readClassSeh(end - kLineSize, lastCls);
                if (ok) {
                    *count = (unsigned int)n;
                    *first = begin;
                }
            }
        }
    }
    utGuardLeave();
    return ok;
}

// The class of one line of an already-validated array. Used for the PREFIX test below.
bool classAt(const unsigned char* first, unsigned int index, unsigned int* out) {
    utGuardEnter();
    const bool ok = readClassSeh(first + (size_t)index * kLineSize, out);
    utGuardLeave();
    return ok;
}

// ---- THE LATCH ---------------------------------------------------------------------------------
// GD matched the swap to the latch by fingerprint because its item-box rollover COPIED the
// vector through a filter pass between GetUIDisplayText and GameTextLineToString. TQ: both item
// rollover sites in TQ.exe hand the SAME vector straight through (0x10A6C3 fills [esp+0x1c] and
// 0x10A6DE passes it; 0x1934C1 fills [esp+0x18] and 0x1934D0 passes it; the only call in between,
// the item's vftable +0x154 at 0x10A6CF, is not a text call), so the EXACT tier fires. The rule is
// kept whole anyway, because it costs nothing and protects every other path:
//
// The match is: SAME THREAD (the latch is thread-local), FRESH (< 1 s), NOT YET CONSUMED, and
// one of two things:
//   * the same {line count, first class, last class} fingerprint;
//   * a PREFIX match: more lines than the latch counted, the same first class, and the latched
//     last class still sitting at index count-1 - the shape a caller leaves when it appends its
//     own lines (a vendor price, say) to the same vector after GetUIDisplayText.
//
// Stale-safety, two rules, both deliberate:
//   (a) the fingerprint is always asked - stack addresses repeat, so a bare pointer is no proof.
//   (b) the latch is CONSUMED BY THE FIRST GameTextLineToString after it, match or no match.
//       A latch that survived a non-match would go stale, and latches are routinely left
//       unclaimed: GetUIDisplayText has non-tooltip callers, and the OneShot_* / QuestItem /
//       ItemArtifactFormula overrides are NOT hooked (OneShot_Dye/Potion/Scroll and QuestItem
//       call Item's base, which IS hooked; they are never catalogue records, so the latch is
//       never set for them).
struct Latch {
    unsigned int count;
    unsigned int firstClass;
    unsigned int lastClass;
    int state;  // 0 = none / consumed, 1 = COLLECTED, 2 = NOT COLLECTED
    DWORD tick;
};
__declspec(thread) Latch t_latch;
// Set by the property search around its own GetUIDisplayText call (tooltipSearchCapture).
__declspec(thread) bool t_searchCapture = false;

void clearLatch() {
    t_latch.state = 0;
}

const unsigned char* pendingLine(const void* vec) {
    if (!vec) return nullptr;
    if (InterlockedCompareExchange(&g_tooltipOff, 0, 0)) return nullptr;
    if (!InterlockedCompareExchange(&g_linesReady, 0, 0)) return nullptr;
    if (!g_cfg.tooltipMark) return nullptr;
    if (t_latch.state != 1 && t_latch.state != 2) return nullptr;
    const Latch cur = t_latch;   // POD copy
    clearLatch();                // ONE-SHOT: consumed here whether it matches below or not
    if (GetTickCount() - cur.tick > kLatchMaxAgeMs) return nullptr;
    if (cur.count == 0) return nullptr;
    const unsigned char* first = nullptr;
    unsigned int count = 0, firstCls = 0, lastCls = 0;
    if (!readVecShape(vec, &first, &count, &firstCls, &lastCls)) return nullptr;
    bool prefixRead = false;
    unsigned int atPrefixEnd = 0;
    if (count > cur.count && firstCls == cur.firstClass) {
        prefixRead = classAt(first, cur.count - 1, &atPrefixEnd);
    }
    if (!tooltipLatchMatch(cur.count, cur.firstClass, cur.lastClass, count, firstCls, lastCls,
                           prefixRead, atPrefixEnd)) {
        return nullptr;
    }
    return cur.state == 1 ? g_lineYes : g_lineNo;
}

// ---- THE SWAP ----------------------------------------------------------------------------------
bool copyLinesSeh(unsigned char* dst, const unsigned char* src, size_t bytes) {
    __try {
        memcpy(dst, src, bytes);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

// The pure half of pendingLine, exposed so the offline harness asks the same rule.
int tooltipLatchMatch(unsigned latchCount, unsigned latchFirst, unsigned latchLast,
                      unsigned count, unsigned firstCls, unsigned lastCls, bool prefixRead,
                      unsigned atPrefixEnd) {
    if (latchCount == 0 || count == 0) return 0;
    if (firstCls != latchFirst) return 0;
    if (count == latchCount && lastCls == latchLast) return 1;               // exact
    if (count > latchCount && prefixRead && atPrefixEnd == latchLast) return 2;  // prefix
    return 0;
}

// Factored out of the detour so the offline harness (tools/test_tooltip.cpp) drives exactly the
// code the game drives. Builds an array of N+1 GameTextLine records: the engine's N copied byte
// for byte, then `extraLine`. Returns false - and allocates nothing - for any vector whose shape
// is not a plausible tooltip.
bool tooltipSwapBuild(const void* vec, const unsigned char* extraLine, unsigned char** ownedOut,
                      UtTooltipSwap* out) {
    if (ownedOut) *ownedOut = nullptr;
    if (!vec || !extraLine || !ownedOut || !out) return false;
    const unsigned char* begin = nullptr;
    const unsigned char* end = nullptr;
    utGuardEnter();
    const bool got = readVecSeh(vec, &begin, &end);
    utGuardLeave();
    if (!got || !begin || end < begin) {
        InterlockedIncrement(&g_refusedShape);
        return false;
    }
    const size_t bytes = (size_t)(end - begin);
    if ((bytes % kLineSize) != 0) {
        InterlockedIncrement(&g_refusedShape);
        return false;
    }
    const size_t n = bytes / kLineSize;
    if (n == 0 || n > kMaxLines) {
        InterlockedIncrement(&g_refusedShape);
        return false;
    }
    unsigned char* buf = (unsigned char*)malloc((n + 1) * kLineSize);
    if (!buf) return false;
    utGuardEnter();
    const bool copied = copyLinesSeh(buf, begin, bytes);
    utGuardLeave();
    if (!copied) {
        free(buf);
        return false;
    }
    memcpy(buf + bytes, extraLine, kLineSize);
    out->begin = buf;
    out->end = buf + bytes + kLineSize;
    out->cap = out->end;
    *ownedOut = buf;
    InterlockedIncrement(&g_allocBalance);
    return true;
}

void tooltipSwapFree(unsigned char* owned) {
    if (!owned) return;
    free(owned);
    InterlockedDecrement(&g_allocBalance);
}

long tooltipSwapAllocBalance() {
    return InterlockedCompareExchange(&g_allocBalance, 0, 0);
}

namespace {

// ---- "is this record already collected?", MEMOISED ----------------------------------------------
// TQ: two questions, both answered here. `liveHasRecord` (is it one of the catalogue's records?)
// is a case-insensitive LINEAR scan of every group's records, and `storeCount` (the journal's rows
// of that record) takes the journal's lock - the one the worker holds across a whole journal
// write, which runs the moment a deposit settles, i.e. exactly while the user is standing at the
// caravan hovering items.
//
// The rollover is rebuilt EVERY FRAME while the cursor rests on an item, so the same record would
// be asked 60-144 times a second. It is asked at most four times a second per record. A quarter
// of a second of staleness is invisible: the only thing that can change the answer is the player's
// own deposit or take, and both take far longer than that to complete.
struct Collected {
    char record[256];
    int state;   // 0 = nothing cached, 1 = COLLECTED, 2 = NOT COLLECTED, 3 = NOT A CATALOGUE RECORD
    DWORD tick;
};
__declspec(thread) Collected t_collected;

const DWORD kMemoMaxAgeMs = 250;

// THE ONLY LOCK ON THE TOOLTIP PATH. Called from latchBody OUTSIDE every SEH frame - see the
// comment there for why that placement is not negotiable.
int collectedState(const char* record, bool* fromMemo) {
    if (fromMemo) *fromMemo = false;
    // while the journal's save set is not open (a world is still loading, or its set
    // could not be opened) storeCount answers 0 for EVERY record, so every catalogue unique would
    // read "Not in your collection". The page's veils guard the same way (ut_owned.cpp:
    // journalSetKnown()). No line, and the answer is NOT memoised: the first hover after the set
    // opens asks afresh. journalSetKnown is lock-free (an interlocked read of the journal's flag).
    if (!journalSetKnown()) {
        t_collected.state = 0;
        return 3;
    }
    const DWORD now = GetTickCount();
    if (t_collected.state && (now - t_collected.tick) <= kMemoMaxAgeMs &&
        _stricmp(t_collected.record, record) == 0) {
        if (fromMemo) *fromMemo = true;
        return t_collected.state;
    }
    // NOT COLLECTIBLE: the record is not one of the catalogue's. Nothing is added - a potion, a
    // relic, a charm, a common or rare item and every quest item keep the vanilla tooltip
    // exactly (a record that is not in the catalogue gets NO line).
    // COLLECTED: the journal has a row for the record (a deposit) - the rule the collection page's
    // veils use (section 4: a copy in the inventory is NOT collected).
    int state = 3;
    if (liveHasRecord(record)) state = storeCount(record) > 0 ? 1 : 2;
    if (state == 2 && !journalSetKnown()) {   // the set closed under the question: the same rule
        t_collected.state = 0;
        return 3;
    }
    _snprintf_s(t_collected.record, sizeof(t_collected.record), _TRUNCATE, "%s", record);
    t_collected.state = state;
    t_collected.tick = now;
    return state;
}

// ---- the detour bodies -------------------------------------------------------------------------
struct LatchPrep {
    char record[256];
    unsigned int count;
    unsigned int firstClass;
    unsigned int lastClass;
};

// MOD-SIDE READS OF ENGINE MEMORY, and NOTHING ELSE: no lock, no allocation, no logging. That is
// what lets the caller wrap this - and only this - in a swallowing frame. TQ: the catalogue test
// moved OUT of it into the memo (collectedState), because TQ's is a scan of mod-owned strings,
// not GD's lock-free hash lookup.
bool latchRead(void* item, void* lines, LatchPrep* p) {
    if (!readRecord(item, p->record, sizeof(p->record)) || !p->record[0]) return false;
    const unsigned char* first = nullptr;
    return readVecShape(lines, &first, &p->count, &p->firstClass, &p->lastClass);
}

// -1 = faulted, 0 = nothing to latch, 1 = `p` is filled.
int latchReadGuarded(void* item, void* lines, LatchPrep* p) {
    __try {
        return latchRead(item, lines, p) ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;   // faulted() is called by the CALLER, outside this frame - it takes the log lock
    }
}

// The mod half of the latch. Runs AFTER the trampoline, so the vector is already filled.
//
// The shape of this function is deliberate. The whole body must NOT sit inside one
// `__except(EXCEPTION_EXECUTE_HANDLER)`, because the body takes TWO critical sections: the
// journal's (`storeCount`) and the log's (`logf`). A fault swallowed with either of them held
// would never leave it - a Windows CRITICAL_SECTION is not released by unwinding - and the
// journal's lock is what the worker's journal write and a deposit on this same game thread both
// wait on, so the game would hang on the next deposit.
//
// So: the SEH frame covers ONLY the pure reads of engine memory (latchReadGuarded), which take no
// lock at all, and everything that locks happens after it has been left.
void latchBody(void* item, void* lines) {
    // FIRST: a search capture is not a rollover. It must not clear a real pending latch (the
    // clearLatch below), take the journal lock (collectedState) or leave a latch of its own.
    if (t_searchCapture) return;
    if (InterlockedCompareExchange(&g_tooltipOff, 0, 0)) return;
    if (!g_cfg.tooltipMark) {
        clearLatch();
        return;
    }
    LatchPrep prep;
    prep.record[0] = 0;
    prep.count = 0;
    prep.firstClass = 0;
    prep.lastClass = 0;
    const int got = latchReadGuarded(item, lines, &prep);
    clearLatch();
    if (got <= 0) {
        if (got < 0) faulted("the GetUIDisplayText latch");   // OUTSIDE the frame: it logs
        return;
    }
    // ---- from here on nothing is inside an SEH frame and every read is mod-owned ---------------
    try {
        const int state = collectedState(prep.record, nullptr);   // the journal lock, <= 4 Hz/record
        if (state != 1 && state != 2) return;                     // not a catalogue record: no line
        Latch fresh;
        fresh.count = prep.count;
        fresh.firstClass = prep.firstClass;
        fresh.lastClass = prep.lastClass;
        fresh.state = state;
        fresh.tick = GetTickCount();
        t_latch = fresh;
        InterlockedIncrement(&g_latches);
        // The only telemetry this feature has. It lives HERE rather than in the worker's heartbeat
        // because here it is more useful: it prints exactly while someone is hovering
        // collectible items, at most once a minute, and never at the menu.
        // (Constant-initialised POD, so no thread-safe-static guard is emitted.)
        static DWORD lastStatus = 0;
        if (fresh.tick - lastStatus > 60000) {
            lastStatus = fresh.tick;
            logD("%s", tooltipStatus());
        }
    } catch (...) {
        // Every C++ body reachable from a detour has one. Nothing above allocates and storeCount
        // cannot throw past its own lock, so this is belt and braces rather than the lock guard -
        // the lock guard is the fact that there is no SEH frame around it.
        clearLatch();
    }
}

// TQ: x86 __thiscall targets are detoured through __fastcall(this, edx, args...) - the same stack
// shape (src\tq_runtime.h); `edx` is unused.
void __fastcall hk_ItemGetUIDisplayText(void* item, void* /*edx*/, const void* character,
                                        void* lines) {
    if (o_ItemText) o_ItemText(item, character, lines);  // ENGINE CALL - unguarded
    const long long t0 = probeNow();   // the mod's part only, never the engine's
    latchBody(item, lines);
    probePresentAdd(probeNow() - t0);
}

// ItemEquipment CALLS the base at Game.dll 0x1C00D5 and then appends its own lines (the pet
// bonus, the item set, the extra relic line), so the latch the base detour takes sees a PARTIAL
// vector. This detour runs on the way out and overwrites it with the complete one - which is what
// makes the exact fingerprint tier cover every weapon, armour and jewellery unique.
void __fastcall hk_EquipGetUIDisplayText(void* item, void* /*edx*/, const void* character,
                                         void* lines) {
    if (o_EquipText) o_EquipText(item, character, lines);
    const long long t0 = probeNow();
    latchBody(item, lines);
    probePresentAdd(probeNow() - t0);
}

void __fastcall hk_ArtifactGetUIDisplayText(void* item, void* /*edx*/, const void* character,
                                            void* lines) {
    if (o_ArtifactText) o_ArtifactText(item, character, lines);
    const long long t0 = probeNow();
    latchBody(item, lines);
    probePresentAdd(probeNow() - t0);
}

void __fastcall hk_RelicGetUIDisplayText(void* item, void* /*edx*/, const void* character,
                                         void* lines) {
    if (o_RelicText) o_RelicText(item, character, lines);
    const long long t0 = probeNow();
    latchBody(item, lines);
    probePresentAdd(probeNow() - t0);
}

// No C++ object and no swallowing frame lives in this function: the `__finally` is CLEANUP, so an
// engine C++ exception out of the trampoline keeps unwinding to the engine's own handler.
void __cdecl hk_GameTextLineToString(const void* lines, void* out) {
    const long long t0 = probeNow();   // the match and the swap build, never the engine's
    const unsigned char* extra = nullptr;
    __try {
        extra = pendingLine(lines);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        extra = nullptr;
        faulted("the GameTextLineToString match");
    }
    if (!extra) {
        probePresentAdd(probeNow() - t0);
        if (o_ToString) o_ToString(lines, out);  // ENGINE CALL - unguarded
        return;
    }
    unsigned char* owned = nullptr;
    UtTooltipSwap borrowed;
    borrowed.begin = nullptr;
    borrowed.end = nullptr;
    borrowed.cap = nullptr;
    if (!tooltipSwapBuild(lines, extra, &owned, &borrowed)) {
        probePresentAdd(probeNow() - t0);
        if (o_ToString) o_ToString(lines, out);
        return;
    }
    InterlockedIncrement(&g_swaps);
    probePresentAdd(probeNow() - t0);
    __try {
        if (o_ToString) o_ToString(&borrowed, out);  // ENGINE CALL - unguarded
    } __finally {
        tooltipSwapFree(owned);
    }
}

// ---- building the two static lines --------------------------------------------------------------
void makeWide(MsvcWString* s, const char* ascii, unsigned short* storage, size_t storageChars) {
    memset(s, 0, sizeof(*s));
    size_t n = strlen(ascii);
    if (n > storageChars - 1) n = storageChars - 1;
    for (size_t i = 0; i < n; ++i) storage[i] = (unsigned short)(unsigned char)ascii[i];
    storage[n] = 0;
    s->size = n;
    if (n < 8) {
        memcpy(s->bx.buf, storage, (n + 1) * sizeof(unsigned short));
        s->bx.buf[n] = 0;
        s->res = 7;
    } else {
        s->bx.ptr = storage;
        s->res = n;
    }
}

// Reads back what the ctor wrote. A mod-side read of a mod-owned buffer, so the swallowing frame
// is the right shape; a mismatch means the export is not the constructor we think it is and the
// feature turns itself off before a single tooltip has been touched.
bool verifyLineSeh(const unsigned char* line, unsigned int cls, size_t wantLen) {
    __try {
        if (*(const unsigned int*)line != cls) return false;
        const MsvcWString* s = (const MsvcWString*)(line + 0x04);   // TQ: +0x04 (GD +0x08)
        if (s->size != wantLen) return false;
        if (s->res < s->size) return false;
        const unsigned short* text = (s->res < 8) ? s->bx.buf : s->bx.ptr;
        if (!text && wantLen) return false;
        if (line[0x1C] != 0) return false;                           // TQ: the bool at +0x1C
        // TQ: no GraphicsTexture* / float scale to read back - the TQ line has neither.
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool buildLine(unsigned char* line, unsigned int cls, const char* ascii) {
    memset(line, 0, kLineSize);
    unsigned short storage[128];
    MsvcWString in;
    makeWide(&in, ascii, storage, 128);
    // ENGINE CALL, deliberately UNGUARDED (rule 9). dllmain's vectored handler already logs any
    // access violation with one of this DLL's frames on the stack, and the ctor never reads its
    // destination - it _Tidy_init's the string in place before assigning.
    p_LineCtor(line, cls, &in, false);
    // The verification re-reads the +0x00 GameTextClass word among the rest, checked against the
    // ini-selected class, so this line is also the proof that the colour the player chose is the
    // one in the record.
    const bool ok = verifyLineSeh(line, cls, in.size);
    const TextClassInfo* ci = classInfo(cls);
    logD("tooltip: line class 0x%02X = style \"%s\" RGB (%d,%d,%d) - \"%s\" (%u chars) "
         "built by the engine's own ctor -> %s",
         cls, ci ? ci->style : "(unregistered)", ci ? ci->r : -1,
         ci ? ci->g : -1, ci ? ci->b : -1, ascii, (unsigned)in.size,
         ok ? "verified (class, string size and the +0x1C bool all read back)"
            : "***** DID NOT VERIFY *****");
    return ok;
}

const char* textFor(const char* fromIni, const char* fallback) {
    if (fromIni && fromIni[0]) return fromIni;
    return fallback;
}

}  // namespace

// An UNREGISTERED class is neither a crash nor a refusal by the engine: GetGameTextStyleName's
// map miss (Game.dll 0x191277) builds a ZERO-LENGTH std::string and returns it - so the line would
// simply lose its style. The mod refuses the value itself rather than shipping a colourless line,
// and says so once. from the ini this refusal cannot fire - the table has no gaps
// (0x00..0x39) and ut_config's UT_INT range 0..57 clamps first, so a typed 84 arrives here as 57
// (with the config's own clamp line). It stays for a direct caller (the harness asks 84, 58, -1).
unsigned tooltipPickClass(int fromIni, unsigned fallback, const char* which) {
    if (fromIni == (int)fallback) return fallback;
    if (fromIni >= 0 && classInfo((unsigned int)fromIni)) return (unsigned int)fromIni;
    logW("tooltip: %s=%d is not a text style this game registers (0..%d) - the default 0x%02X "
         "is kept",
         which, fromIni, (int)kTextClasses[kTextClassCount - 1].cls, fallback);
    logD("an unregistered class has an empty style name, so the line would lose its colour "
         "(%d classes are registered)", kTextClassCount);
    return fallback;
}

int tooltipCollectedState(const char* record, bool* fromMemo) {
    return collectedState(record ? record : "", fromMemo);
}

const char* tooltipTextYes() { return g_textYes; }
const char* tooltipTextNo() { return g_textNo; }

bool tooltipVerifyLine(const unsigned char* line, unsigned cls, size_t wantLen) {
    utGuardEnter();
    const bool ok = verifyLineSeh(line, cls, wantLen);
    utGuardLeave();
    return ok;
}

bool tooltipIsOff() { return InterlockedCompareExchange(&g_tooltipOff, 0, 0) != 0; }

void tooltipSearchCapture(bool on) { t_searchCapture = on; }

int tooltipTextBuilderIndex(const void* fn) {
    if (!fn) return -1;
    if (fn == r_ItemText) return 0;
    if (fn == r_EquipText) return 1;
    if (fn == r_ArtifactText) return 2;
    if (fn == r_RelicText) return 3;
    return -1;
}

bool tooltipTextBuilderKnown(const void* fn) { return tooltipTextBuilderIndex(fn) >= 0; }

bool tooltipInit(HMODULE selfModule) {
    (void)selfModule;
    static bool done = false;
    if (done) return InterlockedCompareExchange(&g_tooltipOff, 0, 0) == 0;
    done = true;

    _snprintf_s(g_textYes, sizeof(g_textYes), _TRUNCATE, "%s",
                textFor(g_cfg.tooltipTextYes, kDefaultTextYes));
    _snprintf_s(g_textNo, sizeof(g_textNo), _TRUNCATE, "%s",
                textFor(g_cfg.tooltipTextNo, kDefaultTextNo));

    // The policy line - printed whatever the keys say, so the menu test can always see it.
    logD("tooltip: tooltip_mark=%d - the mark is five exported Game.dll detours that append one "
         "GameTextLine and never draw a pixel. collected=\"%s\" notCollected=\"%s\"",
         g_cfg.tooltipMark, g_textYes, g_textNo);

    HMODULE game = GetModuleHandleA("Game.dll");
    if (!game) {
        disable("Game.dll is not loaded");
        return false;
    }
    r_ItemText = proc(game, TQ_TIP_ITEM_GETUIDISPLAYTEXT, "Item::GetUIDisplayText");
    r_EquipText = proc(game, TQ_TIP_ITEMEQUIPMENT_GETUIDISPLAYTEXT,
                       "ItemEquipment::GetUIDisplayText");
    r_ArtifactText = proc(game, TQ_TIP_ITEMARTIFACT_GETUIDISPLAYTEXT,
                          "ItemArtifact::GetUIDisplayText");
    r_RelicText = proc(game, TQ_TIP_ITEMRELIC_GETUIDISPLAYTEXT, "ItemRelic::GetUIDisplayText");
    r_ToString = proc(game, TQ_TIP_GAMETEXTLINETOSTRING, "GameTextLineToString");
    r_LineCtor = proc(game, TQ_TIP_GAMETEXTLINE_CTOR, "GameTextLine::GameTextLine");

    if (!r_ItemText || !r_EquipText || !r_ArtifactText || !r_RelicText || !r_ToString ||
        !r_LineCtor) {
        disable("at least one tooltip export is MISSING");
        return false;
    }
    const int a1 = exportAliasCount(game, r_ItemText);
    const int a2 = exportAliasCount(game, r_EquipText);
    const int a3 = exportAliasCount(game, r_ArtifactText);
    const int a4 = exportAliasCount(game, r_RelicText);
    const int a5 = exportAliasCount(game, r_ToString);
    const int a6 = exportAliasCount(game, r_LineCtor);
    logD("tooltip: export-alias check - Item %p x%d, ItemEquipment %p x%d, ItemArtifact %p x%d, "
         "ItemRelic %p x%d, GameTextLineToString %p x%d, GameTextLine ctor %p x%d (1 = not folded)",
         r_ItemText, a1, r_EquipText, a2, r_ArtifactText, a3, r_RelicText, a4, r_ToString, a5,
         r_LineCtor, a6);
    if (a1 != 1 || a2 != 1 || a3 != 1 || a4 != 1 || a5 != 1 || a6 != 1) {
        disable("a tooltip export is FOLDED across several names - detouring it would change "
                "every one of them");
        return false;
    }
    p_LineCtor = (PfnGameTextLine_Ctor)r_LineCtor;

    // The two classes are ini-selectable, and both are validated against the
    // measured registration table before a single GameTextLine is built.
    g_classYes = tooltipPickClass(g_cfg.tooltipClassYes, kClassCollectedDefault,
                                  "tooltip_class_yes");
    g_classNo = tooltipPickClass(g_cfg.tooltipClassNo, kClassNotCollectedDefault,
                                 "tooltip_class_no");
    const bool okYes = buildLine(g_lineYes, g_classYes, g_textYes);
    const bool okNo = buildLine(g_lineNo, g_classNo, g_textNo);
    if (!okYes || !okNo) {
        disable("a prepared GameTextLine did not verify after the engine's ctor ran");
        return false;
    }
    InterlockedExchange(&g_linesReady, 1);
    const TextClassInfo* iy = classInfo(g_classYes);
    const TextClassInfo* in = classInfo(g_classNo);
    logD("tooltip: both lines ready - COLLECTED is class 0x%02X \"%s\" RGB (%d,%d,%d) and NOT "
         "COLLECTED is class 0x%02X \"%s\" RGB (%d,%d,%d); the colours come from the style "
         "records records/game/gameengine.dbr names, read back through the engine's own "
         "GameEngine::GetGameTextStyleName - the mod draws nothing",
         g_classYes, iy ? iy->style : "?", iy ? iy->r : -1, iy ? iy->g : -1, iy ? iy->b : -1,
         g_classNo, in ? in->style : "?", in ? in->r : -1, in ? in->g : -1, in ? in->b : -1);
    return true;
}

const int kJobs = 5;

int tooltipInstall(int* total) {
    if (total) *total = kJobs;
    if (InterlockedCompareExchange(&g_tooltipOff, 0, 0)) {
        logE("tooltip: no detour is installed - %s", g_offWhy);
        return 0;
    }
    // ALL-OR-NOTHING. Every hook is CREATED first; only when all five exist are they enabled.
    // Why FIVE: `ItemEquipment` calls the base and then appends, so the base hook alone latches a
    // partial vector for the whole catalogue; `ItemArtifact` and `ItemRelic` do not call the base
    // at all, so they need their own.
    struct {
        const char* pretty;
        void* target;
        void* detour;
        void** original;
    } jobs[kJobs] = {
        {"Item::GetUIDisplayText", r_ItemText, (void*)&hk_ItemGetUIDisplayText,
         (void**)&o_ItemText},
        {"ItemEquipment::GetUIDisplayText", r_EquipText, (void*)&hk_EquipGetUIDisplayText,
         (void**)&o_EquipText},
        {"ItemArtifact::GetUIDisplayText", r_ArtifactText, (void*)&hk_ArtifactGetUIDisplayText,
         (void**)&o_ArtifactText},
        {"ItemRelic::GetUIDisplayText", r_RelicText, (void*)&hk_RelicGetUIDisplayText,
         (void**)&o_RelicText},
        {"GameTextLineToString", r_ToString, (void*)&hk_GameTextLineToString,
         (void**)&o_ToString},
    };
    int created = 0;
    for (int i = 0; i < kJobs; ++i) {
        const MH_STATUS s = MH_CreateHook(jobs[i].target, jobs[i].detour, jobs[i].original);
        if (s != MH_OK) {
            const char* t = MH_StatusToString(s);
            logE("  tooltip hook %-32s MH_CreateHook FAILED: %s", jobs[i].pretty, t ? t : "?");
            break;
        }
        ++created;
    }
    if (created != kJobs) {
        for (int i = 0; i < created; ++i) MH_RemoveHook(jobs[i].target);
        disable("a tooltip detour could not be created - the group installs ALL or NOTHING");
        return 0;
    }
    int enabled = 0;
    for (int i = 0; i < kJobs; ++i) {
        const MH_STATUS s = MH_EnableHook(jobs[i].target);
        if (s != MH_OK) {
            const char* t = MH_StatusToString(s);
            logE("  tooltip hook %-32s MH_EnableHook FAILED: %s", jobs[i].pretty, t ? t : "?");
            break;
        }
        logD("  tooltip hook %-32s installed at %p (trampoline %p)", jobs[i].pretty,
             jobs[i].target, *jobs[i].original);
        ++enabled;
    }
    if (enabled != kJobs) {
        for (int i = 0; i < kJobs; ++i) MH_DisableHook(jobs[i].target);
        for (int i = 0; i < kJobs; ++i) MH_RemoveHook(jobs[i].target);
        disable("a tooltip detour could not be enabled - the group installs ALL or NOTHING");
        return 0;
    }
    InterlockedExchange(&g_hooksOn, 1);
    // THE one line per session naming the hooked functions.
    logI("tooltip: the collection line is on - 5 of 5 detours installed: Item, ItemEquipment, "
         "ItemArtifact and ItemRelic ::GetUIDisplayText, GameTextLineToString");
    return kJobs;
}

const char* tooltipStatus() {
    _snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
                "tooltip: %s hooks=%ld lines=%ld latches=%ld swaps=%ld refusedShape=%ld "
                "faults=%ld alloc=%ld",
                InterlockedCompareExchange(&g_tooltipOff, 0, 0) ? "OFF" : "armed",
                InterlockedCompareExchange(&g_hooksOn, 0, 0),
                InterlockedCompareExchange(&g_linesReady, 0, 0),
                InterlockedCompareExchange(&g_latches, 0, 0),
                InterlockedCompareExchange(&g_swaps, 0, 0),
                InterlockedCompareExchange(&g_refusedShape, 0, 0),
                InterlockedCompareExchange(&g_faults, 0, 0),
                InterlockedCompareExchange(&g_allocBalance, 0, 0));
    return g_status;
}

#ifdef UT_TOOLTIP_TEST_SEAM
// ---- the detour bodies, driven offline -------------------------------------------------
// Compiled ONLY into tools\test_tooltip.exe (build_test_tooltip.bat defines UT_TOOLTIP_TEST_SEAM);
// build.bat never defines it, so uniquetab.asi carries none of this. The harness hands in stub
// trampolines and two marker lines, then calls the REAL hk_* / latchBody / pendingLine.
void tooltipTestArm(void* baseText, void* equipText, void* artifactText, void* relicText,
                    void* toString, unsigned yesClass, unsigned noClass) {
    o_ItemText = reinterpret_cast<PfnItem_GetUIDisplayText>(baseText);
    o_EquipText = reinterpret_cast<PfnItem_GetUIDisplayText>(equipText);
    o_ArtifactText = reinterpret_cast<PfnItem_GetUIDisplayText>(artifactText);
    o_RelicText = reinterpret_cast<PfnItem_GetUIDisplayText>(relicText);
    o_ToString = reinterpret_cast<PfnGameTextLineToString>(toString);
    memset(g_lineYes, 0xA1, kLineSize);
    memset(g_lineNo, 0xB2, kLineSize);
    *(unsigned int*)g_lineYes = yesClass;
    *(unsigned int*)g_lineNo = noClass;
    InterlockedExchange(&g_linesReady, 1);
    InterlockedExchange(&g_tooltipOff, 0);
    g_offWhy[0] = 0;
    clearLatch();
    t_collected.state = 0;
}
void tooltipTestBaseText(void* item, const void* character, void* lines) {
    hk_ItemGetUIDisplayText(item, nullptr, character, lines);
}
void tooltipTestEquipText(void* item, const void* character, void* lines) {
    hk_EquipGetUIDisplayText(item, nullptr, character, lines);
}
void tooltipTestArtifactText(void* item, const void* character, void* lines) {
    hk_ArtifactGetUIDisplayText(item, nullptr, character, lines);
}
void tooltipTestRelicText(void* item, const void* character, void* lines) {
    hk_RelicGetUIDisplayText(item, nullptr, character, lines);
}
void tooltipTestToString(const void* lines, void* out) { hk_GameTextLineToString(lines, out); }
unsigned tooltipTestLatchCount() {
    return (t_latch.state == 1 || t_latch.state == 2) ? t_latch.count : 0;
}
const unsigned char* tooltipTestLine(bool yes) { return yes ? g_lineYes : g_lineNo; }
#endif  // UT_TOOLTIP_TEST_SEAM

}  // namespace ut
