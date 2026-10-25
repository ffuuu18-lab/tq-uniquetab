// ut_cover.cpp - see ut_cover.h. The painter runs on a thread of its own (outside any gen_heap
// pair, after the start-up builds), writes into the mod's plates\ folder and never touches the
// engine; the game thread loads, swaps and restores.

#include "ut_cover.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "gen/arc_reader.h"
#include "gen/lattice_gen.h"
#include "hooks.h"
#include "tq_runtime.h"
#include "ut_caravangeo.h"
#include "ut_coverstep.h"
#include "ut_generate.h"
#include "ut_live.h"
#include "ut_log.h"
#include "ut_paths.h"
#include "ut_plate.h"
#include "ut_resname.h"
#include "ut_threads.h"
#include "ut_view.h"

namespace ut {

namespace {

const size_t kPageOwnerOff = 0xF1C;       // Transfer page -> its caravan window (TQ.exe 0xC34D1)
const size_t kCaravanVft = 0x21D438;      // the caravan window's vftable rva (0xC787A)
const size_t kCaravanPageOff = 0x1370;    // caravan window -> its Transfer page (0xC7BC6)
const size_t kCaravanBgOff = 0x1308;      // caravan window -> BackgroundImage widget (0xC5F9C)
const size_t kBitmapVft = 0x21C998;       // TQ.exe's bitmap class (0xC79EE)
const size_t kBitmapTexOff = 0x34;        // its GraphicsTexture* (slot +0x88, 0xB6F96)
const size_t kTexNameOff = 0x0C;          // the texture's file name, a std::string (Engine.dll 0x12018F)

// ---- the game thread's state -------------------------------------------------------------------
struct GridIn {
    int user, x, y, cols, rows;
};
GridIn g_grid = {0, 0, 0, 16, 15};

struct BgRead {
    bool read;                 // the reads ran without a fault
    bool shape;                // the caravan window and its background are the classes above
    unsigned char* caravan;
    const void* caravanVft;
    const void* pageBack;      // caravan +0x1370
    const void* bgVft;
    const TqTexture* tex;      // the field
};

bool g_off = false;                 // a fault or the engine's own write: no swap in this session
bool g_installed = false;           // the field holds g_mine
unsigned char* g_caravan = nullptr; // the caravan window written
const TqTexture* g_orig = nullptr;  // the engine's background when the swap went in
const TqTexture* g_mine = nullptr;  // the loaded painted copy (one at a time)
char g_mineLeaf[160] = "";
const TqTexture* g_dropped = nullptr;  // a copy out of every field, given back on a later frame
unsigned long long g_droppedAt = 0;  // the frame it left the mod's hands in
unsigned g_world = 0xFFFFFFFFu;     // the world the per-world caches belong to
bool g_noBackground = false;        // this world: nothing to repaint (said once)
bool g_placeSaid = false;           // this world: the background's place refused (said once)
int g_texW = 0, g_texH = 0;         // the engine's background size, read once per world
char g_texName[260] = "";
// the wanted copy: rebuilt only when the grid in force or the world changes
struct Key {
    int valid;
    int gx, gy, gc, gr, vx, vy, vc, vr, cell, w, h;
};
Key g_key = {};
char g_keyLeaf[160] = "";
bool g_keyFailed = false;           // its load was refused (said once)
long g_keyAsked = -1;               // the painter generation it was asked in
char g_loadHeap[MAX_PATH + 8];      // LoadTexture's std::string (the heap form)

// ---- the painter's shared state ----------------------------------------------------------------
struct PaintReq {
    char leaf[160];
    char tex[260];
    char res[MAX_PATH];
    char game[96];
    gen::LatticeGrid v, g;
    int w, h;
    char keep[160];   // the copy in the field when it was asked: kept on disk beside the new one
    int onDisk;       // the plate was on disk when it was asked: checked at once, without the quiet
};
SRWLOCK g_paintLock = SRWLOCK_INIT;
PaintReq g_req;
volatile LONG g_reqGen = 0;         // +1 per request
volatile LONG g_doneGen = 0;        // +1 per finished pass (painted, already there, or failed)
DWORD g_reqAt = 0;
HANDLE g_paintEvent = nullptr;
volatile LONG g_paintStarted = 0;
char g_platesDir[MAX_PATH] = "";
bool g_sourceAdded = false, g_sourceFailed = false;
char g_sourceHeap[MAX_PATH + 8];
std::vector<std::string> g_refused;   // the painter thread's: names a pass refused (said once)

// The engine's std::string (VS2012 x86) over TEXT without the CRT's allocator (ut_panel's helper).
bool stringOver(TqStdString* s, char* heap, size_t heapSize, const char* text, size_t n) {
    memset(s, 0, sizeof(*s));
    if (n < 16) {
        memcpy(s->buf, text, n);
        s->res = 15;
    } else {
        if (!heap || n + 1 > heapSize) return false;
        memcpy(heap, text, n);
        heap[n] = 0;
        s->ptr = heap;
        s->res = (unsigned)n;
    }
    s->size = (unsigned)n;
    return true;
}

// Pure reads of the caravan window behind the Transfer page, under SEH.
bool readBackground(const void* page, BgRead* o) {
    memset(o, 0, sizeof(*o));
    const unsigned char* exe = (const unsigned char*)GetModuleHandleA(nullptr);
    utGuardEnter();
    __try {
        unsigned char* c = *(unsigned char* const*)((const unsigned char*)page + kPageOwnerOff);
        o->caravan = c;
        if (c) {
            o->caravanVft = *(const void* const*)c;
            o->pageBack = *(const void* const*)(c + kCaravanPageOff);
            o->bgVft = *(const void* const*)(c + kCaravanBgOff);
            o->tex = *(const TqTexture* const*)(c + kCaravanBgOff + kBitmapTexOff);
        }
        o->read = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        o->read = false;
    }
    utGuardLeave();
    o->shape = o->read && o->caravan && exe && o->caravanVft == (const void*)(exe + kCaravanVft) &&
               o->pageBack == page && o->bgVft == (const void*)(exe + kBitmapVft);
    return o->read;
}

// The field of a caravan window the mod wrote, read again (its class checked first).
bool readField(unsigned char* caravan, const TqTexture** tex) {
    const unsigned char* exe = (const unsigned char*)GetModuleHandleA(nullptr);
    bool ok = false;
    utGuardEnter();
    __try {
        ok = exe && *(const void* const*)caravan == (const void*)(exe + kCaravanVft) &&
             *(const void* const*)(caravan + kCaravanBgOff) == (const void*)(exe + kBitmapVft);
        if (ok) *tex = *(const TqTexture* const*)(caravan + kCaravanBgOff + kBitmapTexOff);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    utGuardLeave();
    return ok;
}

bool writeField(unsigned char* caravan, const TqTexture* tex) {
    bool ok = false;
    utGuardEnter();
    __try {
        *(const TqTexture**)(caravan + kCaravanBgOff + kBitmapTexOff) = tex;
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    utGuardLeave();
    return ok;
}

TqGraphicsEngine* gfxNow() {
    TqEngine* e = engine();
    return (e && g_tq.EngineGetGraphicsEngine) ? g_tq.EngineGetGraphicsEngine(e) : nullptr;
}

// The object an engine call is about to be handed, read first: its first word and, for a class with
// a vftable (`virt`: GraphicsTexture, Engine, FileSystem), that table's first slot - pure reads under
// SEH. False when a read faults or the vftable is null: the call is not made, so a fault means the
// engine's state was bad before the call, never that the call broke it. The engine call itself runs
// outside any guarded frame (a fault in it is never swallowed).
bool preRead(const void* obj, bool virt) {
    if (!obj) return false;
    bool ok = false;
    utGuardEnter();
    __try {
        const void* const* vft = *(const void* const* const*)obj;
        ok = !virt || (vft && *vft);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    utGuardLeave();
    return ok;
}

// GraphicsEngine::UnloadTexture after the pre-read: a texture that cannot be read is one ERROR, kept
// (not unloaded), and the swap off.
void unloadTex(const TqTexture* t, const char* what) {
    if (!t || !g_tq.GfxUnloadTexture) return;
    TqGraphicsEngine* gfx = gfxNow();
    if (!gfx) return;
    if (!preRead(gfx, false) || !preRead(t, true)) {
        g_off = true;
        logE("view: %s %p could not be read before GraphicsEngine::UnloadTexture - it is not unloaded and "
             "the background is not swapped again in this session", what, (const void*)t);
        return;
    }
    g_tq.GfxUnloadTexture(gfx, t);
}

void swapOff(const char* why) {
    g_off = true;
    logE("view: %s - the caravan window's background is not swapped again in this session", why);
}

// The engine wrote its own texture into the field while the mod's was there: its slot +0x88 has
// unloaded the mod's copy; the engine's first texture, held by the mod since the swap, is given back.
void engineReplaced(const TqTexture* now, const char* when) {
    logE("view: the caravan window's background holds %p, not the mod's %p (%s) - the engine loaded its "
         "own texture since and unloaded the mod's; the texture the mod held (%p) is given back",
         (const void*)now, (const void*)g_mine, when, (const void*)g_orig);
    const TqTexture* held = g_orig;
    g_installed = false;
    g_mine = nullptr;
    g_mineLeaf[0] = 0;
    g_orig = nullptr;
    unloadTex(held, "the engine's background the mod held");
    swapOff("the engine replaced the swapped background");
}

// ---- the painter thread --------------------------------------------------------------------------
void removeSuperseded(const std::string& dir, const std::string& leaf, const char* keep) {
    const size_t at = leaf.rfind("_L");
    if (at == std::string::npos) return;
    const std::string prefix = leaf.substr(0, at + 2);
    WIN32_FIND_DATAA fd;
    size_t removed = 0, failed = 0;
    const char* const exts[2] = {".tex", ".src"};   // a plate and its background's fingerprint
    for (const char* ext : exts) {
        HANDLE h = FindFirstFileA((dir + "\\" + prefix + "*" + ext).c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        const bool plate = ext[1] == 't';
        do {
            const std::string n = fd.cFileName;
            if (_stricmp(n.c_str(), (leaf + ext).c_str()) == 0) continue;
            if (keep && keep[0] && _stricmp(n.c_str(), (std::string(keep) + ext).c_str()) == 0) continue;
            if (DeleteFileA((dir + "\\" + n).c_str())) removed += plate ? 1 : 0;
            else failed += plate ? 1 : 0;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    if (removed || failed)
        logI("layout plates: %zu superseded plate(s) of the %s background removed%s", removed, prefix.c_str(),
             failed ? " (some are in use - the next pass retries)" : "");
}

// The background's bytes as a short text (their count and FNV-1a 32), kept beside a painted copy
// as <leaf>.src: a custom game update that changes its background under the same name and size
// paints it again.
std::string blobPrint(const std::vector<std::uint8_t>& b) {
    unsigned h = 2166136261u;
    for (std::uint8_t c : b) {
        h ^= c;
        h *= 16777619u;
    }
    char s[48];
    _snprintf_s(s, sizeof(s), _TRUNCATE, "%zu %08x", b.size(), h);
    return s;
}

std::string readSmall(const std::string& path) {
    std::string s;
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return s;
    char buf[64];
    const size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    s.assign(buf, n);
    return s;
}

void paintOne(const PaintReq& r) {
    for (const std::string& f : g_refused)
        if (f == r.leaf) {
            logD("layout plates: %s was refused earlier in this session - not painted again", r.leaf);
            return;
        }
    const std::string dir = g_platesDir;
    const std::string dst = dir + "\\" + r.leaf + ".tex";
    const std::string side = dir + "\\" + r.leaf + ".src";
    std::string why;
    bool ok = false;
    size_t bytes = 0;
    DWORD t0 = GetTickCount();
    gen::LatticeReport rep;
    char gameDir[MAX_PATH] = {0};
    if (!utGameDirA(gameDir, sizeof(gameDir))) why = "the game folder is not known";
    try {
        gen::ResourceArcs res;
        if (why.empty()) res.setRoot(gameDir, r.res);
        std::vector<std::uint8_t> blob, out;
        std::string err;
        std::string texPath = r.tex;   // the engine's name, in the archives' separator
        for (char& ch : texPath)
            if (ch == '/') ch = '\\';
        if (!why.empty()) {
        } else if (!res.read(texPath, blob, 0, &err) || blob.empty()) {
            why = std::string("the background ") + r.tex + " does not read from the archives" +
                  (err.empty() ? "" : " (" + err + ")");
        } else {
            const std::string print = blobPrint(blob);
            if (GetFileAttributesA(dst.c_str()) != INVALID_FILE_ATTRIBUTES && readSmall(side) == print) {
                logD("layout plates: the set _L%s is already on disk (%s)", strrchr(r.leaf, 'L') + 1, r.leaf);
                return;
            }
            logI("layout plates: the grid in force is %d,%d %d x %d cells of %d (the derived %d,%d %d x %d) - "
                 "painting 1 plate(s) %s for the %s game off the game thread; the caravan shows its own "
                 "background meanwhile",
                 r.g.x, r.g.y, r.g.cols, r.g.rows, r.g.cellW, r.v.x, r.v.y, r.v.cols, r.v.rows, r.leaf,
                 r.game[0] ? r.game : "start-up");
            t0 = GetTickCount();
            if (gen::latticePaint(blob, r.v, r.g, out, &rep, &why)) {
                gen::LatticeSurface s;
                if (!gen::latticeDecodeTex(out, s, &why) || s.w != r.w || s.h != r.h) {
                    if (why.empty()) why = "the painted size differs from the engine's background";
                } else {
                    CreateDirectoryA(dir.c_str(), nullptr);
                    const std::string tmp = dst + ".tmp";
                    DeleteFileA(tmp.c_str());
                    FILE* f = nullptr;
                    if (fopen_s(&f, tmp.c_str(), "wb") == 0 && f) {
                        const bool w = fwrite(out.data(), 1, out.size(), f) == out.size();
                        fclose(f);
                        if (w && MoveFileExA(tmp.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                            ok = true;
                            bytes = out.size();
                            FILE* g = nullptr;   // a missing fingerprint only paints it again
                            if (fopen_s(&g, side.c_str(), "wb") == 0 && g) {
                                fwrite(print.data(), 1, print.size(), g);
                                fclose(g);
                            }
                        } else {
                            DeleteFileA(tmp.c_str());
                            why = "the plate could not be written into " + dir;
                        }
                    } else {
                        why = "the plate could not be written into " + dir;
                    }
                }
            }
        }
    } catch (...) {
        why = "out of memory";
    }
    if (!ok) {
        try {
            g_refused.push_back(r.leaf);
        } catch (...) {
        }
        logE("layout plates: painting %s FAILED - %s. The caravan keeps its own background under the moved grid "
             "(not asked again in this session)",
             r.leaf, why.c_str());
        return;
    }
    logI("layout plates: 1 plate(s) %s painted in %lu ms (%d band pixels erased, %d cells stamped, %.1f MB) "
         "into \"%s\" - the page switches to it",
         r.leaf, GetTickCount() - t0, rep.erased, rep.frames, (double)bytes / 1048576.0, dir.c_str());
    removeSuperseded(dir, r.leaf, r.keep);
}

// The painter's own thread: a request is painted after 1 s of quiet (GD's layout plates thread) and
// never while the start-up builds run; a plate already on disk is checked at once (a page change
// between the item grid and the relic area). It never touches the engine.
DWORD WINAPI paintThread(LPVOID) {
    long seen = 0;
    for (;;) {
        WaitForSingleObject(g_paintEvent, 1000);
        const long gen = InterlockedCompareExchange(&g_reqGen, 0, 0);
        if (gen == seen) continue;
        AcquireSRWLockShared(&g_paintLock);
        const DWORD at = g_reqAt;
        const bool quick = g_req.onDisk != 0;
        ReleaseSRWLockShared(&g_paintLock);
        if ((!quick && GetTickCount() - at < 1000) || gamesStartupBusy()) {
            Sleep(250);
            SetEvent(g_paintEvent);
            continue;
        }
        PaintReq r;
        AcquireSRWLockShared(&g_paintLock);
        r = g_req;
        const long took = InterlockedCompareExchange(&g_reqGen, 0, 0);
        ReleaseSRWLockShared(&g_paintLock);
        seen = took;
        paintOne(r);
        InterlockedExchange(&g_doneGen, took);
    }
}

void askPaint(const PaintReq& r) {
    if (!InterlockedExchange(&g_paintStarted, 1)) {
        g_paintEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        HANDLE t = g_paintEvent ? CreateThread(nullptr, kUtStackPainter, &paintThread, nullptr,
                                               STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr)
                                : nullptr;
        if (!t) {
            logW("layout plates: their thread could not be started - the caravan keeps its own background "
                 "under a moved grid in this session");
            g_off = true;
            return;
        }
        SetThreadPriority(t, THREAD_PRIORITY_BELOW_NORMAL);
        CloseHandle(t);
    }
    AcquireSRWLockExclusive(&g_paintLock);
    g_req = r;
    g_reqAt = GetTickCount();
    const long gen = InterlockedIncrement(&g_reqGen);
    ReleaseSRWLockExclusive(&g_paintLock);
    g_keyAsked = gen;
    SetEvent(g_paintEvent);
}

// ---- the swap ------------------------------------------------------------------------------------
void forgetWorld() {
    g_world = viewWorldGeneration();
    g_noBackground = false;
    g_placeSaid = false;
    g_texW = g_texH = 0;
    g_texName[0] = 0;
    g_key.valid = 0;
    g_keyLeaf[0] = 0;
    g_keyFailed = false;
    g_keyAsked = -1;
}

// Once per world: the inputs, said in one line. The place is the game's set's (its W line: the
// caravan window's BackgroundImage record), not read from the widget.
void sayInputs(const BgRead& b, const void* page) {
    UtCaravanGeo d;
    if (!liveCaravanGeometry(&d)) d = utCaravanGeoBase();
    char place[96];
    if (d.bgKnown)
        _snprintf_s(place, sizeof(place), _TRUNCATE, "%d,%d (its record's bitmapPosition, uniq-geometry.txt)",
                    d.bgX, d.bgY);
    else
        _snprintf_s(place, sizeof(place), _TRUNCATE, "unknown (uniq-geometry.txt has none)");
    const unsigned char* exe = (const unsigned char*)GetModuleHandleA(nullptr);
    if (b.shape && b.tex) {
        if (g_tq.TextureGetWidth && g_tq.TextureGetHeight) {   // engine getters: not guarded
            g_texW = g_tq.TextureGetWidth(b.tex);
            g_texH = g_tq.TextureGetHeight(b.tex);
        }
        if (!safeStdString((const unsigned char*)b.tex + kTexNameOff, g_texName, sizeof(g_texName)))
            g_texName[0] = 0;
    }
    logI("view: the caravan window's background at caravan+0x1308 (the Transfer page's owner [page+0xF1C] %p, "
         "vftable %p (%s TQ.exe's caravan window +0x21D438), its Transfer page [+0x1370] %p (%s the page)): "
         "vftable %p (%s TQ.exe's bitmap class +0x21C998), texture [+0x34] %p %dx%d \"%s\", place %s",
         (void*)b.caravan, b.caravanVft, exe && b.caravanVft == (const void*)(exe + kCaravanVft) ? "=" : "NOT",
         b.pageBack, b.pageBack == page ? "=" : "NOT", b.bgVft,
         exe && b.bgVft == (const void*)(exe + kBitmapVft) ? "=" : "NOT", (const void*)b.tex, g_texW, g_texH,
         g_texName, place);
}

void restoreNow(const char* why) {
    if (!g_installed) return;
    const char* w = why ? why : "?";
    const TqTexture* now = nullptr;
    const bool read = g_caravan && readField(g_caravan, &now);
    if (read && now != g_mine) {
        engineReplaced(now, w);
        return;
    }
    const bool wrote = read && writeField(g_caravan, g_orig);
    const UtCoverBack back = utCoverRestoreOf(read, read, wrote);
    const TqTexture* held = g_orig;
    const TqTexture* was = g_mine;
    g_installed = false;
    g_orig = nullptr;
    if (!back.keepMine) {   // the field (or the gone window's destructor) has the mod's copy
        g_mine = nullptr;
        g_mineLeaf[0] = 0;
    }
    if (!read) {
        g_caravan = nullptr;
        logI("view: the caravan window's background is not written back (%s) - the caravan window is gone; "
             "its destructor unloaded the mod's copy, the engine's texture the mod held (%p) is given back",
             w, (const void*)held);
    } else if (wrote) {
        logI("view: the caravan window's own background back (%s) - [+0x34] %p -> %p", w, (const void*)was,
             (const void*)held);
    }
    if (back.unloadOrig) unloadTex(held, "the engine's background the mod held");
    if (back.off)
        swapOff("writing the caravan window's background back faulted (the window keeps the mod's copy and "
                "unloads it itself; the engine's texture the mod held is given back)");
}

// The held copy leaves the mod's hands: given back on a later frame, since this frame's
// background may have been drawn with it. One waits at a time.
void dropMine(unsigned long long frame) {
    if (!g_mine) return;
    if (g_dropped) unloadTex(g_dropped, "the previous painted background");
    logI("view: the painted background %s.tex (%p) is given back - the grid in force changed", g_mineLeaf,
         (const void*)g_mine);
    g_dropped = g_mine;
    g_droppedAt = frame;
    g_mine = nullptr;
    g_mineLeaf[0] = 0;
}

void dropDue(unsigned long long frame) {
    if (!g_dropped || frame == g_droppedAt) return;
    const TqTexture* t = g_dropped;
    g_dropped = nullptr;
    unloadTex(t, "the previous painted background");
}

// One LoadTexture of the painted copy by name (the plates\ folder is a file-system source).
// *refused: the name has a part the engine does not take (ut_resname.h) - it is not handed over.
const TqTexture* loadPlate(const char* leaf, int* w, int* h, bool* refused) {
    *w = *h = 0;
    *refused = false;
    if (!g_tq.GfxLoadTexture) return nullptr;
    char name[192];
    _snprintf_s(name, sizeof(name), _TRUNCATE, "%s.tex", leaf);
    if (!utResNameOk(name)) {
        *refused = true;
        return nullptr;
    }
    TqStdString s;
    if (!stringOver(&s, g_loadHeap, sizeof(g_loadHeap), name, strlen(name))) return nullptr;
    TqGraphicsEngine* gfx = gfxNow();
    if (!gfx) return nullptr;
    if (!preRead(gfx, false)) {
        swapOff("the graphics engine could not be read before GraphicsEngine::LoadTexture of the painted "
                "background");
        return nullptr;
    }
    const TqTexture* t = g_tq.GfxLoadTexture(gfx, &s);
    if (t && g_tq.TextureGetWidth && g_tq.TextureGetHeight) {
        *w = g_tq.TextureGetWidth(t);
        *h = g_tq.TextureGetHeight(t);
    }
    return t;
}

}  // namespace

bool coverGridShown() {
    return !g_off && g_installed && g_mine != nullptr && g_keyLeaf[0] && strcmp(g_mineLeaf, g_keyLeaf) == 0;
}

void coverSetGrid(bool user, int x, int y, int cols, int rows) {
    g_grid.user = user ? 1 : 0;
    g_grid.x = x;
    g_grid.y = y;
    g_grid.cols = cols;
    g_grid.rows = rows;
}

void coverPrepare(bool noWorldYet) {
    if (g_sourceAdded || g_sourceFailed || !noWorldYet || !generateDone()) return;
    if (!g_tq.EngineGetFileSystem || !g_tq.FsAddSource || !g_tq.ppEngine) {
        g_sourceFailed = true;
        logW("layout plates: Engine::GetFileSystem / FileSystem::AddSource are not bound - the caravan keeps "
             "its own background under a moved grid");
        return;
    }
    TqEngine* e = engine();   // the data export, a guarded read
    if (!e) return;           // no engine yet: the next frame
    if (!preRead(e, true)) {
        g_sourceFailed = true;
        logE("layout plates: the engine could not be read before Engine::GetFileSystem - the plates folder is "
             "not added and the caravan keeps its own background under a moved grid in this session");
        return;
    }
    void* fs = g_tq.EngineGetFileSystem(e);
    if (!fs) return;   // no file system yet: the next frame
    HMODULE self = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)&coverPrepare, &self) || !self) {
        g_sourceFailed = true;
        return;
    }
    utModPathA(self, "plates", g_platesDir, sizeof(g_platesDir));
    CreateDirectoryA(g_platesDir, nullptr);
    char dir[MAX_PATH + 2];   // the engine's own form: '/' separators and a trailing '/'
    size_t n = 0;
    for (const char* p = g_platesDir; *p && n + 2 < sizeof(dir); ++p) dir[n++] = *p == '\\' ? '/' : *p;
    dir[n++] = '/';
    dir[n] = 0;
    TqStdString s;
    bool ok = stringOver(&s, g_sourceHeap, sizeof(g_sourceHeap), dir, n);
    if (ok && !preRead(fs, true)) {
        g_sourceFailed = true;
        logE("layout plates: the file system could not be read before FileSystem::AddSource - the plates "
             "folder is not added and the caravan keeps its own background under a moved grid in this session");
        return;
    }
    if (ok) ok = g_tq.FsAddSource(fs, 1, &s, nullptr, 0, 0, 0);
    if (!ok) {
        g_sourceFailed = true;
        logW("layout plates: FileSystem::AddSource refused the mod's plates folder \"%s\" - the caravan keeps "
             "its own background under a moved grid", g_platesDir);
        return;
    }
    g_sourceAdded = true;
    logI("layout plates: the mod's plates\\ folder \"%s\" is a directory source of the engine's file system "
         "(FileSystem::AddSource, partition 1); a painted caravan background loads by name", g_platesDir);
}

void coverRestore(const char* why, bool worldGone) {
    restoreNow(why);
    if (!worldGone) return;
    if (g_mine && !g_installed) unloadTex(g_mine, "the painted background");   // the mod's own reference
    if (g_dropped) unloadTex(g_dropped, "the previous painted background");
    g_dropped = nullptr;
    g_mine = nullptr;
    g_mineLeaf[0] = 0;
    g_caravan = nullptr;
    g_orig = nullptr;
    g_installed = false;
    forgetWorld();
}

// The copy the grid in force names, loaded once the painter has finished the pass it was asked
// in; null (g_keyFailed set, said once) when it cannot be shown.
const TqTexture* loadKeyed() {
    // loaded once the painter has finished the pass this name was asked in
    char path[MAX_PATH + 192];
    _snprintf_s(path, sizeof(path), _TRUNCATE, "%s\\%s.tex", g_platesDir, g_keyLeaf);
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {   // never ask the engine for a missing name
        g_keyFailed = true;   // the painter said why, once
        return nullptr;
    }
    int w = 0, h = 0;
    bool refused = false;
    const TqTexture* t = loadPlate(g_keyLeaf, &w, &h, &refused);
    if (refused) {
        g_keyFailed = true;
        char plate[192];
        _snprintf_s(plate, sizeof(plate), _TRUNCATE, "%s.tex", g_keyLeaf);
        logW("view: the painted background's name \"%s\" has a part of %zu characters - the engine "
             "takes 1..63 between '/' or '\\', so it is NOT loaded; the caravan keeps its own background",
             plate, utResLongestPart(plate));
        return nullptr;
    }
    if (!t || w != g_texW || h != g_texH) {
        g_keyFailed = true;
        logW("view: GraphicsEngine::LoadTexture \"%s.tex\" -> %p %dx%d (need %dx%d) - REJECTED, the caravan "
             "keeps its own background", g_keyLeaf, (const void*)t, w, h, g_texW, g_texH);
        if (t) unloadTex(t, "the rejected painted background");
        return nullptr;
    }
    return t;
}

void coverTick(void* page, int pass) {
    if (g_off || !page || pass == 1) return;
    if (g_world != viewWorldGeneration()) forgetWorld();
    static unsigned long long s_frame = ~0ull;   // once a frame
    const unsigned long long frame = hookFrameCount();
    if (frame == s_frame) return;
    s_frame = frame;
    dropDue(frame);
    if (g_noBackground && !g_installed) return;
    BgRead b;
    if (!readBackground(page, &b)) {
        swapOff("the caravan window behind the Transfer page could not be read (page+0xF1C)");
        return;
    }
    if (g_key.valid == 0 && g_texName[0] == 0 && !g_noBackground) {
        sayInputs(b, page);
        if (!b.shape || !b.tex || g_texW <= 0 || g_texH <= 0 || !g_texName[0]) {
            g_noBackground = true;
            logI("view: no caravan background to repaint in this world (%s) - the mod draws nothing over the "
                 "window's own lattice", !b.shape ? "the caravan window is not the shape read in TQ.exe"
                                         : !b.tex ? "its texture field is empty"
                                                  : "its size or name could not be read");
            if (!g_installed) return;
        } else {
            g_key.valid = -1;   // read, not keyed yet
        }
    }
    // the wanted copy: the grid in force (the player's, or the collection's area) while the view is
    // ON on the Transfer page, when it differs from the derived one
    UtCaravanGeo d;
    if (!liveCaravanGeometry(&d)) d = utCaravanGeoBase();
    int winW = 0, winH = 0;
    liveCaravanWindow(&winW, &winH, nullptr, nullptr);
    const int cell = d.cols > 0 ? d.w / d.cols : 32;
    const bool differs = g_grid.user && !(g_grid.x == d.x0 && g_grid.y == d.y0 && g_grid.cols == d.cols &&
                                          g_grid.rows == d.rows);
    const bool want = viewOn() && plateTransferVisible() && differs && b.shape && b.tex;
    const bool sizeOk = g_texW == winW && g_texH == winH;
    if (want && !sizeOk && !g_keyFailed) {
        g_keyFailed = true;
        logI("view: the caravan background is %dx%d, the caravan window %dx%d - its pixels are not the "
             "window's units, so its lattice is not repainted", g_texW, g_texH, winW, winH);
    }
    // the background's place in the window: the game's set's (the W line, its BackgroundImage
    // record's bitmapPosition), window units; refused (no repaint, said once a world) when the set
    // carries none or the derived grid would leave the background there
    int ox = 0, oy = 0;
    const int place = utCaravanBgPlace(d, g_texW, g_texH, &ox, &oy);
    if (want && sizeOk && place != kUtBgPlaced && !g_placeSaid) {
        g_placeSaid = true;
        if (place == kUtBgUnknown)
            logI("view: this game's uniq-geometry.txt has no place for the caravan background (its W line "
                 "carries none) - its lattice is not repainted");
        else
            logW("view: the caravan background %s %dx%d at %d,%d does not cover the derived grid %d,%d %dx%d - "
                 "its lattice is not repainted", g_texName, g_texW, g_texH, d.bgX, d.bgY, d.x0, d.y0, d.w, d.h);
    }
    if (want && sizeOk && place == kUtBgPlaced) {
        UtCaravanBgGrid bv, bf;   // the derived grid and the grid in force in the background's pixels
        utCaravanBgGrids(d, ox, oy, g_grid.x, g_grid.y, g_grid.cols, g_grid.rows, &bv, &bf);
        Key k = {1, bf.x, bf.y, bf.cols, bf.rows, bv.x, bv.y, bv.cols, bv.rows, cell, g_texW, g_texH};
        if (memcmp(&k, &g_key, sizeof(k)) != 0) {   // a new grid in force: a new name (allocates once)
            g_key = k;
            g_keyFailed = false;
            g_keyAsked = -1;
            gen::LatticeGrid v, g;
            v.x = k.vx; v.y = k.vy; v.cols = k.vc; v.rows = k.vr; v.cellW = v.cellH = cell;
            g.x = k.gx; g.y = k.gy; g.cols = k.gc; g.rows = k.gr; g.cellW = g.cellH = cell;
            char tag[96] = "";
            gameNow(tag, sizeof(tag), nullptr, nullptr);
            try {
                const std::string leaf = gen::latticeLeafName(cell, cell, g_texW, g_texH, tag,
                                                              gen::latticeHash(g_texName, g_texW, g_texH, v, g));
                _snprintf_s(g_keyLeaf, sizeof(g_keyLeaf), _TRUNCATE, "%s", leaf.c_str());
            } catch (...) {
                g_keyLeaf[0] = 0;
            }
            char plate[192];
            _snprintf_s(plate, sizeof(plate), _TRUNCATE, "%s.tex", g_keyLeaf);
            if (g_keyLeaf[0] && !utResNameOk(plate)) {   // never painted, never handed to the engine
                g_keyFailed = true;
                logW("view: the painted background's name \"%s\" has a part of %zu characters - the engine "
                     "takes 1..63 between '/' or '\\', so it is NOT painted or loaded; the caravan keeps its "
                     "own background", plate, utResLongestPart(plate));
                g_keyLeaf[0] = 0;
            }
            if (g_keyLeaf[0]) {
                logI("view: the grid in force %d,%d %d x %d cells differs from the derived %d,%d %d x %d - the "
                     "caravan background %s %dx%d is shown as %s", g_grid.x, g_grid.y, g_grid.cols, g_grid.rows,
                     d.x0, d.y0, d.cols, d.rows, g_texName, g_texW, g_texH, g_keyLeaf);
                PaintReq r;
                memset(&r, 0, sizeof(r));
                _snprintf_s(r.leaf, sizeof(r.leaf), _TRUNCATE, "%s", g_keyLeaf);
                _snprintf_s(r.tex, sizeof(r.tex), _TRUNCATE, "%s", g_texName);
                _snprintf_s(r.game, sizeof(r.game), _TRUNCATE, "%s", tag);
                if (tag[0] && !gameResourcesOf(tag, r.res, sizeof(r.res))) r.res[0] = 0;
                r.v = v;
                r.g = g;
                r.w = g_texW;
                r.h = g_texH;
                _snprintf_s(r.keep, sizeof(r.keep), _TRUNCATE, "%s", g_mineLeaf);
                char onDisk[MAX_PATH + 192];   // once per new grid in force, never per frame
                _snprintf_s(onDisk, sizeof(onDisk), _TRUNCATE, "%s\\%s.tex", g_platesDir, g_keyLeaf);
                r.onDisk = GetFileAttributesA(onDisk) != INVALID_FILE_ATTRIBUTES ? 1 : 0;
                if (!g_sourceAdded) {
                    g_keyFailed = true;
                    logW("view: the plates folder is not a file-system source (it is added before the first "
                         "world) - the caravan keeps its own background under the moved grid");
                } else {
                    askPaint(r);
                }
            }
        }
    }
    UtCoverIn in;
    in.installed = g_installed;
    in.haveMine = g_mine != nullptr;
    in.mineIsKey = g_mine != nullptr && strcmp(g_mineLeaf, g_keyLeaf) == 0;
    in.keyed = g_keyLeaf[0] != 0;
    in.keyFailed = g_keyFailed;
    in.sameCaravan = b.caravan == g_caravan && b.shape;
    in.fieldIsMine = b.tex == g_mine;
    in.want = want;
    in.sizeOk = sizeOk && place == kUtBgPlaced;   // the background's pixels map onto the window's units
    in.painted = g_keyAsked >= 0 && InterlockedCompareExchange(&g_doneGen, 0, 0) >= g_keyAsked;
    const unsigned act = utCoverStep(in);
    if (act & kUtCoverEngineWrote) {
        engineReplaced(b.tex, "the page draw");
        return;
    }
    if (act & kUtCoverRestore)
        restoreNow(!in.sameCaravan ? "another caravan window"
                   : !viewOn()     ? "the view is OFF"
                   : !differs      ? "the derived grid"
                   : !want         ? "not the Transfer page"
                   : !sizeOk       ? "the background's size"
                   : place != kUtBgPlaced ? "the background's place"
                                   : "a new grid in force");
    if (g_off) return;
    if (act & kUtCoverDropMine) dropMine(frame);
    if (act & kUtCoverLoad) {
        const TqTexture* t = loadKeyed();
        if (!t) return;
        g_mine = t;
        _snprintf_s(g_mineLeaf, sizeof(g_mineLeaf), _TRUNCATE, "%s", g_keyLeaf);
    }
    if (act & kUtCoverSwap) {
        // the new copy over the one in the field: the engine's pointer stays held, the replaced
        // copy is given back on a later frame (this frame may have drawn it)
        const TqTexture* t = loadKeyed();
        if (!t) return;
        const TqTexture* was = g_mine;
        if (!writeField(b.caravan, t)) {
            unloadTex(t, "the painted background not written");
            swapOff("writing the caravan window's background faulted");
            return;
        }
        dropMine(frame);
        g_mine = t;
        _snprintf_s(g_mineLeaf, sizeof(g_mineLeaf), _TRUNCATE, "%s", g_keyLeaf);
        logI("view: the caravan window's background swapped for %s.tex - [+0x34] %p -> %p; the lattice "
             "follows the grid in force", g_mineLeaf, (const void*)was, (const void*)g_mine);
    }
    if (act & kUtCoverInstall) {
        // in: the engine's pointer kept, the mod's written
        if (!writeField(b.caravan, g_mine)) {
            swapOff("writing the caravan window's background faulted");
            return;
        }
        g_installed = true;
        g_caravan = b.caravan;
        g_orig = b.tex;
        logI("view: the caravan window's background swapped for %s.tex - [+0x34] %p -> %p; the lattice follows "
             "the grid in force", g_mineLeaf, (const void*)b.tex, (const void*)g_mine);
    }
}

}  // namespace ut
