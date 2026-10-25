// test_text.cpp - the tab's words, offline (src\ut_textcfg.h): the UTF-8 helper, the label
// templates with TQ's {found}, the built-in words and TQ's four templates, the chain over a file's
// [text]; and the pad's template expander (src\ut_padlayout.h utPadLabelTemplated) against TQ
// 1.0.1's own label (utPadLabelLine); and the game's fonts (src\ut_sfnt.h, src\ut_textmeasure.h)
// over the real Resources\Fonts.arc (READ ONLY). tools\build_test_text.bat builds and runs it.
#include <stdio.h>
#include <string.h>

#include <random>
#include <string>
#include <vector>

#include "ut_padlayout.h"
#include "ut_textcfg.h"
#include "test_fonts.h"

static int g_fail = 0, g_pass = 0;
static void check(bool ok, const std::string& what) {
    if (ok) ++g_pass;
    else ++g_fail;
    printf("  %-90s %s\n", what.c_str(), ok ? "ok" : "FAIL");
}

static void testUtf8() {
    printf("\n1. the UTF-8 helper\n");
    check(utt::utf8Valid("Helm") && utt::utf8Valid("\xD0\xA8") && !utt::utf8Valid("\xC3\x28"), "valid / invalid");
    check(utt::utf8Length("a\xD0\xA8z") == 3 && utt::utf16Units("\xF0\x9F\x98\x80") == 2, "code points, UTF-16 units");
    check(utt::utf8Prefix("a\xD0\xA8z", 2) == "a\xD0\xA8", "a prefix cut at a code point");
}

static void testTemplates() {
    printf("\n2. the label templates: TQ's {found}\n");
    std::string why;
    check(utt::utLabelSlotOf("found") == utt::kSlotFound && utt::kSlotCount == 10, "{found} is the tenth placeholder");
    check(utt::utTextTemplateCheck("{group} {found}", &why), "a template with {found} is valid");
    check(!utt::utTextTemplateCheck("{group} {nope}", &why) && why.find("unknown placeholder {nope}") != std::string::npos,
          "an unknown placeholder is refused");
    check(!utt::utTextTemplateCheck("{group", &why) && !utt::utTextTemplateCheck("a}b", &why), "unbalanced braces refused");
    const utt::UtLabelValues v =
        utt::utLabelValues(true, "mode unknown - ", "Torso", 12, 458, 3, 7, 58, 312, 2678, "?", "found 9");
    std::string out;
    check(utt::utTextExpand(utt::utTextDefaultFormat(0), v, out) &&
              out == "Torso  rows 3-7 / 58  owned 12 / 458  all 312 / 2678  found 9",
          "TQ's full form, byte for byte (" + out + ")");
    check(utt::utTextExpand(utt::utTextDefaultFormat(1), v, out) && out == "Torso 3-7/58  12/458  all 312/2678  found 9",
          "TQ's compact form");
    check(utt::utTextExpand(utt::utTextDefaultFormat(2), v, out) && out == "Torso  12/458  all 312/2678  found 9",
          "without the rows");
    check(utt::utTextExpand(utt::utTextDefaultFormat(3), v, out) && out == "Torso  12/458  found 9",
          "without the whole collection");
    check(utt::kTextDefaultFormats == 4 && !utt::utTextDefaultFormat(4), "four built-in templates");
    const utt::UtLabelValues u = utt::utLabelValues(false, "mode unknown - ", "Arms", -1, 9, 1, 1, 1, -1, 9, "?");
    check(utt::utTextExpand("{mode}{group} {owned}/{entries} {all}{{x}}", u, out) &&
              out == "mode unknown - Arms ?/9 ?{x}",
          "unknown counts, {mode}, literal braces; {found} empty by default");
}

