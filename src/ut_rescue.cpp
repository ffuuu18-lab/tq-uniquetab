// ut_rescue.cpp - the journal (TQ-1): the mod's own record of every stored copy, and its CSV export.
//
// TQ PORT of GD's ut_rescue.cpp. See ut_rescue.h for what stayed GD's and what TQ forced. The mod
// writes only its own files here (tq-uniq-items*.jsonl, tq-uniq-items*.csv, the .bad- copy aside),
// always whole and always atomically; the game's own save files are only ever READ (the character
// save's last-write time, journalNewestCharacterSave).
//
// ===== tq-uniq-items.jsonl, format TQ-1 ===========================================================
//
//   line 1   {"journal":"titan quest uniquetab","format":1,"written":"<ISO-8601 UTC>",
//             "set":"<leaf>","entries":N,"collected":C,"pendingIn":I,"pendingOut":O}
//   line n   one stored COPY, keys in this canonical order:
//              "record"      the folded baseName - THE KEY (lower case, '/'). Must equal the fold
//                            of "base", or the line is dropped (refuse when unsure)
//              "deposited"   ISO-8601 UTC, when the row was written
//              "stack"       u32, the item's stack count (1 for a unique)
//              "base"        the replica's baseName BYTE-EXACT (what Item::CreateItem is given)
//              "prefix" "suffix" "relic" "relicBonus" "relic2" "relicBonus2"
//                            the other six replica strings, byte-exact, EMPTY ONES INCLUDED
//              "seed" "var1" "var2"   u32, replica +0x7C / +0x80 / +0xB4
//              "b8"          the replica's +0xB8 byte (0..255)
//              "pending"     OPTIONAL: "in" = deposited, the character file on disk still holds the
//                            item (no save since); "out" = taken, the character file on disk does
//                            not hold it yet. Absent = settled
//              "taken"       ISO-8601 UTC, with "pending":"out" only
//
// THE ROW COUNT GOVERNS. Two lines may be byte-identical except for "deposited" (a 15-bit seed):
// they are two copies, never merged. "Collected" = rows > 0 without the "out" ones.
//
// THE READER'S CONTRACT is GD's: split lines first, parse second; a bad line costs THAT ROW ONLY
// (WARN, and the file is copied aside as .bad-<stamp> before the next write replaces it); the
// object must be FLAT; unknown keys are skipped; a HIGHER format, a header naming another
// journal, or a file that is there and cannot be read = READ-ONLY for the session. "I cannot read
// it" never becomes "there is nothing in it".
#include "ut_rescue.h"

#include <stdio.h>
#include <string.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "ut_log.h"
#include "ut_ownedfold.h"
#include "ut_paths.h"
#include "ut_depositgate.h"   // utJournalStack

