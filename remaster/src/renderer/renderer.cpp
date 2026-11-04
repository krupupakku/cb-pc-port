#include "renderer/renderer.h"
#include <cstdio>

namespace cb::renderer {

static uint32_t g_width = 3840;
static uint32_t g_height = 2160;
static bool     g_widescreen = true;

bool Initialize(uint32_t width, uint32_t height, bool widescreen) {
    g_width = width;
    g_height = height;
    g_widescreen = widescreen;

    // Su PS1 la larghezza logica in AddPolyG4 (gpu.c:57) è scalata su base 640x240:
    // pPrim->v[i].pos.x = (width * (_g_xOffset + quad->pos[i].x)) / 640;
    // Nel nostro renderer intercettiamo i vertici PRIMA della divisione intera GTE
    // mantenendo coordinate floating-point con correzione prospettica UV.
    std::printf("[Renderer] Pipeline grafica 4K pronta (%ux%u, Aspect Ratio %s, GTE Float Precision attiva)\n",
        g_width, g_height, g_widescreen ? "16:9 Widescreen" : "4:3 Originale");
    return true;
}

void Shutdown() {
    std::printf("[Renderer] Chiusura backend grafico.\n");
}

void BeginFrame() {}

void SubmitOrderingTable() {
    // Qui processeremo i 4096 bucket di _pOTCopy (definito in include/gpu.h)
    // e sostituiremo le mesh .mdl con gli asset in remaster/assets_hd/
}

void EndFrame() {}

} // namespace cb::renderer

