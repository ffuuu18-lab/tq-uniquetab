# HANDOFF - for whoever maintains this next

Written for a maintainer, human or machine, who has never seen this code. It describes what is
here, why it is shaped this way, and the handful of rules that must survive any change. Read
sections 1 and 2 before you touch anything.

This is the Titan Quest Anniversary Edition port of the Grim Dawn mod *Unique Collection Tab*. The
port kept the Grim Dawn code wherever the game allowed it - the logger, the settings table, the
bindings registry, the journal's reader contract, the pad, the tooltip line, the harness - and
changed only what Titan Quest forced. Where a file says "GD's" or "TQ port of GD's", that is what
it means.

The mod is C++17, MSVC, built by `build.bat` into `bin\uniquetab.asi` - a plain 32-bit DLL with the
extension Ultimate ASI Loader looks for. It exports nothing. `README.md` is the player's document;
this one is the code's. This text describes version 1.0.0, the first release (the item search is
section 4b); the version is written in one place, `src\ut_version.h`, which the start-up banner and
`tools\package.ps1` both read.

---

## 1. The one rule

**An item must never be lost or duplicated.** Everything below is downstream of that.

In Titan Quest the danger has two faces:

- **A prototype must never reach a save.** The collection page is the caravan's Transfer page
  with a different sack behind it: while the view is on, the page's accessor is handed the MOD
  sack, filled with display prototypes - one real engine `Item` per catalogue record of the page.
  If the engine ever streamed that sack into `winsys.dxb`, or a prototype reached a real sack, the
  player would own items that were never found. `src\ut_view.h` lists the guards (G1..G8) that
  stand in front of it; the single OFF path, `viewForceOff`, re-points the page at the real
  Transfer sack and destroys every prototype.
- **The journal is the authority.** `tq-uniq-items*.jsonl` holds every deposited item with its
  full identity. A deposit's row is written, atomically, before the engine consumes the original;
  a take's row before the prototype reaches the cursor. A move that cannot be recorded is refused
  before anything moves.

## 2. Invariants

These are release blockers, not preferences.

