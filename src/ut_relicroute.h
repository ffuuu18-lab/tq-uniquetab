// ut_relicroute.h - the relic and charm rules of the collection, as PURE logic: where a deposited
// relic or charm goes, which stored row a take hands out, the counts a slot shows, and the words of
// the relic log line and the CSV cells. No Windows, no engine, no file and no globals, so
// tools\test_store.cpp proves the functions the shipped build calls.
//
// A relic / charm record has N shards (its completedRelicLevel) and a table of completion bonuses.
// Its page has ONE red slot per record (the partial pieces, counted in SHARDS) and ONE green slot
// per bonus of the table (the completed items with that bonus, counted in items). The replica says
// which: var1 (+0x80) is the shard count and relicBonus (+0x64) the completion bonus, empty while
// the piece is partial. var1 0 is ONE shard too: ItemRelic::InitializeItem (Game.dll 0x1C3914)
// sets no level for var1 0, so the piece keeps its level 1 - a dropped piece reads var1 0, a piece
// the game (or a take) builds with var1 1 reads 1. utRelicShardsOf is that rule; every count,
// route and choice below goes through it, and a row keeps its var1 as stored.
//
// The journal keeps one row per stored item (ut_rescue.h); the choices below read a row's facts
// (UtRelicRowFacts) and never the rows themselves, so the journal hands them a copy under its lock.
#pragma once

#include <stdio.h>
#include <string.h>

