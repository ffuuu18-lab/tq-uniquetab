// ut_recon.cpp - the journal's unresolved pending rows against the character's containers
// (see ut_recon.h). GAME THREAD ONLY.
//
// Invariants:
//   * engine memory is only READ, every engine call and read inside an SEH guard; nothing is
//     written into any sack, item or prototype. The one engine allocation (GetObjectList's
//     vector) is freed by protoFindItems through MSVCR110; the replica strings by protoCapture;
//   * bounded: at most kIdCap ids per pass (a sack holds a few hundred items at most), at most
//     kPendCap rows; overflowing either makes the pass INCOMPLETE (nothing "absent" is proven);
//   * refuse when unsure: a row is ERASED only when an item with its exact replica identity was
//     read in one of the character's containers; "absent" needs every container read without a
//     fault. Unknown settles nothing (utPendingVerdict -> keep; the caravan pass then falls back
//     to today's rule, with today's WARN wording).
#include "ut_recon.h"

#include <windows.h>

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <vector>

#include "tq_runtime.h"
#include "ut_log.h"
#include "ut_owned.h"
#include "ut_proto.h"
#include "ut_rescue.h"

namespace ut {
namespace {

enum Where { kWInventory = 0, kWEquipment, kWStash, kWTransfer, kWRelic, kWCount };
const char* const kWhereName[kWCount] = {"inventory", "equipment", "caravan stash",
                                         "transfer stash", "relic vault"};

const int kIdCap = 4096;    // container ids gathered per pass
const int kSackCap = 1024;  // ids of one sack
const int kPendCap = 256;   // unresolved rows checked per pass

struct Src {
    unsigned id;
    int where;
};
Src g_src[kIdCap];
int g_srcN = 0;
bool g_overflow = false;
unsigned g_ids[kIdCap];
int g_whereOf[kIdCap];
const void* g_objs[kIdCap];
unsigned g_sackIds[kSackCap];
unsigned g_modIds[kSackCap];
int g_modN = 0;

void add(unsigned id, int where) {
    if (!id) return;
    if (g_srcN >= kIdCap) {
        g_overflow = true;
        return;
    }
    g_src[g_srcN].id = id;
    g_src[g_srcN].where = where;
    ++g_srcN;
}

// One sack's ids (the id map, ut_owned's walk). False = not walkable.
bool addSack(const void* sack, int where) {
    if (!sack) return false;
    const int n = ownedSackKeys(sack, g_sackIds, kSackCap);
    if (n < 0) return false;
    for (int i = 0; i < n; ++i) add(g_sackIds[i], where);
    return true;
}

// The main player's ControllerPlayer (the controller id -> the object walk, as the take's room
// check finds it). Null = not found.
const void* playerController() {
    TqGameEngine* ge = gameEngine();
    if (!ge || !g_tq.GameGetMainPlayer || !g_tq.CharGetControllerId) return nullptr;
    unsigned ctrlId = 0;
    utGuardEnter();
    __try {
        const TqPlayer* p = g_tq.GameGetMainPlayer(ge);
        if (p) ctrlId = g_tq.CharGetControllerId(p);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ctrlId = 0;
    }
    utGuardLeave();
    const void* ctrl = nullptr;
    if (!ctrlId || !protoFindObjects(0, ctrlId, nullptr, &ctrl)) return nullptr;
    return ctrl;
}

// The inventory sacks. False = an export missing, a fault, or a sack not walkable.
bool gatherInventory(const void* ctrl) {
    if (!ctrl || !g_tq.CtrlGetInventoryCtrl || !g_tq.InvCtrlGetNumberOfSacks || !g_tq.InvCtrlGetSack)
        return false;
    const void* sacks[16] = {};
    unsigned n = 0;
    bool ok = false;
    utGuardEnter();
    __try {
        void* inv = g_tq.CtrlGetInventoryCtrl((void*)ctrl);
        n = inv ? g_tq.InvCtrlGetNumberOfSacks(inv) : 0u;
        if (inv && n >= 1u && n <= 16u) {
            ok = true;
            for (unsigned i = 0; i < n; ++i) {
                sacks[i] = g_tq.InvCtrlGetSack(inv, (int)i);
                if (!sacks[i]) ok = false;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    utGuardLeave();
    if (!ok) return false;
    for (unsigned i = 0; i < n; ++i)
        if (!addSack(sacks[i], kWInventory)) return false;
    return true;
}

// EquipmentCtrl::GetItem_HandLeft / _HandRight as this build has them (Game.dll 0x17ABC0 /
// 0x17AC20): the current set (+0x0D) picks primary (+0x84 right, +0x98 left) or
// alternate (+0xC0 right, +0xD4 left). Only when BOTH bodies are these bytes are the four fields
// read directly, so the weapon set NOT in hand is looked at too.
const unsigned char kHandLeft[] = {
    0x33, 0xC0, 0x38, 0x41, 0x0D, 0x74, 0x24, 0x8B, 0x91, 0xD4, 0x00, 0x00, 0x00, 0x85, 0xD2,
    0x74, 0x03, 0x8B, 0xC2, 0xC3, 0x8B, 0x91, 0xCC, 0x00, 0x00, 0x00, 0x83, 0xFA, 0x04, 0x74,
    0x05, 0x83, 0xFA, 0x10, 0x75, 0x27, 0x8B, 0x81, 0xC0, 0x00, 0x00, 0x00, 0xC3, 0x8B, 0x91,
    0x98, 0x00, 0x00, 0x00, 0x85, 0xD2, 0x75, 0xDC, 0x8B, 0x91, 0x90, 0x00, 0x00, 0x00, 0x83,
    0xFA, 0x04, 0x74, 0x05, 0x83, 0xFA, 0x10, 0x75, 0x06, 0x8B, 0x81, 0x84, 0x00, 0x00, 0x00,
    0xC3};
const unsigned char kHandRight[] = {
    0x33, 0xC0, 0x38, 0x41, 0x0D, 0x74, 0x24, 0x8B, 0x91, 0xC0, 0x00, 0x00, 0x00, 0x85, 0xD2,
    0x74, 0x03, 0x8B, 0xC2, 0xC3, 0x8B, 0x91, 0xE0, 0x00, 0x00, 0x00, 0x83, 0xFA, 0x04, 0x74,
    0x05, 0x83, 0xFA, 0x10, 0x75, 0x27, 0x8B, 0x81, 0xD4, 0x00, 0x00, 0x00, 0xC3, 0x8B, 0x91,
    0x84, 0x00, 0x00, 0x00, 0x85, 0xD2, 0x75, 0xDC, 0x8B, 0x91, 0xA4, 0x00, 0x00, 0x00, 0x83,
    0xFA, 0x04, 0x74, 0x05, 0x83, 0xFA, 0x10, 0x75, 0x06, 0x8B, 0x81, 0x98, 0x00, 0x00, 0x00,
    0xC3};
const unsigned kWeaponField[4] = {0x84, 0x98, 0xC0, 0xD4};

bool bodyIs(const void* fn, const unsigned char* want, size_t n) {
    unsigned char got[sizeof(kHandLeft)];
    if (!fn || n > sizeof(got) || !safeRead(fn, got, n)) return false;
    return memcmp(got, want, n) == 0;
}

// The equipment. False = an export missing or a fault (the pass cannot prove "absent").
bool gatherEquipment(const void* ctrl, bool* bothSets) {
    *bothSets = false;
    if (!ctrl || !g_tq.CtrlGetEquipmentCtrl) return false;
    for (int k = 0; k < 10; ++k)
        if (!g_tq.EquipGetItem[k]) return false;
    const bool raw = bodyIs((const void*)g_tq.EquipGetItem[8], kHandLeft, sizeof(kHandLeft)) &&
                     bodyIs((const void*)g_tq.EquipGetItem[9], kHandRight, sizeof(kHandRight));
    unsigned ids[14] = {};
    bool ok = false;
    utGuardEnter();
    __try {
        const void* eq = g_tq.CtrlGetEquipmentCtrl((void*)ctrl);
        if (eq) {
            for (int k = 0; k < 10; ++k) ids[k] = g_tq.EquipGetItem[k](eq);
            if (raw)
                for (int w = 0; w < 4; ++w)
                    ids[10 + w] = *(const unsigned*)((const unsigned char*)eq + kWeaponField[w]);
            ok = true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    utGuardLeave();
    if (!ok) return false;
    for (int k = 0; k < 14; ++k) add(ids[k], kWEquipment);
    *bothSets = raw;
    return true;
}

// The three caravan stores (the real sacks, GameEngine + the offsets the owned walk uses).
bool gatherStores() {
    TqGameEngine* ge = gameEngine();
    if (!ge) return false;
    const unsigned offs[3] = {g_tq.stashOff, g_tq.transferOff, g_tq.relicOff};
    const int where[3] = {kWStash, kWTransfer, kWRelic};
    for (int k = 0; k < 3; ++k) {
        if (!offs[k]) return false;
        if (!addSack((const unsigned char*)ge + offs[k], where[k])) return false;
    }
    return true;
}

// The mod sack's ids: never a container of the character (a prototype carries a row's replica).
// False = it exists and could not be walked.
bool readModSack() {
    g_modN = 0;
    const void* mod = protoSackBuilt();
    if (!mod) return true;
    const int n = ownedSackKeys(mod, g_modIds, kSackCap);
    if (n < 0) return false;
    g_modN = n;
    std::sort(g_modIds, g_modIds + g_modN);
    return true;
}

struct PassOut {
    bool charOk = false, invOk = false, equipOk = false, bothSets = false, storesOk = false;
    int items = 0, read = 0, captureFailed = 0;
    int noItem = 0;   // a container id that resolved to no Item object
};

// One pass. `caravan` = the stores are searched too (a row found nowhere is then "absent").
void runPass(bool caravan) {
    const size_t want = journalUnresolved();
    if (!want) return;
    const char* pass = caravan ? "caravan open" : "world load";
    if (!journalUsable()) {
        if (caravan) journalUnresolvedFallback("the journal is not writable this session");
        return;
    }
    const size_t cap = want > (size_t)kPendCap ? (size_t)kPendCap : want;
    std::vector<UtPendingRow> rows;
    std::vector<const UtReplicaCapture*> ids;
    std::vector<int> found, where;
    try {
        rows.resize(cap);
        const size_t n = journalUnresolvedRows(&rows[0], cap);
        rows.resize(n);
        ids.resize(n);
        found.assign(n, 0);
        where.assign(n, -1);
        for (size_t i = 0; i < n; ++i) ids[i] = &rows[i].id;
    } catch (...) {
        if (caravan) journalUnresolvedFallback("out of memory");
        return;
    }
    const int n = (int)rows.size();
    if (n == 0) return;

    PassOut o;
    g_srcN = 0;
    g_overflow = false;
    const bool modOk = readModSack();
    const void* ctrl = playerController();
    o.charOk = ctrl != nullptr;
    o.invOk = o.charOk && gatherInventory(ctrl);
    o.equipOk = o.charOk && gatherEquipment(ctrl, &o.bothSets);
    o.storesOk = caravan && gatherStores();

    // the ids, sorted and unique, the mod sack's left out; one object walk resolves them
    std::sort(g_src, g_src + g_srcN, [](const Src& a, const Src& b) { return a.id < b.id; });
    int m = 0;
    for (int i = 0; i < g_srcN; ++i) {
        if (m && g_ids[m - 1] == g_src[i].id) continue;
        if (std::binary_search(g_modIds, g_modIds + g_modN, g_src[i].id)) continue;
        g_ids[m] = g_src[i].id;
        g_whereOf[m] = g_src[i].where;
        ++m;
    }
    o.items = m;
    const bool walked = m == 0 || protoFindItems(g_ids, m, g_objs);
    if (walked) {
        UtReplicaCapture live;
        for (int i = 0; i < m; ++i) {
            if (!g_objs[i]) {
                ++o.noItem;   // unread, never "absent"
                continue;
            }
            unsigned st = 0;
            if (!protoCapture(g_objs[i], &live, nullptr, 0, &st)) {
                ++o.captureFailed;
                continue;
            }
            ++o.read;
            const int r = utMatchLive(&ids[0], &found[0], n, live);
            if (r >= 0) {
                found[r] = 1;
                where[r] = g_whereOf[i];
            }
        }
    }
    // "absent" is proven only by a caravan pass that read EVERY container without a fault
    // (the equipment counts as read only with BOTH weapon sets: the pinned hand getters)
    const bool complete = caravan && modOk && o.invOk && o.equipOk && o.bothSets && o.storesOk && walked &&
                          !g_overflow && o.captureFailed == 0 && o.noItem == 0;
    const bool charComplete = modOk && o.invOk && o.equipOk && walked && !g_overflow &&
                              o.captureFailed == 0 && o.noItem == 0;
    std::vector<UtVerdictItem> v;
    int counts[4] = {0, 0, 0, 0};
    try {
        v.resize((size_t)n);
        for (int i = 0; i < n; ++i) {
            // a FOUND row is proven whatever else failed (the compare is exact); only a mod
            // sack that could not be walked makes even a find unprovable
            const int f = !modOk          ? kUtFoundUnknown
                          : found[i]      ? kUtFoundPresent
                          : complete      ? kUtFoundAbsent
                          : (!caravan && charComplete) ? kUtFoundAbsentSoFar
                                                       : kUtFoundUnknown;
            v[i].seq = rows[i].seq;
            v[i].verdict = utPendingVerdict(rows[i].state, f);
            v[i].where = found[i] && where[i] >= 0 && where[i] < kWCount ? kWhereName[where[i]]
                                                                        : nullptr;
            ++counts[v[i].verdict];
        }
    } catch (...) {
        if (caravan) journalUnresolvedFallback("out of memory");
        return;
    }
    logI("journal: container check at %s - %d pending row(s), %d item(s) (%d read) in the "
         "inventory%s%s%s%s: %d dropped (found), %d settled, %d restored, %d kept",
         pass, n, o.items, o.read, o.invOk ? "" : " [NOT READ]",
         o.equipOk ? (o.bothSets ? ", the equipment (both weapon sets)" : ", the equipment")
                   : ", the equipment [NOT READ]",
         caravan ? (o.storesOk ? ", the caravan stores" : ", the caravan stores [NOT READ]") : "",
         g_overflow ? " [id cap reached]" : "", counts[kUtVerdictErase],
         counts[kUtVerdictSettle], counts[kUtVerdictRestore], counts[kUtVerdictKeep]);
    if (o.captureFailed || o.noItem)
        logD("journal: container check - %d item(s) could not be read, %d id(s) resolved to no "
             "item", o.captureFailed, o.noItem);
    // at a caravan open the rows the check leaves undecided get today's rule inside
    // journalApplyVerdicts, so the pass writes the journal ONCE
    const char* why = !modOk            ? "the mod sack could not be walked"
                      : !o.charOk       ? "no player controller"
                      : !o.invOk        ? "the inventory could not be read"
                      : !o.equipOk      ? "the equipment could not be read"
                      : !o.bothSets     ? "the weapon set not in hand could not be read"
                      : !o.storesOk     ? "the caravan stores could not be read"
                      : !walked         ? "the object list could not be read"
                      : g_overflow      ? "more items than the check reads"
                      : o.noItem        ? "an item id resolved to no item"
                      : o.captureFailed ? "an item's identity could not be read"
                                        : "more pending rows than one check reads";
    const int changed =
        journalApplyVerdicts(v.empty() ? nullptr : &v[0], v.size(), caravan ? why : nullptr);
    if (changed != 0) ownedMarkDirty();
    if (changed < 0) logW("journal: the container check's settle could not be written yet - retried");
}

}  // namespace

void reconOnWorld() { runPass(false); }

void reconOnCaravan() { runPass(true); }

}  // namespace ut
