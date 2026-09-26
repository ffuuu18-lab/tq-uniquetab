// ut_depositgate.h - the questions the deposit, the take and the world load ask, as PURE logic.
//
// TQ PORT of the Grim Dawn mod's ut_depositgate.h. GD's shape is kept:
// a header with no Windows, no engine, no file and no globals, so tools\test_store.cpp proves the
// real decision functions the shipped build calls, and in particular the property the whole rule
// rests on: **every verdict except "journal it" is reached without the journal being touched.**
//
// What TQ changed: GD's choke point was AddItemToReagents with
// two proved callers and a reagent-map node test; TQ has two proved GESTURES on the Transfer page
// (the drag-drop into PrimaryTransferActivate, whose disposal the mod performs itself exactly as
// the engine does, and the inventory quick-move into AddItemToTransfer(id), whose TQ.exe caller
// disposes of the original itself on a true), no reagent map, and adds the take and the save set.
#pragma once

#include <string.h>

namespace ut {

// ---- the deposit -----------------------------------------------------------------------------
// WHICH gesture reached the choke point: they need DIFFERENT proofs that the source is removed
// exactly once (HANDOFF.md section 5 names both call sites).
enum UtDepositCallerKind {
    kUtDepCallerUnknown = 0,
    // CursorHandlerItemMove::PrimaryTransferActivate (Game.dll 0x152590) while the view is ON.
    // The engine's own body, after its add, disposes of the cursor original with
    // ControllerCharacter::SendRemoveItemFromInventory(item id) on the controller of the handler's
    // player (0x1526D3..0x1526F0) - the mod makes exactly that call, then SetId(0).
    kUtDepCallerDrag = 1,
    // GameEngine::AddItemToTransfer(id, bool) from TQ.exe 0x107A6A (its only caller): on TRUE the
    // caller runs PlayerInventoryCtrl::RemoveItem (0x107AA2) and SendRemoveItemFromInventory
    // (0x107AB9) itself; on FALSE it keeps the item.
    kUtDepCallerQuick = 2
};

enum UtDepositVerdict {
    kUtDepTable = 0,           // journal it, flush it, then (drag) dispose / (quick) return true
    kUtDepRefuseViewOff,       // not the collection page: the detour is a pass-through anyway
    kUtDepRefuseCaller,        // a caller whose removal the mod has not proved
    kUtDepRefuseMultiplayer,   // a multiplayer session and mp_collect=0 (kUtMpMovesBarred)
    kUtDepRefuseTableOff,      // no save set, the journal is read-only, or has no path
    kUtDepRefuseBindings,      // a deposit binding is missing (the disposal, the capture)
    kUtDepRefuseNotItem,       // the cursor/inventory id is not an Item the mod could find
    kUtDepRefuseNotCatalogue,  // the record is not in the collection's catalogue
    kUtDepRefuseStack,         // a stack of two or more (GetNumberInStack >= 2, or stacked ids)
    kUtDepRefuseCapture,       // the identity could not be captured or does not fold to the record
    // a collection holds ONE copy of each record - a record that already
    // has a journal row (pending-in included) is refused. Rows that already exist twice stay valid
    // and takeable one by one; nothing is ever dropped from a journal.
    kUtDepRefuseHave,
    // the session state could not be read (kUtMpMovesUnknown) - refused whatever
    // mp_collect says. Its own verdict (and text) as in GD's gateDisarmReason, so a user with
    // mp_collect=1 is not told to edit the ini. Appended: the order of the others is unchanged; it
    // is decided at kUtDepRefuseMultiplayer's place.
    kUtDepRefuseMpUnknown
};

struct UtDepositFacts {
    int caller;          // UtDepositCallerKind
    bool viewOn;         // the collection page is shown (mode 1, the caravan open, a world up)
    bool mpKnown;        // GameInfo::GetIsMultiPlayer could be asked
    bool mp;             // ... and said true
    bool tableOwns;      // journalUsable(): a save set is open, writable
    bool bindings;       // the deposit bindings (and, for the drag, the disposal ones) resolved
    bool isItem;         // the id resolved to an object whose vftable slot is Item's replica getter
    bool inCatalogue;    // its record (folded) is one of the catalogue's records
    unsigned int stack;  // Item::GetNumberInStack through the object's vftable (0 = single)
    bool haveCap;        // GetItemReplicaInfo ran and every string was read back
    bool capMatches;     // the captured baseName folds to the object's own record
    unsigned int rowsNow;   // journal rows of its record right now (storeCount: pending-in
                            // included); > 0 = already in the collection. An aggregate that
                            // leaves it out means 0.
    bool mpCollect;      // the ini's mp_collect (GD's key). LAST, so an aggregate that leaves it
                         // out means 0, the strict setting.
};

// ---- THE multiplayer conjunct of every move -------------------------------
// GD's rule (the Grim Dawn mod's mpBarred, ut_reagent.cpp): mp_collect=1 = the collection works in
// a multiplayer session exactly as in single player, for the local player; mp_collect=0 = every
// move refused while the session is multiplayer. The disassembly found every gesture
// the same as the engine's own Transfer move - the drag-drop and the quick-move send the engine's
// own SendRemoveItemFromInventory (0x7C {char, id}), the left-click take reaches the peers through
// the cursor's own SendAddItemToInventory (0x7D, the full replica), the right-click take is the
// engine's GiveItemToCharacter - so no gesture is refused for its own sake.
// ONE deviation from GD: an UNKNOWN session state refuses every move whatever mp_collect says (GD let
// mp_collect=1 through; TQ keeps "refuse when unsure", and the view refuses an unknown state too).
enum UtMpMoves {
    kUtMpMovesOk = 0,
    kUtMpMovesUnknown,   // the session state could not be read
    kUtMpMovesBarred     // multiplayer and mp_collect=0
};
inline int utMpMoves(bool mpKnown, bool mp, bool mpCollect) {
    if (!mpKnown) return kUtMpMovesUnknown;
    if (mp && !mpCollect) return kUtMpMovesBarred;
    return kUtMpMovesOk;
}
inline bool utMpMovesAllowed(bool mpKnown, bool mp, bool mpCollect) {
    return utMpMoves(mpKnown, mp, mpCollect) == kUtMpMovesOk;
}

// Item::GetNumberInStack is `xor eax,eax / ret` on the base Item
// (Game.dll 0x8800): a unique answers 0. Only OneShot_Potion (+0x5EC) and ItemArtifactFormula
// (+0x544) override it, and CursorHandlerItemMove::CreateAndStackIds (0x1527D0) reads 0 as
// "nothing to stack". So 0 and 1 are a single item; only 2 or more is a stack. (required
// exactly 1 and refused every unique.) An unreadable count is 0xFFFFFFFF: a stack, refused.
inline bool utStackIsSingle(unsigned int n) { return n <= 1u; }
// The journal row's "stack": the format says 1 for a single copy.
inline unsigned int utJournalStack(unsigned int n) { return n < 1u ? 1u : n; }

// THE decision, in the order the log reads. Only kUtDepTable touches the journal.
inline int utDepositDecide(const UtDepositFacts& f) {
    if (!f.viewOn) return kUtDepRefuseViewOff;
    if (f.caller != kUtDepCallerDrag && f.caller != kUtDepCallerQuick) return kUtDepRefuseCaller;
    const int mpMoves = utMpMoves(f.mpKnown, f.mp, f.mpCollect);
    if (mpMoves == kUtMpMovesUnknown) return kUtDepRefuseMpUnknown;
    if (mpMoves != kUtMpMovesOk) return kUtDepRefuseMultiplayer;
    if (!f.tableOwns) return kUtDepRefuseTableOff;
    if (!f.bindings) return kUtDepRefuseBindings;
    if (!f.isItem) return kUtDepRefuseNotItem;
    if (!f.inCatalogue) return kUtDepRefuseNotCatalogue;
    if (f.rowsNow > 0u) return kUtDepRefuseHave;
    if (!utStackIsSingle(f.stack)) return kUtDepRefuseStack;
    if (!f.haveCap || !f.capMatches) return kUtDepRefuseCapture;
    return kUtDepTable;
}

inline bool utDepositIsRefusal(int verdict) { return verdict != kUtDepTable; }

inline const char* utDepositWhyText(int v) {
    switch (v) {
    case kUtDepTable: return "deposited";
    case kUtDepRefuseViewOff: return "the collection page is not shown";
    case kUtDepRefuseCaller: return "an unproved caller";
    case kUtDepRefuseMultiplayer:
        return "a multiplayer session is active and mp_collect=0";
    case kUtDepRefuseTableOff: return "the journal cannot take a row (no save set, read-only)";
    case kUtDepRefuseBindings: return "a deposit binding is missing";
    case kUtDepRefuseNotItem: return "the item could not be found";
    case kUtDepRefuseNotCatalogue: return "not a unique of the collection";
    case kUtDepRefuseStack: return "a stack of two or more";
    case kUtDepRefuseCapture: return "its identity could not be captured";
    case kUtDepRefuseHave: return "already in the collection";
    case kUtDepRefuseMpUnknown:
        return "the session mode is UNKNOWN (refuse when unsure, whatever mp_collect says)";
    default: return "?";
    }
}

// The capability slot (+0x30, IsTransferCapable) while the view is ON: the page accepts a drop
// only of an item a deposit would take. Everything else keeps the earlier answer (false).
inline int utDepositCapableVerdict(const UtDepositFacts& f) {
    UtDepositFacts g = f;
    g.haveCap = true;      // the capture runs at the drop itself; the slot asks per frame
    g.capMatches = true;
    return utDepositDecide(g);
}
inline bool utDepositCapable(const UtDepositFacts& f) {
    return utDepositCapableVerdict(f) == kUtDepTable;
}
// The +0x30 thunk's answer (hooks.cpp capTransferThunk): while the view is ON it is the deposit
// decision (TQ.exe 0xBFFFA calls PrimaryTransferActivate only on true); OFF it is the vanilla stub.
inline bool utCapSlotAnswer(bool viewOn, bool depositCapable, bool vanilla) {
    return viewOn ? depositCapable : vanilla;
}

// ---- the take ---------------------------------------------------------------------------------
// The page mouse handler's pick-up (b1 set, b2 clear) on a prototype of the mod sack. The id is
// handed out ONLY when the prototype stands for a journal row (it was built from that row's
// identity) and the journal can record the take; everything else keeps the earlier refusal.
struct UtTakeFacts {
    bool viewOn;
    bool mpKnown;
    bool mp;
    bool tableOwns;     // journalUsable()
    bool isPrototype;   // the id is a live prototype of the mod sack
    bool fromRow;       // it was built from a journal row (seq != 0)
    unsigned int rows;  // journalRows(its record) right now
    bool rowIsNewest;   // its row is still the record's newest (the page is not stale)
    bool mpCollect;     // the ini's mp_collect; LAST (an aggregate without it = 0, strict)
};

enum UtTakeVerdict {
    kUtTakeOk = 0,
    kUtTakeRefuseViewOff,
    kUtTakeRefuseMultiplayer,
    kUtTakeRefuseTableOff,
    kUtTakeRefuseNotPrototype,
    kUtTakeRefuseBare,      // a record with no row: a dim prototype, not takeable
    kUtTakeRefuseStale
};

inline int utTakeDecide(const UtTakeFacts& f) {
    if (!f.viewOn) return kUtTakeRefuseViewOff;
    if (!utMpMovesAllowed(f.mpKnown, f.mp, f.mpCollect)) return kUtTakeRefuseMultiplayer;
    if (!f.tableOwns) return kUtTakeRefuseTableOff;
    if (!f.isPrototype) return kUtTakeRefuseNotPrototype;
    if (!f.fromRow || f.rows == 0u) return kUtTakeRefuseBare;
    if (!f.rowIsNewest) return kUtTakeRefuseStale;
    return kUtTakeOk;
}

// ---- the save set -----------------------------------------------------------------------------
// One journal per set, chosen at world load and never mid-world:
//   GameInfo::GetModName non-empty (a Custom Quest, or a bounce session: the Transfer save path
//   is built with the mod name)              -> "tq-uniq-items-<mod>"  (the name sanitised)
//   else Player::IsInMainQuest true          -> "tq-uniq-items"
//   else Player::IsInMainQuest false         -> "tq-uniq-items-custom"
//   anything unreadable, or a mod name that sanitises to nothing -> "" (UNKNOWN: moves refused)
// The sanitised mod name: lower case a-z 0-9, every other run of characters becomes one '_',
// no leading/trailing '_', at most 48 characters; "custom" is reserved and becomes "mod_custom".
inline bool utSaveSetLeaf(bool modKnown, const char* modName, bool questKnown, bool inMainQuest,
                          char* out, unsigned cap) {
    if (!out || cap < 80) return false;
    out[0] = 0;
    if (!modKnown) return false;
    if (modName && modName[0]) {
        char s[49];
        unsigned n = 0;
        bool sep = false;
        bool lost = false;   // a byte dropped / replaced, or the name cut
        unsigned int h = 2166136261u;   // FNV-1a 32 of the name, ASCII letters folded (the
        const char* p = modName;        // folder names are case-insensitive: "MyMod" = "mymod")
        for (; *p; ++p) {
            char c = *p;
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            h = (h ^ (unsigned char)c) * 16777619u;
            if (n >= 48) {
                lost = true;
                continue;
            }
            const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
            if (ok) {
                if (sep && n > 0 && n < 47) s[n++] = '_';
                s[n++] = c;
                sep = false;
            } else {
                sep = true;
                lost = true;
            }
        }
        s[n] = 0;
        if (n == 0) return false;
        const char* prefix = strcmp(s, "custom") == 0 ? "tq-uniq-items-mod_" : "tq-uniq-items-";
        unsigned k = 0;
        for (const char* q = prefix; *q; ++q) out[k++] = *q;
        for (unsigned i = 0; i < n; ++i) out[k++] = s[i];
        if (lost) {   // "My Mod", "my-mod" and "my_mod" must not share one journal
            static const char kHex[] = "0123456789abcdef";
            out[k++] = '-';
            for (int sh = 28; sh >= 0; sh -= 4) out[k++] = kHex[(h >> sh) & 0xFu];
        }
        out[k] = 0;
        return true;
    }
    if (!questKnown) return false;
    const char* leaf = inMainQuest ? "tq-uniq-items" : "tq-uniq-items-custom";
    unsigned k = 0;
    for (const char* p = leaf; *p; ++p) out[k++] = *p;
    out[k] = 0;
    return true;
}

}  // namespace ut
