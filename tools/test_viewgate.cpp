// test_viewgate.cpp - the collection view's state machine (src\ut_viewgate.h), offline.
// NO GAME, NO FILE, NO WINDOWS. The header is included, so this runs exactly the code the mod runs.
//
// A simulated Transfer page: the cached member (REAL or MOD), the prototypes, the engine's tab.
// Every event order up to kDepth events over the alphabet below is enumerated (a DFS over the
// state, so every prefix is a case of its own), and after EVERY step:
//   I1  view OFF  -> the member is the real sack and no prototype exists
//   I2  view ON   -> caravan open, Transfer tab, world up, not MP, no fault, bindings complete
//   I3  every SAVE (the three StreamOut calls after CaravanGoodbye) sees the real sack - with the
//       StreamOut assert (G3) DISABLED, so G2 alone must hold the door
//   I4  every LOAD (the open) sees the real sack
//   I5  once faulted, no toggle ever turns the view on again
//   I6  the getter's other callers (quick-move) never get the mod sack
// Then a MUTANT run with G2's goodbye re-point removed must be caught by I3 and, with G3 enabled,
// rescued at the save - proof the harness can see the failure it guards against.
//
// the owned marks' grid-origin check (src\ut_gridcheck.h) - a measured 1366 x 768 window, the
// records-consistent case, each refusal, and every hover sequence up to depth 5 over 9 hover kinds
// x 4 bound sets against an independent oracle: an ACCEPTED origin always passed check 1 on every
// counted hover, came from two different prototypes with the same origin, lies inside the canvas,
// and is inside the records' window rect or was vouched for by the cursor on two hovers.

#include <stdio.h>
#include <string.h>

#include <limits>

#include "../src/ut_gridcheck.h"
#include "../src/ut_padlayout.h"
#include "../src/ut_slotart.h"
#include "../src/ut_graytex.h"   // the gray icons (pure)
#include "../src/ut_depositgate.h"
#include "../src/ut_viewgate.h"
#include "../src/ut_rowmath.h"     // the per-tick clamp (GD's utClampRow)
#include "../src/ut_costprobe.h"   // the cost meter

namespace {

int g_fail = 0;
long long g_checks = 0;

void ok(bool cond, const char* what, const char* detail) {
    printf("[viewgate]  %s  %-52s %s\n", cond ? "PASS" : "FAIL", what, detail ? detail : "");
    if (!cond) ++g_fail;
}

enum Act {
    aToggle, aFrame, aTab0, aTab1, aTab2, aClose, aOpen, aWorldUp, aWorldDown, aMpOn, aMpOff,
    aFault, aBindings, aCount
};
const char* const kActName[aCount] = {"toggle", "frame", "tab0", "tab1", "tab2", "close", "open",
                                      "worldUp", "worldDown", "mpOn", "mpOff", "fault",
                                      "bindingsIncomplete"};

struct Sim {
    ut::UtViewState vs;
    int member = 0;        // 0 = the real Transfer sack, 1 = the mod sack
    int protos = 0;        // prototypes alive in the mod sack
    int engineTab = -1;    // what the caravan window really shows
    bool engineOpen = false;
    bool faultedEver = false;
    int g3Fired = 0;
};

struct Opts {
    bool g2Goodbye = true;  // the CaravanGoodbye pre-detour re-points (G2)
    bool g3 = false;        // the StreamOut assert (G3)
};

// What viewForceOff does: re-point, then destroy.
void forceOff(Sim* s) {
    s->member = 0;
    s->protos = 0;
}

void applyStep(Sim* s, const ut::UtViewStep& st) {
    if (st.turnedOn) s->protos = 12;
    if (st.turnedOff) forceOff(s);
}

// ---- the take-path model of InventorySack::GetItemUnderPoint --------
// What the caller does with the id it gets. The right-click clones it into the inventory, the held
// pick-up removes it with InventorySack::RemoveItem, the page mouse handler branches on its two
// bools: b1 = pick-up (SetId + RemoveItemFrom*), else b2 = clone (0xC0530), else the HOVER hands
// it to the tooltip only. A take of a PROTOTYPE id is the door's breach; a hover over a prototype
// that gets 0 is the earlier bug (no tooltip).
enum { kIdNone = 0, kIdProto = 1, kIdReal = 2 };
struct UnderOutcome {
    bool takeOfProto;   // a prototype id reached a take branch
    bool tooltip;       // the tooltip branch saw the prototype id
};
typedef bool (*UnderVerdict)(int site, bool modSack, int frame);
UnderOutcome underModel(UnderVerdict verdict, int site, bool modSack, bool verified, int b1,
                        int b2) {
    const int frame = ut::utUnderFrameKind(verified, (unsigned char)b1, (unsigned char)b2);
    const bool refused = verdict(site, modSack, frame);
    const int got = refused ? kIdNone : (modSack ? kIdProto : kIdReal);
    UnderOutcome o = {false, false};
    // The engine branches on the LOW BYTE being non-zero (`cmp byte ptr [ebp+8],0`).
    const bool press = b1 != 0, modified = b2 != 0;
    switch (site) {
    case ut::kUtUnderRightClick:
    case ut::kUtUnderHeldPickup:
        o.takeOfProto = got == kIdProto;
        break;
    case ut::kUtUnderMouseHandler:
        if (press || modified) {
            o.takeOfProto = got == kIdProto;
        } else {
            o.tooltip = got == kIdProto;
        }
        break;
    default:
        break;
    }
    return o;
}
bool verdictRefuseAll(int site, bool modSack, int) {   // the earlier verdict: every site refused
    return modSack && site != ut::kUtUnderOther;
}
bool verdictNoBools(int site, bool modSack, int) {   // a MUTANT: the handler never refused
    return modSack && (site == ut::kUtUnderRightClick || site == ut::kUtUnderHeldPickup);
}

// Returns false (and fills `why`) when an invariant broke during the step.
bool step(Sim* s, int a, const Opts& o, char* why, size_t cap) {
    ++g_checks;
    switch (a) {
    case aToggle:
        applyStep(s, ut::utViewApply(&s->vs, ut::kUtViewEvToggle, 0));
        if (s->faultedEver && s->vs.on) {
            _snprintf_s(why, cap, _TRUNCATE, "I5: ON after a fault");
            return false;
        }
        break;
    case aFrame:
        // The caravan window's per-frame update: SetCaravanMode(current tab), then the active
        // page's accessor, which caches whatever the getter returns.
        if (s->engineOpen) {
            applyStep(s, ut::utViewApply(&s->vs, ut::kUtViewEvModeChanged, s->engineTab));
            if (s->engineTab == 1) {
                const bool sub = ut::utViewSubstitute(true, s->vs.on, s->vs.mode, s->vs.world,
                                                      s->vs.mpUnknown);
                if (sub && s->protos == 0) {
                    _snprintf_s(why, cap, _TRUNCATE, "substituted an EMPTY mod sack");
                    return false;
                }
                s->member = sub ? 1 : 0;
            }
        }
        // I6: the quick-move path calls the same getter from elsewhere.
        if (ut::utViewSubstitute(false, s->vs.on, s->vs.mode, s->vs.world, s->vs.mpUnknown)) {
            _snprintf_s(why, cap, _TRUNCATE, "I6: a non-accessor caller got the mod sack");
            return false;
        }
        break;
    case aTab0:
    case aTab1:
    case aTab2:
        if (s->engineOpen) {
            s->engineTab = a - aTab0;
            applyStep(s, ut::utViewApply(&s->vs, ut::kUtViewEvModeChanged, s->engineTab));
        }
        break;
    case aClose:
        if (s->engineOpen) {
            // CaravanGoodbye PRE-detour, then the three saves 8-25 ms later, NO frame between.
            const ut::UtViewStep st = ut::utViewApply(&s->vs, ut::kUtViewEvGoodbye, 0);
            if (o.g2Goodbye) applyStep(s, st);
            for (int page = 0; page < 3; ++page) {
                if (o.g3 && s->member == 1) {   // StreamOut PRE: re-point, latch, force OFF
                    s->member = 0;
                    ++s->g3Fired;
                    applyStep(s, ut::utViewApply(&s->vs, ut::kUtViewEvStreamOutModSack, 0));
                    forceOff(s);
                    s->faultedEver = true;
                }
                if (s->member != 0) {
                    _snprintf_s(why, cap, _TRUNCATE, "I3: a save streamed the MOD sack");
                    return false;
                }
            }
            s->engineOpen = false;
        }
        break;
    case aOpen:
        if (!s->engineOpen) {
            s->engineOpen = true;
            s->engineTab = 0;   // TQ opens on whatever tab; the next frame sets the mode
            applyStep(s, ut::utViewApply(&s->vs, ut::kUtViewEvOpen, 0));
            if (s->member != 0) {
                _snprintf_s(why, cap, _TRUNCATE, "I4: the load ran into the MOD sack");
                return false;
            }
        }
        break;
    case aWorldUp:
        applyStep(s, ut::utViewApply(&s->vs, ut::kUtViewEvWorldUp, 0));
        break;
    case aWorldDown:
        applyStep(s, ut::utViewApply(&s->vs, ut::kUtViewEvWorldDown, 0));
        break;
    case aMpOn:
        applyStep(s, ut::utViewApply(&s->vs, ut::kUtViewEvMpChanged, 1));
        break;
    case aMpOff:
        applyStep(s, ut::utViewApply(&s->vs, ut::kUtViewEvMpChanged, 0));
        break;
    case aFault:
        applyStep(s, ut::utViewApply(&s->vs, ut::kUtViewEvFault, 0));
        s->faultedEver = true;
        break;
    case aBindings:
        applyStep(s, ut::utViewApply(&s->vs, ut::kUtViewEvBindingsIncomplete, 0));
        break;
    }
    // I1 / I2 after every step (the G2 mutant breaks I1 by construction: only I3 is asked of it).
    if (o.g2Goodbye && !s->vs.on && (s->member != 0 || s->protos != 0)) {
        _snprintf_s(why, cap, _TRUNCATE, "I1: OFF but member=%d protos=%d", s->member, s->protos);
        return false;
    }
    if (s->vs.on && !(s->vs.open && s->vs.mode == 1 && s->vs.world && !s->vs.mpUnknown &&
                      !s->vs.faulted && s->vs.bindingsOk)) {
        _snprintf_s(why, cap, _TRUNCATE, "I2: ON outside its conditions");
        return false;
    }
    return true;
}

struct Walk {
    Opts opts;
    int depth;
    long long cases = 0;
    long long onReached = 0;       // sequences in which the view was ever ON
    long long savedWhileSub = 0;   // closes that happened while the member held the mod sack
    long long g3Fired = 0;
    int failures = 0;
    char firstFail[512] = {0};
};

void dfs(Walk* w, const Sim& s, int* seq, int n) {
    if (n == w->depth) return;
    for (int a = 0; a < aCount; ++a) {
        Sim t = s;
        seq[n] = a;
        const bool subBefore = t.member == 1;
        char why[160] = {0};
        const bool good = step(&t, a, w->opts, why, sizeof(why));
        ++w->cases;
        if (t.vs.on) ++w->onReached;
        if (a == aClose && subBefore) ++w->savedWhileSub;
        w->g3Fired += t.g3Fired - s.g3Fired;
        if (!good) {
            if (!w->failures++) {
                int k = _snprintf_s(w->firstFail, sizeof(w->firstFail), _TRUNCATE, "%s after:", why);
                for (int i = 0; i <= n && k > 0 && k < (int)sizeof(w->firstFail) - 24; ++i)
                    k += _snprintf_s(w->firstFail + k, sizeof(w->firstFail) - k, _TRUNCATE, " %s",
                                     kActName[seq[i]]);
            }
            continue;   // do not extend a broken sequence
        }
        dfs(w, t, seq, n + 1);
    }
}

Sim fresh(bool bindingsOk) {
    Sim s;
    s.vs.bindingsOk = bindingsOk;
    return s;
}

// Run one explicit sequence; returns the final Sim and whether every step held.
bool run(const int* acts, int n, const Opts& o, Sim* out, char* why, size_t cap) {
    Sim s = fresh(true);
    for (int i = 0; i < n; ++i) {
        if (!step(&s, acts[i], o, why, cap)) {
            if (out) *out = s;
            return false;
        }
    }
    if (out) *out = s;
    return true;
}

}  // namespace

