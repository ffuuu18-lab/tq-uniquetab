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
// GEOMETRY UNITS. The numbers in [advanced] that place something on the page (pad_y, pad_h,
// pad_gap, plate_label_size) are RECORD px - the caravan window's own coordinates, which the mod
// multiplies by the UI scale it measures at run time. They are clamped to their safe range when
// they are read here AND clamped again where they are used.
#pragma once

#include <stddef.h>

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
#define UT_INI_VERSION 17
// The ini_version from which search_mark's default is 1 (the migration moves an old default 3).
#define UT_SEARCH_FRAME_SINCE 16
// The ini_version from which owned_marks' default is 3 (the migration moves an old default 1).
#define UT_GRAY_ICONS_SINCE 13
// The ini_version from which slot_plates' default is 3 (the migration moves an old default 1).
#define UT_SLOT_ART_SINCE 11
// The ini_version from which have_marks' default is 0 (the migration flips an old default 1).
// Every ini_version below 9 (7 and the earlier 8) was written by a build whose default was 1.
#define UT_HAVE_MARKS_OFF_SINCE 9

struct UtConfig {
    int iniVersion = 0;

    // (Titan Quest): only log_level, mp_collect and log_flush_each_line are rows of the table
    // (kUtCfgKeys). Every other member below is a FIXED default until a later version that uses
    // it adds its row back - the same rule as the FIXED BEHAVIOUR block at the end.

    // ---- [general] --------------------------------------------------------------------------
    // 0 = the mod goes quiet: no tab, no buttons, no tooltip line, and nothing new is collected.
    // Everything already stored stays in the mod's own journal file, and rescue=1 still hands it
    // back. It is a switch, not an uninstall. Applied by collapsing the feature switches below
    // (configReload), so an off mod is exactly the configuration "everything off" - no second
    // code path of its own.
    int enabled = 1;
    // error | warn | info | debug | trace. Parsed and validated here; the logging itself still
    // treats every line the same in this build.
    char logLevel[8] = "info";
    // 0 = follow the UI scale the game reports. Anything above 0 is a percentage (clamped to
    // 50..300 where it is used) and overrides it - for a display where the measured scale is
    // wrong.
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
    // 1 = refuse an item that is not pristine: any non-empty string in its ItemReplicaInfo other
    // than the base record (prefix, suffix, modifier, transmute, component, augment, ascendant)
    // or a non-zero seed / affix reroll counter. Off, because the stored row carries the whole
    // ItemReplicaInfo and hands it back on the take.
    int collectPristineOnly = 0;
    int collectQuickPass = 1;     // 1 = shift-click on one of our records goes to the normal tab
    // The OWN filter's start state. 1 = show only the records the collection already holds; every
    // other box is parked off-grid, which also hides it from the game's own search. The OWN
    // button toggles it and the mod writes the new value back into the file.
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
    // Where the JOURNAL file lives. Empty = the mod folder (the folder beside the .asi). Only the
    // journal and its exports move - the log, this file and the rescue report always stay in the
    // mod folder, because this file LIVES there and its path is resolved before a key has been
    // read. READ ONCE, at start-up, and never re-read.
    char journalDir[260] = {0};
    // A GD Stash import file beside the journal, rewritten on every journal write.
    // 0 = off (an existing file is left alone), 1 = the collection plus anything not yet checked
    // against the page, 2 = every journal entry, the not-stored history included.
    int exportGds = 1;
    int exportCsv = 0;            // the same three modes, one RFC-4180 row per entry

