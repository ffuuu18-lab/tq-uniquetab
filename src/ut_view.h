// ut_view.h - the collection VIEW on the caravan's Transfer page (design v2, display only).
//
// While the view is ON, the Transfer page's own accessor is handed the MOD sack (the prototypes of
// one catalogue page) instead of the real Transfer sack, and every gesture that could move an item
// on that page is refused at the engine level. While it is OFF every detour is a pure pass-through.
//
// THE ONE RULE (GD HANDOFF 1, TQ terms): the engine must never stream the mod sack into a save
// file, and no prototype may ever reach a real sack. The guards, by number (DESIGN-PAGE-REUSE 6):
//   G1 substitute only for the Transfer page's accessor      viewGetterResult
//   G2 re-point the cached member on every way out           viewForceOff (the ONLY off path)
//   G3 StreamOut pre-detour: re-point, ERROR, latch           viewOnStreamOut
//   G4 OFF at every open (the load) and world change          viewOnOpen / viewOnWorld
//   G5 prototypes never leave the mod sack; destroyed at OFF  the refusals below + protoDestroyAll
//   G6 OFF on a session-state change, a fault, detach   viewTick / viewFault / viewDetach
//   G8 refuse when unsure                                     viewInit + the ON request
//   G7 journal first: a deposit's row is on disk before the engine consumes the original, a
//      take's before the prototype reaches the cursor         viewDepositDrop / viewDepositQuick /
//                                                              viewFilterSetId (ut_store / ut_rescue)
//
// Threads: every entry point except viewDetach runs on the GAME thread (the detours, the Update
// tick, the window procedure of the game's own window). viewOn() may be read from anywhere.
#pragma once

#include "tq_runtime.h"

