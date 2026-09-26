// ut_bindings.cpp - the registry behind ut_bindings.h: every binding's outcome in one table, one
// INFO line, and the all-or-nothing gate that runs before a single hook is installed.

#include "ut_bindings.h"

#include <stdio.h>
#include <string.h>

#include "ut_log.h"

namespace ut {
namespace {

// ---- the two counted TQ.exe signatures ----------------------------------------------------------
// Derived from the bytes at the AE 2.10 rvas in the on-disk TQ.exe (no DRM stub: the file IS the
// image). Every operand the .reloc table relocates is wildcarded, so neither pattern carries an
// address. RESOLVED AND LOGGED ONLY: nothing is hooked on either.
//
// The Transfer page accessor, AE 2.10 rva 0xC2F50 (HANDOFF.md section 5):
//   53                       push ebx
//   8B D9                    mov  ebx,ecx
//   8B 0D ?? ?? ?? ??        mov  ecx,[gGameEngine]           ; relocated
//   56 / 8B 09 / 57          push esi / mov ecx,[ecx] / push edi
//   FF 15 ?? ?? ?? ??        call [GameEngine::GetPlayerTransfer]  ; IAT, returns to +0x13 = 0xC2F63
//   F3 0F 10 83 A0 00 00 00  movss xmm0,[ebx+0xA0]
//   8B 35 ?? ?? ?? ??        mov  esi,[IAT]                   ; relocated
//   8D BB 88 00 00 00        lea  edi,[ebx+0x88]              ; the UIStashInventory sub-object
//   0F 57 C9 / 0F 2F C1      xorps / comiss
//   8B C8 / 89 4F 60         mov ecx,eax / mov [edi+0x60],ecx ; THE CACHED SACK POINTER
const unsigned char kSigTransferPageBytes[] = {
    0x53, 0x8B, 0xD9, 0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x56, 0x8B, 0x09, 0x57,
    0xFF, 0x15, 0x00, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x10, 0x83, 0xA0, 0x00, 0x00,
    0x00, 0x8B, 0x35, 0x00, 0x00, 0x00, 0x00, 0x8D, 0xBB, 0x88, 0x00, 0x00, 0x00,
    0x0F, 0x57, 0xC9, 0x0F, 0x2F, 0xC1, 0x8B, 0xC8, 0x89, 0x4F, 0x60};
const unsigned char kSigTransferPageMask[] = {
    1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};

// UIStashInventory::StreamOut, AE 2.10 rva 0xBF990 - the writer of the caravan files. The SEH
// prologue alone matches 859 times, so the pattern runs on to the first use of its argument:
//   6A FF / 68 ?? ?? ?? ??   push -1 / push <handler>          ; relocated
//   64 A1 00 00 00 00 / 50   mov eax,fs:[0] / push eax
//   83 EC 48                 sub  esp,0x48
//   A1 ?? ?? ?? ?? / 33 C4   mov eax,[__security_cookie] / xor eax,esp   ; relocated
//   89 44 24 40              mov  [esp+0x40],eax
//   53 55 56 57              push ebx/ebp/esi/edi
//   A1 ?? ?? ?? ?? / 33 C4 / 50                              ; relocated
//   8D 44 24 5C / 64 A3 00 00 00 00                          ; the SEH frame
//   8B F1                    mov  esi,ecx
//   8B 7C 24 6C              mov  edi,[esp+0x6C]              ; the std::string path
//   83 7F 14 10 / 72 04      cmp  dword [edi+0x14],0x10 / jb  ; its capacity: in place or not
//   8B 07                    mov  eax,[edi]
const unsigned char kSigStreamOutBytes[] = {
    0x6A, 0xFF, 0x68, 0x00, 0x00, 0x00, 0x00, 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x50, 0x83, 0xEC,
    0x48, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x33, 0xC4, 0x89, 0x44, 0x24, 0x40, 0x53, 0x55, 0x56, 0x57,
    0xA1, 0x00, 0x00, 0x00, 0x00, 0x33, 0xC4, 0x50, 0x8D, 0x44, 0x24, 0x5C, 0x64, 0xA3, 0x00, 0x00,
    0x00, 0x00, 0x8B, 0xF1, 0x8B, 0x7C, 0x24, 0x6C, 0x83, 0x7F, 0x14, 0x10, 0x72, 0x04, 0x8B, 0x07};
const unsigned char kSigStreamOutMask[] = {
    1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};

// TQ.exe 0xBFD10, the page's RIGHT-CLICK handler (slot 0x21D09C): GetItemUnderPoint([this+0x60])
// then 0xC0530 clones the item into the player's inventory BEFORE any RemoveItemFrom* export
// runs. 197 bytes from the entry to the end of the `FF 15` call; the 28 relocated bytes (SEH
// handler, the cookie twice, fs:[0] is not relocated, three IAT slots, two .rdata floats) are
// wildcarded (tools\sig_from_exe.py, the base-relocation table). Return address = match + 197.
const unsigned char kSigRightClickBytes[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x6A, 0xFF, 0x68, 0x00, 0x00, 0x00, 0x00,
    0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x50, 0x83, 0xEC, 0x38, 0xA1, 0x00, 0x00,
    0x00, 0x00, 0x33, 0xC4, 0x89, 0x44, 0x24, 0x30, 0x53, 0x56, 0x57, 0xA1, 0x00,
    0x00, 0x00, 0x00, 0x33, 0xC4, 0x50, 0x8D, 0x44, 0x24, 0x48, 0x64, 0xA3, 0x00,
    0x00, 0x00, 0x00, 0x8B, 0xF1, 0x80, 0x7E, 0x0B, 0x00, 0x8B, 0x45, 0x10, 0x8B,
    0x5D, 0x08, 0x89, 0x44, 0x24, 0x1C, 0x75, 0x07, 0x32, 0xC0, 0xE9, 0x47, 0x01,
    0x00, 0x00, 0x8B, 0x4E, 0x74, 0x83, 0xC1, 0x08, 0x8B, 0x01, 0xFF, 0x50, 0x04,
    0x83, 0x7B, 0x10, 0x00, 0xC6, 0x44, 0x24, 0x18, 0x00, 0xC6, 0x44, 0x24, 0x17,
    0x00, 0x0F, 0x85, 0x10, 0x01, 0x00, 0x00, 0x6A, 0x44, 0xFF, 0x73, 0x0C, 0x8B,
    0xC8, 0xFF, 0x15, 0x00, 0x00, 0x00, 0x00, 0x84, 0xC0, 0x0F, 0x84, 0xFB, 0x00,
    0x00, 0x00, 0xF3, 0x0F, 0x10, 0x46, 0x48, 0xF3, 0x0F, 0x59, 0x05, 0x00, 0x00,
    0x00, 0x00, 0x8B, 0x4E, 0x60, 0xF3, 0x0F, 0x58, 0x46, 0x40, 0xF3, 0x0F, 0x11,
    0x44, 0x24, 0x20, 0xF3, 0x0F, 0x10, 0x46, 0x4C, 0xF3, 0x0F, 0x59, 0x05, 0x00,
    0x00, 0x00, 0x00, 0xF3, 0x0F, 0x58, 0x46, 0x44, 0xF3, 0x0F, 0x11, 0x44, 0x24,
    0x24, 0xFF, 0x74, 0x24, 0x24, 0xFF, 0x74, 0x24, 0x24, 0xFF, 0x15, 0x00, 0x00,
    0x00, 0x00};
const unsigned char kSigRightClickMask[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
    0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0};

// TQ.exe 0xBEC00, the page's HELD-PRESS pick-up (from the window's input handler 0xC6D64):
// GetItemUnderPoint([this+0x60]) then, with the id on a cursor, InventorySack::RemoveItem
// directly (no RemoveItemFrom* export). 111 bytes to the end of the `FF 15` call, 16 relocated
// bytes wildcarded; one relative call (E8 -> 0x13F00) is matched as is. Return = match + 111.
const unsigned char kSigHeldPickupBytes[] = {
    0x83, 0xEC, 0x08, 0x53, 0x55, 0x8B, 0xD9, 0x56, 0x83, 0x7B, 0x54, 0x00, 0x57,
    0x0F, 0x84, 0xD4, 0x00, 0x00, 0x00, 0xFF, 0x73, 0x78, 0x8B, 0x35, 0x00, 0x00,
    0x00, 0x00, 0xC7, 0x43, 0x54, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xD6, 0x8B, 0xC8,
    0xE8, 0xD4, 0x52, 0xF5, 0xFF, 0xF3, 0x0F, 0x10, 0x43, 0x48, 0xF3, 0x0F, 0x59,
    0x05, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x4B, 0x60, 0x8B, 0xF8, 0xF3, 0x0F, 0x58,
    0x43, 0x40, 0xF3, 0x0F, 0x11, 0x44, 0x24, 0x10, 0xF3, 0x0F, 0x10, 0x43, 0x4C,
    0xF3, 0x0F, 0x59, 0x05, 0x00, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x58, 0x43, 0x44,
    0xF3, 0x0F, 0x11, 0x44, 0x24, 0x14, 0xFF, 0x74, 0x24, 0x14, 0xFF, 0x74, 0x24,
    0x14, 0xFF, 0x15, 0x00, 0x00, 0x00, 0x00};
const unsigned char kSigHeldPickupMask[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
    0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0};

// TQ.exe 0xBFED0, the page's LEFT-CLICK handler (slot 0x21D100) -. Its
// GetItemUnderPoint([this+0x60]) at 0xC00C8 stores the id in [this+0x110]; with the handler's first
// bool that id is the pick-up (SetId -> RemoveItemFrom*), with its SECOND bool (0xC0208) it is
// CLONED into the player's inventory (0xC0530, the right-click's clone) BEFORE
// RemoveItemFromTransfer (0xC0265) - a modified click would copy a prototype out. [this+0x110] is
// read nowhere else in the page class (0xBE000..0xC3000), so a 0 from this call only means "an
// empty cell was clicked", which vanilla does all the time. 97 bytes from 0xC006D (the vault drop
// branch, then the pick-up entry 0xC00B1) to the end of the `FF 15`; the 4 relocated bytes (the IAT
// slot) are wildcarded (tools\sig_from_exe.py). Return address = match + 97 = 0xC00CE.
const unsigned char kSigLeftClickBytes[] = {
    0x83, 0xF8, 0x02, 0x0F, 0x85, 0x60, 0x03, 0x00, 0x00, 0x8B, 0x06, 0x8B, 0xCE,
    0x8B, 0x40, 0x34, 0xFF, 0xD0, 0x84, 0xC0, 0x0F, 0x84, 0x4F, 0x03, 0x00, 0x00,
    0x8B, 0x06, 0x8D, 0x4C, 0x24, 0x14, 0x51, 0x8B, 0xCE, 0xFF, 0x50, 0x08, 0x80,
    0x7D, 0x08, 0x00, 0x0F, 0x84, 0x39, 0x03, 0x00, 0x00, 0x8B, 0x06, 0x8D, 0x4C,
    0x24, 0x14, 0x51, 0x8B, 0xCE, 0xFF, 0x90, 0xA0, 0x00, 0x00, 0x00, 0xE9, 0x25,
    0x03, 0x00, 0x00, 0x8B, 0x54, 0x24, 0x18, 0x8B, 0x4C, 0x24, 0x14, 0x8B, 0x43,
    0x60, 0x85, 0xC0, 0x0F, 0x84, 0x12, 0x03, 0x00, 0x00, 0x52, 0x51, 0x8B, 0xC8,
    0xFF, 0x15, 0x00, 0x00, 0x00, 0x00};
const unsigned char kSigLeftClickMask[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0};

// TQ.exe 0xBFED0, the ENTRY of the page's mouse handler:
// `__thiscall(this, bool b1, bool b2, const Vec2* mouse, const Vec2* origin, Widget** hoverOut)`,
// ret 0x14. b1 = the press (pick-up), b2 = the modified click (clone), both clear = the HOVER, whose
// branch (0xC0314) only feeds the tooltip. 413 bytes from the entry to 0xC006D, where
// exe.leftClickUnderPoint begins: the two patterns together fix every non-relocated byte from the
// entry to the GetItemUnderPoint call, so the frame at that call is exactly the one read below
// (EBP frame, `and esp,-8`, 3 SEH pushes, `sub esp,0x40` at +0x14, 3 register pushes + the cookie,
// the Vec2 by value): return slot = (EBP & ~7) - 0x68. 12 relocated bytes wildcarded (the SEH
// handler, the cookie twice; tools/sig_from_exe.py), exactly one match.
const unsigned char kSigLeftHandlerBytes[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x6A, 0xFF, 0x68, 0x00, 0x00, 0x00, 0x00,
    0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x50, 0x83, 0xEC, 0x40, 0xA1, 0x00, 0x00,
    0x00, 0x00, 0x33, 0xC4, 0x89, 0x44, 0x24, 0x38, 0x53, 0x56, 0x57, 0xA1, 0x00,
    0x00, 0x00, 0x00, 0x33, 0xC4, 0x50, 0x8D, 0x44, 0x24, 0x50, 0x64, 0xA3, 0x00,
    0x00, 0x00, 0x00, 0x8B, 0xD9, 0x8B, 0x43, 0x7C, 0xF3, 0x0F, 0x10, 0x5B, 0x10,
    0xF3, 0x0F, 0x10, 0x53, 0x14, 0xF3, 0x0F, 0x10, 0x63, 0x18, 0xF3, 0x0F, 0x10,
    0x6B, 0x1C, 0x8B, 0x4D, 0x18, 0x89, 0x83, 0x80, 0x00, 0x00, 0x00, 0x8B, 0x45,
    0x14, 0x89, 0x4C, 0x24, 0x24, 0xF3, 0x0F, 0x58, 0x18, 0xF3, 0x0F, 0x58, 0x50,
    0x04, 0x8B, 0x45, 0x10, 0xC6, 0x83, 0x41, 0x01, 0x00, 0x00, 0x00, 0xC7, 0x83,
    0x10, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x10, 0x08, 0x0F,
    0x2F, 0xCB, 0xC6, 0x44, 0x24, 0x13, 0x00, 0x0F, 0x82, 0x77, 0x04, 0x00, 0x00,
    0x0F, 0x28, 0xC3, 0xF3, 0x0F, 0x58, 0xC4, 0x0F, 0x2F, 0xC1, 0x0F, 0x86, 0x67,
    0x04, 0x00, 0x00, 0xF3, 0x0F, 0x10, 0x48, 0x04, 0x0F, 0x2F, 0xCA, 0x0F, 0x82,
    0x59, 0x04, 0x00, 0x00, 0x0F, 0x28, 0xC2, 0xF3, 0x0F, 0x58, 0xC5, 0x0F, 0x2F,
    0xC1, 0x0F, 0x86, 0x49, 0x04, 0x00, 0x00, 0xC6, 0x83, 0x41, 0x01, 0x00, 0x00,
    0x01, 0xF3, 0x0F, 0x10, 0x08, 0xF3, 0x0F, 0x10, 0x40, 0x04, 0xF3, 0x0F, 0x5C,
    0xCB, 0xF3, 0x0F, 0x5C, 0xC2, 0x89, 0x19, 0x8B, 0x43, 0x74, 0xC6, 0x44, 0x24,
    0x13, 0x01, 0xF3, 0x0F, 0x11, 0x4C, 0x24, 0x1C, 0x8B, 0x4C, 0x24, 0x1C, 0xF3,
    0x0F, 0x11, 0x44, 0x24, 0x20, 0x8B, 0x54, 0x24, 0x20, 0x89, 0x4C, 0x24, 0x14,
    0x89, 0x54, 0x24, 0x18, 0x8B, 0xB0, 0x00, 0x01, 0x00, 0x00, 0x85, 0xF6, 0x0F,
    0x84, 0xE0, 0x00, 0x00, 0x00, 0x8B, 0x06, 0x8B, 0xCE, 0xFF, 0x50, 0x64, 0x85,
    0xC0, 0x0F, 0x84, 0xC9, 0x00, 0x00, 0x00, 0x8B, 0x83, 0x48, 0x01, 0x00, 0x00,
    0x83, 0xF8, 0x01, 0x75, 0x3B, 0x8B, 0x06, 0x8B, 0xCE, 0x8B, 0x40, 0x30, 0xFF,
    0xD0, 0x84, 0xC0, 0x0F, 0x84, 0xD2, 0x03, 0x00, 0x00, 0x8B, 0x06, 0x8D, 0x4C,
    0x24, 0x14, 0x51, 0x8B, 0xCE, 0xFF, 0x50, 0x08, 0x80, 0x7D, 0x08, 0x00, 0x0F,
    0x84, 0xBC, 0x03, 0x00, 0x00, 0x8B, 0x06, 0x8D, 0x4C, 0x24, 0x14, 0x51, 0x8B,
    0xCE, 0xFF, 0x90, 0x98, 0x00, 0x00, 0x00, 0xE9, 0xA8, 0x03, 0x00, 0x00, 0x85,
    0xC0, 0x75, 0x3B, 0x8B, 0x06, 0x8B, 0xCE, 0x8B, 0x40, 0x2C, 0xFF, 0xD0, 0x84,
    0xC0, 0x0F, 0x84, 0x93, 0x03, 0x00, 0x00, 0x8B, 0x06, 0x8D, 0x4C, 0x24, 0x14,
    0x51, 0x8B, 0xCE, 0xFF, 0x50, 0x08, 0x80, 0x7D, 0x08, 0x00, 0x0F, 0x84, 0x7D,
    0x03, 0x00, 0x00, 0x8B, 0x06, 0x8D, 0x4C, 0x24, 0x14, 0x51, 0x8B, 0xCE, 0xFF,
    0x90, 0x90, 0x00, 0x00, 0x00, 0xE9, 0x69, 0x03, 0x00, 0x00};
const unsigned char kSigLeftHandlerMask[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
    0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};


// TQ.exe 0xC31A0, the Transfer page window's draw (vtable 0x21D1C0 slot +0x0C, `ret 0x10`):
// `55 8B EC 83 E4 F8 83 EC 1C` ... `8B 86 88 00 00 00` (mov eax,[esi+0x88]: the inventory) ...
// `FF 75 08` / `FF 50 0C` (the inventory's own draw: the cell ground and the items). 185 bytes from
// the entry through that call; the two `E8` rel32 (0xC31CE, 0xC31F0) are wildcarded. The mod's
// PRE-detour draws the slot plates before the original, so the engine draws the items over them.
const unsigned char kSigPageDrawBytes[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x83, 0xEC, 0x1C, 0x53, 0x56, 0x8B, 0xF1,
    0x57, 0x8B, 0x46, 0x1C, 0x89, 0x44, 0x24, 0x18, 0x8B, 0x46, 0x20, 0x89, 0x44,
    0x24, 0x1C, 0x8D, 0x44, 0x24, 0x20, 0x50, 0x8D, 0x44, 0x24, 0x1C, 0x50, 0xC7,
    0x44, 0x24, 0x2C, 0x00, 0x00, 0x00, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x00, 0xF3,
    0x0F, 0x10, 0x4C, 0x24, 0x24, 0x8B, 0x5D, 0x10, 0x0F, 0x28, 0xC1, 0xF3, 0x0F,
    0x11, 0x4C, 0x24, 0x14, 0xF3, 0x0F, 0x11, 0x44, 0x24, 0x10, 0x83, 0xFB, 0x01,
    0x75, 0x1B, 0xE8, 0x00, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x10, 0x4C, 0x24, 0x24,
    0xF3, 0x0F, 0x59, 0xC8, 0xF3, 0x0F, 0x10, 0x44, 0x24, 0x10, 0xF3, 0x0F, 0x11,
    0x4C, 0x24, 0x14, 0x8B, 0x7D, 0x0C, 0xF3, 0x0F, 0x5C, 0xC1, 0x8B, 0x86, 0x88,
    0x00, 0x00, 0x00, 0x8D, 0x8E, 0x88, 0x00, 0x00, 0x00, 0x51, 0x8D, 0x54, 0x24,
    0x24, 0xF3, 0x0F, 0x11, 0x44, 0x24, 0x14, 0xF3, 0x0F, 0x10, 0x07, 0xF3, 0x0F,
    0x58, 0x44, 0x24, 0x1C, 0xF3, 0x0F, 0x11, 0x44, 0x24, 0x24, 0x0F, 0x28, 0xC1,
    0xF3, 0x0F, 0x58, 0x47, 0x04, 0xF3, 0x0F, 0x11, 0x44, 0x24, 0x28, 0xF3, 0x0F,
    0x10, 0x45, 0x14, 0xF3, 0x0F, 0x11, 0x04, 0x24, 0x53, 0x52, 0xFF, 0x75, 0x08,
    0xFF, 0x50, 0x0C,
};
// TQ.exe 0x10A9B0, an item widget's background (vtable slot +0x88 of the four item-widget
// classes 0x21F0C0 / 0x21F208 / 0x21F2B0 / 0x21F358, `ret 0x18`), the WHOLE function (166 bytes):
// `83 EC 10 / F3 0F 7E 41 10` (movq xmm0,[ecx+0x10]: the rect's x, y) ... `F3 0F 7E 41 18` (w, h) ...
// `F3 0F 10 49 78` (the scale [+0x78]) ... `FF 90 84 00 00 00` (the background proper, 0x10A850: the
// tint, then the border texture over the same rect) / `C2 18 00`. The one relocated operand (the
// address of the 2.0 at 0x623400 - the (w - 2k) inset -, `F3 0F 59 0D <d32>`) is wildcarded. The
// mod's detour hands a prototype's widget its SLOT rect for this call and the centred footprint for
// the icon that follows.
const unsigned char kSigItemBackgroundBytes[] = {
    0x83, 0xEC, 0x10, 0xF3, 0x0F, 0x7E, 0x41, 0x10, 0x8B, 0x44, 0x24, 0x18, 0xF3,
    0x0F, 0x10, 0x4C, 0x24, 0x20, 0x66, 0x0F, 0xD6, 0x04, 0x24, 0xF3, 0x0F, 0x7E,
    0x41, 0x18, 0x66, 0x0F, 0xD6, 0x44, 0x24, 0x08, 0xF3, 0x0F, 0x10, 0x00, 0xFF,
    0x74, 0x24, 0x28, 0xF3, 0x0F, 0x10, 0x54, 0x24, 0x0C, 0xF3, 0x0F, 0x10, 0x5C,
    0x24, 0x10, 0xFF, 0x74, 0x24, 0x28, 0xF3, 0x0F, 0x58, 0xC1, 0xFF, 0x74, 0x24,
    0x24, 0x8D, 0x54, 0x24, 0x0C, 0x52, 0xFF, 0x74, 0x24, 0x24, 0xF3, 0x0F, 0x58,
    0x44, 0x24, 0x14, 0xF3, 0x0F, 0x11, 0x44, 0x24, 0x14, 0xF3, 0x0F, 0x10, 0x40,
    0x04, 0x8B, 0x01, 0xF3, 0x0F, 0x58, 0xC1, 0xF3, 0x0F, 0x59, 0x0D, 0x00, 0x00,
    0x00, 0x00, 0xF3, 0x0F, 0x58, 0x44, 0x24, 0x18, 0xF3, 0x0F, 0x5C, 0xD1, 0xF3,
    0x0F, 0x5C, 0xD9, 0xF3, 0x0F, 0x10, 0x49, 0x78, 0xF3, 0x0F, 0x11, 0x44, 0x24,
    0x18, 0x0F, 0x28, 0xC1, 0xF3, 0x0F, 0x59, 0xC2, 0xF3, 0x0F, 0x59, 0xCB, 0xF3,
    0x0F, 0x11, 0x44, 0x24, 0x1C, 0xF3, 0x0F, 0x11, 0x4C, 0x24, 0x20, 0xFF, 0x90,
    0x84, 0x00, 0x00, 0x00, 0x83, 0xC4, 0x10, 0xC2, 0x18, 0x00,
};
const unsigned char kSigItemBackgroundMask[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

const unsigned char kSigPageDrawMask[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1,
};

// ---- the table --------------------------------------------------------------------------------
struct Row {
    const char* name;
    unsigned char cls;
    unsigned char phase;
    const char* how;
    const char* confirm;
    unsigned char gate;  // UT_BIND_CRITICAL unless the row says otherwise
};

// `ok` starts true for STRUCTURAL and LITERAL: their confirmation is not a start-up event but a
// test the code performs at EVERY use (an SEH-guarded read with a plausibility range, a vtable
// cross-check). What the gate asserts for them is that the test EXISTS and is named here; a
// failure at use turns its own route off and says so. Everything else starts NOT resolved and
// must be reported by the module that owns it, so a decoder that silently stopped running is a
// gate failure rather than a feature that quietly went missing.
const Row kRows[] = {
    // -- DECODED, early -------------------------------------------------------------------------
    {"gameEngine.stashSack", UT_BIND_DECODED, UT_BIND_EARLY,
     "GameEngine::GetPlayerStash bytes (8D 81 <d32> C3)",
     "the const twin agrees; 4-aligned, below 0x10000; the three sacks equally spaced"},
    {"gameEngine.transferSack", UT_BIND_DECODED, UT_BIND_EARLY,
     "GameEngine::GetPlayerTransfer bytes (8D 81 <d32> C3)",
     "the const twin agrees; 4-aligned, below 0x10000; the three sacks equally spaced"},
    {"gameEngine.relicVaultSack", UT_BIND_DECODED, UT_BIND_EARLY,
     "GameEngine::GetPlayerRelicVault bytes (8D 81 <d32> C3)",
     "the const twin agrees; 4-aligned, below 0x10000; the three sacks equally spaced"},
    {"gameEngine.caravanMode", UT_BIND_DECODED, UT_BIND_EARLY,
     "GameEngine::GetCaravanMode bytes (8B 81 <d32> C3)", "4-aligned, below the stash sack"},
    {"cursor.itemId", UT_BIND_DECODED, UT_BIND_EARLY,
     "CursorHandlerItemMove::GetId bytes (8B 41 <d8> C3)", "4-aligned, 4..0x7C"},
    // The vftable slots, found BY ADDRESS. Nothing reads them in ADVISORY.
    {"item.replicaSlot", UT_BIND_DECODED, UT_BIND_EARLY,
     "the slot of ??_7Item holding Item::GetItemReplicaInfo", "exactly one slot holds it",
     UT_BIND_ADVISORY},
    {"item.classificationSlot", UT_BIND_DECODED, UT_BIND_EARLY,
     "the slot of ??_7Item holding Item::GetItemClassification", "exactly one slot holds it",
     UT_BIND_ADVISORY},
    {"item.typeSlot", UT_BIND_DECODED, UT_BIND_EARLY,
     "the slot of ??_7Item holding Item::GetItemType", "exactly one slot holds it",
     UT_BIND_ADVISORY},
    {"item.stackSlot", UT_BIND_DECODED, UT_BIND_EARLY,
     "the slot of ??_7Item holding Item::GetNumberInStack", "exactly one slot holds it",
     UT_BIND_ADVISORY},
    {"cursor.capabilitySlots", UT_BIND_DECODED, UT_BIND_EARLY,
     "??_7CursorHandlerItemMove slots +0x18..+0x34",
     "the three Is*Capable exports are one `mov al,1; ret` body and all eight slots hold it",
     UT_BIND_ADVISORY},
    // -- STRUCTURAL -----------------------------------------------------------------------------
    {"msvc.string", UT_BIND_STRUCTURAL, UT_BIND_EARLY, "VS2012 x86 std::string layout (0x18)",
     "capacity < 16 means in place; size <= capacity <= 1 MB or the string is refused"},
    // -- SIGNATURE, early (TQ.exe has no DRM stub); nothing is hooked on them -------------
    // all five are REQUIRED FOR THE VIEW (ut_view.cpp asks bindingsRowOk; a failure makes
    // the view unavailable) and ADVISORY FOR THE MOD (the pass-through detours still run).
    {"exe.transferPageAccessor", UT_BIND_SIGNATURE, UT_BIND_EARLY,
     "50 bytes, the three relocated operands wildcarded, scanned in TQ.exe's .text",
     "exactly one match", UT_BIND_ADVISORY},
    {"exe.stashStreamOut", UT_BIND_SIGNATURE, UT_BIND_EARLY,
     "64 bytes, the three relocated operands wildcarded, scanned in TQ.exe's .text",
     "exactly one match", UT_BIND_ADVISORY},
    {"exe.rightClickUnderPoint", UT_BIND_SIGNATURE, UT_BIND_EARLY,
     "197 bytes to the end of its GetItemUnderPoint call, 28 relocated bytes wildcarded",
     "exactly one match", UT_BIND_ADVISORY},
    {"exe.heldPickupUnderPoint", UT_BIND_SIGNATURE, UT_BIND_EARLY,
     "111 bytes to the end of its GetItemUnderPoint call, 16 relocated bytes wildcarded",
     "exactly one match", UT_BIND_ADVISORY},
    {"exe.leftClickUnderPoint", UT_BIND_SIGNATURE, UT_BIND_EARLY,
     "97 bytes to the end of its GetItemUnderPoint call, 4 relocated bytes wildcarded",
     "exactly one match", UT_BIND_ADVISORY},
    // NOT required for the view - without it the hover stays refused (no tooltips).
    {"exe.leftClickHandler", UT_BIND_SIGNATURE, UT_BIND_EARLY,
     "413 bytes from the page mouse handler's entry to where exe.leftClickUnderPoint begins, 12 "
     "relocated bytes wildcarded",
     "exactly one match (its contiguity with exe.leftClickUnderPoint is the .frame row's check)",
     UT_BIND_ADVISORY},
    // NOT required for the view - without it the slot plates are drawn as thin frames from
    // the Present detour instead (over the items).
    {"exe.transferPageDraw", UT_BIND_SIGNATURE, UT_BIND_EARLY,
     "185 bytes from the Transfer page draw's entry through its `FF 50 0C` (the inventory's draw), "
     "the two E8 rel32 wildcarded",
     "exactly one match", UT_BIND_ADVISORY},
    // NOT required for the view - without it the items stay at their footprint (the
    // ring tints the rest of the slot) and the border frames the footprint only.
    {"exe.itemBackground", UT_BIND_SIGNATURE, UT_BIND_EARLY,
     "166 bytes, the whole item-widget background draw (rect [+0x10] / [+0x18], scale [+0x78], "
     "`FF 90 84 00 00 00`, `ret 0x18`), the one relocated operand wildcarded",
     "exactly one match", UT_BIND_ADVISORY},
    // -- DECODED out of the matched signature bytes (view-required) -----------------------
    {"page.cachedSack", UT_BIND_DECODED, UT_BIND_EARLY,
     "the accessor's `8D BB <d32>` (+0x21) and `89 4F <d8>` (+0x2F): sub-window -> cached sack",
     "both plausible, and its FF 15 (+0x0D) goes through GetPlayerTransfer's IAT slot",
     UT_BIND_ADVISORY},
    {"exe.rightClickUnderPoint.ret", UT_BIND_DECODED, UT_BIND_EARLY,
     "match + 197: the return address of the right-click's GetItemUnderPoint call",
     "the pattern's last `FF 15` goes through GetItemUnderPoint's IAT slot", UT_BIND_ADVISORY},
    {"exe.heldPickupUnderPoint.ret", UT_BIND_DECODED, UT_BIND_EARLY,
     "match + 111: the return address of the held pick-up's GetItemUnderPoint call",
     "the pattern's last `FF 15` goes through GetItemUnderPoint's IAT slot", UT_BIND_ADVISORY},
    {"exe.leftClickUnderPoint.ret", UT_BIND_DECODED, UT_BIND_EARLY,
     "match + 97: the return address of the left-click's GetItemUnderPoint call",
     "the pattern's last `FF 15` goes through GetItemUnderPoint's IAT slot", UT_BIND_ADVISORY},
    {"exe.leftClickHandler.frame", UT_BIND_DECODED, UT_BIND_EARLY,
     "the handler's frame at its GetItemUnderPoint call: `55 8B EC 83 E4 F8` (+0), `83 EC <d8>` "
     "(+0x14): EBP - return slot = 0x28 + d8 + (EBP & 7); b1 [EBP+8], b2 [EBP+0xC], mouse* "
     "[EBP+0x10] (`8B 45 10` +0x69), origin* [EBP+0x14] (`8B 45 14` +0x59)",
     "the bytes read as stated, and at every use the frame distance, [this+0x60] == the sack and "
     "the handler's own point arithmetic (x == mouse.x - (pos.x + origin.x)) all agree, else the "
     "call is refused",
     UT_BIND_ADVISORY},
    // -- the drag-drop deposit's disposal, out of the engine's own drop handler ------------
    {"cursor.disposal", UT_BIND_DECODED, UT_BIND_EARLY,
     "PrimaryTransferActivate (Game.dll 0x152590) after its add: `8B 43 <d8>` (the handler's "
     "player, 0x14), `FF B0 <d32>` (its controller id, 0xC10), then an `E8` to "
     "ControllerCharacter::SendRemoveItemFromInventory (0x1526F0)",
     "the shape is found in the export's first 0x200 bytes, the call's target is the export, "
     "and d32 == the displacement Character::GetControllerId reads (`8B 81 d32 C3`); without it "
     "the drag-drop deposit stays refused (the quick-move is unaffected)",
     UT_BIND_ADVISORY},
    // -- the quick-move's primary AddItemToTransfer(id) call ----------------
    {"exe.quickMoveAdd.ret", UT_BIND_DECODED, UT_BIND_EARLY,
     "TQ.exe `8B 3D <IAT slot of GameEngine::AddItemToTransfer(id,bool)>` (0x107A61) followed within "
     "0x10 bytes by `FF D7 84 C0 0F 84` (0x107A6A): the return address of the quick-move's primary "
     "call (0x107A6C)",
     "exactly one such load of that slot; while the view is ON an AddItemToTransfer(id) from any other "
     "return address (the stacked-extras loop 0x107AEF included) is refused with an ERROR, and without "
     "the row every quick-move deposit is refused",
     UT_BIND_ADVISORY},
    // -- LITERAL / STRUCTURAL, tested at every use ---------------------------------------
    {"ui.stashKind", UT_BIND_LITERAL, UT_BIND_EARLY,
     "UIStashInventory+0x148 = the store kind (0 stash, 1 transfer, 2 vault; TQ.exe 0xBFFE8)",
     "read under SEH; must read 1 on the Transfer page after its accessor ran, else the view is "
     "refused"},
    {"msvc.map", UT_BIND_STRUCTURAL, UT_BIND_EARLY,
     "VS2012 x86 std::map: {head, size}; node {left, parent, right, color, isnil, value}",
     "the walk counts exactly `size` non-nil nodes, or the owned count reads UNKNOWN",
     UT_BIND_ADVISORY},
    {"msvc.vector", UT_BIND_STRUCTURAL, UT_BIND_EARLY, "VS2012 x86 std::vector: {first, last, end}",
     "first <= last <= end, 4-aligned span; freed through MSVCR110 operator delete",
     UT_BIND_ADVISORY},
};

const int kRowCount = (int)(sizeof(kRows) / sizeof(kRows[0]));

// `why` is COPIED, never pointed at. A caller that builds its reason with _snprintf_s into a local
// buffer - which is the natural thing to do when the reason carries a count - would otherwise hand
// this table a dangling pointer that only reads wrong in the log much later.
struct State {
    unsigned long long value = 0;
    bool ok = false;
    bool reported = false;
    bool warned = false;  // an ADVISORY failure is said once
    char why[192];
};

State g_state[kRowCount];
volatile LONG g_seeded = 0;
volatile LONG g_lateSaid = 0;
int g_exportCount = 0;
char g_summary[224] = "bindings: not resolved yet";
const char* g_failName = "";

void seed() {
    if (InterlockedExchange(&g_seeded, 1)) return;
    for (int i = 0; i < kRowCount; ++i) {
        // STRUCTURAL and LITERAL are confirmed at every use, not once at start-up - see the note
        // above kRows. They start satisfied; the code that uses them is what can still say no.
        _snprintf_s(g_state[i].why, sizeof(g_state[i].why), _TRUNCATE, "%s", "not reported");
        if (kRows[i].cls == UT_BIND_STRUCTURAL || kRows[i].cls == UT_BIND_LITERAL) {
            g_state[i].ok = true;
            g_state[i].reported = true;
            _snprintf_s(g_state[i].why, sizeof(g_state[i].why), _TRUNCATE, "%s",
                        kRows[i].confirm);
        }
    }
}

int indexOf(const char* name) {
    for (int i = 0; i < kRowCount; ++i) {
        if (strcmp(kRows[i].name, name) == 0) return i;
    }
    return -1;
}

const char* className(int cls) {
    switch (cls) {
        case UT_BIND_EXPORT: return "export";
        case UT_BIND_SIGNATURE: return "signature";
        case UT_BIND_DECODED: return "decoded";
        case UT_BIND_STRUCTURAL: return "structural";
        default: return "literal";
    }
}

// The size and the PE timestamp of one loaded module, FOR THE RECORD ONLY. Nothing in the mod
// compares these against anything: a version gate is exactly what the capability checks replace.
void sayModule(const wchar_t* name) {
    HMODULE m = name ? GetModuleHandleW(name) : GetModuleHandleW(nullptr);
    if (!m) {
        logI("module %S: not loaded", name ? name : L"TQ.exe");
        return;
    }
    const unsigned char* base = (const unsigned char*)m;
    unsigned long stamp = 0, image = 0;
    __try {
        const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
        if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
            const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)(base + dos->e_lfanew);
            if (nt->Signature == IMAGE_NT_SIGNATURE) {
                stamp = nt->FileHeader.TimeDateStamp;
                image = nt->OptionalHeader.SizeOfImage;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        stamp = 0;
        image = 0;
    }
    wchar_t path[MAX_PATH] = {0};
    GetModuleFileNameW(m, path, MAX_PATH);
    LARGE_INTEGER fsize;
    fsize.QuadPart = 0;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        GetFileSizeEx(f, &fsize);
        CloseHandle(f);
    }
    logI("module %S: %lld bytes on disk, image 0x%lX, PE timestamp 0x%08lX (for the record - "
         "nothing is gated on it)",
         name ? name : L"TQ.exe", (long long)fsize.QuadPart, image, stamp);
}

// WHICH WINDOWS THIS IS. ntdll exports wine_get_version only on Wine (Proton, a Steam Deck,
// CrossOver); on Windows the name is simply absent and this logs nothing. Nothing is gated on it -
// it exists so a report from a player who runs the game through Wine says so in its first lines,
// where the runtime differences that matter (the C++ exception tables, the fault dispatch) are
// otherwise invisible.
void sayHost() {
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return;
    typedef const char*(__cdecl * PfnWineGetVersion)(void);
    typedef void(__cdecl * PfnWineGetHostVersion)(const char**, const char**);
    const PfnWineGetVersion getVersion =
        (PfnWineGetVersion)GetProcAddress(ntdll, "wine_get_version");
    if (!getVersion) return;
    const char* version = nullptr;
    const char* sysname = nullptr;
    const char* release = nullptr;
    __try {
        version = getVersion();
        const PfnWineGetHostVersion getHost =
            (PfnWineGetHostVersion)GetProcAddress(ntdll, "wine_get_host_version");
        if (getHost) getHost(&sysname, &release);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        version = nullptr;
    }
    logI("host: WINE %s on %s %s - not Windows (for the record - nothing is gated on it)",
         version ? version : "(version unreadable)", sysname ? sysname : "?",
         release ? release : "?");
}

// `per` counts the confirmed rows per class, `adv` the ADVISORY rows per class (declared, whether
// or not they held), `advFailed` the advisory rows that did not hold.
void countClasses(int* per, int* adv, int* advFailed) {
    for (int c = 0; c < UT_BIND_CLASS_COUNT; ++c) per[c] = adv[c] = 0;
    per[UT_BIND_EXPORT] = g_exportCount;
    *advFailed = 0;
    for (int i = 0; i < kRowCount; ++i) {
        if (kRows[i].gate == UT_BIND_ADVISORY) {
            adv[kRows[i].cls]++;
            if (g_state[i].reported && !g_state[i].ok) ++*advFailed;
        }
        if (kRows[i].phase == UT_BIND_LATE && !g_state[i].reported) continue;
        if (!g_state[i].ok) continue;
        per[kRows[i].cls]++;
    }
}

// "13 decoded (3 advisory)" - the advisory count is appended only where there is one.
void classCount(char* out, size_t cap, int n, int adv, const char* what) {
    if (adv > 0) {
        _snprintf_s(out, cap, _TRUNCATE, "%d %s (%d advisory)", n, what, adv);
    } else {
        _snprintf_s(out, cap, _TRUNCATE, "%d %s", n, what);
    }
}

void formatSummary(bool failed, int lateOutstanding) {
    int per[UT_BIND_CLASS_COUNT], adv[UT_BIND_CLASS_COUNT], advFailed = 0;
    countClasses(per, adv, &advFailed);
    char dec[48], str[48], lit[48], tail[64];
    classCount(dec, sizeof(dec), per[UT_BIND_DECODED], adv[UT_BIND_DECODED], "decoded");
    classCount(str, sizeof(str), per[UT_BIND_STRUCTURAL], adv[UT_BIND_STRUCTURAL], "structural");
    classCount(lit, sizeof(lit), per[UT_BIND_LITERAL], adv[UT_BIND_LITERAL], "literal");
    if (failed) {
        _snprintf_s(tail, sizeof(tail), _TRUNCATE, "%s", "NOT confirmed");
    } else if (advFailed > 0) {
        _snprintf_s(tail, sizeof(tail), _TRUNCATE, "confirmed except %d advisory (WARN above)",
                    advFailed);
    } else {
        _snprintf_s(tail, sizeof(tail), _TRUNCATE, "%s", "all confirmed");
    }
    _snprintf_s(g_summary, sizeof(g_summary), _TRUNCATE,
                "bindings: %d by export, %d by signature, %s, %s, %s - %s%s", per[UT_BIND_EXPORT],
                per[UT_BIND_SIGNATURE], dec, str, lit, tail,
                lateOutstanding ? ", late bindings pending (game thread)" : "");
}

}  // namespace

const UtBindPattern kUtSigTransferPage = {"exe.transferPageAccessor", kSigTransferPageBytes,
                                          kSigTransferPageMask, sizeof(kSigTransferPageBytes), 1,
                                          0xC2F50};
const UtBindPattern kUtSigStreamOut = {"exe.stashStreamOut", kSigStreamOutBytes, kSigStreamOutMask,
                                       sizeof(kSigStreamOutBytes), 1, 0xBF990};
const UtBindPattern kUtSigRightClick = {"exe.rightClickUnderPoint", kSigRightClickBytes,
                                        kSigRightClickMask, sizeof(kSigRightClickBytes), 1, 0xBFD10};
const UtBindPattern kUtSigHeldPickup = {"exe.heldPickupUnderPoint", kSigHeldPickupBytes,
                                        kSigHeldPickupMask, sizeof(kSigHeldPickupBytes), 1, 0xBEC00};
const UtBindPattern kUtSigLeftClick = {"exe.leftClickUnderPoint", kSigLeftClickBytes,
                                       kSigLeftClickMask, sizeof(kSigLeftClickBytes), 1, 0xC006D};
const UtBindPattern kUtSigPageDraw = {"exe.transferPageDraw", kSigPageDrawBytes, kSigPageDrawMask,
                                      sizeof(kSigPageDrawBytes), 1, 0xC31A0};
static_assert(sizeof(kSigPageDrawBytes) == 185 && sizeof(kSigPageDrawMask) == 185, "185 bytes");
const UtBindPattern kUtSigItemBackground = {"exe.itemBackground", kSigItemBackgroundBytes,
                                            kSigItemBackgroundMask, sizeof(kSigItemBackgroundBytes), 1,
                                            0x10A9B0};
static_assert(sizeof(kSigItemBackgroundBytes) == 166 && sizeof(kSigItemBackgroundMask) == 166,
              "the whole function, 0x10A9B0 .. its ret 0x18");
const UtBindPattern kUtSigLeftHandler = {"exe.leftClickHandler", kSigLeftHandlerBytes,
                                         kSigLeftHandlerMask, sizeof(kSigLeftHandlerBytes), 1,
                                         0xBFED0};
static_assert(sizeof(kSigLeftHandlerBytes) == 413 && sizeof(kSigLeftHandlerMask) == 413,
              "the handler entry pattern ends where the left-click pattern begins (match + 413)");
static_assert(sizeof(kSigLeftClickBytes) == 97 && sizeof(kSigLeftClickMask) == 97,
              "the left-click pattern ends at the return address (match + 97)");
static_assert(sizeof(kSigRightClickBytes) == 197 && sizeof(kSigRightClickMask) == 197,
              "the right-click pattern ends at the return address (match + 197)");
static_assert(sizeof(kSigHeldPickupBytes) == 111 && sizeof(kSigHeldPickupMask) == 111,
              "the held pick-up pattern ends at the return address (match + 111)");
static_assert(sizeof(kSigTransferPageBytes) == sizeof(kSigTransferPageMask), "mask length");
static_assert(sizeof(kSigStreamOutBytes) == sizeof(kSigStreamOutMask), "mask length");

void bindingsNote(const char* name, unsigned long long value, bool ok, const char* why) {
    seed();
    const int i = indexOf(name);
    if (i < 0) {
        // A name that is not in the table is a mistake in the mod, not in the game. Say it once
        // and loudly rather than counting a binding nobody declared.
        logW("bindings: \"%s\" reported an outcome but is not in the table", name ? name : "(null)");
        return;
    }
    g_state[i].value = value;
    g_state[i].ok = ok;
    g_state[i].reported = true;
    _snprintf_s(g_state[i].why, sizeof(g_state[i].why), _TRUNCATE, "%s",
                why && *why ? why : kRows[i].confirm);
}

bool bindingsGate(int resolvedExports, int missingExports) {
    seed();
    g_exportCount = resolvedExports > 0 ? resolvedExports : 0;

    sayModule(nullptr);
    sayModule(L"Game.dll");
    sayModule(L"Engine.dll");
    sayHost();

    const char* failName = nullptr;
    const char* failWhy = nullptr;
    int lateOutstanding = 0;
    for (int i = 0; i < kRowCount; ++i) {
        if (kRows[i].phase == UT_BIND_LATE) {
            if (!g_state[i].reported) ++lateOutstanding;
            continue;
        }
        if (g_state[i].reported && g_state[i].ok) continue;
        if (kRows[i].gate == UT_BIND_ADVISORY) {
            // Nothing reads this row, so it may not switch the mod off: one WARN, said once.
            if (!g_state[i].warned) {
                g_state[i].warned = true;
                logW("bindings: the advisory binding \"%s\" could not be confirmed - expected %s. "
                     "Nothing in the mod reads it, so the mod stays ON.",
                     kRows[i].name,
                     g_state[i].reported ? g_state[i].why : "it never reported an outcome");
            }
            continue;
        }
        if (!failName) {
            failName = kRows[i].name;
            failWhy = g_state[i].reported ? g_state[i].why : "it never reported an outcome";
        }
    }
    if (missingExports > 0 && !failName) {
        failName = "(required exports)";
        failWhy = "every required symbol must resolve by name";
    }
    g_failName = failName ? failName : "";

    formatSummary(failName != nullptr, lateOutstanding);
    logI("%s", g_summary);

    if (logWants(UT_LOG_DEBUG)) {
        for (int i = 0; i < kRowCount; ++i) {
            logD("  binding %-26s %-10s %-5s %-8s value=0x%llX  how: %s  confirmed by: %s",
                 kRows[i].name, className(kRows[i].cls),
                 kRows[i].phase == UT_BIND_LATE ? "late" : "early",
                 kRows[i].gate == UT_BIND_ADVISORY ? "advisory" : "critical", g_state[i].value,
                 kRows[i].how,
                 g_state[i].reported ? g_state[i].why : "NOT REPORTED YET");
        }
    }

    if (failName) {
        logE("the mod is OFF: the binding \"%s\" could not be confirmed - expected %s. Nothing is "
             "hooked, no tab is drawn, no deposit is taken: this game is not the shape this build "
             "knows and a half-bound mod must never touch a save.",
             failName, failWhy ? failWhy : "(no reason recorded)");
        return false;
    }
    return true;
}

void bindingsLateReport() {
    seed();
    for (int i = 0; i < kRowCount; ++i) {
        if (kRows[i].phase != UT_BIND_LATE) continue;
        if (!g_state[i].reported) return;  // still waiting for the rest
        if (!g_state[i].ok) {
            if (InterlockedExchange(&g_lateSaid, 1)) return;
            logE("bindings: the late binding \"%s\" failed - expected %s. Its route is OFF for this "
                 "session; the hooks that were installed before it could be read stay "
                 "installed and keep passing straight through.",
                 kRows[i].name, g_state[i].why[0] ? g_state[i].why : kRows[i].confirm);
            return;
        }
    }
    if (InterlockedExchange(&g_lateSaid, 1)) return;
    formatSummary(false, 0);
    logI("%s", g_summary);
}

const char* bindingsSummary() { return g_summary; }

const char* bindingsGateFailure() { return g_failName; }

int bindingsRowCount() { return kRowCount; }

bool bindingsRowAt(int i, const char** name, int* cls, int* phase, int* gate) {
    if (i < 0 || i >= kRowCount) return false;
    if (name) *name = kRows[i].name;
    if (cls) *cls = (int)kRows[i].cls;
    if (phase) *phase = (int)kRows[i].phase;
    if (gate) *gate = (int)kRows[i].gate;
    return true;
}

bool bindingsRowOk(const char* name) {
    seed();
    const int i = indexOf(name);
    return i >= 0 && g_state[i].reported && g_state[i].ok;
}

}  // namespace ut