1. **The mod writes only its own files**, all inside its own folder (`scripts\uniquetab\` beside
   the `.asi`, or the Documents fallback): `uniquetab.ini`, `uniquetab.log` (+3 archives), the
   journals `tq-uniq-items*.jsonl` and their `.csv` copies, a `.bad-<stamp>` copy of a damaged
   journal, `catalogue.bin`, `catalogue.stamp`, `uniq-records.txt`, `uniq-groups.txt`,
   `uniq-excluded.txt` and the gray icons under `gray\`. Never `Player.chr`, never `winsys.dxb` or
   any caravan file, never anything in the game folder proper.
2. **Never lose or duplicate an item.** A refusal is a true no-op: the item stays where it was.
3. **One journal per save set.** The main campaign, custom quests and each mod have their own
   characters and their own Transfer, and each has its own journal (`utSaveSetLeaf` in
   `ut_depositgate.h`). When the set cannot be read, the mod refuses rather than guessing.
4. **All or nothing on the bindings.** If one early binding cannot be found or confirmed, nothing
   at all is installed: no detour, no page, no deposit. See section 5.
5. **Refuse when unsure.** An unknown multiplayer state refuses every move whatever `mp_collect`
   says; an unreadable journal is read-only, never empty; a reconciliation pass that could not read
   every container proves nothing.
6. **Nothing generated is shipped.** `catalogue.bin`, `catalogue.stamp`, the three `uniq-*.txt`
   lists and the gray icons are built on the player's machine. The package is `uniquetab.asi`,
   three documents (`README.md`, `LICENSE`, `THIRD_PARTY.md`) and `extras\` - the all-items
   collection file, which is the mod's own data (record paths and seeds, section 7). Their
   reference copies belong in `data\oracle\` as test fixtures, which neither the package nor the
   public tree carries. `CHANGELOG.md` is the other way round: the public tree must carry it, and
   the zip leaves it out on purpose (section 9).
7. **Nothing in the tree names a machine.** No absolute path, no user profile folder, no account
   id, in code, comments, scripts or documents; the mod finds everything relative to the game
   folder and the plugin's own location. `tools\release_check.py` enforces it.
8. **The search only reads, and a doubt turns it off.** Its own code only reads engine memory,
   and every read of engine memory happens inside an SEH frame that wraps one engine call, one run
   of pure reads, or one of the real page's two bounded walks (section 4b: `GetInventory` and the
   walk of the sack's map; `GetObjectList` and the resolve of the page's ids over it) - never a mod
   lock, never a log line (the tooltip's rule, for the tooltip's reason: a lock taken inside a
   frame is not released by the unwinding). A fault - the text call,
   the line read, a vector that does not read as one, the free, a failed destroy, a key event that
   cannot be read, the real page's walk - is reported after the frame has been left: one ERROR line
   (`search: OFF ***** <where> faulted ...`), the search off for the session, never retried; the
   pages are then exactly what they are with `search=0`. A line vector that does not read as one
   is left as it is: nothing is freed on a doubt. A throw-away item lives inside one call and is
   never in a sack; a real item is never changed, moved or destroyed.
9. **A capture is not a rollover.** The search runs the item's own rollover builder, whose tooltip
   detours would otherwise clear or set the "In your collection" latch and take the journal lock.
   The thread-local capture flag (`tooltipSearchCapture` in `ut_tooltip.cpp`) is set around every
   capture, and `latchBody` returns on it before anything else.
10. **The key gate takes presses, and only with the focus.** `searchKeyGate` returns true - the
    event is swallowed and the engine never sees it - only while the field has the focus, and
    then for every event but a release (a press, or a repeat were the engine ever to send one).
    Every release goes on to the game, so the engine's held-key bookkeeping stays right, as with
    the game's own edit box. Unfocused, the gate is one interlocked read that returns false.
11. **`search=0` is inert.** Nothing is built, no field is laid out, no mark is drawn, and
    `hooksInstall()` installs no key gate at all: the keyboard is never touched. The gate is
    installed only when `search=1` at start.
12. **The real Transfer page is read only while it is searched.** Its items are read only while
    the view is OFF, the caravan open, the Transfer page shown, `search=1` and `search_transfer=1`,
    with a query standing or the field focused; otherwise nothing is read and the list the draw
    marks is empty. The item pointers are held only while the sack's id set stands (compared every
    Update before any read) and are dropped with the world or a change of the main player.

## 3. The architecture, by file

### Start-up order (`src\dllmain.cpp`)

`DllMain` does the minimum: resolve the mod folder, open the log, install the fault watchdog (a
vectored handler that only *logs* an access violation with this DLL on the stack and always
returns `EXCEPTION_CONTINUE_SEARCH`), start one worker thread, return. Nothing that can block runs
under the loader lock.

The worker then does, in this order:

```
configReload(ini)      log_level must be in force before the export lines are written
waitForGameModules()   Engine.dll + Game.dll, 60 s ceiling
resolveExports()       by decorated name; a missing REQUIRED symbol turns the mod off
generateEnsure()       catalogue.bin, the three lists and gray\, current with the game's archives
decodeBindings()       the DECODED and SIGNATURE rows, before the gate
liveInit()             the group table (uniq-groups.txt), packed into 16 x 15 pages
storeInit()            the journal folder; the save set is chosen at the first world
bindingsGate()         <- ALL OR NOTHING. A failure here returns before any hook exists
tooltipInit()          the "In your collection" line; a miss turns the line off, never the mod
hooksInstall()         MinHook; the search field's key gate only with search=1
viewInit()             one line: "view: available" or "view: unavailable - <reason>"
READY
```

then a once-a-second loop: re-read the ini, flush the log, the journal service, a heartbeat, and
a frames-not-advancing stall check.

### The files

| file | what it owns |
| --- | --- |
| `dllmain.cpp` | entry point, the worker thread, the fault watchdog, the heartbeat and stall check |
| `hooks.cpp/.h` | MinHook: every detour, installed after the gate - the search field's key gate (`Display::HandleKeyEvent`) among them, with `search=1` only |
| `tq_runtime.cpp/.h` | every engine function resolved by name, and the raw structs passed around |
| `tq_exports.h` | generated by `tools\gen_exports_header.py` from the game's export tables (section 5). **Zero addresses** |
| `ut_paths.cpp/.h` | **the only** place that decides where files live. Never call shell32 from here |
| `ut_log.cpp/.h` | the line logger; buffered, five levels, 3 rotated archives |
| `ut_config.cpp/.h` | one table of settings. The parser, the written file and the test all read that table |
| `ut_generate.cpp/.h` | the DLL side of `src\gen`: keep the generated files current before anything reads them |
| `src\gen\` | the catalogue generator: `arz_reader` (ARZ v3), `arc_reader` (ARC v1), `inflate` (zlib), `catalogue_gen`, `generate` |
| `src\model\` | `catalogue` (the binary format), `collection` (ownership), `layout` (the footprint packer) - pure, no Windows |
| `ut_view.cpp/.h` | the collection VIEW on the Transfer page: the substitution, the one OFF path, the guards, the deposit and take routes |
| `ut_proto.cpp/.h` | the mod sack and the display prototypes: one real `Item` per record of the page, built from the replica |
| `ut_live.cpp/.h` | the page model: the group table, the page / group shown, the OWN filter, the row window |
| `ut_panel.cpp/.h` | the pad (15 group buttons, the Transfer toggle - `kUtPadToggle`, which older comments call Collection - OWN, <, >, the search field and its clear button) and the label, drawn inside the caravan frame; the search's slot marks and group-button marks |
| `ut_plate.cpp/.h` | where the Transfer page is on screen, and whether it is on screen at all |
| `ut_owned.cpp/.h` | the owned marking: the gray icons, the veils, the slot plates, the ring, the grid cover; the label's whole-collection count (`ownedLabelAll`) |
| `ut_store.cpp/.h` | the private table's owning side: the save set chosen at world load, the deposit / take choke points, the character-save watch |
| `ut_rescue.cpp/.h` | the journal (format TQ-1) and its CSV export. Pure file I/O - not one engine call |
| `ut_recon.cpp/.h` | the pending rows settled against the character's own containers (read only) |
| `ut_tooltip.cpp/.h` | the item rollover: the "In your collection" line, and the capture flag that keeps a search capture out of it |
| `ut_search.cpp/.h` | the item search (section 4b): the text capture, the index and its schedule, the query, the field and its key gate, the real Transfer page's item cache, the one fault latch. The header's top half is pure |
| `ut_bindings.cpp/.h` | **every fact the mod knows about the game's memory**, in one place, with how it is obtained and how it is confirmed |
| `ut_depositgate.h`, `ut_viewgate.h`, `ut_paintgate.h`, `ut_ownedfold.h`, `ut_rowmath.h`, `ut_padlayout.h`, `ut_protoshift.h`, `ut_gridcheck.h`, `ut_slotart.h`, `ut_graytex.h`, `ut_costprobe.h`, `ut_textfold.h`, `ut_fontmetrics.h` | the pure-logic headers: a decision extracted from its engine context so an offline test can run exactly the code the mod runs |
| `ut_searchfold.h` | pure: the one fold the search applies to an item's text and to the query, and the substring match |
| `ut_color.h` | pure: a colour named in the ini (`search_mark_color`), a name from a fixed table or `#rrggbb` |

Those last headers are the pattern worth keeping. Whenever a decision is dangerous - whether a
deposit may proceed, whether the view may switch on, which row a scroll lands on - it is written
as a pure function in a header, the engine side supplies an SEH-guarded read, and the test
supplies a plain one. Both run the same rule.

### Threads

- **Game thread** - everything that touches engine memory: the detours, the Update tick, the game
  window's procedure. The search's index and the real Transfer page's read run here, from the
  Update (`searchTick`, and `searchBeforePage` at a page build); its key gate runs on the engine's
  own input path (`Display::HandleKeyEvent`, called from `Engine::ProcessUserInput`), which is this
  thread too.
- **Worker thread** - the ini, the log flush, the journal writes, the exports. It reads engine
  state only through published atomics.

The pad reads the search only through interlocked words (the group marks, whether a query stands,
the label's counts), through a copy of the real page's matched rects taken under that list's own
SRW lock, and through a copy of the field's text (`searchFieldText`) taken under the field's own
SRW lock, which is never taken inside an SEH frame. The focus, the text's generation counter, the
last key's tick and the fault flag the gate raises for the next Update are interlocked.

The rule: if a function can call into the engine, its comment says GAME THREAD ONLY and it means
it.

## 4. How the page exists

There is no way to add a caravan tab, so the collection **borrows the Transfer page**.

1. **The substitution.** While the view is on, the Transfer page's own sack accessor returns the
   mod sack instead of the real one (G1). The real Transfer sack is never touched and comes back
   the moment the view switches off.
2. **The prototypes.** The mod sack holds one real engine `Item` per record of the page shown,
   built by `Item::CreateItem` from a replica (`ut_proto.h`), so the game draws the icons and the
   tooltips itself. They never leave the mod sack; they are destroyed at every OFF.
3. **The moves.** A deposit is the engine's own drag-drop onto the page or its own right-click
   quick-move from the inventory; the detour decides whether it is allowed, writes the journal row,
   and lets the engine remove the original. A take hands the player a fresh item built from the
   journal's replica - to the cursor (left-click) or into the inventory (right-click).

**The view never writes a save.** Every way out of the view - a tab change, a caravan close, a
world change, a session-state change, a fault, the process detaching - goes through
`viewForceOff`, and a `StreamOut` of the mod sack that should be impossible is caught by a
pre-detour that re-points, logs an ERROR and latches the view off for the process (G3).

## 4b. How the search works

The collection page can be searched by an item's name and properties. A query **highlights** the
slots whose text holds it and hides nothing: the page lays out exactly as it does with no query
(OWN, the row scroll, the counts), and a change of the query recomputes the highlight and the group
marks without rebuilding the page. Titan Quest has no search box and no searchable item text, so
the mod builds both. The header comments of `src\ut_search.h` (its top half pure, run by
`tools\test_search.cpp`) and `src\ut_search.cpp` are the authoritative text; this is the shape. Three
of their lines are older than the code, and where they differ this section describes the code: the
`ut_search.cpp` header's rule of one engine call per SEH frame (the real page's two walks are
bounded frames of their own, invariant 8); `ut_search.h`'s "the pad lays the field out whenever
search=1" (`utPadFieldLaidOut`: the view ON, or the real page with `search_transfer=1`); and its
"the view ON" for `searchFieldFocus` (the field takes the focus on the real page too). The
start-up log line for a missing key gate is older too: it says the field is not drawn, and the
field is drawn disabled (section 5). The section is numbered 4b so that the sections the code
cites keep their numbers.

**The text of one record.** For each catalogue record the mod keeps the text the engine itself
shows in that item's rollover, in the game's language, captured inside one call on the game
thread (`captureOne`, whose middle is `captureItem`):

1. the bare replica (`utReplicaBuild`: seed 0, no relic - what the page builds for an item not yet
   collected) and `Item::CreateItem` make a throw-away item, never placed in a sack;
2. the item's vftable +0x14C must hold one of the four `GetUIDisplayText` overrides (Item,
   ItemEquipment, ItemArtifact, ItemRelic) the tooltip resolved by name; any other value makes the
   record unindexable;
