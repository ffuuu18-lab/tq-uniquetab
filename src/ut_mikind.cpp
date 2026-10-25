// ut_mikind.cpp - the KIND table (see ut_mikind.h): catalogue.bin in, a sorted vector out, and the
// refusal's words over it.

#include "ut_mikind.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

#include "model/catalogue.h"   // the KIND bit (catIsMi), the name and the item level
#include "ut_depositgate.h"    // UtRollKey, utRollDescribe, utDepositWhyRoll
#include "ut_searchfold.h"     // utSearchStripEscapes (the search's colour-escape rule)
#include "ut_store.h"          // storeRollOf

namespace ut {

struct UtMiFact {
    std::string record;   // folded: lower case, '/'
    std::string name;     // the catalogue's display name
    int level;            // the catalogue's item level
};

struct UtMiTable {
    std::vector<UtMiFact> rows;   // sorted by record
};

namespace {

const UtMiFact* miFind(const UtMiTable* t, const char* record) {
    if (!t || !record || !*record) return nullptr;
    char key[300];
    utMiFold(record, key, sizeof(key));
    const auto it = std::lower_bound(
        t->rows.begin(), t->rows.end(), key,
        [](const UtMiFact& f, const char* k) { return strcmp(f.record.c_str(), k) < 0; });
    return (it != t->rows.end() && strcmp(it->record.c_str(), key) == 0) ? &*it : nullptr;
}

}  // namespace

void utMiFold(const char* in, char* out, size_t cap) {
    if (!out || !cap) return;
    size_t n = 0;
    for (const char* p = in; p && *p && n + 1 < cap; ++p) {
        char c = *p;
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        out[n++] = c;
    }
    out[n] = 0;
}

size_t utMiBytes(const UtMiTable* t) {
    if (!t) return 0;
    size_t b = sizeof(UtMiTable) + t->rows.capacity() * sizeof(UtMiFact);
    for (const UtMiFact& f : t->rows) {
        b += f.record.capacity() > 15 ? f.record.capacity() + 1 : 0;
        b += f.name.capacity() > 15 ? f.name.capacity() + 1 : 0;
    }
    return b;
}

void utMiFree(UtMiTable* t) { delete t; }

UtMiTable* utMiLoad(const char* path, std::string* err) {
    if (!path || !*path) {
        if (err) *err = "no path";
        return nullptr;
    }
    gdut::Catalogue cat;
    std::string why;
    if (!cat.loadFromFile(path, &why)) {
        if (err) *err = why;
        return nullptr;
    }
    UtMiTable* t = new UtMiTable();
    for (size_t i = 0; i < cat.itemCount(); ++i) {
        const gdut::ItemView& e = cat.item(i);
        if (!gdut::catIsMi(e)) continue;
        const std::string rec(e.record);
        char key[300];
        utMiFold(rec.c_str(), key, sizeof(key));
        UtMiFact f;
        f.record = key;
        // the name as the log writes it: TQ's colour escape dropped ("{^l}Alala" -> "Alala")
        const std::string raw(e.name.data(), e.name.size());
        char shown[256];
        utSearchStripEscapes(raw.c_str(), shown, sizeof(shown));
        f.name = shown;
        f.level = (int)e.itemLevel;
        t->rows.push_back(f);
    }
    std::sort(t->rows.begin(), t->rows.end(),
              [](const UtMiFact& a, const UtMiFact& b) { return a.record < b.record; });
    return t;
}

int utMiCount(const UtMiTable* t) { return t ? (int)t->rows.size() : -1; }

int utMiKind(const UtMiTable* t, const char* record) {
    if (!t) return -1;
    return miFind(t, record) ? 1 : 0;
}

bool utMiFacts(const UtMiTable* t, const char* record, char* name, size_t nameCap, int* level) {
    if (name && nameCap) name[0] = 0;
    if (level) *level = 0;
    const UtMiFact* f = miFind(t, record);
    if (!f) return false;
    if (name && nameCap) _snprintf_s(name, nameCap, _TRUNCATE, "%s", f->name.c_str());
    if (level) *level = f->level;
    return true;
}

const char* utMiRollWhy(const UtMiTable* t, const char* record, char* buf, size_t cap) {
    if (!buf || !cap) return "";
    buf[0] = 0;
    const UtMiFact* f = miFind(t, record);
    if (!f) return buf;
    UtRollKey k;
    if (!storeRollOf(record, &k)) utRollClear(&k);
    char roll[512];
    utRollDescribe(k, f->name.c_str(), f->level, roll, sizeof(roll));
    utDepositWhyRoll(roll, buf, cap);
    return buf;
}

}  // namespace ut
