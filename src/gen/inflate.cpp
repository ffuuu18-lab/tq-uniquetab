// inflate.cpp - see inflate.h. A table-driven DEFLATE decoder: codes of up to 9 bits resolve
// in one lookup, longer ones by the canonical count walk (RFC 1951 3.2.2), so a dynamic block
// costs one 512-entry table fill per code, not a 32 K one.
#include "gen/inflate.h"

#include <cstring>

namespace gen {

namespace {

const int kMaxBits = 15;
const int kFastBits = 9;
const int kMaxLit = 288;     // 286 used + 2 reserved
const int kMaxDist = 30;

const std::uint16_t kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const std::uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const std::uint16_t kDistBase[30] = {1,    2,    3,    4,    5,    7,     9,     13,    17,  25,
                                     33,   49,   65,   97,   129,  193,   257,   385,   513, 769,
                                     1025, 1537, 2049, 3073, 4097, 6145,  8193,  12289, 16385, 24577};
const std::uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                     6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
const std::uint8_t kClOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

struct Huff {
    std::uint16_t fast[1 << kFastBits];   // (length << 9) | symbol, 0 = not a short code
    std::uint16_t count[kMaxBits + 1];    // codes per length
    std::uint16_t symbol[kMaxLit];        // symbols in canonical order
};

// Builds the decoding tables from code lengths. Returns the number of unused codes left at the
// longest length (0 = complete), or -1 when the lengths are over-subscribed.
int build(Huff& h, const std::uint8_t* lengths, int n) {
    std::memset(h.count, 0, sizeof h.count);
    std::memset(h.fast, 0, sizeof h.fast);
    for (int s = 0; s < n; ++s) ++h.count[lengths[s]];
    if (h.count[0] == n) return 0;                       // no codes: complete, decoding fails
    int left = 1;
    for (int len = 1; len <= kMaxBits; ++len) {
        left <<= 1;
        left -= h.count[len];
        if (left < 0) return -1;
    }
    std::uint16_t offs[kMaxBits + 2];
    std::uint32_t next[kMaxBits + 2];
    offs[1] = 0;
    for (int len = 1; len < kMaxBits; ++len) offs[len + 1] = std::uint16_t(offs[len] + h.count[len]);
    // RFC 1951 3.2.2 next_code, with bl_count[0] taken as 0
    std::uint32_t code = 0;
    next[0] = 0;
    for (int len = 1; len <= kMaxBits; ++len) {
        code = (code + (len > 1 ? h.count[len - 1] : 0u)) << 1;
        next[len] = code;
    }
    for (int s = 0; s < n; ++s) {
        const int len = lengths[s];
        if (len == 0) continue;
        h.symbol[offs[len]++] = std::uint16_t(s);
        const std::uint32_t c = next[len]++;
        if (len > kFastBits) continue;
        std::uint32_t rev = 0;                           // the stream sends the code MSB first
        for (int i = 0; i < len; ++i) rev |= ((c >> i) & 1u) << (len - 1 - i);
        for (std::uint32_t k = rev; k < (1u << kFastBits); k += (1u << len))
            h.fast[k] = std::uint16_t((len << 9) | s);
    }
    return left;
}

struct State {
    const std::uint8_t* src;
    std::size_t len;
    std::size_t pos = 0;
    std::uint64_t bitbuf = 0;
    int bitcnt = 0;
    const char* err = nullptr;

