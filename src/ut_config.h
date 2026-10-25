// ut_config.h - every setting the mod has, in one plain text file (uniquetab.ini, in the mod
// folder).
//
// "key=value" per line, grouped into [sections] that exist for the reader only - a key is looked
// up by NAME, so moving a line between sections changes nothing. The file is re-read once a
// second by the worker thread. Values are plain ints written by the worker thread and read by the
// render thread; a torn read is impossible for an aligned int (x86 as on x64) and a one-frame-stale value
// is harmless.
//
// ONE TABLE IS THE TRUTH. kUtCfgKeys below is the only list of keys there is: the parser walks
// it, the file the mod writes is RENDERED from it (configRender), and the test suite derives what
// it expects from it. Adding a setting means adding one row - the text and the parser cannot
// drift apart any more, because there is only one of each.
//
// THE SHORTER FILE (GD's renderer). Eight keys are plain lines under [general]: enabled,
// collect_mi, collect_relics, collect_unobtainable, tooltip_mark, log_level, view_hotkey and
// search. Every other
// row is HIDDEN (kUtCfgHidden): one line of the [advanced] blob at the end of the file, commented
// out at its default (";key=default"), and written live only while its value differs. The parser
// reads a live line wherever it stands; a commented row is a comment, so it reads back as the
// default.
//
// GEOMETRY UNITS. The numbers that place something on the page (plate_label_size and the [layout]
// rectangles) are RECORD px - the caravan window's own coordinates, which the mod multiplies by the
// UI scale it measures at run time. plate_label_size is clamped to its safe range when it is read
// here AND where it is used; a [layout] rectangle is checked where it is used (ut_layout.h).
//
// KEYED FAMILIES (GD's). Besides the fixed keys, a family is every key made of a fixed PREFIX and a
// free SUFFIX - `pad_565x637` is the family `pad_` with the suffix `565x637`. kUtCfgFamilies lists
// them: each names its [section], its prefix, the UtCfgFamilyStore inside UtConfig that holds its
// lines, and two checks - one for the suffix (a line whose suffix fails it is an unknown key) and
// one for the value (a value that fails it is refused with a WARN and the line is not kept). The
// lines are kept in file order; a suffix given twice keeps the later value. Read one with
// configFamilyValue ("pad_565x637"), write any key - fixed or family - with configPersistStr.
#pragma once

#include <stddef.h>

#include <string>

