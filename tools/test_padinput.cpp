// test_padinput.cpp - the screen pad's press rules, proved without a game: src\ut_padinput.h, the
// real header the GetMouseEvent detour in ut_screenprobe.cpp runs, over the engine's 0x28-byte
// record (src\ut_inputrec.h).
//
//   tools\build_test_padinput.bat        (needs a vcvars32 shell; the .bat calls it itself)
//
//   1. the event record: utPadApply (rewrite / clear touch exactly the type at +0x08 and the bytes
//      +0x18..+0x19), utPadReadEvent;
//   2. the rectangles: inside / outside / NaN, the button lookup, freshness at every edge;
//   3. INERT: nothing published, shown = 0, stale, not proven - every event type at every point is
//      untouched and nothing acts;
//   4. the button press: LEFT DOWN on a button names it once, every event type passed on during
//      the press is cleared, the LEFT UP is rewritten and ends it, the next event is untouched;
//   5. the ground, the label, the item-on-cursor rule (consumed, no action, a press all the same);
//   6. outside: nothing touched, a press started outside is the world's;
//   7. the polled release (two generations of grace, an UP in either still ends it normally), a
//      lost UP followed by a new DOWN, a press that outlives its rectangles;
//   8. idempotence per pump generation: a repeat replays the bytes and never acts again;
//   9. the editor: Ctrl+RIGHT DOWN on the pad and on the label, its RIGHT UP, a plain RIGHT DOWN;
//      with the editor open the LEFT DOWN / MOVE / LEFT UP are the editor's;
//  10. the adopted drag (an editor drag begun in the window: cleared everywhere outside, its UP
//      cleared not rewritten), a right press ended by the next LEFT DOWN, a DOWN on which the poll
//      ends a press;
//  11. the window is the window's while the editor is open: a point inside the published window
//      rect is untouched, a press begun on the screen still runs to its UP, the adopted drag
//      untouched inside and cleared outside;
//  12. the window procedure's clicks (utPadMsgDecide / utPadMsgPoll, the path ut_panel.cpp's
//      panelInput runs): the held button and its action on the UP, the kept click and its own UP,
//      the lost UP, the right press held while the left clicks, the polled release, swapped
//      buttons, the item on the cursor, the right button, the pad and the label on the game's own
//      Transfer page (their ground kept and claimed, an item on the cursor or not);
//  13. the header's purity;
//  14. the layout editor's claimed press: in the caravan window and outside it, the DOWN, its drag
//      and its UP consumed, a right press, before the proof, after the window's own message.
// Exit code 0 = all of it held. Argument 1: the repository root (for the purity check).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/ut_padinput.h"
#include "../src/ut_padlayout.h"   // kUtPadEdit, kUtPadMax: the pad's roles

using namespace ut;

static int g_fails = 0;
static int g_checks = 0;
static void check(bool ok, const char* what) {
    ++g_checks;
    printf("  %-84s %s\n", what, ok ? "OK" : "FAIL");
    if (!ok) ++g_fails;
}

// A top-right pad at 1920x1080: ground 1500,40 400x60, three buttons, a label under it.
static UtPadScreenRects makeRects(unsigned long long tick) {
    UtPadScreenRects r;
    memset(&r, 0, sizeof(r));
    r.shown = 1;
    r.tick = tick;
    r.pad = UtPadScreenRect{1500.0f, 40.0f, 400.0f, 60.0f};
    r.label = UtPadScreenRect{1500.0f, 104.0f, 400.0f, 24.0f};
    r.buttonCount = 3;
    r.buttons[0] = UtPadScreenButton{UtPadScreenRect{1504.0f, 44.0f, 60.0f, 24.0f}, 0};    // Transfer
    r.buttons[1] = UtPadScreenButton{UtPadScreenRect{1568.0f, 44.0f, 60.0f, 24.0f}, 7};    // a group
    r.buttons[2] = UtPadScreenButton{UtPadScreenRect{1830.0f, 70.0f, 60.0f, 24.0f}, 3};    // OWN
    return r;
}

static UtPadInGates gates() {
    UtPadInGates g;
    memset(&g, 0, sizeof(g));
    g.fresh = 1;
    g.proven = 1;
    g.leftUp = 0;
    g.rightUp = 1;
    return g;
}

static UtPadInEvent ev(int type, float x, float y) { return UtPadInEvent{type, x, y}; }

static bool untouched(const UtPadInOut& o) {
    return !o.rewrite && !o.clear && o.act == kUtPadActNone;
}

// A fresh 0x28-byte record as GetMouseEvent fills it: tags 0/0, the type, x, y, the held bytes.
static void record(unsigned char* b, int type, float x, float y, unsigned char l, unsigned char r) {
    memset(b, 0xAB, kUtMouseEventSize);
    const int zero = 0;
    memcpy(b + kUtMouseEvtTag0Off, &zero, 4);
    memcpy(b + kUtMouseEvtTag1Off, &zero, 4);
    memcpy(b + kUtMouseEvtTypeOff, &type, 4);
    memcpy(b + kUtMouseEvtXOff, &x, 4);
    memcpy(b + kUtMouseEvtYOff, &y, 4);
    b[kUtMouseEvtHeldOff] = l;
    b[kUtMouseEvtHeldOff + 1] = r;
}

static int typeOf(const unsigned char* b) {
    int t = -1;
    memcpy(&t, b + kUtMouseEvtTypeOff, 4);
    return t;
}

static const int kAllTypes[] = {kUtEvtMove, kUtEvtLeftDown, kUtEvtRightDown, 3, 4, 5, 6, 7, 8,
                                kUtEvtLeftUp, kUtEvtRightUp, 0xB, 0xC, kUtEvtWheelUp,
                                kUtEvtWheelDown};
static const int kTypeCount = (int)(sizeof(kAllTypes) / sizeof(kAllTypes[0]));

