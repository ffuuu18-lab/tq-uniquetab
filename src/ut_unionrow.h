// ut_unionrow.h - the saved row of a page that has just become a button's union, and nothing else.
//
// A start (or a switch to another game) restores uniq_group / uniq_row from uniquetab.state. A page
// built before the buttons file is resolved clamps that row to the saved group's OWN rows; when the
// resolve then puts the group on a button of several groups (one grid per button), the saved row
// comes back - GD's rule - but only while nothing has moved the page since the restore (the wheel,
// a window step, a group click, the OWN toggle): a row the player chose is never overridden, and a
// clamped row written back to uniquetab.state in the meantime does not count as a move.
//
// Pure: no Windows, no engine, no allocation, no globals. ut_live.cpp keeps one UtRowRestore (game
// thread: the input and the resolve both run there); tools\test_viewgate.cpp runs this very header.
#pragma once

namespace ut {

// What the last restore gave, and whether the page moved since.
struct UtRowRestore {
    int group;    // the restored uniq_group (-1 = nothing restored)
    int row;      // the restored uniq_row (the first visible slot row)
    bool moved;   // the page was moved since the restore
};

// A restore of the game's state: the group and the row it gave; nothing has moved yet.
inline void utRowRestoreArm(UtRowRestore& r, int group, int row) {
    r.group = group;
    r.row = row > 0 ? row : 0;
    r.moved = false;
}

// A move of the page by the player (the wheel, a window step, a group click, the OWN toggle).
inline void utRowRestoreMoved(UtRowRestore& r) { r.moved = true; }

// The row the page wants after a resolve of the buttons file. The restored row comes back when the
// wanted group is the restored one, nothing has moved the page since, the row in force is below the
// restored one (an earlier page clamped it to the group's own rows), and the group's page has just
// become a union: `membersNow` (its button's groups now) above one, `membersBefore` (before this
// resolve; 1 when no table existed) below two. Otherwise `row`, unchanged. The page build clamps the
// answer against the union's rows.
inline int utUnionRestoredRow(const UtRowRestore& r, int want, int row, int membersNow, int membersBefore) {
    if (r.moved || r.group < 0 || want != r.group) return row;
    if (row >= r.row) return row;
    if (membersNow < 2 || membersBefore >= 2) return row;
    return r.row;
}

}  // namespace ut
