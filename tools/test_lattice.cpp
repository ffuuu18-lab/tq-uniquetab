// test_lattice.cpp - the lattice painter (src/gen/lattice_gen.*), offline.
//
// Part 1 (no game needed): a synthetic background with a known lattice; the painted copy is
// sampled at known pixels - the old lines gone where no new line lies, the new lines' bands at the
// grid in force, the cells' insides untouched, the headers kept byte for byte.
// Part 2 (the installed game, read only; skipped when %TQ_GAME_DIR% / Steam does not resolve):
// the caravan window's own background, XPack\UI\Caravan\CaravanWindow01.tex out of
// Resources\XPack\UI.arc, painted for a 16 x 12 grid one cell right and two cells down; the
// vanilla lattice's lines are gone above the moved grid, its own lines carry the vanilla band
// profile, the window's frame is unchanged.
// Part 3: the background swap's decisions (src/ut_coverstep.h) over a ledger of the textures'
// counts - a new grid while the swap is in, OFF -> ON, the engine's own write, the window gone or
// replaced, the world down, a failed write-back, the derived grid back.
// Part 4 (TQ_TEST_MODS_DIR): a custom game's 20 x 20 caravan background painted for 20 x 19 and
// for a moved 16 x 15, its grids placed through the background's place on the W line (0,11).

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "gen/arc_reader.h"
#include "gen/lattice_gen.h"
#include "ut_caravangeo.h"
#include "ut_coverstep.h"
#include "ut_resname.h"

namespace {

int g_fail = 0, g_pass = 0;

void check(bool ok, const char* what) {
    if (ok) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("FAIL: %s\n", what);
    }
}

// ---- part 1: a synthetic background --------------------------------------------------------------
const int kW = 220, kH = 200;
const std::uint8_t kGround = 41, kLight = 140, kNear = 23, kFar = 32;

void put(std::vector<std::uint8_t>& px, int x, int y, std::uint8_t v) {
    std::uint8_t* p = &px[(std::size_t(y) * kW + x) * 3];
    p[0] = v;
    p[1] = std::uint8_t(v + 1);   // B, G, R kept apart so a channel swap shows
    p[2] = std::uint8_t(v + 2);
}
std::uint8_t lum(const std::vector<std::uint8_t>& blob, std::size_t off, int w, int x, int y) {
    return blob[off + (std::size_t(y) * w + x) * 3];
}

std::vector<std::uint8_t> synthTex(const gen::LatticeGrid& v) {
    std::vector<std::uint8_t> px(std::size_t(kW) * kH * 3, 0);
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) put(px, x, y, kGround);
    const std::uint8_t prof[5] = {kFar, kNear, kLight, kNear, kFar};
    for (int k = 0; k <= v.cols; ++k)   // vertical bands
        for (int d = -2; d <= 2; ++d)
            for (int y = v.y - 1; y <= v.y - 1 + v.rows * v.cellH; ++y) put(px, v.x - 1 + k * v.cellW + d, y, prof[d + 2]);
    for (int k = 0; k <= v.rows; ++k)   // horizontal bands (over the vertical ones: the crossing)
        for (int d = -2; d <= 2; ++d)
            for (int x = v.x - 1; x <= v.x - 1 + v.cols * v.cellW; ++x) {
                const int y = v.y - 1 + k * v.cellH + d;
                const bool onV = ((x - (v.x - 1)) % v.cellW) == 0;
                put(px, x, y, onV ? kLight : prof[d + 2]);
            }
    // TEX 2 + DDSR 24 bpp, the vanilla background's header form
    std::vector<std::uint8_t> blob(13 + 4 + 124, 0);
    std::memcpy(blob.data(), "TEX\x02", 4);
    const std::uint32_t body = std::uint32_t(4 + 124 + px.size());
    std::memcpy(blob.data() + 9, &body, 4);
    std::uint8_t* d = blob.data() + 13;
    std::memcpy(d, "DDSR", 4);
    const std::uint32_t hs = 124, fl = 0x1007, hh = kH, ww = kW, one = 1, pfs = 32, pff = 0x40, bpp = 24;
    std::memcpy(d + 4, &hs, 4);
    std::memcpy(d + 8, &fl, 4);
    std::memcpy(d + 12, &hh, 4);
    std::memcpy(d + 16, &ww, 4);
    std::memcpy(d + 28, &one, 4);
    std::memcpy(d + 4 + 72, &pfs, 4);
    std::memcpy(d + 4 + 76, &pff, 4);
    std::memcpy(d + 4 + 84, &bpp, 4);
    blob.insert(blob.end(), px.begin(), px.end());
    return blob;
}