3. the capture flag is set (invariant 9) and the builder runs with the **main player** as the
   character (ItemEquipment's set block reads it with no null test) into an empty VS2012 vector
   the mod owns;
4. the `GameTextLine`s are staged by pure reads (`utSearchStage` checks every line before it
   copies one: a whole number of 0x20-byte lines, at most 512, a sane size and capacity, a pointer
   where the text is on the heap), then every heap string and the array go back through
   MSVCR110's `operator delete`, the allocator that made them;
5. the flag is cleared, the item destroyed (`DestroyObjectEx`), and the kept lines are folded into
   one UTF-8 string, the lines joined by `\n` - in mod memory, outside every frame.

A record whose item cannot be built, whose builder is not one of the four or whose rollover is
empty is **unindexable**: counted, logged (the first eight, DEBUG) and never retried. The words do
not depend on the character, so the cache lives for the process; it is built only while a main
player exists. Two fallbacks are written and compiled off (`kNameFallback`, the name through
vftable +0xFC; `kRequirementSubtraction`, the requirement lines through +0x15C); the DEBUG capture
line, one per builder (`search: capture <record> (<builder>): ...`, the raw lines with their
classes), shows whether either is ever needed.

**The keep rule and the fold.** `utSearchKeepClass` drops the requirement lines (class 0x11:
"Required Strength" is on every weapon, so "strength" must find what *gives* Strength), the usage
directions (0x1C), and the lore line (0x0E) unless `search_lore=1`. Every other class is kept, one
the table does not know included: over-matching beats missing a property. `ut_searchfold.h` folds
the text and the query with the same functions - ASCII to lower case, the Latin-1 and Latin
Extended-A letters to their base letters, the curly apostrophes to `'`, the no-break space to a
space, Cyrillic capitals to small, the colour escapes dropped, every control character to a
space. A query matches when its fold, trimmed, is one substring of the record's fold (Grim Dawn's
semantics: no word logic); a folded query never holds a line break, so no match spans two lines.
`ut_textfold.h` stays the catalogue names' ASCII fold.

