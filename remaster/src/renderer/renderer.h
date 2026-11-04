#pragma once
// =============================================================================
// Crash Bash: Remaster+ — Renderer 4K, GTE Float Precision & Finestra OpenGL
// =============================================================================

#include <cstdint>

namespace cb::renderer {

bool Initialize(uint32_t internalWidth, uint32_t internalHeight, bool widescreen);
void Shutdown();

// Restituisce false quando l'utente chiude la finestra o preme ESC
bool ProcessWindowEvents();

void BeginFrame(uint64_t frameCount);
void SubmitOrderingTable();
void EndFrame();

} // namespace cb::renderer
