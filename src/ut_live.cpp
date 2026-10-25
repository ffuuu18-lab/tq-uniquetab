// ut_live.cpp - the collection page model (see ut_live.h). TQ port of GD's ut_live.cpp: the group
// table, the want/shown pair written by the input and read by the tick, and the wheel / key /
// select entry points keep GD's names and semantics; the reagent-box capture and the display
// prototype cache are not ported (TQ's page is a sack of prototypes). GD's owned-only
// filter IS ported (the OWN button): the page is laid out by gdut::placePage over the owned flags.
//
// Invariants: the input path writes only g_wantGroup / g_wantPage / g_dirty (interlocked); the
// group table is built once on the worker before any hook and never changes afterwards, so the
// game thread reads it without a lock.

#include "ut_live.h"

#include <stdio.h>

#include <algorithm>

#include <string>
#include <vector>

#include "model/layout.h"
#include "ut_padlayout.h"
#include "ut_caravangeo.h"   // uniq-geometry.txt (pure)
#include "ut_padpage.h"   // one page per button: utUnionPlan / utUnionCell / utUnionRowOf
#include "ut_unionrow.h"   // the saved row on a page that has just become a union (pure)
#include "ut_gridpack.h"   // the re-fit on new cells (pure)
#include "ut_viewgate.h"   // UtNavBurst
#include "ut_config.h"
#include "ut_generate.h"   // utGameFile, the game in force
#include "ut_log.h"
#include "ut_mikind.h"   // the KIND table (the catalogue's monster infrequents)
#include "ut_owned.h"
#include "ut_paths.h"
#include "ut_retire.h"
#include "ut_search.h"   // the pure count / highlight / mark rules
#include "ut_relicpage.h"   // the relic and charm page (pure)
#include "ut_rescue.h"      // journalRowsOf / journalWrites (the relic page's facts)
#include "ut_proto.h"       // protoSlotKindAt (the relic page's highlight)

namespace ut {
namespace {

struct Group {
    std::string label;
    std::string id, tag;                // the G line's (src\gen\ut_groups.h)
    std::vector<std::string> records;   // uniq-groups.txt order
    std::vector<unsigned char> fw, fh;  // footprints
    std::vector<gdut::PackPlacement> placed;
    gdut::SlotGeometry slot;            // one slot size per group (its largest footprint)
    int pages = 0;
    std::vector<unsigned char> owned;   // the OWN filter's flags (sized once, at init)
    int ownedCount = 0;
    std::vector<unsigned char> match;   // the search's flags (sized once, at init)
    int hitCount = 0;                   // the matches that pass OWN, while a query stands
};

std::vector<Group>* g_groups = nullptr;

// The cells every group is packed on: the derived grid's 16 x 15 (the engine's Transfer sack), or
// the cells of the player's grid_ rectangle while it is in force (liveSetGrid, game thread).
int g_hostCols = gdut::kHostCols, g_hostRows = gdut::kHostRows;

// ---- one page per button --------------------------------------------------------------------
// A button of the buttons file holding several groups shows ONE page: a virtual Group whose
// records are its groups' records concatenated in group order (ut_padpage.h utUnionPlan /
// utUnionLocate), its slot the smallest slot of the table that holds every member's slot
// (utUnionCell: the largest member's), its OWN and search flags the members' flags in the same
// order. The page is keyed by the union's FIRST group (its rep): g_wantGroup and g_shownGroup and
// uniq_group hold real group indexes, so the deposit gate, the search's per-group marks and the
// per-group counts read what they read before; the page layout, the row window, the label's
// numbers and the search's shown count read the union. A one-group button has no union.
// Built on the GAME THREAD (liveSetButtons, from ut_panel's resolve of the buttons file) and
// published by pointer; the input path reads it unlocked, so a replaced table goes through the
// two-deep retire list (ut_retire.h) and is freed two replacements later.
struct UnionTable {
    UtUnionPlan plan;
    std::vector<Group> view;                 // per union (plan.members): the virtual group
    std::vector<std::vector<int>> base;      // per union: each member's first index in it
    std::vector<int> cellGroup;              // per union: the member whose slot it takes, -1 none
    std::vector<int> buttonOf;               // per group: its button (-1 = none), as handed over
};
UnionTable* volatile g_unions = nullptr;

// ---- the retired tables (ut_retire.h) -----------------------------------------------------------
// A game switch retires the game's group, KIND and button-page tables together (liveReloadGame); a
// change of the buttons file retires the button-page table alone (liveSetButtons). Each is freed on
// the game thread two retirements later. The sizes are the vectors' and strings' heap blocks.
size_t strBytes(const std::string& s) { return s.capacity() > 15 ? s.capacity() + 1 : 0; }

size_t groupBytes(const Group& g) {
    size_t b = strBytes(g.label) + strBytes(g.id) + strBytes(g.tag);
    b += g.records.capacity() * sizeof(std::string);
    for (const std::string& r : g.records) b += strBytes(r);
    b += g.fw.capacity() + g.fh.capacity() + g.owned.capacity() + g.match.capacity();
    b += g.placed.capacity() * sizeof(gdut::PackPlacement);
    return b;
}

size_t groupsBytes(const std::vector<Group>* gs) {
    if (!gs) return 0;
    size_t b = sizeof(*gs) + gs->capacity() * sizeof(Group);
    for (const Group& g : *gs) b += groupBytes(g);
    return b;
}

size_t unionBytes(const UnionTable* u) {
    if (!u) return 0;
    size_t b = sizeof(UnionTable) + u->view.capacity() * sizeof(Group);
    for (const Group& g : u->view) b += groupBytes(g);
    b += (u->plan.rep.capacity() + u->plan.unionOf.capacity()) * sizeof(int);
    b += u->plan.members.capacity() * sizeof(std::vector<int>);
    for (const std::vector<int>& m : u->plan.members) b += m.capacity() * sizeof(int);
    b += u->base.capacity() * sizeof(std::vector<int>);
    for (const std::vector<int>& m : u->base) b += m.capacity() * sizeof(int);
    b += (u->cellGroup.capacity() + u->buttonOf.capacity()) * sizeof(int);
    return b;
}

struct GameTables {
    std::vector<Group>* groups;
    UtMiTable* mi;
    UnionTable* unions;
    size_t groupsB, miB, unionsB;
};
UtRetireList<GameTables> g_retiredGames;
UtRetireList<UnionTable> g_retiredUnions;
unsigned g_unionReplacements = 0;
volatile LONG g_retiredBytes = 0;   // held in both lists (the memory lines read it)

double mbOf(size_t b) { return (double)b / 1048576.0; }

// The Group whose records, slot and flags the page of `group` shows: its union when it is a
// union's rep, else the group itself. `group` must be a valid index of g_groups.
Group& pageGroup(int group) {
    UnionTable* u = g_unions;
    if (u && (size_t)group < u->plan.unionOf.size()) {
        const int k = u->plan.unionOf[(size_t)group];
        if (k >= 0 && (size_t)k < u->view.size()) return u->view[(size_t)k];
    }
    return (*g_groups)[(size_t)group];
}

// The group whose page shows `group` (its union's first group; itself without one).
int unionRep(int group) {
    const UnionTable* u = g_unions;
    return u ? utUnionRep(u->plan, group) : group;
}

// The union `group` keys (-1 = none).
int unionOfRep(int group) {
    const UnionTable* u = g_unions;
    if (!u || group < 0 || (size_t)group >= u->plan.unionOf.size()) return -1;
    return u->plan.unionOf[(size_t)group];
}

// A union's OWN flags and count from its members' (after a refresh of the owned set).
void unionOwnedFill(UnionTable* u) {
    if (!u || !g_groups) return;
    for (size_t k = 0; k < u->view.size(); ++k) {
        Group& v = u->view[k];
        const std::vector<int>& m = u->plan.members[k];
        int count = 0;
        for (size_t i = 0; i < m.size(); ++i) {
            const Group& g = (*g_groups)[(size_t)m[i]];
            for (size_t r = 0; r < g.owned.size(); ++r) {
                const size_t at = (size_t)u->base[k][i] + r;
                if (at < v.owned.size()) v.owned[at] = g.owned[r];
            }
            count += g.ownedCount;
        }
        v.ownedCount = count;
    }
}

// ---- the KIND table (ut_mikind): the catalogue's monster infrequents ----------------------------
UtMiTable* volatile g_mi = nullptr;   // null = catalogue.bin not read

void loadKinds(HMODULE selfModule) {
    char path[MAX_PATH];
    if (!utGameFile(selfModule, "catalogue.bin", path, sizeof(path))) {
        logW("live: catalogue.bin not found - no record is known as a monster infrequent (a "
             "refusal and the tooltip treat every record as a unique)");
        return;
    }
    std::string err;
    UtMiTable* mi = utMiLoad(path, &err);
    if (!mi) {
        logW("live: catalogue.bin could not be read (%s) - no record is known as a monster "
             "infrequent", err.c_str());
        return;
    }
    InterlockedExchangePointer((void* volatile*)&g_mi, mi);
    logI("mi: %d monster infrequent record(s) in the catalogue - a second roll is refused naming "
         "the stored one, another roll hovered reads \"(another roll)\"", utMiCount(mi));
}
int g_pagesTotal = 0;
volatile LONG g_wantGroup = 0;
// the WINDOW's first slot row. The name stays GD's "page" in the API, the
// unit is a slot row: the wheel moves it by one, < > / PgUp / PgDn by a window (the group's rows).
volatile LONG g_wantPage = 0;
volatile LONG g_dirty = 0;
int g_shownGroup = 0;   // game thread
int g_shownPage = 0;    // the shown window's first slot row
int g_shownRows = 0;    // slot rows of the shown group (the records shown, OWN included)
int g_windowRows = 0;   // slot rows per window of the shown group
// NO settle (the earlier kWheelSettleMs / kWheelHoldMs and the held rebuild are gone). A
// wheel tick marks the window dirty like any navigation and the NEXT GameEngine::Update rebuilds it;
// ticks between two Updates coalesce into one step (ut_viewgate.h UtNavBurst). Game thread.
UtNavBurst g_nav = {0, 0};     // inputs since the last taken rebuild
UtNavBurst g_taken = {0, 0};   // what liveTakeDirty / liveDropDirty took for the rebuild now
char g_status[96] = "groups=0";
// the caravan geometry of the game in force (uniq-geometry.txt, ut_caravangeo.h); read by liveInit,
// forgotten by liveReloadGame. Game thread.
UtCaravanGeo g_geo = {};
// - the OWN filter. Game thread (the pad's click, the view tick, ut_owned's refresh).
bool g_ownedOnly = false;      // asked for (the ini's owned_only, then the OWN button)
bool g_ownKnown = false;       // the flags below come from a KNOWN owned set
bool g_ownPersist = false;     // the tick writes owned_only back
int g_shownCount = 0;          // records shown over all pages of the shown group (last build)
// - the property search (ut_search): a query stands, the groups fully indexed, and what the last
// apply found. The marks are read by the pad's draw.
bool g_searchOn = false;
unsigned g_searchIndexed = 0;
volatile LONG g_searchMarks = 0;
volatile LONG g_searchIndexedPub = 0;   // g_searchIndexed while a query stands, for the pad
volatile LONG g_searchFound = 0;
volatile LONG g_searchGroups = 0;
// the last built page: the record and its index in the shown group, per place (the highlight)
const char* g_placeRec[kUtProtoMax];
int g_placeK[kUtProtoMax];
int g_placeN = 0;

// the tail of every WARN of a game whose generated set does not load (a data fault): the view
// refuses in this game only (ut_view viewOnGameTable), the next game switch reads the next set
const char* const kGameDataOff =
    "the tab is off in this game (a data fault in its generated set; the next game switch starts "
    "clean)";

bool readAll(const char* path, std::string* out) {
    FILE* fh = nullptr;
    if (fopen_s(&fh, path, "rb") != 0 || !fh) return false;
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fh)) > 0) out->append(buf, n);
    fclose(fh);
    return !out->empty();
}