int main(int argc, char** argv) {
    const char* root = argc > 1 ? argv[1] : ".";
    const unsigned long long T = 1000;   // the frame the rectangles were drawn at
    const UtPadScreenRects R = makeRects(T);

    // ---------------------------------------------------------------------------------------
    printf("1. the event record (0x28 bytes: type +0x08, x +0x0C, y +0x10, held +0x18)\n");
    {
        check(kUtMouseEventSize == 0x28 && kUtMouseEvtTypeOff == 0x08 && kUtMouseEvtXOff == 0x0C &&
                  kUtMouseEvtYOff == 0x10 && kUtMouseEvtHeldOff == 0x18 && kUtMouseEvtHeldCleared == 2,
              "the record the detour edits is ut_inputrec.h's, the two held bytes left and right");
        unsigned char b[kUtMouseEventSize];
        record(b, kUtEvtLeftDown, 1530.0f, 50.0f, 1, 1);
        UtPadInOut o;
        memset(&o, 0, sizeof(o));
        o.rewrite = 1;
        utPadApply(b, o);
        float x = 0, y = 0;
        memcpy(&x, b + kUtMouseEvtXOff, 4);
        memcpy(&y, b + kUtMouseEvtYOff, 4);
        bool rest = true;
        for (size_t i = 0x14; i < kUtMouseEventSize; ++i) {
            if (i == kUtMouseEvtHeldOff || i == kUtMouseEvtHeldOff + 1) continue;
            if (b[i] != 0xAB) rest = false;
        }
        check(typeOf(b) == kUtEvtMove && x == 1530.0f && y == 50.0f && b[0x18] == 0 && b[0x19] == 0 &&
                  rest && b[0] == 0 && b[4] == 0,
              "a rewrite: type -> MOVE, bytes 0x18..0x19 -> 0, tags, x/y and every other byte kept");
        record(b, kUtEvtWheelUp, 10.0f, 20.0f, 1, 1);
        memset(&o, 0, sizeof(o));
        o.clear = 1;
        utPadApply(b, o);
        check(typeOf(b) == kUtEvtWheelUp && b[0x18] == 0 && b[0x19] == 0 && b[0x1A] == 0xAB,
              "a clear: the type kept, bytes 0x18..0x19 -> 0, the middle button's byte 0x1A kept");
        record(b, kUtEvtLeftDown, 10.0f, 20.0f, 1, 0);
        memset(&o, 0, sizeof(o));
        utPadApply(b, o);
        check(typeOf(b) == kUtEvtLeftDown && b[0x18] == 1, "an untouched decision writes nothing");
        record(b, kUtEvtRightDown, 12.5f, 7.25f, 0, 1);
        const UtPadInEvent e = utPadReadEvent(b);
        check(e.type == kUtEvtRightDown && e.x == 12.5f && e.y == 7.25f,
              "utPadReadEvent reads type, x and y at +0x08 / +0x0C / +0x10");
        utPadApply(nullptr, o);
        check(utPadReadEvent(nullptr).type == 0, "a null record: nothing read, nothing written");
    }

    // ---------------------------------------------------------------------------------------
    printf("2. the rectangles\n");
    {
        check(utPadScreenIn(R.pad, 1500.0f, 40.0f) && !utPadScreenIn(R.pad, 1900.0f, 40.0f) &&
                  !utPadScreenIn(R.pad, 1499.9f, 50.0f) && !utPadScreenIn(R.pad, 1600.0f, 100.0f),
              "inside is [x, x+w) x [y, y+h)");
        const float nan = nanf("");
        check(!utPadScreenInAny(R, nan, 50.0f) && !utPadScreenInAny(R, 1600.0f, nan),
              "a NaN point is inside nothing");
        check(!utPadScreenIn(UtPadScreenRect{0, 0, 0, 10}, 0, 5) &&
                  !utPadScreenIn(UtPadScreenRect{0, 0, 10, -1}, 1, 0),
              "a rectangle with w or h <= 0 is absent");
        check(utPadScreenButtonAt(R, 1510.0f, 50.0f) == 0 && utPadScreenButtonAt(R, 1570.0f, 60.0f) == 7 &&
                  utPadScreenButtonAt(R, 1850.0f, 80.0f) == 3,
              "the button lookup returns the pad's button index");
        check(utPadScreenButtonAt(R, 1700.0f, 50.0f) == -1 && utPadScreenButtonAt(R, 1510.0f, 110.0f) == -1,
              "the pad's ground and the label have no button");
        UtPadScreenRects odd = R;
        odd.buttons[3] = UtPadScreenButton{UtPadScreenRect{100.0f, 100.0f, 50.0f, 50.0f}, 9};
        odd.buttonCount = 4;
        check(utPadScreenButtonAt(odd, 110.0f, 110.0f) == -1,
              "a button published outside the pad's ground is never hit");
        odd = R;
        odd.buttonCount = 100000;
        check(utPadScreenButtonAt(odd, 1510.0f, 50.0f) == 0, "a wild buttonCount is clamped");
        check(utPadScreenRectsFresh(R, T) && utPadScreenRectsFresh(R, T + kUtPadScreenStaleTicks),
              "fresh at the drawn frame and kUtPadScreenStaleTicks frames later");
        check(!utPadScreenRectsFresh(R, T + kUtPadScreenStaleTicks + 1), "stale one frame after that");
        check(!utPadScreenRectsFresh(R, T - 1), "a frame BEFORE the draw (a wrap) is stale");
        UtPadScreenRects z = R;
        z.tick = 0;
        check(!utPadScreenRectsFresh(z, 3), "never stamped (tick 0) is never fresh");
        z = R;
        z.shown = 0;
        check(!utPadScreenRectsFresh(z, T), "shown = 0 is never fresh");
        z = R;
        z.pad = UtPadScreenRect{0, 0, 0, 0};
        check(utPadScreenRectsFresh(z, T), "a label alone is fresh");
        z.label = UtPadScreenRect{0, 0, 0, 0};
        check(!utPadScreenRectsFresh(z, T), "neither a pad nor a label is never fresh");
        z.window = UtPadScreenRect{10, 10, 565, 637};
        check(utPadScreenWindowFresh(z, T) && !utPadScreenWindowFresh(z, T + 5),
              "the editor's window rect is fresh on its own, and goes stale like the rest");
        UtPadScreenRects none;
        memset(&none, 0, sizeof(none));
        check(!utPadScreenRectsFresh(none, 0) && !utPadScreenRectsFresh(none, T),
              "the zeroed struct (nothing published) is never fresh");
    }

    // ---------------------------------------------------------------------------------------
    printf("3. INERT until fresh rectangles and a proven space\n");
    {
        UtPadScreenRects none;
        memset(&none, 0, sizeof(none));
        const float pts[][2] = {{1510, 50}, {1700, 50}, {1600, 110}, {10, 10}, {1920, 1080}};
        int bad = 0, n = 0;
        UtPadInState s;
        utPadInReset(&s);
        unsigned long long gen = 1;
        for (int variant = 0; variant < 4; ++variant) {
            for (const auto& p : pts) {
                for (int t = 0; t < kTypeCount; ++t) {
                    for (int mod = 0; mod < 4; ++mod) {
                        UtPadInGates g = gates();
                        g.ctrl = mod & 1;
                        g.editorOpen = (mod >> 1) & 1;
                        const UtPadScreenRects* rr = &R;
                        if (variant == 0) { rr = &none; g.fresh = utPadScreenRectsFresh(none, T); }
                        if (variant == 1) g.fresh = utPadScreenRectsFresh(R, T + 50);
                        if (variant == 2) g.proven = 0;
                        if (variant == 3) { g.fresh = 0; g.proven = 0; }
                        const UtPadInOut o =
                            utPadInput(&s, gen++, 0, ev(kAllTypes[t], p[0], p[1]), *rr, g);
                        ++n;
                        if (!untouched(o) || s.press != kUtPressNone) ++bad;
                    }
                }
            }
        }
        char what[120];
        _snprintf_s(what, sizeof(what), _TRUNCATE,
                    "%d events (nothing published / stale / unproven) - all untouched, no press", n);
        check(bad == 0, what);
        unsigned char b[kUtMouseEventSize], keep[kUtMouseEventSize];
        record(b, kUtEvtLeftDown, 1510.0f, 50.0f, 1, 0);
        memcpy(keep, b, sizeof(b));
        UtPadInGates g = gates();
        g.fresh = utPadScreenRectsFresh(none, T);
        const UtPadInOut o = utPadInput(&s, gen++, 0, ev(kUtEvtLeftDown, 1510.0f, 50.0f), none, g);
        utPadApply(b, o);
        check(memcmp(b, keep, sizeof(b)) == 0 && o.why == kUtPadWhyInert,
              "a LEFT DOWN on where a button would be, nothing published: bytes identical");
    }

    // ---------------------------------------------------------------------------------------
    printf("4. the button press\n");
    {
        UtPadInState s;
        utPadInReset(&s);
        UtPadInGates g = gates();
        unsigned long long gen = 10;
        UtPadInOut o = utPadInput(&s, gen, 0, ev(kUtEvtLeftDown, 1570.0f, 60.0f), R, g);
        check(o.rewrite && o.act == kUtPadActButton && o.button == 7 && o.why == kUtPadWhyButton &&
                  s.press == kUtPressButton,
              "LEFT DOWN on a button: rewritten, the button named, a press");
        int bad = 0, acts = 0;
        int idx = 1;
        for (int t = 0; t < kTypeCount; ++t) {
            if (kAllTypes[t] == kUtEvtLeftUp || kAllTypes[t] == kUtEvtLeftDown) continue;
            o = utPadInput(&s, gen, idx++, ev(kAllTypes[t], 300.0f + t, 900.0f), R, g);
            if (!o.clear || o.rewrite || o.why != kUtPadWhyHeld) ++bad;
            if (o.act != kUtPadActNone) ++acts;
        }
        check(bad == 0 && acts == 0,
              "every other event type passed on during the press: bytes cleared, type kept");
        ++gen;
        o = utPadInput(&s, gen, 0, ev(kUtEvtMove, 1570.0f, 60.0f), R, g);
        check(o.clear && !o.rewrite && o.act == kUtPadActNone,
              "a MOVE back over the same button in a later generation: cleared, no second action");
        o = utPadInput(&s, gen, 1, ev(kUtEvtLeftUp, 200.0f, 200.0f), R, g);
        check(o.rewrite && o.ended == 1 && o.why == kUtPadWhyUp && s.press == kUtPressNone,
              "the LEFT UP, wherever it is: rewritten, the press ends");
        o = utPadInput(&s, gen, 2, ev(kUtEvtMove, 1570.0f, 60.0f), R, g);
        check(untouched(o), "the next MOVE (over the button): untouched - hover is the game's");
        o = utPadInput(&s, gen, 3, ev(kUtEvtLeftDown, 1850.0f, 80.0f), R, g);
        check(o.rewrite && o.act == kUtPadActButton && o.button == 3, "a click on OWN: its index");
        o = utPadInput(&s, gen, 4, ev(kUtEvtLeftUp, 1850.0f, 80.0f), R, g);
        check(o.rewrite && o.ended == 1, "and its UP");
        unsigned char b[kUtMouseEventSize];
        ++gen;
        record(b, kUtEvtLeftDown, 1510.0f, 50.0f, 1, 0);
        o = utPadInput(&s, gen, 0, utPadReadEvent(b), R, g);
        utPadApply(b, o);
        const bool downOk = typeOf(b) == kUtEvtMove && b[0x18] == 0;
        record(b, kUtEvtMove, 1600.0f, 300.0f, 1, 0);
        o = utPadInput(&s, gen, 1, utPadReadEvent(b), R, g);
        utPadApply(b, o);
        const bool moveOk = typeOf(b) == kUtEvtMove && b[0x18] == 0;
        record(b, kUtEvtLeftUp, 1600.0f, 300.0f, 0, 0);
        o = utPadInput(&s, gen, 2, utPadReadEvent(b), R, g);
        utPadApply(b, o);
        check(downOk && moveOk && typeOf(b) == kUtEvtMove,
              "the bytes over a whole click: no consumer ever sees a DOWN, a held byte or an UP");
    }

    // ---------------------------------------------------------------------------------------
    printf("5. the ground, the label, an item on the cursor\n");
    {
        UtPadInState s;
        utPadInReset(&s);
        UtPadInGates g = gates();
        UtPadInOut o = utPadInput(&s, 20, 0, ev(kUtEvtLeftDown, 1700.0f, 50.0f), R, g);
        check(o.rewrite && o.act == kUtPadActNone && o.why == kUtPadWhyGround &&
                  s.press == kUtPressKept,
              "LEFT DOWN on the pad's ground: consumed, no action, a press");
        o = utPadInput(&s, 20, 1, ev(kUtEvtMove, 10.0f, 10.0f), R, g);
        check(o.clear, "its held MOVE out over the world: cleared");
        o = utPadInput(&s, 20, 2, ev(kUtEvtLeftUp, 10.0f, 10.0f), R, g);
        check(o.rewrite && o.ended == 1, "its UP over the world: rewritten");
        o = utPadInput(&s, 21, 0, ev(kUtEvtLeftDown, 1600.0f, 110.0f), R, g);
        check(o.rewrite && o.act == kUtPadActNone && o.why == kUtPadWhyGround,
              "LEFT DOWN on the label: consumed, no action");
        o = utPadInput(&s, 21, 1, ev(kUtEvtLeftUp, 1600.0f, 110.0f), R, g);
        check(o.rewrite && o.ended == 1, "and its UP");
        g.cursorItem = 1;
        unsigned char b[kUtMouseEventSize];
        record(b, kUtEvtLeftDown, 1570.0f, 60.0f, 1, 0);
        o = utPadInput(&s, 22, 0, utPadReadEvent(b), R, g);
        utPadApply(b, o);
        check(o.rewrite && o.act == kUtPadActNone && o.why == kUtPadWhyCursorItem &&
                  s.press == kUtPressKept && typeOf(b) == kUtEvtMove && b[0x18] == 0,
              "ITEM ON THE CURSOR, LEFT DOWN on a button: a MOVE with no held byte, NO action");
        record(b, kUtEvtLeftUp, 1570.0f, 60.0f, 0, 0);
        o = utPadInput(&s, 22, 1, utPadReadEvent(b), R, g);
        utPadApply(b, o);
        check(o.rewrite && o.ended == 1 && typeOf(b) == kUtEvtMove,
              "its UP: rewritten - no drop reaches the world either");
        g.editorOpen = 1;
        o = utPadInput(&s, 23, 0, ev(kUtEvtLeftDown, 1570.0f, 60.0f), R, g);
        check(o.rewrite && o.act == kUtPadActEditor && o.editorType == kUtEvtLeftDown &&
                  o.why == kUtPadWhyEditor && s.press == kUtPressEditor,
              "an item on the cursor with the editor open: consumed (the item stays), the editor's");
        o = utPadInput(&s, 23, 1, ev(kUtEvtLeftUp, 1570.0f, 60.0f), R, g);
        check(o.rewrite && o.ended == 1 && o.act == kUtPadActEditor,
              "and its UP: rewritten - no drop reaches the world - and the editor's");
        g.editorOpen = 0;
        g.ctrl = 1;
        o = utPadInput(&s, 24, 0, ev(kUtEvtRightDown, 1700.0f, 50.0f), R, g);
        check(o.rewrite && o.act == kUtPadActNone && o.why == kUtPadWhyCursorItem,
              "an item on the cursor, Ctrl+RIGHT DOWN on the pad: consumed, no editor");
        o = utPadInput(&s, 24, 1, ev(kUtEvtRightUp, 1700.0f, 50.0f), R, g);
        check(o.rewrite && o.ended == 1, "and its RIGHT UP");
    }

    // ---------------------------------------------------------------------------------------
    printf("6. outside the rectangles\n");
    {
        UtPadInState s;
        utPadInReset(&s);
        UtPadInGates g = gates();
        int bad = 0;
        int idx = 0;
        for (int t = 0; t < kTypeCount; ++t) {
            for (int mod = 0; mod < 2; ++mod) {
                g.ctrl = mod;
                const UtPadInOut o = utPadInput(&s, 30, idx++, ev(kAllTypes[t], 1499.0f, 50.0f), R, g);
                if (!untouched(o) || s.press != kUtPressNone) ++bad;
            }
        }
        check(bad == 0, "every event type, Ctrl up or down, one pixel left of the pad: untouched");
        g.ctrl = 0;
        UtPadInOut o = utPadInput(&s, 31, 0, ev(kUtEvtLeftDown, 800.0f, 500.0f), R, g);
        check(untouched(o) && s.press == kUtPressNone, "a LEFT DOWN on the world: the world's");
        o = utPadInput(&s, 31, 1, ev(kUtEvtMove, 1570.0f, 60.0f), R, g);
        check(untouched(o), "dragged onto a button: the held MOVE is still the world's");
        o = utPadInput(&s, 31, 2, ev(kUtEvtLeftUp, 1570.0f, 60.0f), R, g);
        check(untouched(o), "released over the button: the UP is the world's, nothing acts");
        o = utPadInput(&s, 32, 0, ev(kUtEvtRightDown, 1570.0f, 60.0f), R, g);
        check(o.rewrite && o.act == kUtPadActNone && o.why == kUtPadWhyGround &&
                  s.press == kUtPressRight,
              "a plain RIGHT DOWN on a pad button: consumed, no action (no skill under the pad)");
        o = utPadInput(&s, 32, 1, ev(kUtEvtMove, 1575.0f, 62.0f), R, g);
        check(o.clear && !o.rewrite && s.press == kUtPressRight,
              "  the MOVE during that right press: the held bytes cleared");
        o = utPadInput(&s, 32, 2, ev(kUtEvtRightUp, 1575.0f, 62.0f), R, g);
        check(o.rewrite && o.ended && s.press == kUtPressNone, "  its RIGHT UP: rewritten, the press ends");
        o = utPadInput(&s, 33, 0, ev(kUtEvtRightDown, 1600.0f, 110.0f), R, g);
        check(o.rewrite && o.act == kUtPadActNone && s.press == kUtPressRight,
              "a plain RIGHT DOWN on the label: consumed, no action");
        o = utPadInput(&s, 33, 1, ev(kUtEvtRightUp, 1600.0f, 110.0f), R, g);
        o = utPadInput(&s, 34, 0, ev(kUtEvtRightDown, 800.0f, 500.0f), R, g);
        check(untouched(o) && s.press == kUtPressNone,
              "a plain RIGHT DOWN on the world: the world's (the skill click)");
        o = utPadInput(&s, 34, 1, ev(kUtEvtWheelUp, 1570.0f, 60.0f), R, g);
        check(untouched(o), "the wheel over the pad: passes");
    }

    // ---------------------------------------------------------------------------------------
    printf("7. the polled release, a lost UP, a press that outlives its rectangles\n");
    {
        UtPadInState s;
        utPadInReset(&s);
        UtPadInGates g = gates();
        UtPadInOut o = utPadInput(&s, 40, 0, ev(kUtEvtLeftDown, 1570.0f, 60.0f), R, g);
        g.leftUp = 1;
        o = utPadInput(&s, 40, 1, ev(kUtEvtMove, 1570.0f, 60.0f), R, g);
        const bool g0 = o.clear && s.press == kUtPressButton;
        o = utPadInput(&s, 41, 0, ev(kUtEvtMove, 1570.0f, 60.0f), R, g);
        const bool g1 = o.clear && s.press == kUtPressButton;
        o = utPadInput(&s, 42, 0, ev(kUtEvtMove, 1570.0f, 60.0f), R, g);
        check(g0 && g1, "the poll says up: the press holds through that generation and the next");
        check(o.ended == 2 && o.clear && s.press == kUtPressNone,
              "two generations later with no UP: the polled release ends it (that event cleared)");
        o = utPadInput(&s, 42, 1, ev(kUtEvtMove, 1570.0f, 60.0f), R, g);
        check(untouched(o), "the event after it: untouched");
        g.leftUp = 0;
        o = utPadInput(&s, 50, 0, ev(kUtEvtLeftDown, 1700.0f, 50.0f), R, g);
        g.leftUp = 1;
        o = utPadInput(&s, 50, 1, ev(kUtEvtMove, 1700.0f, 50.0f), R, g);
        o = utPadInput(&s, 51, 0, ev(kUtEvtLeftUp, 1700.0f, 50.0f), R, g);
        check(o.rewrite && o.ended == 1,
              "the poll saw up first, the UP came one generation later: rewritten as its UP");
        g.leftUp = 0;
        o = utPadInput(&s, 60, 0, ev(kUtEvtLeftDown, 1570.0f, 60.0f), R, g);
        g.leftUp = 1;
        o = utPadInput(&s, 60, 1, ev(kUtEvtMove, 1570.0f, 60.0f), R, g);
        g.leftUp = 0;
        o = utPadInput(&s, 61, 0, ev(kUtEvtMove, 1570.0f, 60.0f), R, g);
        o = utPadInput(&s, 62, 0, ev(kUtEvtMove, 1570.0f, 60.0f), R, g);
        o = utPadInput(&s, 63, 0, ev(kUtEvtMove, 1570.0f, 60.0f), R, g);
        check(o.clear && s.press == kUtPressButton, "a poll that goes down again re-arms the grace");
        o = utPadInput(&s, 63, 1, ev(kUtEvtLeftDown, 1510.0f, 50.0f), R, g);
        check(o.ended == 3 && o.rewrite && o.act == kUtPadActButton && o.button == 0 &&
                  s.press == kUtPressButton && s.pressButton == 0,
              "a LEFT DOWN while the press is open (lost UP): the old one ends, the new one acts");
        o = utPadInput(&s, 63, 2, ev(kUtEvtLeftUp, 1510.0f, 50.0f), R, g);
        o = utPadInput(&s, 64, 0, ev(kUtEvtLeftDown, 1700.0f, 50.0f), R, g);
        o = utPadInput(&s, 64, 1, ev(kUtEvtLeftDown, 800.0f, 500.0f), R, g);
        check(o.ended == 3 && untouched(o) && s.press == kUtPressNone,
              "a lost UP then a DOWN on the world: the old press ends, the DOWN is the world's");
        utPadInReset(&s);
        g = gates();
        o = utPadInput(&s, 70, 0, ev(kUtEvtLeftDown, 1570.0f, 60.0f), R, g);
        g.fresh = 0;
        o = utPadInput(&s, 71, 0, ev(kUtEvtMove, 1570.0f, 60.0f), R, g);
        const bool held = o.clear;
        o = utPadInput(&s, 71, 1, ev(kUtEvtLeftUp, 1570.0f, 60.0f), R, g);
        check(held && o.rewrite && o.ended == 1,
              "rectangles stale mid-press (the caravan closed): still cleared, the UP rewritten");
        o = utPadInput(&s, 72, 0, ev(kUtEvtLeftDown, 1570.0f, 60.0f), R, g);
        check(untouched(o), "and the next DOWN with stale rectangles: untouched");
    }

    // ---------------------------------------------------------------------------------------
    printf("8. idempotence per pump generation\n");
    {
        UtPadInState s;
        utPadInReset(&s);
        UtPadInGates g = gates();
        const UtPadInOut a = utPadInput(&s, 80, 0, ev(kUtEvtLeftDown, 1850.0f, 80.0f), R, g);
        const UtPadInOut b = utPadInput(&s, 80, 0, ev(kUtEvtLeftDown, 1850.0f, 80.0f), R, g);
        check(a.act == kUtPadActButton && b.act == kUtPadActNone && b.repeat && b.rewrite && a.rewrite,
              "the same (generation, index) twice: the same rewrite, the button named only once");
        unsigned char x1[kUtMouseEventSize], x2[kUtMouseEventSize];
        record(x1, kUtEvtLeftDown, 1850.0f, 80.0f, 1, 0);
        record(x2, kUtEvtLeftDown, 1850.0f, 80.0f, 1, 0);
        utPadApply(x1, a);
        utPadApply(x2, b);
        check(memcmp(x1, x2, sizeof(x1)) == 0, "byte for byte the same record both times");
        const UtPadInOut c = utPadInput(&s, 80, 1, ev(kUtEvtMove, 10.0f, 10.0f), R, g);
        const UtPadInOut d = utPadInput(&s, 80, 1, ev(kUtEvtMove, 10.0f, 10.0f), R, g);
        check(c.clear && d.clear && d.repeat, "a held event repeated: cleared both times");
        const UtPadInOut u1 = utPadInput(&s, 80, 2, ev(kUtEvtLeftUp, 10.0f, 10.0f), R, g);
        const UtPadInOut u2 = utPadInput(&s, 80, 2, ev(kUtEvtLeftUp, 10.0f, 10.0f), R, g);
        check(u1.rewrite && u1.ended == 1 && u2.rewrite && u2.repeat && u2.ended == 0,
              "the UP repeated: rewritten both times, the press ended once");
        const UtPadInOut k1 = utPadInput(&s, 80, 3, ev(kUtEvtLeftDown, 1850.0f, 80.0f), R, g);
        const UtPadInOut k2 = utPadInput(&s, 80, 3, ev(kUtEvtLeftUp, 1850.0f, 80.0f), R, g);
        const UtPadInOut k3 = utPadInput(&s, 80, 3, ev(kUtEvtLeftDown, 800.0f, 500.0f), R, g);
        check(k1.act == kUtPadActButton && !k2.repeat && k2.ended == 1 && !k3.repeat && untouched(k3),
              "the same key with a DIFFERENT event (a stuck counter): decided anew");
        const UtPadInOut n1 = utPadInput(&s, 81, 0, ev(kUtEvtLeftDown, 1850.0f, 80.0f), R, g);
        check(n1.act == kUtPadActButton && !n1.repeat, "the same index in the NEXT generation acts");
        utPadInput(&s, 81, 1, ev(kUtEvtLeftUp, 1850.0f, 80.0f), R, g);
        g.editorOpen = 1;
        const UtPadInOut e1 = utPadInput(&s, 82, 0, ev(kUtEvtLeftDown, 1700.0f, 50.0f), R, g);
        const UtPadInOut e2 = utPadInput(&s, 82, 0, ev(kUtEvtLeftDown, 1700.0f, 50.0f), R, g);
        check(e1.act == kUtPadActEditor && e2.act == kUtPadActNone && e2.rewrite,
              "a repeat of an editor press: rewritten, not the editor's twice");
    }

    // ---------------------------------------------------------------------------------------
    printf("9. the editor\n");
    {
        UtPadInState s;
        utPadInReset(&s);
        UtPadInGates g = gates();
        g.ctrl = 1;
        UtPadInOut o = utPadInput(&s, 90, 0, ev(kUtEvtRightDown, 1700.0f, 50.0f), R, g);
        check(o.rewrite && o.act == kUtPadActEditor && o.editorType == kUtEvtRightDown &&
                  o.why == kUtPadWhyCtrlRight && s.press == kUtPressRight,
              "Ctrl+RIGHT DOWN on the pad's ground: the editor's entry, rewritten");
        o = utPadInput(&s, 90, 1, ev(kUtEvtMove, 1710.0f, 50.0f), R, g);
        check(o.clear && o.act == kUtPadActNone, "its held MOVE: cleared");
        o = utPadInput(&s, 90, 2, ev(kUtEvtRightUp, 1710.0f, 50.0f), R, g);
        check(o.rewrite && o.ended == 1 && s.press == kUtPressNone, "its RIGHT UP: rewritten");
        o = utPadInput(&s, 91, 0, ev(kUtEvtRightDown, 1570.0f, 60.0f), R, g);
        check(o.act == kUtPadActEditor && o.editorType == kUtEvtRightDown,
              "Ctrl+RIGHT DOWN on a button: the editor's entry too (not the button)");
        g.rightUp = 1;
        o = utPadInput(&s, 92, 0, ev(kUtEvtMove, 1570.0f, 60.0f), R, g);
        o = utPadInput(&s, 93, 0, ev(kUtEvtMove, 1570.0f, 60.0f), R, g);
        o = utPadInput(&s, 94, 0, ev(kUtEvtMove, 1570.0f, 60.0f), R, g);
        check(o.ended == 2 && s.press == kUtPressNone,
              "a Ctrl+right press whose RIGHT UP is lost: the polled RIGHT button ends it");
        o = utPadInput(&s, 95, 0, ev(kUtEvtRightDown, 1600.0f, 110.0f), R, g);
        check(o.rewrite && o.act == kUtPadActEditor && o.editorType == kUtEvtRightDown,
              "Ctrl+RIGHT DOWN on the label: the editor's entry");
        o = utPadInput(&s, 95, 1, ev(kUtEvtRightUp, 1600.0f, 110.0f), R, g);
        o = utPadInput(&s, 96, 0, ev(kUtEvtRightDown, 1000.0f, 500.0f), R, g);
        check(untouched(o), "Ctrl+RIGHT DOWN outside: untouched");
        utPadInReset(&s);
        g = gates();
        g.editorOpen = 1;
        o = utPadInput(&s, 100, 0, ev(kUtEvtMove, 1700.0f, 50.0f), R, g);
        check(!o.rewrite && !o.clear && o.act == kUtPadActEditor && o.editorType == kUtEvtMove,
              "editor open, a MOVE inside: the editor's, bytes untouched");
        o = utPadInput(&s, 100, 1, ev(kUtEvtMove, 900.0f, 500.0f), R, g);
        check(untouched(o), "editor open, a MOVE outside with no press: untouched");
        o = utPadInput(&s, 100, 2, ev(kUtEvtLeftDown, 1570.0f, 60.0f), R, g);
        check(o.rewrite && o.act == kUtPadActEditor && o.editorType == kUtEvtLeftDown &&
                  s.press == kUtPressEditor,
              "editor open, LEFT DOWN on a button: the editor's (not the button's), a drag");
        o = utPadInput(&s, 101, 0, ev(kUtEvtMove, 600.0f, 700.0f), R, g);
        check(o.clear && o.act == kUtPadActEditor && o.editorType == kUtEvtMove,
              "the drag's MOVE far outside: the editor's, held bytes cleared");
        o = utPadInput(&s, 101, 1, ev(kUtEvtWheelDown, 600.0f, 700.0f), R, g);
        check(o.clear && o.act == kUtPadActNone, "a wheel during the drag: cleared");
        o = utPadInput(&s, 101, 2, ev(kUtEvtLeftUp, 600.0f, 700.0f), R, g);
        check(o.rewrite && o.act == kUtPadActEditor && o.editorType == kUtEvtLeftUp &&
                  o.ended == 1 && s.press == kUtPressNone,
              "the drag's LEFT UP outside: the editor's and rewritten");
        o = utPadInput(&s, 102, 0, ev(kUtEvtLeftUp, 1700.0f, 50.0f), R, g);
        check(!o.rewrite && !o.clear && o.act == kUtPadActEditor && o.editorType == kUtEvtLeftUp,
              "a stray LEFT UP inside, no press: never consumed");
        g.ctrl = 1;
        o = utPadInput(&s, 103, 0, ev(kUtEvtRightDown, 1700.0f, 50.0f), R, g);
        check(o.rewrite && o.act == kUtPadActNone,
              "Ctrl+RIGHT DOWN with the editor already open: consumed, nothing to enter");
        o = utPadInput(&s, 103, 1, ev(kUtEvtRightUp, 1700.0f, 50.0f), R, g);
        check(o.rewrite && o.ended == 1, "and its RIGHT UP");
        g.proven = 0;
        o = utPadInput(&s, 104, 0, ev(kUtEvtLeftDown, 1700.0f, 50.0f), R, g);
        check(untouched(o), "editor open but the space not proven: untouched");
    }

    // ---------------------------------------------------------------------------------------
    printf("10. the adopted drag, a right press and a new DOWN\n");
    {
        UtPadInState s;
        utPadInReset(&s);
        UtPadInGates g = gates();
        g.editorOpen = 1;
        g.editorDrag = 1;
        UtPadInOut o = utPadInput(&s, 310, 0, ev(kUtEvtMove, 900.0f, 300.0f), R, g);
        check(o.clear && !o.rewrite && o.act == kUtPadActEditor && o.editorType == kUtEvtMove &&
                  s.press == kUtPressEditor && s.adopted,
              "editor mid-drag, a MOVE over the world: adopted - bytes cleared, the editor's");
        o = utPadInput(&s, 310, 1, ev(kUtEvtWheelUp, 900.0f, 300.0f), R, g);
        check(o.clear && o.act == kUtPadActNone, "a wheel during it: cleared");
        o = utPadInput(&s, 311, 0, ev(kUtEvtLeftUp, 905.0f, 290.0f), R, g);
        check(o.clear && !o.rewrite && o.ended == 1 && o.act == kUtPadActEditor &&
                  o.editorType == kUtEvtLeftUp && s.press == kUtPressNone && !s.adopted,
              "its LEFT UP over the world: CLEARED not rewritten (the caravan saw the DOWN)");
        g.editorDrag = 0;
        o = utPadInput(&s, 311, 1, ev(kUtEvtMove, 905.0f, 290.0f), R, g);
        check(untouched(o), "the drag over: the next MOVE over the world is untouched");
        g.editorDrag = 1;
        g.proven = 0;
        o = utPadInput(&s, 312, 0, ev(kUtEvtMove, 905.0f, 290.0f), R, g);
        check(untouched(o) && s.press == kUtPressNone, "not proven: nothing adopted");
        g.proven = 1;
        o = utPadInput(&s, 313, 0, ev(kUtEvtLeftDown, 905.0f, 290.0f), R, g);
        check(untouched(o) && s.press == kUtPressNone,
              "a LEFT DOWN is never adopted: outside the rectangles it is the world's");
        o = utPadInput(&s, 314, 0, ev(kUtEvtMove, 905.0f, 290.0f), R, g);
        g.leftUp = 1;
        o = utPadInput(&s, 314, 1, ev(kUtEvtMove, 905.0f, 290.0f), R, g);
        o = utPadInput(&s, 315, 0, ev(kUtEvtMove, 905.0f, 290.0f), R, g);
        o = utPadInput(&s, 316, 0, ev(kUtEvtMove, 905.0f, 290.0f), R, g);
        check(o.ended == 2 && s.press == kUtPressNone, "an adopted drag whose UP is lost: the poll ends it");
        utPadInReset(&s);
        g = gates();
        g.ctrl = 1;
        o = utPadInput(&s, 320, 0, ev(kUtEvtRightDown, 1700.0f, 50.0f), R, g);
        check(o.act == kUtPadActEditor && s.press == kUtPressRight, "Ctrl+RIGHT DOWN: the editor opens");
        g.ctrl = 0;
        g.editorOpen = 1;
        g.rightUp = 1;
        o = utPadInput(&s, 321, 0, ev(kUtEvtLeftDown, 1700.0f, 50.0f), R, g);
        check(o.ended == 3 && o.rewrite && o.act == kUtPadActEditor && o.editorType == kUtEvtLeftDown &&
                  s.press == kUtPressEditor,
              "the next LEFT DOWN with the RIGHT UP lost: the right press ends, the DOWN is the editor's");
        utPadInReset(&s);
        g = gates();
        g.ctrl = 1;
        o = utPadInput(&s, 330, 0, ev(kUtEvtRightDown, 1700.0f, 50.0f), R, g);
        g.ctrl = 0;
        g.rightUp = 1;
        o = utPadInput(&s, 330, 1, ev(kUtEvtMove, 1700.0f, 50.0f), R, g);
        o = utPadInput(&s, 331, 0, ev(kUtEvtMove, 1700.0f, 50.0f), R, g);
        o = utPadInput(&s, 332, 0, ev(kUtEvtRightDown, 1570.0f, 60.0f), R, g);
        check(o.ended == 2 && o.rewrite && s.press == kUtPressRight && o.why == kUtPadWhyGround,
              "a RIGHT DOWN on which the poll ends the old press: judged on its own");
    }

    // ---------------------------------------------------------------------------------------
    printf("11. the window is the window's while the editor is open\n");
    {
        UtPadScreenRects W = R;
        W.window = UtPadScreenRect{10.0f, 200.0f, 565.0f, 637.0f};
        W.pad = UtPadScreenRect{1500.0f, 40.0f, 400.0f, 60.0f};
        UtPadInState s;
        utPadInReset(&s);
        UtPadInGates g = gates();
        g.editorOpen = 1;
        g.windowFresh = 1;
        int bad = 0;
        for (int t = 0; t < kTypeCount; ++t) {
            const UtPadInOut o = utPadInput(&s, 400, t, ev(kAllTypes[t], 300.0f, 500.0f), W, g);
            if (!untouched(o) || o.why != kUtPadWhyWindow || s.press != kUtPressNone) ++bad;
        }
        check(bad == 0, "every event type inside the window rect: untouched, no press (the window's)");
        UtPadInOut o = utPadInput(&s, 401, 0, ev(kUtEvtLeftDown, 1700.0f, 50.0f), W, g);
        check(o.rewrite && s.press == kUtPressEditor, "a LEFT DOWN on the screen pad: the editor's");
        o = utPadInput(&s, 401, 1, ev(kUtEvtMove, 300.0f, 500.0f), W, g);
        check(o.clear && o.act == kUtPadActEditor, "carried into the window: still cleared and the editor's");
        o = utPadInput(&s, 401, 2, ev(kUtEvtLeftUp, 300.0f, 500.0f), W, g);
        check(o.rewrite && o.ended == 1, "its UP inside the window: rewritten (the caravan never saw its DOWN)");
        g.editorDrag = 1;
        o = utPadInput(&s, 402, 0, ev(kUtEvtMove, 300.0f, 500.0f), W, g);
        check(untouched(o) && o.why == kUtPadWhyWindow && s.adopted,
              "an adopted drag inside the window: untouched");
        o = utPadInput(&s, 402, 1, ev(kUtEvtMove, 900.0f, 100.0f), W, g);
        check(o.clear && o.act == kUtPadActEditor, "  outside it: cleared, the editor's");
        o = utPadInput(&s, 402, 2, ev(kUtEvtLeftUp, 300.0f, 500.0f), W, g);
        check(untouched(o) && o.ended == 1 && s.press == kUtPressNone,
              "  its UP inside the window: untouched, the press over");
        g.windowFresh = 0;
        g.editorDrag = 0;
        o = utPadInput(&s, 403, 0, ev(kUtEvtLeftDown, 300.0f, 500.0f), W, g);
        check(untouched(o) && o.why != kUtPadWhyWindow, "a stale window rect: the point is judged as any other");
    }

    // ---------------------------------------------------------------------------------------
    printf("12. the window procedure's clicks (utPadMsgDecide / utPadMsgPoll, ut_panel.cpp panelInput)\n");
    {
        UtPadMsgState s;
        utPadMsgReset(&s);
        const UtPadMsgFacts onBtn5 = {5, 0, 0}, onBtn6 = {6, 0, 0}, page = {-1, 0, 0};
        const UtPadMsgFacts ground = {-1, 1, 0}, scrBtn5 = {5, 1, 0}, scrItem = {5, 1, 1};
        UtPadMsgOut o = utPadMsgDecide(&s, kUtMsgLeftDown, onBtn5);
        check(o.swallow && o.claim && o.judged && o.act < 0 && s.down == 5 && utPadMsgOpen(s),
              "LEFT DOWN on a button: swallowed, claimed, held (no action yet)");
        o = utPadMsgDecide(&s, kUtMsgLeftUp, onBtn5);
        check(o.swallow && o.claim && o.act == 5 && s.down < 0 && !utPadMsgOpen(s),
              "its LEFT UP on the same button: the button runs (the action on the UP)");
        utPadMsgDecide(&s, kUtMsgLeftDown, onBtn5);
        o = utPadMsgDecide(&s, kUtMsgLeftUp, onBtn6);
        check(o.swallow && o.act < 0 && s.down < 0, "released on another button: swallowed, nothing runs");
        o = utPadMsgDecide(&s, kUtMsgLeftUp, onBtn5);
        check(!o.swallow && o.act < 0, "a LEFT UP with no press of the pad's: the engine's own click");
        o = utPadMsgDecide(&s, kUtMsgLeftDown, page);
        check(!o.swallow && o.judged && !utPadMsgOpen(s), "a LEFT DOWN off the pad: passes");
        o = utPadMsgDecide(&s, kUtMsgLeftDown, scrBtn5);
        utPadMsgDecide(&s, kUtMsgLeftUp, scrBtn5);
        check(o.swallow && o.claim, "a button of the screen pad: the same press");

        // the ground of a screen part, and the kept click
        utPadMsgReset(&s);
        o = utPadMsgDecide(&s, kUtMsgLeftDown, ground);
        check(o.swallow && !o.claim && o.act < 0 && s.kept == kUtMsgLeft,
              "LEFT DOWN on a screen part's ground: swallowed, kept, no action");
        o = utPadMsgDecide(&s, kUtMsgLeftUp, page);
        check(o.swallow && s.kept == 0, "  its own LEFT UP (wherever it lands): swallowed, the latch ends");

        // B1, the lost UP: pressed on the ground, released off the window (no WM_LBUTTONUP)
        utPadMsgReset(&s);
        utPadMsgDecide(&s, kUtMsgLeftDown, ground);
        o = utPadMsgDecide(&s, kUtMsgLeftDown, page);
        check(!o.swallow && o.ended == kUtMsgLeft && s.kept == 0,
              "the lost UP: the next LEFT DOWN on the Transfer page passes and ends the latch");
        o = utPadMsgDecide(&s, kUtMsgLeftUp, page);
        check(!o.swallow, "  and its LEFT UP passes too (the item is picked up)");

        // B1, the right press held while the left clicks the page
        utPadMsgReset(&s);
        o = utPadMsgDecide(&s, kUtMsgRightDown, ground);
        check(o.swallow && s.kept == kUtMsgRight, "a RIGHT DOWN on the label: swallowed, kept");
        o = utPadMsgDecide(&s, kUtMsgLeftDown, page);
        check(!o.swallow && s.kept == kUtMsgRight, "  a LEFT DOWN on the page meanwhile: passes, the right latch stays");
        o = utPadMsgDecide(&s, kUtMsgLeftUp, page);
        check(!o.swallow && s.kept == kUtMsgRight, "  its LEFT UP: passes (a left UP never ends the right latch)");
        o = utPadMsgDecide(&s, kUtMsgRightUp, page);
        check(o.swallow && s.kept == 0, "  the RIGHT UP: swallowed, the latch ends");
        utPadMsgDecide(&s, kUtMsgRightDown, ground);
        o = utPadMsgDecide(&s, kUtMsgRightDown, page);
        check(!o.swallow && o.ended == kUtMsgRight && s.kept == 0,
              "a RIGHT DOWN after a lost RIGHT UP: the old latch ends, the new one is judged on its own");

        // the polled release (Alt+Tab with the button held, a release off the window)
        utPadMsgReset(&s);
        utPadMsgDecide(&s, kUtMsgLeftDown, ground);
        check(utPadMsgPoll(&s, 1, 0, 0) == 0 && s.kept == kUtMsgLeft, "the poll: held - the latch stays");
        check(utPadMsgPoll(&s, 0, 0, 0) == 0 && s.kept == kUtMsgLeft, "  up at one poll: not yet");
        check(utPadMsgPoll(&s, 0, 0, 0) == kUtMsgLeft && s.kept == 0, "  up at the next poll: the latch ends");
        o = utPadMsgDecide(&s, kUtMsgLeftUp, page);
        check(!o.swallow, "  a stray LEFT UP after it: passes");
        utPadMsgDecide(&s, kUtMsgLeftDown, ground);
        utPadMsgPoll(&s, 0, 0, 0);
        o = utPadMsgDecide(&s, kUtMsgLeftUp, page);
        check(o.swallow && s.kept == 0 && s.upSeen == 0 && utPadMsgPoll(&s, 0, 0, 0) == 0,
              "an UP queued when the poll first saw the button up still ends the latch normally");
        utPadMsgDecide(&s, kUtMsgLeftDown, ground);
        utPadMsgPoll(&s, 0, 0, 0);
        utPadMsgPoll(&s, 1, 0, 0);
        check(utPadMsgPoll(&s, 0, 0, 0) == 0 && s.kept == kUtMsgLeft, "  a poll that sees it held again starts over");
        utPadMsgReset(&s);

        // swapped buttons: the logical left is the physical right
        utPadMsgDecide(&s, kUtMsgLeftDown, ground);
        utPadMsgPoll(&s, 0, 1, 1);
        check(utPadMsgPoll(&s, 0, 1, 1) == 0 && s.kept == kUtMsgLeft,
              "swapped buttons: the physical right held keeps the logical left's latch");
        utPadMsgPoll(&s, 1, 0, 1);
        check(utPadMsgPoll(&s, 1, 0, 1) == kUtMsgLeft && s.kept == 0,
              "  the physical right up (the left held) ends it");
        utPadMsgDecide(&s, kUtMsgRightDown, ground);
        utPadMsgPoll(&s, 0, 1, 1);
        check(utPadMsgPoll(&s, 0, 1, 1) == kUtMsgRight && s.kept == 0,
              "  the logical right's latch ends when the physical left is up");

        // the held button whose UP was lost
        utPadMsgReset(&s);
        utPadMsgDecide(&s, kUtMsgLeftDown, onBtn5);
        utPadMsgPoll(&s, 0, 0, 0);
        check(utPadMsgPoll(&s, 0, 0, 0) == kUtMsgLeft && s.down < 0,
              "a held button released off the window: the poll lets it go (no longer drawn pressed)");
        utPadMsgDecide(&s, kUtMsgLeftDown, onBtn5);
        o = utPadMsgDecide(&s, kUtMsgLeftDown, page);
        check(!o.swallow && o.ended == kUtMsgLeft && s.down < 0,
              "  or the next LEFT DOWN off the pad: it passes and the held button ends");
        o = utPadMsgDecide(&s, kUtMsgLeftUp, page);
        check(!o.swallow && o.act < 0, "  its LEFT UP is the engine's (never swallowed for the lost press)");

        // the item on the cursor
        utPadMsgReset(&s);
        o = utPadMsgDecide(&s, kUtMsgLeftDown, scrItem);
        check(o.swallow && !o.claim && !o.judged && s.down < 0 && s.kept == kUtMsgLeft,
              "an item on the cursor, on a screen button: swallowed, kept, no button held");
        o = utPadMsgDecide(&s, kUtMsgLeftUp, scrItem);
        check(o.swallow && o.act < 0 && s.kept == 0, "  its LEFT UP: swallowed, nothing runs");
        o = utPadMsgDecide(&s, kUtMsgLeftDown, UtPadMsgFacts{5, 0, 1});
        check(o.swallow && o.claim && s.down == 5, "an item on the cursor over the in-window pad: the button's press");
        utPadMsgReset(&s);

        // the right button on the pad
        o = utPadMsgDecide(&s, kUtMsgRightDown, onBtn5);
        check(o.swallow && s.kept == kUtMsgRight, "a RIGHT DOWN on a button: swallowed and kept, nothing runs");
        o = utPadMsgDecide(&s, kUtMsgRightUp, page);
        check(o.swallow && o.act < 0 && s.kept == 0, "  its RIGHT UP, wherever it lands: swallowed, nothing runs");
        o = utPadMsgDecide(&s, kUtMsgRightDown, page);
        check(!o.swallow && o.judged, "a RIGHT DOWN off the pad: passes (the blur rule still reads it)");
        o = utPadMsgDecide(&s, kUtMsgRightUp, ground);
        check(!o.swallow && s.kept == 0, "  its RIGHT UP on a screen part's ground (a held skill let go there): passes");
        o = utPadMsgDecide(&s, kUtMsgRightUp, onBtn5);
        check(!o.swallow && o.act < 0, "a RIGHT UP on a button the pad never pressed: passes");
        check(!utPadMsgDecide(nullptr, kUtMsgLeftDown, onBtn5).swallow && utPadMsgPoll(nullptr, 0, 0, 0) == 0,
              "no state: nothing swallowed");

        // the game's own Transfer page: the pad and the label drawn over its cells are theirs whole
        utPadMsgReset(&s);
        const UtPadMsgFacts realGround = {-1, 0, 0, 1}, realBtn5 = {5, 0, 0, 1};
        const UtPadMsgFacts realItem = {-1, 0, 1, 1}, realBtnItem = {5, 0, 1, 1};
        o = utPadMsgDecide(&s, kUtMsgLeftDown, realGround);
        check(o.swallow && o.claim && o.judged && o.act < 0 && s.kept == kUtMsgLeft && s.down < 0,
              "the real page: LEFT DOWN on the pad's ground over a cell: swallowed, claimed, kept, no action");
        o = utPadMsgDecide(&s, kUtMsgLeftUp, page);
        check(o.swallow && o.act < 0 && s.kept == 0, "  its LEFT UP (wherever it lands): swallowed, the latch ends");
        o = utPadMsgDecide(&s, kUtMsgRightDown, realGround);
        check(o.swallow && o.claim && s.kept == kUtMsgRight,
              "the real page: RIGHT DOWN on the pad's ground or the label: swallowed, claimed, kept");
        o = utPadMsgDecide(&s, kUtMsgRightUp, page);
        check(o.swallow && s.kept == 0, "  its RIGHT UP: swallowed, the latch ends");
        o = utPadMsgDecide(&s, kUtMsgLeftDown, realItem);
        check(o.swallow && o.claim && o.act < 0 && s.kept == kUtMsgLeft,
              "the real page: an item on the cursor over the pad's ground: kept and claimed (never dropped on "
              "the covered cell)");
        utPadMsgDecide(&s, kUtMsgLeftUp, realItem);
        o = utPadMsgDecide(&s, kUtMsgLeftDown, realBtn5);
        check(o.swallow && o.claim && s.down == 5 && s.kept == 0, "the real page: a button over a cell: the button's press");
        o = utPadMsgDecide(&s, kUtMsgLeftUp, realBtn5);
        check(o.swallow && o.act == 5, "  its LEFT UP on the same button: the button runs");
        o = utPadMsgDecide(&s, kUtMsgLeftDown, realBtnItem);
        utPadMsgDecide(&s, kUtMsgLeftUp, realBtnItem);
        check(o.swallow && o.claim && s.down < 0, "the real page: a button with an item on the cursor: acts as in the window");
        o = utPadMsgDecide(&s, kUtMsgLeftDown, page);
        check(!o.swallow && !o.claim && !utPadMsgOpen(s), "the real page, off the pad: the cell's own click passes");
        o = utPadMsgDecide(&s, kUtMsgRightDown, page);
        check(!o.swallow && !o.claim, "  and its right-click too");
        utPadMsgDecide(&s, kUtMsgRightUp, page);
        utPadMsgDecide(&s, kUtMsgLeftDown, realGround);
        o = utPadMsgDecide(&s, kUtMsgLeftDown, realGround);
        check(o.swallow && o.ended == kUtMsgLeft && s.kept == kUtMsgLeft,
              "the real page: a second LEFT DOWN on the pad after a lost UP: the old latch ends, the new one is kept");
        // a screen part pinned over the real cells with an item on the cursor: kept like any screen
        // part with an item held, NOT claimed - the GetMouseEvent detour consumes the engine's own
        // read of a click on a screen part, so nothing reaches the cell under it
        utPadMsgReset(&s);
        const UtPadMsgFacts realScreenItem = {-1, 1, 1, 1};
        o = utPadMsgDecide(&s, kUtMsgLeftDown, realScreenItem);
        check(o.swallow && !o.claim && !o.judged && o.act < 0 && s.kept == kUtMsgLeft,
              "the real page: a screen part over the cells, an item on the cursor: kept, not claimed (the "
              "GetMouseEvent detour consumes the engine's read)");
        o = utPadMsgDecide(&s, kUtMsgLeftUp, realScreenItem);
        check(o.swallow && s.kept == 0, "  its LEFT UP: swallowed, the latch ends");
        utPadMsgReset(&s);
    }

    // ---------------------------------------------------------------------------------------
    printf("13. purity\n");
    {
        char path[600];
        _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\src\\ut_padinput.h", root);
        FILE* f = nullptr;
        fopen_s(&f, path, "rb");
        static char buf[1 << 17];
        size_t n = 0;
        if (f) {
            n = fread(buf, 1, sizeof(buf) - 1, f);
            fclose(f);
        }
        buf[n] = 0;
        check(n > 0 && !strstr(buf, "<windows.h>") && !strstr(buf, "GetAsyncKeyState(") &&
                  !strstr(buf, "malloc(") && !strstr(buf, "std::") && !strstr(buf, "\nstatic "),
              "the header is pure: no Windows, no allocation, no state of its own");
    }

    printf("\n14. the layout editor's claimed press\n");
    {
        // The editor open over the caravan window at 873,282 777 x 876 (client px), every part in
        // the window: no screen pad or label published, only the window.
        UtPadScreenRects W;
        memset(&W, 0, sizeof(W));
        W.shown = 1;
        W.tick = T;
        W.window = UtPadScreenRect{873.0f, 282.0f, 777.0f, 876.0f};
        UtPadInGates g = gates();
        g.fresh = utPadScreenRectsFresh(W, T) ? 1 : 0;
        g.editorOpen = 1;
        g.windowFresh = utPadScreenWindowFresh(W, T) ? 1 : 0;
        const float hx = 873.0f + 779.0f, hy = 282.0f + 271.0f;   // the grid's right-edge handle, 2 px out
        UtPadInState s;
        utPadInReset(&s);
        unsigned long long gen = 5000;
        UtPadInOut o = utPadInput(&s, ++gen, 0, ev(kUtEvtLeftDown, hx, hy), W, g);
        check(g.fresh == 0 && g.windowFresh == 1 && untouched(o) && s.press == kUtPressNone,
              "without the claim the handle's press 2 px outside the window reaches the world");
        utPadInReset(&s);
        g.editorClaim = 1;
        o = utPadInput(&s, ++gen, 0, ev(kUtEvtLeftDown, hx, hy), W, g);
        check(o.rewrite && o.why == kUtPadWhyEditor && s.press == kUtPressEditor && s.adopted == 0,
              "claimed: the DOWN outside the window is consumed, an editor press starts");
        g.editorClaim = 0;
        o = utPadInput(&s, ++gen, 0, ev(kUtEvtMove, hx + 40.0f, hy), W, g);
        UtPadInOut o2 = utPadInput(&s, ++gen, 0, ev(kUtEvtMove, 1200.0f, 700.0f), W, g);
        check(o.clear && !o.rewrite && o2.clear && !o2.rewrite,
              "  its drag, outside the window and inside it: the held bytes cleared");
        o = utPadInput(&s, ++gen, 0, ev(kUtEvtLeftUp, 1200.0f, 700.0f), W, g);
        check(o.rewrite && o.ended == 1 && s.press == kUtPressNone,
              "  its UP inside the window: consumed (the caravan never saw the DOWN), the press ends");
        g.editorClaim = 1;
        o = utPadInput(&s, ++gen, 0, ev(kUtEvtLeftDown, 1200.0f, 700.0f), W, g);
        check(o.rewrite && s.press == kUtPressEditor, "a claimed DOWN inside the window: consumed too");
        g.editorClaim = 0;
        o = utPadInput(&s, ++gen, 0, ev(kUtEvtLeftUp, 1210.0f, 690.0f), W, g);
        check(o.rewrite && o.ended == 1, "  and its UP");
        o = utPadInput(&s, ++gen, 0, ev(kUtEvtLeftDown, 1200.0f, 700.0f), W, g);
        check(untouched(o) && o.why == kUtPadWhyWindow && s.press == kUtPressNone,
              "a DOWN inside the window the editor does not claim: the window's own, untouched");
        g.editorClaim = 1;
        o = utPadInput(&s, ++gen, 0, ev(kUtEvtRightDown, hx, hy), W, g);
        g.editorClaim = 0;
        UtPadInOut ru = utPadInput(&s, ++gen, 0, ev(kUtEvtRightUp, hx, hy), W, g);
        check(o.rewrite && ru.rewrite && ru.ended == 1 && s.press == kUtPressNone,
              "a claimed RIGHT DOWN: consumed, its RIGHT UP too");
        g.editorClaim = 1;
        g.proven = 0;
        g.cursorItem = 1;
        o = utPadInput(&s, ++gen, 0, ev(kUtEvtLeftDown, hx, hy), W, g);
        check(o.rewrite && s.press == kUtPressEditor,
              "before the event space is proven, an item on the cursor: a claimed DOWN is still consumed");
        utPadInReset(&s);
        g = gates();
        g.editorOpen = 1;
        g.windowFresh = 1;
        g.fresh = 0;
        g.editorDrag = 1;   // the window's message came first: the editor already drags
        g.editorClaim = 1;
        o = utPadInput(&s, ++gen, 0, ev(kUtEvtLeftDown, hx, hy), W, g);
        g.editorClaim = 0;
        UtPadInOut up = utPadInput(&s, ++gen, 0, ev(kUtEvtLeftUp, hx + 60.0f, hy), W, g);
        check(o.rewrite && s.adopted == 0 && up.rewrite && !up.clear && up.ended == 1,
              "the editor already mid-drag: the claimed DOWN is not adopted - consumed, its UP consumed");
        unsigned char rec[kUtMouseEventSize];
        record(rec, kUtEvtLeftDown, hx, hy, 1, 0);
        utPadInReset(&s);
        g.editorDrag = 0;
        g.editorClaim = 1;
        utPadApply(rec, utPadInput(&s, ++gen, 0, utPadReadEvent(rec), W, g));
        check(typeOf(rec) == kUtEvtMove && rec[kUtMouseEvtHeldOff] == 0,
              "  the record: the DOWN becomes a MOVE, its held byte cleared");
    }

    printf("\n15. the EDIT button (edit_button=1): a pad press like every other\n");
    {
        UtPadMsgState s;
        utPadMsgReset(&s);
        const UtPadMsgFacts onEdit{kUtPadEdit, 0, 0, 0};
        UtPadMsgOut o = utPadMsgDecide(&s, kUtMsgLeftDown, onEdit);
        check(o.swallow == 1 && o.claim == 1 && o.act < 0 && s.down == kUtPadEdit,
              "a LEFT DOWN on EDIT in the window: swallowed, the engine's read claimed, the button held");
        o = utPadMsgDecide(&s, kUtMsgLeftUp, onEdit);
        check(o.swallow == 1 && o.claim == 1 && o.act == kUtPadEdit && s.down < 0,
              "its LEFT UP on EDIT: swallowed and claimed, EDIT runs (the editor opens)");
        const UtPadMsgFacts real{kUtPadEdit, 0, 0, 1};
        o = utPadMsgDecide(&s, kUtMsgLeftDown, real);
        const UtPadMsgOut u = utPadMsgDecide(&s, kUtMsgLeftUp, real);
        check(o.swallow == 1 && o.claim == 1 && u.swallow == 1 && u.claim == 1 && u.act == kUtPadEdit,
              "on the game's own Transfer page the same: both messages the pad's, EDIT runs");
        const UtPadMsgFacts held{kUtPadEdit, 1, 1, 0};
        o = utPadMsgDecide(&s, kUtMsgLeftDown, held);
        const UtPadMsgOut hu = utPadMsgDecide(&s, kUtMsgLeftUp, held);
        check(o.swallow == 1 && o.act < 0 && hu.swallow == 1 && hu.act < 0,
              "a screen EDIT with an item on the cursor: kept, nothing runs (the item stays on the cursor)");
        UtPadScreenRects r = makeRects(T);
        r.buttons[r.buttonCount++] = UtPadScreenButton{UtPadScreenRect{1700.0f, 70.0f, 34.0f, 24.0f}, kUtPadEdit};
        check(utPadScreenButtonAt(r, 1710.0f, 80.0f) == kUtPadEdit && kUtPadMax <= kUtPadScreenMaxButtons,
              "a screen pad publishes EDIT under its own index (every role fits the 128 published buttons)");
        UtPadInState st;
        utPadInReset(&st);
        const UtPadInOut io = utPadInput(&st, 1, 0, ev(kUtEvtLeftDown, 1710.0f, 80.0f), r, gates());
        check(io.rewrite == 1 && io.act == kUtPadActButton && io.button == kUtPadEdit,
              "the engine's own read of a DOWN on the screen EDIT is consumed (rewritten): it never reaches the world");
    }

    printf("\n%d checks, %d failed - %s\n", g_checks, g_fails,
           g_fails ? "PAD INPUT TESTS FAILED" : "PAD INPUT TESTS PASSED");
    return g_fails ? 1 : 0;
}