**When the index is built.** Nothing is built before the first query, the field's first focus or,
with `search_prebuild=1`, the first view-ON. Then the group the page wants is indexed at once,
whole (`searchBeforePage` at the page build, or the next Update after a focus or a query), so its
highlight is there at the first letter. Every other group is indexed in the background by
`searchTick`, about 1.5 ms per Update (the budget is checked after each record), only while a
world with a main player exists (paused
otherwise, the cache kept) and not in an Update in which the real Transfer page is read. One INFO
line says when it is complete (`search: index built - 1588 records (...)`: the lines kept and
dropped, the size, the time).

**The highlight.** `search_mark` picks the look: 1 a frame, 2 a wash, 3 both. The frame lies on the
slot's outermost pixel ring (`utSearchMarkRect`, inset 0: clear of the engine's rarity art inside
the slot, never in the next slot), 1 px thick below a 48 px cell (UI scale under 1.5) and 2 px from
there; it is drawn after the last page draw (over the veils, under the tooltip), or by the Present
detour in a frame where that did not run. The wash is the slot inset by 1 px at 35 % of the
colour, drawn by the page draw's PRE (over the slot art, under the ring and the icon). The colour
is `search_mark_color`: a name of `ut_color.h`'s table - gold (the default, #ffd700), green, white,
red, orange, cyan, magenta, and blue (GD's hovered-plate blue) - or `#rrggbb`; a value that names no
colour is one WARN at load and gold, and the ini reader keeps a `#` that starts this key's value
instead of taking it for a comment. Prototype i of the page is matched to its record by its place,
then by the record pointer (`utSearchPlaceIndex`); a prototype that belongs to no place is never
marked.

**The group-button marks.** With `search_buttons=1` (the default) and a query standing, a group
button's plate turns blue once its group is **fully indexed** and holds a match that passes OWN
(`utSearchGroupMarked`), so the marks under-report while the index runs and never over-report.
The captions of the fully indexed groups without a match are dimmed, and a pressed button is drawn
exactly as it is without the search. They are drawn while the view is ON only.

**The field.** Row 2 of the pad (`ut_padlayout.h`) is Transfer (2 cells), OWN (1 cell), `<` and `>`
(half a cell each), then the field (3 cells, its clear square - the row's height - at its right
end), then the label (the remaining 8 cells, the same box whether the search is on or off). The
field is laid out whenever `search=1` and the view is ON, or OFF with `search_transfer=1`
(`utPadFieldLaidOut`). While it cannot take the focus (the search off after a fault, no key gate,
the collection not active) it is drawn disabled, reading `search off`, and a click on it writes
the reason to the log once.

- **The focus** is a left click on it (`fieldClick`, then `searchFieldFocus`); the first focus
  starts the index, and the next Update indexes the shown group.
- **The blur paths** (`searchFieldBlur`, one INFO line with the reason): Enter (the query is kept);
  Esc on an empty field (on a non-empty one Esc clears it and keeps the focus); a left or right
  press anywhere but the field, its clear square, the Transfer toggle and - on the real page - a
  group button (those switch the page and keep the focus: one query serves both pages); the
  Transfer page no longer shown, the view OFF with `search_transfer=0`, the caravan closed, the
  world unloaded, the pad not live, `search=0` in the ini, a fault (`fieldTick`, every Update);
  `WM_KILLFOCUS` and `WM_ACTIVATEAPP` (FALSE) in the window procedure, never swallowed. A blur
  restores nothing: the gate holds no engine state, it only stops taking presses.
- **The watchdog**: 60 s without a key drops the focus (the `GetTickCount` difference is unsigned,
  so the wrap is harmless). The gate checks it too, with the view and the page, because the Update
  may not run (a loading screen).
- **The clear button**: its `x` shows while the field holds text; a click clears the query through
  the same edit as Esc on a non-empty field and leaves the focus as it was. On an empty field the
  square is the field's own target.
- **The keys**, while the field has the focus (invariant 10): Backspace erases, Ctrl+Backspace
  clears, Esc and Enter as above, and any other press appends the event's `GetText` characters
  from 0x20 up (DEL and the control characters a Ctrl combination gives are dropped). The mod's
  own keys - the view hotkey, PgUp / PgDn - do nothing while the field has the focus.
- **The Backspace repeat**: the engine sends one event at the press and one at the release and
  none while a key is held, so the field repeats Backspace itself (`UtBackRepeat`): the press
  erases, then after 400 ms one character every 40 ms until the release, an empty field, a blur or
  another key. `GetAsyncKeyState`, polled each Update, ends it should the release event never
  arrive.
- **At most 32 characters** (`kUtSearchFieldMax`); a longer query shows its tail. The Update folds
  the text into the query whenever its generation counter moves. `search_debug_query` (advanced)
  fills the field whenever its value changes.

**The label.** While the view is ON the label (`utPadLabelLine`) carries the group, the rows shown,
the group's owned count, the whole collection's owned count after `all` and, while a query stands,
`indexing k/1588` until the index is complete, then `found N` - the matches of every group that
pass OWN. At the label's size the first form that fits is drawn: the full form, the compact one,
then (a query only) the search word's short form (`=N`), then without the rows, then without the
`all` count and last, with a query only, without the group's owned count; the last form tried
(with no query, the one without `all`) shrinks, down to size 6. The whole-collection count
(`ownedLabelAll`, `ut_owned.cpp`) tests every
catalogue record against the journal on the game thread - at every refresh (the view ON, a
deposit, a take), and after a page build only when the journal set is not the one it was counted
on; never per frame. It reads `?` while the journal set is not known. On the real Transfer page
the label is `the real Transfer page` (or `Transfer`), and with a query `... found N` (`reading
k/n` while its items are read), shortened the same way (`utPadLabelRealLine`).

**The real Transfer page.** With `search_transfer=1` the same query marks the items of the real
Transfer sack. `realTick` runs once per Update and reads only under invariant 12:

- the sack's item map (section 5) is walked every Update - `GetInventory` on the real sack and the
  pure walk, in one SEH frame - because a sort moves the rects and keeps the ids;
- the walk's ids, ascending, are compared with the id set the cache was built on. When they differ
  (a deposit, a take, a pick-up, a sort into new ids), **one** `ObjectManager::GetObjectList` walk
  resolves every id to its object and its record name, copied into fixed mod arrays inside the
  frame, and the list is freed through the CRT's delete in a frame of its own. An id with no
  object stays in the cache as unindexable;
- the merge: an item still there under the same id, the same pointer and the same record keeps its
  text; every other item is read again with the index's own capture (`captureItem` on the live
  item, which is never changed, moved or destroyed), about 1.5 ms per Update (checked after each
  item, so at least one). The collection's background index waits in that Update;
