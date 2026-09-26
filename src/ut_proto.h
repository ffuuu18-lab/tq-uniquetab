// ut_proto.h - the collection page's DISPLAY PROTOTYPES: one real engine Item per catalogue record
// of the page, built with Item::CreateItem from a mod-built ItemReplicaInfo and placed
// footprint-true into the mod's own InventorySack. Nothing here ever touches a REAL sack.
//
// Two halves:
//   * the replica assembly (pure, inline, below): the 0xBC-byte ItemReplicaInfo with its seven
//     VS2012 x86 std::strings, built WITHOUT any allocation - a long baseName points into the
//     UtReplica's own buffer. Item::CreateItem takes the replica by CONST reference and only
//     copies out of it, so nothing the engine owns ever points at this memory, and nothing is
//     freed (the MSVCR110 rule applies to strings the ENGINE allocates; there are none here).
//     tools\test_proto.cpp --units checks the bytes.
//   * the engine half (ut_proto.cpp, game thread only): the mod sack (constructed once, on the
//     PROCESS heap, never freed), the page build and the destroy.
//
// Replica layout (measured at run time): +0x00 u32 raw0; strings at +0x04 baseName,
// +0x1C prefix, +0x34 suffix, +0x4C relic, +0x64 relicBonus, +0x84 relic2, +0x9C relicBonus2;
// u32 +0x7C seed, +0x80 var1, +0xB4 var2; +0xB8 one byte. Total 0xBC.
#pragma once

#include <string.h>

#include "ut_rescue.h"   // UtReplicaCapture, utJournalKey
#include "ut_protoshift.h"   // kUtProtoMax, UtProtoPlace, UtProtoLive, the row-scroll shift