int main() {
    char d[512];

    // ---- the pure predicates ---------------------------------------------------------------
    // which sack the substituting getter hands the page
    ok(ut::utViewGetterPicksMod(true, 5, false) && ut::utViewGetterPicksMod(true, 5, true) &&
           ut::utViewGetterPicksMod(true, 0, true) && !ut::utViewGetterPicksMod(true, 0, false) &&
           !ut::utViewGetterPicksMod(false, 5, false) && !ut::utViewGetterPicksMod(false, 0, true) &&
           !ut::utViewGetterPicksMod(true, -1, true),
       "getter: mod sack with prototypes or an empty OWN page; a failed build -> real", "7 rows");
    ok(ut::utViewSubstitute(true, true, 1, true, false) &&
           !ut::utViewSubstitute(false, true, 1, true, false) &&
           !ut::utViewSubstitute(true, false, 1, true, false) &&
           !ut::utViewSubstitute(true, true, 0, true, false) &&
           !ut::utViewSubstitute(true, true, 2, true, false) &&
           !ut::utViewSubstitute(true, true, 1, false, false) &&
           !ut::utViewSubstitute(true, true, 1, true, true),
       "G1 substitute(accessor, on, mode 1, world, state known) only (MP substitutes)",
       "7 combinations");
    {
        ut::UtViewState s;
        int refusals[6];
        refusals[0] = ut::utViewRefusal(s);
        s.bindingsOk = true;
        s.faulted = true;
        refusals[1] = ut::utViewRefusal(s);
        s.faulted = false;
        refusals[2] = ut::utViewRefusal(s);
        s.world = true;
        s.mpUnknown = true;   // only an UNKNOWN session state refuses, not multiplayer
        refusals[3] = ut::utViewRefusal(s);
        s.mpUnknown = false;
        s.mp = true;
        refusals[4] = ut::utViewRefusal(s);
        s.open = true;
        s.mode = 2;
        refusals[5] = ut::utViewRefusal(s);
        s.mode = 1;
        const int allowed = ut::utViewRefusal(s);
        ok(refusals[0] == ut::kUtViewWhyRefusedBindings &&
               refusals[1] == ut::kUtViewWhyRefusedFaulted &&
               refusals[2] == ut::kUtViewWhyRefusedNoWorld &&
               refusals[3] == ut::kUtViewWhyRefusedMp &&
               refusals[4] == ut::kUtViewWhyRefusedClosed &&
               refusals[5] == ut::kUtViewWhyRefusedNotTransfer && allowed == ut::kUtViewWhyOk,
           "G8 every refusal names itself, in order", ut::utViewWhyText(refusals[5]));
    }
    {
        // every event that must force OFF does, from a clean ON state
        const int evs[] = {ut::kUtViewEvToggle,   ut::kUtViewEvOpen,      ut::kUtViewEvGoodbye,
                           ut::kUtViewEvWorldUp,  ut::kUtViewEvWorldDown, ut::kUtViewEvFault,
                           ut::kUtViewEvBindingsIncomplete, ut::kUtViewEvStreamOutModSack};
        int offs = 0;
        for (int e : evs) {
            ut::UtViewState s;
            s.bindingsOk = s.world = s.open = true;
            s.mode = 1;
            ut::utViewApply(&s, ut::kUtViewEvToggle, 0);
            const ut::UtViewStep st = ut::utViewApply(&s, e, 0);
            if (st.turnedOff && !s.on) ++offs;
        }
        ut::UtViewState s;
        s.bindingsOk = s.world = s.open = true;
        s.mode = 1;
        ut::utViewApply(&s, ut::kUtViewEvToggle, 0);
        const bool tab = ut::utViewApply(&s, ut::kUtViewEvModeChanged, 0).turnedOff;
        ut::utViewApply(&s, ut::kUtViewEvModeChanged, 1);
        ut::utViewApply(&s, ut::kUtViewEvToggle, 0);
        const bool same = !ut::utViewApply(&s, ut::kUtViewEvModeChanged, 1).turnedOff && s.on;
        const bool mp = ut::utViewApply(&s, ut::kUtViewEvMpChanged, 1).turnedOff;
        _snprintf_s(d, sizeof(d), _TRUNCATE, "%d of 8 events + tab change + MP; mode 1 again keeps it",
                    offs);
        ok(offs == 8 && tab && same && mp, "every OFF trigger turns the view OFF", d);
    }

    // ---- GetItemUnderPoint - every site x sack x frame x bool byte ----------------------
    {
        int cases = 0, breaches = 0, tooltips = 0, missedTooltips = 0, vanillaTouched = 0;
        for (int site = 0; site < 4; ++site)
            for (int mod = 0; mod < 2; ++mod)
                for (int ver = 0; ver < 2; ++ver)
                    for (int b1 = 0; b1 < 256; ++b1)
                        for (int b2 = 0; b2 < 256; ++b2) {
                            ++cases;
                            ++g_checks;
                            const UnderOutcome o =
                                underModel(ut::utUnderPointRefuse, site, mod != 0, ver != 0, b1, b2);
                            if (o.takeOfProto) ++breaches;
                            if (o.tooltip) ++tooltips;
                            const bool provenHover = site == ut::kUtUnderMouseHandler && mod &&
                                                     ver && b1 == 0 && b2 == 0;
                            if (provenHover && !o.tooltip) ++missedTooltips;
                            if (!mod && ut::utUnderPointRefuse(site, false, ut::kUtFrameClick))
                                ++vanillaTouched;
                        }
        _snprintf_s(d, sizeof(d), _TRUNCATE,
                    "%d cases: %d prototype takes, tooltip on %d (the proven hovers), %d missed, "
                    "%d real-sack refusals", cases, breaches, tooltips, missedTooltips,
                    vanillaTouched);
        ok(breaches == 0 && tooltips == 1 && missedTooltips == 0 && vanillaTouched == 0,
           "under-point: no take of a prototype, the proven hover gets its id", d);
        // the two wrong verdicts are CAUGHT: the earlier loses the tooltip, the mutant leaks a take
        int refuseAllTips = 0, mutantTakes = 0;
        for (int b1 = 0; b1 < 2; ++b1)
            for (int b2 = 0; b2 < 2; ++b2) {
                refuseAllTips += underModel(verdictRefuseAll, ut::kUtUnderMouseHandler, true, true, b1, b2).tooltip;
                mutantTakes +=
                    underModel(verdictNoBools, ut::kUtUnderMouseHandler, true, true, b1, b2).takeOfProto;
            }
        _snprintf_s(d, sizeof(d), _TRUNCATE, "verdict: %d tooltip(s); bool-blind mutant: %d take(s)",
                    refuseAllTips, mutantTakes);
        ok(refuseAllTips == 0 && mutantTakes == 3, "under-point: the earlier bug and a bool-blind mutant are caught", d);
    }
    {
        // the frame shape: EBP - return slot == dist + (EBP & 7), EBP 4-aligned
        const unsigned dist = 0x68;
        const bool a = ut::utUnderFrameShape(0x0019F000u, 0x0019F000u - 0x68, dist);        // aligned
        const bool b = ut::utUnderFrameShape(0x0019F004u, 0x0019F004u - 0x6C, dist);        // +4
        const bool c = !ut::utUnderFrameShape(0x0019F004u, 0x0019F004u - 0x68, dist);       // off by 4
        const bool e = !ut::utUnderFrameShape(0x0019F002u, 0x0019F002u - 0x6A, dist);       // misaligned
        const bool f = !ut::utUnderFrameShape(0x0019F000u, 0x0019F000u - 0x68, 0);          // unknown
        const bool g = !ut::utUnderFrameShape(0x0019F000u, 0x0019F000u + 0x68, dist);       // below
        g_checks += 6;
        ok(a && b && c && e && f && g, "under-point: the frame shape is exact",
           "aligned, +4, off by 4, misaligned, dist 0, EBP below the slot");
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const bool m1 = ut::utUnderPointMatches(100.0f, 40.0f, 150.0f, 250.0f, 13.0f, 2.0f, 37.0f, 208.0f);
        const bool m2 = !ut::utUnderPointMatches(101.0f, 40.0f, 150.0f, 250.0f, 13.0f, 2.0f, 37.0f, 208.0f);
        const bool m3 = !ut::utUnderPointMatches(nan, 40.0f, 150.0f, 250.0f, 13.0f, 2.0f, 37.0f, 208.0f);
        const bool m4 = ut::utUnderFrameKind(true, 0, 0) == ut::kUtFrameHover &&
                        ut::utUnderFrameKind(true, 1, 0) == ut::kUtFrameClick &&
                        ut::utUnderFrameKind(true, 0, 1) == ut::kUtFrameClick &&
                        ut::utUnderFrameKind(true, 2, 0) == ut::kUtFrameUnknown &&
                        ut::utUnderFrameKind(false, 0, 0) == ut::kUtFrameUnknown;
        g_checks += 4;
        ok(m1 && m2 && m3 && m4, "under-point: the point cross-check and the bool bytes",
           "exact passes, 1 px off fails, NaN fails; 0/0 hover, 1 click, 2 or unverified unknown");
    }

    // ---- named sequences -------------------------------------------------------------------
    {
        const Opts o;   // G2 on, G3 off: the door must hold on G2 alone
        char why[160] = {0};
        Sim s;
        const int noFrame[] = {aWorldUp, aOpen, aFrame, aTab1, aToggle, aClose};
        bool good = run(noFrame, 6, o, &s, why, sizeof(why));
        ok(good && s.member == 0 && !s.vs.on, "ON then close with NO frame", why);
        const int withFrame[] = {aWorldUp, aOpen, aTab1, aToggle, aFrame, aClose};
        good = run(withFrame, 6, o, &s, why, sizeof(why));
        ok(good && s.member == 0 && s.protos == 0, "ON, a frame (member = mod), close", why);
        const int tabFast[] = {aWorldUp, aOpen, aTab1, aToggle, aFrame, aTab0, aClose};
        good = run(tabFast, 7, o, &s, why, sizeof(why));
        ok(good && s.member == 0, "ON, a frame, tab click, close with no frame", why);
        const int worldDown[] = {aWorldUp, aOpen, aTab1, aToggle, aFrame, aWorldDown, aClose};
        good = run(worldDown, 7, o, &s, why, sizeof(why));
        ok(good && s.member == 0, "ON, a frame, the world unloads, close", why);
        const int mp[] = {aWorldUp, aOpen, aTab1, aToggle, aFrame, aMpOn, aFrame, aClose};
        good = run(mp, 8, o, &s, why, sizeof(why));
        ok(good && s.member == 0 && !s.vs.on, "ON, a frame, multiplayer, close", why);
        const int fault[] = {aWorldUp, aOpen, aTab1, aToggle, aFrame, aFault, aToggle, aFrame};
        good = run(fault, 8, o, &s, why, sizeof(why));
        ok(good && !s.vs.on && s.member == 0, "a fault latches: the next toggle is refused", why);
        const int again[] = {aWorldUp, aOpen, aTab1, aToggle, aFrame, aTab0, aFrame, aTab1,
                             aFrame, aToggle, aFrame, aClose};
        good = run(again, 12, o, &s, why, sizeof(why));
        ok(good && s.member == 0, "ON, Stash and back (OFF), ON again, close", why);
    }

    // ---- the enumeration -------------------------------------------------------------------
    const int kDepth = 7;
    int seq[16];
    {
        Walk w;
        w.opts.g2Goodbye = true;
        w.opts.g3 = false;
        w.depth = kDepth;
        dfs(&w, fresh(true), seq, 0);
        _snprintf_s(d, sizeof(d), _TRUNCATE,
                    "%lld sequences (depth %d, %d events), ON reached in %lld, %lld closes with "
                    "the mod sack cached; %s",
                    w.cases, kDepth, (int)aCount, w.onReached, w.savedWhileSub,
                    w.failures ? w.firstFail : "no invariant broke");
        ok(w.failures == 0 && w.onReached > 0 && w.savedWhileSub > 0,
           "EVERY order: G2 alone keeps the member real at every save", d);
    }
    {
        Walk w;
        w.opts.g2Goodbye = true;
        w.opts.g3 = true;
        w.depth = kDepth;
        dfs(&w, fresh(true), seq, 0);
        _snprintf_s(d, sizeof(d), _TRUNCATE, "%lld sequences, G3 fired %lld times (expected 0)",
                    w.cases, w.g3Fired);
        ok(w.failures == 0 && w.g3Fired == 0, "with G3 on as well: G3 never has to fire", d);
    }
    {
        Walk w;
        w.opts.g2Goodbye = true;
        w.depth = 6;
        dfs(&w, fresh(false), seq, 0);
        _snprintf_s(d, sizeof(d), _TRUNCATE, "%lld sequences, ON reached %lld times", w.cases,
                    w.onReached);
        ok(w.failures == 0 && w.onReached == 0, "bindings incomplete: the view is never ON", d);
    }
    {
        // THE MUTANT: no re-point at CaravanGoodbye. I3 must catch it...
        Walk w;
        w.opts.g2Goodbye = false;
        w.opts.g3 = false;
        w.depth = 6;
        dfs(&w, fresh(true), seq, 0);
        ok(w.failures > 0 && strstr(w.firstFail, "I3:") == w.firstFail,
           "mutant without G2 is CAUGHT by I3", w.firstFail);
        // ... and G3 alone must rescue every save of it.
        Walk w3;
        w3.opts.g2Goodbye = false;
        w3.opts.g3 = true;
        w3.depth = 6;
        dfs(&w3, fresh(true), seq, 0);
        _snprintf_s(d, sizeof(d), _TRUNCATE, "%lld sequences, G3 fired %lld times; %s", w3.cases,
                    w3.g3Fired, w3.failures ? w3.firstFail : "every save saw the real sack");
        ok(w3.failures == 0 && w3.g3Fired > 0, "mutant without G2: G3 alone holds the door", d);
    }

    // ---- the owned marks' grid-origin check (ut_gridcheck.h) ---------------------------
    {
        using namespace ut;
        // a 1366 x 768 canvas at UI scale 1, the window's record (10, 65.5) 565 x 637; the
        // engine puts the grid origin at (93, 177) with 32 px cells
        const UtGridBounds win1366 = {1366.0f, 768.0f, 10.0f, 65.5f, 565.0f, 637.0f};
        auto hover = [](float gx, float gy, float x, float y, unsigned id, int col, int row, int w,
                        int h, bool cur, float curDx) {
            UtGridHover v = {};
            v.gridX = gx;
            v.gridY = gy;
            v.x = x;
            v.y = y;
            v.id = id;
            v.col = col;
            v.row = row;
            v.w = w;
            v.h = h;
            v.cw = v.ch = 32;
            v.cursorRead = cur;
            v.cursorX = gx + x + curDx;
            v.cursorY = gy + y;
            return v;
        };
        {   // the measured numbers, the cursor agreeing: accepted although the records disagree
            UtGridState st;
            utGridReset(&st);
            const int r1 = utGridStep(&st, hover(93, 177, 506, 28, 60937, 14, 0, 2, 2, true, 0.0f), win1366);
            const int r2 = utGridStep(&st, hover(93, 177, 10, 10, 60938, 0, 0, 2, 2, true, 1.5f), win1366);
            _snprintf_s(d, sizeof(d), _TRUNCATE, "steps %d, %d; verdict %d at (%.0f,%.0f), window %d",
                        r1, r2, st.verdict, st.gridX, st.gridY, st.windowOk ? 1 : 0);
            ok(r1 == kUtGridFirst && r2 == kUtGridAccepted && st.verdict == 1 && st.gridX == 93.0f &&
                   st.gridY == 177.0f && !st.windowOk,
               "grid: the measured numbers + the cursor -> accepted", d);
        }
        {   // the same numbers without a cursor: waits, and never refuses
            UtGridState st;
            utGridReset(&st);
            utGridStep(&st, hover(93, 177, 506, 28, 60937, 14, 0, 2, 2, false, 0.0f), win1366);
            int waits = 0, last = 0;
            for (int i = 0; i < 1000 && st.verdict == 0; ++i) {
                last = utGridStep(&st, hover(93, 177, 10, 10, 60938, 0, 0, 2, 2, false, 0.0f), win1366);
                if (last == kUtGridWaitCursor) ++waits;
            }
            _snprintf_s(d, sizeof(d), _TRUNCATE, "%d waits (state %d), then %d, verdict %d", waits,
                        st.waits, last, st.verdict);
            ok(waits == 1000 && st.waits == 1000 && last == kUtGridWaitCursor && st.verdict == 0,
               "grid: outside the window, no cursor -> waits, never refused", d);
        }
        {   // the cursor 56 px off (the engine's units are not canvas px): never accepted
            UtGridState st;
            utGridReset(&st);
            for (int i = 0; i < 100 && st.verdict == 0; ++i)
                utGridStep(&st, hover(93, 177, 10.0f + (float)(i % 2) * 64.0f, 10, 60937 + (i % 2),
                                      (i % 2) * 2, 0, 2, 2, true, 56.0f), win1366);
            ok(st.verdict == 0 && st.cursorAgree == 0,
               "grid: a cursor 56 px off never vouches (and never refuses)", nullptr);
        }
        {   // an origin the records agree with: accepted on the second prototype, no cursor needed
            UtGridState st;
            utGridReset(&st);
            utGridStep(&st, hover(37, 192, 40, 40, 7, 1, 1, 1, 1, false, 0.0f), win1366);
            const int r = utGridStep(&st, hover(37, 192, 100, 40, 8, 3, 1, 1, 1, false, 0.0f), win1366);
            ok(r == kUtGridAccepted && st.windowOk, "grid: inside the records' window -> accepted",
               nullptr);
        }
        {   // each refusal
            UtGridState st;
            utGridReset(&st);
            int r = utGridStep(&st, hover(93, 177, 506, 28, 1, 0, 0, 2, 2, true, 0.0f), win1366);
            const int r2 = utGridStep(&st, hover(93, 177, 10, 10, 2, 0, 0, 2, 2, true, 0.0f), win1366);
            ok(r == kUtGridRefusedCell && r2 == kUtGridIgnored && st.verdict == -1,
               "grid: check 1 failing refuses, for good", nullptr);
            utGridReset(&st);
            utGridStep(&st, hover(93, 177, 10, 10, 1, 0, 0, 2, 2, true, 0.0f), win1366);
            r = utGridStep(&st, hover(94, 177, 10, 10, 2, 0, 0, 2, 2, true, 0.0f), win1366);
            ok(r == kUtGridRefusedOrigin && st.verdict == -1,
               "grid: two hovers 1 px apart refuse", nullptr);
            utGridReset(&st);
            utGridStep(&st, hover(93, 177, 10, 10, 1, 0, 0, 2, 2, true, 0.0f), win1366);
            r = utGridStep(&st, hover(93, 177, 20, 20, 1, 0, 0, 2, 2, true, 0.0f), win1366);
            ok(r == kUtGridSamePrototype && st.verdict == 0,
               "grid: the same prototype twice decides nothing", nullptr);
            utGridReset(&st);
            const UtGridBounds small = {600.0f, 600.0f, 0.0f, 0.0f, 600.0f, 600.0f};
            utGridStep(&st, hover(93, 177, 10, 10, 1, 0, 0, 2, 2, true, 0.0f), small);
            r = utGridStep(&st, hover(93, 177, 80, 10, 2, 2, 0, 2, 2, true, 0.0f), small);
            ok(r == kUtGridRefusedCanvas && st.verdict == -1, "grid: off the canvas refuses",
               nullptr);
            utGridReset(&st);
            const float nan = std::numeric_limits<float>::quiet_NaN();
            utGridStep(&st, hover(nan, 177, 10, 10, 1, 0, 0, 2, 2, true, 0.0f), win1366);
            r = utGridStep(&st, hover(nan, 177, 80, 10, 2, 2, 0, 2, 2, true, 0.0f), win1366);
            ok(r == kUtGridRefusedOrigin && st.verdict == -1, "grid: a NaN origin refuses", nullptr);
        }
        {   // 40 decisive hovers with the cursor 10 px behind, then two at rest
            UtGridState st;   // 15 cells apart -> accepted (the wait never refuses)
            utGridReset(&st);
            utGridStep(&st, hover(93, 177, 506, 28, 1, 14, 0, 2, 2, true, 10.0f), win1366);
            int waits = 0;
            for (int i = 0; i < 40; ++i)
                if (utGridStep(&st, hover(93, 177, 10, 10, 2, 0, 0, 2, 2, true, 10.0f), win1366) ==
                    kUtGridWaitCursor)
                    ++waits;
            const int r1 = utGridStep(&st, hover(93, 177, 506, 28, 1, 14, 0, 2, 2, true, 0.0f), win1366);
            const int r2 = utGridStep(&st, hover(93, 177, 10, 10, 2, 0, 0, 2, 2, true, 1.0f), win1366);
            _snprintf_s(d, sizeof(d), _TRUNCATE, "%d waits, then %d %d, verdict %d, agree %d of %d",
                        waits, r1, r2, st.verdict, st.cursorAgree, st.cursorRead);
            ok(waits == 40 && r1 == kUtGridSamePrototype && r2 == kUtGridAccepted &&
                   st.verdict == 1 && st.gridX == 93.0f && st.gridY == 177.0f && !st.windowOk,
               "grid: 40 waits with the cursor 10 px behind, then two at rest -> accepted", d);
        }
        {   // two matches 7 cells apart do not vouch; 8 cells apart do (x, then y)
            UtGridState st;
            utGridReset(&st);
            utGridStep(&st, hover(93, 177, 10, 10, 1, 0, 0, 2, 2, true, 0.0f), win1366);
            const int r1 = utGridStep(&st, hover(93, 177, 234, 10, 2, 7, 0, 1, 1, true, 0.0f), win1366);
            const int r2 = utGridStep(&st, hover(93, 177, 266, 10, 3, 8, 0, 1, 1, true, 0.0f), win1366);
            utGridReset(&st);
            utGridStep(&st, hover(93, 177, 10, 10, 1, 0, 0, 2, 2, true, 0.0f), win1366);
            const int r3 = utGridStep(&st, hover(93, 177, 10, 234, 2, 0, 7, 1, 1, true, 0.0f), win1366);
            const int r4 = utGridStep(&st, hover(93, 177, 10, 266, 3, 0, 8, 1, 1, true, 0.0f), win1366);
            _snprintf_s(d, sizeof(d), _TRUNCATE, "x: %d %d, y: %d %d", r1, r2, r3, r4);
            ok(r1 == kUtGridWaitCursor && r2 == kUtGridAccepted && r3 == kUtGridWaitCursor &&
                   r4 == kUtGridAccepted,
               "grid: the cursor proof needs its two matches 8 cells apart", d);
        }
        {   // check 1 fails on ONE footprint bound alone - footprint (3,4) 2x2
            static const float kP[4][2] = {{100, 100}, {100, 200}, {70, 140}, {170, 140}};
            static const char* kWhat[4] = {"row above", "row below", "column left", "column right"};
            for (int i = 0; i < 4; ++i) {
                UtGridState st;
                utGridReset(&st);
                const int r = utGridStep(&st, hover(93, 177, kP[i][0], kP[i][1], 9, 3, 4, 2, 2, true,
                                                    0.0f), win1366);
                _snprintf_s(d, sizeof(d), _TRUNCATE, "%s: point (%.0f,%.0f) -> %d", kWhat[i],
                            kP[i][0], kP[i][1], r);
                ok(r == kUtGridRefusedCell && st.verdict == -1,
                   "grid: check 1 fails on one footprint bound alone", d);
            }
            UtGridState st;
            utGridReset(&st);
            ok(utGridStep(&st, hover(93, 177, 100, 140, 9, 3, 4, 2, 2, true, 0.0f), win1366) ==
                   kUtGridFirst,
               "grid: check 1 holds inside the footprint (3,4) 2x2", nullptr);
        }
        {   // every sequence up to depth 5 over 12 hover kinds x 4 bound sets vs an oracle
            struct Kind { float gx, gy, x, y; unsigned id; int col, row, w, h; bool cur; float dx; };
            static const Kind kK[12] = {
                {93, 177, 506, 28, 1, 14, 0, 2, 2, true, 0.0f},    // A, cursor agrees
                {93, 177, 10, 10, 2, 0, 0, 2, 2, true, 2.0f},      // B, cursor agrees
                {93, 177, 40, 100, 3, 1, 3, 1, 1, false, 0.0f},    // C, no cursor
                {93, 177, 40, 100, 3, 1, 3, 1, 1, true, 30.0f},    // C, cursor far
                {93, 177, 300, 300, 4, 0, 0, 2, 2, true, 0.0f},    // check 1 fails
                {93.3f, 177, 10, 10, 2, 0, 0, 2, 2, false, 0.0f},  // B, origin within tol
                {95, 177, 10, 10, 2, 0, 0, 2, 2, true, 0.0f},      // B, another origin
                {93, 177, 10, 10, 0, 0, 0, 2, 2, true, 0.0f},      // no item
                {37, 192, 40, 40, 5, 1, 1, 1, 1, false, 0.0f},     // records' origin, no cursor
                {93, 177, 100, 100, 6, 3, 4, 2, 2, true, 0.0f},   // check 1 fails on the row alone (above)
                {93, 177, 100, 200, 7, 3, 4, 2, 2, true, 0.0f},   // ... on the row alone (below)
                {93, 177, 70, 140, 8, 3, 4, 2, 2, true, 0.0f}};   // ... on the column alone (left)
            const UtGridBounds kB[4] = {
                {1366, 768, 10, 65.5f, 565, 637},    // the measured window (the grid at 93 leaves it)
                {1366, 768, 0, 0, 1366, 768},        // a window the whole canvas
                {600, 600, 0, 0, 600, 600},          // a small canvas (the grid at 93 leaves it)
                {1366, 768, 30, 150, 560, 560}};     // a window that holds the records' origin
            long long seqs = 0, accepted = 0, bad = 0;
            char firstBad[160] = "";
            int hseq[5];
            const int n = 12, depth = 5;
            for (int bi = 0; bi < 4; ++bi) {
                for (int len = 1; len <= depth; ++len) {
                    long long total = 1;
                    for (int i = 0; i < len; ++i) total *= n;
                    for (long long c = 0; c < total; ++c) {
                        long long v = c;
                        for (int i = 0; i < len; ++i) {
                            hseq[i] = (int)(v % n);
                            v /= n;
                        }
                        ++seqs;
                        UtGridState st;
                        utGridReset(&st);
                        int counted = 0, agree = 0, decidedAt = -1, lastVerdict = 0;
                        float amnx = 0, amxx = 0, amny = 0, amxy = 0;   // the matched points
                        bool cellAll = true, sameOrigin = true, finalBroken = false;
                        unsigned ids[5] = {0, 0, 0, 0, 0};
                        float ox = 0, oy = 0;
                        for (int i = 0; i < len; ++i) {
                            const Kind& k = kK[hseq[i]];
                            UtGridHover h = {};
                            h.gridX = k.gx; h.gridY = k.gy; h.x = k.x; h.y = k.y; h.id = k.id;
                            h.col = k.col; h.row = k.row; h.w = k.w; h.h = k.h;
                            h.cw = h.ch = 32; h.cursorRead = k.cur;
                            h.cursorX = k.gx + k.x + k.dx; h.cursorY = k.gy + k.y;
                            const bool before = st.verdict != 0;
                            if (!before && k.id) {   // the oracle's own record of counted hovers
                                const int cx = (int)(k.x / 32.0f), cy = (int)(k.y / 32.0f);
                                if (!(cx >= k.col && cx < k.col + k.w && cy >= k.row &&
                                      cy < k.row + k.h))
                                    cellAll = false;
                                if (counted == 0) { ox = k.gx; oy = k.gy; }
                                else if (k.gx - ox > 0.5f || ox - k.gx > 0.5f ||
                                         k.gy - oy > 0.5f || oy - k.gy > 0.5f)
                                    sameOrigin = false;
                                if (k.cur && k.dx <= 4.0f) {
                                    if (agree == 0) { amnx = amxx = k.x; amny = amxy = k.y; }
                                    if (k.x < amnx) amnx = k.x;
                                    if (k.x > amxx) amxx = k.x;
                                    if (k.y < amny) amny = k.y;
                                    if (k.y > amxy) amxy = k.y;
                                    ++agree;
                                }
                                ids[counted++] = k.id;
                            }
                            utGridStep(&st, h, kB[bi]);
                            ++g_checks;
                            if (before && st.verdict != lastVerdict) finalBroken = true;
                            if (!before && st.verdict != 0) decidedAt = i;
                            lastVerdict = st.verdict;
                        }
                        bool distinct = false;
                        for (int i = 1; i < counted; ++i)
                            if (ids[i] != ids[0]) distinct = true;
                        const UtGridBounds& b = kB[bi];
                        const bool candCanvas = counted > 0 && ox >= 0 && oy >= 0 &&
                                                ox + 512 <= b.canvasW + 0.5f &&
                                                oy + 480 <= b.canvasH + 0.5f;
                        if (st.verdict == -1 && cellAll && sameOrigin && !(distinct && !candCanvas)) {
                            ++bad;   // a refusal needs (a), (b) or the canvas
                            if (!firstBad[0])
                                _snprintf_s(firstBad, sizeof(firstBad), _TRUNCATE,
                                            "a refusal without cause (bounds %d len %d)", bi, len);
                        }
                        if (st.verdict != 1) {
                            if (finalBroken) { ++bad; if (!firstBad[0]) _snprintf_s(firstBad, sizeof(firstBad), _TRUNCATE, "a decision changed (bounds %d)", bi); }
                            continue;
                        }
                        ++accepted;
                        const bool inCanvas = st.gridX >= 0 && st.gridY >= 0 &&
                                              st.gridX + 512 <= b.canvasW + 0.5f &&
                                              st.gridY + 480 <= b.canvasH + 0.5f;
                        const bool inWin = st.gridX >= b.winX - 0.5f && st.gridY >= b.winY - 0.5f &&
                                           st.gridX + 512 <= b.winX + b.winW + 0.5f &&
                                           st.gridY + 480 <= b.winY + b.winH + 0.5f;
                        const bool proof = agree >= 2 && (amxx - amnx >= 256.0f || amxy - amny >= 256.0f);
                        const bool good = cellAll && sameOrigin && distinct && inCanvas &&
                                          (inWin || proof) && !finalBroken &&
                                          decidedAt >= 1 && st.gridX == ox && st.gridY == oy;
                        if (!good) {
                            ++bad;
                            if (!firstBad[0])
                                _snprintf_s(firstBad, sizeof(firstBad), _TRUNCATE,
                                            "bounds %d len %d: cell %d origin %d distinct %d canvas %d win %d agree %d",
                                            bi, len, cellAll, sameOrigin, distinct, inCanvas, inWin, agree);
                        }
                    }
                }
            }
            _snprintf_s(d, sizeof(d), _TRUNCATE, "%lld sequences, %lld accepted, %lld bad%s%s", seqs,
                        accepted, bad, bad ? ": " : "", firstBad);
            ok(bad == 0 && accepted > 0, "grid: every accepted origin meets (a) (b) (c)", d);
        }
    }


    // ---- the live frame and the pad inside it (ut_padlayout.h) ----------------------
    // A measured window: canvas 1366 x 768, UI scale
    // 1.000, grid measured at (93,177) with 32 px cells (x 93..605, y 177..657), the live frame at
    // about x 66..631 x 66..703, the character window from x ~640. The records put the frame at
    // (10,65.5). The scenario helper keeps the SAME layout at any scale: frame origin F, the page
    // at F + (0,126)s (the handler's parent origin), the grid at page + (27,-15)s, cells 32s.
    {
        struct Scn {
            float s, canvasW, canvasH, fx, fy;
        };
        auto frameIn = [](const Scn& c, float pageDy) {
            ut::UtFrameIn in;
            in.scale = c.s;
            in.canvasW = c.canvasW;
            in.canvasH = c.canvasH;
            in.originX = c.fx;
            in.originY = c.fy + 126.0f * c.s;
            in.gridX = in.originX + 27.0f * c.s;
            in.gridY = in.originY + pageDy * c.s;
            in.cellW = 32.0f * c.s;
            in.cellH = 32.0f * c.s;
            in.pageX = 0.0f;
            in.pageY = 126.0f;
            in.frameW = 565.0f;
            in.frameH = 637.0f;
            return in;
        };
        auto padIn = [](const ut::UtRectF& f, const ut::UtRectF& g, const Scn& c) {
            ut::UtPadIn p;
            p.frame = f;
            p.grid = g;
            p.scale = c.s;
            p.canvasW = c.canvasW;
            p.canvasH = c.canvasH;
            p.dx = 0;
            p.dy = 4;
            p.cellH = 14;
            p.gap = 1;
            p.search = true;   // the search field is checked with every button
            return p;
        };
        // every shown rect inside the ground, the ground inside the frame and the canvas, nothing
        // on the grid, no two buttons overlapping, the label clear of every button
        auto padSound = [](const ut::UtPadOut& o, const ut::UtRectF& f, const ut::UtRectF& g,
                           float cw, float ch, char* why, size_t cap) {
            const ut::UtRectF canvas = {0.0f, 0.0f, cw, ch};
            if (!ut::utRectInside(o.ground, f, 0.0f)) return _snprintf_s(why, cap, _TRUNCATE, "ground leaves the frame"), false;
            if (!ut::utRectInside(o.ground, canvas, 0.0f)) return _snprintf_s(why, cap, _TRUNCATE, "ground leaves the canvas"), false;
            if (ut::utRectsMeet(o.ground, g)) return _snprintf_s(why, cap, _TRUNCATE, "ground meets the grid"), false;
            for (int i = 0; i < ut::kUtPadMax; ++i) {
                if (!(o.btn[i].w > 0.0f && o.btn[i].h > 0.0f)) return _snprintf_s(why, cap, _TRUNCATE, "button %d empty", i), false;
                if (!ut::utRectInside(o.btn[i], o.ground, 0.0f)) return _snprintf_s(why, cap, _TRUNCATE, "button %d leaves the ground", i), false;
                if (ut::utRectsMeet(o.btn[i], o.label)) return _snprintf_s(why, cap, _TRUNCATE, "button %d meets the label", i), false;
                for (int j = i + 1; j < ut::kUtPadMax; ++j)
                    if (!(i == ut::kUtPadSearch && j == ut::kUtPadClear) && ut::utRectsMeet(o.btn[i], o.btn[j])) return _snprintf_s(why, cap, _TRUNCATE, "buttons %d and %d overlap", i, j), false;
            }
            if (!ut::utRectInside(o.btn[ut::kUtPadClear], o.btn[ut::kUtPadSearch], 0.0f)) return _snprintf_s(why, cap, _TRUNCATE, "the clear square leaves the field"), false;
            if (!(o.label.w > 40.0f) || !ut::utRectInside(o.label, o.ground, 0.0f)) return _snprintf_s(why, cap, _TRUNCATE, "label box"), false;
            why[0] = 0;
            return true;
        };
        char why[96];

        // 1. the measured window, scale 1.000
        const Scn win1366 = {1.0f, 1366.0f, 768.0f, 66.0f, 66.0f};
        ut::UtRectF f, g;
        int r = ut::utFrameFromHover(frameIn(win1366, -15.0f), &f, &g);
        _snprintf_s(d, sizeof(d), _TRUNCATE, "frame (%.0f,%.0f) %.0fx%.0f, grid (%.0f,%.0f) %.0fx%.0f",
                    f.x, f.y, f.w, f.h, g.x, g.y, g.w, g.h);
        ok(r == ut::kUtFrameOk && f.x == 66.0f && f.y == 66.0f && f.w == 565.0f && f.h == 637.0f &&
               g.x == 93.0f && g.y == 177.0f && g.w == 512.0f && g.h == 480.0f,
           "frame: the measured numbers -> 66..631 x 66..703", d);
        ut::UtPadOut o = {};
        const int band = ut::utPadLayout(padIn(f, g, win1366), &o);
        const bool sound = band == ut::kUtPadBelow && padSound(o, f, g, 1366.0f, 768.0f, why, sizeof(why));
        _snprintf_s(d, sizeof(d), _TRUNCATE, "band %d, pad (%.0f,%.0f) %.0fx%.0f, cells %dx%d %s",
                    band, o.ground.x, o.ground.y, o.ground.w, o.ground.h, o.cellW, o.cellH, why);
        ok(sound && o.ground.x >= 93.0f && o.ground.x + o.ground.w <= 605.0f &&
               o.ground.y >= 657.0f && o.ground.y + o.ground.h <= 703.0f && o.cellW == 32 &&
               o.cellH == 14,
           "pad: below the grid, inside its width, 32 x 14 cells", d);
        {   // the old column's area, the grid's last column and the character window: no button
            int hits = 0;
            for (float y = 60.0f; y < 768.0f; y += 2.0f)
                for (float x = 0.0f; x < 1366.0f; x += 2.0f) {
                    const bool onButton = [&] {
                        for (int i = 0; i < ut::kUtPadMax; ++i)
                            if (ut::utRectHas(o.btn[i], x, y)) return true;
                        return false;
                    }();
                    const bool forbidden = ut::utRectHas(g, x, y) || x >= 631.0f ||
                                           (x >= 581.0f && x < 681.0f && y >= 160.0f && y < 545.0f) ||
                                           x < 66.0f || y >= 703.0f;   // the column: 581..681 x 160..545
                    if (onButton && forbidden) ++hits;
                }
            _snprintf_s(d, sizeof(d), _TRUNCATE, "%d forbidden points on a button", hits);
            ok(hits == 0, "pad: no button on the grid, the old column, the char window", d);
        }
        ok(o.btn[ut::kUtPadToggle].y > o.btn[ut::kUtPadGroup0].y &&
               o.btn[ut::kUtPadOwn].x > o.btn[ut::kUtPadToggle].x &&
               o.btn[ut::kUtPadPrev].x > o.btn[ut::kUtPadOwn].x &&
               o.btn[ut::kUtPadNext].x > o.btn[ut::kUtPadPrev].x &&
               o.label.x > o.btn[ut::kUtPadNext].x,
           "pad: row 2 = Collection | OWN | < | > | label", "");
        {   // row 2 at scale 1: the four buttons in 4 cells + 3 gaps, the field in the next 3 cells +
            // 2 gaps with its clear square inside, the label = the box it had with the search off
            const ut::UtRectF& sf = o.btn[ut::kUtPadSearch];
            const ut::UtRectF& sq = o.btn[ut::kUtPadClear];
            const ut::UtRectF& tb = o.btn[ut::kUtPadToggle];
            const ut::UtRectF& ob = o.btn[ut::kUtPadOwn];
            const ut::UtRectF& pb = o.btn[ut::kUtPadPrev];
            const ut::UtRectF& nb = o.btn[ut::kUtPadNext];
            ut::UtPadIn p0 = padIn(f, g, win1366);
            p0.search = false;
            ut::UtPadOut o0 = {};
            const int b0 = ut::utPadLayout(p0, &o0);
            const float ox = o.ground.x;
            _snprintf_s(d, sizeof(d), _TRUNCATE,
                        "Transfer +%.0f %.0f, OWN +%.0f %.0f, < +%.0f %.0f, > +%.0f %.0f, field +%.0f %.0f, square %.0fx%.0f, text %.0f, label +%.0f %.0f",
                        tb.x - ox, tb.w, ob.x - ox, ob.w, pb.x - ox, pb.w, nb.x - ox, nb.w, sf.x - ox, sf.w, sq.w, sq.h,
                        o.fieldText.w, o.label.x - ox, o.label.w);
            ok(tb.x == ox + 3.0f && nb.x + nb.w == ox + 3.0f + 131.0f && sf.x == ox + 3.0f + 132.0f && sf.w == 98.0f &&
                   tb.w == 65.0f && ob.x == ox + 69.0f && ob.w == 32.0f && pb.x == ox + 102.0f && pb.w == 16.0f &&
                   nb.x == ox + 119.0f && nb.w == 15.0f &&
                   ob.x == tb.x + tb.w + 1.0f && pb.x == ob.x + ob.w + 1.0f && nb.x == pb.x + pb.w + 1.0f,
               "row 2 at scale 1: Transfer 65, OWN one cell (32), < 16 and > 15 (half a cell each, a gap between), ending at m + 4 steps - gap (131 px); the field 3 cells + 2 gaps (98 px)", d);
            ok(sq.w == o.rowPx && sq.h == o.rowPx && sq.y == sf.y && sq.x + sq.w == sf.x + sf.w &&
                   o.fieldText.x == sf.x && o.fieldText.w == sf.w - sq.w - 1.0f,
               "the clear square: the row's height, at the field's right end inside it; the text area = the rest less one gap", d);
            ok(b0 == band && o0.btn[ut::kUtPadSearch].w == 0.0f && o0.btn[ut::kUtPadClear].w == 0.0f &&
                   o0.fieldText.w == 0.0f && !memcmp(&o0.label, &o.label, sizeof(o.label)) &&
                   !memcmp(&o0.btn[ut::kUtPadToggle], &tb, sizeof(tb)) && !memcmp(&o0.btn[ut::kUtPadNext], &nb, sizeof(nb)) &&
                   o.label.x == ox + 237.0f && o.label.x + o.label.w == ox + 497.0f,
               "search on or off: the same buttons and the label's box byte-for-byte (7 cells + 3 px in, 260 px); off = no field, no square", d);
            {   // the real Transfer page (the view OFF): search_transfer=1 lays the field out there too,
                // on the same pad - the toggle, the field, its square and the label where they are ON
                ut::UtPadIn pOff = padIn(f, g, win1366);
                pOff.search = ut::utPadFieldLaidOut(false, 1, 1);
                ut::UtPadOut oOff = {};
                const int bOff = ut::utPadLayout(pOff, &oOff);
                ut::UtPadIn pNo = padIn(f, g, win1366);
                pNo.search = ut::utPadFieldLaidOut(false, 1, 0);
                ut::UtPadOut oNo = {};
                const int bNo = ut::utPadLayout(pNo, &oNo);
                ok(bOff == band && !memcmp(&oOff, &o, sizeof(o)) && oOff.btn[ut::kUtPadSearch].w == 98.0f &&
                       ut::utRectInside(oOff.btn[ut::kUtPadClear], oOff.btn[ut::kUtPadSearch], 0.0f) &&
                       ut::utRectInside(oOff.btn[ut::kUtPadSearch], oOff.ground, 0.0f) &&
                       !ut::utRectsMeet(oOff.btn[ut::kUtPadSearch], oOff.btn[ut::kUtPadToggle]) &&
                       !ut::utRectsMeet(oOff.btn[ut::kUtPadSearch], oOff.label) &&
                       bNo == band && oNo.btn[ut::kUtPadSearch].w == 0.0f && !memcmp(&oNo.label, &o.label, sizeof(o.label)),
                   "the view OFF, search_transfer=1: the same pad byte-for-byte - the field (98 px) with its square beside the toggle, the label box unchanged; search_transfer=0: no field OFF", "");
            }
            {   // the real sack's item rects (RectExt: drawn px from the grid's top-left) -> canvas px
                const ut::UtRectF gr = {93.0f, 177.0f, 512.0f, 480.0f};   // scale 1: 32 px cells
                ut::UtRectF rr = {0.0f, 0.0f, 0.0f, 0.0f}, r2 = rr, r3 = rr;
                const bool in1 = ut::utSearchRealRect(gr, 64.0f, 32.0f, 64.0f, 96.0f, &rr);
                const ut::UtRectF slot = ut::utSlotDrawnRect(gr, 2, 1, 2, 3);
                const bool corner = ut::utSearchRealRect(gr, 448.0f, 384.0f, 64.0f, 96.0f, &r2);   // the last cells
                const ut::UtRectF g44 = {100.0f, 150.0f, 704.0f, 660.0f};   // 1.375: 44 px cells
                const bool frac = ut::utSearchRealRect(g44, 43.99f, 0.2f, 88.0f, 132.0f, &r3);
                ut::UtRectF nbr = r3;
                const bool nbOk = ut::utSearchRealRect(g44, 131.99f, 0.2f, 44.0f, 44.0f, &nbr);
                _snprintf_s(d, sizeof(d), _TRUNCATE, "(64,32) 64x96 -> (%.0f,%.0f) %.0fx%.0f; 1.375: (%.0f,%.0f) %.0fx%.0f, its right neighbour at %.0f",
                            rr.x, rr.y, rr.w, rr.h, r3.x, r3.y, r3.w, r3.h, nbr.x);
                ok(in1 && rr.x == 157.0f && rr.y == 209.0f && rr.w == 64.0f && rr.h == 96.0f && rr.x == slot.x &&
                       rr.y == slot.y && rr.w == slot.w && rr.h == slot.h && corner && r2.x + r2.w == gr.x + gr.w &&
                       r2.y + r2.h == gr.y + gr.h && frac && r3.x == 144.0f && r3.y == 150.0f && r3.w == 88.0f &&
                       r3.h == 132.0f && nbOk && nbr.x == r3.x + r3.w,
                   "the real page: an item's sack rect + the grid's origin = its slot on the canvas (edges rounded, neighbours meet exactly)", d);
                ok(!ut::utSearchRealRect(gr, 480.0f, 0.0f, 64.0f, 32.0f, &rr) && !ut::utSearchRealRect(gr, 0.0f, 460.0f, 32.0f, 32.0f, &rr) &&
                       !ut::utSearchRealRect(gr, -1.0f, 0.0f, 32.0f, 32.0f, &rr) && !ut::utSearchRealRect(gr, 0.0f, 0.0f, 0.0f, 32.0f, &rr) &&
                       !ut::utSearchRealRect(gr, 0.0f, 0.0f, 32.0f, 0.5f, &rr) && !ut::utSearchRealRect(gr, 1.0e9f, 0.0f, 32.0f, 32.0f, &rr) &&
                       ut::utSearchRealRect(gr, -0.4f, 0.0f, 32.0f, 32.0f, nullptr) && ut::utSearchRealRect(gr, 480.4f, 448.4f, 32.0f, 32.0f, nullptr),
                   "the real page: a rect leaving the grid (right, bottom, left), empty or not a number is never marked; half a pixel is tolerated", "");
            }
            {   // the pad's one caption size and the toggle's caption at scale 1
                const int fs = ut::utPadCaptionSize(o.btn[ut::kUtPadGroup0].w, o.btn[ut::kUtPadGroup0].h, 5, 1.0f);
                const char* tc = ut::utPadToggleCaption(tb.w, tb.h, 1.0f, fs);
                _snprintf_s(d, sizeof(d), _TRUNCATE, "captions %d, the toggle says \"%s\"", fs, tc);
                ok(fs == 10 && !strcmp(tc, "Transfer") && ut::utPadTextWidth("OWN", fs) <= ob.w - 2.0f &&
                       ut::utPadTextWidth("<", fs) <= pb.w - 2.0f,
                   "scale 1: \"Transfer\" + the lamp fit the toggle at the pad's size 10; OWN and < fit centred", d);
                ok(!strcmp(ut::utPadToggleCaption(65.0f, 14.0f, 1.0f, 10), "Transfer") &&
                       !strcmp(ut::utPadToggleCaption(49.0f, 14.0f, 1.0f, 10), "Trans.") &&
                       !strcmp(ut::utPadToggleCaption(40.0f, 14.0f, 1.0f, 10), "Tab") &&
                       !strcmp(ut::utPadToggleCaption(10.0f, 14.0f, 1.0f, 10), "Tab") &&
                       ut::utPadLampTextOffset(14.0f, 1.0f) == 14.0f,
                   "the toggle's caption falls to \"Trans.\" then \"Tab\" (65 / 49 / 40 / 10 px at size 10), never a smaller size", "");
            }
            {   // the label's line at the label's size (13 at scale 1) in its 260 px box
                char ln[128];
                ut::UtPadLabelWords w;
                w.group = "Torso";
                w.span = "1-15";
                w.rows = 35;
                w.known = true;
                w.owned = 120;
                w.total = 241;
                w.allKnown = true;
                w.allOwned = 312;
                w.allTotal = 1588;
                const int lab = ut::utPadLabelFont(13, o.label.h);
                int lf = ut::utPadLabelLine(w, 1000.0f, lab, ln, sizeof(ln));
                ok(lab == 13 && lf == 13 && !strcmp(ln, "Torso  rows 1-15 / 35  owned 120 / 241  all 312 / 1588"),
                   "label: size 13 in the 14 px row; a wide box, no query - the full form with the whole collection", ln);
                lf = ut::utPadLabelLine(w, o.label.w, lab, ln, sizeof(ln));
                ok(lf == 13 && !strcmp(ln, "Torso 1-15/35  120/241  all 312/1588"),
                   "label: no query in the 260 px box - the compact form, the line ends after the all count", ln);
                w.found = "found 9";
                w.brief = "=9";
                lf = ut::utPadLabelLine(w, 1000.0f, lab, ln, sizeof(ln));
                ok(!strcmp(ln, "Torso  rows 1-15 / 35  owned 120 / 241  all 312 / 1588  found 9"), "label: the full form with a query", ln);
                lf = ut::utPadLabelLine(w, 330.0f, lab, ln, sizeof(ln));
                ok(lf == 13 && !strcmp(ln, "Torso 1-15/35  120/241  all 312/1588  found 9"), "label: the compact form with a query (330 px)", ln);
                lf = ut::utPadLabelLine(w, 290.0f, lab, ln, sizeof(ln));
                ok(lf == 13 && !strcmp(ln, "Torso 1-15/35  120/241  all 312/1588  =9"), "label: (i) the search word's short form (290 px)", ln);
                w.found = "found 1193";
                w.brief = "=1193";
                lf = ut::utPadLabelLine(w, o.label.w, lab, ln, sizeof(ln));
                ok(lf == 13 && ut::utPadTextWidth(ln, lf) <= o.label.w && !strcmp(ln, "Torso  120/241  all 312/1588  =1193"),
                   "label: (ii) found 1193 in the 260 px box - the rows dropped", ln);
                w.found = "indexing 1234/1588";
                w.brief = "1234/1588";
                lf = ut::utPadLabelLine(w, o.label.w, lab, ln, sizeof(ln));
                ok(lf == 13 && ut::utPadTextWidth(ln, lf) <= o.label.w && !strcmp(ln, "Torso  120/241  1234/1588"),
                   "label: (iii) indexing in the 260 px box - the whole collection dropped", ln);
                w.found = "found 1193";
                w.brief = "=1193";
                lf = ut::utPadLabelLine(w, 90.0f, 9, ln, sizeof(ln));
                ok(lf == 9 && ut::utPadTextWidth(ln, lf) <= 90.0f && !strcmp(ln, "Torso  =1193"),
                   "label: (iv) a 90 px box - the owned count dropped too", ln);
                lf = ut::utPadLabelLine(w, 30.0f, 9, ln, sizeof(ln));
                ok(lf == 6 && !strcmp(ln, "Torso  =1193"), "label: the last form, too wide even at 6, is drawn at 6", ln);
                w.found = "";
                w.brief = "";
                lf = ut::utPadLabelLine(w, 90.0f, 9, ln, sizeof(ln));
                ok(!strcmp(ln, "Torso  120/241"), "label: no query in a 90 px box - (iii) is the last form", ln);
                w.known = false;
                w.allKnown = false;
                lf = ut::utPadLabelLine(w, o.label.w, lab, ln, sizeof(ln));
                ok(!strcmp(ln, "Torso 1-15/35  ?/241  all ?/1588"), "label: an unknown journal set - both counts say ?", ln);
                w.allTotal = 0;
                lf = ut::utPadLabelLine(w, 1000.0f, lab, ln, sizeof(ln));
                ok(!strcmp(ln, "Torso  rows 1-15 / 35  owned ? / 241"), "label: no catalogue count - no all part", ln);
            }
        }

        // 2. the lone toggle before the measurement: inside the records' frame AND the live one
        const ut::UtRectF rec = {10.0f, 65.5f, 565.0f, 637.0f};
        const ut::UtRectF t = ut::utPadFallbackToggle(rec, 1.0f, 14);
        _snprintf_s(d, sizeof(d), _TRUNCATE, "toggle (%.0f,%.0f) %.0fx%.0f", t.x, t.y, t.w, t.h);
        ok(ut::utRectInside(t, rec, 0.0f) && ut::utRectInside(t, f, 0.0f) && !ut::utRectsMeet(t, g) &&
               t.x + t.w < 631.0f,
           "fallback toggle: in both frames, off the grid", d);

        // 3. the same layout at UI scale 1.25 and 0.8 (canvas scaled with it), and a scale sweep
        const Scn scns[] = {{1.25f, 1708.0f, 960.0f, 82.5f, 82.5f}, {0.8f, 1366.0f, 768.0f, 52.8f, 129.0f},
                            {0.8f, 1093.0f, 614.0f, 52.8f, 52.8f}};
        for (const Scn& c : scns) {
            ut::UtRectF f2, g2;
            const int r2 = ut::utFrameFromHover(frameIn(c, -15.0f), &f2, &g2);
            ut::UtPadOut o2 = {};
            const int b2 = r2 == ut::kUtFrameOk ? ut::utPadLayout(padIn(f2, g2, c), &o2) : -9;
            const bool s2 = b2 == ut::kUtPadBelow && padSound(o2, f2, g2, c.canvasW, c.canvasH, why, sizeof(why));
            const ut::UtRectF rec2 = {10.0f * c.s, (c.canvasH - 637.0f * c.s) * 0.5f, 565.0f * c.s, 637.0f * c.s};
            const ut::UtRectF t2 = ut::utPadFallbackToggle(rec2, c.s, 14);
            _snprintf_s(d, sizeof(d), _TRUNCATE, "scale %.2f: frame %d, band %d, pad (%.0f,%.0f) %.0fx%.0f %s",
                        c.s, r2, b2, o2.ground.x, o2.ground.y, o2.ground.w, o2.ground.h, why);
            ok(r2 == ut::kUtFrameOk && s2 && ut::utRectInside(t2, rec2, 0.0f),
               "frame + pad at another UI scale", d);
        }
        {
            int bad = 0, n = 0;
            char first[96] = "";
            for (int k = 60; k <= 220; ++k) {   // UI scale 0.60 .. 2.20 on a canvas that fits
                const float s = (float)k / 100.0f;
                const Scn c = {s, 1400.0f * s, 780.0f * s, 60.0f * s, 70.0f * s};
                ut::UtRectF f3, g3;
                ut::UtPadOut o3 = {};
                ++n;
                if (ut::utFrameFromHover(frameIn(c, -15.0f), &f3, &g3) != ut::kUtFrameOk ||
                    ut::utPadLayout(padIn(f3, g3, c), &o3) != ut::kUtPadBelow ||
                    !padSound(o3, f3, g3, c.canvasW, c.canvasH, why, sizeof(why))) {
                    if (!bad++) _snprintf_s(first, sizeof(first), _TRUNCATE, "first at %.2f: %s", s, why);
                }
            }
            _snprintf_s(d, sizeof(d), _TRUNCATE, "%d scales, %d bad %s", n, bad, first);
            ok(bad == 0, "pad: sound at every UI scale 0.60..2.20", d);
        }

        // 4. refusals: the frame
        {
            ut::UtFrameIn in = frameIn(win1366, -15.0f);
            in.originY = 66.0f;   // the other reading (parent origin = the frame's): frame y -60
            in.gridY = 177.0f;
            ok(ut::utFrameFromHover(in, &f, &g) == ut::kUtFrameOffCanvas,
               "frame: parent origin taken as the frame's -> refused", "");
            in = frameIn(win1366, -15.0f);
            in.originX += 60.0f;   // grid left of the derived frame
            ok(ut::utFrameFromHover(in, &f, &g) == ut::kUtFrameGridOutside,
               "frame: grid outside the derived frame -> refused", "");
            in = frameIn(win1366, -15.0f);
            in.originX = std::numeric_limits<float>::quiet_NaN();
            ok(ut::utFrameFromHover(in, &f, &g) == ut::kUtFrameBadInput, "frame: a NaN -> refused", "");
            in = frameIn(win1366, -15.0f);
            in.scale = 5.0f;
            ok(ut::utFrameFromHover(in, &f, &g) == ut::kUtFrameBadInput, "frame: scale 5 -> refused", "");
            in = frameIn(win1366, -15.0f);
            in.cellW = 0.0f;
            ok(ut::utFrameFromHover(in, &f, &g) == ut::kUtFrameBadInput, "frame: no cell -> refused", "");
            const Scn big = {1.25f, 1366.0f, 768.0f, 82.5f, -14.0f};   // 796 px tall on 768
            ok(ut::utFrameFromHover(frameIn(big, -15.0f), &f, &g) == ut::kUtFrameOffCanvas,
               "frame: 1.25 on a 768 canvas leaves it -> refused", "");
        }
        // 5. the bands: a short band shrinks the pad, a shorter one moves it above, none refuses
        {
            ut::UtRectF fA, gA;   // the page's grid at +0 (not -15): the band below is 31 px
            ut::utFrameFromHover(frameIn({1.0f, 1366.0f, 768.0f, 66.0f, 51.0f}, 0.0f), &fA, &gA);
            ut::UtPadOut oA;
            const int bA = ut::utPadLayout(padIn(fA, gA, win1366), &oA);
            _snprintf_s(d, sizeof(d), _TRUNCATE, "band %d, cells %dx%d, pad y %.0f..%.0f, frame bottom %.0f",
                        bA, oA.cellW, oA.cellH, oA.ground.y, oA.ground.y + oA.ground.h, fA.y + fA.h);
            ok(bA == ut::kUtPadBelow && oA.cellH == 12 &&
                   padSound(oA, fA, gA, 1366.0f, 768.0f, why, sizeof(why)),
               "pad: a 31 px band -> pad_y 0 and 12 px rows", d);
            // a band of 20 px below (200 px above): the pad is FITTED below, never moved
            // above - rows under GD's floor (9 px, margin 1), still inside the frame
            const ut::UtRectF fB = {66.0f, 100.0f, 565.0f, 700.0f};
            const ut::UtRectF gB = {93.0f, 300.0f, 512.0f, 480.0f};   // bottom 780 = frame bottom - 20
            ut::UtPadOut oB;
            const int bB = ut::utPadLayout(padIn(fB, gB, {1.0f, 1366.0f, 900.0f, 0.0f, 0.0f}), &oB);
            _snprintf_s(d, sizeof(d), _TRUNCATE, "band %d fit %d rows %.0f margin %.0f h %.0f", bB,
                        oB.fit, oB.rowPx, oB.marginPx, oB.ground.h);
            ok(bB == ut::kUtPadBelow && oB.fit == ut::kUtPadFitBelowFloor && oB.rowPx == 9.0f &&
                   oB.marginPx == 1.0f && padSound(oB, fB, gB, 1366.0f, 900.0f, why, sizeof(why)),
               "pad: a 20 px band -> fitted below (rows 9 px under the floor), never above", d);
            // a band of 10 px: not even 8 px rows - over the frame's bottom border, off the grid
            const ut::UtRectF fC = {66.0f, 100.0f, 565.0f, 510.0f};
            const ut::UtRectF gC = {93.0f, 120.0f, 512.0f, 480.0f};   // bottom 600, frame bottom 610
            ut::UtPadOut oC;
            const int bC = ut::utPadLayout(padIn(fC, gC, {1.0f, 1366.0f, 900.0f, 0.0f, 0.0f}), &oC);
            _snprintf_s(d, sizeof(d), _TRUNCATE, "band %d fit %d y %.0f..%.0f", bC, oC.fit, oC.ground.y,
                        oC.ground.y + oC.ground.h);
            ok(bC == ut::kUtPadBorder && oC.fit == ut::kUtPadFitBorder && oC.ground.y >= 600.0f &&
                   oC.ground.y + oC.ground.h <= 900.0f && !ut::utRectsMeet(oC.ground, gC),
               "pad: a 10 px band -> over the frame's bottom border, clear of the grid", d);
            ok(ut::utPadLayout(padIn(fC, gC, {1.0f, 1366.0f, 610.0f, 0.0f, 0.0f}), &oC) == ut::kUtPadNoRoom,
               "pad: no room even on the canvas -> refused (the only refusal)", "");
            ut::UtPadIn wide = padIn(f, g, win1366);
            {   // every legal pad_x places the pad, clamped to the frame's edges
                ut::UtRectF fU, gU;   // the measured frame and grid, fresh (f / g were reused above)
                ut::utFrameFromHover(frameIn(win1366, -15.0f), &fU, &gU);
                ut::UtPadIn wU = padIn(fU, gU, win1366);
                int placedN = 0, atLeft = 0, atRight = 0;
                bool soundN = true;
                for (int padDx = -100; padDx <= 100; ++padDx) {
                    wU.dx = padDx;
                    if (ut::utPadLayout(wU, &oC) != ut::kUtPadBelow ||
                        !padSound(oC, fU, gU, 1366.0f, 768.0f, why, sizeof(why))) {
                        soundN = false;
                        continue;
                    }
                    ++placedN;
                    if (oC.ground.x == fU.x) ++atLeft;
                    if (oC.ground.x + oC.ground.w == fU.x + fU.w) ++atRight;
                }
                _snprintf_s(d, sizeof(d), _TRUNCATE, "%d of 201 placed, %d at the left edge, %d at the right edge",
                            placedN, atLeft, atRight);
                ok(soundN && placedN == 201 && atLeft > 0 && atRight > 0,
                   "pad: every pad_x -100..100 places the pad inside the frame (clamped)", d);
            }
            wide.dx = 10;
            ok(ut::utPadLayout(wide, &oC) == ut::kUtPadBelow && padSound(oC, f, g, 1366.0f, 768.0f, why, sizeof(why)),
               "pad: pad_x 10 stays inside the frame", "");
            ut::UtPadIn bad = padIn(f, g, win1366);
            bad.scale = std::numeric_limits<float>::quiet_NaN();
            ok(ut::utPadLayout(bad, &oC) == ut::kUtPadBadInput, "pad: a NaN scale -> refused", "");
        }
        // 7. the frame follows the window - accepted at (93,177), then (93,160)
        {
            ut::UtFrameTrack trk7;
            ut::utFrameTrackReset(&trk7);
            ut::UtRectF f1, g1, f2, g2;
            const bool s0 = ut::utFrameTrackStep(&trk7, 93.0f, 177.0f) == ut::kUtFrameTrackDerive;
            const int r1 = ut::utFrameFromHover(frameIn(win1366, -15.0f), &f1, &g1);
            ut::utFrameTrackDecided(&trk7, r1 == ut::kUtFrameOk ? 1 : -1, g1.x, g1.y);
            const bool s1 = ut::utFrameTrackStep(&trk7, 93.0f, 177.0f) == ut::kUtFrameTrackKeep &&
                            ut::utFrameTrackStep(&trk7, 93.6f, 176.2f) == ut::kUtFrameTrackKeep &&
                            ut::utFrameTrackStep(&trk7, std::numeric_limits<float>::quiet_NaN(), 177.0f) ==
                                ut::kUtFrameTrackKeep;
            const bool s2 = ut::utFrameTrackStep(&trk7, 93.0f, 160.0f) == ut::kUtFrameTrackDerive;
            const Scn moved = {1.0f, 1366.0f, 768.0f, 66.0f, 49.0f};
            const int r2 = ut::utFrameFromHover(frameIn(moved, -15.0f), &f2, &g2);
            ut::utFrameTrackDecided(&trk7, r2 == ut::kUtFrameOk ? 1 : -1, g2.x, g2.y);
            ut::UtPadOut o1, o2;
            const int b1 = ut::utPadLayout(padIn(f1, g1, win1366), &o1);
            const int b2 = ut::utPadLayout(padIn(f2, g2, moved), &o2);
            _snprintf_s(d, sizeof(d), _TRUNCATE,
                        "frame y %.0f -> %.0f, grid y %.0f -> %.0f, pad y %.0f -> %.0f (the stale pad "
                        "would end at %.0f, the moved frame at %.0f)",
                        f1.y, f2.y, g1.y, g2.y, o1.ground.y, o2.ground.y, o1.ground.y + o1.ground.h,
                        f2.y + f2.h);
            ok(s0 && s1 && s2 && r1 == ut::kUtFrameOk && r2 == ut::kUtFrameOk && g2.x == 93.0f &&
                   g2.y == 160.0f && f2.y == 49.0f && b1 == ut::kUtPadBelow && b2 == ut::kUtPadBelow &&
                   padSound(o2, f2, g2, 1366.0f, 768.0f, why, sizeof(why)) &&
                   o2.ground.y == o1.ground.y - 17.0f && !ut::utRectInside(o1.ground, f2, 0.0f) &&
                   trk7.verdict == 1 && trk7.gridY == 160.0f && trk7.moves == 1,
               "frame: accepted at (93,177), a hover at (93,160) -> derived again, the pad follows, clear of the grid", d);
            // a window that will not hold still: refused after kUtFrameMovesMax moves, per showing
            ut::utFrameTrackReset(&trk7);
            ut::utFrameTrackStep(&trk7, 93.0f, 177.0f);
            ut::utFrameTrackDecided(&trk7, 1, 93.0f, 177.0f);
            int derives = 0, unstableAt = -1, keepsAfter = 0;
            for (int i7 = 0; i7 < 40; ++i7) {
                const float y7 = (i7 & 1) ? 177.0f : 160.0f;
                const int st7 = ut::utFrameTrackStep(&trk7, 93.0f, y7);
                if (st7 == ut::kUtFrameTrackDerive) {
                    ++derives;
                    ut::utFrameTrackDecided(&trk7, 1, 93.0f, y7);
                } else if (st7 == ut::kUtFrameTrackUnstable && unstableAt < 0) {
                    unstableAt = i7;
                } else if (st7 == ut::kUtFrameTrackKeep && unstableAt >= 0) {
                    ++keepsAfter;
                }
            }
            const bool refused = trk7.verdict == -1 && trk7.unstable;
            ut::utFrameTrackReset(&trk7);
            _snprintf_s(d, sizeof(d), _TRUNCATE, "%d derivations, unstable at move %d, %d keeps after",
                        derives, unstableAt, keepsAfter);
            ok(derives == ut::kUtFrameMovesMax && unstableAt == ut::kUtFrameMovesMax &&
                   keepsAfter == 40 - ut::kUtFrameMovesMax - 1 && refused &&
                   ut::utFrameTrackStep(&trk7, 93.0f, 177.0f) == ut::kUtFrameTrackDerive,
               "frame: a window that keeps moving -> refused after 16 moves; a new showing measures again", d);
        }
        // 8. the OFF toggle's ground never leaves the pad (so the frame), even when
        //    the pad is flush with the frame's bottom, over scales 0.60..2.20, pad_y 0..40, pad_h 12..20
        {
            long long layouts = 0, oldPast = 0;
            bool inside = true;
            for (int si = 0; si <= 160 && inside; ++si) {
                const float sc8 = 0.60f + 0.01f * (float)si;
                const ut::UtRectF gS = {66.0f + ut::utPadRound(27.0f * sc8), 66.0f + ut::utPadRound(111.0f * sc8),
                                        512.0f * sc8, 480.0f * sc8};
                for (int dy8 = 0; dy8 <= 40 && inside; ++dy8) {
                    for (int ch8 = 12; ch8 <= 20 && inside; ++ch8) {
                        ut::UtPadIn p8;
                        p8.frame = ut::UtRectF{66.0f, 66.0f, ut::utPadRound(565.0f * sc8), 4000.0f};
                        p8.grid = gS;
                        p8.scale = sc8;
                        p8.canvasW = 5000.0f;
                        p8.canvasH = 5000.0f;
                        p8.dx = 0;
                        p8.dy = dy8;
                        p8.cellH = ch8;
                        p8.gap = 1;
                        ut::UtPadOut o8;
                        if (ut::utPadLayout(p8, &o8) != ut::kUtPadBelow) continue;
                        p8.frame.h = o8.ground.y + o8.ground.h - p8.frame.y;   // flush with the bottom
                        if (ut::utPadLayout(p8, &o8) != ut::kUtPadBelow) continue;
                        ++layouts;
                        const ut::UtRectF gr8 = ut::utPadToggleGround(o8, sc8);
                        const ut::UtRectF& tb8 = o8.btn[ut::kUtPadToggle];
                        const float m8 = ut::utPadRound(3.0f * sc8);
                        if (tb8.y + tb8.h + m8 > o8.ground.y + o8.ground.h) ++oldPast;
                        inside = ut::utRectInside(gr8, o8.ground, 0.0f) && ut::utRectInside(gr8, p8.frame, 0.0f) &&
                                 ut::utRectInside(tb8, gr8, 0.0f) && !ut::utRectsMeet(gr8, gS);
                    }
                }
            }
            _snprintf_s(d, sizeof(d), _TRUNCATE, "%lld flush layouts, the old ground ended past the pad in %lld",
                        layouts, oldPast);
            ok(inside && layouts > 1000, "pad: the OFF toggle's ground stays inside the pad and the frame", d);
        }
        // 6. captions: the short names, and the size fitted to the 32 x 14 cell
        {
            const char* labels[15] = {"Helms", "Torso", "Arms",   "Legs",     "Amulets",
                                      "Rings", "Shields", "Axes", "Maces",    "Staves",
                                      "Swords", "Throwing", "Spears", "Bows", "Artifacts"};
            int widest = 0;
            bool allShort = true;
            for (int i = 0; i < 15; ++i) {
                const char* sn = ut::utPadShortName(labels[i]);
                const int n = ut::utPadStrLen(sn);
                if (n > widest) widest = n;
                allShort = allShort && n <= 5;
            }
            const int fs = ut::utPadCaptionSize(32.0f, 14.0f, widest);
            _snprintf_s(d, sizeof(d), _TRUNCATE, "widest %d chars, size %d (%.1f px wide)", widest, fs,
                        ut::kUtPadEmPerChar * (float)fs * (float)widest);
            ok(allShort && widest == 5 && fs >= 9 && ut::kUtPadEmPerChar * (float)fs * (float)widest <= 30.0f &&
                   !strcmp(ut::utPadShortName("Artifacts"), "Artf") &&
                   !strcmp(ut::utPadShortName("Unknown"), "Unknown"),
               "captions: short names fit a 32 x 14 cell", d);
        }
    }

    // ---- the deposit / take gestures -----------------------------------------------------
    {
        char wd[200];
        // (a) the gesture reading agrees with the earlier frame kind for every pair of bool bytes
        long bad = 0, n = 0;
        for (int v = 0; v < 2; ++v)
            for (int b1 = 0; b1 < 256; ++b1)
                for (int b2 = 0; b2 < 256; ++b2) {
                    const int k = ut::utUnderFrameKind(v != 0, (unsigned char)b1, (unsigned char)b2);
                    const int g = ut::utUnderFrameGesture(v != 0, (unsigned char)b1, (unsigned char)b2);
                    const bool same = (k == ut::kUtFrameUnknown && g == ut::kUtGestUnknown) ||
                                      (k == ut::kUtFrameHover && g == ut::kUtGestHover) ||
                                      (k == ut::kUtFrameClick &&
                                       (g == ut::kUtGestPick || g == ut::kUtGestRight ||
                                        (g == ut::kUtGestUnknown && b1 == 1 && b2 == 1)));
                    if (!same) ++bad;
                    if (g == ut::kUtGestPick && !(v && b1 == 1 && b2 == 0)) ++bad;
                    if (g == ut::kUtGestRight && !(v && b1 == 0 && b2 == 1)) ++bad;
                    ++n;
                }
        _snprintf_s(wd, sizeof(wd), _TRUNCATE, "%ld byte pairs", n);
        ok(bad == 0, "gesture = frame kind; a pick is b1=1, b2=0 on a verified frame only", wd);

        // (b) utUnderPointDecide: with takes impossible (view OFF, MP, journal unwritable) it IS
        // the earlier refusal, site by site; with takes possible exactly ONE cell changes.
        const int frameOf[4] = {ut::kUtFrameUnknown, ut::kUtFrameHover, ut::kUtFrameClick,
                                ut::kUtFrameClick};
        long diff = 0, cells = 0, opened = 0, openedRight = 0;
        for (int site = 0; site < 4; ++site)
            for (int mod = 0; mod < 2; ++mod)
                for (int g = 0; g < 4; ++g)
                    for (int tp = 0; tp < 2; ++tp) {
                        const int dec = ut::utUnderPointDecide(site, mod != 0, g, tp != 0);
                        const bool pointRefuse = ut::utUnderPointRefuse(site, mod != 0, frameOf[g]);
                        ++cells;
                        if (dec == ut::kUtUnderTakeRightClick) {   // the right-click take
                            ++openedRight;
                            if (!(tp && mod && (site == ut::kUtUnderRightClick ||   // b2
                                                (site == ut::kUtUnderMouseHandler &&
                                                 g == ut::kUtGestRight))))
                                ++diff;
                            continue;
                        }
                        if (dec == ut::kUtUnderTakeIfCollected) {
                            ++opened;
                            if (!(tp && mod && site == ut::kUtUnderMouseHandler &&
                                  g == ut::kUtGestPick))
                                ++diff;
                            continue;
                        }
                        if ((dec == ut::kUtUnderRefuse) != pointRefuse) ++diff;
                    }
        _snprintf_s(wd, sizeof(wd), _TRUNCATE, "%ld cells, %ld opened for the take", cells, opened);
        ok(diff == 0 && opened == 1 && openedRight == 4 + 1,
           "under-point: takes impossible -> exactly; possible -> the verified pick and "
           " the right-click (no frame, any gesture byte)", wd);
        ok(ut::utUnderPointDecide(ut::kUtUnderRightClick, true, ut::kUtGestPick, true) ==
                   ut::kUtUnderTakeRightClick &&
               ut::utUnderPointDecide(ut::kUtUnderRightClick, true, ut::kUtGestUnknown, false) ==
                   ut::kUtUnderRefuse &&
               ut::utUnderPointDecide(ut::kUtUnderRightClick, false, ut::kUtGestUnknown, true) ==
                   ut::kUtUnderPass &&
               ut::utUnderPointDecide(ut::kUtUnderHeldPickup, true, ut::kUtGestPick, true) ==
                   ut::kUtUnderRefuse &&
               ut::utUnderPointDecide(ut::kUtUnderMouseHandler, true, ut::kUtGestRight, true) ==
                   ut::kUtUnderTakeRightClick &&   // the mouse right-click (b2)
               ut::utUnderPointDecide(ut::kUtUnderMouseHandler, true, ut::kUtGestRight, false) ==
                   ut::kUtUnderRefuse &&
               ut::utUnderPointDecide(ut::kUtUnderMouseHandler, true, ut::kUtGestUnknown, true) ==
                   ut::kUtUnderRefuse,
           "the right-click opens only while takes are possible (a real sack is vanilla); "
           "held, modified click and an unverified frame stay refused", nullptr);

        // (c) the gesture model: one throw-away unique and one green rare, the journal, the page.
        // Every gesture goes through the SAME pure decisions the detours call; the invariants:
        //   I1 the unique exists exactly once (cursor + inventory + journal rows + REAL Transfer)
        //   I2 no prototype ever reaches a real place (cursor only through a journalled take)
        //   I3 a refused gesture changes nothing (journal unwritable / MP -> exactly)
        struct M {
            int cursor;      // 0 empty, 1 the unique, 2 the rare, 3 a prototype handed out
            int inv;         // 1 = the unique in the inventory
            int rare;        // 1 = the rare in the inventory
            int rows;        // journal rows of the unique's record
            int realT;       // items in the REAL Transfer sack
        };
        long gestures = 0, violations = 0;
        for (int mp = 0; mp < 2; ++mp)
            for (int writable = 0; writable < 2; ++writable)
                for (int start = 0; start < 3; ++start)
                    for (int seqIx = 0; seqIx < 64; ++seqIx) {
                        M m = {start == 0 ? 1 : 0, start == 1 ? 1 : 0, 1, start == 2 ? 1 : 0, 0};
                        for (int step = 0; step < 6; ++step) {
                            const int g = (seqIx >> step) & 1 ? (step % 3) : (3 + step % 3);
                            const M before = m;
                            ut::UtDepositFacts f;
                            memset(&f, 0, sizeof(f));
                            f.viewOn = true;
                            f.mpKnown = true;
                            f.mp = mp != 0;
                            f.tableOwns = writable != 0;
                            f.bindings = true;
                            f.isItem = true;
                            f.stack = 1;
                            f.haveCap = f.capMatches = true;
                            ++gestures;
                            if (g == 0 || g == 3) {   // drag-drop of the cursor item
                                f.caller = ut::kUtDepCallerDrag;
                                f.inCatalogue = m.cursor == 1;
                                if (m.cursor == 1 || m.cursor == 2) {
                                    if (!ut::utDepositIsRefusal(ut::utDepositDecide(f))) {
                                        m.rows += 1;   // journal first ...
                                        m.cursor = 0;  // ... then the engine's own disposal
                                    }
                                }
                            } else if (g == 1 || g == 4) {   // quick-move from the inventory
                                f.caller = ut::kUtDepCallerQuick;
                                f.inCatalogue = m.inv == 1;
                                if (m.inv && !ut::utDepositIsRefusal(ut::utDepositDecide(f))) {
                                    m.rows += 1;
                                    m.inv = 0;     // the TQ.exe caller removes it on true
                                } else if (m.rare && g == 4) {
                                    f.inCatalogue = false;   // the green rare
                                    if (!ut::utDepositIsRefusal(ut::utDepositDecide(f))) ++violations;
                                }
                            } else {   // left-click on the prototype (collected iff rows > 0)
                                const bool tp = !mp && writable && m.cursor == 0;
                                const int dec = ut::utUnderPointDecide(
                                    ut::kUtUnderMouseHandler, true, ut::kUtGestPick, tp);
                                ut::UtTakeFacts t = {true, true, mp != 0, writable != 0, true,
                                                     m.rows > 0, (unsigned)m.rows, true};
                                if (dec == ut::kUtUnderTakeIfCollected &&
                                    ut::utTakeDecide(t) == ut::kUtTakeOk) {
                                    m.rows -= 1;       // journal first ...
                                    m.cursor = 1;      // ... then the prototype IS the item
                                }
                            }
                            // the player puts a cursor item back into the inventory between gestures
                            if (step % 2 == 1 && m.cursor == 1 && !m.inv) {
                                m.cursor = 0;
                                m.inv = 1;
                            }
                            const int copies = (m.cursor == 1) + m.inv + m.rows + m.realT;
                            if (copies != 1) ++violations;                         // I1
                            if (m.realT != 0 || m.cursor == 3) ++violations;       // I2
                            if ((mp || !writable) &&
                                (m.rows != before.rows)) ++violations;             // I3
                        }
                    }
        _snprintf_s(wd, sizeof(wd), _TRUNCATE, "%ld gestures in %d sequences", gestures,
                    2 * 2 * 3 * 64);
        ok(violations == 0,
           "gesture model: the unique exists once, nothing reaches the real sack, refusals are no-ops",
           wd);
    }

    // =========================================================================================
    // the lost-item guards and the slot-wide answer. Each property
    // is run twice: on the mod's own predicate (0 violations) and on a MUTANT that drops the guard
    // (the same property must catch it - proof the harness sees the failure it guards against).
    // =========================================================================================
    {
        char wd[256];
        // ---- every switch is allowed while the cursor holds an item ------
        // The pad's model: state (on, group, page, own); requests Transfer, group 0..2, <, >, OWN,
        // F6, wheel - every sequence of 4 requests, each with the cursor EMPTY, holding a REAL item
        // (in no sack) or holding the TAKEN prototype (the kept take, possibly still listed in the
        // mod sack's map). A held item never changes what a switch does, and every page rebuild a
        // switch causes leaves the held take where it is (the SHIPPED utTeardownAction forgets it:
        // never destroyed, never returned). Mutants: the earlier refusal, a teardown that ignores the keep.
        struct PadSt {
            bool on;
            int group, page, own, last;
        };
        enum { rTransfer, rG0, rG1, rG2, rPrev, rNext, rOwn, rF6, rWheel, rCount };
        enum { hNone, hReal, hTaken, hCount };
        typedef bool (*Allowed)(int);
        typedef int (*Tear)(unsigned, const unsigned*, int, unsigned);
        auto apply = [](PadSt s, int r) {
            switch (r) {
            case rTransfer: s.on = false; break;
            case rG0: case rG1: case rG2:
                s.group = r - rG0; s.page = 0; s.on = true; s.last = s.group; break;
            case rPrev: if (s.on && s.page > 0) --s.page; break;
            case rNext: if (s.on && s.page < 2) ++s.page; break;
            case rOwn: s.own ^= 1; s.page = 0; break;
            case rF6: s.on = !s.on; if (s.on) s.group = s.last; break;
            case rWheel: if (s.on) s.page = (s.page + 1) % 3; break;
            default: break;
            }
            return s;
        };
        auto runPad = [&](Allowed allowed, Tear tear, long* seqs) {
            long bad = 0;
            const unsigned kTaken = 7002u, protos[3] = {7001u, 7002u, 7003u};
            const int kDepthP = 4, kChoices = rCount * hCount;
            long total = 1;
            for (int i = 0; i < kDepthP; ++i) total *= kChoices;
            *seqs = total;
            for (long n = 0; n < total; ++n) {
                long v = n;
                PadSt s = {false, 0, 0, 0, 0};
                for (int k = 0; k < kDepthP; ++k) {
                    const int c = (int)(v % kChoices);
                    v /= kChoices;
                    const int r = c / hCount, h = c % hCount;
                    const PadSt want = apply(s, r);
                    const PadSt t = allowed(h) ? apply(s, r) : s;
                    ++g_checks;
                    if (t.on != want.on || t.group != want.group || t.page != want.page ||
                        t.own != want.own || t.last != want.last)
                        ++bad;   // the held item changed what the switch does
                    const bool rebuilt = t.on != s.on || t.group != s.group || t.page != s.page ||
                                         t.own != s.own;
                    if (rebuilt && h == hTaken &&   // the rebuild walks the map: the take is listed
                        tear(kTaken, protos, 3, kTaken) != ut::kUtTearForget)
                        ++bad;   // destroyed (lost) or returned (a second copy)
                    s = t;
                }
            }
            return bad;
        };
        auto always = [](int) { return true; };
        auto removedRefusal = [](int h) { return h == hNone; };   // the removed refusal
        auto tearReal = [](unsigned k, const unsigned* p, int n, unsigned keep) {
            return ut::utTeardownAction(k, p, n, keep);
        };
        auto tearNoKeep = [](unsigned k, const unsigned* p, int n, unsigned) {
            return ut::utTeardownAction(k, p, n, 0u);
        };
        long padSeqs = 0;
        const long padBad = runPad(always, tearReal, &padSeqs);
        const long padMutRefuse = runPad(removedRefusal, tearReal, &padSeqs);
        const long padMutTear = runPad(always, tearNoKeep, &padSeqs);
        _snprintf_s(wd, sizeof(wd), _TRUNCATE,
                    "%ld sequences of 4 (empty / real / taken on the cursor): %ld violation(s); the "
                    "refusal mutant: %ld; the keep-ignoring teardown mutant: %ld",
                    padSeqs, padBad, padMutRefuse, padMutTear);
        ok(padBad == 0 && padMutRefuse > 0 && padMutTear > 0,
           "removed: a switch with a held item acts as with an empty cursor; the held take "
           "is never destroyed or returned", wd);

        // ---- 1(b)+(c) the axe replayed: an item from the REAL page on the cursor, the view ON
        // (the page's cached sack is the mod sack), the engine auto-places the held item into the
        // cached sack, then the view goes OFF (the teardown). Where is the item at the end?
        // guard b: the AddItem guard (real / none); teardown c: the key rule (real / destroy all).
        enum { xCursor, xModSack, xRealT, xLost };
        auto axe = [](bool guardB, bool realTeardown) {
            int x = xCursor;
            const unsigned kX = 106509u, protos[3] = {7001u, 7002u, 7003u};
            // the engine's add into the cached (mod) sack, from anything but the mod's placement
            const int d = guardB ? ut::utSackAddDecide(true, false) : (int)ut::kUtSackAddPass;
            if (d == ut::kUtSackAddPass) x = xModSack;   // the engine let go of it
            if (x == xModSack) {                           // OFF: the teardown walks the map
                const int a = realTeardown ? ut::utTeardownAction(kX, protos, 3, 0u)
                                           : (int)ut::kUtTearDestroy;
                x = a == ut::kUtTearDestroy ? xLost : a == ut::kUtTearReturn ? xRealT : xModSack;
            }
            return x;
        };
        const int aBC = axe(true, true), aB = axe(true, false), aC = axe(false, true),
                  aNone = axe(false, false);
        _snprintf_s(wd, sizeof(wd), _TRUNCATE,
                    "b+c: %s, b alone: %s, c alone: %s, neither (the tree): %s",
                    aBC == xCursor ? "cursor" : "?", aB == xCursor ? "cursor" : "?",
                    aC == xRealT ? "real Transfer" : "?", aNone == xLost ? "LOST" : "?");
        ok(aBC == xCursor && aB == xCursor && aC == xRealT && aNone == xLost,
           "1(b)(c) the axe replayed: either guard alone keeps the item, neither loses it", wd);

        // ---- 1(b) the AddItem guard over every sack and caller ---------------------------------
        // sacks 0..3 real (Transfer, Stash, Vault, an inventory sack), 4 the mod sack, 5 a retired
        // one; the mod's own placement or not. A real sack ALWAYS passes (the file load is never
        // touched); a mod sack passes only the mod's own placement.
        {
            int bad = 0, mutA = 0, mutB = 0;
            for (int sack = 0; sack < 6; ++sack)
                for (int placing = 0; placing < 2; ++placing) {
                    const bool mod = sack >= 4;
                    const int want = (mod && !placing) ? ut::kUtSackAddRefuse : ut::kUtSackAddPass;
                    if (ut::utSackAddDecide(mod, placing != 0) != want) ++bad;
                    // mutant A (no guard): a foreign add into a mod sack passes
                    if (mod && !placing) ++mutA;
                    // mutant B (refuse every foreign add): the load into a real sack is refused
                    if (!mod && !placing) ++mutB;
                    ++g_checks;
                }
            _snprintf_s(wd, sizeof(wd), _TRUNCATE,
                        "12 cases: %d wrong; the no-guard mutant lets %d foreign add(s) in, the "
                        "refuse-all mutant refuses %d load(s)", bad, mutA, mutB);
            ok(bad == 0 && mutA > 0 && mutB > 0,
               "1(b) engine add into a mod sack -> refused; real sacks pass (the load)", wd);
        }

        // ---- 1(c) the teardown over random maps ------------------------------------------------
        // A map = some of the page's prototypes + foreign ids + (maybe) the kept take. Every
        // prototype is destroyed, every foreign id returned, the kept take neither.
        {
            unsigned seed = 0x41u;
            auto rnd = [&seed]() {
                seed = seed * 1103515245u + 12345u;
                return (seed >> 16) & 0x7FFFu;
            };
            long bad = 0, mutDestroyAll = 0, mutOldKeep = 0;
            for (int trial = 0; trial < 20000; ++trial) {
                unsigned protos[12];
                const int np = (int)(rnd() % 12u);
                for (int i = 0; i < np; ++i) protos[i] = 1000u + (unsigned)i;
                unsigned keep = 0u;
                const unsigned pick = rnd() % 3u;
                if (pick == 0) keep = 5000u;                                  // left the list
                else if (pick == 1 && np > 0) keep = protos[rnd() % (unsigned)np];   // still listed
                unsigned keys[24];
                int nk = 0;
                for (int i = 0; i < np; ++i)
                    if (rnd() % 4u) keys[nk++] = protos[i];
                const int nf = (int)(rnd() % 4u);
                for (int i = 0; i < nf; ++i) keys[nk++] = 9000u + (unsigned)i;   // foreign
                if (keep && (rnd() % 2u)) {
                    bool in = false;
                    for (int i = 0; i < nk; ++i) in = in || keys[i] == keep;
                    if (!in) keys[nk++] = keep;
                }
                for (int k = 0; k < nk; ++k) {
                    const unsigned key = keys[k];
                    bool isProto = false;
                    for (int i = 0; i < np; ++i) isProto = isProto || protos[i] == key;
                    const bool isKeep = keep != 0 && key == keep;
                    const int a = ut::utTeardownAction(key, protos, np, keep);
                    const int want = isKeep ? ut::kUtTearForget
                                     : isProto ? ut::kUtTearDestroy : ut::kUtTearReturn;
                    if (a != want) ++bad;
                    // mutant 1: destroy the whole map (the teardown with a foreign id in it)
                    if (!isProto && !isKeep) ++mutDestroyAll;
                    // mutant 2: the keep test only inside the prototype list (the interrupted
                    // WIP): a kept take that left the list would be auto-placed = a second copy
                    const int old = isProto ? (isKeep ? ut::kUtTearForget : ut::kUtTearDestroy)
                                            : ut::kUtTearReturn;
                    if (isKeep && old == ut::kUtTearReturn) ++mutOldKeep;
                    ++g_checks;
                }
            }
            _snprintf_s(wd, sizeof(wd), _TRUNCATE,
                        "20000 maps: %ld wrong; the destroy-all mutant destroys %ld foreign id(s), "
                        "the old keep rule returns the kept take %ld time(s)",
                        bad, mutDestroyAll, mutOldKeep);
            ok(bad == 0 && mutDestroyAll > 0 && mutOldKeep > 0,
               "1(c) foreign id at OFF -> returned, never destroyed; the kept take never moves", wd);
        }

        // ---- a failed hand-out, then the fault's teardown ---------------------
        // viewTakeRemove / viewTakeTick: protoKeep(id) -> protoHandOut fails (the id is STILL in
        // the sack's map) -> protoForgetTaken -> viewFault -> protoDestroyAll -> protoSweepForeign.
        // The taken id (on the cursor) must be neither destroyed nor auto-placed (a clone into the
        // real Transfer = a duplicate). The mutant is the old protoForgetTaken: keep id cleared, no
        // forgotten list.
        {
            unsigned seed = 0x41F1u;
            auto rnd = [&seed]() {
                seed = seed * 1103515245u + 12345u;
                return (seed >> 16) & 0x7FFFu;
            };
            long placed = 0, destroyed = 0, foreignKept = 0, mutPlaced = 0, seqs = 0;
            for (int trial = 0; trial < 20000; ++trial) {
                unsigned live[12];
                int nl = 1 + (int)(rnd() % 12u);
                for (int i = 0; i < nl; ++i) live[i] = 1000u + (unsigned)i;
                const int ti = (int)(rnd() % (unsigned)nl);
                const unsigned taken = live[ti];
                ut::UtForgotten f;
                ut::utForgottenClear(f);
                unsigned keep = taken;      // protoKeep at the journalled take
                unsigned keepOld = taken;   // the mutant's keep slot
                for (int k = ti + 1; k < nl; ++k) live[k - 1] = live[k];   // off the live list
                --nl;
                ut::utForgetTaken(f, keep, taken);
                if (keepOld == taken) keepOld = 0;   // mutant: the old clearing
                if (nl > 0 && (rnd() % 3u) == 0) {   // a second take armed before the teardown
                    keep = live[rnd() % (unsigned)nl];
                    keepOld = keep;
                }
                unsigned keys[24];
                int nk = 0;
                keys[nk++] = taken;   // the failed removal: the map still lists it
                for (int i = 0; i < nl; ++i)
                    if (rnd() % 4u) keys[nk++] = live[i];
                const int nf = (int)(rnd() % 3u);
                for (int i = 0; i < nf; ++i) keys[nk++] = 9000u + (unsigned)i;   // foreign
                for (int k = 0; k < nk; ++k) {
                    const unsigned key = keys[k];
                    const int a = ut::utTeardownAction(key, live, nl, keep, &f);
                    if (key == taken && a == ut::kUtTearReturn) ++placed;
                    if (key == taken && a == ut::kUtTearDestroy) ++destroyed;
                    if (key >= 9000u && a != ut::kUtTearReturn) ++foreignKept;
                    if (key == taken &&
                        ut::utTeardownAction(key, live, nl, keepOld) == ut::kUtTearReturn)
                        ++mutPlaced;
                    ++g_checks;
                }
                ++seqs;
            }
            // Bounded list: past kUtForgotMax nothing unlisted is returned; a retire clears it.
            ut::UtForgotten big;
            ut::utForgottenClear(big);
            unsigned kz = 0;
            for (unsigned i = 0; i < (unsigned)ut::kUtForgotMax + 1u; ++i)
                ut::utForgetTaken(big, kz, 7000u + i);
            const bool overflowSafe =
                ut::utTeardownAction(7000u + (unsigned)ut::kUtForgotMax, nullptr, 0, 0u, &big) ==
                    ut::kUtTearForget &&
                ut::utTeardownAction(9999u, nullptr, 0, 0u, &big) == ut::kUtTearForget;
            ut::utForgottenClear(big);
            const bool clearedOk =
                ut::utTeardownAction(7000u, nullptr, 0, 0u, &big) == ut::kUtTearReturn;
            g_checks += 3;
            _snprintf_s(wd, sizeof(wd), _TRUNCATE,
                        "%ld sequences: the taken id auto-placed %ld / destroyed %ld time(s), a "
                        "foreign id kept %ld; the old clearing auto-places it %ld time(s); "
                        "overflow safe %d, cleared at retire %d",
                        seqs, placed, destroyed, foreignKept, mutPlaced, overflowSafe ? 1 : 0,
                        clearedOk ? 1 : 0);
            ok(placed == 0 && destroyed == 0 && foreignKept == 0 && mutPlaced > 0 &&
                   overflowSafe && clearedOk,
               "a failed hand-out: the taken id is never auto-placed nor destroyed",
               wd);
        }

        // ---- AddItemToTransfer(id) callers while ON ----------------------------
        // The primary site (0x107A6C) tests the result (false -> it keeps its item); the extras
        // loop (0x107AED) ignores it and disposes of its item. Outcomes: 0 kept, 1 a deposit, 2 a
        // copy on the REAL Transfer, 3 LOST. The extras loop runs only after a primary success, so
        // with the site unconfirmed (every call refused) it is unreachable. The mutant is the
        // earlier rule (every non-primary caller refused).
        {
            long lost = 0, misread = 0, mutLost = 0, depositOk = 0, cases = 0;
            auto outcome = [](int d, bool prim) {
                if (d == ut::kUtQuickPrimary) return 1;
                if (d == ut::kUtQuickVanilla) return 2;
                return prim ? 0 : 3;
            };
            for (int conf = 0; conf < 2; ++conf)
                for (int prim = 0; prim < 2; ++prim) {
                    if (!conf && !prim) continue;   // unreachable (see above)
                    const int qd = ut::utQuickCallerDecide(conf != 0, prim != 0);
                    const int o = outcome(qd, prim != 0);
                    if (o == 3) ++lost;
                    if (!prim && o == 1) ++misread;
                    if (conf && prim && o == 1) ++depositOk;
                    const int oldD = (conf && prim) ? (int)ut::kUtQuickPrimary : (int)ut::kUtQuickRefuse;
                    if (outcome(oldD, prim != 0) == 3) ++mutLost;
                    ++cases;
                    ++g_checks;
                }
            _snprintf_s(wd, sizeof(wd), _TRUNCATE,
                        "%ld caller cases: lost %ld, an extra read as a deposit %ld, the primary "
                        "deposits %ld; the refuse-every-other-caller mutant loses %ld",
                        cases, lost, misread, depositOk, mutLost);
            ok(lost == 0 && misread == 0 && depositOk == 1 && mutLost > 0,
               "a non-primary AddItemToTransfer caller: the real Transfer, never lost",
               wd);
        }

        // ---- 4(a) slot-wide: which prototype holds a cell --------------------------------------
        {
            unsigned seed = 0x4A1u;
            auto rnd = [&seed]() {
                seed = seed * 1103515245u + 12345u;
                return (seed >> 16) & 0x7FFFu;
            };
            long bad = 0, cells = 0;
            for (int trial = 0; trial < 3000; ++trial) {
                ut::UtSlotRect s[128];
                int n = 0;
                int occ[15][16];
                memset(occ, -1, sizeof(occ));
                const int sw = 1 + (int)(rnd() % 2u), sh = 1 + (int)(rnd() % 4u);
                for (int row = 0; row + sh <= 15 && n < 128; row += sh)
                    for (int col = 0; col + sw <= 16 && n < 128; col += sw) {
                        if (rnd() % 5u == 0) continue;   // a gap
                        s[n].col = col;
                        s[n].row = row;
                        s[n].w = sw;
                        s[n].h = sh;
                        for (int r = row; r < row + sh; ++r)
                            for (int c = col; c < col + sw; ++c) occ[r][c] = n;
                        ++n;
                    }
                for (int r = 0; r < 15; ++r)
                    for (int c = 0; c < 16; ++c) {
                        ++cells;
                        if (ut::utSlotIndexAt(s, n, c, r) != occ[r][c]) ++bad;
                    }
            }
            ut::UtSlotRect two[2] = {{0, 0, 2, 2}, {1, 1, 2, 2}};   // overlapping: refuse
            const bool overlapRefused = ut::utSlotIndexAt(two, 2, 1, 1) == -1 &&
                                        ut::utSlotIndexAt(two, 2, 0, 0) == 0 &&
                                        ut::utSlotIndexAt(two, 2, 2, 2) == 1 &&
                                        ut::utSlotIndexAt(two, 2, 5, 5) == -1;
            g_checks += cells;
            _snprintf_s(wd, sizeof(wd), _TRUNCATE, "%ld cells over 3000 layouts: %ld wrong; "
                        "an overlapped cell -> none", cells, bad);
            ok(bad == 0 && overlapRefused, "4(a) the slot that holds a cell (gaps -> none)", wd);
        }

        // ---- 4(a) slot-wide: WHERE the slot answers ---------------------------------------------
        // Every site x gesture x takes-possible x sack: the slot answer is allowed only where
        // already answers (the verified hover, the verified pick while takes are possible) - never
        // the held pick-up, an unknown frame, another site, a real sack; the right-click take (
        // the button handler; the mouse handler's b2) answers by slot too.
        {
            int bad = 0, mut = 0, allowedN = 0;
            for (int site = 0; site < 4; ++site)
                for (int gest = 0; gest < 4; ++gest)
                    for (int tp = 0; tp < 2; ++tp)
                        for (int mod = 0; mod < 2; ++mod) {
                            const int dd = ut::utUnderPointDecide(site, mod != 0, gest, tp != 0);
                            const bool a = ut::utSlotWideAllowed(site, mod != 0, gest, dd);
                            const bool want =
                                (mod && site == ut::kUtUnderMouseHandler &&
                                 ((gest == ut::kUtGestHover && dd == ut::kUtUnderPass) ||
                                  (gest == ut::kUtGestPick && dd == ut::kUtUnderTakeIfCollected) ||
                                  (gest == ut::kUtGestRight &&   // the mouse right-click
                                   dd == ut::kUtUnderTakeRightClick))) ||
                                (mod && site == ut::kUtUnderRightClick &&   // slot-wide
                                 dd == ut::kUtUnderTakeRightClick);
                            if (a != want) ++bad;
                            if (a) ++allowedN;
                            if (a && site != ut::kUtUnderRightClick &&
                                (site == ut::kUtUnderHeldPickup ||
                                 (gest == ut::kUtGestRight && dd != ut::kUtUnderTakeRightClick) ||
                                 gest == ut::kUtGestUnknown || !mod))
                                ++bad;
                            if (a && site == ut::kUtUnderRightClick && (!tp || !mod)) ++bad;
                            // mutant: "any id would not refuse" (d != Refuse) - lets another
                            // site and the real sack answer by slot
                            if (dd != ut::kUtUnderRefuse && !want) ++mut;
                            ++g_checks;
                        }
            _snprintf_s(wd, sizeof(wd), _TRUNCATE,
                        "64 cases: %d wrong, %d allowed (the hover x2, the pick with takes); the "
                        "not-refused mutant answers %d more", bad, allowedN, mut);
            ok(bad == 0 && allowedN == 3 + 4 + 1 && mut > 0,   // + the mouse right-click take
               "4(a) the slot answers only on the verified hover / take pick and the "
               "right-click take with takes possible", wd);
        }

        // ---- the MOUSE right-click is the page mouse handler's b2 (TQ.exe 0xC0208) ------
        // GetObject(id) -> 0xC0650 Player::IsInventorySpaceAvailable -> 0xC0530 GiveItemToCharacter
        // -> RemoveItemFromTransfer(id); the button handler 0xBFD10 is the pad's (KeyMap op 0x44 on
        // an InputDevice::Button, the point = the centre of the pad selection rect [+0x40..+0x4C]).
        ok(ut::utUnderFrameGesture(true, 0, 1) == ut::kUtGestRight &&
               ut::utUnderFrameGesture(true, 1, 0) == ut::kUtGestPick &&
               ut::utUnderFrameGesture(true, 1, 1) == ut::kUtGestUnknown &&
               ut::utUnderFrameGesture(false, 0, 1) == ut::kUtGestUnknown &&
               ut::utUnderFrameKind(true, 0, 1) == ut::kUtFrameClick &&
               ut::utUnderPointDecide(ut::kUtUnderMouseHandler, true, ut::kUtGestRight, true) ==
                   ut::kUtUnderTakeRightClick &&
               ut::utUnderPointDecide(ut::kUtUnderMouseHandler, true, ut::kUtGestRight, false) ==
                   ut::kUtUnderRefuse &&
               ut::utUnderPointDecide(ut::kUtUnderMouseHandler, false, ut::kUtGestRight, true) ==
                   ut::kUtUnderPass &&
               ut::utUnderPointDecide(ut::kUtUnderMouseHandler, true, ut::kUtGestUnknown, true) ==
                   ut::kUtUnderRefuse &&
               ut::utUnderPointDecide(ut::kUtUnderMouseHandler, true, ut::kUtGestPick, true) ==
                   ut::kUtUnderTakeIfCollected &&
               ut::utSlotWideAllowed(ut::kUtUnderMouseHandler, true, ut::kUtGestRight,
                                     ut::kUtUnderTakeRightClick) &&
               !ut::utSlotWideAllowed(ut::kUtUnderMouseHandler, true, ut::kUtGestRight,
                                      ut::kUtUnderRefuse) &&
               !ut::utSlotWideAllowed(ut::kUtUnderMouseHandler, false, ut::kUtGestRight,
                                      ut::kUtUnderTakeRightClick) &&
               !ut::utSlotWideAllowed(ut::kUtUnderMouseHandler, true, ut::kUtGestPick,
                                      ut::kUtUnderTakeRightClick) &&
               !ut::utSlotWideAllowed(ut::kUtUnderMouseHandler, true, ut::kUtGestUnknown,
                                      ut::kUtUnderTakeRightClick) &&
               !ut::utSlotWideAllowed(ut::kUtUnderHeldPickup, true, ut::kUtGestRight,
                                      ut::kUtUnderTakeRightClick),
           "the mouse right-click (b2 alone, verified frame) is the right-click take while "
           "takes are possible, answered by slot; b1+b2, an unverified frame, takes off and a real "
           "sack keep their answers; the left-click pick is unchanged", "");

        // ---- the axe's geometry: the lone toggle vs the pad ------------------------
        // Measured: the live frame (66,51) 565 x 637 (bottom 688), grid (93,177) 512 x 480;
        // the records' frame (10,65) 565 x 637 put the lone toggle at y 678..692: its lower rows
        // lay OUTSIDE the window, where the engine's world got the click. draws no lone
        // toggle; every pixel of every pad button lies in the live frame. that is what closes
        // the axe case (the ActivateWorld detour and utPadClaimsPoint are gone).
        {
            const ut::UtRectF live = {66.0f, 51.0f, 565.0f, 637.0f};
            const ut::UtRectF grid = {93.0f, 177.0f, 512.0f, 480.0f};
            const ut::UtRectF rec = {10.0f, 65.0f, 565.0f, 637.0f};
            const ut::UtRectF lone = ut::utPadFallbackToggle(rec, 1.0f, 14);
            long loneOut = 0;
            for (float y = lone.y + 0.5f; y < lone.y + lone.h; y += 1.0f)
                for (float x = lone.x + 0.5f; x < lone.x + lone.w; x += 1.0f)
                    if (!ut::utRectHas(live, x, y)) ++loneOut;
            ut::UtPadIn p;
            p.frame = live;
            p.grid = grid;
            p.scale = 1.0f;
            p.canvasW = 1366.0f;
            p.canvasH = 768.0f;
            p.dx = 0;
            p.dy = 4;
            p.cellH = 14;
            p.gap = 1;
            ut::UtPadOut o = {};
            const int band = ut::utPadLayout(p, &o);
            long padOut = 0, onGrid = 0;
            for (int i = 0; i < ut::kUtPadMax; ++i)
                for (float y = o.btn[i].y + 0.5f; y < o.btn[i].y + o.btn[i].h; y += 1.0f)
                    for (float x = o.btn[i].x + 0.5f; x < o.btn[i].x + o.btn[i].w; x += 1.0f) {
                        if (!ut::utRectHas(live, x, y)) ++padOut;
                        if (ut::utRectHas(grid, x, y)) ++onGrid;
                        ++g_checks;
                    }
            _snprintf_s(wd, sizeof(wd), _TRUNCATE,
                        "lone toggle (%.0f,%.0f) %.0fx%.0f: %ld px outside the live frame; pad band "
                        "%d (%.0f,%.0f) %.0fx%.0f: %ld px outside, %ld on the grid",
                        lone.x, lone.y, lone.w, lone.h, loneOut, band, o.ground.x, o.ground.y,
                        o.ground.w, o.ground.h, padOut, onGrid);
            ok(loneOut > 0 && band == ut::kUtPadBelow && padOut == 0 && onGrid == 0,
               "the axe's geometry: the old toggle left the window; the pad never does", wd);
        }
    }

    // =========================================================================================
    // =========================================================================================
    {
        char wd[256];
        // ---- 1: a single unique on the cursor makes the +0x30 thunk answer true on the page -----
        // The facts viewCapable builds for a unique on the collection page, with the count
        // Item::GetNumberInStack really answers (Game.dll 0x8800 `xor eax,eax / ret`: 0).
        ut::UtDepositFacts f;
        memset(&f, 0, sizeof(f));
        f.caller = ut::kUtDepCallerDrag;
        f.viewOn = f.mpKnown = f.tableOwns = f.bindings = f.isItem = f.inCatalogue = true;
        const unsigned counts[4] = {0u, 1u, 2u, 0xFFFFFFFFu};
        bool ans[4], mut[4];
        for (int i = 0; i < 4; ++i) {
            f.stack = counts[i];
            ans[i] = ut::utCapSlotAnswer(true, ut::utDepositCapable(f), false);
            mut[i] = ut::utCapSlotAnswer(true, ut::utDepositCapable(f) && f.stack == 1u, false);
            g_checks += 2;
        }
        f.stack = 0;
        f.inCatalogue = false;   // a green rare
        const bool rare = ut::utCapSlotAnswer(true, ut::utDepositCapable(f), false);
        const bool off = ut::utCapSlotAnswer(false, false, true);   // OFF: the vanilla stub (mov al,1)
        _snprintf_s(wd, sizeof(wd), _TRUNCATE,
                    "stack 0 -> %d, 1 -> %d, 2 -> %d, unreadable -> %d; a green rare -> %d; view OFF "
                    "-> vanilla %d; the rule (exactly 1) answers stack 0 -> %d",
                    ans[0], ans[1], ans[2], ans[3], rare, off, mut[0]);
        ok(ans[0] && ans[1] && !ans[2] && !ans[3] && !rare && off && !mut[0],
           "a single unique on the cursor makes the +0x30 thunk answer true", wd);

        // ---- 2: the caravan frame from the Transfer page draw ---------------------------------
        // Measured: the page mouse handler's parent origin (66,177), grid (93,177), frame
        // (66,51) 565x637 on a 1366x768 canvas at UI scale 1. The page draw gets the caravan
        // window's origin (66,51), the page place (0,126) (TransferWindow.dbr) and the inventory's
        // place (27,0): the same point, grid and frame, with no mouse move.
        auto frameOf = [](float px, float py, float gx, float gy, float s, ut::UtRectF* fr) {
            ut::UtFrameIn in;
            in.originX = px;
            in.originY = py;
            in.gridX = gx;
            in.gridY = gy;
            in.cellW = 32.0f * s;
            in.cellH = 32.0f * s;
            in.scale = s;
            in.canvasW = 1366.0f;
            in.canvasH = 768.0f;
            in.pageX = 0.0f;
            in.pageY = 126.0f;
            in.frameW = 565.0f;
            in.frameH = 637.0f;
            ut::UtRectF g;
            return ut::utFrameFromHover(in, fr, &g);
        };
        ut::UtDrawOriginIn dw;
        memset(&dw, 0, sizeof(dw));
        dw.originX = 66.0f;
        dw.originY = 51.0f;
        dw.posX = 0.0f;
        dw.posY = 126.0f;
        dw.invX = 27.0f;
        dw.invY = 0.0f;
        dw.uiScale = 1.0f;
        dw.pass = 0;
        dw.downsizingKnown = true;
        dw.downsizing = false;
        float px = 0, py = 0, gx = 0, gy = 0;
        const int r0 = ut::utDrawOrigin(dw, &px, &py, &gx, &gy);
        ut::UtRectF fr = {0, 0, 0, 0};
        const int fv = frameOf(px, py, gx, gy, 1.0f, &fr);
        _snprintf_s(wd, sizeof(wd), _TRUNCATE,
                    "page point (%.0f,%.0f), grid (%.0f,%.0f), frame (%.0f,%.0f) %.0fx%.0f (the hover "
                    "measured (66,177) / (93,177) / (66,51) 565x637)", px, py, gx, gy, fr.x, fr.y,
                    fr.w, fr.h);
        ok(r0 == ut::kUtDrawOk && fv == ut::kUtFrameOk && px == 66.0f && py == 177.0f &&
               gx == 93.0f && gy == 177.0f && fr.x == 66.0f && fr.y == 51.0f && fr.w == 565.0f &&
               fr.h == 637.0f,
           "the page draw's numbers give the frame the hover measured", wd);
        // Every UI scale 0.50..3.00: the draw's point = the window origin + the scaled page place,
        // so the derived frame is the window origin itself (the frame the handler's route gives);
        // a mutant that forgets the scale on y (origin.y + pos.y) is off for every scale but 1.
        long scaleBad = 0, scaleMut = 0;
        for (int k = 50; k <= 300; ++k) {
            const float s = (float)k / 100.0f;
            ut::UtDrawOriginIn e = dw;
            e.uiScale = s;
            e.invX = 27.0f * s;
            e.originX = 10.0f * s;
            e.originY = (768.0f - 637.0f * s) * 0.5f;
            float qx, qy, hx, hy;
            ut::UtRectF f2 = {0, 0, 0, 0};
            if (ut::utDrawOrigin(e, &qx, &qy, &hx, &hy) != ut::kUtDrawOk) {
                ++scaleBad;
                continue;
            }
            const float hoverY = e.originY + 126.0f * s;   // what the page mouse handler receives
            if (ut::utFrameMoved(qx, qy, e.originX, hoverY)) ++scaleBad;
            const int v2 = frameOf(qx, qy, hx, hy, s, &f2);
            if (v2 == ut::kUtFrameOk && ut::utFrameMoved(f2.x, f2.y, e.originX, e.originY))
                ++scaleBad;
            if (!ut::utFrameMoved(qx, e.originY + 126.0f, e.originX, hoverY) && k != 100) ++scaleMut;
            g_checks += 3;
        }
        _snprintf_s(wd, sizeof(wd), _TRUNCATE,
                    "251 UI scales: %ld wrong; the unscaled-y mutant agrees at %ld scale(s) besides 1",
                    scaleBad, scaleMut);
        ok(scaleBad == 0 && scaleMut == 0, "the draw's point follows the UI scale", wd);
        // Refused when unsure: downsizing set or unknown, the pass-1 draw, a NaN, an absurd value.
        ut::UtDrawOriginIn z = dw;
        z.downsizing = true;
        const int rDown = ut::utDrawOrigin(z, &px, &py, &gx, &gy);
        z = dw;
        z.downsizingKnown = false;
        const int rUnk = ut::utDrawOrigin(z, &px, &py, &gx, &gy);
        z = dw;
        z.pass = 1;
        const int rPass = ut::utDrawOrigin(z, &px, &py, &gx, &gy);
        z = dw;
        z.originY = std::numeric_limits<float>::quiet_NaN();
        const int rNan = ut::utDrawOrigin(z, &px, &py, &gx, &gy);
        z = dw;
        z.posY = 1.0e9f;
        const int rBig = ut::utDrawOrigin(z, &px, &py, &gx, &gy);
        z = dw;
        z.uiScale = 0.0f;
        const int rScale = ut::utDrawOrigin(z, &px, &py, &gx, &gy);
        g_checks += 6;
        ok(rDown == ut::kUtDrawDownsizing && rUnk == ut::kUtDrawDownsizing &&
               rPass == ut::kUtDrawPass1 && rNan == ut::kUtDrawBadInput &&
               rBig == ut::kUtDrawBadInput && rScale == ut::kUtDrawBadInput,
           "refused (the hover stays the route) when downsizing / unknown / pass 1 / NaN / "
           "absurd / scale 0", nullptr);
    }

    // ---------------------------------------------------------------
    {
        using namespace ut;
        char wd[200];
        // 1. the fed grid: the measured numbers - frame (66,51) 565 x 637, grid (93,177), 32 px
        const UtGridBounds frame = {1366.0f, 768.0f, 66.0f, 51.0f, 565.0f, 637.0f};
        auto hv = [](float gx, float gy, float x, float y, unsigned id, int col, int row, int w, int h) {
            UtGridHover v = {};
            v.gridX = gx;
            v.gridY = gy;
            v.x = x;
            v.y = y;
            v.id = id;
            v.col = col;
            v.row = row;
            v.w = w;
            v.h = h;
            v.cw = v.ch = 32;
            return v;
        };
        UtGridState st;
        utGridReset(&st);
        const int f1 = utGridFeed(&st, 93.0f, 177.0f, 32, 32, frame);
        const bool drawsAtOnce = st.verdict == 1 && st.gridX == 93.0f && st.gridY == 177.0f;
        const int a1 = utGridStep(&st, hv(93, 177, 234, 460, 61520, 7, 14, 2, 1), frame);
        const int a2 = utGridStep(&st, hv(93, 177, 240, 460, 61520, 7, 14, 2, 1), frame);
        const int a3 = utGridStep(&st, hv(93, 177, 266, 426, 61529, 8, 13, 2, 1), frame);
        const int a4 = utGridStep(&st, hv(95, 177, 10, 10, 61530, 0, 0, 2, 2), frame);
        g_checks += 5;
        ok(f1 == kUtGridFed && drawsAtOnce && a1 == kUtGridCrossAgree &&
               a2 == kUtGridSamePrototype && a3 == kUtGridConfirmed && a4 == kUtGridIgnored &&
               st.verdict == 1,
           "fed origin then agreeing hovers - the marks draw from the feed (no hover), two "
           "prototypes confirm, later hovers are ignored", nullptr);
        utGridReset(&st);
        utGridFeed(&st, 93.0f, 177.0f, 32, 32, frame);
        const int d1 = utGridStep(&st, hv(95, 177, 234, 460, 61520, 7, 14, 2, 1), frame);
        utGridReset(&st);
        utGridFeed(&st, 93.0f, 177.0f, 32, 32, frame);
        utGridStep(&st, hv(93, 177, 234, 460, 61520, 7, 14, 2, 1), frame);
        const int d2 = utGridStep(&st, hv(93, 177, 266, 426, 61529, 0, 0, 1, 1), frame);   // (a)
        utGridReset(&st);   // a hover BEFORE the feed that disagrees
        utGridStep(&st, hv(99, 177, 10, 10, 1, 0, 0, 2, 2), frame);
        const int d3 = utGridFeed(&st, 93.0f, 177.0f, 32, 32, frame);
        utGridReset(&st);   // ... and one that agrees: the cross-check's first hover
        utGridStep(&st, hv(93, 177, 10, 10, 1, 0, 0, 2, 2), frame);
        const int d4 = utGridFeed(&st, 93.0f, 177.0f, 32, 32, frame);
        const int d5 = utGridStep(&st, hv(93, 177, 80, 10, 2, 2, 0, 2, 2), frame);
        g_checks += 5;
        ok(d1 == kUtGridRefusedOrigin && d2 == kUtGridRefusedCell && d3 == kUtGridRefusedOrigin &&
               d4 == kUtGridFed && d5 == kUtGridConfirmed,
           "fed origin then a disagreeing hover -> refused (kUtGridRefusedOrigin, the marks "
           "off); a wrong cell refused; the order hover-then-feed is checked too", nullptr);
        // the tolerance sweep: a hover dx px off the fed origin is refused iff
        // |dx| > kUtGridFedTol (1.0 px = ut_panel's kUtFrameTol); 0.5 < |dx| <= 1.0 ADOPTS the
        // hover's origin; |dx| <= 0.5 agrees and keeps the fed one
        long sweepBad = 0, sweepMut = 0;
        for (int k = -40; k <= 40; ++k) {
            const float dx = (float)k * 0.1f;
            const float ad = dx < 0.0f ? -dx : dx;
            utGridReset(&st);
            utGridFeed(&st, 93.0f, 177.0f, 32, 32, frame);
            const int r = utGridStep(&st, hv(93.0f + dx, 177, 234, 460, 5, 7, 14, 2, 1), frame);
            const bool refused = r == kUtGridRefusedOrigin;
            const bool wantRefused = ad > 1.0f + 1e-4f;
            const bool wantAdopt = !wantRefused && ad > 0.5f + 1e-4f;
            if (refused != wantRefused) ++sweepBad;
            if (!refused && (r == kUtGridCrossAdopted) != wantAdopt) ++sweepBad;
            if (!refused && st.gridX != (wantAdopt ? 93.0f + dx : 93.0f)) ++sweepBad;
            if (!refused && st.verdict != 1) ++sweepBad;
            if (!refused && ad > 1.1f) ++sweepMut;   // a feed that never cross-checks
            ++g_checks;
        }
        _snprintf_s(wd, sizeof(wd), _TRUNCATE, "81 offsets, %ld wrong", sweepBad);
        ok(sweepBad == 0 && sweepMut == 0,
           "/ the cross-check refuses a fed grid off by > 1.0 px (kUtFrameTol), "
           "adopts the hover's origin at 0.5 .. 1.0 px", wd);
        // the band ut_panel accepts (0.5 .. 1.0 px) is no refusal
        {
            g_checks += 1;
            ok(kUtGridFedTol == kUtFrameTol, "kUtGridFedTol == ut_panel's kUtFrameTol",
               nullptr);
            // fed, then a hover 0.75 px off: adopted, the marks stay on at the hover's origin;
            // a second prototype on the same (hover) origin confirms
            utGridReset(&st);
            utGridFeed(&st, 93.0f, 177.0f, 32, 32, frame);
            const int m1 = utGridStep(&st, hv(93.75f, 177.75f, 234, 460, 61520, 7, 14, 2, 1), frame);
            const bool m1on = st.verdict == 1 && st.adopted && st.gridX == 93.75f &&
                              st.gridY == 177.75f && st.fedX == 93.0f && st.fedY == 177.0f;
            const int m2 = utGridStep(&st, hv(93.75f, 177.75f, 240, 460, 61520, 7, 14, 2, 1), frame);
            const int m3 = utGridStep(&st, hv(93.75f, 177.75f, 266, 426, 61529, 8, 13, 2, 1), frame);
            const bool m3on = st.verdict == 1 && st.confirmed && st.gridX == 93.75f;
            // fed, a hover 0.75 px off (adopted), then a second prototype back ON the fed origin:
            // the hovers disagree among themselves by 0.75 > 0.5 px -> refused, as (b)
            utGridReset(&st);
            utGridFeed(&st, 93.0f, 177.0f, 32, 32, frame);
            utGridStep(&st, hv(93.75f, 177, 234, 460, 61520, 7, 14, 2, 1), frame);
            const int m4 = utGridStep(&st, hv(93.0f, 177, 266, 426, 61529, 8, 13, 2, 1), frame);
            // a hover 1.25 px off the fed grid: refused (beyond kUtFrameTol)
            utGridReset(&st);
            utGridFeed(&st, 93.0f, 177.0f, 32, 32, frame);
            const int m5 = utGridStep(&st, hv(93.0f, 178.25f, 234, 460, 61520, 7, 14, 2, 1), frame);
            // a hover BEFORE the feed, 0.75 px off: fed at the hover's origin (adopted), then a
            // second prototype agreeing with that hover confirms
            utGridReset(&st);
            utGridStep(&st, hv(93.75f, 177, 10, 10, 1, 0, 0, 2, 2), frame);
            const int m6 = utGridFeed(&st, 93.0f, 177.0f, 32, 32, frame);
            const bool m6on = st.verdict == 1 && st.adopted && st.gridX == 93.75f && st.fedAgree == 1;
            const int m7 = utGridStep(&st, hv(93.75f, 177, 80, 10, 2, 2, 0, 2, 2), frame);
            // ... and one 1.25 px off before the feed: refused
            utGridReset(&st);
            utGridStep(&st, hv(94.25f, 177, 10, 10, 1, 0, 0, 2, 2), frame);
            const int m8 = utGridFeed(&st, 93.0f, 177.0f, 32, 32, frame);
            g_checks += 11;
            ok(m1 == kUtGridCrossAdopted && m1on && m2 == kUtGridSamePrototype &&
                   m3 == kUtGridConfirmed && m3on && m4 == kUtGridRefusedOrigin &&
                   m5 == kUtGridRefusedOrigin && m6 == kUtGridFed && m6on && m7 == kUtGridConfirmed &&
                   m8 == kUtGridRefusedOrigin,
               "fed, then a hover 0.75 px off -> no refusal (the grid adopts the "
               "hover's origin, a second prototype confirms); 1.25 px off refused; hovers among "
               "themselves keep 0.5 px; the order hover-then-feed too", nullptr);
        }
        // not usable: the grid outside the frame (the records' rect) or an absurd cell -> not fed,
        // the hovers decide exactly as before ("hovers only")
        const UtGridBounds records = {1366.0f, 768.0f, 10.0f, 65.5f, 565.0f, 637.0f};
        utGridReset(&st);
        const int n1 = utGridFeed(&st, 93.0f, 177.0f, 32, 32, records);
        const int n2 = utGridFeed(&st, 93.0f, 177.0f, 4, 32, frame);
        const int n3 = utGridFeed(&st, std::numeric_limits<float>::quiet_NaN(), 177.0f, 32, 32, frame);
        const bool stillOpen = st.verdict == 0 && !st.fed;
        UtGridHover h1 = hv(93, 177, 506, 28, 60937, 14, 0, 2, 2);
        h1.cursorRead = true;
        h1.cursorX = 93 + 506;
        h1.cursorY = 177 + 28;
        UtGridHover h2 = hv(93, 177, 10, 10, 60938, 0, 0, 2, 2);
        h2.cursorRead = true;
        h2.cursorX = 93 + 10 + 1.5f;
        h2.cursorY = 177 + 10;
        const int o1 = utGridStep(&st, h1, records);
        const int o2 = utGridStep(&st, h2, records);
        g_checks += 5;
        ok(n1 == kUtGridFedNotUsable && n2 == kUtGridFedNotUsable && n3 == kUtGridFedNotUsable &&
               stillOpen && o1 == kUtGridFirst && o2 == kUtGridAccepted && !st.fed,
           "hovers only (no usable feed) - unchanged: the cursor proof accepts", nullptr);

        // 4. the Ctrl+wheel cycle: Transfer, G1 ... G15, Transfer
        const int n = 15;
        long cyc = 0;
        int at = kUtCycleTransfer, visits = 0;
        for (int i = 0; i < n + 1; ++i) {   // forward from the real page: G1 .. G15, Transfer
            const int to = utGroupCycleStep(at, n, +1);
            if (i < n ? to != i : to != kUtCycleTransfer) ++cyc;
            at = to;
            ++visits;
        }
        at = kUtCycleTransfer;
        for (int i = 0; i < n + 1; ++i) {   // back: G15 .. G1, Transfer
            const int to = utGroupCycleStep(at, n, -1);
            if (i < n ? to != n - 1 - i : to != kUtCycleTransfer) ++cyc;
            at = to;
        }
        if (utGroupCycleStep(14, n, +1) != kUtCycleTransfer || utGroupCycleStep(0, n, -1) != kUtCycleTransfer ||
            utGroupCycleStep(kUtCycleTransfer, n, +1) != 0 || utGroupCycleStep(kUtCycleTransfer, n, -1) != 14 ||
            utGroupCycleStep(6, n, +1) != 7 || utGroupCycleStep(6, n, -1) != 5 ||
            utGroupCycleStep(3, 0, +1) != kUtCycleNone || utGroupCycleStep(3, n, 0) != kUtCycleNone)
            ++cyc;
        // the wrap (G15 -> G1) is the mutant the ends catch
        const int wrapMutant = (14 + 1) % n;
        g_checks += 2 * (n + 1) + 8;
        _snprintf_s(wd, sizeof(wd), _TRUNCATE, "%d groups, %d forward steps, %ld wrong; the old wrap "
                    "gave G%d after G15", n, visits, cyc, wrapMutant + 1);
        ok(cyc == 0 && wrapMutant != kUtCycleTransfer,
           "Ctrl+wheel down on G15 and up on G1 -> Transfer; from the real page down -> G1, "
           "up -> G15", wd);

        // the notch accumulator (the usual remainder rule)
        {
            UtWheelAcc a;
            int bad = 0;
            if (utWheelAccumulate(a, 120) != 1 || a.sum != 0) ++bad;             // one notch
            if (utWheelAccumulate(a, -240) != -2 || a.sum != 0) ++bad;           // two at once
            if (utWheelAccumulate(a, 40) != 0 || a.sum != 40) ++bad;             // a third of one: kept
            if (utWheelAccumulate(a, 40) != 0 || a.sum != 80) ++bad;
            if (utWheelAccumulate(a, 40) != 1 || a.sum != 0) ++bad;              // the third third steps
            if (utWheelAccumulate(a, 100) != 0 || a.sum != 100) ++bad;
            if (utWheelAccumulate(a, -30) != 0 || a.sum != -30) ++bad;           // a direction change drops +100
            if (utWheelAccumulate(a, -100) != -1 || a.sum != -10) ++bad;         // -130: one notch, -10 kept
            if (utWheelAccumulate(a, 0) != 0 || a.sum != -10) ++bad;             // nothing
            if (utWheelAccumulate(a, 250) != 2 || a.sum != 10) ++bad;            // reset, then 250 = 2 + 10
            UtWheelAcc f;
            for (int i = 0; i < 1000000; ++i) utWheelAccumulate(f, 0x7FFF);    // a flood stays bounded
            if (f.sum < 0 || f.sum >= 120) ++bad;
            g_checks += 11;
            _snprintf_s(wd, sizeof(wd), _TRUNCATE, "%d wrong of 11 (a whole notch, two at once, three thirds, "
                        "a direction change, -130 = one + -10, 0, 250 = two + 10, a flood)", bad);
            ok(bad == 0, "sub-notch wheel deltas add up to whole notches (remainder kept, reset on "
               "a direction change)", wd);
        }

        // the gray copy - an RGBA pixel, a DXT1 block, the names
        {
            int bad = 0;
            // (a) a 3 x 1 DDSR 32 bpp TEX v1 (masks 0 = B, G, R, A): red a=128, green a=255, white a=0
            unsigned char t[12 + 4 + 124 + 12];
            memset(t, 0, sizeof(t));
            memcpy(t, "TEX\x01", 4);
            memcpy(t + 12, "DDSR", 4);
            auto put32 = [](unsigned char* p, unsigned v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
                                                             p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24); };
            put32(t + 16, 124);          // header size
            put32(t + 24, 1);            // height
            put32(t + 28, 3);            // width
            put32(t + 40, 1);            // mips
            put32(t + 16 + 72, 32);      // pf size
            put32(t + 16 + 76, 0x40);    // DDPF_RGB
            put32(t + 16 + 84, 32);      // bpp
            unsigned char* px = t + 12 + 4 + 124;
            const unsigned char in[12] = {0, 0, 255, 128, 0, 255, 0, 255, 255, 255, 255, 0};
            memcpy(px, in, 12);
            if (utGrayTex(t, sizeof(t)) != kUtGrayRaw32) ++bad;
            const unsigned char want[12] = {76, 76, 76, 128, 150, 150, 150, 255, 255, 255, 255, 0};
            if (memcmp(px, want, 12) != 0) ++bad;          // luma 76 / 150 / 255, alpha kept
            unsigned char shortT[sizeof(t)];
            memcpy(shortT, t, sizeof(t));
            if (utGrayTex(shortT, sizeof(t) - 1) != kUtGrayNone) ++bad;   // a truncated surface: untouched
            // (b) DXT1 colour blocks: red / green flips the order (swap + remap), red / blue does not,
            // two endpoints with one gray become index 0
            unsigned char b1[8] = {0x00, 0xF8, 0xE0, 0x07, 0xE4, 0xE4, 0xE4, 0xE4};   // c0 red > c1 green
            utGrayDxtColourBlock(b1, true);
            const unsigned char w1[8] = {0xB2, 0x94, 0x69, 0x4A, 0xB1, 0xB1, 0xB1, 0xB1};
            if (memcmp(b1, w1, 8) != 0) ++bad;
            unsigned char b2[8] = {0x00, 0xF8, 0x1F, 0x00, 0xE4, 0xE4, 0xE4, 0xE4};   // c0 red > c1 blue
            utGrayDxtColourBlock(b2, true);
            const unsigned char w2[8] = {0x69, 0x4A, 0xE4, 0x20, 0xE4, 0xE4, 0xE4, 0xE4};
            if (memcmp(b2, w2, 8) != 0) ++bad;
            unsigned char b3[8] = {0x41, 0x08, 0x40, 0x08, 0xE4, 0xE4, 0xE4, 0xE4};   // two dark grays -> one
            utGrayDxtColourBlock(b3, true);
            const unsigned char w3[8] = {0x41, 0x08, 0x41, 0x08, 0, 0, 0, 0};
            if (memcmp(b3, w3, 8) != 0) ++bad;
            unsigned char b4[8] = {0xE0, 0x07, 0x00, 0xF8, 0xE4, 0xE4, 0xE4, 0xE4};   // 3-colour: green <= red
            utGrayDxtColourBlock(b4, true);   // gray green 150 > gray red 76: swap, 0<->1, 2 and 3 stay
            const unsigned char w4[8] = {0x69, 0x4A, 0xB2, 0x94, 0xE1, 0xE1, 0xE1, 0xE1};
            if (memcmp(b4, w4, 8) != 0) ++bad;
            if (utGray565(0xFFFF) != 0xFFFF || utGray565(0x0000) != 0x0000) ++bad;   // white, black stay
            // (c) the names: FNV-1a 64 (the published vectors for "" and "a"), case and slash folded
            char nm[32];
            if (!utGrayName("", nm, sizeof(nm)) || strcmp(nm, "ugcbf29ce484222325.tex") != 0) ++bad;
            if (!utGrayName("a", nm, sizeof(nm)) || strcmp(nm, "ugaf63dc4c8601ec8c.tex") != 0) ++bad;
            if (utGrayHash("Records\\Items\\X.DBR") != utGrayHash("records/items/x.dbr")) ++bad;
            if (utGrayHash("records/items/x.dbr") == utGrayHash("records/items/y.dbr")) ++bad;
            if (utGrayName("a", nm, kUtGrayNameLen) || strlen(nm) != kUtGrayNameLen) ++bad;   // the cap is checked
            g_checks += 13;
            _snprintf_s(wd, sizeof(wd), _TRUNCATE, "%d wrong of 13 (RGBA luma 76 / 150 / 255 with alpha "
                        "kept, a truncated surface refused, 4 DXT1 blocks, 565 white / black, 5 name rows)", bad);
            ok(bad == 0, "the gray copy of an icon (a 32 bpp pixel, DXT1 blocks) and its name", wd);
        }

        // 2. the capability answer for a second copy (the +0x30 thunk)
        UtDepositFacts f;
        memset(&f, 0, sizeof(f));
        f.caller = kUtDepCallerDrag;
        f.viewOn = f.mpKnown = f.tableOwns = f.bindings = f.isItem = f.inCatalogue = true;
        f.stack = 0;
        const bool first = utCapSlotAnswer(true, utDepositCapable(f), false);
        f.rowsNow = 1;
        const bool second = utCapSlotAnswer(true, utDepositCapable(f), false);
        const int why = utDepositCapableVerdict(f);
        f.rowsNow = 2;   // this player's journal: two rows of one record
        const bool third = utCapSlotAnswer(true, utDepositCapable(f), false);
        g_checks += 3;
        ok(first && !second && !third && why == kUtDepRefuseHave,
           "the +0x30 thunk answers true for a first copy, false for a record already "
           "collected (1 or 2 rows): 'already in the collection'", nullptr);

        // 3. the right-click caller: takeable / not collected / view OFF / real sack
        UtTakeFacts t = {true, true, false, true, true, true, 1u, true};
        const bool takeable = utUnderPointDecide(kUtUnderRightClick, true, kUtGestUnknown, true) ==
                                  kUtUnderTakeRightClick &&
                              utTakeDecide(t) == kUtTakeOk;
        t.fromRow = false;
        t.rows = 0;
        const bool dimRefused = utTakeDecide(t) == kUtTakeRefuseBare;
        const bool offRefused =
            utUnderPointDecide(kUtUnderRightClick, true, kUtGestUnknown, false) == kUtUnderRefuse;
        const bool realVanilla =
            utUnderPointDecide(kUtUnderRightClick, false, kUtGestUnknown, true) == kUtUnderPass &&
            !utSlotWideAllowed(kUtUnderRightClick, false, kUtGestUnknown, kUtUnderPass);
        const bool slotWide = utSlotWideAllowed(kUtUnderRightClick, true, kUtGestUnknown,
                                                kUtUnderTakeRightClick);
        g_checks += 5;
        ok(takeable && dimRefused && offRefused && realVanilla && slotWide,
           "the right-click gets the id only for a takeable prototype (slot-wide); a dim "
           "record answers 0, the view OFF refuses, a real sack stays vanilla", nullptr);
    }

    // ---- the slot-wide tint - the engine's choice and the ring geometry ---------------
    {
        using namespace ut;
        bool kindOk = true;
        for (int met = 0; met < 2; ++met)
            for (int opt = 0; opt < 2; ++opt)
                for (int cls = 0; cls < 13; ++cls)
                    for (int col = 0; col < 2; ++col) {
                        const int k = utTintKind(met != 0, opt != 0, cls, col != 0);
                        const int want = !met ? kUtTintFails
                                         : (opt && cls != 0 && col) ? kUtTintClass
                                                                    : kUtTintShade;
                        kindOk = kindOk && k == want;
                        ++g_checks;
                    }
        ok(kindOk, "the tint follows TQ.exe 0x10A850 (red when unmet, else class, else shade)",
           "104 combinations");
        // geometry: every 2x2 / 2x3 / 2x4 / 1x1 slot with every legal footprint centred in it, at
        // three cell sizes and three insets; the ring + the item = the slot, no overlap, and zero
        // parts when the item fills the slot
        bool ringOk = true, fillOk = true, bandOk = true, preOk = true;
        auto inter = [](const UtRectF& a, const UtRectF& b) {
            const float x0 = a.x > b.x ? a.x : b.x, y0 = a.y > b.y ? a.y : b.y;
            const float x1 = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
            const float y1 = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
            return x1 > x0 && y1 > y0 ? (x1 - x0) * (y1 - y0) : 0.0f;
        };
        const float cells[3] = {32.0f, 24.0f, 41.5f};
        const float insets[3] = {0.0f, 1.0f, 2.0f};
        for (int sw = 1; sw <= 2; ++sw)
            for (int sh = 1; sh <= 5; ++sh)
                for (int fw = 1; fw <= sw; ++fw)
                    for (int fh = 1; fh <= sh; ++fh)
                        for (int ci = 0; ci < 3; ++ci)
                            for (int ki = 0; ki < 3; ++ki) {
                                const float c = cells[ci], k = insets[ki];
                                const float ox = 93.0f + 3.0f * c, oy = 177.0f + 2.0f * c;
                                const UtRectF slot = {ox + k, oy + k, sw * c - 2 * k, sh * c - 2 * k};
                                const float fx = ox + (float)((sw - fw) / 2) * c,
                                            fy = oy + (float)((sh - fh) / 2) * c;
                                const UtRectF item = {fx + k, fy + k, fw * c - 2 * k, fh * c - 2 * k};
                                UtRectF r[4];
                                const int n = utSlotRing(slot, item, r);
                                float area = item.w * item.h;
                                for (int a = 0; a < n; ++a) {
                                    area += r[a].w * r[a].h;
                                    // no part overlaps the item's rect (the icon)
                                    const bool apart = r[a].x + r[a].w <= item.x + 0.001f ||
                                                       item.x + item.w <= r[a].x + 0.001f ||
                                                       r[a].y + r[a].h <= item.y + 0.001f ||
                                                       item.y + item.h <= r[a].y + 0.001f;
                                    const bool inside = r[a].x >= slot.x - 0.001f &&
                                                        r[a].y >= slot.y - 0.001f &&
                                                        r[a].x + r[a].w <= slot.x + slot.w + 0.001f &&
                                                        r[a].y + r[a].h <= slot.y + slot.h + 0.001f;
                                    ringOk = ringOk && apart && inside && r[a].w > 0 && r[a].h > 0;
                                    for (int b = 0; b < a; ++b) {
                                        const bool sep = r[a].x + r[a].w <= r[b].x + 0.001f ||
                                                         r[b].x + r[b].w <= r[a].x + 0.001f ||
                                                         r[a].y + r[a].h <= r[b].y + 0.001f ||
                                                         r[b].y + r[b].h <= r[a].y + 0.001f;
                                        ringOk = ringOk && sep;
                                    }
                                }
                                const float sa = slot.w * slot.h;
                                ringOk = ringOk && area > sa - 0.05f && area < sa + 0.05f;
                                // inside the FOOTPRINT (the icon's rect) the ring
                                // covers exactly the k-px band the engine's tint leaves, and with
                                // k > 0 and a smaller item that band is not empty - so the ring must
                                // be drawn in the PRE, under the icon (ut_panel drawTintRing)
                                const UtRectF foot = {fx, fy, fw * c, fh * c};
                                float into = 0.0f;
                                for (int a = 0; a < n; ++a) into += inter(r[a], foot);
                                const float band = inter(foot, slot) - item.w * item.h;
                                bandOk = bandOk && into > band - 0.05f && into < band + 0.05f;
                                if (k > 0.0f && (fw < sw || fh < sh)) preOk = preOk && into > 0.5f;
                                if (k == 0.0f) preOk = preOk && into < 0.001f;
                                if (fw == sw && fh == sh) fillOk = fillOk && n == 0;
                                else fillOk = fillOk && n >= 1 && n <= 4;
                                g_checks += 3;
                            }
        ok(ringOk, "ring + the engine's tint rect (the footprint inset by k) = the slot, no overlap",
           nullptr);
        ok(bandOk, "inside the footprint the ring covers only the k-px band the engine's tint "
           "leaves", nullptr);
        ok(preOk, "with k > 0 that band is not empty (so the ring is drawn in the PRE, under "
           "the icon); with k = 0 the ring never enters the footprint", nullptr);
        ok(fillOk, "an item that fills its slot gets no ring (zero rectangles)", nullptr);
        UtRectF r[4];
        const UtRectF slot = {100, 100, 64, 128}, away = {300, 300, 32, 32};
        ok(utSlotRing(slot, away, r) == 1 && r[0].w == 64 && r[0].h == 128,
           "an item outside its slot: the ring is the whole slot", nullptr);
        const UtRectF nothing = {100, 100, 0, 10};
        ok(utSlotRing(nothing, away, r) == 0, "an empty slot draws nothing", nullptr);
        const UtRectF item1 = {116, 132, 32, 64};   // a 1x2 centred in a 2x4 of 32 px
        ok(utSlotRing(slot, item1, r) == 4 && r[0].h == 32 && r[1].y == 196 && r[2].w == 16 &&
               r[3].x == 148,
           "a 1x2 in a 2x4: top, bottom, left, right bands", nullptr);
        g_checks += 3;
    }

    // ---- the UI scale sweep - one drawn cell, the pad always fits ------
    // TQ AE's own scales (Engine.dll 0x140B50 Engine::SetUIScale: 1.0, or by the screen width
    // 1.0625 / 1.15625 / 1.25 / 1.375 / 1.5 / 1.6875) plus 0.7 / 0.8 / 2.0 beyond them, on three
    // canvases. The engine's cell: floorf(32 x s + 0.5f) (Game.dll 0x198C40); the frame 565 x 637 x s
    // centred, the page at + (0,126) s, the grid at the page + (27,0) s, 16 x 15 of the cell.
    {
        const float scales[] = {0.7f, 0.8f, 1.0f, 1.0625f, 1.15625f, 1.25f, 1.375f, 1.5f, 1.6875f, 2.0f};
        const float canv[3][2] = {{1366.0f, 768.0f}, {1920.0f, 1080.0f}, {3440.0f, 1440.0f}};
        int combos = 0, skipped = 0, bad = 0, pads = 0, underFloorTq = 0, toggleShort = 0;
        float narrowArrow = 1.0e9f;
        char firstBad[200] = "";
        auto fail = [&](const char* what, float s, float cwv) {
            ++bad;
            if (!firstBad[0])
                _snprintf_s(firstBad, sizeof(firstBad), _TRUNCATE, "%s at scale %.4f on %.0f wide", what, s, cwv);
        };
        for (float s : scales) {
            for (const auto& cv : canv) {
                const float cell = ut::utEngineCellPx(s);
                if (565.0f * s > cv[0] || 637.0f * s > cv[1]) {   // the engine downsizes there
                    ++skipped;
                    continue;
                }
                ++combos;
                ut::UtFrameIn in;
                in.scale = s;
                in.canvasW = cv[0];
                in.canvasH = cv[1];
                in.originX = ut::utPadFloor((cv[0] - 565.0f * s) * 0.5f);
                in.originY = (cv[1] - 637.0f * s) * 0.5f + 126.0f * s;
                in.gridX = in.originX + 27.0f * s;
                in.gridY = in.originY;
                in.cellW = cell;
                in.cellH = cell;
                in.pageX = 0.0f;
                in.pageY = 126.0f;
                in.frameW = 565.0f;
                in.frameH = 637.0f;
                ut::UtRectF f, g;
                if (ut::utFrameFromHover(in, &f, &g) != ut::kUtFrameOk) { fail("frame refused", s, cv[0]); continue; }
                // the ONE drawn cell: grid / 16 = the sack's live cell; a cell cached at 1.0 is caught
                float cp = 0.0f;
                if (ut::utDrawnCell(g.w, g.h, cell, cell, &cp) != ut::kUtCellOk || cp != cell) fail("drawn cell", s, cv[0]);
                if ((cell > 33.0f || cell < 31.0f) && ut::utDrawnCell(g.w, g.h, 32.0f, 32.0f, nullptr) == ut::kUtCellOk)
                    fail("a stale 32 px sack cell accepted", s, cv[0]);
                // the pad: every legal pad_y / pad_h / pad_gap -> below the grid, inside the frame
                for (int py = 0; py <= 12; py += 4)
                    for (int ph = 12; ph <= 20; ph += 2)
                        for (int gp = 0; gp <= 4; ++gp) {
                            ut::UtPadIn p;
                            p.frame = f;
                            p.grid = g;
                            p.scale = s;
                            p.canvasW = cv[0];
                            p.canvasH = cv[1];
                            p.dx = 0;
                            p.dy = py;
                            p.cellH = ph;
                            p.gap = gp;
                            p.search = true;
                            ut::UtPadOut o = {};
                            ++pads;
                            if (ut::utPadLayout(p, &o) != ut::kUtPadBelow) { fail("pad not below", s, cv[0]); continue; }
                            const ut::UtRectF canvas = {0.0f, 0.0f, cv[0], cv[1]};
                            bool sound = ut::utRectInside(o.ground, f, 0.0f) && ut::utRectInside(o.ground, canvas, 0.0f) &&
                                         !ut::utRectsMeet(o.ground, g) && o.label.w > 40.0f;
                            for (int i = 0; i < ut::kUtPadMax && sound; ++i) {
                                sound = o.btn[i].w > 0.0f && o.btn[i].h >= ut::kUtPadRowMinPx &&
                                        ut::utRectInside(o.btn[i], o.ground, 0.0f) && !ut::utRectsMeet(o.btn[i], o.label);
                                for (int j = i + 1; j < ut::kUtPadMax && sound; ++j)
                                    sound = (i == ut::kUtPadSearch && j == ut::kUtPadClear) ||
                                            !ut::utRectsMeet(o.btn[i], o.btn[j]);
                            }
                            // the rows scale: never over pad_h x s, and the caption of 14 x s at most
                            if (sound && o.rowPx > ut::utPadRound((float)ph * s)) sound = false;
                            if (!sound) fail("pad unsound", s, cv[0]);
                            // the field: its text area readable (the dim "search off" at size 6 and a
                            // margin) at every fit, the clear square inside it at its right end
                            {
                                const ut::UtRectF& sf = o.btn[ut::kUtPadSearch];
                                const ut::UtRectF& sq = o.btn[ut::kUtPadClear];
                                if (!(o.fieldText.w >= ut::utPadTextWidth("search off", 6) + 6.0f) ||
                                    ut::utRectsMeet(sf, g) || !ut::utRectInside(sq, sf, 0.0f) ||
                                    sq.x + sq.w != sf.x + sf.w || sq.w != o.rowPx ||
                                    ut::utRectsMeet(o.fieldText, sq))
                                    fail("search field unreadable, on the grid, or its square misplaced", s, cv[0]);
                            }
                            // row 2: the four end at m + 4 steps - gap, the field starts at m + 4 steps;
                            // between neighbours no overlap and no hole wider than the gap + 1 px
                            {
                                const float st = (float)(o.cellW + gp);
                                const float ox = o.ground.x;
                                const int order[5] = {ut::kUtPadToggle, ut::kUtPadOwn, ut::kUtPadPrev, ut::kUtPadNext, ut::kUtPadSearch};
                                const ut::UtRectF& nb = o.btn[ut::kUtPadNext];
                                if (nb.x + nb.w != ox + ut::utPadRound((3.0f + 4.0f * st - (float)gp) * s) ||
                                    o.btn[ut::kUtPadSearch].x != ox + ut::utPadRound((3.0f + 4.0f * st) * s) ||
                                    o.btn[ut::kUtPadSearch].x + o.btn[ut::kUtPadSearch].w != ox + ut::utPadRound((3.0f + 7.0f * st - (float)gp) * s))
                                    fail("row 2 edges", s, cv[0]);
                                for (int k = 0; k + 1 < 5; ++k) {
                                    const ut::UtRectF& l = o.btn[order[k]];
                                    const float hole = o.btn[order[k + 1]].x - (l.x + l.w);
                                    if (hole < 0.0f || hole > ut::utPadRound((float)gp * s) + 1.0f) fail("row 2 hole", s, cv[0]);
                                }
                                // the label's box is the one it has with the search off, byte for byte
                                ut::UtPadIn p0 = p;
                                p0.search = false;
                                ut::UtPadOut o0 = {};
                                if (ut::utPadLayout(p0, &o0) != ut::kUtPadBelow || memcmp(&o0.label, &o.label, sizeof(o.label)) ||
                                    o0.btn[ut::kUtPadSearch].w != 0.0f || o0.btn[ut::kUtPadClear].w != 0.0f)
                                    fail("the label's box moved with the search", s, cv[0]);
                                // the captions at the pad's ONE size: OWN and < / > centred; "Transfer"
                                // beside the lamp with the default ini, else one of its short forms
                                const int fs = ut::utPadCaptionSize(o.btn[ut::kUtPadGroup0].w, o.btn[ut::kUtPadGroup0].h, 5, s);
                                const ut::UtRectF& tb = o.btn[ut::kUtPadToggle];
                                const char* tc = ut::utPadToggleCaption(tb.w, tb.h, s, fs);
                                if (fs >= 6 && (ut::utPadTextWidth("OWN", fs) > o.btn[ut::kUtPadOwn].w - 2.0f ||
                                                ut::utPadTextWidth("<", fs) > o.btn[ut::kUtPadPrev].w - 2.0f ||
                                                ut::utPadTextWidth(">", fs) > o.btn[ut::kUtPadNext].w - 2.0f))
                                    fail("OWN, < or > does not fit at the pad's size", s, cv[0]);
                                // OWN is one cell, < and > half of the next each (a gap between)
                                {
                                    const ut::UtRectF& ob2 = o.btn[ut::kUtPadOwn];
                                    const ut::UtRectF& pb2 = o.btn[ut::kUtPadPrev];
                                    if (ob2.x != ox + ut::utPadRound((3.0f + 2.0f * st) * s) ||
                                        ob2.x + ob2.w != ox + ut::utPadRound((3.0f + 3.0f * st - (float)gp) * s) ||
                                        pb2.x != ox + ut::utPadRound((3.0f + 3.0f * st) * s) ||
                                        o.btn[ut::kUtPadNext].x != ox + ut::utPadRound((3.0f + 3.5f * st) * s) ||
                                        pb2.w - o.btn[ut::kUtPadNext].w > 1.0f || o.btn[ut::kUtPadNext].w - pb2.w > 1.0f)
                                        fail("OWN not one cell or < / > not half a cell each", s, cv[0]);
                                    if (pb2.w < narrowArrow) narrowArrow = pb2.w;
                                    if (o.btn[ut::kUtPadNext].w < narrowArrow) narrowArrow = o.btn[ut::kUtPadNext].w;
                                }
                                if (py == 4 && ph == 14 && gp == 1 && strcmp(tc, "Transfer") != 0)
                                    fail("\"Transfer\" + the lamp do not fit with the default ini", s, cv[0]);
                                // the disabled field's "search off" is drawn at the pad's size too: it
                                // fits the text area with a margin with the default ini
                                if (py == 4 && ph == 14 && gp == 1 && fs >= 6 &&
                                    ut::utPadTextWidth("search off", fs) + 6.0f > o.fieldText.w)
                                    fail("\"search off\" does not fit the field at the pad's size", s, cv[0]);
                                if (strcmp(tc, "Transfer") != 0) ++toggleShort;
                            }
                            // the label's line: the widest words (3-digit counts, "indexing
                            // 1234/1588") fit the label's box at size 6, with no query and with one
                            {
                                static const char* const fw[3][2] = {
                                    {"", ""}, {"indexing 1234/1588", "1234/1588"}, {"found 1193", "=1193"}};
                                for (int q = 0; q < 3; ++q) {
                                    ut::UtPadLabelWords w;
                                    w.group = "Torso";
                                    w.span = "15-35";
                                    w.rows = 35;
                                    w.known = true;
                                    w.owned = 120;
                                    w.total = 241;
                                    w.found = fw[q][0];
                                    w.brief = fw[q][1];
                                    w.allKnown = true;
                                    w.allOwned = 1234;
                                    w.allTotal = 1588;
                                    char ln[128];
                                    const int lf = ut::utPadLabelLine(w, o.label.w, 6, ln, sizeof(ln));
                                    if (ut::utPadTextWidth(ln, lf) > o.label.w)
                                        fail("the label's line overflows its box", s, cv[0]);
                                }
                            }
                            if (s >= 1.0f && o.fit >= ut::kUtPadFitBelowFloor) ++underFloorTq;
                        }
                {   // the search's highlight on a 1 x 1 and a 2 x 3 slot: the frame on the slot's outermost
                    // ring, 1 px below a 48 px cell and 2 px from there, never in the next slot (right
                    // and below); the wash 1 px inside; the icon's middle left clear
                    const ut::UtRectF s1 = ut::utSlotDrawnRect(g, 3, 2, 1, 1), s2 = ut::utSlotDrawnRect(g, 4, 0, 2, 3);
                    const ut::UtRectF n1 = ut::utSlotDrawnRect(g, 4, 2, 1, 1), n2 = ut::utSlotDrawnRect(g, 3, 3, 1, 1);
                    const ut::UtRectF n3 = ut::utSlotDrawnRect(g, 6, 0, 1, 1), n4 = ut::utSlotDrawnRect(g, 4, 3, 2, 1);
                    const ut::UtRectF m1 = ut::utSearchMarkRect(s1, false), m2 = ut::utSearchMarkRect(s2, false);
                    const ut::UtRectF w1 = ut::utSearchMarkRect(s1, true), w2 = ut::utSearchMarkRect(s2, true);
                    const float mt = ut::utSearchMarkThick(cell);
                    if (!(!memcmp(&m1, &s1, sizeof(m1)) && !memcmp(&m2, &s2, sizeof(m2)) &&
                          mt == (cell >= 48.0f ? 2.0f : 1.0f) && (s < 1.5f) == (mt == 1.0f) &&
                          !ut::utRectsMeet(m1, n1) && !ut::utRectsMeet(m1, n2) && !ut::utRectsMeet(m2, n3) &&
                          !ut::utRectsMeet(m2, n4) && ut::utRectInside(w1, s1, 0.0f) && ut::utRectInside(w2, s2, 0.0f) &&
                          w1.x == s1.x + 1.0f && w1.w == s1.w - 2.0f && m1.w - 2.0f * mt >= 0.9f * cell))
                        fail("search mark geometry", s, cv[0]);
                }
                // the grid feed takes the frame's grid with the DRAWN cell
                ut::UtGridState st;
                ut::utGridReset(&st);
                ut::UtGridBounds b = {cv[0], cv[1], f.x, f.y, f.w, f.h};
                if (ut::utGridFeed(&st, g.x, g.y, (unsigned)cell, (unsigned)cell, b) != ut::kUtGridFed) { fail("feed", s, cv[0]); continue; }
                // a proven hover inside prototype (4,0) 2x3, in its third row: cell (4,2), agreed
                const float hx = 4.0f * cell + 0.8f * cell, hy = 2.0f * cell + 0.4f * cell;
                int hc = -1, hr = -1;
                if (!ut::utCellAtPoint(hx, hy, cell, &hc, &hr) || hc != 4 || hr != 2) fail("hover cell", s, cv[0]);
                ut::UtGridHover h = {};
                h.gridX = g.x;
                h.gridY = g.y;
                h.x = hx;
                h.y = hy;
                h.id = 126121u;
                h.col = 4;
                h.row = 0;
                h.w = 2;
                h.h = 3;
                h.cw = (unsigned)cell;
                h.ch = (unsigned)cell;
                if (ut::utGridStep(&st, h, b) != ut::kUtGridCrossAgree) fail("hover check", s, cv[0]);
                // the rect route: a 2 x 3 slot at (4,0), a widget in either unit (record w x s, or
                // drawn w x 1): the drawn slot is 2 x 3 drawn cells; a 1 x 3 item centred inside
                const ut::UtRectF footRec = {4.0f * cell, 0.0f, 64.0f, 96.0f};
                const ut::UtRectF footDrawn = {4.0f * cell, 0.0f, 2.0f * cell, 3.0f * cell};
                ut::UtRectF slot, cen;
                const float tolCell = cell - 32.0f * s;   // the engine's own rounding (0 at TQ's scales)
                const bool recOk = ut::utWidgetSlotRects(footRec, 4, 0, 2, 3, 4, 0, 2, 3, s, &slot, &cen, cell);
                if (tolCell <= 1.0f && tolCell >= -1.0f) {
                    const ut::UtRectF dr = ut::utWidgetDrawn(slot, s);
                    if (!recOk || ut::utPadRound(dr.w) != ut::utPadRound(2.0f * 32.0f * s) ||
                        ut::utPadRound(dr.h) != ut::utPadRound(3.0f * 32.0f * s) || dr.x != 4.0f * cell)
                        fail("rect route (record units)", s, cv[0]);
                }
                if (!ut::utWidgetSlotRects(footDrawn, 4, 0, 2, 3, 4, 0, 2, 3, 1.0f, &slot, &cen, cell) ||
                    slot.w != 2.0f * cell || slot.h != 3.0f * cell || slot.x != 4.0f * cell)
                    fail("rect route (drawn units)", s, cv[0]);
                const ut::UtRectF sword = {4.0f * cell, 0.0f, cell, 3.0f * cell};
                if (!ut::utWidgetSlotRects(sword, 4, 0, 1, 3, 4, 0, 2, 3, 1.0f, &slot, &cen, cell) ||
                    !ut::utRectInside(cen, slot, 0.0f) || cen.x != 4.0f * cell + ut::utSlotRound(cell * 0.5f))
                    fail("rect route (centred)", s, cv[0]);
                // a widget of a stale 32 px cell at scale 1 is refused unless 32 is the cell
                const ut::UtRectF stale = {128.0f, 0.0f, 64.0f, 96.0f};
                if ((cell > 33.0f || cell < 31.0f) &&
                    ut::utWidgetSlotRects(stale, 4, 0, 2, 3, 4, 0, 2, 3, 1.0f, &slot, &cen, cell))
                    fail("stale widget accepted", s, cv[0]);
                // the one helper: slot -> drawn rect
                const ut::UtRectF sd = ut::utSlotDrawnRect(g, 4, 0, 2, 3);
                if (sd.x != g.x + 4.0f * cell || sd.w != 2.0f * cell || sd.h != 3.0f * cell) fail("utSlotDrawnRect", s, cv[0]);
            }
        }
        _snprintf_s(d, sizeof(d), _TRUNCATE, "%d scale x canvas pairs (%d skipped: the frame is larger than the canvas), %d pads, %d bad%s%s; under GD's floor at TQ's scales: %d",
                    combos, skipped, pads, bad, bad ? ": " : "", firstBad, underFloorTq);
        printf("  (row 2: the toggle's caption was shortened in %d of %d pads - none with the default ini)\n", toggleShort, pads);
        printf("  (row 2: the narrowest < or > in the sweep is %.0f px, and its caption fits there)\n", narrowArrow);
        ok(bad == 0 && combos >= 24 && underFloorTq == 0,
           "sweep: at every scale the pad fits below the grid, the fed grid is accepted, the hover and the rect route use the drawn cell", d);
        // the five scales the row and the label are reported at: the default ini (pad_y 4, pad_h 14,
        // pad_gap 1, plate_label_size 13) on a 3440 x 1440 canvas, the grid 15 record px above the
        // page as measured on the 1366 x 768 window
        {
            const float rs[5] = {0.8f, 1.0f, 1.25f, 1.5f, 2.0f};
            int good = 0;
            for (int k = 0; k < 5; ++k) {
                const float s5 = rs[k];
                const float cell = ut::utEngineCellPx(s5);
                ut::UtFrameIn in;
                in.scale = s5;
                in.canvasW = 3440.0f;
                in.canvasH = 1440.0f;
                in.originX = ut::utPadFloor((3440.0f - 565.0f * s5) * 0.5f);
                in.originY = ut::utPadFloor((1440.0f - 637.0f * s5) * 0.5f) + 126.0f * s5;
                in.gridX = in.originX + 27.0f * s5;
                in.gridY = in.originY - 15.0f * s5;
                in.cellW = cell;
                in.cellH = cell;
                in.pageX = 0.0f;
                in.pageY = 126.0f;
                in.frameW = 565.0f;
                in.frameH = 637.0f;
                ut::UtRectF f5, g5;
                if (ut::utFrameFromHover(in, &f5, &g5) != ut::kUtFrameOk) continue;
                ut::UtPadIn p;
                p.frame = f5;
                p.grid = g5;
                p.scale = s5;
                p.canvasW = 3440.0f;
                p.canvasH = 1440.0f;
                p.dx = 0;
                p.dy = 4;
                p.cellH = 14;
                p.gap = 1;
                p.search = true;
                ut::UtPadOut o = {};
                if (ut::utPadLayout(p, &o) != ut::kUtPadBelow) continue;
                const int fs = ut::utPadCaptionSize(o.btn[ut::kUtPadGroup0].w, o.btn[ut::kUtPadGroup0].h, 5, s5);
                const ut::UtRectF& tb = o.btn[ut::kUtPadToggle];
                const char* tc = ut::utPadToggleCaption(tb.w, tb.h, s5, fs);
                const int want = (int)ut::utPadRound(13.0f * s5);   // utPlateLabelPx(13, s)
                const int lab = ut::utPadLabelFont(want, o.label.h);
                ut::UtPadLabelWords w;
                w.group = "Torso";
                w.span = "3-7";
                w.rows = 22;
                w.known = true;
                w.owned = 12;
                w.total = 241;
                w.allKnown = true;
                w.allOwned = 312;
                w.allTotal = 1588;
                char l0[128], l1[128], l2[128];
                const int f0 = ut::utPadLabelLine(w, o.label.w, lab, l0, sizeof(l0));
                w.found = "found 9";
                w.brief = "=9";
                const int f1 = ut::utPadLabelLine(w, o.label.w, lab, l1, sizeof(l1));
                w.found = "indexing 1234/1588";
                w.brief = "1234/1588";
                const int f2 = ut::utPadLabelLine(w, o.label.w, lab, l2, sizeof(l2));
                printf("  scale %.2f: rows %.0f px (%s), captions %d, toggle \"%s\" in %.0f px, label size %d in %.0f px\n"
                       "      no query \"%s\" (%d) | found 9 \"%s\" (%d) | indexing \"%s\" (%d)\n",
                       s5, o.rowPx, ut::utPadFitText(o.fit), fs, tc, tb.w, lab, o.label.w, l0, f0, l1, f1, l2, f2);
                const int rowCap = (int)o.rowPx - 1;
                if (!strcmp(tc, "Transfer") && lab == (want < rowCap ? want : rowCap) && f0 == lab && f1 == lab &&
                    f2 == lab && ut::utPadTextWidth(l0, f0) <= o.label.w && ut::utPadTextWidth(l1, f1) <= o.label.w &&
                    ut::utPadTextWidth(l2, f2) <= o.label.w)
                    ++good;
            }
            _snprintf_s(d, sizeof(d), _TRUNCATE, "%d of 5 scales", good);
            ok(good == 5,
               "default ini at 0.8 / 1.0 / 1.25 / 1.5 / 2.0: \"Transfer\" fits, the label at min(13 x s, row - 1), every line inside its box at that size", d);
        }

        // a measured 3440 x 1440 canvas at UI scale 1.375
        const float s = 1.375f;
        const ut::UtRectF f = {873.0f, 455.2f - 126.0f * s, 565.0f * s, 637.0f * s};
        const ut::UtRectF g = {910.1f, 455.2f, 704.0f, 660.0f};
        float cp = 0.0f;
        ok(ut::utEngineCellPx(s) == 44.0f && ut::utDrawnCell(g.w, g.h, 44.0f, 44.0f, &cp) == ut::kUtCellOk && cp == 44.0f &&
               ut::utDrawnCell(g.w, g.h, 32.0f, 32.0f, nullptr) == ut::kUtCellGridVsSack,
           "measured: the drawn cell is 44 px = 704 / 16 = floorf(32 x 1.375 + 0.5); the cached 32 is refused", "");
        ut::UtPadIn p;
        p.frame = f;
        p.grid = g;
        p.scale = s;
        p.canvasW = 3440.0f;
        p.canvasH = 1440.0f;
        p.dx = 0;
        p.dy = 4;
        p.cellH = 14;
        p.gap = 1;
        ut::UtPadOut o = {};
        const int band = ut::utPadLayout(p, &o);
        _snprintf_s(d, sizeof(d), _TRUNCATE, "band %d fit %d: (%.0f,%.1f) %.0fx%.0f rows %.0f gap %.0f margin %.0f; band %.3f px",
                    band, o.fit, o.ground.x, o.ground.y, o.ground.w, o.ground.h, o.rowPx, o.gapPx, o.marginPx,
                    f.y + f.h - (g.y + g.h));
        ok(band == ut::kUtPadBelow && o.fit == ut::kUtPadFitRows && o.rowPx == 17.0f && o.gapPx == 0.0f &&
               o.marginPx == 4.0f && o.ground.h == 42.0f && o.ground.w == 688.0f &&
               ut::utRectInside(o.ground, f, 0.0f) && !ut::utRectsMeet(o.ground, g),
           "measured: at 1.375 the pad fits the 42.6 px band (rows 17 px, the layout refused it)", d);
        const int fs = ut::utPadCaptionSize(o.btn[ut::kUtPadGroup0].w, o.btn[ut::kUtPadGroup0].h, 5, s);
        _snprintf_s(d, sizeof(d), _TRUNCATE, "group button %.0fx%.0f, caption %d", o.btn[ut::kUtPadGroup0].w,
                    o.btn[ut::kUtPadGroup0].h, fs);
        ok(o.btn[ut::kUtPadGroup0].w == 44.0f && fs == 12 && ut::utPadCaptionSize(64.0f, 40.0f, 3, 2.0f) == 28 &&
               ut::utPadCaptionSize(64.0f, 40.0f, 3) == 14,
           "measured: the buttons and the caption cap scale (14 x s)", d);
        int hc = -1, hr = -1;
        ok(ut::utCellAtPoint(211.9f, 104.8f, 44.0f, &hc, &hr) && hc == 4 && hr == 2 &&
               ut::utCellAtPoint(211.9f, 104.8f, 32.0f, &hc, &hr) && hc == 6 && hr == 3,
           "measured: the hover (211.9,104.8) is cell (4,2) of 44 px - the earlier (6,3) was / 32", "");
        ut::UtGridState st;
        ut::utGridReset(&st);
        const ut::UtGridBounds b = {3440.0f, 1440.0f, f.x, f.y, f.w, f.h};
        ut::UtGridHover h = {};
        h.gridX = g.x;
        h.gridY = g.y;
        h.x = 211.9f;
        h.y = 104.8f;
        h.id = 126121u;
        h.col = 4;
        h.row = 0;
        h.w = 2;
        h.h = 3;
        h.cw = 44u;
        h.ch = 44u;
        const int fed = ut::utGridFeed(&st, g.x, g.y, 44u, 44u, b);
        const int step = ut::utGridStep(&st, h, b);
        ut::UtGridState st2;
        ut::utGridReset(&st2);
        const int fed32 = ut::utGridFeed(&st2, g.x, g.y, 32u, 32u, b);
        ok(fed == ut::kUtGridFed && step == ut::kUtGridCrossAgree && fed32 == ut::kUtGridFed &&
               st2.cw == 32u,
           "measured: the fed grid of 44 px cells is accepted and the hover agrees", "");
        ut::UtRectF slot, cen;
        const ut::UtRectF foot = {176.0f, 0.0f, 64.0f, 96.0f};
        const bool rr = ut::utWidgetSlotRects(foot, 4, 0, 2, 3, 4, 0, 2, 3, s, &slot, &cen, 44.0f);
        const ut::UtRectF dr = ut::utWidgetDrawn(slot, s);
        _snprintf_s(d, sizeof(d), _TRUNCATE, "slot (%.0f,%.0f) %.0fx%.0f drawn", dr.x, dr.y, dr.w, dr.h);
        ok(rr && dr.x == 176.0f && dr.w == 88.0f && dr.h == 132.0f,
           "measured: a 2x3 slot at 1.375 is 88x132 drawn px at x 176 (4 x 44)", d);
        // the earlier numbers: the widget of the stale 32 px sack (x 128) - a drawn cell of 44 (the
        // widget's own w x scale), so the cell check passes; the position was the sack's fault
        // (fixed at the source: the prototypes are placed on the live cell)
        const ut::UtRectF earlierRect = {128.0f, 0.0f, 64.0f, 96.0f};
        ok(ut::utWidgetSlotRects(earlierRect, 4, 0, 2, 3, 4, 0, 2, 3, s, &slot, &cen, 44.0f) && slot.x == 128.0f,
           "measured: the earlier rect route wrote (128,0) 64x96 x 1.375 = 88x132 at x 128 - 48 px left of cell 4", "");
        // a disagreement (panelCellVerdict -1) refuses the rect route - never
        // "no check"; 0 (no evidence) still runs unchecked, as before
        const float nanCell = std::numeric_limits<float>::quiet_NaN();
        ok(!ut::utWidgetSlotRects(foot, 4, 0, 2, 3, 4, 0, 2, 3, s, &slot, &cen, -1.0f) &&
               !ut::utWidgetSlotRects(foot, 4, 0, 2, 3, 4, 0, 2, 3, s, &slot, &cen, nanCell) &&
               ut::utWidgetSlotRects(foot, 4, 0, 2, 3, 4, 0, 2, 3, s, &slot, &cen, 0.0f),
           "grid / 16 vs the mod sack's cell disagree (-1, NaN) -> the rect route is refused", "");
        // the label font = plate_label_size x s cut by the LABEL row's own rule
        // (3/4 h, h - 2), not by the group caption: 18 in a 17 px row -> 12; 13 in 12 -> 9; a
        // tall row keeps the scaled size; a row too short for 6 -> 0 (no label)
        ok(ut::utPadLabelFont(18, 17.0f) == 16 &&   // utPlateLabelPx(13, 1.375) = 18 (test_config)
               ut::utPadLabelFont(13, 12.0f) == 11 && ut::utPadLabelFont(18, 40.0f) == 18 &&
               ut::utPadLabelFont(13, 14.0f) == 13 && ut::utPadLabelFont(13, 7.0f) == 6 &&
               ut::utPadLabelFont(13, 6.5f) == 0 && ut::utPadLabelFont(13, nanCell) == 0 &&
               ut::utPadLabelFont(4, 20.0f) == 6,
           "the plate label is cut by its own row (its height - 1), not by the group buttons' caption", "");
    }

    // ---- the rect route's arithmetic (utWidgetSlotRects), over the REAL groups --------
    // data\oracle\uniq-groups.txt: every group's slot (G: cellW / cellH px at 32 px a cell) x every
    // footprint its records have (E lines). The item sits at the slot's top-left cell (the packer's
    // placement); the slot rect must be the slot's cells exactly, the centred rect the footprint
    // shifted by half the free cells (whole px), inside the slot, and zero shift when it fills it.
    {
        using namespace ut;
        char path[1024];
        _snprintf_s(path, sizeof(path), _TRUNCATE, "%s", __FILE__);
        char* cut = strrchr(path, '\\');
        if (!cut) cut = strrchr(path, '/');
        if (cut) *cut = 0;
        char gpath[1100], apath[1100];
        _snprintf_s(gpath, sizeof(gpath), _TRUNCATE, "%s\\..\\data\\oracle\\uniq-groups.txt", path);
        _snprintf_s(apath, sizeof(apath), _TRUNCATE, "%s\\..\\data\\oracle\\slotart.txt", path);
        FILE* f = fopen(gpath, "rb");
        ok(f != nullptr, "data\\oracle\\uniq-groups.txt readable", gpath);
        int groups = 0, shapes = 0, cases = 0, zero = 0, bad = 0, seenShape[8][8] = {{0}};
        int artMiss = 0;   // a group label the runtime art lookup does not find
        char firstBad[200] = "", firstMiss[64] = "";
        if (f) {
            char line[512];
            int sw = 0, sh = 0;
            bool seenFoot[8][8];
            memset(seenFoot, 0, sizeof(seenFoot));
            while (fgets(line, sizeof(line), f)) {
                int idx = 0, cols = 0, rows = 0, cw = 0, chh = 0, n = 0, w = 0, h = 0;
                char label[64], rec[400];
                if (line[0] == 'G' && sscanf(line, "G\t%d\t%63s\t%d\t%d\t%d\t%d\t%d", &idx, label, &cols,
                                             &rows, &cw, &chh, &n) == 7) {
                    sw = cw / 32;
                    sh = chh / 32;
                    ++groups;
                    if (utSlotArtFor(label) < 0 && artMiss++ == 0)
                        _snprintf_s(firstMiss, sizeof(firstMiss), _TRUNCATE, "%s", label);
                    memset(seenFoot, 0, sizeof(seenFoot));
                    if (sw >= 1 && sw < 8 && sh >= 1 && sh < 8 && !seenShape[sw][sh]++) ++shapes;
                    continue;
                }
                if (line[0] != 'E' || sscanf(line, "E\t%399[^\t]\t%d\t%d", rec, &w, &h) != 3) continue;
                if (w < 1 || h < 1 || w >= 8 || h >= 8 || seenFoot[w][h]) continue;
                seenFoot[w][h] = true;
                // three cell sizes (UI scale 1, a smaller and a larger UI), two slot positions
                const float cells[3] = {32.0f, 24.0f, 40.0f};
                for (int c = 0; c < 3; ++c) {
                    for (int at = 0; at < 2; ++at) {
                        const float cs = cells[c];
                        const int scol = at ? 8 : 0, srow = at ? 3 : 0;
                        const UtRectF foot = {10.0f + (float)scol * cs, 20.0f + (float)srow * cs,
                                              (float)w * cs, (float)h * cs};
                        UtRectF slot, cen;
                        const bool r = utWidgetSlotRects(foot, scol, srow, w, h, scol, srow, sw, sh, 1.0f,
                                                         &slot, &cen);
                        const float dx = cen.x - slot.x, dy = cen.y - slot.y;
                        const float wantDx = utSlotRound((float)(sw - w) * cs * 0.5f);
                        const float wantDy = utSlotRound((float)(sh - h) * cs * 0.5f);
                        const bool good = r && slot.x == foot.x && slot.y == foot.y &&
                                          slot.w == (float)sw * cs && slot.h == (float)sh * cs &&
                                          dx == wantDx && dy == wantDy && cen.w == foot.w &&
                                          cen.h == foot.h && cen.x >= slot.x && cen.y >= slot.y &&
                                          cen.x + cen.w <= slot.x + slot.w &&
                                          cen.y + cen.h <= slot.y + slot.h &&
                                          ((sw == w && sh == h) == (dx == 0.0f && dy == 0.0f));
                        ++cases;
                        if (r && dx == 0.0f && dy == 0.0f) ++zero;
                        if (!good) {
                            ++bad;
                            if (!firstBad[0])
                                _snprintf_s(firstBad, sizeof(firstBad), _TRUNCATE,
                                            "%s: %dx%d in %dx%d at %.0f px: r=%d slot %.1f,%.1f %.1fx%.1f "
                                            "shift %.1f,%.1f", label, w, h, sw, sh, cs, (int)r, slot.x,
                                            slot.y, slot.w, slot.h, dx, dy);
                        }
                    }
                }
            }
            fclose(f);
        }
        char detail[320];
        _snprintf_s(detail, sizeof(detail), _TRUNCATE,
                    "%d groups, %d slot shapes, %d footprint cases (3 cell sizes x 2 places), %d with no "
                    "shift, %d wrong%s%s", groups, shapes, cases, zero, bad, firstBad[0] ? ": " : "", firstBad);
        ok(groups == 15 && shapes == 7 && cases > 0 && bad == 0,
           "every real footprint in its group's slot: the slot rect, the centring, zero when it fills",
           detail);
        _snprintf_s(detail, sizeof(detail), _TRUNCATE, "%d groups, %d without art%s%s", groups, artMiss,
                    firstMiss[0] ? ", first: " : "", firstMiss);
        ok(groups == 15 && artMiss == 0,
           "every group label of uniq-groups.txt finds its equipment-slot art (the runtime lookup)",
           detail);
        // the 1-wide sword in the 2-wide slot: 16 px right, no vertical shift
        UtRectF slot, cen;
        const UtRectF sword = {100.0f, 200.0f, 32.0f, 128.0f};
        ok(utWidgetSlotRects(sword, 4, 0, 1, 4, 4, 0, 2, 4, 1.0f, &slot, &cen) && cen.x == 116.0f &&
               cen.y == 200.0f && slot.w == 64.0f && slot.h == 128.0f,
           "a 1x4 sword in a 2x4 slot: slot 64 x 128, icon 16 px right", nullptr);
        // an item not at the slot's top-left (a general placement): the slot rect goes back to it
        const UtRectF mid = {132.0f, 232.0f, 32.0f, 32.0f};   // cell (1,1) of a 2x2 slot at cell (0,0)
        ok(utWidgetSlotRects(mid, 1, 1, 1, 1, 0, 0, 2, 2, 1.0f, &slot, &cen) && slot.x == 100.0f &&
               slot.y == 200.0f && cen.x == 116.0f && cen.y == 216.0f,
           "an item off the slot's corner: the slot rect is found from its cell", nullptr);
        // the same cell at draw scale 0.5 (16 px drawn cells): the way back is in DRAWN px
        const UtRectF midHalf = {116.0f, 216.0f, 32.0f, 32.0f};
        ok(utWidgetSlotRects(midHalf, 1, 1, 1, 1, 0, 0, 2, 2, 0.5f, &slot, &cen) && slot.x == 100.0f &&
               slot.y == 200.0f && slot.w == 64.0f && slot.h == 64.0f && cen.x == 108.0f &&
               cen.y == 208.0f,
           "off the slot's corner at draw scale 0.5: the slot rect in drawn px",
           nullptr);
        // the draw scale: the DRAWN icon is centred in the DRAWN slot
        ok(utWidgetSlotRects(sword, 4, 0, 1, 4, 4, 0, 2, 4, 0.5f, &slot, &cen) && cen.x == 108.0f,
           "at draw scale 0.5 the shift is 8 px", nullptr);
        // refusals: not the item's footprint, outside its slot, a silly scale, non-finite
        const UtRectF skew = {100.0f, 200.0f, 32.0f, 100.0f};
        const UtRectF nan = {std::numeric_limits<float>::quiet_NaN(), 0.0f, 32.0f, 32.0f};
        ok(!utWidgetSlotRects(skew, 4, 0, 1, 4, 4, 0, 2, 4, 1.0f, &slot, &cen) &&
               !utWidgetSlotRects(sword, 4, 0, 1, 4, 5, 0, 2, 4, 1.0f, &slot, &cen) &&
               !utWidgetSlotRects(sword, 4, 0, 1, 4, 4, 0, 2, 4, 9.0f, &slot, &cen) &&
               !utWidgetSlotRects(nan, 0, 0, 1, 1, 0, 0, 1, 1, 1.0f, &slot, &cen) &&
               !utWidgetSlotRects(sword, 4, 0, 1, 4, 4, 0, 2, 4, 1.0f, nullptr, &cen),
           "a rect that is not the item's footprint in its slot is left alone", nullptr);
        g_checks += cases + 7;

        // ---- the slot art table against the oracle ui_slotart.py wrote from the game ----
        FILE* a = fopen(apath, "rb");
        ok(a != nullptr, "data\\oracle\\slotart.txt readable", apath);
        int rows = 0, same = 0;
        char firstDiff[200] = "";
        if (a) {
            char line[512];
            while (fgets(line, sizeof(line), a)) {
                char grp[64], box[64], how[32];
                int x = 0, y = 0, w = 0, h = 0, gw = 0, gh = 0;
                if (line[0] == '#' || sscanf(line, "%63s\t%63s\t%d\t%d\t%d\t%d\t%d\t%d\t%31s", grp, box, &x, &y,
                                             &w, &h, &gw, &gh, how) != 9)
                    continue;
                ++rows;
                const int k = utSlotArtFor(grp);
                UtRectF t = {0.0f, 0.0f, 0.0f, 0.0f};
                const int fit = k >= 0 ? utSlotArtFit(kUtSlotArt[k], (float)gw * 32.0f, (float)gh * 32.0f, &t) : 0;
                const bool eq = k == rows - 1 && strcmp(kUtSlotArt[k].box, box) == 0 && kUtSlotArt[k].x == x &&
                                kUtSlotArt[k].y == y && kUtSlotArt[k].w == w && kUtSlotArt[k].h == h &&
                                fit == (strcmp(how, "stretch") == 0 ? kUtArtStretch : kUtArtCover);
                // the texture rect never leaves the box, and a cover crop has the slot's aspect
                const bool inside = fit && t.x >= (float)x - 0.001f && t.y >= (float)y - 0.001f &&
                                    t.x + t.w <= (float)(x + w) + 0.001f && t.y + t.h <= (float)(y + h) + 0.001f;
                const float sa = (float)gw / (float)gh, ta = fit ? t.w / t.h : 0.0f;
                const bool aspect = fit != kUtArtCover || (ta > sa * 0.99f && ta < sa * 1.01f);
                if (eq && inside && aspect) ++same;
                else if (!firstDiff[0])
                    _snprintf_s(firstDiff, sizeof(firstDiff), _TRUNCATE, "%s: table %s (%d,%d) %dx%d fit %d, oracle %s (%d,%d) %dx%d %s",
                                grp, k >= 0 ? kUtSlotArt[k].box : "?", k >= 0 ? kUtSlotArt[k].x : 0,
                                k >= 0 ? kUtSlotArt[k].y : 0, k >= 0 ? kUtSlotArt[k].w : 0,
                                k >= 0 ? kUtSlotArt[k].h : 0, fit, box, x, y, w, h, how);
            }
            fclose(a);
        }
        _snprintf_s(detail, sizeof(detail), _TRUNCATE, "%d of %d oracle rows agree%s%s", same, rows,
                    firstDiff[0] ? "; first difference " : "", firstDiff);
        ok(rows == 15 && same == 15,
           "kUtSlotArt is the game's equipment boxes, in group order, with the oracle's fit", detail);
        UtRectF t = {0.0f, 0.0f, 0.0f, 0.0f};
        ok(utSlotArtFit(kUtSlotArt[12], 32.0f, 160.0f, &t) == kUtArtCover && t.h == 124.0f &&
               t.w > 24.7f && t.w < 24.9f && t.x > 15.0f + 22.0f && t.x < 15.0f + 22.2f,
           "the spear slot (1x5) shows the hand art's centred 24.8 x 124 column", nullptr);
        ok(utSlotArtFor("Swords") == 10 && utSlotArtFor("Sword") == -1 && utSlotArtFor(nullptr) == -1,
           "a group is found by its exact label", nullptr);
        g_checks += 3;
    }

    {   // the wheel WITHOUT the settle (ut_viewgate.h UtNavBurst; ut_live stepRows /
        // liveTakeDirty). Every sequence of wheel ticks (up / down) and GameEngine::Updates up to
        // 9 events (then one closing Update), for a group with 0, 1, 3 and 14 row offsets:
        //   - at most ONE rebuild per Update, and one for every Update that follows a moving tick;
        //   - the rebuild reports exactly the ticks that moved the offset since the last one, and
        //     the QPC of the FIRST of them (the tick-to-window latency's start);
        //   - the step it shows is at most its ticks (coalesced, never more), and after the last
        //     Update the window is the oracle's cumulative clamp: no tick lost, none repeated;
        //   - a tick at an end moves nothing and is not reported (stepRows answers false).
        // the per-tick clamp is ut_live stepRows' gdut::clampRowOffset(g.slot, shown,
        // offset), whose body (src\model\layout.cpp) is utClampRow(offset, utMaxRowOf(shown, g.cols,
        // g.rows)): the row limits come from the same utMaxRowOf over real shapes - 8 x 7 slots with
        // 40, 60, 80 and 168 (Helms) records shown = 0, 1, 3 and 14 row offsets
        const int v46Shown[4] = {40, 60, 80, 168};
        int v46Max[4];
        for (int mi = 0; mi < 4; ++mi) v46Max[mi] = ut::utMaxRowOf(v46Shown[mi], 8, 7);
        ok(v46Max[0] == 0 && v46Max[1] == 1 && v46Max[2] == 3 && v46Max[3] == 14,
           "the coalesced-wheel rows clamp with clampRowOffset's utMaxRowOf limits",
           nullptr);
        long long v46Cases = 0;
        int v46Bad = 0, v46Rebuilds = 0;
        for (int mi = 0; mi < 4; ++mi) {
            const int maxRow = v46Max[mi];
            int total = 1;
            for (int len = 1; len <= 9; ++len) {
                total *= 3;
                for (int code = 0; code < total; ++code) {
                    ut::UtNavBurst nav = {0, 0};
                    int want = 0, shown = 0, oracleRow = 0, oracleTicks = 0, cc = code;
                    long long qpc = 1000, oracleFirst = 0;
                    bool dirty = false, v46Ok = true;
                    for (int s = 0; s <= len; ++s) {
                        int ev = 2;   // the closing Update
                        if (s < len) {
                            ev = cc % 3;
                            cc /= 3;
                        }
                        qpc += 7;
                        if (ev < 2) {   // a wheel tick over the page
                            const int dir = ev == 0 ? -1 : 1;
                            oracleRow = ut::utClampRow(oracleRow + dir, maxRow);
                            const int to = ut::utClampRow(want + dir, maxRow);
                            if (to != want) {   // stepRows: the offset, the burst, then the dirty flag
                                want = to;
                                ut::utNavNote(nav, true, qpc);
                                dirty = true;
                                ++oracleTicks;
                                if (!oracleFirst) oracleFirst = qpc;
                            }
                        } else if (dirty) {   // the Update: liveTakeDirty, one rebuild
                            dirty = false;
                            const ut::UtNavBurst t = ut::utNavTake(nav);
                            const int step = want - shown;
                            shown = want;
                            ++v46Rebuilds;
                            v46Ok = v46Ok && t.ticks == oracleTicks && t.firstQpc == oracleFirst &&
                                    t.ticks >= 1 && (step < 0 ? -step : step) <= t.ticks;
                            oracleTicks = 0;
                            oracleFirst = 0;
                        } else {   // no input: no rebuild, nothing pending
                            v46Ok = v46Ok && nav.ticks == 0 && nav.firstQpc == 0 && oracleTicks == 0;
                        }
                    }
                    v46Ok = v46Ok && shown == oracleRow && shown == want && !dirty && nav.ticks == 0;
                    ++v46Cases;
                    if (!v46Ok) ++v46Bad;
                }
            }
        }
        char v46d[96];
        snprintf(v46d, sizeof(v46d), "%lld sequences, %d rebuilds, %d bad", v46Cases, v46Rebuilds,
                 v46Bad);
        ok(v46Bad == 0, "coalesced wheel: one step per Update, no row skipped or repeated",
           v46d);
        g_checks += v46Cases;
        ut::UtNavBurst nb = {0, 0};
        ut::utNavNote(nb, false, 0);   // a key / button: no tick, and a zero QPC still marks it
        const ut::UtNavBurst nt = ut::utNavTake(nb);
        ok(nt.ticks == 0 && nt.firstQpc != 0 && nb.firstQpc == 0,
           "a non-wheel input reports input-to-window, no ticks", nullptr);
    }

    {   // the cost meter's text and its once-per-session line (ut_costprobe.h)
        const long long f = 10000000;   // a 10 MHz QPC
        ut::UtCostWindow pr = {1000000, 60000, 100};   // 100 frames, 100 ms in all, max 6 ms
        ut::UtCostWindow up = {500000, 127000, 250};
        ut::UtCostWindow rb = {160000, 127000, 3};
        ut::UtCostWindow sc = {41000, 41000, 1};
        char line[200];
        ut::utCostFormat(line, sizeof(line), pr, up, rb, sc, f);
        const char* want =
            "mod: present avg 1.00 / max 6.00 ms, update avg 0.20 / max 12.70 ms, rebuilds 3 (max "
            "12.7 ms), scan 1 (max 4.1 ms)";
        ok(strcmp(line, want) == 0, "the mod: line", line);
        ut::UtCostWindow none = {0, 0, 0};
        ut::utCostFormat(line, sizeof(line), none, none, none, none, 0);
        ok(strstr(line, "present avg 0.00 / max 0.00 ms") && strstr(line, "rebuilds 0 (max 0.0 ms)"),
           "an empty window (and no frequency) formats zeros, never divides by zero", line);
        char tiny[16];
        ut::utCostFormat(tiny, sizeof(tiny), pr, up, rb, sc, f);
        ok(strlen(tiny) == 15, "a short buffer truncates, terminated", tiny);
        ut::UtCostFirst c1 = {};
        const bool a1 = ut::utCostFirstRebuild(c1, 31.8), a2 = ut::utCostFirstRebuild(c1, 12.7),
                   a3 = ut::utCostFirstRebuild(c1, 14.1), a4 = ut::utCostFirstScan(c1, 4.2),
                   a5 = ut::utCostFirstRebuild(c1, 9.0), a6 = ut::utCostFirstScan(c1, 5.0);
        ok(!a1 && !a2 && !a3 && a4 && !a5 && !a6 && c1.rebuild[2] == 14.1 && c1.scan == 4.2,
           "the first-costs line: once, when 3 rebuilds and the scan are known", nullptr);
        ut::UtCostFirst c2 = {};
        const bool b1 = ut::utCostFirstScan(c2, 4.0), b2 = ut::utCostFirstRebuild(c2, 1.0),
                   b3 = ut::utCostFirstRebuild(c2, 2.0), b4 = ut::utCostFirstRebuild(c2, 3.0),
                   b5 = ut::utCostFirstRebuild(c2, 4.0);
        ok(!b1 && !b2 && !b3 && b4 && !b5 && c2.rebuild[0] == 1.0,
           "... the scan first: said at the third rebuild, never again", nullptr);
        g_checks += 5;
    }

    printf("[viewgate]  %lld steps checked\n", g_checks);
    // ---- the view in multiplayer -----------------------------------------
    {
        // Every sequence of 1..7 events over {toggle, single, multiplayer, unknown} from a ready
        // page (bindings, world, caravan open on the Transfer tab), checked against the TRUE
        // session state the events carry: never ON while it is unknown; a toggle while OFF turns
        // ON iff it is known (multiplayer included); every CHANGE of it while ON turns the view
        // OFF; a repeat never does. The shipped step must pass; two mutants must not: the rule
        // (a toggle refused in multiplayer) and a step that ignores an unknown read.
        typedef ut::UtViewStep (*Apply)(ut::UtViewState*, int, int);
        struct Mut {
            static ut::UtViewStep oldRule(ut::UtViewState* s, int ev, int arg) {
                if (ev == ut::kUtViewEvToggle && !s->on && s->mp) {
                    ut::UtViewStep st = {false, false, ut::kUtViewWhyRefusedMp};
                    return st;
                }
                return ut::utViewApply(s, ev, arg);
            }
            static ut::UtViewStep ignoreUnknown(ut::UtViewState* s, int ev, int arg) {
                if (ev == ut::kUtViewEvMpChanged && arg == 2) {
                    ut::UtViewStep st = {false, false, ut::kUtViewWhyOk};
                    return st;
                }
                return ut::utViewApply(s, ev, arg);
            }
        };
        const int kEv[4][2] = {{ut::kUtViewEvToggle, 0}, {ut::kUtViewEvMpChanged, 0},
                               {ut::kUtViewEvMpChanged, 1}, {ut::kUtViewEvMpChanged, 2}};
        const Apply fns[3] = {&ut::utViewApply, &Mut::oldRule, &Mut::ignoreUnknown};
        long long bad[3] = {0, 0, 0};
        long long seqs = 0, steps = 0, onInMp = 0, changeOffs = 0, repeats = 0, subInMp = 0;
        for (int fi = 0; fi < 3; ++fi) {
            for (int len = 1; len <= 7; ++len) {
                int total = 1;
                for (int i = 0; i < len; ++i) total *= 4;
                for (int code = 0; code < total; ++code) {
                    ut::UtViewState s;
                    s.bindingsOk = s.world = s.open = true;
                    s.mode = 1;
                    int truth = 0;   // 0 single, 1 multiplayer, 2 unknown
                    bool seqOnInMp = false;
                    int c = code;
                    for (int i = 0; i < len; ++i, c /= 4) {
                        const int e = c % 4;
                        const bool wasOn = s.on;
                        const int prev = truth;
                        if (e > 0) truth = kEv[e][1];
                        const ut::UtViewStep st = fns[fi](&s, kEv[e][0], kEv[e][1]);
                        if (fi == 0) ++steps;
                        if (s.on && truth == 2) ++bad[fi];
                        if (e == 0 && !wasOn && st.turnedOn != (truth != 2)) ++bad[fi];
                        if (e > 0 && wasOn && truth != prev) {
                            if (st.turnedOff && !s.on) {
                                if (fi == 0) ++changeOffs;
                            } else {
                                ++bad[fi];
                            }
                        }
                        if (e > 0 && truth == prev) {
                            if (fi == 0) ++repeats;
                            if (st.turnedOff) ++bad[fi];
                        }
                        if (s.on && truth == 1) {
                            seqOnInMp = true;
                            if (fi == 0 && ut::utViewSubstitute(true, s.on, s.mode, s.world,
                                                                s.mpUnknown))
                                ++subInMp;
                        }
                    }
                    if (fi == 0) {
                        ++seqs;
                        if (seqOnInMp) ++onInMp;
                    }
                }
            }
        }
        _snprintf_s(d, sizeof(d), _TRUNCATE,
                    "%lld sequences, %lld steps: ON in multiplayer in %lld (the getter substitutes "
                    "at %lld steps), %lld changes while ON turned it OFF, %lld repeats left it; "
                    "mutants caught: rule %lld, ignore-unknown %lld",
                    seqs, steps, onInMp, subInMp, changeOffs, repeats, bad[1], bad[2]);
        ok(bad[0] == 0 && onInMp > 0 && subInMp > 0 && changeOffs > 0 && repeats > 0 &&
               bad[1] > 0 && bad[2] > 0,
           "the view in multiplayer: on in MP, off on a state change, refused when unknown", d);
    }
    {
        // the +0x30 capability (IsTransferCapable) while ON: single / multiplayer / unknown x
        // mp_collect 0 / 1 - GD's rule, and an unknown state refuses whatever mp_collect says
        ut::UtDepositFacts f;
        memset(&f, 0, sizeof(f));
        f.caller = ut::kUtDepCallerDrag;
        f.viewOn = f.tableOwns = f.bindings = f.isItem = f.inCatalogue = true;
        f.stack = 0;
        int rows = 0;
        for (int st = 0; st < 3; ++st)
            for (int collect = 0; collect < 2; ++collect) {
                f.mpKnown = st != 2;
                f.mp = st == 1;
                f.mpCollect = collect != 0;
                const bool cap = ut::utDepositCapable(f);
                const bool want = st == 0 || (st == 1 && collect == 1);
                const bool named = want || ut::utDepositCapableVerdict(f) ==
                                               (st == 2 ? ut::kUtDepRefuseMpUnknown
                                                        : ut::kUtDepRefuseMultiplayer);
                if (cap == want && named && ut::utCapSlotAnswer(true, cap, true) == want &&
                    ut::utCapSlotAnswer(false, cap, true))
                    ++rows;
            }
        _snprintf_s(d, sizeof(d), _TRUNCATE, "%d of 6 rows", rows);
        ok(rows == 6,
           "30 in MP: mp_collect=1 accepts a unique, 0 refuses (multiplayer named), unknown "
           "refuses both; OFF stays vanilla", d);
    }
    ok(strcmp(ut::utMpShapeText(false, true, true, false), "?") == 0 &&
           strcmp(ut::utMpShapeText(true, false, false, false), "off") == 0 &&
           strcmp(ut::utMpShapeText(true, true, true, false), "host") == 0 &&
           strcmp(ut::utMpShapeText(true, true, false, true), "client") == 0 &&
           strcmp(ut::utMpShapeText(true, true, true, true), "host") == 0 &&
           strcmp(ut::utMpShapeText(true, true, false, false), "on") == 0 &&
           strcmp(ut::utMpShapeText(false, false, false, false), "?") == 0,
       "heartbeat mp= host / client / off / on (known MP, no role) / ? (unknown only; "
       ")", "7 rows");
    {
        // an uncollected record shows no tint, no red and no border unless hovered - over
        // every combination (collected x hovered x owned_marks 0..3 x route live): the original
        // is skipped ONLY for uncollected + not hovered + marks != 0 + route live (3 of 32)
        int rows = 0, skips = 0;
        // the gray lend's own runtime rule (utOwnedGray, which
        // ownedGrayWanted runs) matches its spec, and gray + route live implies the skip (the gray
        // icon never sits on a coloured background) - two runtime predicates, not a restatement
        int grayRows = 0, grays = 0, implied = 0;
        for (int c = 0; c < 2; ++c)
            for (int h = 0; h < 2; ++h)
                for (int m = 0; m <= 3; ++m)
                    for (int r = 0; r < 2; ++r) {
                        const bool got = ut::utBackgroundSkip(c != 0, h != 0, m, r != 0);
                        const bool want = c == 0 && h == 0 && m >= 1 && m <= 3 && r == 1;
                        const bool gray = ut::utOwnedGray(c != 0, h != 0, m);
                        if (got) ++skips;
                        if (got == want) ++rows;
                        if (gray == (m == 3 && c == 0 && h == 0)) ++grayRows;
                        if (gray) ++grays;
                        if (!(gray && r != 0) || got) ++implied;
                    }
        _snprintf_s(d, sizeof(d), _TRUNCATE, "%d of 32 rows, %d skip", rows, skips);
        ok(rows == 32 && skips == 3,
           "background skip: only uncollected + not hovered + owned_marks 1..3 + route live; "
           "collected, hovered, marks 0 or no route draw as the engine draws", d);
        _snprintf_s(d, sizeof(d), _TRUNCATE, "%d of 32 gray rows (%d gray), %d of 32 implied",
                    grayRows, grays, implied);
        ok(grayRows == 32 && grays == 2 && implied == 32,
           "gray lend (utOwnedGray: owned_marks 3, uncollected, not hovered) + route live => "
           "the background is skipped", d);
        // the token: -1 = the original + no Post (a fault in Pre gives -1 = no skip); the save
        // index and the skip flag round-trip; a refused widget can skip with no Post
        int trows = 0;
        const int saves[] = {-1, 0, 1, 7, 255, 1023, ut::kUtBgNoSave - 1};
        for (int k = 0; k < (int)(sizeof(saves) / sizeof(saves[0])); ++k)
            for (int sk = 0; sk < 2; ++sk) {
                const int t = ut::utBgToken(saves[k], sk != 0);
                const bool skipOk = ut::utBgTokenSkip(t) == (sk != 0);
                const bool saveOk = ut::utBgTokenSave(t) == saves[k];
                const bool plain = saves[k] < 0 && sk == 0 ? t == -1 : t >= 0;
                if (skipOk && saveOk && plain) ++trows;
            }
        const bool fault = !ut::utBgTokenSkip(-1) && ut::utBgTokenSave(-1) == -1 &&
                           ut::utBgToken(ut::kUtBgNoSave, false) == -1;
        _snprintf_s(d, sizeof(d), _TRUNCATE, "%d of 14 rows, fault %s", trows,
                    fault ? "ok" : "BAD");
        ok(trows == 14 && fault,
           "the route token carries the skip: save index and flag round-trip; -1 (a fault "
           "in Pre) = the original runs", d);
    }
    printf(g_fail ? "[viewgate]  FAILED: %d\n" : "[viewgate]  ALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
