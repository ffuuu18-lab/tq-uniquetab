// test_proto.cpp - the display prototypes' replica assembly (src\ut_proto.h, the pure half), offline.
// NO GAME. `--units` (the default) checks the bytes Item::CreateItem will read:
//   * the layout: 0xBC bytes, the seven VS2012 x86 strings at +0x04/1C/34/4C/64/84/9C, the u32s at
//     +0x7C seed / +0x80 var1 / +0xB4 var2 all zero, +0x00 and +0xB8 zero;
//   * baseName: '/' -> '\', the text in place when shorter than 16 (capacity 15), else a pointer to
//     the replica's OWN buffer (capacity = size), NUL-terminated either way; the six other strings
//     empty in place (size 0, capacity 15);
//   * refusals: empty, too long, a control or non-ASCII byte;
//   * every record of data\oracle\uniq-records.txt builds (the real catalogue paths).
#define UT_PROTO_PURE_ONLY
#include "../src/ut_proto.h"

#include <stdio.h>
#include <string.h>

#include <string>
#include <chrono>   // the shift's own bookkeeping cost (informational)

namespace {

int g_fail = 0;
int g_ran = 0;

void ok(bool cond, const char* what, const char* detail) {
    ++g_ran;
    printf("[proto]  %s  %-50s %s\n", cond ? "PASS" : "FAIL", what, detail ? detail : "");
    if (!cond) ++g_fail;
}

unsigned u32(const unsigned char* p) {
    unsigned v;
    memcpy(&v, p, 4);
    return v;
}

// Reads a VS2012 string at `at`; false when its invariants do not hold.
bool vsRead(const ut::UtReplica& r, unsigned off, std::string* out) {
    const unsigned char* at = r.bytes + off;
    const unsigned size = u32(at + 0x10), cap = u32(at + 0x14);
    if (size > cap) return false;
    if (cap < 16) {
        if (cap != 15 || at[size] != 0) return false;
        out->assign((const char*)at, size);
        return true;
    }
    const char* p;
    memcpy(&p, at, sizeof(p));
    if (p != r.longBase || p[size] != 0) return false;   // must point into the replica itself
    out->assign(p, size);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    const char* records = argc > 2 ? argv[2] : "data/oracle/uniq-records.txt";
    char d[320];
    ok(sizeof(((ut::UtReplica*)0)->bytes) == 0xBC, "the replica is 0xBC bytes", "the measured layout");
    ok(ut::kUtReplicaStr[0] == 0x04 && ut::kUtReplicaStr[1] == 0x1C && ut::kUtReplicaStr[2] == 0x34 &&
           ut::kUtReplicaStr[3] == 0x4C && ut::kUtReplicaStr[4] == 0x64 &&
           ut::kUtReplicaStr[5] == 0x84 && ut::kUtReplicaStr[6] == 0x9C,
       "strings at +04/1C/34/4C/64/84/9C", nullptr);

    ut::UtReplica r;
    memset(&r, 0xCD, sizeof(r));
    const char* shortRec = "records/a/b.dbr";   // 15 chars: in place
    bool built = ut::utReplicaBuild(&r, shortRec);
    std::string base;
    bool good = built && vsRead(r, 0x04, &base) && base == "records\\a\\b.dbr" &&
                u32(r.bytes + 0x04 + 0x14) == 15;
    ok(good, "a 15-char record: in place, '/' -> '\\'", base.c_str());

    const char* longRec = "records/item/equipmenthelm/u_l_agamemnon'sdeathmask.dbr";
    memset(&r, 0xCD, sizeof(r));
    built = ut::utReplicaBuild(&r, longRec);
    good = built && vsRead(r, 0x04, &base) &&
           base == "records\\item\\equipmenthelm\\u_l_agamemnon'sdeathmask.dbr" &&
           u32(r.bytes + 0x04 + 0x14) == base.size() && u32(r.bytes + 0x04 + 0x10) == base.size();
    ok(good, "a long record: pointer into the replica, cap = size", base.c_str());

    int emptyOk = 0;
    for (int i = 1; i < 7; ++i) {
        std::string e;
        if (vsRead(r, ut::kUtReplicaStr[i], &e) && e.empty() && u32(r.bytes + ut::kUtReplicaStr[i] + 0x10) == 0)
            ++emptyOk;
    }
    _snprintf_s(d, sizeof(d), _TRUNCATE, "%d of 6 empty, capacity 15", emptyOk);
    ok(emptyOk == 6, "prefix/suffix/relic/bonus/relic2/bonus2 empty", d);
    ok(u32(r.bytes + ut::kUtReplicaSeed) == 0 && u32(r.bytes + ut::kUtReplicaVar1) == 0 &&
           u32(r.bytes + ut::kUtReplicaVar2) == 0 && u32(r.bytes) == 0 && r.bytes[0xB8] == 0,
       "seed / var1 / var2 / raw0 / +0xB8 are zero", "seed 0 = the engine rolls one");
    // every byte outside the seven strings is zero (no 0xCD survives)
    int stray = 0;
    for (unsigned k = 0; k < 0xBC; ++k) {
        bool inStr = false;
        for (int i = 0; i < 7; ++i)
            if (k >= ut::kUtReplicaStr[i] && k < ut::kUtReplicaStr[i] + 0x18) inStr = true;
        if (!inStr && r.bytes[k] != 0) ++stray;
    }
    _snprintf_s(d, sizeof(d), _TRUNCATE, "%d stray byte(s)", stray);
    ok(stray == 0, "no byte outside the strings is left over", d);

    std::string tooLong(ut::kUtReplicaMaxBase + 1, 'a');
    ok(!ut::utReplicaBuild(&r, "") && !ut::utReplicaBuild(&r, nullptr) &&
           !ut::utReplicaBuild(&r, tooLong.c_str()) && !ut::utReplicaBuild(&r, "records/\x01.dbr") &&
           !ut::utReplicaBuild(&r, "records/\xe9.dbr") && !ut::utReplicaBuild(nullptr, "a"),
       "refusals: empty, null, too long, control, non-ASCII", nullptr);
    std::string edge(ut::kUtReplicaMaxBase, 'b');
    ok(ut::utReplicaBuild(&r, edge.c_str()) && vsRead(r, 0x04, &base) && base.size() == edge.size(),
       "the longest legal record builds", nullptr);

    FILE* fh = nullptr;
    int n = 0, bad = 0;
    if (fopen_s(&fh, records, "rb") == 0 && fh) {
        char line[512];
        while (fgets(line, sizeof(line), fh)) {
            size_t k = strlen(line);
            while (k && (line[k - 1] == '\n' || line[k - 1] == '\r')) line[--k] = 0;
            if (!k) continue;
            ++n;
            std::string got;
            if (!ut::utReplicaBuild(&r, line) || !vsRead(r, 0x04, &got) || got.size() != k ||
                got.find('/') != std::string::npos)
                ++bad;
        }
        fclose(fh);
    }
    _snprintf_s(d, sizeof(d), _TRUNCATE, "%d records from %s, %d refused", n, records, bad);
    ok(n == 1588 && bad == 0, "every catalogue record builds a replica", d);


    // ---- the identity round trip ------------------------------
    // journal row -> replica bytes -> the row again -> the same bytes. Every field: the seven
    // strings (in place below 16 characters, a pointer into the replica's own buffer from 16 on),
    // seed / var1 / var2, the +0xB8 byte; raw0 stays 0 (CreateItem gives the object its own id).
    {
        ut::UtReplicaCapture id;
        memset(&id, 0, sizeof(id));
        const char* s[7] = {"records\\item\\equipmentring\\u_n_ring_01.dbr", "short",
                            "records\\item\\lootmagicalaffixes\\suffix\\default\\x.dbr",
                            "", "records\\item\\relics\\bonus_long_name_here.dbr",
                            "exactly15chars!", "exactly16chars!!"};
        for (int k = 0; k < 7; ++k) _snprintf_s(id.str[k], ut::kUtIdStrMax, _TRUNCATE, "%s", s[k]);
        id.seed = 0x7FFFu;
        id.var1 = 0xDEADBEEFu;
        id.var2 = 3u;
        id.b8 = 0xABu;
        id.stack = 1;
        ut::utJournalKey(id.str[0], id.record, sizeof(id.record));
        static ut::UtReplica a, b;
        const bool idBuilt = ut::utReplicaFromIdentity(&a, id);
        ok(idBuilt, "identity -> replica", nullptr);
        ok(u32(a.bytes + 0x7C) == 0x7FFFu && u32(a.bytes + 0x80) == 0xDEADBEEFu &&
               u32(a.bytes + 0xB4) == 3u && a.bytes[0xB8] == 0xAB && u32(a.bytes) == 0,
           "seed +0x7C, var1 +0x80, var2 +0xB4, b8 +0xB8, raw0 0", nullptr);
        bool strOk = true;
        for (int k = 0; k < 7; ++k) {
            const unsigned char* at = a.bytes + ut::kUtReplicaStr[k];
            const unsigned len = (unsigned)strlen(s[k]);
            const unsigned cap = len < 16 ? 15u : len;
            const char* text = (const char*)at;
            if (len >= 16) memcpy(&text, at, sizeof(text));
            if (u32(at + 0x10) != len || u32(at + 0x14) != cap || strcmp(text, s[k]) != 0)
                strOk = false;
            if (len >= 16 && (text < (const char*)&a || text >= (const char*)(&a + 1)))
                strOk = false;   // the heap form points into the replica's OWN buffers
        }
        ok(strOk, "7 strings: SSO below 16, own buffer from 16, byte-exact", nullptr);
        ut::UtReplicaCapture back;
        const bool read = ut::utReplicaReadIdentity(a.bytes, &back);
        bool same = read && strcmp(back.record, id.record) == 0 && back.seed == id.seed &&
                    back.var1 == id.var1 && back.var2 == id.var2 && back.b8 == id.b8;
        for (int k = 0; k < 7 && same; ++k) same = strcmp(back.str[k], id.str[k]) == 0;
        ok(same, "replica -> identity: every field back", nullptr);
        const bool rebuilt = read && ut::utReplicaFromIdentity(&b, back);
        unsigned char ca[ut::kUtReplicaSize], cb[ut::kUtReplicaSize];
        memcpy(ca, a.bytes, sizeof(ca));
        memcpy(cb, b.bytes, sizeof(cb));
        for (int k = 0; k < 7; ++k) {   // the heap pointers point into different replicas
            if (u32(ca + ut::kUtReplicaStr[k] + 0x14) >= 16u) memset(ca + ut::kUtReplicaStr[k], 0, 4);
            if (u32(cb + ut::kUtReplicaStr[k] + 0x14) >= 16u) memset(cb + ut::kUtReplicaStr[k], 0, 4);
        }
        ok(rebuilt && memcmp(ca, cb, sizeof(ca)) == 0,
           "row -> bytes -> row -> the SAME bytes (pointer words aside)", nullptr);
        ut::UtReplicaCapture badId = id;
        badId.b8 = 300;
        ok(!ut::utReplicaFromIdentity(&b, badId), "b8 > 255 refused", nullptr);
        badId = id;
        badId.str[3][0] = '\x7F';
        ok(!ut::utReplicaFromIdentity(&b, badId), "a non-printable string refused", nullptr);
        badId = id;
        badId.str[0][0] = 0;
        ok(!ut::utReplicaFromIdentity(&b, badId), "an empty base refused", nullptr);
        badId = id;
        memset(badId.str[2], 'a', 259);
        badId.str[2][259] = 0;
        ok(ut::utReplicaFromIdentity(&b, badId), "a 259-character string still fits", nullptr);
        memset(badId.str[2], 'a', 260);   // no terminator inside kUtIdStrMax
        ok(!ut::utReplicaFromIdentity(&b, badId), "an unterminated string refused", nullptr);
    }

    {   // a teardown never destroys the object of a journalled take
        const unsigned ids[4] = {101, 102, 103, 104};
        int destroyed = 0, forgotten = 0;
        bool takenDestroyed = false;
        for (int i = 0; i < 4; ++i) {
            if (ut::utProtoTeardownAction(ids[i], 103) == ut::kUtProtoForget) {
                ++forgotten;
            } else {
                ++destroyed;
                if (ids[i] == 103) takenDestroyed = true;
            }
        }
        ok(forgotten == 1 && destroyed == 3 && !takenDestroyed,
           "teardown: the journalled take is forgotten, never destroyed", nullptr);
        ok(ut::utProtoTeardownAction(101, 0) == ut::kUtProtoDestroy &&
               ut::utProtoTeardownAction(0, 0) == ut::kUtProtoDestroy,
           "teardown: no journalled take -> every prototype destroyed", nullptr);
    }

    {   // the row-scroll shift (ut_protoshift.h utProtoShift) over a fake mod sack
        // A 2x2 group (Helms): 8 slots a row, 7 slot rows a window = 56, 21 slot rows = 168
        // records. The fake engine: objects by id (alive / destroyed), a 16 x 15 sack whose AddItem
        // REFUSES an overlap (the strict reading), and a fault injected at the k-th RemoveItem /
        // AddItem / create.
        enum { kIds = 32768 };   // the 2000-shift timing loop uses ~16k ids
        struct World {
            bool alive[kIds];
            bool inSack[kIds];
            int col[kIds], row[kIds], w[kIds], h[kIds];
            unsigned nextId;
            int removes, adds, creates, destroys, doubleDestroy;
            int failRemoveAt, failAddAt, failCreateAt;
            int failDestroyAt;      // the k-th destroy faults (the object stays alive:
            unsigned faultedId;     // its state is unknown - the mod must never touch it again)
            int touchFaulted;
            unsigned long long seqOf[480];
        };
        static World W;
        static const char* recs[480];   // 168 for the 2x2 group; 480 for the earlier 1x1 group
        static char recText[480][32];
        for (int k = 0; k < 480; ++k) {
            snprintf(recText[k], sizeof(recText[k]), "records/item/helm%03d.dbr", k);
            recs[k] = recText[k];
        }
        struct Ops {
            World* w;
            const ut::UtProtoPlace* places;
            bool overlaps(unsigned self, int c, int sr, int fw, int fh) {
                for (unsigned o = 1; o < w->nextId; ++o) {
                    if (o == self || !w->inSack[o]) continue;
                    if (c < w->col[o] + w->w[o] && w->col[o] < c + fw && sr < w->row[o] + w->h[o] &&
                        w->row[o] < sr + fh)
                        return true;
                }
                return false;
            }
            bool remove(unsigned id) {
                if (w->faultedId && id == w->faultedId) ++w->touchFaulted;
                if (++w->removes == w->failRemoveAt) return false;
                w->inSack[id] = false;
                return true;
            }
            bool add(void* item, unsigned id, int c, int sr) {
                if (w->faultedId && id == w->faultedId) ++w->touchFaulted;
                if (++w->adds == w->failAddAt) return false;
                if ((unsigned)(size_t)item != id || !w->alive[id]) return false;
                if (overlaps(id, c, sr, w->w[id], w->h[id])) return false;
                w->inSack[id] = true;
                w->col[id] = c;
                w->row[id] = sr;
                return true;
            }
            bool destroy(void* item) {
                const unsigned id = (unsigned)(size_t)item;
                if (w->faultedId && id == w->faultedId) ++w->touchFaulted;
                if (++w->destroys == w->failDestroyAt) {
                    w->faultedId = id;
                    return false;
                }
                if (!w->alive[id]) ++w->doubleDestroy;
                w->alive[id] = false;
                return true;
            }
            bool create(int i, ut::UtProtoLive* out) {
                if (++w->creates == w->failCreateAt) return false;
                if (w->nextId >= (unsigned)kIds) return false;   // never past the fake's id space
                const ut::UtProtoPlace& p = places[i];
                const unsigned id = w->nextId++;
                w->alive[id] = true;
                w->w[id] = p.w;
                w->h[id] = p.h;
                if (!add((void*)(size_t)id, id, p.col, p.row)) {
                    w->alive[id] = false;   // the protoBuild body destroys what it cannot place
                    return false;
                }
                out->item = (void*)(size_t)id;
                out->id = id;
                out->record = p.record;
                ut::utShiftCopyPlace(*out, p);
                out->seq = w->seqOf[(p.record - recText[0]) / 32];
                return true;
            }
        };
        static ut::UtProtoLive live[ut::kUtProtoMax], scratch[ut::kUtProtoMax];
        static ut::UtProtoPlace places[ut::kUtProtoMax];
        static unsigned long long seq[ut::kUtProtoMax];
        int count = 0;
        unsigned gen = 0;
        // the group's geometry: 8 x 7 slots of 2x2, 168 records (changes it below)
        int gPerRow = 8, gSlotW = 2, gSlotH = 2, gRows = 7, gTotal = 168;
        bool gMixed = false;   // footprints differ inside a slot: centred as the model places them
        auto window = [&](int off) {   // the model's placeWindow for this group
            int wn = 0;
            for (int k = off * gPerRow; k < (off + gRows) * gPerRow && k < gTotal; ++k) {
                ut::UtProtoPlace& p = places[wn];
                p.record = recs[k];
                p.slotCol = (k % gPerRow) * gSlotW;
                p.slotRow = (k / gPerRow - off) * gSlotH;
                p.slotW = gSlotW;
                p.slotH = gSlotH;
                p.w = gMixed ? 1 + k % gSlotW : gSlotW;   // per record: the same item, the same size
                p.h = gMixed ? 1 + (k / 2) % gSlotH : gSlotH;
                p.col = p.slotCol + (gSlotW - p.w) / 2;
                p.row = p.slotRow + (gSlotH - p.h) / 2;
                seq[wn] = W.seqOf[k];
                ++wn;
            }
            return wn;
        };
        auto reset = [&]() {
            memset(&W, 0, sizeof(W));
            W.nextId = 1;
            count = 0;
        };
        auto fullBuild = [&](int off) {   // protoBuild into an empty list
            Ops o = {&W, places};
            const int wn = window(off);
            count = 0;
            for (int i = 0; i < wn; ++i)
                if (o.create(i, &live[count])) ++count;
            ++gen;
        };
        auto teardown = [&]() {   // protoDestroyAll: remove + destroy every listed prototype
            Ops o = {&W, places};
            const int saveR = W.failRemoveAt;
            W.failRemoveAt = 0;
            for (int j = count - 1; j >= 0; --j) {
                o.remove(live[j].id);
                o.destroy(live[j].item);
            }
            W.failRemoveAt = saveR;
            count = 0;
        };
        // every listed prototype is alive and in the sack at its listed cell; every alive object
        // is listed (no orphan, no leak); the list is the window's places in order
        auto exact = [&](int wn, bool inOrder) {
            int aliveN = 0;
            for (unsigned id = 1; id < W.nextId; ++id) aliveN += W.alive[id] ? 1 : 0;
            if (aliveN != count || W.doubleDestroy) return false;
            for (int j = 0; j < count; ++j) {
                const unsigned id = live[j].id;
                if (!W.alive[id] || !W.inSack[id] || W.col[id] != live[j].col || W.row[id] != live[j].row)
                    return false;
                if (inOrder && (j >= wn || live[j].record != places[j].record ||
                                live[j].col != places[j].col || live[j].row != places[j].row))
                    return false;
            }
            return !inOrder || count == wn;
        };
        auto shift = [&](int off, ut::UtShiftCounts* c) {
            Ops o = {&W, places};
            const int wn = window(off);
            return ut::utProtoShift(live, &count, places, seq, wn, o, c, &gen, scratch);
        };

        reset();
        fullBuild(0);
        ok(count == 56 && exact(56, true), "the fake full build: 56 prototypes", nullptr);
        unsigned idsBefore[56];
        for (int j = 0; j < 56; ++j) idsBefore[j] = live[j].id;
        ut::UtShiftCounts c;
        unsigned g0 = gen;
        int sr = shift(1, &c);
        char sd[160];
        snprintf(sd, sizeof(sd), "moved %d created %d destroyed %d stayed %d gen +%u", c.moved,
                 c.created, c.destroyed, c.stayed, gen - g0);
        ok(sr == ut::kUtShiftDone && c.moved == 48 && c.created == 8 && c.destroyed == 8 &&
               c.stayed == 0 && c.failed == 0 && exact(window(1), true) && gen != g0,
           "one row down: moved 48, created 8, destroyed 8", sd);
        bool same = true;   // the kept ones are the SAME objects (ids 9..56 of the first window)
        for (int j = 0; j < 48; ++j) same = same && live[j].id == idsBefore[j + 8];
        ok(same, "the moved prototypes are the same objects (ids kept)", nullptr);
        g0 = gen;
        sr = shift(0, &c);
        ok(sr == ut::kUtShiftDone && c.moved == 48 && c.created == 8 && c.destroyed == 8 &&
               exact(window(0), true) && gen != g0,
           "one row up: moved 48, created 8, destroyed 8", nullptr);
        sr = shift(3, &c);
        snprintf(sd, sizeof(sd), "moved %d created %d destroyed %d", c.moved, c.created, c.destroyed);
        ok(sr == ut::kUtShiftDone && c.moved == 32 && c.created == 24 && c.destroyed == 24 &&
               exact(window(3), true),
           "a coalesced 3-row step: moved 32, created 24, destroyed 24", sd);
        sr = shift(10, &c);
        ok(sr == ut::kUtShiftDone && c.moved == 0 && c.created == 56 && c.destroyed == 56 &&
               exact(window(10), true),
           "a window step (no overlap): created 56, destroyed 56", nullptr);
        sr = shift(14, &c);   // the last full window: 14..20
        ok(sr == ut::kUtShiftDone && c.moved == 24 && c.created == 32 && exact(window(14), true),
           "to the last window: moved 24, created 32", nullptr);
        g0 = gen;
        sr = shift(14, &c);
        ok(sr == ut::kUtShiftDone && c.stayed == 56 && c.moved == 0 && c.created == 0 &&
               c.destroyed == 0 && exact(window(14), true) && gen != g0,
           "the same window again: 56 unchanged, the generation still bumped", nullptr);
        W.seqOf[14 * 8 + 5] = 77;   // a deposit gave record 117 a (newer) journal row
        sr = shift(14, &c);
        ok(sr == ut::kUtShiftDone && c.stayed == 55 && c.created == 1 && c.destroyed == 1 &&
               exact(window(14), true) && live[5].seq == 77,
           "a changed journal row: that record re-created, 55 unchanged", nullptr);
        teardown();
        ok(exact(0, false) && count == 0, "the teardown after shifts leaves nothing alive",
           nullptr);
        {
            ut::UtShiftCounts z;
            int zc = 0;
            Ops o = {&W, places};
            const int wn = window(0);
            ok(ut::utProtoShift(live, &zc, places, seq, wn, o, &z, &gen, scratch) ==
                   ut::kUtShiftNotApplied && zc == 0,
               "no live list -> not applied, nothing touched", nullptr);
        }

        {   // the shift's OWN cost (matching, the four phases, the reorder) over the fake engine: the
            // engine's CreateItem / DestroyObjectEx / sack calls are not in it (the in-game log has those)
            reset();
            fullBuild(0);
            const int kRuns = 2000;
            const auto a = std::chrono::steady_clock::now();
            for (int k = 0; k < kRuns; ++k) shift((k & 1) ? 0 : 1, &c);
            const auto b = std::chrono::steady_clock::now();
            const double us = std::chrono::duration<double, std::micro>(b - a).count() / kRuns;
            printf("[proto]  info  a one-row shift of 56 (fake engine, incl. its sack model): "
                   "%.1f us each\n", us);
            teardown();
        }
        // Faults: the k-th RemoveItem, the k-th AddItem or the k-th create fails, for every k a
        // one-row and a three-row step reach. The list stays exact; after an abort the full
        // teardown leaves NO orphan (nothing alive that is not destroyed) and an empty sack.
        int cases = 0, badCases = 0;
        for (int kind = 0; kind < 3; ++kind) {
            for (int step = 1; step <= 3; step += 2) {
                for (int k = 1; k <= 60; ++k) {
                    reset();
                    fullBuild(2);
                    W.removes = W.adds = W.creates = 0;
                    if (kind == 0) W.failRemoveAt = k;
                    if (kind == 1) W.failAddAt = k;
                    if (kind == 2) W.failCreateAt = k;
                    const int wn = window(2 + step);
                    sr = shift(2 + step, &c);
                    ++cases;
                    bool fine;
                    if (sr == ut::kUtShiftAborted) {
                        // movers may be out of the sack (listed): exact() does not apply here
                        // every listed one is alive; nothing alive is unlisted
                        int aliveN = 0;
                        for (unsigned id = 1; id < W.nextId; ++id) aliveN += W.alive[id] ? 1 : 0;
                        fine = c.aborted == 1 && aliveN == count && !W.doubleDestroy;
                        for (int j = 0; j < count; ++j) fine = fine && W.alive[live[j].id];
                        teardown();
                        int still = 0, inSack = 0;
                        for (unsigned id = 1; id < W.nextId; ++id) {
                            still += W.alive[id] ? 1 : 0;
                            inSack += W.inSack[id] ? 1 : 0;
                        }
                        fine = fine && still == 0 && inSack == 0 && !W.doubleDestroy;
                    } else {
                        fine = sr == ut::kUtShiftDone &&
                               exact(wn, c.failed == 0) && c.created + c.failed == 8 * step;
                        if (c.failed) {   // the refused one is simply missing, like a full build's
                            fine = fine && count == wn - c.failed && kind != 0;   // a refused create (its own add or CreateItem)
                        }
                        teardown();
                        int still = 0;
                        for (unsigned id = 1; id < W.nextId; ++id) still += W.alive[id] ? 1 : 0;
                        fine = fine && still == 0;
                    }
                    if (!fine) {
                        ++badCases;
                        if (badCases < 4) {
                            snprintf(sd, sizeof(sd), "kind %d step %d k %d sr %d", kind, step, k, sr);
                            ok(false, "fault case", sd);
                        }
                    }
                }
            }
        }
        snprintf(sd, sizeof(sd), "%d fault cases, %d badCases", cases, badCases);
        ok(badCases == 0, "a fault mid-shift leaves no orphan (remove / add / create)", sd);

        // other geometries and a failing destroy
        auto noneAlive = [&]() {
            int still = 0, inSack = 0;
            for (unsigned id = 1; id < W.nextId; ++id) {
                still += W.alive[id] ? 1 : 0;
                inSack += W.inSack[id] ? 1 : 0;
            }
            return still == 0 && inSack == 0 && !W.doubleDestroy;
        };
        // (a) a mixed-footprint group: 5 x 5 slots of 3x3, items 1..3 wide / tall centred in them
        gPerRow = 5;
        gSlotW = 3;
        gSlotH = 3;
        gRows = 5;
        gTotal = 168;
        gMixed = true;
        reset();
        fullBuild(0);
        int offCentre = 0;
        for (int j = 0; j < count; ++j) offCentre += live[j].col != live[j].slotCol ? 1 : 0;
        bool mixOk = count == 25 && exact(25, true) && offCentre > 0;
        sr = shift(1, &c);
        mixOk = mixOk && sr == ut::kUtShiftDone && c.moved == 20 && c.created == 5 && c.destroyed == 5 &&
                c.failed == 0 && exact(window(1), true);
        sr = shift(4, &c);
        mixOk = mixOk && sr == ut::kUtShiftDone && c.moved == 10 && c.created == 15 &&
                c.destroyed == 15 && c.failed == 0 && exact(window(4), true);
        sr = shift(3, &c);
        mixOk = mixOk && sr == ut::kUtShiftDone && c.moved == 20 && c.created == 5 &&
                exact(window(3), true);
        teardown();
        snprintf(sd, sizeof(sd), "%d off-centre of 25; last: moved %d created %d destroyed %d", offCentre,
                 c.moved, c.created, c.destroyed);
        ok(mixOk && noneAlive(), "a mixed-footprint group (centred items): shifts exact",
           sd);
        int mixBad = 0, mixCases = 0;   // ... and its RemoveItem / AddItem faults
        for (int kind = 0; kind < 2; ++kind) {
            for (int k = 1; k <= 30; ++k) {
                reset();
                fullBuild(2);
                W.removes = W.adds = W.creates = 0;
                if (kind == 0) W.failRemoveAt = k;
                if (kind == 1) W.failAddAt = k;
                const int wn = window(3);
                sr = shift(3, &c);
                ++mixCases;
                bool fine = sr == ut::kUtShiftAborted;
                if (sr == ut::kUtShiftDone)   // a refused create is simply missing, as in a full build
                    fine = c.failed ? exact(wn, false) && count == wn - c.failed : exact(wn, true);
                teardown();
                fine = fine && noneAlive();
                if (!fine) ++mixBad;
            }
        }
        snprintf(sd, sizeof(sd), "%d fault cases, %d bad", mixCases, mixBad);
        ok(mixBad == 0, "... a fault mid-shift there leaves no orphan", sd);
        // (b) a 1x1 group with the full 240-place window (kUtProtoMax: the matcher's worst case)
        gPerRow = 16;
        gSlotW = 1;
        gSlotH = 1;
        gRows = 15;
        gTotal = 480;
        gMixed = false;
        reset();
        fullBuild(0);
        bool fullOk = count == ut::kUtProtoMax && exact(ut::kUtProtoMax, true);
        sr = shift(1, &c);
        fullOk = fullOk && sr == ut::kUtShiftDone && c.moved == 224 && c.created == 16 &&
                 c.destroyed == 16 && exact(window(1), true);
        sr = shift(4, &c);
        fullOk = fullOk && sr == ut::kUtShiftDone && c.moved == 192 && c.created == 48 &&
                 c.destroyed == 48 && exact(window(4), true);
        sr = shift(15, &c);   // the last window (rows 15..29): 4 rows overlap
        fullOk = fullOk && sr == ut::kUtShiftDone && c.moved == 64 && c.created == 176 &&
                 c.destroyed == 176 && exact(window(15), true);
        sr = shift(0, &c);    // back to the top: no overlap
        fullOk = fullOk && sr == ut::kUtShiftDone && c.moved == 0 && c.created == 240 &&
                 c.destroyed == 240 && exact(window(0), true);
        sr = shift(0, &c);
        fullOk = fullOk && sr == ut::kUtShiftDone && c.stayed == 240 && exact(window(0), true);
        snprintf(sd, sizeof(sd), "last: stayed %d, %u ids used", c.stayed, W.nextId - 1);
        teardown();
        ok(fullOk && noneAlive(), "a 1x1 group, the full 240-place window: shifts exact",
           sd);
        {
            reset();
            fullBuild(0);
            const auto a = std::chrono::steady_clock::now();
            for (int k = 0; k < 100; ++k) shift((k & 1) ? 0 : 1, &c);
            const auto b = std::chrono::steady_clock::now();
            printf("[proto]  info  a one-row shift of 240 (1x1, fake engine): %.1f us "
                   "each\n", std::chrono::duration<double, std::micro>(b - a).count() / 100);
            teardown();
        }
        // (c) a failing destroy (the 2x2 group): the leaving object whose DestroyObjectEx faults is
        // off the list anyway (protoDestroyAll's rule) and never touched again - no double destroy
        gPerRow = 8;
        gSlotW = 2;
        gSlotH = 2;
        gRows = 7;
        gTotal = 168;
        int destroyBad = 0;
        for (int k = 1; k <= 8; ++k) {
            reset();
            fullBuild(0);
            W.destroys = 0;
            W.failDestroyAt = k;
            sr = shift(1, &c);
            const int wn = window(1);
            bool fine = sr == ut::kUtShiftDone && c.destroyed == 7 && count == wn && W.faultedId != 0 &&
                        !W.inSack[W.faultedId];
            for (int j = 0; j < count && fine; ++j) {
                const unsigned id = live[j].id;
                fine = id != W.faultedId && W.alive[id] && W.inSack[id] && W.col[id] == live[j].col &&
                       W.row[id] == live[j].row && live[j].record == places[j].record;
            }
            teardown();
            int still = 0;
            for (unsigned id = 1; id < W.nextId; ++id) still += W.alive[id] ? 1 : 0;
            fine = fine && still == 1 && W.alive[W.faultedId] && W.touchFaulted == 0 && !W.doubleDestroy;
            if (!fine) ++destroyBad;
        }
        snprintf(sd, sizeof(sd), "8 cases, %d bad", destroyBad);
        ok(destroyBad == 0, "a failing destroy: off the list, never touched again", sd);
    }

    {   // the shift's refusal guards (ut_protoshift.h utShiftAllowed, protoShift's)
        ut::UtShiftFacts sfBase = {};
        sfBase.sack = true;
        sfBase.liveCount = 56;
        sfBase.places = 56;
        int sfBad = ut::utShiftAllowed(sfBase) ? 0 : 1;
        for (int sfRow = 0; sfRow < 12; ++sfRow) {
            ut::UtShiftFacts f = sfBase;
            if (sfRow == 0) f.sack = false;
            if (sfRow == 1) f.sackFull = true;
            if (sfRow == 2) f.sackStale = true;
            if (sfRow == 3) f.recheckCell = true;
            if (sfRow == 4) f.keepId = 77;          // a take during a scroll: still in the keep slot
            if (sfRow == 5) f.forgotten = 1;        // its hand-out failed: forgotten
            if (sfRow == 6) f.forgotOverflow = true;
            if (sfRow == 7) f.liveCount = 0;
            if (sfRow == 8) f.places = 0;
            if (sfRow == 9) f.places = ut::kUtProtoMax + 1;
            if (sfRow == 10) f.liveCount = -1;
            if (sfRow == 11) f.cellStale = true;    // a UI scale change re-celled
            if (ut::utShiftAllowed(f)) ++sfBad;
        }
        ut::UtShiftFacts sfFull = sfBase;   // the take handed out: off the list, keep slot empty
        sfFull.places = ut::kUtProtoMax;
        sfFull.liveCount = ut::kUtProtoMax - 1;
        ok(sfBad == 0 && ut::utShiftAllowed(sfFull),
           "the shift refuses a pending or forgotten take, a sack that retires, no "
           "list, a bad count", nullptr);
        ut::UtShiftFacts sfCell = sfBase;
        sfCell.cellStale = true;
        ok(!ut::utShiftAllowed(sfCell) && ut::utShiftAllowed(sfBase),
           "prototypes placed on a stale cell (a UI scale change re-celled the sack) "
           "refuse the shift - the fact cellStale; the full rebuild re-cells", nullptr);
    }

    printf(g_fail ? "[proto]  FAILED: %d of %d\n" : "[proto]  ALL PASS (%d checks)\n",
           g_fail ? g_fail : g_ran, g_ran);
    return g_fail ? 1 : 0;
}