static void testBuiltins() {
    printf("\n3. the built-in words (TQ's)\n");
    std::size_t n = 0;
    const utt::UtTextBuiltin* t = utt::utTextBuiltins(&n);
    check(n == 28 && !strcmp(t[0].id, "vanilla") && !strcmp(t[0].caption, "Transfer") && !strcmp(t[26].id, "owned"),
          "the table: the Transfer toggle, 25 groups, OWN, Other");
    const char* const shortNames[][2] = {{"head", "Helm"},   {"chest", "Torso"},  {"hands", "Arms"},
                                         {"legs", "Legs"},   {"amulet", "Amul"},  {"ring", "Ring"},
                                         {"shield", "Shld"}, {"axe1h", "Axe"},    {"mace1h", "Mace"},
                                         {"scepter", "Staff"}, {"sword1h", "Sword"}, {"ranged1h", "Throw"},
                                         {"spear2h", "Spear"}, {"ranged2h", "Bow"}, {"artifact", "Artf"}};
    bool same = true;
    for (const auto& s : shortNames) {
        const utt::UtTextBuiltin* b = utt::utTextBuiltinById(s[0]);
        same = same && b && !strcmp(b->caption, s[1]) && !strcmp(b->caption, ut::utPadShortName(b->name));
    }
    check(same, "the 15 groups' captions are TQ 1.0.1's short names (utPadShortName of their labels)");
    check(!strcmp(utt::utTextBuiltinPlain(utt::kTextTooltipYes), "In your collection") &&
              !strcmp(utt::utTextBuiltinPlain(utt::kTextTooltipNo), "Not in your collection") &&
              !strcmp(utt::utTextBuiltinPlain(utt::kTextTooltipRoll), " (another roll)"),
          "the rollover words: the tooltip's compiled ones");
    check(!strcmp(utt::utStyleDefaultText("font.caption"), "fonts/albertus mt light.fnt"), "the caption font documented");
    // the [tab] sizes and colours the pad reads (unset = the design's)
    utt::UtTextStyle st;
    float rgb[3] = {0.0f, 0.0f, 0.0f};
    check(utt::utStyleCaptionFixed(st) == 0 && utt::utStyleSizeOr(st, utt::kStyleSizeCaptionMax, 14) == 14 &&
              utt::utStyleSizeOr(st, utt::kStyleSizeLabel, 13) == 13 && !utt::utStyleColorRgb(st, utt::kStyleColorCaption, rgb),
          "no [tab] size or colour: the pad's own (fitted captions under 14, plate_label_size, the palette)");
    st.size[utt::kStyleSizeCaption] = 0;
    st.size[utt::kStyleSizeCaptionMax] = 9;
    st.size[utt::kStyleSizeLabel] = 10;
    check(utt::utStyleCaptionFixed(st) == 0 && utt::utStyleSizeOr(st, utt::kStyleSizeCaptionMax, 14) == 9 &&
              utt::utStyleSizeOr(st, utt::kStyleSizeLabel, 13) == 10,
          "size.caption = 0 is auto; size.caption_max and size.label win over the pad's 14 and plate_label_size");
    st.size[utt::kStyleSizeCaption] = 12;
    check(utt::utStyleCaptionFixed(st) == 12, "size.caption = 12: one fixed caption size");
    st.color[utt::kStyleColorRing] = 0xFF8000;
    check(utt::utStyleColorRgb(st, utt::kStyleColorRing, rgb) && rgb[0] == 1.0f && rgb[1] == 128.0f / 255.0f &&
              rgb[2] == 0.0f && !utt::utStyleColorRgb(st, utt::kStyleColorMark, rgb) &&
              !utt::utStyleColorRgb(st, utt::kStyleColorCount, rgb) && !utt::utStyleColorRgb(st, -1, rgb),
          "color.ring = FF8000 as channels; an unset or unknown colour stays the design's");
}

