#!/usr/bin/env python3
"""Refuse to ship a tracked file that names a machine or the way the mod was developed - and, for a
release, a public tree or a zip that carries anything it should not.

This mod must work out of the folder it is installed in and must carry no trace of the machine it
was written on. release_check.py walks every tracked text file that ships and FAILS (exit 1) on:

    * an absolute Windows path        a drive letter followed by ":\\" or ":/"
    * "Users\\" or "Users/"           a user profile folder
    * "userdata\\<digits>"            a Steam account id
    * a UNC path, and this machine's name (%COMPUTERNAME%, read at run time)
    * the development process (PROCESS below): the name or the attribution of a test sitting, a
      process label (two letters and a number), a planning word, a path into the development
      tree's own folders, a date stamp or a clock time; the development's short labels (a
      capital and a number), its design document's name, and the investigation's own words and script names. A
      shipped comment says what the code does and why, or cites a section of HANDOFF.md.
    * every pattern listed, one regular expression per line, in the OPTIONAL and GITIGNORED file
      tools/release_check.local - which is where a personal token (a user name, an account id,
      the name of the folder the mod was developed in, the development repositories' names)
      belongs, so that checking for it never puts it into the tracked tree itself.
    * the version spelled anywhere but src/ut_version.h: UT_VERSION is defined there once, and
      no other source or tool file repeats its value as a string.

--root <folder> checks a public tree instead of the tracked files: every file under the folder
(the .git folder aside) is scanned as above, AND its file list must be on the allow-list below -
nothing from the development tree's own folders (the design notes, the engine investigation) or
the build outputs (bin, build, out), no .log, none of the development-only tools, and of
data\\oracle\\ only ORACLE.txt.

--zip <file.zip> checks the player package: exactly scripts/uniquetab.asi, README.md, LICENSE and
THIRD_PARTY.md at its root plus the two extras (extras/tq-uniq-items-all.jsonl, the all-items
collection file, and its report), named UniqueCollectionTab-TQ-<UT_VERSION>.zip, the .asi a 32-bit
(i386) DLL whose strings name no machine, the documents and the extras scanned as above, and the
collection file read the way the mod reads it: its header names this mod's journal, format 1,
nothing pending, and every row carries the identity keys with "record" the folded "base".

--strict additionally REPORTS, without failing, the history words a comment should not need:
stage numbers, review and checkpoint references, "lane". --list prints every one of those hits as
file:line (it implies --strict). The PROCESS rules are fatal in every mode.

This script ships with the sources; tools/publish.ps1, which writes the public tree, does not.

    python tools/release_check.py                          before every commit; must print CLEAN
    python tools/release_check.py --root <public tree>     the exported tree
    python tools/release_check.py --zip <zip>              the package (combine with --root)
    python tools/release_check.py --strict / --list        plus the history tokens
    python tools/release_check.py --print-public           the tracked files that ship, one per line

Exit codes: 0 clean, 1 something forbidden was found, 2 the check could not run.
"""

import fnmatch
import json
import os
import re
import struct
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
LOCAL = os.path.join(HERE, "release_check.local")

# (name, compiled pattern). A hit in any tracked text file fails the run.
FORBIDDEN = [
    # A drive letter, but not the "p:" of "http://": the character before it must not be a letter
    # or a digit. Both separators, because either spelling is a path out of the game folder.
    ("absolute Windows path", re.compile(r"(?<![A-Za-z0-9])[A-Za-z]:[\\/]")),
    ("a user profile folder", re.compile(r"Users[\\/]")),
    ("a Steam account id", re.compile(r"userdata[\\/]\d")),
    # A UNC path (\\host\share), as text or C-escaped ("\\\\host\\share"). The host is followed by ONE
    # separator, so a C string's escaped path ("<library>\\steamapps\\common") is not one, and the
    # device prefixes "\\?\" and "\\.\" do not start with a letter.
    ("a UNC path", re.compile(r"(?<![\\\w])(?:\\\\[A-Za-z][\w.-]*\\(?=[\w$])|\\\\\\\\[A-Za-z][\w.-]*\\\\(?=[\w$]))")),
]