namespace ut {

const char* const kUtIdStrKey[kUtIdStrCount] = {"base",       "prefix", "suffix",     "relic",
                                                "relicBonus", "relic2", "relicBonus2"};

namespace {

struct Row {
    std::string key;
    std::string s[kUtIdStrCount];
    unsigned int seed = 0, var1 = 0, var2 = 0, b8 = 0, stack = 1;
    unsigned long long at = 0;        // deposited
    unsigned long long takenAt = 0;   // "pending":"out" only
    int pending = 0;                  // 0 settled, 1 "in", 2 "out"
    unsigned long long seq = 0;       // this session's identity of the row (file order at load)
    bool unresolved = false;          // pending at a deferred load, waiting for the container
                                      // check (session only; the save watch leaves it alone)
    bool wasOut = false;              // an unresolved row that was "pending out" and
                                      // was RESTORED at the open (session only; see settleAtLoad)
};

const int kPendNone = 0, kPendIn = 1, kPendOut = 2;

CRITICAL_SECTION g_cs;
bool g_csReady = false;
std::vector<Row>* g_rows = nullptr;
// FNV-1a 64 of the key -> rows in the collection. Rebuilt after every change, read without
// allocation (journalRows is called per page build and per owned refresh).
std::unordered_map<unsigned long long, unsigned int>* g_index = nullptr;
unsigned int g_collected = 0;
unsigned long long g_nextSeq = 0;
char g_dir[MAX_PATH] = {0};
char g_leaf[96] = {0};
char g_path[MAX_PATH] = {0};
char g_csvPath[MAX_PATH] = {0};
volatile LONG g_setOpen = 0;
HANDLE g_event = nullptr;
volatile LONG g_dirty = 0;
volatile LONG g_writes = 0;
volatile LONG g_csvMode = 0;       // GD's latch; TQ default 0 (the ini's export_csv)
volatile LONG g_csvArm = 0;
volatile LONG g_csvWrites = 0;
volatile LONG g_csvFailLogged = 0;
volatile LONG g_readOnly = 0;
volatile LONG g_copyAside = 0;
volatile LONG g_readOnlySaid = 0;

unsigned long long keyHash(const char* k) {
    unsigned long long h = 14695981039346656037ULL;
    for (const unsigned char* p = (const unsigned char*)k; p && *p; ++p) {
        h ^= *p;
        h *= 1099511628211ULL;
    }
    return h;
}

unsigned long long nowFileTime() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

// Caller holds g_cs.
void rebuildIndex() {
    g_index->clear();
    g_collected = 0;
    for (size_t i = 0; i < g_rows->size(); ++i) {
        const Row& r = (*g_rows)[i];
        if (r.pending == kPendOut) continue;
        ++(*g_index)[keyHash(r.key.c_str())];
        ++g_collected;
    }
}

// ---- the atomic write (GD, verbatim) --------------------------------------------------------
// THE WHOLE file is built in memory, written to "<path>.tmp" in the SAME FOLDER, flushed with
// FlushFileBuffers, and only then renamed over the real file with MoveFileEx(REPLACE_EXISTING |
// WRITE_THROUGH): at every instant the real path names either the whole old file or the whole new
// one. One retry after Sleep(30) covers a transient sharing violation.
bool writeWholeFileAtomic(const char* path, const char* data, size_t len) {
    if (!path || !path[0]) return false;
    char tmp[MAX_PATH];
    _snprintf_s(tmp, sizeof(tmp), _TRUNCATE, "%s.tmp", path);
    HANDLE h = CreateFileA(tmp, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = true;
    size_t at = 0;
    while (ok && at < len) {
        const DWORD chunk = (DWORD)((len - at) > 0x400000u ? 0x400000u : (len - at));
        DWORD wrote = 0;
        if (!WriteFile(h, data + at, chunk, &wrote, nullptr) || wrote != chunk) ok = false;
        at += wrote;
    }
    if (!FlushFileBuffers(h)) ok = false;
    CloseHandle(h);
    if (!ok) {
        DeleteFileA(tmp);
        return false;
    }
    const DWORD kMove = MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH;
    if (!MoveFileExA(tmp, path, kMove)) {
        Sleep(30);
        if (!MoveFileExA(tmp, path, kMove)) {
            DeleteFileA(tmp);
            return false;
        }
    }
    return true;
}

void stampNow(char* out, size_t cap) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    _snprintf_s(out, cap, _TRUNCATE, "%04u%02u%02u-%02u%02u%02u", st.wYear, st.wMonth, st.wDay,
                st.wHour, st.wMinute, st.wSecond);
}

void isoFromFileTime(unsigned long long ft, char* out, size_t cap) {
    out[0] = 0;
    if (!ft) return;
    FILETIME f;
    f.dwLowDateTime = (DWORD)(ft & 0xFFFFFFFFull);
    f.dwHighDateTime = (DWORD)(ft >> 32);
    SYSTEMTIME st;
    if (!FileTimeToSystemTime(&f, &st)) return;
    const unsigned long long tick = ft % 10000000ull;
    _snprintf_s(out, cap, _TRUNCATE, "%04u-%02u-%02uT%02u:%02u:%02u.%07lluZ", st.wYear, st.wMonth,
                st.wDay, st.wHour, st.wMinute, st.wSecond, tick);
}

void jsonEscapeTo(std::string* out, const char* s) {
    out->push_back('"');
    for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
        const unsigned char c = *p;
        switch (c) {
            case '"': out->append("\\\""); break;
            case '\\': out->append("\\\\"); break;
            case '\b': out->append("\\b"); break;
            case '\f': out->append("\\f"); break;
            case '\n': out->append("\\n"); break;
            case '\r': out->append("\\r"); break;
            case '\t': out->append("\\t"); break;
            default:
                if (c < 0x20) {
                    char u[8];
                    _snprintf_s(u, sizeof(u), _TRUNCATE, "\\u%04X", (unsigned int)c);
                    out->append(u);
                } else {
                    out->push_back((char)c);
                }
                break;
        }
    }
    out->push_back('"');
}

void appendKeyStr(std::string* out, const char* key, const char* val, bool first = false) {
    if (!first) out->push_back(',');
    jsonEscapeTo(out, key);
    out->push_back(':');
    jsonEscapeTo(out, val);
}

void appendKeyU32(std::string* out, const char* key, unsigned int v) {
    char num[16];
    _snprintf_s(num, sizeof(num), _TRUNCATE, "%u", v);
    out->push_back(',');
    jsonEscapeTo(out, key);
    out->push_back(':');
    out->append(num);
}

void appendRowLine(std::string* out, const Row& r) {
    char iso[64];
    out->push_back('{');
    appendKeyStr(out, "record", r.key.c_str(), true);
    isoFromFileTime(r.at, iso, sizeof(iso));
    appendKeyStr(out, "deposited", iso);
    appendKeyU32(out, "stack", r.stack);
    for (int i = 0; i < kUtIdStrCount; ++i) appendKeyStr(out, kUtIdStrKey[i], r.s[i].c_str());
    appendKeyU32(out, "seed", r.seed);
    appendKeyU32(out, "var1", r.var1);
    appendKeyU32(out, "var2", r.var2);
    appendKeyU32(out, "b8", r.b8);
    if (r.pending == kPendIn) appendKeyStr(out, "pending", "in");
    if (r.pending == kPendOut) {
        appendKeyStr(out, "pending", "out");
        isoFromFileTime(r.takenAt, iso, sizeof(iso));
        appendKeyStr(out, "taken", iso);
    }
    out->append("}\n");
}

void countPending(size_t* in, size_t* outN) {
    size_t a = 0, b = 0;
    for (size_t i = 0; i < g_rows->size(); ++i) {
        if ((*g_rows)[i].pending == kPendIn) ++a;
        if ((*g_rows)[i].pending == kPendOut) ++b;
    }
    if (in) *in = a;
    if (outN) *outN = b;
}

void buildText(std::string* out) {
    out->clear();
    out->reserve(g_rows->size() * 360 + 256);
    size_t pin = 0, pout = 0;
    countPending(&pin, &pout);
    char iso[64];
    isoFromFileTime(nowFileTime(), iso, sizeof(iso));
    char hdr[512];
    _snprintf_s(hdr, sizeof(hdr), _TRUNCATE,
                "{\"journal\":\"%s\",\"format\":%u,\"written\":\"%s\",\"set\":\"%s\","
                "\"entries\":%u,\"collected\":%u,\"pendingIn\":%u,\"pendingOut\":%u}\n",
                UT_JOURNAL_NAME, (unsigned int)UT_JOURNAL_FORMAT, iso, g_leaf,
                (unsigned int)g_rows->size(), g_collected, (unsigned int)pin, (unsigned int)pout);
    out->append(hdr);
    for (size_t i = 0; i < g_rows->size(); ++i) appendRowLine(out, (*g_rows)[i]);
}

bool writeFile() {
    if (!g_rows || !g_path[0]) return false;
    if (InterlockedCompareExchange(&g_copyAside, 0, 1) == 1) {
        char stamp[32], aside[MAX_PATH];
        stampNow(stamp, sizeof(stamp));
        _snprintf_s(aside, sizeof(aside), _TRUNCATE, "%s.bad-%s", g_path, stamp);
        if (CopyFileA(g_path, aside, TRUE)) {
            logW("journal: the file that was read did not parse cleanly, so a copy of it was kept "
                 "as %s before this write replaced it", aside);
        }
    }
    std::string text;
    buildText(&text);
    if (!writeWholeFileAtomic(g_path, text.c_str(), text.size())) return false;
    InterlockedIncrement(&g_writes);
    return true;
}

bool readWholeFile(const char* path, std::vector<unsigned char>* out, bool* present) {
    *present = false;
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD e = GetLastError();
        *present = !(e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND);
        return false;
    }
    *present = true;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0 || size.QuadPart > (64 << 20)) {
        CloseHandle(h);
        return false;
    }
    out->resize((size_t)size.QuadPart);
    DWORD got = 0;
    const BOOL r = ReadFile(h, &(*out)[0], (DWORD)out->size(), &got, nullptr);
    CloseHandle(h);
    return r && got == out->size();
}

// ---- the TEXT reader (GD, verbatim) -----------------------------------------------------------
struct JsonKV {
    std::string key;
    std::string val;
    bool isString;
};

int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

void skipWs(const char*& p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t')) ++p;
}

void utf8Put(std::string* out, unsigned int cp) {
    if (cp < 0x80) {
        out->push_back((char)cp);
    } else if (cp < 0x800) {
        out->push_back((char)(0xC0u | (cp >> 6)));
        out->push_back((char)(0x80u | (cp & 0x3Fu)));
    } else {
        out->push_back((char)(0xE0u | (cp >> 12)));
        out->push_back((char)(0x80u | ((cp >> 6) & 0x3Fu)));
        out->push_back((char)(0x80u | (cp & 0x3Fu)));
    }
}

bool jsonString(const char*& p, const char* end, std::string* out) {
    if (p >= end || *p != '"') return false;
    ++p;
    out->clear();
    while (p < end) {
        const char c = *p++;
        if (c == '"') return true;
        if (c != '\\') {
            out->push_back(c);
            continue;
        }
        if (p >= end) return false;
        const char e = *p++;
        switch (e) {
            case '"': out->push_back('"'); break;
            case '\\': out->push_back('\\'); break;
            case '/': out->push_back('/'); break;
            case 'b': out->push_back('\b'); break;
            case 'f': out->push_back('\f'); break;
            case 'n': out->push_back('\n'); break;
            case 'r': out->push_back('\r'); break;
            case 't': out->push_back('\t'); break;
            case 'u': {
                if (end - p < 4) return false;
                unsigned int cp = 0;
                for (int i = 0; i < 4; ++i) {
                    const int h = hexVal(p[i]);
                    if (h < 0) return false;
                    cp = cp * 16u + (unsigned int)h;
                }
                p += 4;
                utf8Put(out, cp);
                break;
            }
            default: return false;
        }
    }
    return false;
}