static void testChain() {
    printf("\n4. the chain over a [text] file (the buttons file's [tab] maps onto it)\n");
    utt::UtTextFile f;
    std::string err;
    check(utt::utTextParse("[text]\ncaption.vanilla = Tab\nlabel.format.1 = {group} {found}\n"
                           "tooltip.yes = \"Got it \"\n", f, &err),
          "parsed (" + err + ")");
    utt::UtTextContext c;
    c.file = &f;
    check(utt::utTextResolve(c, utt::kTextCaption, "vanilla").text == "Tab", "caption.vanilla from the file");
    check(utt::utTextResolve(c, utt::kTextTooltipYes, "").text == "Got it ", "a quoted value keeps its blank");
    std::vector<std::string> fm;
    check(utt::utTextResolveFormats(c, fm, nullptr) == utt::kSrcCommon && fm.size() == 1 && fm[0] == "{group} {found}",
          "a file's template list is taken whole");
    utt::UtTextContext none;
    check(utt::utTextResolveFormats(none, fm, nullptr) == utt::kSrcBuiltin && fm.size() == 4, "no file: TQ's four");
    check(utt::utTextResolve(none, utt::kTextCaption, "vanilla").text == "Transfer", "no file: \"Transfer\"");
    check(!utt::utTextParse("[text]\nlabel.format.1 = {found\n", f, &err) && err.find("line 2") == 0,
          "a bad template refuses the file, naming the line");
}

static ut::UtPadLabelFormats builtinFormats() {
    ut::UtPadLabelFormats fm;
    fm.n = 0;
    for (int i = 0; i < utt::kTextDefaultFormats; ++i)
        _snprintf_s(fm.f[fm.n++], sizeof(fm.f[0]), _TRUNCATE, "%s", utt::utTextDefaultFormat(i));
    _snprintf_s(fm.unknown, sizeof(fm.unknown), _TRUNCATE, "?");
    return fm;
}

static void testPadLabel() {
    printf("\n5. the pad's template expander = TQ 1.0.1's label (a group named by its short name)\n");
    const ut::UtPadLabelFormats fm = builtinFormats();
    char a[640], b[640];
    int compared = 0, differ = 0;
    std::string first;
    for (int known = 0; known < 2; ++known)
        for (int allKnown = 0; allKnown < 2; ++allKnown)
            for (int query = 0; query < 3; ++query)
                for (int notShown = 0; notShown < 2; ++notShown)
                    for (int wpx = 20; wpx <= 900; wpx += 7) {
                        ut::UtPadLabelWords w;
                        w.group = "Torso";
                        w.span = "3-7";
                        w.first = 3;
                        w.last = 7;
                        w.rows = 58;
                        w.known = known != 0;
                        w.owned = 12;
                        w.total = 458;
                        w.allKnown = allKnown != 0;
                        w.allOwned = 312;
                        w.allTotal = 2678;
                        w.found = query == 0 ? "" : query == 1 ? "found 9" : "indexing 3/2678";
                        w.brief = query == 0 ? "" : query == 1 ? "=9" : "3/2678";
                        w.notShown = notShown ? 6 : 0;
                        const int fa = ut::utPadLabelLine(w, (float)wpx, 13, a, sizeof(a));
                        const int fb = ut::utPadLabelTemplated(fm, w, (float)wpx, 13, b, sizeof(b));
                        // 1.0.1's last form with a query - the name and the short search words,
                        // "Torso  =9" - has no template among the four (the last is
                        // "{group}  {owned}/{entries}  {found}"): left out of the comparison
                        if (query && (fa < 13 || std::string(a) == std::string("Torso  ") + w.brief)) continue;
                        ++compared;
                        if (fa != fb || strcmp(a, b)) {
                            if (!differ) first = std::string(a) + " | " + b + " @" + std::to_string(wpx);
                            ++differ;
                        }
                    }
    check(compared > 1000 && differ == 0,
          "identical line and size at every width, every count and search state (" + std::to_string(compared) +
              " compared" + (differ ? ", first difference: " + first : std::string()) + ")");
    ut::UtPadLabelFormats one;
    one.n = 1;
    _snprintf_s(one.f[0], sizeof(one.f[0]), _TRUNCATE, "%s", "{group}: {owned} of {entries} {{{found}}}");
    _snprintf_s(one.unknown, sizeof(one.unknown), _TRUNCATE, "n/a");
    ut::UtPadLabelWords w;
    w.group = "Helms";
    w.total = 40;
    w.found = "found 2";
    w.brief = "=2";
    ut::utPadLabelTemplated(one, w, 1000.0f, 13, b, sizeof(b));
    check(!strcmp(b, "Helms: n/a of 40 {found 2}"), std::string("a user's template, label.unknown, braces (") + b + ")");
    const int fs = ut::utPadLabelTemplated(one, w, 30.0f, 13, b, sizeof(b));
    check(fs == 6 && !strcmp(b, "Helms: n/a of 40 {=2}"), "too narrow: the short search words, shrunk to 6");
    char tiny[8];
    ut::utPadLabelTemplated(one, w, 1000.0f, 13, tiny, sizeof(tiny));
    check(strlen(tiny) <= 7 && !strncmp(tiny, "Helms:", 6), std::string("the buffer bounds the line (") + tiny + ")");
}

