#include "core/psyq_shim.h"
#include <cstdio>
#include <cmath>

namespace cb::core {

static EngineConfig g_config;
static uint64_t     g_frameCounter = 0;

void Initialize(const EngineConfig& cfg) {
    g_config = cfg;
    g_frameCounter = 0;
    std::printf("[Core] Motore Crash Bash: Remaster+ inizializzato (%ux%u @ %u FPS, Widescreen=%s, HighPoly=%s)\n",
        g_config.renderWidth,
        g_config.renderHeight,
        g_config.targetFps,
        g_config.widescreen ? "ON" : "OFF",
        g_config.highPolyMode ? "ON" : "OFF");
}

void Shutdown() {
    std::printf("[Core] Shutdown completato dopo %llu frame logici.\n",
        static_cast<unsigned long long>(g_frameCounter));
}

void TickGameLogic() {
    g_frameCounter++;
}

const EngineConfig& GetConfig() {
    return g_config;
}

} // namespace cb::core