namespace ut {

// ---- the routing of a captured relic / charm ------------------------------------------------
enum UtRelicRoute {
    kUtRelicRouteNone = 0,       // not decided (no capture yet)
    kUtRelicRouteRed,            // a partial piece: the record's red slot
    kUtRelicRouteGreen,          // a completed item: the green slot of its bonus
    kUtRelicRouteRefuseBonus,    // completed, with a bonus its record's table does not list
    kUtRelicRouteRefuseShards    // a shard count the record does not allow, or a bonus that does
                                 // not fit the count (a partial with one, a completed without)
};

const unsigned kUtRelicBonusMax = 260;   // one folded bonus record path with its NUL

// The fold every comparison uses: lower case, '\\' -> '/' (the journal key's fold). False when
// `in` does not fit `cap` (out is then "").
inline bool utRelicFold(const char* in, char* out, size_t cap) {
    if (!out || !cap) return false;
    out[0] = 0;
    if (!in) return true;
    size_t i = 0;
    for (; in[i]; ++i) {
        if (i + 1 >= cap) {
            out[0] = 0;
            return false;
        }
        char c = in[i];
        if (c == '\\') c = '/';
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        out[i] = c;
    }
    out[i] = 0;
    return true;
}

// Two bonus record paths name the same record: case-insensitive, '/' and '\\' alike. Null = "".
inline bool utRelicFoldSame(const char* a, const char* b) {
    if (!a) a = "";
    if (!b) b = "";
    for (;; ++a, ++b) {
        char x = *a, y = *b;
        if (x == '\\') x = '/';
        if (y == '\\') y = '/';
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return false;
        if (!x) return true;
    }
}

// The shards a piece of var1 `var1` holds: var1 0 and 1 are both ONE shard (see the top).
inline unsigned utRelicShardsOf(unsigned var1) { return var1 == 0u ? 1u : var1; }

// THE routing. `shards` = the record's N; `table` / `tableN` = its completion bonuses (the
// non-zero-weight entries, in table order, any spelling). `bonusIndex` (may be null) gets the
// table entry of a green verdict, else -1. k = utRelicShardsOf(var1) (var1 0 = one shard):
//   k < N, no bonus                -> red
//   k == N, the bonus in the table -> green (that entry)
//   k == N, the bonus not in it    -> refused (a completion bonus its table does not list)
//   anything else (k > N, a partial with a bonus, a completed without one, N 0)
//                                  -> refused (a shard count its record does not allow)
inline int utRelicRoute(unsigned var1, unsigned shards, const char* bonus,
                        const char* const* table, int tableN, int* bonusIndex) {
    if (bonusIndex) *bonusIndex = -1;
    const bool hasBonus = bonus && bonus[0];
    const unsigned k = utRelicShardsOf(var1);
    if (shards == 0u || k > shards) return kUtRelicRouteRefuseShards;
    if (k < shards) return hasBonus ? kUtRelicRouteRefuseShards : kUtRelicRouteRed;
    if (!hasBonus) return kUtRelicRouteRefuseShards;
    for (int i = 0; table && i < tableN; ++i) {
        if (table[i] && table[i][0] && utRelicFoldSame(table[i], bonus)) {
            if (bonusIndex) *bonusIndex = i;
            return kUtRelicRouteGreen;
        }
    }
    return kUtRelicRouteRefuseBonus;
}

inline bool utRelicRouteAccepted(int route) {
    return route == kUtRelicRouteRed || route == kUtRelicRouteGreen;
}

// The refusal words (the deposit gate's utDepositWhyText uses the same).
inline const char* utRelicRouteWhy(int route) {
    switch (route) {
    case kUtRelicRouteRed: return "red";
    case kUtRelicRouteGreen: return "green";
    case kUtRelicRouteRefuseBonus: return "a completion bonus its record's table does not list";
    case kUtRelicRouteRefuseShards: return "a shard count its record does not allow";
    default: return "not routed";
    }
}

// ---- the stored rows of one record, as the choices read them ---------------------------------
struct UtRelicRowFacts {
    unsigned long long seq;         // the journal row
    unsigned long long deposited;   // its deposit time (FILETIME units); ties go by seq
    unsigned int var1;              // its var1 as stored (utRelicShardsOf = its shards)
    int pending;                    // 0 settled, 1 "in" (no save since the deposit), 2 "out"
    bool unresolved;                // a load's pending row still waiting for the container check
    char bonus[kUtRelicBonusMax];   // relicBonus folded; "" = a partial piece (the red slot)
};

const int kUtRelicPendNone = 0, kUtRelicPendIn = 1, kUtRelicPendOut = 2;

inline bool utRelicRowIsRed(const UtRelicRowFacts& r) { return r.bonus[0] == 0; }

// `a` was deposited before `b` (the time, then the row order).
inline bool utRelicRowOlder(const UtRelicRowFacts& a, const UtRelicRowFacts& b) {
    if (a.deposited != b.deposited) return a.deposited < b.deposited;
    return a.seq < b.seq;
}

// `a` goes out before `b`: a settled row before a row pending in, then the older one.
inline bool utRelicRowFirst(const UtRelicRowFacts& a, const UtRelicRowFacts& b) {
    const bool sa = a.pending == kUtRelicPendNone, sb = b.pending == kUtRelicPendNone;
    if (sa != sb) return sa;
    return utRelicRowOlder(a, b);
}

// The green slot of `bonus` (any spelling): the row a take hands out - the OLDEST row of that
// bonus not pending out, settled rows before rows pending in. -1 = the slot is empty.
inline int utRelicGreenChoice(const UtRelicRowFacts* rows, int n, const char* bonus) {
    if (!bonus || !bonus[0]) return -1;
    int best = -1;
    for (int i = 0; rows && i < n; ++i) {
        const UtRelicRowFacts& r = rows[i];
        if (utRelicRowIsRed(r) || r.pending == kUtRelicPendOut || !utRelicFoldSame(r.bonus, bonus))
            continue;
        if (best < 0 || utRelicRowFirst(r, rows[best])) best = i;
    }
    return best;
}

// The green slot's count: its rows not pending out.
inline unsigned utRelicGreenCount(const UtRelicRowFacts* rows, int n, const char* bonus) {
    unsigned c = 0;
    if (!bonus || !bonus[0]) return 0;
    for (int i = 0; rows && i < n; ++i)
        if (!utRelicRowIsRed(rows[i]) && rows[i].pending != kUtRelicPendOut &&
            utRelicFoldSame(rows[i].bonus, bonus))
            ++c;
    return c;
}

// The red slot's count: the SHARDS of the record's partial rows not pending out (a row of var1 0
// is one shard).
inline unsigned utRelicRedShards(const UtRelicRowFacts* rows, int n) {
    unsigned c = 0;
    for (int i = 0; rows && i < n; ++i)
        if (utRelicRowIsRed(rows[i]) && rows[i].pending != kUtRelicPendOut)
            c += utRelicShardsOf(rows[i].var1);
    return c;
}

// The red slot's take gives ONE shard. Its source among the partial rows that are neither pending
// out nor unresolved:
//   1. a row of ONE shard (var1 0 or 1) - handed out whole, as stored (settled before pending in,
//      the oldest first);
//   2. else the oldest settled row - split (*split = true);
//   3. else the oldest row pending in - split.
// -1 = nothing to hand out.
inline int utRelicShardSource(const UtRelicRowFacts* rows, int n, bool* split) {
    if (split) *split = false;
    int one = -1, settled = -1, in = -1;
    for (int i = 0; rows && i < n; ++i) {
        const UtRelicRowFacts& r = rows[i];
        if (!utRelicRowIsRed(r) || r.pending == kUtRelicPendOut || r.unresolved) continue;
        if (utRelicShardsOf(r.var1) == 1u) {
            if (one < 0 || utRelicRowFirst(r, rows[one])) one = i;
        } else if (r.pending == kUtRelicPendNone) {
            if (settled < 0 || utRelicRowOlder(r, rows[settled])) settled = i;
        } else if (r.pending == kUtRelicPendIn) {
            if (in < 0 || utRelicRowOlder(r, rows[in])) in = i;
        }
    }
    if (one >= 0) return one;
    const int src = settled >= 0 ? settled : in;
    if (src >= 0 && split) *split = true;
    return src;
}

// The var1 of the red slot's prototype, built from its source row (`rowVar1` as stored): a row of
// one shard is handed out whole, so the prototype is that row byte for byte (var1 0 stays 0); a
// row that is split hands out a 1-shard piece (var1 1).
inline unsigned utRelicProtoVar1(unsigned rowVar1) {
    return utRelicShardsOf(rowVar1) > 1u ? 1u : rowVar1;
}

// ---- the relic log line (DEBUG) ---------------------------------------------------------------
// "relic: <record> var1=<n> of <N> relic='<relic>' relicBonus='<bonus>' seed=<s> -> <tail>"
// (the log adds its own level tag). The tail is utRelicRouteTail's or utRelicShardTail's.
inline void utRelicDebugLine(char* out, size_t cap, const char* record, unsigned var1,
                             unsigned shards, const char* relic, const char* relicBonus,
                             unsigned seed, const char* tail) {
    if (!out || !cap) return;
    _snprintf_s(out, cap, _TRUNCATE, "relic: %s var1=%u of %u relic='%s' relicBonus='%s' seed=%u -> %s",
                record ? record : "", var1, shards, relic ? relic : "", relicBonus ? relicBonus : "",
                seed, tail ? tail : "");
}

// A refused deposit's facts for its one REFUSED line: "var1=<n> of <N> relicBonus='<bonus>'
// seed=<s>" (the line names the record and the reason itself).
inline void utRelicRefusalFacts(char* out, size_t cap, unsigned var1, unsigned shards,
                                const char* relicBonus, unsigned seed) {
    if (!out || !cap) return;
    _snprintf_s(out, cap, _TRUNCATE, "var1=%u of %u relicBonus='%s' seed=%u", var1, shards,
                relicBonus ? relicBonus : "", seed);
}

// A deposit's tail: "red", "green <bonus>", or "refused (<why>)".
inline void utRelicRouteTail(int route, const char* bonus, char* out, size_t cap) {
    if (!out || !cap) return;
    if (route == kUtRelicRouteGreen)
        _snprintf_s(out, cap, _TRUNCATE, "green %s", bonus ? bonus : "");
    else if (route == kUtRelicRouteRed)
        _snprintf_s(out, cap, _TRUNCATE, "red");
    else
        _snprintf_s(out, cap, _TRUNCATE, "refused (%s)", utRelicRouteWhy(route));
}

// The red take's tail: "one shard of row <seq>, <left> left".
inline void utRelicShardTail(unsigned long long seq, unsigned left, char* out, size_t cap) {
    if (!out || !cap) return;
    _snprintf_s(out, cap, _TRUNCATE, "one shard of row %llu, %u left", seq, left);
}

// ---- the CSV cells ----------------------------------------------------------------------------
// `tier` 1..3 (the record's place in its block) -> "Normal" / "Epic" / "Legendary"; else "".
inline const char* utRelicTierWord(int tier) {
    switch (tier) {
    case 1: return "Normal";
    case 2: return "Epic";
    case 3: return "Legendary";
    default: return "";
    }
}

// The row's shards as "k / N", k = utRelicShardsOf(var1) ("" when the record has no shard count).
inline void utRelicShardsCell(unsigned var1, unsigned shards, char* out, size_t cap) {
    if (!out || !cap) return;
    out[0] = 0;
    if (shards == 0u) return;
    _snprintf_s(out, cap, _TRUNCATE, "%u / %u", utRelicShardsOf(var1), shards);
}

}  // namespace ut