// A toggle caption of the file's own (mat.caption) is cut to what fits right of the lamp.
static void testFitCaption() {
    printf("\n6. the Transfer toggle: a caption of the file's own is cut to fit\n");
    char buf[64];
    const float w = 60.0f, h = 14.0f, s = 1.0f;
    const int fs = 10;
    const float room = w - ut::utPadLampTextOffset(h, s) - 2.0f;
    const char* whole = ut::utPadFitCaption("Tab", w, h, s, fs, buf, sizeof(buf));
    check(!strcmp(whole, "Tab") && whole != buf, "a caption that fits is drawn whole");
    const char* cut = ut::utPadFitCaption("Transfer to stash", w, h, s, fs, buf, sizeof(buf));
    check(cut == buf && strlen(cut) >= 1 && strlen(cut) < 17 && !strncmp(cut, "Transfer to stash", strlen(cut)) &&
              !(ut::utPadTextWidth(cut, fs) > room),
          std::string("a long one is cut to what fits (") + cut + ")");
    const char* sh = ut::utPadFitCaption("\xD0\xA8\xD0\xA8\xD0\xA8\xD0\xA8\xD0\xA8\xD0\xA8\xD0\xA8\xD0\xA8\xD0\xA8", w, h,
                                     s, fs, buf, sizeof(buf));
    check(utt::utf8Valid(sh) && strlen(sh) % 2 == 0 && strlen(sh) >= 2, "never cut inside a UTF-8 sequence");
    const char* one = ut::utPadFitCaption("Transfer", 1.0f, h, s, fs, buf, sizeof(buf));
    check(!strcmp(one, "T"), "no room at all: one character");
}

