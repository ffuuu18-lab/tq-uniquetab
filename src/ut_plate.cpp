// ut_plate.cpp - the Transfer page's visibility and screen rect (see ut_plate.h). TQ port of GD
// ut_plate's visibility test only.

#include "ut_plate.h"

#include <windows.h>

#include "hooks.h"
#include "tq_runtime.h"
#include "ut_config.h"

namespace ut {

bool plateTransferVisible() { return hookCaravanOpen() && hookCaravanMode() == 1; }

bool plateGeometry(PlateGeometry* g) {
    TqEngine* e = engine();
    if (!e || !g_tq.EngineGetGraphicsEngine || !g_tq.GfxGetCanvas || !g_tq.CanvasGetWidth ||
        !g_tq.CanvasGetHeight)
        return false;
    float s = 1.0f;
    int w = 0, h = 0;
    utGuardEnter();
    __try {
        if (g_tq.EngineGetUIScale) s = g_tq.EngineGetUIScale(e);
        TqGraphicsEngine* gfx = g_tq.EngineGetGraphicsEngine(e);
        TqCanvas* c = gfx ? g_tq.GfxGetCanvas(gfx) : nullptr;
        if (c) {
            w = g_tq.CanvasGetWidth(c);
            h = g_tq.CanvasGetHeight(c);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        w = 0;
    }
    utGuardLeave();
    if (w <= 0 || h <= 0) return false;
    if (!(s > 0.3f && s < 4.0f)) s = 1.0f;
    g->scale = s;
    g->canvasW = w;
    g->canvasH = h;
    g->winW = (float)g_cfg.caravanW * s;
    g->winH = (float)g_cfg.caravanH * s;
    g->winX = (float)g_cfg.caravanX * s;                               // aligned Left
    g->winY = ((float)h - g->winH) * 0.5f + (float)g_cfg.caravanY * s;  // aligned Centre
    g->pageX = g->winX;
    g->pageY = g->winY + (float)g_cfg.pageY * s;
    g->pageW = g->winW;
    g->pageH = g->winH - (float)g_cfg.pageY * s;
    return true;
}

bool platePointOnPage(const PlateGeometry& g, float x, float y) {
    return x >= g.pageX && x < g.pageX + g.pageW && y >= g.pageY && y < g.pageY + g.pageH;
}

}  // namespace ut
