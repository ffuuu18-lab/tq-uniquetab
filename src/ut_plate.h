// ut_plate.h - where the Transfer page is on screen, and whether it is on screen at all. TQ port of
// the ONE part of GD's ut_plate the port keeps: the visibility test that gates the paint and the
// input (GD: plateMaterialsVisible / plateCursorInWindow). GD's generated plate textures, the
// ReagentWindow::Draw detour and the reagent-map walk do not exist on TQ and are not ported.
//
// The caravan window's rect comes from its record (caravanwindow.dbr: 565 x 637, windowDefaultX 10,
// Y 0, X aligned Left, Y Centred; TransferWindow.dbr at (0,126)) times Engine::GetUIScale, the ini
// carrying the record values so a UI mod that moves the window can be followed. Pure arithmetic
// plus two engine reads; no engine memory is written.
#pragma once

namespace ut {

// The caravan is open AND its Transfer tab is the one on screen (mode 1). The paint gate of the
// toggle; the pad and the marks also need viewOn().
bool plateTransferVisible();

struct PlateGeometry {
    float scale;              // Engine::GetUIScale (1.0 when unreadable)
    int canvasW, canvasH;     // the canvas the pad draws on
    float winX, winY, winW, winH;     // the caravan window, canvas px
    float pageX, pageY, pageW, pageH; // the Transfer page inside it (TransferWindow location)
};

// Fills `g` for this frame. False when the canvas or the scale cannot be read.
bool plateGeometry(PlateGeometry* g);

// Is the canvas point over the Transfer page (the wheel target)?
bool platePointOnPage(const PlateGeometry& g, float x, float y);

}  // namespace ut
