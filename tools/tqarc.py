"""Reader for Titan Quest AE ARC v1 resource archives (Text_<lang>.arc, Items.arc, Item.arc ...).

GD's tools/arc.py with the Titan Quest deltas (checked byte-exact on Text_EN.arc and UI.arc):
  header 28 bytes '<7I': magic 'ARC\\0', version (TQ: 1; GD: 3), numFileEntries, numDataRecords,
                         recordTableSize, stringTableSize, recordTableOffset
  data-record table at recordTableOffset:  numDataRecords * '<3I' (partOffset, csize, dsize)
  string table       at recordTableOffset + recordTableSize (stringTableSize bytes, '\\0'-separated)
  file-entry table   at recordTableOffset + recordTableSize + stringTableSize:
      numFileEntries * '<5I Q 4I' = 44 bytes:
        entryType, fileOffset, compressedSize, decompressedSize, crc/hash,
        fileTime(u64), numParts, firstPartIndex, nameLength, nameOffset
  Each part is a zlib stream (GD: an LZ4 block); a part whose csize == dsize is stored raw.

A resource path names its own archive (GD stacks one archive name over four folders instead):
"[XPack<n>\\]<Arc>\\<rest>" is the entry <rest> of <game>\\Resources\\[XPack<n>\\]<Arc>.arc; an
"Items" path whose Items.arc is absent falls back to Item.arc (Eternal Embers ships Item.arc).

Text: <game>\\Text\\Text_<lang>.arc, one archive for the base game and every expansion. 53 of the
57 files in Text_EN.arc are UTF-16LE with a BOM, the other 4 single-byte.
"""

from __future__ import annotations

import os
import re
import struct
import zlib
from typing import Dict, Iterator, List, Optional

HEADER = struct.Struct("<7I")
DATAREC = struct.Struct("<3I")
FILEENT = struct.Struct("<IIIIIQIIII")
ARC_MAGIC = 0x00435241
MAX_PART_BYTES = 64 * 1024 * 1024


class ArcEntry:
    __slots__ = ("name", "etype", "offset", "csize", "dsize", "num_parts",
                 "first_part", "ftime")

    def __init__(self, name, etype, offset, csize, dsize, num_parts, first_part, ftime):
        self.name = name
        self.etype = etype
        self.offset = offset
        self.csize = csize
        self.dsize = dsize
        self.num_parts = num_parts
        self.first_part = first_part
        self.ftime = ftime

    def __repr__(self):
        return f"<ArcEntry {self.name} {self.dsize}B parts={self.num_parts}>"


class ArcArchive:
    def __init__(self, path: str, tag: Optional[str] = None):
        self.path = path
        self.tag = tag or os.path.splitext(os.path.basename(path))[0]
        # the tables only: an item archive is hundreds of MB and only a few headers are read
        self._fh = open(path, "rb")
        head = self._fh.read(28)
        (magic, self.version, n_files, n_recs,
         rec_size, str_size, rec_off) = HEADER.unpack_from(head, 0)
        if magic != ARC_MAGIC or self.version != 1:
            raise ValueError(f"{path}: not an ARC v1 archive (magic={magic:#x} ver={self.version})")
        self._fh.seek(rec_off)
        b = self._fh.read(rec_size + str_size + 44 * n_files)
        self._parts = [DATAREC.unpack_from(b, 12 * i) for i in range(n_recs)]
        str_off = rec_size
        ent_off = str_off + str_size
        self.entries: Dict[str, ArcEntry] = {}
        self._order: List[str] = []
        for i in range(n_files):
            (etype, foff, csize, dsize, _crc, ftime,
             nparts, first_part, nlen, noff) = FILEENT.unpack_from(b, ent_off + 44 * i)
            name = b[str_off + noff: str_off + noff + nlen].decode("latin-1")
            name = name.replace("\\", "/")
            e = ArcEntry(name, etype, foff, csize, dsize, nparts, first_part, ftime)
            self.entries[name.lower()] = e
            self._order.append(name.lower())

    def __len__(self) -> int:
        return len(self.entries)

    def __contains__(self, name: str) -> bool:
        return name.replace("\\", "/").lower() in self.entries

    def keys(self) -> Iterator[str]:
        return iter(self._order)

    def names(self) -> List[str]:
        return [self.entries[k].name for k in self._order]

    def _at(self, off: int, n: int) -> bytes:
        self._fh.seek(off)
        return self._fh.read(n)

    def read(self, name: str, limit: int = 0) -> Optional[bytes]:
        """The entry body; limit > 0 stops after the first part that reaches it (as the C++)."""
        e = self.entries.get(name.replace("\\", "/").lower())
        if e is None:
            return None
        if e.num_parts == 0:                      # stored whole, uncompressed
            want = limit if (limit and limit < e.dsize) else e.dsize
            return self._at(e.offset, want)
        out = bytearray()
        for i in range(e.first_part, e.first_part + e.num_parts):
            if limit and len(out) >= limit:
                break
            poff, pcs, pds = self._parts[i]
            chunk = self._at(poff, pcs)
            if pcs == pds:
                out += chunk
            else:
                if pds > MAX_PART_BYTES:
                    return None
                d = zlib.decompress(chunk)
                if len(d) != pds:
                    return None
                out += d
        return bytes(out)


