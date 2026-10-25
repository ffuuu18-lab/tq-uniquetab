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
this one is the code's. This text describes version 1.1.0 (the item search is section 4b; the
custom games, the relic and charm page and the collection rule are in sections 6 and 7); the
version is written in one place, `src\ut_version.h`, which the start-up banner and
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
   the `.asi`, or the Documents fallback): `uniquetab.ini`, `uniquetab.state`, `uniquetab.log`
   (+3 archives), the journals `tq-uniq-items*.jsonl` and their `.csv` copies, a `.bad-<stamp>`
   copy of a damaged journal, `catalogue.bin`, `catalogue.stamp`, `uniq-records.txt`,
   `uniq-groups.txt`, `uniq-excluded.txt`, `uniq-geometry.txt`, `uniq-relics.txt` and the gray
   icons under `gray\`; and per mod, under
   `mods\<leaf>\` (`ut_folders.h`), that mod's journal set, its own `uniquetab.ini`,
   `uniquetab.state` and `uniquetab.log` (+3 archives). The one-time move of an older build's mod
   journal into `mods\<leaf>\` is a rename inside this folder. Never `Player.chr`, never `winsys.dxb` or
   any caravan file, never anything in the game folder proper. **`uniquetab-groups.ini` is the
   user's: the mod only reads it** (`ut_groups_default.h` `loadGroupsFile` opens it "rb" and
   nothing else) - no template on disk, no copy, no rename, no repair.
2. **Never lose or duplicate an item.** A refusal is a true no-op: the item stays where it was.
3. **One journal per save set.** The main campaign, custom quests and each mod have their own
   characters and their own Transfer, and each has its own journal (`utSaveSetLeaf` in
   `ut_depositgate.h`; a mod's lives in `mods\<leaf>\`, the folder named by `utSaveSetGame` from
   the same leaf, so the two never disagree). When the set cannot be read, the mod refuses rather
   than guessing.
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

13. **One game in force, one leaf, every generated file through `utGameFile`.** The game whose
    catalogue, lists and gray icons are read changes only at `Engine::InitializeMod` (a custom game:
    `ut_gameprobe.h` -> `GameSpec::name()` = `utSaveSetGame(utSaveSetLeaf())`, the same leaf as the
    save set's folder) and at `Engine::CleanseDatabase` (the start-up game); every reader of
    catalogue.bin / uniq-*.txt / gray\ resolves it through `utGameFile` (`ut_generate.h`). Every
    custom game is generated at START-UP only (`generateGamesStart`, its own thread, one game at a
    time, each inside the build arena or, without one, a private heap released after it,
    `src\gen\gen_heap.h`, invariant 17); nothing is generated at
    an entry (`neverAtEntry`) or inside a world. The `InitializeMod` detour waits only while the
    entered game's own build is pending or running (`utGamesEntryWaits`), and makes it the next one
    built (`utGamesNext`); an entry or a world up (`gamesOnWorld`) stops the builds after the game
    in hand, and the games left are built at the next start (one INFO line names them). A set that
    is not current at the entry = the tab off in that game (one WARN, built at the next start);
    reading a current set needs 64 MB of the largest free block (`ut_gameswitch.h`). A switch retires the group / KIND / union tables (freed two switches later, invariant 17) and
    drops the search index, the
    owned counts, the prototypes and the gray cache exactly once (`hooks.cpp` `gameResets`).
    `Engine::InitializeMod` receives (NAME, FOLDER) - the engine's own load is
    `<folder><name>/Database/<name>.arz`; `utGameCallOf` maps them, and the hook's INFO line names
    both strings at every call. A game entered twice in a row before its world loads is the game
    in force again (`utGameAgain`): no second switch, no second generation.

14. **The grid in force.** The collection is packed on the grid in force: the derived Transfer
    sack (16 x 15 cells at the caravan geometry the game's database gives, `ut_caravangeo.h`) or the
    player's `grid_<W>x<H>` rectangle (whole 32-px cells under the caravan window's box cap, at
    least the largest group slot). A change of cells re-packs every group, all or none
    (`ut_gridpack.h`, called by `liveSetGrid`); the shown row keeps its first visible item
    (`utRowKeep`). The view OFF never re-packs (the collection stays on the player's cells; only
    the sack's place and size follow the view), and a game switch packs the new game on the derived
    cells first. Only the MOD sack is re-dimmed and moved (`viewAccessorGrid`, under its vftable
    and "the place is still ours" checks); the REAL Transfer sack is never re-dimmed, moved or
    covered, and every OFF puts the page's own place back. The caravan's painted lattice is in the
    caravan window's own background and follows the grid in force while the collection is on screen
    (invariant 16); every cells-to-px turn of an overlay takes the grid in force's cells. The
    caravan is the window the game's HUD record names (`hudCaravanWindow` of hud_x3.dbr, then
    hud_x3_new.dbr; the xpack caravan window last) -> its TransferWindow -> its InventoryDBR. When
    that area is larger than the real sack (SVAERA: 20 x 20 in 660 x 699), the collection is packed
    on it (`utCaravanCollectionCells`: every row with `pad_screen` and `label_screen` in force,
    else less the pad's band of 31 record px) and the mod sack is sized for it as for a player's
    grid; the real sack stays 16 x 15 (TQ.exe 0xC2F50). The grid line feeds the collection with the
    view ON and OFF alike (`utCollGridKeys`), so a toggle never re-packs. The derived grid
    the panel and the editor use is the MEASURED one (the page draw's inventory place and the real
    sack, `g_frameGrid`); the database's grid place (the P line of `uniq-geometry.txt`) feeds only
    the `view: caravan geometry` line, which says which one is in force when they differ; the W
    line (the window and the Transfer page's place) is used for the frame.

15. **The screen parts.** `pad_screen` / `label_screen` are `[layout]` keys written only while set
    (`kUtCfgWhenSet`). They are placed only while the consuming `GetMouseEvent` detour is installed,
    not off, and the event space is not refused in the world (`screenInputReady`): the first MOVE
    within 2 px of `GetCursorPos` proves it (INFO `consumption ENABLED`); 20 MOVEs that disagree
    refuse it for that world (WARN `consumption NOT PROVEN`) and the parts go back into the caravan
    window. Before the proof, a DOWN whose cursor point lies on a published part is decided at the
    cursor's point. GD's item-on-cursor rule is first: a click on a screen part with an item on the
    cursor is consumed (the type made a MOVE, the held bytes cleared - the one write, in its own
    guarded frame) and never reaches the world. The parts (and the open editor) draw at the head of
    the HUD item rollover's Draw (TQ.exe 0x18E6D0, `hk_RollDraw`), after the caravan window and
    under the item tooltip and the item on the cursor; the Present detour draws them only in a
    frame where that site did not. A fault at the site is one ERROR and the site off for the
    session (Present draws them).

16. **The caravan background swap** (`ut_cover.cpp`; its decisions in the pure `ut_coverstep.h`).
    The mod's painted copy is in the caravan window's field (caravan+0x1308, texture at +0x34) only
    while the view is ON on the Transfer page with a grid in force other than the derived one; the
    engine's pointer goes back at every OFF, the field compared with the mod's pointer first.
    Whatever the field holds is unloaded by the window's destructor (0xB6D70) or the engine's
    reload (slot +0x88); the mod unloads only what no field holds - the engine's texture when the
    field kept the mod's copy (the window gone, the engine's own write, a failed write-back), its
    own copy otherwise. A new grid while the copy is in: the engine's pointer back first, the old
    copy given back on the NEXT frame (this frame drew it), then the new one loaded and written.
    The painter (`gen\lattice_gen.*`) runs on its own thread outside any gen_heap pair, never calls
    the engine, writes .tmp then renames; a refused name is said once per session; a painted copy
    is reused only while its `<leaf>.src` fingerprint (the background's byte count and FNV-1a)
    matches. Nothing else writes the field.

17. **Memory.**
    - **The build arena** (`src\gen\gen_heap.*`). When the staleness check finds a build due, the
      worker reserves ONE 128 MB range, `MEM_RESERVE | MEM_TOP_DOWN`, before it waits for the game's
      modules (`generateArenaReserve`). Every start-up build's `genHeapBegin` / `genHeapEnd` pair
      allocates inside it (a boundary-tag allocator with TLSF-style free lists, committed in 256 KB
      steps, decommitted and reset per pair). A free from another thread is counted and never
      touches the range. It is released in one piece at the builds' end, at the world-up stop, on
      the worker's early returns and on `generateGamesStart`'s failures - never while a pair holds
      it - and nothing is reserved when nothing is due. A pair that outgrows it fails with
      `bad_alloc` and `genHeapOutgrown()` true: its game is left for the next start (also when the
      failure's own report could not be made - both `catch (...)` blocks ask). An entry waiting for
      its own game's build waits for the builds' end once the stop is set, so the database load
      never meets the range reserved. `arenaInfo()` from another thread returns nothing while the
      range is being released. Nothing made inside a pair may outlive it (Threads, below).
    - **Retired tables** (`ut_retire.h`). A replaced group / KIND / button-page table (and each of
      the buttons file's four objects) lives through the next two retirements of its kind and is
      then freed by the thread that retires the third. Readers use a table within one call.
    - **The search text kept per game** (`ut_searchsave.h`, `uniq-search.txt` beside the game's
      `uniq-groups.txt`). Key: FNV-1a 64 of `catalogue.stamp`, `text_language`, the database
      checksum, `search_lore`, `UT_VERSION`. Read at a game's first indexing (`beginWork`), and
      only after ONE saved record, read from its item, matches its saved text - the engine's own
      language and its text archives are not in the key. Written once per game when the index is
      complete; a record with a set-bonus line (class 0x16, the worn pieces) is never written. A
      damaged, foreign or other-key file is one INFO line, never a fault.
    - **The index starts per game.** `searchGameChanged` keeps `g_started` only while a query
      stands (`utSearchStartedAfterSwitch`), so a search in one game never starts the next game's
      background index (each item it makes fills the engine's record cache).
    - **Stack reserves** (`ut_threads.h`): worker 1 MB, start-up builds 1 MB, the plates' painter
      512 KB, the log flusher 128 KB, each with `STACK_SIZE_PARAM_IS_A_RESERVATION`.
    - **The `mem:` lines** (`ut_mem.*`) read only: the engine counters on the game thread in one
      guarded frame (the `ObjectManager::Get` call outside it, once per session); a fault turns the
      engine part off for the session with one ERROR; every line is built on the stack.
    - **Which latch is which.** A game's DATA fault (`UtViewState::gameOff`: its set does not load
      on its own area, or its first page cannot be built) turns the tab off in that game only, and
      the next switch clears it (`viewOnGameTable`). The ENGINE latches - the view's, the search's
      `g_off`, the saved text's `g_saveOff`, the gray icons', the engine counters' - stay
      session-wide.
    - **No lock is held across a throw.** Whatever allocates (a string, a list) is made before
      the lock and copied or swapped in under it (`g_slotLock`'s lists and `g_resLock`'s folder in
      `ut_generate.cpp`); an INFO line that may allocate runs under its own `catch (...)`, so it
      never stops a build.
    - **The uninstall deletes by name.** `undeploy.bat --purge` runs `tools\journal_guard.ps1`
      first: every `tq-uniq-items*.jsonl` in the mod folder and in each `mods\<mod>\` (and in
      `journal_dir` and its `mods\<mod>\` when the main `uniquetab.ini` sets one), whatever the
      rest of its name, refuses the purge when it holds a row or a pending row or cannot be read.
      The purge then deletes only the generated files it names and the contents of `gray\` and
      `plates\` by their patterns, and `rd` without `/s`: never `mods\`, a journal, a settings,
      buttons or groups file, an export or an unknown file. A new generated file is added to that
      list by name.

18. **The pad on the game's own Transfer page** (GD 1.2.0's materials-page rule). The view OFF
    resolves the page with the derived grid (the real sack, never moved or re-dimmed), and its pad
    and label are the collection page's resolution, whole (`ut_layout.h` `utLayoutRealPage`, from
    `g_layColl` when a grid line is set; with none the two resolutions are one). They are drawn
    over the real cells they cover; a point inside them is theirs: `utPadMsgDecide`'s `onRealPad`
    keeps a LEFT or RIGHT DOWN on their ground and claims it (`g_padPressAt`), `panelPadClaimsPress`
    refuses the page drop there (the label too), and `hk_GetItemUnderPoint` answers 0 on the REAL
    sack at the page's own sites (`viewRealPageSite`) while the polled cursor is on them or on the
    open editor's parts, or for 400 ms after a press the pad claimed there (`g_realPressAt`: the
    engine reads the press after the message, the cursor may have left the pad's edge by then)
    (`panelRealPadOwnsCursor`): no tooltip, no highlight, no pick-up. That read sits in its own
    guarded frame, the last error saved around it; a fault is one ERROR and the refusal off for the
    session. The editor judges the parts there against the collection's grid line (`editBase`,
    `utEditCheck`) and its refusal names that grid as "the collection's item grid"
    (`utEditWhyText` / `utEditAgainstName`), since the grid it outlines there is the real sack's;
    SAVE never writes the grid there. One INFO line per change (held 250 ms, 12 per game) names the
    cells covered (`buttons: the real Transfer page: ...`).

19. **The engine's 63-character name parts** (`ut_resname.h`). Engine.dll fast-fails (no handler
    sees it) on a part of 64 characters or more between `/` or `\` of a resource name, so every name
    handed to `GraphicsEngine::LoadTexture` / `LoadFont` passes `utResNameOk` right before the call;
    a refused name is one WARN and that feature off for it. A `FileSystem::AddSource` folder is not
    checked: the engine compares a source's own names with a requested name's parts and never copies
    them, and with the mod's arguments AddSource does not scan the folder. A
    painted background carries its game as FNV-1a 32 of the tag (`latticeLeafName`); a game leaf is
    57 characters at most where it is made (`utSaveSetLeaf`: 48, '-', 8 hex digits).

## 3. The architecture, by file

### Start-up order (`src\dllmain.cpp`)

`DllMain` does the minimum: resolve the mod folder, open the log, install the fault watchdog (a
vectored handler that only *logs* an access violation with this DLL on the stack and always
returns `EXCEPTION_CONTINUE_SEARCH`), start one worker thread, return. Nothing that can block runs
under the loader lock.

The worker then does, in this order:

```
configReload(ini)      log_level must be in force before the export lines are written
memSay(...)            `mem: the DLL is loaded`, then the threads' stack reserves line
generateArenaReserve() the 128 MB build arena, only when a build is due (invariant 17)
waitForGameModules()   Engine.dll + Game.dll, 60 s ceiling
resolveExports()       by decorated name; a missing REQUIRED symbol turns the mod off
generateEnsure()       catalogue.bin, the three lists and gray\, current with the game's archives
decodeBindings()       the DECODED and SIGNATURE rows, before the gate
liveInit()             the group table (uniq-groups.txt), packed on the derived 16 x 15 cells (a grid_ line re-packs it: liveSetGrid)
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
| `hooks.cpp/.h` | MinHook: every detour, installed after the gate - the search field's key gate (`Display::HandleKeyEvent`) among them, with `search=1` only; the HUD item rollover's Draw (`hk_RollDraw`, the screen parts' draw site) and the world-up edge that stops the start-up builds |
| `tq_runtime.cpp/.h` | every engine function resolved by name, and the raw structs passed around |
| `tq_exports.h` | generated by `tools\gen_exports_header.py` from the game's export tables (section 5). **Zero addresses** |
| `ut_paths.cpp/.h` | **the only** place that decides where files live. Never call shell32 from here |
| `ut_log.cpp/.h` | the line logger; buffered, five levels, 3 rotated archives; the per-game tee (`logTeeOpen`: a mod's `mods\<leaf>\uniquetab.log`). The tee is GD's shape: while a mod is played every flushed line (a WARN / ERROR, or every line with `log_flush_each_line=1`) costs two `FlushFileBuffers` on the thread that logs it, the flusher holds the lock across both, and the world load that switches the game rotates and opens the tee on the game thread - never per frame, accepted as GD ships it |
| `ut_config.cpp/.h` | one table of settings. The parser, the written file and the test all read that table. The games: a mod's complete own file (seeded once, `seedFrom`; one from an older numbered version is parsed over `seedForGame`, which takes the main file's values only for the visible keys it has no line for and the `[advanced]` rows that version did not have (`keySince`) - never its screen keys, its `[layout]` lines or its commented rows), `uniquetab.state`, `publishLocked`, the persist router (`configPersistStr`), `journal_dir` main-only (`configMainOnlyKey`: a mod's file shows it commented, a live line there is one WARN), `configSetGame` at the world load |
| `ut_folders.h` | where a mod's files live (`mods\<leaf>\`, the leaf from `utSaveSetGame`) and the one-time journal move: all or nothing per set, the `.jsonl` last, a failed rename rolled back; `utJournalDirResolved`, journal_dir as the mod reads it (the same cut as `journal_guard.ps1`'s; tested in test_journal) |
| `ut_generate.cpp/.h` | the DLL side of `src\gen`: keep the generated files current before anything reads them; the custom games built at start-up on their own thread (`gamesMain` / `buildGame`, each game's state for the entry, `gamesOnWorld` / `gamesStopFor`), and the entry's wait and stamp check (`gameBeforeEntry`) |
| `ut_gameprobe.h` | where a custom game's files are, from the root and name `Engine::InitializeMod` receives: `utGameLocate` checks the pair (one folder name, no `..` in the root, the folder and its `Database\<name>.arz` there) and fills a `gen::GameSpec` |
| `src\gen\` | the catalogue generator: `arz_reader` (ARZ v3), `arc_reader` (ARC v1), `inflate` (zlib), `catalogue_gen`, `generate` (with `GameSpec`, GD's view: a custom game's `.arz`, `Text.arc` and archives over the base game's, its leaf from `utSaveSetLeaf`); the groups model `ut_groups.h` (the `uniquetab-groups.ini` parser, the matcher, the auto groups and Other, the built-in table, the G line) and `ut_groups_default.h` (the built-in template as a raw string, `builtinGroupSet`, `loadGroupsFile`) |
| `src\model\` | `catalogue` (the binary format), `collection` (ownership), `layout` (the footprint packer) - pure, no Windows |
| `ut_view.cpp/.h` | the collection VIEW on the Transfer page: the substitution, the one OFF path, the guards, the deposit and take routes |
| `ut_proto.cpp/.h` | the mod sack and the display prototypes: one real `Item` per record of the page, built from the replica |
| `ut_live.cpp/.h` | the page model: the group table, the page / group shown, the OWN filter, the row window |
| `ut_mikind.cpp/.h` | the KIND table: the catalogue's monster infrequents by record (any spelling), their name and item level, and the refusal's words naming the stored roll. Pure; `test_store` links it over `data\oracle\catalogue.bin` |
| `ut_screenprobe.cpp/.h` | the screen parts' input: a detour on the front input device's `GetMouseEvent` (found from the live vtable of `Engine::GetInputDevice()` at the first world up, checked by its bytes) and a generation counter on `Engine::ProcessUserInput`; the original always runs first, the record is read under SEH, the press machine (`ut_padinput.h`) decides it, and a consumed event's type and held bytes are rewritten in their own guarded frame; the per-world proof of the event space and the `buttons: screen input` lines (the 20 event lines at DEBUG, the proof and the consumed-click lines at INFO once per world) |
| `ut_padinput.h` | the press machine (pure, GD's): which event over a screen part is consumed, which press it opens or ends (its UP, the polled release, a new DOWN), the item-on-cursor rule, the editor's adopted drag, the press the layout editor claims (consumed in the window and outside it, its drag and its UP); TQ's 0x28-byte record (`ut_inputrec.h`) |
| `ut_screendraw.h` | the screen parts' frame and the publication the input reads (pure) |
| `ut_caravangeo.h` | the caravan geometry of a game (`uniq-geometry.txt`, written by the generator from the merged database): the grid's origin, size and cells, the cover, the caravan window and the Transfer page's place, and the background's place (the W line's fields 5-6: the window's `BackgroundImage` record's `bitmapPositionX/Y`, GDUT-STAMP 10; a W line of four fields = place unknown, no repaint; `utCaravanBgPlace` refuses a place that leaves the derived grid off the background); derived against measured once per world |
| `ut_cover.cpp/.h` | the lattice follows the grid: the caravan window's background swapped for a copy painted at the grid in force while the collection is on screen, restored at every OFF; the painter thread and the `plates\` folder (a directory source of the engine's file system); the inputs said once per world (invariant 16) |
| `ut_coverstep.h` | pure: the swap's decision per frame and the hand-back of the two textures (`test_lattice` part 3) |
| `gen\lattice_gen.cpp/.h` | the painter (GD's plate painter): the TEX decoded, the vanilla lattice erased, the cells of the grid in force stamped from a vanilla cell on the derived lines, the name per grid (`_L<hash8>`) |
| `ut_gridpack.h` | the re-fit on new cells (pure): every group packed or none, a several-group button's slot counts, `utRowKeep` |
| `ut_inputrec.h` | the engine's `MouseEvent` record (0x28 bytes: type +0x08, x +0x0C, y +0x10, held +0x18), the event types, and the pure byte checks the mod and `test_bindings` share. Pure |
| `ut_panel.cpp/.h` | the pad (15 group buttons, the Transfer toggle - `kUtPadToggle`, which older comments call Collection - OWN, <, >, the search field and its clear button) and the label, drawn inside the caravan frame; the search's slot marks and group-button marks |
| `ut_plate.cpp/.h` | where the Transfer page is on screen, and whether it is on screen at all. `PlateGeometry::scale` is the scale the mod DRAWS at (`ui_scale_pct` applied, `utUiScaleOf`); `engineScale` is `Engine::GetUIScale` itself, which the caravan-frame derivation and the engine-cell fallback use, because the engine places its window and items at its own scale |
| `ut_owned.cpp/.h` | the owned marking: the gray icons, the veils, the slot plates, the ring, the grid cover; the label's whole-collection count (`ownedLabelAll`) |
| `ut_store.cpp/.h` | the private table's owning side: the save set chosen at world load (and with it the game: the tee, `configSetGame`, the journal's folder), the start-up journal move, the deposit / take choke points, the character-save watch |
| `ut_rescue.cpp/.h` | the journal (format TQ-1) and its CSV export. Pure file I/O - not one engine call. `journal_dir` is read once in `journalInit`: a folder that does not exist or cannot be written (one create-and-delete check) is ignored with one WARN; while it is in force a set whose `.jsonl` is still in the mod folder is named once (`sayUnreadModFolderJournal`) - that file is not read |
| `ut_recon.cpp/.h` | the pending rows settled against the character's own containers (read only) |
| `ut_tooltip.cpp/.h` | the item rollover: the "In your collection" line, and the capture flag that keeps a search capture out of it |
| `ut_search.cpp/.h` | the item search (section 4b): the text capture, the index and its schedule, the query, the field and its key gate, the real Transfer page's item cache, the one fault latch. The header's top half is pure |
| `ut_bindings.cpp/.h` | **every fact the mod knows about the game's memory**, in one place, with how it is obtained and how it is confirmed |
| `ut_coverstep.h`, `ut_depositgate.h`, `ut_viewgate.h`, `ut_paintgate.h`, `ut_ownedfold.h`, `ut_rowmath.h`, `ut_padlayout.h`, `ut_protoshift.h`, `ut_gridcheck.h`, `ut_slotart.h`, `ut_graytex.h`, `ut_costprobe.h`, `ut_textfold.h`, `ut_sfnt.h`, `ut_textmeasure.h`, `ut_unionrow.h`, `ut_resname.h` | the pure-logic headers: a decision extracted from its engine context so an offline test can run exactly the code the mod runs |
| `ut_searchsave.h` | pure: the search text kept per game (`uniq-search.txt`): the key line, the rows, the round trip and its refusals, the lookup |
| `ut_mem.cpp/.h` | the `mem:` lines: the address-space walk, the process and heap figures, the engine counters (game thread, one guarded frame), the mod's own figures, the deltas since the index started; the walk, the line and the delta are pure, in the header |
| `ut_retire.h` | pure: the two-deep retire list of the replaced tables |
| `ut_threads.h` | the four threads' stack reserves |
| `gen\gen_heap.cpp/.h` | the build arena and the per-build heap pair (invariant 17) |
| `gen\relics_gen.cpp/.h` | `uniq-relics.txt`: the relic and charm records `collectItems` hands it, in blocks (a record's tiers) and page order; the relic page reads it |
| `ut_relicpage.h`, `ut_relicroute.h` | pure: the relic and charm page (the blocks laid out on its area, the fixed pages, the wheel, what each place shows, OWN, the search's relic rules, the rollover's slot line) and the relic rules of the collection (where a deposited piece goes, which row a take hands out, one shard from the red slot, the CSV cells) |
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
- **Start-up builds thread** (`ut_generate.cpp` `gamesMain`, below-normal priority) - the custom
  games' catalogues, one at a time, each inside a `genHeapBegin` / `genHeapEnd` pair (in the build
  arena while one is reserved). It never
  touches engine memory. Each game's state (pending / building / done) is published under
  `g_slotLock` for the entry; its whole body is under one catch (one ERROR), and it always ends
  with `g_gamesBuilding` cleared and `g_gamesDone` set. It does not raise `g_generating` (the game
  thread keeps drawing, so the stall check stays on). Nothing made inside a pair may outlive it:
  no iostream / locale / regex in `src\gen` (`release_check.py` refuses those headers there), no
  lazily filled global or class-type function-local static on the generator path.
- **Stack reserves**: every `CreateThread` passes its own size with
  `STACK_SIZE_PARAM_IS_A_RESERVATION` (`ut_threads.h`; one INFO line `threads: stack reserves`).

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

**The relic page's texts.** The relic group has no records of its own; its index is one text per
GREEN slot - per (record, bonus) of `uniq-relics.txt` - captured the same way from the synthetic
completed replica the empty slot shows (`captureRelic`: `utReplicaBuildRelic(record, N, bonus)`),
so it holds the relic's name and the bonus's lines in the game's words. The red slot has no text.
The texts sit in `Index::rtext` per relic state (`liveRelicStateIndex` = the row's first state +
slot), are indexed after the groups in the background budget, or whole at the relic page's build
while a query stands (`syncRelic`), and are never written to `uniq-search.txt`. A match sets
`liveSearchSetRelicMatch`; the relic page's highlight is `utRelicSearchHit` through the prototype's
slot; the found count and the relic button's mark are `utRelicSearchFound` (under OWN only the held
blocks), the mark lit once every green slot is indexed.

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
line says when it is complete (`search: index built - 2762 records (...)`: the lines kept and
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
`indexing k/2762` until the index is complete, then `found N` - the matches of every group that
pass OWN. While the journal holds records no page of this game shows (left out by `collect_mi=0`
or `collect_unobtainable=0`), the line ends with ` - +N not on these pages` (`utNotShownSuffix`).
At the label's size the first form that fits is drawn: the full form, the compact one, then (a
query only) the search word's short form (`=N`), then without the suffix, then without the rows,
then without the `all` count and last, with a query only, without the group's owned count; the last
form tried (with no query, the one without `all`) shrinks, down to size 6. The whole-collection
count (`ownedLabelAll`, `ut_owned.cpp`) tests every catalogue record against the journal on the game
thread, and the same pass looks every record the journal holds up in the pages (the suffix's N,
`ownedNotShown`) and in the KIND table (the held monster infrequents, `ownedHeldMi`, the heartbeat's
`mi=`) - at every refresh (the view ON, a deposit, a take), and after a page build only when the
journal set is not the one it was counted on; never per frame. It reads `?` while the journal set is
not known. On the real Transfer page the label is `the real Transfer page` (or `Transfer`), and with
a query `... found N` (`reading k/n` while its items are read), shortened the same way
(`utPadLabelRealLine`). The Relic page's label is short and names no page (the pressed button does):
`64-67/93  page 17/23  8/1971  pieces 50`, then without the fixed page, without the blocks,
without the pieces (`utPadLabelRelic`, five forms); a query adds `found N` / `indexing k/n` to
the first form and the short `=N` / `k/n` to every later one.

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

Memory: the index costs about 321 bytes per record (the engine's text, measured in the game at
432 KB for the 1,588 uniques' records, about 277 B each, plus the heap block, the header, the
string and the state): about 866 KB for the 2,762 records. The `search: index built` INFO line gives
the real figure.

The engine's side is larger: each item made to read its text fills the engine's record cache
(15-47 MB at SVAERA, 43-62 MB at the base game), and that is a high-water of the process heap,
not given back to the address space. So the index starts only at a focus, a query or
`search_prebuild`, per game, and its text is kept per game in `uniq-search.txt` (invariant 17):
a later session's index makes one item, to check the file. `beginWork` (the first indexing) takes
the memory lines' base and reads the file; `saveIndex` writes it once the index is complete.

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

The screen input log's three rows, all ADVISORY (a miss turns the log off alone, the tab is
unaffected): `input.deviceField` (DECODED, early: `Engine::GetInputDevice` reads `+0x164` and the pump
`Engine::ProcessUserInput` reads the same field at +0x43), `input.mouseEventSlot` (DECODED, LATE - the
only late row: the live front device's vtable slot +0x1C is `GetMouseEvent` by its bytes, read on the
game thread at the first world up; its vtable is the one `InputDevice::Create` stores) and
`input.mouseEventRecord` (STRUCTURAL: the 0x28-byte `MouseEvent`, out of the exported `operator=` and
`ConvertToButtonEvent`; the constants in `src\ut_inputrec.h`). A failed advisory late row is one INFO.
It only logs and consumes nothing: its lines prove the inputs (the event's point against the
cursor, the types, the held bytes, the tags, the item flag); that a consumed click never reaches
the world with an item on the cursor is shown in the game by the screen parts' own build.

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
- **The "(another roll)" getters**: `Item::GetPrefixName` and `Item::GetSuffixName` (Game.dll rva
  0x1B8970 / 0x1B8990, `QAEPBDXZ`: non-const `__thiscall` members returning `const char*`), OPTIONAL
  rows of the "Item" block of `gen_exports_header.py`'s `SELECTION`. Each is `cmp [ecx+X+0x14],16;
  lea eax,[ecx+X]; jb; mov eax,[eax]; ret` - the c_str of the item's own replica prefix / suffix
  string, X = 0x2E4 / 0x2FC beside `Item::GetItemReplicaInfo`'s `lea eax,[ecx+0x2C8]` (+0x1C / +0x34
  of the replica). The tooltip decodes the three at start (`utRollGetterOffset` /
  `utReplicaGetterOffset` in `ut_depositgate.h`) and turns only the "(another roll)" line off when a
  getter is missing or reads another slot. The roll read is a pure read inside its own SEH frame:
  the two getters and the seed, the DWORD at the replica's +0x7C (`this + 0x2C8 + 0x7C`, the
  replica's offset taken from `GetItemReplicaInfo`'s `lea`; the tooltip never calls that export);
  a fault turns the line off with one ERROR, never retried. The refusal's words and the log name a
  record by its catalogue name with TQ's colour escape dropped (`{^l}Alala` -> `Alala`,
  `utSearchStripEscapes`, the search fold's own escape rule).
  Only a record the catalogue marks as a monster infrequent (`liveRecordKind` over
  `ut_mikind.cpp`'s table, read from `catalogue.bin` at start) and the collection holds is read; a unique never is.
  `tools\test_bindings.cpp` checks the rvas, the bytes and the convention in the installed Game.dll.
  The hovered object is read only when it passes the vtable test every capture makes
  (`protoIsItem`: its `GetItemReplicaInfo` slot holds `Item`'s). A page box whose newest row could
  not be rebuilt shows the bare prototype (dim, not takeable, its own WARN); that box carries the
  prototype's roll, so hovering it reads "(another roll)" although the record is held.
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
        -> a custom game (GameSpec): the same four files, gray\ and catalogue.stamp in the folder
           the caller passes, <mod folder>\mods\<leaf>\ (tools\build_catalogue.py --game-mod is
           its reference; tools\test_gameview.py compares the two on the installed games)
```

Nothing on the left ships. The package is the `.asi` and three documents (not `CHANGELOG.md`,
section 9); `data\oracle\` is the
harness's and is left out of both the zip and the public tree. `data\oracle\ORACLE.txt` gives the
order to rebuild it from a game folder, read only.

`uniq-groups.txt` is GD's group model: per group one line
`G <n> <label> <cols> <rows> <cellW> <cellH> <count> <id> <tag>` (GD's `formatGLine`,
`src\gen\ut_groups.h`; tab separated) and then one `E <record> <w> <h>` per member. The id is GD's
slot id (`head`, `chest` ...), except the Artifacts' `artifact` (TQ's word: a relic is a
different item in TQ; their catalogue slot word stays `relic`), the tag three letters (`HEL`, `TOR` ...
`ART`, `BLU`: GD's where the group is GD's own, else GD's `tagFrom` of TQ's label; `BLU` is GD's
blueprint tag under TQ's label). The built-in table in
`ut_groups.h` and `SLOT_ORDER` in `tools\build_catalogue.py` are twins, entry for entry. The reader
(`src\model\layout.cpp`) also takes the 1.0 line without `<id> <tag>` and gives it the table's. `build_model.bat` runs the model test over
`tests\uniq-groups-7fields.txt`, a 1.0 groups file, as well.

**The groups are data (GD's groups model).** `src\gen\ut_groups.h` is GD's parser and matcher;
`src\gen\ut_groups_default.h` holds the built-in template (`defaultGroupsIni`, the raw string
between `R"UTG(` and `)UTG"`), which BOTH implementations read: the DLL compiles it and
`tools\build_catalogue.py` (`template_text`, `parse_groups`, `merge_groups`, `assign_groups`) reads
the text between the markers - one source. The template declares the 25 groups of the built-in
table in its order with its labels and tags; each block's `class` is the Titan Quest class that
feeds it (the `kEquipSlot` map; `ItemArtifact` -> `artifact`, `ItemArtifactFormula` ->
`blueprint`), at `rank = Epic, Legendary` (`any` for the artifacts and the formulae); the nine
slots no TQ class fills keep GD's class (declared, empty). `[view:collect_mi]` adds a Rare rule per
equipment class: `generate.cpp` passes the key `collect_mi` only while `collect_mi=1`, and the
collection rule still admits the MIs only then (so `nomi\catalogue.bin` is unchanged). `[auto]
min = 1000`: a class the generator does not know shares the one group Other (a mod's record of a TQ
item class goes to that class's group);
today the collection admits only known classes, so Other never appears. `catalogue_gen.cpp`
`buildLists` tries every record against the blocks (first match wins), writes the groups that
received a record in declaration order (`assignGroups`), sorts inside a group by (rank,
lower-case name, item level, record) - one name's tiers in item-level order, normal < epic <
legendary (`itemLevel`: `levelRequirement` is 0 on almost every monster infrequent; GDUT-STAMP 9), and leaves a record no group takes off `uniq-records.txt` and `uniq-groups.txt`
(it stays in `catalogue.bin`, whose group byte is the slot's, independent of the groups). The
default output is byte-identical to the oracle (test_catalogue, both generators); test_groups
checks the template over `data\oracle\catalogue.bin` gives the oracle's grouping in both cuts.

**The player's file.** `uniquetab-groups.ini` beside the main `uniquetab.ini` (`ut_generate.cpp`:
`utModPathA`): ONE file for every game - a custom game's generation reads the same file, and a
`[view:<leaf>]` block applies in that game only (`utg::viewKeysFor`: a game answers to its leaf,
the start-up game to none; `collect_mi` rides along while it is on); a mod's `mods\<leaf>\` holds
no groups file that is read. `loadGroupsFile` parses it on its own (UTF-8, optional
BOM, CRLF or LF) and `mergeGroupSets` puts it IN FRONT of the built-in set (its rules first, its
new groups first, a built-in id extended); any error refuses it WHOLE: one WARN `catalogue:
uniquetab-groups.ini is ignored - <file>: line N: <reason>; the built-in groups are used`
(printed at every start while the file stands, up to date or not), and the built-in set. GD's
grammar exactly: a reserved id (`vanilla`, `owned`, `next`, `other`, `auto.*`) or tag (`MAT`,
`OWN`) is refused; an unknown class is not an error (the block takes nothing); a built-in id is
extended, not refused. The stamp carries `groups=builtin:<fnv64 of the template>[+user:<fnv64 of
the file's bytes>]` (GDUT-STAMP 7), so an edit regenerates once and `stampMismatch` says "the
groups changed (uniquetab-groups.ini or the built-in groups)". One INFO line per start:
`catalogue: the built-in groups (25)` or `catalogue: groups from <file>: N built-in + M of yours
(K built-in group(s) given more rules)`, with `, G group(s)` after a generation. GD's stock-copy
retirement (`isStockGroupsText`, the `.old` rename) is DROPPED: it cures a file GD once wrote, and
TQ never wrote one. GD's journal count scan is not ported (TQ's generator reads no journal; the
label's "+N not on these pages" is the runtime's). Titan Quest has no measured class -> type word
table, so `classTypeTag` names none and a derived label is the id's last part.

Two decisions bind what the catalogue holds (`tools\build_catalogue.py` and
`src\gen\catalogue_gen.h` list the exclusions): the records only monsters hold STAY (monsters drop
what they hold), and records that share a display name STAY separate (distinct records, distinct
drops).

EXCLUDE rule 7 drops what nobody can obtain: a record whose only holders are fields that never
drop it (`setMembers`, a formula's reagents, the worn slots of an Npc / Guard / merchant / Pet or
of a Monster with `dropItems=0`, the misc loot of those non-droppers). The database cannot see
what the world places, so `src\gen\placed_records.h` lists the catalogue records that
`Resources\Levels.arc` and the `Quests.arc` files name directly (some set pieces have no other
holder); both generators read it, and `tools\reach_scan.py --write-placed` rewrites it.
`tools\reach_scan.py --assert-rule` walks the world from those archives and fails unless rule 7
drops exactly the records it cannot reach (14 on AE 2.10, the monster infrequents below
included); `tools\build_test_catalogue.bat`
runs it, so a game update that moves a holder is caught there - rerun `--write-placed`, then
the fixtures. `collect_unobtainable=1` keeps the rule 7 records (listed as kept); it is a
`catalogue.stamp` input, a line only while it is on, and a flip names the key in the log.

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

**The relics and charms (`uniq-relics.txt`, `GDUT-STAMP 12`).** `collectItems` (when given the two
relic vectors) and `build_catalogue.py` take every `ItemRelic` / `ItemCharm` record (the class decides
relic or charm) under the collection's filter (no creature folder, a name tag that resolves, the
template rule) whose completion bonus table (`bonusTableName`) names at least one bonus of weight
above 0 (`relicBonuses` / `relic_bonuses`: `randomizerName<n>` in n order, a repeat folded by case
and slashes kept once, as the table spells it first), through the same EXCLUDE rule; the icon is
`relicBitmap` and must be 1 x 1 cells. A class record with no such table (a formula, a currency or a
placeholder using the class) is no candidate and in no list. `relics_gen.cpp` / `relic_order` join a
record's tiers into a block by the basename (`0N_<stem>`, else `<stem>_n|_e|_l[_<rest>]`, else
`<stem>_dN[_<rest>]`; anything else is a block of its own, tier 0) and order the blocks: relics, then
charms; by expansion (an `x4_` basename after its `0N_` is Eternal Embers), the first row's
`levelRequirement`, the block id. The file: `B <n> <block id> <R|C> <rows>`, then the block's
`R <record> <tier> <shards> <relicBitmap> <shardBitmap> <bonus count> <bonus>...` lines, then
`X <record> <reason>` for the records the EXCLUDE rule left out (uniq-excluded.txt does not list
them). The relic candidates take part in `reach_scan.py` (`--write-placed`, `--assert-rule`), so
`kUnreachedRecords` holds the Epic and Legendary Power of Nerthus (only reward tables nothing reaches
name them; the Normal tier's quest chest is placed). The gray copies of every `relicBitmap` and
`shardBitmap` the list names are `gray\ug<hash of the icon path>.tex` (`writeGrayRelicIcons`,
counted apart from the records' gray icons). The relic page reads the file (`src\ut_relicpage.h`;
the `relics` G line of `uniq-groups.txt`, the default buttons file's `[REL]`), and the deposit gate
routes a relic or a charm through `src\ut_relicroute.h` (section 7). Base game: 255 class records, 93 blocks
(53 relic, 40 charm), 253 rows, 1,971 bonus slots, 2 X lines.

**The collection rule is every layer's (GD 1.2.1 6c3162e; `GDUT-STAMP 11`).** There is no path
rule: a record of a collected class and rank is a candidate anywhere in the database but a
creature folder (`junkPath` / `junk_path`: a path component `creature` or `creatures`, the gear
NPCs and monsters wear; GD's junk folders), in the base game and a custom game alike, when its
name tag resolves. The template words (`templateWords` / `template_words`: blank, test, template,
copy of) match whole words only (no letter a-z on either side: `gigantestorso`, `whitestalker`
no longer hit) and are decided after the scan: a template record with a drop holder (another
record holds it in a field that can drop it) or in `kPlacedRecords` stays a candidate, one with
neither is no candidate (in no list, as before). EXCLUDE rule 4 counts `kPlacedRecords` as a
reference (`reach_scan.py --write-placed` lists a record no database field holds when a quest
archive names it - the Lupine Necklace Design of `x2sq20_trapper.qst` - but not one only a
level's record table names, so `x2sq25_secretrecipe`, in `world01.map`'s table, stays
unreferenced, as do the three `x3sq10_materialrecipe` formulae, as decided for the four quest formulae). The reach
walk (`reachedKeys` / `reach_walk`, one pass beside the reverse index): from every record that is
not a loot table (Class `Loot*` or no class: a monster, a chest, a merchant, a formula, an NPC)
down the fields that can drop, through the loot tables, never entering a holder in a developer
folder or with a template word in its path (`walkSkipped` / `walk_skipped`). Rule 3 drops a
developer folder's record only when the walk does not reach it (SVAERA's six `sandbox/chris`
jewels are collected; the base game's test items and the `_devcheat` staff, held by a monster in
`zz_dev/charles - monster test folder/`, stay out). Rule 7 drops a record outside the item folders
(`itemPath`, `records/(xpackN/)item(s)/`) that has a drop holder but the walk does not reach
(SVAERA's four `drxitem` wands: held by `LootItemTable_FixedWeight` tables only `LootMasterTable`s
nothing names hold) as `unobtainable: only in loot tables nothing reaches`. Inside the item
folders the walk does not decide: it reads the database only, and the base game's quest rewards
are loot tables only a quest file names (`records/quests/rewards/*.dbr`), so it would cut the
Secreto armour and the Dragon King weapons; there `kUnreachedRecords` decides as before. A Quest
formula is packed as Epic like a Common one. AE 2.10: 1,715 candidates (`CANDIDATES_EXPECT`) and
1,118 monster infrequents (`MI_CANDIDATES_EXPECT`), 2,774 records after EXCLUDE (+12: the six
Eternal Embers formulae, Alcyoneus' Breastplate x3, the Lupine Necklace Design x3; 59 listed in
`uniq-excluded.txt`); every custom game keeps every record it had (TQ Retold +917, SVAERA +156,
the others +12). The upgrade: every set regenerates once at the next start; on a page that gained
an item the boxes after it move one place along; the collection file is untouched.

**The relic page on or off** (`collect_relics`, `[general]`, default 1, ini_version 27; a stamp
input as `collect_mi`: `collect_relics=1` is a stamp line while on, a flip regenerates every set
once and the reason names the key). Off, `generate.cpp` drops the two relic vectors right after
`collectItems` - the catalogue and its four lists are the same bytes - so `uniq-relics.txt` is
empty, `uniq-groups.txt` has no relic G line, no gray relic icon is written, and the summary ends
`(collect_relics=0: no relic page)`. At run time `readRelics` installs no table for a file with no
row (one INFO line), so `liveRelicFind` is -1 for every record: the deposit gate refuses a relic as
`not a unique of the collection`, the tooltip has no relic line, the stored relic rows stay in the
journal (the label's `+N not on these pages` counts them; the CSV export writes them, with empty
tier and shards cells: those come from the relic table). The buttons
file: `btnOffIds` (ut_panel.cpp) names `relics` while the game's table has no relic group, and
`utbResolve`'s `switchedOff` lets a section naming it hold nothing - its tag goes to `offTags` for
one INFO line - instead of refusing the whole file; the late offer and the `[unassigned]` rewrite
read only the table's groups, so neither adds it, and with the group back the section's button is
back. A game's own file from 26 takes the main file's value (a `[general]` key it has no line for,
filled by `seedFrom`; `keySince` lists it at 27 for the record, that table decides only for the
hidden rows). A version-13 stamp written before this key has no `collect_relics` line, so the
first start of this build regenerates every set once with the reason `collect_relics changed to
1 (the relics and charms have their page)` although the player changed nothing: expected, the
stamp gained the line.

**The monster infrequents** (`collect_mi`, default 1, a stamp input: `collect_mi=1` is a stamp
line while on). Every EQUIP_SLOT record whose own itemClassification is Rare passes the same
name / template filter (`is_mi_candidate` / `isMiCandidate`; 1,118 on AE 2.10, asserted as
`MI_CANDIDATES_EXPECT` beside `CANDIDATES_EXPECT = 1715`) and the same EXCLUDE rule:
1,099 stay, 2,681 records with the uniques (the arcane formulae below make 2,774). Each carries `isMi` = catalogue.bin flag bit 8
(`ItemFlag::MonsterInfrequent`, `catIsMi`; the format stays v2, an old file has the bit clear),
is packed as Epic, and sorts after the uniques of its group (catalogue.bin: group, KIND, item
level, name, record; the lists: the rank Rare after Legendary and Epic, one name's tiers by item level). The oracle is the
collect_mi=1 cut (`build_catalogue.py --collect-mi`); `data\oracle\nomi\` is the collect_mi=0
cut (the same tool without the flag), which the harness compares with the DLL's collect_mi=0
output. EXCLUDE rule 7 reaches the monster infrequents through a second list in
`placed_records.h`: `kUnreachedRecords`, the catalogue records `reach_scan.py`'s walk from the
level and quest archives cannot reach. A record whose only dropping holders are loot tables
nothing reaches (seven monster infrequents on AE 2.10: the Yerren greaves, the three troll-mage
torsos, Zazamankh's three heretic staves) is unobtainable with the reason
`unobtainable: only in loot tables nothing reaches`; the per-reference rule runs first, so the
other records keep their holder kinds. For those seven the walk is the rule itself, not a check
of it: `--assert-rule` agrees with rule 7 on them by construction (`kUnreachedRecords` is the
walk's own output), while for the six uniques and Zazamankh's boss staff the per-reference rule
stays an independent check. A game update that gives one of their loot tables a holder is
caught only by rerunning `--write-placed`.

**The arcane formulae** (the blueprints page). Every `ItemArtifactFormula` record
(`BLUEPRINT_CLASSES` / `kBlueprintClasses`) at every classification passes the same path / name /
template filter (106 on AE 2.10; a formula whose own name tag does not resolve takes the crafted
artifact's name) and the same EXCLUDE rule: 93 stay, 2,774 records in all. Each carries
GD's blueprint flag (`F_BLUEPRINT`, catalogue.bin flag bit 1: on every formula, on nothing else),
a Common or Quest one is packed as Epic, and they fill group 15 after the Artifacts (`blueprint` / `BLU`,
the built-in table's row and the default buttons file's `[BLU]`) in 2 x 1 cells. The nine Atlantis
formulae (`records/xpack3/items/artifacts/arcaneformulae/`) go to EXCLUDE as unreferenced; their
description tag does not resolve, so the stats table lists them without a name. Six Eternal Embers
formulae (`records/xpack4/.../l_da_*_formula.dbr`) carry the FileDescription
`Test - Arcane Formula`; loot tables hold them, so the template words do not drop them.

The tooltip: `ItemArtifactFormula::GetUIDisplayText` is the sixth tooltip detour. The engine
builds the crafted artifact inside it, and that artifact's own text detour runs there; a
per-thread depth (`t_formulaDepth`, raised around the formula's original call and lowered in a
`__finally`) turns the inner latch into a no-op, counted as `innerSuppressed` in the tooltip
summary line, so a formula reads its own line and never the artifact's.

Memory: the generation's peak working set with the 1,096 monster infrequents and their gray
icons is about 83 MB in `test_catalogue`'s fresh x86 process (`--peak`: +77 MB over its start;
the uniques alone +75 MB). In the game the rise is about three times the harness's (the uniques
alone: +228 MB, process peak 366 MB), because the game grows during the same seconds, so expect
about +235 MB and a process peak near 375 MB against TQ.exe's 2,048 MB of user address space
(no large-address flag). The logs give the working set, not the virtual size.

**A catalogue per game (`GameSpec`, GD's view).** A custom game (`GameInfo::GetModName` set, its
folder `mods\<leaf>\`) gets a generated set of its own; nothing compares it with the base game
first. `gen::GameSpec {root, modName}` is built from what `Engine::InitializeMod(name, folder)` receives, and
`name()` is the leaf `utSaveSetLeaf` + `utSaveSetGame` give (one leaf function: the folder, the
journal and the generated set never disagree); `ut_gameprobe.h` `utGameLocate` checks the pair (one
folder name, no `..` in the root, `<root><name>\Database\<name>.arz` a file; the `Resources` folder
may be absent). The layers, in the engine's order: `ArzDatabase::loadGame(gameDir, err, gameArz)`
stacks the game's `.arz` LAST (tag `game`: its records win, a record only it has comes after the
base game's and carries `fromMod`, which a player's `source = mod` rule sees); `loadTextTags`
merges the game's `Resources\Text.arc` after `Text_<lang>.arc` (a tag it defines replaces the base
one); `ResourceArcs::setRoot(gameDir, gameResources)` searches the game's archives FIRST, entry by
entry - `<res>\[XPack<n>\]<Arc>.arc` as the base game names them, then, for an expansion path,
`<res>\XPack<n>.arc`, whose entries keep the archive's name (`items/...`) - and falls to the base
game for a path none of them holds. The rules are the base game's, unchanged: the class decides
the group and an unknown class goes to Other; the collection rule is every layer's (no path rule,
the creature folders aside), so a game's records anywhere in its database are candidates, and
outside the item folders the reach walk decides rule 7; EXCLUDE otherwise as it is. A game's record held in a
droppable field of the game's own loot tables has a drop reference, so rules 4 and 7 keep it;
`placed_records.h` (`kPlacedRecords`, `kUnreachedRecords`) are scans of the base game's levels and
quests and never name a game's record, so a game's record whose only holders are non-dropping
fields (one the game places through its own levels or quests) is dropped as unobtainable and listed
(`collect_unobtainable=1` keeps it), and a base record the game makes reachable stays dropped. The
view keys: `utg::viewKeysFor(leaf)` plus `collect_mi` while the flag is on. The outputs go to the
`modDir` the caller passes - the game's folder - so the gray icons, named by record path alone, get
a `gray\` of their own and an icon the game overrides never meets the base copy. The stamp:
`game=<leaf>` after `lang=`, then the base game's lines, then the size + mtime of the game's `.arz`
and of every `.arc` of its `Resources` folder and of the folders in it (each list sorted, absolute
paths as labels, GD's `customLayerFiles`); the start-up game's stamp is byte for byte what it was.
The Python twin: `tools\build_catalogue.py --game-mod <root>\<name>` (`GameSpec`, `save_set_game`,
`tqarz.ArzDatabase.load_game(..., game_arz=)`, `tqarc.load_text_tags(..., game_resources)`,
`tqarc.ResourceArcs(game_dir, game_resources)`); the AE 2.10 count asserts hold for the base game
only. `test_catalogue` pass 6 (`tools\test_gameview.py`) takes every game under
`TQ_TEST_MODS_DIR` (else one skip line): `--game-view` in a process of its own per game, the four
files byte-identical with the Python reference, the gray icons counted and three sampled (their
size; the reference writes none), and the measurements. The largest installed game measured
9,009 records (4,695 in Rings, 164 Artifacts, 1,171 Blueprints), 8.9 s, a peak rise of +169 MB
(the base game +77 MB), 84.8 MB of gray icons, the search index about 321 B per record (2.8 MB);
the owned tables cost 192 KB at 8,192 and 768 KB at 32,768.

**The game switch at run time (`hooks.cpp` `hk_InitializeMod` / `hk_CleanseDatabase`,
`ut_generate.cpp` `gameBeforeEntry` / `gameAfterEntry` / `gameEnterStartup`).** Both detours are
bound by export name and go in as a pair or not at all (a miss: one WARN, the start-up game's files
serve every game). `InitializeMod(name, folder)`, BEFORE the original (the game thread, no world): the
two `std::string` reads under SEH (a fault: the switch off for the session, one ERROR), then
`utGameLocate` -> the `GameSpec` and the leaf, `mods\<leaf>\` made, the game's log tee and its
`uniquetab.ini` in force (`storeFollowGame`; the main and the custom-without-mod sets keep
the world-load path), the free address space logged, the 64 MB floor, then
`ensureOutputsGuarded(gameDir, mods\<leaf>, lang, ..., &spec)` with `GenInputs::mayGenerate` =
`neverAtEntry` (a set the stamp does not call current = the not-built WARN, the tab off, nothing
written). Before all of that, while the start-up builds run: the entered game becomes the next one
built, the builds stop after it, and the game thread waits while its own build is pending or
running (`gamesLeafState` polled every 100 ms against `g_gamesDone`, at most 10 minutes) - the
wait comes before `storeFollowGame`, so the other games' lines stay in the main log. THEN the
original. After it:
`utGameEnter` (one switch or none) and `gameResets`, which runs `ut_gameswitch.h`'s step table
(`utGameResets` over `kUtResetOrder`, each step once per switch): the world teardown first, only
when the world-gone edge had not fired; then what points into the old game's records (the
prototypes, the owned counts keyed by its ids); then the group / KIND / union tables they came
from, the search index built from those, the panel's gray cache and button table, and last the
game's `uniquetab.state`. Every step only drops - the new game's are built when a reader asks, and
the old tables are never freed - so only "the world first" is load-bearing; the rest is the chosen
order, and `test_viewgate` holds it. An entry that cannot be completed (the locator refuses the
folder, or the entry throws) follows `utGameEntryFailed`: a game it had named is entered with the
tab off; with none named, a custom game in force turns the tab off over the start-up files (one
switch) until `CleanseDatabase`, and the start-up game in force keeps its files. `InitializeMod`
empties FileSystem partition 0
(`test_bindings` reads the `push 0` before its `RemoveSourcesFromPartition` call); the start-up
gray folder and the `mods\` folder are partition-1 directory sources, registered once each, and a
custom game's gray icons load as `<leaf>/gray/ug<hash>.tex` through the `mods\` source, so no name
(and no engine texture cache entry) is shared between two games and no source order matters.
`CleanseDatabase` (after the original): back to the start-up game, the main settings, the resets.
At the world load `storeSelectSet` checks the save set's game against the entered one (a WARN when
they differ; the world's set decides the settings and the journal).

## 7. The journal format

`tq-uniq-items.jsonl` (and the `-custom` / `-<mod>` sets), JSON Lines, UTF-8. The full spec is the
file header of `src\ut_rescue.cpp`; this is the shape.

```
line 1   {"journal":"titan quest uniquetab","format":1,"written":"<ISO-8601 UTC>",
          "set":"<leaf>","entries":N,"collected":C,"pendingIn":I,"pendingOut":O}
line n   one stored COPY: "record" (THE KEY, folded), "deposited", "stack", "base" (byte-exact),
         the other six replica strings (empty ones included), "seed" "var1" "var2", "b8",
         and optionally "pending" ("in" / "out") with "taken"; a split "in" row of a relic or
         charm also "depositedVar1" right after "pending" (below)
```

**The row count governs.** Two byte-identical lines are two copies, never merged.

**Relic and charm rows (several per record).** A relic or charm record keeps any number of rows
(`ut_depositgate.h`: `relicKind` skips the one-copy refusal). `src\ut_relicroute.h` decides, purely:
a partial piece (var1 < N, no `relicBonus`) belongs to the record's red slot, a completed one
(var1 = N, a `relicBonus` its table lists) to the green slot of that bonus; anything else is
refused. A green take hands out the OLDEST row of the slot not pending out (settled rows first):
`journalTakeRow(key, seq)` refuses any other row as a stale page. The red slot's take gives ONE
shard (`journalTakeShard`): a row of one shard is taken whole (today's take); otherwise the oldest
settled partial row, else the oldest one pending in, is SPLIT in one atomic write - the source keeps
its seq, state and identity with var1 one lower, and a new row of the same identity with var1 = 1 is
appended `"pending":"out"` (its `deposited` = the source's). That row is the shard handed out, so
every rule above settles it exactly as any take. A split source pending in also gets
`"depositedVar1":k` (u32, written right after `"pending":"in"`): the shard count of the item the
character file on disk still holds; the container check looks for that count
(`journalUnresolvedRows`), and the key goes when the row settles. A reader that does not know the
key skips it (format 1 unchanged): it then looks for the row's own var1 and settles it as absent -
a duplicate at worst, never a loss. The relic page's red slot makes these takes; `test_journal`
section R drives every settle path of a split.

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

**The game switch leaves the journal alone.** The journal per save set is chosen at the world
load exactly as before (`storeSelectSet`, `utSaveSetLeaf`); `InitializeMod` only brings the game's
settings forward. `test_store` opens a mod's set after another game was entered and the main set
after the switch back, with its rows.

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

Twenty offline suites: every `tools\build_test_*.bat` and `build_model.bat`. **All twenty must be
green before any commit that touches `src\` or `tools\`.** Run them sequentially - they share build output directories (two runs in one tree at
once collide on the build's PDB).

| suite | what it covers |
| --- | --- |
| `tools\build_test_bindings.bat` | every export name resolved in the installed DLLs, the signatures counted in `TQ.exe`, the decoders, and the class/phase/gate classification row by row |
| `tools\build_test_catalogue.bat` | the whole generator end to end against a 2.10 game folder: the outputs byte-compared with `data\oracle\`, a fresh install into empty folders, the stamp, the error paths, the peak working set; with `TQ_TEST_MODS_DIR` set, every custom game under it byte-compared with `build_catalogue.py --game-mod` and measured (`tools\test_gameview.py`; without it one skip line); pass 7 builds the start-up game and then every custom game inside ONE 128 MB build arena (the high-water per game, nothing outgrown, no foreign free, the release in one piece) and pass 8 reads every set through the in-game reader on its own area (`--reader` repeats it over an installed game folder) |
| `tools\build_test_arena.bat` | the build arena `src\gen\gen_heap.*`: the reserve, the free lists, split and merge, the reset and decommit per pair, the outgrow path, a free from another thread refused, the release refused while bound, a churn of small blocks timed against a private heap |
| `tools\build_test_config.bat` | the settings table: every row's default matches its member, the writer, the parser, the clamp, and the path rules |
| `tools\build_test_journal.bat` | the journal TQ-1: reader and writer, the format gate, the pending rows, the reconciliation verdicts; then `--load` over `data\allitems\tq-uniq-items-all.jsonl` (every row read, none dropped, the take's round trip for every record) |
| `tools\build_test_proto.bat` | the prototypes' replica assembly, over every catalogue record |
| `tools\build_test_search.bat` | the search's pure half, no game and no fixtures: the staging and the free of a captured line vector over a fake engine vector (inline and heap strings, the small-string edge, malformed shapes refused, every free counted), the keep rule, the fold, the matching, the highlight and the count, the key gate's decisions and the field's edits, the Backspace repeat, the group marks, the mark's colour and geometry, and the real Transfer page's map walk over a fake VS2012 map with its label forms; the search text kept per game (`ut_searchsave.h`: the round trip, each key part refusing alone, a damaged row refusing the whole file, a 9,009-row file's time) and the index's start across a game switch |
| `tools\build_test_store.bat` | the private table and `ut_depositgate.h` |
| `tools\build_test_tooltip.bat` | the tooltip line |
| `tools\build_test_viewgate.bat` | the view's ON/OFF state machine, every event order up to depth 7 against a simulated Transfer page; the custom game entry's decisions (`utGameEntryStep`) and the start-up builds' order and per-game wait (`utGamesNext`, `utGamesEntryWaits`); the retire list's lifetimes, the memory line and its arena part, and the per-game data latch; the caravan re-opened in place (`utCaravanInteract`: a goodbye inside `OnPlayerInteract` is a close, then an open, and the view's state is open again) |
| `tools\build_test_relicpage.bat` | the relic and charm page (`src\ut_relicpage.h`) over the oracle's `uniq-relics.txt`: the list, the fixed pages at 16 x 15 and 20 x 19, the wrap, the dead cells, the window of whole blocks, the wheel (one block) and `<` `>` (a fixed page), what each place shows; OWN (`utRelicBlocksHeld`, the layout of the held blocks with every slot in place), the search's relic rules, the have-mark key, the rollover's slot state and a partial piece's one line (`utRelicTipLine`) |
| `tools\build_test_buttons.bat` | the buttons file `uniquetab-buttons.ini`: the grammar and its refusals, the resolve against the groups, the `[unassigned]` rewrite, a new built-in group offered once (the rewrite holds it out until its offer ran; only the "added once" line is an opt-out - a player's SVAERA file the rewrite reached first gets `[SOU]`; the group table the offer reads is recorded at the game switch when the settings already follow the entered game, else at the tab's first open), the default file byte for byte against the Python twin |
| `tools\build_test_text.bat` | the words of the buttons file's `[tab]`: UTF-8, the placeholders (`{found}` included), TQ's four label forms, and the label's fit against the earlier fixed cascade |
| `tools\build_test_layout.bat` | the layout model `src\ut_layout.h` and the pad's pages `src\ut_padpage.h`: the `[layout]` text, the pad fitted to a rectangle (paged when narrow), the label, the grid part (`grid_<W>x<H>`: its whole cells, the box cap, the floor), the resolution of the grid, the pad and the label against each other, the screen parts' frame and publication, the layout from the measured page over the UI scale sweep, the game's own Transfer page (`utLayoutRealPage`, `utRealCellsCovered`, `utRealPadText`), and the re-fit on new cells (`src\ut_gridpack.h` over `src\model\layout.cpp`: every group re-packed or none, the pages, `utRowKeep`) |
| `tools\build_test_layoutedit.bat` | the in-game layout editor `src\ut_layoutedit.h`: the handles and the toolbar, every clamp of a drag (a sweep: nothing leaves the window, nothing SAVE allows meets the grid), SAVE / CANCEL / RESET and every cancel rule, the refusal with an item on the cursor, and the round trip of the lines SAVE writes over the UI scale sweep; the page's pick-up refused under the editor's press (`utEditOwnsTake`), and the editor's grid against the drawn grid (10c); the shown group's slots as its ghost lines and the items hidden while it is open (12); a part over the game's own Transfer page's cells judged against the collection's grid (13); the press the editor claims, its handle past the window's edge (14); RESET on the real page and the grid's line (15); a label in a screen pad judged on the screen (16); the EDIT button: the editor's pad floor and a moved pad's label box one cell later (E) |
| `tools\build_test_groups.bat` | the groups model `src\gen\ut_groups.h` / `ut_groups_default.h`, no game: the template (its 25 groups = the built-in table, the 14 collect_mi rules, `[auto] min = 1000`), the glob, first-match precedence and views, every refusal naming its line (GD's words), the auto groups and Other (a mod's unknown classes), the G line, the player's file (absent / loaded / refused; never written, renamed or created), the merge (and README's two examples over the shipped catalogue), the derived words, and the template over `data\oracle\catalogue.bin`: the oracle's grouping with collect_mi=1 and `nomi\`'s without |
| `tools\build_test_lattice.bat` | the lattice painter `src\gen\lattice_gen.*`: a synthetic background sampled pixel by pixel, the base game's caravan background painted for a moved 16 x 12, the swap's decisions over a ledger of the textures' counts (`src\ut_coverstep.h`: a new grid while the copy is in, OFF -> ON, the engine's own write, the window gone or replaced, the world down, a failed write-back), and with `TQ_TEST_MODS_DIR` SVAERA's 20 x 20 background painted for 20 x 19 and a moved 16 x 15 |
| `tools\build_test_padinput.bat` | the press machine `src\ut_padinput.h` over TQ's event record: GD's cases (the item on the cursor, the ground, a button, the polled release, a repeat, the editor's adopted drag); and the window procedure's clicks (`utPadMsgDecide` / `utPadMsgPoll`: the latch keeps its button, a lost release, a right press held, swapped buttons, a right release the pad never pressed, the pad and the label on the game's own Transfer page: their ground kept and claimed); the layout editor's claimed press, in the window and outside it, its drag and its UP (14); the EDIT button's press in the window, on the real Transfer page and on a screen pad (15) |
| `tools\build_test_journalguard.bat` | the uninstall guard `tools\journal_guard.ps1` and `undeploy.bat --purge` (`tools\test_journal_guard.ps1`) on temp folders under `build\`: a row, a pending row, an empty or unreadable journal refuse, in the mod folder and in every `mods\<mod>\`; a `tq-uniq-items*.jsonl` of any other name is judged like a journal; after a purge `mods\`, the journals, the settings and every unknown file are still there |
| `build_model.bat` (repo root) | `tests\model_test.cpp` - the pure model over `data\oracle\catalogue.bin`; its first step packs `data\oracle\catalogue.json` into the bin only when the json is newer (`tools\pack_catalogue.py --if-newer`; `--dry-run` says what it would do), so a stale json never overwrites the committed bin |

One environment variable: `TQ_GAME_DIR` names the game folder (the one that holds `TQ.exe`),
opened **read only**; without it the Steam registry's default library is used. `TQ_TEST_MODS_DIR`
names a folder of custom games (the Steam workshop content folder of the game): `catalogue` then
generates every game in it, compares each C++ set with the Python twin and prints each game's
caravan geometry, records, groups and generation peak. `bindings` and
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
2. Build, and run the twenty suites (section 8).
3. `powershell -ExecutionPolicy Bypass -File tools\package.ps1` stages
   `out\UniqueCollectionTab-TQ-<version>\` (`scripts\uniquetab.asi`, `README.md`, `LICENSE`,
   `THIRD_PARTY.md`, and `extras\tq-uniq-items-all.jsonl` + its report from `data\allitems\`) and
   zips it from inside, so the zip mirrors the game folder. It refuses to stage anything else
   generated. The extras are rebuilt from the oracle after every catalogue change:
   `python tools\make_all_items.py --records data\oracle\uniq-records.txt --out
   data\allitems\tq-uniq-items-all.jsonl` (its report is written beside it).
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
- **Proton / Steam Deck are untested.**
- **The export dumps and the fixtures come from an installed game.** `tools\tqexports.py` and the
  `data\oracle\` rebuild need a 2.10 game folder, as the bindings and catalogue suites do; neither
  is committed.
- **The packaged build is a person's to QA.** A clean install from the zip itself is checked by
  hand, not by the offline suites.
- **Where a large custom game's address space goes after a visit is not settled.** The `mem:`
  lines exist to compare the same route with the tab on and with `enabled=0`: heap reserved growth
  far larger with the tab on points to the engine's record cache filled by the search's items;
  equal growth points to the game itself; private memory outside the heap growing with the tab on
  only points to the gray icons' textures. Indexing only the shown page at the first query waits
  for that comparison.
- **The painter thread's deepest stack frame is not measured.** 512 KB is a wide margin, not a
  measurement.
- **The engine counters' offsets** (`ut_mem.cpp`: the record count at ObjectManager+0x9C, the
  objects at +0x24, the pending queue at +0x80/+0x84, the seven resource managers) were read from
  the code; the first `mem: the engine counters' inputs` line of a session shows their values.

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
