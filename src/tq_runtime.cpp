#include "tq_runtime.h"

#include <stdio.h>
#include <string.h>

#include "ut_bindings.h"
#include "ut_log.h"

namespace ut {

TqRuntime g_tq;

namespace {

// ---- modules ------------------------------------------------------------------------------------
// The image range of each module the mod knows, taken once by the worker (GetModuleHandle only -
// never LoadLibrary), so fmtAddr can name an address from any thread without the loader lock.
struct ModRange {
    const char* name;
    const unsigned char* base;
    size_t size;
};
ModRange g_ranges[4] = {{"TQ.exe", nullptr, 0},
                        {"Engine.dll", nullptr, 0},
                        {"Game.dll", nullptr, 0},
                        {"uniquetab.asi", nullptr, 0}};

const IMAGE_NT_HEADERS32* ntOf(HMODULE m) {
    if (!m) return nullptr;
    const unsigned char* base = (const unsigned char*)m;
    IMAGE_DOS_HEADER dos;
    if (!safeRead(base, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    const IMAGE_NT_HEADERS32* nt = (const IMAGE_NT_HEADERS32*)(base + dos.e_lfanew);
    IMAGE_NT_HEADERS32 copy;
    if (!safeRead(nt, &copy, sizeof(copy)) || copy.Signature != IMAGE_NT_SIGNATURE) return nullptr;
    if (copy.FileHeader.Machine != IMAGE_FILE_MACHINE_I386) return nullptr;
    return nt;
}

size_t imageSize(HMODULE m) {
    const IMAGE_NT_HEADERS32* nt = ntOf(m);
    IMAGE_NT_HEADERS32 h;
    return (nt && safeRead(nt, &h, sizeof(h))) ? h.OptionalHeader.SizeOfImage : 0;
}

bool inModuleImage(HMODULE m, const void* p) {
    const size_t size = imageSize(m);
    const unsigned char* base = (const unsigned char*)m;
    return size && (const unsigned char*)p >= base && (const unsigned char*)p < base + size;
}

// The first executable section of a loaded module (.text), or false.
bool textRange(HMODULE m, const unsigned char** lo, const unsigned char** hi) {
    const IMAGE_NT_HEADERS32* nt = ntOf(m);
    IMAGE_NT_HEADERS32 h;
    if (!nt || !safeRead(nt, &h, sizeof(h))) return false;
    // IMAGE_FIRST_SECTION, computed from the guarded copy's own fields.
    const IMAGE_SECTION_HEADER* sec =
        (const IMAGE_SECTION_HEADER*)((const unsigned char*)nt +
                                      FIELD_OFFSET(IMAGE_NT_HEADERS32, OptionalHeader) +
                                      h.FileHeader.SizeOfOptionalHeader);
    for (WORD i = 0; i < h.FileHeader.NumberOfSections && i < 96; ++i) {
        IMAGE_SECTION_HEADER s;
        if (!safeRead(sec + i, &s, sizeof(s))) return false;
        if (!(s.Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        *lo = (const unsigned char*)m + s.VirtualAddress;
        *hi = *lo + s.Misc.VirtualSize;
        return true;
    }
    return false;
}

bool inModuleText(HMODULE m, const void* p) {
    const unsigned char *lo = nullptr, *hi = nullptr;
    return textRange(m, &lo, &hi) && (const unsigned char*)p >= lo && (const unsigned char*)p < hi;
}

HMODULE moduleByName(const char* name) {
    if (!_stricmp(name, "Engine.dll")) return g_tq.engineDll;
    if (!_stricmp(name, "Game.dll")) return g_tq.gameDll;
    if (!_stricmp(name, "MSVCR110.dll")) return g_tq.crtDll;
    return nullptr;
}

// Resolve one export, CONFIRM it, log the result, and count a miss when the symbol is required.
// Confirmed = a data export inside its module's image and readable; a code export inside its
// module's executable section whose first byte is not padding.
void* resolveOne(const char* dllName, const char* mangled, const char* pretty, bool required,
                 bool data) {
    HMODULE mod = moduleByName(dllName);
    void* p = mod ? (void*)GetProcAddress(mod, mangled) : nullptr;
    const char* why = mod ? "not exported" : "module not loaded";
    if (p) {
        unsigned char first = 0;
        if (data) {
            if (!inModuleImage(mod, p) || !readable(p, 4)) {
                why = "data outside its image or unreadable";
                p = nullptr;
            }
        } else if (!inModuleText(mod, p) || !safeRead(p, &first, 1) || first == 0xCC ||
                   first == 0x00) {
            why = "code outside the executable section, or padding";
            p = nullptr;
        }
    }
    if (p) {
        // Counted so the bindings gate can say how many of the mod's facts are name-resolved -
        // the class that survives any patch which keeps the names, i.e. every minor one.
        ++g_tq.resolvedByName;
        logT("  %-14s %-38s -> %p", dllName, pretty, p);
    } else if (required) {
        logE("  %-14s %-38s -> MISSING (REQUIRED): %s", dllName, pretty, why);
        ++g_tq.missingRequired;
    } else {
        logD("  %-14s %-38s -> MISSING: %s", dllName, pretty, why);
    }
    return p;
}

#define TQ_RESOLVE(field, type, X, pretty) \
    g_tq.field = (type)resolveOne(X##_MOD, X, pretty, X##_REQ != 0, X##_CC == TQCC_DATA)

// ---- the SEH-guard depth, in DYNAMIC TLS ------------------------------------------------------
// TlsAlloc, not __declspec(thread): the loader may map this DLL long after the process started.
// TlsGetValue resets the thread's last-error value; every helper keeps it as the engine left it.
volatile LONG g_tls = (LONG)TLS_OUT_OF_INDEXES;

DWORD tlsIndex() {
    const LONG v = g_tls;
    if (v != (LONG)TLS_OUT_OF_INDEXES) return (DWORD)v;
    const DWORD n = TlsAlloc();
    if (n == TLS_OUT_OF_INDEXES) return n;
    if (InterlockedCompareExchange(&g_tls, (LONG)n, (LONG)TLS_OUT_OF_INDEXES) !=
        (LONG)TLS_OUT_OF_INDEXES) {
        TlsFree(n);
    }
    return (DWORD)g_tls;
}

void guardEnter() {
    const DWORD e = GetLastError();
    const DWORD t = tlsIndex();
    if (t != TLS_OUT_OF_INDEXES) TlsSetValue(t, (void*)((uintptr_t)TlsGetValue(t) + 1));
    SetLastError(e);
}

void guardLeave() {
    const DWORD e = GetLastError();
    const DWORD t = tlsIndex();
    if (t != TLS_OUT_OF_INDEXES) {
        const uintptr_t v = (uintptr_t)TlsGetValue(t);
        TlsSetValue(t, (void*)(v ? v - 1 : 0));
    }
    SetLastError(e);
}

// The first 8 bytes of an exported function, copied under the guard. False when unreadable.
bool head8(const void* fn, unsigned char* out) {
    memset(out, 0, 8);
    return fn && safeRead(fn, out, 8);
}

unsigned decodeWith(const void* fn, unsigned (*decoder)(const unsigned char*)) {
    unsigned char c[8];
    return head8(fn, c) ? decoder(c) : 0;
}

// Up to `cap` slots of a vftable in Game.dll, stopping at the first pointer that is not code in
// Game.dll (the end of the table). Returns how many were read.
int readVtable(const void* vt, unsigned* out, int cap) {
    int n = 0;
    for (; n < cap; ++n) {
        unsigned p = 0;
        if (!safeRead((const unsigned char*)vt + 4 * n, &p, 4)) break;
        if (!inModuleText(g_tq.gameDll, (const void*)(uintptr_t)p)) break;
        out[n] = p;
    }
    return n;
}

void noteSlot(const char* row, const void* vt, const void* fn) {
    static unsigned slots[512];
    char why[160];
    int off = -1;
    if (vt && fn) {
        const int n = readVtable(vt, slots, 512);
        off = utVtableSlotOf(slots, n, (unsigned)(uintptr_t)fn);
    }
    _snprintf_s(why, sizeof(why), _TRUNCATE, "exactly one slot holds it (found %s)",
                off == -2 ? "SEVERAL - a folded body" : off < 0 ? "none" : "one");
    bindingsNote(row, off >= 0 ? (unsigned long long)off : 0, off >= 0 && (off & 3) == 0, why);
    if (off >= 0) {
        if (!strcmp(row, "item.replicaSlot")) g_tq.itemSlotReplica = off;
        if (!strcmp(row, "item.classificationSlot")) g_tq.itemSlotClass = off;
        if (!strcmp(row, "item.typeSlot")) g_tq.itemSlotType = off;
        if (!strcmp(row, "item.stackSlot")) g_tq.itemSlotStack = off;
    }
}

// what the four matched signatures say about the code around them (1a-1d).
// Every value is read out of the MATCHED bytes, and every indirect call the view relies on is
// confirmed by reading the IAT slot it goes through: it must hold the export the mod bound.
bool iatSlotHolds(const unsigned char* callFF15, const void* want) {
    unsigned char c[6];
    unsigned slot = 0, held = 0;
    if (!want || !safeRead(callFF15, c, 6) || c[0] != 0xFF || c[1] != 0x15) return false;
    memcpy(&slot, c + 2, 4);
    return safeRead((const void*)(uintptr_t)slot, &held, 4) && held == (unsigned)(uintptr_t)want;
}

void decodeViewBindings() {
    char why[200];
    const unsigned char* exe = (const unsigned char*)g_tq.exe;
    // 1a: the accessor - the getter call at +0x0D, `8D BB d32` at +0x21, `89 4F d8` at +0x2F.
    {
        unsigned ui = 0, sack = 0;
        bool ok = false;
        if (g_tq.sigTransferPageRva) {
            const unsigned char* f = exe + g_tq.sigTransferPageRva;
            unsigned char b[0x32];
            if (safeRead(f, b, sizeof(b))) {
                ui = utDecodeLeaEdiEbx32(b + 0x21);
                sack = utDecodeMovEdi8Ecx(b + 0x2F);
                ok = utFieldPlausible(ui, 0x1000) && utFieldPlausible(sack, 0x100) &&
                     iatSlotHolds(f + 0x0D, (const void*)g_tq.GameGetPlayerTransfer);
            }
        }
        _snprintf_s(why, sizeof(why), _TRUNCATE,
                    "the accessor stores the getter's result at [this+0x%X+0x%X]; its FF 15 call "
                    "goes through the IAT slot of GameEngine::GetPlayerTransfer",
                    ui, sack);
        bindingsNote("page.cachedSack", ui + sack, ok, why);
        if (ok) {
            g_tq.pageUiOff = ui;
            g_tq.pageSackOff = sack;
        }
    }
    // 1c / 1d: the pattern ends WITH the `FF 15` call: the return address is match + len.
    struct {
        const char* row;
        unsigned rva;
        const UtBindPattern* p;
        const void** out;
    } rs[3] = {{"exe.rightClickUnderPoint.ret", g_tq.sigRightClickRva, &kUtSigRightClick,
                &g_tq.retRightClick},
               {"exe.heldPickupUnderPoint.ret", g_tq.sigHeldPickupRva, &kUtSigHeldPickup,
                &g_tq.retHeldPickup},
               {"exe.leftClickUnderPoint.ret", g_tq.sigLeftClickRva, &kUtSigLeftClick,
                &g_tq.retLeftClick}};
    for (int i = 0; i < 3; ++i) {
        const unsigned char* call =
            rs[i].rva ? exe + rs[i].rva + (unsigned)rs[i].p->len - 6 : nullptr;
        const bool ok = call && iatSlotHolds(call, (const void*)g_tq.SackGetItemUnderPoint);
        _snprintf_s(why, sizeof(why), _TRUNCATE,
                    "the pattern's last 6 bytes are `FF 15` through the IAT slot of "
                    "InventorySack::GetItemUnderPoint (return address TQ.exe+0x%X)",
                    rs[i].rva ? rs[i].rva + (unsigned)rs[i].p->len : 0);
        bindingsNote(rs[i].row, rs[i].rva ? rs[i].rva + rs[i].p->len : 0, ok, why);
        if (ok) *rs[i].out = call + 6;
    }
    // the page mouse handler's frame. The entry pattern must end
    // exactly where the left-click pattern begins, so the bytes from the entry to the call are all
    // fixed; the distance is then read out of them: `83 EC d8` at +0x14 (the locals) plus 0x28
    // (3 SEH pushes, ebx/esi/edi, the cookie, the Vec2 by value, the return address).
    {
        const unsigned char* f = g_tq.sigLeftHandlerRva ? exe + g_tq.sigLeftHandlerRva : nullptr;
        const bool contiguous = f && g_tq.sigLeftClickRva &&
                                g_tq.sigLeftHandlerRva + (unsigned)kUtSigLeftHandler.len ==
                                    g_tq.sigLeftClickRva;
        static const unsigned char kPro[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8};
        const bool shape = contiguous && memcmp(f, kPro, sizeof(kPro)) == 0 && f[0x14] == 0x83 &&
                           f[0x15] == 0xEC && f[0x69] == 0x8B && f[0x6A] == 0x45 &&
                           f[0x6B] == 0x10 && f[0x59] == 0x8B && f[0x5A] == 0x45 &&
                           f[0x5B] == 0x14;
        const unsigned dist = shape ? 0x28u + f[0x16] : 0;
        const bool ok = shape && dist >= 0x28 && dist < 0x200 && g_tq.retLeftClick;
        _snprintf_s(why, sizeof(why), _TRUNCATE,
                    "entry at TQ.exe+0x%X, contiguous with the left-click pattern %d, prologue "
                    "and argument reads %d: EBP - return slot = 0x%X + (EBP & 7)",
                    g_tq.sigLeftHandlerRva, contiguous ? 1 : 0, shape ? 1 : 0, dist);
        bindingsNote("exe.leftClickHandler.frame", dist, ok, why);
        if (ok) g_tq.leftFrameDist = dist;
    }
}

// the drag-drop deposit's disposal, decoded out of the engine's own
// CursorHandlerItemMove::PrimaryTransferActivate. After its add the engine runs
//     8B 43 d8            mov eax,[ebx+d8]          ; the handler's player
//     FF B0 d32           push [eax+d32]            ; its controller id
//     FF D6 / 8B C8 / E8  ObjectManager::Get + the ControllerPlayer lookup
//     ... E8 rel32        ControllerCharacter::SendRemoveItemFromInventory(item id)
// The row is confirmed when that shape is found inside the export's first 0x200 bytes, the last
// call's target IS the SendRemoveItemFromInventory export, and d32 equals the displacement
// Character::GetControllerId reads (`8B 81 d32 C3`). The mod then makes the same call on the same
// controller - never an engine-internal function.
void decodeDisposal() {
    char why[256];
    const unsigned char* f = (const unsigned char*)g_tq.CursorPrimaryTransferActivate;
    const unsigned char* g = (const unsigned char*)g_tq.CharGetControllerId;
    const unsigned char* send = (const unsigned char*)g_tq.CtrlSendRemoveItem;
    unsigned d8 = 0, d32 = 0, getter = 0;
    bool shape = false, getterOk = false;
    if (f && g && send && readable(f, 0x220) && readable(g, 8)) {
        guardEnter();
        __try {
            getterOk = g[0] == 0x8B && g[1] == 0x81 && g[6] == 0xC3;
            if (getterOk) memcpy(&getter, g + 2, 4);
            for (unsigned i = 0; i + 9 < 0x200 && !shape; ++i) {
                if (f[i] != 0x8B || f[i + 1] != 0x43 || f[i + 3] != 0xFF || f[i + 4] != 0xB0)
                    continue;
                for (unsigned k = i + 9; k + 5 < i + 0x40 && k + 5 < 0x220; ++k) {
                    if (f[k] != 0xE8) continue;
                    int rel = 0;
                    memcpy(&rel, f + k + 1, 4);
                    if (f + k + 5 + rel == send) {
                        d8 = f[i + 2];
                        memcpy(&d32, f + i + 5, 4);
                        shape = true;
                        break;
                    }
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            shape = false;
        }
        guardLeave();
    }
    const bool ok = shape && getterOk && d32 == getter && d8 && d8 < 0x100 && d32 < 0x10000;
    _snprintf_s(why, sizeof(why), _TRUNCATE,
                "PrimaryTransferActivate's disposal: player [handler+0x%X], controller id "
                "[player+0x%X] (GetControllerId reads +0x%X), the call to "
                "SendRemoveItemFromInventory %s",
                d8, d32, getter, shape ? "found" : "NOT found");
    bindingsNote("cursor.disposal", ok ? d32 : 0, ok, why);
    if (ok) {
        g_tq.cursorPlayerOff = d8;
        g_tq.ctrlIdOff = d32;
    }
}

// the quick-move's PRIMARY AddItemToTransfer(id,bool) call. TQ.exe loads
// the IAT slot once (`8B 3D <slot>` at 0x107A61) and calls it twice through EDI: the primary call
// (`FF D7` at 0x107A6A, then `84 C0 0F 84`: its result decides) and the stacked-extras loop
// (0x107AED, result ignored). While the view is ON only the primary's return address is accepted.
void decodeQuickMove() {
    char why[256];
    const unsigned char *lo = nullptr, *hi = nullptr;
    UtQuickSite sites[16];
    int n = -1, hits = 0;
    unsigned long ret = 0;
    const void* want = (const void*)g_tq.GameAddItemToTransferId;
    if (want && textRange(g_tq.exe, &lo, &hi) && readable(lo, (size_t)(hi - lo))) {
        guardEnter();
        __try {
            n = utQuickMoveSites(lo, hi, sites, 16);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            n = -1;
        }
        guardLeave();
        for (int i = 0; i < n && i < 16; ++i) {
            const void* held = nullptr;
            if (safeRead((const void*)(uintptr_t)sites[i].slot, &held, sizeof(held)) && held == want) {
                ++hits;
                ret = sites[i].ret;
            }
        }
    }
    const bool ok = n >= 0 && n <= 16 && hits == 1;
    const unsigned rva = ok ? (unsigned)((lo + ret) - (const unsigned char*)g_tq.exe) : 0;
    _snprintf_s(why, sizeof(why), _TRUNCATE,
                "exactly one `8B 3D <IAT slot of GameEngine::AddItemToTransfer(id,bool)>` followed "
                "within 0x10 bytes by `FF D7 84 C0 0F 84` (found %d of %d such shapes; return address "
                "TQ.exe+0x%X)",
                hits, n, rva);
    bindingsNote("exe.quickMoveAdd.ret", rva, ok, why);
    if (ok) g_tq.retQuickMove = lo + ret;
}

// One counted signature over TQ.exe's .text. `rvaOut` = the match, module-relative.
void scanSignature(const UtBindPattern& p, unsigned* rvaOut) {
    const unsigned char *lo = nullptr, *hi = nullptr;
    const unsigned char* hits[4] = {nullptr, nullptr, nullptr, nullptr};
    int n = -1;
    if (textRange(g_tq.exe, &lo, &hi) && readable(lo, (size_t)(hi - lo))) {
        guardEnter();
        __try {
            n = utBindScan(lo, hi, p, hits, 4);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            n = -1;
        }
        guardLeave();
    }
    const unsigned rva = (n >= 1 && hits[0]) ? (unsigned)(hits[0] - (const unsigned char*)g_tq.exe)
                                             : 0;
    char why[160];
    if (n < 0) {
        _snprintf_s(why, sizeof(why), _TRUNCATE, "%s", "exactly one match (TQ.exe .text unreadable)");
    } else {
        _snprintf_s(why, sizeof(why), _TRUNCATE, "exactly one match (found %d%s)", n,
                    n > 4 ? "+" : "");
    }
    const bool ok = n == p.expectHits;
    bindingsNote(p.name, rva, ok, why);
    if (ok) {
        *rvaOut = rva;
        logI("signature %s: 1 match at TQ.exe+0x%X (%u bytes)", p.name, rva, (unsigned)p.len);
    }
}

}  // namespace

void utGuardEnter() { guardEnter(); }
void utGuardLeave() { guardLeave(); }

// ---- guarded memory access -----------------------------------------------------------------------
bool readable(const void* p, size_t n) {
    if (!p || (uintptr_t)p < 0x10000) return false;
    const uintptr_t lo = (uintptr_t)p;
    const uintptr_t hi = lo + (n ? n : 1);
    if (hi < lo) return false;
    uintptr_t at = lo;
    for (int guard = 0; guard < 64 && at < hi; ++guard) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery((const void*)at, &mbi, sizeof(mbi))) return false;
        if (mbi.State != MEM_COMMIT) return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
        const DWORD rd = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                         PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        if (!(mbi.Protect & rd)) return false;
        at = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
    }
    return at >= hi;
}

bool safeRead(const void* p, void* out, size_t n) {
    if (!readable(p, n)) return false;
    bool ok = false;
    guardEnter();
    __try {
        memcpy(out, p, n);
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    guardLeave();
    return ok;
}

int guardDepth() {
    const LONG t = g_tls;
    if (t == (LONG)TLS_OUT_OF_INDEXES) return 0;
    const DWORD e = GetLastError();
    const int d = (int)(uintptr_t)TlsGetValue((DWORD)t);
    SetLastError(e);
    return d;
}

TqGameEngine* gameEngine() {
    TqGameEngine* ge = nullptr;
    if (g_tq.ppGameEngine) safeRead(g_tq.ppGameEngine, &ge, sizeof(ge));
    return ge;
}

TqEngine* engine() {
    TqEngine* e = nullptr;
    if (g_tq.ppEngine) safeRead(g_tq.ppEngine, &e, sizeof(e));
    return e;
}

unsigned cursorItemId(const void* cursor) {
    unsigned id = 0;
    if (cursor && g_tq.cursorIdOff) safeRead((const unsigned char*)cursor + g_tq.cursorIdOff, &id, 4);
    return id;
}

bool safeStdString(const void* s, char* out, size_t cap) {
    if (!cap) return false;
    out[0] = 0;
    TqStdString v;
    if (!safeRead(s, &v, sizeof(v))) return false;
    if (v.size > (1u << 20) || v.res < 15 || v.size > v.res) return false;
    const size_t n = v.size < cap - 1 ? v.size : cap - 1;
    if (v.res < 16) {
        memcpy(out, v.buf, n);
    } else if (!safeRead(v.ptr, out, n)) {
        return false;
    }
    out[n] = 0;
    for (size_t i = 0; i < n; ++i) {
        if ((unsigned char)out[i] < 0x20 || (unsigned char)out[i] >= 0x7f) out[i] = '?';
    }
    return true;
}

void fmtAddr(const void* a, char* out, size_t cap) {
    const unsigned char* p = (const unsigned char*)a;
    for (int i = 0; i < 4; ++i) {
        const ModRange& r = g_ranges[i];
        if (r.base && p >= r.base && p < r.base + r.size) {
            _snprintf_s(out, cap, _TRUNCATE, "%s+0x%X", r.name, (unsigned)(p - r.base));
            return;
        }
    }
    _snprintf_s(out, cap, _TRUNCATE, "%p", a);
}

void logFault(const char* what, const EXCEPTION_RECORD* rec, DWORD tid) {
    if (!rec) return;
    char where[64];
    fmtAddr(rec->ExceptionAddress, where, sizeof(where));
    const ULONG_PTR kind = rec->NumberParameters >= 1 ? rec->ExceptionInformation[0] : 0;
    const ULONG_PTR addr = rec->NumberParameters >= 2 ? rec->ExceptionInformation[1] : 0;
    logD("watchdog: %s: code 0x%08lX at %s, %s of %p, thread %lu", what, rec->ExceptionCode, where,
         kind == 0 ? "read" : kind == 1 ? "write" : kind == 8 ? "execute" : "access",
         (void*)addr, tid);
}

bool waitForGameModules(DWORD timeoutMs) {
    const DWORD start = GetTickCount();
    for (;;) {
        HMODULE e = GetModuleHandleA("Engine.dll");
        HMODULE g = GetModuleHandleA("Game.dll");
        if (e && g) {
            g_tq.engineDll = e;
            g_tq.gameDll = g;
            g_tq.exe = GetModuleHandleA(nullptr);
            // The game's CRT: TQ.exe imports it, so it is mapped already. Looked up, never loaded.
            g_tq.crtDll = GetModuleHandleA("MSVCR110.dll");
            logD("modules ready after %lu ms: Engine.dll=%p Game.dll=%p MSVCR110.dll=%p",
                 GetTickCount() - start, (void*)e, (void*)g, (void*)g_tq.crtDll);
            return true;
        }
        if (GetTickCount() - start > timeoutMs) {
            logE("GIVING UP after %lu ms: Engine.dll=%p Game.dll=%p (one or both never loaded)",
                 GetTickCount() - start, (void*)e, (void*)g);
            return false;
        }
        Sleep(50);
    }
}

bool resolveExports() {
    g_tq.missingRequired = 0;
    g_tq.resolvedByName = 0;

    // The module ranges for fmtAddr, taken here on the worker (GetModuleHandle only).
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)&g_tq, &self);
    const HMODULE mods[4] = {g_tq.exe, g_tq.engineDll, g_tq.gameDll, self};
    for (int i = 0; i < 4; ++i) {
        g_ranges[i].size = imageSize(mods[i]);
        g_ranges[i].base = g_ranges[i].size ? (const unsigned char*)mods[i] : nullptr;
    }

    logT("resolving exports by name (GetProcAddress):");

    // Engine.dll -- the singleton (a DATA export: the address of the GAME::Engine* variable),
    // the frame and the database.
    TQ_RESOLVE(ppEngine, TqEngine**, TQ_ENGINE_GENGINE, "GAME::gEngine (data)");
    TQ_RESOLVE(EnginePresentSurface, PfnEngine_Void, TQ_ENGINE_PRESENTSURFACE,
               "Engine::PresentSurface");
    TQ_RESOLVE(EngineLoadMainDatabase, PfnEngine_Void, TQ_ENGINE_LOADMAINDATABASE,
               "Engine::LoadMainDatabase");
    TQ_RESOLVE(EngineLoadDatabase, PfnEngine_LoadDatabase, TQ_ENGINE_LOADDATABASE,
               "Engine::LoadDatabase");
    TQ_RESOLVE(EngineGetDatabaseArchiveChecksum, PfnEngine_GetChecksum,
               TQ_ENGINE_GETDATABASEARCHIVECHECKSUM, "Engine::GetDatabaseArchiveChecksum");
    TQ_RESOLVE(EngineHasLoadedCustomDatabase, PfnEngine_Bool, TQ_ENGINE_HASLOADEDCUSTOMDATABASE,
               "Engine::HasLoadedCustomDatabase");
    TQ_RESOLVE(EngineInitializeMod, PfnEngine_InitializeMod, TQ_ENGINE_INITIALIZEMOD,
               "Engine::InitializeMod");
    TQ_RESOLVE(EngineGetGameInfo, PfnEngine_GetGameInfo, TQ_ENGINE_GETGAMEINFO,
               "Engine::GetGameInfo");
    TQ_RESOLVE(EngineIsNetworkClient, PfnEngine_Bool, TQ_ENGINE_ISNETWORKCLIENT,
               "Engine::IsNetworkClient");
    TQ_RESOLVE(EngineIsNetworkServer, PfnEngine_Bool, TQ_ENGINE_ISNETWORKSERVER,
               "Engine::IsNetworkServer");
    TQ_RESOLVE(EngineGetGraphicsEngine, PfnEngine_GetGraphicsEngine, TQ_ENGINE_GETGRAPHICSENGINE,
               "Engine::GetGraphicsEngine");
    TQ_RESOLVE(EngineGetUIScale, PfnEngine_GetUIScale, TQ_ENGINE_GETUISCALE, "Engine::GetUIScale");
    TQ_RESOLVE(GameInfoGetIsMultiPlayer, PfnGameInfo_GetIsMultiPlayer, TQ_GAMEINFO_GETISMULTIPLAYER,
               "GameInfo::GetIsMultiPlayer");
    TQ_RESOLVE(GameInfoGetModName, PfnGameInfo_GetModName, TQ_GAMEINFO_GETMODNAME,
               "GameInfo::GetModName");

    // Engine.dll -- the canvas set (optional in the page comes).
    TQ_RESOLVE(GfxGetCanvas, PfnGfx_GetCanvas, TQ_GFX_GETCANVAS, "GraphicsEngine::GetCanvas");
    TQ_RESOLVE(GfxIsDownsizing, PfnGfx_IsDownsizing, TQ_GFX_ISDOWNSIZING,
               "GraphicsEngine::IsDownsizing");   // the page-draw frame
    TQ_RESOLVE(GfxLoadTexture, PfnGfx_LoadTexture, TQ_GFX_LOADTEXTURE,
               "GraphicsEngine::LoadTexture");
    TQ_RESOLVE(CanvasGetWidth, PfnCanvas_GetInt, TQ_GFX_CANVAS_GETWIDTH, "GraphicsCanvas::GetWidth");
    TQ_RESOLVE(CanvasGetHeight, PfnCanvas_GetInt, TQ_GFX_CANVAS_GETHEIGHT,
               "GraphicsCanvas::GetHeight");
    TQ_RESOLVE(CanvasRenderRect, PfnCanvas_RenderRect, TQ_GFX_CANVAS_RENDERRECT,
               "GraphicsCanvas::RenderRect");
    // the textured overload and the texture's size (the equipment-slot art).
    TQ_RESOLVE(CanvasRenderRectTex, PfnCanvas_RenderRectTex, TQ_GFX_CANVAS_RENDERRECT_TEX,
               "GraphicsCanvas::RenderRect(texture)");
    TQ_RESOLVE(TextureGetWidth, PfnTexture_GetInt, TQ_GFX_TEXTURE_GETWIDTH, "GraphicsTexture::GetWidth");
    TQ_RESOLVE(TextureGetHeight, PfnTexture_GetInt, TQ_GFX_TEXTURE_GETHEIGHT,
               "GraphicsTexture::GetHeight");
    // the gray icons (a directory source for the mod's gray\ folder, the unload).
    TQ_RESOLVE(GfxUnloadTexture, PfnGfx_UnloadTexture, TQ_GFX_UNLOADTEXTURE,
               "GraphicsEngine::UnloadTexture");
    TQ_RESOLVE(EngineGetFileSystem, PfnEngine_GetFileSystem, TQ_ENGINE_GETFILESYSTEM,
               "Engine::GetFileSystem");
    TQ_RESOLVE(FsAddSource, PfnFs_AddSource, TQ_ENGINE_FS_ADDSOURCE, "FileSystem::AddSource");
    TQ_RESOLVE(CanvasRenderTextA, PfnCanvas_RenderTextA, TQ_GFX_CANVAS_RENDERTEXT_A,
               "GraphicsCanvas::RenderText(char)");
    TQ_RESOLVE(CanvasRenderTextW, PfnCanvas_RenderTextW, TQ_GFX_CANVAS_RENDERTEXT_W,
               "GraphicsCanvas::RenderText(wchar_t)");
    TQ_RESOLVE(DisplayHandleKeyEvent, PfnDisplay_HandleKeyEvent, TQ_INPUT_DISPLAY_HANDLEKEYEVENT,
               "Display::HandleKeyEvent");
    TQ_RESOLVE(ButtonEventGetText, PfnButtonEvent_GetText, TQ_INPUT_BUTTONEVENT_GETTEXT,
               "ButtonEvent::GetText");
    TQ_RESOLVE(RenderDeviceGetActiveAPI, PfnRenderDevice_GetActiveAPI, TQ_GFX_RD_GETACTIVEAPI,
               "RenderDevice::GetActiveAPI");
    TQ_RESOLVE(GfxLoadFont, PfnGfx_LoadFont, TQ_GFX_LOADFONT, "GraphicsEngine::LoadFont");

    // Engine.dll -- GAME::Object accessors (Item derives from Object) and the object manager.
    TQ_RESOLVE(ObjectManagerGet, PfnObjectManager_Get, TQ_OBJ_OM_GET, "ObjectManager::Get");
    TQ_RESOLVE(ObjectManagerDestroyObjectEx, PfnObjectManager_DestroyObjectEx,
               TQ_OBJ_OM_DESTROYOBJECTEX, "ObjectManager::DestroyObjectEx");
    TQ_RESOLVE(ObjectGetObjectId, PfnObject_GetObjectId, TQ_OBJ_GETOBJECTID, "Object::GetObjectId");
    TQ_RESOLVE(ObjectGetObjectName, PfnObject_GetObjectName, TQ_OBJ_GETOBJECTNAME,
               "Object::GetObjectName");
    TQ_RESOLVE(LoadTableBinaryGetInt, PfnLoadTable_GetInt, TQ_OBJ_LTB_GETINT,
               "LoadTableBinary::GetInt");
    TQ_RESOLVE(ObjectManagerGetObjectList, PfnObjectManager_GetObjectList, TQ_OBJ_OM_GETOBJECTLIST,
               "ObjectManager::GetObjectList");

    // Game.dll -- GameEngine: the frame, the three stores, the caravan.
    TQ_RESOLVE(ppGameEngine, TqGameEngine**, TQ_GAMEENGINE_GGAMEENGINE, "GAME::gGameEngine (data)");
    TQ_RESOLVE(GameUpdate, PfnGameEngine_Update, TQ_GAMEENGINE_UPDATE, "GameEngine::Update");
    TQ_RESOLVE(GameGetMainPlayer, PfnGameEngine_GetMainPlayer, TQ_GAMEENGINE_GETMAINPLAYER,
               "GameEngine::GetMainPlayer");
    TQ_RESOLVE(GameGetPlayerStash, PfnGameEngine_GetSack, TQ_GAMEENGINE_GETPLAYERSTASH,
               "GameEngine::GetPlayerStash");
    TQ_RESOLVE(GameGetPlayerStashC, PfnGameEngine_GetSack, TQ_GAMEENGINE_GETPLAYERSTASH_C,
               "GameEngine::GetPlayerStash const");
    TQ_RESOLVE(GameGetPlayerTransfer, PfnGameEngine_GetSack, TQ_GAMEENGINE_GETPLAYERTRANSFER,
               "GameEngine::GetPlayerTransfer");
    TQ_RESOLVE(GameGetPlayerTransferC, PfnGameEngine_GetSack, TQ_GAMEENGINE_GETPLAYERTRANSFER_C,
               "GameEngine::GetPlayerTransfer const");
    TQ_RESOLVE(GameGetPlayerRelicVault, PfnGameEngine_GetSack, TQ_GAMEENGINE_GETPLAYERRELICVAULT,
               "GameEngine::GetPlayerRelicVault");
    TQ_RESOLVE(GameGetPlayerRelicVaultC, PfnGameEngine_GetSack,
               TQ_GAMEENGINE_GETPLAYERRELICVAULT_C, "GameEngine::GetPlayerRelicVault const");
    TQ_RESOLVE(GameGetCaravanMode, PfnGameEngine_GetCaravanMode, TQ_GAMEENGINE_GETCARAVANMODE,
               "GameEngine::GetCaravanMode");
    TQ_RESOLVE(GameSetCaravanMode, PfnGameEngine_SetCaravanMode, TQ_GAMEENGINE_SETCARAVANMODE,
               "GameEngine::SetCaravanMode");
    TQ_RESOLVE(GameCaravanGoodbye, PfnGameEngine_Void, TQ_GAMEENGINE_CARAVANGOODBYE,
               "GameEngine::CaravanGoodbye");
    TQ_RESOLVE(GameAddItemToStashId, PfnGameEngine_AddItemId, TQ_GAMEENGINE_ADDITEMTOSTASH_ID,
               "GameEngine::AddItemToStash(id)");
    TQ_RESOLVE(GameAddItemToTransferId, PfnGameEngine_AddItemId,
               TQ_GAMEENGINE_ADDITEMTOTRANSFER_ID, "GameEngine::AddItemToTransfer(id)");
    TQ_RESOLVE(GameAddItemToRelicVaultId, PfnGameEngine_AddItemId,
               TQ_GAMEENGINE_ADDITEMTORELICVAULT_ID, "GameEngine::AdditemToRelicVault(id)");
    TQ_RESOLVE(GameRemoveItemFromStash, PfnGameEngine_RemoveItem, TQ_GAMEENGINE_REMOVEITEMFROMSTASH,
               "GameEngine::RemoveItemFromStash");
    TQ_RESOLVE(GameRemoveItemFromTransfer, PfnGameEngine_RemoveItem,
               TQ_GAMEENGINE_REMOVEITEMFROMTRANSFER, "GameEngine::RemoveItemFromTransfer");
    TQ_RESOLVE(GameRemoveItemFromRelicVault, PfnGameEngine_RemoveItem,
               TQ_GAMEENGINE_REMOVEITEMFROMRELICVAULT, "GameEngine::RemoveItemFromRelicVault");

    // Game.dll -- the caravan NPC and the player.
    TQ_RESOLVE(NpcCaravanOnPlayerInteract, PfnNpcCaravan_OnPlayerInteract,
               TQ_NPC_ONPLAYERINTERACT, "NpcCaravan::OnPlayerInteract");
    TQ_RESOLVE(PlayerIsInMainQuest, PfnPlayer_Bool, TQ_NPC_PLAYER_ISINMAINQUEST,
               "Player::IsInMainQuest");

    // Game.dll -- CursorHandlerItemMove, the drop gate.
    TQ_RESOLVE(CursorVftable, const void*, TQ_CURSOR_VFTABLE, "CursorHandlerItemMove vftable");
    TQ_RESOLVE(CursorGetId, PfnCursor_GetId, TQ_CURSOR_GETID, "CursorHandlerItemMove::GetId");
    TQ_RESOLVE(CursorSetId, PfnCursor_SetId, TQ_CURSOR_SETID, "CursorHandlerItemMove::SetId");
    TQ_RESOLVE(CursorPrimaryStashActivate, PfnCursor_Primary, TQ_CURSOR_PRIMARYSTASHACTIVATE,
               "CursorHandlerItemMove::PrimaryStashActivate");
    TQ_RESOLVE(CursorPrimaryTransferActivate, PfnCursor_Primary, TQ_CURSOR_PRIMARYTRANSFERACTIVATE,
               "CursorHandlerItemMove::PrimaryTransferActivate");
    TQ_RESOLVE(CursorPrimaryRelicVaultActive, PfnCursor_Primary, TQ_CURSOR_PRIMARYRELICVAULTACTIVE,
               "CursorHandlerItemMove::PrimaryRelicVaultActive");
    TQ_RESOLVE(CursorIsStashCapable, PfnCursor_Capable, TQ_CURSOR_ISSTASHCAPABLE,
               "CursorHandlerItemMove::IsStashCapable");
    TQ_RESOLVE(CursorIsTransferCapable, PfnCursor_Capable, TQ_CURSOR_ISTRANSFERCAPABLE,
               "CursorHandlerItemMove::IsTransferCapable");
    TQ_RESOLVE(CursorIsRelicVaultCapable, PfnCursor_Capable, TQ_CURSOR_ISRELICVAULTCAPABLE,
               "CursorHandlerItemMove::IsRelicVaultCapable");

    // Game.dll -- InventorySack and Item: resolved at start-up for the files that call them, never here.
    TQ_RESOLVE(SackAddItem, PfnSack_AddItem, TQ_SACK_ADDITEM, "InventorySack::AddItem(Item*)");
    TQ_RESOLVE(SackAddItemVec, PfnSack_AddItemVec, TQ_SACK_ADDITEM_VEC,
               "InventorySack::AddItem(Vec2)");
    TQ_RESOLVE(SackRemoveItem, PfnSack_RemoveItem, TQ_SACK_REMOVEITEM, "InventorySack::RemoveItem");
    TQ_RESOLVE(SackIsSpaceForItem, PfnSack_IsSpaceForItem, TQ_SACK_ISSPACEFORITEM,
               "InventorySack::IsSpaceForItem");
    TQ_RESOLVE(SackGetItemUnderPoint, PfnSack_GetItemUnderPoint, TQ_SACK_GETITEMUNDERPOINT,
               "InventorySack::GetItemUnderPoint");
    TQ_RESOLVE(SackCtor, PfnSack_Ctor, TQ_SACK_CTOR, "InventorySack::InventorySack");
    TQ_RESOLVE(SackVftable, const void*, TQ_SACK_VFTABLE, "InventorySack vftable");
    TQ_RESOLVE(SackSort, PfnSack_Sort, TQ_SACK_SORT, "InventorySack::Sort");
    TQ_RESOLVE(SackSetDims, PfnSack_SetDims, TQ_SACK_SETDIMS, "InventorySack::SetDims");
    TQ_RESOLVE(SackContainsItem, PfnSack_ContainsItem, TQ_SACK_CONTAINSITEM,
               "InventorySack::ContainsItem");
    TQ_RESOLVE(SackGetCellWidth, PfnSack_GetU, TQ_SACK_GETCELLWIDTH, "InventorySack::GetCellWidth");
    TQ_RESOLVE(SackGetCellHeight, PfnSack_GetU, TQ_SACK_GETCELLHEIGHT,
               "InventorySack::GetCellHeight");
    TQ_RESOLVE(SackGetGridWidth, PfnSack_GetU, TQ_SACK_GETGRIDWIDTH, "InventorySack::GetGridWidth");
    TQ_RESOLVE(SackGetGridHeight, PfnSack_GetU, TQ_SACK_GETGRIDHEIGHT,
               "InventorySack::GetGridHeight");
    TQ_RESOLVE(SackGetInventory, PfnSack_GetInventory, TQ_SACK_GETINVENTORY,
               "InventorySack::GetInventory");
    TQ_RESOLVE(CharGetControllerId, PfnCharacter_GetControllerId, TQ_CTRL_GETCONTROLLERID,
               "Character::GetControllerId");
    TQ_RESOLVE(CtrlSendRemoveItem, PfnCtrl_SendRemoveItem, TQ_CTRL_SENDREMOVEITEM,
               "ControllerCharacter::SendRemoveItemFromInventory");
    TQ_RESOLVE(ControllerPlayerVftable, const void*, TQ_CTRL_PLAYER_VFTABLE,
               "ControllerPlayer vftable");
    TQ_RESOLVE(CtrlGetInventoryCtrl, PfnCtrl_GetInventoryCtrl, TQ_CTRL_PLAYER_GETINVENTORYCTRL,
               "ControllerPlayer::GetInventoryCtrl");   // the right-click take's room check
    TQ_RESOLVE(InvCtrlGetNumberOfSacks, PfnInvCtrl_GetNumberOfSacks,
               TQ_CTRL_INVCTRL_GETNUMBEROFSACKS, "PlayerInventoryCtrl::GetNumberOfSacks");
    TQ_RESOLVE(InvCtrlGetSack, PfnInvCtrl_GetSack, TQ_CTRL_INVCTRL_GETSACK,
               "PlayerInventoryCtrl::GetSack");
    TQ_RESOLVE(PlayerIsInventorySpaceAvailable, PfnPlayer_IsInventorySpaceAvailable,
               TQ_CTRL_PLAYER_ISINVENTORYSPACEAVAILABLE,
               "Player::IsInventorySpaceAvailable");   // the mouse right-click's room check
    TQ_RESOLVE(PlayerPlayInventoryFullSound, PfnPlayer_PlayInventoryFullSound,
               TQ_CTRL_PLAYER_PLAYINVENTORYFULLSOUND,
               "Player::PlayInventoryFullSound");   // its "no room" sound
    // the slot-wide item tint (optional: missing = no ring, said once by ut_panel)
    TQ_RESOLVE(CtrlGetEquipmentCtrl, PfnCtrl_GetEquipmentCtrl, TQ_CTRL_CHAR_GETEQUIPMENTCTRL,
               "ControllerCharacter::GetEquipmentCtrl");
    TQ_RESOLVE(EquipAreRequirementsMet, PfnEquipCtrl_AreRequirementsMet,
               TQ_CTRL_EQUIPCTRL_AREREQUIREMENTSMET, "EquipmentCtrl::AreRequirementsMet");
    // the equipped ids for the journal's load-time reconciliation (optional)
    TQ_RESOLVE(EquipGetItem[0], PfnEquipCtrl_GetItem, TQ_CTRL_EQUIPCTRL_GETITEM_HEAD,
               "EquipmentCtrl::GetItem_Head");
    TQ_RESOLVE(EquipGetItem[1], PfnEquipCtrl_GetItem, TQ_CTRL_EQUIPCTRL_GETITEM_NECK,
               "EquipmentCtrl::GetItem_Neck");
    TQ_RESOLVE(EquipGetItem[2], PfnEquipCtrl_GetItem, TQ_CTRL_EQUIPCTRL_GETITEM_ARTIFACT,
               "EquipmentCtrl::GetItem_Artifact");
    TQ_RESOLVE(EquipGetItem[3], PfnEquipCtrl_GetItem, TQ_CTRL_EQUIPCTRL_GETITEM_UPPERBODY,
               "EquipmentCtrl::GetItem_UpperBody");
    TQ_RESOLVE(EquipGetItem[4], PfnEquipCtrl_GetItem, TQ_CTRL_EQUIPCTRL_GETITEM_LOWERBODY,
               "EquipmentCtrl::GetItem_LowerBody");
    TQ_RESOLVE(EquipGetItem[5], PfnEquipCtrl_GetItem, TQ_CTRL_EQUIPCTRL_GETITEM_FINGER1,
               "EquipmentCtrl::GetItem_Finger1");
    TQ_RESOLVE(EquipGetItem[6], PfnEquipCtrl_GetItem, TQ_CTRL_EQUIPCTRL_GETITEM_FINGER2,
               "EquipmentCtrl::GetItem_Finger2");
    TQ_RESOLVE(EquipGetItem[7], PfnEquipCtrl_GetItem, TQ_CTRL_EQUIPCTRL_GETITEM_FOREARM,
               "EquipmentCtrl::GetItem_Forearm");
    TQ_RESOLVE(EquipGetItem[8], PfnEquipCtrl_GetItem, TQ_CTRL_EQUIPCTRL_GETITEM_HANDLEFT,
               "EquipmentCtrl::GetItem_HandLeft");
    TQ_RESOLVE(EquipGetItem[9], PfnEquipCtrl_GetItem, TQ_CTRL_EQUIPCTRL_GETITEM_HANDRIGHT,
               "EquipmentCtrl::GetItem_HandRight");
    TQ_RESOLVE(ItemGetActualClassification, PfnItem_GetActualItemClassification,
               TQ_ITEM_GETACTUALITEMCLASSIFICATION, "Item::GetActualItemClassification");
    TQ_RESOLVE(GameGetItemColor, PfnGameEngine_GetItemColor, TQ_GAMEENGINE_GETITEMCOLOR,
               "GameEngine::GetItemColor");
    TQ_RESOLVE(GameGetItemBackgroundOpacity, PfnGameEngine_GetItemBackgroundOpacity,
               TQ_GAMEENGINE_GETITEMBACKGROUNDOPACITY, "GameEngine::GetItemBackgroundOpacity");
    TQ_RESOLVE(EngineGetOptions, PfnEngine_GetOptions, TQ_ENGINE_GETOPTIONS, "Engine::GetOptions");
    TQ_RESOLVE(OptionsGetBool, PfnOptions_GetBool, TQ_ENGINE_OPTIONS_GETBOOL, "Options::GetBool");
    TQ_RESOLVE(CharGetInventoryItems, PfnCharacter_GetInventoryItems, TQ_CHAR_GETINVENTORYITEMS,
               "Character::GetInventoryItems");
    TQ_RESOLVE(ItemVftable, const void*, TQ_ITEM_VFTABLE, "Item vftable");
    TQ_RESOLVE(ItemCreateItem, PfnItem_CreateItem, TQ_ITEM_CREATEITEM, "Item::CreateItem");
    TQ_RESOLVE(ItemGetItemReplicaInfo, PfnItem_GetItemReplicaInfo, TQ_ITEM_GETITEMREPLICAINFO,
               "Item::GetItemReplicaInfo");
    TQ_RESOLVE(ItemGetItemClassification, PfnItem_GetInt, TQ_ITEM_GETITEMCLASSIFICATION,
               "Item::GetItemClassification");
    TQ_RESOLVE(ItemGetItemType, PfnItem_GetInt, TQ_ITEM_GETITEMTYPE, "Item::GetItemType");
    TQ_RESOLVE(ItemGetNumberInStack, PfnItem_GetNumberInStack, TQ_ITEM_GETNUMBERINSTACK,
               "Item::GetNumberInStack");

    // MSVCR110.dll -- the game's operator delete: every engine-filled std::string is freed here.
    TQ_RESOLVE(CrtOperatorDelete, PfnCrt_OperatorDelete, TQ_CRT_OPERATORDELETE,
               "MSVCR110 operator delete");

    logD("exports: %d of %d resolved by name (%d required)", g_tq.resolvedByName, TQ_EXPORT_COUNT,
         TQ_EXPORT_REQUIRED_COUNT);
    if (g_tq.missingRequired == 0) {
        logD("all required exports resolved");
        return true;
    }
    logE("ERROR: %d required export(s) MISSING - hooks will not be installed",
         g_tq.missingRequired);
    return false;
}

void decodeBindings() {
    char why[192];

    // ---- the three store sacks and the caravan mode, out of the getters' own bytes ----------
    const unsigned stash = decodeWith((const void*)g_tq.GameGetPlayerStash, utDecodeLeaEcx32);
    const unsigned stashC = decodeWith((const void*)g_tq.GameGetPlayerStashC, utDecodeLeaEcx32);
    const unsigned transfer = decodeWith((const void*)g_tq.GameGetPlayerTransfer, utDecodeLeaEcx32);
    const unsigned transferC =
        decodeWith((const void*)g_tq.GameGetPlayerTransferC, utDecodeLeaEcx32);
    const unsigned relic = decodeWith((const void*)g_tq.GameGetPlayerRelicVault, utDecodeLeaEcx32);
    const unsigned relicC = decodeWith((const void*)g_tq.GameGetPlayerRelicVaultC, utDecodeLeaEcx32);
    const unsigned mode = decodeWith((const void*)g_tq.GameGetCaravanMode, utDecodeMovEcx32);
    const bool trio = utSackTrioPlausible(stash, transfer, relic, mode);
    struct {
        const char* row;
        unsigned off, twin;
        unsigned* dst;
    } sacks[3] = {{"gameEngine.stashSack", stash, stashC, &g_tq.stashOff},
                  {"gameEngine.transferSack", transfer, transferC, &g_tq.transferOff},
                  {"gameEngine.relicVaultSack", relic, relicC, &g_tq.relicOff}};
    for (int i = 0; i < 3; ++i) {
        const bool ok = trio && sacks[i].off == sacks[i].twin;
        _snprintf_s(why, sizeof(why), _TRUNCATE,
                    "the const twin agrees (+0x%X vs +0x%X); 4-aligned, below 0x10000; the three "
                    "sacks equally spaced (+0x%X/+0x%X/+0x%X)",
                    sacks[i].off, sacks[i].twin, stash, transfer, relic);
        bindingsNote(sacks[i].row, sacks[i].off, ok, why);
        if (ok) *sacks[i].dst = sacks[i].off;
    }
    const bool modeOk = utFieldPlausible(mode, 0x10000) && stash && mode < stash;
    _snprintf_s(why, sizeof(why), _TRUNCATE, "4-aligned, below the stash sack (+0x%X vs +0x%X)",
                mode, stash);
    bindingsNote("gameEngine.caravanMode", mode, modeOk, why);
    if (modeOk) g_tq.modeOff = mode;

    // ---- the cursor's item id -------------------------------------------------------------------
    const unsigned cid = decodeWith((const void*)g_tq.CursorGetId, utDecodeMovEcx8);
    const bool cidOk = cid >= 4 && cid < 0x80 && (cid & 3) == 0;
    _snprintf_s(why, sizeof(why), _TRUNCATE, "4-aligned, 4..0x7C (decoded +0x%X)", cid);
    bindingsNote("cursor.itemId", cid, cidOk, why);
    if (cidOk) g_tq.cursorIdOff = cid;

    // ---- the Item vftable slots, found BY ADDRESS ---------------------------------------------
    noteSlot("item.replicaSlot", g_tq.ItemVftable, (const void*)g_tq.ItemGetItemReplicaInfo);
    noteSlot("item.classificationSlot", g_tq.ItemVftable,
             (const void*)g_tq.ItemGetItemClassification);
    noteSlot("item.typeSlot", g_tq.ItemVftable, (const void*)g_tq.ItemGetItemType);
    noteSlot("item.stackSlot", g_tq.ItemVftable, (const void*)g_tq.ItemGetNumberInStack);

    // ---- the CursorHandlerItemMove capability slots +0x18..+0x34 -> one shared stub ------------
    {
        const void* stub = (const void*)g_tq.CursorIsStashCapable;
        const bool same = stub && stub == (const void*)g_tq.CursorIsTransferCapable &&
                          stub == (const void*)g_tq.CursorIsRelicVaultCapable;
        unsigned char c[8];
        const bool body = same && head8(stub, c) && c[0] == 0xB0 && c[1] == 0x01 && c[2] == 0xC3;
        int held = 0;
        if (body && g_tq.CursorVftable) {
            for (int k = 0; k < 8; ++k) {
                unsigned p = 0;
                if (safeRead((const unsigned char*)g_tq.CursorVftable + 0x18 + 4 * k, &p, 4) &&
                    p == (unsigned)(uintptr_t)stub)
                    ++held;
            }
        }
        _snprintf_s(why, sizeof(why), _TRUNCATE,
                    "the three Is*Capable exports are one `mov al,1; ret` body (same=%d body=%d) "
                    "and all eight slots hold it (%d of 8)",
                    same ? 1 : 0, body ? 1 : 0, held);
        bindingsNote("cursor.capabilitySlots", 0x18, body && held == 8, why);
    }

    // ---- the five counted TQ.exe signatures (hooked / return-address tested by the view) --
    scanSignature(kUtSigTransferPage, &g_tq.sigTransferPageRva);
    scanSignature(kUtSigStreamOut, &g_tq.sigStreamOutRva);
    scanSignature(kUtSigRightClick, &g_tq.sigRightClickRva);
    scanSignature(kUtSigHeldPickup, &g_tq.sigHeldPickupRva);
    scanSignature(kUtSigLeftClick, &g_tq.sigLeftClickRva);
    scanSignature(kUtSigLeftHandler, &g_tq.sigLeftHandlerRva);   // the hover frame
    decodeViewBindings();
    decodeDisposal();
    scanSignature(kUtSigPageDraw, &g_tq.sigPageDrawRva);   // the slot plates
    scanSignature(kUtSigItemBackground, &g_tq.sigItemBackgroundRva);   // the rect route
    decodeQuickMove();

    logI("decoded: GameEngine +0x%X stash / +0x%X transfer / +0x%X relic vault, caravan mode "
         "+0x%X, cursor item id +0x%X; Item slots replica +0x%X class +0x%X type +0x%X stack "
         "+0x%X",
         g_tq.stashOff, g_tq.transferOff, g_tq.relicOff, g_tq.modeOff, g_tq.cursorIdOff,
         (unsigned)g_tq.itemSlotReplica, (unsigned)g_tq.itemSlotClass,
         (unsigned)g_tq.itemSlotType, (unsigned)g_tq.itemSlotStack);
}

}  // namespace ut