void markDirty() { InterlockedExchange(&g_dirty, 1); }

void relicSearchCount(unsigned* marks, int* found, int* groups);

// The search's counts and marks, from the match, owned and OWN state as it is now.
void searchRecompute() {
    if (!g_groups) return;
    const bool own = g_ownedOnly && g_ownKnown;
    unsigned marks = 0;
    int found = 0, groups = 0;
    for (size_t gi = 0; gi < g_groups->size(); ++gi) {
        Group& g = (*g_groups)[gi];
        const int n = (int)g.records.size();
        g.hitCount = g_searchOn ? utSearchCount(g.owned.data(), g.match.data(), n, own) : 0;
        if (!g_searchOn) continue;
        found += g.hitCount;
        if (g.hitCount > 0) ++groups;
        const bool indexed = gi < 32 && (g_searchIndexed & (1u << (unsigned)gi)) != 0;
        if (gi < 32 && utSearchGroupMarked(g.owned.data(), g.match.data(), n, own, indexed))
            marks |= 1u << (unsigned)gi;
    }
    relicSearchCount(&marks, &found, &groups);   // the relic page: its green slots' matches
    if (UnionTable* u = g_unions) {   // a union's matches: its members' (one page, one count)
        for (size_t k = 0; k < u->view.size(); ++k) {
            int hits = 0;
            for (int gi : u->plan.members[k]) hits += (*g_groups)[(size_t)gi].hitCount;
            u->view[k].hitCount = hits;
        }
    }
    InterlockedExchange(&g_searchMarks, (LONG)marks);
    InterlockedExchange(&g_searchIndexedPub, g_searchOn ? (LONG)g_searchIndexed : 0);
    InterlockedExchange(&g_searchFound, found);
    InterlockedExchange(&g_searchGroups, groups);
}

// an INPUT navigation (not a deposit / owned refresh): the burst the rebuild reports.
void noteNav(bool wheel) {
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    utNavNote(g_nav, wheel, q.QuadPart);
}

}  // namespace

void applyStateLocked();

// uniq-geometry.txt through utGameFile: the caravan geometry the game's generated set carries.
// Missing or unreadable: the base game's, said once (one WARN per game) - or, for a game whose tab
// is off (`baseIfMissing` false), nothing: its caravan geometry line then gives the measured place
// only.
static void readGeometry(HMODULE selfModule, bool baseIfMissing = true) {
    char path[MAX_PATH];
    std::string text;
    UtCaravanGeo g;
    if (utGameFile(selfModule, "uniq-geometry.txt", path, sizeof(path)) && readAll(path, &text) &&
        utCaravanGeoParse(text.data(), text.size(), &g)) {
        g_geo = g;
        const bool inside = g.x0 >= 0 && g.y0 >= 0 && g.x0 + g.w <= g.windowW && g.y0 + g.h <= g.windowH;
        logI("live: caravan geometry from uniq-geometry.txt: grid %d,%d %dx%d = %d x %d cells; cover "
             "%dx%d (%s); caravan window %dx%d, the Transfer page at %d,%d%s",
             g.x0, g.y0, g.w, g.h, g.cols, g.rows, g.plateW, g.plateH, g.plate, g.windowW, g.windowH,
             g.pageX, g.pageY, inside ? "" : " - the grid leaves the caravan window");
        return;
    }
    if (!baseIfMissing) {
        logI("live: no uniq-geometry.txt in this game's set - its caravan geometry line gives the "
             "measured place only");
        return;
    }
    g_geo = utCaravanGeoBase();
    logW("live: uniq-geometry.txt is missing or unreadable in the mod folder - the caravan geometry "
         "is the base game's (grid 27,126 512x480 = 16 x 15 cells)");
}