# The development process - FATAL, like the rules above. Each pattern is written so that this file
# does not spell the token it looks for (the file ships, and a plain search of the public tree must
# find none of them). The .gitignore is read as it ships: without its development-tree block.
PROCESS = [
    ("a test sitting's name", re.compile(r"u[s]er[ -]?test", re.IGNORECASE)),
    ("a test sitting's attribution", re.compile(
        r"\bthe u[s]er(?:'s\b|\s+(?:saw|logs?|asked|requests?|wanted)\b|\s*:)|\bthe scree[n] ?shots?\b",
        re.IGNORECASE)),
    ("a process label", re.compile(r"[Ww][Pp][ -]?[0-9]")),
    ("a planning word", re.compile(
        r"work[ ]packages?|deliverabl[e]|the[ ]brief|baseline[ ]commit|deviation[ ]table",
        re.IGNORECASE)),
    # The development repositories' names are NOT here: they are private, so they are patterns in
    # the gitignored tools/release_check.local (tools/publish.ps1 refuses to run without them).
    # Any case for the folders; "docs/" only before a capital, the lowercase one being a URL path.
    ("a development-tree path", re.compile(
        r"(?i:(?<![A-Za-z_])(?:researc[h]|prob[e])[\\/]|\bdoc[s]\\)|\bdoc[s]/[A-Z]|\bDoc[s]/")),
    ("a date stamp", re.compile(
        r"\b20[2-3][0-9][01][0-9][0-3][0-9]\b|\b20[2-3][0-9]-[01][0-9]-[0-3][0-9]\b")),
    ("a clock time", re.compile(
        r"\b[0-2][0-9]:[0-5][0-9]:[0-5][0-9]\b|\b[0-2][0-9]\.[0-5][0-9]\.[0-5][0-9]\b")),
    # The development's short labels: its runs, decisions, invariants and reports
    # (P, D, N or V and one or two digits) and the numbered lines of its first run's log (L and one
    # or two digits), whole words, on every line (code included: a label is no better as a name).
    # ONE exception, the only one the shipped files need: an x86 byte in a byte listing - "FF D7",
    # "8B D0" - is a byte right after another byte and a space, and passes. Only a D and ONE digit
    # can be a byte (P, N and V are no hex digits), and only right after a whole two-hex-digit
    # token (a longer hex word before it does not count). The binary's strings are not held to
    # these (see check_zip).
    ("a short process label", re.compile(
        r"\b[PNV][0-9]{1,2}\b|\bD[0-9]{2}\b|(?<!\b[0-9A-F]{2} )\bD[0-9]\b")),
    ("a log line label", re.compile(r"\bL[0-9]{1,2}\b")),
    # The design document's name in capitals, and a report item (a V, a digit, a dash, a digit).
    ("a design document's name", re.compile(r"\bPLA[N]\b")),
    ("a report item", re.compile(r"\bV[0-9]-[0-9]\b")),
    # The numbered items of the development's own lists (an H, M or Q and one or two digits: no
    # shipped file defines one) and the name of its list of open questions.
    ("an item label", re.compile(r"\b[HMQ][0-9]{1,2}\b")),
    ("a development document's name", re.compile(r"OPEN-QUESTION[S]")),
    # The investigation itself, in any case and any form (the word is the process, whatever it
    # is "of"), and the run that measured the engine, in any form of the word.
    ("the investigation's word", re.compile(r"researc[h]", re.IGNORECASE)),
    ("the investigation's run", re.compile(r"\bprob(?:e|es|ed|ing)\b", re.IGNORECASE)),
    # Its scripts' names (a, b or v, an optional digit, an underscore - no shipped tool is named
    # so) and their dumps' prefix.
    ("an investigation script", re.compile(r"(?<![A-Za-z0-9_])[abv][0-9]?_[a-z0-9_]+\.py|\ba_ex[p]_")),
]
# The rules for comments only: never applied to the binary's strings.
COMMENT_ONLY = ("a clock time", "a short process label", "a log line label")
# The markers of the .gitignore block tools/publish.ps1 drops.
DEV_BLOCK_START = "# ---- development tree only"
DEV_BLOCK_END = "# ---- end of the development tree block"


def host_rule():
    """This machine's name (%COMPUTERNAME%), read at run time so no file has to spell it."""
    host = os.environ.get("COMPUTERNAME", "").strip()
    if len(host) < 3:
        return []
    return [("this machine's name",
             re.compile(r"(?<![A-Za-z0-9])" + re.escape(host) + r"(?![A-Za-z0-9])", re.IGNORECASE))]