bool parseFlatObject(const char* p, const char* end, std::vector<JsonKV>* out, const char** why) {
    out->clear();
    *why = "?";
    skipWs(p, end);
    if (p >= end || *p != '{') {
        *why = "the line does not start with '{'";
        return false;
    }
    ++p;
    skipWs(p, end);
    if (p < end && *p == '}') {
        ++p;
        skipWs(p, end);
        if (p != end) {
            *why = "trailing text after '}'";
            return false;
        }
        return true;
    }
    for (;;) {
        skipWs(p, end);
        JsonKV kv;
        kv.isString = false;
        if (!jsonString(p, end, &kv.key)) {
            *why = "a key is not a quoted JSON string";
            return false;
        }
        skipWs(p, end);
        if (p >= end || *p != ':') {
            *why = "no ':' after a key";
            return false;
        }
        ++p;
        skipWs(p, end);
        if (p >= end) {
            *why = "the line ends where a value was expected";
            return false;
        }
        if (*p == '"') {
            if (!jsonString(p, end, &kv.val)) {
                *why = "an unterminated or badly escaped string value";
                return false;
            }
            kv.isString = true;
        } else if (*p == '{' || *p == '[') {
            *why = "a nested object or array - this reader is deliberately FLAT";
            return false;
        } else {
            const char* vs = p;
            while (p < end && *p != ',' && *p != '}') ++p;
            const char* ve = p;
            while (ve > vs && (ve[-1] == ' ' || ve[-1] == '\t')) --ve;
            if (ve == vs) {
                *why = "an empty value";
                return false;
            }
            kv.val.assign(vs, (size_t)(ve - vs));
        }
        out->push_back(kv);
        skipWs(p, end);
        if (p < end && *p == ',') {
            ++p;
            continue;
        }
        if (p < end && *p == '}') {
            ++p;
            skipWs(p, end);
            if (p != end) {
                *why = "trailing text after '}'";
                return false;
            }
            return true;
        }
        *why = "expected ',' or '}'";
        return false;
    }
}

bool jsonU32(const std::string& v, unsigned int* out) {
    if (v.empty() || v.size() > 10) return false;
    unsigned long long x = 0;
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] < '0' || v[i] > '9') return false;
        x = x * 10ull + (unsigned long long)(v[i] - '0');
        if (x > 0xFFFFFFFFull) return false;
    }
    *out = (unsigned int)x;
    return true;
}

unsigned long long fileTimeFromIso(const std::string& s) {
    unsigned int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    if (sscanf_s(s.c_str(), "%u-%u-%uT%u:%u:%u", &y, &mo, &d, &h, &mi, &se) != 6) return 0;
    SYSTEMTIME st;
    memset(&st, 0, sizeof(st));
    st.wYear = (WORD)y;
    st.wMonth = (WORD)mo;
    st.wDay = (WORD)d;
    st.wHour = (WORD)h;
    st.wMinute = (WORD)mi;
    st.wSecond = (WORD)se;
    FILETIME ft;
    if (!SystemTimeToFileTime(&st, &ft)) return 0;
    unsigned long long v = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    const size_t dot = s.find('.');
    if (dot != std::string::npos) {
        unsigned long long frac = 0;
        int digits = 0;
        for (size_t i = dot + 1; i < s.size() && digits < 7; ++i) {
            if (s[i] < '0' || s[i] > '9') break;
            frac = frac * 10ull + (unsigned long long)(s[i] - '0');
            ++digits;
        }
        while (digits < 7) {
            frac *= 10ull;
            ++digits;
        }
        v += frac;
    }
    return v;
}

bool printable(const std::string& s) {
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c >= 0x7F) return false;
    }
    return true;
}

// One row out of one parsed line. Every identity key is REQUIRED (an empty string is written as
// "" on purpose): a row missing one would hand CreateItem a different item.
bool rowFromKVs(const std::vector<JsonKV>& kvs, Row* r, char* why, size_t whyCap) {
    bool haveRecord = false, haveStr[kUtIdStrCount] = {}, haveSeed = false, haveV1 = false,
         haveV2 = false, haveB8 = false, haveStack = false;
    std::string record, pending, taken;
    for (size_t i = 0; i < kvs.size(); ++i) {
        const JsonKV& kv = kvs[i];
        bool known = false;
        for (int k = 0; k < kUtIdStrCount; ++k) {
            if (kv.key == kUtIdStrKey[k]) {
                known = true;
                if (!kv.isString) {
                    _snprintf_s(why, whyCap, _TRUNCATE, "\"%s\" is not a string", kUtIdStrKey[k]);
                    return false;
                }
                r->s[k] = kv.val;
                haveStr[k] = true;
            }
        }
        if (known) continue;
        unsigned int* u = nullptr;
        bool* have = nullptr;
        if (kv.key == "seed") u = &r->seed, have = &haveSeed;
        else if (kv.key == "var1") u = &r->var1, have = &haveV1;
        else if (kv.key == "var2") u = &r->var2, have = &haveV2;
        else if (kv.key == "b8") u = &r->b8, have = &haveB8;
        else if (kv.key == "stack") u = &r->stack, have = &haveStack;
        if (u) {
            if (kv.isString || !jsonU32(kv.val, u)) {
                _snprintf_s(why, whyCap, _TRUNCATE, "\"%s\" is not a plain number", kv.key.c_str());
                return false;
            }
            *have = true;
            continue;
        }
        if (kv.key == "record" && kv.isString) record = kv.val, haveRecord = true;
        else if (kv.key == "deposited" && kv.isString) r->at = fileTimeFromIso(kv.val);
        else if (kv.key == "pending" && kv.isString) pending = kv.val;
        else if (kv.key == "taken" && kv.isString) taken = kv.val;
        // anything else: skipped (forward compatibility; the format number guards meaning)
    }
    if (!haveRecord) {
        _snprintf_s(why, whyCap, _TRUNCATE, "no \"record\"");
        return false;
    }
    for (int k = 0; k < kUtIdStrCount; ++k) {
        if (!haveStr[k]) {
            _snprintf_s(why, whyCap, _TRUNCATE, "no \"%s\"", kUtIdStrKey[k]);
            return false;
        }
        if (r->s[k].size() >= kUtIdStrMax || !printable(r->s[k])) {
            _snprintf_s(why, whyCap, _TRUNCATE, "\"%s\" is too long or not printable ASCII",
                        kUtIdStrKey[k]);
            return false;
        }
    }
    if (!haveSeed || !haveV1 || !haveV2 || !haveB8 || !haveStack) {
        _snprintf_s(why, whyCap, _TRUNCATE, "a number (seed/var1/var2/b8/stack) is missing");
        return false;
    }
    if (r->b8 > 255u) {
        _snprintf_s(why, whyCap, _TRUNCATE, "b8 %u out of range", r->b8);
        return false;
    }
    r->stack = utJournalStack(r->stack);   // a row is one copy; "stack":0 reads as 1
    char key[256];
    if (!utJournalKey(r->s[kUtIdBase].c_str(), key, sizeof(key)) || record != key) {
        _snprintf_s(why, whyCap, _TRUNCATE, "\"record\" is not the folded \"base\"");
        return false;
    }
    r->key = key;
    if (pending.empty()) {
        r->pending = kPendNone;
    } else if (pending == "in") {
        r->pending = kPendIn;
    } else if (pending == "out") {
        r->pending = kPendOut;
        r->takenAt = fileTimeFromIso(taken);
    } else {
        _snprintf_s(why, whyCap, _TRUNCATE, "\"pending\" is neither \"in\" nor \"out\"");
        return false;
    }
    return true;
}

