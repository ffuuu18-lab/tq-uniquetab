// ut_protoshift.h - a row scroll KEEPS the prototypes that stay in the window (pure).
//
// Until every navigation was destroy-all + create-all (ut_view buildPage -> protoDestroyAll
// -> protoBuild): 56 Item::CreateItem calls for a one-row scroll of a 2x2 group. The shift below
// matches the new window's places against the live prototypes by (record, journal row): a match
// is the SAME engine object, re-positioned in the mod sack (InventorySack::RemoveItem then
// AddItem(Vec2) - the mod's own calls, which the AddItem guards let through as they do for
// protoBuild); only the leaving ones are destroyed and only the entering ones are created.
//
// The bookkeeping rule (every prototype the mod created is, at every return, in exactly one of:
// listed in `live` [in the sack, or - after an abort only - out of it], or destroyed):
//   1. the leaving ones: RemoveItem, then destroyed, then off the list (protoDestroyAll's order);
//   2. the movers: ALL leave their old cells first (so no re-add meets a cell still taken) ...
//   3. ... then each is added at its new cell - the same object, the same id;
//   4. the entering ones are created (the caller's protoBuild body) and the list is rewritten in
//      the new window's order, so protoAt(i) is places[i] exactly as after a full build.
// A failed RemoveItem / AddItem ABORTS: the list is compacted to the objects still alive (a mover
// that is out of the sack stays LISTED), `aborted` is set, and the caller runs the full path
// (protoDestroyAll removes and destroys every listed one - no orphan - then protoBuild).
// The generation (the tint cache key) is bumped at the start and at every return.
//
// Ops (the engine half in ut_proto.cpp, a fake sack in tools\test_proto.cpp):
//   bool remove(unsigned id);                              InventorySack::RemoveItem, mod sack
//   bool add(void* item, unsigned id, int col, int row);   AddItem(Vec2) + ContainsItem
//   bool destroy(void* item);                              ObjectManager::DestroyObjectEx
//   bool create(int place, UtProtoLive* out);              CreateItem + AddItem; false = nothing left
#pragma once