// The game's fonts. The expected numbers were taken once with fontTools 4.64 over the same
// Fonts.arc (TTFont(...).getBestCmap(), name id 1, head.unitsPerEm, hmtx advances) and are fixed
// here: they are the archive's, not this code's.
static int countIn(const ut::UtTmFont& f, char32_t lo, char32_t hi) {
    int n = 0;
    for (const ut::UtTmAdvance& a : f.adv) n += a.cp >= lo && a.cp <= hi;
    return n;
}
static void testFonts() {
    printf("\n8. the game's fonts: Resources\\Fonts.arc through ut_sfnt.h and ut_textmeasure.h\n");
    static ut::UtTmFontSet set;
    const bool all = tfReadSet(&set);
    check(all, "the nine fonts read from " + (tfFontsArc().empty() ? std::string("(no TQ_GAME_DIR)") : tfFontsArc()));
    if (!all) return;
    struct Want {
        int id;
        const char* family;
        int upem;
        size_t cps;
        int greek, cyrillic, cjk;
        bool cff;
    };
    const Want want[] = {
        {ut::kUtTmAlbertusLight, "Albertus MT Light", 2048, 250, 0, 0, 0, false},
        {ut::kUtTmAlbertus, "Albertus MT", 2048, 255, 1, 0, 0, false},
        {ut::kUtTmAlbertusCz, "Albertus MT Light", 2048, 270, 0, 0, 0, false},
        {ut::kUtTmArial, "Arial", 2048, 1186, 73, 118, 0, false},
        {ut::kUtTmArialRussian, "Arial", 2048, 1419, 73, 118, 0, false},
        {ut::kUtTmDejaVuSerif, "DejaVu Serif", 2048, 3447, 121, 204, 0, false},
        {ut::kUtTmLinLibertine, "Arial", 2048, 1186, 73, 118, 0, false},
        {ut::kUtTmAsia, "Source Han Serif K", 1000, 43031, 49, 66, 20971, true},
        {ut::kUtTmTimesEe, "Times New Roman", 2048, 1142, 73, 94, 0, false},
    };
    for (const Want& w : want) {
        const ut::UtTmFont& f = set.font[w.id];
        char d[200];
        _snprintf_s(d, sizeof(d), _TRUNCATE, "%s: %s, %d per em, %u code points, Greek %d Cyrillic %d CJK %d%s",
                    ut::utTmFontPath(w.id), f.family.c_str(), f.upem, (unsigned)f.adv.size(),
                    countIn(f, 0x370, 0x3FF), countIn(f, 0x400, 0x4FF), countIn(f, 0x4E00, 0x9FFF),
                    f.cff ? ", OTTO" : "");
        check(f.valid && f.family == w.family && f.upem == w.upem && f.adv.size() == w.cps &&
                  countIn(f, 0x370, 0x3FF) == w.greek && countIn(f, 0x400, 0x4FF) == w.cyrillic &&
                  countIn(f, 0x4E00, 0x9FFF) == w.cjk && f.cff == w.cff,
              d);
    }
    const ut::UtTmFont& al = set.font[ut::kUtTmAlbertusLight];
    const ut::UtTmFont& ar = set.font[ut::kUtTmArial];
    const ut::UtTmFont& dv = set.font[ut::kUtTmDejaVuSerif];
    const ut::UtTmFont& as = set.font[ut::kUtTmAsia];
    std::uint16_t a1 = 0, a2 = 0, a3 = 0, a4 = 0, a5 = 0, a6 = 0, a7 = 0, a8 = 0;
    check(ut::utTmAdvOf(al, 'H', &a1) && a1 == 1473 && ut::utTmAdvOf(al, 'm', &a2) && a2 == 1599 && al.qAdv == 811 &&
              ut::utTmAdvOf(ar, 0x428, &a3) && a3 == 1877 && ut::utTmAdvOf(ar, 0x3B1, &a4) && a4 == 1184 &&
              ut::utTmAdvOf(dv, 0x428, &a5) && a5 == 2337 && ut::utTmAdvOf(as, 0x65E5, &a6) && a6 == 1000 &&
              ut::utTmAdvOf(as, 0xD55C, &a7) && a7 == 966 && ut::utTmAdvOf(as, 'A', &a8) && a8 == 719,
          "advances (hmtx): albertus H 1473 m 1599 ? 811; arial U+0428 1877 U+03B1 1184; dejavuserif U+0428 2337; "
          "override_asia U+65E5 1000 U+D55C 966 A 719");
    // the measure: the engine's - each glyph's hinted advance in whole px at 14 pt / 300 dpi
    // (58 px per em for a font with head.flags bit 3, else 3733/64) over 56, the run scaled by
    // floor(size x 0.875 + 0.5) and truncated
    check(al.xScale == 118784 && ut::utTmBaseGlyphPx(al, 'H') == 42 && ut::utTmBaseGlyphPx(al, 'm') == 45 &&
              as.xScale == 244646 && ut::utTmRunScale(12) == 11.0f && ut::utTmRunScale(6) == 5.0f &&
              ut::utTmRunScale(9) == 8.0f,
          "the engine's base: albertus x_scale 118784 (58 px per em over 2048 units, 16.16), H 42 px, m 45 px; "
          "override_asia (no bit 3) at 3733/64 per em; a run at 12 is scaled by 11, at 6 by 5, at 9 by 8");
    check(ut::utTmFontWidth(al, "Helm", 10) == 20.0f && ut::utTmFontWidth(al, "Helm", 100) == 196.0f &&
              ut::utTmFontWidth(al, "\xD0\x96", 10) == 3.0f && ut::utTmFontWidth(ar, "\xD0\xA8", 14) == 11.0f &&
              ut::utTmFontWidth(as, "\xE6\x97\xA5\xE3\x81\x82", 20) == 37.0f &&
              ut::utTmFontWidth(as, "\xED\x95\x9C", 20) == 18.0f,
          "the measure: Helm at 10 = 20, at 100 = 196; a letter albertus lacks is '?' (3); arial U+0428 at 14 = 11; "
          "U+65E5 U+3042 at 20 = 37; U+D55C at 20 = 18");
    {   // RenderText's own floats, read from the game's log (albertus mt light): the measure is
        // the engine's width; on two captions the engine kerned a pair its glyph cache had
        // computed and drew 1 px narrower - the measure leaves kerning out (an upper bound)
        struct Seen {
            const char* s;
            int size, renderText, kerned;
        };
        static const Seen kSeen[] = {
            {"<", 12, 7, 0},          {">", 12, 7, 0},           {">>", 9, 11, 0},
            {"Amul", 12, 26, 0},      {"Arcane For", 6, 24, 0},  {"Arcane Form", 6, 28, 0},
            {"Arms", 12, 26, 0},      {"Artf", 12, 21, 0},       {"Axe", 12, 18, 1},
            {"BLU", 12, 20, 0},       {"Bow", 12, 19, 0},        {"Helm", 12, 24, 0},
            {"Legendary R", 6, 26, 0}, {"Legendary Ri", 6, 27, 0}, {"Legs", 12, 20, 0},
            {"Mace", 12, 24, 0},      {"OWN", 12, 29, 0},        {"RNL", 12, 21, 0},
            {"Ring", 12, 21, 0},      {"Shld", 12, 19, 0},       {"Spear", 12, 24, 0},
            {"Staff", 12, 21, 0},     {"Sword", 12, 28, 1},      {"Throw", 12, 30, 0},
            {"Torso", 12, 28, 0},     {"Transfer", 12, 39, 0},
        };
        int exact = 0, bad = 0;
        std::string why;
        for (const Seen& r : kSeen) {
            const float m = ut::utTmFontWidth(al, r.s, r.size);
            if (m == (float)(r.renderText + r.kerned)) {
                exact += r.kerned ? 0 : 1;
            } else {
                ++bad;
                why += std::string(" ") + r.s + "=" + std::to_string((int)m);
            }
        }
        check(bad == 0 && exact == 24,
              "the measure equals RenderText's float on 24 of the 26 captions the game drew (Helm 24, Transfer 39, "
              "OWN 29, < 7, Legendary R at 6 26, Arcane For at 6 24 ...); Axe and Sword 1 px over (kerned)" + why);
    }
    {   // any alphabet: Cyrillic, CJK and Hangul captions, names and label words - GD's limits
        // (a caption 1..12 UTF-16 units, a name up to 48), the font that has the letters, the wide call
        std::string why;
        const char* cyrCap = "\xD0\x9A\xD0\xBE\xD0\xBB\xD1\x8C\xD1\x86\xD0\xB0";   // 6 letters
        const char* korCap = "\xEB\xB0\x98\xEC\xA7\x80";                           // 2 syllables
        const char* cjkCap = "\xE6\x8C\x87\xE8\xBC\xAA";                           // 2 ideographs
        std::string long13;
        for (int k = 0; k < 13; ++k) long13 += "\xD0\xA8";
        const bool limits = utt::utTextValueOk(utt::kTextCaption, cyrCap, nullptr) &&
                            utt::utTextValueOk(utt::kTextCaption, korCap, nullptr) &&
                            utt::utTextValueOk(utt::kTextCaption, cjkCap, nullptr) &&
                            utt::utTextValueOk(utt::kTextCaption, long13.substr(0, 24), nullptr) &&
                            !utt::utTextValueOk(utt::kTextCaption, long13, &why) &&
                            why.find("longer than 12 UTF-16 units") != std::string::npos &&
                            utt::utTextValueOk(utt::kTextName, std::string(korCap) + " " + cjkCap, nullptr);
        const ut::UtTmPick pc = ut::utTmPickFont(set, ut::kUtTmAlbertusLight, cyrCap);
        const ut::UtTmPick pk = ut::utTmPickFont(set, ut::kUtTmAlbertusLight, korCap);
        const ut::UtTmPick pj = ut::utTmPickFont(set, ut::kUtTmAlbertusLight, cjkCap);
        unsigned short wk[8];
        const std::size_t nk = ut::utWidenUtf8(korCap, wk, 8);
        const wchar_t wkw[3] = {(wchar_t)0xBC18, (wchar_t)0xC9C0, 0};
        const bool wide = !ut::utTmIsPlainAscii(cyrCap) && !ut::utTmIsPlainAscii(korCap) &&
                          !ut::utTmIsPlainAscii(cjkCap) && ut::utTmIsPlainAscii("Helm") && nk == 2 &&
                          wk[0] == 0xBC18 && wk[1] == 0xC9C0 &&
                          ut::utTmFontWidthW(as, wkw, 2, 12) == ut::utTmFontWidth(as, korCap, 12);
        check(limits && pc.font == ut::kUtTmArial && pc.coversAll && pk.font == ut::kUtTmAsia && pk.coversAll &&
                  pj.font == ut::kUtTmAsia && pj.coversAll && wide,
              "any alphabet: Cyrillic, Hangul and Han captions pass GD's 12-unit limit (13 Cyrillic letters are "
              "refused: " + why + "), a Hangul + Han name passes; Cyrillic draws in arial, Hangul and Han in "
              "override_asia, all through the wide call (UTF-16), measured the same both ways");
    }
    // the font that follows the letters, from the caption font albertus
    const int R = ut::kUtTmAlbertusLight;
    const ut::UtTmPick latin = ut::utTmPickFont(set, R, "Helm");
    const ut::UtTmPick cyr = ut::utTmPickFont(set, R, "\xD0\xA8\xD0\xBB\xD0\xB5\xD0\xBC");
    const ut::UtTmPick grk = ut::utTmPickFont(set, R, "\xCE\xB1\xCE\xB2\xCE\xB3");
    const ut::UtTmPick cjk = ut::utTmPickFont(set, R, "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E");
    const ut::UtTmPick kor = ut::utTmPickFont(set, R, "\xED\x95\x9C\xEA\xB5\xAD");
    const ut::UtTmPick thai = ut::utTmPickFont(set, R, "\xE0\xB8\x81");
    const ut::UtTmPick none = ut::utTmPickFont(set, -1, "Helm");
    const std::string fb = ut::utTmFallbackText(thai, "the captions");
    check(latin.font == R && latin.coversAll && cyr.font == ut::kUtTmArial && cyr.coversAll &&
              grk.font == ut::kUtTmArial && cjk.font == ut::kUtTmAsia && cjk.coversAll &&
              kor.font == ut::kUtTmAsia && kor.coversAll && !thai.coversAll && thai.font == R &&
              thai.missing == 0xE01 && none.font == -1 &&
              fb == "fonts/albertus mt light.fnt is used for the captions; U+0E01 is drawn by the engine's own "
                    "fallback font",
          "the letters pick the font: Latin albertus, Cyrillic and Greek arial, Han and Hangul override_asia; a "
          "letter no font has: the role's font, said in GD's words (" + fb + ")");
    // refusals: a truncated file, a table that points outside the buffer; OTTO accepted
    std::vector<std::uint8_t> b = tfFontBytes("fonts/albertus mt light.fnt");
    ut::UtTmFont t;
    std::vector<char32_t> cps;
    const bool whole = !b.empty() && ut::utTmFontRead(b.data(), b.size(), &t) && ut::utSfntCodePoints(b.data(), b.size(), &cps) &&
                       cps.size() == t.adv.size();
    const bool cut = !ut::utTmFontRead(b.data(), 100, &t) && !t.valid && !ut::utSfntCodePoints(b.data(), 100, &cps);
    bool outside = false;
    if (b.size() > 12) {
        const std::size_t nt = ut::utSfntBe16(b.data() + 4);
        for (std::size_t i = 0; i < nt; ++i) {
            unsigned char* r = b.data() + 12 + i * 16;
            if (ut::utSfntBe32(r) != 0x636D6170u) continue;   // 'cmap'
            r[8] = 0x7F;                                        // its offset: past the end
            outside = !ut::utTmFontRead(b.data(), b.size(), &t) && !ut::utSfntCodePoints(b.data(), b.size(), &cps);
        }
    }
    std::vector<std::uint8_t> ot = tfFontBytes("fonts/override_asia.fnt");
    const bool otto = ot.size() == 23590300u && ot[0] == 'O' && ot[1] == 'T' && ot[2] == 'T' && ot[3] == 'O' &&
                      ut::utSfntCodePoints(ot.data(), ot.size(), &cps) && cps.size() == 43031u;
    check(whole && cut && outside && otto,
          "the reader: GD's utSfntCodePoints agrees; a truncated buffer and a cmap pointing outside are refused; "
          "override_asia.fnt (23,590,300 bytes, OTTO) is read");
    // the wide call's text, and the estimate without a table
    unsigned short wbuf[8];
    const std::size_t nw = ut::utWidenUtf8("\xD0\xA8\xE6\x97\xA5", wbuf, 8);
    ut::utTmInstall(nullptr);
    const float est = ut::utPadTextWidth("Helm", 10);
    ut::utTmInstall(&set);
    const float real = ut::utPadTextWidth("Helm", 10);
    const float cyrW = ut::utPadTextWidth("\xD0\xA8\xD0\xBB\xD0\xB5\xD0\xBC", 10);
    ut::utTmInstall(nullptr);
    check(nw == 2 && wbuf[0] == 0x428 && wbuf[1] == 0x65E5 && est == 22.0f && real == 20.0f &&
              cyrW == ut::utTmFontWidth(ar, "\xD0\xA8\xD0\xBB\xD0\xB5\xD0\xBC", 10),
          "UTF-8 widened for the wide call; the pad measures with the set installed (20), the estimate without (22), "
          "a Cyrillic caption in arial");
}

