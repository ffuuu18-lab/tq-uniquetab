#pragma once

// The engine's mouse event record, InputDevice::MouseEvent (x86, 0x28 bytes), and the pure byte
// checks that prove its layout and the front input device's two mouse slots from the code itself.
// No Windows and no engine calls: the mod runs them over live bytes, the offline test over the
// installed Engine.dll, so both read the same verdict out of the same bytes.

#include <stddef.h>
#include <string.h>

namespace ut {

// ---- the record --------------------------------------------------------------------------------
constexpr unsigned kUtMouseEventSize = 0x28;
constexpr unsigned kUtMouseEvtTag0Off = 0x00;   // 0 from a backend; 3 in the front's synthesized MOVE
constexpr unsigned kUtMouseEvtTag1Off = 0x04;   // 0 from a backend; 1 in the front's synthesized MOVE
constexpr unsigned kUtMouseEvtTypeOff = 0x08;   // int, the kUtEvt* below
constexpr unsigned kUtMouseEvtXOff = 0x0C;      // float, client px
constexpr unsigned kUtMouseEvtYOff = 0x10;      // float, client px
constexpr unsigned kUtMouseEvtWheelOff = 0x14;  // int, the wheel total
constexpr unsigned kUtMouseEvtHeldOff = 0x18;   // u8[8]: [0] left, [1] right, [2] middle
constexpr unsigned kUtMouseEvtModOff = 0x20;    // u8[4]: caps, shift, alt, ctrl

// The event types (the producer's numbering: DOWN = button + 1, UP = button + 9).
constexpr int kUtEvtMove = 0;
constexpr int kUtEvtLeftDown = 1;
constexpr int kUtEvtRightDown = 2;
constexpr int kUtEvtLeftUp = 9;
constexpr int kUtEvtRightUp = 0xA;
constexpr int kUtEvtWheelUp = 0x11;
constexpr int kUtEvtWheelDown = 0x12;

// ---- the front input device: the object Engine::GetInputDevice returns -------------------------
constexpr unsigned kUtDevSlotMouseCount = 0x18;   // GetNumMouseEvents()
constexpr unsigned kUtDevSlotMouseEvent = 0x1C;   // GetMouseEvent(out, idx): thiscall, ret 8, returns out

// Engine::GetInputDevice is `mov eax, [ecx + d32] ; ret` (8B 81 d32 C3): the field. 0 otherwise.
inline unsigned utInputDeviceField(const unsigned char* p, size_t n) {
    if (!p || n < 7 || p[0] != 0x8B || p[1] != 0x81 || p[6] != 0xC3) return 0;
    unsigned d;
    memcpy(&d, p + 2, 4);
    return d;
}

// Engine::ProcessUserInput loads the same field at +0x43 (`mov ecx, [edi + d32]`, 8B 8F d32).
constexpr size_t kUtPumpDeviceRead = 0x43;
inline unsigned utPumpDeviceField(const unsigned char* p, size_t n) {
    if (!p || n < kUtPumpDeviceRead + 6) return 0;
    const unsigned char* q = p + kUtPumpDeviceRead;
    if (q[0] != 0x8B || q[1] != 0x8F) return 0;
    unsigned d;
    memcpy(&d, q + 2, 4);
    return d;
}

// GetMouseEvent: its head, the call to the exported MouseEvent constructor at +0x4A (the zeroed
// record it hands back when no backend is active) and the forward to the active backend's own
// slot +0x1C at +0xAB (`mov eax,[ecx] ; push esi ; call [eax+0x1C]`).
constexpr size_t kUtFrontEvtCtorCall = 0x4A;   // E8 rel32
constexpr size_t kUtFrontEvtForward = 0xAB;    // 8B 01 56 FF 50 1C
constexpr size_t kUtFrontEvtSpan = kUtFrontEvtForward + 6;   // the bytes a check reads
constexpr size_t kUtFrontCountSpan = 14;

inline bool utFrontMouseEventOk(const unsigned char* p, size_t n) {
    static const unsigned char kHead[13] = {0x83, 0xEC, 0x28, 0x56, 0x8B, 0xF1, 0x8B,
                                            0x4E, 0x04, 0x85, 0xC9, 0x75, 0x22};
    static const unsigned char kFwd[6] = {0x8B, 0x01, 0x56, 0xFF, 0x50,
                                          (unsigned char)kUtDevSlotMouseEvent};
    return p && n >= kUtFrontEvtSpan && !memcmp(p, kHead, sizeof(kHead)) &&
           p[kUtFrontEvtCtorCall] == 0xE8 && !memcmp(p + kUtFrontEvtForward, kFwd, sizeof(kFwd));
}

// Where the call at +0x4A lands, for bytes that live at `fnAddr`; 0 when it is not a call.
inline unsigned long utFrontMouseEventCtor(const unsigned char* p, size_t n, unsigned long fnAddr) {
    if (!p || n < kUtFrontEvtCtorCall + 5 || p[kUtFrontEvtCtorCall] != 0xE8) return 0;
    long rel;
    memcpy(&rel, p + kUtFrontEvtCtorCall + 1, 4);
    return (unsigned long)((long)(fnAddr + kUtFrontEvtCtorCall + 5) + rel);
}

// GetNumMouseEvents: `push esi ; mov esi,ecx ; mov ecx,[esi+4] ; test ecx,ecx ; jnz +4 ;
// xor eax,eax ; pop esi ; ret` - no active backend, no events.
inline bool utFrontMouseCountOk(const unsigned char* p, size_t n) {
    static const unsigned char k[kUtFrontCountSpan] = {0x56, 0x8B, 0xF1, 0x8B, 0x4E, 0x04, 0x85,
                                                       0xC9, 0x75, 0x04, 0x33, 0xC0, 0x5E, 0xC3};
    return p && n >= sizeof(k) && !memcmp(p, k, sizeof(k));
}

// InputDevice::Create stores the front's vtable at +0x50 (`mov dword [esi], imm32`, C7 06 imm32):
// the vtable without a live object. 0 when the bytes are not that store.
constexpr size_t kUtCreateVtableStore = 0x50;
inline unsigned long utCreateFrontVtable(const unsigned char* p, size_t n) {
    if (!p || n < kUtCreateVtableStore + 6) return 0;
    const unsigned char* q = p + kUtCreateVtableStore;
    if (q[0] != 0xC7 || q[1] != 0x06) return 0;
    unsigned long v;
    memcpy(&v, q + 2, 4);
    return v;
}

// The record size, out of the exported MouseEvent::operator=: it copies the record in movq pairs,
// the last store `movq [ecx+d8], xmm0` (66 0F D6 41 d8) right before `mov eax,ecx ; ret 4`
// (8B C1 C2 04 00) -> d8 + 8. 0 when that tail is not there.
inline unsigned utMouseEventSizeFromAssign(const unsigned char* p, size_t n) {
    if (!p) return 0;
    for (size_t i = 0; i + 10 <= n; ++i) {
        if (p[i] == 0x66 && p[i + 1] == 0x0F && p[i + 2] == 0xD6 && p[i + 3] == 0x41 &&
            p[i + 5] == 0x8B && p[i + 6] == 0xC1 && p[i + 7] == 0xC2 && p[i + 8] == 0x04 &&
            p[i + 9] == 0x00)
            return p[i + 4] + 8u;
    }
    return 0;
}

// The type's offset, out of the exported MouseEvent::ConvertToButtonEvent: `mov eax,[ecx+d8] ;
// add eax,-3` (8B 41 d8 83 C0 FD) at +0x20. -1 when the bytes are not that read.
constexpr size_t kUtConvertTypeRead = 0x20;
inline int utMouseEventTypeOffFromConvert(const unsigned char* p, size_t n) {
    if (!p || n < kUtConvertTypeRead + 6) return -1;
    const unsigned char* q = p + kUtConvertTypeRead;
    if (q[0] != 0x8B || q[1] != 0x41 || q[3] != 0x83 || q[4] != 0xC0 || q[5] != 0xFD) return -1;
    return q[2];
}

// The record row's verdict: the engine's own copy and its own type read agree with the constants.
inline bool utMouseEventRecordOk(const unsigned char* assign, size_t na, const unsigned char* conv,
                                 size_t nc) {
    return utMouseEventSizeFromAssign(assign, na) == kUtMouseEventSize &&
           utMouseEventTypeOffFromConvert(conv, nc) == (int)kUtMouseEvtTypeOff;
}

}  // namespace ut
