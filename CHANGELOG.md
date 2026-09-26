# Changelog - Unique Collection Tab for Titan Quest

What changed in each release, newest first. `README.md` says how everything works; this file says
when it arrived.

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
