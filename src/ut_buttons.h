// ut_buttons.h - the buttons file: uniquetab-buttons.ini, one per game (the main game's beside
// uniquetab.ini, a mod's in mods\<leaf>\). Its parser, the resolver that maps buttons -> groups
// -> pages against the game's group table, the default file the mod writes populated the first
// time the tab opens in a game (tools\build_catalogue.py --buttons-default writes the same
// bytes), the [unassigned] rewrite-in-place, and the one-time button of a built-in group that
// came later.
//
// Pure C++17: no Windows, no engine, no globals; only ut_textcfg.h (the UTF-8 helper, the label
// templates, the chain's kinds and limits, the [style] keys). tools\test_buttons.cpp links it.
// Grim Dawn's file, with what Titan Quest forces: the Transfer toggle's caption is mat.caption
// (built in "Transfer"), the label takes {found}, [tab] also takes tooltip.class_yes /
// tooltip.class_no (the rollover lines' game text styles, 0..57), the default file gives every
// button TQ's short name as its caption and lists tooltip.roll; the font keys name one of the
// game's fonts, and the font that follows the letters is the measure's (ut_textmeasure.h).
//
// THE FILE (UTF-8, an optional BOM, CRLF or LF):
//   ; comment        a line whose first non-blank character is ';' or '#'. A ';' after a blank
//                    or a tab also starts a comment ("groups = head   ; helms"); a ';' with no
//                    blank before it is text ("a;b"). A value in double quotes keeps everything
//                    inside them (blanks, ';'), and only a comment may follow the closing quote.
//   [tab]            the tab's words: label.format.1..4, label.mode_unknown, label.unknown,
//                    tooltip.yes, tooltip.no, tooltip.roll, tooltip.class_yes, tooltip.class_no,
//                    mat.caption, own.caption, next.caption, edit.caption (read, never written:
//                    the generated file does not list it); and the look: font.caption,
//                    font.label, size.label, size.caption_max, size.caption, color.caption,
//                    color.label, color.ring, color.mark (as the old text file's [text] / [style]
//                    keys, one to one - see utbTabKeyToText).
//   [TAG]            one button. The section name IS the tag: 1..3 printable ASCII characters
//                    (not blank [ ] ; = , " #), unique ignoring case; MAT, OWN and TAB are taken.
//                    Keys: name (the page name in the label line, 1..48 UTF-16 units), caption
//                    (the button's text when it is not the tag, 1..12 units), groups (the item
//                    groups on it: ids from the groups file, comma-separated; several ids = one
//                    button whose ONE page holds all of their items, in group order), size (this
//                    button's largest caption size 6..36), font (this button's font,
//                    fonts/<name>.fnt).
//   [unassigned]     groups = <ids>: every group of this game no button holds. Read for nothing:
//                    the mod REWRITES this section (and only it) whenever the set differs. Its
//                    comment line "; added once by the mod ...: <ids>" names the built-in groups
//                    that came later and have been given a button ONCE (utbOfferNew).
// Button order = file order. ANY error refuses the WHOLE file and names the line (the caller
// then uses the built-in buttons - one per group of the game - and logs it once per load):
// not UTF-8, a control character, a line that is neither a comment, a section nor key = value,
// a key outside a section, an unknown key, a key set twice in one section, a bad tag, a tag
// twice, a button without groups, an empty or bad value; and, against the game's groups
// (utbResolve), an id that names no group the game can have, an id on two buttons. An id a
// setting switches off in this game (the relic page's with collect_relics=0) is no error: its
// section holds nothing here, the rest of the file stays in force.
#pragma once

#include "ut_textcfg.h"

#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace utb {

using utt::UtTextFile;

// ================================================================= the parsed file
struct UtbButton {
    std::string tag;                    // the section name as written
    int line = 0;                       // the section header's line
    std::string name, caption, font;    // "" = unset
    int nameLine = 0, captionLine = 0, fontLine = 0;
    int size = -1, sizeLine = 0;        // -1 = unset
    std::vector<std::string> groups;    // lower-cased ids, as listed
    int groupsLine = 0;
};

struct UtbFile {
    std::vector<UtbButton> buttons;     // file order
    UtTextFile words;                   // [tab], as the old text file's [text] + [style]
    std::vector<std::string> unassigned;   // as listed (read for nothing)
    int unassignedLine = 0;             // the [unassigned] header's line, 0 = none
    int keys = 0;                       // key lines read
    int tooltipClass[2] = {-1, -1};     // [tab] tooltip.class_yes / _no, 0..57; -1 = unset
    int tooltipClassLine[2] = {0, 0};
};

// The two [tab] keys only Titan Quest has: the rollover lines' text styles. 0 = yes, 1 = no,
// -1 = not one of them.
inline int utbTooltipClassKey(const std::string& key) {
    if (key == "tooltip.class_yes") return 0;
    if (key == "tooltip.class_no") return 1;
    return -1;
}
const int kUtbTooltipClassMax = 57;   // the game registers text styles 0..57

// A [tab] key -> the old text file's key (and whether it was a [style] key); false = unknown.
inline bool utbTabKeyToText(const std::string& key, std::string* textKey, bool* style) {
    *style = false;
    if (key == "mat.caption") { *textKey = "caption.vanilla"; return true; }
    if (key == "own.caption") { *textKey = "caption.owned"; return true; }
    if (key == "next.caption") { *textKey = "caption.next"; return true; }
    if (key == "edit.caption") { *textKey = "caption.edit"; return true; }
    if (key == "label.mode_unknown" || key == "label.unknown" || key == "tooltip.yes" ||
        key == "tooltip.no" || key == "tooltip.roll") {
        *textKey = key;
        return true;
    }
    if (utbTooltipClassKey(key) >= 0) { *textKey = key; return true; }   // TQ: read by utbParse
    if (key.size() == 14 && key.compare(0, 13, "label.format.") == 0 && key[13] >= '1' &&
        key[13] < '1' + utt::kFormatMax) {
        *textKey = key;
        return true;
    }
    for (int i = 0; i < utt::kStyleFontCount; ++i)
        if (key == utt::utStyleFontKey(i)) { *textKey = key; *style = true; return true; }
    for (int i = 0; i < utt::kStyleSizeCount; ++i)
        if (key == utt::utStyleSizeKey(i)) { *textKey = key; *style = true; return true; }
    for (int i = 0; i < utt::kStyleColorCount; ++i)
        if (key == utt::utStyleColorKey(i)) { *textKey = key; *style = true; return true; }
    return false;
}

