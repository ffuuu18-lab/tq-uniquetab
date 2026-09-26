# Unique Collection Tab for Titan Quest

A collection tab for Titan Quest Anniversary Edition, built into the caravan's Transfer page. It is
the Titan Quest port of the Grim Dawn mod of the same name. `CHANGELOG.md`, which comes with the
source rather than in the zip, lists what changed in each release.

Put an Epic or a Legendary into it and the page keeps a slot for that item: one slot per item
record, filled or empty, sorted into 15 groups - Helms, Torso, Arms, Legs, Amulets, Rings, Shields,
Axes, Maces, Staves, Swords, Throwing, Spears, Bows and Artifacts. Every Epic and Legendary of the
game and its expansions is there, the ones only monsters carry included: 1,588 records. It is a
place to put the uniques you want to keep, one copy of each, and a way to see at a glance what you
have found and what you have not.

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
writes the item catalogue there, with the three record lists beside it and a gray copy of every
collectable item's icon in `scripts\uniquetab\gray\` (about 27 MB). It takes about five seconds at
the main menu, once. It happens again after a game update, and never otherwise. The settings file
`scripts\uniquetab\uniquetab.ini` is written at the same time, with every option and a line of
explanation for each.

If the game folder cannot be written to, the mod uses
`Documents\My Games\Titan Quest - Immortal Throne\uniquetab` instead (`OneDrive\Documents\...`
when that is where your Documents folder lives) and says so in its log.

**Smart App Control and SmartScreen** may block an unsigned DLL. You can tell: the mod always
writes `scripts\uniquetab\uniquetab.log` when it starts (or the Documents fallback above), so a
game that runs with no log in either place never loaded the plugin. Check the loader first, then Windows Security's app control.

## Using it

Open the caravan and go to the **Transfer** page. Under the grid, inside the caravan's frame, the
mod draws a small pad of buttons:

- **Transfer** (its lamp lit while the real Transfer page shows) switches the Transfer page to the
  collection and back. **F6** does the same
  (`view_hotkey`). Your real Transfer items are untouched and come back the moment you switch
  back.
- The **15 group buttons** (Helm, Torso, ... Artf) select a group and switch the collection on.
- **OWN** toggles between showing every item of the group and only the ones you already have.
- **<** and **>** (half a cell each, beside OWN) turn the group's pages. After them, past the
  search field, the label says the group, the slot rows shown (`rows 1-5 / 12`), how many of the
  group's items you own and, after `all`, how many of the whole
  collection's items you own (`all 312 / 1588`). While a search stands, its count follows
  (`found 9`). When the line is longer than its box it gets shorter first: the compact form
  (`Torso 3-7/22  12/241  all 312/1588  found 9`), then the search's short form (`=9`), then
  without the rows, then without the `all` count, and last, with a search only, without the
  group's owned count. A line that still does not fit is drawn smaller, down to size 6; with the
  default settings that never happens.

Over the page:

- The **mouse wheel** scrolls the group by one row of slots; **PageUp / PageDown** turn a page.
- **Ctrl+wheel** (or **Ctrl+PageUp / Ctrl+PageDown**) moves to the next or previous group. The
  real Transfer page is one of the stops in that cycle, so it works from there too.

A slot you own shows the item in colour. A slot you have not filled shows the item's icon in gray
and is drawn bare - no slot tint, no border - until the cursor is on it. Hovering any slot shows
the game's own tooltip, and every item tooltip in the game - in your inventory, on the ground, at
a merchant - carries one more line: **In your collection** or **Not in your collection**
(`tooltip_mark`).

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
  read (`search_buttons`); the others are dimmed. The label says `indexing k/1588` while the mod
  reads the items the first time (a few seconds, in the background) and then `found N` (`=N` when
  the line is short of room).
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

- **Not a unique of the collection** - a common or rare item, a potion, a relic or a charm on its
  own. Only the 1,588 Epic and Legendary records have slots.
- **Already in the collection** - the collection keeps one copy of each record.
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
charm, the same seed.

### One collection per save set