// Bumped whenever the key set changes. A file written by an older build is MERGED, never reset:
// every key that still exists keeps the player's value, new keys take their default, keys this
// build no longer has are dropped with one log line naming them, and the file is rewritten in the
// current layout. See configReload().
// Titan Quest restarts the count at 1: a new game, a new file. A Grim Dawn uniquetab.ini copied
// across (ini_version 36) differs from it and is migrated by the rule above like any other.
// 2 = [advanced] text_language (the catalogue's names come from Text_<lang>.arc).
// 3 = [view] view_hotkey; [display] group_buttons, page_hotkeys, plate_label (GD's keys);
//     [advanced] pad_x/pad_y/pad_w/pad_h/pad_gap, plate_label_size, font, caravan_x/y/w/h, page_y.
// 4 = [display] owned_marks (0 off, 1 the veil, 2 a thin frame).
// 5 = the pad moved INSIDE the caravan frame - pad_x / pad_y are now offsets inside the
//     frame's band (pad_x from the centred spot, pad_y off the grid), pad_h / pad_gap take GD's
//     ranges (12..20, 0..4), pad_w is gone (the cell width is the design), and [display]
//     owned_only (GD's key: the OWN filter's start state, written back by the OWN button). A file
//     from any other version gets pad_x / pad_y / pad_h / pad_gap RESET to their defaults (their
//     old meaning was the column beside the window, or GD's plate), said in one log line.
// 6 = [display] have_marks (the "have one somewhere" corner tick); [files] export_csv (GD's
//     key: the journal's CSV copy). mp_collect was read and kept, moves always refused in
//     multiplayer (made it GD's rule, below). A file from 5 keeps every value; the pad keys are
//     reset only below 5.
// 7 = [display] slot_plates (0 none, 1 auto, 2 thin frames). A
//     file from 5 or 6 keeps every value.
// 8 = [display] grid_cover (1 = the grid cells no slot of the shown window uses are covered
//     in the caravan's ground colour). A file from 5, 6 or 7 keeps every value.
// 9 = [display] tooltip_mark and [advanced] tooltip_text_yes / tooltip_text_no /
//     tooltip_class_yes / tooltip_class_no (GD's keys: the "In your collection" tooltip line), and
//     have_marks now defaults to 0 - a file from before 9 that still reads the old default 1 is
//     flipped to 0 once (UT_HAVE_MARKS_OFF_SINCE), a 0 stays 0.
// 10 = both of the above together. A file from 8 gains the
//     tooltip keys and its have_marks=1 is flipped; a file from 9 gains grid_cover
//     and its have_marks is left as it is (9 was only ever written with the default 0).
// 11 = [display] slot_plates gains 3 = the equipment window's own slot art (the new
//     default). A file from before 11 that still reads the old default 1 is moved to 3 once
//     (UT_SLOT_ART_SINCE); a 0 or a 2 (the player's) stays.
// 12 = [display] owned_marks gains 3 = the uncollected records' icons
//     drawn in gray (the new default). A file that still reads the old default 1 is moved to 3 once
//     (UT_GRAY_ICONS_SINCE); a 0 or a 2 (the player's) stays.
// 12, revised: no key and no value changed. mp_collect's COMMENT
//     now says GD's rule (1 = deposits and takes work in a hosted or joined game). The comments are
//     written only when a file is created or migrated, so a file from 11 still said "ALWAYS refused
//     while you play multiplayer"; the bump rewrites every comment once and keeps every value.
// 13 = both 12 changes together, one bump above either kind of 12. Every
//     file from before 13 gets the new comments; owned_marks' old default 1 is moved to 3 in a
//     file from before 13. A 12 file can be either kind: in the gray-icons kind a 1 is the old
//     default, in the comments kind it may be the player's; the gray icons are the new default,
//     so a 12 is treated as before the gray default.
// 14 = [display] search, search_buttons (GD's key), search_lore; [advanced] search_prebuild,
//     search_debug_query: the property search on the collection page. A file from 13 keeps every
//     value and gains the five keys at their defaults.
// 15 = [display] search_mark: how a record the search finds is highlighted. A file from 14 keeps
//     every value and gains the key at its default.
// 16 = [display] search_mark's default goes from 3 (the frame and the wash) to 1 (a thin frame),
//     and search_mark_color (a colour name or #rrggbb, default gold) colours both. A file from
//     before 16 that still reads the old default 3 is moved to 1 once (UT_SEARCH_FRAME_SINCE); a 1
//     or a 2 (the player's) stays, and every other value is kept.
// 17 = [display] search_transfer: the search field is shown on the real Transfer page too (the
//     view OFF) and marks the real sack's items the query finds. A file from 16 keeps every value
//     and gains the key at its default.
// 18 = [general] collect_unobtainable: 1 keeps the records nobody can obtain (EXCLUDE rule 7) in
//     the catalogue. A file from 17 keeps every value and gains the key at its default 0.
// 19 = [general] collect_mi: 1 (the default) collects the monster infrequents, every Rare
//     equipment record, on their groups' pages after the uniques. A file from 18 keeps every
//     value and gains the key at its default 1.
// 20 = the shorter file: [general] holds the seven keys above and every other key is one line of
//     the [advanced] blob, commented out at its default; [general] enabled (GD's master switch).
//     A file from 19 keeps every value. Each line found under another [block] than its new one is
//     logged with its value, one log line per old and new block, and the [advanced] rows that hold
//     their default are named once more: they come back as commented lines. owned_only is not a
//     setting any more: the OWN filter, with the group and row the collection was left on, lives
//     in uniquetab.state beside the file (a line in an older file seeds it once).
// 21 = the rollover keys leave the file for [tab] of uniquetab-buttons.ini: tooltip_text_yes ->
//     tooltip.yes, tooltip_text_no -> tooltip.no, tooltip_class_yes -> tooltip.class_yes,
//     tooltip_class_no -> tooltip.class_no. A file from 20 keeps every other value; a value of
//     the four that is not its old default is carried ONCE into [tab] of the main game's buttons
//     file (configTakeTooltipCarry; one INFO line); the file keeps its old version and the four
//     keys until then (configTooltipCarryDone). A Grim Dawn file's two class numbers name
//     other styles and are not carried. In a file of this version (or a mod's own file) a line
//     naming one of them is ignored with one WARN that names its new home.
// 22 = [layout]: pad_<W>x<H> and label_<W>x<H> (GD's keyed lines: the pad's and the label's
//     rectangles, x,y,w,h in record px from the caravan window's top-left, for the caravan window
//     of that size - 565x637 in vanilla) and [advanced] layout_outline. pad_x, pad_y, pad_h and
//     pad_gap are RETIRED: a file from before 22 keeps every other value and the four are named
//     with their values in one INFO line (the pad is placed by its rectangle now); in a file of
//     this version (or a mod's own file) a line naming one is ignored with one WARN.
// 23 = [advanced] ui_scale_pct, collect_pristine_only, pad_caption_max and journal_dir (GD's
//     keys, GD's ranges, defaults and comments). A file from 22 keeps every value and every line
//     (the [layout] lines, the screen keys, a commented row stays commented) and gains the four
//     keys commented at their defaults. A mod's own file from 22 takes the main file's values only
//     for the three game keys it did not have: its commented rows, its screen keys and its
//     [layout] lines (or their absence) are its own and stay. journal_dir is the one key a mod's own file does not
//     hold: it is read once at start-up from the main file, a mod's file shows it as a commented
//     line saying so, and a live line there is ignored with one WARN.
// 24 = [advanced] edit_button (GD's key, default 1): an EDIT button on the pad opens the layout
//     editor. A file from 23 keeps every value and every line and gains the key commented at its
//     default. A mod's own file from 23 takes the main file's edit_button (the key it did not
//     have); every other line of it stays its own.
// 25 = the relic and charm pages: [layout] relics_<W>x<H> (their own area, x,y,w,h like grid_;
//     no line = the collection's grid in force) and [advanced] relic_block_gap (empty lines
//     between two blocks, 0..2, default 1) and relic_bonus_gap (empty columns between a row's
//     shard slot and its first bonus, 0..1, default 1). A file from 24 keeps every value and every
//     line and gains the two keys commented at their defaults (and the commented relics_ row);
//     a mod's own file from 24 takes the main file's two values (the keys it did not have) and
//     keeps its own [layout] lines, or their absence.
// 26 = [advanced] relic_panels (default 1): every block of the relic pages stands on its own
//     framed panel. A file from 25 keeps every value and every line and gains the key commented at
//     its default; a mod's own file from 25 takes the main file's value (the key it did not have).
// 27 = [general] collect_relics (default 1): the relic and charm page; 0 = no relic page, the
//     relics and charms stored stay in the journal, on no page. A file from 26 keeps every value
//     and every line and gains the key at its default; a mod's own file from 26 takes the main
//     file's value (the key it did not have).
#define UT_INI_VERSION 27
// The ini_version from which search_mark's default is 1 (the migration moves an old default 3).
#define UT_SEARCH_FRAME_SINCE 16
// The ini_version from which owned_marks' default is 3 (the migration moves an old default 1).
#define UT_GRAY_ICONS_SINCE 13
// The ini_version from which slot_plates' default is 3 (the migration moves an old default 1).
#define UT_SLOT_ART_SINCE 11
// The ini_version from which have_marks' default is 0 (the migration flips an old default 1).
// Every ini_version below 9 (7 and the earlier 8) was written by a build whose default was 1.
#define UT_HAVE_MARKS_OFF_SINCE 9