// The other way: an old text-file key -> the [tab] key ("" = not a [tab] key).
inline std::string utbTextKeyToTab(const std::string& textKey) {
    if (textKey == "caption.vanilla") return "mat.caption";
    if (textKey == "caption.owned") return "own.caption";
    if (textKey == "caption.next") return "next.caption";
    if (textKey == "caption.edit") return "edit.caption";
    std::string k;
    bool st = false;
    if (utbTabKeyToText(textKey, &k, &st) && k == textKey) return textKey;
    return std::string();
}

inline std::string utbUpper(std::string s) {
    for (char& c : s) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return s;
}

// A button tag: 1..3 printable ASCII, not blank [ ] ; = , " #.
inline bool utbValidTag(const std::string& t) {
    if (t.empty() || t.size() > 3) return false;
    for (unsigned char c : t)
        if (c <= 0x20 || c >= 0x7F || c == '[' || c == ']' || c == ';' || c == '=' || c == ',' ||
            c == '"' || c == '#')
            return false;
    return true;
}
inline bool utbReservedTag(const std::string& t) {
    const std::string u = utbUpper(t);
    return u == "MAT" || u == "OWN" || u == "TAB";
}

// A value with its comment cut: a ';' after a blank or a tab starts it (outside double quotes);
// a quoted value keeps what is inside. False with `why` on an unbalanced quote or text after one.
inline bool utbValue(const std::string& raw, std::string* out, std::string* why) {
    const std::size_t first = raw.find_first_not_of(" \t");
    if (first != std::string::npos && first > 0 && raw[first] == ';') {   // "key = ; a comment"
        out->clear();
        return true;
    }
    std::string v = utt::utTextTrim(raw);
    if (!v.empty() && v[0] == '"') {
        const std::size_t close = v.find('"', 1);
        if (close == std::string::npos) { *why = "an unbalanced '\"'"; return false; }
        const std::string rest = utt::utTextTrim(v.substr(close + 1));
        if (!rest.empty() && rest[0] != ';') {
            *why = "text after the closing '\"' (a comment needs a ';')";
            return false;
        }
        *out = v.substr(1, close - 1);
        return true;
    }
    for (std::size_t i = 1; i < v.size(); ++i) {
        if (v[i] == ';' && (v[i - 1] == ' ' || v[i - 1] == '\t')) {
            v = utt::utTextTrim(v.substr(0, i));
            break;
        }
    }
    *out = v;
    return true;
}

// The ids of a groups line: comma-separated, blanks around each dropped, lower-cased. An empty
// list is allowed only when `emptyOk` ([unassigned]).
inline bool utbIdList(const std::string& v, bool emptyOk, std::vector<std::string>* ids,
                      std::string* why) {
    ids->clear();
    if (utt::utTextTrim(v).empty()) {
        if (emptyOk) return true;
        *why = "\"groups\" is empty (a button needs at least one group id)";
        return false;
    }
    std::size_t b = 0;
    while (b <= v.size()) {
        std::size_t e = v.find(',', b);
        if (e == std::string::npos) e = v.size();
        const std::string id = utt::utTextLower(utt::utTextTrim(v.substr(b, e - b)));
        if (!utt::utTextValidId(id)) {
            *why = id.empty() ? std::string("an empty id between two commas")
                              : "\"" + id + "\" is not a group id ([a-z0-9_.-], up to 40)";
            return false;
        }
        for (const std::string& x : *ids)
            if (x == id) { *why = "\"" + id + "\" is listed twice"; return false; }
        ids->push_back(id);
        b = e + 1;
    }
    return true;
}