struct TextParse {
    bool ok;
    bool foreign;       // line 1 names another journal
    unsigned int format;
    unsigned int headerEntries;
    int badLines;
    int dropped;
    bool tooNew;
};

TextParse parseTextBuffer(const std::vector<unsigned char>& f, const char* path) {
    TextParse r;
    memset(&r, 0, sizeof(r));
    const char* base = f.empty() ? nullptr : (const char*)&f[0];
    const size_t total = f.size();
    size_t at = 0;
    int lineNo = 0;
    bool headerSeen = false;
    std::vector<JsonKV> kvs;
    while (at <= total) {
        if (at == total && lineNo > 0) break;
        size_t nl = at;
        while (nl < total && base[nl] != '\n') ++nl;
        const char* ls = base + at;
        size_t ll = nl - at;
        while (ll && (ls[ll - 1] == '\r')) --ll;
        at = nl < total ? nl + 1 : total + 1;
        ++lineNo;
        {
            size_t k = 0;
            while (k < ll && (ls[k] == ' ' || ls[k] == '\t')) ++k;
            if (k == ll) continue;
        }
        const char* why = "?";
        if (!parseFlatObject(ls, ls + ll, &kvs, &why)) {
            if (!headerSeen) {
                logE("journal: line 1 of %s is not a JSON object (%s)", path, why);
                return r;
            }
            ++r.badLines;
            logW("journal: line %d does not parse (%s) - that ONE row is lost, the rest of the "
                 "file is fine", lineNo, why);
            continue;
        }
        if (!headerSeen) {
            headerSeen = true;
            bool sawFormat = false, sawName = false;
            for (size_t i = 0; i < kvs.size(); ++i) {
                if (kvs[i].key == "format") {
                    if (kvs[i].isString || !jsonU32(kvs[i].val, &r.format)) break;
                    sawFormat = true;
                } else if (kvs[i].key == "entries" && !kvs[i].isString) {
                    jsonU32(kvs[i].val, &r.headerEntries);
                } else if (kvs[i].key == "journal" && kvs[i].isString) {
                    sawName = kvs[i].val == UT_JOURNAL_NAME;
                }
            }
            if (!sawName) {
                r.foreign = true;
                logE("journal: line 1 of %s does not name \"%s\" - not this mod's journal", path,
                     UT_JOURNAL_NAME);
                return r;
            }
            if (!sawFormat || r.format < 1) {
                logE("journal: line 1 of %s has no usable \"format\" number", path);
                return r;
            }
            if (r.format > UT_JOURNAL_FORMAT) {
                r.tooNew = true;
                logE("***** journal: the file is format %u and this build reads at most %u - "
                     "READ-ONLY this session; update the mod *****",
                     r.format, (unsigned int)UT_JOURNAL_FORMAT);
            }
            r.ok = true;
            continue;
        }
        Row row;
        char why2[192] = {0};
        if (!rowFromKVs(kvs, &row, why2, sizeof(why2))) {
            ++r.dropped;
            logW("***** journal: %s line %d is DROPPED (%s) - that ONE row is lost, the rest of "
                 "the file is fine", path, lineNo, why2);
            continue;
        }
        row.seq = ++g_nextSeq;
        g_rows->push_back(row);   // every line is a copy: never merged
    }
    if (!headerSeen) logD("journal: %s is empty - ignored", path);
    return r;
}

// Reads the set's file into g_rows. Caller holds g_cs. Sets the READ-ONLY latch for every "there
// and cannot be used" case.
void readSetFile() {
    std::vector<unsigned char> f;
    bool present = false;
    if (!readWholeFile(g_path, &f, &present)) {
        if (present) {
            InterlockedExchange(&g_readOnly, 1);
            logE("***** journal: %s EXISTS but could not be read - READ-ONLY this session; "
                 "close whatever is holding the file and restart *****", g_path);
        }
        return;
    }
    const TextParse r = parseTextBuffer(f, g_path);
    if (!r.ok) {
        InterlockedExchange(&g_readOnly, 1);
        logE("***** journal: %s is there and could not be used%s - READ-ONLY this session; it "
             "is left exactly as it is *****", g_path,
             r.foreign ? " (another journal's header)" : "");
        return;
    }
    if (r.tooNew) InterlockedExchange(&g_readOnly, 1);
    if (r.headerEntries != (unsigned int)g_rows->size() || r.badLines || r.dropped) {
        InterlockedExchange(&g_copyAside, 1);
        logW("***** journal: the file says %u rows and %u were read (%d unparseable, %d "
             "dropped) - the good ones are kept; the file is copied aside before the next "
             "write *****", r.headerEntries, (unsigned int)g_rows->size(), r.badLines, r.dropped);
    }
}

// TODAY'S rule for the pending rows a save could not settle - all of them, or only the
// unresolved ones (the earlier fallback): an "out" row is RESTORED, an "in" row is reported and left to
// the next save. Caller holds g_cs. Returns the rows it touched.
size_t todaysRule(bool onlyUnresolved) {
    size_t restored = 0, restoredNow = 0, pendingIn = 0;
    const char* firstOut = nullptr;
    const char* firstIn = nullptr;
    for (size_t i = 0; i < g_rows->size(); ++i) {
        Row& r = (*g_rows)[i];
        if (onlyUnresolved && !r.unresolved) continue;
        const bool wasOut = r.unresolved && r.wasOut;   // restored at the open
        r.unresolved = false;
        r.wasOut = false;
        if (wasOut || r.pending == kPendOut) {
            // The take reached the journal but no character save was seen after it: the file on
            // disk may not hold the item. Restore the row - a duplicate at worst, never a loss.
            if (r.pending == kPendOut) {
                r.pending = kPendNone;
                r.takenAt = 0;
                ++restoredNow;
            }
            if (!firstOut) firstOut = r.key.c_str();
            ++restored;
        } else if (r.pending == kPendIn) {
            if (!firstIn) firstIn = r.key.c_str();
            ++pendingIn;
        }
    }
    if (restoredNow) InterlockedExchange(&g_dirty, 1);
    if (restored) {
        logW("journal: %u take(s) were written before a character save that never came - the "
             "row(s) are RESTORED to the collection (first: %s); if the item is also in your "
             "inventory, that is a duplicate, not a loss", (unsigned int)restored, firstOut);
    }
    if (pendingIn) {
        logW("journal: %u row(s) written before a character save that never came - check for a "
             "duplicate of %s%s (the item may also still be in the character's inventory)",
             (unsigned int)pendingIn, firstIn, pendingIn > 1 ? " and the others" : "");
    }
    return restored + pendingIn;
}

