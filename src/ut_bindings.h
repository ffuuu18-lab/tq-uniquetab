// ut_bindings.h - EVERY fact this mod knows about the game's memory, in one place, with the way
// it is obtained and the way it is confirmed.
//
// WHY THIS FILE EXISTS. Nothing in the mod is allowed to assume a game version. What is asked
// instead is a CAPABILITY question, one binding at a time: can this address still be found, and
// does what was found still look like the thing it is supposed to be? A binding that answers yes
// works on any build that kept the shape; a binding that answers no turns the WHOLE mod off before
// a single hook is installed, because a half-bound mod is the one thing that could damage a save.
//
// THE FIVE CLASSES, from safest to most fragile:
//
//   EXPORT      GetProcAddress by decorated name in Game.dll / Engine.dll (and MSVCR110.dll for
//               the game's operator delete). Survives any patch that keeps the name. tq_exports.h
//               holds ZERO addresses; there is nothing in it to go stale.
//   SIGNATURE   a byte pattern scanned in a module's .text at run time. Carries no address of its
//               own, but it does carry the compiler's instruction selection, so it survives a data
//               patch and does not survive a recompile of that function. Every pattern here is
//               proved to match EXACTLY ONCE in the AE 2.10 image by tools/test_bindings.cpp, and
//               the run-time scan re-counts the matches and refuses anything but the expected count.
//   DECODED     an offset read out of an EXPORTED function's own instruction bytes (`lea eax,
//               [ecx+d32]; ret` and friends), or a vftable slot found BY ADDRESS (the slot that
//               holds an exported function). The name anchors it.
//   STRUCTURAL  a layout the COMPILER guarantees, not the game: the VS2012 x86 std::string.
//   LITERAL     a number compiled in. NONE.
//
// TQ.exe carries no DRM stub (sections .text/.rdata/.data/.rsrc/.reloc, the same on disk as in
// memory), so unlike GD's exe signatures the two here are EARLY: scanned by the worker before any
// hook, from the loaded image.
//
// ---------------------------------------------------------------------------------------------
// THE INVENTORY. One row per binding: what it is / how it is obtained / how it is confirmed.
// The AE 2.10 column is EVIDENCE, never a gate - it is what the offline test asserts, and the mod
// itself never compares against it.
// ---------------------------------------------------------------------------------------------
//
// == EXPORT (counted, not listed: the list is tq_exports.h, and resolveExports() logs one line per
//            symbol at trace) ==
//     obtained    GetProcAddress(module, "<decorated name>")
//     confirmed   a code address inside its module's executable section whose first byte is not
//                 padding (CC / 00); a data export inside its module's image and readable. A
//                 REQUIRED symbol that fails turns the mod off (tq_runtime.cpp, folded into the gate).
//
// == SIGNATURE (TQ.exe .text, EARLY; ADVISORY for the mod, REQUIRED for the view) ======
//   exe.transferPageAccessor           AE 2.10 rva 0xC2F50   (HANDOFF.md section 5)
//     obtained    50 bytes: the prologue, the IAT call to GameEngine::GetPlayerTransfer, the
//                 `lea edi,[ebx+0x88]` of the UIStashInventory sub-object and the `mov [edi+0x60],
//                 ecx` that caches the sack; the three absolute operands (relocated) wildcarded
//     confirmed   exactly one match in TQ.exe's .text
//   exe.stashStreamOut                 AE 2.10 rva 0xBF990   (UIStashInventory::StreamOut)
//     obtained    64 bytes: the SEH prologue, `sub esp,0x48`, `mov edi,[esp+0x6c]` and the
//                 `cmp dword [edi+0x14],0x10` of its std::string path argument; the three
//                 absolute operands (relocated) wildcarded
//     confirmed   exactly one match in TQ.exe's .text
//
// == DECODED (out of an exported function's own bytes, at export-resolution time, EARLY) ==
//   gameEngine.stashSack      0x2928  GetPlayerStash        `8D 81 <d32> C3`   critical
//   gameEngine.transferSack   0x2960  GetPlayerTransfer     same shape         critical
//   gameEngine.relicVaultSack 0x2998  GetPlayerRelicVault   same shape         critical
//     confirmed   each const twin decodes to the same field; 4-aligned, non-zero, below 0x10000;
//                 the three are equally spaced and ascending (three InventorySack members in a row)
//   gameEngine.caravanMode    0x2924  GetCaravanMode        `8B 81 <d32> C3`   critical
//     confirmed   4-aligned, non-zero and BELOW the stash sack (the int in front of the three)
//   cursor.itemId             0x24    CursorHandlerItemMove::GetId  `8B 41 <d8> C3`   critical
//     confirmed   4-aligned, 4..0x7C (a positive disp8)
//   item.replicaSlot  +0x1B0 / item.classificationSlot +0x1C4 / item.typeSlot +0x194 /
//   item.stackSlot    +0x1B8    the ONE slot of ??_7Item holding the exported function  ADVISORY
//     confirmed   exactly one slot holds it (not a folded body), 4-aligned. Nothing reads them in
//                 so a failure is one WARN
//   cursor.capabilitySlots  +0x18..+0x34  every slot of ??_7CursorHandlerItemMove from +0x18 to
//                 +0x34 holds the ONE shared `mov al,1; ret` body that IsStashCapable /
//                 IsTransferCapable / IsRelicVaultCapable resolve to (the hover gate)  ADVISORY
//     confirmed   the three exports are one address, its bytes are B0 01 C3, all eight slots hold it
//
// == STRUCTURAL (the compiler's layout, not the game's - documented, never scanned) ==
//   msvc.string   VS2012 x86 std::string, 0x18 bytes: 16-byte buffer/pointer union, size +0x10,
//                 capacity +0x14 (capacity < 16 => in place). Engine-filled heaps are freed through
//                 MSVCR110 operator delete, never through this DLL's own CRT.
//
// == LITERAL - none ==
//
// ---------------------------------------------------------------------------------------------
#pragma once

