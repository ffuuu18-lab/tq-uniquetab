// test_bindings.cpp - the OFFLINE proof behind every binding the mod locates for itself.
// NO GAME IS LAUNCHED AND NOTHING IS WRITTEN. One read-only input: the installed game folder (the
// one that holds TQ.exe), from %TQ_GAME_DIR% or else from the Steam registry (SteamPath and its
// libraryfolders.vdf). Titan Quest's TQ.exe carries no DRM stub, so unlike Grim Dawn the file on
// disk IS the image that runs and its .text can be scanned directly. Each file half SKIPS LOUDLY
// when the folder is not found; the gate half needs no input and always runs.
//
// THE PATTERNS AND THE DECODERS ARE NOT COPIED INTO THIS FILE. ut_bindings.cpp is linked in and
// ut_bindings.h is included, so the harness runs exactly the bytes and the decoders the mod uses.
//
// THE CASES:
//   1  every name in src/tq_exports.h resolves in its module's export table (TQ_EXPORTS_FOR_EACH),
//      to code inside an executable section or to data inside the image - MSVCR110.dll from the
//      system copy the game loads
//   2  the decoders yield 0x2928 / 0x2960 / 0x2998 (and each const twin agrees), mode 0x2924 and
//      the cursor id 0x24 out of the getters' own bytes, and the trio passes the plausibility rule
//   3  the vftable slots found BY ADDRESS: Item +0x1B0 / +0x1C4 / +0x194 / +0x1B8, and the eight
//      CursorHandlerItemMove capability slots +0x18..+0x34 all holding the one `mov al,1; ret` stub
//   4  each TQ.exe signature matches EXACTLY ONCE, at its AE 2.10 rva; a corrupted pattern (one
//      fixed byte changed) matches ZERO times; a byte changed under a WILDCARD still matches once
//   5  the pure decoders on synthetic bytes: every shape decodes, a wrong shape gives 0
//   4b the two new signatures (right-click 0xBFD10, held pick-up 0xBEC00) likewise; the
//      accessor's own bytes decode the cached member 0x88 + 0x60; each pattern's `FF 15` goes
//      through the IAT slot the import table names GetPlayerTransfer / GetItemUnderPoint
//   6  the gate PASSES when every early binding has reported, and its line reads "5 by signature,
//      10 decoded (5 advisory), 1 structural, 0 literal - all confirmed"
//   7  the CLASSIFICATION, row by row against an explicit list: class, phase and CRITICAL /
//      ADVISORY; a failed ADVISORY row leaves the gate open, a failed CRITICAL row closes it

#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "../src/tq_exports.h"
#include "../src/ut_bindings.h"

namespace {

int g_fail = 0;
int g_ran = 0;

void ok(bool cond, const char* what, const char* detail) {
    ++g_ran;
    if (cond) {
        printf("[bindings]  PASS  %-40s %s\n", what, detail ? detail : "");
    } else {
        printf("[bindings]  FAIL  %-40s %s\n", what, detail ? detail : "");
        ++g_fail;
    }
}

bool readFile(const char* path, std::vector<unsigned char>* out) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) {
        fclose(f);
        return false;
    }
    out->resize((size_t)n);
    const size_t got = fread(&(*out)[0], 1, (size_t)n, f);
    fclose(f);
    return got == (size_t)n;
}

// ---- a PE32 file, read only ----------------------------------------------------------------------
struct Pe {
    std::vector<unsigned char> raw;
    struct Sec {
        unsigned long va, size, ptr, flags;
        std::string name;
    };
    std::vector<Sec> secs;
    unsigned long exportRva = 0;
    unsigned long imageBase = 0;
    unsigned long imageSize = 0;

    bool load(const char* path) {
        if (!readFile(path, &raw) || raw.size() < 0x200) return false;
        const unsigned long lfa = *(const unsigned long*)&raw[0x3C];
        if (lfa + 0x100 >= raw.size()) return false;
        if (*(const unsigned short*)&raw[lfa + 4] != IMAGE_FILE_MACHINE_I386) return false;
        const unsigned short nsec = *(const unsigned short*)&raw[lfa + 6];
        const unsigned short optSize = *(const unsigned short*)&raw[lfa + 20];
        // IMAGE_OPTIONAL_HEADER32: ImageBase at +28, SizeOfImage at +56, DataDirectory at +96.
        imageBase = *(const unsigned long*)&raw[lfa + 24 + 28];
        imageSize = *(const unsigned long*)&raw[lfa + 24 + 56];
        exportRva = *(const unsigned long*)&raw[lfa + 24 + 96];
        const size_t secOff = lfa + 24 + optSize;
        for (unsigned short i = 0; i < nsec; ++i) {
            const size_t o = secOff + (size_t)i * 40;
            if (o + 40 > raw.size()) return false;
            Sec s;
            const unsigned long vsize = *(const unsigned long*)&raw[o + 8];
            s.va = *(const unsigned long*)&raw[o + 12];
            const unsigned long rsize = *(const unsigned long*)&raw[o + 16];
            s.ptr = *(const unsigned long*)&raw[o + 20];
            s.flags = *(const unsigned long*)&raw[o + 36];
            s.size = vsize < rsize ? vsize : rsize;  // what the file really holds
            char nm[9] = {0};
            memcpy(nm, &raw[o], 8);
            s.name = nm;
            secs.push_back(s);
        }
        return true;
    }
    bool textRange(const unsigned char** lo, const unsigned char** hi, unsigned long* va) const {
        for (size_t i = 0; i < secs.size(); ++i) {
            if (!(secs[i].flags & IMAGE_SCN_MEM_EXECUTE)) continue;
            *lo = &raw[secs[i].ptr];
            *hi = *lo + secs[i].size;
            *va = secs[i].va;
            return true;
        }
        return false;
    }
    bool isCode(unsigned long rva) const {
        for (size_t i = 0; i < secs.size(); ++i) {
            if ((secs[i].flags & IMAGE_SCN_MEM_EXECUTE) && rva >= secs[i].va &&
                rva < secs[i].va + secs[i].size)
                return true;
        }
        return false;
    }
    size_t toFile(unsigned long rva) const {
        for (size_t i = 0; i < secs.size(); ++i) {
            if (rva >= secs[i].va && rva < secs[i].va + secs[i].size) {
                return secs[i].ptr + (rva - secs[i].va);
            }
        }
        return 0;
    }
    const unsigned char* at(unsigned long rva) const {
        const size_t f = rva ? toFile(rva) : 0;
        return f && f + 8 <= raw.size() ? &raw[f] : nullptr;
    }
    unsigned long fnRva(const char* name) const {
        const size_t e = toFile(exportRva);
        if (!e || e + 40 > raw.size()) return 0;
        const unsigned long nNames = *(const unsigned long*)&raw[e + 24];
        const unsigned long fnT0 = *(const unsigned long*)&raw[e + 28];
        const unsigned long nameT0 = *(const unsigned long*)&raw[e + 32];
        const unsigned long ordT0 = *(const unsigned long*)&raw[e + 36];
        const size_t fnT = toFile(fnT0), nameT = toFile(nameT0), ordT = toFile(ordT0);
        if (!fnT || !nameT || !ordT) return 0;
        for (unsigned long i = 0; i < nNames; ++i) {
            const unsigned long nrva = *(const unsigned long*)&raw[nameT + i * 4];
            const size_t a = toFile(nrva);
            if (!a || a >= raw.size()) continue;
            if (strcmp((const char*)&raw[a], name) != 0) continue;
            const unsigned short ord = *(const unsigned short*)&raw[ordT + i * 2];
            return *(const unsigned long*)&raw[fnT + (size_t)ord * 4];
        }
        return 0;
    }
    const unsigned char* fn(const char* name) const { return at(fnRva(name)); }
    // The vftable at `vtRva` as a run of slot RVAs (the file holds relocated VAs: ImageBase + rva),
    // stopping at the first slot that is not code.
    std::vector<unsigned> vtable(unsigned long vtRva, int cap) const {
        std::vector<unsigned> out;
        for (int i = 0; i < cap; ++i) {
            const unsigned char* p = at(vtRva + 4 * (unsigned long)i);
            if (!p) break;
            const unsigned long va = *(const unsigned long*)p;
            if (va < imageBase) break;
            const unsigned long rva = va - imageBase;
            if (!isCode(rva)) break;
            out.push_back((unsigned)rva);
        }
        return out;
    }
};