// The load-time rules for the crash window. Caller holds g_cs.
// the newest character save settles first (utLoadSettles: bounded by this
// process's start), and only what it cannot settle is restored / reported (today's rule) - or,
// with the earlier defer, marked UNRESOLVED for the container check (ut_recon.cpp).
void settleAtLoad(unsigned long long saveTime, unsigned long long processStart, bool defer) {
    size_t settledOut = 0, settledIn = 0;
    for (size_t i = g_rows->size(); i-- > 0;) {
        Row& r = (*g_rows)[i];
        if (r.pending == kPendOut && utLoadSettles(r.takenAt, saveTime, processStart)) {
            g_rows->erase(g_rows->begin() + (ptrdiff_t)i);   // the save holds the take: gone
            ++settledOut;
        } else if (r.pending == kPendIn && utLoadSettles(r.at, saveTime, processStart)) {
            r.pending = kPendNone;
            ++settledIn;
        }
    }
    if (settledOut || settledIn) {
        InterlockedExchange(&g_dirty, 1);
        logI("journal: a character save written after them settled %u take(s) and %u "
             "deposit(s) at load", (unsigned int)settledOut, (unsigned int)settledIn);
    }
    if (!defer) {
        todaysRule(false);
        return;
    }
    size_t in = 0, out = 0;
    const char* first = nullptr;
    for (size_t i = 0; i < g_rows->size(); ++i) {
        Row& r = (*g_rows)[i];
        if (r.pending != kPendIn && r.pending != kPendOut) continue;
        r.unresolved = true;
        if (!first) first = r.key.c_str();
        if (r.pending == kPendOut) {
            // RESTORED on disk now (today's non-loss default), so no stale "pending
            // out" is carried into a character save of this session - a later load would take
            // that save for the take's and erase the row (a loss). The session mark wasOut lets
            // the container check still ERASE the row when the item is proven present; "absent"
            // then changes nothing.
            r.pending = kPendNone;
            r.takenAt = 0;
            r.wasOut = true;
            InterlockedExchange(&g_dirty, 1);
            ++out;
        } else {
            ++in;
        }
    }
    if (in || out) {
        logI("journal: %u deposit(s) and %u take(s) were written before a character save that "
             "never came (first: %s) - the take(s) are back in the collection meanwhile; they are "
             "checked against the character's inventory, equipment and caravan stores",
             (unsigned int)in, (unsigned int)out, first);
    }
}

// ---- the CSV export (GD's csvField, TQ's columns) ---------------------------------------------
bool g_csvFieldOk = true;

void csvField(std::string* out, const char* text, bool last) {
    const char* t = text ? text : "";
    bool needQuote = false;
    for (const char* p = t; *p; ++p) {
        if (*p == ',' || *p == '"' || *p == '\r' || *p == '\n') {
            needQuote = true;
            break;
        }
    }
    try {
        if (needQuote) {
            out->push_back('"');
            for (const char* p = t; *p; ++p) {
                if (*p == '"') out->push_back('"');
                out->push_back(*p);
            }
            out->push_back('"');
        } else {
            out->append(t);
        }
        out->append(last ? "\r\n" : ",");
    } catch (...) {
        g_csvFieldOk = false;
    }
}

void csvRow(std::string* out, const Row& r) {
    char num[32], iso[64];
    csvField(out, r.key.c_str(), false);
    csvField(out, r.pending == kPendOut ? "taken" : r.pending == kPendIn ? "unsaved" : "yes",
             false);
    isoFromFileTime(r.at, iso, sizeof(iso));
    csvField(out, iso, false);
    _snprintf_s(num, sizeof(num), _TRUNCATE, "%u", r.stack);
    csvField(out, num, false);
    for (int k = 0; k < kUtIdStrCount; ++k) csvField(out, r.s[k].c_str(), false);
    _snprintf_s(num, sizeof(num), _TRUNCATE, "%u", r.seed);
    csvField(out, num, false);
    _snprintf_s(num, sizeof(num), _TRUNCATE, "%u", r.var1);
    csvField(out, num, false);
    _snprintf_s(num, sizeof(num), _TRUNCATE, "%u", r.var2);
    csvField(out, num, false);
    _snprintf_s(num, sizeof(num), _TRUNCATE, "%u", r.b8);
    csvField(out, num, true);
}

void csvWrite() {
    const LONG mode = InterlockedCompareExchange(&g_csvMode, 0, 0);
    if (mode <= 0 || !g_csvPath[0] || !g_rows) return;
    std::string text;
    g_csvFieldOk = true;
    try {
        text.append("record,collected,deposited,stack,base,prefix,suffix,relic,relicBonus,relic2,"
                    "relicBonus2,seed,var1,var2,b8\r\n");
        for (size_t i = 0; i < g_rows->size(); ++i) {
            if ((*g_rows)[i].pending == kPendOut && mode < 2) continue;
            csvRow(&text, (*g_rows)[i]);
        }
    } catch (...) {
        return;
    }
    if (!g_csvFieldOk) return;
    if (writeWholeFileAtomic(g_csvPath, text.c_str(), text.size())) {
        InterlockedIncrement(&g_csvWrites);
        InterlockedExchange(&g_csvFailLogged, 0);
        return;
    }
    if (!InterlockedExchange(&g_csvFailLogged, 1)) {
        logW("export: %s could NOT be written (the journal itself is intact; nothing reads the "
             "CSV back)", g_csvPath);
    }
}

bool leafOk(const char* leaf) {
    if (!leaf || strncmp(leaf, "tq-uniq-items", 13) != 0) return false;
    const size_t n = strlen(leaf);
    if (n > 80) return false;
    for (size_t i = 0; i < n; ++i) {
        const char c = leaf[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'))
            return false;
    }
    return true;
}

// Runs `mutate` on the rows under the lock, writes the file, and on a failed write puts the
// PRE-IMAGE back exactly (GD's journalDepositCommit, generalised to the take).
template <typename F>
bool commitWith(F mutate, bool* rolledBack) {
    if (rolledBack) *rolledBack = false;
    std::vector<Row> pre;
    bool ok = false;
    EnterCriticalSection(&g_cs);
    try {
        pre = *g_rows;
        ok = mutate();
        if (ok) rebuildIndex();
    } catch (...) {
        ok = false;
    }
    if (!ok) {
        try {
            *g_rows = pre;
            rebuildIndex();
        } catch (...) {
        }
    }
    LeaveCriticalSection(&g_cs);
    if (!ok) return false;
    if (journalFlushNow()) return true;
    EnterCriticalSection(&g_cs);
    try {
        *g_rows = pre;
        rebuildIndex();
        if (rolledBack) *rolledBack = true;
    } catch (...) {
    }
    LeaveCriticalSection(&g_cs);
    InterlockedExchange(&g_dirty, 1);
    if (g_event) SetEvent(g_event);
    return false;
}

bool sameIdentity(const Row& r, const UtReplicaCapture& c) {
    for (int k = 0; k < kUtIdStrCount; ++k)
        if (r.s[k] != c.str[k]) return false;
    return r.seed == c.seed && r.var1 == c.var1 && r.var2 == c.var2 && r.b8 == c.b8 &&
           r.stack == c.stack;
}

}  // namespace

