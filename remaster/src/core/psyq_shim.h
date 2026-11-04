#pragma once
// =============================================================================
// Crash Bash: Remaster+ — Core & Sony Psy-Q Shim Layer
// Fornisce le variabili globali del gioco (mappate da symbols/variables.txt)
// e sostituisce le macro assembly MIPS della GTE con matematica float/fixed PC.
// =============================================================================

#include <cstdint>

namespace cb::core {

// Configurazione di esecuzione del motore PC
struct EngineConfig {
    uint32_t renderWidth  = 3840; // Default 4K UHD
    uint32_t renderHeight = 2160;
    uint32_t targetFps    = 60;   // Logica originale a 30Hz + interpolazione a 60+Hz
    bool     widescreen   = true; // Frustum 16:9 anziché 4:3 (320x240)
    bool     highPolyMode = true; // Sostituzione modelli low-poly -> high-poly
};

void Initialize(const EngineConfig& cfg);
void Shutdown();

// Esegue un tick deterministico della logica di gioco (30Hz nativo Crash Bash)
void TickGameLogic();

const EngineConfig& GetConfig();

} // namespace cb::core