// One family's lines, in file order. Plain bytes, zero-filled when unused, so UtConfig stays a
// copyable value.
enum {
    kUtCfgFamilySlots = 16,      // lines per family; one more is refused with a WARN
    kUtCfgFamilySuffixCap = 48,  // the suffix, NUL included
    kUtCfgFamilyValueCap = 64    // the value, NUL included
};
struct UtCfgFamilyEntry {
    char suffix[kUtCfgFamilySuffixCap];
    char value[kUtCfgFamilyValueCap];
};
struct UtCfgFamilyStore {
    int count;
    UtCfgFamilyEntry e[kUtCfgFamilySlots];
};

struct UtConfig {
    int iniVersion = 0;

    // (Titan Quest): the keys kUtCfgKeys lists are the settings. Every other member below is a
    // FIXED default until a later version that uses it adds its row back - the same rule as the
    // FIXED BEHAVIOUR block at the end.

    // ---- [general] --------------------------------------------------------------------------
    // 0 = the mod goes quiet: the collection view cannot be switched on (a shown one goes off and
    // the view key is left to the game), no pad, no marks, no tooltip line, no search, and nothing
    // new is collected. Everything already stored stays in the mod's own journal file. It is a
    // switch, not an uninstall: the hooks stay in place and pass every call through. Applied by
    // collapsing the feature switches below (configReload), so an off mod is exactly the
    // configuration "everything off" - no second code path of its own; the view and the view key
    // read it once more (ut_view.cpp, ut_panel.cpp).
    int enabled = 1;
    // 1 = the monster infrequents (every Rare equipment record) are in the catalogue, after the
    // uniques of their group; 0 = they are not, and the ones stored stay in the journal, on no
    // page. A generation input (catalogue.stamp): a change regenerates the catalogue once.
    int collectMi = 1;
    // 1 = the relics and charms are collected on the relic page (uniq-relics.txt, the "relics"
    // group); 0 = no relic page: no relic row, no relic group, a relic or charm is refused as any
    // record outside the collection, and the ones stored stay in the journal, on no page. A
    // generation input (catalogue.stamp): a change regenerates the catalogue once.
    int collectRelics = 1;
    // 1 = the records nobody can obtain (gear worn by statues and quest people, a boss's weapon
    // he never drops, set pieces no loot table names) are in the catalogue too; 0 = they are left
    // out and the start-up log lists them. A generation input (catalogue.stamp): a change
    // regenerates the catalogue once, at the next start.
    int collectUnobtainable = 0;
    // error | warn | info | debug | trace. Parsed and validated here; the logging itself still
    // treats every line the same in this build.
    char logLevel[8] = "info";
    // ui_scale_pct: 0 = follow the UI scale the game reports. Anything above 0 is a percentage
    // (clamped to 50..300 where it is used, utUiScaleOf) and overrides it - for a display where
    // the measured scale is wrong. Read where the mod measures the scale (plateGeometry).
    int uiScalePct = 0;

