// ut_owned.h - the OWNED marking of the collection view.
//
// What is owned: the folded record paths of every item in the REAL Transfer, Stash and Relic Vault
// sacks (InventorySack::GetInventory -> the id map, walked as a VS2012 std::map) plus the
// character's inventory ids (Character::GetInventoryItems, advisory), resolved id -> object through
// ObjectManager::GetObjectList + Object::GetObjectId, and named with Object::GetObjectName. The keys
// are folded with GD's `utOwnedNormaliseKey` and hashed with GD's `utOwnedRowHash` (ut_ownedfold.h),
// kept as a sorted array; nothing is ever written into engine memory.
//
// Two self-checks decide whether the set is trusted (else the label says "owned ?" and no mark is
// drawn): the map walk must return EXACTLY the mod sack's own prototype ids, and GetObjectName of
// every prototype must fold to the record it was built from.
//
// Where the marks go: the grid's on-screen origin is measured, not guessed - a proven hover
// (ut_view: the page mouse handler's own frame) hands over `page pos + parent origin`, the very
// numbers the engine used for the point. it is accepted once (INFO `owned: grid at ...`)
// when (a) the prototype under the point covers the cell the point falls in, (b) a second proven
// hover on ANOTHER prototype gives the same origin and passes (a), and (c) the 16 x 15 grid lies
// inside the canvas and inside the caravan window rect of the records - or, where the records
// disagree, the hovers' points matched the OS cursor (proof that the engine's UI units are canvas
// px). the earlier record x (inventoryX 27) and page-rect tests are gone: the engine measured x 93, not
// 37 (a 1366 x 768 window at UI scale 1). Any failure refuses the MARKS (one WARN), never the view. A new canvas size or
// UI scale asks for the check again.
//
// every prototype of a group sits centred in the group's uniform SLOT (ut_live); the veil
// and the "cursor on a prototype" test cover the whole slot.
//
// the window rect of check (c) is the LIVE caravan frame (ut_panel measures it from the
// same proven hover, ut_padlayout.h) once it is accepted - the window test is the primary route
// again; the cursor proof stays the fallback while the frame is unmeasured or refused. The OWN
// filter can leave a page EMPTY: the two self-checks then keep the verdict they proved on a
// non-empty page of this world (an empty sack must still walk to zero keys).
//
// Threads: everything here runs on the game thread (the view tick, the page build, the
// GetItemUnderPoint detour, the Present detour), except ownedMarkDirty (any thread).
#pragma once

#include "ut_plate.h"

namespace ut {

void ownedOnViewOn();      // after the first page is built: a full refresh + the self-checks
void ownedPageBuilt();     // after a navigation: the page flags and the group count
void ownedMarkDirty();     // a store event passed through: refresh on a later tick while ON
void ownedTick(bool on);   // the view tick: a dirty refresh, at most twice a second
void ownedOnViewOff();     // the page flags are dropped (no mark without prototypes)
void ownedWorldGone();     // the set is dropped; the grid stays measured (same window)
// A proven hover on the mod sack: the grid origin the handler used (page pos + parent origin),
// the parent origin alone (for the log), the sack point, the id.
void ownedNoteHover(float gridX, float gridY, float originX, float originY, float x, float y,
                    unsigned id);

// Per frame (Present detour), allocation-free.
// The mark style for this frame (the ini's owned_marks: 1 the veil, 2 a frame), 0 = none. The
// cursor (canvas px; `cursorOk` = it was read) hides them while it is on a prototype of the grid:
// the engine's tooltip is open then, and the marks are drawn after it.
int ownedMarksBegin(const PlateGeometry& g, bool cursorOk, float cx, float cy);
// only the hovered slot again (the rect route's Begin, the POST's cursor).
void ownedUnveilRefresh(bool cursorOk, float cx, float cy);
bool ownedMarkRect(int i, float* x, float* y, float* w, float* h);   // prototype i's SLOT, if UNOWNED
// prototype i's icon is to be drawn gray - owned_marks=3, the marks are on this frame (the
// same trust as the veil), the record is uncollected and the slot is not the hovered one (the earlier
// un-veil rule: the item under the cursor shows its colours). Game thread; no allocation.
bool ownedGrayWanted(int i);
// prototype i's background draw (the engine's red, class tint, rarity border) is to be
// SKIPPED - ut_viewgate.h utBackgroundSkip over: the record uncollected, the slot not the hovered
// one (the same g_unveil as the gray lend and the veil), the marks' style this frame (0 when the
// marks are off or not trusted: nothing is skipped) and `routeLive` (the caller's: the rect route
// decides that widget's draw). Game thread; no allocation.
bool ownedBackgroundSkip(int i, bool routeLive);
// the marks are trusted this frame (ownedMarksBegin's answer: owned_marks != 0,
// the journal and the self-checks known, the grid measured, the OWN filter off). For the log only.
bool ownedMarksNow();
bool ownedLabel(int* owned, int* total);        // the shown group; false = unknown
// the whole catalogue (every group): the records collected and the records; false = unknown.
// Counted on the game thread when the journal set changes, never per frame.
bool ownedLabelAll(int* owned, int* total);
// the corner tick of an UNCOLLECTED prototype whose record the player has somewhere (the
// store set; ini have_marks). False when there is none for prototype i.
bool ownedHaveRect(int i, float* x, float* y, float* w, float* h);

// (the OWN filter, ut_live): the owned set is KNOWN (both self-checks held), and one
// record's state in it: 1 owned, 0 not, -1 unknown. Game thread; no allocation.
bool ownedKnown();
int ownedRecordState(const char* record);

const char* ownedStatus();

// the keys of a sack's map (SEH-guarded walk, msvc.map row); -1 = unreadable or > cap.
int ownedSackKeys(const void* sack, unsigned* out, int cap);

}  // namespace ut