void partSynthetic() {
    gen::LatticeGrid v;
    v.x = 20;
    v.y = 30;
    v.cols = 5;
    v.rows = 4;
    gen::LatticeGrid g;   // 3 x 2 cells, moved right and down by a cell and a half
    g.x = 68;
    g.y = 78;
    g.cols = 3;
    g.rows = 2;
    const std::vector<std::uint8_t> in = synthTex(v);
    std::vector<std::uint8_t> out;
    gen::LatticeReport rep;
    std::string why;
    check(gen::latticePaint(in, v, g, out, &rep, &why), "synthetic: the paint runs");
    if (out.size() != in.size()) {
        check(false, "synthetic: the painted file has the background's size");
        return;
    }
    const std::size_t off = 13 + 4 + 124;
    check(std::memcmp(in.data(), out.data(), off) == 0, "synthetic: the TEX and DDS headers are kept byte for byte");
    check(rep.frames == 6, "synthetic: six cells stamped");
    check(rep.erased > 0, "synthetic: the vanilla bands counted");
    // an old interior line where no new line lies: vertical x = 20 - 1 + 32 = 51, at y = 40
    check(lum(out, off, kW, 51, 40) == kGround, "synthetic: the old vertical line at x 51 is gone");
    check(lum(out, off, kW, 49, 40) == kGround && lum(out, off, kW, 53, 40) == kGround,
          "synthetic: its band is gone too");
    // an old horizontal line: y = 30 - 1 + 32 = 61, at x = 30
    check(lum(out, off, kW, 30, 61) == kGround, "synthetic: the old horizontal line at y 61 is gone");
    // the old outer line inside the panel: x = 19 at y = 40
    check(lum(out, off, kW, 19, 40) == kGround, "synthetic: the old outer line is gone inside the panel");
    // the new lattice: vertical lines at x = 67 + 32 k, horizontal at y = 77 + 32 k
    check(lum(out, off, kW, 67 + 32, 90) == kLight, "synthetic: a new vertical line at x 99");
    check(lum(out, off, kW, 66 + 32, 90) == kNear && lum(out, off, kW, 65 + 32, 90) == kFar,
          "synthetic: the new line carries the band profile");
    check(lum(out, off, kW, 67, 77 + 64) == kLight, "synthetic: the new bottom line at y 141");
    check(lum(out, off, kW, 67 + 96, 77 + 32) == kLight, "synthetic: the new crossing at 163,109");
    // the inside of a new cell and the outside of the grid
    check(lum(out, off, kW, 67 + 16, 77 + 16) == kGround, "synthetic: a new cell's inside is the ground");
    check(lum(out, off, kW, 200, 20) == kGround, "synthetic: outside every grid nothing changes");
    // the channels stay in place
    check(out[off + (std::size_t(90) * kW + 99) * 3 + 2] == kLight + 2, "synthetic: B, G, R kept apart");
    // a 0 x 0 want (the relic page): the lattice erased, nothing stamped
    {
        gen::LatticeGrid none;
        none.x = g.x;
        none.y = g.y;
        std::vector<std::uint8_t> out0;
        gen::LatticeReport rep0;
        check(gen::latticePaint(in, v, none, out0, &rep0, &why) && out0.size() == in.size() &&
                  rep0.frames == 0 && rep0.erased > 0 && lum(out0, off, kW, 51, 40) == kGround &&
                  lum(out0, off, kW, 19, 40) == kGround && lum(out0, off, kW, 67 + 32, 90) == kGround &&
                  std::memcmp(in.data(), out0.data(), off) == 0,
              "synthetic: a 0 x 0 grid erases the lattice and stamps nothing");
    }
    // a grid that leaves the texture is cut, not refused
    gen::LatticeGrid edge = g;
    edge.x = 200;
    edge.y = 180;
    std::vector<std::uint8_t> out2;
    check(gen::latticePaint(in, v, edge, out2, &rep, &why) && rep.frames >= 1,
          "synthetic: a grid crossing the texture's edge is cut to it");
    // refusals
    std::vector<std::uint8_t> bad = in;
    bad[3] = 7;
    check(!gen::latticePaint(bad, v, g, out2, nullptr, &why), "synthetic: a file that is not a TEX is refused");
    std::vector<std::uint8_t> flat = in;   // a background with no lattice at the vanilla grid
    for (std::size_t i = off; i < flat.size(); ++i) flat[i] = kGround;
    check(!gen::latticePaint(flat, v, g, out2, nullptr, &why) &&
              why.find("no lattice") != std::string::npos,
          "synthetic: a background with no lattice at the derived grid is refused");
    gen::LatticeGrid outside = v;
    outside.x = 200;
    check(!gen::latticePaint(in, outside, g, out2, nullptr, &why),
          "synthetic: a vanilla grid outside the texture is refused");
    // the names
    const std::string h1 = gen::latticeHash("XPack\\UI\\Caravan\\CaravanWindow01.tex", 565, 637, v, g);
    const std::string h2 = gen::latticeHash("xpack/ui/caravan/caravanwindow01.tex", 565, 637, v, g);
    gen::LatticeGrid g2 = g;
    g2.x += 32;
    const std::string h3 = gen::latticeHash("XPack\\UI\\Caravan\\CaravanWindow01.tex", 565, 637, v, g2);
    check(h1.size() == 8 && h1 == h2, "names: the hash is eight hex digits, the same for the same inputs");
    check(h1 != h3, "names: another grid has another hash");
    check(gen::latticeLeafName(32, 32, 565, 637, "", "1a2b3c4d") == "uniq_plate_32x32_565x637_L1a2b3c4d",
          "names: the start-up game's leaf");
    check(gen::latticeLeafName(32, 32, 660, 699, "svaera_customquest-8350d063", "1a2b3c4d") ==
              "uniq_plate_32x32_660x699_e5a963c1_L1a2b3c4d",
          "names: a custom game's leaf carries eight hex digits of FNV-1a 32 over its tag");
    // the engine's 63-character name parts: the name LoadTexture is handed is "<leaf>.tex"
    const std::string crash = "uniq_plate_32x32_565x637_itemization_quest-d1d5b2f1_L42ef5abf.tex";
    check(crash.size() == 65 && !ut::utResNameOk(crash.c_str()),
          "names: the 65-character name the engine fast-failed on is refused by the name check");
    const std::string iq = gen::latticeLeafName(32, 32, 565, 637, "itemization_quest-d1d5b2f1", "42ef5abf");
    check(iq == "uniq_plate_32x32_565x637_edebc817_L42ef5abf" && ut::utResNameOk((iq + ".tex").c_str()),
          "names: that game's plate is named by its tag's hash now, and the engine may be handed it");
    const char* const tags[4] = {"svaera_customquest-8350d063", "cataclysm_chaos_edition-917adb7a",
                                 "lootplusxmaxftwxmax-6a8c3b4f", nullptr};
    const std::string longTag(200, 'q');
    size_t longest = 0;
    bool allOk = true, apart = true;
    std::string seen[4];
    for (int i = 0; i < 4; ++i) {
        const std::string t = tags[i] ? tags[i] : longTag;
        // the widest real background (four-digit sizes) and three-digit cells
        seen[i] = gen::latticeLeafName(128, 128, 2048, 2048, t, "ffffffff") + ".tex";
        longest = seen[i].size() > longest ? seen[i].size() : longest;
        allOk = allOk && ut::utResNameOk(seen[i].c_str());
        for (int k = 0; k < i; ++k) apart = apart && seen[k] != seen[i];
    }
    char d[96];
    std::snprintf(d, sizeof d, "names: the longest real tags and a 200-character tag - %zu characters at most "
                               "with .tex, every one taken by the engine, no two alike", longest);
    check(allOk && apart && longest <= 51, d);
}