// ---- the game folder: %TQ_GAME_DIR%, else the Steam library folders from the registry ----------
bool hasTqExe(const std::string& dir) {
    const std::string p = dir + "\\TQ.exe";
    const DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::string findGameDir() {
    const char* env = getenv("TQ_GAME_DIR");
    if (env && *env) return hasTqExe(env) ? std::string(env) : std::string();
    char steam[MAX_PATH] = {0};
    DWORD cb = sizeof(steam);
    if (RegGetValueA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath", RRF_RT_REG_SZ,
                     nullptr, steam, &cb) != ERROR_SUCCESS) {
        cb = sizeof(steam);
        if (RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Valve\\Steam", "InstallPath",
                         RRF_RT_REG_SZ, nullptr, steam, &cb) != ERROR_SUCCESS)
            return std::string();
    }
    std::vector<std::string> libs;
    libs.push_back(steam);
    std::vector<unsigned char> vdf;
    if (readFile((std::string(steam) + "\\steamapps\\libraryfolders.vdf").c_str(), &vdf)) {
        const std::string text((const char*)&vdf[0], vdf.size());
        for (size_t at = text.find("\"path\""); at != std::string::npos;
             at = text.find("\"path\"", at + 6)) {
            const size_t q1 = text.find('"', at + 6);
            const size_t q2 = q1 == std::string::npos ? q1 : text.find('"', q1 + 1);
            if (q2 == std::string::npos) break;
            std::string v = text.substr(q1 + 1, q2 - q1 - 1);
            std::string un;
            for (size_t i = 0; i < v.size(); ++i) {
                if (v[i] == '\\' && i + 1 < v.size() && v[i + 1] == '\\') ++i;
                un += v[i];
            }
            libs.push_back(un);
        }
    }
    for (size_t i = 0; i < libs.size(); ++i) {
        std::string cand = libs[i] + "\\steamapps\\common\\Titan Quest Anniversary Edition";
        for (size_t k = 0; k < cand.size(); ++k)
            if (cand[k] == '/') cand[k] = '\\';
        if (hasTqExe(cand)) return cand;
    }
    return std::string();
}

// ---- case 1: every export name resolves ----------------------------------------------------------
struct Row {
    const char* macro;
    const char* module;
    const char* name;
    int required;
};
#define UT_ROW(M, MOD, NAME, REQ) {#M, MOD, NAME, REQ},
const Row kRows[] = {TQ_EXPORTS_FOR_EACH(UT_ROW)};
#undef UT_ROW

void runExportHalf(const std::string& game, Pe* engine, Pe* gamedll) {
    Pe crt;
    char sys[MAX_PATH] = {0};
    GetSystemDirectoryA(sys, MAX_PATH);  // a 32-bit process is redirected to SysWOW64: the x86 copy
    std::string crtPath = game + "\\MSVCR110.dll";
    if (!crt.load(crtPath.c_str())) {
        crtPath = std::string(sys) + "\\MSVCR110.dll";
        crt.load(crtPath.c_str());
    }
    int total = 0, resolved = 0, required = 0, bad = 0;
    char firstBad[200] = "";
    for (size_t i = 0; i < sizeof(kRows) / sizeof(kRows[0]); ++i) {
        const Row& r = kRows[i];
        const Pe* pe = !strcmp(r.module, "Engine.dll") ? engine
                       : !strcmp(r.module, "Game.dll") ? gamedll
                                                        : &crt;
        ++total;
        if (r.required) ++required;
        const unsigned long rva = pe->raw.empty() ? 0 : pe->fnRva(r.name);
        // A data export (a global or a vftable) must lie inside the image; code in an executable
        // section - the same confirmation the mod applies to the loaded module.
        const bool data = strstr(r.name, "@3") != nullptr || !strncmp(r.name, "??_7", 4);
        const bool good = rva && (data ? rva < pe->imageSize : pe->isCode(rva));
        if (good) {
            ++resolved;
        } else {
            ++bad;
            if (!firstBad[0])
                _snprintf_s(firstBad, sizeof(firstBad), _TRUNCATE, ": first miss %s %s", r.module,
                            r.macro);
        }
    }
    char detail[300];
    _snprintf_s(detail, sizeof(detail), _TRUNCATE,
                "%d of %d names resolve (%d required); MSVCR110 from %s%s", resolved, total,
                required, crt.raw.empty() ? "NOWHERE" : "the system copy", firstBad);
    ok(bad == 0 && total == TQ_EXPORT_COUNT && required == TQ_EXPORT_REQUIRED_COUNT,
       "every tq_exports.h name resolves", detail);
    {   // the search field's key gate: the two Engine.dll exports, where they are and their bytes
        const unsigned long hk = engine->fnRva(TQ_INPUT_DISPLAY_HANDLEKEYEVENT);
        const unsigned long gt = engine->fnRva(TQ_INPUT_BUTTONEVENT_GETTEXT);
        const unsigned char* ph = (const unsigned char*)engine->fn(TQ_INPUT_DISPLAY_HANDLEKEYEVENT);
        const unsigned char* pg = (const unsigned char*)engine->fn(TQ_INPUT_BUTTONEVENT_GETTEXT);
        static const unsigned char kPro[5] = {0xA1, 0xF0, 0x43, 0x37, 0x10};   // mov eax,[gEngine]
        static const unsigned char kGet[4] = {0x8D, 0x41, 0x24, 0xC3};         // lea eax,[ecx+0x24]; ret
        _snprintf_s(detail, sizeof(detail), _TRUNCATE, "HandleKeyEvent rva 0x%lX, GetText rva 0x%lX", hk, gt);
        ok(hk == 0x133370 && gt == 0x1A0750 && ph && !memcmp(ph, kPro, 5) && pg && !memcmp(pg, kGet, 4) &&
               TQ_INPUT_DISPLAY_HANDLEKEYEVENT_CC == TQCC_THISCALL && TQ_INPUT_BUTTONEVENT_GETTEXT_CC == TQCC_THISCALL,
           "the key gate: Display::HandleKeyEvent (a clean 5-byte prologue) and ButtonEvent::GetText",
           detail);
    }
}