- the matches use the same keep rule, fold and substring test, and are recomputed when an item is
  read or the query changes, never per frame. The matched rects go to the draw under their own
  lock, the label's counts as interlocked words;
- the cap is 512 entries (`kUtSearchRealMax`; the sack has 240 cells): a map that claims more does
  not read as one, which is a fault like any other (invariant 8);
- the cache lives for the session, keyed by the id set, and is dropped when the world unloads or
  the main player changes. One INFO line says when it is first complete (`search: the Transfer
  page's items are read - n item(s) in t ms`); a binding it needs that is missing is one INFO line
  and the page is not read.

The marks there are the same frame and wash (`drawRealSearchMarks`), each item's rect put on the
measured grid by `utSearchRealRect` (a number, not empty, inside the 16 x 15 grid within half a
pixel, or never drawn). They are drawn by the page draw's POST, or by the Present fallback; the
page draw's PRE does not run while the view is OFF, so there the wash lies over the icon.

## 5. The bindings - the classes and the gate

`src\ut_bindings.h` is the single inventory. Read its header comment; it is the authoritative
version of this section.

The mod **never asks what version the game is**. It asks a *capability* question instead, one
binding at a time - can this still be found, and does what was found still look like the thing it
is supposed to be?

| class | how it is obtained |
| --- | --- |
| **EXPORT** | `GetProcAddress` by decorated name in Game.dll / Engine.dll |
| **SIGNATURE** | a byte pattern scanned in `TQ.exe`'s `.text`, with an exact expected hit count |
| **DECODED** | an offset or call target read out of an *exported* function's own instruction bytes |
| **STRUCTURAL** | a layout the compiler guarantees (VS2012's `std::map`, `std::string`, `std::vector`) |
| **LITERAL** | a field offset inside an object the mod already proved it holds, read under SEH |

Titan Quest's `TQ.exe` is not encrypted on disk, so unlike Grim Dawn's the exe signatures are
EARLY: scanned by the worker before any hook. They are ADVISORY for the mod and REQUIRED for the
view - a miss makes the view unavailable (`view: unavailable - <reason>`) and leaves the rest of
the mod running. `bindingsGate()` logs the module identity (size and PE timestamp of `TQ.exe`,
Game.dll and Engine.dll) for the record, never as a gate, one INFO line with the counts per class
(`bindings: ... all confirmed`), and the whole table at DEBUG.

To add a binding, a decoder or a signature, follow the Grim Dawn rules, which the port kept: a row
in the inventory comment, `bindingsNote(...)` where it resolves, the phase and the gate chosen
honestly (`tools\test_bindings.cpp` asserts both row by row), decoders in a pure header, signature
bytes + mask in `ut_bindings.cpp` shared with the test, every address-bearing byte wildcarded
(`tools\sig_from_exe.py` cuts a counted pattern with the relocations wildcarded).

### Where the bindings come from

Every binding was derived offline from the on-disk AE 2.10 files, by disassembling the exported
functions and the code around them. `TQ.exe` carries no DRM stub, so the file on disk is the image
the game runs; Game.dll and Engine.dll export their C++ classes by decorated name. The `rva 0x...`
numbers in the comments (`ut_bindings.h`, `ut_tooltip.cpp`, `ut_view.cpp` and the rest) are the
places in the 2.10 files a reader can open in a disassembler to check a claim; the mod itself
resolves nothing by address - an export by its name, a signature by a counted byte pattern, a
decoded offset out of an exported function's own bytes.

The ones the rest of the code leans on:

- **The Transfer page accessor**, `TQ.exe` rva 0xC2F50: the prologue, the IAT call to
  `GameEngine::GetPlayerTransfer`, the `lea edi,[ebx+0x88]` of the page's `UIStashInventory` and
  the `mov [edi+0x60],ecx` that caches the sack. The signature (`kUtSigTransferPage`) is those bytes with every relocated
  operand wildcarded; `ut_bindings.h` has the row and the decoded offsets.
- **The two deposit gestures** that `ut_depositgate.h` tells apart: the drag-drop into
  `PrimaryTransferActivate`, whose disposal of the source the mod performs itself exactly as the
  engine does, and the inventory quick-move into `GameEngine::AddItemToTransfer(id, bool)`, whose
  one primary `TQ.exe` call site (the call returns to rva 0x107A6C) disposes of the original
  itself when the call returns true. `tools\test_bindings.cpp` finds that call site again in the
  installed `TQ.exe` and asserts there is exactly one.
- **The inventory draw's operands** (the tint and the item widget's icon, which the owned marks
  and the slot art follow): decoded from the draw functions' own bytes; `tools\test_bindings.cpp`
  re-reads each from the installed `TQ.exe`.
