#pragma once
// =============================================================================
// Crash Bash: Remaster+ — Renderer 4K, GTE Float Precision & High-Poly Loader
// Intercetta la OrderingTable (OT_SIZE = 4096) di gpu.h e le chiamate di disegno
// dei modelli (.mdl) per renderizzarli in 4K Widescreen senza vertex wobble.
// =============================================================================

#include <cstdint>

namespace cb::renderer {

struct RenderStats {
    uint32_t polysSubmitted = 0;
    uint32_t highPolySwaps  = 0;
};

bool Initialize(uint32_t width, uint32_t height, bool widescreen);
void Shutdown();

void BeginFrame();
// Processa la Ordering Table PS1 o i modelli High-Poly sostitutivi
void SubmitOrderingTable();
void EndFrame();

} // namespace cb::renderer