// ---- cases 2 and 3: the decoders and the vftable slots, over Game.dll ----------------------------
void runDecoderHalf(const Pe& game) {
    struct Get {
        const char* what;
        const char* name;
        const char* twin;
        unsigned expect;
    };
    const Get gets[] = {
        {"gameEngine.stashSack", TQ_GAMEENGINE_GETPLAYERSTASH, TQ_GAMEENGINE_GETPLAYERSTASH_C,
         0x2928},
        {"gameEngine.transferSack", TQ_GAMEENGINE_GETPLAYERTRANSFER,
         TQ_GAMEENGINE_GETPLAYERTRANSFER_C, 0x2960},
        {"gameEngine.relicVaultSack", TQ_GAMEENGINE_GETPLAYERRELICVAULT,
         TQ_GAMEENGINE_GETPLAYERRELICVAULT_C, 0x2998},
    };
    unsigned got[3] = {0, 0, 0};
    char detail[256];
    for (int i = 0; i < 3; ++i) {
        got[i] = ut::utDecodeLeaEcx32(game.fn(gets[i].name));
        const unsigned twin = ut::utDecodeLeaEcx32(game.fn(gets[i].twin));
        _snprintf_s(detail, sizeof(detail), _TRUNCATE,
                    "lea eax,[ecx+0x%X] (const twin +0x%X), AE 2.10 recorded 0x%X", got[i], twin,
                    gets[i].expect);
        ok(got[i] == gets[i].expect && twin == got[i], gets[i].what, detail);
    }
    const unsigned mode = ut::utDecodeMovEcx32(game.fn(TQ_GAMEENGINE_GETCARAVANMODE));
    _snprintf_s(detail, sizeof(detail), _TRUNCATE, "mov eax,[ecx+0x%X], AE 2.10 recorded 0x2924",
                mode);
    ok(mode == 0x2924, "gameEngine.caravanMode", detail);
    _snprintf_s(detail, sizeof(detail), _TRUNCATE,
                "+0x%X/+0x%X/+0x%X equally spaced, mode +0x%X below them", got[0], got[1], got[2],
                mode);
    ok(ut::utSackTrioPlausible(got[0], got[1], got[2], mode), "the trio passes the mod's rule",
       detail);
    ok(!ut::utSackTrioPlausible(got[0], got[1] + 4, got[2], mode) &&
           !ut::utSackTrioPlausible(got[0], got[1], got[2], got[0] + 4) &&
           !ut::utSackTrioPlausible(got[0] + 2, got[1] + 2, got[2] + 2, mode),
       "a skewed / misplaced / unaligned trio is refused", "uneven spacing, mode above, +2");
    const unsigned cid = ut::utDecodeMovEcx8(game.fn(TQ_CURSOR_GETID));
    _snprintf_s(detail, sizeof(detail), _TRUNCATE, "mov eax,[ecx+0x%X], AE 2.10 recorded 0x24", cid);
    ok(cid == 0x24, "cursor.itemId", detail);

    // -- the Item vftable slots, found by address ------------------------------------------------
    const std::vector<unsigned> item = game.vtable(game.fnRva(TQ_ITEM_VFTABLE), 512);
    struct Slot {
        const char* what;
        const char* name;
        int expect;
    };
    const Slot slots[] = {{"item.replicaSlot", TQ_ITEM_GETITEMREPLICAINFO, 0x1B0},
                          {"item.classificationSlot", TQ_ITEM_GETITEMCLASSIFICATION, 0x1C4},
                          {"item.typeSlot", TQ_ITEM_GETITEMTYPE, 0x194},
                          {"item.stackSlot", TQ_ITEM_GETNUMBERINSTACK, 0x1B8}};
    for (size_t i = 0; i < sizeof(slots) / sizeof(slots[0]); ++i) {
        const int off = item.empty() ? -1
                                     : ut::utVtableSlotOf(&item[0], (int)item.size(),
                                                          (unsigned)game.fnRva(slots[i].name));
        _snprintf_s(detail, sizeof(detail), _TRUNCATE,
                    "slot +0x%X of %u in ??_7Item, AE 2.10 recorded +0x%X", (unsigned)off,
                    (unsigned)item.size(), (unsigned)slots[i].expect);
        ok(off == slots[i].expect, slots[i].what, detail);
    }

    // -- the eight CursorHandlerItemMove capability slots -> one shared stub ----------------------
    const unsigned long stub = game.fnRva(TQ_CURSOR_ISSTASHCAPABLE);
    const bool same = stub && stub == game.fnRva(TQ_CURSOR_ISTRANSFERCAPABLE) &&
                      stub == game.fnRva(TQ_CURSOR_ISRELICVAULTCAPABLE);
    const unsigned char* sb = game.at(stub);
    const bool body = sb && sb[0] == 0xB0 && sb[1] == 0x01 && sb[2] == 0xC3;
    const std::vector<unsigned> cur = game.vtable(game.fnRva(TQ_CURSOR_VFTABLE), 64);
    int held = 0;
    for (int k = 0; k < 8; ++k)
        if (6 + k < (int)cur.size() && cur[6 + k] == stub) ++held;
    _snprintf_s(detail, sizeof(detail), _TRUNCATE,
                "one address for all three %d, body B0 01 C3 %d, slots +0x18..+0x34 holding it %d "
                "of 8",
                same ? 1 : 0, body ? 1 : 0, held);
    ok(same && body && held == 8, "cursor.capabilitySlots", detail);
    // And the one slot the exported GetId occupies is NOT one of them (a sanity bound on the scan).
    const int getIdSlot = cur.empty() ? -1
                                      : ut::utVtableSlotOf(&cur[0], (int)cur.size(),
                                                           (unsigned)game.fnRva(TQ_CURSOR_GETID));
    _snprintf_s(detail, sizeof(detail), _TRUNCATE, "GetId in slot +0x%X of %u", (unsigned)getIdSlot,
                (unsigned)cur.size());
    ok(getIdSlot >= 0 && (getIdSlot < 0x18 || getIdSlot > 0x34),
       "the cursor vftable is read to its end", detail);
}

// ---- case 4: the two TQ.exe signatures -----------------------------------------------------------
void runExeHalf(const Pe& exe) {
    const unsigned char *lo = nullptr, *hi = nullptr;
    unsigned long va = 0;
    if (!exe.textRange(&lo, &hi, &va)) {
        ok(false, "TQ.exe .text", "TQ.exe has no executable section");
        return;
    }
    const ut::UtBindPattern* sigs[8] = {&ut::kUtSigTransferPage, &ut::kUtSigStreamOut,
                                        &ut::kUtSigRightClick, &ut::kUtSigHeldPickup,
                                        &ut::kUtSigLeftClick, &ut::kUtSigLeftHandler,
                                        &ut::kUtSigPageDraw,   // the slot plates
                                        &ut::kUtSigItemBackground};   // the rect route
    for (int i = 0; i < 8; ++i) {
        const unsigned char* hits[4] = {nullptr, nullptr, nullptr, nullptr};
        const int n = ut::utBindScan(lo, hi, *sigs[i], hits, 4);
        const unsigned long rva = hits[0] ? (unsigned long)(hits[0] - lo) + va : 0;
        char detail[256];
        _snprintf_s(detail, sizeof(detail), _TRUNCATE,
                    "%u bytes: %d hit(s), first at TQ.exe+0x%lX, expected 1 at 0x%X",
                    (unsigned)sigs[i]->len, n, rva, (unsigned)sigs[i]->rva210);
        ok(n == 1 && rva == sigs[i]->rva210, sigs[i]->name, detail);

        // A CORRUPTED pattern must be NOT FOUND, never matched somewhere else: flip the last
        // fixed byte. And a byte changed under a WILDCARD must still match - the mask is live.
        std::vector<unsigned char> bytes(sigs[i]->bytes, sigs[i]->bytes + sigs[i]->len);
        std::vector<unsigned char> mask(sigs[i]->mask, sigs[i]->mask + sigs[i]->len);
        size_t fixed = bytes.size() - 1, wild = 0;
        while (fixed > 0 && !mask[fixed]) --fixed;
        while (wild < mask.size() && mask[wild]) ++wild;
        std::vector<unsigned char> bad = bytes;
        bad[fixed] = (unsigned char)(bad[fixed] ^ 0xFF);
        ut::UtBindPattern pb = {"corrupted", &bad[0], &mask[0], bad.size(), 1, 0};
        const int nb = ut::utBindScan(lo, hi, pb, hits, 4);
        std::vector<unsigned char> wl = bytes;
        if (wild < wl.size()) wl[wild] = (unsigned char)(wl[wild] ^ 0xFF);
        ut::UtBindPattern pw = {"wildcarded", &wl[0], &mask[0], wl.size(), 1, 0};
        const int nw = ut::utBindScan(lo, hi, pw, hits, 4);
        _snprintf_s(detail, sizeof(detail), _TRUNCATE,
                    "fixed byte %u flipped -> %d hit(s); wildcard byte %u flipped -> %d hit(s)",
                    (unsigned)fixed, nb, (unsigned)wild, nw);
        ok(nb == 0 && nw == 1 && wild < wl.size(), "  corrupted 0, wildcarded still 1", detail);
    }
}

