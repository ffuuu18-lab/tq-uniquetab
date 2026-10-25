// test_buttons.cpp - the buttons file, offline (src\ut_buttons.h): the grammar and every refusal,
// TQ's two [tab] text-style keys, buttons -> groups against TQ's group table, [unassigned] and its
// byte-preserving rewrite, the generated default file against the Python twin's fixture
// (data\oracle\uniquetab-buttons.ini, tools\build_catalogue.py --buttons-default) and the identity
// (the default file = the built-in buttons and words), the reserved ids and tags of
// src\gen\ut_groups.h. NO GAME NEEDED. tools\build_test_buttons.bat builds and runs it.
#include <stdio.h>
#include <string.h>

#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "gen/ut_groups.h"
#include "ut_buttons.h"
#include "ut_padpage.h"   // the unions and the wheel over the resolved buttons

static int g_fail = 0, g_pass = 0;
static void check(bool ok, const std::string& what) {
    if (ok) ++g_pass;
    else ++g_fail;
    printf("  %-96s %s\n", what.c_str(), ok ? "ok" : "FAIL");
}
static std::string readAll(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}
static utb::UtbFile mustParseB(const std::string& text, const std::string& what) {
    utb::UtbFile f;
    std::string err;
    const bool ok = utb::utbParse(text, f, &err);
    check(ok, what + " parses" + (ok ? std::string() : " (" + err + ")"));
    return f;
}
static void mustRefuseB(const std::string& text, int line, const std::string& part, const std::string& what) {
    utb::UtbFile f;
    std::string err;
    const bool refused = !utb::utbParse(text, f, &err);
    check(refused && err.find("line " + std::to_string(line) + ":") == 0 && err.find(part) != std::string::npos &&
              f.buttons.empty(),
          what + " (" + err + ")");
}
// The G lines of a uniq-groups.txt: (id, tag, label), group order.
static std::vector<utb::UtbGroupIn> fixtureGroups(const std::string& path) {
    std::vector<utb::UtbGroupIn> g;
    std::stringstream s(readAll(path));
    std::string line;
    while (std::getline(s, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> p;
        std::size_t b = 0;
        for (std::size_t e; (e = line.find('\t', b)) != std::string::npos; b = e + 1) p.push_back(line.substr(b, e - b));
        p.push_back(line.substr(b));
        if (p.size() == 10 && p[0] == "G") g.push_back({p[8], p[9], utb::utbGroupLabel(p[2])});
    }
    return g;
}

static void testParse() {
    printf("\n1. the grammar (Grim Dawn's, byte for byte)\n");
    const std::string brief =
        "; a header\n[tab]\nlabel.format.1 = {group} - {owned}/{entries}   ; one template\n"
        "tooltip.yes = In your collection\nmat.caption = MAT\nnext.caption = >>\n\n"
        "[HEL]\nname    = Helms\ngroups  = head\n\n"
        "[BOW]\nname    = Bows\ncaption = BOW                           ; optional\n"
        "groups  = staff, ranged2h     ; two groups = one button\nsize    = 12\n"
        "font    = fonts/ru/savapromedium.fnt    ; optional\n\n"
        "[unassigned]\n; a comment\ngroups  = reignofterror.javelins, retired\n";
    utb::UtbFile f = mustParseB(brief, "Grim Dawn's example");
    check(f.buttons.size() == 2 && f.buttons[0].tag == "HEL" && f.buttons[1].tag == "BOW", "two buttons in file order");
    check(f.buttons[1].groups == std::vector<std::string>({"staff", "ranged2h"}) && f.buttons[1].size == 12 &&
              f.buttons[1].font == "fonts/ru/savapromedium.fnt" && f.buttons[1].caption == "BOW" &&
              f.buttons[1].groupsLine == 15,
          "[BOW]: groups, size, font (kept), caption; a ';' after a blank is a comment");
    check(f.words.common.at("label.format.1").text == "{group} - {owned}/{entries}" &&
              f.words.common.at("caption.vanilla").text == "MAT" && f.words.common.at("caption.next").text == ">>",
          "[tab] maps onto the old [text] keys (mat.caption -> caption.vanilla)");
    check(f.unassigned == std::vector<std::string>({"reignofterror.javelins", "retired"}) && f.unassignedLine == 19,
          "[unassigned] read (for nothing)");
    f = mustParseB("\xEF\xBB\xBF[tab]\r\ntooltip.no = \"  a ; b  \"  ; c\r\nlabel.unknown = a;b\r\n[a1]\r\ngroups=head\r\nname = x ;y\r\n",
                   "CRLF + BOM");
    check(f.words.common.at("tooltip.no").text == "  a ; b  " && f.words.common.at("label.unknown").text == "a;b" &&
              f.buttons.size() == 1 && f.buttons[0].name == "x",
          "quotes keep blanks and ';'; 'a;b' stays text; ' ;y' is a comment");
    f = mustParseB("[tab]\nsize.caption_max = 9\ncolor.ring = FF8000\nfont.label = fonts/albertus mt light.fnt\n"
                   "label.format.2 = {group} {found}\n[ X ]\ngroups = head\n[unassigned]\ngroups =\n",
                   "[tab] style keys and {found}");
    check(f.words.style.size[utt::kStyleSizeCaptionMax] == 9 && f.words.style.color[utt::kStyleColorRing] == 0xFF8000 &&
              f.words.style.font[utt::kStyleFontLabel] == "fonts/albertus mt light.fnt" && f.buttons[0].tag == "X",
          "the style keys are read and kept; blanks around a tag dropped");
    f = mustParseB("[tab]\nsize.caption = 12\nsize.label = 10\ncolor.caption = FFD700\ncolor.mark = 102030\n"
                   "next.caption = \xD0\x95\xD1\x89\xD1\x91\n[A]\ngroups = head\n",
                   "[tab] sizes, colours and next.caption");
    {
        utt::UtTextContext tc;
        tc.file = &f.words;
        float rgb[3] = {0.0f, 0.0f, 0.0f};
        check(utt::utStyleCaptionFixed(f.words.style) == 12 &&
                  utt::utStyleSizeOr(f.words.style, utt::kStyleSizeLabel, 13) == 10 &&
                  utt::utStyleSizeOr(f.words.style, utt::kStyleSizeCaptionMax, 14) == 14 &&
                  utt::utStyleColorRgb(f.words.style, utt::kStyleColorCaption, rgb) && rgb[0] == 1.0f &&
                  rgb[1] == 215.0f / 255.0f && rgb[2] == 0.0f &&
                  utt::utStyleColorRgb(f.words.style, utt::kStyleColorMark, rgb) &&
                  !utt::utStyleColorRgb(f.words.style, utt::kStyleColorRing, rgb) &&
                  utt::utTextResolve(tc, utt::kTextCaption, "next").text == "\xD0\x95\xD1\x89\xD1\x91",
              "the parser keeps them for the pad: size.caption 12, size.label 10, color.caption / color.mark, next.caption; "
              "the rest unset");
        utt::UtTextContext none;
        check(utt::utTextResolve(none, utt::kTextCaption, "next").text == ">>",
              "no buttons file: NEXT's caption is >>");
    }
    f = mustParseB("[tab]\ntooltip.class_yes = 30\ntooltip.class_no = 0\n[A]\ngroups = head\n", "the text styles");
    check(f.tooltipClass[0] == 30 && f.tooltipClass[1] == 0 && f.tooltipClassLine[0] == 2 && f.words.keys == 2,
          "tooltip.class_yes / tooltip.class_no (TQ's): read, counted as [tab] words");
    f = mustParseB("[tab]\ntooltip.roll = \" (noch ein Wurf)\"\n[A]\ngroups = head\n", "a quoted tooltip.roll");
    check(f.words.common.at("tooltip.roll").text == " (noch ein Wurf)", "tooltip.roll keeps its quoted blank");
}

static void testRefusals() {
    printf("\n2. every refusal names its line\n");
    mustRefuseB("[HEL]\nname = Helms\n", 1, "has no groups", "a button without groups");
    mustRefuseB("[HELM]\ngroups = head\n", 1, "not a button tag", "a 4-character tag");
    mustRefuseB("[mat]\ngroups = head\n", 1, "is taken", "MAT is taken (the Transfer toggle's caption key)");
    mustRefuseB("[A]\ngroups = head\n[a]\ngroups = feet\n", 3, "appears twice (first at line 1)", "a tag twice, any case");
    mustRefuseB("[A]\ngroups = head\nname = x\nname = y\n", 4, "set twice in [A] (first at line 3)", "a key twice");
    mustRefuseB("[A]\ngroups = head, , feet\n", 2, "empty id", "an empty id");
    mustRefuseB("[A]\ngroups = head, Feet!\n", 2, "not a group id", "a bad id");
    mustRefuseB("[A]\ngroups = head, head\n", 2, "listed twice", "an id twice on one line");
    mustRefuseB("[A]\ngroups =\n", 2, "a button needs at least one", "an empty groups line on a button");
    mustRefuseB("[A]\ngroups = head\nsize = 5\n", 3, "6..36", "a size below 6");
    mustRefuseB("[A]\ngroups = head\nfont = ../x.fnt\n", 3, "fonts/<name>.fnt", "a font outside fonts/");
    mustRefuseB("[A]\ngroups = head\ncaption = ABCDEFGHIJKLM\n", 3, "longer than 12", "a caption too long");
    mustRefuseB("[A]\ngroups = head\ncolor = 123456\n", 3, "unknown key \"color\" in [A]", "an unknown button key");
    mustRefuseB("groups = head\n", 1, "outside a section", "a key outside a section");
    mustRefuseB("[tab]\nlabel.format.1 = {nope}\n", 2, "unknown placeholder", "a bad template");
    mustRefuseB("[tab]\ncaption.head = HEL\n", 2, "unknown key \"caption.head\" in [tab]", "an old [text] key");
    mustRefuseB("[tab]\nsize.label = 99\n", 2, "\"size.label\" must be a whole number 8..32", "[tab] size range");
    mustRefuseB("[tab]\ntooltip.yes =\n", 2, "\"tooltip.yes\" is empty", "an empty [tab] word");
    mustRefuseB("[tab]\nmat.caption = ABCDEFGHIJKLM\n", 2, "\"mat.caption\": longer than 12", "[tab] names its own key");
    mustRefuseB("[tab]\ntooltip.class_yes = 58\n", 2, "a game text style, a whole number 0..57", "a text style above 57");
    mustRefuseB("[tab]\ntooltip.class_no = green\n", 2, "0..57", "a text style that is no number");
    mustRefuseB("[tab]\ntooltip.class_no = 1\ntooltip.class_no = 2\n", 3, "set twice in [tab]", "a text style twice");
    mustRefuseB("[tab]\n[tab]\n", 2, "[tab] appears twice", "[tab] twice");
    mustRefuseB("[unassigned]\nname = x\n", 2, "only groups", "[unassigned] takes groups only");
    mustRefuseB("[A]\ngroups = head\nname = \"x\" y\n", 3, "text after the closing", "text after a quote");
    mustRefuseB("[A] x\ngroups = head\n", 1, "text after the section header", "text after a header");
    mustRefuseB("[A]\ngroups = head\nname = \xC3\x28\n", 3, "not UTF-8", "not UTF-8");
}

static std::vector<std::string> knownIds() {
    std::vector<std::string> v;
    std::size_t n = 0;
    const utg::BuiltinGroup* t = utg::builtinGroups(&n);
    for (std::size_t i = 0; i < n; ++i) v.push_back(t[i].id);
    v.push_back("other");
    return v;
}

static void testResolve(const std::vector<utb::UtbGroupIn>& g) {
    printf("\n3. buttons -> groups against TQ's table (%u groups)\n", (unsigned)g.size());
    utb::UtbSet s;
    std::string err;
    utb::UtbFile f = mustParseB("[RNG]\nname = Rings & Amulets\ngroups = ring, amulet\n[HEL]\ngroups = head\n"
                                "[SHO]\ngroups = shoulders\n",
                                "two groups on one button");
    check(utb::utbResolve(f, g, knownIds(), s, &err), "resolves (" + err + ")");
    check(s.buttons.size() == 2 && s.dropped == 1 && s.buttons[0].groups == std::vector<int>({4, 5}),
          "file order; the union in GROUP order (amulet 4 before ring 5); a built-in id TQ fills nothing with drops its button");
    check(s.buttonOfGroup[4] == 0 && s.buttonOfGroup[5] == 0 && s.buttonOfGroup[0] == 1 && s.buttonOfGroup[1] == -1,
          "group -> button; an unlisted group has none");
    check(s.unassigned.size() == 13 && s.unassigned[0] == "chest" && s.unassigned.back() == "blueprint",
          "[unassigned] = every group no button holds, in group order");
    f = mustParseB("[A]\ngroups = head\n[B]\ngroups = legs, head\n", "an id on two buttons");
    check(!utb::utbResolve(f, g, knownIds(), s, &err) && err.find("line 4:") == 0 &&
              err.find("on [A] already (line 2)") != std::string::npos && utb::utbIsBuiltin(s, g),
          "an id on two buttons: refused naming both lines, the built-in set instead");
    f = mustParseB("[A]\ngroups = relic\n", "Grim Dawn's id for the artifacts");
    check(!utb::utbResolve(f, g, knownIds(), s, &err) && err.find("\"relic\"") != std::string::npos,
          "relic is no TQ group id (the Artifacts are artifact): refused naming it");
}

static void testUnassigned() {
    printf("\n4. the [unassigned] rewrite (byte-preserving)\n");
    std::string out;
    const std::string lf = "[A]\ngroups = head\n\n[unassigned]\n; mine\ngroups = feet\n  \n[B]\ngroups = legs\n";
    check(utb::utbRewriteUnassigned(lf, {"feet", "ring"}, &out) &&
              out == "[A]\ngroups = head\n\n[unassigned]\n; mine\ngroups = feet, ring\n  \n[B]\ngroups = legs\n",
          "LF: the groups line replaced in place; everything else kept");
    const std::string once = out;
    check(!utb::utbRewriteUnassigned(once, {"feet", "ring"}, &out) && out == once, "the same set: nothing to write");
    const std::string crlf = "[A]\r\ngroups = head\r\n[unassigned]\r\n; c1\r\ngroups = a\r\n; c2\r\ngroups = b\r\n\r\n";
    check(utb::utbRewriteUnassigned(crlf, {}, &out) && out == "[A]\r\ngroups = head\r\n[unassigned]\r\n; c1\r\ngroups =\r\n; c2\r\n\r\n",
          "CRLF: two groups lines become one, the comment between kept");
    check(utb::utbRewriteUnassigned("[A]\ngroups = head", {"x"}, &out) &&
              out == "[A]\ngroups = head\n\n[unassigned]\n; rewritten by the mod: every group of this game that no button lists\ngroups = x\n",
          "no section: appended");
    check(utb::utbRewriteUnassigned("[unassigned]", {"x"}, &out) && out == "[unassigned]\ngroups = x\n", "a bare header");
}

static void testDefault(const std::string& root, const std::vector<utb::UtbGroupIn>& g) {
    printf("\n5. the default file: C++ = Python, and = the built-in buttons and words\n");
    const std::string text = utb::utbDefaultText(utb::utbViewTitle(""), g);
    const std::string want = readAll(root + "/data/oracle/uniquetab-buttons.ini");
    check(!want.empty() && utb::utbCrlf(text) == want,
          "C++ utbDefaultText = tools\\build_catalogue.py buttons_default_text, byte for byte (" +
              std::to_string(want.size()) + " B)");
    utb::UtbFile f = mustParseB(text, "the generated file");
    utb::UtbSet s;
    std::string err;
    check(utb::utbResolve(f, g, knownIds(), s, &err) && s.buttons.size() == g.size() && s.unassigned.empty() && !s.dropped,
          std::to_string(g.size()) + " buttons, one per group in pad order, none unassigned");
    std::string rw;
    check(!utb::utbRewriteUnassigned(want, s.unassigned, &rw), "the generated file needs no [unassigned] rewrite");
    bool ids = true;
    for (std::size_t i = 0; i < g.size(); ++i)
        ids = ids && s.buttons[i].tag == g[i].tag && s.buttons[i].groups == std::vector<int>(1, (int)i) &&
              s.buttons[i].name == g[i].label;
    check(ids && g.back().id == "relics" && s.buttons.back().tag == "REL", "tag = the G line's tag, name = its label; [REL] groups = relics, last");
    utt::UtTextContext none, c;
    c.file = &f.words;
    const utb::UtbPad p = utb::utbResolvePad(c, s, g);
    const std::vector<std::string> caps = {"Transfer", "Helm", "Torso", "Arms", "Legs", "Amul", "Ring", "Shld", "Axe",
                                           "Mace", "Staff", "Sword", "Throw", "Spear", "Bow", "Artf", "BLU", "Relic", "OWN", ">>",
                                           "EDIT"};
    check(p.caps == caps && p.skipped.empty(), "the captions: Transfer, TQ 1.0.1's short names, OWN, >>");
    std::vector<std::string> fm, fm0;
    utt::utTextResolveFormats(c, fm, nullptr);
    utt::utTextResolveFormats(none, fm0, nullptr);
    bool words = fm == fm0 && fm.size() == 4;
    for (utt::UtTextKind k : {utt::kTextModeUnknown, utt::kTextUnknown, utt::kTextTooltipYes, utt::kTextTooltipNo,
                              utt::kTextTooltipRoll})
        words = words && utt::utTextResolve(c, k, "").text == utt::utTextResolve(none, k, "").text;
    check(words && f.tooltipClass[0] == 22 && f.tooltipClass[1] == 24,
          "the identity: the templates, the words, the rollover lines and their styles 22 / 24");
    check(utb::utbDefaultText(utb::utbViewTitle("mymod"), {}).find("in the game mymod.") != std::string::npos &&
              text.find("in the main game.") != std::string::npos,
          "the header names the game");
    const std::string odd = utb::utbDefaultText("x", {{"a", "MAT", "A"}, {"b", "BBB", "B"}, {"c", "bbb", "C"}});
    f = mustParseB(odd, "a reserved and a duplicate tag");
    check(f.buttons.size() == 1 && f.unassigned == std::vector<std::string>({"a", "c"}), "a reserved / duplicate tag: [unassigned]");
    std::string offered;
    const std::string souls = "svaera_customquest-8350d063.souls";
    check(utb::utbLateBuiltinIds() == std::vector<std::string>({souls, "relics"}) &&
              !utb::utbOfferNew(want, mustParseB(want, "the fixture"), g, utb::utbLateBuiltinIds(), &offered, nullptr, nullptr),
          "the late group is the custom game's souls page; a game without it is offered nothing");
    // a game that has the souls group and a buttons file written before it: [SOU] once, after the
    // last button, every other byte kept; a second load offers nothing
    std::vector<utb::UtbGroupIn> gs = g;
    gs.insert(gs.begin(), utb::UtbGroupIn{souls, "SOU", "Souls"});
    std::vector<std::string> added, refused;
    const bool once = utb::utbOfferNew(want, mustParseB(want, "the fixture"), gs, utb::utbLateBuiltinIds(), &offered,
                                       &added, &refused);
    const std::size_t at = offered.find("[SOU]");   // in the file's own line ends
    check(once && added == std::vector<std::string>({souls}) && refused.empty() && at != std::string::npos &&
              offered.find("groups  = " + souls, at) != std::string::npos &&
              offered.find(std::string(utb::utbOfferedPrefix()) + " relics, " + souls) != std::string::npos &&
              offered.size() > want.size(),
          "a file written before the souls page gets [SOU] once and the added-once line");
    std::string again;
    check(!utb::utbOfferNew(offered, mustParseB(offered, "the offered file"), gs, utb::utbLateBuiltinIds(), &again,
                            nullptr, nullptr),
          "the second load offers nothing");
    const std::string fresh = utb::utbDefaultText(utb::utbViewTitle("x"), gs);
    check(fresh.find(std::string(utb::utbOfferedPrefix()) + " " + souls) != std::string::npos &&
              fresh.find("\n[SOU]\n") != std::string::npos &&
              !utb::utbOfferNew(fresh, mustParseB(fresh, "the default file"), gs, utb::utbLateBuiltinIds(), &again,
                                nullptr, nullptr),
          "a default file written with the souls page names it on the added-once line; nothing is offered");
}

static void testReserved() {
    printf("\n6. the reserved ids and tags (src\\gen\\ut_groups.h)\n");
    std::size_t n = 0;
    const utg::BuiltinGroup* t = utg::builtinGroups(&n);
    bool clean = true, sameOrder = true;
    std::size_t bn = 0;
    const utt::UtTextBuiltin* b = utt::utTextBuiltins(&bn);
    for (std::size_t i = 0; i < n; ++i) {
        clean = clean && !utg::reservedId(t[i].id) && !utg::reservedTag(t[i].tag) && !utb::utbReservedTag(t[i].tag);
        sameOrder = sameOrder && i + 1 < bn && std::string(b[i + 1].id) == t[i].id;
    }
    check(n == 25 && clean, "no built-in group takes a reserved id or tag");
    check(sameOrder, "ut_textcfg.h's built-in words list the groups in ut_groups.h's order");
    check(utg::reservedId("vanilla") && utg::reservedId("owned") && utg::reservedId("auto.x") && !utg::reservedId("artifact"),
          "vanilla, owned, next, other, auto.* are reserved ids");
    check(utg::reservedTag("MAT") && utg::reservedTag("OWN") && !utg::reservedTag("ART"), "MAT and OWN are reserved tags");
}

// GD's resolve cases that apply: an unknown id names its line, the built-in set, and the pad's
// per-button words (captions, names, sizes, fonts) from a file's buttons.
static void testResolveMore(const std::vector<utb::UtbGroupIn>& g) {
    printf("\n7. resolve: GD's further cases over TQ's groups\n");
    utb::UtbSet s;
    std::string err;
    utb::UtbFile f = mustParseB("[A]\ngroups = hed\n", "a typo");
    check(!utb::utbResolve(f, g, knownIds(), s, &err) && err.find("line 2:") == 0 && err.find("\"hed\"") != std::string::npos,
          "an unknown id: refused naming the line (" + err + ")");
    check(utb::utbIsBuiltin(utb::utbBuiltinSet(g), g) && utb::utbBuiltinSet(g).unassigned.empty(), "the built-in set");
    {   // no usable file: the pad draws exactly what the written default file says
        utb::UtbFile df;
        utb::UtbSet ds;
        std::string why;
        const bool parsed = utb::utbParse(utb::utbDefaultText("", g), df, &why) &&
                            utb::utbResolve(df, g, std::vector<std::string>(), ds, &why);
        const utb::UtbSet dset = utb::utbDefaultSet(g);
        bool same = parsed && dset.buttons.size() == ds.buttons.size() && dset.unassigned == ds.unassigned;
        for (std::size_t i = 0; same && i < ds.buttons.size(); ++i)
            same = dset.buttons[i].tag == ds.buttons[i].tag && dset.buttons[i].caption == ds.buttons[i].caption &&
                   dset.buttons[i].name == ds.buttons[i].name && dset.buttons[i].groups == ds.buttons[i].groups;
        utt::UtTextContext c;
        const utb::UtbPad p1 = utb::utbResolvePad(c, dset, g), p2 = utb::utbResolvePad(c, ds, g);
        same = same && p1.caps == p2.caps && p1.names == p2.names && dset.buttons.size() == g.size();
        std::string caps;
        for (const std::string& cap : p1.caps) caps += (caps.empty() ? "" : " ") + cap;
        check(same, "no file: the default file's buttons - one per group, its captions and names (" + caps + ")");
        const std::string row = " " + caps + " ";
        check(caps == "Transfer Helm Torso Arms Legs Amul Ring Shld Axe Mace Staff Sword Throw Spear Bow Artf BLU OWN >> EDIT" &&
                  row.find("Arcane For") == std::string::npos,
              "no file: the captions are the default file's, literally (the tag BLU, never a label cut to fit)");
    }
    f = mustParseB("[RNG]\nname = Rings & Amulets\ncaption = Jwl\nsize = 9\ngroups = ring, amulet\n"
                   "[HEL]\ngroups = head\nfont = fonts/ru/savapromedium.fnt\ncaption = Hat\n",
                   "a pad");
    check(utb::utbResolve(f, g, knownIds(), s, &err), "a pad resolves (" + err + ")");
    utt::UtTextContext c;
    const utb::UtbPad p = utb::utbResolvePad(c, s, g);
    check(p.caps.size() == 6 && p.caps[5] == "EDIT" && p.caps[1] == "Jwl" && p.caps[2] == "Hat" && p.caps[3] == "OWN" && p.names.size() == 2 &&
              p.names[0] == "Rings & Amulets" && p.names[1] == g[0].label && p.sizes[1] == 9 &&
              p.fonts[2] == "fonts/ru/savapromedium.fnt" && p.skipped.empty(),
          "the pad: the toggle, the file's buttons in file order, OWN, NEXT; names, sizes and fonts per button");
}

static void testUnassignedMore() {
    printf("\n8. the [unassigned] rewrite: GD's further cases\n");
    std::string out;
    check(utb::utbRewriteUnassigned("[A]\r\ngroups = h\r\n[unassigned]\r\n; only a comment\r\n", {"x", "y"}, &out) &&
              out == "[A]\r\ngroups = h\r\n[unassigned]\r\n; only a comment\r\ngroups = x, y\r\n",
          "no groups line: after the section's last comment");
    check(utb::utbRewriteUnassigned("[unassigned]\ngroups = a   \n[B]\ngroups = b\n", {"a"}, &out) &&
              out == "[unassigned]\ngroups = a\n[B]\ngroups = b\n",
          "trailing blanks of the rewritten line go, the rest is byte for byte");
    utb::UtbFile f = mustParseB(out, "a rewritten file");
    check(f.unassigned == std::vector<std::string>({"a"}), "the rewritten section reads back");
    const std::string before = "[SPE]\nname    = Spears\ngroups  = spear2h\n\n[unassigned]\n"
                               "; rewritten by the mod: every group of this game that no button lists\ngroups =\n";
    check(utb::utbRewriteUnassigned(before, {"ranged2h"}, &out) &&
              out == before.substr(0, before.size() - 1) + " ranged2h\n",
          "a deleted button's group lands under [unassigned]");
}

// utbOfferNew, GD's cases. TQ's late list is empty today, so a synthetic late group (GD's
// spear1h) stands in for the groups a later release adds.
static const std::vector<std::string>& lateIds() {
    static const std::vector<std::string> v = {"spear1h"};
    return v;
}
static bool offer(const std::string& bytes, const std::vector<utb::UtbGroupIn>& g, std::string* out,
                  std::vector<std::string>* added = nullptr, std::vector<std::string>* refused = nullptr) {
    utb::UtbFile f = mustParseB(bytes, "a buttons file before the offer");
    return utb::utbOfferNew(bytes, f, g, lateIds(), out, added, refused);
}
static std::string removeSection(const std::string& t, const std::string& tag) {
    const std::string eol = t.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    const std::size_t a = t.find("[" + tag + "]" + eol);
    if (a == std::string::npos) return t;
    std::size_t b = t.find(eol + "[", a + 1);
    b = b == std::string::npos ? t.size() : b + eol.size();
    return t.substr(0, a) + t.substr(b);
}

static void testOfferNew(const std::string& root, const std::vector<utb::UtbGroupIn>& g15) {
    printf("\n9. a group that came later gets its button once (utbOfferNew, GD's cases)\n");
    std::vector<utb::UtbGroupIn> g = g15;
    g.push_back({"spear1h", "SP1", "1H Spears"});
    const std::string old = readAll(root + "/data/oracle/uniquetab-buttons.ini");
    std::string out;
    std::vector<std::string> added, refused;
    check(offer(old, g, &out, &added, &refused) && added == std::vector<std::string>({"spear1h"}) && refused.empty(),
          "TQ's default file and a later group: offered");
    const std::string once = out;
    const std::string sec = "[SP1]\r\nname    = 1H Spears\r\ngroups  = spear1h\r\n\r\n";
    const std::string mk = ", spear1h";   // the default file's added-once line names relics already
    std::string back = once;
    const std::size_t at = back.find(sec);
    if (at != std::string::npos) back.erase(at, sec.size());
    const std::size_t ma = back.find(mk);
    if (ma != std::string::npos) back.erase(ma, mk.size());
    check(at != std::string::npos && ma != std::string::npos && back == old, "every other byte kept, CRLF throughout");
    check(at != std::string::npos && once.compare(at + sec.size(), 12, "[unassigned]") == 0 && ma > at,
          "[SP1] after the last button, right before [unassigned]; the line inside [unassigned]");
    bool lone = false;
    for (std::size_t i = 0; i < once.size(); ++i) lone |= once[i] == '\n' && (i == 0 || once[i - 1] != '\r');
    check(!lone, "no lone LF in a CRLF file");
    utb::UtbFile f = mustParseB(once, "the file after the offer");
    utb::UtbSet set;
    std::string err;
    check(utb::utbResolve(f, g, {}, set, &err) && set.unassigned.empty() && !f.buttons.empty() &&
              f.buttons.back().tag == "SP1" && f.buttons.back().name == "1H Spears",
          "it resolves: [SP1] is the last button, nothing on no button");
    std::string rw;
    check(!utb::utbRewriteUnassigned(once, set.unassigned, &rw), "and needs no [unassigned] rewrite");
    check(!offer(once, g, &out) && out == once, "a second load adds nothing");
    const std::string deleted = removeSection(once, "SP1");
    check(deleted != once && !offer(deleted, g, &out), "[SP1] deleted: not offered again");
    utb::UtbFile fd = mustParseB(deleted, "the file without [SP1]");
    utb::UtbSet sd;
    check(utb::utbResolve(fd, g, {}, sd, &err) && sd.unassigned == std::vector<std::string>({"spear1h"}),
          "the deleted group is on no button");
    check(utb::utbRewriteUnassigned(deleted, sd.unassigned, &rw) && rw.find(mk) != std::string::npos &&
              rw.find("groups = spear1h\r\n") != std::string::npos && !offer(rw, g, &out),
          "the rewrite lists it under [unassigned], keeps the added-once line, and it stays off");

    // LF, a file with no [unassigned], a group the file already lists, a taken tag
    g = {{"head", "HEL", "Helms"}, {"artifact", "ART", "Artifacts"}, {"spear1h", "SP1", "1H Spears"}};
    const std::string lf = "; mine\n[HEL]\ngroups = head\n\n[ART]\ngroups = artifact\n\n[unassigned]\n; rewritten\ngroups =\n";
    check(offer(lf, g, &out) &&
              out == "; mine\n[HEL]\ngroups = head\n\n[ART]\ngroups = artifact\n\n[SP1]\nname    = 1H Spears\n"
                     "groups  = spear1h\n\n[unassigned]\n; rewritten\n" + std::string(utb::utbOfferedPrefix()) +
                         " spear1h\ngroups =\n",
          "LF: the section and the line, LF");
    const std::string bare = "[HEL]\ngroups = head\n[ART]\ngroups = artifact";
    check(offer(bare, g, &out) &&
              out == "[HEL]\ngroups = head\n[ART]\ngroups = artifact\n\n[SP1]\nname    = 1H Spears\ngroups  = spear1h\n"
                     "\n[unassigned]\n; rewritten by the mod: every group of this game that no button lists\n" +
                         std::string(utb::utbOfferedPrefix()) + " spear1h\n",
          "no [unassigned] and an unterminated last line: the section, then a new [unassigned] with the line");
    utb::UtbFile fb = mustParseB(out, "the bare file after the offer");
    utb::UtbSet sb;
    check(utb::utbResolve(fb, g, {}, sb, &err) && sb.unassigned.empty() && utb::utbRewriteUnassigned(out, sb.unassigned, &rw) &&
              rw.find(std::string(utb::utbOfferedPrefix()) + " spear1h\ngroups =\n") != std::string::npos,
          "the appended [unassigned] takes the rewrite's groups line");
    const std::string listed = "[HEL]\ngroups = head\n[ART]\ngroups = artifact\n[unassigned]\ngroups = spear1h\n";
    check(offer(listed, g, &out) &&
              out == "[HEL]\ngroups = head\n[ART]\ngroups = artifact\n\n[SP1]\nname    = 1H Spears\ngroups  = spear1h\n"
                     "\n[unassigned]\n" + std::string(utb::utbOfferedPrefix()) + " spear1h\ngroups = spear1h\n",
          "a group the file lists under [unassigned] but never offered (not on the added-once line): offered once");
    const std::string kept = "[HEL]\ngroups = head\n[ART]\ngroups = artifact\n[unassigned]\n" +
                             std::string(utb::utbOfferedPrefix()) + " spear1h\ngroups = spear1h\n";
    check(!offer(kept, g, &out) && out == kept, "listed and on the added-once line: it stays there");
    added.clear();
    refused.clear();
    const std::string taken = "[HEL]\ngroups = head\n[sp1]\ngroups = artifact\n\n[unassigned]\ngroups =\n";
    check(offer(taken, g, &out, &added, &refused) && added.empty() &&
              refused == std::vector<std::string>({"spear1h"}) &&
              out == "[HEL]\ngroups = head\n[sp1]\ngroups = artifact\n\n[unassigned]\n" +
                         std::string(utb::utbOfferedPrefix()) + " spear1h\ngroups =\n" &&
              !offer(out, g, &rw),
          "the tag SP1 is a button already: no section, only the line (it lands in [unassigned])");
    const std::string mid = "[unassigned]\ngroups =\n\n[HEL]\ngroups = head\n";
    check(offer(mid, g, &out) &&
              out == "[unassigned]\n" + std::string(utb::utbOfferedPrefix()) +
                         " spear1h\ngroups =\n\n[HEL]\ngroups = head\n\n[SP1]\nname    = 1H Spears\ngroups  = spear1h\n",
          "[unassigned] before the buttons: the section after the LAST button");
    const std::vector<utb::UtbGroupIn> noBp = {{"head", "HEL", "Helms"}};
    check(!offer("[HEL]\ngroups = head\n", noBp, &out), "a game without the group: nothing");
}

// Random buttons files from troublesome pieces: the parser never refuses without naming a line,
// and a refused file leaves no button behind.
static void testFuzzB() {
    printf("\n10. fuzz: 3000 random buttons files\n");
    static const char* const kPieces[] = {
        "[HEL]", "[tab]", "[unassigned]", "[MAT]", "[text]", "[", "]", "\n", "\r\n", "\n", " ", "=", ";", "#", "\"",
        "name", "caption", "groups", "size", "font", "head", "ring, amulet", ",", "label.format.1", "tooltip.yes",
        "tooltip.class_yes", "mat.caption", "{group}", "{found}", "{", "}", "13", "fonts/a.fnt", "\xD0\xA8",
        "\xF0\x9F\x98\x80", "\xD0", "\x80", "\xEF\xBB\xBF", "\t", "HEL",
    };
    const std::size_t nPieces = sizeof kPieces / sizeof *kPieces;
    std::mt19937 rng(777);
    int accepted = 0, refused = 0;
    bool named = true, clean = true;
    std::string firstUnnamed;
    for (int t = 0; t < 3000; ++t) {
        std::string text;
        const int len = int(rng() % 40);
        for (int i = 0; i < len; ++i) text += kPieces[rng() % nPieces];
        utb::UtbFile f;
        std::string err;
        if (utb::utbParse(text, f, &err)) {
            ++accepted;
        } else {
            ++refused;
            if (err.compare(0, 5, "line ") != 0) {
                if (named) firstUnnamed = err;
                named = false;
            }
            if (!f.buttons.empty()) clean = false;
        }
    }
    check(named && clean && accepted > 0 && refused > 0,
          "fuzz: " + std::to_string(accepted) + " accepted, " + std::to_string(refused) +
              " refused, every refusal names its line" + (named ? std::string() : " (not: " + firstUnnamed + ")"));
}

// The two headers stay pure: standard headers only (and the buttons file's one on the text
// file), plain ASCII - any compiler, any code page.
static void testPurity(const std::string& header, const std::vector<std::string>& allowed) {
    const std::string src = readAll(header);
    check(!src.empty(), "purity: the header is readable (" + header + ")");
    std::size_t pos = 0;
    std::size_t includes = 0;
    bool pure = true;
    while ((pos = src.find("#include", pos)) != std::string::npos) {
        const std::size_t eol = src.find('\n', pos);
        std::string line = src.substr(pos + 8, eol - pos - 8);
        if (!line.empty() && line.back() == '\r') line.pop_back();   // a CRLF checkout
        const std::string what = utt::utTextTrim(line);
        bool ok = false;
        for (const std::string& a : allowed) ok = ok || what == a;
        if (!ok) {
            pure = false;
            printf("  not an allowed header: %s\n", what.c_str());
        }
        ++includes;
        pos = eol;
    }
    check(pure && includes == allowed.size(),
          "purity: " + header.substr(header.rfind('/') + 1) + " includes only " + std::to_string(allowed.size()) +
              " allowed headers");
    bool ascii = true;
    for (unsigned char ch : src) ascii = ascii && !(ch > 0x7E || (ch < 0x20 && ch != '\n' && ch != '\r'));
    check(ascii, "purity: " + header.substr(header.rfind('/') + 1) + " is plain ASCII");
}

// The resolve as the pad draws it (utbPadMap): one cell per button in file order, a union's groups
// on one button, a group left off on none (and under [unassigned]); the wheel over that map.
static void testPadMap(const std::string& root, const std::vector<utb::UtbGroupIn>& g) {
    printf("\n12. resolve -> pad: the buttons the pad draws\n");
    utb::UtbSet s;
    std::string err;
    utt::UtTextContext c;
    utb::UtbFile f = mustParseB(readAll(root + "/data/oracle/uniquetab-buttons.ini"), "the default file");
    check(utb::utbResolve(f, g, knownIds(), s, &err), "the default file resolves (" + err + ")");
    c.file = &f.words;
    utb::UtbPadMap m = utb::utbPadMap(s, utb::utbResolvePad(c, s, g));
    bool ident = m.buttons() == 17 && m.buttonOfGroup.size() == 17;
    for (int b = 0; b < m.buttons() && ident; ++b)
        ident = m.first(b) == b && m.buttonOfGroup[(size_t)b] == b && m.groups[(size_t)b].size() == 1;
    check(ident, "the default file: 17 buttons, button b = group b (today's pad in today's order)");
    check(m.caps.size() == 17 && m.caps[0] == "Helm" && m.caps[1] == "Torso" && m.caps[14] == "Artf" && m.caps[15] == "BLU" &&
              m.caps[16] == "Relic" && m.sizes[0] == -1,
          "the default file: the captions are 1.0.1's short names (" + (m.caps.empty() ? std::string() : m.caps[0]) + " ... " +
              (m.caps.size() < 15 ? std::string() : m.caps[14]) + ")");
    check(m.names.size() == 17 && m.names[0] == g[0].label, "the default file: a button's name is its group's label");
    // two groups on one button, one left off, the rest one each
    std::string t = "[HT]\nname = Helms and Torso\ncaption = H+T\nsize = 8\ngroups = " + g[1].id + ", " + g[0].id + "\n";
    for (size_t i = 2; i < g.size(); ++i)
        if (i != 5) t += "[B" + std::to_string(i) + "]\ngroups = " + g[i].id + "\n";
    f = mustParseB(t, "a union and a group left off");
    c.file = &f.words;
    check(utb::utbResolve(f, g, knownIds(), s, &err), "a union and a group left off resolve (" + err + ")");
    m = utb::utbPadMap(s, utb::utbResolvePad(c, s, g));
    check(m.buttons() == 15 && m.groups[0] == std::vector<int>({0, 1}) && m.first(0) == 0 && m.first(1) == 2,
          "15 buttons; [HT] holds groups 0 and 1 (group order) and keys its page by group 0");
    check(m.caps[0] == "H+T" && m.sizes[0] == 8 && m.names[0] == "Helms and Torso" && m.caps[1] == "B2",
          "[HT]: its caption, size and name; a button with no caption shows its tag");
    check(m.buttonOfGroup[0] == 0 && m.buttonOfGroup[1] == 0 && m.buttonOfGroup[5] == -1 && s.unassigned.size() == 1 &&
              s.unassigned[0] == g[5].id,
          "group 5 is on no button and listed under [unassigned] (" + g[5].id + ")");
    const ut::UtUnionPlan up = ut::utUnionPlan(15, [&](int x, int* out, int cap) {
        int k = 0;
        for (int y = 0; y < 15 && k < cap; ++y)
            if (m.buttonOfGroup[(size_t)x] >= 0 && m.buttonOfGroup[(size_t)y] == m.buttonOfGroup[(size_t)x]) out[k++] = y;
        return k;
    });
    check(up.members.size() == 1 && up.members[0] == std::vector<int>({0, 1}) && ut::utUnionRep(up, 1) == 0 &&
              ut::utUnionRep(up, 5) == 5,
          "one union (groups 0 and 1, keyed by 0); every other group its own page");
    auto bo = [&](int x) { return x >= 0 && x < 15 ? m.buttonOfGroup[(size_t)x] : -1; };
    std::vector<int> walk;
    int cur = -1;
    for (int i = 0; i < 14; ++i) walk.push_back(cur = ut::utPadWheelButton(cur, +1, 15, bo));
    const std::vector<int> want = {0, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, -1};
    check(walk == want, "Ctrl+wheel walks Transfer, [HT], [B2] ... [B14], Transfer; group 5 is never a stop");
}

#include "../src/ut_textmeasure.h"

// The font keys of a buttons file name the game's fonts by their Fonts.arc paths: [tab]
// font.caption / font.label and a button's font resolve to one of the nine (ut_textmeasure.h); a
// path that is not one of them (GD's language folders) resolves to none, so the role's font draws.
static void testFontKeys() {
    printf("\nthe font keys: Titan Quest's font paths\n");
    const utb::UtbFile f = mustParseB(
        "[tab]\nfont.caption = fonts/arial.fnt\nfont.label = fonts/DejaVuSerif.fnt\n"
        "[HEL]\ngroups = head\nfont = fonts/override_asia.fnt\ncaption = \xD0\xA8\xD0\xBB\xD0\xB5\xD0\xBC\n"
        "[TOR]\ngroups = chest\nfont = fonts/ru/savapromedium.fnt\n",
        "the font keys");
    check(ut::utTmFontOfPath(f.words.style.font[utt::kStyleFontCaption].c_str()) == ut::kUtTmArial &&
              ut::utTmFontOfPath(f.words.style.font[utt::kStyleFontLabel].c_str()) == ut::kUtTmDejaVuSerif &&
              ut::utTmFontOfPath(f.buttons[0].font.c_str()) == ut::kUtTmAsia &&
              ut::utTmFontOfPath(f.buttons[1].font.c_str()) == -1 &&
              ut::utTmFontOfPath("fonts/albertus mt light.fnt") == ut::kUtTmAlbertusLight &&
              ut::utTmFontOfPath("Fonts\\Times_EE.fnt") == ut::kUtTmTimesEe,
          "font.caption / font.label / font = resolve to TQ's fonts (any case); a GD path to none");
    bool paths = true;
    for (int i = 0; i < ut::kUtTmFontCount; ++i) {
        std::string norm;
        paths = paths && utt::utStyleFontOk(ut::utTmFontPath(i), &norm) && ut::utTmFontOfPath(norm.c_str()) == i;
    }
    check(paths, "every one of the nine font paths passes the file's font check and resolves back to itself");
}

// A letter above U+FFFF (GD README "Alphabets"): Titan Quest's glyph check (utbGlyphDrawable) passes
// every letter up to U+FFFF and none above, so a caption holding one falls back to the tag, a name to
// its group's own name, a [tab] word to the built-in English; one WARN names the first (utbSkippedWarn).
static void testAboveBmp(const std::vector<utb::UtbGroupIn>& g) {
    printf("\n13. a letter above U+FFFF: the caption -> the tag, the name -> the group's name, one WARN\n");
    check(utb::utbGlyphDrawable(utt::kFontCaption, 0x41, nullptr) && utb::utbGlyphDrawable(utt::kFontLabel, 0x4E00, nullptr) &&
              utb::utbGlyphDrawable(utt::kFontCaption, 0xFFFF, nullptr) && !utb::utbGlyphDrawable(utt::kFontCaption, 0x10000, nullptr) &&
              !utb::utbGlyphDrawable(utt::kFontTooltip, 0x1F5E1, nullptr),
          "the glyph check: A, U+4E00 and U+FFFF draw; U+10000 and U+1F5E1 do not");
    const std::string dagger = "\xF0\x9F\x97\xA1";   // U+1F5E1
    const std::string text = "[tab]\nown.caption = \xF0\x9F\x92\x8E\n"   // U+1F48E
                             "[SWO]\ncaption = " + dagger + " Swords\nname = " + dagger + " Swords\ngroups = " + g[1].id + "\n"
                             "[HEL]\ncaption = \xD0\xA8\xD0\xBB\xD0\xB5\xD0\xBC\nname = \xE5\xA4\xB4\xE7\x9B\x94\ngroups = " + g[0].id + "\n";
    utb::UtbFile f = mustParseB(text, "a file with U+1F5E1 and U+1F48E");
    utb::UtbSet s;
    std::string err;
    check(utb::utbResolve(f, g, knownIds(), s, &err) && s.buttons.size() == 2, "it resolves (" + err + ")");
    utt::UtTextContext plain, c;
    plain.file = c.file = &f.words;
    c.glyph = utb::utbGlyphDrawable;
    const utb::UtbPad p0 = utb::utbResolvePad(plain, s, g);
    check(p0.caps[1] == dagger + " Swords" && p0.skipped.empty(), "no glyph check: the caption is taken as it is (1.0.1)");
    const utb::UtbPad p = utb::utbResolvePad(c, s, g);
    check(p.caps.size() == 6 && p.caps[5] == "EDIT" && p.caps[1] == "SWO" && p.caps[2] == "\xD0\xA8\xD0\xBB\xD0\xB5\xD0\xBC" && p.caps[3] == "OWN" &&
              p.caps[4] == ">>",
          "the captions: [SWO] -> its tag SWO, [HEL]'s Cyrillic kept, own.caption -> OWN (" + p.caps[1] + ")");
    check(p.names.size() == 2 && p.names[0] == g[1].label && p.names[1] == "\xE5\xA4\xB4\xE7\x9B\x94" && p.nameFile == 1 &&
              p.capFile == 1,
          "the names: [SWO] -> its group's own name (" + p.names[0] + "), [HEL]'s Chinese kept");
    const utb::UtbPadMap m = utb::utbPadMap(s, p);
    check(m.names[0] != s.buttons[0].name && m.names[1] == s.buttons[1].name,
          "the pad map: the passed-over name differs from the file's (the panel then draws the group's name)");
    check(p.skipped.size() == 3 && p.skipped[0] == "[SWO] caption: U+1F5E1 is not in the caption font" &&
              p.skipped[1] == "[SWO] name: U+1F5E1 is not in the label font" &&
              p.skipped[2] == "caption.owned from [text]: U+1F48E is not in the caption font",
          "three passed over, in pad order (" + (p.skipped.empty() ? std::string() : p.skipped[0]) + ")");
    const std::string warn = utb::utbSkippedWarn(p.skipped);
    check(warn == "[SWO] caption: U+1F5E1 is not in the caption font (and 2 more) - the next source is used instead "
                  "(the tag, the group's own name, else the built-in English); no font of the game has U+1F5E1, so "
                  "no font line can draw it (letters above U+FFFF are not drawn - README \"Alphabets\")",
          "the one WARN: the first, how many more, the next source, why no font line helps");
    check(utb::utbSkippedWarn({}).empty(), "nothing passed over: no WARN");
    check(utb::utbSkippedWarn({"[SWO] caption: not UTF-8"}) ==
              "[SWO] caption: not UTF-8 - the next source is used instead (the tag, the group's own name, else the "
              "built-in English)",
          "a value the key does not take: the WARN without the font words");
    utb::UtbFile t = mustParseB("[tab]\ntooltip.yes = \xF0\x9F\x91\x8D\ntooltip.no = \xD0\x9D\xD0\xB5\xD1\x82\n"
                                "label.format.1 = {group} " + dagger + " {found}\n[HEL]\ngroups = " + g[0].id + "\n",
                                "rollover words and a template with a letter above U+FFFF");
    utt::UtTextContext w;
    w.file = &t.words;
    w.glyph = utb::utbGlyphDrawable;
    const utt::UtTextPick yes = utt::utTextResolve(w, utt::kTextTooltipYes, "");
    const utt::UtTextPick no = utt::utTextResolve(w, utt::kTextTooltipNo, "");
    check(yes.text == "In your collection" && yes.skipped == "tooltip.yes from [text]: U+1F44D is not in the tooltip font" &&
              no.text == "\xD0\x9D\xD0\xB5\xD1\x82" && no.skipped.empty(),
          "tooltip.yes -> the built-in English (" + yes.text + "); tooltip.no's Cyrillic kept");
    std::vector<std::string> fm;
    std::string fskip;
    check(utt::utTextResolveFormats(w, fm, &fskip) == utt::kSrcBuiltin && fm.size() == 4 &&
              fskip == "label.format from [text]: U+1F5E1 is not in the label font",
          "a template holding one: the built-in templates (" + fskip + ")");
}

// The SVAERA buttons file as the previous build left it in a game folder: 17 buttons
// ending with [BLU], and an [unassigned] section the worker rewrote BEFORE the souls page's
// offer ran (it lists the souls group; no "added once" line). Its bytes, CRLF.
static const std::string& svaeraFileAsLeft() {
    static const std::string t =
        "; uniquetab-buttons.ini - the buttons of the Unique Collection tab in the game svaera_customquest-8350d063.\r\n"
        "; Written once by the mod; edit it freely (it is re-read within a second); delete it to\r\n"
        "; get the default back. UTF-8. A line starting with ; is a comment, and so is the rest of\r\n"
        "; a line after a blank and a ;. Put a value in \"double quotes\" to keep blanks or a ; in it.\r\n"
        ";\r\n"
        "; [TAG]    one button; the section name is its tag (1 to 3 characters; MAT, OWN and TAB\r\n"
        ";          are taken). Button order = file order.\r\n"
        ";   name    = the page name in the label line\r\n"
        ";   caption = the button's text when it is not the tag (optional)\r\n"
        ";   groups  = the item groups on it (ids from the groups file); several ids = one button,\r\n"
        ";             all of their items on one page, in group order\r\n"
        ";   size    = this button's largest caption size, 6..36 (optional)\r\n"
        ";   font    = this button's font, for example fonts/albertus mt light.fnt (optional)\r\n"
        "; [tab]    the label line's templates and words, the rollover lines and their text\r\n"
        ";          styles (tooltip.class_yes / tooltip.class_no, 0..57), the captions of the\r\n"
        ";          Transfer toggle (mat.caption), OWN and >>; it also takes font.caption,\r\n"
        ";          font.label, size.label, size.caption_max, size.caption and color.caption,\r\n"
        ";          color.label, color.ring, color.mark (RRGGBB). Label placeholders: {mode}\r\n"
        ";          {group} {owned} {entries} {first} {last} {rows} {all} {coll} {found}.\r\n"
        "; [unassigned] the groups no button holds - their items have no page. The mod rewrites\r\n"
        ";          that section (only it) whenever the set changes: move an id from it into a\r\n"
        ";          button's groups line to show them.\r\n"
        "; A mistake anywhere makes the mod use the built-in buttons; the log names the line.\r\n"
        "\r\n"
        "[tab]\r\n"
        "label.format.1     = {group}  rows {first}-{last} / {rows}  owned {owned} / {entries}  all {all} / {coll}  {found}\r\n"
        "label.format.2     = {group} {first}-{last}/{rows}  {owned}/{entries}  all {all}/{coll}  {found}\r\n"
        "label.format.3     = {group}  {owned}/{entries}  all {all}/{coll}  {found}\r\n"
        "label.format.4     = {group}  {owned}/{entries}  {found}\r\n"
        "label.mode_unknown = \"mode unknown - \"\r\n"
        "label.unknown      = ?\r\n"
        "tooltip.yes        = In your collection\r\n"
        "tooltip.no         = Not in your collection\r\n"
        "tooltip.roll       = \" (another roll)\"\r\n"
        "tooltip.class_yes  = 22\r\n"
        "tooltip.class_no   = 24\r\n"
        "mat.caption        = Transfer\r\n"
        "own.caption        = OWN\r\n"
        "next.caption       = >>\r\n"
        "\r\n"
        "[RNL]\r\n"
        "name    = Legendary Rings\r\n"
        "groups  = ring.legendary\r\n"
        "\r\n"
        "[HEL]\r\n"
        "name    = Helms\r\n"
        "caption = Helm\r\n"
        "groups  = head\r\n"
        "\r\n"
        "[TOR]\r\n"
        "name    = Torso\r\n"
        "caption = Torso\r\n"
        "groups  = chest\r\n"
        "\r\n"
        "[ARM]\r\n"
        "name    = Arms\r\n"
        "caption = Arms\r\n"
        "groups  = hands\r\n"
        "\r\n"
        "[LEG]\r\n"
        "name    = Legs\r\n"
        "caption = Legs\r\n"
        "groups  = legs\r\n"
        "\r\n"
        "[AMU]\r\n"
        "name    = Amulets\r\n"
        "caption = Amul\r\n"
        "groups  = amulet\r\n"
        "\r\n"
        "[RNG]\r\n"
        "name    = Rings\r\n"
        "caption = Ring\r\n"
        "groups  = ring\r\n"
        "\r\n"
        "[SHD]\r\n"
        "name    = Shields\r\n"
        "caption = Shld\r\n"
        "groups  = shield\r\n"
        "\r\n"
        "[AXE]\r\n"
        "name    = Axes\r\n"
        "caption = Axe\r\n"
        "groups  = axe1h\r\n"
        "\r\n"
        "[MAC]\r\n"
        "name    = Maces\r\n"
        "caption = Mace\r\n"
        "groups  = mace1h\r\n"
        "\r\n"
        "[STA]\r\n"
        "name    = Staves\r\n"
        "caption = Staff\r\n"
        "groups  = scepter\r\n"
        "\r\n"
        "[SWO]\r\n"
        "name    = Swords\r\n"
        "caption = Sword\r\n"
        "groups  = sword1h\r\n"
        "\r\n"
        "[THR]\r\n"
        "name    = Throwing\r\n"
        "caption = Throw\r\n"
        "groups  = ranged1h\r\n"
        "\r\n"
        "[SPR]\r\n"
        "name    = Spears\r\n"
        "caption = Spear\r\n"
        "groups  = spear2h\r\n"
        "\r\n"
        "[BOW]\r\n"
        "name    = Bows\r\n"
        "caption = Bow\r\n"
        "groups  = ranged2h\r\n"
        "\r\n"
        "[ART]\r\n"
        "name    = Artifacts\r\n"
        "caption = Artf\r\n"
        "groups  = artifact\r\n"
        "\r\n"
        "[BLU]\r\n"
        "name    = Arcane Formulae\r\n"
        "caption = BLU\r\n"
        "groups  = blueprint\r\n"
        "\r\n"
        "[unassigned]\r\n"
        "; rewritten by the mod: every group of this game that no button lists\r\n"
        "groups = svaera_customquest-8350d063.souls\r\n";
    return t;
}

static void testLateOffer(const std::vector<utb::UtbGroupIn>& g16) {
    printf("\n11. the late group gets its button once, also in a file the rewrite reached first\n");
    const std::string souls = "svaera_customquest-8350d063.souls";
    const std::string mk = std::string(utb::utbOfferedPrefix()) + " " + souls;
    std::vector<utb::UtbGroupIn> gs = g16;   // SVAERA's 18 groups, in its order
    gs.insert(gs.begin(), utb::UtbGroupIn{souls, "SOU", "Souls"});
    gs.insert(gs.begin(), utb::UtbGroupIn{"ring.legendary", "RNL", "Legendary Rings"});
    const std::string user = svaeraFileAsLeft();
    bool lone = false;
    for (std::size_t i = 0; i < user.size(); ++i) lone |= user[i] == '\n' && (i == 0 || user[i - 1] != '\r');
    check(user.size() == 3617 && !lone && user.find("\r\n[unassigned]\r\n; rewritten by the mod: every group of this "
                                                    "game that no button lists\r\ngroups = " + souls + "\r\n") != std::string::npos &&
              user.find(utb::utbOfferedPrefix()) == std::string::npos,
          "the file as left: 3617 bytes, CRLF, the souls group under [unassigned], no added-once line");
    utb::UtbFile f = mustParseB(user, "the SVAERA file as left");
    utb::UtbSet set;
    std::string err;
    check(utb::utbResolve(f, gs, knownIds(), set, &err) && set.buttons.size() == 17 && f.buttons.back().tag == "BLU" &&
              set.unassigned == std::vector<std::string>({souls}),
          "it resolves: 17 buttons ending with [BLU], 1 group on no button (the log's line)");

    // the offer: [SOU] right after [BLU], the added-once line, every other byte kept
    std::string out;
    std::vector<std::string> added, refused;
    check(utb::utbOfferNew(user, f, gs, utb::utbLateBuiltinIds(), &out, &added, &refused) &&
              added == std::vector<std::string>({souls}) && refused.empty(),
          "listed under [unassigned] but never offered: offered (the added-once line is the only opt-out)");
    const std::size_t at = out.find("[SOU]\r\n");
    const std::size_t end = at == std::string::npos ? at : out.find("\r\n\r\n[unassigned]\r\n", at);
    const std::string sec = end == std::string::npos ? std::string() : out.substr(at, end + 4 - at);
    check(at == user.find("[unassigned]\r\n") && sec.find("[SOU]\r\nname    = Souls\r\n") == 0 &&
              sec.size() > 24 && sec.compare(sec.size() - souls.size() - 14, std::string::npos,
                                             "groups  = " + souls + "\r\n\r\n") == 0,
          "one [SOU] section right after [BLU]'s blank line, before [unassigned]");
    std::string back = out;
    if (!sec.empty()) back.erase(at, sec.size());
    const std::size_t ma = back.find(mk + "\r\n");
    if (ma != std::string::npos) back.erase(ma, mk.size() + 2);
    check(ma != std::string::npos && back == user, "plus the added-once line; every other byte kept");
    lone = false;
    for (std::size_t i = 0; i < out.size(); ++i) lone |= out[i] == '\n' && (i == 0 || out[i - 1] != '\r');
    check(!lone, "CRLF throughout");
    utb::UtbFile fo = mustParseB(out, "the SVAERA file after the offer");
    utb::UtbSet so;
    check(utb::utbResolve(fo, gs, knownIds(), so, &err) && so.buttons.size() == 18 && fo.buttons.back().tag == "SOU" &&
              so.unassigned.empty(),
          "it resolves: 18 buttons, [SOU] the last, nothing on no button");
    std::string again, rw;
    check(!utb::utbOfferNew(out, fo, gs, utb::utbLateBuiltinIds(), &again, nullptr, nullptr) && again == out,
          "the next read: nothing");
    check(utb::utbRewriteUnassigned(out, utb::utbRewriteIds(out, so.unassigned, utb::utbLateBuiltinIds()), &rw) &&
              rw.find(mk + "\r\ngroups =\r\n") != std::string::npos &&
              !utb::utbOfferNew(rw, mustParseB(rw, "the rewritten file"), gs, utb::utbLateBuiltinIds(), &again, nullptr,
                                nullptr),
          "the rewrite then empties [unassigned], keeps the added-once line; still nothing to offer");

    // the opt-out: the id on the added-once line and no SOU button
    std::string optedOut = user;
    const std::size_t gl = optedOut.find("groups = " + souls);
    if (gl != std::string::npos) optedOut.insert(gl, mk + "\r\n");
    utb::UtbFile fx = mustParseB(optedOut, "the file with the added-once line and no [SOU]");
    utb::UtbSet sx;
    check(utb::utbResolve(fx, gs, knownIds(), sx, &err) &&
              !utb::utbOfferNew(optedOut, fx, gs, utb::utbLateBuiltinIds(), &again, nullptr, nullptr) && again == optedOut,
          "on the added-once line with no button: nothing");
    check(utb::utbRewriteIds(optedOut, sx.unassigned, utb::utbLateBuiltinIds()) == std::vector<std::string>({souls}) &&
              !utb::utbRewriteUnassigned(optedOut, utb::utbRewriteIds(optedOut, sx.unassigned, utb::utbLateBuiltinIds()), &rw),
          "and the rewrite keeps it under [unassigned]");

    // the race: the rewrite runs before the offer (the game's table not recorded yet)
    std::string before = user;   // the file before that rewrite: [unassigned] empty
    const std::size_t bl = before.find("groups = " + souls);
    if (bl != std::string::npos) before.replace(bl, 9 + souls.size(), "groups =");
    utb::UtbFile fb = mustParseB(before, "the SVAERA file before the souls page");
    utb::UtbSet sb;
    check(utb::utbResolve(fb, gs, knownIds(), sb, &err) && sb.unassigned == std::vector<std::string>({souls}) &&
              utb::utbRewriteIds(before, sb.unassigned, utb::utbLateBuiltinIds()).empty() &&
              !utb::utbRewriteUnassigned(before, utb::utbRewriteIds(before, sb.unassigned, utb::utbLateBuiltinIds()), &rw),
          "the rewrite first: it does not list the souls group (its offer has not run) - nothing to write");
    check(utb::utbOfferNew(before, fb, gs, utb::utbLateBuiltinIds(), &out, &added, &refused) &&
              out.find("[SOU]\r\n") != std::string::npos && out.find(mk + "\r\ngroups =\r\n") != std::string::npos,
          "then the offer: [SOU] and the added-once line");
    // the offer and a stale rewrite in one poll: the resolve in force is of the file before the
    // offer (souls on no button), and the added-once line lets utbRewriteIds pass it - so the poll
    // that wrote the offer runs no rewrite (panelButtonsPoll); the next one resolves the offered file
    const std::string offeredB = out;
    check(utb::utbRewriteUnassigned(offeredB, utb::utbRewriteIds(offeredB, sb.unassigned, utb::utbLateBuiltinIds()), &rw) &&
              rw.find("[SOU]\r\n") != std::string::npos && rw.find("\r\ngroups = " + souls + "\r\n") != std::string::npos,
          "the pre-offer list over the offered file would list the group beside its [SOU] - the offer's poll skips it");
    utb::UtbFile fbo = mustParseB(offeredB, "the offered file of the race");
    utb::UtbSet sbo;
    check(utb::utbResolve(fbo, gs, knownIds(), sbo, &err) && sbo.unassigned.empty() &&
              !utb::utbRewriteUnassigned(offeredB, utb::utbRewriteIds(offeredB, sbo.unassigned, utb::utbLateBuiltinIds()), &rw),
          "the next poll resolves the offered file: nothing on no button, nothing to rewrite");
    check(utb::utbRewriteUnassigned(user, utb::utbRewriteIds(user, set.unassigned, utb::utbLateBuiltinIds()), &rw) &&
              rw == before && utb::utbOfferNew(rw, mustParseB(rw, "the rewritten file"), gs, utb::utbLateBuiltinIds(), &out,
                                                nullptr, nullptr),
          "a file a rewrite already listed it in: the rewrite takes it out again, and the offer still gives it its button");

    // utbRewriteIds alone
    const std::vector<std::string> late = {"spear1h"};
    check(utb::utbRewriteIds("[unassigned]\ngroups =\n", {"head", "spear1h", "legs"}, late) ==
              std::vector<std::string>({"head", "legs"}),
          "a late group not yet offered is held out; the others keep their order");
    check(utb::utbRewriteIds("[unassigned]\n" + std::string(utb::utbOfferedPrefix()) + " spear1h\ngroups =\n",
                             {"head", "spear1h"}, late) == std::vector<std::string>({"head", "spear1h"}),
          "once on the added-once line it is listed");
    check(utb::utbRewriteIds("[A]\n" + std::string(utb::utbOfferedPrefix()) + " spear1h\ngroups = head\n", {"spear1h"}, late)
              .empty(),
          "the line counts only inside [unassigned]");
}

// The relic and charm page's button: one [REL] in the default file (the added-once line names it),
// and offered ONCE in a buttons file written before it - every other byte kept. TQ_TEST_BUTTONS_DIR
// (optional) names a folder of copies of real files: main.ini (the main game's, which Itemization
// Quest also reads: its settings follow the main world), svaera.ini, each with <name>.groups.txt (its
// game's uniq-groups.txt as written before the relic line; the line is appended here).
static void testRelicOffer(const std::vector<utb::UtbGroupIn>& gAll) {
    printf("\n12. the relic and charm page's button: in the default file, offered once in an older file\n");
    const std::string rel = "relics";
    const std::string text = utb::utbDefaultText(utb::utbViewTitle(""), gAll);
    std::size_t at = text.find("\n[REL]\nname    = Relics and Charms\ncaption = Relic\ngroups  = relics\n");
    check(at != std::string::npos && text.find("[REL]", at + 2) == std::string::npos &&
              text.find(std::string(utb::utbOfferedPrefix()) + " relics\n") != std::string::npos,
          "the default file: one [REL] (name Relics and Charms, caption Relic) and the added-once line");
    std::vector<utb::UtbGroupIn> g16(gAll.begin(), gAll.end() - 1);
    const std::string old = utb::utbCrlf(utb::utbDefaultText(utb::utbViewTitle(""), g16));
    auto offerCase = [&](const std::string& bytes, const std::vector<utb::UtbGroupIn>& gs, const std::string& what) {
        utb::UtbFile f;
        std::string err;
        if (!utb::utbParse(bytes, f, &err)) {
            check(false, what + ": the file parses (" + err + ")");
            return;
        }
        std::string out, again;
        std::vector<std::string> added, refused;
        const bool did = utb::utbOfferNew(bytes, f, gs, utb::utbLateBuiltinIds(), &out, &added, &refused);
        // every byte of the file is still there in order; what is new is the [REL] section, its blank
        // line and the added-once line
        std::size_t i = 0, j = 0, extra = 0;
        while (i < bytes.size() && j < out.size()) {
            if (bytes[i] == out[j]) { ++i; ++j; } else { ++j; ++extra; }
        }
        extra += out.size() - j;
        int rels = 0;
        for (std::size_t p = out.find("[REL]"); p != std::string::npos; p = out.find("[REL]", p + 1)) ++rels;
        const std::string sect = "[REL]\r\nname    = Relics and Charms\r\ncaption = Relic\r\ngroups  = relics\r\n";
        bool lone = false;
        for (std::size_t k = 0; k < out.size(); ++k) lone |= out[k] == '\n' && (k == 0 || out[k - 1] != '\r');
        utb::UtbFile fo;
        const bool reparsed = utb::utbParse(out, fo, &err);
        check(did && added == std::vector<std::string>({rel}) && refused.empty() && i == bytes.size() && rels == 1 &&
                  out.find(sect) != std::string::npos && !lone && reparsed &&
                  extra <= sect.size() + 2 + std::string(utb::utbOfferedPrefix()).size() + 2 + 2 + rel.size() + 2 &&
                  !utb::utbOfferNew(out, fo, gs, utb::utbLateBuiltinIds(), &again, nullptr, nullptr) && again == out,
              what + ": one [REL] added once (" + std::to_string(extra) + " bytes new), every other byte kept, "
              "nothing offered again");
    };
    offerCase(old, gAll, "the base game's default file written before the relic page");
    const char* dir = getenv("TQ_TEST_BUTTONS_DIR");
    if (!dir || !*dir) {
        printf("  (TQ_TEST_BUTTONS_DIR not set - the copies of real files are not checked)\n");
        return;
    }
    // a copy that holds the relic page's section already (offered once before): nothing again
    auto heldCase = [&](const std::string& bytes, const std::vector<utb::UtbGroupIn>& gs, const std::string& what) {
        utb::UtbFile f;
        std::string err, out;
        const bool parsed = utb::utbParse(bytes, f, &err);
        check(parsed && !utb::utbOfferNew(bytes, f, gs, utb::utbLateBuiltinIds(), &out, nullptr, nullptr) &&
                  out == bytes,
              what + ": it holds [REL] already - nothing is offered again, not a byte changes");
    };
    auto holdsRel = [&](const std::string& bytes) {
        utb::UtbFile f;
        std::string err;
        if (!utb::utbParse(bytes, f, &err)) return false;
        for (const utb::UtbButton& b : f.buttons)
            for (const std::string& id : b.groups)
                if (id == rel) return true;
        return false;
    };
    for (const char* name : {"main", "svaera"}) {
        const std::string bytes = readAll(std::string(dir) + "/" + name + ".ini");
        std::vector<utb::UtbGroupIn> gs = fixtureGroups(std::string(dir) + "/" + name + ".groups.txt");
        if (bytes.empty() || gs.empty()) {
            check(false, std::string(name) + ".ini and " + name + ".groups.txt are in TQ_TEST_BUTTONS_DIR");
            continue;
        }
        gs.push_back(utb::UtbGroupIn{rel, "REL", "Relics and Charms"});
        if (holdsRel(bytes)) heldCase(bytes, gs, std::string("a copy of the real ") + name + " file");
        else offerCase(bytes, gs, std::string("a copy of the real ") + name + " file");
    }
    const std::string iq = readAll(std::string(dir) + "/main.ini");
    std::vector<utb::UtbGroupIn> gi = fixtureGroups(std::string(dir) + "/iq.groups.txt");
    if (!iq.empty() && !gi.empty()) {
        gi.push_back(utb::UtbGroupIn{rel, "REL", "Relics and Charms"});
        if (holdsRel(iq)) heldCase(iq, gi, "the main file under Itemization Quest's groups");
        else offerCase(iq, gi, "the main file under Itemization Quest's groups");
    }
}

// collect_relics=0: the game's table has no relic group. A section naming it gives no button (the
// resolve names its tag for the one INFO line), every other section stays in force, nothing is
// offered, [unassigned] never gains it; back at 1 (the relic group in the table) the button is
// back. Without the switched-off id the same file is refused whole (the old behaviour).
static void testRelicsOff(const std::vector<utb::UtbGroupIn>& gAll) {
    printf("\n18. collect_relics=0: a section naming the relic page gives no button, the file stays in force\n");
    const std::string rel = "relics";
    const std::vector<std::string> off = {rel};
    auto offCase = [&](const std::string& bytes, const std::vector<utb::UtbGroupIn>& gs, const std::string& what,
                       const std::vector<std::string>& wantOff) {
        utb::UtbFile f;
        std::string err;
        if (!utb::utbParse(bytes, f, &err)) {
            check(false, what + ": parses (" + err + ")");
            return;
        }
        std::vector<utb::UtbGroupIn> noRel, withRel;
        for (const utb::UtbGroupIn& x : gs)
            if (x.id != rel) noRel.push_back(x);
        withRel = noRel;
        withRel.push_back(utb::UtbGroupIn{rel, "REL", "Relics and Charms"});
        std::size_t onlyRel = 0;   // the sections whose every id is the relic page's
        for (const utb::UtbButton& b : f.buttons) {
            bool only = !b.groups.empty();
            for (const std::string& id : b.groups) only = only && id == rel;
            onlyRel += only ? 1 : 0;
        }
        utb::UtbSet old, s, back;
        std::string why;
        const bool refused = !utb::utbResolve(f, noRel, knownIds(), old, &why);
        check(refused && why.find("\"relics\"") != std::string::npos && utb::utbIsBuiltin(old, noRel),
              what + ": without the switched-off id the whole file is refused (" + why + ")");
        const bool ok = utb::utbResolve(f, noRel, knownIds(), s, &err, off);
        bool relNone = true, unNone = true;
        for (const std::string& u : s.unassigned) unNone = unNone && u != rel;
        for (const utb::UtbResolved& b : s.buttons)
            for (int gi : b.groups) relNone = relNone && noRel[static_cast<std::size_t>(gi)].id != rel;
        check(ok && s.offTags == wantOff && relNone && unNone &&
                  s.buttons.size() + static_cast<std::size_t>(s.dropped) == f.buttons.size() &&
                  static_cast<std::size_t>(s.dropped) >= onlyRel,
              what + ": with collect_relics=0 it resolves (" + err + "), " + std::to_string(s.buttons.size()) +
                  " buttons, the relic section(s) name(s) for the INFO line, relics on no button and not under [unassigned]");
        std::string out;
        check(!utb::utbOfferNew(bytes, f, noRel, utb::utbLateBuiltinIds(), &out, nullptr, nullptr) && out == bytes,
              what + ": the late offer adds nothing (the game has no relic group)");
        const std::vector<std::string> ids = utb::utbRewriteIds(bytes, s.unassigned, utb::utbLateBuiltinIds());
        bool idsNone = true;
        for (const std::string& id : ids) idsNone = idsNone && id != rel;
        std::string rw;
        const bool rewrote = utb::utbRewriteUnassigned(bytes, ids, &rw);
        utb::UtbFile fr;
        const bool reparsed = utb::utbParse(rewrote ? rw : bytes, fr, &err);
        bool unFile = true;
        for (const std::string& id : fr.unassigned) unFile = unFile && id != rel;
        check(idsNone && reparsed && unFile,
              what + ": the [unassigned] rewrite never writes relics (" + (rewrote ? "rewritten" : "nothing to rewrite") + ")");
        const bool okBack = utb::utbResolve(f, withRel, knownIds(), back, &err, std::vector<std::string>());
        const int relGroup = static_cast<int>(withRel.size()) - 1;
        check(okBack && back.offTags.empty() && back.buttonOfGroup[static_cast<std::size_t>(relGroup)] >= 0 &&
                  back.buttons.size() == s.buttons.size() + onlyRel,
              what + ": back at collect_relics=1 the relic button is back (" + std::to_string(back.buttons.size()) + " buttons)");
    };
    const std::vector<utb::UtbGroupIn> g(gAll.begin(), gAll.end() - (gAll.empty() ? 0 : 1));
    offCase("[HEL]\ngroups = head\n[REL]\nname = Relics and Charms\ngroups = relics\n[ART]\ngroups = artifact\n",
            gAll, "a file with [REL] (groups = relics)", {"REL"});
    offCase("[HEL]\ngroups = head\n[ART]\ngroups = artifact, relics\n", gAll,
            "a file with relics beside artifact on [ART]", {"ART"});
    (void)g;
    const char* dir = getenv("TQ_TEST_BUTTONS_DIR");
    if (!dir || !*dir) {
        printf("  (TQ_TEST_BUTTONS_DIR not set - the copies of real files are not checked)\n");
        return;
    }
    for (const char* name : {"main", "svaera"}) {
        const std::string bytes = readAll(std::string(dir) + "/" + name + ".ini");
        std::vector<utb::UtbGroupIn> gs = fixtureGroups(std::string(dir) + "/" + name + ".groups.txt");
        if (bytes.empty() || gs.empty()) {
            check(false, std::string(name) + ".ini and " + name + ".groups.txt are in TQ_TEST_BUTTONS_DIR");
            continue;
        }
        offCase(bytes, gs, std::string("a copy of the real ") + name + " file", {"REL"});
    }
}

int main(int argc, char** argv) {
    const std::string root = argc > 1 ? argv[1] : ".";
    printf("test_buttons - the buttons file (src\\ut_buttons.h)\n");
    const std::vector<utb::UtbGroupIn> gAll = fixtureGroups(root + "/data/oracle/uniq-groups.txt");
    check(gAll.size() == 17 && gAll[16].id == "relics" && gAll[16].tag == "REL" && gAll[16].label == "Relics and Charms",
          "the fixture's 17 G lines read (the relic page's relics / REL last)");
    const std::vector<utb::UtbGroupIn> g(gAll.begin(), gAll.end() - (gAll.empty() ? 0 : 1));
    check(g.size() == 16 && g[0].id == "head" && g[0].tag == "HEL" && g[15].id == "blueprint" && g[15].tag == "BLU", "the collection's 16 G lines (blueprint / BLU last)");
    testParse();
    testRefusals();
    testResolve(g);
    testUnassigned();
    testDefault(root, gAll);
    testRelicOffer(gAll);
    testReserved();
    testResolveMore(g);
    testUnassignedMore();
    testOfferNew(root, gAll);
    testLateOffer(g);
    testRelicsOff(gAll);
    testPadMap(root, gAll);
    testFuzzB();
    testFontKeys();
    testAboveBmp(g);
    testPurity(root + "/src/ut_textcfg.h", {"<cerrno>", "<cstddef>", "<cstdint>", "<cstdio>", "<cstdlib>", "<map>",
                                            "<string>", "<utility>", "<vector>"});
    testPurity(root + "/src/ut_buttons.h", {"\"ut_textcfg.h\"", "<cstddef>", "<map>", "<string>", "<utility>", "<vector>"});
    testPurity(root + "/src/ut_sfnt.h", {"<algorithm>", "<cstddef>", "<cstdint>", "<vector>"});
    testPurity(root + "/src/ut_textmeasure.h", {"\"ut_sfnt.h\"", "\"ut_textcfg.h\"", "<algorithm>", "<cstddef>",
                                                "<cstdint>", "<cstring>", "<string>", "<vector>"});
    {   // edit.caption: read like tooltip.roll, never written
        const utb::UtbFile ef = mustParseB("[tab]\nedit.caption = ED\n[A]\ngroups = head\n", "a file with edit.caption");
        std::string tk;
        bool st = true;
        bool listed = false;
        for (const std::string& k : utb::utbTabOrder()) listed = listed || k == "edit.caption";
        const utb::UtbDefaults dd = utb::utbBuiltinDefaults();
        const auto it = ef.words.common.find("caption.edit");
        check(utb::utbTabKeyToText("edit.caption", &tk, &st) && tk == "caption.edit" && !st &&
                  utb::utbTextKeyToTab("caption.edit") == "edit.caption" && it != ef.words.common.end() &&
                  it->second.text == "ED" && !listed && dd.tab.count("edit.caption") == 0,
              "edit.caption is read (caption.edit = ED) and never among the written [tab] keys or their defaults");
        utt::UtTextContext ec, none;
        ec.file = &ef.words;
        check(utt::utTextResolve(ec, utt::kTextCaption, "edit").text == "ED" &&
                  utt::utTextResolve(none, utt::kTextCaption, "edit").text == "EDIT" &&
                  utt::utTextBuiltinById("edit") == utt::utTextBuiltinEdit() &&
                  std::string(utt::utTextBuiltinEdit()->name) == "Layout editor",
              "EDIT's caption: edit.caption when set, else EDIT (the built-in edit / EDIT / Layout editor)");
    }
    printf("\n%s - %d passed, %d failed\n", g_fail ? "FAILED" : "ALL PASS", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
