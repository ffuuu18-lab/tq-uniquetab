// inflate.h - the zlib decoder the Titan Quest archives need (TQ-only; GD's archives are LZ4).
//
// Titan Quest stores every ARZ record body and every ARC part as a zlib stream (RFC 1950: a
// two-byte header, a DEFLATE body per RFC 1951, a big-endian adler32 of the output). This is the
// one decoder the readers call. It is original code, not a vendored library: see
// THIRD_PARTY.md (zlib) - the interface is one function, so a vendored
// inflate (miniz's tinfl) can replace the body without touching a caller.
//
// FAIL CLOSED. Nothing here trusts a size it did not decode: the output grows as the stream
// produces it and is refused - never cut - the moment it would pass `maxOut`; a truncated
// stream, a malformed block, an over-subscribed or incomplete Huffman code, a distance that
// reaches before the start of this stream's output, a preset dictionary, and an adler32 that
// does not match are all refusals with the reason in `why`. Nothing throws (bar std::bad_alloc
// from the vector, which the generator's guard turns into an error string).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gen {

// Appends the decoded bytes of the zlib stream src[0..srcLen) to `out`. At most maxOut bytes
// are appended; a stream that would produce more is refused. On false, `out` is restored to the
// size it had and `why` (if given) says what was wrong. Bytes after the adler32 trailer are
// ignored, as Python's zlib.decompress ignores them.
bool zlibInflate(const std::uint8_t* src, std::size_t srcLen, std::vector<std::uint8_t>& out,
                 std::size_t maxOut, std::string* why = nullptr);

// adler32 (RFC 1950), continued from `a` (1 to start).
std::uint32_t adler32(std::uint32_t a, const std::uint8_t* p, std::size_t n);

} // namespace gen
