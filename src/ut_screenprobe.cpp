// The screen input: a detour on the front input device's GetMouseEvent. The original runs first
// with the caller's arguments and its return value is handed back; then (1) the screen input log
// writes one DEBUG line per mouse event (up to 20 per world): what the engine's record says, where
// the cursor really is and whether an item is on the cursor; and (2) the press machine of
// ut_padinput.h decides the event against the screen parts the Present detour published: a click
// on a screen part (and every event of its press) is consumed in the record the original filled -
// the type rewritten to a MOVE and the held-button bytes cleared - so the world, the HUD and every
// other window never see it, and an item on the cursor is never dropped through it. The action of
// a pad button runs from the game window's procedure (ut_panel.cpp panelInput), which carries the
// same click as a WM_LBUTTON message; the detour only consumes. A generation counter on
// Engine::ProcessUserInput (one per pump) keys each event, so an event the engine hands out twice
// is decided once and said once.

#include "ut_screenprobe.h"

#include <windows.h>

#include <stdio.h>
#include <string.h>

#include "MinHook.h"
#include "hooks.h"
#include "tq_runtime.h"
#include "ut_bindings.h"
#include "ut_inputrec.h"
#include "ut_log.h"
#include "ut_padinput.h"
#include "ut_panel.h"
#include "ut_plate.h"