_XPACK = re.compile(r"^xpack\d*$")


class ResourceArcs:
    """The item archives, opened on first use: a resource path names its archive.

    game_resources: a custom game's Resources folder ("" = the start-up game), searched FIRST,
    entry by entry (arc_reader.h ResourceArcs): the same names under it, then, for an expansion
    path, <res>/XPack<n>.arc whose entries keep the archive's name; the first that holds the entry
    wins, anything else is the base game's.

    A custom game's archive that does not load is skipped, never fatal: kept as absent, named once
    in `skipped`, and the lookup goes on to the next candidate and the base game's archive. A base
    game's archive that does not load raises (the installation itself is broken)."""

    def __init__(self, game_dir: str, game_resources: str = ""):
        self.root = os.path.join(game_dir, "Resources")
        self.game_root = game_resources
        self._arcs: Dict[str, Optional[ArcArchive]] = {}
        self.skipped: List[str] = []      # "<path>: <reason>", in the order they were met

    def _open(self, root: str, rel: str, ck: str):
        if ck not in self._arcs:
            full = os.path.join(root, rel)
            a = None
            if os.path.isfile(full):
                try:
                    a = ArcArchive(full, ck)
                except (OSError, ValueError, struct.error) as ex:
                    if not ck.startswith("game:"):
                        raise
                    self.skipped.append(skipped_archive_text(full, str(ex)))
            self._arcs[ck] = a
        return self._arcs[ck]

    def _archive_for(self, res_path: str):
        key = res_path.replace("\\", "/")
        low = key.lower()
        parts = low.split("/")
        orig = key.split("/")
        if len(parts) < 2:
            return None, ""
        if _XPACK.match(parts[0]):
            if len(parts) < 3:
                return None, ""
            folder, arc_name, inner = orig[0], orig[1], "/".join(parts[2:])
        else:
            folder, arc_name, inner = "", orig[0], "/".join(parts[1:])
        cands = [arc_name + ".arc"]
        if arc_name.lower() == "items":
            cands.append("Item.arc")
        if self.game_root:      # a custom game's archives first, entry by entry
            mine = [(os.path.join(folder, c) if folder else c, inner) for c in cands]
            if folder:
                mine.append((folder + ".arc", "/".join(parts[1:])))
            for rel, inn in mine:
                a = self._open(self.game_root, rel, "game:" + rel.replace("/", "\\").lower())
                if a is not None and inn in a:
                    return a, inn
        for c in cands:
            rel = os.path.join(folder, c) if folder else c
            a = self._open(self.root, rel, rel.replace("/", "\\").lower())
            if a is not None:
                return a, inner
        return None, inner

    def read(self, res_path: str, limit: int = 0) -> Optional[bytes]:
        a, inner = self._archive_for(res_path)
        return a.read(inner, limit) if a is not None else None

    def source_of(self, res_path: str) -> str:
        a, inner = self._archive_for(res_path)
        return a.tag if (a is not None and inner in a) else ""


def _cp1252_high() -> Dict[int, str]:
    out: Dict[int, str] = {}
    for b in range(0x80, 0xA0):
        try:
            out[b] = bytes([b]).decode("cp1252")
        except UnicodeDecodeError:
            pass                      # the five undefined bytes keep their own code point
    return out