    // ---- [collection] - what the collection accepts ------------------------------------------
    int maxPerRecord = 1;         // copies of one record the collection keeps; 0 = no limit
    // 1 = a DRAG of a soulbound / untradeable unique is let through:
    // CursorHandlerItemMove::PrimaryReagentActivate refuses those two bytes before the deposit is
    // ever attempted, while the exe's own quick-move site has no such test - so without this the
    // same item goes in on shift-click and never on a drag. The mod clears the two bytes for the
    // duration of that ONE call and restores them unless the deposit succeeded. Barred in a
    // multiplayer session whatever this says; a shift-click still gets the same item in.
    int soulboundCollect = 1;
    // 1 = the collection works in a MULTIPLAYER session exactly as it does in single player, for
    // the local player. Other players need nothing installed and can see nothing. 0 = refuse
    // every deposit while the session is multiplayer, or while the mod cannot READ the mode.
    // (TQ): the view works in a hosted or joined game whatever this says; 1 = deposits and takes
    // work there as in single player, 0 = they are refused. An UNKNOWN session state refuses the
    // view and every move whatever this says (refuse when unsure; GD let 1 through).
    int mpCollect = 1;
    // collect_pristine_only: 1 = refuse an item that is not pristine: any non-empty string in its
    // ItemReplicaInfo other than the base record (a prefix, a suffix, a relic or charm and its
    // completion bonus, in either socket - utPartsPristine). Off, because the stored row carries
    // the whole ItemReplicaInfo and hands it back on the take.
    int collectPristineOnly = 0;
    int collectQuickPass = 1;     // 1 = shift-click on one of our records goes to the normal tab
    // The OWN filter's start state. 1 = show only the records the collection already holds; every
    // other box is parked off-grid, which also hides it from the game's own search. The OWN
    // button toggles it and the mod writes the new value into the game's uniquetab.state.
    int ownedOnly = 0;