// ---- the relic and charm page -----------------------------------------------------------------
// uniq-relics.txt, read with the group table. The group whose G line id is "relics" shows it: its
// window starts at a BLOCK (g_wantPage), the wheel moves one block, < > a fixed page. The places'
// states (counts, the row each prototype is built from) are taken from the journal ONCE per journal
// change (journalWrites moved) when the relic page is built, never per frame. A table of a game
// left behind is never freed (as the group tables): an input or draw path may still hold it.
namespace {
struct RelicTables {
    UtRelicList list;
    UtRelicLayout layout;
    std::vector<int> stateFirst;            // per row: its first state (red, then each bonus)
    std::vector<UtRelicPlaceState> state;   // per (row, slot)
    std::vector<const char*> bonusPtr;      // the table's strings, per (row, slot); null on red
    long listedWrites = -1;                 // journalWrites() of the last relist
    std::vector<unsigned char> held;        // per block: some slot holds something (OWN)
    bool ownLaid = false;                   // the layout in force is OWN's (the held blocks only)
    std::vector<unsigned char> match;       // per state: the search's match (green slots only)
    int bonusesHeld = 0;                    // green slots holding at least one item
    unsigned pieces = 0;                    // the shards in every red slot
};
RelicTables* g_relic = nullptr;
int g_relicGroup = -1;                      // the "relics" G line's group, -1 = none
UtRelicPlace g_relicShown[kUtProtoMax];     // the shown window's places (dead cells included)
int g_relicShownN = 0;
int g_relicShownEnd = 0;                    // one past the shown window's last block
const char* const kRelicGroupId = "relics";

// The relic part of the search's counts: the green slots whose text holds the query, in the blocks
// the page lays out; the relic group's button is marked once the relic part is fully indexed.
void relicSearchCount(unsigned* marks, int* found, int* groups) {
    RelicTables* t = g_relic;
    if (!t || !g_groups || g_relicGroup < 0 || g_relicGroup >= (int)g_groups->size()) return;
    Group& g = (*g_groups)[(size_t)g_relicGroup];
    g.hitCount = g_searchOn ? utRelicSearchFound(t->list, t->stateFirst.data(), t->match.data(),
                                                 t->ownLaid ? t->held.data() : nullptr)
                            : 0;
    if (!g_searchOn) return;
    *found += g.hitCount;
    if (g.hitCount > 0) ++*groups;
    const unsigned bit = g_relicGroup < 32 ? 1u << (unsigned)g_relicGroup : 0u;
    if (bit && (g_searchIndexed & bit) && g.hitCount > 0) *marks |= bit;
}

// OWN on the relic page: in force as on the unique pages (asked for, the owned set known), and the
// journal's facts are known (they decide which blocks hold something).
bool relicOwnActive(const RelicTables* t) {
    return g_ownedOnly && g_ownKnown && t && t->listedWrites >= 0 && journalSetKnown();
}
void relicRelayout();

// the relic area's cells and the two gap settings (liveSetRelicGeometry); until the panel sets
// them, the collection's grid in force with the default gaps. A game switch forgets them (the
// next game's list is laid out on ITS area; the panel sets the area in force again)
UtRelicGeometry g_relicGeo;
bool g_relicGeoSet = false;

UtRelicGeometry relicGeometry() {
    if (g_relicGeoSet) return g_relicGeo;
    UtRelicGeometry g;
    g.cols = g_hostCols;
    g.rows = g_hostRows;
    return g;
}

void readRelics(HMODULE selfModule) {
    char path[MAX_PATH];
    if (!utGameFile(selfModule, "uniq-relics.txt", path, sizeof(path))) {
        logI("live: no uniq-relics.txt in the mod folder - no relic page");
        return;
    }
    std::string text, err;
    if (!readAll(path, &text)) {
        logW("live: uniq-relics.txt could not be read - no relic page");
        return;
    }
    RelicTables* t = new RelicTables();
    if (!utRelicListRead(text, &t->list, &err) ||
        !utRelicLayoutBuild(t->list, relicGeometry(), &t->layout)) {
        logW("live: %s - no relic page",
             err.empty() ? "the relic page does not fit the area" : err.c_str());
        delete t;
        return;
    }
    for (size_t r = 0; r < t->list.rows.size(); ++r) {
        t->stateFirst.push_back((int)t->state.size());
        t->state.push_back(UtRelicPlaceState());
        t->bonusPtr.push_back(nullptr);
        for (size_t i = 0; i < t->list.rows[r].bonuses.size(); ++i) {
            t->state.push_back(UtRelicPlaceState());
            t->bonusPtr.push_back(t->list.rows[r].bonuses[i].c_str());
        }
    }
    for (size_t k = 0; k < t->state.size(); ++k) {
        memset(&t->state[k], 0, sizeof(t->state[k]));
        t->state[k].empty = true;
    }
    if (t->list.rows.empty()) {   // collect_relics=0, or a game without relics: no relic page
        logI("live: uniq-relics.txt lists no relic or charm%s - no relic page",
             g_cfg.collectRelics ? "" : " (collect_relics=0)");
        delete t;
        return;
    }
    t->held.assign(t->list.blocks.size(), 0);
    t->match.assign(t->state.size(), 0);
    g_relic = t;
    const UtRelicGeometry used = relicGeometry();
    logI("live: uniq-relics.txt - %zu blocks (%d relic, %d charm), %zu records, %d bonus slots, "
         "%d fixed page(s) on %d x %d cells", t->list.blocks.size(), t->list.relicBlocks,
         t->list.charmBlocks, t->list.rows.size(), t->list.bonusSlots,
         utRelicPageCount(t->layout), used.cols, used.rows);
}

// The facts of every relic record, once per journal change (game thread, the page build).
void relicRelist() {
    RelicTables* t = g_relic;
    if (!t) return;
    const long w = journalWrites();
    if (!utRelicRelistDue(t->listedWrites, w)) return;
    t->listedWrites = w;
    static UtRelicRowFacts facts[512];
    char key[256];
    int held = 0;
    unsigned pieces = 0;
    for (size_t r = 0; r < t->list.rows.size(); ++r) {
        const UtRelicRow& row = t->list.rows[r];
        size_t n = 0;
        if (utJournalKey(row.folded.c_str(), key, sizeof(key))) n = journalRowsOf(key, facts, 512);
        const int got = (int)(n < 512 ? n : 512);
        static bool s_truncSaid = false;
        if (n > 512 && !s_truncSaid) {
            s_truncSaid = true;
            logW("live: %s has %zu journal rows - the relic page counts its first 512",
                 row.folded.c_str(), n);
        }
        const int first = t->stateFirst[r];
        for (int s = 0; s <= (int)row.bonuses.size(); ++s) {
            UtRelicPlaceState& st = t->state[(size_t)(first + s)];
            utRelicPlaceState(row, s, got ? facts : nullptr, got, &st);
            if (s > 0 && st.count > 0u) ++held;
            if (s == 0) pieces += st.count;
        }
    }
    t->bonusesHeld = held;
    t->pieces = pieces;
    // OWN's blocks: laid out again only when the set of held blocks moved (once per journal change)
    std::vector<unsigned char> keep(t->held.size(), 0);
    if (!keep.empty()) utRelicBlocksHeld(t->list, t->stateFirst.data(), t->state.data(), keep.data());
    if (keep != t->held) {
        t->held.swap(keep);
        if (t->ownLaid) relicRelayout();
        if (g_searchOn) searchRecompute();
    }
    logD("live: relic facts taken (journal write %ld): %d of %d bonus slots held, %u pieces", w,
         held, t->list.bonusSlots, pieces);
}

bool relicShown(int group) { return g_relic && group >= 0 && group == g_relicGroup; }

// the CSV's relic cells (ut_rescue's csvWrite, any thread: the table of the game in force)
bool relicCells(const char* key, int* tier, unsigned* shards) {
    RelicTables* t = g_relic;
    const int r = t ? utRelicFind(t->list, key) : -1;
    if (r < 0) return false;
    *tier = t->list.rows[(size_t)r].tier;
    *shards = t->list.rows[(size_t)r].shards;
    return true;
}

void relicFindGroup() {
    g_relicGroup = -1;
    if (!g_groups) return;
    for (size_t i = 0; i < g_groups->size(); ++i)
        if ((*g_groups)[i].id == kRelicGroupId) g_relicGroup = (int)i;
}

// The wheel / page step on the relic page: g_wantPage is the window's first block.
bool relicStep(int dir, bool page) {
    if (!g_relic) return false;
    const int from = (int)InterlockedCompareExchange(&g_wantPage, 0, 0);
    const UtRelicLayout& y = g_relic->layout;
    const int at = utRelicClampFirst(y, from);
    const int to = page ? utRelicPageStep(y, at, dir) : utRelicWheelStep(y, at, dir);
    if (to == from) return false;
    InterlockedExchange(&g_wantPage, to);
    noteNav(!page);
    markDirty();
    logD("live: relic page - %s, blocks from %d (page %d of %d)", page ? "page step" : "wheel",
         to + 1, utRelicPageOf(y, to) + 1, utRelicPageCount(y));
    return true;
}
}  // namespace

bool liveInit(HMODULE selfModule) {
    if (g_groups) return true;
    if (!g_geo.valid) {
        try {
            readGeometry(selfModule);
        } catch (...) {
            g_geo = utCaravanGeoBase();
            logW("live: uniq-geometry.txt could not be read (out of memory) - the caravan geometry is "
                 "the base game's");
        }
    }
    // the game's own area (its uniq-geometry.txt P line): the generator writes the G lines on it,
    // so the reader checks them on it and the first pack uses it (the next frame's gridApply
    // re-packs on the grid in force)
    const int areaCols = g_geo.valid ? g_geo.cols : gdut::kHostCols;
    const int areaRows = g_geo.valid ? g_geo.rows : gdut::kHostRows;
    char path[MAX_PATH];
    if (!utGameFile(selfModule, "uniq-groups.txt", path, sizeof(path))) {
        logW("live: uniq-groups.txt not found in the mod folder - %s", kGameDataOff);
        return false;
    }
    try {
        std::string text;
        if (!readAll(path, &text)) {
            logW("live: uniq-groups.txt could not be read - %s", kGameDataOff);
            return false;
        }
        std::vector<gdut::GroupList> lists;
        std::vector<gdut::PackedGroup> packed;
        std::string err;
        if (!gdut::readGameGroups(text, areaCols, areaRows, &lists, &packed, &err)) {
            logW("live: %s (the game's area %d x %d cells) - %s", err.c_str(), areaCols, areaRows,
                 kGameDataOff);
            return false;
        }
        g_hostCols = areaCols;
        g_hostRows = areaRows;
        std::vector<Group>* gs = new std::vector<Group>();
        size_t entries = 0;
        for (size_t li = 0; li < lists.size(); ++li) {
            const gdut::GroupList& l = lists[li];
            Group g;
            g.label = l.label;
            g.id = l.id;
            g.tag = l.tag;
            for (size_t k = 0; k < l.entries.size(); ++k) {
                g.records.push_back(l.entries[k].record);
                g.fw.push_back((unsigned char)l.entries[k].w);
                g.fh.push_back((unsigned char)l.entries[k].h);
            }
            g.placed.swap(packed[li].placed);
            g.slot = packed[li].slot;
            g.pages = packed[li].pages;
            g.owned.assign(l.entries.size(), 0);
            g.match.assign(l.entries.size(), 0);
            logD("live: group %s (%s %s) - %zu records, slot %dx%d, %d x %d = %d per page, %d page(s)",
                 l.label.c_str(), l.id.c_str(), l.tag.c_str(), l.entries.size(), g.slot.w, g.slot.h, g.slot.cols, g.slot.rows,
                 g.slot.perPage(), g.pages);
            entries += l.entries.size();
            g_pagesTotal += g.pages;
            gs->push_back(g);
        }
        g_groups = gs;
        readRelics(selfModule);
        relicFindGroup();
        journalSetRelicCells(&relicCells);
        g_ownedOnly = g_cfg.ownedOnly != 0;   // the start state (configReload ran before)
        applyStateLocked();
        loadKinds(selfModule);
        logI("live: %zu groups, %zu records on %d pages of %d x %d, one slot size per group "
             "(uniq-groups.txt)", gs->size(), entries, g_pagesTotal, g_hostCols, g_hostRows);
        return true;
    } catch (...) {
        logW("live: out of memory building the group table - %s", kGameDataOff);
        return false;
    }
}

