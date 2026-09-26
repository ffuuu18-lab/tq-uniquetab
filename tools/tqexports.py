#!/usr/bin/env python3
"""Dump the export tables of the game's own modules - the input of tools/gen_exports_header.py.

    python tools/tqexports.py [--out <folder>] [module ...]

The game folder is found by tqpath.py (%TQ_GAME_DIR%, else Steam's registry and library list) and
READ ONLY. Each module (default: Game.dll, Engine.dll, TQ.exe) is written to
<out>/exports_<stem>.txt (default out: build/exports, which is not tracked), one named export per
line, in the export name table's order:

    0x<rva, 8 hex digits> <ordinal, biased, left-aligned in 6> <decorated name>

The names are the decorated (mangled) C++ names exactly as the module exports them; that is what
GetProcAddress takes and what gen_exports_header.py checks every name of tq_exports.h against.
The PE reader is self-contained (the standard library only) and handles x86 and x64 images.

The two steps that regenerate src/tq_exports.h (HANDOFF.md section 5):

    python tools/tqexports.py
    python tools/gen_exports_header.py            (--check: exit 1 when the header is stale)
"""

import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import tqpath  # noqa: E402

DEFAULT_MODULES = ("Game.dll", "Engine.dll", "TQ.exe")
DEFAULT_OUT = os.path.join(ROOT, "build", "exports")


class PeError(Exception):
    pass


def read_exports(data):
    """[(name, biased ordinal, function rva)] in the export name table's order."""
    if len(data) < 0x40 or data[:2] != b"MZ":
        raise PeError("not an MZ image")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise PeError("no PE header")
    nsec, = struct.unpack_from("<H", data, pe + 6)
    optsize, = struct.unpack_from("<H", data, pe + 20)
    opt = pe + 24
    magic, = struct.unpack_from("<H", data, opt)
    if magic == 0x10B:
        ddir = opt + 96        # PE32
    elif magic == 0x20B:
        ddir = opt + 112       # PE32+
    else:
        raise PeError("unknown optional header magic 0x%X" % magic)
    exp_rva, exp_size = struct.unpack_from("<II", data, ddir)
    sections = []
    sec = opt + optsize
    for i in range(nsec):
        vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", data, sec + 40 * i + 8)
        sections.append((va, max(vsize, rawsize), rawptr))

    def r2o(rva):
        for va, size, rawptr in sections:
            if va <= rva < va + size:
                return rva - va + rawptr
        raise PeError("rva 0x%X is in no section" % rva)

    if not exp_rva:
        return []
    o = r2o(exp_rva)
    (_chars, _ts, _mj, _mn, _name, base, _nfunc, nnames,
     afuncs, anames, aords) = struct.unpack_from("<IIHHIIIIIII", data, o)
    of, on, oo = r2o(afuncs), r2o(anames), r2o(aords)
    out = []
    for i in range(nnames):
        no = r2o(struct.unpack_from("<I", data, on + 4 * i)[0])
        end = data.index(b"\0", no)
        name = data[no:end].decode("latin-1")
        idx = struct.unpack_from("<H", data, oo + 2 * i)[0]
        rva = struct.unpack_from("<I", data, of + 4 * idx)[0]
        out.append((name, idx + base, rva))
    return out


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--out", default=DEFAULT_OUT, help="the folder the dumps go to")
    ap.add_argument("modules", nargs="*", default=list(DEFAULT_MODULES))
    a = ap.parse_args(argv[1:])
    game = tqpath.game_dir()
    os.makedirs(a.out, exist_ok=True)
    for module in a.modules:
        path = os.path.join(game, module)
        try:
            with open(path, "rb") as fh:
                data = fh.read()
            rows = read_exports(data)
        except (OSError, PeError, struct.error, ValueError) as exc:
            sys.stderr.write("tqexports: %s: %s\n" % (module, exc))
            return 1
        stem = os.path.splitext(module)[0]
        dst = os.path.join(a.out, "exports_%s.txt" % stem)
        with open(dst, "w", encoding="latin-1", newline="\n") as fh:
            for name, ordinal, rva in rows:
                fh.write("0x%08x %-6d %s\n" % (rva, ordinal, name))
        print("tqexports: %-11s %6d named exports -> %s" % (module, len(rows),
                                                           os.path.relpath(dst, ROOT)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
