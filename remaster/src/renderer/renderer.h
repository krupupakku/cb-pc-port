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

// Mostra l'area di display della VRAM PS1 (RGB555, 1024x512) nella finestra, mantenendo l'aspect ratio.
// enabled=false => schermo nero. aspect = rapporto larghezza/altezza desiderato (es. 4/3 o 16/9).
void PresentPsxFrame(const uint16_t* vram, int x, int y, int w, int h, bool enabled, float aspect);
void SetWindowTitle(const char* title);

} // namespace cb::renderer