// ---- case 4b: what the view reads out of the matched bytes -----------------------------------
// The import name an unbound IAT slot (a VA inside TQ.exe) points at, or "".
std::string importNameAt(const Pe& exe, unsigned long slotVa) {
    if (slotVa < exe.imageBase) return "";
    const size_t off = exe.toFile(slotVa - exe.imageBase);
    if (!off || off + 4 > exe.raw.size()) return "";
    const unsigned long thunk = *(const unsigned long*)&exe.raw[off];
    if (!thunk || (thunk & 0x80000000u)) return "";
    const size_t name = exe.toFile(thunk + 2);   // IMAGE_IMPORT_BY_NAME: hint, then the name
    if (!name || name >= exe.raw.size()) return "";
    std::string out;
    for (size_t k = name; k < exe.raw.size() && exe.raw[k]; ++k) out += (char)exe.raw[k];
    return out;
}

// ---- the quick-move's primary call site and the page draw's shape ----------
void runQuickMoveHalf(const Pe& exe) {
    const unsigned char *lo = nullptr, *hi = nullptr;
    unsigned long va = 0;
    if (!exe.textRange(&lo, &hi, &va)) return;
    ut::UtQuickSite sites[16];
    const int n = ut::utQuickMoveSites(lo, hi, sites, 16);
    int hits = 0;
    unsigned long ret = 0, load = 0;
    for (int i = 0; i < n && i < 16; ++i) {
        if (importNameAt(exe, sites[i].slot) == "?AddItemToTransfer@GameEngine@GAME@@QAE_NI_N@Z") {
            ++hits;
            ret = (unsigned long)sites[i].ret + va;
            load = (unsigned long)sites[i].load + va;
        }
    }
    char detail[256];
    _snprintf_s(detail, sizeof(detail), _TRUNCATE,
                "%d `8B 3D slot ... FF D7 84 C0 0F 84` shapes, %d on AddItemToTransfer(id,bool)'s slot; "
                "load TQ.exe+0x%lX, return address TQ.exe+0x%lX (expected 0x107A61 / 0x107A6C)",
                n, hits, load, ret);
    ok(n >= 1 && n <= 16 && hits == 1 && ret == 0x107A6C && load == 0x107A61,
       "exe.quickMoveAdd.ret: exactly one primary quick-move call, returning to 0x107A6C", detail);
    // The second call through the same register (the stacked-extras loop) must NOT match the shape:
    // its result is ignored (no `84 C0 0F 84` after it), so it can never be the accepted caller.
    int extras = 0;
    for (const unsigned char* q = lo + (0x107A6C - va); q < lo + (0x107B40 - va) && q + 2 <= hi; ++q)
        if (q[0] == 0xFF && q[1] == 0xD7) ++extras;
    _snprintf_s(detail, sizeof(detail), _TRUNCATE, "%d more `FF D7` call(s) in 0x107A6C..0x107B40", extras);
    ok(extras >= 1, "  the extras loop calls the same slot again, and is not the accepted site", detail);

    // The page draw: `ret 0x10` (4 arguments: canvas, parent origin, pass, alpha) and the
    // inventory's draw is `call [eax+0x0C]` on this+0x88 inside the signature.
    const unsigned long d = ut::kUtSigPageDraw.rva210;
    const size_t o = exe.toFile(d);
    bool ret10 = false;
    for (size_t k = o; o && k + 3 <= exe.raw.size() && k < o + 0x200; ++k) {
        if (exe.raw[k] == 0xC2 && exe.raw[k + 1] == 0x10 && exe.raw[k + 2] == 0x00) {
            ret10 = true;
            break;
        }
    }
    const size_t len = ut::kUtSigPageDraw.len;
    const bool tail = o && exe.raw[o + len - 3] == 0xFF && exe.raw[o + len - 2] == 0x50 &&
                      exe.raw[o + len - 1] == 0x0C;
    ok(ret10 && tail, "exe.transferPageDraw: ends in the inventory's `FF 50 0C`, returns `ret 0x10`",
       ret10 ? (tail ? "both" : "the tail is not FF 50 0C") : "no ret 0x10 in 0x200 bytes");
}

// the inventory draw's tint operands ut_panel reads (readInventoryTint) - the
// displacements and the option id are decoded from TQ.exe, not trusted from the disassembly notes.
// the rect route's widget fields and its call site, decoded from TQ.exe (not trusted from the
// disassembly notes): the item loop calls the background at vt+0x88 and the icon at vt+0x0C right
// after it on the same widget; the item id is `push [eax+0x30]`; the background and the icon read
// the rect at +0x10 / +0x18 and the scale at +0x78; the four item-widget vtables hold 0x10A9B0 at
// +0x88.
void runRectRouteHalf(const Pe& exe) {
    struct Op {
        const char* what;
        unsigned long rva;
        unsigned char b[6];
        int n;
    } ops[] = {
        {"the item loop: push [eax+item id]", 0x16E3B3, {0xFF, 0x70, (unsigned char)ut::kUtWidgetItemId}, 3},
        {"the item loop: call [edx+0x88] (the background)", 0x16E430, {0xFF, 0x92, 0x88, 0x00, 0x00, 0x00}, 6},
        {"the item loop: call [eax+0x0C] (the icon, next)", 0x16E452, {0xFF, 0x50, 0x0C}, 3},
        {"the background: movq xmm0,[ecx+rect]", 0x10A9B3, {0xF3, 0x0F, 0x7E, 0x41, (unsigned char)ut::kUtWidgetRect}, 5},
        {"the background: movq xmm0,[ecx+rect+8]", 0x10A9C7, {0xF3, 0x0F, 0x7E, 0x41, (unsigned char)(ut::kUtWidgetRect + 8)}, 5},
        {"the background: movss xmm1,[ecx+scale]", 0x10AA28, {0xF3, 0x0F, 0x10, 0x49, (unsigned char)ut::kUtWidgetScale}, 5},
        {"the icon: movq xmm0,[ebx+rect]", 0x10AE53, {0xF3, 0x0F, 0x7E, 0x43, (unsigned char)ut::kUtWidgetRect}, 5},
        {"the icon: movq xmm0,[ebx+rect+8]", 0x10AE5E, {0xF3, 0x0F, 0x7E, 0x43, (unsigned char)(ut::kUtWidgetRect + 8)}, 5},
        {"the icon: movss xmm1,[ebx+scale]", 0x10AE35, {0xF3, 0x0F, 0x10, 0x4B, (unsigned char)ut::kUtWidgetScale}, 5},
        // the texture the icon draw passes on is the widget's [+0x3C]
        {"the icon: mov ecx,[ebx+icon] (GetWidth)", 0x10ADED, {0x8B, 0x4B, (unsigned char)ut::kUtWidgetIcon}, 3},
        {"the icon: push [ebx+icon] (RenderRect's texture)", 0x10AE8F, {0xFF, 0x73, (unsigned char)ut::kUtWidgetIcon}, 3},
    };
    char detail[192];
    for (size_t i = 0; i < sizeof(ops) / sizeof(ops[0]); ++i) {
        const size_t o = exe.toFile(ops[i].rva);
        bool same = o && o + (size_t)ops[i].n <= exe.raw.size();
        for (int k = 0; same && k < ops[i].n; ++k) same = exe.raw[o + k] == ops[i].b[k];
        _snprintf_s(detail, sizeof(detail), _TRUNCATE, "TQ.exe+0x%lX %s", ops[i].rva,
                    same ? "reads as stated" : "is NOT the expected instruction");
        ok(same, ops[i].what, detail);
    }
    const unsigned long vts[4] = {0x21F0C0, 0x21F208, 0x21F2B0, 0x21F358};
    int held = 0;
    for (int i = 0; i < 4; ++i) {
        const size_t o = exe.toFile(vts[i] + 0x88);
        if (o && o + 4 <= exe.raw.size() &&
            *(const unsigned long*)&exe.raw[o] == exe.imageBase + ut::kUtSigItemBackground.rva210)
            ++held;
    }
    _snprintf_s(detail, sizeof(detail), _TRUNCATE, "%d of 4 item-widget vtables hold TQ.exe+0x%X at +0x88",
                held, (unsigned)ut::kUtSigItemBackground.rva210);
    ok(held == 4, "exe.itemBackground is the item widgets' background slot", detail);
}