// Parses the whole file (the grammar; the ids are checked against a view by utbResolve). Any
// error refuses it: `out` is left empty and `error` reads "line N: <reason>".
inline bool utbParse(const std::string& bytes, UtbFile& out, std::string* error) {
    out = UtbFile();
    UtbFile f;
    auto fail = [&](int line, const std::string& why) {
        if (error) *error = "line " + std::to_string(line) + ": " + why;
        return false;
    };
    enum { kNone, kTab, kButton, kUnassigned } sec = kNone;
    std::map<std::string, int> styleSeen, keySeen;
    int tabLine = 0;
    std::size_t pos = 0;
    int lineNo = 0;
    if (bytes.compare(0, 3, "\xEF\xBB\xBF") == 0) pos = 3;
    while (pos <= bytes.size()) {
        std::size_t nl = bytes.find('\n', pos);
        if (nl == std::string::npos) nl = bytes.size();
        std::string raw = bytes.substr(pos, nl - pos);
        pos = nl + 1;
        ++lineNo;
        const bool last = nl == bytes.size();
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        if (!utt::utf8Valid(raw)) return fail(lineNo, "not UTF-8 (save the file as UTF-8)");
        const std::string line = utt::utTextTrim(raw);
        if (line.empty() || line[0] == ';' || line[0] == '#') {
            if (last) break;
            continue;
        }
        for (unsigned char c : line)
            if ((c < 0x20 && c != '\t') || c == 0x7F) return fail(lineNo, "a control character");
        if (line[0] == '[') {
            const std::string& head = line;
            const std::size_t close = head.find(']');
            if (close == std::string::npos) return fail(lineNo, "a section header must end with ']'");
            const std::string rest = utt::utTextTrim(head.substr(close + 1));
            if (!rest.empty() && rest[0] != ';')
                return fail(lineNo, "text after the section header (a comment needs a ';')");
            const std::string name = utt::utTextTrim(head.substr(1, close - 1));
            const std::string low = utt::utTextLower(name);
            keySeen.clear();
            if (low == "tab") {
                if (tabLine)
                    return fail(lineNo, "[tab] appears twice (first at line " + std::to_string(tabLine) + ")");
                tabLine = lineNo;
                sec = kTab;
            } else if (low == "unassigned") {
                if (f.unassignedLine)
                    return fail(lineNo, "[unassigned] appears twice (first at line " +
                                            std::to_string(f.unassignedLine) + ")");
                f.unassignedLine = lineNo;
                sec = kUnassigned;
            } else {
                if (!utbValidTag(name))
                    return fail(lineNo, "[" + name + "] is not a button tag (1 to 3 characters, "
                                        "letters, digits or signs; or [tab], [unassigned])");
                if (utbReservedTag(name))
                    return fail(lineNo, "[" + name + "] is taken (MAT, OWN and TAB are the tab's "
                                        "own; their words are mat.caption / own.caption under [tab])");
                for (const UtbButton& b : f.buttons)
                    if (utbUpper(b.tag) == utbUpper(name))
                        return fail(lineNo, "the button [" + name + "] appears twice (first at line " +
                                                std::to_string(b.line) + ")");
                UtbButton b;
                b.tag = name;
                b.line = lineNo;
                f.buttons.push_back(b);
                sec = kButton;
            }
            if (last) break;
            continue;
        }
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) return fail(lineNo, "not a comment, a [section] or key = value");
        const std::string key = utt::utTextLower(utt::utTextTrim(line.substr(0, eq)));
        std::string val, why;
        if (key.empty()) return fail(lineNo, "a value without a key");
        if (sec == kNone)
            return fail(lineNo, "\"" + key + "\" is outside a section (put it under [tab] or a [TAG])");
        if (!utbValue(line.substr(eq + 1), &val, &why)) return fail(lineNo, why);
        for (unsigned char c : val)
            if (c < 0x20) return fail(lineNo, "a control character in the value");
        ++f.keys;
        if (sec == kTab) {
            std::string tk;
            bool style = false;
            if (!utbTabKeyToText(key, &tk, &style))
                return fail(lineNo, "unknown key \"" + key + "\" in [tab]");
            const auto seen = keySeen.find(key);
            if (seen != keySeen.end())
                return fail(lineNo, "\"" + key + "\" is set twice in [tab] (first at line " +
                                        std::to_string(seen->second) + ")");
            keySeen[key] = lineNo;
            const int ck = utbTooltipClassKey(key);
            if (ck >= 0) {
                const bool num = !val.empty() && val.size() <= 2 &&
                                 val.find_first_not_of("0123456789") == std::string::npos;
                const int n = num ? std::atoi(val.c_str()) : -1;
                if (n < 0 || n > kUtbTooltipClassMax)
                    return fail(lineNo, "\"" + key + "\" must be a game text style, a whole number 0..57");
                f.tooltipClass[ck] = n;
                f.tooltipClassLine[ck] = lineNo;
                ++f.words.keys;
                if (last) break;
                continue;
            }
            if (!utt::utTextApplyKey(f.words, style, &f.words.common, styleSeen, tk, val, lineNo, &why)) {
                // name the [tab] key, not the old one
                const std::string from = "\"" + tk + "\"", to = "\"" + key + "\"";
                for (std::size_t at = 0; key != tk && (at = why.find(from, at)) != std::string::npos; at += to.size())
                    why.replace(at, from.size(), to);
                return fail(lineNo, why);
            }
            ++f.words.keys;
        } else if (sec == kUnassigned) {
            if (key != "groups")
                return fail(lineNo, "unknown key \"" + key + "\" in [unassigned] (only groups =)");
            std::vector<std::string> ids;
            if (!utbIdList(val, true, &ids, &why)) return fail(lineNo, why);
            for (const std::string& id : ids) f.unassigned.push_back(id);
        } else {
            UtbButton& b = f.buttons.back();
            const auto seen = keySeen.find(key);
            if (seen != keySeen.end())
                return fail(lineNo, "\"" + key + "\" is set twice in [" + b.tag + "] (first at line " +
                                        std::to_string(seen->second) + ")");
            keySeen[key] = lineNo;
            if (key == "name" || key == "caption") {
                const utt::UtTextKind kind = key == "name" ? utt::kTextName : utt::kTextCaption;
                if (val.empty())
                    return fail(lineNo, "\"" + key + "\" is empty (remove the line to use the default)");
                if (!utt::utTextValueOk(kind, val, &why)) return fail(lineNo, "\"" + key + "\": " + why);
                (key == "name" ? b.name : b.caption) = val;
                (key == "name" ? b.nameLine : b.captionLine) = lineNo;
            } else if (key == "groups") {
                if (!utbIdList(val, false, &b.groups, &why)) return fail(lineNo, why);
                b.groupsLine = lineNo;
            } else if (key == "size") {
                const bool num = !val.empty() && val.size() <= 3 &&
                                 val.find_first_not_of("0123456789") == std::string::npos;
                const int n = num ? std::atoi(val.c_str()) : -1;
                if (n < 6 || n > 36) return fail(lineNo, "\"size\" must be a whole number 6..36");
                b.size = n;
                b.sizeLine = lineNo;
            } else if (key == "font") {
                if (!utt::utStyleFontOk(val, &b.font))
                    return fail(lineNo, "\"font\" must be fonts/<name>.fnt ([a-z0-9 _./-], no \"..\")");
                b.fontLine = lineNo;
            } else {
                return fail(lineNo, "unknown key \"" + key + "\" in [" + b.tag +
                                        "] (name, caption, groups, size, font)");
            }
        }
        if (last) break;
    }
    for (const UtbButton& b : f.buttons)
        if (!b.groupsLine)
            return fail(b.line, "the button [" + b.tag + "] has no groups = line");
    out = f;
    return true;
}

// ================================================================= against the view's groups
// One group of the view's table, in group order: its id, its G line's tag and its label (colour
// codes removed).
struct UtbGroupIn {
    std::string id, tag, label;
};

struct UtbResolved {
    std::string tag, name, caption, font;   // as the file has them ("" = unset)
    int size = -1;
    std::vector<int> groups;                // indexes into the view's table, ascending
};
struct UtbSet {
    std::vector<UtbResolved> buttons;       // file order; a button none of whose ids is in the
                                            // view is left out (the main file serves two views)
    std::vector<int> buttonOfGroup;         // per group: the button index, -1 = unassigned
    std::vector<std::string> unassigned;    // the ids no button holds, in group order
    int dropped = 0;                        // buttons left out (no group of theirs in this view)
    std::vector<std::string> offTags;       // the sections that name a switched-off id, file order
};

// The built-in buttons: one per group, captioned by its tag, named by its label - what no file
// (or the generated default) comes to.
inline UtbSet utbBuiltinSet(const std::vector<UtbGroupIn>& groups) {
    UtbSet s;
    for (std::size_t g = 0; g < groups.size(); ++g) {
        UtbResolved b;
        b.tag = groups[g].tag;
        b.groups.push_back(static_cast<int>(g));
        s.buttons.push_back(b);
        s.buttonOfGroup.push_back(static_cast<int>(g));
    }
    return s;
}