    // ---- [display] - what the mod draws, and its controls ------------------------------------
    int groupButtons = 1;         // the category pad over the page, and its clicks
    int pageHotkeys = 1;          // the wheel scroll and Ctrl+PageUp/PageDown group switch
    // The property search (ut_search): 1 = on. 0 = inert - no text is built and the page is
    // never filtered.
    int search = 1;
    // 1 = while a query stands, the group buttons whose group holds a shown match are marked
    // (Grim Dawn's key and look), once that group's text is fully indexed.
    int searchButtons = 1;
    // 1 = the item's lore line (text class 0x0E) is searched too. 0 = names and properties only.
    int searchLore = 0;
    // How a record the query matches is highlighted on its slot: 1 = a thin frame on the slot's
    // outermost pixels, 2 = a wash under the icon, 3 = both. Nothing is ever hidden or dimmed.
    int searchMark = 1;
    // The colour of that frame and wash: a name (gold, green, white, red, orange, cyan, magenta,
    // blue) or #rrggbb (ut_color.h). Anything else is refused at load and gold is used.
    char searchMarkColor[16] = "gold";
    // 1 = the search field is shown on the real Transfer page too (the view OFF), and the items
    // of the real transfer sack the query finds are marked the same way. 0 = the field only on the
    // collection page, and nothing is read from the real sack.
    int searchTransfer = 1;
    // 1 = ALSO mark a group for records you do NOT own. A record that has been shown at any point
    // this session is matched EXACTLY (the same engine predicate over the display prototype it
    // left behind); one that has never been on screen is matched against its catalogue display
    // NAME only, so that half UNDER-reports and never over-reports. Skipped while ownedOnly=1.
    int searchButtonsUnowned = 1;
    // 1 = write the one byte the shared item-box rollover tests before it builds the "Currently
    // Equipped" comparison, on the boxes of a collection group only (never on the vanilla
    // crafting-materials page). No detour and no engine call.
    int comparePopup = 1;
    // 1 = append one line to every item tooltip in the game saying whether that item is already
    // in the collection. Items that are not collectible get no extra line at all. Off = the
    // vanilla tooltip, and so is any failure to hook.
    int tooltipMark = 1;
    int plateLabel = 1;           // the group / owned / row-window line over the page
    // 1 = outline the grid, the pad and the label rectangles in force, with their numbers, so a
    // [layout] key can be set by eye; a refused rectangle is outlined in red with the reason.
    int layoutOutline = 0;
    // 1 = open the in-game layout editor on the next collection group; the editor writes it back
    // to 0 when it opens (a one-shot key, GD's).
    int layoutEdit = 0;
    // 1 = an EDIT button in the pad's row 2, right of the search field, opens the layout editor;
    // 0 = no button (the pad re-lays out at the reload; Ctrl+right-click and layout_edit=1 still
    // open it). GD's edit_button.
    int editButton = 1;
    // The relic and charm pages: empty lines between two blocks (0..2) and empty columns between
    // a row's shard slot and its first bonus slot (0..1).
    int relicBlockGap = 1;
    int relicBonusGap = 1;
    // The relic and charm pages: every block on its own framed panel (1) or none (0).
    int relicPanels = 1;
    char padScreen[48] = "";     // pad_screen: the pad pinned to a screen corner
    char labelScreen[48] = "";   // label_screen: the label pinned to a screen corner

    // ---- [layout] - where the grid, the pad and the label sit, per caravan window size ---------
    // grid_<W>x<H>, pad_<W>x<H>, label_<W>x<H> = x,y,w,h: the player's rectangles for the caravan
    // window of that size (vanilla: 565x637), record px from its top-left. No line = the derived
    // place.
    UtCfgFamilyStore layGrid = {};
    UtCfgFamilyStore layPad = {};
    UtCfgFamilyStore layLabel = {};
    // relics_<W>x<H> = x,y,w,h: the relic and charm pages' own area for that caravan window size,
    // whole cells from its top-left. No line = the collection's grid in force (grid_ or derived).
    UtCfgFamilyStore layRelics = {};
    // how an UNOWNED prototype is marked - 0 = not at all, 1 = GD's dim
    // look (a translucent dark veil over its cells), 2 = a thin frame around its cells. Either
    // mark is hidden while the cursor is on a prototype (its tooltip is open).
    int ownedMarks = 3;
    // 1 = an uncollected record the player already has somewhere (the store set) gets
    // a small corner tick; the primary bright/dim mark is the journal's rows. OFF by
    // default - the tooltip line ("In your collection" / "Not in your collection") says it now.
    int haveMarks = 0;
    // the ground under each slot of the collection page - 0 none,
    // 1 auto (the page-draw plates under the items; thin frames when they did not run), 2 thin
    // frames always (for a page that covers the plates).
    int slotPlates = 3;   // 3 = the equipment-slot art (1 / 2 its fallback)
    // 1 = cover the grid cells below / after the shown window's last slot (caravan ground).
    int gridCover = 1;

    // ---- [files] -----------------------------------------------------------------------------
    // tq-uniq-items*.csv beside each journal for a spreadsheet, one RFC-4180 row per entry:
    // 0 off, 1 the collection, 2 all.
    int exportCsv = 0;
    // journal_dir: the folder the journals (and their CSV copies) are kept in, read ONCE at
    // start-up from the main uniquetab.ini (journalInit); empty = the mod's own folder. A mod's
    // journal goes under <journal_dir>\mods\<leaf>\. Everything else - the log, the settings, the
    // state, the generated set - stays in the mod folder. While a mod's own file is in force this
    // still holds the main file's value (publishLocked).
    char journalDir[260] = {0};