namespace ut {

enum { kUtProtoMax = 16 * 15 };   // one prototype per cell at most

struct UtProtoPlace {
    const char* record;   // catalogue path (lives in the group table)
    int col, row;         // top-left cell of the footprint (centred in its slot)
    int w, h;             // footprint in cells
    int slotCol, slotRow; // the slot's top-left cell; the owned marks and the "cursor on
    int slotW, slotH;     //   a prototype" test cover the whole slot (the group's largest footprint)
};

// One live prototype (ut_proto's list).
struct UtProtoLive {
    void* item;                           // the engine Item (TqItem*)
    unsigned id;
    const char* record;
    int col, row;
    int w, h;
    int slotCol, slotRow, slotW, slotH;
    unsigned long long seq;               // the journal row it was built from, 0 = bare
};

struct UtShiftCounts {
    int moved;       // kept, re-positioned
    int stayed;      // kept, already at its cell
    int created;     // entering, placed
    int destroyed;   // leaving, destroyed
    int failed;      // entering, refused by the engine (the full build's "failed")
    int aborted;     // 1 = a RemoveItem / AddItem failed; the caller rebuilds whole
};

enum { kUtShiftAborted = -1, kUtShiftNotApplied = 0, kUtShiftDone = 1 };

// may the shift run at all? (ut_proto protoShift's refusal guards, pure so a test
// pins them.) A journalled take pending in the keep slot, a forgotten take (or an overflowed
// forgotten list), and a sack that must retire (full, stale, a cell to recheck) keep the proven
// teardown path: the full rebuild forgets / retires exactly as decided. So do no sack,
// no live list and a place count outside 1..kUtProtoMax. and so do prototypes
// placed on another cell than the sack's live one (a UI scale change re-celled the sack): moving
// them would add the movers at col x the NEW cell beside the others at col x the old one.
struct UtShiftFacts {
    bool sack;             // the mod sack exists
    bool sackFull;         // it could not be built on (kept, never built on again)
    bool sackStale;        // its map may list a forgotten object: it retires at the teardown
    bool recheckCell;      // its cell size must be read again
    unsigned keepId;       // the prototype of a journalled take (0 = none)
    int forgotten;         // forgotten taken ids in the sack
    bool forgotOverflow;   // more than the forgotten list holds
    int liveCount;         // prototypes listed now
    int places;            // the wanted window's places
    bool cellStale;        // the live prototypes sit on a cell the sack no longer has
};
inline bool utShiftAllowed(const UtShiftFacts& f) {
    return f.sack && !f.sackFull && !f.sackStale && !f.recheckCell && !f.cellStale && f.keepId == 0 &&
           f.forgotten == 0 && !f.forgotOverflow && f.liveCount > 0 && f.places > 0 &&
           f.places <= kUtProtoMax;
}

inline void utShiftCopyPlace(UtProtoLive& l, const UtProtoPlace& p) {
    l.col = p.col;
    l.row = p.row;
    l.w = p.w;
    l.h = p.h;
    l.slotCol = p.slotCol;
    l.slotRow = p.slotRow;
    l.slotW = p.slotW;
    l.slotH = p.slotH;
}

// `seq[i]` = the journal row a fresh build of places[i] would carry (0 = bare). `scratch` holds
// kUtProtoMax entries. Returns kUtShiftDone, kUtShiftAborted, or kUtShiftNotApplied (nothing was
// touched: no live list, no places, or too many).
template <class Ops>
int utProtoShift(UtProtoLive* live, int* count, const UtProtoPlace* places,
                 const unsigned long long* seq, int n, Ops& ops, UtShiftCounts* c, unsigned* gen,
                 UtProtoLive* scratch) {
    UtShiftCounts zero = {};
    *c = zero;
    const int m = count ? *count : 0;
    if (!live || !places || !seq || !scratch || !gen || n <= 0 || n > kUtProtoMax || m <= 0 ||
        m > kUtProtoMax)
        return kUtShiftNotApplied;
    int match[kUtProtoMax];   // place i -> live j, -1 = entering
    int owner[kUtProtoMax];   // live j -> place i, -1 = leaving
    bool dead[kUtProtoMax];
    bool off[kUtProtoMax];
    for (int j = 0; j < m; ++j) {
        owner[j] = -1;
        dead[j] = false;
        off[j] = false;
    }
    for (int i = 0; i < n; ++i) {
        match[i] = -1;
        for (int j = 0; j < m; ++j) {
            // the same catalogue entry (the group table's pointer), the same journal row, the same
            // footprint: what a fresh build would create is this very object
            if (owner[j] >= 0 || live[j].record != places[i].record || live[j].seq != seq[i] ||
                live[j].w != places[i].w || live[j].h != places[i].h)
                continue;
            match[i] = j;
            owner[j] = i;
            break;
        }
    }
    ++*gen;
    bool abort = false;
    // 1. the leaving ones
    for (int j = m - 1; j >= 0 && !abort; --j) {
        if (owner[j] >= 0) continue;
        if (!ops.remove(live[j].id)) {
            abort = true;   // still listed: the full teardown removes and destroys it
            break;
        }
        if (ops.destroy(live[j].item)) ++c->destroyed;   // a faulted destroy: off the list anyway
        dead[j] = true;                                  // (protoDestroyAll's rule)
    }
    // 2. the movers leave their old cells, all of them first
    for (int j = 0; j < m && !abort; ++j) {
        const int i = owner[j];
        if (i < 0) continue;
        if (live[j].col == places[i].col && live[j].row == places[i].row) continue;
        if (!ops.remove(live[j].id)) {
            abort = true;
            break;
        }
        off[j] = true;
    }
    // 3. ... and go into their new cells: the same object, the same id
    for (int j = 0; j < m && !abort; ++j) {
        if (!off[j]) continue;
        const UtProtoPlace& p = places[owner[j]];
        if (!ops.add(live[j].item, live[j].id, p.col, p.row)) {
            abort = true;   // out of the sack but LISTED: the full teardown destroys it
            break;
        }
        off[j] = false;
        utShiftCopyPlace(live[j], p);
        ++c->moved;
    }
    if (abort) {
        int k = 0;
        for (int j = 0; j < m; ++j)
            if (!dead[j]) live[k++] = live[j];
        *count = k;
        c->aborted = 1;
        ++*gen;
        return kUtShiftAborted;
    }
    // 4. the list in the new window's order; the entering ones created in place
    int k = 0;
    for (int i = 0; i < n; ++i) {
        const int j = match[i];
        if (j >= 0) {
            scratch[k] = live[j];
            utShiftCopyPlace(scratch[k], places[i]);
            ++k;
            continue;
        }
        UtProtoLive e = {};
        if (ops.create(i, &e)) {
            scratch[k++] = e;
            ++c->created;
        } else {
            ++c->failed;
        }
    }
    for (int i = 0; i < k; ++i) live[i] = scratch[i];
    *count = k;
    c->stayed = k - c->created - c->moved;
    ++*gen;
    return kUtShiftDone;
}

}  // namespace ut
