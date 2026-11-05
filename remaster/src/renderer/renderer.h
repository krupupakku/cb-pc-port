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

// Controlli Widescreen, Filtri Grafici e Schermo Intero (Fase 2)
void CycleAspectRatio();
float GetCurrentAspectRatio();
const char* GetAspectRatioName();
void SetAspectRatio(float aspect);

void CycleVideoFilter();
const char* GetVideoFilterName();

// Risoluzione interna di rendering (1x, 2x, 4x)
void CycleInternalResolution();
int GetInternalResolutionScale();
const char* GetInternalResolutionName();

// DuckStation-style Widescreen Hack (3D FOV Expansion a 16:9)
bool IsWidescreenHackEnabled();

// Filtro texture sui modelli 3D (Bilineare HD vs Point Retro)
void ToggleBilinearTextures();
bool IsBilinearTexturesEnabled();

void ToggleFullscreen();
bool IsFullscreen();

// Mostra l'area di display della VRAM PS1 o del buffer ad alta risoluzione (hires_rgba) nella finestra.
// enabled=false => schermo nero. aspect <= 0 => usa l'aspect ratio corrente.
void PresentPsxFrame(const uint16_t* vram, int x, int y, int w, int h, bool enabled, float aspect = 0.0f,
                     const uint32_t* hires_rgba = nullptr, int hires_scale = 1);
void SetWindowTitle(const char* title);

} // namespace cb::renderer