- **The tooltip's `GameTextLine`** (0x20 bytes) and the rollover it is read in: from the
  disassembly of Game.dll's rollover functions; `ut_tooltip.cpp`'s header lists every offset it
  uses, and `tools\test_tooltip.cpp` models that layout. The search reads the same line vector:
  the class at +0x00, a VS2012 `basic_string<unsigned short>` at +0x04 (size +0x14, capacity
  +0x18, the text in place while the capacity is below 8), from the builder in the item's vftable
  +0x14C, which must be one of the four `GetUIDisplayText` exports the tooltip resolves.
- **The search field's key gate**: two Engine.dll exports, both OPTIONAL rows of
  `gen_exports_header.py`'s `SELECTION` (the "Input" block of `tq_exports.h`), resolved by name.
  `Display::HandleKeyEvent(ButtonEvent const&)`, rva 0x133370, is the detour's target: its one
  caller is `Engine::ProcessUserInput`, so every keyboard event passes it before any game widget.
  `ButtonEvent::GetText() const`, rva 0x1A0750, is `lea eax,[ecx+0x24]; ret` and gives the typed
  characters. The event's button (+0x0C, a DIK-style scan code) and state (+0x10: 0 a press, 1 a
  release) are read under SEH. `tools\test_bindings.cpp` finds both in the installed Engine.dll and
  checks their first bytes. Without either the gate is not live and the field is drawn disabled;
  nothing else changes.
