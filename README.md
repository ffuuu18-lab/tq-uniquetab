# Unique Collection Tab for Titan Quest

A collection tab for Titan Quest Anniversary Edition, built into the caravan's Transfer page. It is
the Titan Quest port of the Grim Dawn mod of the same name. `CHANGELOG.md`, which comes with the
source rather than in the zip, lists what changed in each release.

Put an Epic or a Legendary into it and the page keeps a slot for that item: one slot per item
record, filled or empty, sorted into 16 groups - Helms, Torso, Arms, Legs, Amulets, Rings, Shields,
Axes, Maces, Staves, Swords, Throwing, Spears, Bows, Artifacts and Arcane Formulae. Every Epic and Legendary of the
game and its expansions is there, the ones only monsters carry included: 1,582 records, and
after them on each group's pages the 1,099 monster infrequents (the Rare items named after their
monster, every level tier its own slot, one name's tiers side by side in level order - normal,
epic, legendary; `collect_mi`), and on a page of their own the 93 arcane
formulae: 2,774 records. An item the game keeps outside its item folders counts too when
something hands it out (a loot table, a merchant, a chest, a monster or a quest), in the base
game and in a custom game alike. The **Relic** page keeps the relics and charms besides: a
block per relic or charm, its shards and each of its completion bonuses a slot of their own
(`collect_relics`). It is a place to put the uniques you want to keep, one copy of each, and a
way to see at a glance what you have found and what you have not.

**The arcane formulae** have their page after the Artifacts, each in a cell two squares wide.
Hovering a formula says whether the formula itself is in your collection; the artifact it makes
keeps its own line on its own tooltip, never on the formula's.

**Your items do not go into a caravan file.** The collection lives in the mod's own file next to
the mod. The mod never writes `Player.chr`, the Transfer's `winsys.dxb` or any other save file:
when you deposit an item the game takes it out of your inventory the way it always does, and the
mod's journal is what remembers it. If you remove the mod, your collection file stays on disk
exactly as it was - take your items back out first (see *Uninstall*), because without the mod
the game has no way to show them.

## What you need

- **Titan Quest Anniversary Edition 2.10 from Steam** - the 32-bit `TQ.exe`. The mod checks the
  game's shape, not its version number: at start-up it confirms every fact it needs about the
  game's memory, and the log's start-up block says `bindings: ... all confirmed` when they all
  hold. On any other build it turns itself off, and the Transfer page stays the game's own; nothing
  else changes (see *When something goes wrong*).
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader), the **x86 (32-bit)**
  build. This mod is an ASI plugin: it does nothing until a loader loads it. The loader is not
  included here - download it once and it serves every ASI mod you use. If another mod already
  installed it, that copy is the one to use.
- Windows. Steam Deck, SteamOS and Proton are **untested**: the loader is a DLL override like any
  other and may well work there, but nobody has tried it.
- Nothing else. No script extender, no launcher, no other mod.

## Install

Everything goes in the game's own folder - the one that holds `TQ.exe`.