    // ---- [one-shot] - commands that set themselves back to 0 ---------------------------------
    // 1 (with the caravan window open and a character loaded) = hand EVERY stored item back
    // through the engine's own take path, write the rescue report and set this back to 0. A
    // SECOND trigger does not depend on this file at all: create an empty RESCUE-NOW file beside
    // it. configReload notices it, DELETES it and latches rescue=1 until the run writes 0 back.
    int rescue = 0;
    // 1 = with the caravan open, drop every journal entry the reconciliation has just marked
    // "stored":false (never an unmarked one), after copying the whole file aside and logging
    // every record it drops. The only setting in the mod that deliberately removes entries.
    int journalPrune = 0;

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
    // The button pad is a fixed 9-column x 3-row grid of 34 x padH cells with a padGap gutter and
    // a 3 px side margin, so it is 3 + 9*34 + 8*padGap + 3 = 320 px wide at the default gap and
    // spans record x 102..421 - one px inside the joint limit 422. The RECTANGLE is refused whole
    // when it would leave the band that is flat and opaque on the game's own materials plate AND
    // on all seven of the mod's own plates: record x 102..422, y 25..69.
    // TQ: the pad is a two-row strip INSIDE the caravan frame, in the band below the grid
    // (ut_padlayout.h) - padX record px right of its centred spot, padY below the grid's bottom
    // row, padH per button row (GD 12..20), padGap between cells (GD 0..4). Never over the grid,
    // never outside the frame. (the earlier column and its padW are gone.)
    int padY = 4;
    int padH = 14;
    int padGap = 1;
    int padX = 0;
    int plateLabelSize = 13;      // label height in record px (scaled: utPlateLabelPx)
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
    char tooltipTextYes[96] = {0};
    char tooltipTextNo[96] = {0};
    // The suffix of resources\Text_<lang>.arc the generated catalogue takes item names from.
    char textLanguage[8] = "EN";
    // A GameTextClass. The engine keeps a map<GameTextClass, style-record-name> and paints each
    // line in its class's style, so choosing a class IS choosing a colour - the mod still never
    // draws a pixel. TQ: 22 = 0x16 ItemSetBonuses, the green (64,255,64) the game uses for set
    // bonuses. 24 = 0x18 ItemRelicNumber, a warm amber (255,191,0). Both are stat-line size (14).
    // 0..57 are the classes this build measured as registered; anything else resolves to an EMPTY
    // style name and the line would lose its colour entirely, so it is refused.
    int tooltipClassYes = 0x16;
    int tooltipClassNo = 0x18;
    // 0 = buffer log lines and flush once a second from the worker. 1 = write each line at once,
    // for debugging a crash where the very last lines matter.
    int logFlushEachLine = 0;
    // 1 = the search's text index starts at the first view-ON instead of the first query.
    int searchPrebuild = 0;
    // The query applied to the collection page while the mod has no field to type it into.
    char searchDebugQuery[96] = {0};
    // WRITTEN BY THE MOD, not by the player: the view the tab was last left in. They live here
    // rather than in a file of their own because a setting the user can see and reset by hand is
    // worth more than a tidier split, and a version bump now KEEPS them like any other key.
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
};

struct UtCfgSection {
    const char* name;      // "general"
    const char* blurb;     // one line under the [block] header
};

extern const UtCfgKey kUtCfgKeys[];
extern const int kUtCfgKeyCount;
extern const UtCfgSection kUtCfgSections[];
extern const int kUtCfgSectionCount;

// Renders the whole file - header, sections, one line per key - taking every value from `src`.
// Returns the bytes written (the NUL is not counted), or 0 if it did not fit in `cap`.
size_t configRender(char* out, size_t cap, const UtConfig& src);

// Bytes configRender produces for the DEFAULTS. What a fresh file weighs.
size_t configTemplateBytes();

// Re-reads the file; writes it with the defaults if it does not exist. A file from another
// ini_version is MERGED into the current layout - kept values, defaulted new keys, dropped
// unknown keys - and rewritten. Returns true if the file was parsed.
bool configReload(const wchar_t* path);

// Rewrites ONE integer key in the file, keeping every other line untouched. Atomic: a temp file
// next to it is written in full and then moved over the original, so a reader never sees a
// half-written file. Used by the tab's view keys and by the one-shot commands.
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

}  // namespace ut