bool liveActive() { return g_groups != nullptr && !g_groups->empty(); }

size_t liveRetiredBytes() { return (size_t)(unsigned long)InterlockedCompareExchange(&g_retiredBytes, 0, 0); }

// GD's re-fit (ut_live.cpp layoutCompute: "every group re-fitted") on TQ's pages: every group is
// packed again on the new cells (all of them or none: one that cannot be packed keeps the grid in
// force), every several-group button's page takes the new slot counts, and the shown window keeps
// its first visible item (GD's utRowKeep over the slot columns). Game thread; allocates only on a
// change.
namespace {
// the relic layout on the grid in force (game thread); the window's first block is kept
void relicRelayout() {
    RelicTables* t = g_relic;
    if (!t) return;
    const bool own = relicOwnActive(t);
    UtRelicLayout y;
    if (!utRelicLayoutBuild(t->list, relicGeometry(), &y, own ? t->held.data() : nullptr)) return;
    t->layout = y;
    if (own != t->ownLaid) {
        t->ownLaid = own;
        if (g_searchOn) searchRecompute();   // the found count follows the blocks shown
    }
}
}  // namespace

bool liveSetRelicGeometry(int cols, int rows, int blockGap, int bonusGap) {
    if (cols < 1 || rows < 1) return false;
    const UtRelicGeometry g = utRelicGeometryOf(cols, rows, blockGap, bonusGap);
    if (g_relicGeoSet && utRelicGeometrySame(g, g_relicGeo)) return false;
    g_relicGeo = g;
    g_relicGeoSet = true;
    relicRelayout();
    markDirty();
    return true;
}

bool liveSetGrid(int cols, int rows) {
    if (cols < 1 || rows < 1 || (cols == g_hostCols && rows == g_hostRows)) return false;
    std::vector<Group>* gs = g_groups;
    if (!gs) {
        g_hostCols = cols;
        g_hostRows = rows;
        return true;
    }
    const int oldSlotCols = (size_t)g_shownGroup < gs->size() ? pageGroup(g_shownGroup).slot.cols : 0;
    int total = 0;
    try {
        std::vector<std::vector<gdut::PackItem>> items(gs->size());
        for (size_t i = 0; i < gs->size(); ++i) {
            const Group& g = (*gs)[i];
            items[i].reserve(g.records.size());
            for (size_t k = 0; k < g.records.size(); ++k)
                items[i].push_back(gdut::PackItem{(int)k, (int)g.fw[k], (int)g.fh[k]});
        }
        UtGridPack pk;
        if (!utGridPackAll(items, cols, rows, &pk)) {
            const size_t f = pk.failed >= 0 ? (size_t)pk.failed : 0;
            logW("live: group %s cannot be packed into %d x %d pages - the grid in force stays "
                 "%d x %d", f < gs->size() ? (*gs)[f].label.c_str() : "?", cols, rows, g_hostCols,
                 g_hostRows);
            return false;
        }
        for (size_t i = 0; i < gs->size(); ++i) {
            Group& g = (*gs)[i];
            g.placed.swap(pk.placed[i]);
            g.slot = pk.slot[i];
            g.pages = pk.pages[i];
        }
        total = pk.total;
    } catch (...) {
        logW("live: out of memory packing the groups into %d x %d pages - the grid in force stays "
             "%d x %d", cols, rows, g_hostCols, g_hostRows);
        return false;
    }
    g_hostCols = cols;
    g_hostRows = rows;
    g_pagesTotal = total;
    relicRelayout();   // the relic page follows the grid in force
    if (UnionTable* u = g_unions) {
        for (Group& v : u->view) v.pages = utGridUnionSlots(&v.slot, cols, rows, (int)v.records.size());
    }
    const int newSlotCols = (size_t)g_shownGroup < gs->size() ? pageGroup(g_shownGroup).slot.cols : 0;
    const int row = (int)InterlockedCompareExchange(&g_wantPage, 0, 0);
    if (row > 0 && oldSlotCols > 0 && newSlotCols > 0)
        InterlockedExchange(&g_wantPage, utRowKeep(row, oldSlotCols, newSlotCols));
    markDirty();
    logI("live: the grid in force is %d x %d cells - every group re-packed (%d pages)%s", cols, rows,
         total, oldSlotCols > 0 ? "; the page on screen is laid out again from its first visible item" : "");
    return true;
}

void liveGridCells(int* cols, int* rows) {
    if (cols) *cols = g_hostCols;
    if (rows) *rows = g_hostRows;
}

bool liveMaxSlot(int* w, int* h) {
    if (!g_groups || g_groups->empty()) return false;
    int mw = 0, mh = 0;
    for (const Group& g : *g_groups) {
        if (g.slot.w > mw) mw = g.slot.w;
        if (g.slot.h > mh) mh = g.slot.h;
    }
    if (w) *w = mw;
    if (h) *h = mh;
    return mw > 0 && mh > 0;
}

namespace {
long g_stateGame = 0;     // configGameGeneration() the state was taken from
int g_savedGroup = -2;    // what uniquetab.state says now (-2 = not known yet)
int g_savedRow = -1;
DWORD g_savedAt = 0;
long g_unionsGame = -1;   // configGameGeneration() the union table was resolved for
UtRowRestore g_restore = {-1, 0, false};   // the last restore of the state, and a move since
void unionsWantOnKey();
}  // namespace

// g_cfg's state (the game's uniquetab.state) into the wanted group, row and OWN switch.
void applyStateLocked() {
    g_stateGame = configGameGeneration();
    g_ownedOnly = g_cfg.ownedOnly != 0;
    const int n = g_groups ? (int)g_groups->size() : 0;
    // the saved group itself: its page's key comes from THIS game's buttons (unionsWantOnKey, at
    // the resolve of the game's buttons file, or at once when they are already in force)
    const int group = g_cfg.uniqGroup >= 0 && g_cfg.uniqGroup < n ? g_cfg.uniqGroup : 0;
    InterlockedExchange(&g_wantGroup, group);
    InterlockedExchange(&g_wantPage, g_cfg.uniqRow > 0 ? g_cfg.uniqRow : 0);
    g_savedGroup = g_cfg.uniqGroup;
    g_savedRow = g_cfg.uniqRow;
    utRowRestoreArm(g_restore, g_cfg.uniqGroup, g_cfg.uniqRow);
}

void liveFollowGame() {
    if (!liveActive() || configGameGeneration() == g_stateGame) return;
    applyStateLocked();
    if (g_unionsGame == configGameGeneration()) unionsWantOnKey();   // this game's buttons are in force
    noteNav(false);
    markDirty();
    logI("live: the game's state is in force - group %s, row %d, OWN %s (uniquetab.state)",
         liveGroupLabel(liveWantedGroup()), (int)InterlockedCompareExchange(&g_wantPage, 0, 0) + 1,
         g_ownedOnly ? "ON" : "OFF");
}

namespace {
// The group table in force, read ONCE: the buttons worker calls the accessors below while the game
// thread may switch the table (liveReloadGame sets g_groups to null, then to the next game's). One
// volatile load per call, so the check and the use see the same table; a table it saw stays alive
// two switches (the retire list), past the end of the call.
std::vector<Group>* groupsNow() {
    return *(std::vector<Group>* volatile const*)&g_groups;
}
}  // namespace

int liveGroupCount() {
    const std::vector<Group>* gs = groupsNow();
    return gs ? (int)gs->size() : 0;
}

const char* liveGroupLabel(int group) {
    const std::vector<Group>* gs = groupsNow();
    if (!gs || group < 0 || group >= (int)gs->size()) return "?";
    return (*gs)[(size_t)group].label.c_str();
}