void runTintHalf(const Pe& exe) {
    struct Op {
        const char* what;
        unsigned long rva;
        unsigned char b0, b1;
        unsigned disp;
    } ops[3] = {
        {"mov eax,[esi+inset]", ut::kUtRvaInvInset, 0x8B, 0x86, ut::kUtInvInset},
        {"lea eax,[esi+fails]", ut::kUtRvaInvFails, 0x8D, 0x86, ut::kUtInvFails},
        {"lea eax,[esi+shade]", ut::kUtRvaInvShade, 0x8D, 0x86, ut::kUtInvShade},
    };
    char detail[192];
    for (int i = 0; i < 3; ++i) {
        const size_t o = exe.toFile(ops[i].rva);
        const bool op = o && o + 6 <= exe.raw.size() && exe.raw[o] == ops[i].b0 && exe.raw[o + 1] == ops[i].b1;
        unsigned disp = 0;
        if (op) memcpy(&disp, &exe.raw[o + 2], 4);
        _snprintf_s(detail, sizeof(detail), _TRUNCATE, "TQ.exe+0x%lX %s: %s, displacement 0x%X (want 0x%X)",
                    ops[i].rva, ops[i].what, op ? "decoded" : "NOT the expected opcode", disp, ops[i].disp);
        ok(op && disp == ops[i].disp, "exe.inventoryTint: the item draw's operand is ut_panel's offset", detail);
    }
    const size_t o = exe.toFile(ut::kUtRvaOptItemTint);
    const bool push = o && o + 2 <= exe.raw.size() && exe.raw[o] == 0x6A && exe.raw[o + 1] == ut::kUtOptItemTint;
    _snprintf_s(detail, sizeof(detail), _TRUNCATE, "TQ.exe+0x%lX: %02X %02X (want 6A %02X)", ut::kUtRvaOptItemTint,
                o ? exe.raw[o] : 0, o ? exe.raw[o + 1] : 0, ut::kUtOptItemTint);
    ok(push, "exe.inventoryTint: the item-colour option is `push 0x18`", detail);
}

