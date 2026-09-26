# Third-party components

The Titan Quest port of Unique Collection Tab is MIT (see `LICENSE`), as the Grim Dawn original.
This file lists everything else it uses and what that means for anyone who ships, forks or
repackages it.

| component | licence | how it is used |
| --- | --- | --- |
| MinHook | BSD 2-Clause | vendored source under `third_party\minhook`, compiled into `uniquetab.asi` |
| Ultimate ASI Loader | MIT | required at run time, linked to, **not** redistributed here |
| zlib / DEFLATE decoding | ours (MIT, with the mod) | `src\gen\inflate.cpp`, original code - no inflate library is vendored; see below |
| Titan Quest's own data | not ours | read at run time from the player's own installation; nothing derived from it ships - see below |

---

## MinHook

The detour library. Its sources are vendored under `third_party\minhook` and are compiled
directly into `uniquetab.asi`, so the binary is a derived work and carries the notice below.
Upstream: <https://github.com/TsudaKageyu/minhook>.

The text is `third_party\minhook\LICENSE.txt`, reproduced in full:

```
MinHook - The Minimalistic API Hooking Library for x64/x86
Copyright (C) 2009-2017 Tsuda Kageyu.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

================================================================================
Portions of this software are Copyright (c) 2008-2009, Vyacheslav Patkov.
================================================================================
Hacker Disassembler Engine 32 C
Copyright (c) 2008-2009, Vyacheslav Patkov.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

-------------------------------------------------------------------------------
Hacker Disassembler Engine 64 C
Copyright (c) 2008-2009, Vyacheslav Patkov.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

The authors listed by upstream in `third_party\minhook\AUTHORS.txt` are Tsuda Kageyu (creator and
maintainer), Michael Maltsev (the queue functions and many fixes) and Andrey Unis (the plain-C
rewrite of the hook engine).

Anyone who redistributes a build of this mod redistributes MinHook in binary form and must carry
the text above with it. Keeping this file beside the binary is enough.

## Ultimate ASI Loader

`uniquetab.asi` is an ASI plugin: it does nothing until a loader loads it. That loader is
Ultimate ASI Loader (MIT), by ThirteenAG: <https://github.com/ThirteenAG/Ultimate-ASI-Loader>.

It is **not** part of this package. Its licence would allow shipping it, but the player downloads
it themselves, once, for every ASI mod they use - and a loader that comes from upstream is a
loader they can update without waiting for this mod. `README.md` says which file to take and
where to put it.

Nothing in this repository is derived from Ultimate ASI Loader's source. The mod only relies on
its documented behaviour: a `.asi` file next to the host executable (or in `scripts\` /
`plugins\`) is `LoadLibrary`'d at start-up.

## zlib / DEFLATE decoding

Titan Quest stores its database records and archive parts as zlib streams, so the generator needs
an inflater. No inflate library is vendored: `src\gen\inflate.cpp` is an original, table-driven
RFC 1950 / RFC 1951 decoder written for this mod, under the mod's own licence. It is one function
(`gen::zlibInflate`); replacing its body with miniz's `tinfl` later touches no caller - vendor
`miniz.h` + `miniz.c` from the official release, record the version and its MIT notice here, and
route `zlibInflate` through `tinfl_decompress`.

The game's own `zlib1.dll` is never loaded: a mod must not bind itself to a game library it does
not own.

## Titan Quest

Titan Quest Anniversary Edition and its expansions are THQ Nordic's. No part of the game's program
is here: no executable, no library, no game archive, no icon.

**Everything the tab shows is made on the player's machine, out of the player's own copy.** On the
first launch, and again whenever the game's archives change, the mod reads the archives the player
already has (`Database\database.arz`, `Text\Text_<lang>.arc`, the item archives under
`Resources\`) and writes into its own folder:

- `catalogue.bin` and three record lists - the item names, icon sizes, classes and record paths the
  tab shows;
- `gray\`, a gray copy of every collectable item's icon (about 27 MB), painted from the game's own
  item textures, which the page shows for a record you do not own yet.

**The release package ships none of it.** It is `uniquetab.asi`, three documents and `extras\`;
the files above exist only on the machine of a player who owns the game, written from that
player's copy.

**`extras\tq-uniq-items-all.jsonl` is the mod's own data, not the game's.** It is a collection
file in the mod's own format: one line per collectable record, naming the record path the mod
stores for every deposit anyway, and numbers the mod chooses (a seed computed from the path,
zeros). No name, description, icon, statistic or other content of the game is in it. Its report,
`tq-uniq-items-all.report.txt`, adds counts per item class (the game's class identifiers, like the
paths). Both are written by `tools\make_all_items.py` from the record list and are committed
under `data\allitems\`.

**The development repository carries one copy of that derived data**, under `data\oracle\`, as
the fixtures the generator is compared against: `catalogue.bin` (the English display names of about
1,600 items and their record and icon paths), `uniq-records.txt`, `uniq-groups.txt` and
`uniq-excluded.txt` (record paths), `slotart.txt` (the equipment window's slot positions, read from
the game's UI records) and `catalogue-stats.md`. That is item text and record paths extracted from the game, kept so
the test suite can prove the generator's output. It is not in the package and not in the public
tree; `data\oracle\ORACLE.txt` says how to rebuild it from a copy of Titan Quest AE 2.10 before
running the catalogue harness.

Titan Quest is a trademark of its owner. This is an unofficial mod. It is not endorsed by, nor
affiliated with, THQ Nordic.