const char* liveGroupId(int group) {
    const std::vector<Group>* gs = groupsNow();
    if (!gs || group < 0 || group >= (int)gs->size()) return "";
    return (*gs)[(size_t)group].id.c_str();
}

const char* liveGroupTag(int group) {
    const std::vector<Group>* gs = groupsNow();
    if (!gs || group < 0 || group >= (int)gs->size()) return "";
    return (*gs)[(size_t)group].tag.c_str();
}

int liveGroupEntries(int group) {
    const std::vector<Group>* gs = groupsNow();
    if (!gs || group < 0 || group >= (int)gs->size()) return -1;
    return (int)(*gs)[(size_t)group].records.size();
}

const char* liveGroupRecord(int group, int k) {
    const std::vector<Group>* gs = groupsNow();
    if (!gs || group < 0 || group >= (int)gs->size()) return nullptr;
    const Group& g = (*gs)[(size_t)group];
    return (k >= 0 && k < (int)g.records.size()) ? g.records[(size_t)k].c_str() : nullptr;
}

int livePageCount(int group) {
    if (!g_groups || group < 0 || group >= (int)g_groups->size()) return 0;
    if (relicShown(group)) return utRelicPageCount(g_relic->layout);
    const Group& g = pageGroup(group);
    return liveOwnedOnlyActive() ? gdut::pagesFor(g.slot, g.ownedCount) : g.pages;
}

namespace {
// the records the wanted group shows now (OWN: its owned count) and the offset clamp.
int shownOf(const Group& g) {
    return liveOwnedOnlyActive() ? g.ownedCount : (int)g.records.size();
}
bool stepRows(int delta, const char* what, bool wheel = false) {
    if (!liveActive()) return false;
    const int gi = liveWantedGroup();
    const Group& g = pageGroup(gi);
    const int from = (int)InterlockedCompareExchange(&g_wantPage, 0, 0);
    const int to = gdut::clampRowOffset(g.slot, shownOf(g), from + delta);
    if (to == from) return false;
    InterlockedExchange(&g_wantPage, to);
    utRowRestoreMoved(g_restore);
    noteNav(wheel);   // before the dirty flag, so the rebuild that takes it reports it
    markDirty();
    logD("live: %s - %s rows %d-%d of %d (from row %d)", what, g.label.c_str(), to + 1,
         to + g.slot.rows, gdut::slotRowsFor(g.slot, shownOf(g)), from + 1);
    return true;
}
}  // namespace

bool liveOwnedOnly() { return g_ownedOnly; }
bool liveOwnedOnlyActive() { return g_ownedOnly && g_ownKnown; }

bool liveToggleOwnedOnly() {
    if (!liveActive()) return false;
    g_ownedOnly = !g_ownedOnly;
    g_ownPersist = true;
    if (g_searchOn) searchRecompute();   // the counts and the marks follow OWN
    InterlockedExchange(&g_wantPage, 0);   // GD: a filter change starts the group at its top
    utRowRestoreMoved(g_restore);
    noteNav(false);
    markDirty();
    logI("live: OWN %s - %s", g_ownedOnly ? "ON" : "OFF",
         !g_ownedOnly ? "every record of the group"
         : g_ownKnown ? "only the records you own"
                      : "asked for; the owned set is not known yet, every record is shown");
    return true;
}

void livePersistOwnedOnly() {
    if (g_ownPersist) {
        g_ownPersist = false;
        if (!configPersistInt("owned_only", g_ownedOnly ? 1 : 0))
            logI("live: owned_only=%d could not be written to uniquetab.state (kept for this "
                 "session)", g_ownedOnly ? 1 : 0);
    }
    // the group and the row the collection is on, at most once a second while they change, and
    // only for the game whose state is in force (a switch re-reads it first: liveFollowGame)
    if (!liveActive() || configGameGeneration() != g_stateGame) return;
    const int group = liveWantedGroup();
    const int row = (int)InterlockedCompareExchange(&g_wantPage, 0, 0);
    if (group == g_savedGroup && row == g_savedRow) return;
    const DWORD now = GetTickCount();
    if (now - g_savedAt < 1000) return;
    g_savedAt = now;
    g_savedGroup = group;
    g_savedRow = row;
    char g[16], r[16];
    _snprintf_s(g, sizeof(g), _TRUNCATE, "%d", group);
    _snprintf_s(r, sizeof(r), _TRUNCATE, "%d", row);
    const char* keys[2] = {"uniq_group", "uniq_row"};
    const char* values[2] = {g, r};
    if (!configPersistStr(keys, values, 2))
        logD("live: uniq_group=%d uniq_row=%d could not be written to uniquetab.state", group, row);
}

void liveOwnedChanged() {
    if (!liveActive()) return;
    const bool known = ownedKnown();
    bool changed = known != g_ownKnown;
    for (Group& g : *g_groups) {
        int count = 0;
        for (size_t k = 0; k < g.records.size(); ++k) {
            const unsigned char f = known && ownedRecordState(g.records[k].c_str()) == 1 ? 1 : 0;
            if (f != g.owned[k]) changed = true;
            g.owned[k] = f;
            count += f;
        }
        g.ownedCount = count;
    }
    unionOwnedFill(g_unions);   // a union's flags are its members'
    g_ownKnown = known;
    if (changed && g_searchOn) searchRecompute();   // the marks follow the owned set
    if (changed && g_ownedOnly) markDirty();   // the filtered page follows the set
}

bool liveEmptyPageOk() { return liveOwnedOnlyActive() && g_shownCount == 0; }

void liveSearchSetMatch(int group, int k, bool match) {
    if (!g_groups || group < 0 || group >= (int)g_groups->size()) return;
    Group& g = (*g_groups)[(size_t)group];
    if (k >= 0 && k < (int)g.match.size()) g.match[(size_t)k] = match ? 1 : 0;
    if (UnionTable* u = g_unions) {   // the union holding the group, at the member's offset
        const int rep = utUnionRep(u->plan, group);
        const int ui = unionOfRep(rep);
        if (ui >= 0 && k >= 0 && k < (int)g.match.size()) {
            const std::vector<int>& m = u->plan.members[(size_t)ui];
            for (size_t i = 0; i < m.size(); ++i)
                if (m[i] == group) {
                    const size_t at = (size_t)u->base[(size_t)ui][i] + (size_t)k;
                    Group& v = u->view[(size_t)ui];
                    if (at < v.match.size()) v.match[at] = match ? 1 : 0;
                }
        }
    }
}

void liveSearchApply(bool active, unsigned indexedMask) {
    if (!liveActive()) return;
    g_searchOn = active;
    g_searchIndexed = indexedMask;
    searchRecompute();   // the page stays as it is: the highlight is drawn per frame
}

bool liveSearchHighlight(int i, const char* record) {
    if (relicShown(g_shownGroup)) {   // a green slot whose (record, bonus) text holds the query
        RelicTables* t = g_relic;
        int slot = 0;
        const int r = record ? utRelicFind(t->list, record) : -1;
        if (r < 0 || !protoSlotKindAt(i, &slot) || slot < 2) return false;
        return utRelicSearchHit(t->match.data(), t->stateFirst[(size_t)r] + slot - 1, slot - 1,
                                g_searchOn);
    }
    if (!g_searchOn || !g_groups || !record || g_shownGroup < 0 ||
        g_shownGroup >= (int)g_groups->size())
        return false;
    const Group& g = pageGroup(g_shownGroup);
    const int k = utSearchPlaceIndex(g_placeRec, g_placeK, g_placeN, i, record);
    return utSearchHighlight(g.match.data(), (int)g.match.size(), k, true);
}

unsigned liveSearchMarks() { return (unsigned)InterlockedCompareExchange(&g_searchMarks, 0, 0); }

unsigned liveSearchIndexed() {
    return (unsigned)InterlockedCompareExchange(&g_searchIndexedPub, 0, 0);
}

int liveSearchFound(int* groups) {
    if (groups) *groups = (int)InterlockedCompareExchange(&g_searchGroups, 0, 0);
    return (int)InterlockedCompareExchange(&g_searchFound, 0, 0);
}

int liveSearchShownIn(int group, int* of) {
    if (of) *of = liveGroupEntries(group);
    if (!g_groups || group < 0 || group >= (int)g_groups->size()) return 0;
    if (g_relic && group == g_relicGroup) {   // the relic page: its laid-out green slots, no records
        const RelicTables* t = g_relic;
        const int green = utRelicGreenSlots(t->list, t->ownLaid ? t->held.data() : nullptr);
        if (of) *of = green;
        return g_searchOn ? (*g_groups)[(size_t)group].hitCount : green;
    }
    const Group& g = pageGroup(group);   // a union's rep: the whole page's
    if (of) *of = (int)g.records.size();
    return g_searchOn ? g.hitCount : (int)g.records.size();
}

int livePagesTotal() { return g_pagesTotal; }
int liveShownGroup() { return g_shownGroup; }
int liveShownPage() { return g_shownPage; }
int liveWantedGroup() { return (int)InterlockedCompareExchange(&g_wantGroup, 0, 0); }

