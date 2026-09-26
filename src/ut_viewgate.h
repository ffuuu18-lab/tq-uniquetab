// ut_viewgate.h - the collection view's ON/OFF state machine, as pure logic - and nothing else.
//
// THE ONE-WAY DOOR THIS FILE GUARDS (GD HANDOFF 1, in TQ terms): the engine must never stream the
// mod sack into a save file, and no prototype may ever reach a real sack. The Transfer page caches
// its sack pointer in `UIStashInventory+0x60` and CLOSING THE CARAVAN SAVES FROM THAT MEMBER with
// no page refresh in between (measured). So every way out of the view must RE-POINT the member at the
// real sack and destroy the prototypes, and it must do so through ONE function (ut_view.cpp's
// `viewForceOff`). This header decides WHEN; the caller does the re-point.
//
// The shape is GD's `ut_paintgate.h` / `ut_rowmath.h`: no Windows, no engine, no file, no globals,
// so `tools\test_viewgate.cpp` enumerates every event order (and "ON then close with no frame")
// against a simulated page and asserts the member is the real sack at every save.
//
// Rules:
//   * ON needs EVERYTHING at once: the view's bindings complete and confirmed, a world up, a
//     KNOWN session state (single player or multiplayer: - the view works in a hosted or joined
//     game; only an UNKNOWN state refuses, "refuse when unsure"), the caravan open on the Transfer
//     tab (mode 1), and no fault this session. Any one missing -> the request is REFUSED (the step
//     says why) and the view stays OFF.
//   * every CHANGE of the session state (single <-> multiplayer <-> unknown) turns the view
//     OFF once (G6); the user can turn it on again in the new state (unless it is unknown).
//   * Every event that makes one of those false while ON turns the view OFF (`turnedOff`), and
//     the caller MUST re-point the member and destroy the prototypes before it returns.
//   * A fault or a StreamOut that saw the mod sack LATCHES: the view stays unavailable for the
//     rest of the process (refuse when unsure).
//   * `utViewSubstitute` is the getter's G1 predicate: the mod sack is returned ONLY to the Transfer
//     page's own accessor, ONLY while ON, on mode 1, in a world, the session state known (in
//     multiplayer too - the substitution is the local page's pointer and nothing leaves the
//     machine).
//   * `utViewGetterPicksMod` then picks WHICH sack: the mod sack when it holds the
//     page's prototypes or the page is empty BY DESIGN (the OWN filter on a group with nothing owned);
//     the real sack when the mod sack is missing or empty for any other reason.
#pragma once