# Reported by --strict, never fatal (yet).
HISTORY = [
    ("stage number", re.compile(r"stage [0-9]+", re.IGNORECASE)),
    ("checkpoint", re.compile(r"checkpoint", re.IGNORECASE)),
    ("lane", re.compile(r"\blanes?\b", re.IGNORECASE)),
    ("review", re.compile(r"review", re.IGNORECASE)),
]

# The ONE allowed absolute path: none is spelled out today, so this list is empty. If a comment
# ever has to show what the Steam registry lookup returns, add "<path>:<line>" of that comment
# here and say why - never widen the pattern itself.
ALLOWED = set()

BINARY_EXT = {
    ".arz", ".arc", ".bin", ".tex", ".png", ".jpg", ".jpeg", ".gif", ".dds", ".ico",
    ".zip", ".gds", ".gst", ".gsh", ".dbr", ".pdb", ".dll", ".exe", ".obj", ".lib", ".asi",
    ".chr", ".dxb", ".dxg",
}

# The public tree: every file must match one of these (fnmatch, forward slashes; "*" crosses
# folders) and none of DENY. tools/publish.ps1 copies by the same list.
PUBLIC_ALLOW = [
    ".gitattributes", ".gitignore",
    "CHANGELOG.md", "HANDOFF.md", "LICENSE", "README.md", "THIRD_PARTY.md",
    "build.bat", "build_model.bat", "deploy.bat", "undeploy.bat",
    "data/oracle/ORACLE.txt",
    "src/*", "tests/model_test.cpp", "third_party/*", "tools/*",
    "data/allitems/tq-uniq-items-all.jsonl", "data/allitems/tq-uniq-items-all.report.txt",
]
# Named files that ship although a DENY pattern matches them: the all-items collection file is the
# mod's own data (record paths and seeds, written by tools/make_all_items.py), unlike any other
# .jsonl (a player's collection never ships).
PUBLIC_KEEP = {"data/allitems/tq-uniq-items-all.jsonl"}
# The development tree's own folders and files; the names are split so that this file spells
# neither word.
PUBLIC_DENY = [d + "/*" for d in ("docs", "rese" "arch", "pro" "be", "bin", "build", "out")] + [
    "*.log", "*.pdb", "*.asi", "*.obj", "*.jsonl", "*/__pycache__/*",
    "tools/release_check.local", "tools/publish.ps1", "tools/test_" "pro" "be_bindings.py",
]
# Every one of these must be present in a public tree.
PUBLIC_REQUIRED = [
    "CHANGELOG.md", "HANDOFF.md", "LICENSE", "README.md", "THIRD_PARTY.md", "build.bat",
    "build_model.bat", "deploy.bat", "undeploy.bat", "src/ut_version.h", "tools/package.ps1",
    "tools/release_check.py", "data/oracle/ORACLE.txt", "third_party/minhook/LICENSE.txt",
    "tools/tqexports.py", "tools/gen_exports_header.py", "src/tq_exports.h",
]

ZIP_EXTRAS = ("extras/tq-uniq-items-all.jsonl", "extras/tq-uniq-items-all.report.txt")
ZIP_ENTRIES = {"scripts/uniquetab.asi", "README.md", "LICENSE", "THIRD_PARTY.md"} | set(ZIP_EXTRAS)
ID_KEYS = ("record", "deposited", "stack", "base", "prefix", "suffix", "relic", "relicBonus",
           "relic2", "relicBonus2", "seed", "var1", "var2", "b8")


def check_collection(where, lines, failures):
    """The all-items file as the mod reads it (ut_rescue.cpp): the header, then one row per copy,
    each with every identity key and "record" the folded "base", none pending."""
    rows = [ln for ln in lines if ln.strip()]
    try:
        head = json.loads(rows[0]) if rows else {}
    except ValueError:
        head = {}
    if not isinstance(head, dict) or head.get("journal") != "titan quest uniquetab" or \
            head.get("format") != 1:
        failures.append((where + ":1", "not this mod's format-1 journal", str(rows[:1])[:80], ""))
        return
    for n, ln in enumerate(rows[1:], 2):
        try:
            row = json.loads(ln)
        except ValueError:
            row = None
        bad = not isinstance(row, dict) or any(k not in row for k in ID_KEYS)
        if not bad:
            bad = row["record"] != row["base"].replace("\\", "/").lower() or "pending" in row
        if bad:
            failures.append(("%s:%d" % (where, n), "a row the mod would drop or leave pending",
                             ln[:80], ""))
            return
    want = len(rows) - 1
    got = tuple(head.get(k) for k in ("entries", "collected", "pendingIn", "pendingOut"))
    if want < 1 or got != (want, want, 0, 0):
        failures.append((where + ":1", "the header does not say %d rows, all collected" % want,
                         rows[0][:120], ""))