bool liveSelectGroup(int group) {
    if (!liveActive() || group < 0 || group >= liveGroupCount()) return false;
    InterlockedExchange(&g_wantGroup, unionRep(group));   // its button's page
    InterlockedExchange(&g_wantPage, 0);
    utRowRestoreMoved(g_restore);
    noteNav(false);
    markDirty();
    return true;
}

// a WINDOW step (< > / PgUp / PgDn): the offset moves by the group's slot rows per window,
// clamped - so the last step lands on the last FULL window (it overlaps the one before it).
bool liveStepPage(int dir) {
    if (!liveActive()) return false;
    if (relicShown(liveWantedGroup())) return relicStep(dir < 0 ? -1 : 1, true);
    const Group& g = pageGroup(liveWantedGroup());
    return stepRows((dir < 0 ? -1 : 1) * (g.slot.rows > 0 ? g.slot.rows : 1), "window step");
}

// one slot row (the wheel over the page). rebuilt on the next Update, no settle.
bool liveStepRow(int dir) {
    if (liveActive() && relicShown(liveWantedGroup())) return relicStep(dir < 0 ? -1 : 1, false);
    return stepRows(dir < 0 ? -1 : 1, "wheel", true);
}

bool liveHandleWheel(int ticks, bool ctrl) {
    if (!liveActive() || ticks == 0) return false;
    // ut_panel's cycleStep owns the Ctrl cycle (Transfer is a stop) and calls this
    // with ctrl=false only; the Ctrl branch is kept for GD's signature and answers false at the ends.
    if (ctrl) {   // the groups inside the cycle; the ends go to Transfer (ut_panel's step)
        const int to = utGroupCycleStep(liveWantedGroup(), liveGroupCount(), ticks > 0 ? -1 : 1);
        return to >= 0 && liveSelectGroup(to);
    }
    liveStepRow(ticks > 0 ? -1 : 1);   // one slot row; consumed even at the end, as GD's wheel
    return true;
}

bool liveHandleKey(int vk, bool ctrl) {
    if (vk != VK_PRIOR && vk != VK_NEXT) return false;
    if (ctrl) return liveHandleWheel(vk == VK_PRIOR ? 1 : -1, true);
    liveStepPage(vk == VK_PRIOR ? -1 : 1);   // PgUp / PgDn move a whole window
    return true;
}

bool liveTakeDirty() {
    // the relic page also rebuilds when the journal moved under it (a save settled its rows)
    if (InterlockedExchange(&g_dirty, 0) == 0 && !liveRelicStale()) return false;
    g_taken = utNavTake(g_nav);   // every input since the last Update, in this one step
    return true;
}

void liveDropDirty() {
    g_taken = utNavTake(g_nav);   // the view's own build (ON) is the input's window: it reports it
    InterlockedExchange(&g_dirty, 0);
}

bool liveRebuildInput(int* wheelTicks, long long* firstQpc) {
    const UtNavBurst t = utNavTake(g_taken);   // reported once
    if (wheelTicks) *wheelTicks = t.ticks;
    if (firstQpc) *firstQpc = t.firstQpc;
    return t.firstQpc != 0;
}

int liveShownRows() { return g_shownRows; }
int liveWindowRows() { return g_windowRows; }

int liveLeftover(UtCellRect* out, int cap) {
    if (!liveActive() || !out || cap < 3) return 0;
    const Group& g = pageGroup(g_shownGroup);
    gdut::CellRect r[3];
    const int n = gdut::windowLeftover(g.slot, g_shownCount, g_shownPage, g_hostCols,
                                       g_hostRows, r);
    for (int i = 0; i < n; ++i) out[i] = UtCellRect{r[i].col, r[i].row, r[i].w, r[i].h};
    return n;
}

void liveMarkDirty() { markDirty(); }

int liveRecordKind(const char* record) { return utMiKind(g_mi, record); }

bool liveRecordFacts(const char* record, char* name, size_t nameCap, int* level) {
    return utMiFacts(g_mi, record, name, nameCap, level);
}

int liveMiCount() { return utMiCount(g_mi); }

const UtMiTable* liveMiTable() { return g_mi; }

bool liveHasRecord(const char* folded) {
    if (!g_groups || !folded || !*folded) return false;
    for (size_t g = 0; g < g_groups->size(); ++g) {
        const std::vector<std::string>& r = (*g_groups)[g].records;
        for (size_t k = 0; k < r.size(); ++k) {
            // the catalogue's paths are already lower case with '/' (uniq-groups.txt)
            if (_stricmp(r[k].c_str(), folded) == 0) return true;
        }
    }
    return false;
}

int livePagePlaces(UtProtoPlace* out, int cap) {
    if (!liveActive()) return 0;
    const int g = liveWantedGroup();
    if (relicShown(g)) {
        // the relic page: the window of whole blocks from g_wantPage, its places' states from
        // the facts of the last journal change (taken here when the journal moved)
        RelicTables* t = g_relic;
        relicRelist();
        if (relicOwnActive(t) != t->ownLaid) relicRelayout();   // OWN flipped, or its facts came
        const int asked = (int)InterlockedCompareExchange(&g_wantPage, 0, 0);
        const int first = utRelicClampFirst(t->layout, asked);
        if (first != asked) InterlockedExchange(&g_wantPage, first);
        g_relicShownN = utRelicWindowPlaces(t->list, t->layout, first, g_relicShown, kUtProtoMax);
        g_relicShownEnd = utRelicWindowEnd(t->layout, first);
        int n = 0;
        for (int i = 0; i < g_relicShownN && n < cap; ++i) {
            const UtRelicPlace& rp = g_relicShown[i];
            if (rp.kind == kUtRelicSlotDead) continue;   // no prototype on a dead cell
            const UtRelicRow& row = t->list.rows[(size_t)rp.row];
            const size_t k = (size_t)(t->stateFirst[(size_t)rp.row] + rp.slot);
            const UtRelicPlaceState& st = t->state[k];
            UtProtoPlace& p = out[n++];
            memset(&p, 0, sizeof(p));
            p.record = row.folded.c_str();
            p.col = p.slotCol = rp.col;
            p.row = p.slotRow = rp.line;
            p.w = p.h = p.slotW = p.slotH = 1;
            p.slot = utRelicProtoSlot(rp.slot);
            p.seq = st.empty ? 0ull : st.seq;
            p.var1 = st.protoVar1;
            p.bonus = rp.slot > 0 ? t->bonusPtr[k] : "";
        }
        g_placeN = 0;   // no search highlight on the relic page
        g_shownCount = n;
        g_shownGroup = g;
        g_shownPage = first;
        g_shownRows = (int)t->layout.blockLines.size();   // the relic page counts blocks (OWN: held)
        g_windowRows = g_relicShownEnd - first;
        _snprintf_s(g_status, sizeof(g_status), _TRUNCATE,
                    "groups=%d pages=%d shown=%d/%d relic blocks %d-%d", liveGroupCount(),
                    g_pagesTotal, g, first, first + 1, g_relicShownEnd);
        return n;
    }
    int page = (int)InterlockedCompareExchange(&g_wantPage, 0, 0);
    const Group& gr = pageGroup(g);   // a union: its members' records in one page
    // one pure placement for both views - every record, or the OWN filter's owned ones in
    // the same slots (gdut::placePage; packSlots is its unfiltered special case, model_test).
    // `page` is the window's first SLOT ROW, clamped so the window stays full (the model's
    // placeWindow / clampRowOffset, model_test "window").
    const bool own = liveOwnedOnlyActive();
    page = gdut::clampRowOffset(gr.slot, own ? gr.ownedCount : (int)gr.records.size(), page);
    if (page != (int)InterlockedCompareExchange(&g_wantPage, 0, 0))
        InterlockedExchange(&g_wantPage, page);   // the filter shrank the group: clamp
    static gdut::PackPlacement placed[kUtProtoMax];
    const int want = cap < kUtProtoMax ? cap : kUtProtoMax;
    const int m = gdut::placeWindow(gr.slot, gr.fw.data(), gr.fh.data(), (int)gr.records.size(),
                                    own ? gr.owned.data() : nullptr,
                                    page, placed, want,
                                    &g_shownCount);
    int n = 0;
    for (int i = 0; i < m; ++i) {
        const gdut::PackPlacement& p = placed[i];
        out[n].record = gr.records[(size_t)p.index].c_str();
        out[n].col = p.col;
        out[n].row = p.row;
        out[n].w = gr.fw[(size_t)p.index];
        out[n].h = gr.fh[(size_t)p.index];
        out[n].slotCol = p.slotCol;
        out[n].slotRow = p.slotRow;
        out[n].slotW = gr.slot.w;
        out[n].slotH = gr.slot.h;
        out[n].slot = 0;   // a unique's place (the caller's array may hold a relic page's)
        out[n].seq = 0;
        out[n].var1 = 0;
        out[n].bonus = nullptr;
        g_placeRec[n] = out[n].record;   // the highlight's record -> index map
        g_placeK[n] = p.index;
        ++n;
    }
    g_placeN = n;
    g_shownGroup = g;
    g_shownPage = page;
    g_shownRows = gdut::slotRowsFor(gr.slot, g_shownCount);
    g_windowRows = gr.slot.rows;
    _snprintf_s(g_status, sizeof(g_status), _TRUNCATE, "groups=%d pages=%d shown=%d/%d",
                liveGroupCount(), g_pagesTotal, g, page);
    return n;
}