// ---- part 2: the installed game's caravan background ---------------------------------------------
bool hasTqExe(const std::string& dir) {
    const DWORD a = GetFileAttributesA((dir + "\\TQ.exe").c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::string gameDir() {
    const char* env = std::getenv("TQ_GAME_DIR");
    if (env && *env) return hasTqExe(env) ? std::string(env) : std::string();
    char steam[MAX_PATH] = {0};
    DWORD cb = sizeof(steam);
    if (RegGetValueA(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath", RRF_RT_REG_SZ, nullptr, steam,
                     &cb) != ERROR_SUCCESS)
        return std::string();
    std::vector<std::string> libs;
    libs.push_back(steam);
    FILE* f = std::fopen((std::string(steam) + "\\steamapps\\libraryfolders.vdf").c_str(), "rb");
    if (f) {
        std::string text;
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
        std::fclose(f);
        for (size_t at = text.find("\"path\""); at != std::string::npos; at = text.find("\"path\"", at + 6)) {
            const size_t q1 = text.find('"', at + 6);
            const size_t q2 = q1 == std::string::npos ? q1 : text.find('"', q1 + 1);
            if (q2 == std::string::npos) break;
            std::string v = text.substr(q1 + 1, q2 - q1 - 1), un;
            for (size_t i = 0; i < v.size(); ++i) {
                if (v[i] == '\\' && i + 1 < v.size() && v[i + 1] == '\\') ++i;
                un += v[i];
            }
            libs.push_back(un);
        }
    }
    for (const std::string& l : libs) {
        std::string cand = l + "\\steamapps\\common\\Titan Quest Anniversary Edition";
        for (char& c : cand)
            if (c == '/') c = '\\';
        if (hasTqExe(cand)) return cand;
    }
    return std::string();
}

int sum3(const gen::LatticeSurface& s, int x, int y) {
    const std::uint8_t* p = s.at(x, y);
    return (int(p[0]) + int(p[1]) + int(p[2])) / 3;
}

void partGame() {
    const std::string game = gameDir();
    if (game.empty()) {
        std::printf("game: no Titan Quest AE folder (TQ_GAME_DIR) - the installed background is not checked\n");
        return;
    }
    gen::ResourceArcs res;
    res.setRoot(game);
    std::vector<std::uint8_t> blob;
    std::string err;
    if (!res.read("XPack\\UI\\Caravan\\CaravanWindow01.tex", blob, 0, &err) || blob.empty()) {
        check(false, "game: XPack\\UI\\Caravan\\CaravanWindow01.tex reads from the archives");
        return;
    }
    gen::LatticeSurface van;
    std::string why;
    check(gen::latticeDecodeTex(blob, van, &why) && van.w == 565 && van.h == 637,
          "game: the caravan background decodes, 565 x 637");
    gen::LatticeGrid v;   // the derived grid: 27,126 16 x 15 of 32 (the window's units = its pixels)
    v.x = 27;
    v.y = 126;
    v.cols = 16;
    v.rows = 15;
    // the measured vanilla lattice: light lines at x 26 + 32 k, y 125 + 32 k
    bool lines = true;
    for (int k = 0; k <= 16; ++k) lines = lines && sum3(van, 26 + 32 * k, 300) > 95;
    for (int k = 0; k <= 15; ++k) lines = lines && sum3(van, 300, 125 + 32 * k) > 95;
    check(lines, "game: the vanilla lattice lies at x 26 + 32 k and y 125 + 32 k");
    check(sum3(van, 58 + 16, 157 + 16) < 70, "game: a vanilla cell's inside is dark");
    gen::LatticeGrid g = v;   // 16 x 12, one cell right and two cells down (it crosses the frame)
    g.x = 27 + 32;
    g.y = 126 + 64;
    g.rows = 12;
    std::vector<std::uint8_t> out;
    gen::LatticeReport rep;
    check(gen::latticePaint(blob, v, g, out, &rep, &why), "game: the paint runs");
    gen::LatticeSurface p;
    if (!gen::latticeDecodeTex(out, p, &why)) {
        check(false, "game: the painted file decodes");
        return;
    }
    check(out.size() == blob.size() && p.w == 565 && p.h == 637, "game: the painted file keeps the background's size");
    // the vanilla lattice is gone above the moved grid (y < 189): every old line at y 140
    bool gone = true;
    for (int k = 1; k <= 15; ++k) gone = gone && sum3(p, 26 + 32 * k, 140) < 70;
    for (int x = 40; x < 520; x += 37) gone = gone && sum3(p, x, 157) < 70;
    check(gone, "game: the vanilla lines are gone where the grid in force is not");
    // the grid in force: its lines at x 58 + 32 k and y 189 + 32 k carry the vanilla band
    bool now = true;
    for (int k = 0; k <= 15; ++k) now = now && sum3(p, 58 + 32 * k, 189 + 16) > 95;
    for (int k = 0; k <= 12; ++k) now = now && sum3(p, 58 + 16, 189 + 32 * k) > 95;
    check(now, "game: the grid in force has its lines at x 58 + 32 k and y 189 + 32 k");
    bool profile = true;
    for (int d = -2; d <= 2; ++d)
        profile = profile && std::abs(sum3(p, 122 + d, 205) - sum3(van, 90 + d, 141)) <= 12;
    check(profile, "game: a painted line has the vanilla band profile");
    check(sum3(p, 58 + 16 + 32, 189 + 16 + 32) < 70, "game: a painted cell's inside is dark");
    // the window's frame and title are the vanilla pixels
    bool frame = true;
    for (int y = 0; y < 120; y += 7)
        for (int x = 0; x < 565; x += 13) frame = frame && std::memcmp(p.at(x, y), van.at(x, y), 3) == 0;
    for (int y = 0; y < 637; y += 11) frame = frame && std::memcmp(p.at(22, y), van.at(22, y), 3) == 0;
    check(frame, "game: the window's frame, title and tabs are unchanged");
    check(rep.frames == 16 * 12, "game: 192 cells stamped");
    // the painted background beside the test, to look at
    char exe[MAX_PATH] = {0};
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string dump(exe);
    dump = dump.substr(0, dump.find_last_of('\\') + 1) + "test_lattice_painted.tex";
    if (FILE* f = std::fopen(dump.c_str(), "wb")) {
        std::fwrite(out.data(), 1, out.size(), f);
        std::fclose(f);
    }
    std::printf("game: %d band pixels erased, %d cells stamped\n", rep.erased, rep.frames);
}

// ---- part 3: the swap's decisions (src/ut_coverstep.h) over a ledger of the textures' counts -----
// Texture 1 is the engine's background (loaded by name: a second load is the same object with a
// count of two); the mod's copies are 2, 3, ... The field is the caravan window's +0x34; the
// window's destructor and the engine's reload unload whatever it holds. At the end every count
// is zero and no texture was unloaded at zero.
struct Swap {
    int cnt[32] = {};
    int dbl = 0, next = 2, field = 1, win = 1, wroteWin = 0, loads = 0;
    bool installed = false, off = false, keyFailed = false;
    int mine = 0, orig = 0, dropped = 0, key = 0;
    int keyOf[32] = {};     // the grid a loaded copy shows
    bool painted[32] = {};  // the painter's pass for a grid is done
    Swap() { cnt[1] = 1; }
    void unload(int t) {
        if (!t) return;
        if (cnt[t] <= 0) ++dbl;
        else --cnt[t];
    }
    int shows() const { return keyOf[field]; }
    void engineWrote() {   // ut_cover engineReplaced
        const ut::UtCoverBack b = ut::utCoverRestoreOf(true, false, false);
        installed = false;
        mine = 0;
        if (b.unloadOrig) unload(orig);
        orig = 0;
        off = off || b.off;
    }
    void restore(bool writeOk) {   // ut_cover restoreNow
        if (!installed) return;
        const bool read = wroteWin == win;
        if (read && field != mine) {
            engineWrote();
            return;
        }
        const bool wrote = read && writeOk;
        const ut::UtCoverBack b = ut::utCoverRestoreOf(read, read, wrote);
        if (wrote) field = orig;
        const int held = orig;
        installed = false;
        orig = 0;
        if (!b.keepMine) mine = 0;
        if (b.unloadOrig) unload(held);
        if (b.off) off = true;
    }
    // one frame of the Transfer page: `want` = a grid `k` off the derived one in force, view ON
    void frame(bool want, int k, bool writeOk = true) {
        if (off) return;
        if (dropped) {   // the previous frame's copy
            unload(dropped);
            dropped = 0;
        }
        if (want && k != key) {   // a new name: the painter is asked
            key = k;
            keyFailed = false;
        }
        ut::UtCoverIn in = {};
        in.installed = installed;
        in.haveMine = mine != 0;
        in.mineIsKey = mine != 0 && keyOf[mine] == key;
        in.keyed = key != 0;
        in.keyFailed = keyFailed;
        in.sameCaravan = wroteWin == win;
        in.fieldIsMine = field == mine;
        in.want = want;
        in.sizeOk = true;
        in.painted = painted[key];
        const unsigned a = ut::utCoverStep(in);
        if (key) painted[key] = true;   // the painter finishes after this frame
        if (a & ut::kUtCoverEngineWrote) {
            engineWrote();
            return;
        }
        if (a & ut::kUtCoverRestore) restore(writeOk);
        if (off) return;
        if (a & ut::kUtCoverDropMine) {
            if (dropped) unload(dropped);
            dropped = mine;
            mine = 0;
        }
        if (a & ut::kUtCoverLoad) {
            mine = next++;
            cnt[mine] = 1;
            keyOf[mine] = key;
            ++loads;
        }
        if (a & ut::kUtCoverSwap) {   // the new copy over the old one; the old one given back later
            const int t = next++;
            cnt[t] = 1;
            keyOf[t] = key;
            ++loads;
            if (dropped) unload(dropped);
            dropped = mine;
            field = t;
            mine = t;
        }
        if (a & ut::kUtCoverInstall) {
            orig = field;
            field = mine;
            installed = true;
            wroteWin = win;
        }
    }
    void engineReload() {   // slot +0x88 with the background's own name
        unload(field);
        ++cnt[1];
        field = 1;
    }
    void newWindow(bool sameAddress) {   // the caravan window destroyed and built again
        unload(field);
        ++cnt[1];
        field = 1;
        if (!sameAddress) ++win;
    }
    bool worldDown() {   // ut_cover coverRestore(worldGone), then the window's destructor
        restore(true);
        if (mine && !installed) unload(mine);
        if (dropped) unload(dropped);
        mine = dropped = 0;
        unload(field);
        field = 0;
        bool zero = dbl == 0;
        for (int i = 0; i < 32; ++i) zero = zero && cnt[i] == 0;
        return zero;
    }
};

void partSwap() {
    {   // the grid changes while the swap is in: the new copy goes on screen, the old one is given back
        Swap s;
        s.frame(true, 1);
        s.frame(true, 1);
        const bool a = s.installed && s.shows() == 1;
        s.frame(true, 2);
        const bool b = s.installed && s.shows() == 1 && s.dropped == 0;   // the old copy until painted
        s.frame(true, 2);
        const bool c = s.installed && s.shows() == 2 && s.loads == 2 && s.dropped != 0;
        s.frame(true, 2);
        const bool c2 = s.installed && s.shows() == 2 && s.dropped == 0;   // given back a frame later
        s.frame(true, 1);   // back to the first grid (the relic area and the item grid in turn)
        s.frame(true, 1);
        const bool c3 = s.installed && s.shows() == 1 && s.loads == 3;
        s.frame(false, 0);
        s.frame(true, 1);
        const bool d = s.shows() == 1 && s.loads == 3;
        check(a && b && c && c2 && c3 && d && s.worldDown(),
              "swap: a new grid while the swap is in - the old copy stays in the field until the new one "
              "is painted, then the new copy is written over it and the old one given back a frame later "
              "(never a frame of the window's own lattice); a toggle keeps the new one");
    }
    {   // the step: in place, waiting for the painter; a refused name takes the copy out
        ut::UtCoverIn in = {};
        in.installed = in.haveMine = in.sameCaravan = in.fieldIsMine = true;
        in.want = in.sizeOk = in.keyed = true;
        const unsigned wait = ut::utCoverStep(in);
        in.painted = true;
        const unsigned go = ut::utCoverStep(in);
        in.keyFailed = true;
        const unsigned refused = ut::utCoverStep(in);
        in.keyFailed = false;
        in.want = false;
        const unsigned off = ut::utCoverStep(in);
        check(wait == 0 && go == ut::kUtCoverSwap &&
                  refused == (ut::kUtCoverRestore | ut::kUtCoverDropMine) && off == ut::kUtCoverRestore,
              "swap: a new name while a copy is in waits in place, swaps once painted; a refused name or "
              "the view OFF gives the window its own background back");
    }
    {   // OFF -> ON with the same grid: the held copy again, no second load
        Swap s;
        s.frame(true, 1);
        s.frame(true, 1);
        s.frame(false, 0);
        const bool a = !s.installed && s.shows() == 0 && s.mine != 0;
        s.frame(true, 1);
        check(a && s.installed && s.shows() == 1 && s.loads == 1 && s.worldDown(),
              "swap: OFF -> ON with the same grid writes the held copy again");
    }
    {   // the grid changed while the view was OFF: never the old copy again
        Swap s;
        s.frame(true, 1);
        s.frame(true, 1);
        s.frame(false, 0);
        s.frame(true, 2);
        const bool a = s.shows() == 0;
        s.frame(true, 2);
        check(a && s.shows() == 2 && s.loads == 2 && s.worldDown(),
              "swap: a grid changed while OFF shows its own copy at the next ON, never the old one");
    }
    {   // the engine writes its own texture into the field (slot +0x88)
        Swap s;
        s.frame(true, 1);
        s.frame(true, 1);
        s.engineReload();
        s.frame(true, 1);
        s.frame(true, 1);
        check(s.off && !s.installed && s.shows() == 0 && s.worldDown(),
              "swap: the engine's own write - the held background given back once, the swap off");
    }
    {   // the caravan window gone, then one at another address and one at the same address
        Swap s;
        s.frame(true, 1);
        s.frame(true, 1);
        s.newWindow(false);
        s.frame(true, 1);
        const bool a = !s.off && s.mine == 0;
        Swap t;
        t.frame(true, 1);
        t.frame(true, 1);
        t.newWindow(true);
        t.frame(true, 1);
        check(a && s.worldDown() && t.off && t.worldDown(),
              "swap: the caravan window gone (another address: the held background given back) or replaced at "
              "the same address (the engine's write) - every count balances");
    }
    {   // the world goes down with the swap in, and with the copy held OFF
        Swap s;
        s.frame(true, 1);
        s.frame(true, 1);
        Swap t;
        t.frame(true, 1);
        t.frame(true, 1);
        t.frame(false, 0);
        check(s.worldDown() && t.worldDown(), "swap: the world goes down with the swap in or out - every count balances");
    }
    {   // a write-back faults: the copy stays with the window, its destructor unloads it once
        Swap s;
        s.frame(true, 1);
        s.frame(true, 1);
        s.frame(false, 0, false);
        check(s.off && s.mine == 0 && s.worldDown(),
              "swap: a failed write-back leaves the copy to the window (no second unload)");
    }
    {   // the derived grid back while the swap is in: restored, nothing painted or loaded again
        Swap s;
        s.frame(true, 1);
        s.frame(true, 1);
        s.frame(false, 1);
        const bool a = !s.installed && s.shows() == 0 && s.loads == 1;
        s.frame(true, 1);
        check(a && s.installed && s.loads == 1 && s.worldDown(),
              "swap: the derived grid back while the swap is in restores; the same grid again reuses the copy");
    }
    {   // the step itself: nothing before the painter's pass, a refused name never loads
        ut::UtCoverIn in = {};
        in.want = in.sizeOk = in.keyed = true;
        const unsigned wait = ut::utCoverStep(in);
        in.painted = true;
        const unsigned go = ut::utCoverStep(in);
        in.keyFailed = true;
        const unsigned refused = ut::utCoverStep(in);
        check(wait == 0 && go == (ut::kUtCoverLoad | ut::kUtCoverInstall) && refused == 0,
              "swap: the step waits for the painter and never loads a refused name");
    }
}

// ---- part 4: a custom game's 20 x 20 background (TQ_TEST_MODS_DIR, read only) -------------------
// A caravan window of 660 x 699 whose background N66_Mods\caravanxl\caravanwindow_20x20_new.tex
// sits at 0,11 with its 20 x 20 lattice at 11,46: lines at x 10 + 32 k and y 45 + 32 k, hand drawn
// - seven of its 21 horizontal lines lie one pixel lower, and its outermost bottom line (y 686)
// is outside the derived panel, so the erasure leaves it to the frame. Painted for 20 x 19 at the
// same origin (the collection with the pad's band) and for 16 x 15 two cells right and down;
// skipped when no installed custom game has it.
void partMods() {
    const char* mods = std::getenv("TQ_TEST_MODS_DIR");
    const std::string game = gameDir();
    if (!mods || !*mods || game.empty()) {
        std::printf("mods: TQ_TEST_MODS_DIR not set - the 20 x 20 caravan background is not checked\n");
        return;
    }
    const char* kTex = "N66_Mods\\caravanxl\\caravanwindow_20x20_new.tex";
    std::vector<std::uint8_t> blob;
    std::string found;
    WIN32_FIND_DATAA a;
    HANDLE ha = FindFirstFileA((std::string(mods) + "\\*").c_str(), &a);
    if (ha != INVALID_HANDLE_VALUE) {
        do {
            if (!(a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || a.cFileName[0] == '.') continue;
            const std::string item = std::string(mods) + "\\" + a.cFileName;
            WIN32_FIND_DATAA b;
            HANDLE hb = FindFirstFileA((item + "\\*").c_str(), &b);
            if (hb == INVALID_HANDLE_VALUE) continue;
            do {
                if (!(b.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || b.cFileName[0] == '.') continue;
                const std::string res = item + "\\" + b.cFileName + "\\Resources";
                gen::ResourceArcs r;
                r.setRoot(game, res);
                std::string err;
                if (found.empty() && r.read(kTex, blob, 0, &err) && !blob.empty()) found = res;
            } while (FindNextFileA(hb, &b));
            FindClose(hb);
        } while (found.empty() && FindNextFileA(ha, &a));
        FindClose(ha);
    }
    if (found.empty()) {
        std::printf("mods: no installed custom game holds %s - not checked\n", kTex);
        return;
    }
    std::printf("mods: %s from %s\n", kTex, found.c_str());
    gen::LatticeSurface van;
    std::string why;
    check(gen::latticeDecodeTex(blob, van, &why) && van.w == 660 && van.h == 699,
          "mods: the 20 x 20 caravan background decodes, 660 x 699");
    if (van.w != 660 || van.h != 699) return;
    bool lines = true;
    for (int k = 0; k <= 20; ++k) lines = lines && sum3(van, 10 + 32 * k, 300) > 95;
    int low = 0;
    for (int k = 0; k <= 20; ++k) {
        const bool at = sum3(van, 300, 45 + 32 * k) > 95, below = sum3(van, 300, 46 + 32 * k) > 95;
        lines = lines && (at || below);
        low += !at && below ? 1 : 0;
    }
    check(lines && low == 7, "mods: its lattice lies at x 10 + 32 k and y 45 + 32 k, seven lines a pixel lower");
    // the game's set: the derived 11,57 20 x 20 in the 660 x 699 window, the background (its
    // BackgroundImage record backgroundimage_20x20.dbr) at 0,11 - the cover's arithmetic
    const char geoText[] = "P\t11\t57\t640\t640\t0\t0\t\r\nW\t660\t699\t0\t81\t0\t11\r\n";
    ut::UtCaravanGeo geo;
    int ox = -1, oy = -1;
    const bool parsed = ut::utCaravanGeoParse(geoText, sizeof(geoText) - 1, &geo);
    const int placed = ut::utCaravanBgPlace(geo, van.w, van.h, &ox, &oy);
    check(parsed && placed == ut::kUtBgPlaced && ox == 0 && oy == 11,
          "mods: the place 0,11 leaves the derived 11,57 20 x 20 inside the 660 x 699 background");
    // the cover's own arithmetic (utCaravanBgGrids, as coverTick calls it): the grid in force at the
    // derived origin with the collection's cells
    int cc = 0, cr = 0;
    ut::utCaravanCollectionCells(geo, ut::kUtCaravanPadBand, &cc, &cr);
    ut::UtCaravanBgGrid bv, bf;
    ut::utCaravanBgGrids(geo, ox, oy, geo.x0, geo.y0, cc, cr, &bv, &bf);
    gen::LatticeGrid v;
    v.x = bv.x;
    v.y = bv.y;
    v.cols = bv.cols;
    v.rows = bv.rows;
    check(v.x == 11 && v.y == 46 && v.cols == 20 && v.rows == 20,
          "mods: the vanilla grid is at 11,46 in the background's pixels");
    gen::LatticeGrid g = v;   // the collection with the pad's band: 20 x 19, the grid in force
    g.x = bf.x;
    g.y = bf.y;
    g.cols = bf.cols;
    g.rows = bf.rows;
    check(cc == 20 && cr == 19 && g.x == 11 && g.y == 46, "mods: the grid in force is the collection's 20 x 19");
    {   // a place far off the background: refused, never INT_MIN arithmetic
        const char farText[] = "P\t11\t57\t640\t640\t0\t0\t\r\nW\t660\t699\t0\t81\t0\t99999\r\n";
        ut::UtCaravanGeo bad;
        int fx = -1, fy = -1;
        check(ut::utCaravanGeoParse(farText, sizeof(farText) - 1, &bad) &&
                  ut::utCaravanBgPlace(bad, van.w, van.h, &fx, &fy) == ut::kUtBgOutside && fx == 0 && fy == 0,
              "mods: an absurd place (0,99999) is refused before the painter");
    }
    std::vector<std::uint8_t> out;
    gen::LatticeReport rep;
    check(gen::latticePaint(blob, v, g, out, &rep, &why), "mods: the 20 x 19 paint runs");
    gen::LatticeSurface p;
    if (!gen::latticeDecodeTex(out, p, &why) || p.w != 660 || p.h != 699) {
        check(false, "mods: the 20 x 19 copy decodes at 660 x 699");
        return;
    }
    bool kept = true;
    for (int k = 0; k <= 19; ++k) kept = kept && sum3(p, 300, 45 + 32 * k) > 95;
    for (int k = 1; k <= 18; ++k) kept = kept && sum3(p, 300, 46 + 32 * k) < 70;   // one pixel thick
    for (int k = 0; k <= 20; ++k) kept = kept && sum3(p, 10 + 32 * k, 300) > 95;
    bool gone = true;   // the 20th row: its verticals below y 653 and its derived bottom line
    for (int x = 40; x < 640; x += 37) gone = gone && sum3(p, x, 685) < 70;
    for (int k = 1; k < 20; ++k) gone = gone && sum3(p, 10 + 32 * k, 669) < 70;
    bool profile = true;
    for (int d = -2; d <= 2; ++d) profile = profile && std::abs(sum3(p, 42 + d, 300) - sum3(van, 42 + d, 300)) <= 12;
    check(kept && gone && profile && rep.frames == 20 * 19,
          "mods: 20 x 19 has its 19 rows' lines one pixel thick with the band profile; the 20th row's are erased");
    gen::LatticeGrid m = v;   // 16 x 15 two cells right and down: at 75,121 in the window
    ut::utCaravanBgGrids(geo, ox, oy, geo.x0 + 64, geo.y0 + 64, 16, 15, &bv, &bf);
    m.x = bf.x;
    m.y = bf.y;
    m.cols = bf.cols;
    m.rows = bf.rows;
    check(m.x == 11 + 64 && m.y == 46 + 64 && bv.x == 11 && bv.y == 46,
          "mods: the moved grid at 75,121 in the window is at 75,110 in the background's pixels");
    std::vector<std::uint8_t> out2;
    check(gen::latticePaint(blob, v, m, out2, &rep, &why) && gen::latticeDecodeTex(out2, p, &why),
          "mods: the moved 16 x 15 paint runs");
    bool moved = true;
    for (int k = 0; k <= 16; ++k) moved = moved && sum3(p, 74 + 32 * k, 109 + 16) > 95;
    for (int k = 0; k <= 15; ++k) moved = moved && sum3(p, 74 + 16, 109 + 32 * k) > 95;
    for (int k = 1; k <= 14; ++k) moved = moved && sum3(p, 74 + 16, 110 + 32 * k) < 70;
    moved = moved && sum3(p, 10 + 32, 60) < 70 && sum3(p, 300, 45 + 32) < 70;
    check(moved && rep.frames == 16 * 15,
          "mods: a moved 16 x 15 has its lines at x 74 + 32 k, y 109 + 32 k and none of the old ones above");
    std::printf("mods: %d band pixels erased, %d cells stamped (moved grid)\n", rep.erased, rep.frames);
}

}  // namespace

int main() {
    partSynthetic();
    partGame();
    partSwap();
    partMods();
    std::printf("lattice: %d passed, %d failed\n", g_pass, g_fail);
    if (g_fail) return 1;
    std::printf("LATTICE TESTS PASSED\n");
    return 0;
}