// The file against the view's groups. `alsoKnown` = ids a game served by the same file may have
// (the main file serves the base game and the base game with mods; the built-in group ids): such
// an id is no error and simply holds nothing here. `switchedOff` = ids a setting switches off in
// this game (the relic page's while the game has no relic group): no error either, they hold
// nothing, and the section's tag goes to `offTags` (the caller's one INFO line). False with
// `error` ("line N: ...") on an id no group can have or an id on two buttons; `out` is then the
// built-in set.
inline bool utbResolve(const UtbFile& f, const std::vector<UtbGroupIn>& groups,
                       const std::vector<std::string>& alsoKnown, UtbSet& out, std::string* error,
                       const std::vector<std::string>& switchedOff = std::vector<std::string>()) {
    out = UtbSet();
    UtbSet s;
    s.buttonOfGroup.assign(groups.size(), -1);
    std::map<std::string, int> index;
    for (std::size_t g = 0; g < groups.size(); ++g) index[groups[g].id] = static_cast<int>(g);
    std::map<std::string, std::pair<std::string, int>> owner;   // id -> (tag, line)
    for (const UtbButton& b : f.buttons) {
        UtbResolved r;
        r.tag = b.tag;
        r.name = b.name;
        r.caption = b.caption;
        r.font = b.font;
        r.size = b.size;
        for (const std::string& id : b.groups) {
            const auto o = owner.find(id);
            if (o != owner.end()) {
                if (error)
                    *error = "line " + std::to_string(b.groupsLine) + ": the group \"" + id +
                             "\" is on [" + o->second.first + "] already (line " +
                             std::to_string(o->second.second) + ") - a group can be on one button only";
                out = utbBuiltinSet(groups);
                return false;
            }
            owner[id] = std::make_pair(b.tag, b.groupsLine);
            const auto it = index.find(id);
            if (it != index.end()) {
                r.groups.push_back(it->second);
                continue;
            }
            bool off = false;
            for (const std::string& k : switchedOff) off |= k == id;
            if (off) {
                if (s.offTags.empty() || s.offTags.back() != b.tag) s.offTags.push_back(b.tag);
                continue;
            }
            bool known = false;
            for (const std::string& k : alsoKnown) known |= k == id;
            if (!known) {
                if (error)
                    *error = "line " + std::to_string(b.groupsLine) + ": [" + b.tag + "] names \"" + id +
                             "\", which is no group of this game (the ids are listed under [unassigned] "
                             "and in the groups file)";
                out = utbBuiltinSet(groups);
                return false;
            }
        }
        if (r.groups.empty()) {
            ++s.dropped;
            continue;
        }
        for (std::size_t i = 1; i < r.groups.size(); ++i)   // group order
            for (std::size_t j = i; j > 0 && r.groups[j - 1] > r.groups[j]; --j)
                std::swap(r.groups[j - 1], r.groups[j]);
        for (int g : r.groups) s.buttonOfGroup[static_cast<std::size_t>(g)] = static_cast<int>(s.buttons.size());
        s.buttons.push_back(r);
    }
    for (std::size_t g = 0; g < groups.size(); ++g)
        if (s.buttonOfGroup[g] < 0) s.unassigned.push_back(groups[g].id);
    out = s;
    return true;
}

// Is the set the built-in one (one button per group, in group order, tag = the G line's tag, no
// caption/size/font, name unset or the label)? The identity gate's question.
inline bool utbIsBuiltin(const UtbSet& s, const std::vector<UtbGroupIn>& groups) {
    if (s.buttons.size() != groups.size()) return false;
    for (std::size_t i = 0; i < groups.size(); ++i) {
        const UtbResolved& b = s.buttons[i];
        if (b.groups.size() != 1 || b.groups[0] != static_cast<int>(i)) return false;
        if (b.tag != groups[i].tag || !b.caption.empty() || !b.font.empty() || b.size >= 0) return false;
        if (!b.name.empty() && b.name != groups[i].label) return false;
    }
    return true;
}

// ================================================================= the pad, resolved
// The pad's captions in LOGICAL order (the Transfer toggle, the buttons, OWN, NEXT), the names per button, and
// per logical button the size cap (-1 = none) and the font path ("" = the role's); each caption
// and name through the glyph check of the font that draws it. A caption falls back to the tag
// (ASCII, so always drawable), a name to its first group's label, then the id's word.
struct UtbPad {
    std::vector<std::string> caps;
    std::vector<int> sizes;
    std::vector<std::string> fonts;
    std::vector<std::string> names;              // per button
    std::vector<std::string> skipped;            // every candidate passed over (one WARN)
    int capFile = 0, nameFile = 0;               // how many came from the file
};
inline UtbPad utbResolvePad(const utt::UtTextContext& c, const UtbSet& s,
                            const std::vector<UtbGroupIn>& groups) {
    UtbPad p;
    auto fixed = [&](const char* id, const char* dflt) {
        const utt::UtTextPick k = utt::utTextResolve(c, utt::kTextCaption, id, dflt);
        p.caps.push_back(k.text);
        p.sizes.push_back(-1);
        p.fonts.push_back(std::string());
        if (!k.skipped.empty()) p.skipped.push_back(k.skipped);
        if (k.source <= utt::kSrcCommon) ++p.capFile;
    };
    auto covered = [&](utt::UtTextFont font, const std::string& text, utt::UtTextKind kind,
                       const std::string& what) {
        std::string why;
        char32_t missing = 0;
        if (!utt::utTextValueOk(kind, text, &why)) {
            p.skipped.push_back(what + ": " + why);
            return false;
        }
        if (!utt::utTextCovered(c, font, text, &missing)) {
            p.skipped.push_back(what + ": " + utt::utTextCodePointName(missing) + " is not in the " +
                                (font == utt::kFontCaption ? "caption" : "label") + " font");
            return false;
        }
        return true;
    };
    fixed("vanilla", "Transfer");
    for (const UtbResolved& b : s.buttons) {
        // A button's own font draws its caption: the glyph callback cannot ask it, so a caption
        // with a font line is taken as it is (the live side checks it against that font).
        if (!b.caption.empty() && (!b.font.empty() ||
                                   covered(utt::kFontCaption, b.caption, utt::kTextCaption,
                                           "[" + b.tag + "] caption"))) {
            p.caps.push_back(b.caption);
            ++p.capFile;
        } else {
            p.caps.push_back(b.tag);
        }
        p.sizes.push_back(b.size);
        p.fonts.push_back(b.font);
        const UtbGroupIn* g0 = b.groups.empty() ? nullptr : &groups[static_cast<std::size_t>(b.groups[0])];
        std::string name;
        if (!b.name.empty() && covered(utt::kFontLabel, b.name, utt::kTextName, "[" + b.tag + "] name")) {
            name = b.name;
            ++p.nameFile;
        } else if (g0 && !g0->label.empty() &&
                   covered(utt::kFontLabel, g0->label, utt::kTextName, "the label of " + g0->id)) {
            name = g0->label;
        } else {
            name = g0 ? utt::utTextDerivedName(g0->id) : b.tag;
        }
        p.names.push_back(name);
    }
    fixed("owned", "OWN");
    fixed("next", ">>");
    fixed("edit", "EDIT");
    return p;
}

// The glyph check of Titan Quest's text (utt::UtTextContext::glyph): the engine draws any letter up
// to U+FFFF in every role's font (a letter the font lacks is left to the game's own fallback), and
// none above it - the engine takes one UTF-16 unit per letter. So a caption holding a letter above
// U+FFFF falls back to the tag, a name to its group's own name, a word to the built-in English.
inline bool utbGlyphDrawable(utt::UtTextFont, char32_t cp, const void*) {
    return cp <= 0xFFFF;
}