// GAME THREAD (ut_panel's resolve of the buttons file). Builds the unions from the buttons'
// groups; with no several-group button it says nothing, as before. The wanted group moves to its
// union's first group (the first visible item kept while OWN is off) and the page is rebuilt when
// the page on screen now holds another set of groups.
void liveSetButtons(const int* buttonOf, int n) {
    if (!liveActive()) return;
    const int groups = (int)g_groups->size();
    std::vector<int> bo((size_t)groups, -1);
    for (int g = 0; g < groups && g < n; ++g) bo[(size_t)g] = buttonOf ? buttonOf[g] : g;
    UnionTable* old = g_unions;
    if (old && old->buttonOf == bo) {   // the same map: no new table; the wanted group still
        g_unionsGame = configGameGeneration();   // goes on its page's key
        unionsWantOnKey();
        return;
    }
    UnionTable* u = nullptr;
    try {
        u = new UnionTable();
        u->buttonOf = bo;
        bool relicJoined = false;   // the relic page's group never joins a union
        u->plan = utUnionPlan(groups, [&](int g, int* out, int cap) {
            const int b = bo[(size_t)g];
            int k = 0;
            if (b < 0 || g == g_relicGroup) return 0;
            for (int x = 0; x < groups && k < cap; ++x)
                if (bo[(size_t)x] == b) {
                    if (x == g_relicGroup) {
                        relicJoined = true;
                        continue;
                    }
                    out[k++] = x;
                }
            return k;
        });
        if (relicJoined)
            logW("live: a button joins %s with other groups - the relic page stands alone, the "
                 "button's other groups share a page without it", kRelicGroupId);
        std::vector<UtCell> cells;   // the table's distinct slot sizes
        for (const Group& g : *g_groups) {
            bool have = false;
            for (const UtCell& c : cells) have = have || (c.w == g.slot.w && c.h == g.slot.h);
            if (!have) cells.push_back(UtCell{g.slot.w, g.slot.h});
        }
        for (const std::vector<int>& m : u->plan.members) {
            Group v;
            const Group& first = (*g_groups)[(size_t)m[0]];
            v.id = first.id;
            v.tag = first.tag;
            std::vector<UtCell> mc;
            std::vector<int> base;
            std::string ids;   // for the log line: "0 head, 1 chest"
            for (size_t i = 0; i < m.size(); ++i) {
                const Group& g = (*g_groups)[(size_t)m[i]];
                ids += (i ? ", " : "") + std::to_string(m[i]) + (g.id.empty() ? "" : " " + g.id);
                if (i) v.label += " + ";
                v.label += g.label;
                mc.push_back(UtCell{g.slot.w, g.slot.h});
                base.push_back((int)v.records.size());
                v.records.insert(v.records.end(), g.records.begin(), g.records.end());
                v.fw.insert(v.fw.end(), g.fw.begin(), g.fw.end());
                v.fh.insert(v.fh.end(), g.fh.begin(), g.fh.end());
                v.owned.insert(v.owned.end(), g.owned.begin(), g.owned.end());
                v.match.insert(v.match.end(), g.match.begin(), g.match.end());
                v.ownedCount += g.ownedCount;
                v.hitCount += g.hitCount;
            }
            int exact = 0;
            const UtCell c = utUnionCell(mc, cells, &exact);
            v.slot.w = c.w;
            v.slot.h = c.h;
            v.slot.cols = c.w > 0 ? g_hostCols / c.w : 0;
            v.slot.rows = c.h > 0 ? g_hostRows / c.h : 0;
            v.pages = gdut::pagesFor(v.slot, (int)v.records.size());
            int cellGroup = -1;   // the member whose slot the page takes (its slot art)
            for (int gi : m)
                if (cellGroup < 0 && (*g_groups)[(size_t)gi].slot.w == c.w && (*g_groups)[(size_t)gi].slot.h == c.h)
                    cellGroup = gi;
            if (u->view.size() < 8) {
                logI("live: one grid per button - \"%s\" (groups %s, %zu items) is one page: slots "
                     "%dx%d, %d x %d%s",
                     v.label.c_str(), ids.c_str(), v.records.size(), v.slot.w, v.slot.h, v.slot.cols,
                     v.slot.rows,
                     exact ? "" : " - no group of this game has that slot, so the page shows the "
                                  "plain plate");
            }
            u->view.push_back(v);
            u->base.push_back(base);
            u->cellGroup.push_back(cellGroup);
        }
        if (u->view.size() > 8)
            logI("live: one grid per button - and %zu more button(s) of several groups", u->view.size() - 8);
        if (u->view.empty() && old && !old->view.empty())
            logI("live: one grid per button - no button holds several groups any more");
    } catch (...) {
        delete u;
        return;   // nothing changes; the next resolve tries again
    }
    const int cur = g_shownGroup;
    const std::vector<int> before = old ? utUnionMembers(old->plan, cur) : std::vector<int>(1, cur);
    g_unions = u;   // the previous table is retired below, after its last read here
    const std::vector<int> after = utUnionMembers(u->plan, cur);
    if (before != after) markDirty();
    g_unionsGame = configGameGeneration();
    // GD: the restored row comes back when the restored group's page has just become a union and
    // nothing has moved the page since the restore (a page built before this resolve clamped the row
    // to the group's own rows); the page build clamps it against the union (ut_unionrow.h)
    const int w = liveWantedGroup();
    if (w >= 0 && w < groups && g_stateGame == g_unionsGame) {
        const int row = (int)InterlockedCompareExchange(&g_wantPage, 0, 0);
        const int to = utUnionRestoredRow(g_restore, w, row, (int)utUnionMembers(u->plan, w).size(),
                                          old ? (int)utUnionMembers(old->plan, w).size() : 1);
        if (to != row) {
            logD("live: the saved row %d of group %d is restored on its button's page (one grid per "
                 "button; an earlier page clamped it to the group's own rows)", to + 1, w);
            InterlockedExchange(&g_wantPage, to);
            markDirty();
        }
    }
    unionsWantOnKey();
    if (old) {
        const size_t b = unionBytes(old);
        const UtRetired<UnionTable> out = g_retiredUnions.retire(old, ++g_unionReplacements, b);
        InterlockedExchangeAdd(&g_retiredBytes, (LONG)b);
        if (out.table) {
            InterlockedExchangeAdd(&g_retiredBytes, -(LONG)out.bytes);
            logD("live: the button pages' table retired at replacement #%u freed (%.2f MB)", out.when,
                 mbOf(out.bytes));
            delete out.table;
        }
    }
}

namespace {
// The wanted group on its page's key; the first visible item kept while OWN is off (GD's
// unionsSync step). Run at every resolve of the buttons file, the map changed or not, and at a
// game switch whose buttons are already resolved - never through another game's unions.
void unionsWantOnKey() {
    const UnionTable* u = g_unions;
    if (!u || !liveActive()) return;
    const int groups = (int)g_groups->size();
    const int want = liveWantedGroup();
    if (want < 0 || want >= groups) return;
    const int rep = utUnionRep(u->plan, want);
    if (rep == want) return;
    int row = 0;
    if (!liveOwnedOnlyActive()) {
        const std::vector<int> m = utUnionMembers(u->plan, rep);
        row = utUnionRowOf(m, liveGroupEntries, want, (int)InterlockedCompareExchange(&g_wantPage, 0, 0),
                           (*g_groups)[(size_t)want].slot.cols, pageGroup(rep).slot.cols);
    }
    logD("live: group %d is shown on the page of its button's first group %d (one grid per button) "
         "- row %d", want, rep, row + 1);
    InterlockedExchange(&g_wantGroup, rep);
    InterlockedExchange(&g_wantPage, row);
    markDirty();
}
}  // namespace

int liveUnionRep(int group) { return g_groups ? unionRep(group) : group; }

int liveShownMembers(int* out, int cap) { return liveMembersOf(g_shownGroup, out, cap); }

int liveMembersOf(int group, int* out, int cap) {
    if (!liveActive() || !out || cap < 1) return 0;
    const UnionTable* u = g_unions;
    const int g = group;
    if (g < 0 || g >= (int)g_groups->size()) return 0;
    if (!u) {
        out[0] = g;
        return 1;
    }
    const std::vector<int> m = utUnionMembers(u->plan, g);
    int n = 0;
    for (int x : m)
        if (n < cap) out[n++] = x;
    return n;
}