#include <windows.h>

#include <stddef.h>
#include <string.h>

namespace ut {

// ---- the pure scanner ------------------------------------------------------------------------
// Wildcards are a parallel mask array: mask[i] == 0 means "any byte here". Kept free of Windows
// and of the mod's own state on purpose, so tools/test_bindings.cpp runs EXACTLY this code over the
// TQ.exe file offline.  Returns how many matches were found, capping the stored hits at
// `maxHits` but counting up to `maxHits + 1` so "more than expected" is always reportable.
struct UtBindPattern {
    const char* name;
    const unsigned char* bytes;
    const unsigned char* mask;  // 1 = must match, 0 = wildcard
    size_t len;
    int expectHits;             // how many matches the pattern MUST have
    size_t rva210;              // the AE 2.10 rva of the first hit - evidence for the test only
};

inline int utBindScan(const unsigned char* lo, const unsigned char* hi, const UtBindPattern& p,
                      const unsigned char** hits, int maxHits) {
    if (!lo || !hi || hi <= lo || p.len == 0 || (size_t)(hi - lo) < p.len) return 0;
    const unsigned char first = p.bytes[0];
    const bool firstFixed = p.mask[0] != 0;
    int count = 0;
    for (const unsigned char* q = lo; q + p.len <= hi; ++q) {
        if (firstFixed && *q != first) continue;
        size_t j = 0;
        for (; j < p.len; ++j) {
            if (p.mask[j] && q[j] != p.bytes[j]) break;
        }
        if (j != p.len) continue;
        if (count < maxHits) hits[count] = q;
        if (++count > maxHits) break;
    }
    return count;
}

// The two counted TQ.exe signatures. Defined in ut_bindings.cpp so the test and the mod share one
// copy of the bytes.
extern const UtBindPattern kUtSigTransferPage;
extern const UtBindPattern kUtSigStreamOut;
// the two pick-up paths refused on the mod sack. Each pattern runs from the function's entry
// to the END of its `FF 15` call of InventorySack::GetItemUnderPoint, so the return address of
// that call is match + len.
extern const UtBindPattern kUtSigRightClick;
extern const UtBindPattern kUtSigHeldPickup;
extern const UtBindPattern kUtSigLeftClick;
extern const UtBindPattern kUtSigLeftHandler;   // the mouse handler entry (the hover frame)
extern const UtBindPattern kUtSigPageDraw;      // the Transfer page draw (the slot plates)
extern const UtBindPattern kUtSigItemBackground;   // an item widget's background (the rect route)
// the item widget's fields the rect route reads and writes (TQ.exe 0x10A9B0 / 0x10ADE0 /
// 0x16E3B3; test_bindings decodes each from TQ.exe): the rect x, y, w, h (floats), the item's
// object id (the item loop's `FF 70 30`), the draw scale (float).
const unsigned kUtWidgetRect = 0x10;
const unsigned kUtWidgetItemId = 0x30;
const unsigned kUtWidgetScale = 0x78;
// the icon texture the item widget's icon draw (0x10ADE0) passes to RenderRect: `mov ecx,
// [ebx+0x3C]` (GetWidth / GetHeight) and `push dword ptr [ebx+0x3C]` (test_bindings runRectRouteHalf).
const unsigned kUtWidgetIcon = 0x3C;

// every `8B 3D <d32>` (mov edi,[IAT slot]) followed within 0x10 bytes by
// `FF D7 84 C0 0F 84` (call edi / test al,al / je rel32). `load` and `ret` are offsets from lo
// (`ret` = just past the FF D7), `slot` the d32. Returns how many exist (at most `cap` are stored).
struct UtQuickSite {
    unsigned long load;
    unsigned slot;
    unsigned long ret;
};
inline int utQuickMoveSites(const unsigned char* lo, const unsigned char* hi, UtQuickSite* out,
                            int cap) {
    int n = 0;
    for (const unsigned char* p = lo; p + 6 <= hi; ++p) {
        if (p[0] != 0x8B || p[1] != 0x3D) continue;
        for (const unsigned char* q = p + 6; q + 6 <= hi && q < p + 6 + 0x10; ++q) {
            if (q[0] == 0xFF && q[1] == 0xD7 && q[2] == 0x84 && q[3] == 0xC0 && q[4] == 0x0F &&
                q[5] == 0x84) {
                if (n < cap) {
                    out[n].load = (unsigned long)(p - lo);
                    out[n].slot = (unsigned)p[2] | ((unsigned)p[3] << 8) | ((unsigned)p[4] << 16) |
                                  ((unsigned)p[5] << 24);
                    out[n].ret = (unsigned long)(q + 2 - lo);
                }
                ++n;
                break;
            }
        }
    }
    return n;
}

// ---- the pure x86 decoders -------------------------------------------------------------------
// Each takes the first bytes of an EXPORTED function (at least 8 readable bytes) and returns the
// field offset its one instruction reads, or 0 when the bytes are not that shape. No Windows, no
// SEH, no mod state: the mod runs them over the loaded module (after a guarded copy) and the
// offline test over the DLL file.
inline unsigned utLoadU32(const unsigned char* p) {
    unsigned v;
    memcpy(&v, p, 4);
    return v;
}
// `8D 81 <d32> C3`  lea eax,[ecx+d32]; ret           - GameEngine::GetPlayer{Stash,Transfer,RelicVault}
inline unsigned utDecodeLeaEcx32(const unsigned char* p) {
    return (p && p[0] == 0x8D && p[1] == 0x81 && p[6] == 0xC3) ? utLoadU32(p + 2) : 0;
}
// `8B 81 <d32> C3`  mov eax,[ecx+d32]; ret           - GameEngine::GetCaravanMode
inline unsigned utDecodeMovEcx32(const unsigned char* p) {
    return (p && p[0] == 0x8B && p[1] == 0x81 && p[6] == 0xC3) ? utLoadU32(p + 2) : 0;
}
// `8A 81 <d32> C3`  mov al,[ecx+d32]; ret            - a bool getter
inline unsigned utDecodeMovAlEcx32(const unsigned char* p) {
    return (p && p[0] == 0x8A && p[1] == 0x81 && p[6] == 0xC3) ? utLoadU32(p + 2) : 0;
}
// `0F B6 81 <d32> C3`  movzx eax,byte [ecx+d32]; ret - a bool getter, the other selection
inline unsigned utDecodeMovzxEcx32(const unsigned char* p) {
    return (p && p[0] == 0x0F && p[1] == 0xB6 && p[2] == 0x81 && p[7] == 0xC3) ? utLoadU32(p + 3)
                                                                              : 0;
}
// `8B 41 <d8> C3`  mov eax,[ecx+d8]; ret             - CursorHandlerItemMove::GetId
inline unsigned utDecodeMovEcx8(const unsigned char* p) {
    return (p && p[0] == 0x8B && p[1] == 0x41 && p[3] == 0xC3 && p[2] < 0x80) ? p[2] : 0;
}

//, inside the matched Transfer page accessor:
// `8D BB <d32>`  lea edi,[ebx+d32]  - the sub-window's UIStashInventory sub-object (+0x88)
inline unsigned utDecodeLeaEdiEbx32(const unsigned char* p) {
    return (p && p[0] == 0x8D && p[1] == 0xBB) ? utLoadU32(p + 2) : 0;
}
// `89 4F <d8>`  mov [edi+d8],ecx     - the cached sack pointer inside it (+0x60)
inline unsigned utDecodeMovEdi8Ecx(const unsigned char* p) {
    return (p && p[0] == 0x89 && p[1] == 0x4F && p[2] < 0x80) ? p[2] : 0;
}

// The plausibility test every decoded field offset passes: non-zero, 4-aligned, below `limit`.
inline bool utFieldPlausible(unsigned off, unsigned limit) {
    return off != 0 && (off & 3u) == 0 && off < limit;
}
// The three store sacks: each plausible, ascending, equally spaced (three InventorySack members in
// a row), and the caravan mode int sits below the first of them.
inline bool utSackTrioPlausible(unsigned stash, unsigned transfer, unsigned relic, unsigned mode) {
    if (!utFieldPlausible(stash, 0x10000) || !utFieldPlausible(transfer, 0x10000) ||
        !utFieldPlausible(relic, 0x10000))
        return false;
    if (!(stash < transfer && transfer < relic)) return false;
    if (transfer - stash != relic - transfer) return false;
    return utFieldPlausible(mode, 0x10000) && mode < stash;
}

// A vftable is a run of function pointers. The byte offset of the ONE slot among the first `n`
// holding `fn`, -1 when none does, -2 when several do (a folded body - ambiguous, refused).
inline int utVtableSlotOf(const unsigned* slots, int n, unsigned fn) {
    int found = -1;
    for (int i = 0; i < n; ++i) {
        if (slots[i] != fn) continue;
        if (found >= 0) return -2;
        found = 4 * i;
    }
    return found;
}

// ---- the registry ----------------------------------------------------------------------------
enum UtBindClass {
    UT_BIND_EXPORT = 0,
    UT_BIND_SIGNATURE = 1,
    UT_BIND_DECODED = 2,
    UT_BIND_STRUCTURAL = 3,
    UT_BIND_LITERAL = 4,
    UT_BIND_CLASS_COUNT = 5,
};

// EARLY bindings must all be resolved and confirmed before ANY hook is installed. LATE ones are
// resolved from the game thread; TQ has none (its exe is plain), the phase is kept so a
// later binding that does need the game thread can say so.
enum UtBindPhase { UT_BIND_EARLY = 0, UT_BIND_LATE = 1 };

// CRITICAL rows have a consumer: some decision in the mod reads the value, so a failure turns the
// mod off. ADVISORY rows are decoded, confirmed and reported like the others, but nothing reads
// them, so a failure is one WARN naming the row and the gate still passes. Orthogonal to the
// class and to the phase; the offline test asserts the flag row by row.
enum UtBindGate { UT_BIND_CRITICAL = 0, UT_BIND_ADVISORY = 1 };

// Register one binding's outcome. `value` is the offset/slot/rva that was obtained (0 when the
// binding is not a number), `ok` is whether it RESOLVED AND CONFIRMED, and `why` is what the
// confirmation expected - it is what the ERROR line prints when ok is false. Names are compared
// by pointer-free strcmp against the compiled-in table, so a typo is reported, never silent.
void bindingsNote(const char* name, unsigned long long value, bool ok, const char* why);

// EARLY gate. Logs the module identity (size + PE timestamp of TQ.exe, Game.dll and Engine.dll -
// for the record, NEVER as a gate), one compact INFO line with the counts per class, and the full
// table at DEBUG. Returns false when any EARLY binding failed, and the caller must then install
// nothing at all.
bool bindingsGate(int resolvedExports, int missingExports);

// The LATE half: called from the game thread whenever a late binding reports. Emits the
// "all late bindings confirmed" INFO line once, or one ERROR naming the first failure.
void bindingsLateReport();

// For the status/heartbeat line: "69 by export, 2 by signature, 10 decoded (5 advisory),
// 1 structural, 0 literal - all confirmed".
const char* bindingsSummary();

// The CRITICAL row the last bindingsGate refused on ("" when it passed).
const char* bindingsGateFailure();

// The registry itself, read only, so tools/test_bindings.cpp can assert the
// CLASSIFICATION and not just the outcomes. Returns false when `i` is out of range; any out
// pointer may be null.
int bindingsRowCount();
bool bindingsRowAt(int i, const char** name, int* cls, int* phase, int* gate = nullptr);

// did the row `name` report RESOLVED AND CONFIRMED? False for an unknown name or a row that
// never reported. The view's own gate (ut_view.cpp, G8) asks this row by row.
bool bindingsRowOk(const char* name);


// the inventory draw's tint operands (TQ.exe 2.10) that ut_panel's
// readInventoryTint reads, decoded by tools\test_bindings.cpp (runTintHalf): the Transfer page's
// UIStashInventory at page + 0x88 (the page draw's `lea ecx,[esi+0x88]`); in its per-item draw
// 0x16E3E3 `mov eax,[esi+0x134]` (the inset k), 0x16E403 `lea eax,[esi+0x124]` (the "requirements
// not met" colour), 0x16E419 `lea eax,[esi+0x114]` (the shade); 0x10A86C `push 0x18` (the option
// the item-colour draw asks GameOptions for).
constexpr unsigned kUtInvOfPage = 0x88;
constexpr unsigned kUtInvShade = 0x114, kUtInvFails = 0x124, kUtInvInset = 0x134;
constexpr unsigned kUtOptItemTint = 0x18;
constexpr unsigned long kUtRvaInvInset = 0x16E3E3, kUtRvaInvFails = 0x16E403, kUtRvaInvShade = 0x16E419;
constexpr unsigned long kUtRvaOptItemTint = 0x10A86C;

}  // namespace ut
