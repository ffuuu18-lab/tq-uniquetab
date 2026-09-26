// ut_search.h - the property search: the text index of every catalogue record, the page filter and
// the group marks (Grim Dawn's search_buttons, ported to a text index the mod builds itself).
//
// Titan Quest has no search box, so the mod builds the text a search needs: for each catalogue
// record it creates a throw-away item from the bare replica (seed 0, no relic; never placed in a
// sack), asks the item's own rollover builder (vftable +0x14C, GetUIDisplayText) for its lines
// with the main player as the character, keeps the lines by class, folds them (ut_searchfold.h)
// into one UTF-8 string per record, frees every engine string and the vector through the engine's
// CRT, and destroys the item again - all inside one call on the game thread, so no engine tick
// ever sees the item. The words do not depend on the character, so the cache lives for the
// process; it is built only while a main player exists.
//
// The query is typed into the search field on the pad (the ini key search_debug_query, when set,
// fills the field). The search HIGHLIGHTS: while a query stands, every record whose text holds it
// is marked on its slot, and nothing is hidden - the page lays out exactly as with no query (OWN,
// the row scroll, the counts). Each group button whose group holds a match the page can show is
// marked once that group is fully indexed. search=0 makes all of it inert: nothing is built,
// no field is drawn and no key is taken.
//
// The top half of this file is PURE (no engine, no Windows) so the offline harness
// (tools\test_search.cpp) drives exactly what the game runs.
#pragma once

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <string>

#include "ut_searchfold.h"