int liveShownCellGroup() {
    if (!liveActive()) return -1;
    const int g = g_shownGroup;
    const int ui = unionOfRep(g);
    if (ui < 0) return g;
    const UnionTable* u = g_unions;
    return u && (size_t)ui < u->cellGroup.size() ? u->cellGroup[(size_t)ui] : -1;
}

bool liveShownSlot(int* w, int* h) {
    const std::vector<Group>* gs = groupsNow();
    const int g = liveShownCellGroup();
    if (!gs || g < 0 || g >= (int)gs->size()) return false;
    const Group& gr = (*gs)[(size_t)g];
    if (gr.slot.w < 1 || gr.slot.h < 1) return false;
    if (w) *w = gr.slot.w;
    if (h) *h = gr.slot.h;
    return true;
}

const char* liveStatus() { return g_status; }

// The game in force switched (ut_generate.cpp; the game thread, no world loaded): the group table,
// the KIND table and the union table become the new game's, read from its files (utGameFile). The
// old tables are retired, not freed (the draw path reads them without a lock): the two-deep retire
// list frees them two switches later (ut_retire.h). A game whose tab is off gets no table at all.
bool liveCaravanGeometry(UtCaravanGeo* out) {
    if (!out || !g_geo.valid) return false;
    *out = g_geo;
    return true;
}

// The caravan window and the Transfer page's place in it, record px: the game's own (the W line of
// its uniq-geometry.txt) - except an [advanced] caravan_w / caravan_h / page_y the player moved off
// its built-in value, which wins. Before the game's file is read: the settings' values. Pure reads,
// nothing allocated: the draw path calls it every frame.
bool liveCaravanWindow(int* w, int* h, int* pageX, int* pageY) {
    static const UtConfig kBuiltin;
    const int set[3] = {g_cfg.caravanW, g_cfg.caravanH, g_cfg.pageY};
    const int builtin[3] = {kBuiltin.caravanW, kBuiltin.caravanH, kBuiltin.pageY};
    int out[4];
    const bool derived = utCaravanWindowOf(g_geo, g_geo.valid, set, builtin, out);
    if (w) *w = out[0];
    if (h) *h = out[1];
    if (pageX) *pageX = out[2];
    if (pageY) *pageY = out[3];
    return derived;
}

// The game's tables into the retire list at switch #n; the ones retired two switches before are
// freed (one INFO line each way). A switch out of a game with no tables (its tab was off) retires
// an empty entry, so every switch advances the list and "two switches later" holds; an empty
// entry says nothing, going in or out.
void retireGameTables() {
    const bool none = !g_groups && !g_mi && !g_unions;
    unsigned sw = 0;
    gameNow(nullptr, 0, &sw, nullptr);
    GameTables* t = nullptr;
    try {
        t = new GameTables{g_groups, g_mi, g_unions, groupsBytes(g_groups), utMiBytes(g_mi),
                           unionBytes(g_unions)};
    } catch (...) {
        return;   // out of memory: the tables stay where they are, unfreed
    }
    const size_t bytes = t->groupsB + t->miB + t->unionsB;
    if (!none)
        logI("live: the previous game's tables retired at switch #%u (%.2f MB: groups %.2f, KIND %.2f, "
             "button pages %.2f) - freed two switches later",
             sw, mbOf(bytes), mbOf(t->groupsB), mbOf(t->miB), mbOf(t->unionsB));
    const UtRetired<GameTables> out = g_retiredGames.retire(t, sw, bytes);
    InterlockedExchangeAdd(&g_retiredBytes, (LONG)bytes);
    if (!out.table) return;
    InterlockedExchangeAdd(&g_retiredBytes, -(LONG)out.bytes);
    if (out.table->groups || out.table->mi || out.table->unions)
        logI("live: the tables retired at switch #%u freed (%.2f MB)", out.when, mbOf(out.bytes));
    delete out.table->groups;
    utMiFree(out.table->mi);
    delete out.table->unions;
    delete out.table;
}

bool liveIsRelicGroup(int group) { return relicShown(group); }
int liveRelicGroup() { return g_relic ? g_relicGroup : -1; }
const UtRelicList* liveRelicList() { return g_relic ? &g_relic->list : nullptr; }

int liveRelicFind(const char* folded) { return g_relic ? utRelicFind(g_relic->list, folded) : -1; }

bool liveRelicFacts(const char* folded, unsigned* shards, int* tier, const char* const** table,
                    int* tableN) {
    RelicTables* t = g_relic;
    const int r = t ? utRelicFind(t->list, folded) : -1;
    if (r < 0) return false;
    const UtRelicRow& row = t->list.rows[(size_t)r];
    if (shards) *shards = row.shards;
    if (tier) *tier = row.tier;
    if (table) *table = row.bonuses.empty() ? nullptr : &t->bonusPtr[(size_t)t->stateFirst[(size_t)r] + 1];
    if (tableN) *tableN = (int)row.bonuses.size();
    return true;
}

int liveRelicStateCount() { return g_relic ? (int)g_relic->state.size() : 0; }

int liveRelicStateIndex(int row, int slot) {
    RelicTables* t = g_relic;
    if (!t || row < 0 || row >= (int)t->list.rows.size() || slot < 0 ||
        slot > (int)t->list.rows[(size_t)row].bonuses.size())
        return -1;
    return t->stateFirst[(size_t)row] + slot;
}

void liveSearchSetRelicMatch(int stateIndex, bool match) {
    RelicTables* t = g_relic;
    if (!t || stateIndex < 0 || stateIndex >= (int)t->match.size()) return;
    t->match[(size_t)stateIndex] = (unsigned char)(match ? 1 : 0);
}

int liveRelicPlaceCount() { return relicShown(g_shownGroup) ? g_relicShownN : 0; }

bool liveRelicPlaceAt(int i, UtRelicPlace* place, UtRelicPlaceState* state) {
    RelicTables* t = g_relic;
    if (!t || !relicShown(g_shownGroup) || i < 0 || i >= g_relicShownN) return false;
    const UtRelicPlace& rp = g_relicShown[i];
    if (place) *place = rp;
    if (state) *state = t->state[(size_t)(t->stateFirst[(size_t)rp.row] + rp.slot)];
    return true;
}

bool liveRelicWindow(int* firstBlock, int* endBlock, int* blocks, int* page, int* pages) {
    RelicTables* t = g_relic;
    if (!t || !relicShown(g_shownGroup)) return false;
    if (firstBlock) *firstBlock = g_shownPage;
    if (endBlock) *endBlock = g_relicShownEnd;
    if (blocks) *blocks = (int)t->layout.blockLines.size();
    if (page) *page = utRelicPageOf(t->layout, g_shownPage);
    if (pages) *pages = utRelicPageCount(t->layout);
    return true;
}

bool liveRelicTotals(int* bonusesHeld, int* bonusSlots, unsigned* pieces) {
    RelicTables* t = g_relic;
    if (!t) return false;
    if (bonusesHeld) *bonusesHeld = t->bonusesHeld;
    if (bonusSlots) *bonusSlots = t->list.bonusSlots;
    if (pieces) *pieces = t->pieces;
    return true;
}

bool liveRelicStale() {
    RelicTables* t = g_relic;
    return t && relicShown(g_shownGroup) && journalWrites() != t->listedWrites;
}

bool liveReloadGame(HMODULE selfModule) {
    retireGameTables();
    g_groups = nullptr;
    g_geo.valid = false;   // the next game's uniq-geometry.txt
    g_pagesTotal = 0;
    g_mi = nullptr;
    g_unions = nullptr;
    g_unionsGame = -1;   // the game's buttons resolve again over the new table
    g_placeN = 0;
    g_shownGroup = 0;
    g_shownPage = 0;
    g_shownRows = 0;
    g_shownCount = 0;
    g_ownKnown = false;
    g_searchOn = false;
    g_searchIndexed = 0;
    InterlockedExchange(&g_searchMarks, 0);
    InterlockedExchange(&g_searchIndexedPub, 0);
    InterlockedExchange(&g_searchFound, 0);
    InterlockedExchange(&g_searchGroups, 0);
    char leaf[96] = "";
    bool off = false;
    gameNow(leaf, sizeof(leaf), nullptr, &off);
    if (off) {
        logI("live: no group table in the %s game - the tab is off there", leaf[0] ? leaf : "main");
        // the caravan geometry stays the game's own when its set carries it: the caravan frame
        // (the Transfer page's place) and the geometry line follow the game with the tab off too
        try {
            readGeometry(selfModule, false);
        } catch (...) {
            g_geo.valid = false;
        }
        return false;
    }
    // The next game is packed on its own area (liveInit, its P line); the next frame's gridApply
    // applies the grid in force.
    g_hostCols = gdut::kHostCols;
    g_hostRows = gdut::kHostRows;
    g_relic = nullptr;   // the next game's uniq-relics.txt (the old table is never freed)
    g_relicGroup = -1;
    g_relicShownN = 0;
    g_relicGeoSet = false;   // the previous game's relic area is not this game's
    const bool ok = liveInit(selfModule);
    markDirty();
    return ok;
}

}  // namespace ut