void runViewHalf(const Pe& exe) {
    char detail[256];
    const size_t acc = exe.toFile(ut::kUtSigTransferPage.rva210);
    const unsigned ui = acc ? ut::utDecodeLeaEdiEbx32(&exe.raw[acc + 0x21]) : 0;
    const unsigned sack = acc ? ut::utDecodeMovEdi8Ecx(&exe.raw[acc + 0x2F]) : 0;
    _snprintf_s(detail, sizeof(detail), _TRUNCATE, "sub-window +0x%X, then +0x%X = +0x%X", ui, sack,
                ui + sack);
    ok(ui == 0x88 && sack == 0x60, "page.cachedSack decodes 0x88 + 0x60", detail);
    struct Call {
        const char* what;
        unsigned long at;   // rva of the FF 15
        const char* want;
    } calls[4] = {
        {"accessor +0x0D -> GetPlayerTransfer", ut::kUtSigTransferPage.rva210 + 0x0D,
         "?GetPlayerTransfer@GameEngine@GAME@@QAEAAVInventorySack@2@XZ"},
        {"right-click end -> GetItemUnderPoint",
         ut::kUtSigRightClick.rva210 + (unsigned long)ut::kUtSigRightClick.len - 6,
         "?GetItemUnderPoint@InventorySack@GAME@@QBEIVVec2@2@@Z"},
        {"held pick-up end -> GetItemUnderPoint",
         ut::kUtSigHeldPickup.rva210 + (unsigned long)ut::kUtSigHeldPickup.len - 6,
         "?GetItemUnderPoint@InventorySack@GAME@@QBEIVVec2@2@@Z"},
        {"left-click end -> GetItemUnderPoint",
         ut::kUtSigLeftClick.rva210 + (unsigned long)ut::kUtSigLeftClick.len - 6,
         "?GetItemUnderPoint@InventorySack@GAME@@QBEIVVec2@2@@Z"},
    };
    for (int i = 0; i < 4; ++i) {
        const size_t o = exe.toFile(calls[i].at);
        std::string name;
        if (o && o + 6 <= exe.raw.size() && exe.raw[o] == 0xFF && exe.raw[o + 1] == 0x15)
            name = importNameAt(exe, *(const unsigned long*)&exe.raw[o + 2]);
        _snprintf_s(detail, sizeof(detail), _TRUNCATE, "FF 15 at TQ.exe+0x%lX -> %s", calls[i].at,
                    name.empty() ? "(not an import slot)" : name.c_str());
        ok(name == calls[i].want, calls[i].what, detail);
    }
    const unsigned long rets[3] = {
        ut::kUtSigRightClick.rva210 + (unsigned long)ut::kUtSigRightClick.len,
        ut::kUtSigHeldPickup.rva210 + (unsigned long)ut::kUtSigHeldPickup.len,
        ut::kUtSigLeftClick.rva210 + (unsigned long)ut::kUtSigLeftClick.len};
    _snprintf_s(detail, sizeof(detail), _TRUNCATE,
                "right-click TQ.exe+0x%lX, held TQ.exe+0x%lX, left-click TQ.exe+0x%lX", rets[0],
                rets[1], rets[2]);
    ok(rets[0] == 0xBFDD5 && rets[1] == 0xBEC6F && rets[2] == 0xC00CE,
       "the return addresses are match + len (the disassembly's and the earlier)", detail);

    // every GetItemUnderPoint use in the page class (UIStashInventory, TQ.exe
    // 0xBE000..0xC3000) must be one of the three refused calls - a fourth would be an unrefused take
    // path. Counts every dword that holds the IAT slot's VA, and the `FF 15 <slot>` calls among them.
    {
        const size_t so = exe.toFile(rets[0] - 4);
        const unsigned long slot =
            (so && so + 4 <= exe.raw.size()) ? *(const unsigned long*)&exe.raw[so] : 0;
        int refs = 0, callSites = 0, covered = 0;
        for (unsigned long r = 0xBE000; r + 4 <= 0xC3000; ++r) {
            const size_t o = exe.toFile(r);
            if (!o || o < 2 || o + 4 > exe.raw.size()) continue;
            if (*(const unsigned long*)&exe.raw[o] != slot) continue;
            ++refs;
            if (exe.raw[o - 2] != 0xFF || exe.raw[o - 1] != 0x15) continue;
            ++callSites;
            for (int k = 0; k < 3; ++k)
                if (rets[k] == r + 4) ++covered;
        }
        _snprintf_s(detail, sizeof(detail), _TRUNCATE,
                    "slot VA 0x%lX: %d reference(s), %d `FF 15` call(s), %d at a refused return "
                    "address", slot, refs, callSites, covered);
        ok(slot != 0 && refs == 3 && callSites == 3 && covered == 3,
           "the page class calls GetItemUnderPoint exactly 3 times, all refused or hover-proven", detail);

        // the census of the WHOLE .text, not just the page class. Every reference
        // to the slot is pinned by address: the three refused sites; the player inventory UI (real
        // sacks only, 7 calls + one `8B 3D` mov edi,[slot] for a `call edi`); and the four
        // base-widget calls, two of which the page's vftable reaches on the CACHED sack (slot +0xDC
        // = 0x16EA70 calls at 0x16EE65, and its callee 0x16D7E0 at 0x16D824: the controller
        // cell-cursor hover, pass-through while ON). A new caller anywhere fails this row.
        static const unsigned long kCalls[] = {0xBEC69,  0xBFDCF,  0xC00C8,  0x105A0F, 0x106463,
                                               0x1093E6, 0x109464, 0x10DEF6, 0x10E7D0, 0x10F20C,
                                               0x16D824, 0x16DBF1, 0x16DE15, 0x16EE65};
        const int kCallN = (int)(sizeof(kCalls) / sizeof(kCalls[0]));
        const unsigned long kMovEdi = 0x106F7B;
        const unsigned char* lo = nullptr;
        const unsigned char* hi = nullptr;
        unsigned long textVa = 0;
        int callN = 0, pinned = 0, movs = 0, other = 0;
        if (slot && exe.textRange(&lo, &hi, &textVa)) {
            for (const unsigned char* q = lo + 2; q + 4 <= hi; ++q) {
                if (*(const unsigned long*)q != slot) continue;
                const unsigned long at = textVa + (unsigned long)(q - lo) - 2;   // the opcode
                if (q[-2] == 0xFF && q[-1] == 0x15) {
                    ++callN;
                    for (int k = 0; k < kCallN; ++k)
                        if (kCalls[k] == at) ++pinned;
                } else if (q[-2] == 0x8B && q[-1] == 0x3D && at == kMovEdi) {
                    ++movs;
                } else {
                    ++other;
                }
            }
        }
        _snprintf_s(detail, sizeof(detail), _TRUNCATE,
                    "TQ.exe .text: %d `FF 15` call(s) (%d at a pinned address, %d pinned), %d `8B 3D` "
                    "at 0x%lX, %d other reference(s)", callN, pinned, kCallN, movs, kMovEdi, other);
        ok(callN == kCallN && pinned == kCallN && movs == 1 && other == 0,
           "the whole exe: every GetItemUnderPoint reference is a pinned, known site", detail);

        // The two inherited sites the page vftable reaches: the slot, and what each does with the
        // id (checked - harmless: a flag and the tooltip-entry scan).
        const size_t vo = exe.toFile(0x21CFD8 + 0xDC);
        const unsigned long slotDC =
            (vo && vo + 4 <= exe.raw.size()) ? *(const unsigned long*)&exe.raw[vo] : 0;
        // 0x16EE6B: test eax,eax / je / cmp byte [edi+0x50],0 / mov [edi+0x54],eax / je /
        //           push ecx / mov ecx,edi / call 0x16D7E0 (its result -> *hoverOut)
        const unsigned char useA[21] = {0x85, 0xC0, 0x74, 0x4E, 0x80, 0x7F, 0x50, 0x00, 0x89, 0x47, 0x54,
                                        0x74, 0x22, 0x51, 0x8B, 0xCF, 0xE8, 0x60, 0xE9, 0xFF, 0xFF};
        // 0x16D82A: mov esi,eax / test / je / cmp byte [ebp+0x50],0 / mov [ebp+0x54],esi / je /
        //           the entry scan over [ebp+0x88..0x8C] comparing [entry+0x30] with the id
        const unsigned char useB[47] = {0x8B, 0xF0, 0x85, 0xF6, 0x74, 0x31, 0x80, 0x7D, 0x50, 0x00, 0x89, 0x75,
                                        0x54, 0x74, 0x28, 0x8B, 0x85, 0x88, 0x00, 0x00, 0x00, 0x8B, 0x95, 0x8C,
                                        0x00, 0x00, 0x00, 0x2B, 0xD0, 0xC1, 0xFA, 0x02, 0x33, 0xDB, 0x85, 0xD2,
                                        0x74, 0x11, 0x8B, 0xC8, 0x8B, 0x01, 0x39, 0x70, 0x30, 0x74, 0x14};
        const size_t oa = exe.toFile(0x16EE6B), ob = exe.toFile(0x16D82A);
        const bool aOk = oa && oa + sizeof(useA) <= exe.raw.size() &&
                         memcmp(&exe.raw[oa], useA, sizeof(useA)) == 0;
        const bool bOk = ob && ob + sizeof(useB) <= exe.raw.size() &&
                         memcmp(&exe.raw[ob], useB, sizeof(useB)) == 0;
        _snprintf_s(detail, sizeof(detail), _TRUNCATE,
                    "page vftable +0xDC -> 0x%lX; 0x16EE6B use %s; 0x16D82A use %s", slotDC,
                    aOk ? "as pinned" : "CHANGED", bOk ? "as pinned" : "CHANGED");
        ok(slotDC == exe.imageBase + 0x16EA70 && aOk && bOk,
           "the inherited cell-cursor sites only flag +0x54 and scan the tooltip entries", detail);
    }

    // the page mouse handler. Its entry pattern ends where the
    // left-click pattern begins, the frame distance decodes to 0x68, its two bools pick the
    // pick-up / clone branches right after the call, and the branch both bools skip to (the hover)
    // calls nothing but the tooltip's vcalls: no `FF 15` import call, no `E8` call.
    {
        const unsigned long h = ut::kUtSigLeftHandler.rva210;
        const bool contiguous = h + (unsigned long)ut::kUtSigLeftHandler.len == ut::kUtSigLeftClick.rva210;
        const size_t f = exe.toFile(h);
        const unsigned char pro[6] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8};
        const bool shape = f && f + 0x70 <= exe.raw.size() && memcmp(&exe.raw[f], pro, 6) == 0 &&
                           exe.raw[f + 0x14] == 0x83 && exe.raw[f + 0x15] == 0xEC &&
                           exe.raw[f + 0x59] == 0x8B && exe.raw[f + 0x5A] == 0x45 &&
                           exe.raw[f + 0x5B] == 0x14 && exe.raw[f + 0x69] == 0x8B &&
                           exe.raw[f + 0x6A] == 0x45 && exe.raw[f + 0x6B] == 0x10;
        const unsigned dist = shape ? 0x28u + exe.raw[f + 0x16] : 0;
        _snprintf_s(detail, sizeof(detail), _TRUNCATE,
                    "entry TQ.exe+0x%lX + %u = 0x%lX (left-click pattern at 0x%X); frame distance "
                    "0x%X", h, (unsigned)ut::kUtSigLeftHandler.len,
                    h + (unsigned long)ut::kUtSigLeftHandler.len, (unsigned)ut::kUtSigLeftClick.rva210,
                    dist);
        ok(contiguous && shape && dist == 0x68 && h == 0xBFED0,
           "exe.leftClickHandler: contiguous, EBP - return slot = 0x68 + (EBP & 7)", detail);

        // `80 7D 08 00 / 0F 84 rel32` at the return address (b1), `80 7D 0C 00 / 0F 84 rel32` at its
        // target (b2); the second target is the hover branch, which ends at the common exit.
        const unsigned long ret = rets[2];
        auto rel32Target = [&](unsigned long at) -> unsigned long {
            const size_t o = exe.toFile(at);
            if (!o || o + 6 > exe.raw.size() || exe.raw[o] != 0x0F || exe.raw[o + 1] != 0x84) return 0;
            return at + 6 + (unsigned long)*(const long*)&exe.raw[o + 2];
        };
        auto bytesAre = [&](unsigned long at, const unsigned char* want, size_t n) {
            const size_t o = exe.toFile(at);
            return o && o + n <= exe.raw.size() && memcmp(&exe.raw[o], want, n) == 0;
        };
        const unsigned char b1[4] = {0x80, 0x7D, 0x08, 0x00};
        const unsigned char b2[4] = {0x80, 0x7D, 0x0C, 0x00};
        const unsigned long notB1 = bytesAre(ret, b1, 4) ? rel32Target(ret + 12) : 0;   // `8B D0`, `89 93 d32`, then `0F 84`
        const unsigned long hoverAt = (notB1 && bytesAre(notB1, b2, 4)) ? rel32Target(notB1 + 4) : 0;
        const unsigned long exitAt = rel32Target(ut::kUtSigLeftClick.rva210 + 0x51);   // C00BE: je exit
        int ff15 = 0, e8 = 0;
        for (unsigned long r = hoverAt; hoverAt && r < exitAt; ++r) {
            const size_t o = exe.toFile(r);
            if (!o || o + 2 > exe.raw.size()) break;
            if (exe.raw[o] == 0xFF && exe.raw[o + 1] == 0x15) ++ff15;
            // an E8 byte right after a short-jump opcode is that jump's rel8, not a call
            const unsigned char prev = exe.raw[o - 1];
            if (exe.raw[o] == 0xE8 && !((prev >= 0x70 && prev <= 0x7F) || prev == 0xEB)) ++e8;
        }
        _snprintf_s(detail, sizeof(detail), _TRUNCATE,
                    "b1 test at 0x%lX, b2 test at 0x%lX, hover branch 0x%lX..0x%lX: %d FF 15, %d E8",
                    ret, notB1, hoverAt, exitAt, ff15, e8);
        ok(notB1 == 0xC0208 && hoverAt == 0xC0314 && exitAt == 0xC03D6 && ff15 == 0 && e8 == 0,
           "the handler's (0,0) branch is hover-only (no import call, no call)", detail);
    }
}