// GD's fuzz: random files from troublesome pieces; the parser never fails without naming a line,
// and a refused file leaves nothing behind.
static void testFuzz() {
    printf("\n7. fuzz: 3000 random [text] files\n");
    static const char* const kPieces[] = {
        "[text]", "[style]", "[text.ru]", "[bogus]", "[", "]", "\n", "\r\n", "\n", " ", "=", ";", "#", "\"",
        "caption.head", "name.head", "label.format.1", "label.format.4", "tooltip.yes", "size.label",
        "color.mark", "font.label", "{group}", "{found}", "{", "}", "{{", "13", "3FD46A", "fonts/a.fnt", "\xD0\xA8",
        "\xF0\x9F\x98\x80", "\xD0", "\x80", "\xEF\xBB\xBF", "\t", "HEL",
    };
    const std::size_t nPieces = sizeof kPieces / sizeof *kPieces;
    std::mt19937 rng(777);
    int accepted = 0, refused = 0;
    bool named = true, clean = true;
    for (int t = 0; t < 3000; ++t) {
        std::string text;
        const int len = int(rng() % 40);
        for (int i = 0; i < len; ++i) text += kPieces[rng() % nPieces];
        utt::UtTextFile f;
        std::string err;
        if (utt::utTextParse(text, f, &err)) {
            ++accepted;
        } else {
            ++refused;
            if (err.compare(0, 5, "line ") != 0) named = false;
            if (f.keys || !f.common.empty() || !f.lang.empty()) clean = false;
        }
    }
    check(named && clean && accepted > 0 && refused > 0,
          "fuzz: " + std::to_string(accepted) + " accepted, " + std::to_string(refused) +
              " refused, every refusal names its line");
}

int main() {
    printf("test_text - the tab's words (src\\ut_textcfg.h, src\\ut_padlayout.h)\n");
    testUtf8();
    testTemplates();
    testBuiltins();
    testChain();
    testPadLabel();
    testFitCaption();
    testFuzz();
    testFonts();
    printf("\n%s - %d passed, %d failed\n", g_fail ? "FAILED" : "ALL PASS", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