_CP1252_HIGH = _cp1252_high()


def decode_cp1252(data: bytes) -> str:
    """TQ: a single-byte tag file is Windows-1252 (x2sidequest.txt holds 0x85 and 0x92), not
    latin-1; the five bytes cp1252 leaves undefined keep their own code point, as latin-1 does.
    src/gen/arc_reader.cpp cp1252ToUtf8 is the twin."""
    return data.decode("latin-1").translate(_CP1252_HIGH)


def parse_tag_file(data: bytes) -> Dict[str, str]:
    """`tagName=Text` lines; skips comments/blank lines. TQ: UTF-16LE when the file opens with
    its BOM (FF FE), else GD's rule - UTF-8 (a BOM dropped), falling back to single-byte text,
    which in TQ is cp1252."""
    if data[:2] == b"\xff\xfe":
        text = data[2:].decode("utf-16-le", "replace")
    else:
        try:
            text = data.decode("utf-8-sig")
        except UnicodeDecodeError:
            text = decode_cp1252(data)
    out: Dict[str, str] = {}
    for line in text.splitlines():
        if not line or line.startswith("//") or line.startswith("#") or "=" not in line:
            continue
        k, _, v = line.partition("=")
        k = k.strip()
        if k and not k.startswith("<"):
            out[k] = v
    return out


def skipped_archive_text(path: str, why: str) -> str:
    """"<path>: <reason>" for an archive that did not load (arc_reader.h skippedArchiveText): the
    reader's message already starts with the path when it names it."""
    return why if why.startswith(path) else path + ": " + why


def _merge_text_arc(path: str, tag: str, tags: Dict[str, str]) -> Dict[str, str]:
    arc = ArcArchive(path, tag)
    for key in arc.keys():
        if not key.endswith(".txt"):
            continue
        try:
            data = arc.read(key)
        except zlib.error:
            data = None
        if data is None:                  # as the C++: an entry that does not read fails the archive
            raise ValueError(f"{arc.path}: cannot read {arc.entries[key].name}")
        if data:
            tags.update(parse_tag_file(data))
    return tags


def load_text_tags(game_dir: str, lang: str = "EN", game_resources: str = "",
                   skipped: Optional[List[str]] = None) -> Dict[str, str]:
    """Merged tag -> text over every .txt of Text/Text_<lang>.arc (later files win); a language
    other than EN has Text_EN.arc merged first and Text_<lang>.arc over it, so a tag the
    translation lacks keeps its English text. Then a custom game's <Resources>/Text.arc the same
    way: a tag the game defines replaces the base one. A game's Text.arc that cannot be read is
    skipped whole ("<path>: <reason>" appended to `skipped`); a base text archive raises."""
    tags: Dict[str, str] = {}
    if lang.upper() != "EN":
        _merge_text_arc(os.path.join(game_dir, "Text", "Text_EN.arc"), "text", tags)
    _merge_text_arc(os.path.join(game_dir, "Text", f"Text_{lang}.arc"), "text", tags)
    mine = os.path.join(game_resources, "Text.arc") if game_resources else ""
    if mine and os.path.isfile(mine):
        try:
            game = _merge_text_arc(mine, "gametext", {})
        except (OSError, ValueError, struct.error, zlib.error) as ex:
            if skipped is not None:
                skipped.append(skipped_archive_text(mine, str(ex)))
        else:
            tags.update(game)
    return tags


if __name__ == "__main__":
    import sys

    import tqpath

    GAME = tqpath.game_dir()
    if len(sys.argv) > 1 and sys.argv[1] == "tags":
        t = load_text_tags(GAME)
        print(f"{len(t)} tags")
        for k in sys.argv[2:]:
            print(f"{k} = {t.get(k)!r}")
    else:
        rel = sys.argv[1] if len(sys.argv) > 1 else os.path.join("Text", "Text_EN.arc")
        a = ArcArchive(os.path.join(GAME, rel))
        print(f"{a.tag:9s} {len(a):7d} files  {a.path}")
        for k in list(a.keys())[:20]:
            print("  ", k)