// The one WARN over the candidates the resolve passed over ("" for none): the first, how many more,
// what is used instead and, for a letter no font of the game has, that no font line can draw it.
inline std::string utbSkippedWarn(const std::vector<std::string>& skipped) {
    if (skipped.empty()) return std::string();
    std::string warn = skipped[0];
    if (skipped.size() > 1) warn += " (and " + std::to_string(skipped.size() - 1) + " more)";
    warn += " - the next source is used instead (the tag, the group's own name, else the built-in English)";
    for (const std::string& s : skipped) {
        const std::size_t t = s.find(" is not in the ");
        const std::size_t u = t == std::string::npos ? t : s.rfind("U+", t);
        if (u == std::string::npos) continue;
        const char32_t cp = static_cast<char32_t>(std::strtoul(s.c_str() + u + 2, nullptr, 16));
        if (cp <= 0xFFFF) continue;
        warn += "; no font of the game has " + utt::utTextCodePointName(cp) +
                ", so no font line can draw it (letters above U+FFFF are not drawn - README \"Alphabets\")";
        break;
    }
    return warn;
}

// ================================================================= the default file
inline std::string utbPad(const std::string& k, std::size_t w) {
    return k.size() >= w ? k : k + std::string(w - k.size(), ' ');
}

// A value as the file writes it: in double quotes when blanks around it or a ';' after a blank
// would otherwise be lost, or when it starts with a quote.
inline std::string utbQuote(const std::string& v) {
    bool q = v.empty() || v[0] == ' ' || v[0] == '\t' || v.back() == ' ' || v.back() == '\t' || v[0] == '"';
    for (std::size_t i = 1; i < v.size() && !q; ++i)
        q = v[i] == ';' && (v[i - 1] == ' ' || v[i - 1] == '\t');
    if (!q && !v.empty() && (v[0] == ';' || v[0] == '#')) q = true;
    return q ? "\"" + v + "\"" : v;
}

// The words the default file starts from: the built-in ones, or an old text file's (the fold).
struct UtbDefaults {
    // [tab] key -> value, in utbTabOrder; a missing key is not written (the built-in applies)
    std::map<std::string, std::string> tab;
    std::map<std::string, std::string> caption;   // group id -> caption override
    std::map<std::string, std::string> name;      // group id -> name override
};

// The [tab] keys in the order the file lists them (TQ lists tooltip.roll and the two text styles
// too: the words and colours the ini's tooltip keys had). edit.caption is not among them: a [tab]
// key the README documents and the parser takes, never written into the generated file (so the
// generated bytes stay what they were before the key existed).
inline const std::vector<std::string>& utbTabOrder() {
    static const std::vector<std::string> k = {
        "label.format.1", "label.format.2", "label.format.3", "label.format.4",
        "label.mode_unknown", "label.unknown", "tooltip.yes", "tooltip.no", "tooltip.roll",
        "tooltip.class_yes", "tooltip.class_no", "mat.caption", "own.caption", "next.caption",
        "font.caption", "font.label", "size.label", "size.caption_max", "size.caption",
        "color.caption", "color.label", "color.ring", "color.mark"};
    return k;
}

inline UtbDefaults utbBuiltinDefaults() {
    UtbDefaults d;
    for (int i = 0; i < utt::kTextDefaultFormats; ++i)
        d.tab["label.format." + std::to_string(i + 1)] = utt::utTextDefaultFormat(i);
    d.tab["label.mode_unknown"] = utt::utTextBuiltinPlain(utt::kTextModeUnknown);
    d.tab["label.unknown"] = utt::utTextBuiltinPlain(utt::kTextUnknown);
    d.tab["tooltip.yes"] = utt::utTextBuiltinPlain(utt::kTextTooltipYes);
    d.tab["tooltip.no"] = utt::utTextBuiltinPlain(utt::kTextTooltipNo);
    d.tab["tooltip.roll"] = utt::utTextBuiltinPlain(utt::kTextTooltipRoll);
    d.tab["tooltip.class_yes"] = "22";
    d.tab["tooltip.class_no"] = "24";
    d.tab["mat.caption"] = "Transfer";
    d.tab["own.caption"] = "OWN";
    d.tab["next.caption"] = ">>";
    d.caption["relics"] = "Relic";   // the relic and charm page's button
    return d;
}

// The game as the header names it: "the main game", or "the game <leaf>" for a mod's own set.
inline std::string utbViewTitle(const std::string& customKey) {
    return customKey.empty() ? std::string("the main game") : "the game " + customKey;
}

// A G line's label as the tab shows it: the game's colour codes ({^X} and ^X) removed, and the
// bytes taken as UTF-8 when they are, else as latin-1 (each byte one code point).
inline std::string utbGroupLabel(const std::string& raw) {
    std::string s;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '{' && i + 3 < raw.size() && raw[i + 1] == '^' && raw[i + 2] && raw[i + 3] == '}') {
            i += 3;
            continue;
        }
        if (raw[i] == '^' && i + 1 < raw.size()) {
            ++i;
            continue;
        }
        s += raw[i];
    }
    if (utt::utf8Valid(s)) return s;
    std::string u;
    for (unsigned char c : s) utt::utf8Append(c, u);
    return u;
}

// A G line label as a button name: at most 48 UTF-16 units (a longer one is cut at a code point).
inline std::string utbNameOf(const std::string& label) {
    std::string n = label;
    while (!n.empty() && utt::utf16Units(n) > 48) n = utt::utf8Prefix(n, utt::utf8Length(n) - 1);
    return n;
}

// ================================================================= the built-ins that come later
// The BUILT-IN groups a later build adds to a game that had a buttons file before them: a file
// written before one of them existed in that game has no button for it - utbOfferNew appends one,
// once - and the default file names them on its "added once" line, so a button the user deletes
// from it is never offered back. A group a game does not have is never offered there (the base
// game's file is unchanged). SVAERA's souls page (src\gen\ut_groups_default.h) came after its
// first buttons file; the relic and charm page (ut_groups.h kRelicGroupId) came after every
// game's.
inline const std::vector<std::string>& utbLateBuiltinIds() {
    static const std::vector<std::string> v = {"svaera_customquest-8350d063.souls", "relics"};
    return v;
}

// The [unassigned] comment line that remembers them. Everything after its ':' is the id list.
inline const char* utbOfferedPrefix() {
    return "; added once by the mod - delete its button and it stays deleted:";
}