namespace ut {

// ---- the captured lines ---------------------------------------------------------------------
// GameTextLine (TQ x86): 0x20 bytes. +0x00 the u32 text class; +0x04 a VS2012
// basic_string<unsigned short> (16-byte buffer or pointer, size at +0x14, capacity at +0x18; the
// text is in the buffer while the capacity is below 8); +0x1C a bool. The vector is VS2012's
// {first, last, end}.
const unsigned kUtSearchLineSize = 0x20;
const unsigned kUtSearchMaxLines = 512;          // a real rollover is 15-40 lines
const unsigned kUtSearchMaxLineChars = 0x4000;   // a real line is a few dozen characters
const unsigned kUtSearchStageChars = 0x8000;     // the staged text of one record, at most

// The classes a record's text keeps. OUT: 0x11 ItemRequirements ("Required Strength" is on every
// weapon, so "strength" must find what GIVES Strength), 0x1C ItemDirections (the usage hint), and
// 0x0E ItemDescription (the lore line) unless search_lore=1. IN: everything else, including a
// class this table does not know - over-matching beats missing a property.
const unsigned kUtSearchClsLore = 0x0E;
const unsigned kUtSearchClsRequirements = 0x11;
const unsigned kUtSearchClsDirections = 0x1C;

inline bool utSearchKeepClass(unsigned cls, bool keepLore) {
    if (cls == kUtSearchClsRequirements || cls == kUtSearchClsDirections) return false;
    if (cls == kUtSearchClsLore) return keepLore;
    return true;
}

// One record's vector, copied into mod memory: the classes, the text, and the engine pointers to
// free. Filled by utSearchStage with PURE READS of engine memory (the game wraps that call, and
// only that call, in an SEH frame); everything after it works on this copy.
struct UtSearchStage {
    unsigned count;                        // lines staged
    const void* buffer;                    // the vector's own array: freed last
    unsigned heapCount;
    const void* heap[kUtSearchMaxLines];   // the strings held on the heap: freed first
    unsigned cls[kUtSearchMaxLines];
    unsigned start[kUtSearchMaxLines];     // into text
    unsigned len[kUtSearchMaxLines];
    unsigned used;
    unsigned short text[kUtSearchStageChars];
};

// Reads the vector at `vec`. Returns the number of lines staged (0 = an empty vector: nothing to
// read; `buffer` may still be set and is freed), or -1 when the shape is not a GameTextLine
// vector (not a multiple of 0x20, more than 512 lines, a string whose size or capacity makes no
// sense): then NOTHING is staged and nothing may be freed. Every line is validated before any
// text is copied, so a refusal never leaves a half-staged record behind.
inline int utSearchStage(const void* vec, UtSearchStage* st) {
    st->count = 0;
    st->buffer = nullptr;
    st->heapCount = 0;
    st->used = 0;
    const unsigned char* const* v = (const unsigned char* const*)vec;
    const unsigned char* first = v[0];
    const unsigned char* last = v[1];
    const unsigned char* end = v[2];
    if (!first && !last) return 0;
    if (!first || last < first || end < last) return -1;
    const size_t bytes = (size_t)(last - first);
    if ((bytes % kUtSearchLineSize) != 0) return -1;
    const size_t n = bytes / kUtSearchLineSize;
    if (n > kUtSearchMaxLines) return -1;
    for (size_t i = 0; i < n; ++i) {
        const unsigned char* line = first + i * kUtSearchLineSize;
        unsigned size = 0, cap = 0;
        memcpy(&size, line + 0x14, 4);
        memcpy(&cap, line + 0x18, 4);
        if (cap < 7 || size > cap || cap > kUtSearchMaxLineChars) return -1;
        if (cap >= 8) {
            const unsigned short* p = nullptr;
            memcpy(&p, line + 0x04, sizeof(p));
            if (!p) return -1;
        }
    }
    st->buffer = first;
    for (size_t i = 0; i < n; ++i) {
        const unsigned char* line = first + i * kUtSearchLineSize;
        unsigned cls = 0, size = 0, cap = 0;
        memcpy(&cls, line, 4);
        memcpy(&size, line + 0x14, 4);
        memcpy(&cap, line + 0x18, 4);
        const unsigned short* text = (const unsigned short*)(line + 0x04);
        if (cap >= 8) {
            memcpy(&text, line + 0x04, sizeof(text));
            st->heap[st->heapCount++] = text;
        }
        const unsigned room = kUtSearchStageChars - st->used;
        const unsigned take = size < room ? size : room;   // a record past 32K chars is cut
        memcpy(st->text + st->used, text, take * sizeof(unsigned short));
        st->cls[i] = cls;
        st->start[i] = st->used;
        st->len[i] = take;
        st->used += take;
    }
    st->count = (unsigned)n;
    return (int)n;
}

// Frees what utSearchStage found - every heap string, then the array - through `del` (the game:
// MSVCR110's operator delete, the allocator that made them). Returns how many were freed.
typedef void (*UtSearchDeleter)(void* p);
inline int utSearchFreeStaged(const UtSearchStage* st, UtSearchDeleter del) {
    int n = 0;
    for (unsigned i = 0; i < st->heapCount; ++i) {
        del((void*)st->heap[i]);
        ++n;
    }
    if (st->buffer) {
        del((void*)st->buffer);
        ++n;
    }
    return n;
}

struct UtSearchCounts {
    unsigned kept;
    unsigned dropReq;     // 0x11
    unsigned dropLore;    // 0x0E
    unsigned dropDir;     // 0x1C
};

// The kept lines, folded and joined by '\n' onto `out`.
inline void utSearchFoldStage(const UtSearchStage* st, bool keepLore, std::string* out,
                              UtSearchCounts* c) {
    for (unsigned i = 0; i < st->count; ++i) {
        const unsigned cls = st->cls[i];
        if (!utSearchKeepClass(cls, keepLore)) {
            if (c) {
                if (cls == kUtSearchClsRequirements) ++c->dropReq;
                else if (cls == kUtSearchClsLore) ++c->dropLore;
                else ++c->dropDir;
            }
            continue;
        }
        if (!out->empty()) out->push_back('\n');
        utSearchFoldWide(st->text + st->start[i], st->len[i], out);
        if (c) ++c->kept;
    }
}

// ---- the highlight and the marks ------------------------------------------------------------
// The matches of one group the page can show: a match that passes OWN (the label's "found N").
inline int utSearchCount(const unsigned char* owned, const unsigned char* match, int n, bool own) {
    int count = 0;
    if (!match) return 0;
    for (int k = 0; k < n; ++k) {
        if (match[k] && (!own || (owned && owned[k]))) ++count;
    }
    return count;
}

// Record k of a group of n gets the highlight on its slot: a query stands and the record's text
// holds it. It never decides what is laid out - only what is marked.
inline bool utSearchHighlight(const unsigned char* match, int n, int k, bool query) {
    return query && match && k >= 0 && k < n && match[k] != 0;
}

// The group index of the shown page's prototype i, whose record is `rec`. The page builds its
// prototypes in place order, so place i is tried first; a prototype that failed to build shifts
// the ones after it, so the places are then searched by the record pointer (a prototype's record
// IS its place's record string, not a copy). -1 = no place holds it (a prototype of another page
// or group): it is never marked.
inline int utSearchPlaceIndex(const char* const* placeRec, const int* placeK, int n, int i,
                              const char* rec) {
    if (!placeRec || !placeK || !rec || n <= 0) return -1;
    if (i >= 0 && i < n && placeRec[i] == rec) return placeK[i];
    for (int j = 0; j < n; ++j)
        if (placeRec[j] == rec) return placeK[j];
    return -1;
}

// A group button is marked when its group is FULLY indexed and holds a match the page would show
// (a match that passes OWN). So the marks under-report while the index runs and never over-report.
inline bool utSearchGroupMarked(const unsigned char* owned, const unsigned char* match, int n,
                                bool own, bool indexed) {
    if (!indexed || !match) return false;
    for (int k = 0; k < n; ++k) {
        if (match[k] && (!own || (owned && owned[k]))) return true;
    }
    return false;
}

// ---- the field: the key gate's decisions and the text edits (pure) ---------------------------
const int kUtSearchFieldMax = 32;             // characters
const unsigned kUtSearchIdleMs = 60000;       // the no-key watchdog
// The engine's ButtonEvent: +0x0C the button (DIK-style scan code), +0x10 the state (0 = press).
const int kUtKeyEsc = 0x01, kUtKeyBack = 0x0E, kUtKeyReturn = 0x1C;
struct UtSearchField {
    unsigned short text[kUtSearchFieldMax + 1];   // zero-terminated
    int len;
};
enum { kUtKeyPass = 0, kUtKeyEscape = 1, kUtKeyErase = 2, kUtKeyClear = 3, kUtKeyEnter = 4,
       kUtKeyText = 5, kUtKeySwallow = 6 };
const int kUtKeyStatePress = 0, kUtKeyStateRelease = 1;
// What the gate does with one key event. Unfocused: everything passes. Focused: a release (state
// 1) passes (the engine's held-key bookkeeping must see it, as with the game's own edit box); a
// press is the field's: Esc, Back (Ctrl+Back clears), Return, or text. Any other state (a repeat,
// were the engine ever to send one) is the field's too, so it never reaches the game as a hotkey:
// Back erases (Ctrl+Back clears) and text is typed as a press would, while Esc and Return do
// nothing (a held Esc must not blur the field it just cleared).
inline int utSearchKeyAction(bool focused, int state, int button, bool ctrl) {
    if (!focused || state == kUtKeyStateRelease) return kUtKeyPass;
    if (state != kUtKeyStatePress && (button == kUtKeyEsc || button == kUtKeyReturn))
        return kUtKeySwallow;
    if (button == kUtKeyEsc) return kUtKeyEscape;
    if (button == kUtKeyBack) return ctrl ? kUtKeyClear : kUtKeyErase;
    if (button == kUtKeyReturn) return kUtKeyEnter;
    return kUtKeyText;
}
// Appends the typed characters >= 0x20 (DEL and the control characters a Ctrl combination gives
// are dropped) up to the maximum; `s` holds at most `n` characters and may end early at a 0.
// Returns how many were appended.
inline int utSearchFieldType(UtSearchField* f, const unsigned short* s, int n) {
    int added = 0;
    for (int i = 0; s && i < n && s[i]; ++i) {
        if (s[i] < 0x20 || s[i] == 0x7F) continue;
        if (f->len >= kUtSearchFieldMax) break;
        f->text[f->len++] = s[i];
        ++added;
    }
    f->text[f->len] = 0;
    return added;
}
inline bool utSearchFieldErase(UtSearchField* f) {
    if (f->len <= 0) return false;
    f->text[--f->len] = 0;
    return true;
}
inline bool utSearchFieldClear(UtSearchField* f) {
    const bool had = f->len > 0;
    f->len = 0;
    f->text[0] = 0;
    return had;
}
// Esc: a non-empty field is cleared and keeps the focus (true); an empty one blurs (false).
inline bool utSearchFieldEscape(UtSearchField* f) { return utSearchFieldClear(f); }
// The first character drawn, so that the tail of a long query fits `fit` characters.
inline int utSearchFieldTailStart(int len, int fit) {
    if (fit < 1) fit = 1;
    return len > fit ? len - fit : 0;
}
// The watchdog: no key for 60 s drops the focus (GetTickCount wraps: unsigned difference).
inline bool utSearchFieldIdle(unsigned now, unsigned lastKey) {
    return now - lastKey >= kUtSearchIdleMs;
}

// ---- Backspace held: the field's own repeat -----------------------------------------------
// The engine sends one event at the press and one at the release, none while a key is held, so
// the field repeats Backspace itself at the keyboard's usual feel: the press erases (the gate),
// then after 400 ms one character every 40 ms until the release, an empty field or a blur.
const unsigned kUtSearchRepeatDelayMs = 400;
const unsigned kUtSearchRepeatEveryMs = 40;
// An erase is due: `heldSince` = the press's tick, `lastErase` = the last repeat's tick (the
// press's before the first), `erasedCount` = the repeats so far (the press's erase not counted).
// GetTickCount wraps: every difference is unsigned, as in the watchdog.
inline bool utSearchRepeatDue(unsigned now, unsigned heldSince, unsigned lastErase,
                              int erasedCount) {
    if (now - heldSince < kUtSearchRepeatDelayMs) return false;
    return erasedCount <= 0 || now - lastErase >= kUtSearchRepeatEveryMs;
}
struct UtBackRepeat {
    bool held;
    unsigned since, last;
    int count;
    bool seenDown;   // the key's own state was seen down during this press
};
// The Backspace press (or a repeat event from the engine: the timer starts again).
inline void utBackRepeatPress(UtBackRepeat* r, unsigned now) {
    r->held = true;
    r->since = now;
    r->last = now;
    r->count = 0;
    r->seenDown = false;
}
// The release, an empty field, a blur, or any other key.
inline void utBackRepeatRelease(UtBackRepeat* r) {
    r->held = false;
    r->count = 0;
    r->seenDown = false;
}
// Once per Update, the key's own state (GetAsyncKeyState): an "up" after a "down" of this press
// ends the repeat, should the release event never arrive. A poll that never sees the key down
// leaves the repeat to the events.
inline void utBackRepeatPoll(UtBackRepeat* r, bool down) {
    if (!r->held) return;
    if (down) r->seenDown = true;
    else if (r->seenDown) utBackRepeatRelease(r);
}
// Once per Update: true = erase one character now (the caller releases when the field is empty).
inline bool utBackRepeatStep(UtBackRepeat* r, unsigned now) {
    if (!r->held || !utSearchRepeatDue(now, r->since, r->last, r->count)) return false;
    r->last = now;
    ++r->count;
    return true;
}

// ---- the real Transfer page: its item map, read as the engine keeps it ----------------------
// InventorySack::GetInventory returns `const std::map<unsigned id, RectExt>&` (VS2012 x86): the
// map object is {head, size}; a node is left +0x00, parent +0x04, right +0x08, color +0x0C, isnil
// +0x0D, the key (the item's object id) +0x10, then the RectExt: float x +0x14, y +0x18, w +0x1C,
// h +0x20 - the item's rect in drawn px from the grid's top-left (GetItemUnderPoint, Game.dll
// 0x1B6580, tests x <= px < x + w on exactly these four floats). The head is the nil node; its
// left is the smallest key.
const int kUtSearchRealMax = 512;   // entries read at most (a sack holds 16 x 15 = 240 cells)
struct UtSackEntry {
    unsigned id;
    float x, y, w, h;
};
struct UtSackMapNode {
    const UtSackMapNode* left;
    const UtSackMapNode* parent;
    const UtSackMapNode* right;
    char color;
    char isnil;
    char pad[2];
    unsigned key;
    float x, y, w, h;
};
#if defined(_M_IX86)
static_assert(sizeof(void*) == 4 && offsetof(UtSackMapNode, key) == 0x10 &&
                  offsetof(UtSackMapNode, x) == 0x14 &&
                  offsetof(UtSackMapNode, h) == 0x20,
              "the VS2012 x86 map node: the key at +0x10, the RectExt at +0x14..+0x20");
#endif
// An in-order walk of the map at `map` into `out` (ids ascending), no allocation. PURE READS only
// (the game wraps the call in an SEH frame). -1 = it does not read as a VS2012 map: the head not
// the nil node, a null link, more entries than `cap`, a step bound exceeded, or a count that is
// not the stored size. The rects are copied as they are; the draw checks them (utSearchRealRect).
// The in-order walk itself, for any reader of the map: `visit(node)` is called once per entry,
// ids ascending, and returns false to stop the walk (-1). The refusals are utSackMapRead's.
template <class Visit>
inline int utSackMapWalk(const void* map, int cap, Visit visit) {
    if (!map || cap < 0) return -1;
    const UtSackMapNode* head = *(const UtSackMapNode* const*)map;
    unsigned size = 0;
    memcpy(&size, (const unsigned char*)map + sizeof(void*), sizeof(size));
    if (!head || head->isnil != 1 || size > (unsigned)cap) return -1;
    const unsigned maxSteps = size * 64u + 64u;
    unsigned seen = 0, steps = 0;
    const UtSackMapNode* node = head->left;   // the smallest element (the head itself when empty)
    while (node != head) {
        if (!node || ++steps > maxSteps || node->isnil || seen >= size) return -1;
        if (!visit(*node)) return -1;
        ++seen;
        if (!node->right) return -1;
        if (!node->right->isnil) {
            node = node->right;
            while (node->left && !node->left->isnil) {
                if (++steps > maxSteps) return -1;
                node = node->left;
            }
            if (!node->left) return -1;
        } else {
            const UtSackMapNode* p = node->parent;
            while (p && !p->isnil && node == p->right) {
                if (++steps > maxSteps) return -1;
                node = p;
                p = p->parent;
            }
            if (!p) return -1;
            node = p;
        }
    }
    return seen == size ? (int)seen : -1;
}
inline int utSackMapRead(const void* map, UtSackEntry* out, int cap) {
    if (!out || cap <= 0) return -1;
    UtSackEntry* at = out;   // the walk visits `size` <= cap entries at most
    return utSackMapWalk(map, cap, [&at](const UtSackMapNode& nd) {
        at->id = nd.key;
        at->x = nd.x;
        at->y = nd.y;
        at->w = nd.w;
        at->h = nd.h;
        ++at;
        return true;
    });
}
// Two walks hold the same id set (both ascending, as the walk returns them).
inline bool utSackSameIds(const UtSackEntry* a, int na, const unsigned* b, int nb) {
    if (na != nb || (na > 0 && (!a || !b))) return false;
    for (int i = 0; i < na; ++i)
        if (a[i].id != b[i]) return false;
    return true;
}
// The label's words on the real Transfer page: "reading k/n" while the items are read (and "k/n"
// for a narrow box), then "found N" ("=N"). Both empty while no query stands, and while no read
// is under way (a binding missing, no player or object manager yet): the page's name alone.
inline void utSearchRealWords(bool query, bool ready, int done, int total, int found, char* out,
                              size_t cap, char* brief, size_t briefCap) {
    if (out && cap) out[0] = 0;
    if (brief && briefCap) brief[0] = 0;
    if (!query || !ready || !out || !cap) return;
    if (done < total) {
        _snprintf_s(out, cap, _TRUNCATE, "reading %d/%d", done, total);
        if (brief && briefCap) _snprintf_s(brief, briefCap, _TRUNCATE, "%d/%d", done, total);
    } else {
        _snprintf_s(out, cap, _TRUNCATE, "found %d", found);
        if (brief && briefCap) _snprintf_s(brief, briefCap, _TRUNCATE, "=%d", found);
    }
}

// ---- the game side (ut_search.cpp; game thread unless said otherwise) ----------------------
// The view tick, once per Update, before the page's dirty flag is read: follows the ini's query
// (a change recomputes the highlight and the group marks; the page is never rebuilt for it), and
// indexes the other groups in the background, 1.5 ms per Update at most. `worldUp` = a world with a main player may exist.
void searchTick(bool worldUp);
// The page build, just before it lays out `group`: a standing query (or search_prebuild=1 at the
// first view-ON) starts the index, and a group that is not indexed yet is indexed now, whole.
void searchBeforePage(int group);
// Any thread (the pad draws them): the marked groups (bit i = group i), 0 while no query stands
// or search_buttons=0; whether a query stands; and the label's words ("indexing k/1588",
// "found N") with their short forms for a narrow box ("k/1588", "=N"), all empty while no query
// stands.
unsigned searchMarks();
bool searchQueryStands();
void searchLabel(char* out, size_t cap, char* brief = nullptr, size_t briefCap = 0);

// The field. The game window's procedure, the Update, the draw and the engine's key dispatch all
// run on the game thread; the text is still kept under its own lock, never taken inside an SEH
// frame. Wanted = search=1, not off, the key gate live and the page live (liveActive; the view ON
// or OFF alike): the field can take the focus. The pad lays the field out with the view ON, or OFF
// with search_transfer=1 (utPadFieldLaidOut), and draws it disabled while it is not wanted.
bool searchFieldWanted();
// A click on the field while it cannot take the focus: one INFO line a session with the reason.
void searchFieldOffClick();
// The field's clear button: the query cleared by the same edit as Esc on a non-empty field (the
// focus stays as it was). False = the field held no text.
bool searchFieldClearQuery();
// A left click on the field (the pad live; the view ON, or OFF with search_transfer=1): focus.
// False = refused.
bool searchFieldFocus();
// Drops the focus and logs `why`; nothing when the field has none.
void searchFieldBlur(const char* why);
bool searchFieldFocused();
// The field's text for the draw (zero-terminated); returns its length.
int searchFieldText(wchar_t* out, int cap);
// The key gate, asked first by the Display::HandleKeyEvent detour. True = the event is the
// field's and the original is not called. Unfocused it returns false at once.
bool searchKeyGate(const void* buttonEvent);
// The per-record highlight for the page draw: prototype i of the page shows `record`.
bool searchHighlight(int i, const char* record);
// The real Transfer page (the view OFF, search_transfer=1): the matched items' rects (drawn px from
// the grid's top-left) for the draw, at most `cap`; 0 while no query stands or nothing matched.
// Any thread (a copy under the list's own lock).
int searchRealMarks(UtSackEntry* out, int cap);
// ... and the label's words there ("reading k/n" / "found N" and the short forms), empty while no
// query stands. Any thread (interlocked words).
void searchRealLabel(char* out, size_t cap, char* brief, size_t briefCap);

}  // namespace ut