    // ---- [advanced] - tuning. Every one of these is clamped to the range in its ini line ------
    int liveCacheMax = 4096;      // display prototypes kept per HUD; past it boxes re-Load
    // How many records the search sweep checks per game-thread tick. It runs from a needle
    // CHANGE, never per frame, and stops as soon as it has walked the collection once; higher
    // marks the buttons sooner and does more work in one tick.
    int searchSweep = 32;
    int plateCountMs = 1000;      // shortest gap between two reagent-map walks for the counters
    int takeWatchMs = 400;        // how often the take watch may walk the map (game thread)
    // How long the take observer's arm stays valid. The exe runs GetMainPlayer ->
    // IsInventorySpaceAvailable -> GetItemReplicaInfo -> GetItemMaxStackSize -> CreateItem inside
    // one frame, so this is a sanity bound, not a tuning knob.
    int identityWindowMs = 250;
    // TQ: the pad's place, row height and gap are no keys since ini_version 22 (pad_x, pad_y,
    // pad_h and pad_gap were retired: a pad_<W>x<H> rectangle places the pad); the derived pad's
    // numbers are ut_layout.h kUtPadDefY / kUtPadDefH / kUtPadDefGap.
    int plateLabelSize = 13;      // label height in record px (scaled: utPlateLabelPx)
    // pad_caption_max: the largest caption size on the buttons, record px (6..36). The buttons
    // file's size.caption_max and a button's own size win over it (utPadCaptionCeiling).
    int padCaptionMax = 14;
    // TQ: the view toggle key, a Windows virtual-key code (117 = F6); 0 = no hotkey.
    int viewHotkey = 117;
    // TQ: the caravan window's record values (caravanwindow.dbr 565 x 637 at 10,0, X Left,
    // Y Centred; TransferWindow.dbr at y 126). A UI mod that moves the window is followed here.
    int caravanX = 10;
    int caravanY = 0;
    int caravanW = 565;
    int caravanH = 637;
    int pageY = 126;
    // TQ: the pad's caption font, by resource name (Resources\Fonts.arc).
    char fontName[64] = "fonts/albertus mt light.fnt";
    // 1 = the painted cover plate follows the group: a generated per-cell-size plate behind a
    // collection group, the ORIGINAL vanilla plate on group -1. 0 = never touch it.
    int plateSwap = 1;
    // The flat ground a cover plate gets when the caravan is a MOD's: six hex digits RRGGBB,
    // empty = the vanilla ground's own tone, sampled from the vanilla stash cover at generation
    // (06 06 06). A mod's cover carries its own baked box lattice that our frames cannot be
    // aligned to, so no pixel of it is used; only the mod's page is affected, never a vanilla
    // plate. Changing it regenerates the set on the next launch (it is part of catalogue.stamp).
    char plateGround[8] = {0};
    // The two tooltip texts, ASCII, read ONCE at start-up (they become engine-owned strings, so a
    // change applies from the next launch). Empty = the built-in wording.
    // The suffix of resources\Text_<lang>.arc the generated catalogue takes item names from.
    char textLanguage[8] = "EN";
    // A GameTextClass. The engine keeps a map<GameTextClass, style-record-name> and paints each
    // line in its class's style, so choosing a class IS choosing a colour - the mod still never
    // draws a pixel. TQ: 22 = 0x16 ItemSetBonuses, the green (64,255,64) the game uses for set
    // bonuses. 24 = 0x18 ItemRelicNumber, a warm amber (255,191,0). Both are stat-line size (14).
    // 0..57 are the classes this build measured as registered; anything else resolves to an EMPTY
    // style name and the line would lose its colour entirely, so it is refused.
    // 0 = buffer log lines and flush once a second from the worker. 1 = write each line at once,
    // for debugging a crash where the very last lines matter.
    int logFlushEachLine = 0;
    // 1 = the search's text index starts at the first view-ON instead of the first query.
    int searchPrebuild = 0;
    // The query applied to the collection page while the mod has no field to type it into.
    char searchDebugQuery[96] = {0};
    // WRITTEN BY THE MOD, not by the player: the group and row the collection was left on in this
    // game. They live in uniquetab.state (configRenderState), never in a settings file.
    int uniqGroup = -1;           // -1 = the vanilla Crafting Materials layout, else the group
    int uniqRow = 0;              // the first visible row inside that group (virtual scroll)