1. Take `dinput8.dll` from the x86 build of Ultimate ASI Loader and put it beside `TQ.exe`. If a
   `dinput8.dll` that is not the loader is already there (another mod's), use the loader under the
   name `xinput1_3.dll` instead. Use exactly one loader: two in one folder would load every plugin
   twice. If you already have the loader from another mod, skip this step.
2. Copy `uniquetab.asi` from the package's `scripts` folder into a `scripts` folder beside
   `TQ.exe` (create it if there is none). Unzipping the package straight into the game folder does
   exactly that.

Never put the plugin into the game's `MilesRedist` folder, and never point the loader at it: the
`.asi` files there (`mssmp3.asi`, `mssvoice.asi`) are the Miles sound system's codecs, not
plugins.

`deploy.bat` in the source tree does step 2 for you: `deploy.bat "<the folder that holds TQ.exe>"`
copies the built plugin into `scripts\` and refuses to run until a loader is there.

That is all. Start the game and sit at the main menu for a few seconds.

**The first launch builds everything else.** The mod creates its folder, `scripts\uniquetab\`,
reads your own installation - the database, your language's text, the item archives - and
writes the item catalogue there, with its record lists beside it and a gray copy of every
collectable item's icon in `scripts\uniquetab\gray\` (about 50 MB). It takes about five seconds at
the main menu, once. It happens again after a game update, and never otherwise. The settings file
`scripts\uniquetab\uniquetab.ini` is written at the same time, with every option and a line of
explanation for each.

If the game folder cannot be written to, the mod uses
`Documents\My Games\Titan Quest - Immortal Throne\uniquetab` instead (`OneDrive\Documents\...`
when that is where your Documents folder lives) and says so in its log.

**Smart App Control and SmartScreen** may block an unsigned DLL. You can tell: the mod always
writes `scripts\uniquetab\uniquetab.log` when it starts (or the Documents fallback above), so a
game that runs with no log in either place never loaded the plugin. Check the loader first, then Windows Security's app control.

**Upgrading from 1.0.x** is the same step 2: replace `scripts\uniquetab.asi` and start the game.
There is nothing to do by hand. Your collection and your settings stay where they are, and the
first start does the rest, once:

- `uniquetab.ini` is rewritten as `ini_version 27` with every value you set kept and a `[layout]`
  part added; a setting that moved (where the collection was left goes to `uniquetab.state`, the
  rollover words to `[tab]` of the buttons file) is carried there, and the log names each move;
- every catalogue is rebuilt from your game files, because what goes into it changed; on a page
  that gained an item the boxes after it move one place along (your collection file is
  untouched, and every item you stored stays collected);
- a mod's journal an earlier version kept beside `uniquetab.ini` moves into `mods\<mod>\` (see
  "Your collection, and backing it up");
- every custom game installed is built in the background (about a minute and a half for thirteen
  large ones; see "Custom games"), and later only when it changes;
- the first time you open the tab in a game, its buttons file `uniquetab-buttons.ini` is written
  (an existing one gains the Relic button, `[REL]`, once).

Going back to 1.0.x: it does not look in `mods\`, so move each `mods\<mod>\tq-uniq-items-<mod>.jsonl`
back beside `uniquetab.ini` first.

## Using it

Open the caravan and go to the **Transfer** page. Under the grid, inside the caravan's frame, the
mod draws a small pad of buttons:

- **Transfer** (its lamp lit while the real Transfer page shows) switches the Transfer page to the
  collection and back. **F6** does the same
  (`view_hotkey`). Your real Transfer items are untouched and come back the moment you switch
  back.
- The **group buttons** (Helm, Torso, ... Artf, BLU, and Relic for the relic and charm page) select
  a group and switch the collection on.
- **OWN** toggles between showing every item of the group and only the ones you already have.
- **<** and **>** (half a cell each, beside OWN) turn the group's pages. After them, past the
  search field, the label says the group, the slot rows shown (`rows 1-5 / 12`), how many of the
  group's items you own and, after `all`, how many of the whole
  collection's items you own (`all 312 / 2774`). While a search stands, its count follows
  (`found 9`). When the line is longer than its box it gets shorter first: the compact form
  (`Torso 3-7/58  12/461  all 312/2774  found 9`), then the search's short form (`=9`), then
  without the rows, then without the `all` count, and last, with a search only, without the
  group's owned count. A line that still does not fit is drawn smaller, down to size 6; with the
  default settings that never happens. While your collection holds items no page shows (the
  monster infrequents with `collect_mi=0`, the records nobody can obtain with
  `collect_unobtainable=0`), the label ends with `- +N not on these pages`; a short line gives that
  up first after the search's short form.

Over the page:

- The **mouse wheel** scrolls the group by one row of slots; **PageUp / PageDown** turn a page.
- **Ctrl+wheel** (or **Ctrl+PageUp / Ctrl+PageDown**) moves to the next or previous group. The
  real Transfer page is one of the stops in that cycle, so it works from there too.

A slot you own shows the item in colour. A slot you have not filled shows the item's icon in gray
and is drawn bare - no slot tint, no border - until the cursor is on it. Hovering any slot shows
the game's own tooltip, and every item tooltip in the game - in your inventory, on the ground, at
a merchant - carries one more line: **In your collection** or **Not in your collection**
(`tooltip_mark`).

### The relic and charm page

The **Relic** button opens the relics and charms (`collect_relics=1`, the default): 93 blocks in
the base game, the relics first, then the charms. A block is one relic or charm, one row per tier;
each row starts with its **shard slot** and then holds one **bonus slot** per completion bonus its
record can give. The shard slot takes the partial pieces of that record and counts their shards
(its number is the shards stored); a bonus slot takes the completed relics or charms with that
bonus and counts them. Unlike an item's box, these slots keep any number. You put a piece in as
any item (see *Putting an item in*); a completed piece with a bonus its record cannot give, or a
shard count the record does not have, is refused. Taking from a shard slot gives **one shard** at
a time; a bonus slot gives back the oldest piece stored there. An empty slot shows its piece in
gray. The mouse wheel moves the page by one block; `<` `>` and PageUp / PageDown turn to the
previous or next fixed page.

The page stands on the caravan's own background with its cell lattice erased, each block on a
framed panel of its own (gold for relics, a bronze patina for charms; `relic_panels=0` removes
them). `relic_block_gap` and `relic_bonus_gap` under `[advanced]` set the empty lines between
blocks and the empty columns before the first bonus, and `relics_565x637=x,y,w,h` under
`[layout]` gives the page an area of its own (the layout editor opened on the Relic page writes
it; with no line the item grid in force is used).

On the **Relic** page every relic or charm tooltip speaks of its slot there. A
completed relic or charm says **In your collection** when its record with that completion bonus is
stored, **In your collection (another roll)** when only other bonuses of that record are, and
**Not in your collection** otherwise. A partial piece speaks of its record's shard slot in one
line: **In your collection (pieces 15)** - the shards stored there, the slot's own number - or
**Not in your collection**. The same lines show on the pieces in your
inventory, stash and Relic Vault. **OWN** on the Relic page shows only the blocks that hold
something, every slot of such a block in its place. With `have_marks=1` an empty bonus slot gets
the corner tick when you carry that relic or charm with that bonus somewhere (the shard slot never
does).

The Relic page's label is short and names no page (the pressed **Relic** button does):
`64-67/93  page 17/23  8/1971  pieces 50` - the blocks shown of all blocks, the fixed page, the bonus
slots holding something of all bonus slots, and the shards stored. A narrower label box drops the
page, then the blocks, then the pieces; a search adds `found N` (`=N` in a narrow box).

### Searching

While the collection is shown, the box between **>** and the label is a search field. Click it
and type: every item on the page whose name or properties hold what you typed gets a thin gold
frame round its slot (`search_mark`, `search_mark_color`). Nothing is hidden or moved - the page,
OWN, the scrolling and the counts stay exactly as they are. "fire" finds Fire Damage, Fire
Resistance and every item with "fire" in its name.

- **What is searched**: the item's name and its property lines, as the game shows them in your
  game language (the same text as its tooltip). The requirements (level, strength, ...) and the
  story text are not searched (`search_lore=1` adds the story text). Upper and lower case, accents
  and curly apostrophes do not matter.
- **The group buttons**: a group that holds a match turns blue once all of its items have been
  read (`search_buttons`); the others are dimmed. The label says `indexing k/2774` while the mod
  reads the items the first time (a few seconds, in the background) and then `found N` (`=N` when
  the line is short of room).
- **When the items are read**: your first click into the field or your first search starts the
  reading (`search_prebuild=1` starts it when the collection is first shown), in each game anew -
  a search in one game does not start the reading in the next one. To read an item's text the
  game makes that item once, which costs the game memory: about 15-60 MB in a large game.
- **The text is kept per game**: once every item of a game has been read, their text is written
  to `uniq-search.txt` (the main game's beside `uniq-groups.txt` in the mod folder, a custom
  game's in `mods\<mod>\`), and later sessions read that file instead of making the items again;
  one item is still read to check the file against the game's own text. The file is written again
  when the catalogue, `text_language`, `search_lore`, the game's database or the mod's version
  changes, or when that check finds another text (after you switch the game's language, for
  example). A set item read while you wore a piece of its set is not in the file and is read again
  in the next session (its set lines follow the pieces you wear). Deleting the file is safe: it is
  written again.
- **The Relic page**: each bonus slot is searched by the completed relic or charm with that
  bonus, as its tooltip shows it (its name and the bonus's words), so "attack speed" frames every
  bonus slot whose bonus gives Attack Speed and the **Relic** button turns blue. The shard slots are
  not searched. These texts are read every session (not kept in `uniq-search.txt`), after the
  groups' items, or at once when the Relic page is shown with a search standing. The Relic page's
  label ends with the same `found N` (`indexing k/n` while the items are read, `=N` in a narrow box).
- **On the real Transfer page** the same field is there too (`search_transfer`), with the same
  search: the items in your real Transfer sack whose name or properties hold it get the same
  frame, and the label says `the real Transfer page  found N` (`reading k/n` while the mod reads
  those items the first time, and again after the sack changes). Switching between the real page
  and the collection keeps the field and the search.
- **The x** at the field's right end clears the search, the same as **Esc**; it shows while the
  field holds text. On an empty field a click there simply puts the cursor in the field.
- **search off**: when the search cannot be used (it turned itself off after an error, or `search`
  was 0 when the game started) the field stays in its place and says `search off`; a click on it
  only writes the reason to the log, once.
- **The keys**: while the field has the focus every key you press is text for the field - no game
  window, potion or skill hotkey fires. **Backspace** erases a character (hold it to keep erasing),
  **Ctrl+Backspace** clears the field, **Esc** clears it (a second Esc leaves the field), **Enter**
  leaves the field and keeps the search. Clicking anywhere else (but the Transfer button and, on
  the real page, a group button), switching the caravan to its other page, closing the caravan,
  or switching to another program also leaves it, and so does a minute without a key. The
  Transfer button keeps the field, unless `search_transfer=0` (then the real page has no field).
  At most 32 characters; a long search shows its end.
- `search=0` turns all of it off: no field, no marks, and the keyboard is never touched.

### Putting an item in

Two ways, both of them the ordinary ones:

- **Drag** the item onto the collection page and drop it anywhere on it; it goes to its own slot.
- **Right-click** it in your inventory while the collection is on screen.

The slot fills and the journal remembers the item exactly - its prefix, its suffix, its relic or
charm and their bonuses, its seed. The log writes a `journal: deposit <record> ...` line for
each one.

### What is refused, and why

Every refusal leaves the item exactly where it was, and the log says why
(`<gesture> REFUSED - <why>: <record> [...]; the item was not touched`, or
`deposit REFUSED: <why> - the item was not touched` for an item the mod puts back). At the default
`log_level=info` each reason is logged once in a row and the next refusals for the same reason go
to DEBUG; `log_level=debug` shows every one:

- **Not a unique of the collection** - a common or rare item, a potion. Only the 1,582 Epic and
  Legendary records, the 1,099 monster infrequents, the 93 arcane formulae and the relics and
  charms of the Relic page have slots (with `collect_mi=0` the monster infrequents have none; with
  `collect_relics=0` there is no Relic page, and a relic or charm is refused with this line too).
  Fourteen more records exist that nobody can obtain - six uniques and eight monster infrequents
  (gear worn by statues and by a quest character, a boss's weapon he never drops, two set pieces
  no loot table, chest or quest gives, and items that sit only in loot tables nothing in the game
  uses): they are left out and refused like any other item, and the log lists them once at
  start-up. Set
  `collect_unobtainable=1` to collect them anyway.
- **Already in the collection** - the collection keeps one copy of each record (the Relic page's
  slots keep any number).
- **No slot on the Relic page** - a completed relic or charm with a bonus its record's table does
  not list, or a shard count the record does not have.
- **A stack** of two or more.
- **The right-click route with the real Transfer page full** - a right-click deposit passes
  through the Transfer page's own room check, so empty a cell of the real Transfer first, or drag.
- **Multiplayer with `mp_collect=0`** - the page still shows, but nothing goes in or out.
- **An unknown session state** - when the mod cannot tell whether the game is single player or
  multiplayer, it refuses every move until it can.
- **No character save visible** - the mod settles each move against your next character save,
  read (never written) from `Documents\My Games\Titan Quest - Immortal Throne\SaveData\`. When no
  `Player.chr` can be seen there, takes are refused.

### Taking an item back

- **Left-click** a collected slot: the item comes to the cursor, and you put it wherever you like.
- **Right-click** a collected slot: the item goes straight into your inventory. With no room for
  it the take is refused with the game's own sound, and the item stays in the collection.

It comes back as the item you put in, not as a fresh roll: the same prefix, suffix, relic or
charm, the same seed. On the Relic page a shard slot gives one shard at a time, and a bonus slot
the oldest piece stored there.

### One collection per save set

Titan Quest keeps separate characters and a separate Transfer for the main campaign, for custom
quests and for a mod, and so does the collection. Each has its own journal, never mixed - the
main campaign's in `scripts\uniquetab\`, a mod's in that mod's own folder
`scripts\uniquetab\mods\<its name>\` (see "Your collection, and backing it up"):

| you play | your collection is |
| --- | --- |
| the main campaign | `tq-uniq-items.jsonl` |
| a custom quest, or a mod | `mods\<its name>\tq-uniq-items-<its name>.jsonl`, the name written as below |
| anything else outside the main campaign (the game names no quest or mod) | `tq-uniq-items-custom.jsonl` |

The name of a custom quest or a mod is written the way a file name can hold it: lower case, the
letters and digits kept, every other run of characters turned into one `_`, at most 48 characters
(a name that is just `custom` becomes `mod_custom`). When anything had to be changed or cut, a `-`
and a short hex code follow, so that two names which look alike afterwards keep separate files:
`My Mod` gives `tq-uniq-items-my_mod-` and a code, in the folder `mods\my_mod-` and the same
code. You never need to work it out - when a world loads, the log line `journal: set "..."` names
the file with its folder.

### "Check for a duplicate"

A deposit or a take is written to the journal the moment it happens, and **settled** at your
character's next save. If the game ends before that save - a crash, a killed process - the
character file still has the item where it was, while the journal says the move happened. The log
then says a row was `written before a character save that never came - check for a duplicate`.

On the next load the mod checks those rows itself: when the world loads and again when you next
open a caravan, it looks for the exact item in your inventory, your equipment (both weapon sets)
and the caravan's pages. An item it **finds** decides the row - a deposit whose item is back in
your bag is dropped from the collection (deposit it again), and a take whose item is in your
inventory is removed for good. An item it cannot find anywhere
leaves a deposit in the collection and puts a take's item back into it (the character file never
had it), with the same warning: the mod does not know which of your characters made the move, so
it asks you to look rather than guess. Either way nothing is lost; at worst you hold a duplicate.

## Settings

`scripts\uniquetab\uniquetab.ini` is written by the mod on the first launch and re-read about once
a second, so you can edit it while the game runs. A value outside its allowed range is clamped and
the log says so. Delete the file and it comes back with the defaults.

It has three parts: the eight everyday settings under `[general]`, your own places for the grid,
the buttons and the label under `[layout]` (no line until you set one), and every other setting
under `[advanced]` at the end, commented out at its default.

**[general]**

| key | default | what it does |
| --- | --- | --- |
| `enabled` | 1 | 0 = the mod goes quiet: no tab, nothing new collected. Nothing is lost. The collection view cannot be switched on (a shown one goes back to the Transfer page), the view key is the game's again, and no pad, mark, tooltip line or search field is drawn; what you stored stays in the journal, and `enabled=1` brings it all back |
| `collect_mi` | 1 | 1 = monster infrequents are collected, one roll per item; 0 = they are not, and the ones you stored stay in the collection, hidden until it is 1 again (from the next start) |
| `collect_relics` | 1 | 1 = relics and charms have their own page, one shard at a time; 0 = no relic page: no Relic button (a section of the buttons file that names `relics` simply gives none, the rest of the file stays in force), a relic or charm is refused like any item outside the collection, and the ones you stored stay in the collection, hidden until it is 1 again (from the next start; the CSV export still lists them, with empty tier and shards cells) |
| `collect_unobtainable` | 0 | 1 = also collect the records no monster, chest, merchant or quest gives (gear worn by statues and quest people, a boss weapon he never drops, set pieces no loot table names); 0 = they are left out, the start-up log lists them, and the ones you stored stay in the collection, hidden until it is 1 again (from the next start) |
| `tooltip_mark` | 1 | 1 = every item tooltip says whether it is in your collection |
| `log_level` | info | how much goes in the log: error, warn, info, debug, trace |
| `view_hotkey` | 117 | the key that switches the Transfer page to the collection and back, as a Windows virtual-key code (117 = F6, the default; 0 = the Transfer button only) |
| `search` | 1 | 1 = the search field on the collection page; 0 = no field, no marks, no key taken (if the game started with 0, a 1 works from the next start) |

The file says `ini_version=27`. An older file is carried over on the first start with every value
kept. A file from 26 keeps every line and gains `collect_relics=1` under `[general]`; a mod's own
file from 26 takes the main file's `collect_relics` once, at its first entry. A file from 22
keeps every line - your `[layout]` lines, `pad_screen` and `label_screen`,
and every commented row stays commented - and gains `ui_scale_pct`, `collect_pristine_only`,
`pad_caption_max` and `journal_dir` under `[advanced]`, commented out at their defaults. A mod's
own file from 22 keeps its own lines the same way (a `[layout]` line or a screen key it does not
have stays absent: that game keeps its derived place); only the three new keys it may hold are
copied once from the main file. An older
one: `tooltip_mark`, `view_hotkey` and `search` move up into `[general]`, where `log_level`,
`collect_mi` and `collect_unobtainable` stay and `enabled` is new; every key of the other old
blocks moves into `[advanced]`, beside the keys already there. Under `[advanced]` a key stands
commented out at its default, or as a live line if you had set it (`font=...` stays as you set it).
The log names every line that moved, with its value.

**[advanced]** - every other setting, one line each, commented out at its default with what it
does beside it. Remove the `;` in front of one to change it; the line keeps its place.

| key | default | what it does |
| --- | --- | --- |
| `ui_scale_pct` | 0 | 0 = follow the game's UI scale, else force one in percent (50..300): the buttons, the label, the outlines and the marks the mod draws are placed at it. For a display where the measured scale comes out wrong; the game's own window and items do not change, so set it to the game's own UI scale: any other value moves the marks and outlines off the game's items (a deposit still lands in the cell under the cursor) |
| `mp_collect` | 1 | 1 = deposits and takes work in a multiplayer game, hosted or joined, for you only; 0 = the page shows there but every move is refused |
| `collect_pristine_only` | 0 | 1 = refuse items with affixes, relics or charms: a unique with a relic or charm in a socket (or its completion bonus), or an item with a prefix or a suffix, is not taken, and the log names the part (`deposit ... REFUSED - the item is not pristine: <record> [relic = "..."]`). The slot still shows such an item as one it takes: the refusal comes at the drop |
| `group_buttons` | 1 | draw the pad of group buttons and let it be clicked |
| `page_hotkeys` | 1 | the wheel scrolls rows, PageUp/PageDown turn pages, Ctrl changes group |
| `plate_label` | 1 | draw the group label beside the buttons |
| `owned_marks` | 3 | how an item you do not own yet is marked: 0 none, 1 a dark veil, 2 a thin frame, 3 its icon in gray |
| `have_marks` | 0 | 1 = an item you do not own yet but carry somewhere (inventory, stash, Transfer, Relic Vault) gets a small corner tick |
| `slot_plates` | 3 | the ground under each slot: 0 none, 1 a dark plate (thin frames where it cannot be drawn), 2 thin frames, 3 the equipment window's own slot art |
| `grid_cover` | 1 | cover the grid cells no slot uses in the caravan's ground colour; on the relic and charm pages the caravan background is shown with its lattice erased instead (that ground colour only until the copy is painted) |
| `search_buttons` | 1 | turn a group button blue when its group holds a match |
| `search_mark` | 1 | how a found item is marked: 1 a thin frame round its slot, 2 a wash under it, 3 both |
| `search_mark_color` | gold | the colour of that frame and wash: `gold`, `green`, `white`, `red`, `orange`, `cyan`, `magenta`, `blue`, or `#rrggbb` |
| `search_transfer` | 1 | the search field on the real Transfer page too, marking the items there that match; 0 = on the collection page only |
| `search_lore` | 0 | 1 = also search the items' story text |
| `search_prebuild` | 0 | 1 = read every item for the search when the collection is first shown (a few seconds, in the background) instead of at the first search |
| `search_debug_query` | empty | a search applied to the collection page without typing it, read again every second |
| `journal_dir` | empty | where the collection file is kept (read at start-up): a full path to an existing folder the mod can write to, else the mod's own folder with one warning in the log. Empty = the mod's own folder. Main `uniquetab.ini` only: a mod's own file shows it as a commented line, and a line you add there is ignored with one line in the log. A mod's journal goes into `<journal_dir>\mods\<mod>\`, and the spreadsheet copies follow the journals; the settings, the log and the generated files stay in `scripts\uniquetab\`. The mod does not move your journals: copy them there yourself with the game closed (while a journal is still in the mod's folder, the log names it once: it is not read) |
| `export_csv` | 0 | write a spreadsheet copy of the journal beside it (`tq-uniq-items*.csv`): 0 off, 1 the collection, 2 also the takes no save has settled yet |
| `layout_outline` | 0 | 1 = outline the grid, the buttons and the label with their `[layout]` numbers (white = the default place, green = yours, red = a line of yours that is refused, with the reason) |
| `layout_edit` | 0 | 1 = open the in-game layout editor on the next Transfer page (the key sets itself back to 0) |
| `edit_button` | 1 | 1 = an EDIT button right of the search field opens the layout editor; 0 = no button, its cell goes to the label (Ctrl+right-click still opens the editor) |
| `relic_block_gap` | 1 | the Relic page: the empty lines between two blocks (0..2) |
| `relic_bonus_gap` | 1 | the Relic page: the empty columns between a row's shard slot and its first bonus (0..1) |
| `relic_panels` | 1 | the Relic page: 1 = every block stands on its own framed panel; 0 = no panels |
| `plate_label_size` | 13 | the height of the label line's text, 8..32 (the button captions are fitted to their buttons; `size.label` in the buttons file wins over it) |
| `pad_caption_max` | 14 | largest caption size on the buttons; the button size limits it too (6..36). `size.caption_max` in the buttons file and a button's own `size` win over it. A long caption of the buttons file is fitted on its own button under the same ceiling |
| `font` | `fonts/albertus mt light.fnt` | the caption font, by its name in `Resources\Fonts.arc` |
| `caravan_x`, `caravan_y`, `caravan_w`, `caravan_h` | 10, 0, 565, 637 | the caravan window's place and size as its record gives them; change them only under a UI mod that moves the window |
| `page_y` | 126 | the Transfer page's top inside the caravan window |
| `log_flush_each_line` | 0 | 1 = write every log line at once (slow; for chasing a crash) |
| `text_language` | EN | the suffix of `Text\Text_<lang>.arc` the catalogue takes item names from. Under a language other than EN, `Text_EN.arc` is read first and `Text_<lang>.arc` over it, so an item the translation does not name keeps its English name instead of leaving the catalogue |

**[layout]** - where the item grid, the buttons and the label go. Every line is optional; without
them the page is laid out as it always was. The in-game editor writes these lines for you.

| key | what it does |
| --- | --- |
| `grid_<W>x<H>`, `pad_<W>x<H>`, `label_<W>x<H>` = `x,y,w,h` | your own rectangles for the caravan window of that size, in page pixels from the window's top-left corner at UI scale 1 (the outline prints them) |
| `relics_<W>x<H>` = `x,y,w,h` | the relic and charm pages' own area for that window size, whole cells from its top-left; no line = the item grid in force. The editor writes it when it is opened on a relic page, and RESET + SAVE removes it. Under `[advanced]`, `relic_block_gap` (0..2, default 1) is the empty lines between two blocks and `relic_bonus_gap` (0..1, default 1) the empty columns between a row's shard slot and its first bonus; `relic_panels` (default 1) stands every block on its own framed panel, 0 = no panels |
| `pad_screen`, `label_screen` = `<corner>,x,y,w,h` | the buttons or the label line pinned to a corner of the SCREEN instead of the window (below) |

`<W>x<H>` is the caravan window: `565x637` for the game's own and for most custom games, `660x699`
for a custom game with a 20 x 20 caravan (SVAERA); a mod with another caravan window keeps its own
lines. Lines written for one size do not apply to another: SVAERA's caravan is read as 660 x 699
now, so its lines written for `565x637` no longer apply - set the layout again in the editor (SAVE
writes `_660x699`). By default the grid is the Transfer
sack's place (`27,126,512,480`, 16 x 15 cells), the buttons sit in the band below it
(`33,606,500,31`) and the label in the buttons' second row (`270,622,260,12`); a label line inside
that box keeps the label in the pad. The buttons' rectangle takes the group buttons in one row
and Transfer / OWN / < / > / the search field, EDIT and the label in a second; when the group buttons do
not fit with a readable caption they are paged and a `>>` button shows the next page. The label's
height limits `plate_label_size`.

**The grid.** `grid_565x637=27,40,512,560` moves the collection's item grid and gives it as many
whole cells of 32 page pixels as fit from its top-left, up to what the caravan window holds (17 x 19
in `565x637`, 20 x 21 in `660x699`); every group is laid out again on those cells, so a page holds as many items as the
grid fits, and the row you were on keeps its first item on screen. The smallest grid is the largest
item's slot (64 x 128 for the two-handed weapons); a smaller one is refused. Only the collection
page moves: the game's own Transfer page keeps its sack where the game puts it (the editor outlines
it there, "fixed: 16 x 15"). Everything the mod draws on the cells follows the grid - the slot
grounds, the gray veils, the marks - and so does the caravan's cell lattice: while a collection page
is on screen with a grid of yours, the caravan window's background is shown with its lattice
repainted at your grid's cells. Each grid is painted once, in the background, in about a second
(the old lattice shows until then), and kept in the `plates\` folder beside the mod (a few MB per
grid; a new grid replaces the older one of the same background). The game's own Transfer page, Storage
and Relic Vault always show the game's own background. The relic and charm pages show the caravan
background with its lattice erased (with `grid_cover=1`), their blocks on framed panels; the flat
ground colour covers the cells until that copy is painted, or when it cannot be. If a background
cannot be repainted, the log says why once, and the caravan keeps its own lattice under your grid. Where the background lies in
the window is read from the game's own records (the caravan window's `BackgroundImage`) when the
game's set is built. The game's engine takes at
most 63 characters between `/` or `\` in a name, so every name the mod hands it (these backgrounds,
the gray icons, the fonts) is checked first; one that is longer is skipped with one
warning and the game's own background or icon stays.

**On the game's own Transfer page** the buttons and the label stay where they are on the collection
page - your rectangles, the default ones or the pinned ones - even where the game's 16 x 15 sack
lies under them (a smaller grid of yours leaves room beside it, and the buttons may sit there). They
are drawn over the cells they cover, and those cells are theirs while they are there: every button
works, a click or a right-click between the buttons does nothing, a click with an item on the
cursor never drops it into a covered cell, and the cursor over them shows no item tooltip and picks
up nothing under them. The log says once how many of the sack's cells they cover
(`buttons: the real Transfer page: the pad ... covers 32 of the sack's 240 cell(s)`). The sack
itself never moves.

**Off the window.** `pad_screen=tr,8,8,320,44` puts the buttons in the top-right corner of the
screen: the corner is `tl`, `tr`, `bl` or `br`, `x,y` the distance from that corner inwards and
`w,h` the size, in the same page pixels as the other lines, scaled with the UI scale like the
page. `label_screen` does the same for the label line. One line each serves every caravan of its
file: the main `uniquetab.ini`'s in the main campaign, a custom game's own file's in that game. The
pinned parts are drawn on a dark ground with a thin frame, only while the caravan is open on the
Transfer page, over the game's windows but under the item tooltip and the item on the cursor: a
tooltip or a carried item goes over them, as it goes over the buttons in the window. Their clicks are theirs: every button does there what it does in the window, a click on them
never moves the character, a plain right-click on them fires no skill, and a click with an item on
the cursor does nothing (the item stays on the cursor). A line that would cover the collection's item
grid, has no room for the buttons or the text, or is as large as the screen is refused with one line in the
log, and the part stays in the window; so is every pinned part in a world where the mod could not
match the game's mouse to the screen (the log says `consumption NOT PROVEN`). The grid always stays
in the window. Delete the line to put the part back. While a part is on the screen its
`pad_<W>x<H>` / `label_<W>x<H>` line is not used and not judged. The label line rides in the
pinned buttons while it has no line of its own: it is on the screen with them, so no part in the
window is refused for meeting the place it would have in the window.

**Edit the layout in the game.** Open the caravan on the Transfer page - the collection or the
game's own page - and click **EDIT**, the button right of the search field (`edit_button=0` under
`[advanced]` removes it and gives its cell back to the label; with an item on the cursor the editor
does not open), or **Ctrl+right-click the buttons** (or a pinned label line, or set
`layout_edit=1` under `[advanced]`: the editor opens on the next Transfer page and the key sets
itself back to 0). On a collection page the items are hidden while the editor is open (they come
back when it closes), and the grid shows the page's slots (one per item) as faint lines that follow it as you
drag, so you see where whole items fall before you SAVE. The grid, the buttons and the label become
rectangles with handles, and SAVE / CANCEL / RESET sit at the top right of the buttons and move with
them:

- drag a rectangle to move it, a corner or an edge to resize it. The grid stays in the window and
  snaps to whole cells. It moves on a collection page only: on the game's own Transfer page it is
  outlined, "(fixed)", with no handles, so open the editor on a collection page to move it. The
  buttons and the label may be dragged anywhere - over the grid too; only where a drag ENDS
  counts. On the game's own Transfer page they may end over its cells: they are judged against the
  collection's grid (yours, or the sack's own place without a grid line), which they must stay
  clear of on the collection page. Drag them off the window to put them anywhere on the screen (the status line says "on
  the screen - SAVE writes pad_screen"), and back onto the page to return them; a drag begun in the
  window may carry them straight out over the world, and the character does not move while you
  drag, hold still or let go there. The label put back where it sits by default, inside the
  buttons' second row, is accepted.
- a rectangle turns red when it cannot be used - it overlaps another one, it is too small for the
  buttons or the text, or, on the screen, it would cover the item grid or is as large as the screen
  - and SAVE is off while one is red (the words say why).
- **SAVE** writes `pad_screen` / `label_screen` for a part on the screen (pinned to the nearest
  corner) and `grid_/pad_/label_565x637` for the parts in the window, all under `[layout]` of the
  settings file in force (`uniquetab.ini`, or a custom game's own `mods\<mod>\uniquetab.ini` while
  you play it); a part left at its default has its line removed, and a SAVE on the game's own
  Transfer page never writes a `grid_` line (after a RESET there it removes it). The page follows within a second and keeps
  the layout at the next start.
- **CANCEL** closes the editor and writes nothing; so does closing the caravan, another UI scale or
  the game losing the focus.
- A click on the caravan driver while the caravan is open makes the game close the caravan and open
  it again in place: the editor closes with nothing written, and the tab comes back as after any
  open (the pad and the toggle on the Transfer page, the view OFF, Ctrl+wheel).
- **RESET** puts all three back at their default places; SAVE then removes their lines - on the
  game's own Transfer page too, where the grid cannot be moved: the collection's `grid_` line goes
  as well.

The editor never opens while an item is on the cursor (the log says so once): a click of the
editor must never drop it. Every press the editor takes - a part, a handle (a handle reaches a
little past its part's edge, past the window's edge too), SAVE / CANCEL / RESET - reaches nothing
else, inside the caravan window or outside it: not the page under it, not the world (no caravan
driver, no step), its drag and its release neither.

**By hand**, the same lines: set `layout_outline=1` under `[advanced]` and open the caravan: every
part is outlined with its numbers. Then add, for example, under `[layout]`:

```
pad_565x637=27,40,512,40
label_565x637=27,22,512,16
```

and the buttons move above the tab row, the label above them, within a second. A rectangle that
leaves the caravan window, overlaps another part, or is too small for readable buttons or text is
refused whole: the log says why (`layout: pad_565x637=... is refused - it ...`), the outline shows
it in red, and the default place is used. Delete the line to get the default back. `pad_x`,
`pad_y`, `pad_h` and `pad_gap` are gone: an older file's values are named once in the log when it
is carried over (`ini_version=22`), and the pad keeps its default place until you set a
`pad_565x637` line.

The OWN filter is not a setting: the OWN button's state, with the group and the row you were on,
is kept in `uniquetab.state` (an `owned_only` line of an earlier file is moved there once).

### A mod's own settings

A mod gets its own complete settings file, `mods\<mod>\uniquetab.ini`, the first time you load a
world of that mod: a copy of the main file's values at that moment. From then on the two are
independent - while you play the mod only its file is read, and a change to the main file does
not reach it. Delete it and the next load (or the next second, while you play it) copies the main
file's values again. Every key is the mod's own, `search` included; the search field's key
catcher is installed when the game starts, if the main file or any mod's file says `search=1`
(a mod switched to 1 later gets it from the next start, and `search=0` in a mod leaves it idle
there). A mod's file comes into force when the game loads the mod, before its world - so the item
names (`text_language`), `collect_mi`, `collect_relics` and `collect_unobtainable` of the mod's file decide its
catalogue. The `font` key is the exception: it is read once, when the game starts, from the main
file. `journal_dir` is the one key a mod's file does not hold: it is read once at start-up from
the main file, before any mod is known, so the mod's file shows it as a commented line saying so,
and a `journal_dir` line you add there is ignored with one line in the log. A custom map that names no mod uses the main files, unless the game loads a database of its
own for it: then it is a custom game (below), with its own catalogue and settings.

### Custom games

A mod that keeps its own save files gets its own collection pages. Every custom game installed
here - the workshop items in the game's Steam library and the custom quests in your Documents
folder - gets its catalogue built when the game starts, in the background, one game at a time,
while the address space is still clean; the menu stays usable. A later load of that game only reads
its files. What it costs: 49-85 MB of gray icons on disk per game, and about a minute and a half
of background work at the first start after you install or update custom games (thirteen large
ones took about 100 s here); a start with nothing new checks a stamp per game and takes no time.
The log says one line per game and one summary (`catalogue: custom games at start: N found - ...`,
with the free address space before and after). If you load a custom game while its own build is
still running, the load waits for that game alone (the window may stop answering for those
seconds); once you load a custom game or enter any world, the builds stop after the game in hand,
and the games not reached are built at the next start (the log names them). A mod installed while
the game runs is built at the next start too: until then the tab is off in it, and the log says so.
A custom game's resource archive that cannot be read (a damaged download, a zeroed file) is skipped
with one warning naming it - `catalogue: the <game> game: skips an unreadable archive (...)` - and
the base game's archives stand in for it; the game keeps its tab. An item whose icon only that archive holds is left out of the set until the archive can be read again (the repaired file rebuilds the set); what you stored of it stays in your journal. A broken archive of the base game
itself still stops the catalogue (repair the game's files).
Nothing is ever built inside a running game. Everything of that game lives in `mods\<mod>\` (the folder name may end
in `-<8 hex digits>`): its catalogue and lists, its gray icons (49-85 MB of disk per game), its
`uniquetab.ini`, its buttons file, its place on the pages and its collection journal. The groups
file `uniquetab-groups.ini` stays one file for every game; a `[view:<mod folder name>]` block in it
applies in that game only. Later loads read the files (no pause) until the game or the mod
changes. Going back to the main campaign switches back to the main pages. Each custom game's
caravan is read from its own database: the caravan window the game's HUD record names, its Transfer
page and the Transfer sack's place and cells.

**A modded caravan's own size.** A mod that brings a larger caravan is read as it is: SVAERA's caravan
window is 660 x 699 with a Transfer area of 20 x 20 cells. The collection uses the mod's caravan
area - 20 x 20 cells when the buttons and the label are on the screen (`pad_screen`, with
`label_screen` or the label in the pinned buttons), 20 x 19 when the buttons sit in the window under the grid (their band takes the
last row; `grid_660x699=11,57,640,640` in the editor gives the 20th row back) - and Ctrl+wheel and
Ctrl+PageUp / PageDown answer over the whole area. The game's own Transfer page keeps 16 x 15: the
game itself always makes its Transfer sack 16 x 15, whatever size a mod's records ask for, so in
SVAERA the cells beyond 16 x 15 on that page are painted but take no item, and the game's own sort
on that page sorts into 16 x 15 (Storage and the Relic Vault have the mod's size; that is the
game's own). The mod never resizes or moves the game's Transfer page; with the buttons in the window
they sit, on that page too, in the band under the collection's area, over its last painted row.

**SVAERA's souls.** SVAERA's soul rings (4,586 records under `records/item/equipmentring/soul/` and
`records/item/formulas/souls/`) have a Souls page of their own, the first of SVAERA's groups, so the
Rings pages hold the other rings. Its button is `[SOU]`: an existing SVAERA buttons file gets it
once, after its last button, when SVAERA is entered (before the caravan opens), even when its `[unassigned]` already lists the souls group (only the
"added once" line there keeps a deleted `[SOU]` off). It is a built-in `[view:svaera_customquest-8350d063]` block of the
groups template; a group of your own groups file is tried first, as everywhere.

A custom game's items get their own pages, and its default buttons file has one button per
group of that game - an item class none of the game's groups knows lands in an Other group, with
its own button. The game has 2 GB of
address space; when less than 64 MB of it is free in one block to read a built catalogue, the tab
stays off in that mod and the log says why (`live: the tab is off in this game (the <mod> game)
because the largest free address block is N MB, ...`) - the main game is untouched, and the next
load of that mod tries again.

**The start-up builds' reserved range.** When at least one game's catalogue has to be built, the
mod reserves one 128 MB range at the top of the game's address space as soon as it is loaded,
before the game fills memory, and every start-up build works inside it. When the builds end (or
stop because you loaded a custom game or entered a world) the range is given back in one piece,
so the builds leave no scattered blocks behind and a large custom game's database later finds
one large free block. Nothing is reserved when nothing needs building. A build that would need
more stops, and its game is built at the next start (the log says so). The largest custom game
here needs about 76 MB of it. The log's `mem: the start-up builds ended | ... | arena: ...` line
gives the range's place, the most its builds used, and the free address space before and after
it was given back.

**Memory: restart the game between large custom games.** Titan Quest is a 32-bit program and TQ.exe
is not large-address-aware, so the whole game has 2 GB of address space. Large custom games played
one after another in one session can exhaust it: a game loaded after another large one may fail to
load, or its tab stays off. Restart the game before you switch to another large custom game. Some
mods ship a 4 GB patch for TQ.exe.

### Your buttons

The first time you open the tab, the mod writes `uniquetab-buttons.ini` beside `uniquetab.ini` (a
mod's own in `mods\<mod>\`, the first time you open the tab while you play it). Edit it freely: it
is re-read within a second. Delete it and it is written again the next time the tab opens. A
mistake anywhere makes the mod ignore the whole file, and the log names the line: the buttons and
words of its last good read stay in force (the built-in ones when it was never read good).
A line starting with `;` is a comment, and so is the rest of a line after a blank and a `;`; put a
value in "double quotes" to keep blanks or a `;` in it. Save it as UTF-8: a `name`, `caption` or
template may use any alphabet (see "Alphabets" below; the rollover lines take any letter too).

- `[tab]` holds the tab's words:
  - `label.format.1` ... `label.format.4`: the label line's templates; the first that fits is
    drawn. Placeholders: `{group}` (the page name), `{owned}`, `{entries}`, `{first}`, `{last}`,
    `{rows}`, `{all}`, `{coll}`, `{found}` (the search's words, empty while no query stands) and
    `{mode}` (always empty in Titan Quest); `{{` and `}}` are braces. `label.unknown` stands for a
    count the mod does not know yet (`?`).
  - `tooltip.yes`, `tooltip.no`: the rollover lines (*In your collection* / *Not in your
    collection*); `tooltip.roll`: the words after `tooltip.yes` on a monster infrequent you hold in
    another roll (quote it to keep its leading blank); `tooltip.class_yes`, `tooltip.class_no`:
    their colours, a game text style number (0..57; 22 and 24). These were `tooltip_text_yes`,
    `tooltip_text_no`, `tooltip_class_yes` and `tooltip_class_no` in `uniquetab.ini`: a value you
    had set there is carried into the main game's `[tab]` once.
  - `mat.caption`: the Transfer toggle's caption (*Transfer*, shortened to *Trans.* or *Tab* when
    it does not fit; a caption of your own is cut to what fits); `own.caption`: the OWN button's.
  - `next.caption`: the NEXT button's caption (`>>`; a longer one is cut to the button, and the
    paged pad makes NEXT wide enough for its first five letters, as for a group caption).
  - `edit.caption`: the EDIT button's caption (`EDIT`; read, never written into the generated
    file). A caption too wide for its cell is drawn smaller, else cut, as a group's is.
  - `font.caption`, `font.label`: the fonts of the captions and of the label line.
  - `size.caption`: one fixed caption size for every button (0 = fitted, the default), still bounded
    by the button's height, each caption cut to its button; `size.caption_max`: the largest caption
    size when they are fitted (14 by default; a button's own `size` is that button's largest caption size and wins over both);
    `size.label`: the label's size (wins over `plate_label_size`; the label row still cuts it). The
    sizes are at UI scale 1 and grow with it.
  - `color.caption`, `color.label`, `color.ring`, `color.mark` (`RRGGBB`): the button captions, the
    label line, the ring of the page shown and the plate of a button the search marks (its hover is
    a shade lighter). Unset, the tab keeps its own colours; a lit button's text and a dimmed caption keep
    theirs; the caption on a marked plate takes `color.caption` when it is set (else its near-white).
- `[HEL]`, `[TOR]` ... `[ART]`, `[BLU]`, `[REL]`: one button per section, in pad order - `name` (the page name in the
  label line), `caption` (the button's text: Helm, Torso ... Artf, BLU, Relic), `groups` (the group ids:
  `head`, `chest`, `hands`, `legs`, `amulet`, `ring`, `shield`, `axe1h`, `mace1h`, `scepter`,
  `sword1h`, `ranged1h`, `spear2h`, `ranged2h`, `artifact`, `blueprint`, and `relics` for the Relic
  page: with `collect_relics=0` a section naming `relics` gives no button, one INFO line says so,
  and the rest of the file stays in force), `size` (the caption's
  largest size, 6..36: drawn at the largest size up to it that the button holds whole, even above the pad's size and `size.caption`) and `font` (that button's caption font, see "Alphabets" below). A button
  without a `caption` shows its tag; a button's `name` is the label's `{group}`.
- The pad's top row draws one button per section, in the file's order. When they do not all fit, a
  NEXT button (`>>`) at the row's end shows the next buttons. The second row (Transfer, OWN, `<`
  `>`, the search field and the label) never changes. Several ids on one button make ONE page of all
  their items, in group order, in the largest group's slots: the label's numbers, OWN and the
  search's count are that page's. A group on no button is not on the pad (it is listed under
  `[unassigned]`): put it on a button to reach it. Ctrl+wheel and Ctrl+PageUp / PageDown walk the
  buttons in the file's order, with Transfer as a stop.
- `[unassigned]`: the groups no button holds. The mod rewrites this section (and only it).
- A built-in page that arrived after your file was written gets its button once, at the end of
  the file: `[REL]` (the Relic page), and in SVAERA `[SOU]`. The "added once" line the mod writes
  under `[unassigned]` keeps a button you deleted off.
- **Alphabets.** Every alphabet draws, with no font line. Latin letters draw in the caption font
  (`fonts/albertus mt light.fnt`, the `font` key of `uniquetab.ini`); Cyrillic and Greek ones in
  the game's own Arial (`fonts/arial.fnt`, else `fonts/dejavuserif.fnt`); Chinese, Japanese and
  Korean ones in the game's Asian font (`fonts/override_asia.fnt`: Han, kana and Hangul). Each
  caption, the label line and the search field's text are drawn in the font their own letters
  need, so a Russian caption draws in Arial beside English ones in Albertus (a button's own
  `font =` keeps that button apart). A letter none of the game's nine fonts has - Thai, Arabic,
  Hebrew, Devanagari ... - is drawn all the same, in the font that has most of the text, and the
  game's own fallback decides how it looks (it may show a box or nothing); the log says so once.
  Unlike in Grim Dawn, Windows' own fonts never draw a letter here: the game draws only its own
  fonts. A `font.caption` / `font.label` line, or a button's `font =`, names one of the fonts in the
  game's `Resources\Fonts.arc` - `fonts/albertus mt light.fnt`, `fonts/albertus mt.fnt`,
  `fonts/albertus_cz.fnt`, `fonts/arial.fnt`, `fonts/arial_russian.fnt`, `fonts/dejavuserif.fnt`,
  `fonts/linlibertine_r.fnt`, `fonts/override_asia.fnt`, `fonts/times_ee.fnt` - and the letters
  start from it; any other path is not used (one WARN names it) and the caption font draws. Every
  caption is measured with its font's own letter widths, the way the game draws it, so a long
  caption is drawn smaller, then cut, never past its button. The limits: no right-to-left order and no joined or shaped letters
  (an Arabic or Hindi word draws as separate letters, left to right); nothing above U+FFFF
  (emoji): a caption holding one falls back to the button's tag, a name to the group's own name, a
  `[tab]` word or a label template to the built-in English, and one WARN line says what was passed
  over each time the file is read (a button with its own `font =` keeps its caption as written).
### Your own groups

The tab's groups are built in: the 15 equipment pages, the Artifacts and the Arcane Formulae, each
taking its item class (a Rare item of an equipment class too, while `collect_mi=1`). The Relic
page is apart from them: `collect_relics` decides it. To change
them, create `uniquetab-groups.ini` beside the main `uniquetab.ini` (UTF-8, a BOM is fine). It
ADDS to the built-in groups: your blocks are tried first, so a block of yours takes its items off
their usual page. Your new groups come first in the group order; the buttons file decides where
their buttons go (a new group is listed under `[unassigned]` until you put it on a button). The
mod NEVER writes this file - no template, no copy, no rename: it only reads yours. Each game has
its own catalogue (the main campaign's in the mod folder, a custom game's in its `mods\<mod>\`
folder), and every one of them is built from this one file beside the main `uniquetab.ini`; a
`[view:<mod folder name>]` block in it applies in that game only (see "Custom games"), a
`[view:vanilla]` block in the main campaign (and in a custom game whose folder is named `vanilla`). A
`uniquetab-groups.ini` in a mod's `mods\<mod>\` folder is not read.

- `[group <id>]` - a block of rules for one group; the id is `a-z 0-9 _ . -`, up to 40. Keys:
  `label` (the group's name), `tag` (the 3-character button caption), and the filters `class`
  (the item class: `ArmorProtective_Head`, `ArmorProtective_UpperBody`, `ArmorProtective_Forearm`,
  `ArmorProtective_LowerBody`, `ArmorJewelry_Ring`, `ArmorJewelry_Amulet`, `WeaponMelee_Sword`,
  `WeaponMelee_Axe`, `WeaponMelee_Mace`, `WeaponHunting_Spear`, `WeaponHunting_Bow`,
  `WeaponHunting_RangedOneHand`, `WeaponMagical_Staff`, `WeaponArmor_Shield`, `ItemArtifact`,
  `ItemArtifactFormula`), `path` (record path patterns: `*` one folder level or part of one, `**`
  any depth, `?` one character), `rank` (Legendary, Epic, Rare, Common, any), `flag` (set, relic
  = an artifact, equipment, blueprint = an arcane formula; faction, augment and extra match
  nothing here), `source` (base or mod: in a custom game, `mod` takes the records only that
  game's own files have and `base` every other; in the main campaign every record is `base`).
  Values in one key are alternatives; every key of a block must match. A class no item has is no
  error: the block takes nothing. Without `label` a group takes its id's last part; without
  `tag`, three letters of its label that no other group holds.
- `[exclude]` - a block whose items stay off every page (built-in groups included). An item you
  own that no page shows stays in your collection; the label line counts it (`+N not on these
  pages`), and it is back once a group takes it again.
- `[view:<key>]` - the blocks after it apply only in that game: `vanilla` for the main campaign, or a
  custom game's folder name under `mods\` (it may end in `-<8 hex digits>`); `[view:*]` ends it. A
  custom game whose folder is itself named `vanilla` answers to `vanilla` too. At every start the log
  names, in one INFO line, a `[view:...]` key of your file that no installed game answers to (a typo,
  or a mod you removed): its blocks never apply.
- `[view:collect_mi]` - the blocks after it apply only while `collect_mi=1`; `[view:*]` ends it.
  The built-in groups put the monster infrequents on their pages there (one Rare rule per
  equipment class).
- `[auto]` - `min = N` (1..1000), `enabled = yes|no`: a class the mod does not know gets its own
  group when it has at least `min` items, smaller ones share the group Other. The built-in `min`
  is 1000, the most `min` allows, so every such class with fewer than 1000 items shares Other; a
  class with 1000 or more gets its own group (and with `enabled = no` such items are on no page).
  Today every catalogue item has one of the classes above, so Other stays empty.
- A block with a built-in id (`[group ring]`, `[group artifact]` ...) adds rules to that group;
  its `label`/`tag` replace the built-in ones. The built-in ids: `head`, `chest`, `hands`, `legs`,
  `amulet`, `ring`, `shield`, `axe1h`, `mace1h`, `scepter`, `sword1h`, `ranged1h`, `spear2h`,
  `ranged2h`, `artifact`, `blueprint`, and `shoulders`, `waist`, `feet`, `medal`, `offhand`,
  `dagger`, `axe2h`, `mace2h`, `sword2h` (slots no Titan Quest item fills: empty until a block of
  yours fills them). Two groups with the same name stay two groups - putting groups under one
  button is the buttons file's job.
- A file with an error is ignored as a whole, and the built-in groups are used. The log says it
  in one WARN line naming the file and the line:
  `catalogue: uniquetab-groups.ini is ignored - <file>: line 3: the tag MAT is the pad's own; the built-in groups are used`.
  The reasons are, among others: `the group id "other" is reserved` (also `vanilla`, `owned`,
  `next` and `auto.` ids), `bad group id "Rings" (a-z 0-9 _ . -, 1..40)`, `a tag is exactly 3
  printable ASCII characters`, `the tag OWN is the pad's own` (also MAT), `unknown key "kind"`,
  `unknown section [groups]`, `unknown flag "shiny"`, `the [group] block has no filter (class,
  path, rank, flag or source)`, `group ring already has the label "Rings"`, `min must be 1..1000`.
- A change regenerates the catalogue once, at the next start: the log says `catalogue: building
  from the game's database (the groups changed (uniquetab-groups.ini or the built-in groups),
  ...) ...`, then `catalogue: groups from <file>: 25 built-in + 1 of yours (0 built-in group(s)
  given more rules), 17 group(s)`. Without a file it says `catalogue: the built-in groups (25)`.

Example 1 - the Legendary rings on a page of their own (46 of them; the 38 Epic ones stay on
Rings):

```
[group ring.legendary]
label = Legendary Rings
tag   = RNL
class = ArmorJewelry_Ring
rank  = Legendary
```

Example 2 - everything Eternal Embers adds on one page, from a path pattern (210 items: uniques,
monster infrequents, artifacts and formulae, off their usual pages):

```
[group embers]
label = Eternal Embers
tag   = EMB
path  = records/xpack4/**
```

Then give the new group a button in `uniquetab-buttons.ini`, for example:

```
[EMB]
caption = Embers
groups  = embers
```

## Multiplayer

The collection works in multiplayer, **as the host and as a client**, with `mp_collect=1` (the
default): deposits and takes behave as in single player, for you only, and every player keeps
their own collection file on their own machine. Set `mp_collect=0` to keep the page on screen but
refuse every move while you play multiplayer.

## Your collection, and backing it up

Everything lives in `scripts\uniquetab\` - the main campaign's files in the folder itself, and
each mod's in a folder of its own under `mods\`:

```
scripts\uniquetab\
    tq-uniq-items.jsonl            the main campaign's collection
    tq-uniq-items-custom.jsonl     a custom map that names no mod
    uniquetab.ini                  your settings
    uniquetab.state                where the collection was left (group, row, OWN)
    uniquetab.log                  the log (and its older copies)
    catalogue.bin, uniq-*.txt, gray\ ...   generated from your game
    mods\
        <mod>\                     one folder per mod, named like its journal
            tq-uniq-items-<mod>.jsonl   that mod's collection
            uniquetab.ini          that mod's own settings (complete)
            uniquetab.state        where the collection was left in that mod
            uniquetab.log          that mod's lines of the log (every line is in the main log too)
```

| file | what it is |
| --- | --- |
| `tq-uniq-items.jsonl` | **this is your collection** (the main campaign) |
| `tq-uniq-items-custom.jsonl`, `mods\<mod>\tq-uniq-items-<mod>.jsonl` | the same, for a custom map that names no mod and for a mod |
| `tq-uniq-items*.jsonl.bad-<stamp>` | a journal the mod could not read, copied aside before it wrote a new one: what it held is still in it |
| `tq-uniq-items*.csv` | the spreadsheet copies, with `export_csv` on, beside their journal |
| `uniquetab.ini`, `mods\<mod>\uniquetab.ini` | your settings, and a mod's own (see "Settings") |
| `uniquetab-buttons.ini`, `mods\<mod>\uniquetab-buttons.ini` | the buttons and words of the tab (see "Your buttons") |
| `uniquetab-groups.ini` | your own groups, optional (see "Your own groups") |
| `uniquetab.state`, `mods\<mod>\uniquetab.state` | written by the mod: the group, the row and the OWN switch the collection was left on. Delete it to start on the first group |
| `uniquetab.log` (and its older copies), `mods\<mod>\uniquetab.log` | what happened in the last sessions; a mod's file holds the lines written while that mod was played |
| `catalogue.bin`, `catalogue.stamp`, `uniq-records.txt`, `uniq-groups.txt`, `uniq-excluded.txt`, `uniq-geometry.txt`, `uniq-relics.txt`, `uniq-search.txt`, `gray\` (and the same in each `mods\<mod>\`) | generated from your game; delete them and they come back |
| `plates\` | the layout plates the mod paints for a moved grid; delete them and they come back |

**Yours and the mod's.** In each folder the mod writes only the names above. *Yours* is read by
the mod and not rewritten by it (except the part named); *the mod's* is rewritten or rebuilt at
will, and deleting it loses nothing you made.

| folder | yours | the mod's |
| --- | --- | --- |
| `scripts\uniquetab\` | the journals `tq-uniq-items.jsonl` and `tq-uniq-items-custom.jsonl` (the collection) and their `.bad-<stamp>` copies (in `journal_dir` when that is set); `uniquetab.ini` (written at the first start with every option, then changed only by an upgrade, the layout editor's SAVE and `layout_edit=1` setting itself back to 0, each said in the log); `uniquetab-buttons.ini` (written once, then only its `[unassigned]` section, a new built-in group's button added once, and at the upgrade from 1.0.x the old rollover words carried into `[tab]` once); the optional `uniquetab-groups.ini` (never written) | `uniquetab.state`; the log `uniquetab.log` and its older copies; the spreadsheet copies `tq-uniq-items*.csv` (in `journal_dir` when that is set); the generated set `catalogue.bin`, `catalogue.stamp`, `uniq-records.txt`, `uniq-groups.txt`, `uniq-excluded.txt`, `uniq-geometry.txt`, `uniq-relics.txt`, `uniq-search.txt` and `gray\`; the layout plates in `plates\` |
| `scripts\uniquetab\mods\<mod>\` | that mod's journal `tq-uniq-items-<mod>.jsonl` and its `.bad-<stamp>` copies (in `<journal_dir>\mods\<mod>\` when `journal_dir` is set), its own complete `uniquetab.ini` and its `uniquetab-buttons.ini` | its `.csv` (in `<journal_dir>\mods\<mod>\` when `journal_dir` is set), its `uniquetab.state` and `uniquetab.log`, and that game's generated set (the same names, `gray\` included) |

The generated set: `catalogue.bin` is every collectable item of that game with its name, its
icon and its size, and `catalogue.stamp` what it was built from (a change rebuilds it);
`uniq-records.txt` the item records in box order, one per line; `uniq-excluded.txt` each record
left out, with the reason; `uniq-geometry.txt` the caravan window and its Transfer area as the
game's database gives them; `uniq-relics.txt` the relics and charms of the Relic page, block by
block; `uniq-search.txt` the search's item text (see "Searching"); `gray\`
the gray copy of every item's icon. `uniq-groups.txt` is the pages as the tab reads them: plain
text, one line per thing, its fields separated by tabs:

| line | fields | how many |
| --- | --- | --- |
| `G` | `<index> <label> <cols> <rows> <cell w> <cell h> <item count> <id> <tag>` | one per group, in page order: its slot in cells and pixels, how many items it holds |
| `E` | `<item record> <w> <h>` | one per item of the group above, in box order: its record and its size in cells |

To see exactly what is in a group, read its `G` line and the `E` lines below it: the `<id>` is the
one a button's `groups =` names, the `<tag>` its default button, and each item is named by its own
record, as the game's database names it (`records/item/equipmenthelm/u_e_athlon.dbr`).

**A mod's journal from an earlier version moves once.** Earlier versions kept
`tq-uniq-items-<mod>.jsonl` (and its `.csv`) beside the main one. At the first start of this
version it is moved into `mods\<mod>\` - renamed, never copied or merged: the `.csv` first, the
journal itself last. If a file of the set is open in another program the move is undone, the set
stays where it was and is read from there, and the next start tries again (the log says so). If
`mods\<mod>\` already holds a journal of that name, nothing is moved and both files are kept: the
mod uses the one in `mods\<mod>\`, and the log names both so you can decide. If only another file
of the set is in the way (an old `.csv`), nothing is moved either: the mod keeps reading the
journal beside `uniquetab.ini`, and the log names the file to move or delete. Going back to an
earlier version: it does not look in `mods\`, so move `mods\<mod>\tq-uniq-items-<mod>.*` back
beside `uniquetab.ini` first, or that mod's collection shows empty.

**Back up `tq-uniq-items*.jsonl`.** That file *is* the collection: every stored item, with its
full identity, one item per line. It is plain text - you can open it and search it. Copy it
somewhere safe as often as you would back up a save.

**It does not travel with Steam Cloud.** Steam Cloud carries your characters, not the game
folder, so on another machine your characters arrive without their collection. Copy the journal
across yourself, or set `journal_dir` (under `[advanced]`) to a folder that is synced or backed up
and copy the journals there with the game closed: the collection is then kept there.

**TQVault and other save editors** see your character and caravan files, not the collection: an
item in the collection is in the journal only. After a deposit or a take, let the game save your
character (leave the caravan and keep playing, or quit to the menu) before you open the save in an
editor.

## Every item at once (the extras folder)

The zip carries one more file, `extras\tq-uniq-items-all.jsonl`: a collection in which **every one
of the 2,774 records is collected**, one copy each (a monster infrequent as its plain record: its
row has no affixes). It is an ordinary collection file, the same
format the mod writes, and each line holds an identity the mod can hand back - take one out and a
**real item** of that record lands in your bag. `extras\tq-uniq-items-all.report.txt` beside it
says how it was made.

To use it:

1. Quit the game.
2. Back up your own collection: copy `scripts\uniquetab\tq-uniq-items.jsonl` somewhere safe (if you
   have one; for a custom quest or a mod, that set's own file - see step 3).
3. Copy `extras\tq-uniq-items-all.jsonl` into `scripts\uniquetab\` and name it
   `tq-uniq-items.jsonl`, over your own. That is the main campaign's collection. A custom quest or
   a mod has its own name (*One collection per save set*): load a character there once first,
   then use exactly the name that session's log line `journal: set "..."` shows, plus `.jsonl`.
   (A mod that changes the item list shows only the records it knows.)
4. Start the game. Every group is complete and every unique's tooltip says it is in your
   collection.

What it means: a complete collection to browse. Taking items out of it is your choice - the mod
does not judge; use a throw-away character if you care. To go back, quit the game and copy your
backup over the file again; the mod never deletes a collection file, so your own is exactly as
you left it.

One caveat: the file was generated, not played. The identities are the plain record with no
prefix, suffix or relic, and a seed chosen from the record's path, so a taken item has a
plausible roll and nothing more.

## When something goes wrong

The first thing to do is read `scripts\uniquetab\uniquetab.log`. It is plain text and it is written
to explain itself. If you report a problem anywhere, send that file (and, for a collection
problem, your `tq-uniq-items*.jsonl`).

- **There is no log at all** - neither in `scripts\uniquetab\` nor in the Documents fallback
  (`[OneDrive\]Documents\My Games\Titan Quest - Immortal Throne\uniquetab`). The loader never
  loaded the plugin. Check that the loader
  (`dinput8.dll`) sits beside `TQ.exe` and `uniquetab.asi` in `scripts\` below it, and that Smart
  App Control or SmartScreen did not block it.
- **The log says the mod is OFF.** That is the mod refusing to run on a game it does not
  recognise, and it is deliberate. It checks every single thing it needs to know about the game's
  memory before it installs anything at all, and if one of them cannot be found or no longer looks
  like itself, the whole mod stands down rather than running half-connected. The log names the one
  that failed. Usually this means the game has been patched and the mod needs an update; nothing
  is lost and your collection file is untouched.
- **The log says `view: unavailable - <reason>`.** The mod runs, but the collection page cannot be
  shown safely on this game; the Transfer page stays the game's own and the toggle does nothing.
  The reason names what is missing.
- **The page looks plainer than it should.** The mod falls back rather than draw something wrong:
  a dark veil where a gray icon is missing, thin frames where a slot plate cannot be drawn, a thin
  ring round a slot where the game's own slot tint is not available. The log says which and why.
- **A deposit or a take was refused.** The log says why, in a sentence (once per reason in a row
  at `log_level=info`; set `debug` to see every refusal). Refusals
  never move anything.
- **A warning says "check for a duplicate".** See *"Check for a duplicate"* above.
- **A custom game does not load, or its tab stays off, after another large one.** The 32-bit game
  has run out of address space: restart the game between large custom games (see "Memory: restart
  the game between large custom games" under *Custom games*).
- **The `mem:` lines.** The log writes one `mem: <point> | ...` INFO line at each point that
  changes the game's memory: the mod's load, the start-up builds' end, every world up and gone,
  every game switch both ways, right before a custom game's database loads, the search's start,
  its end and each group it reads at once, and the caravan's close. They are written with
  `enabled=0` too, so two sessions - the tab on and off - compare field by field. The fields:
  `space` (the largest free block of the address space and all of it free, then what is taken by
  program images, mapped files and private memory), `process` (the private bytes and the working
  set), `heap` (the game's main heap: reserved, committed, in use), `engine` (the game's record
  cache, its objects, the objects waiting for deletion and its seven resource managers'
  bytes), `mod` (the search text, the items made for the page, the gray icons held, the tables
  of a game you left that are not freed yet) and, while the start-up builds run, `arena`. The
  search's lines add what changed since it started. Nothing in them is an error; the first one
  of a session also says where the engine's counters were read (`mem: the engine counters'
  inputs`).

## Uninstall

**Take everything out first.** The collection lives in the journal; the game itself does not know
it exists, so a plugin you remove leaves your items in a file nothing reads. Take each item back
into your inventory (or stash) while the mod is still installed.

Then delete `scripts\uniquetab.asi`. The game runs, the pad under the grid is gone, and the Transfer
page is back to normal. Delete `scripts\uniquetab\` as well if you want the journal, the settings
and the generated files gone.

`undeploy.bat "<the folder that holds TQ.exe>"` in the source tree removes the plugin. With
`--purge` it also deletes the files the mod generated in `scripts\uniquetab\` - the catalogue, its
stamp, the lists, `uniq-search.txt`, `gray\` and `plates\` - each by its name, and refuses to
while any collection still holds an item: it reads every `tq-uniq-items*.jsonl` in
`scripts\uniquetab\` and in every `mods\<mod>\` folder - a copy you made under another name
counts too - and refuses on a stored item, a taken one not yet saved, or a file it cannot read.
Your own files stay (the journals, the ini files, the log, `uniquetab.state`, the `.csv` copies,
any file it does not name), and so does `mods\`; delete `scripts\uniquetab\` by hand when you
are done.

The loader (`dinput8.dll`) is shared with any other ASI mod you use, so delete it only if this was
the only one. Neither script ever touches it.

## Licence

MIT - see `LICENSE`. Third-party components and what comes from the game are listed in
`THIRD_PARTY.md`. This is an unofficial mod, not endorsed by or affiliated with THQ Nordic or the
makers of Titan Quest.