bool utCaptureValid(const UtReplicaCapture& cap, char* why, size_t whyCap) {
    for (int k = 0; k < kUtIdStrCount; ++k) {
        const size_t n = strnlen(cap.str[k], kUtIdStrMax);
        if (n >= kUtIdStrMax) {
            _snprintf_s(why, whyCap, _TRUNCATE, "string %s has no end", kUtIdStrKey[k]);
            return false;
        }
        for (size_t i = 0; i < n; ++i) {
            const unsigned char c = (unsigned char)cap.str[k][i];
            if (c < 0x20 || c >= 0x7F) {
                _snprintf_s(why, whyCap, _TRUNCATE, "string %s is not printable ASCII",
                            kUtIdStrKey[k]);
                return false;
            }
        }
    }
    char key[256];
    if (!cap.str[kUtIdBase][0] || !utJournalKey(cap.str[kUtIdBase], key, sizeof(key))) {
        _snprintf_s(why, whyCap, _TRUNCATE, "no base record");
        return false;
    }
    if (strncmp(key, cap.record, sizeof(key)) != 0) {
        _snprintf_s(why, whyCap, _TRUNCATE, "the key is not the folded base");
        return false;
    }
    if (cap.b8 > 255u || cap.stack == 0u) {
        _snprintf_s(why, whyCap, _TRUNCATE, "b8 %u / stack %u out of range", cap.b8, cap.stack);
        return false;
    }
    return true;
}

bool journalInit(HMODULE selfModule) {
    if (!g_csReady) {
        InitializeCriticalSection(&g_cs);
        g_csReady = true;
    }
    if (g_rows) return true;
    try {
        g_rows = new std::vector<Row>();
        g_index = new std::unordered_map<unsigned long long, unsigned int>();
    } catch (...) {
        return false;
    }
    utModDirA(selfModule, g_dir, sizeof(g_dir));
    g_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    logD("journal: the files live in \"%s\"; the set is chosen at the first world", g_dir);
    return true;
}

bool journalOpenSet(const char* leaf, unsigned long long saveTime,
                    unsigned long long processStart, bool defer) {
    if (!g_rows || !g_csReady) return false;
    const bool want = leaf && leaf[0];
    if (want && !leafOk(leaf)) {
        logW("journal: set name \"%s\" refused - moves are refused for this world", leaf);
        return journalOpenSet(nullptr);
    }
    if (InterlockedCompareExchange(&g_setOpen, 0, 0) && want && strcmp(leaf, g_leaf) == 0)
        return true;   // the same set again: nothing moves
    if (InterlockedCompareExchange(&g_setOpen, 0, 0) &&
        InterlockedCompareExchange(&g_dirty, 0, 0)) {
        journalFlushNow();   // best effort: the pending marks of the set being left
    }
    EnterCriticalSection(&g_cs);
    InterlockedExchange(&g_setOpen, 0);
    g_rows->clear();
    g_index->clear();
    g_collected = 0;
    InterlockedExchange(&g_dirty, 0);
    InterlockedExchange(&g_readOnly, 0);
    InterlockedExchange(&g_copyAside, 0);
    InterlockedExchange(&g_readOnlySaid, 0);
    g_path[0] = g_csvPath[0] = g_leaf[0] = 0;
    if (want) {
        _snprintf_s(g_leaf, sizeof(g_leaf), _TRUNCATE, "%s", leaf);
        _snprintf_s(g_path, sizeof(g_path), _TRUNCATE, "%s\\%s.jsonl", g_dir, leaf);
        _snprintf_s(g_csvPath, sizeof(g_csvPath), _TRUNCATE, "%s\\%s.csv", g_dir, leaf);
        try {
            readSetFile();
            settleAtLoad(saveTime, processStart, defer);
            rebuildIndex();
        } catch (...) {
            g_rows->clear();
            g_index->clear();
            InterlockedExchange(&g_readOnly, 1);
            logE("journal: reading %s threw - READ-ONLY this session", g_path);
        }
        InterlockedExchange(&g_setOpen, 1);
    }
    const unsigned int rows = (unsigned int)g_rows->size();
    const unsigned int collected = g_collected;
    LeaveCriticalSection(&g_cs);
    if (want) {
        logI("journal: set \"%s\" - %u row(s), %u in the collection, \"%s\"%s", g_leaf, rows,
             collected, g_path,
             InterlockedCompareExchange(&g_readOnly, 0, 0) ? " [READ-ONLY this session]" : "");
        if (InterlockedCompareExchange(&g_dirty, 0, 0) && g_event) SetEvent(g_event);
    } else {
        logI("journal: no save set for this world - deposits and takes are refused, the page "
             "shows nothing collected");
    }
    return want;
}

bool journalSetKnown() { return InterlockedCompareExchange(&g_setOpen, 0, 0) != 0; }
const char* journalSetLeaf() { return g_leaf; }
bool journalReadOnly() { return InterlockedCompareExchange(&g_readOnly, 0, 0) != 0; }
bool journalUsable() {
    return g_rows && journalSetKnown() && g_path[0] && !journalReadOnly();
}
HANDLE journalEvent() { return g_event; }

bool journalFlushNow() {
    if (!g_rows || !g_csReady || !journalSetKnown()) return false;
    InterlockedExchange(&g_csvArm, 0);
    if (journalReadOnly()) {
        if (InterlockedExchange(&g_readOnlySaid, 1) == 0) {
            logW("***** journal: %s is NOT written - READ-ONLY this session, so deposits and "
                 "takes are refused *****", g_path);
        }
        return false;
    }
    InterlockedExchange(&g_dirty, 0);
    bool ok = false;
    size_t n = 0;
    EnterCriticalSection(&g_cs);
    try {
        n = g_rows->size();
        ok = writeFile();
        if (ok) csvWrite();
    } catch (...) {
        ok = false;
    }
    LeaveCriticalSection(&g_cs);
    if (!ok) {
        logE("journal: WRITE FAILED (%s)", g_path);
        InterlockedExchange(&g_dirty, 1);
        return false;
    }
    logD("journal: wrote %u row(s) to %s", (unsigned int)n, g_path);
    return true;
}

void journalService() {
    if (!g_rows || !g_csReady || !journalSetKnown()) return;
    if (!InterlockedCompareExchange(&g_dirty, 0, 0)) {
        if (!InterlockedExchange(&g_csvArm, 0)) return;
        if (journalReadOnly()) return;
        EnterCriticalSection(&g_cs);
        try {
            csvWrite();
        } catch (...) {
        }
        LeaveCriticalSection(&g_cs);
        return;
    }
    journalFlushNow();
}

unsigned int journalRows(const char* key) {
    if (!key || !*key || !g_rows || !g_csReady || !journalSetKnown()) return 0;
    const unsigned long long h = keyHash(key);
    unsigned int n = 0;
    EnterCriticalSection(&g_cs);
    std::unordered_map<unsigned long long, unsigned int>::const_iterator it = g_index->find(h);
    if (it != g_index->end()) n = it->second;
    LeaveCriticalSection(&g_cs);
    return n;
}

unsigned int journalCollectedTotal() {
    if (!g_rows || !g_csReady || !journalSetKnown()) return 0;
    EnterCriticalSection(&g_cs);
    const unsigned int n = g_collected;
    LeaveCriticalSection(&g_cs);
    return n;
}

size_t journalCount() {
    if (!g_rows || !g_csReady) return 0;
    EnterCriticalSection(&g_cs);
    const size_t n = g_rows->size();
    LeaveCriticalSection(&g_cs);
    return n;
}