def tracked_files(scan_root=None):
    """The git-tracked files of the tree, or - with --root - every file under that folder
    except its .git folder (a public tree about to be committed is checked as it lies on disk)."""
    if scan_root:
        found = []
        for base, dirs, names in os.walk(scan_root):
            dirs[:] = [d for d in dirs if d != ".git"]
            for n in names:
                full = os.path.join(base, n)
                found.append(os.path.relpath(full, scan_root).replace("\\", "/"))
        return sorted(found)
    try:
        out = subprocess.run(
            ["git", "-C", ROOT, "ls-files", "-z"],
            check=True, stdout=subprocess.PIPE,
        ).stdout
    except (OSError, subprocess.CalledProcessError) as exc:
        sys.exit("release_check: cannot list the tracked files (%s)" % exc)
    return [p for p in out.decode("utf-8", "replace").split("\0") if p]


def read_text(path):
    """The file's lines, or None when it is binary or unreadable."""
    if os.path.splitext(path)[1].lower() in BINARY_EXT:
        return None
    try:
        with open(path, "rb") as fh:
            raw = fh.read()
    except OSError:
        return None
    if b"\0" in raw:
        return None
    return raw.decode("utf-8", "replace").splitlines()


def as_shipped(rel, lines):
    """The text as the public tree carries it: the .gitignore without its development-tree block
    (tools/publish.ps1 drops it). The dropped lines become empty, so the line numbers stay."""
    if lines is None or rel != ".gitignore":
        return lines
    out, skip = [], False
    for line in lines:
        if line.startswith(DEV_BLOCK_START):
            skip = True
            out.append("")
        elif line.startswith(DEV_BLOCK_END):
            skip = False
            out.append("")
        else:
            out.append("" if skip else line)
    return out