namespace ut {

// After hooksInstall: decides whether the view can ever be enabled this process (G8) and says so
// in ONE line: "view: available ..." or "view: unavailable - <reason>". `hooksOk` = every
// detour and the capability-slot patch went in.
void viewInit(bool hooksOk);
bool viewAvailable();
bool viewOn();

// THE single OFF path: re-points the page's cached member at the real Transfer sack (when it holds
// the mod sack), destroys every prototype, marks the view OFF. Idempotent; game thread.
// `worldGone`: the world is torn down - the prototypes are dropped, not destroyed (the engine may
// have freed them already), and the mod sack is abandoned for a fresh one.
void viewForceOff(const char* reason, bool worldGone = false);

// A WARN/ERROR in the view code, or an exception it caught: force OFF and latch for the process.
void viewFault(const char* where);
void viewFaultSafe(const char* where);     // viewFault under its own SEH guard

// Input (the game's window procedure) and the pad: queued, applied on the next game tick.
void viewRequestToggle();

// The game tick (GameEngine::Update detour, after the original): applies a queued toggle,
// rebuilds the page after a navigation, polls multiplayer.
void viewTick();

// ---- the events the detours report ----------------------------------------------------------
void viewOnModeChanged(int mode);     // SetCaravanMode(arg) PRE - every frame; cheap when equal
void viewOnOpen();                    // NpcCaravan::OnPlayerInteract PRE
void viewOnGoodbye();                 // GameEngine::CaravanGoodbye PRE (G2: before the saves)
void viewOnWorld(bool up);            // the main player appeared / went away
unsigned viewWorldGeneration();      // bumped at every world up / down

// ---- the substitution (G1) --------------------------------------------------------------------
void viewAccessorEnter(void* subWindow);   // Transfer page accessor PRE
void viewAccessorLeave(void* subWindow);   // ... POST (the live member check)
TqSack* viewGetterResult(TqSack* real);    // GetPlayerTransfer: the mod sack or `real`

// ---- the save assert (G3) ---------------------------------------------------------------------
bool viewStreamOutMustSkip(void* ui);      // after viewOnStreamOut: still a mod sack?
void viewOnStreamOut(void* ui);            // UIStashInventory::StreamOut PRE

// ---- the refusals (G5); each is FALSE whenever the view is OFF -------------------------------
bool viewRefuseTransferDrop();             // PrimaryTransferActivate
bool viewRefuseTransferAdd();              // GameEngine::AddItemToTransfer(id) (quick-move)
bool viewRefuseRemove(unsigned id);        // RemoveItemFromTransfer(id) of a prototype
unsigned viewFilterSetId(unsigned id);     // CursorHandlerItemMove::SetId: a prototype id -> 0
// InventorySack::GetItemUnderPoint. `retSlot` = the detour's _AddressOfReturnAddress (the
// caller's EBP was saved right below it). True = return 0 (right-click, held pick-up, a click of
// the page mouse handler, or any frame that could not be verified); false for a PROVEN hover, and
// then `hover` carries the grid origin the handler computed (for the owned marks).
struct UtHoverFrame {
    bool valid;
    bool realSack;            // read on the REAL Transfer sack (view OFF): the frame only
    bool slotWide;            // utSlotWideAllowed held - an empty cell of a slot may
                              // answer with the slot's prototype (the hover / the verified pick)
    float gridX, gridY;       // page pos + parent origin: what the handler subtracts
    float originX, originY;   // the parent origin alone (the owned grid check's log)
};
// `takeCheck` (may be null) comes back true for a verified pick-up press while takes are
// possible: the detour then calls the original and passes its id through viewTakeFilterUnder.
// `takeRight` (may be null) says the take is the RIGHT-CLICK's (viewTakeRightClick).
bool viewRefuseUnderPoint(const TqSack* s, const void* returnAddress, const void* const* retSlot,
                          float x, float y, UtHoverFrame* hover, bool* takeCheck = nullptr,
                          bool* takeRight = nullptr);
void viewNoteHover(const UtHoverFrame& hover, float x, float y, unsigned id);   // after the original
bool viewRefuseSort(const TqSack* s);      // InventorySack::Sort on the mod sack
bool viewIsModSack(const void* s);

// ---- the moves (G7 journal first). Each is a pure pass-through while the view is OFF. ----
enum { kUtMovePass = 0, kUtMoveRefuse = 1, kUtMoveDone = 2 };
// PrimaryTransferActivate while ON: Refuse = return false (the cursor keeps the item); Done = the
// row is on disk and the original was disposed of the engine's way (the detour then SetId(0)s
// the handler with the ORIGINAL SetId and returns true). Never calls a Primary* original.
int viewDepositDrop(TqCursorItemMove* handler);
// AddItemToTransfer(id) while ON: Done = the row is on disk, return true WITHOUT the original
// (the TQ.exe caller removes and destroys the original itself); Refuse = return false.
int viewDepositQuick(unsigned id);
// The capability slot (+0x30) while ON: true only for an item a drop would deposit (cached per
// cursor id: one object-list walk per new cursor item, never per frame).
bool viewCapable(const TqCursorItemMove* handler);
// After GetItemUnderPoint's original, for a verified pick-up press (kUtUnderTakeIfCollected):
// the id when it is a takeable prototype (armed for the SetId of this same gesture), else 0.
unsigned viewTakeFilterUnder(unsigned id);
// after GetItemUnderPoint's original for the RIGHT-CLICK (its call pinned at
// TQ.exe+0xBFDD5, kUtUnderTakeRightClick): the id when it is a takeable prototype, the right-click
// serves the Transfer page holding this mod sack (mode 1) and the player's inventory has room for
// it (InventorySack::IsSpaceForItem on the sacks PlayerInventoryCtrl::AddItem tries); the take is
// then JOURNALLED here (G7: the engine gives the object to Player::GiveItemToCharacter before its
// RemoveItemFromTransfer) and armed for viewTakeRemove. Else 0 (nothing happens).
// `rawId` = the original's answer (0 = the slot-wide lookup gave `id`), (x, y) = the point
// (the DEBUG trace), `viaMouse` = the page mouse handler's b2 (its engine asks
// Player::IsInventorySpaceAvailable before it gives the item, so the take asks it too).
unsigned viewTakeRightClick(const TqSack* s, unsigned id, unsigned rawId, float x, float y,
                            bool viaMouse);
// RemoveItemFromTransfer(id): Pass (not a prototype), Refuse (a prototype without a journalled
// take), Done (the journalled take: the prototype left the MOD sack; return true, no original).
int viewTakeRemove(unsigned id);
// after the ORIGINAL SetId(id) of a journalled take - the handler that holds the
// taken item. Its "out" row is not settled by any save while the item stays on that cursor.
void viewTakeOnCursor(TqCursorItemMove* handler, unsigned id);
// SetId's filter faulted: the id the cursor may safely get (a prototype only when
// its take is journalled, else 0; a real item's id unchanged).
unsigned viewSetIdOnFault(unsigned id);
// the right-click take faulted after viewTakeRightClick journalled and armed it:
// the id (the engine gives it to the player as intended), else 0.
unsigned viewTakeRightOnFault(unsigned id);

// DllMain(DETACH): re-point only (a plain memory write under SEH; no engine call, no free).
void viewDetach();

// "view=on group=3 page=1/3 protos=52 refused(drop=0 add=0 setid=1 remove=1 under=2 sort=0)"
const char* viewStatus();

// ------------------------------------------------------------------------
// The pad and the hotkey ask; the view tick decides (also while an item is held).
enum { kUtViewReqToggle = 1, kUtViewReqOn = 2, kUtViewReqOff = 3 };
void viewRequest(int want);
// the view's state INCLUDING a queued request (the wheel cycle steps from it).
bool viewPendingOn();
// 1(b) / 1(d) InventorySack::AddItem on a MOD sack: true = refuse (false without the original; one
// WARN with the caller, the item id and its record). RemoveItem on a mod sack: DEBUG line.
bool viewSackAddRefused(const TqSack* s, const TqItem* item, const void* ret, bool vec);
void viewSackRemoveNoted(const TqSack* s, unsigned id, bool result, const void* ret);
// AddItemToTransfer(id) while ON - kUtQuickPrimary (the
// quick-move's primary call: the deposit may run), kUtQuickVanilla (any other caller: the engine's
// own add into the REAL Transfer, never a deposit) or kUtQuickRefuse (the call site is unknown).
int viewQuickCaller(const void* ret, unsigned id);
// The non-primary caller's item went through the engine's own add: one ERROR saying where it went.
void viewQuickVanillaNoted(const void* ret, unsigned id, bool added);
// 4(a) the prototype whose SLOT holds the sack point (x, y) - the whole slot selects its item.
unsigned viewSlotIdAt(const TqSack* s, float x, float y);
// The Transfer page sub-window the accessor ran on (null = not seen yet).
void* viewPageWindow();
// the page origin, the grid and the cell size from the Transfer page draw's arguments
// (kUtDrawOk, or why not - ut_padlayout.h utDrawOrigin). Reads only.
int viewDrawOrigin(void* page, const void* origin, int pass, float* pageX, float* pageY,
                   float* gridX, float* gridY, unsigned* cellW, unsigned* cellH);

}  // namespace ut