    // ---- FIXED BEHAVIOUR - these are no longer settings --------------------------------------
    // Nothing parses these, so they always hold the value below; they stay in the struct so that
    // the code reading them is untouched, and so that a future build can make one a setting again
    // by adding one row to the table. The one exception is `enabled=0`, which collapses the
    // display and collection switches (and these) to off - see configReload.
    int dbLoad = 1;               // load the collection pages beside the game database
    int journal = 1;              // the private table may own rows: the collection itself
    int identity = 1;             // an item comes back exactly as it went in
    int identityRequireCallsite = 0;  // the exe call-site RVA is LOGGED, never required
    int takeWatch = 1;            // notice a take the exe does inline and cannot be hooked
    int livePages = 1;            // the page is re-pointed in place, so a switch is immediate
    int uniqPage = -1;            // the built-once page selector the live tab replaced
};

namespace ut {

extern UtConfig g_cfg;
// the file's search value before enabled=0 collapses it; the search key gate is installed from it
extern int g_searchInFile;

// ---- THE TABLE ------------------------------------------------------------------------------
enum UtCfgType {
    kUtCfgBool = 0,   // an int that may only be 0 or 1
    kUtCfgInt,        // an int in [lo, hi]
    kUtCfgStr         // text, at most `cap` - 1 bytes
};

struct UtCfgKey {
    const char* section;   // the [block] it is written under; the parser ignores blocks
    const char* name;      // the key as it appears in the file
    int type;              // UtCfgType
    int def;               // default for kUtCfgBool / kUtCfgInt
    int lo, hi;            // inclusive range; a value outside it is clamped and logged
    const char* defStr;    // default for kUtCfgStr (never null)
    size_t off;            // offsetof(UtConfig, member)
    size_t cap;            // sizeof(member) for kUtCfgStr
    const char* const* choices;  // null-terminated list of legal texts, or null
    const char* comment;   // ONE line, in plain words, written after the value
    int flags;             // kUtCfgHidden
};

enum UtCfgFlags {
    kUtCfgHidden = 1,      // a row of the commented [advanced] blob (live only when not default)
    kUtCfgWhenSet = 2      // a screen key of [layout]: no line until it is set, removed at its default
};

// ---- KEYED FAMILIES (see the top of this file) ----------------------------------------------
struct UtCfgFamily {
    const char* section;        // the [block] its lines are written under
    const char* prefix;         // "pad_"
    size_t off;                 // offsetof(UtConfig, <its UtCfgFamilyStore>)
    bool (*suffixOk)(const char* suffix);
    const char* (*valueWhy)(const char* value);   // null = a good value, else why it is refused
    const char* example;        // "565x637=..." for the comments
    const char* comment;        // ONE line
};
extern const UtCfgFamily kUtCfgFamilies[];
extern const int kUtCfgFamilyCount;
// The family `key` belongs to (its suffix passing that family's own check), or null. `suffix`
// (optional) receives a pointer into `key`.
const UtCfgFamily* configFamilyOf(const char* key, const char** suffix);
// One line of a store by its suffix, or null.
const char* configFamilyGet(const UtCfgFamilyStore& s, const char* suffix);
// Sets (or adds) one line; false when it does not fit.
bool configFamilySet(UtCfgFamilyStore* s, const char* suffix, const char* value);
// The value of a whole family key in `c` - configFamilyValue(g_cfg, "pad_565x637") - or null.
const char* configFamilyValue(const UtConfig& c, const char* key);

struct UtCfgSection {
    const char* name;      // "general"
    const char* blurb;     // one line under the [block] header
};

extern const UtCfgKey kUtCfgKeys[];
extern const int kUtCfgKeyCount;
extern const UtCfgSection kUtCfgSections[];
extern const int kUtCfgSectionCount;

// Renders the whole file - the header, the visible keys under [general], then the [advanced]
// blob (one line per hidden key, commented out at its default) - taking every value from `src`.
// Returns the bytes written (the NUL is not counted), or 0 if it did not fit in `cap`.
size_t configRender(char* out, size_t cap, const UtConfig& src);

// Bytes configRender produces for the DEFAULTS. What a fresh file weighs.
size_t configTemplateBytes();

// ---- THE GAMES (ut_folders.h) -----------------------------------------------------------------
// A mod has a COMPLETE settings file of its own, <mod folder>\mods\<leaf>\uniquetab.ini, written
// once from the main file's values the first time the mod is entered (THE SEED) and independent
// afterwards: while that mod is played only its file is read, except journal_dir (read once at
// start-up from the main file). The main game - and the
// custom-quest save set with no mod name - uses uniquetab.ini. Each game has its uniquetab.state
// (uniq_group, uniq_row, owned_only: where the collection was left), written by the mod.
// Delete a mod's file and the next read writes it again from the main file's values.

// The one key a game's own file cannot carry (journal_dir: read once at start-up from the main
// file, before any game is known; a game's file shows it as a commented line saying so, and a
// live line there is ignored with one WARN), and the three state keys.
bool configMainOnlyKey(const char* key);
// The search key gate is installed once, at start: true when the main file OR any existing
// mods\<game>\uniquetab.ini says search=1 (`games` lists the game files that did). A game file
// switched to 1 later gets the gate at the next start; per game, search=0 leaves it inert.
bool configSearchGateAtStart(char* games, size_t cap);
bool configIsStateKey(const char* key);
// The game's two files beside `mainIni`: mods\<game>\uniquetab.ini and its uniquetab.state, or
// (game "" or not a legal leaf) no ini and the root's uniquetab.state.
void configGameFiles(const wchar_t* mainIni, const char* game, std::wstring* ini,
                     std::wstring* state);
// The generation inputs of one game (text_language, collect_mi, collect_relics,
// collect_unobtainable) for its
// build at start-up, read without touching the settings in force: from mods\<game>\uniquetab.ini
// when that file exists (read as its entry reads it: a file of another ini_version starts from
// the main file's values, then its own lines), else the main file's values. True = the game's
// own file was read.
struct UtGenSettings {
    char textLanguage[8];
    int collectMi;
    int collectRelics;
    int collectUnobtainable;
};
bool configGameGenInputs(const char* game, UtGenSettings* out);
// A mod's own file (`game` = its leaf) and the state file, rendered.
size_t configRenderGame(char* out, size_t cap, const char* game, const UtConfig& src);
size_t configRenderState(char* out, size_t cap, const UtConfig& src);
// THE SWITCH (game thread, at the world load that chooses the save set): the game's files come
// into force - its own file created from the seed when absent, its state read, g_cfg published.
// "" = the main game. True when the game changed.
bool configSetGame(const char* game);
void configActiveGame(char* out, size_t cap);

// The rollover words an older uniquetab.ini set (ini_version 20 and before, see 21 above), for
// [tab] of the main game's buttons file: taken ONCE (true, then false until the next migration).
// "" / -1 = that key held its old default and is not carried.
struct UtTooltipCarry {
    char yes[96];
    char no[96];
    int classYes;
    int classNo;
};
bool configTakeTooltipCarry(UtTooltipCarry* out);
// The carry is in the buttons file (or named in a WARN because that file exists already): the
// main file, left at its old version while the carry was pending, is rewritten now. Nothing to
// do when no carry held it.
void configTooltipCarryDone();
// Counters: every published change of g_cfg, and every game switch.
long configGeneration();
long configGameGeneration();
// Writes keys to the file whose values are in force (the active mod's own file, or the main
// file; search always to the main file; the state keys to the game's uniquetab.state). Atomic.
bool configPersistStr(const char* const* keys, const char* const* values, int n);

// Re-reads the file; writes it with the defaults if it does not exist. A file from another
// ini_version is MERGED into the current layout - kept values, defaulted new keys, dropped
// unknown keys - and rewritten. Returns true if the file was parsed.
bool configReload(const wchar_t* path);

// Rewrites ONE integer key in the file whose values are in force (configPersistStr's routing),
// keeping every other line untouched. Atomic: a temp file next to it is written in full and then
// moved over the original, so a reader never sees a half-written file. The OWN button's
// owned_only goes to the game's uniquetab.state.
bool configPersistInt(const char* key, int value);

// plate_label_size is RECORD px (8..32, the ini clamps it too): the label
// font in DRAWN px is GD's floorf(v x UI scale + 0.5f), never under 6 (a scale that cannot be read
// or is outside 0.3..4 counts as 1). The pad's label row may cut it further (ut_panel.cpp).
inline int utPlateLabelPx(int recordSize, float uiScale) {
    int v = recordSize < 8 ? 8 : (recordSize > 32 ? 32 : recordSize);
    const float s = (uiScale == uiScale && uiScale > 0.3f && uiScale < 4.0f) ? uiScale : 1.0f;
    const float f = (float)v * s + 0.5f;
    v = (int)f;
    if ((float)v > f) --v;
    return v < 6 ? 6 : v;
}

// ui_scale_pct (GD's uiScaleCached, its first rung): 0 = the measured scale as it is; anything
// above 0 is the player's own percentage, clamped to 50..300, and overrides it.
inline float utUiScaleOf(int pct, float measured) {
    if (pct <= 0) return measured;
    const int clamped = pct < 50 ? 50 : (pct > 300 ? 300 : pct);
    return (float)clamped / 100.0f;
}

}  // namespace ut
