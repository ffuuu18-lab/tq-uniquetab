#!/usr/bin/env python3
"""sig_from_exe.py - cut a counted signature out of the on-disk TQ.exe (READ ONLY).

Usage: sig_from_exe.py <TQ.exe> <rva-hex> <len-hex>

Every dword the base-relocation table marks (an absolute VA the loader rebases) is wildcarded;
every other byte is fixed. The pattern is then counted over the executable sections: a usable
signature matches EXACTLY ONCE. Prints the C arrays (bytes + mask) and the count.
Nothing is written anywhere; the game file is opened read-only.
"""
import struct
import sys


def load(path):
    with open(path, 'rb') as f:
        raw = f.read()
    lfa = struct.unpack_from('<I', raw, 0x3C)[0]
    nsec = struct.unpack_from('<H', raw, lfa + 6)[0]
    opt = struct.unpack_from('<H', raw, lfa + 20)[0]
    reloc_rva, reloc_size = struct.unpack_from('<II', raw, lfa + 24 + 96 + 5 * 8)
    secs = []
    so = lfa + 24 + opt
    for i in range(nsec):
        o = so + 40 * i
        vsize, va, rsize, ptr = struct.unpack_from('<IIII', raw, o + 8)
        flags = struct.unpack_from('<I', raw, o + 36)[0]
        secs.append((va, min(vsize, rsize), ptr, flags))
    return raw, secs, reloc_rva, reloc_size


def rva_to_off(secs, rva):
    for va, size, ptr, _ in secs:
        if va <= rva < va + size:
            return ptr + rva - va
    raise ValueError('rva 0x%x not in the file' % rva)


def relocated(raw, secs, reloc_rva, reloc_size):
    out = set()
    off = rva_to_off(secs, reloc_rva)
    end = off + reloc_size
    while off + 8 <= end:
        page, size = struct.unpack_from('<II', raw, off)
        if size < 8:
            break
        for k in range((size - 8) // 2):
            e = struct.unpack_from('<H', raw, off + 8 + 2 * k)[0]
            if (e >> 12) == 3:  # IMAGE_REL_BASED_HIGHLOW
                out.add(page + (e & 0xFFF))
        off += size
    return out


def main():
    path, rva, n = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16)
    raw, secs, rr, rs = load(path)
    rel = relocated(raw, secs, rr, rs)
    o = rva_to_off(secs, rva)
    body = raw[o:o + n]
    mask = [1] * n
    for i in range(n):
        if rva + i in rel:
            for k in range(4):
                if i + k < n:
                    mask[i + k] = 0
    # count over every executable section
    hits = []
    for va, size, ptr, flags in secs:
        if not flags & 0x20000000:
            continue
        text = raw[ptr:ptr + size]
        first = body[0]
        pos = text.find(bytes([first]))
        while pos != -1 and pos + n <= len(text):
            if all((not mask[j]) or text[pos + j] == body[j] for j in range(n)):
                hits.append(va + pos)
            pos = text.find(bytes([first]), pos + 1)
    print('// TQ.exe rva 0x%X, %d bytes, %d relocated byte(s) wildcarded, %d match(es): %s' % (
        rva, n, mask.count(0), len(hits), ' '.join('0x%X' % h for h in hits[:4])))
    print('const unsigned char kBytes[] = {')
    for i in range(0, n, 13):
        print('    ' + ', '.join('0x%02X' % (b if mask[i + j] else 0) for j, b in enumerate(body[i:i + 13])) + ',')
    print('};')
    print('const unsigned char kMask[] = {')
    for i in range(0, n, 25):
        print('    ' + ', '.join(str(m) for m in mask[i:i + 25]) + ',')
    print('};')


if __name__ == '__main__':
    main()
