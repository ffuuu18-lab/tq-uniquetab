// ut_padinput.h - the SCREEN pad's input: the press rules over the engine's mouse-event stream.
//
// WHY THIS EXISTS. A pad (or label) drawn OUTSIDE the caravan window lies over the world, the HUD
// or another window: a click on it must reach none of them. Every mouse event of the game passes
// ONE function first - the front input device's GetMouseEvent (Engine.dll, slot +0x1C of the
// object Engine::GetInputDevice returns; the only copy-out the engine's Engine::ProcessUserInput
// pump makes, ut_inputrec.h) - before the UI, the HUD, the windows or the WORLD see it.
// ut_screenprobe.cpp detours it, and this header decides, per event, what the detour does to the
// 0x28-byte record it hands back:
//
//   untouched   the default: outside the published screen rectangles, before anything was
//               published, when the rectangles are stale, before the event space is proven;
//   REWRITTEN   type -> MOVE (0) and the held-button bytes [+0x18..+0x19] cleared: the click is
//               consumed for every consumer at once (world, HUD, other windows) while hover and
//               tooltips keep working - a MOVE is what arrives every tick anyway;
//   CLEARED     the held-button bytes [+0x18..+0x19] cleared, the type kept: every event passed on
//               DURING a press, so a held press on the pad never starts a "hold to move" in the
//               world, whatever the world reads the held bytes of.
//
// THE RULES (tools\test_padinput.cpp proves each one):
//   * LEFT DOWN inside a shown button -> rewritten, a press starts (kUtPadActButton names the
//     button; on Titan Quest the button's action runs from the window procedure, which carries the
//     same click as a WM_LBUTTON message - ut_panel.cpp panelInput);
//   * LEFT DOWN on the pad's ground or the label (no button) -> rewritten, no action, a press;
//   * LEFT DOWN with an item on the cursor (or the cursor unreadable) -> rewritten, NO action, a
//     press. THE ITEM-ON-CURSOR RULE: the safe one. A passed click outside the caravan would drop
//     the item on the ground, so the click is eaten and the item stays on the cursor;
//   * while a press lasts every event passed on has its button bytes cleared, whatever its type
//     and wherever it is (the rectangles going stale mid-press do not end it);
//   * the press's own UP (LEFT UP, or RIGHT UP for a right press) is rewritten and ends it;
//   * THE POLLED RELEASE ends a press whose UP never came: once the polled button has been up for
//     two whole pump generations (an UP queued in the generation the poll first saw up, or in the
//     next, still ends it normally);
//   * a second LEFT DOWN while a left press is still open (its UP was lost) ends the old press and
//     is judged on its own;
//   * Ctrl+RIGHT DOWN inside the pad or the label -> the layout editor's entry, rewritten, and its
//     RIGHT UP rewritten too; a plain RIGHT DOWN inside them is consumed too, with no action (the
//     player's right-click skill must not fire under the pad); the wheel passes;
//   * while the layout editor is OPEN: LEFT DOWN inside the rectangles is the editor's (not a
//     button's) and starts an editor press, during which every MOVE and the LEFT UP are the
//     editor's too; a MOVE or a stray LEFT UP inside the rectangles with no press is untouched;
//     with an item on the cursor the LEFT DOWN is still the editor's (consumed: the item stays;
//     the editor then acts only on CANCEL);
//   * THE ADOPTED DRAG: the editor is mid-drag and no press is open here - a drag that STARTED IN
//     THE WINDOW and is carried across the window's edge by the polled cursor. It becomes this
//     machine's editor press at its first event, so the world never sees a held byte of it, and
//     its LEFT UP is CLEARED, not rewritten (the caravan saw that drag's DOWN, so it sees its UP);
//   * a LEFT DOWN while a RIGHT press is open (its RIGHT UP lost, the poll not yet run) ends the
//     right press and is judged on its own; so is any DOWN on which the polled release ends a press;
//   * THE EDITOR'S CLAIM: a LEFT DOWN or a RIGHT DOWN the layout editor claims (`editorClaim`: a
//     point on one of its parts, a handle - which reaches past a part's edge, past the window's
//     too - or its toolbar, or the press its window procedure has just taken there) is rewritten
//     wherever it lies, inside the caravan window or outside it, and starts an editor press (a
//     right press for a RIGHT DOWN): its MOVEs are cleared and its own UP rewritten, so neither
//     the caravan's page nor the world nor the HUD sees the DOWN, its drag or its UP. The editor
//     reads the same click from the window's own mouse message;
//   * THE WINDOW IS THE WINDOW'S: while the layout editor is open it publishes the caravan window's
//     rect (`window`) with its grounds. A point inside it is the window's own: no press STARTS
//     there and the event is untouched (the caravan's page and the window procedure take it, as
//     the in-window editor always did). An ADOPTED drag leaves its events inside the window
//     untouched too and keeps clearing everything outside. A press that began on the SCREEN runs
//     to its UP wherever it goes (the caravan never saw its DOWN);
//   * outside the rectangles, and with no press, NOTHING is touched;
//   * IDEMPOTENT per pump generation: the same (generation, index) seen twice with the same event
//     gets the same rewrite again and NO second action; a DIFFERENT event under the same key is
//     decided as new (a stuck counter can never make an old decision eat a new click).
//
// THE RECTANGLES are published by the draw that painted them (ut_screendraw.h, from the Present
// detour): client pixels - the event's own space (the screen input log's measure: the event's
// point equals GetCursorPos + ScreenToClient, delta 0) - and the frame they were drawn at. Nothing
// published, `shown == 0`, or older than kUtPadScreenStaleTicks frames = INERT.
//
// Pure: standard headers and ut_inputrec.h only, no Windows, no mod state; ut_screenprobe.cpp runs
// it from the detour and tools\test_padinput.cpp runs the same code offline.
#pragma once