    void fill() {
        while (bitcnt <= 56 && pos < len) {
            bitbuf |= std::uint64_t(src[pos++]) << bitcnt;
            bitcnt += 8;
        }
    }
    // n <= 16 bits, LSB first. False (err set) when the stream ends first.
    bool bits(int n, std::uint32_t& v) {
        if (bitcnt < n) fill();
        if (bitcnt < n) { err = "the stream ends inside a block"; return false; }
        v = std::uint32_t(bitbuf & ((std::uint64_t(1) << n) - 1));
        bitbuf >>= n;
        bitcnt -= n;
        return true;
    }
    // One Huffman symbol, or -1 (err set).
    int decode(const Huff& h) {
        if (bitcnt < kMaxBits) fill();
        const std::uint16_t e = h.fast[bitbuf & ((1u << kFastBits) - 1)];
        if (e) {
            const int l = e >> 9;
            if (l > bitcnt) { err = "the stream ends inside a code"; return -1; }
            bitbuf >>= l;
            bitcnt -= l;
            return e & 0x1FF;
        }
        int code = 0, first = 0, index = 0;
        for (int l = 1; l <= kMaxBits; ++l) {
            code |= int((bitbuf >> (l - 1)) & 1u);
            const int count = h.count[l];
            if (code - first < count) {
                if (l > bitcnt) { err = "the stream ends inside a code"; return -1; }
                bitbuf >>= l;
                bitcnt -= l;
                return h.symbol[index + (code - first)];
            }
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        err = "an invalid Huffman code";
        return -1;
    }
};

struct Out {
    std::vector<std::uint8_t>& v;
    std::size_t base, op, limit;   // limit = base + maxOut
    const char* err = nullptr;
    Out(std::vector<std::uint8_t>& vec, std::size_t maxOut)
        : v(vec), base(vec.size()), op(vec.size()), limit(vec.size() + maxOut) {
        if (limit < base) limit = SIZE_MAX;           // maxOut so large the sum wrapped
    }
    // Room for n more bytes: grows geometrically, never past the cap.
    bool room(std::size_t n) {
        if (n > limit - op) { err = "the output would pass its size cap"; return false; }
        if (op + n <= v.size()) return true;
        std::size_t want = v.size() - base;
        want = want < 4096 ? 4096 : want * 2;
        std::size_t target = base + want;
        if (target < op + n) target = op + n;
        if (target > limit || target < base) target = limit;
        v.resize(target);
        return true;
    }
};

bool codes(State& s, Out& o, const Huff& lit, const Huff& dist) {
    for (;;) {
        const int sym = s.decode(lit);
        if (sym < 0) return false;
        if (sym < 256) {
            if (!o.room(1)) return false;
            o.v[o.op++] = std::uint8_t(sym);
            continue;
        }
        if (sym == 256) return true;
        const int li = sym - 257;
        if (li >= 29) { s.err = "a length symbol out of range"; return false; }
        std::uint32_t extra = 0;
        if (!s.bits(kLenExtra[li], extra)) return false;
        const std::size_t n = kLenBase[li] + extra;
        const int ds = s.decode(dist);
        if (ds < 0) return false;
        if (ds >= kMaxDist) { s.err = "a distance symbol out of range"; return false; }
        if (!s.bits(kDistExtra[ds], extra)) return false;
        const std::size_t d = kDistBase[ds] + extra;
        if (d > o.op - o.base) { s.err = "a distance before the start of the output"; return false; }
        if (!o.room(n)) return false;
        std::uint8_t* p = o.v.data() + o.op;
        const std::uint8_t* q = p - d;
        for (std::size_t i = 0; i < n; ++i) p[i] = q[i];   // overlap-safe forward copy
        o.op += n;
    }
}

bool stored(State& s, Out& o) {
    // to the byte boundary, then hand the whole buffered bytes back to the input
    s.bitbuf >>= (s.bitcnt & 7);
    s.bitcnt &= ~7;
    s.pos -= std::size_t(s.bitcnt / 8);
    s.bitbuf = 0;
    s.bitcnt = 0;
    if (s.len - s.pos < 4) { s.err = "the stream ends inside a stored block header"; return false; }
    const std::uint32_t n = std::uint32_t(s.src[s.pos]) | (std::uint32_t(s.src[s.pos + 1]) << 8);
    const std::uint32_t nn = std::uint32_t(s.src[s.pos + 2]) | (std::uint32_t(s.src[s.pos + 3]) << 8);
    s.pos += 4;
    if (n != (~nn & 0xFFFFu)) { s.err = "a stored block whose length check fails"; return false; }
    if (s.len - s.pos < n) { s.err = "the stream ends inside a stored block"; return false; }
    if (!o.room(n)) return false;
    if (n) std::memcpy(o.v.data() + o.op, s.src + s.pos, n);
    o.op += n;
    s.pos += n;
    return true;
}

bool fixedBlock(State& s, Out& o) {
    Huff lit, dist;
    std::uint8_t l[kMaxLit];
    int i = 0;
    for (; i < 144; ++i) l[i] = 8;
    for (; i < 256; ++i) l[i] = 9;
    for (; i < 280; ++i) l[i] = 7;
    for (; i < kMaxLit; ++i) l[i] = 8;
    build(lit, l, kMaxLit);
    for (i = 0; i < kMaxDist; ++i) l[i] = 5;
    build(dist, l, kMaxDist);
    return codes(s, o, lit, dist);
}

bool dynamicBlock(State& s, Out& o) {
    std::uint32_t hlit = 0, hdist = 0, hclen = 0;
    if (!s.bits(5, hlit) || !s.bits(5, hdist) || !s.bits(4, hclen)) return false;
    const int nlen = int(hlit) + 257, ndist = int(hdist) + 1, ncode = int(hclen) + 4;
    if (nlen > 286 || ndist > kMaxDist) { s.err = "too many length or distance codes"; return false; }
    std::uint8_t l[kMaxLit + kMaxDist];
    std::memset(l, 0, sizeof l);
    for (int i = 0; i < ncode; ++i) {
        std::uint32_t v = 0;
        if (!s.bits(3, v)) return false;
        l[kClOrder[i]] = std::uint8_t(v);
    }
    Huff cl;
    if (build(cl, l, 19) != 0) { s.err = "an incomplete code-length code"; return false; }
    std::memset(l, 0, sizeof l);
    int idx = 0;
    while (idx < nlen + ndist) {
        int sym = s.decode(cl);
        if (sym < 0) return false;
        if (sym < 16) { l[idx++] = std::uint8_t(sym); continue; }
        std::uint8_t val = 0;
        std::uint32_t rep = 0;
        if (sym == 16) {
            if (idx == 0) { s.err = "a repeat with no previous length"; return false; }
            val = l[idx - 1];
            if (!s.bits(2, rep)) return false;
            rep += 3;
        } else if (sym == 17) {
            if (!s.bits(3, rep)) return false;
            rep += 3;
        } else {
            if (!s.bits(7, rep)) return false;
            rep += 11;
        }
        if (idx + int(rep) > nlen + ndist) { s.err = "code lengths run past the table"; return false; }
        while (rep--) l[idx++] = val;
    }
    if (l[256] == 0) { s.err = "no end-of-block code"; return false; }
    Huff lit, dist;
    int err = build(lit, l, nlen);
    if (err < 0 || (err > 0 && nlen != lit.count[0] + lit.count[1])) {
        s.err = "an over-subscribed or incomplete literal/length code";
        return false;
    }
    err = build(dist, l + nlen, ndist);
    if (err < 0 || (err > 0 && ndist != dist.count[0] + dist.count[1])) {
        s.err = "an over-subscribed or incomplete distance code";
        return false;
    }
    return codes(s, o, lit, dist);
}

} // namespace

std::uint32_t adler32(std::uint32_t a, const std::uint8_t* p, std::size_t n) {
    std::uint32_t s1 = a & 0xFFFF, s2 = a >> 16;
    while (n) {
        std::size_t k = n < 5552 ? n : 5552;
        n -= k;
        while (k--) { s1 += *p++; s2 += s1; }
        s1 %= 65521u;
        s2 %= 65521u;
    }
    return (s2 << 16) | s1;
}

bool zlibInflate(const std::uint8_t* src, std::size_t srcLen, std::vector<std::uint8_t>& out,
                 std::size_t maxOut, std::string* why) {
    const std::size_t start = out.size();
    auto refuse = [&](const char* reason) {
        out.resize(start);
        if (why) *why = reason;
        return false;
    };
    if (!src || srcLen < 6) return refuse("shorter than a zlib stream");
    const unsigned cmf = src[0], flg = src[1];
    if ((cmf & 0x0F) != 8 || (cmf >> 4) > 7) return refuse("not a DEFLATE zlib header");
    if ((cmf * 256 + flg) % 31 != 0) return refuse("a zlib header whose check bits fail");
    if (flg & 0x20) return refuse("a zlib stream that needs a preset dictionary");
    State s;
    s.src = src;
    s.len = srcLen;
    s.pos = 2;
    Out o(out, maxOut);
    for (;;) {
        std::uint32_t last = 0, type = 0;
        if (!s.bits(1, last) || !s.bits(2, type)) return refuse(s.err);
        bool ok;
        if (type == 0) ok = stored(s, o);
        else if (type == 1) ok = fixedBlock(s, o);
        else if (type == 2) ok = dynamicBlock(s, o);
        else return refuse("a reserved block type");
        if (!ok) return refuse(s.err ? s.err : o.err ? o.err : "a malformed block");
        if (last) break;
    }
    // the trailer: to the byte boundary, then four big-endian bytes
    s.bitbuf >>= (s.bitcnt & 7);
    s.bitcnt &= ~7;
    s.pos -= std::size_t(s.bitcnt / 8);
    if (s.len - s.pos < 4) return refuse("the stream ends before its adler32");
    const std::uint32_t want = (std::uint32_t(src[s.pos]) << 24) | (std::uint32_t(src[s.pos + 1]) << 16)
                             | (std::uint32_t(src[s.pos + 2]) << 8) | std::uint32_t(src[s.pos + 3]);
    out.resize(o.op);
    if (adler32(1, out.data() + start, o.op - start) != want)
        return refuse("an adler32 that does not match the output");
    return true;
}

} // namespace gen