bool journalNewest(const char* key, UtReplicaCapture* out, unsigned long long* seq) {
    if (!key || !*key || !g_rows || !g_csReady || !journalSetKnown()) return false;
    bool found = false;
    EnterCriticalSection(&g_cs);
    for (size_t i = g_rows->size(); i-- > 0;) {
        const Row& r = (*g_rows)[i];
        if (r.pending == kPendOut || r.key != key) continue;
        if (out) {
            memset(out, 0, sizeof(*out));
            _snprintf_s(out->record, sizeof(out->record), _TRUNCATE, "%s", r.key.c_str());
            for (int k = 0; k < kUtIdStrCount; ++k)
                _snprintf_s(out->str[k], kUtIdStrMax, _TRUNCATE, "%s", r.s[k].c_str());
            out->seed = r.seed;
            out->var1 = r.var1;
            out->var2 = r.var2;
            out->b8 = r.b8;
            out->stack = r.stack;
        }
        if (seq) *seq = r.seq;
        found = true;
        break;
    }
    LeaveCriticalSection(&g_cs);
    return found;
}

bool journalDepositCommit(const UtReplicaCapture& cap, unsigned long long* seqOut,
                          bool* rolledBack) {
    if (rolledBack) *rolledBack = false;
    if (!journalUsable()) {
        logW("deposit REFUSED: %s - the item was not touched",
             !journalSetKnown() ? "no save set is known for this world"
             : journalReadOnly() ? "the journal is READ-ONLY this session"
                                 : "the journal has no path");
        return false;
    }
    char why[160];
    if (!utCaptureValid(cap, why, sizeof(why))) {
        logW("deposit REFUSED: the captured identity is unusable (%s) - the item was not touched",
             why);
        return false;
    }
    unsigned long long seq = 0;
    bool cancelled = false;
    const bool ok = commitWith(
        [&]() -> bool {
            // A deposit of exactly the identity of a take that no save has seen yet CANCELS that
            // take (the row is settled again): the character file on disk never lost the row's
            // item, so a new row would be a duplicate after a crash.
            for (size_t i = g_rows->size(); i-- > 0;) {
                Row& r = (*g_rows)[i];
                if (r.pending == kPendOut && r.key == cap.record && sameIdentity(r, cap)) {
                    r.pending = kPendNone;
                    r.takenAt = 0;
                    r.unresolved = false;
                    seq = r.seq;
                    cancelled = true;
                    return true;
                }
            }
            Row r;
            r.key = cap.record;
            for (int k = 0; k < kUtIdStrCount; ++k) r.s[k] = cap.str[k];
            r.seed = cap.seed;
            r.var1 = cap.var1;
            r.var2 = cap.var2;
            r.b8 = cap.b8;
            r.stack = cap.stack;
            r.at = nowFileTime();
            r.pending = kPendIn;
            r.seq = seq = ++g_nextSeq;
            g_rows->push_back(r);
            return true;
        },
        rolledBack);
    if (!ok) {
        logW("deposit REFUSED: the journal could not be written - the item was not touched");
        return false;
    }
    if (seqOut) *seqOut = seq;
    logD("journal: deposit row %llu %s%s", seq, cap.record,
         cancelled ? " (cancels an unsaved take of the same identity)" : "");
    return true;
}

bool journalTakeCommit(const char* key, unsigned long long seq, bool* rolledBack) {
    if (rolledBack) *rolledBack = false;
    if (!key || !journalUsable()) return false;
    bool stale = false, erased = false;
    const bool ok = commitWith(
        [&]() -> bool {
            for (size_t i = g_rows->size(); i-- > 0;) {
                Row& r = (*g_rows)[i];
                if (r.pending == kPendOut || r.key != key) continue;
                if (r.seq != seq) {   // not the newest row: the page is stale - refuse
                    stale = true;
                    return false;
                }
                if (r.pending == kPendIn) {
                    // Deposited and taken with no save in between: the character file on disk
                    // still holds the item, so the row simply goes (both states agree).
                    g_rows->erase(g_rows->begin() + (ptrdiff_t)i);
                    erased = true;
                } else {
                    r.pending = kPendOut;
                    r.takenAt = nowFileTime();
                    r.unresolved = false;
                }
                return true;
            }
            return false;
        },
        rolledBack);
    if (!ok) {
        logW("take REFUSED: %s - nothing was handed out",
             stale ? "the page's copy is not the newest row (stale page)"
                   : "the journal could not be written or has no such row");
        return false;
    }
    logD("journal: take row %llu %s%s", seq, key, erased ? " (an unsaved deposit: erased)" : "");
    return true;
}

// the "out" row whose item is still on the cursor (game thread writes, the settle
// reads it under g_cs).
static unsigned long long g_heldSeq = 0;

void journalSetHeldSeq(unsigned long long seq) {
    if (!g_csReady) return;
    EnterCriticalSection(&g_cs);
    g_heldSeq = seq;
    LeaveCriticalSection(&g_cs);
}

bool journalTouchOut(unsigned long long seq) {
    if (!g_rows || !g_csReady || !seq) return false;
    bool found = false;
    EnterCriticalSection(&g_cs);
    for (size_t i = 0; i < g_rows->size(); ++i) {
        Row& r = (*g_rows)[i];
        if (r.pending == kPendOut && r.seq == seq) {
            r.takenAt = nowFileTime();
            found = true;
            break;
        }
    }
    LeaveCriticalSection(&g_cs);
    if (found) {
        InterlockedExchange(&g_dirty, 1);
        if (g_event) SetEvent(g_event);
    }
    return found;
}

int journalSaveObserved(unsigned long long saveWriteTime) {
    if (!g_rows || !g_csReady || !journalSetKnown() || !saveWriteTime) return 0;
    int changed = 0;
    EnterCriticalSection(&g_cs);
    try {
        // an unresolved "in" row this save would settle was not decided by the
        // container check before the save (the save may hold the item too): today's WARN first,
        // then the save settles it, as. (An unresolved "out" row was restored at the
        // open: "pending" none, nothing here touches it.)
        size_t warnIn = 0;
        const char* firstIn = nullptr;
        for (size_t i = 0; i < g_rows->size(); ++i) {
            Row& r = (*g_rows)[i];
            if (!r.unresolved || r.wasOut || r.pending != kPendIn || r.at >= saveWriteTime)
                continue;
            r.unresolved = false;
            if (!firstIn) firstIn = r.key.c_str();
            ++warnIn;
        }
        if (warnIn) {
            logW("journal: %u row(s) written before a character save that never came - check for "
                 "a duplicate of %s%s (the item may also still be in the character's inventory)",
                 (unsigned int)warnIn, firstIn, warnIn > 1 ? " and the others" : "");
        }
        for (size_t i = g_rows->size(); i-- > 0;) {
            Row& r = (*g_rows)[i];
            if (r.unresolved) continue;   // the container check decides these
            if (r.pending == kPendIn && r.at < saveWriteTime) {
                r.pending = kPendNone;
                ++changed;
            } else if (r.pending == kPendOut && r.takenAt < saveWriteTime &&
                       !(g_heldSeq && r.seq == g_heldSeq)) {
                g_rows->erase(g_rows->begin() + (ptrdiff_t)i);
                ++changed;
            }
        }
        if (changed) rebuildIndex();
    } catch (...) {
    }
    LeaveCriticalSection(&g_cs);
    if (changed) {
        InterlockedExchange(&g_dirty, 1);
        if (g_event) SetEvent(g_event);
    }
    return changed;
}

