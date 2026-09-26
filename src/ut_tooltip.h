// ut_tooltip.h - the item rollover: the "In your collection" line (GD ut_tooltip.h, ported).
//
// GD's file had two halves; Titan Quest keeps ONE:
//
//  1. THE COMPARE POPUP - GD's one byte on its own `UIReagentItem` boxes. NOT PORTED: the TQ
//     collection page shows REAL engine items in a real InventorySack on the Transfer page, whose
//     rollover is the engine's own; there is no mod-owned item box to flag.
//
//  2. THE "ALREADY COLLECTED" LINE - FIVE exported Game.dll detours, all-or-nothing.
//     `Item::GetUIDisplayText` (+ the `ItemEquipment` override, which calls the base at Game.dll
//     0x1C00D5 and then APPENDS - so the base's latch would otherwise be of a partial vector - and
//     the `ItemArtifact` / `ItemRelic` overrides, which do not call the base at all) LATCHES
//     {which state, when} after the original has filled the line vector, and
//     `GameTextLineToString` SWAPS in a borrowed array of N+1 `GameTextLine` records - the
//     engine's N, byte for byte, plus one mod-owned static line - for the duration of that one
//     call. The mod never pushes into the engine's vector (it may be at capacity, and the engine
//     destroys what it holds) and never draws a pixel: the engine lays the extra line out exactly
//     as it lays out its own.
//     Every weapon, armour and jewellery class of TQ (Weapon*, WeaponArmor*, Armor*,
//     ArmorJewelry*) has ItemEquipment's function in its vftable's +0x14C slot, so the whole
//     catalogue goes through the ItemEquipment detour; ItemCharm shares ItemRelic's.
//
// Behind the ini key `tooltip_mark` (default 1) and one session latch: any failure to resolve an
// export or install a detour turns the whole mark OFF for the session with one capitalised line.
#pragma once

#include <windows.h>

#include <stddef.h>

namespace ut {

// Worker thread, once, before hooksInstall(). Resolves the SIX Game.dll exports - the four
// `GetUIDisplayText` overrides that get a detour (Item, ItemEquipment, ItemArtifact, ItemRelic),
// `GameTextLineToString` (the fifth detour) and the `GameTextLine` constructor, which is CALLED
// and never hooked - all OPTIONAL (a miss can never fail start-up, it only turns the mark off),
// checks none of them is a folded stub, and builds the two static GameTextLine records. Safe to
// call twice.
bool tooltipInit(HMODULE selfModule);

// Worker thread, from hooksInstall(). Installs the tooltip detours ALL-OR-NOTHING: every hook is
// created first, and only if every create succeeded are they enabled; otherwise the created ones
// are removed again and the feature is off for the session. Returns how many were installed and
// writes how many were attempted into `total`. TQ: COUNTED in the mod's `detours installed: N of
// N` line, so a missing tooltip export shows there as a WARN, never an ERROR.
int tooltipInstall(int* total);

// One line for the log / the menu test. Never touches engine memory.
const char* tooltipStatus();

// ---- the swap, exposed so the offline harness drives the SAME code the game drives -------------
// The borrowed vector handed to GameTextLineToString's trampoline: VS2012 std::vector's
// {first, last, end}, over an array this mod owns for the duration of one call.
struct UtTooltipSwap {
    const unsigned char* begin;
    const unsigned char* end;
    const unsigned char* cap;
};

// Builds an array of N+1 0x20-byte GameTextLine records - the engine's N copied byte for byte,
// then `extraLine` - and fills `out` with a triple over it. `*ownedOut` is the array, to be given
// back to tooltipSwapFree. Returns false, having allocated nothing, for any vector whose shape is
// not a plausible tooltip (unreadable, not a multiple of 0x20, empty, or over 512 lines).
bool tooltipSwapBuild(const void* vec, const unsigned char* extraLine, unsigned char** ownedOut,
                      UtTooltipSwap* out);
void tooltipSwapFree(unsigned char* owned);

// Borrowed arrays alive right now. Zero at rest - the harness asserts exactly that.
long tooltipSwapAllocBalance();

// ---- the pure pieces, exposed for the offline harness (tools/test_tooltip.cpp) ------------------
// The latch's match against the vector GameTextLineToString is handed: 1 = the exact
// {count, first class, last class} fingerprint, 2 = the prefix one (more lines, the same first
// class, the latched last class at index count-1), 0 = no match. `atPrefixEnd` is the class at
// index latchCount-1 of the vector, valid only when `prefixRead` (the game reads it under SEH).
int tooltipLatchMatch(unsigned latchCount, unsigned latchFirst, unsigned latchLast,
                      unsigned count, unsigned firstCls, unsigned lastCls, bool prefixRead,
                      unsigned atPrefixEnd);
// The memo in front of the catalogue and the journal: the state of `record` (1 COLLECTED, 2 NOT
// COLLECTED, 3 NOT A CATALOGUE RECORD - no line) and
// whether it was answered from the memo (`*fromMemo`) - fresh for kMemoMaxAgeMs per record.
int tooltipCollectedState(const char* record, bool* fromMemo);
// The texts in use (the ini's, or the built-in wording), latched by tooltipInit.
const char* tooltipTextYes();
const char* tooltipTextNo();
// The text class an ini value resolves to (the registered table, else the default).
unsigned tooltipPickClass(int fromIni, unsigned fallback, const char* which);
// The read-back tooltipInit applies to a line the engine's ctor has just built: the class at
// +0x00, the VS2012 wstring at +0x04 (size +0x14, capacity +0x18), the bool at +0x1C false.
bool tooltipVerifyLine(const unsigned char* line, unsigned cls, size_t wantLen);
// The latch as the harness sees it: true once the session's line is OFF, and why.
bool tooltipIsOff();

// The property search (ut_search.cpp) builds each record's text through the item's own
// GetUIDisplayText, which runs through the detours above. While this thread-local flag is set the
// latch body returns at once: a capture neither clears a real rollover's pending latch, nor takes
// the journal lock, nor leaves a latch of its own. Game thread.
void tooltipSearchCapture(bool on);
// True when `fn` is one of the four resolved GetUIDisplayText overrides (Item, ItemEquipment,
// ItemArtifact, ItemRelic): the search calls an item's +0x14C slot only when it holds one of them.
bool tooltipTextBuilderKnown(const void* fn);
// Which of the four `fn` is: 0 Item, 1 ItemEquipment, 2 ItemArtifact, 3 ItemRelic; -1 none.
int tooltipTextBuilderIndex(const void* fn);

#ifdef UT_TOOLTIP_TEST_SEAM
//, the harness ONLY (build_test_tooltip.bat defines it; the mod never does): stub
// trampolines and marker lines in, then the REAL detour bodies driven one by one.
void tooltipTestArm(void* baseText, void* equipText, void* artifactText, void* relicText,
                    void* toString, unsigned yesClass, unsigned noClass);
void tooltipTestBaseText(void* item, const void* character, void* lines);      // hk_ItemGetUIDisplayText
void tooltipTestEquipText(void* item, const void* character, void* lines);     // hk_EquipGetUIDisplayText
void tooltipTestArtifactText(void* item, const void* character, void* lines);  // hk_ArtifactGetUIDisplayText
void tooltipTestRelicText(void* item, const void* character, void* lines);     // hk_RelicGetUIDisplayText
void tooltipTestToString(const void* lines, void* out);                        // hk_GameTextLineToString
unsigned tooltipTestLatchCount();   // the pending latch's line count, 0 = none
const unsigned char* tooltipTestLine(bool yes);
#endif

}  // namespace ut
