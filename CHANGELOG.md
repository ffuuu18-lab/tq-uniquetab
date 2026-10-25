# Changelog - Unique Collection Tab for Titan Quest

What changed in each release, newest first. `README.md` says how everything works; this file says
when it arrived.

## 1.1.0 - 25 October 2026

Requires Titan Quest Anniversary Edition 2.10 (the 32-bit `TQ.exe`) with the x86 build of
Ultimate ASI Loader.

### New

- **Custom games get their own collection.** Every custom game installed - the workshop items and
  the custom quests in your Documents folder - gets its own catalogue, pages, gray icons, settings
  file, buttons file and collection journal, all in `scripts\uniquetab\mods\<mod>\`. They are built
  when the game starts, in the background, one game at a time, while the address space is still
  clean; the menu stays usable. Loading a custom game later only reads its files, and nothing is
  built inside a running game. The first start after you install or update custom games works for
  about a minute and a half in the background (49-85 MB of gray icons on disk per game); a game
  you load while its own build runs waits for that game only, and a mod installed while the game
  runs is built at the next start. A game's own `uniquetab.ini` is a complete copy of the main
  one's values at its first entry, and from then on its own. Each game's caravan is read from its
  own database, so a larger caravan gives a larger collection (SVAERA: 20 x 20 cells, 20 x 19 with
  the buttons in the window). An item class none of a game's groups knows gets an Other group with
  its own button, and SVAERA's soul rings have a Souls page of their own (`[SOU]`). Built and
  played here: SVAERA, Itemization Quest, TQ Retold, Cataclysm and the LootPlus variants.
- **The monster infrequents join the collection**: every Rare item named after its monster, each
  level tier (normal, epic, legendary) its own slot, side by side in level order after the uniques
  on their group's pages - 1,099 records. One copy per box holds for them too: a second roll of one
  you hold is refused, the log names the roll its box already holds, and hovering another roll
  reads "In your collection (another roll)". `collect_mi` in `[general]` (default 1): 0 leaves
  them out.
- **The arcane formulae have a page of their own** after the Artifacts (`[BLU]`), each in a cell
  two squares wide: 93 records. Hovering a formula says whether the formula itself is in your
  collection, never whether the artifact it makes is. With them the base game's collection holds
  2,774 records, and the label's total reads `/2774`.
- **The relic and charm page** (the **Relic** button, `[REL]`, added once to a buttons file that
  has none). Each relic or charm is a block with one row per tier; a row has a shard slot, which
  takes the partial pieces and counts their shards, and one slot per completion bonus, which takes
  the completed relics or charms with that bonus. These slots keep any number. Taking from a shard
  slot gives one shard at a time; a bonus slot gives back the oldest one stored. The wheel moves one
  block, `<` `>` and PageUp / PageDown a fixed page, and the label is short:
  `64-67/93  page 17/23  8/1971  pieces 50` (the blocks shown, the page, the bonus slots holding
  something, the shards stored). Every relic or charm tooltip - in your inventory, stash and Relic
  Vault too - says whether that record with that bonus is in your collection ("(another roll)" when
  only its other bonuses are), and a partial piece says `In your collection (pieces 15)`. The
  search finds a bonus slot by its bonus's words and marks the Relic button; OWN shows only the
  blocks that hold something; `have_marks=1` ticks an empty bonus slot whose relic you carry. The
  page stands on the caravan's own background with the cell lattice erased, each block on its own
  framed panel (gold for relics, a bronze patina for charms). Under `[advanced]`: `relic_panels`
  (default 1; 0 = no panels), `relic_block_gap` (0..2) and `relic_bonus_gap` (0..1), the empty
  lines between blocks and the empty columns before the first bonus; under `[layout]`
  `relics_<W>x<H>=x,y,w,h` gives the page its own area, and the layout editor opened on the page
  writes it.
- **`collect_relics`** in `[general]` (default 1), as `collect_mi`: 0 takes the relic page away -
  no Relic button (a section of the buttons file that names `relics` simply gives none, one line
  in the log, and the rest of the file stays in force), no gray relic icons, and a relic or charm is
  refused like any item outside the collection. The relics and charms you stored stay in your
  collection file untouched, hidden until it is 1 again (the CSV export still lists them). A change
  rebuilds the catalogue once, at the next start.
- **The layout editor.** Open the caravan on the Transfer page and click **EDIT** (right of the
  search field; `edit_button=0` under `[advanced]` removes it), Ctrl+right-click the buttons, or
  set `layout_edit=1`. The item grid, the buttons and the label become rectangles with handles:
  drag to move, drag a corner or an edge to resize. The grid snaps to whole cells and takes as many
  as fit, up to 17 x 19 in the game's caravan window; every group is laid out again on them and the
  row you were on keeps its first item. The buttons and the label may leave the window: drag them
  out and they are pinned to a corner of the screen (`pad_screen` / `label_screen`), where a click
  never moves the character, fires a skill or drops the item on the cursor. While the editor is
  open the page's items are hidden and its slots drawn as faint lines. SAVE writes the `[layout]`
  lines (`grid_565x637`, `pad_565x637`, `label_565x637`, also by hand), CANCEL writes nothing,
  RESET puts everything back. It opens on the game's own Transfer page too, where the sack stays
  fixed. Every press the editor takes is its own: it never reaches the page or the world.
  `layout_outline=1` outlines every part with its numbers. On the game's own Transfer page the
  buttons and the label stay where they are on the collection page, drawn over the sack's cells
  they cover; those cells are theirs (no drop, no tooltip, no pick-up under them).
- **The lattice follows the grid**: while a collection page with a grid of your own is on screen,
  the caravan background is shown with its cell lattice repainted at your cells (painted once in
  the background, kept in `plates\`). Everything the mod draws on the cells follows the grid.
- **Your own groups**: an optional `uniquetab-groups.ini` beside `uniquetab.ini` adds pages of your
  own (by item class, rarity or record path) in front of the built-in groups, with `[view:<game>]`
  blocks for one game only. A file with an error is ignored as a whole and the log names its line.
  The mod never writes this file.
- **Your own words, any alphabet, your own looks** in the buttons file: Cyrillic, Greek, Chinese,
  Japanese and Korean captions, names and label words draw in the game font that has their letters;
  captions are measured exactly as the game draws them. The `[tab]` keys `size.caption`,
  `size.caption_max`, `size.label`, `color.caption`, `color.label`, `color.ring`, `color.mark`,
  `next.caption` and `edit.caption` set the sizes, colours and words; the rollover words
  (`tooltip.yes`, `tooltip.no` and their colours) live there now too.
- **`collect_unobtainable`** in `[general]` (default 0): fourteen records nobody can obtain - gear
  worn by statues and a quest character, a boss's weapon he never drops, two set pieces no loot
  table, chest or quest gives, and items only loot tables nothing uses hold - are left out, and the
  start-up log lists them once with the reason; 1 collects them.
- **Items the game keeps outside its item folders count** when something hands them out (a loot
  table, a merchant, a chest, a monster or a quest): in the base game the six Eternal Embers arcane
  formulae whose description says "Test", Alcyoneus' Breastplate and the Lupine Necklace Design;
  in a custom game whatever its database keeps elsewhere. "blank", "test", "template" and "copy
  of" count only as whole words, and only on a record nothing hands out.
- **The label** ends with `- +N not on these pages` while your collection holds items no page shows
  (left out by `collect_mi=0`, `collect_relics=0`, `collect_unobtainable=0` or a group file of
  yours).
- **The search** finds the arcane formulae and the relic bonuses too; it reads the items only after
  your first click into the field or your first search (or with `search_prebuild=1`), in each game
  anew, and keeps each game's item text in `uniq-search.txt`, so later sessions read that file
  instead of making every item again.
- **More settings** under `[advanced]`: `journal_dir` (where the collection is kept, from the main
  `uniquetab.ini` only), `collect_pristine_only` (1 = refuse items with affixes, relics or charms;
  the log names the part), `ui_scale_pct` (0 = follow the game's UI scale, else force one, 50..300)
  and `pad_caption_max` (the largest caption size on the buttons). `enabled=0` in `[general]` makes
  the mod go quiet without losing anything. The settings file has three parts: the everyday
  settings under `[general]`, your places under `[layout]`, and every other setting under
  `[advanced]`, commented out at its default.
- **Memory**: the start-up builds work inside one 128 MB range reserved before the game fills
  memory and given back in one piece; the gray icons go back to the game when a world unloads; the
  mod's threads reserve less stack. The log's `mem:` lines give the address space, the process and
  the game's heap at every point that changes them.
- `undeploy.bat --purge` checks every collection before it deletes anything - the journals of the
  main folder, of every `mods\<mod>\` folder and any `tq-uniq-items*.jsonl` whatever the rest of
  its name - and deletes only the files the mod generated, each by its name, never `mods\` or a
  file of yours.

### Fixed

- **A custom game with a long name closed the game** when a caravan page was opened with a moved
  grid (Itemization Quest): the engine takes at most 63 characters between `/` or `\` in a name.
  A painted background is named by a short code of its game now, and every name the mod hands the
  engine is checked first (a longer one is skipped with one warning).
- **A click on the caravan driver while the caravan is open** (the game closes the caravan and
  opens it again in place) no longer hides the tab until the caravan is closed: the pad, the toggle
  and Ctrl+wheel come back as after any open, and an open layout editor closes with nothing
  written.
- **The pad's clicks**: a press that loses its release (dragged out of the window, Alt+Tab) no
  longer eats the next click on the page, a right press held on the pad no longer swallows a left
  click, a pad button let go off the window no longer stays pressed, and a right button pressed
  elsewhere and let go over the pad is no longer swallowed.
- Under a `text_language` other than EN, an item the translation does not name keeps its English
  name instead of leaving the catalogue.
- A custom game's resource archive that cannot be read is skipped with one warning naming it; the
  game keeps its tab and the base game's archives stand in.
- A caption holding a letter above U+FFFF (an emoji) shows the button's tag, a name the group's own
  name, a `[tab]` word or a label template the built-in English; one WARN says what was passed
  over.
- `deploy.bat`, `undeploy.bat` and the catalogue test call Windows' `find.exe` (and `fc.exe`) by
  their full path, so they work when run from Git Bash.
- The log no longer promises a `rescue=1` the mod does not have.

### Known limit

- Titan Quest is a 32-bit program and `TQ.exe` is not large-address-aware: the whole game has 2 GB
  of address space. Large custom games played one after another in one session can exhaust it, so
  a game loaded after another large one may fail to load or keep its tab off. Restart the game
  before you switch to another large custom game (README, "Memory: restart the game between large
  custom games").

### Upgrading

Replace `scripts\uniquetab.asi` and start the game; there is nothing to do by hand. The first
start, once:

- rewrites `uniquetab.ini` as `ini_version=27` with every value you set kept: a setting that moved
  (where the collection was left goes to `uniquetab.state`, the rollover words to `[tab]` of the
  buttons file) is carried there and named in the log, every new `[advanced]` key arrives
  commented out at its default, and `collect_relics=1` joins `[general]`;
- rebuilds every catalogue from your game files: on a page that gained an item the boxes after it
  move one place along;
- moves each mod's journal into `mods\<mod>\` (going back to 1.0.x: move it back beside
  `uniquetab.ini` first);
- builds every installed custom game in the background;
- and the first open of the tab in a game writes its buttons file (an existing one gains its
  `[REL]` button once).

Nothing in your collection file changes, and every item you stored stays collected. If you stored
one of the six uniques nobody can obtain, it stays in the file, hidden and counted in the label's
`+N not on these pages`, until `collect_unobtainable=1`.

## 1.0.1 - 3 October 2026

- Scrolling the collection page or switching its group no longer plays the drop sound of every item
  on the page. Putting an item in and taking one out sound as before.

## 1.0.0 - 26 September 2026

The first release.

- The collection page in the caravan's Transfer tab: one slot for each of the 1,588 Epic and
  Legendary records, in 15 groups. Under the grid, the pad: the group buttons, **Transfer** (the
  toggle, its lamp lit while the real Transfer page shows), **OWN**, the **<** and **>** arrows,
  the search field, and the label, which counts the group and the whole collection
  (`all 312 / 1588`).
- Deposits (drag an item onto the page, or right-click it) and takes, each recorded with the
  item's full identity in the mod's own journal, `tq-uniq-items.jsonl`; no save file is written.
- The owned marks (an item you do not have shows its icon in gray), the *In your collection* /
  *Not in your collection* tooltip line, and `extras\tq-uniq-items-all.jsonl`, a collection with
  every record in it.
- **Search**, on the collection page and on the real Transfer page. Click the field under the grid
  and type: every item whose name or properties hold what you typed gets a thin gold frame round
  its slot. "fire" finds Fire Damage, Fire Resistance and every item with "fire" in its name;
  upper and lower case, accents and curly apostrophes do not matter. Nothing is hidden or moved.
  The group buttons that hold a match turn blue and the label counts the matches (`found 9`).
  **Esc** clears the search (a second Esc leaves the field), **Enter** leaves the field and keeps
  the search, **Backspace** held keeps erasing, **Ctrl+Backspace** clears, and the **x** at the
  field's right end clears it too. While the field has the focus, no game hotkey fires. The first
  search reads every item once, in the background, with the label saying `indexing k/1588`.
- Every setting lives in `uniquetab.ini`, written with its own comments on the first start; the
  README's *Settings* section lists each key. The search's are `search`, `search_buttons`,
  `search_mark`, `search_mark_color`, `search_lore` and `search_transfer`, plus the advanced
  `search_prebuild` and `search_debug_query`.