#include <stddef.h>
#include <string.h>

#include "ut_inputrec.h"   // the 0x28-byte record: type +0x08, x +0x0C, y +0x10, held +0x18

namespace ut {

// The held-button bytes a decision clears: [0] left and [1] right.
const size_t kUtMouseEvtHeldCleared = 2;

// ---- the published rectangles (ut_screendraw.h fills them, the panel stamps the frame) ---------
const int kUtPadScreenMaxButtons = 128;
// A frame later than this many Present frames after the draw = stale = inert.
const unsigned long long kUtPadScreenStaleTicks = 4;

struct UtPadScreenRect {
    float x, y, w, h;   // client px; w or h <= 0 = absent
};
struct UtPadScreenButton {
    UtPadScreenRect r;
    int index;          // the pad's button (ut_padlayout.h's kUtPad* index)
};
struct UtPadScreenRects {
    int shown;                 // 1 = a screen pad and/or label was painted this frame
    unsigned long long tick;   // the frame it was painted at (hookFrameCount); 0 = never
    UtPadScreenRect pad;          // the pad's ground
    UtPadScreenRect label;        // the label's ground
    UtPadScreenRect window;       // the open layout editor only: the caravan window's client rect -
                                  // a point inside it is the window's (the screen pad's own
                                  // publication leaves it 0 x 0 = none)
    int buttonCount;           // 0..kUtPadScreenMaxButtons
    UtPadScreenButton buttons[kUtPadScreenMaxButtons];
};

inline bool utPadScreenRectValid(const UtPadScreenRect& r) { return r.w > 0.0f && r.h > 0.0f; }

// NaN coordinates compare false everywhere, so a NaN point is inside nothing.
inline bool utPadScreenIn(const UtPadScreenRect& r, float x, float y) {
    return utPadScreenRectValid(r) && x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

inline bool utPadScreenRectsFresh(const UtPadScreenRects& r, unsigned long long now) {
    if (!r.shown || r.tick == 0 || now < r.tick || now - r.tick > kUtPadScreenStaleTicks) return false;
    return utPadScreenRectValid(r.pad) || utPadScreenRectValid(r.label);
}

// The button under (x, y), or -1. Buttons count only inside the pad's ground (a button published
// outside it is a publisher's mistake and is never hit).
inline int utPadScreenButtonAt(const UtPadScreenRects& r, float x, float y) {
    if (!utPadScreenIn(r.pad, x, y)) return -1;
    const int n = r.buttonCount < 0 ? 0
                  : r.buttonCount > kUtPadScreenMaxButtons ? kUtPadScreenMaxButtons
                                                           : r.buttonCount;
    for (int i = 0; i < n; ++i) {
        if (r.buttons[i].index >= 0 && utPadScreenIn(r.buttons[i].r, x, y)) return r.buttons[i].index;
    }
    return -1;
}

inline bool utPadScreenInAny(const UtPadScreenRects& r, float x, float y) {
    return utPadScreenIn(r.pad, x, y) || utPadScreenIn(r.label, x, y);
}

// The open editor's window rect is fresh: published (shown), with a window, at most
// kUtPadScreenStaleTicks frames old. It needs no pad or label (every part in the window).
inline bool utPadScreenWindowFresh(const UtPadScreenRects& r, unsigned long long now) {
    if (!r.shown || r.tick == 0 || now < r.tick || now - r.tick > kUtPadScreenStaleTicks) return false;
    return utPadScreenRectValid(r.window);
}

// ---- the press machine -----------------------------------------------------------------------
enum UtPadPress {
    kUtPressNone = 0,
    kUtPressButton = 1,   // LEFT DOWN on a button
    kUtPressKept = 2,     // LEFT DOWN on the ground / the label / with an item: consumed, no action
    kUtPressEditor = 3,   // LEFT DOWN handed to the open layout editor: its drag
    kUtPressRight = 4,    // RIGHT DOWN (Ctrl: the editor's entry)
};

enum UtPadAct {
    kUtPadActNone = 0,
    kUtPadActButton = 1,   // the button `button` was pressed (once per press)
    kUtPadActEditor = 2,   // `editorType` at the event's point is the layout editor's
};

enum UtPadWhy {
    kUtPadWhyUntouched = 0,   // outside, or an event nothing applies to
    kUtPadWhyInert,           // nothing fresh published, or the event space not proven
    kUtPadWhyButton,          // LEFT DOWN on a button
    kUtPadWhyGround,          // LEFT DOWN on the pad's ground or the label
    kUtPadWhyCursorItem,      // a DOWN with an item on the cursor
    kUtPadWhyEditor,          // the layout editor's
    kUtPadWhyCtrlRight,       // Ctrl+RIGHT DOWN: the editor's entry
    kUtPadWhyHeld,            // an event passed on during a press: bytes cleared
    kUtPadWhyUp,              // the press's own UP
    kUtPadWhyWindow,          // inside the open editor's window rect - the window's own
    kUtPadWhyCount
};

struct UtPadInEvent {
    int type;
    float x, y;   // client px
};

// What the caller knows about the world right now. Every field is 0/1.
struct UtPadInGates {
    int fresh;        // utPadScreenRectsFresh(published, now)
    int proven;       // the event space is proven (a MOVE agreed with GetCursorPos)
    int cursorItem;   // an item is on the cursor, or the read failed (unknown = an item)
    int ctrl;         // Ctrl is down
    int editorOpen;   // the layout editor is open
    int leftUp;       // POLLED: the logical left button is up now (swapped buttons honoured)
    int rightUp;      // POLLED: the logical right button is up now
    int editorDrag;   // the layout editor is mid-drag (a drag that started in the window too)
    int windowFresh;  // utPadScreenWindowFresh(published, now) and the caravan open
    int editorClaim;  // a DOWN only: the layout editor claims its point (ut_layoutedit.h
                      // utEditClaims), inside the caravan window or outside it
};

struct UtPadInOut {
    int rewrite;      // type -> MOVE, held bytes cleared: the event is consumed
    int clear;        // held bytes cleared, type kept: an event passed on during a press
    int act;          // UtPadAct
    int button;       // the button for kUtPadActButton, else -1
    int editorType;   // the event type for kUtPadActEditor
    int why;          // UtPadWhy
    int ended;        // a press ended here: 1 = its UP, 2 = the polled release, 3 = a new DOWN
    int repeat;       // 1 = this (generation, index) was already decided: same bytes, no action
};

struct UtPadInState {
    int press;                    // UtPadPress
    int adopted;                  // 1 = the editor press is an ADOPTED window drag (its UP cleared)
    int pressButton;              // the button of a kUtPressButton press, else -1
    unsigned long long upSince;   // the generation the poll first saw the button up (+1), 0 = no
    int have;                     // a (generation, index) has been decided
    unsigned long long gen;
    int idx;
    UtPadInEvent lastEvt;         // the event it was decided on (a repeat is the same bytes)
    UtPadInOut last;              // its decision, replayed on a repeat
};

inline void utPadInReset(UtPadInState* s) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->pressButton = -1;
    s->last.button = -1;
}

// The polled release needs this many generations of "up" with no UP event before it ends a press.
const unsigned long long kUtPadPollGenerations = 2;

namespace padinput_detail {

inline UtPadInOut fresh() {
    UtPadInOut o;
    memset(&o, 0, sizeof(o));
    o.button = -1;
    return o;
}

inline bool inEditorWindow(const UtPadScreenRects& r, const UtPadInGates& g, float x, float y) {
    return g.windowFresh && g.editorOpen && utPadScreenIn(r.window, x, y);
}

// No press is open: the rules for an event that may start one.
inline void decideIdle(UtPadInState* s, const UtPadInEvent& e, const UtPadScreenRects& r,
                       const UtPadInGates& g, UtPadInOut* o) {
    if (g.editorClaim && (e.type == kUtEvtLeftDown || e.type == kUtEvtRightDown)) {
        // the editor's press, in the window or out of it: consumed, its drag and its UP too
        o->rewrite = 1;
        s->press = e.type == kUtEvtLeftDown ? kUtPressEditor : kUtPressRight;
        s->adopted = 0;
        s->pressButton = -1;
        s->upSince = 0;
        o->act = kUtPadActEditor;
        o->editorType = e.type;
        o->why = kUtPadWhyEditor;
        return;
    }
    if (inEditorWindow(r, g, e.x, e.y)) {
        // the window's own - untouched; the window procedure hands it to the editor
        o->why = kUtPadWhyWindow;
        return;
    }
    if (!g.fresh || !g.proven) {
        o->why = kUtPadWhyInert;
        return;
    }
    if (!utPadScreenInAny(r, e.x, e.y)) return;   // outside: untouched
    switch (e.type) {
        case kUtEvtLeftDown:
            o->rewrite = 1;
            if (g.editorOpen) {
                // The editor's, item or not: consumed either way (the item stays on the cursor),
                // and the editor - told the cursor holds an item - acts on CANCEL only.
                s->press = kUtPressEditor;
                o->act = kUtPadActEditor;
                o->editorType = kUtEvtLeftDown;
                o->why = kUtPadWhyEditor;
            } else if (g.cursorItem) {
                s->press = kUtPressKept;
                o->why = kUtPadWhyCursorItem;
            } else {
                const int b = utPadScreenButtonAt(r, e.x, e.y);
                if (b >= 0) {
                    s->press = kUtPressButton;
                    s->pressButton = b;
                    o->act = kUtPadActButton;
                    o->button = b;
                    o->why = kUtPadWhyButton;
                } else {
                    s->press = kUtPressKept;
                    o->why = kUtPadWhyGround;
                }
            }
            s->upSince = 0;
            return;
        case kUtEvtRightDown:
            o->rewrite = 1;
            s->press = kUtPressRight;
            s->upSince = 0;
            if (!g.ctrl) {
                // A PLAIN right click on the pad or the label: consumed with no action, so the
                // player's right-click skill never fires at a point under the pad; its RIGHT UP is
                // rewritten by the right press like the Ctrl one's.
                o->why = kUtPadWhyGround;
                return;
            }
            if (g.cursorItem) {
                o->why = kUtPadWhyCursorItem;
            } else if (g.editorOpen) {
                o->why = kUtPadWhyGround;   // already open: nothing to enter
            } else {
                o->act = kUtPadActEditor;
                o->editorType = kUtEvtRightDown;
                o->why = kUtPadWhyCtrlRight;
            }
            return;
        case kUtEvtLeftUp:
        case kUtEvtMove:
            // The editor's drag reads its own MOVE / LEFT UP; neither is consumed.
            if (g.editorOpen) {
                o->act = kUtPadActEditor;
                o->editorType = e.type;
                o->why = kUtPadWhyEditor;
            }
            return;
        default:
            return;
    }
}

}  // namespace padinput_detail

// One event. `gen` is the pump's generation (the detour's counter), `idx` the event's index inside
// it - the pump asks each index once per generation, in order.
inline UtPadInOut utPadInput(UtPadInState* s, unsigned long long gen, int idx,
                             const UtPadInEvent& e, const UtPadScreenRects& r,
                             const UtPadInGates& g) {
    using namespace padinput_detail;
    UtPadInOut o = fresh();
    if (!s) return o;
    // A repeat is the same queue slot read again: same generation, same index AND the same
    // event. A different event under the same key (a generation counter that did not advance)
    // is a new event, never a replay of an old decision.
    if (s->have && s->gen == gen && s->idx == idx && s->lastEvt.type == e.type &&
        memcmp(&s->lastEvt.x, &e.x, sizeof(e.x)) == 0 &&
        memcmp(&s->lastEvt.y, &e.y, sizeof(e.y)) == 0) {
        o = s->last;
        o.act = kUtPadActNone;
        o.button = -1;
        o.ended = 0;
        o.repeat = 1;
        return o;
    }
    s->have = 1;
    s->gen = gen;
    s->idx = idx;
    s->lastEvt = e;

    // THE ADOPTED DRAG: the editor drags (its press began in the window) and nothing is open
    // here - this event is the first of a press of ours. A LEFT DOWN is never adopted: it is a
    // new click (the drag's own UP was lost) and is judged below on its own.
    if (s->press == kUtPressNone && g.editorDrag && g.proven && e.type != kUtEvtLeftDown) {
        s->press = kUtPressEditor;
        s->adopted = 1;
        s->pressButton = -1;
        s->upSince = 0;
    }

    if (s->press != kUtPressNone) {
        const bool right = s->press == kUtPressRight;
        const int ownUp = right ? kUtEvtRightUp : kUtEvtLeftUp;
        // An ADOPTED drag inside the open editor's window is the window's - its MOVEs and its UP
        // pass untouched (the caravan saw its DOWN); a LEFT DOWN there is a new click, judged
        // below (the window's too).
        if (s->adopted && e.type != kUtEvtLeftDown && inEditorWindow(r, g, e.x, e.y)) {
            o.why = kUtPadWhyWindow;
            if (e.type == ownUp) {
                o.ended = 1;
            } else if (!g.leftUp) {
                s->upSince = 0;
            } else if (s->upSince == 0) {
                s->upSince = gen + 1;
            } else if (gen + 1 >= s->upSince + kUtPadPollGenerations) {
                o.ended = 2;   // the polled release, here too (the event itself untouched)
            }
            if (o.ended) {
                s->press = kUtPressNone;
                s->adopted = 0;
                s->pressButton = -1;
                s->upSince = 0;
            }
            s->last = o;
            return o;
        }
        if (e.type == ownUp) {
            // An adopted drag's UP is CLEARED, not rewritten: the caravan saw its DOWN.
            if (s->adopted) o.clear = 1;
            else o.rewrite = 1;
            o.why = kUtPadWhyUp;
            o.ended = 1;
            if (s->press == kUtPressEditor) {
                o.act = kUtPadActEditor;
                o.editorType = kUtEvtLeftUp;
            }
            s->press = kUtPressNone;
            s->adopted = 0;
            s->pressButton = -1;
            s->upSince = 0;
            s->last = o;
            return o;
        }
        // The polled release: up for kUtPadPollGenerations whole generations with no UP event.
        const int upNow = right ? g.rightUp : g.leftUp;
        bool endByPoll = false;
        if (!upNow) {
            s->upSince = 0;
        } else if (s->upSince == 0) {
            s->upSince = gen + 1;
        } else if (gen + 1 >= s->upSince + kUtPadPollGenerations) {
            endByPoll = true;
        }
        // A LEFT DOWN ends ANY open press: a left one lost its UP, a right one is over.
        const bool lostUp = e.type == kUtEvtLeftDown;
        const bool down = e.type == kUtEvtLeftDown || e.type == kUtEvtRightDown;
        if (endByPoll || lostUp) {
            s->press = kUtPressNone;
            s->adopted = 0;
            s->pressButton = -1;
            s->upSince = 0;
            o.ended = lostUp ? 3 : 2;
            // The last event of the press: cleared like every other one of it.
            o.clear = 1;
            o.why = kUtPadWhyHeld;
            if (down) {
                // A new DOWN is judged on its own: outside the rectangles it is the world's,
                // byte for byte (its held byte is ITS press, not the lost one) - also when the
                // poll ends the old press on it.
                const int ended = o.ended;
                o = fresh();
                o.ended = ended;
                decideIdle(s, e, r, g, &o);
            }
            s->last = o;
            return o;
        }
        o.clear = 1;
        o.why = kUtPadWhyHeld;
        if (s->press == kUtPressEditor && e.type == kUtEvtMove) {
            o.act = kUtPadActEditor;
            o.editorType = kUtEvtMove;
        }
        s->last = o;
        return o;
    }

    decideIdle(s, e, r, g, &o);
    s->last = o;
    return o;
}

// Apply a decision to the engine's 0x28-byte record in place.
inline void utPadApply(unsigned char* evt, const UtPadInOut& o) {
    if (!evt) return;
    if (o.rewrite) {
        const int move = kUtEvtMove;
        memcpy(evt + kUtMouseEvtTypeOff, &move, sizeof(move));
    }
    if (o.rewrite || o.clear) memset(evt + kUtMouseEvtHeldOff, 0, kUtMouseEvtHeldCleared);
}

inline UtPadInEvent utPadReadEvent(const unsigned char* evt) {
    UtPadInEvent e = {0, 0.0f, 0.0f};
    if (!evt) return e;
    memcpy(&e.type, evt + kUtMouseEvtTypeOff, sizeof(e.type));
    memcpy(&e.x, evt + kUtMouseEvtXOff, sizeof(e.x));
    memcpy(&e.y, evt + kUtMouseEvtYOff, sizeof(e.y));
    return e;
}

// ---- the window procedure's clicks (ut_panel.cpp panelInput) ---------------------------------
// On Titan Quest a pad button runs from the game window's mouse messages, after the layout editor
// has had them. This is that path's whole decision, per message:
//   * LEFT DOWN on a shown button -> swallowed, the button is held (drawn pressed); its LEFT UP is
//     swallowed too and runs the button when it lands on the same button (the action on the UP);
//   * a DOWN on a shown screen part with an item on the cursor -> swallowed with no action (the
//     item stays on the cursor), and so is that button's own UP: the KEPT click;
//   * LEFT DOWN on a screen part's ground (no button) -> kept, the same way;
//   * a RIGHT DOWN on a screen part's ground or on a button -> kept, no action (its RIGHT UP is
//     swallowed wherever it lands); a RIGHT UP the pad never kept passes (the engine saw its DOWN);
//   * THE GAME'S OWN TRANSFER PAGE (the view OFF): the pad and the label stay where the player put
//     them, drawn over the real cells they cover, so a point anywhere inside them is theirs: a LEFT
//     DOWN or a RIGHT DOWN on their ground (no button) is kept with no action and claimed (the
//     engine's own read of the press takes nothing from the cell under it and drops nothing on
//     it), an item on the cursor or not; a button acts as everywhere;
//   * everything else passes - a kept click never swallows a press off the pad's parts.
// THE LATCH KEEPS ITS BUTTON: a kept LEFT DOWN waits for the LEFT UP, a kept RIGHT DOWN for the
// RIGHT UP; the other button's messages are judged on their own. It ends on its own UP, on the
// next DOWN of the same button (its UP was lost: the release happened off the window, or behind
// Alt+Tab) and by the polled release (utPadMsgPoll, once a frame): the logical button up at two
// polls in a row with no UP between, swapped buttons honoured. A held button ends the same way.
// tools\test_padinput.cpp drives it.
enum UtPadMsg { kUtMsgLeftDown = 1, kUtMsgLeftUp, kUtMsgRightDown, kUtMsgRightUp };
const int kUtMsgLeft = 1;    // a latch's button, as bits
const int kUtMsgRight = 2;

struct UtPadMsgState {
    int down;     // the pad button a LEFT DOWN landed on (its LEFT UP acts), -1 = none
    int kept;     // kUtMsgLeft / kUtMsgRight: a swallowed DOWN whose own UP is swallowed too
    int upSeen;   // the bits the poll saw up once (the next poll that sees them up ends them)
};

struct UtPadMsgFacts {
    int button;       // the shown pad button under the point, -1 = none
    int onScreen;     // the point is on a shown screen part (the pad or the label off the window)
    int cursorItem;   // an item is on the cursor (read for a DOWN on a screen part only)
    int onRealPad;    // the game's own Transfer page is shown and the point is inside the pad's
                      // ground or the label drawn there (over its cells)
};

struct UtPadMsgOut {
    int swallow;   // 1 = the pad's: the window procedure eats the message
    int act;       // the button to run (a LEFT UP over the button its DOWN landed on), -1 = none
    int claim;     // 1 = the engine's own read of this press is the pad's too
    int judged;    // 1 = a DOWN that reached the pad's hit test (the search field's blur reads it)
    int ended;     // the bits of a latch or a held button this DOWN ended (their UP was lost)
};

inline void utPadMsgReset(UtPadMsgState* s) {
    if (!s) return;
    s->down = -1;
    s->kept = 0;
    s->upSeen = 0;
}

// A press is open: a held button or a kept click.
inline bool utPadMsgOpen(const UtPadMsgState& s) { return s.down >= 0 || s.kept != 0; }

inline UtPadMsgOut utPadMsgDecide(UtPadMsgState* s, int msg, const UtPadMsgFacts& f) {
    UtPadMsgOut o = {0, -1, 0, 0, 0};
    if (!s) return o;
    const bool left = msg == kUtMsgLeftDown || msg == kUtMsgLeftUp;
    const bool isDown = msg == kUtMsgLeftDown || msg == kUtMsgRightDown;
    const int bit = left ? kUtMsgLeft : kUtMsgRight;
    if (!isDown && (s->kept & bit)) {   // the kept DOWN's own UP: swallowed, the latch ends
        s->kept &= ~bit;
        s->upSeen &= ~bit;
        o.swallow = 1;
        return o;
    }
    if (isDown) {
        // the same button down again while its press is open here: that press lost its UP
        if (s->kept & bit) {
            s->kept &= ~bit;
            o.ended |= bit;
        }
        if (left && s->down >= 0) {
            s->down = -1;
            o.ended |= kUtMsgLeft;
        }
        s->upSeen &= ~bit;
        if (f.onScreen && f.cursorItem) {   // an item on the cursor: kept, no action
            s->kept |= bit;
            o.swallow = 1;
            return o;
        }
    }
    o.judged = isDown ? 1 : 0;
    switch (msg) {
        case kUtMsgLeftDown:
            if (f.button < 0) {   // the ground of a screen part or of the real page's pad: kept, no
                                  // action; elsewhere it passes
                if (f.onScreen || f.onRealPad) {
                    s->kept |= kUtMsgLeft;
                    o.swallow = 1;
                    o.claim = f.onRealPad ? 1 : 0;   // the cell under it never takes the press
                }
                return o;
            }
            s->down = f.button;
            o.swallow = 1;
            o.claim = 1;
            return o;
        case kUtMsgLeftUp: {
            const int was = s->down;
            s->down = -1;
            s->upSeen &= ~kUtMsgLeft;
            if (was < 0) return o;   // not the pad's press: the engine's own click
            o.swallow = 1;           // the pad's press: its release is the pad's too
            o.claim = 1;
            if (f.button == was) o.act = was;
            return o;
        }
        default:   // the right button: a DOWN on a button, on a screen part's ground or on the
                   // real page's pad is kept, nothing runs, and its own UP is swallowed above; an
                   // UP the pad never kept (a press the engine saw, let go over the pad) is the
                   // engine's
            if (isDown && (f.button >= 0 || f.onScreen || f.onRealPad)) {
                s->kept |= kUtMsgRight;
                o.swallow = 1;
                o.claim = f.onRealPad ? 1 : 0;
            }
            return o;
    }
}

// The polled release, once a frame: `physL` / `physR` = the PHYSICAL buttons held now
// (GetAsyncKeyState), `swapped` = the buttons are swapped (the logical left is the physical right).
// A latch or a held button whose logical button is up at two polls in a row ends: an UP queued
// when the first poll saw it up still arrives before the second and ends it normally. Returns the
// bits it ended.
inline int utPadMsgPoll(UtPadMsgState* s, int physL, int physR, int swapped) {
    if (!s) return 0;
    const int heldL = swapped ? physR : physL;
    const int heldR = swapped ? physL : physR;
    const int open = s->kept | (s->down >= 0 ? kUtMsgLeft : 0);
    int ended = 0;
    for (int bit = kUtMsgLeft; bit <= kUtMsgRight; bit <<= 1) {
        const int held = bit == kUtMsgLeft ? heldL : heldR;
        if (!(open & bit) || held) {
            s->upSeen &= ~bit;
            continue;
        }
        if (!(s->upSeen & bit)) {
            s->upSeen |= bit;
            continue;
        }
        s->kept &= ~bit;
        s->upSeen &= ~bit;
        if (bit == kUtMsgLeft) s->down = -1;
        ended |= bit;
    }
    return ended;
}

}  // namespace ut