// A group's caption in the default file: the defaults' own, else the pad's short name (TQ 1.0.1's,
// utTextBuiltins), else none (the tag).
inline const std::string* utbDefaultCaption(const UtbDefaults& d, const std::string& id, std::string* tmp) {
    const auto cp = d.caption.find(id);
    if (cp != d.caption.end()) return &cp->second;
    const utt::UtTextBuiltin* b = utt::utTextBuiltinById(id);
    if (!b || id == "vanilla" || id == "owned" || id == "other" || id == "next" || id == "edit") return nullptr;
    *tmp = b->caption;
    return tmp;
}

// The default file, LF (the writer writes CRLF): the header, [tab], one [TAG] per group of the
// game in group order (name = its label, caption = its short name, groups = its id), an empty
// [unassigned]. A tag the file
// grammar cannot take (reserved, or not 1..3 printable) is never produced by the groups file, but
// such a group would go to [unassigned] rather than make a file the parser refuses.
inline std::string utbDefaultText(const std::string& viewTitle, const std::vector<UtbGroupIn>& groups,
                                  const UtbDefaults& d = utbBuiltinDefaults()) {
    std::string t =
        "; uniquetab-buttons.ini - the buttons of the Unique Collection tab in " + viewTitle + ".\n"
        "; Written once by the mod; edit it freely (it is re-read within a second); delete it to\n"
        "; get the default back. UTF-8. A line starting with ; is a comment, and so is the rest of\n"
        "; a line after a blank and a ;. Put a value in \"double quotes\" to keep blanks or a ; in it.\n"
        ";\n"
        "; [TAG]    one button; the section name is its tag (1 to 3 characters; MAT, OWN and TAB\n"
        ";          are taken). Button order = file order.\n"
        ";   name    = the page name in the label line\n"
        ";   caption = the button's text when it is not the tag (optional)\n"
        ";   groups  = the item groups on it (ids from the groups file); several ids = one button,\n"
        ";             all of their items on one page, in group order\n"
        ";   size    = this button's largest caption size, 6..36 (optional)\n"
        ";   font    = this button's font, for example fonts/albertus mt light.fnt (optional)\n"
        "; [tab]    the label line's templates and words, the rollover lines and their text\n"
        ";          styles (tooltip.class_yes / tooltip.class_no, 0..57), the captions of the\n"
        ";          Transfer toggle (mat.caption), OWN and >>; it also takes font.caption,\n"
        ";          font.label, size.label, size.caption_max, size.caption and color.caption,\n"
        ";          color.label, color.ring, color.mark (RRGGBB). Label placeholders: {mode}\n"
        ";          {group} {owned} {entries} {first} {last} {rows} {all} {coll} {found}.\n"
        "; [unassigned] the groups no button holds - their items have no page. The mod rewrites\n"
        ";          that section (only it) whenever the set changes: move an id from it into a\n"
        ";          button's groups line to show them.\n"
        "; A mistake anywhere makes the mod use the built-in buttons; the log names the line.\n"
        "\n"
        "[tab]\n";
    for (const std::string& k : utbTabOrder()) {
        const auto it = d.tab.find(k);
        if (it != d.tab.end()) t += utbPad(k, 18) + " = " + utbQuote(it->second) + "\n";
    }
    std::vector<std::string> unassigned;
    std::vector<std::string> seen;
    for (const UtbGroupIn& g : groups) {
        bool dup = false;
        for (const std::string& s : seen) dup |= utbUpper(s) == utbUpper(g.tag);
        if (!utbValidTag(g.tag) || utbReservedTag(g.tag) || dup) {
            unassigned.push_back(g.id);
            continue;
        }
        seen.push_back(g.tag);
        t += "\n[" + g.tag + "]\n";
        const auto nm = d.name.find(g.id);
        t += "name    = " + utbQuote(nm != d.name.end() ? nm->second : utbNameOf(g.label)) + "\n";
        std::string tmp;
        if (const std::string* cp = utbDefaultCaption(d, g.id, &tmp)) t += "caption = " + utbQuote(*cp) + "\n";
        t += "groups  = " + g.id + "\n";
    }
    t += "\n[unassigned]\n"
         "; rewritten by the mod: every group of this game that no button lists\n";
    std::string late;
    for (const std::string& id : utbLateBuiltinIds())
        for (const UtbGroupIn& g : groups)
            if (g.id == id) late += (late.empty() ? " " : ", ") + id;
    if (!late.empty()) t += std::string(utbOfferedPrefix()) + late + "\n";
    t += "groups =";
    for (std::size_t i = 0; i < unassigned.size(); ++i) t += (i ? ", " : " ") + unassigned[i];
    t += "\n";
    return t;
}

// The buttons no usable file comes to (none, or a refused one): exactly the default file's -
// utbDefaultText parsed and resolved - so the pad draws the same captions and names with no file
// as with the file the mod writes. The built-in set only if that text could not be used.
inline UtbSet utbDefaultSet(const std::vector<UtbGroupIn>& groups) {
    UtbFile f;
    UtbSet s;
    if (!utbParse(utbDefaultText("", groups), f, nullptr) ||
        !utbResolve(f, groups, std::vector<std::string>(), s, nullptr))
        return utbBuiltinSet(groups);
    return s;
}

inline std::string utbCrlf(const std::string& lf) {
    std::string out;
    out.reserve(lf.size() + lf.size() / 20);
    for (char c : lf) {
        if (c == '\n') out += '\r';
        out += c;
    }
    return out;
}