namespace ut {
namespace {

typedef void*(__thiscall* PfnDevGetMouseEvent)(void* dev, void* out, int idx);

const LONG kScrProbeCap = 20;

PfnDevGetMouseEvent o_DevGetMouseEvent = nullptr;
PfnEngine_Void o_ProcessUserInput = nullptr;
void* g_scrEvtTarget = nullptr;      // GetMouseEvent, resolved AND checked by its bytes
volatile LONG g_scrResolved = 0;     // 0 pending, 1 confirmed, -1 refused
volatile LONG g_scrHookOn = 0;
volatile LONG g_scrOff = 0;          // the SESSION latch: a fault is never retried
volatile LONG64 g_scrGen = 0;        // one per Engine::ProcessUserInput
volatile LONG g_scrProbes = 0;       // per world
volatile LONG g_scrMoveSaid = 0;     // per world: the first MOVE has its line
volatile LONG g_scrRepeatSaid = 0;   // per world
volatile LONG g_scrFirstCall = 0;    // the SESSION latch: the first event's thread, said once
// The pump's thread only: the event indices already read in generation g_scrSeenGen.
LONG64 g_scrSeenGen = -1;
unsigned long long g_scrSeenMask = 0;
// The press machine (ut_padinput.h): the pump's thread only.
const LONG kScrProofCap = 20;        // MOVEs tested against the cursor per world (GD's cap)
volatile LONG g_scrProofs = 0;       // per world
volatile LONG g_scrProven = 0;       // per world: the event space agreed with the cursor
volatile LONG g_scrUnproven = 0;     // per world: the proof spent unproven - the parts stay in the window
volatile LONG g_scrReset = 0;        // a world up asks the pump's thread to drop its press
volatile LONG g_scrLogged = 0;       // per world: the once-per-world lines said (a bit each)
UtPadInState g_scrState;
UtPadScreenRects g_scrSnap;          // the pump's copy of the publication (off the stack)
// The published rectangles, behind a sequence lock (odd = being written).
volatile LONG g_scrSeq = 0;
UtPadScreenRects g_scrRects;

const char* mhName(MH_STATUS s) {
    const char* t = MH_StatusToString(s);
    return t ? t : "?";
}

void scrOff(const char* why) {
    if (InterlockedExchange(&g_scrOff, 1)) return;
    logE("buttons: screen input OFF for this session - %s. The detours stay installed and pass "
         "every event through untouched.", why ? why : "?");
}

// A pure read of the record the original filled, inside the guarded frame.
bool scrReadRecord(const void* out, unsigned char* rec) {
    bool ok = true;
    utGuardEnter();
    __try {
        memcpy(rec, out, kUtMouseEventSize);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    utGuardLeave();
    return ok;
}

// GD's replay rule: a repeat of (generation, index) is one event, said once, never two lines.
bool scrRepeat(LONG64 gen, int idx) {
    if (gen != g_scrSeenGen) {
        g_scrSeenGen = gen;
        g_scrSeenMask = 0;
    }
    if (idx < 0 || idx >= 64) return false;
    const unsigned long long bit = 1ull << idx;
    const bool seen = (g_scrSeenMask & bit) != 0;
    g_scrSeenMask |= bit;
    return seen;
}

void scrProbe(const void* out, int idx) {
    if (InterlockedCompareExchange(&g_scrProbes, 0, 0) >= kScrProbeCap) return;
    unsigned char rec[kUtMouseEventSize];
    if (!scrReadRecord(out, rec)) {
        scrOff("the record GetMouseEvent filled could not be read");
        return;
    }
    const LONG64 gen = InterlockedCompareExchange64(&g_scrGen, 0, 0);
    if (scrRepeat(gen, idx)) {
        if (!InterlockedExchange(&g_scrRepeatSaid, 1))
            logD("buttons: screen input gen=%llu idx=%d was read again in the same pump - one "
                 "event, logged once", (unsigned long long)gen, idx);
        return;
    }
    int type = 0, tag0 = 0, tag1 = 0;
    float x = 0.0f, y = 0.0f;
    memcpy(&tag0, rec + kUtMouseEvtTag0Off, 4);
    memcpy(&tag1, rec + kUtMouseEvtTag1Off, 4);
    memcpy(&type, rec + kUtMouseEvtTypeOff, 4);
    memcpy(&x, rec + kUtMouseEvtXOff, 4);
    memcpy(&y, rec + kUtMouseEvtYOff, 4);
    const unsigned held0 = rec[kUtMouseEvtHeldOff], held1 = rec[kUtMouseEvtHeldOff + 1];
    if (type == kUtEvtMove && InterlockedCompareExchange(&g_scrMoveSaid, 0, 0)) return;
    // The event-space test: the event's point against GetCursorPos + ScreenToClient of the game
    // window, in client px. No window or no cursor = no line.
    POINT pt = {0, 0};
    HWND h = hookGameWindow();
    if (!h) h = GetActiveWindow();
    if (!h || !GetCursorPos(&pt) || !ScreenToClient(h, &pt)) return;
    const LONG n = InterlockedIncrement(&g_scrProbes);
    if (n > kScrProbeCap) return;
    if (type == kUtEvtMove) InterlockedExchange(&g_scrMoveSaid, 1);
    const bool itemOnCursor = hookCursorHolds();
    const long cx = pt.x, cy = pt.y;
    const float dx = (float)pt.x - x;
    const float dy = (float)pt.y - y;
    logD("buttons: screen input %ld/20 gen=%llu idx=%d type=0x%X evt=(%.1f,%.1f) cursor=(%ld,%ld) "
         "delta=(%.1f,%.1f) held=%02X%02X tag=%d/%d item=%s", n, (unsigned long long)gen, idx,
         (unsigned)type, x, y, cx, cy, dx, dy, held0, held1, tag0, tag1,
         itemOnCursor ? "yes" : "no");
    if (n == kScrProbeCap)
        logD("buttons: screen input %ld/20 spent (last delta=(%.1f,%.1f) px) - no more lines "
             "until the next world", n, dx, dy);
}

void scrLogOnce(LONG bit, const char* text) {
    if (InterlockedOr(&g_scrLogged, bit) & bit) return;
    logI("buttons: screen input - %s", text);
}

bool scrSnapshot(UtPadScreenRects* out) {
    for (int i = 0; i < 4; ++i) {
        const LONG a = InterlockedCompareExchange(&g_scrSeq, 0, 0);
        if (a & 1) continue;
        *out = g_scrRects;
        if (InterlockedCompareExchange(&g_scrSeq, 0, 0) == a) return true;
    }
    return false;
}

// The event-space proof (GD's rule): a MOVE's point against GetCursorPos + ScreenToClient of the
// game window, within 2 px on both axes. Up to kScrProofCap MOVEs per world, only while a screen
// part is shown.
void scrProve(float ex, float ey) {
    if (InterlockedCompareExchange(&g_scrProven, 0, 0)) return;
    if (InterlockedCompareExchange(&g_scrProofs, 0, 0) >= kScrProofCap) return;
    POINT pt = {0, 0};
    HWND h = hookGameWindow();
    if (!h) h = GetActiveWindow();
    if (!h || !GetCursorPos(&pt) || !ScreenToClient(h, &pt)) return;
    const LONG n = InterlockedIncrement(&g_scrProofs);
    const float dx = (float)pt.x - ex;
    const float dy = (float)pt.y - ey;
    if (dx > -2.0f && dx < 2.0f && dy > -2.0f && dy < 2.0f) {
        if (!InterlockedExchange(&g_scrProven, 1))
            logI("buttons: screen input - consumption ENABLED - the event space is proven (MOVE "
                 "%ld, delta=(%.1f,%.1f) px)", n, dx, dy);
    } else if (n == kScrProofCap) {
        InterlockedExchange(&g_scrUnproven, 1);
        logW("buttons: screen input - consumption NOT PROVEN after %ld MOVEs (last delta=(%.1f,"
             "%.1f) px) - pad_screen / label_screen are not used this world: the pad and the label "
             "stay in the caravan window", n, dx, dy);
    }
}

// The press machine over the record the original filled. Reads: the record (guarded), the
// mod's own publication and state, the polled keys. The one write: the type and the two held
// bytes of the caller's own record, which the original has just filled and the guarded read has
// just read back.
void scrConsume(void* out, int idx) {
    if (InterlockedExchange(&g_scrReset, 0)) utPadInReset(&g_scrState);
    const bool pressed = g_scrState.press != kUtPressNone;
    // Never act while the Transfer page is not shown: a pad painted a frame ago is not trusted
    // once the caravan has gone.
    const bool shown = plateTransferVisible();
    // THE ADOPTED DRAG: the layout editor carries a drag that began in the window.
    const bool editorDrag = !pressed && panelLayoutEditDragging();
    // a press the layout editor has just taken: its engine copy may come after the editor closed
    const bool editorRecent = !pressed && shown && panelLayoutEditPressRecent();
    if (!pressed && !editorDrag && !editorRecent) {
        // The cheap exit: nothing fresh published = the original's record, untouched.
        if (!shown || !g_scrRects.shown) return;
        const unsigned long long t = g_scrRects.tick;
        const unsigned long long now = hookFrameCount();
        if (t == 0 || now < t || now - t > kUtPadScreenStaleTicks) return;
    }
    unsigned char rec[kUtMouseEventSize];
    if (!scrReadRecord(out, rec)) {
        scrOff("the record GetMouseEvent filled could not be read");
        return;
    }
    const bool have = scrSnapshot(&g_scrSnap);
    if (!have) g_scrSnap.shown = 0;
    const UtPadInEvent e = utPadReadEvent(rec);
    const unsigned long long now = hookFrameCount();
    UtPadInGates g;
    memset(&g, 0, sizeof(g));
    g.fresh = shown && have && utPadScreenRectsFresh(g_scrSnap, now) ? 1 : 0;
    if (g.fresh && e.type == kUtEvtMove) scrProve(e.x, e.y);
    g.proven = InterlockedCompareExchange(&g_scrProven, 0, 0) ? 1 : 0;
    // Before the proof: a DOWN whose cursor (GetCursorPos in client px) lies on a published
    // screen part is decided at the cursor's point, so a click there never reaches the world.
    UtPadInEvent ev = e;
    if (g.fresh && !g.proven && (e.type == kUtEvtLeftDown || e.type == kUtEvtRightDown)) {
        POINT pt = {0, 0};
        HWND h = hookGameWindow();
        if (!h) h = GetActiveWindow();
        if (h && GetCursorPos(&pt) && ScreenToClient(h, &pt) &&
            utPadScreenInAny(g_scrSnap, (float)pt.x, (float)pt.y)) {
            ev.x = (float)pt.x;
            ev.y = (float)pt.y;
            g.proven = 1;
            scrLogOnce(64, "a DOWN before the event space was proven lay on a screen part at the "
                           "cursor's point - decided there");
        }
    }
    g.editorOpen = panelLayoutEditOpen() ? 1 : 0;
    bool cursorItem = false;
    if (g.fresh && (e.type == kUtEvtLeftDown || e.type == kUtEvtRightDown))
        cursorItem = hookCursorHolds();
    g.cursorItem = cursorItem ? 1 : 0;
    g.ctrl = e.type == kUtEvtRightDown && (GetAsyncKeyState(VK_CONTROL) & 0x8000) ? 1 : 0;
    g.editorDrag = editorDrag ? 1 : 0;
    g.windowFresh = shown && have && utPadScreenWindowFresh(g_scrSnap, now) ? 1 : 0;
    if (shown && (e.type == kUtEvtLeftDown || e.type == kUtEvtRightDown)) {
        // the layout editor's press, in the caravan window or outside it: at the event's point
        // once the event space is proven, else at the cursor's (client px either way)
        float px = ev.x, py = ev.y;
        if (!g.proven) {
            POINT pt = {0, 0};
            HWND h = hookGameWindow();
            if (!h) h = GetActiveWindow();
            if (h && GetCursorPos(&pt) && ScreenToClient(h, &pt)) {
                px = (float)pt.x;
                py = (float)pt.y;
            }
        }
        g.editorClaim = panelLayoutEditClaims(px, py) ? 1 : 0;
    }
    if (pressed || editorDrag) {
        // GetAsyncKeyState reads the PHYSICAL buttons: with swapped buttons the logical left is
        // the physical right.
        const bool sw = GetSystemMetrics(SM_SWAPBUTTON) != 0;
        g.leftUp = (GetAsyncKeyState(sw ? VK_RBUTTON : VK_LBUTTON) & 0x8000) ? 0 : 1;
        g.rightUp = (GetAsyncKeyState(sw ? VK_LBUTTON : VK_RBUTTON) & 0x8000) ? 0 : 1;
    }
    const UtPadInOut o = utPadInput(
        &g_scrState, (unsigned long long)InterlockedCompareExchange64(&g_scrGen, 0, 0), idx, ev,
        g_scrSnap, g);
    if (o.rewrite || o.clear) {
        // The one write, in its own guarded frame: a fault turns the screen input off.
        bool wrote = true;
        utGuardEnter();
        __try {
            utPadApply((unsigned char*)out, o);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            wrote = false;
        }
        utGuardLeave();
        if (!wrote) {
            scrOff("the record GetMouseEvent filled could not be written");
            return;
        }
    }
    if (o.repeat) return;
    if (o.why == kUtPadWhyCursorItem)
        scrLogOnce(4, "a click with an item on the cursor was consumed - no action, the item "
                      "stays on the cursor");
    if (o.why == kUtPadWhyGround)
        scrLogOnce(8, "a click on the screen pad's ground or the label was consumed - no action");
    if (o.why == kUtPadWhyButton)
        scrLogOnce(32, "a click on a screen pad button was consumed - its action runs from the "
                       "window's own mouse message");
    if (o.why == kUtPadWhyEditor && (e.type == kUtEvtLeftDown || e.type == kUtEvtRightDown))
        scrLogOnce(128, "a press of the layout editor was consumed (in the caravan window or outside "
                        "it) - the editor takes it from the window's own mouse message, its drag and "
                        "its release reach nothing else");
    if (o.ended == 2)
        scrLogOnce(16, "a press ended on the polled release (its UP event never arrived)");
}

// The original ALWAYS runs first with the caller's arguments; then the log reads the record and
// the press machine decides it.
void* __fastcall hk_DevGetMouseEvent(void* dev, void* /*edx*/, void* out, int idx) {
    void* r = o_DevGetMouseEvent(dev, out, idx);
    if (out && !InterlockedCompareExchange(&g_scrOff, 0, 0)) {
        if (!InterlockedExchange(&g_scrFirstCall, 1))
            logD("buttons: screen input - the first event through the GetMouseEvent detour, "
                 "thread t%lu", GetCurrentThreadId());
        if (logWants(UT_LOG_DEBUG)) scrProbe(out, idx);
        scrConsume(out, idx);
    }
    return r;
}

// One generation per pump: the events this pump hands out carry the new number.
void __fastcall hk_ProcessUserInput(TqEngine* self, void* /*edx*/) {
    InterlockedIncrement64(&g_scrGen);
    o_ProcessUserInput(self);
}

bool inEngineCode(const void* p) {
    MEMORY_BASIC_INFORMATION mbi;
    if (!p || !g_tq.engineDll || !VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    const DWORD exec = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                       PAGE_EXECUTE_WRITECOPY;
    return mbi.AllocationBase == (void*)g_tq.engineDll && (mbi.Protect & exec) != 0;
}

// A refused binding is not a fault: the late report says it once (INFO, advisory).
void scrRefuse(const char* why) {
    InterlockedExchange(&g_scrResolved, -1);
    InterlockedExchange(&g_scrOff, 1);
    bindingsNote("input.mouseEventSlot", 0, false, why);
    bindingsLateReport();
}

// GAME THREAD. Reads only: one exported getter, two vtable slots and the code bytes the rows name.
void scrResolve() {
    if (InterlockedCompareExchange(&g_scrResolved, 0, 0) != 0) return;
    if (!bindingsRowOk("input.deviceField")) {
        scrRefuse("Engine::GetInputDevice's field was not confirmed (input.deviceField)");
        return;
    }
    if (!g_tq.EngineGetInputDevice || !g_tq.EngineProcessUserInput || !g_tq.MouseEventCtor ||
        !g_tq.MouseEventAssign || !g_tq.MouseEventConvert || !g_tq.InputDeviceCreate) {
        scrRefuse("an input export (GetInputDevice, ProcessUserInput, MouseEvent, "
                  "InputDevice::Create) is not exported by its name");
        return;
    }
    unsigned char asg[0x40], conv[0x28], create[0x58];
    if (!safeRead((const void*)g_tq.MouseEventAssign, asg, sizeof(asg)) ||
        !safeRead((const void*)g_tq.MouseEventConvert, conv, sizeof(conv)) ||
        !utMouseEventRecordOk(asg, sizeof(asg), conv, sizeof(conv))) {
        char why[160];
        _snprintf_s(why, sizeof(why), _TRUNCATE,
                    "MouseEvent::operator= copies 0x%X bytes and ConvertToButtonEvent reads the "
                    "type at %d (want 0x%X / 0x%X)",
                    utMouseEventSizeFromAssign(asg, sizeof(asg)),
                    utMouseEventTypeOffFromConvert(conv, sizeof(conv)), kUtMouseEventSize,
                    kUtMouseEvtTypeOff);
        bindingsNote("input.mouseEventRecord", 0, false, why);
        scrRefuse("the MouseEvent record is not the 0x28-byte shape (input.mouseEventRecord)");
        return;
    }
    TqEngine* eng = engine();
    if (!eng) return;   // no engine yet: the next world tries again
    void* dev = g_tq.EngineGetInputDevice(eng);
    if (!dev) return;   // no device yet: the next world tries again
    void* vt = nullptr;
    void* slots[2] = {nullptr, nullptr};
    if (!safeRead(dev, &vt, sizeof(vt)) ||
        !safeRead((const unsigned char*)vt + kUtDevSlotMouseCount, slots, sizeof(slots))) {
        scrRefuse("Engine::GetInputDevice()'s vtable is unreadable");
        return;
    }
    const unsigned long builds =
        safeRead((const void*)g_tq.InputDeviceCreate, create, sizeof(create))
            ? utCreateFrontVtable(create, sizeof(create))
            : 0;
    if (builds == 0 || builds != (unsigned long)(uintptr_t)vt) {
        scrRefuse("the live device's vtable is not the one InputDevice::Create stores");
        return;
    }
    unsigned char evt[kUtFrontEvtSpan], cnt[kUtFrontCountSpan];
    const unsigned long evtAddr = (unsigned long)(uintptr_t)slots[1];
    if (!inEngineCode(slots[1]) || !inEngineCode(slots[0]) ||
        !safeRead(slots[1], evt, sizeof(evt)) || !safeRead(slots[0], cnt, sizeof(cnt)) ||
        !utFrontMouseEventOk(evt, sizeof(evt)) || !utFrontMouseCountOk(cnt, sizeof(cnt)) ||
        utFrontMouseEventCtor(evt, sizeof(evt), evtAddr) !=
            (unsigned long)(uintptr_t)g_tq.MouseEventCtor) {
        scrRefuse("the live device's slot +0x1C / +0x18 bytes are not the front's GetMouseEvent / "
                  "GetNumMouseEvents");
        return;
    }
    g_scrEvtTarget = slots[1];
    InterlockedExchange(&g_scrResolved, 1);
    const unsigned long long rva = (unsigned long long)(evtAddr - (unsigned long)(uintptr_t)g_tq.engineDll);
    char why[160];
    _snprintf_s(why, sizeof(why), _TRUNCATE,
                "slot +0x1C = Engine.dll rva 0x%llX, the head and the forward match, the call at "
                "+0x4A is the MouseEvent constructor; vtable %p", rva, vt);
    bindingsNote("input.mouseEventSlot", rva, true, why);
    bindingsLateReport();
    logD("buttons: screen input - GetMouseEvent %p (Engine.dll rva 0x%llX) is slot +0x1C of "
         "Engine::GetInputDevice() %p (vtable %p); the record is 0x%X bytes",
         g_scrEvtTarget, rva, dev, vt, kUtMouseEventSize);
}

// GAME THREAD. Both detours or none, at every log level (the screen parts' clicks need them; the
// log's lines are written at debug only).
void scrInstall() {
    if (InterlockedCompareExchange(&g_scrResolved, 0, 0) != 1) return;
    if (InterlockedCompareExchange(&g_scrHookOn, 0, 0)) return;
    if (InterlockedCompareExchange(&g_scrOff, 0, 0)) return;
    void* pump = (void*)g_tq.EngineProcessUserInput;
    MH_STATUS s = MH_CreateHook(pump, (void*)&hk_ProcessUserInput, (void**)&o_ProcessUserInput);
    if (s == MH_ERROR_ALREADY_CREATED) s = o_ProcessUserInput ? MH_OK : MH_ERROR_NOT_CREATED;
    if (s != MH_OK) {
        logE("  hook %-32s FAILED at MH_CreateHook: %s (%d)", "Engine::ProcessUserInput",
             mhName(s), (int)s);
        scrOff("the Engine::ProcessUserInput detour could not be created");
        return;
    }
    s = MH_CreateHook(g_scrEvtTarget, (void*)&hk_DevGetMouseEvent, (void**)&o_DevGetMouseEvent);
    if (s == MH_ERROR_ALREADY_CREATED) s = o_DevGetMouseEvent ? MH_OK : MH_ERROR_NOT_CREATED;
    if (s != MH_OK) {
        logE("  hook %-32s FAILED at MH_CreateHook: %s (%d)", "InputDevice::GetMouseEvent",
             mhName(s), (int)s);
        scrOff("the GetMouseEvent detour could not be created");
        return;
    }
    // The pump first: a generation counter that runs before the first event is harmless.
    MH_STATUS e = MH_EnableHook(pump);
    if (e == MH_ERROR_ENABLED) e = MH_OK;
    if (e != MH_OK) {
        logE("  hook %-32s FAILED at MH_EnableHook: %s (%d)", "Engine::ProcessUserInput",
             mhName(e), (int)e);
        scrOff("the Engine::ProcessUserInput detour could not be enabled");
        return;
    }
    e = MH_EnableHook(g_scrEvtTarget);
    if (e == MH_ERROR_ENABLED) e = MH_OK;
    if (e != MH_OK) {
        MH_DisableHook(pump);
        logE("  hook %-32s FAILED at MH_EnableHook: %s (%d)", "InputDevice::GetMouseEvent",
             mhName(e), (int)e);
        scrOff("the GetMouseEvent detour could not be enabled");
        return;
    }
    InterlockedExchange(&g_scrHookOn, 1);
    logI("  hook %-32s installed at %p (trampoline %p), Engine::ProcessUserInput at %p (trampoline "
         "%p) on t%lu - the screen parts' input and the screen input log",
         "InputDevice::GetMouseEvent", g_scrEvtTarget, (void*)o_DevGetMouseEvent, pump,
         (void*)o_ProcessUserInput, GetCurrentThreadId());
}

}  // namespace

void screenProbeOnWorld() {
    InterlockedExchange(&g_scrProbes, 0);
    InterlockedExchange(&g_scrMoveSaid, 0);
    InterlockedExchange(&g_scrRepeatSaid, 0);
    InterlockedExchange(&g_scrProofs, 0);
    InterlockedExchange(&g_scrProven, 0);
    InterlockedExchange(&g_scrUnproven, 0);
    InterlockedExchange(&g_scrLogged, 0);
    InterlockedExchange(&g_scrReset, 1);
    screenInputPublish(nullptr);
    scrResolve();
    scrInstall();
}

// The detours installed, not off, and the event space not refused in this world (the proof spent
// unproven): otherwise the screen parts stay in the caravan window.
bool screenInputReady() {
    return InterlockedCompareExchange(&g_scrHookOn, 0, 0) != 0 &&
           InterlockedCompareExchange(&g_scrOff, 0, 0) == 0 &&
           InterlockedCompareExchange(&g_scrUnproven, 0, 0) == 0;
}

void screenInputPublish(const UtPadScreenRects* r) {
    InterlockedIncrement(&g_scrSeq);   // odd: being written
    if (r && r->shown) {
        g_scrRects = *r;
        if (g_scrRects.buttonCount < 0) g_scrRects.buttonCount = 0;
        if (g_scrRects.buttonCount > kUtPadScreenMaxButtons)
            g_scrRects.buttonCount = kUtPadScreenMaxButtons;
        const unsigned long long f = hookFrameCount();
        g_scrRects.tick = f ? f : 1;
    } else {
        g_scrRects.shown = 0;
    }
    InterlockedIncrement(&g_scrSeq);   // even: whole again
}

}  // namespace ut