size_t journalPendingIn() {
    if (!g_rows || !g_csReady) return 0;
    size_t a = 0;
    EnterCriticalSection(&g_cs);
    countPending(&a, nullptr);
    LeaveCriticalSection(&g_cs);
    return a;
}

size_t journalPendingOut() {
    if (!g_rows || !g_csReady) return 0;
    size_t b = 0;
    EnterCriticalSection(&g_cs);
    countPending(nullptr, &b);
    LeaveCriticalSection(&g_cs);
    return b;
}

// ---- the unresolved rows ---------------------------------------------------------------
size_t journalUnresolved() {
    if (!g_rows || !g_csReady) return 0;
    size_t n = 0;
    EnterCriticalSection(&g_cs);
    for (size_t i = 0; i < g_rows->size(); ++i)
        if ((*g_rows)[i].unresolved) ++n;
    LeaveCriticalSection(&g_cs);
    return n;
}

size_t journalUnresolvedRows(UtPendingRow* out, size_t cap) {
    if (!g_rows || !g_csReady || !out) return 0;
    size_t n = 0;
    EnterCriticalSection(&g_cs);
    for (size_t i = 0; i < g_rows->size() && n < cap; ++i) {
        const Row& r = (*g_rows)[i];
        if (!r.unresolved) continue;
        UtPendingRow& o = out[n++];
        memset(&o, 0, sizeof(o));
        o.seq = r.seq;
        o.state = r.wasOut ? kPendOut : r.pending;   // judged as "out"
        _snprintf_s(o.id.record, sizeof(o.id.record), _TRUNCATE, "%s", r.key.c_str());
        for (int k = 0; k < kUtIdStrCount; ++k)
            _snprintf_s(o.id.str[k], kUtIdStrMax, _TRUNCATE, "%s", r.s[k].c_str());
        o.id.seed = r.seed;
        o.id.var1 = r.var1;
        o.id.var2 = r.var2;
        o.id.b8 = r.b8;
        o.id.stack = r.stack;
    }
    LeaveCriticalSection(&g_cs);
    return n;
}

int journalApplyVerdicts(const UtVerdictItem* v, size_t n, const char* fallbackWhy) {
    if (!g_rows || !g_csReady || !journalUsable()) return 0;
    if (!v) n = 0;
    if (!n && !fallbackWhy) return 0;
    int changed = 0;
    bool disk = false;
    EnterCriticalSection(&g_cs);
    try {
        for (size_t j = 0; j < n; ++j) {
            const int verdict = v[j].verdict;
            if (verdict == kUtVerdictKeep) continue;
            const char* where = v[j].where && v[j].where[0] ? v[j].where : "containers";
            for (size_t i = 0; i < g_rows->size(); ++i) {
                Row& r = (*g_rows)[i];
                if (!r.unresolved || r.seq != v[j].seq) continue;
                // an "out" row was restored at the open (wasOut, "pending" none)
                const bool in = r.pending == kPendIn && !r.wasOut;
                const bool out = r.wasOut || r.pending == kPendOut;
                char key[256];
                _snprintf_s(key, sizeof(key), _TRUNCATE, "%s", r.key.c_str());
                if (verdict == kUtVerdictErase && in) {
                    g_rows->erase(g_rows->begin() + (ptrdiff_t)i);
                    disk = true;
                    logI("journal: %s - the item is back in your %s (its deposit never reached a "
                         "character save); the row is dropped - deposit it again", key, where);
                } else if (verdict == kUtVerdictErase && out) {
                    g_rows->erase(g_rows->begin() + (ptrdiff_t)i);
                    disk = true;
                    logI("journal: %s - the taken item is in your %s (the take reached a character "
                         "save); the row is removed for good", key, where);
                } else if (verdict == kUtVerdictSettle && in) {
                    // "absent" is judged against the LOADED character only (the
                    // rows do not record their character; one journal serves the save set), so
                    // it proves nothing about another character: today's WARN wording stays.
                    r.pending = kPendNone;
                    r.unresolved = false;
                    disk = true;
                    logW("journal: %s - a row written before a character save that never came is "
                         "in none of the loaded character's containers; it is settled as "
                         "collected - check for a duplicate (the item may still be in another "
                         "character's inventory)", key);
                } else if (verdict == kUtVerdictRestore && out) {
                    if (r.pending == kPendOut) {
                        r.pending = kPendNone;
                        r.takenAt = 0;
                        disk = true;
                    }
                    r.unresolved = false;
                    r.wasOut = false;
                    logW("journal: %s - a take written before a character save that never came is "
                         "in none of the loaded character's containers; the row is RESTORED to "
                         "the collection - if the item is also in another character's inventory, "
                         "that is a duplicate, not a loss", key);
                } else {
                    break;   // a verdict that does not fit the row's state: nothing changes
                }
                ++changed;
                break;
            }
        }
        // the rows the check could not decide get today's rule HERE, in memory,
        // so a pass that settles some rows and falls back for the others writes ONCE
        size_t fell = 0;
        if (fallbackWhy) {
            bool any = false;
            for (size_t i = 0; i < g_rows->size() && !any; ++i) any = (*g_rows)[i].unresolved;
            if (any) {
                logI("journal: the pending rows could not be checked against the character's "
                     "containers (%s) - the load rule applies", fallbackWhy);
                fell = todaysRule(true);
            }
        }
        if (changed || fell) rebuildIndex();
    } catch (...) {
    }
    LeaveCriticalSection(&g_cs);
    if (disk) InterlockedExchange(&g_dirty, 1);
    if (!InterlockedCompareExchange(&g_dirty, 0, 0)) return changed;
    if (journalFlushNow()) return changed;   // the ONE atomic write of this settle
    if (g_event) SetEvent(g_event);          // failed: the worker retries (the rows stay settled)
    return -1;
}

int journalUnresolvedFallback(const char* why) {
    if (!g_rows || !g_csReady) return 0;
    size_t n = 0;
    EnterCriticalSection(&g_cs);
    bool any = false;
    for (size_t i = 0; i < g_rows->size() && !any; ++i) any = (*g_rows)[i].unresolved;
    if (any) {
        logI("journal: the pending rows could not be checked against the character's containers "
             "(%s) - the load rule applies", why ? why : "unknown");
        try {
            n = todaysRule(true);
            rebuildIndex();
        } catch (...) {
        }
    }
    LeaveCriticalSection(&g_cs);
    if (InterlockedCompareExchange(&g_dirty, 0, 0) && g_event) SetEvent(g_event);
    return (int)n;
}

void journalSetCsvExport(int mode) {
    const LONG m = mode < 0 ? 0 : mode > 2 ? 2 : (LONG)mode;
    const LONG prev = InterlockedExchange(&g_csvMode, m);
    if (prev != m && m > 0) {
        InterlockedExchange(&g_csvArm, 1);
        if (g_event) SetEvent(g_event);
    }
}

const char* journalPath() { return g_path; }
const char* journalCsvPath() { return g_csvPath; }
long journalWrites() { return InterlockedCompareExchange(&g_writes, 0, 0); }
long journalCsvWrites() { return InterlockedCompareExchange(&g_csvWrites, 0, 0); }

}  // namespace ut
