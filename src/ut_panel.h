// ut_panel.h - the collection pad and label on the Transfer page. TQ port of GD's ut_panel: the
// flat pad of solid quads with a caption per button (GD's palette verbatim), the selected-group
// ring, the amber lit toggles with their lamps, and the one-line group label - drawn from the
// PresentSurface detour with the canvas exports, and ONE geometry (ut_padlayout.h) shared by the
// draw and the click so the two can never disagree. GD's catalogue/ownership feed, the tooltip and
// the reagent-window draw route are not ported (/ not applicable).
//
// the pad lives INSIDE the caravan frame, GD-style - a compact two-row pad
// in the frame's band below the grid (ut_padlayout.h):
//   row 1  the 15 group buttons (short captions: Helm, Torso, ... Artf)
//   row 2  Collection (the view toggle) | OWN (the owned-only filter) | < | > | the label
//          "<group>  page p / n  owned o / t"
// placed from the LIVE frame: the first proven hover (the page mouse handler's own numbers, via
// ut_owned) gives the parent origin, the frame = that origin - the page's record place, and the
// measured grid must lie inside it (panelNoteHover). Before that measurement only the Collection
// toggle is drawn, small and centred in the records' frame (utPadFallbackToggle); a refused
// frame draws no pad and no label while the view is ON (one WARN). The column beside the
// window is gone - it covered the grid's last column and the character window.
//
// Threads: everything here runs on the game thread (the Present detour, the game window's
// procedure, the GetItemUnderPoint detour). Nothing is allocated, logged or loaded per frame; the
// font is loaded once per world; the frame lines are logged once per window geometry.
#pragma once

#include <windows.h>

#include "ut_padlayout.h"

namespace ut {

// The Present detour, before the original: draws the pad and the label when their gates are open.
void panelDraw();

// The game window's procedure: true = the message was the pad's (consumed; the caller returns 0
// without the original). A click INSIDE a shown button of the pad, the wheel over the page or the
// pad while ON, the view hotkey and PageUp/PageDown while ON. Every action is queued or a plain
// flag (viewRequestToggle, liveSelectGroup, liveToggleOwnedOnly, ...).
bool panelInput(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
// The search field is laid out and the pad was drawn in the last 500 ms (its focus needs both).
bool panelSearchFieldShown();

// World teardown: the font pointer is dropped and loaded again in the next world (GD's
// panelForgetUiFonts rule: a font held across a teardown faults instead of drawing).
void panelForgetFonts();

// the mod's gray\ folder becomes a source of the engine's file system at the
// Present detour BEFORE the first world (the main menu, the catalogue generation done), never
// mid-world beside the engine's loader thread. Once per process; owned_marks=3 only.
void panelGrayPrepare(bool noWorldYet);

// the OS cursor in canvas px, read NOW (at the hover, not at the last
// frame); false = not readable (no window yet, or panelDraw has not measured the client rect).
// The owned grid check compares it with the engine's own hover point (ut_owned).
bool panelCursorNow(float* x, float* y);

// a proven hover's numbers (ut_owned hands them over): the handler's parent origin, the
// grid origin (page pos + parent origin) and the sack's cell size. Decides the LIVE caravan frame
// once per window geometry (canvas size + UI scale): accepted = the pad follows it; refused = one
// WARN and no pad / label while ON.
void panelNoteHover(float originX, float originY, float gridX, float gridY, unsigned cellW,
                    unsigned cellH);
// The accepted live frame for the current geometry (canvas px); false = not measured or refused.
bool panelFrame(UtRectF* frame);
// the grid the accepted frame was measured with (canvas px); false = none.
bool panelFrameGrid(UtRectF* grid);
// (rule): THE drawn cell of the shown page (canvas px) = the measured grid / 16,
// which must be the mod sack's live cell within 1 px (ut_padlayout.h utDrawnCell). false while the
// frame is unmeasured or refused, the mod sack not built, or the two disagree.
bool panelCellPx(float* cell);
// the same check as a verdict: 1 = agreed (*cell set), 0 = no evidence (no
// accepted frame, or the mod sack's cell unreadable), -1 = the frame is accepted and the grid / 16
// is NOT the mod sack's live cell - the rect route and the per-slot overlays (plates, ring, veils,
// grid cover) are refused for that geometry; the pad stays.
int panelCellVerdict(float* cell);
// ... and the grid the per-slot overlays draw on: panelFrameGrid, false on a -1 verdict.
bool panelSlotGrid(UtRectF* grid);
// where the accepted frame came from ("the Transfer page draw's page origin" / "the page
// mouse handler's parent origin"), for the owned marks' fed-grid line. Never null.
const char* panelFrameSourceText();

// "pad=drawn font=ok hover=-1 frame=1"
const char* panelStatus();

// true = the world click is the mod's - the point is on the measured
// caravan frame or on the pad, or a pad button was pressed within 400 ms. Game thread.
// true = a pad click (a button pressed within 400 ms, or the cursor on the pad's ground): the
// page drop (PrimaryTransferActivate) is refused, ON or OFF - the held item stays on the cursor.
bool panelPadClaimsPress();
// the Transfer page draw's PRE-detour - the slot plates under the items (one set per
// frame, only on the page the accessor ran on, only while the view is ON, only on the engine canvas).
void panelPageDrawPre(void* page, void* canvas, const void* origin, int pass);
// after the original - the owned veils, on the LAST page-draw call of a frame once the
// per-frame call pattern is stable (else the Present detour draws them, over everything).
void panelPageDrawPost(void* page, void* canvas, int pass);
// the rect route. hk_PageDraw calls Begin before the page draw's original and End after
// it; the TQ.exe 0x10A9B0 detour calls Pre before the original background draw (the slot rect in,
// a token out, -1 = untouched) and Post after it (the centred footprint in, for the icon).
// the token is ut_viewgate.h's utBgToken - the save index for Post (utBgTokenSave) and
// the skip flag (utBgTokenSkip: do NOT run the original - an uncollected, not hovered prototype).
void panelRectRouteBegin(void* page);
void panelRectRouteEnd();
// hk_PageDraw's __finally when the page draw did not return normally - every
// widget the route touched gets its own rect and icon back at once (the next Begin says it).
void panelRectRouteUnwound();
int panelItemBackgroundPre(void* widget);
void panelItemBackgroundPost(int save);   // utBgTokenSave(token) >= 0

}  // namespace ut