// ---- case 5: the pure decoders on synthetic bytes ------------------------------------------------
void runSyntheticHalf() {
    const unsigned char lea[8] = {0x8D, 0x81, 0x28, 0x29, 0x00, 0x00, 0xC3, 0xCC};
    const unsigned char mov[8] = {0x8B, 0x81, 0x24, 0x29, 0x00, 0x00, 0xC3, 0xCC};
    const unsigned char movAl[8] = {0x8A, 0x81, 0x10, 0x01, 0x00, 0x00, 0xC3, 0xCC};
    const unsigned char movzx[8] = {0x0F, 0xB6, 0x81, 0x11, 0x01, 0x00, 0x00, 0xC3};
    const unsigned char mov8[8] = {0x8B, 0x41, 0x24, 0xC3, 0xCC, 0xCC, 0xCC, 0xCC};
    const unsigned char notRet[8] = {0x8D, 0x81, 0x28, 0x29, 0x00, 0x00, 0xC2, 0x04};
    const unsigned char neg8[8] = {0x8B, 0x41, 0xFC, 0xC3, 0xCC, 0xCC, 0xCC, 0xCC};
    ok(ut::utDecodeLeaEcx32(lea) == 0x2928 && ut::utDecodeMovEcx32(mov) == 0x2924 &&
           ut::utDecodeMovAlEcx32(movAl) == 0x110 && ut::utDecodeMovzxEcx32(movzx) == 0x111 &&
           ut::utDecodeMovEcx8(mov8) == 0x24,
       "each x86 getter shape decodes", "8D 81 / 8B 81 / 8A 81 / 0F B6 81 / 8B 41");
    ok(ut::utDecodeLeaEcx32(mov) == 0 && ut::utDecodeMovEcx32(lea) == 0 &&
           ut::utDecodeLeaEcx32(notRet) == 0 && ut::utDecodeMovEcx8(neg8) == 0 &&
           ut::utDecodeMovzxEcx32(movAl) == 0 && ut::utDecodeLeaEcx32(nullptr) == 0,
       "a wrong shape decodes to 0", "swapped opcodes, a ret 4, a negative disp8, null");
    const unsigned vt[5] = {0x10, 0x20, 0x30, 0x20, 0x40};
    ok(ut::utVtableSlotOf(vt, 5, 0x30) == 8 && ut::utVtableSlotOf(vt, 5, 0x20) == -2 &&
           ut::utVtableSlotOf(vt, 5, 0x99) == -1,
       "the slot finder: one, folded, absent", "+0x8 / -2 / -1");
}