Titan Quest keeps separate characters and a separate Transfer for the main campaign, for custom
quests and for a mod, and so does the collection. Each has its own journal in
`scripts\uniquetab\`, never mixed:

| you play | your collection is |
| --- | --- |
| the main campaign | `tq-uniq-items.jsonl` |
| a custom quest, or a mod | `tq-uniq-items-<its name>.jsonl`, the name written as below |
| anything else outside the main campaign (the game names no quest or mod) | `tq-uniq-items-custom.jsonl` |

The name of a custom quest or a mod is written the way a file name can hold it: lower case, the
letters and digits kept, every other run of characters turned into one `_`, at most 48 characters
(a name that is just `custom` becomes `mod_custom`). When anything had to be changed or cut, a `-`
and a short hex code follow, so that two names which look alike afterwards keep separate files:
`My Mod` gives `tq-uniq-items-my_mod-` and a code. You never need to work it out - when a world
loads, the log line `journal: set "..."` names the file (add `.jsonl`).

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

Each line in the file carries its own explanation. These are the ones worth knowing about.

**[general]**

| key | default | what it does |
| --- | --- | --- |
| `log_level` | `info` | how much goes in the log: error, warn, info, debug, trace |

**[collection]**

| key | default | what it does |
| --- | --- | --- |
| `mp_collect` | `1` | 1 = deposits and takes work in a multiplayer game, hosted or joined, for you only; 0 = the page shows there but every move is refused |

**[view]**

| key | default | what it does |
| --- | --- | --- |
| `view_hotkey` | `117` | the key that switches the Transfer page to the collection and back, as a Windows virtual-key code (117 = F6; 0 = the Transfer button only) |

**[display]**

| key | default | what it does |
| --- | --- | --- |
| `group_buttons` | `1` | draw the pad of group buttons and let it be clicked |
| `page_hotkeys` | `1` | the wheel scrolls rows, PageUp/PageDown turn pages, Ctrl changes group |
| `plate_label` | `1` | draw the group label beside the buttons |
| `owned_marks` | `3` | how an item you do not own yet is marked: 0 none, 1 a dark veil, 2 a thin frame, 3 its icon in gray |
| `have_marks` | `0` | 1 = an item you do not own yet but carry somewhere (inventory, stash, Transfer, Relic Vault) gets a small corner tick |
| `tooltip_mark` | `1` | every item tooltip says whether it is in your collection |
| `slot_plates` | `3` | the ground under each slot: 0 none, 1 a dark plate (thin frames where it cannot be drawn), 2 thin frames, 3 the equipment window's own slot art |
| `grid_cover` | `1` | cover the grid cells no slot uses in the caravan's ground colour |
| `owned_only` | `0` | the OWN filter at start-up; the OWN button writes it back |
| `search` | `1` | the search field on the collection page; 0 = no field, no marks, no key taken |
| `search_mark` | `1` | how a found item is marked: 1 a thin frame round its slot, 2 a wash under it, 3 both |
| `search_mark_color` | `gold` | the colour of that frame and wash: `gold`, `green`, `white`, `red`, `orange`, `cyan`, `magenta`, `blue`, or `#rrggbb` |
| `search_transfer` | `1` | the search field on the real Transfer page too, marking the items there that match; 0 = on the collection page only |
| `search_buttons` | `1` | turn a group button blue when its group holds a match |
| `search_lore` | `0` | 1 = also search the items' story text |

**[files]**

| key | default | what it does |
| --- | --- | --- |
| `export_csv` | `0` | write a spreadsheet copy of the journal beside it (`tq-uniq-items*.csv`): 0 off, 1 the collection, 2 also the takes no save has settled yet |

**[advanced]** is tuning. The ones a player may want:

| key | default | what it does |
| --- | --- | --- |
| `pad_x` | `0` | move the pad right (or left, negative) of its centred place, in page pixels |
| `pad_y` | `4` | the pad's gap below the grid |
| `pad_h` | `14` | the height of one row of buttons |
| `pad_gap` | `1` | the gap between two buttons |
| `plate_label_size` | `13` | the height of the button captions and the label, 8..32 |
| `search_prebuild` | `0` | 1 = read every item for the search when the collection is first shown (a few seconds, in the background) instead of at the first search |
| `tooltip_text_yes`, `tooltip_text_no` | empty | your own wording for the tooltip line; empty = *In your collection* / *Not in your collection* |

The rest - the caravan window's geometry, the caption font, the text language, the tooltip
colours, a slow log flush for chasing a crash - leave alone unless a line in the file tells you
exactly what it is for; every value is clamped to the range printed beside it.

## Multiplayer

The collection works in multiplayer, **as the host and as a client**, with `mp_collect=1` (the
default): deposits and takes behave as in single player, for you only, and every player keeps
their own collection file on their own machine. Set `mp_collect=0` to keep the page on screen but
refuse every move while you play multiplayer.

## Your collection, and backing it up

Everything lives in `scripts\uniquetab\`:

| file | what it is |
| --- | --- |
| `tq-uniq-items.jsonl` | **this is your collection** (the main campaign) |
| `tq-uniq-items-custom.jsonl`, `tq-uniq-items-<mod>.jsonl` | the same, for custom quests and for a mod |
| `tq-uniq-items*.csv` | the spreadsheet copies, with `export_csv` on |
| `uniquetab.ini` | your settings |
| `uniquetab.log` (and its older copies) | what happened in the last sessions |
| `catalogue.bin`, `catalogue.stamp`, `uniq-*.txt`, `gray\` | generated from your game; delete them and they come back |

**Back up `tq-uniq-items*.jsonl`.** That file *is* the collection: every stored item, with its
full identity, one item per line. It is plain text - you can open it and search it. Copy it
somewhere safe as often as you would back up a save.

**It does not travel with Steam Cloud.** Steam Cloud carries your characters, not the game
folder, so on another machine your characters arrive without their collection. Copy the journal
across yourself.

**TQVault and other save editors** see your character and caravan files, not the collection: an
item in the collection is in the journal only. After a deposit or a take, let the game save your
character (leave the caravan and keep playing, or quit to the menu) before you open the save in an
editor.

## Every item at once (the extras folder)

The zip carries one more file, `extras\tq-uniq-items-all.jsonl`: a collection in which **every one
of the 1,588 records is collected**, one copy each. It is an ordinary collection file, the same
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

## Uninstall

**Take everything out first.** The collection lives in the journal; the game itself does not know
it exists, so a plugin you remove leaves your items in a file nothing reads. Take each item back
into your inventory (or stash) while the mod is still installed.

Then delete `scripts\uniquetab.asi`. The game runs, the pad under the grid is gone, and the Transfer
page is back to normal. Delete `scripts\uniquetab\` as well if you want the journal, the settings
and the generated files gone.

`undeploy.bat "<the folder that holds TQ.exe>"` in the source tree removes the plugin; with
`--purge` it also removes `scripts\uniquetab\`, and it refuses to while any journal still holds an
item.

The loader (`dinput8.dll`) is shared with any other ASI mod you use, so delete it only if this was
the only one. Neither script ever touches it.

## Licence

MIT - see `LICENSE`. Third-party components and what comes from the game are listed in
`THIRD_PARTY.md`. This is an unofficial mod, not endorsed by or affiliated with THQ Nordic or the
makers of Titan Quest.
