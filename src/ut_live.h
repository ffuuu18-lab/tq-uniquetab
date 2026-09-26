// ut_live.h - the collection page model: the group table, the page / group the view shows, and
// the wheel / key / button navigation. TQ port of GD's ut_live (the group table, the page model,
// the wheel and button input); GD's reagent-box capture and re-point are NOT here - on TQ the
// page is the mod's own InventorySack of display prototypes (ut_proto) that the Transfer page is
// handed while the view is ON (ut_view).
//
// GD's "row" (a scroll position in a box grid) is TQ's "page": a group is laid out in uniform
// SLOTS (one slot size per group = its largest footprint, each item centred in its slot;
// gdut::packSlots, src\model\layout.h) on as many 16 x 15 pages as it needs, and the view shows
// one page at a time. Navigation only writes two mod-owned ints and a dirty flag; the page is
// rebuilt on the next game tick (viewTick), never from the input path.
#pragma once

#include <windows.h>

#include "ut_proto.h"

namespace ut {

// Reads uniq-groups.txt (the mod folder, as generates it) and packs every group. Worker
// thread, once, before the hooks. False = the view is unavailable (said by viewInit).
bool liveInit(HMODULE selfModule);
bool liveActive();

int liveGroupCount();
const char* liveGroupLabel(int group);   // never null
int liveGroupEntries(int group);         // -1 out of range
int livePageCount(int group);            // 0 out of range; the OWN filter's count when active
const char* liveGroupRecord(int group, int k);   // the k-th record of a group, null out of range
int livePagesTotal();

// What the page shows now (set when the page is built) and what the input asked for.
int liveShownGroup();
int liveShownPage();
int liveWantedGroup();

bool liveSelectGroup(int group);   // absolute: one button, one group; the window at row 0
// the view shows a WINDOW of the group's slot rows per page, from a slot-row
// offset (liveShownPage). liveStepPage moves it by a whole window (< > / PgUp / PgDn), liveStepRow
// by one slot row (the wheel); both clamp so the window stays full (the last one overlaps).
bool liveStepPage(int dir);        // -1 / +1 window, clamped; false when nothing moved
bool liveStepRow(int dir);         // -1 / +1 slot row, clamped; rebuilt on the next Update
// The wheel over the page: one tick = one slot row (GD: one row). Ctrl+wheel steps the groups;
// the cycle's ends (Transfer is a stop) are ut_panel's cycleStep - Ctrl answers false there.
bool liveHandleWheel(int ticks, bool ctrl);
// PageUp / PageDown = a window, Ctrl+PageUp / PageDown = group (GD's keys, TQ's units).
bool liveHandleKey(int vk, bool ctrl);
// the shown window - slot rows of the shown group (all windows) and per window.
int liveShownRows();
int liveWindowRows();
// the host cells no slot of the shown window uses (model windowLeftover): at most 3.
struct UtCellRect {
    int col, row, w, h;
};
int liveLeftover(UtCellRect* out, int cap);

// - the OWN filter (GD's owned-only view). ON = a group shows
// only its OWNED records (.x: "owned somewhere" = ut_owned's store + inventory set;
// the journal's rows > 0), packed into the group's same uniform slots in catalogue order
// (gdut::placePage); the page count follows (at least one page, which may be empty). The start
// state is the ini's owned_only; the OWN button flips it and the new value is written back into
// the file (GD's rule) on the next game tick. While the owned set is UNKNOWN the filter cannot be
// applied: the page shows every record (the owned self-checks need prototypes to run on) and the
// label says "owned ?" - the lamp still shows what was asked.
bool liveOwnedOnly();              // asked for (the lamp)
bool liveOwnedOnlyActive();        // asked for AND the owned set is known (the page is filtered)
bool liveToggleOwnedOnly();        // the OWN button (game thread): flips, rebuilds the page
void livePersistOwnedOnly();       // the view tick: writes owned_only back after a flip
// ut_owned, after every refresh of the owned set (game thread): re-reads every record's owned
// flag (ownedRecordState); a change while the filter is active rebuilds the page.
void liveOwnedChanged();
// The page just built is legitimately EMPTY (the filter is active and the group owns nothing).
bool liveEmptyPageOk();

// - the property search's highlight (ut_search writes it; game thread unless said otherwise). Each
// group keeps a match flag per record next to its owned flag. The search never lays the page out:
// OWN, the row scroll, livePageCount and the window are the ones without the search, always; a
// match only marks its slot (liveSearchHighlight).
void liveSearchSetMatch(int group, int k, bool match);
// `active` = a query stands. Recomputes every group's match count and the marks (bit i = group i
// is fully indexed, per `indexedMask`, and holds a match that passes OWN). The page is not rebuilt.
void liveSearchApply(bool active, unsigned indexedMask);
// Prototype i of the last built page shows `record`: is it highlighted now (a query stands and
// the record matches)? Read every frame by the page draw.
bool liveSearchHighlight(int i, const char* record);
// Any thread: the marks of the last apply (0 while no query stands).
unsigned liveSearchMarks();
// Any thread: the groups fully indexed at the last apply (0 while no query stands).
unsigned liveSearchIndexed();
// The shown matches over every group (OWN applied), and in how many groups (`groups` may be null).
int liveSearchFound(int* groups);
// The matches of `group` that pass OWN while a query stands, and its size (`of`).
int liveSearchShownIn(int group, int* of);

// True once after a navigation asked for another page (the tick rebuilds it). no settle -
// the NEXT GameEngine::Update rebuilds (at most one rebuild per Update), and every input that came
// since the last one is in that one step (ut_viewgate.h UtNavBurst).
bool liveTakeDirty();
// drop a pending rebuild (the view builds the page itself, e.g. when it turns ON).
void liveDropDirty();
// the INPUT the rebuild just taken answers (a wheel tick, a key, a group / OWN button):
// `wheelTicks` = the wheel ticks it covers, `firstQpc` = QueryPerformanceCounter at the first input
// (the tick-to-window latency's start). False (and 0 / 0) = no input (a deposit, an owned refresh).
// Reported once: a second call answers false.
bool liveRebuildInput(int* wheelTicks, long long* firstQpc);
// rebuild the shown page on the next view tick (a deposit or a take changed its rows).
void liveMarkDirty();
// is `folded` (lower case, '/') one of the catalogue's records? Game thread; no allocation.
bool liveHasRecord(const char* folded);
// Fills the WANTED page's places (catalogue record + top-left cell) and makes it the shown one.
int livePagePlaces(UtProtoPlace* out, int cap);

// "groups=15 pages=38 shown=0/0 wanted=0/0"
const char* liveStatus();

}  // namespace ut