// ---- cases 6 and 7: the gate itself --------------------------------------------------------------
// ut_log writes nothing without logInit, so the gate's own lines go nowhere and the summary is read
// back with bindingsSummary() instead.
void runGateHalf() {
    // 1. nothing has reported yet - every EARLY binding is outstanding, so the gate must refuse.
    ok(!ut::bindingsGate(80, 0), "an unreported binding turns it off",
       "no decoder has reported, so the gate must refuse to install anything");

    // 2. every EARLY binding reports a confirmed outcome - now it must pass.
    struct Early {
        const char* name;
        unsigned long long value;
    };
    const Early early[] = {
        {"gameEngine.stashSack", 0x2928},     {"gameEngine.transferSack", 0x2960},
        {"gameEngine.relicVaultSack", 0x2998}, {"gameEngine.caravanMode", 0x2924},
        {"cursor.itemId", 0x24},               {"item.replicaSlot", 0x1B0},
        {"item.classificationSlot", 0x1C4},    {"item.typeSlot", 0x194},
        {"item.stackSlot", 0x1B8},             {"cursor.capabilitySlots", 0x18},
        {"exe.transferPageAccessor", 0xC2F50}, {"exe.stashStreamOut", 0xBF990},
        {"exe.rightClickUnderPoint", 0xBFD10}, {"exe.heldPickupUnderPoint", 0xBEC00},
        {"page.cachedSack", 0xE8},             {"exe.rightClickUnderPoint.ret", 0xBFDD5},
        {"exe.heldPickupUnderPoint.ret", 0xBEC6F},
        {"exe.leftClickUnderPoint", 0xC006D},  {"exe.leftClickUnderPoint.ret", 0xC00CE},
        {"exe.leftClickHandler", 0xBFED0},     {"exe.leftClickHandler.frame", 0x68},
        {"cursor.disposal", 0xC10},   // PrimaryTransferActivate's disposal shape
        {"exe.transferPageDraw", 0xC31A0},   // the slot plates
        {"exe.itemBackground", 0x10A9B0},    // the rect route
        {"exe.quickMoveAdd.ret", 0x107A6C},
    };
    for (size_t i = 0; i < sizeof(early) / sizeof(early[0]); ++i) {
        ut::bindingsNote(early[i].name, early[i].value, true, "the offline harness said so");
    }
    ok(ut::bindingsGate(80, 0), "it passes once every early binding is confirmed",
       ut::bindingsSummary());
    printf("[bindings] the gate's INFO line reads: %s\n", ut::bindingsSummary());
    ok(strstr(ut::bindingsSummary(), "80 by export, 8 by signature") != nullptr &&
           strstr(ut::bindingsSummary(), "17 decoded (12 advisory)") != nullptr &&
           strstr(ut::bindingsSummary(), "3 structural (2 advisory), 1 literal") != nullptr &&
           strstr(ut::bindingsSummary(), "all confirmed") != nullptr &&
           strstr(ut::bindingsSummary(), "pending") == nullptr,
       "a healthy game reads 8 by signature, 17 decoded (12 advisory), all confirmed",
       ut::bindingsSummary());

    // 2a. THE CLASSIFICATION, row by row.
    {
        struct Want {
            const char* name;
            int cls, phase, gate;
        };
        const int D = ut::UT_BIND_DECODED, S = ut::UT_BIND_STRUCTURAL, G = ut::UT_BIND_SIGNATURE;
        const int L = ut::UT_BIND_LITERAL;
        const int E = ut::UT_BIND_EARLY;
        const int C = ut::UT_BIND_CRITICAL, A = ut::UT_BIND_ADVISORY;
        const Want want[] = {
            {"gameEngine.stashSack", D, E, C},     {"gameEngine.transferSack", D, E, C},
            {"gameEngine.relicVaultSack", D, E, C}, {"gameEngine.caravanMode", D, E, C},
            {"cursor.itemId", D, E, C},            {"item.replicaSlot", D, E, A},
            {"item.classificationSlot", D, E, A},  {"item.typeSlot", D, E, A},
            {"item.stackSlot", D, E, A},           {"cursor.capabilitySlots", D, E, A},
            {"msvc.string", S, E, C},              {"exe.transferPageAccessor", G, E, A},
            {"exe.stashStreamOut", G, E, A},
            {"exe.rightClickUnderPoint", G, E, A}, {"exe.heldPickupUnderPoint", G, E, A},
            {"exe.leftClickUnderPoint", G, E, A},  {"exe.leftClickHandler", G, E, A},
            {"exe.transferPageDraw", G, E, A},
            {"exe.itemBackground", G, E, A},
            {"page.cachedSack", D, E, A},          {"exe.rightClickUnderPoint.ret", D, E, A},
            {"exe.heldPickupUnderPoint.ret", D, E, A}, {"exe.leftClickUnderPoint.ret", D, E, A},
            {"exe.leftClickHandler.frame", D, E, A},
            {"cursor.disposal", D, E, A},
            {"exe.quickMoveAdd.ret", D, E, A},
            {"ui.stashKind", L, E, C},
            {"msvc.map", S, E, A},                 {"msvc.vector", S, E, A},
        };
        const int nWant = (int)(sizeof(want) / sizeof(want[0]));
        int bad = 0, advisory = 0;
        char firstBad[96] = "";
        for (int i = 0; i < ut::bindingsRowCount() || i < nWant; ++i) {
            const char* name = nullptr;
            int cls = -1, phase = -1, gate = -1;
            const bool have = ut::bindingsRowAt(i, &name, &cls, &phase, &gate);
            const bool match = have && i < nWant && strcmp(name, want[i].name) == 0 &&
                               cls == want[i].cls && phase == want[i].phase &&
                               gate == want[i].gate;
            if (have && gate == ut::UT_BIND_ADVISORY) ++advisory;
            if (match) continue;
            ++bad;
            if (!firstBad[0]) {
                _snprintf_s(firstBad, sizeof(firstBad), _TRUNCATE, "row %d: table %s, expected %s",
                            i, have ? name : "(none)", i < nWant ? want[i].name : "(none)");
            }
        }
        char detail[200];
        _snprintf_s(detail, sizeof(detail), _TRUNCATE, "%d rows, %d advisory, %d mismatch%s%s",
                    ut::bindingsRowCount(), advisory, bad, firstBad[0] ? ": " : "", firstBad);
        ok(bad == 0 && ut::bindingsRowCount() == nWant && advisory == 22,   // + 2; + 1
           "the classification is the expected list", detail);
    }

    // 2b. The early set is exactly the early reporters: every EARLY row that is not structural or
    // literal is in early[] above, and no row is LATE (TQ.exe is plain: nothing needs the game
    // thread).
    {
        int mustReport = 0, mismatched = 0, late = 0;
        char firstBad[96] = "";
        for (int i = 0; i < ut::bindingsRowCount(); ++i) {
            const char* name = nullptr;
            int cls = 0, phase = 0;
            if (!ut::bindingsRowAt(i, &name, &cls, &phase)) continue;
            if (phase == ut::UT_BIND_LATE) {
                ++late;
                continue;
            }
            if (cls == ut::UT_BIND_STRUCTURAL || cls == ut::UT_BIND_LITERAL) continue;
            ++mustReport;
            bool listed = false;
            for (size_t j = 0; j < sizeof(early) / sizeof(early[0]); ++j) {
                if (strcmp(early[j].name, name) == 0) { listed = true; break; }
            }
            if (!listed) {
                ++mismatched;
                if (!firstBad[0]) _snprintf_s(firstBad, sizeof(firstBad), _TRUNCATE, "%s", name);
            }
        }
        char detail[256];
        _snprintf_s(detail, sizeof(detail), _TRUNCATE,
                    "%d rows must report before the gate, the harness reports %u, %d late%s%s",
                    mustReport, (unsigned)(sizeof(early) / sizeof(early[0])), late,
                    firstBad[0] ? ", first mismatch: " : "", firstBad[0] ? firstBad : "");
        ok(mismatched == 0 && late == 0 && mustReport == (int)(sizeof(early) / sizeof(early[0])),
           "the early set is exactly the early reporters", detail);
    }

    // 3a. an ADVISORY row fails (a signature no longer matches) - the gate stays open.
    ut::bindingsNote("exe.stashStreamOut", 0, false, "exactly one match (found 0)");
    ok(ut::bindingsGate(80, 0) && ut::bindingsGateFailure()[0] == 0,
       "a failed advisory signature leaves the gate open", ut::bindingsSummary());
    ok(strstr(ut::bindingsSummary(), "confirmed except 1 advisory") != nullptr &&
           strstr(ut::bindingsSummary(), "7 by signature") != nullptr,   // 8 - the failed one
       "and the count line says which", ut::bindingsSummary());

    // 3b. a CRITICAL row fails - and the whole mod has to go off, naming it.
    ut::bindingsNote("gameEngine.transferSack", 0, false, "the const twin agrees");
    ok(!ut::bindingsGate(80, 0) && strcmp(ut::bindingsGateFailure(), "gameEngine.transferSack") == 0,
       "one failed critical binding turns the whole mod off, naming it",
       ut::bindingsGateFailure());
    // 3c. and a missing required export does the same, even with every row confirmed.
    ut::bindingsNote("gameEngine.transferSack", 0x2960, true, "restored");
    ok(!ut::bindingsGate(79, 1) && strcmp(ut::bindingsGateFailure(), "(required exports)") == 0,
       "a missing required export turns it off too", ut::bindingsGateFailure());
    printf("[bindings] and then it reads: %s\n", ut::bindingsSummary());
}

}  // namespace

int main() {
    printf("[bindings] the offline proof behind the mod's self-located bindings (x86)\n");

    runSyntheticHalf();
    runGateHalf();  // needs no input, so this suite always checks something

    const std::string dir = findGameDir();
    if (dir.empty()) {
        printf("[bindings] ***** SKIPPED the file halves - no Titan Quest AE folder: set TQ_GAME_DIR\n");
        printf("[bindings]       to the folder that holds TQ.exe, or install it through Steam.\n");
        printf("[bindings]       The folder is only ever READ. *****\n");
    } else {
        printf("[bindings] game folder found (%s), opened READ ONLY\n",
               getenv("TQ_GAME_DIR") ? "TQ_GAME_DIR" : "Steam registry");
        Pe engine, game, exe;
        const bool e = engine.load((dir + "\\Engine.dll").c_str());
        const bool g = game.load((dir + "\\Game.dll").c_str());
        const bool x = exe.load((dir + "\\TQ.exe").c_str());
        ok(e && g && x, "TQ.exe, Engine.dll, Game.dll are x86 PE files",
           e && g && x ? "all three read" : "one of them could not be read as PE32");
        if (e && g) runExportHalf(dir, &engine, &game);
        if (g) runDecoderHalf(game);
        if (x) {
            runExeHalf(exe);
            runViewHalf(exe);
            runTintHalf(exe);
            runRectRouteHalf(exe);   // the rect route
            runQuickMoveHalf(exe);
        }
    }

    printf("[bindings] %d check(s) ran, %d failed\n", g_ran, g_fail);
    printf(g_fail ? "[bindings] FAILURES\n" : "[bindings] ALL PASS\n");
    return g_fail ? 1 : 0;
}