namespace ut {

const unsigned kUtReplicaSize = 0xBC;
const unsigned kUtReplicaStr[7] = {0x04, 0x1C, 0x34, 0x4C, 0x64, 0x84, 0x9C};
const unsigned kUtReplicaSeed = 0x7C;
const unsigned kUtReplicaVar1 = 0x80;
const unsigned kUtReplicaVar2 = 0xB4;
const unsigned kUtReplicaMaxBase = 259;   // a record path longer than this is refused

const unsigned kUtReplicaB8 = 0xB8;      // the one byte at +0xB8 (kept byte-exact)

struct UtReplica {
    unsigned char bytes[kUtReplicaSize];
    char longBase[kUtReplicaMaxBase + 1];   // the heap-form baseName's text, when len >= 16
    // the heap-form text of the six other strings of an identity replica (index 1..6; 0 is
    // longBase). Item::CreateItem takes the replica by const reference and copies out of it.
    char longStr[7][kUtReplicaMaxBase + 1];
};

// One VS2012 x86 std::string in place at `at`: 16-byte buffer/pointer union, size (+0x10),
// capacity (+0x14). Capacity 15 = the text lives in the buffer.
inline void utVsStringSet(unsigned char* at, const char* text, unsigned len, char* heapBuf) {
    memset(at, 0, 0x18);
    unsigned cap = 15;
    if (len < 16) {
        memcpy(at, text, len);
        at[len] = 0;
    } else {
        memcpy(heapBuf, text, len);
        heapBuf[len] = 0;
        const char* p = heapBuf;
        memcpy(at, &p, sizeof(p));
        cap = len;
    }
    memcpy(at + 0x10, &len, 4);
    memcpy(at + 0x14, &cap, 4);
}

// Builds the replica for `record` ("records/item/..." as the catalogue holds it): baseName =
// the record with '/' -> '\' (the engine's own form on disk and in every replica it builds), every
// other string empty, seed 0 (the engine rolls one), ids and variations 0. False when the
// record is empty, too long, or not a plain printable path.
inline bool utReplicaBuild(UtReplica* r, const char* record) {
    if (!r || !record) return false;
    const size_t n = strlen(record);
    if (n == 0 || n > kUtReplicaMaxBase) return false;
    char base[kUtReplicaMaxBase + 1];
    for (size_t i = 0; i < n; ++i) {
        const unsigned char c = (unsigned char)record[i];
        if (c < 0x20 || c >= 0x7F) return false;
        base[i] = (c == '/') ? '\\' : (char)c;
    }
    base[n] = 0;
    memset(r->bytes, 0, sizeof(r->bytes));
    memset(r->longBase, 0, sizeof(r->longBase));
    char unused[1];
    utVsStringSet(r->bytes + kUtReplicaStr[0], base, (unsigned)n, r->longBase);
    for (int i = 1; i < 7; ++i) utVsStringSet(r->bytes + kUtReplicaStr[i], "", 0, unused);
    return true;
}

// ---- a replica from a journal row's IDENTITY, and back -----------------------------------
// The seven strings byte-exact (the base is NOT converted: the row keeps the engine's own
// spelling), seed / var1 / var2 at +0x7C / +0x80 / +0xB4, the +0xB8 byte; raw0 (an object id)
// stays 0 - CreateItem gives the new object its own id. False when a string is too long or not
// printable, or the base is empty.
inline bool utReplicaFromIdentity(UtReplica* r, const UtReplicaCapture& id) {
    if (!r || !id.str[kUtIdBase][0] || id.b8 > 255u) return false;
    memset(r->bytes, 0, sizeof(r->bytes));
    memset(r->longBase, 0, sizeof(r->longBase));
    memset(r->longStr, 0, sizeof(r->longStr));
    for (int k = 0; k < 7; ++k) {
        const char* t = id.str[k];
        const size_t n = strnlen(t, kUtIdStrMax);
        if (n > kUtReplicaMaxBase) return false;
        for (size_t i = 0; i < n; ++i) {
            const unsigned char c = (unsigned char)t[i];
            if (c < 0x20 || c >= 0x7F) return false;
        }
        utVsStringSet(r->bytes + kUtReplicaStr[k], t, (unsigned)n,
                      k == 0 ? r->longBase : r->longStr[k]);
    }
    memcpy(r->bytes + kUtReplicaSeed, &id.seed, 4);
    memcpy(r->bytes + kUtReplicaVar1, &id.var1, 4);
    memcpy(r->bytes + kUtReplicaVar2, &id.var2, 4);
    r->bytes[kUtReplicaB8] = (unsigned char)id.b8;
    return true;
}

// Reads one VS2012 string out of a replica THIS PROCESS can dereference (a mod-built one, or the
// engine's after a guarded copy of its heap text - the engine half reads engine memory through
// safeStdString instead). False when the 24 bytes are not a plausible string.
inline bool utVsStringGet(const unsigned char* at, char* out, unsigned cap) {
    unsigned len = 0, res = 0;
    memcpy(&len, at + 0x10, 4);
    memcpy(&res, at + 0x14, 4);
    if (res < 15u || len > res || len + 1u > cap) return false;
    const char* text = (const char*)at;
    if (res >= 16u) memcpy(&text, at, sizeof(text));
    if (!text) return false;
    memcpy(out, text, len);
    out[len] = 0;
    return strnlen(out, len + 1u) == len;
}

// The identity back out of a replica (the round trip the offline test proves: row -> replica
// bytes -> the same row). `record` is the folded base.
inline bool utReplicaReadIdentity(const unsigned char* bytes, UtReplicaCapture* id) {
    if (!bytes || !id) return false;
    memset(id, 0, sizeof(*id));
    for (int k = 0; k < 7; ++k)
        if (!utVsStringGet(bytes + kUtReplicaStr[k], id->str[k], kUtIdStrMax)) return false;
    memcpy(&id->seed, bytes + kUtReplicaSeed, 4);
    memcpy(&id->var1, bytes + kUtReplicaVar1, 4);
    memcpy(&id->var2, bytes + kUtReplicaVar2, 4);
    id->b8 = bytes[kUtReplicaB8];
    id->stack = 1;
    return utJournalKey(id->str[kUtIdBase], id->record, sizeof(id->record));
}

}  // namespace ut