def load_local():
    """The gitignored extra patterns. One regular expression per line; # starts a comment."""
    if not os.path.exists(LOCAL):
        return [], False
    pats = []
    with open(LOCAL, "r", encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            try:
                pats.append(("a local pattern", re.compile(line, re.IGNORECASE)))
            except re.error as exc:
                sys.exit("release_check: %s is not a regular expression (%s)" % (line, exc))
    return pats, True


def read_version(base):
    """UT_VERSION out of <base>/src/ut_version.h, or None."""
    try:
        with open(os.path.join(base, "src", "ut_version.h"), "r", encoding="utf-8") as fh:
            m = re.search(r'#define\s+UT_VERSION\s+"([^"]+)"', fh.read())
    except OSError:
        return None
    return m.group(1) if m else None


def matches(rel, pats):
    return any(fnmatch.fnmatchcase(rel, p) for p in pats)


def ships(rel):
    """On the public allow-list and off the deny list (PUBLIC_KEEP names the exceptions)."""
    return matches(rel, PUBLIC_ALLOW) and (rel in PUBLIC_KEEP or not matches(rel, PUBLIC_DENY))


def check_public_list(files, failures):
    """The public tree's file list against the allow-list."""
    have = set(files)
    for rel in files:
        if not ships(rel):
            failures.append((rel, "not on the public allow-list", rel, rel))
    for rel in PUBLIC_REQUIRED:
        if rel not in have:
            failures.append((rel, "missing from the public tree", rel, rel))


def check_version(base, files, version, failures):
    """UT_VERSION is defined once, in src/ut_version.h, and its value is spelled nowhere else in
    the code (the documents may name the release)."""
    if not version:
        failures.append(("src/ut_version.h", "UT_VERSION not found", "", ""))
        return
    lit = '"%s"' % version
    for rel in files:
        if not (rel.startswith("src/") or rel.startswith("tools/") or rel.startswith("tests/")):
            continue
        if rel in ("src/ut_version.h", "tools/release_check.py"):
            continue
        lines = read_text(os.path.join(base, rel))
        if lines is None:
            continue
        for n, line in enumerate(lines, 1):
            if re.search(r"#define\s+UT_VERSION\b", line) or lit in line:
                failures.append(("%s:%d" % (rel, n), "the version outside src/ut_version.h",
                                 version, line.strip()[:120]))


def pe_machine(blob):
    """The PE machine word and whether the image is a DLL, or (None, False)."""
    if len(blob) < 0x40 or blob[:2] != b"MZ":
        return None, False
    pe = struct.unpack_from("<I", blob, 0x3C)[0]
    if pe + 24 > len(blob) or blob[pe:pe + 4] != b"PE\0\0":
        return None, False
    machine, = struct.unpack_from("<H", blob, pe + 4)
    chars, = struct.unpack_from("<H", blob, pe + 22)
    return machine, bool(chars & 0x2000)


def check_zip(path, version, rules, failures):
    """The package: its name, its entries, the .asi's shape and strings, the documents' text."""
    name = os.path.basename(path)
    want = "UniqueCollectionTab-TQ-%s.zip" % version
    if name != want:
        failures.append((name, "the zip is not named " + want, name, name))
    try:
        zf = zipfile.ZipFile(path)
    except (OSError, zipfile.BadZipFile) as exc:
        failures.append((name, "not a readable zip", str(exc), ""))
        return []
    listing = []
    with zf:
        entries = {}
        for info in zf.infolist():
            if info.is_dir():
                continue
            entries[info.filename.replace("\\", "/")] = info
            listing.append((info.filename.replace("\\", "/"), info.file_size))
        got = set(entries)
        for extra in sorted(got - ZIP_ENTRIES):
            failures.append((name + ":" + extra, "not a package file", extra, extra))
        for missing in sorted(ZIP_ENTRIES - got):
            failures.append((name + ":" + missing, "missing from the package", missing, missing))
        if "scripts/uniquetab.asi" in entries:
            blob = zf.read(entries["scripts/uniquetab.asi"])
            machine, is_dll = pe_machine(blob)
            if machine != 0x14C or not is_dll:
                failures.append((name + ":scripts/uniquetab.asi", "not a 32-bit (i386) DLL",
                                 "machine=%s dll=%s" % (machine, is_dll), ""))
            # In a binary the bare "X:\" rule matches instruction bytes (":C:Q:\:"), so a drive
            # letter counts only as the start of a real path: at least two components. A clock
            # time and the short labels are comment rules; the binary's strings are not
            # comments (a run of instruction bytes can spell such a label).
            bin_rules = [(n, p) for n, p in rules
                         if n != "absolute Windows path" and n not in COMMENT_ONLY]
            bin_rules.append(("absolute Windows path",
                              re.compile(r"(?<![A-Za-z0-9])[A-Za-z]:[\\/][A-Za-z0-9_ .()-]{2,}[\\/]")))
            # The mod's own paths are wchar_t: scan the UTF-16LE runs as well as the ASCII ones.
            runs = [r.decode("ascii") for r in re.findall(rb"[\x20-\x7e]{5,}", blob)]
            runs += [r.decode("utf-16le") for r in re.findall(rb"(?:[\x20-\x7e]\x00){5,}", blob)]
            for text in runs:
                for rname, pat in bin_rules:
                    m = pat.search(text)
                    if m:
                        failures.append((name + ":scripts/uniquetab.asi", rname, m.group(0),
                                         text[:120]))
        if ZIP_EXTRAS[0] in entries:
            check_collection(name + ":" + ZIP_EXTRAS[0],
                             zf.read(entries[ZIP_EXTRAS[0]]).decode("utf-8", "replace").splitlines(),
                             failures)
        for doc in ("README.md", "LICENSE", "THIRD_PARTY.md") + ZIP_EXTRAS:
            if doc not in entries:
                continue
            lines = zf.read(entries[doc]).decode("utf-8", "replace").splitlines()
            for n, line in enumerate(lines, 1):
                for rname, pat in rules:
                    m = pat.search(line)
                    if m:
                        failures.append(("%s:%s:%d" % (name, doc, n), rname, m.group(0),
                                         line.strip()[:120]))
    return sorted(listing)


def usage():
    sys.stderr.write("usage: release_check.py [--strict] [--list] [--root <folder>] [--zip <file>]\n")
    return 2


def main(argv):
    args = list(argv[1:])
    scan_root = None
    zip_path = None
    for flag in ("--root", "--zip"):
        if flag in args:
            i = args.index(flag)
            if i + 1 >= len(args):
                return usage()
            val = os.path.abspath(args[i + 1])
            del args[i:i + 2]
            if flag == "--root":
                scan_root = val
            else:
                zip_path = val
    if "--print-public" in args:
        # The allow-listed tracked files, one per line: tools/publish.ps1 copies exactly these.
        for rel in tracked_files(None):
            if ships(rel):
                print(rel)
        return 0
    listing = "--list" in args
    strict = listing or "--strict" in args
    for arg in args:
        if arg not in ("--strict", "--list"):
            return usage()
    if scan_root and not os.path.isdir(scan_root):
        sys.stderr.write("release_check: %s is not a folder\n" % scan_root)
        return 2

    local, have_local = load_local()
    rules = FORBIDDEN + PROCESS + host_rule() + local
    base = scan_root or ROOT

    failures = []
    counts = {name: 0 for name, _ in HISTORY}
    examples = {}
    hits_by_line = []      # (where, [category names], line text) - filled only by --list
    scanned = 0

    files = tracked_files(scan_root)
    private = 0
    if not scan_root:
        # The development tree also tracks its private folders (the design notes, the engine
        # investigation); only what
        # would ship - the files on the public allow-list - is held to the rules.
        shipped = [f for f in files if ships(f)]
        private = len(files) - len(shipped)
        files = shipped
    for rel in files:
        # Two files are skipped. The local pattern file is gitignored, so it is never in the
        # tracked list at all - it is skipped defensively in case someone adds it by hand. This
        # script itself necessarily spells out every pattern it looks for, so scanning it would
        # be a guaranteed false positive; keep it free of anything that is not a pattern.
        slug = rel.replace("\\", "/")
        if slug in ("tools/release_check.local", "tools/release_check.py"):
            continue
        lines = as_shipped(slug, read_text(os.path.join(base, rel)))
        if lines is None:
            continue
        scanned += 1
        for n, line in enumerate(lines, 1):
            where = "%s:%d" % (rel, n)
            for name, pat in rules:
                m = pat.search(line)
                if m and where not in ALLOWED:
                    failures.append((where, name, m.group(0), line.strip()[:120]))
            if strict:
                names_here = []
                for name, pat in HISTORY:
                    hits = pat.findall(line)
                    if hits:
                        counts[name] += len(hits)
                        examples.setdefault(name, where)
                        names_here.append(name)
                if listing and names_here:
                    hits_by_line.append((where, names_here, line.strip()[:160]))

    version = read_version(base)
    check_version(base, files, version, failures)
    if scan_root:
        check_public_list(files, failures)
    zip_listing = check_zip(zip_path, version, rules, failures) if zip_path else []

    print("release_check: %d %s text files scanned "
          "(this script and tools/release_check.local are skipped - they hold the patterns)"
          % (scanned, "public-tree" if scan_root else "tracked"))
    if private:
        print("release_check: %d tracked file(s) off the public allow-list not scanned "
              "(the development tree's own folders)" % private)
    print("release_check: tools/release_check.local %s"
          % ("loaded, %d extra pattern(s)" % len(local) if have_local else "not present"))
    print("release_check: UT_VERSION %s, defined in src/ut_version.h" % (version or "NOT FOUND"))
    if scan_root:
        print("release_check: public tree %s - %d files against the allow-list" % (scan_root, len(files)))
    if zip_path:
        print("release_check: package %s" % zip_path)
        for entry, size in zip_listing:
            print("  %-40s %10d" % (entry, size))

    if strict:
        print("\nhistory tokens (reported, not fatal):")
        for name, _ in HISTORY:
            n = counts[name]
            print("  %-14s %6d%s" % (name, n, ("   e.g. " + examples[name]) if n else ""))

    if listing:
        print("\nhistory hits (%d lines):" % len(hits_by_line))
        for where, names, line in hits_by_line:
            print("  %s: %s" % (where, ", ".join(names)))
            print("      %s" % line)

    if failures:
        print("\nFORBIDDEN (%d):" % len(failures))
        for where, name, hit, line in failures:
            print("  %s: %s -> %r" % (where, name, hit))
            if line:
                print("      %s" % line)
        print("\nrelease_check: FAILED - %d hit(s)" % len(failures))
        return 1

    print("\nrelease_check: CLEAN")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