- **The Transfer sack's item map**, which the search walks on the real Transfer page:
  `InventorySack::GetInventory` returns a `const std::map<unsigned, RectExt>&`, VS2012 x86. The map
  is {head, size}, the head the nil node, whose left is the smallest key; a node is left +0x00,
  parent +0x04, right +0x08, color +0x0C, isnil +0x0D, the key (the item's object id) +0x10, then
  the `RectExt`: four floats, x +0x14, y +0x18, w +0x1C, h +0x20, the item's rect in drawn px from
  the grid's top-left. The four floats were verified against Game.dll's
  `InventorySack::GetItemUnderPoint` (rva 0x1B6580), which tests `x <= px < x + w` on exactly those
  four and steps through +0x08 / +0x00 / +0x04 with isnil at +0x0D. `UtSackMapNode` (`ut_search.h`)
  static-asserts the offsets and `tools\test_search.cpp` walks a fake map of that shape. The walk,
  `utSackMapWalk` (the `msvc.map` row; the owned marking's id read in `ut_owned.cpp` uses the same
  one), refuses a head that is not the nil node, a null link, more entries than its cap, a step
  bound exceeded, or a count that is not the stored size. The ids are resolved to items through
  exports the mod already holds (`ObjectManager::GetObjectList`, `Object::GetObjectId`,
  `Object::GetObjectName`).

`tools\build_test_bindings.bat` is the proof that the installed game still matches every row: it
resolves every export name, counts every signature and runs every decoder against the game folder
(read only). A game update that moves one of them fails that suite, and the mod's own gate refuses
the part that depends on it at run time.

### The export names: regenerating `src\tq_exports.h`

`src\tq_exports.h` is generated and committed. Two steps regenerate it from an installed game:

```
python tools\tqexports.py              -> build\exports\exports_Game.txt, exports_Engine.txt, exports_TQ.txt
python tools\gen_exports_header.py     -> src\tq_exports.h          (--check: exit 1 when it is stale)
```

`tqexports.py` reads the export name tables of Game.dll, Engine.dll and `TQ.exe` out of the game
folder `tqpath.py` finds (read only; the dumps land in the untracked `build\`).
`gen_exports_header.py` emits one block of macros per name in its `SELECTION` table: a name that
is not in its module's dump stops it, the calling convention and a by-value return are read out of
the mangling, and the comment is `undname.exe`'s output (found through `vswhere`, never a written
path). To add an export, add its row to `SELECTION` (module, macro suffix, decorated name,
REQUIRED or optional) and run both steps.

## 6. The data pipeline, and the byte-identical oracle

```
a Titan Quest AE 2.10 game folder
        |
        |  tools\build_catalogue.py   -> data\oracle\catalogue.json (not tracked), catalogue-stats.md,
        |                                uniq-records.txt, uniq-groups.txt, uniq-excluded.txt
        |  tools\pack_catalogue.py    -> data\oracle\catalogue.bin (format GDUT v2)
        |  tools\ui_slotart.py --oracle data\oracle\slotart.txt
        v                                                          (ALL OF IT: FIXTURES)
src\gen  (the same job, in C++, inside the DLL, on the player's own archives)
        -> <mod folder>\catalogue.bin, uniq-records.txt, uniq-groups.txt, uniq-excluded.txt, gray\
```

Nothing on the left ships. The package is the `.asi` and three documents (not `CHANGELOG.md`,
section 9); `data\oracle\` is the
harness's and is left out of both the zip and the public tree. `data\oracle\ORACLE.txt` gives the
order to rebuild it from a game folder, read only.

Two decisions bind what the catalogue holds (`tools\build_catalogue.py` and
`src\gen\catalogue_gen.h` list the exclusions): the records only monsters hold STAY (monsters drop
what they hold), and records that share a display name STAY separate (distinct records, distinct
drops).

The Python tools under `tools\` are the **reference implementation**, and the contract is that the
files the DLL writes are **byte-identical** to the Python ones. `tools\build_test_catalogue.bat`
is the oracle: it runs the C++ generator over a real installation and compares the outputs byte
for byte against `data\oracle\`.

The fixtures are **CRLF**, because that is what the DLL writes. `.gitattributes` marks
`data\oracle\` as `-text` so git stores the real bytes; undo that and a fresh clone fails the text
comparisons.

`catalogue.stamp` fingerprints every input archive; a matching stamp costs one stat per archive.
Regeneration writes everything to temporary names, re-reads `catalogue.bin` through the model
loader, and only then renames the set into place. On any failure the folder keeps the files it
already had. The gray icons carry their own `gray\gray.stamp`.

## 7. The journal format

`tq-uniq-items.jsonl` (and the `-custom` / `-<mod>` sets), JSON Lines, UTF-8. The full spec is the
file header of `src\ut_rescue.cpp`; this is the shape.

```
line 1   {"journal":"titan quest uniquetab","format":1,"written":"<ISO-8601 UTC>",
          "set":"<leaf>","entries":N,"collected":C,"pendingIn":I,"pendingOut":O}
line n   one stored COPY: "record" (THE KEY, folded), "deposited", "stack", "base" (byte-exact),
         the other six replica strings (empty ones included), "seed" "var1" "var2", "b8",
         and optionally "pending" ("in" / "out") with "taken"
```

**The row count governs.** Two byte-identical lines are two copies, never merged.

**The reader's contract is Grim Dawn's.** Split lines first, parse second; a bad line costs that
row only (and the file is copied aside as `.bad-<stamp>` before the next write replaces it); a
higher format, a header naming another journal, or a file that is there and cannot be read means
**read-only for the session**. "I cannot read it" never becomes "there is nothing in it".

**Pending rows.** A deposit's row is `"pending":"in"` and a take's `"pending":"out"` until the
character's next save; the character-save watch (`ut_store.cpp`, reading `Player.chr` under the
game's `SaveData` folder, read only) settles them. A row the game never saved - a crash in
between - is checked at the next world load and caravan open against the character's inventory,
equipment and caravan stores (`ut_recon.cpp`, `utPendingVerdict` in `ut_rescue.h`): an exact
replica found decides the row; absence after a complete pass keeps the move's outcome with a
"check for a duplicate" warning, because a row does not yet record which character made it.

### The all-items collection file (`data\allitems\`)

`tools\make_all_items.py` writes `data\allitems\tq-uniq-items-all.jsonl` (+ `.report.txt`): a
format-1 collection with one row per record of `uniq-records.txt`, each row the page prototype's
recipe (`utReplicaBuild` in `ut_proto.h`) - `base` = the record with `/` -> `\`, the six other
strings empty, `var1` / `var2` / `b8` 0 (`var2` is the second relic's shard count: the `Item`
constructor zeroes it and only `ItemEquipment::AddSecondRelic` stores into it) - and a seed the
prototype leaves to the engine but a row must name, because a pending take is settled by finding
the live item with the row's exact identity: FNV-1a of the folded key, folded into 1..32767 (the
engine's seeds are 15-bit). A re-run is byte-identical apart from the stamps (`--stamp` fixes
them). `--journal <copy>` carries a real collection's rows through byte for byte and generates
only the missing records; rows the reader would drop are left out and listed in the report. The
file ships in the zip as `extras\` (README "Every item at once"). To regenerate it: the fixtures
first (`data\oracle\ORACLE.txt`; `tools\regen_all_items.bat` runs `build_catalogue.py` itself when
`uniq-records.txt` is missing), then `tools\regen_all_items.bat`, then `tools\build_test_journal.bat`,
whose last step loads the file through the reader (`test_journal --load`) and checks the take's
round trip for every record. `.gitattributes` keeps `data\allitems\` byte-exact.

## 8. Building, and the tests

Build with MSVC Build Tools (any edition with the x86 C++ toolchain). `tools\find_vcvars.bat`
locates `vcvars32.bat` through `vswhere` without naming a drive or a user folder; every `.bat`
calls it itself, so any shell works. The target is **x86, Release, `/MT`**: Titan Quest is a
32-bit VS2012 program, and the mod keeps its own static CRT and never shares a heap with the
engine (every string the engine fills is freed through MSVCR110's own `operator delete`,
resolved by name). `/PDBALTPATH` keeps the build folder out of the binary.

```
build.bat                           -> bin\uniquetab.asi (+ .pdb)
tools\build_test_<suite>.bat        -> build\test\...
build_model.bat                     -> build\model\model_test.exe
```

Ten offline suites. **All ten must be green before any commit that touches `src\` or
`tools\`.** Run them sequentially - they share build output directories.

| suite | what it covers |
| --- | --- |
| `tools\build_test_bindings.bat` | every export name resolved in the installed DLLs, the signatures counted in `TQ.exe`, the decoders, and the class/phase/gate classification row by row |
| `tools\build_test_catalogue.bat` | the whole generator end to end against a 2.10 game folder: the outputs byte-compared with `data\oracle\`, a fresh install into empty folders, the stamp, the error paths, the peak working set |
| `tools\build_test_config.bat` | the settings table: every row's default matches its member, the writer, the parser, the clamp, and the path rules |
| `tools\build_test_journal.bat` | the journal TQ-1: reader and writer, the format gate, the pending rows, the reconciliation verdicts; then `--load` over `data\allitems\tq-uniq-items-all.jsonl` (every row read, none dropped, the take's round trip for every record) |
| `tools\build_test_proto.bat` | the prototypes' replica assembly, over every catalogue record |
| `tools\build_test_search.bat` | the search's pure half, no game and no fixtures: the staging and the free of a captured line vector over a fake engine vector (inline and heap strings, the small-string edge, malformed shapes refused, every free counted), the keep rule, the fold, the matching, the highlight and the count, the key gate's decisions and the field's edits, the Backspace repeat, the group marks, the mark's colour and geometry, and the real Transfer page's map walk over a fake VS2012 map with its label forms |
| `tools\build_test_store.bat` | the private table and `ut_depositgate.h` |
| `tools\build_test_tooltip.bat` | the tooltip line |
| `tools\build_test_viewgate.bat` | the view's ON/OFF state machine, every event order up to depth 7 against a simulated Transfer page |
| `build_model.bat` (repo root) | `tests\model_test.cpp` - the pure model over `data\oracle\catalogue.bin` |

One environment variable: `TQ_GAME_DIR` names the game folder (the one that holds `TQ.exe`),
opened **read only**; without it the Steam registry's default library is used. `bindings` and
`catalogue` need it; `search` needs neither the game nor the fixtures. Without the fixtures - the public tree carries none of `data\oracle\` -
`catalogue`, `viewgate` and `build_model.bat` cannot compare anything; rebuild them first with the
steps in `data\oracle\ORACLE.txt`.

And one the mod itself honours: `%UNIQUETAB_OUT%` overrides the mod folder. It is how each harness
gets its own empty directory, and it is read before anything else so a test can never reach a
real installation.

No test launches the game. None writes outside `build\`.

## 9. The release procedure

1. Set `UT_VERSION` in `src\ut_version.h`; nothing else in the code spells the version. Then add
   the release's section at the top of `CHANGELOG.md`: the version, the date, what a player sees,
   every new ini key with its default, and the `ini_version` step with what the migration keeps.
   `release_check.py` requires the file in the public tree; `package.ps1` leaves it out of the zip
   on purpose (the zip carries what a player installs, and README says where the changelog is).
2. Build, and run the ten suites.
3. `powershell -ExecutionPolicy Bypass -File tools\package.ps1` stages
   `out\UniqueCollectionTab-TQ-<version>\` (`scripts\uniquetab.asi`, `README.md`, `LICENSE`,
   `THIRD_PARTY.md`, and `extras\tq-uniq-items-all.jsonl` + its report from `data\allitems\`) and
   zips it from inside, so the zip mirrors the game folder. It refuses to stage anything else
   generated.
4. `python tools\release_check.py --strict` must print CLEAN before every commit, and
   `--root <public tree> --zip <zip>` before a release. It scans every file that ships for machine
   paths and for the development process (test-sitting names, process labels, planning words, paths
   into the development tree's own folders, date stamps, clock times - its header lists the rules),
   checks a public tree's file list against its allow-list, and checks the zip's entries and the
   `.asi`'s strings. `tools\publish.ps1`, which exports the public tree, builds and tests it there
   and packages the zip from it, is development-only and is not in the public tree. The fixtures
   under `data\oracle\` are left out of the public tree by design: `data\oracle\ORACLE.txt` rebuilds
   them from a game folder. With `--root` the check reads that folder as it lies on disk, ignored
   files included, so run it on a fresh export or after `git clean -fdx`: a build, the suites or
   regenerated fixtures leave logs and caches there that it flags.
5. The documents are committed LF and `.gitattributes` pins them (`*.md`, `LICENSE`: `eol=lf`), so
   `package.ps1` in a fresh clone gives the same bytes whatever `core.autocrlf` says.

## 10. Where the design lives

The development tree keeps the engine notes, the design notes and the test sheets that produced this
code; they are not part of the public tree. Every decision that still binds the code is
written in the code itself: the headers of `ut_bindings.h`, `ut_view.h`, `ut_store.h`,
`ut_rescue.cpp`, `ut_search.h`, `ut_search.cpp` and the pure-logic headers are the authoritative
text (section 4b names the few lines of the search's headers that are older than the code).

## 11. Open questions

- **The rows do not record their character.** One journal serves a whole save set, so an absent
  item is judged against whichever character loads; that is why the reconciliation keeps the
  "check for a duplicate" wording instead of claiming a proof. The full fix is a format field (the
  character's folder) and an engine read of the loaded character.
- **Only vanilla records are collectable.** A mod's own uniques are in no catalogue; a mod that
  replaces the database gets its own journal but the base game's item set.
- **Proton / Steam Deck are untested.**
- **The export dumps and the fixtures come from an installed game.** `tools\tqexports.py` and the
  `data\oracle\` rebuild need a 2.10 game folder, as the bindings and catalogue suites do; neither
  is committed.
- **The packaged build is a person's to QA.** A clean install from the zip itself is checked by
  hand, not by the offline suites.

## 12. What NOT to do

- **Do not write the game's save files.** Not `Player.chr`, not `winsys.dxb`, not any caravan
  file. If a change could possibly do this, it does not ship.
- **Do not let a prototype leave the mod sack**, and do not add a way out of the view that skips
  `viewForceOff`.
- **Do not install a hook before the gate.** `bindingsGate()` runs first, and a failure means
  *nothing* is installed.
- **Do not hard-code a code address.** A field offset inside an object the mod already proved it
  holds is acceptable *with* a plausibility test; a code address is not.
- **Do not compare against a game version.** Ask the capability question.
- **Do not ship a generated file.** `catalogue.bin`, `catalogue.stamp`, the `uniq-*.txt` lists, the
  gray icons, the ini, the log, the journals, anything under `data\oracle\`. Ever.
- **Do not disable hooks in `DllMain`.** The detours are deliberately left in place until process
  exit.
- **Do not call shell32 from `ut_paths.cpp`.** `utModDir()` is first called from `DllMain`, under
  the loader lock. Documents is reached through the environment instead.
- **Do not let a "cannot read" become an "it is empty".** Fail closed, say why, and change nothing.
- **Do not take a lock or write a log line inside an SEH frame**, in the search as in the tooltip,
  and do not retry the search after a fault: it stays off for the session.
- **Do not let the key gate swallow a release**, or take any key while the field has no focus.
- **Do not launch the game from an automated run**, and do not deploy from one. Nothing automated
  should write inside a game, Steam or Documents folder.