// ---- the teardown rule for a journalled take (pure; test_proto) ----------------
// The prototype whose take is ON DISK (its id is on the cursor, or about to be) is NEVER destroyed:
// a failed hand-out, or a view OFF between SetId and RemoveItemFromTransfer, FORGETS it (it leaves
// the live list, the object stays the player's item) and the sack is retired when its map may
// still list the object. Every other prototype is destroyed as before.
namespace ut {
enum { kUtProtoDestroy = 0, kUtProtoForget = 1 };
inline int utProtoTeardownAction(unsigned id, unsigned takenId) {
    return (takenId != 0u && id == takenId) ? kUtProtoForget : kUtProtoDestroy;
}
}  // namespace ut

#ifndef UT_PROTO_PURE_ONLY
#include "tq_runtime.h"

namespace ut {

// ---- the engine half (ut_proto.cpp). Game thread only. ---------------------------------------
// (kUtProtoMax and UtProtoPlace moved to ut_protoshift.h, unchanged.)

// The mod sack: constructed on first use with the exported ctor (the object registers itself as
// an options listener, so it lives on the PROCESS heap and is never freed), then sized 16 x 15
// with SetDims from its own cell size. Null when anything about it is not as expected.
TqSack* protoSack();
// The mod sack if it was ever built, else null - NEVER builds (safe from DllMain and the detours).
TqSack* protoSackBuilt();
// Builds one page: CreateItem + AddItem(Vec2 in sack pixels) per place, verified with
// ContainsItem. Returns the number placed; *failed gets the misses (each one destroyed again).
int protoBuild(const UtProtoPlace* places, int n, int* failed);
// Removes every prototype from the mod sack and destroys it the engine's way
// (ObjectManager::DestroyObjectEx). Returns how many were destroyed.
int protoDestroyAll();
// a navigation inside the SAME group (a row scroll, a window step, a same-window refresh):
// the prototypes that stay are re-positioned in the mod sack (the same objects), only the leaving
// ones destroyed and only the entering ones created (ut_protoshift.h utProtoShift; the protoBuild
// body creates). Runs the teardown's foreign sweep first, as protoDestroyAll does (
// a protoDestroyAll that follows a refused-after-the-sweep or aborted shift skips its own sweep - one
// sweep per teardown). Refuses per utShiftAllowed (ut_protoshift.h). kUtShiftDone =
// the window is built; kUtShiftNotApplied = nothing touched (a journalled take pending, a forgotten
// take in the sack, a stale sack, no live list); kUtShiftAborted = a RemoveItem / AddItem failed,
// the list is exact (a mover out of the sack stays listed): both of the latter want the full path
// (protoDestroyAll + protoBuild) next. `out` gets the counts.
int protoShift(const UtProtoPlace* places, int n, UtShiftCounts* out);
// World gone: the engine may already have torn its objects down, so NOTHING is called on them.
// The prototype list is dropped and the mod sack is abandoned (its block stays allocated, as a
// registered listener must); the next ON builds a fresh sack. Returns how many were dropped.
int protoForgetAll();
// Is `id` one of the prototypes alive right now? (a linear scan of at most 240 ids)
bool protoIsId(unsigned id);
// Is `s` a mod sack - the current one, or one a world teardown retired? (G2, G3 and
// G4 compare a page's member against every sack the mod ever built; the blocks are never freed.)
bool protoIsModSack(const void* s);
int protoCount();
// changes whenever the prototype list does (build, destroy, forget, a take), so a
// cache keyed on it never outlives a rebuild that reused the same heap addresses.
unsigned protoGeneration();
// The last page's prototype record index -> cell rect, for the owned marks. False past the end.
bool protoAt(int i, const char** record, int* col, int* row, unsigned* id, int* w = nullptr,
             int* h = nullptr);
// the slot of prototype i (top-left cell and size in cells). False past the end.
bool protoSlotAt(int i, int* col, int* row, int* w, int* h);
// The engine Item of prototype i (the owned marking checks its name against its record).
const void* protoItem(int i);

// ---- identity, deposit and take -----------------------------------------------------------
// protoBuild builds a record with journal rows from its NEWEST row's identity (bright, takeable)
// and a record with none as a bare prototype (dim). This says which: `seq` = the row (0 = bare).
bool protoLookup(unsigned id, const char** record, unsigned long long* seq);
bool protoAtSeq(int i, unsigned long long* seq);
// The take: the prototype `id` leaves the mod sack (InventorySack::RemoveItem on the MOD sack,
// never a real one) and the mod forgets it WITHOUT destroying it - the object is the player's
// item now. False when `id` is not a live prototype or the removal failed.
bool protoHandOut(unsigned id);
// `id` has a journalled take - from now on it is never destroyed (0 = none).
void protoKeep(unsigned id);
// A take whose hand-out FAILED: `id` leaves the live list WITHOUT being destroyed (it is on the
// cursor) and the mod sack is retired at the next teardown or use (its map may still list it).
bool protoForgetTaken(unsigned id);
// The property search's throw-away item: created from `rep` and never placed in any sack; the
// same caller destroys it again with protoDestroyLoose before it returns, so no engine tick sees
// it. Null or an id of 0 = refused (a null item needs no destroy; one with id 0 does).
// `fault` (may be null) names the place when CreateItem or GetObjectId FAULTED, which is told
// apart from a genuine null or 0: the caller then turns its feature off (an item that exists after
// a GetObjectId fault is still the caller's to destroy).
TqItem* protoCreateLoose(const UtReplica& rep, unsigned* id, const char** fault = nullptr);
bool protoDestroyLoose(TqItem* item);
// The deposit's engine reads, one ObjectManager::GetObjectList walk: the object with `itemId`
// (must carry Item's vftable replica slot) and, when `ctrlId` is non-zero, the object with that
// id whose vftable is ControllerPlayer's. Either may come back null.
bool protoFindObjects(unsigned itemId, unsigned ctrlId, const void** item, const void** ctrl);
// ONE object walk resolving many ids to their Item objects (read only). `ids` sorted
// ascending; objs[i] = the Item object of ids[i], or null (no such object / not an Item).
// False = the object list could not be read (every objs[i] null).
bool protoFindItems(const unsigned* ids, int n, const void** objs);
// GetItemReplicaInfo into a canary-guarded mod buffer (the layout measured at run time), the seven strings
// copied out, the engine's heap strings freed through MSVCR110 operator delete. `name` gets
// Object::GetObjectName folded. False = not captured (nothing is freed on a fault: a leak, never
// a guess).
bool protoCapture(const void* item, UtReplicaCapture* out, char* name, unsigned nameCap,
                  unsigned* stack);
// Item::GetNumberInStack through the object's own vftable slot (0 = a single item; a fault
// or a null item = 0xFFFFFFFF, which the deposit gate refuses as a stack).
unsigned protoStackCount(const void* item);
// false = item.stackSlot not decoded; protoStackCount then answers 0 without a call.
bool protoStackReadable();
// Object::GetObjectName of `obj`, folded (the record it was built from). "" when unreadable.
void protoObjectRecord(const void* obj, char* out, unsigned cap);
// The mod sack's cell size in sack pixels (copied from the real Transfer sack); false if unknown.
// the mod sack's LIVE cell (InventorySack::GetCellWidth / Height): DRAWN px, the engine's
// floorf(32 x UI scale + 0.5f) - it follows every UI scale change (the sack is an options listener).
bool protoCellSize(unsigned* w, unsigned* h);
// the cell the live prototypes were PLACED with (col x cell), and whether the live cell has
// moved away from it since (a UI scale change): the view then rebuilds the window whole.
bool protoPlacedCell(unsigned* w, unsigned* h);
bool protoPlacedCellStale();

// -------------------------------------------------------------------------------------------
// True while THIS thread is inside the mod's own add/remove on the mod sack (the AddItem guard lets
// exactly those through). Game thread; the flag is set and cleared around each single call.
bool protoPlacing();
// The live prototype whose SLOT holds the cell (col, row) - index / id, -1 / 0 = none.
int protoSlotIndexAtCell(int col, int row);
unsigned protoIdAtSlotCell(int col, int row);
// walk the mod sack's map before a teardown; every key that is not a prototype
// the mod created is FOREIGN: returned to the real Transfer (GameEngine::AddItemToTransfer's
// original, the engine's own auto-place), else left in the sack, which is then retired (never
// destroyed). ERROR per foreign id. `worldGone`: no return is tried (the world took the objects).
int protoSweepForeign(bool worldGone);

}  // namespace ut
#endif