// ================================================================= the [unassigned] rewrite
// The file with its [unassigned] section saying exactly `ids`; every other byte kept. Inside the
// section its comments and blank lines stay where they are; its key lines become ONE groups line,
// where the first of them was (else after the section's last comment, else right after the
// header). No section: one is appended, with the file's own line ending (CRLF when the file has
// one). Returns false (and `out` = bytes) when nothing would change.
inline bool utbRewriteUnassigned(const std::string& bytes, const std::vector<std::string>& ids,
                                 std::string* out) {
    const std::string eol = bytes.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    std::string want = "groups =";
    for (std::size_t i = 0; i < ids.size(); ++i) want += (i ? ", " : " ") + ids[i];
    struct Line { std::size_t b, next; bool ends; std::string trim; };
    std::vector<Line> lines;
    std::size_t pos = 0;
    while (pos < bytes.size()) {
        const std::size_t nl = bytes.find('\n', pos);
        const std::size_t next = nl == std::string::npos ? bytes.size() : nl + 1;
        std::size_t e = nl == std::string::npos ? bytes.size() : nl;
        if (e > pos && bytes[e - 1] == '\r') --e;
        lines.push_back({pos, next, nl != std::string::npos, utt::utTextTrim(bytes.substr(pos, e - pos))});
        pos = next;
    }
    auto isHeader = [&](std::size_t i) { return !lines[i].trim.empty() && lines[i].trim[0] == '['; };
    auto isComment = [&](std::size_t i) {
        return !lines[i].trim.empty() && (lines[i].trim[0] == ';' || lines[i].trim[0] == '#');
    };
    std::size_t head = lines.size();
    for (std::size_t i = 0; i < lines.size() && head == lines.size(); ++i) {
        if (!isHeader(i)) continue;
        const std::size_t close = lines[i].trim.find(']');
        if (close != std::string::npos &&
            utt::utTextLower(utt::utTextTrim(lines[i].trim.substr(1, close - 1))) == "unassigned")
            head = i;
    }
    std::string r;
    if (head == lines.size()) {
        r = bytes;
        if (!r.empty() && r.back() != '\n') r += eol;
        r += eol + "[unassigned]" + eol +
             "; rewritten by the mod: every group of this game that no button lists" + eol + want + eol;
    } else {
        std::size_t end = head + 1;
        while (end < lines.size() && !isHeader(end)) ++end;
        std::size_t firstKey = 0, lastComment = head;
        for (std::size_t i = head + 1; i < end; ++i) {
            if (isComment(i)) { if (!firstKey) lastComment = i; }
            else if (!lines[i].trim.empty() && !firstKey) firstKey = i;
        }
        auto copyLine = [&](std::size_t i) {
            r += bytes.substr(lines[i].b, lines[i].next - lines[i].b);
            if (!lines[i].ends) r += eol;   // the file's unterminated last line, before ours
        };
        r = bytes.substr(0, lines[head].b);
        copyLine(head);
        if (!firstKey && lastComment == head) r += want + eol;
        for (std::size_t i = head + 1; i < end; ++i) {
            const bool key = !lines[i].trim.empty() && !isComment(i);
            if (key) {
                if (i == firstKey) r += want + eol;
                continue;
            }
            if (!lines[i].ends && i + 1 == lines.size() && (firstKey || lastComment != i)) {
                r += bytes.substr(lines[i].b);   // the last line keeps its missing ending
                continue;
            }
            copyLine(i);
            if (!firstKey && i == lastComment) r += want + eol;
        }
        if (end < lines.size()) r += bytes.substr(lines[end].b);
    }
    *out = r;
    return r != bytes;
}

// ================================================================= a new built-in group's button, ONCE
struct UtbLine {
    std::size_t b, next;   // the line's first byte; the next line's first byte
    bool ends;             // it has a line ending
    std::string trim;
};
inline std::vector<UtbLine> utbLines(const std::string& bytes) {
    std::vector<UtbLine> lines;
    std::size_t pos = 0;
    while (pos < bytes.size()) {
        const std::size_t nl = bytes.find('\n', pos);
        const std::size_t next = nl == std::string::npos ? bytes.size() : nl + 1;
        std::size_t e = nl == std::string::npos ? bytes.size() : nl;
        if (e > pos && bytes[e - 1] == '\r') --e;
        lines.push_back({pos, next, nl != std::string::npos, utt::utTextTrim(bytes.substr(pos, e - pos))});
        pos = next;
    }
    return lines;
}
// A section header's name, lower-cased ("" when the line is no header).
inline std::string utbHeaderName(const std::string& trim) {
    if (trim.empty() || trim[0] != '[') return "";
    const std::size_t close = trim.find(']');
    if (close == std::string::npos) return "";
    return utt::utTextLower(utt::utTextTrim(trim.substr(1, close - 1)));
}
inline bool utbIsOfferedLine(const std::string& trim) {
    const std::string p = utbOfferedPrefix();
    const std::size_t colon = p.find(':');
    return trim.compare(0, colon, p, 0, colon) == 0;
}

// The ids on the [unassigned] section's "added once" line, lower-cased, in file order.
inline std::vector<std::string> utbOfferedIds(const std::string& bytes) {
    std::vector<std::string> ids;
    const std::vector<UtbLine> lines = utbLines(bytes);
    bool in = false;
    for (const UtbLine& l : lines) {
        const std::string h = utbHeaderName(l.trim);
        if (!h.empty()) {
            in = h == "unassigned";
            continue;
        }
        if (!in || !utbIsOfferedLine(l.trim)) continue;
        std::string rest = l.trim.substr(l.trim.find(':') + 1);
        std::size_t p = 0;
        while (p <= rest.size()) {
            std::size_t c = rest.find(',', p);
            if (c == std::string::npos) c = rest.size();
            const std::string id = utt::utTextLower(utt::utTextTrim(rest.substr(p, c - p)));
            if (!id.empty()) ids.push_back(id);
            p = c + 1;
        }
    }
    return ids;
}