namespace ut {

enum UtViewEvent {
    kUtViewEvToggle = 0,          // the button / the hotkey (ON when OFF, OFF when ON)
    kUtViewEvModeChanged,         // SetCaravanMode(arg): the tab
    kUtViewEvOpen,                // NpcCaravan::OnPlayerInteract (the LOAD path runs next)
    kUtViewEvGoodbye,             // GameEngine::CaravanGoodbye PRE: the three saves follow
    kUtViewEvWorldUp,
    kUtViewEvWorldDown,
    kUtViewEvMpChanged,           // arg = 0 single player, 1 multiplayer, 2 unknown
    kUtViewEvFault,               // a WARN/ERROR raised by the view code, a caught exception
    kUtViewEvBindingsIncomplete,  // a view binding turned out unusable at run time
    kUtViewEvStreamOutModSack,    // G3 fired: StreamOut found the mod sack in the member
    kUtViewEvCount
};

enum UtViewWhy {
    kUtViewWhyOk = 0,
    // why the view went OFF
    kUtViewWhyToggleOff,
    kUtViewWhyTabChange,
    kUtViewWhyOpen,
    kUtViewWhyGoodbye,
    kUtViewWhyWorldDown,
    kUtViewWhyMp,
    kUtViewWhyFault,
    kUtViewWhyBindings,
    kUtViewWhyStreamOut,
    // why an ON request was refused
    kUtViewWhyRefusedBindings,
    kUtViewWhyRefusedFaulted,
    kUtViewWhyRefusedNoWorld,
    kUtViewWhyRefusedMp,
    kUtViewWhyRefusedClosed,
    kUtViewWhyRefusedNotTransfer,
    kUtViewWhyCount
};

inline const char* utViewWhyText(int why) {
    static const char* const k[kUtViewWhyCount] = {
        "ok",
        "toggled off",
        "the caravan tab changed",
        "the caravan opened (its load path must see the real sack)",
        "the caravan is closing (CaravanGoodbye: the saves follow)",
        "the world went down",
        "the multiplayer state changed",
        "a fault in the view code",
        "a view binding is unusable",
        "StreamOut found the mod sack in the page (G3)",
        "the view's bindings are not all resolved and confirmed",
        "a fault earlier in this session",
        "no world is up",
        "the multiplayer state is unknown (refuse when unsure)",
        "the caravan is not open",
        "the caravan is not on the Transfer tab",
    };
    return (why >= 0 && why < kUtViewWhyCount) ? k[why] : "?";
}

struct UtViewState {
    bool on = false;
    bool open = false;      // the caravan window
    int mode = -1;          // the last SetCaravanMode argument (1 = Transfer)
    bool world = false;
    bool mp = false;          // the last KNOWN session state: multiplayer (hosted or joined)
    bool mpUnknown = false;   // the last read could not tell (refuse when unsure)
    bool faulted = false;   // latched for the process
    bool bindingsOk = false;
};

struct UtViewStep {
    bool turnedOn;    // the caller builds the page (the substitution starts at the next accessor)
    bool turnedOff;   // the caller MUST re-point the member and destroy the prototypes NOW
    int why;          // UtViewWhy: why it went off, or why an ON request was refused
};

// Why an ON request would be refused right now, or kUtViewWhyOk. The order is the log's order.
inline int utViewRefusal(const UtViewState& s) {
    if (!s.bindingsOk) return kUtViewWhyRefusedBindings;
    if (s.faulted) return kUtViewWhyRefusedFaulted;
    if (!s.world) return kUtViewWhyRefusedNoWorld;
    if (s.mpUnknown) return kUtViewWhyRefusedMp;   // multiplayer itself is no refusal
    if (!s.open) return kUtViewWhyRefusedClosed;
    if (s.mode != 1) return kUtViewWhyRefusedNotTransfer;
    return kUtViewWhyOk;
}

// G1: may the getter hand the mod sack to THIS call?
inline bool utViewSubstitute(bool callerInAccessor, bool on, int mode, bool world, bool mpUnknown) {
    return callerInAccessor && on && mode == 1 && world && !mpUnknown;
}

// the session's shape for the heartbeat and the log: "off" (single player), "host"
// (Engine::IsNetworkServer), "client" (Engine::IsNetworkClient), "on" (multiplayer, KNOWN, but
// neither role reads true - the view works there), "?" (the state is UNKNOWN, and only that:
// reads this field to tell the unknown state apart).
inline const char* utMpShapeText(bool known, bool mp, bool server, bool client) {
    if (!known) return "?";
    if (!mp) return "off";
    if (server) return "host";
    if (client) return "client";
    return "on";
}

// an EMPTY OWN page must still put the MOD sack on the page - every refusal keys on
// the mod sack (or on prototype ids), so the real sack there would be live under "Collection ON".
// A mod sack left empty by a failed or unfinished build gives the real sack, as before. The
// StreamOut assert swaps the mod sack out whether it holds prototypes or not (protoIsModSack).
inline bool utViewGetterPicksMod(bool modBuilt, int protoCount, bool emptyByDesign) {
    return modBuilt && (protoCount > 0 || (protoCount == 0 && emptyByDesign));
}

// Apply one event. `arg` is the mode for kUtViewEvModeChanged and 0/1/2 (single / multiplayer /
// unknown) for kUtViewEvMpChanged.
inline UtViewStep utViewApply(UtViewState* s, int ev, int arg) {
    UtViewStep st;
    st.turnedOn = false;
    st.turnedOff = false;
    st.why = kUtViewWhyOk;
    int offWhy = kUtViewWhyOk;   // set when the event forces the view OFF
    switch (ev) {
    case kUtViewEvToggle:
        if (s->on) {
            offWhy = kUtViewWhyToggleOff;
        } else {
            st.why = utViewRefusal(*s);
            if (st.why == kUtViewWhyOk) {
                s->on = true;
                st.turnedOn = true;
            }
            return st;
        }
        break;
    case kUtViewEvModeChanged:
        s->mode = arg;
        if (arg != 1) offWhy = kUtViewWhyTabChange;
        break;
    case kUtViewEvOpen:
        s->open = true;
        offWhy = kUtViewWhyOpen;   // never ON across an open: the load resizes the member
        break;
    case kUtViewEvGoodbye:
        s->open = false;
        offWhy = kUtViewWhyGoodbye;
        break;
    case kUtViewEvWorldUp:
        s->world = true;
        offWhy = kUtViewWhyWorldDown;   // a world load while ON (should not happen): OFF anyway
        break;
    case kUtViewEvWorldDown:
        s->world = false;
        offWhy = kUtViewWhyWorldDown;
        break;
    case kUtViewEvMpChanged: {   // OFF on a CHANGE, in either direction, never on a repeat
        const bool unknown = arg == 2;
        const bool changed = unknown != s->mpUnknown || (!unknown && (arg == 1) != s->mp);
        s->mpUnknown = unknown;
        if (!unknown) s->mp = arg == 1;
        if (changed) offWhy = kUtViewWhyMp;
        break;
    }
    case kUtViewEvFault:
        s->faulted = true;
        offWhy = kUtViewWhyFault;
        break;
    case kUtViewEvBindingsIncomplete:
        s->bindingsOk = false;
        offWhy = kUtViewWhyBindings;
        break;
    case kUtViewEvStreamOutModSack:
        s->faulted = true;          // the door was reached: never again this process
        offWhy = kUtViewWhyStreamOut;
        break;
    default:
        return st;
    }
    if (offWhy != kUtViewWhyOk && s->on) {
        s->on = false;
        st.turnedOff = true;
        st.why = offWhy;
    }
    return st;
}

// ---- InventorySack::GetItemUnderPoint on a mod sack (G5) -------------
// The page class calls it from three sites. The BUTTON handler (TQ.exe 0xBFD10: the gamepad's
// right-click) and the held pick-up (InventorySack::RemoveItem) take whatever id they get:
// the held pick-up is always refused on a mod sack; the button handler takes a collected record
// when takes are possible (kUtUnderTakeRightClick) and is refused otherwise
// (utUnderPointDecide; this said "always refused").
// The page's MOUSE HANDLER (TQ.exe 0xBFED0) serves three branches picked by its two bool
// arguments: b1 = the press (pick-up), b2 = the MOUSE RIGHT-CLICK, neither = the HOVER, which
// only hands the id to the tooltip. Only a PROVEN hover gets the real id; a set bool, or any frame
// the detour could not verify, gets 0 exactly as (refuse when unsure).
enum UtUnderSite { kUtUnderOther = 0, kUtUnderRightClick, kUtUnderHeldPickup, kUtUnderMouseHandler };
enum UtUnderFrame { kUtFrameUnknown = 0, kUtFrameClick, kUtFrameHover };

inline bool utUnderPointRefuse(int site, bool modSack, int frame) {
    if (!modSack) return false;   // a real sack: vanilla, always
    switch (site) {
    case kUtUnderRightClick:
    case kUtUnderHeldPickup:
        return true;
    case kUtUnderMouseHandler:
        return frame != kUtFrameHover;
    default:
        return false;   // not a page-class site (test_bindings proves there are exactly three)
    }
}

// The two bool bytes (the low byte of each pushed argument, as the handler's `cmp byte ptr` reads
// them) -> the frame kind. Anything but 0/1, or an unverified frame, is UNKNOWN.
inline int utUnderFrameKind(bool frameVerified, unsigned char b1, unsigned char b2) {
    if (!frameVerified || b1 > 1u || b2 > 1u) return kUtFrameUnknown;
    return (b1 || b2) ? kUtFrameClick : kUtFrameHover;
}

// The frame shape at the handler's call: `and esp,-8` after `mov ebp,esp`, then a fixed push
// sequence, so EBP - return slot == dist + (EBP & 7) with EBP 4-aligned (dist decoded, 0x68).
inline bool utUnderFrameShape(unsigned ebp, unsigned retSlot, unsigned dist) {
    return dist != 0u && (ebp & 3u) == 0u && ebp > retSlot && ebp - retSlot == dist + (ebp & 7u);
}

// The handler's own arithmetic: the point it passes is mouse - (page pos + parent origin). A frame
// read at the wrong place (or a NaN) fails it.
inline bool utUnderPointMatches(float x, float y, float mx, float my, float px, float py, float ox,
                                float oy) {
    const float ex = mx - (px + ox);
    const float ey = my - (py + oy);
    const float dx = ex > x ? ex - x : x - ex;
    const float dy = ey > y ? ey - y : y - ey;
    return dx <= 0.01f && dy <= 0.01f;   // false for NaN
}

// ---- the take gesture on the page mouse handler -------------------------------------------
// The handler's two bools as a GESTURE: b1 alone = the pick-up press (the take), neither = the
// hover. b2 alone is the MOUSE RIGHT-CLICK - the handler's branch at
// TQ.exe 0xC0208 (b1 is tested first, 0xC00CE): GetObject(id), 0xC0650 =
// Player::IsInventorySpaceAvailable(item) (vt+0x2E8; no room -> PlayInventoryFullSound and a
// dialog, nothing moves), 0xC0530 = Player::GiveItemToCharacter, then, by the page mode,
// RemoveItemFromTransfer([this+0x110] = the id) at 0xC0265. carried GD's name for it (a
// "clone"); TQ has none. b1 AND b2 (never seen; the engine would take the b1 branch): unknown.
enum UtUnderGesture { kUtGestUnknown = 0, kUtGestHover, kUtGestPick, kUtGestRight };

inline int utUnderFrameGesture(bool frameVerified, unsigned char b1, unsigned char b2) {
    if (!frameVerified || b1 > 1u || b2 > 1u) return kUtGestUnknown;
    if (b1 && b2) return kUtGestUnknown;   // refused, as before (it was a "clone")
    if (b2) return kUtGestRight;
    return b1 ? kUtGestPick : kUtGestHover;
}

// What GetItemUnderPoint does on a mod sack. the earlier rule for every site, with ONE opening: a
// verified pick-up press on the mouse handler while takes are possible is decided AFTER the
// original ran - the id passes only when it is a takeable prototype (utTakeDecide), else 0.
// the RIGHT-CLICK (TQ.exe 0xBFD10, its call pinned at +0xBFDD5) opens too
// while takes are possible: the id passes only for a takeable prototype with room in the player's
// inventory, and the take is journalled BEFORE the id is returned - the engine hands the object to
// Player::GiveItemToCharacter (0xBFDEE -> 0xC0530) before it calls RemoveItemFromTransfer
// (0xBFE6E), which then hands it out of the mod sack. The site reads no frame: no gesture.
enum UtUnderDecision { kUtUnderRefuse = 0, kUtUnderPass, kUtUnderTakeIfCollected,
                       kUtUnderTakeRightClick };

inline int utUnderPointDecide(int site, bool modSack, int gesture, bool takesPossible) {
    if (!modSack) return kUtUnderPass;
    switch (site) {
    case kUtUnderRightClick:
        return takesPossible ? kUtUnderTakeRightClick : kUtUnderRefuse;
    case kUtUnderHeldPickup:
        return kUtUnderRefuse;
    case kUtUnderMouseHandler:
        if (gesture == kUtGestHover) return kUtUnderPass;
        if (gesture == kUtGestPick && takesPossible) return kUtUnderTakeIfCollected;
        // the MOUSE right-click takes like the button handler's (viewTakeRightClick)
        if (gesture == kUtGestRight && takesPossible) return kUtUnderTakeRightClick;
        return kUtUnderRefuse;
    default:
        return kUtUnderPass;
    }
}

// =============================================================================================
// The lost-item guards, pure. Each has a mutant in
// tools\test_viewgate.cpp that the same property check must catch.
// =============================================================================================
// 1(a) REMOVED - every switch is allowed while the cursor holds an item (the
// item stays on the cursor, the page changes); the teardown's key rule below keeps a held take.

// 1(b) An engine add into a MOD sack (the current one or a retired one) is refused unless it is
// the mod's own prototype placement; every other sack is a pure pass-through, so the file load
// (the Vec2 overload into the REAL sacks) is never touched.
enum { kUtSackAddPass = 0, kUtSackAddRefuse = 1 };
inline int utSackAddDecide(bool modSack, bool modPlacing) {
    return (modSack && !modPlacing) ? kUtSackAddRefuse : kUtSackAddPass;
}

// 1(c) The mod sack's teardown, per key of its map: only an id the mod created (the prototype
// list) is destroyed; the kept take is forgotten; anything else is FOREIGN and is
// returned to the real Transfer sack (or left in the retired sack), never destroyed.
enum { kUtTearDestroy = 0, kUtTearForget = 1, kUtTearReturn = 2 };
// a taken prototype that could NOT leave the mod sack (a failed hand-out) is still
// listed in that sack's map, but it is on the cursor. It stays FORGOTTEN - never destroyed, never
// returned - until that sack is retired (the list is cleared then; a retired sack is never swept
// again). The keep id alone was not enough: the old protoForgetTaken cleared it, so the fault's
// teardown read the taken id as FOREIGN and auto-placed a CLONE of it into the real Transfer.
enum { kUtForgotMax = 16 };
struct UtForgotten {
    unsigned ids[kUtForgotMax];
    int n;
    bool overflow;   // more than kUtForgotMax: every id the mod did not list is left in the sack
};
inline void utForgottenClear(UtForgotten& f) {
    f.n = 0;
    f.overflow = false;
}
inline bool utForgottenHas(const UtForgotten& f, unsigned id) {
    if (!id) return false;
    for (int i = 0; i < f.n && i < kUtForgotMax; ++i)
        if (f.ids[i] == id) return true;
    return false;
}
// protoForgetTaken's bookkeeping: the id moves from the keep slot onto the forgotten list.
inline void utForgetTaken(UtForgotten& f, unsigned& keepId, unsigned id) {
    if (!id) return;
    if (!utForgottenHas(f, id)) {
        if (f.n < kUtForgotMax) f.ids[f.n++] = id;
        else f.overflow = true;
    }
    if (keepId == id) keepId = 0;   // the list carries it now
}

inline int utTeardownAction(unsigned key, const unsigned* protos, int nProtos, unsigned keepId,
                            const UtForgotten* forgot) {
    // The kept take is on the CURSOR (or leaving for it): never destroyed and never "returned" -
    // an auto-place of a cursor item into the real Transfer would make a second copy.
    if (keepId != 0 && key == keepId) return kUtTearForget;
    if (forgot && utForgottenHas(*forgot, key)) return kUtTearForget;
    for (int i = 0; i < nProtos; ++i) {
        if (protos[i] == key) return kUtTearDestroy;
    }
    // Too many forgotten takes to tell one from a foreign id: it stays in the (retired) sack -
    // never a second copy.
    if (forgot && forgot->overflow) return kUtTearForget;
    return kUtTearReturn;
}
inline int utTeardownAction(unsigned key, const unsigned* protos, int nProtos, unsigned keepId) {
    return utTeardownAction(key, protos, nProtos, keepId, nullptr);
}

// GameEngine::AddItemToTransfer(id) while the view is ON. Only the
// quick-move's PRIMARY call (TQ.exe 0x107A6C: it tests the result and keeps its item on false) may
// deposit. The only other caller is the stacked-extras loop (0x107AED): it ignores the result and
// disposes of its item unconditionally, so a refusal there is a certain LOSS - it gets the engine's
// own add instead (a copy on the REAL Transfer page, never the mod sack), never a deposit. With the
// call site unconfirmed nothing can be told apart: refused (the primary call keeps its item, and
// the extras loop only runs after a primary success).
enum { kUtQuickPrimary = 0, kUtQuickRefuse = 1, kUtQuickVanilla = 2 };
inline int utQuickCallerDecide(bool siteConfirmed, bool primary) {
    if (!siteConfirmed) return kUtQuickRefuse;
    return primary ? kUtQuickPrimary : kUtQuickVanilla;
}

// 4(a) Slot-wide items, WHERE: the slot's prototype answers for a point on the slot's EMPTY cells
// only where already answers with an id - the verified hover (the tooltip) and the verified
// pick while takes are possible (still filtered by utTakeDecide afterwards). Never for the held
// pick-up, an unverified frame, another site or a real sack. the right-click take
// (the button handler's, and the mouse handler's b2) answers by slot too - the icon is drawn
// centred in its slot, so the user right-clicks what is drawn.
inline bool utSlotWideAllowed(int site, bool modSack, int gesture, int decision) {
    // the right-click take answers by slot like the left-click take (still filtered)
    if (modSack && site == kUtUnderRightClick) return decision == kUtUnderTakeRightClick;
    if (!modSack || site != kUtUnderMouseHandler) return false;
    if (decision == kUtUnderTakeIfCollected) return gesture == kUtGestPick;
    if (decision == kUtUnderTakeRightClick) return gesture == kUtGestRight;
    if (decision == kUtUnderPass) return gesture == kUtGestHover;
    return false;
}

// 4(a) Slot-wide items: the index of the prototype whose SLOT holds the cell (col, row); -1 when
// no slot holds it, or when two do (refuse when unsure).
struct UtSlotRect {
    int col, row, w, h;
};
inline int utSlotIndexAt(const UtSlotRect* s, int n, int col, int row) {
    int found = -1;
    for (int i = 0; i < n; ++i) {
        if (s[i].w < 1 || s[i].h < 1) continue;
        if (col >= s[i].col && col < s[i].col + s[i].w && row >= s[i].row && row < s[i].row + s[i].h) {
            if (found >= 0) return -1;
            found = i;
        }
    }
    return found;
}

// the wheel WITHOUT the settle. Every navigation input (a wheel tick, a key, a group or
// OWN button) writes the absolute window offset (ut_live's g_wantPage, clamped per tick) and marks
// the window dirty; the NEXT GameEngine::Update takes it (viewTick: at most one rebuild per Update)
// and so every input between two Updates coalesces into ONE step. The burst below is only what that
// rebuild reports: the wheel ticks it covers and the QPC of the first input (its tick-to-window
// latency). The WndProc that feeds it is pumped by the game thread (the window was found with
// EnumThreadWindows on the Present thread, which runs GameEngine::Update), so it is a plain struct.
struct UtNavBurst {
    int ticks;            // wheel ticks that moved the offset
    long long firstQpc;   // QPC of the first input since the last take, 0 = none
};
inline void utNavNote(UtNavBurst& b, bool wheel, long long qpc) {
    if (wheel) ++b.ticks;
    if (b.firstQpc == 0) b.firstQpc = qpc != 0 ? qpc : 1;
}
inline UtNavBurst utNavTake(UtNavBurst& b) {
    const UtNavBurst t = b;
    b.ticks = 0;
    b.firstQpc = 0;
    return t;
}

// an UNCOLLECTED record shows no tint, no red and no border unless hovered.
// The item widget's background draw (TQ.exe 0x10A9B0: the requirement red, the class tint + the
// rarity border texture, the grey shade, the hover tint) is SKIPPED for a prototype whose record
// is not collected and whose slot is not the hovered one (the earlier un-veil rule - the same
// predicate as the gray lend), in every owned_marks mode except 0 (the veil and the frame
// lose the tint and border too), and only while the rect route decides that widget's draw (the
// detour armed on a prototype of the shown page). A collected record, the hovered slot,
// owned_marks=0 and every widget outside the route draw as the engine draws them.
inline bool utBackgroundSkip(bool collected, bool hovered, int ownedMarks, bool routeLive) {
    return !collected && !hovered && ownedMarks != 0 && routeLive;
}
// the gray lend's rule (ut_owned.cpp ownedGrayWanted) as its own pure
// predicate, so the viewgate row asserts "gray + route live => the background is skipped" over the
// SAME rule the runtime runs. `ownedMarks` is this frame's style (0 when the marks are not trusted).
inline bool utOwnedGray(bool collected, bool hovered, int ownedMarks) {
    return !collected && !hovered && ownedMarks == 3;
}

// The rect route's token (panelItemBackgroundPre -> hk_ItemBackground): -1 = the original runs and
// no Post (not a prototype, the route not armed, a fault in Pre); otherwise the low 16 bits are the
// route's save index for the Post (0xFFFF = none: a refused widget) and kUtBgSkipFlag tells the
// detour NOT to run the original. A fault anywhere = -1 = the original runs (no skip).
const int kUtBgSkipFlag = 0x40000000;
const int kUtBgNoSave = 0xFFFF;
inline int utBgToken(int save, bool skip) {
    if (save < 0 || save >= kUtBgNoSave) {
        if (!skip) return -1;
        save = kUtBgNoSave;
    }
    return save | (skip ? kUtBgSkipFlag : 0);
}
inline bool utBgTokenSkip(int token) { return token >= 0 && (token & kUtBgSkipFlag) != 0; }
inline int utBgTokenSave(int token) {
    if (token < 0) return -1;
    const int s = token & 0xFFFF;
    return s == kUtBgNoSave ? -1 : s;
}

}  // namespace ut