// A BUILT-IN group that came after 1.1.0 (`late`, utbLateBuiltinIds) gets a button ONCE. For each
// such group of the view that no button lists and that is not on the "added once" line (the ONLY
// memory of the opt-out: a button deleted after the offer leaves its id there), a [TAG] section
// (the default file's three lines: name = the label, caption when the defaults have one, groups =
// the id) is inserted after the LAST button
// section, and the id is added to the "added once" line of [unassigned] (a new line after the
// section's leading comments, or a new [unassigned] section at the end). Every other byte is kept;
// the file's own line ending is used. A group whose tag a button already has (ignoring case), or a
// tag the grammar cannot take, gets no section - only the "added once" line - and lands in
// [unassigned] with the next rewrite. Returns false (`out` = bytes) when nothing is new.
// `added` / `refused` name the ids, for the one INFO line.
// Unlike GD, a late group listed under [unassigned] but not on the "added once" line is offered:
// it was never offered (an [unassigned] rewrite listed it first - utbRewriteIds now holds such a
// group out of the rewrite until its offer ran, GD's order, where the offer comes first).
inline bool utbOfferNew(const std::string& bytes, const UtbFile& f, const std::vector<UtbGroupIn>& groups,
                        const std::vector<std::string>& late, std::string* out,
                        std::vector<std::string>* added, std::vector<std::string>* refused,
                        const UtbDefaults& d = utbBuiltinDefaults()) {
    *out = bytes;
    auto has = [](const std::vector<std::string>& v, const std::string& x) {
        for (const std::string& y : v)
            if (y == x) return true;
        return false;
    };
    const std::vector<std::string> offered = utbOfferedIds(bytes);
    std::vector<std::string> onButton, tags;
    for (const UtbButton& b : f.buttons) {
        tags.push_back(utbUpper(b.tag));
        for (const std::string& id : b.groups) onButton.push_back(utt::utTextLower(id));
    }
    std::string sections;   // LF; converted below
    std::vector<std::string> fresh;
    for (const std::string& id : late) {
        const UtbGroupIn* g = nullptr;
        for (const UtbGroupIn& x : groups)
            if (x.id == id) g = &x;
        if (!g || has(onButton, id) || has(offered, id)) continue;
        fresh.push_back(id);
        if (!utbValidTag(g->tag) || utbReservedTag(g->tag) || has(tags, utbUpper(g->tag))) {
            if (refused) refused->push_back(id);
            continue;
        }
        tags.push_back(utbUpper(g->tag));
        if (added) added->push_back(id);
        if (!sections.empty()) sections += "\n";
        sections += "[" + g->tag + "]\n";
        const auto nm = d.name.find(g->id);
        sections += "name    = " + utbQuote(nm != d.name.end() ? nm->second : utbNameOf(g->label)) + "\n";
        std::string tmp;
        if (const std::string* cp = utbDefaultCaption(d, g->id, &tmp)) sections += "caption = " + utbQuote(*cp) + "\n";
        sections += "groups  = " + g->id + "\n";
    }
    if (fresh.empty()) return false;
    const bool crlf = bytes.find("\r\n") != std::string::npos;
    const std::string eol = crlf ? "\r\n" : "\n";
    auto native = [&](const std::string& lf) { return crlf ? utbCrlf(lf) : lf; };
    std::string ids;
    for (const std::string& id : offered) ids += (ids.empty() ? " " : ", ") + id;
    for (const std::string& id : fresh) ids += (ids.empty() ? " " : ", ") + id;
    const std::string marker = std::string(utbOfferedPrefix()) + ids;

    const std::vector<UtbLine> lines = utbLines(bytes);
    const std::size_t n = lines.size();
    std::size_t head = n, lastEnd = n, markerAt = n, markerInsert = n;
    bool anyButton = false;
    for (std::size_t i = 0; i < n; ++i) {
        const std::string h = utbHeaderName(lines[i].trim);
        if (h.empty()) continue;
        std::size_t end = i + 1;
        while (end < n && utbHeaderName(lines[end].trim).empty()) ++end;
        if (h == "unassigned") {
            if (head == n) {
                head = i;
                std::size_t p = i + 1;
                while (p < end && !lines[p].trim.empty() && (lines[p].trim[0] == ';' || lines[p].trim[0] == '#')) {
                    if (utbIsOfferedLine(lines[p].trim)) markerAt = p;
                    ++p;
                }
                for (std::size_t q = p; q < end && markerAt == n; ++q)
                    if (utbIsOfferedLine(lines[q].trim)) markerAt = q;
                markerInsert = p;
            }
        } else if (h != "tab") {
            anyButton = true;
            lastEnd = end;
        }
    }
    std::size_t ins = anyButton ? lastEnd : head;   // after the last button; none: before [unassigned]
    std::vector<std::string> before(n + 1);
    // the sections: a blank line before them unless there is one already, and one after them when
    // a section follows
    if (!sections.empty()) {
        std::string blk;
        if (ins == n && n && !lines[n - 1].ends) blk += eol;
        if (ins > 0 && !lines[ins - 1].trim.empty()) blk += eol;
        blk += native(sections);
        if (ins < n) blk += eol;
        before[ins] += blk;
    }
    std::string tail;
    if (head == n) {
        if (sections.empty() && n && !lines[n - 1].ends) tail += eol;
        tail += eol + "[unassigned]" + eol + "; rewritten by the mod: every group of this game that no button lists" +
                eol + marker + eol;
    } else if (markerAt == n) {
        std::string m;
        if (markerInsert == n && n && !lines[n - 1].ends) m += eol;
        m += marker + eol;
        before[markerInsert] = m + before[markerInsert];
    }
    std::string r;
    for (std::size_t i = 0; i < n; ++i) {
        r += before[i];
        if (i == markerAt) {
            r += marker;
            const std::size_t e = lines[i].next;
            std::size_t le = e;
            if (lines[i].ends) {
                le = e - 1;
                if (le > lines[i].b && bytes[le - 1] == '\r') --le;
                r += bytes.substr(le, e - le);
            }
            continue;
        }
        r += bytes.substr(lines[i].b, lines[i].next - lines[i].b);
    }
    r += before[n];
    r += tail;
    *out = r;
    return r != bytes;
}

// The ids the [unassigned] rewrite writes into `bytes`: the resolve's `unassigned` (in group
// order) without a late built-in group (`late`, utbLateBuiltinIds) that is not on the file's
// "added once" line - its offer has not run yet (it needs the group table of the file's own game,
// which may be recorded later than the file is read), and utbOfferNew gives it a button first.
// Once offered - a button, or only the line when its tag is taken - it is listed like any other.
inline std::vector<std::string> utbRewriteIds(const std::string& bytes, const std::vector<std::string>& unassigned,
                                              const std::vector<std::string>& late) {
    const std::vector<std::string> offered = utbOfferedIds(bytes);
    std::vector<std::string> ids;
    for (const std::string& id : unassigned) {
        const std::string low = utt::utTextLower(id);
        bool waits = false;
        for (const std::string& l : late) waits = waits || utt::utTextLower(l) == low;
        for (const std::string& o : offered) waits = waits && o != low;
        if (!waits) ids.push_back(id);
    }
    return ids;
}

// ================================================================= the pad keyed by the buttons
// What Titan Quest's pad draws for a resolved set: per button, in file order, its caption and size
// cap (the UtbPad's, logical index b + 1), its name and its groups (ascending; the first one keys
// the button's page - a click, NEXT and the wheel select it); per group its button (-1 = on no
// button: listed under [unassigned], not on the pad).
struct UtbPadMap {
    std::vector<std::string> caps, names;
    std::vector<int> sizes;
    std::vector<std::vector<int>> groups;   // per button
    std::vector<int> buttonOfGroup;         // per group
    int buttons() const { return static_cast<int>(groups.size()); }
    int first(int b) const {
        return b >= 0 && b < buttons() && !groups[static_cast<std::size_t>(b)].empty()
                   ? groups[static_cast<std::size_t>(b)][0] : -1;
    }
};
inline UtbPadMap utbPadMap(const UtbSet& s, const UtbPad& p) {
    UtbPadMap m;
    m.buttonOfGroup = s.buttonOfGroup;
    for (std::size_t b = 0; b < s.buttons.size(); ++b) {
        m.caps.push_back(b + 1 < p.caps.size() ? p.caps[b + 1] : s.buttons[b].tag);
        m.sizes.push_back(b + 1 < p.sizes.size() ? p.sizes[b + 1] : -1);
        m.names.push_back(b < p.names.size() ? p.names[b] : s.buttons[b].tag);
        m.groups.push_back(s.buttons[b].groups);
    }
    return m;
}

} // namespace utb
