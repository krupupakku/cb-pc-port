// =============================================================================
// Crash Bash: Remaster+ — Entry Point Principale PC
// =============================================================================

#include <cstdio>
#include "core/psyq_shim.h"
#include "renderer/renderer.h"
#include "input/input_manager.h"
#include "audio/audio_manager.h"
#include "netcode/rollback_manager.h"

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    std::printf("================================================================\n");
    std::printf("   CRASH BASH: REMASTER+ (PC Recompilation & High-Poly Engine)  \n");
    std::printf("================================================================\n\n");

    cb::core::EngineConfig config{};
    config.renderWidth  = 3840;
    config.renderHeight = 2160;
    config.targetFps    = 60;
    config.widescreen   = true;
    config.highPolyMode = true;

    cb::core::Initialize(config);
    cb::renderer::Initialize(config.renderWidth, config.renderHeight, config.widescreen);
    cb::input::Initialize();
    cb::audio::Initialize();
    cb::netcode::Initialize();

    // Verifica stato dei 4 controller (XInput / SteamInput)
    std::printf("\n--- Stato Controller Rilevati (Multitap 4P Virtuale) ---\n");
    for (int p = 0; p < cb::input::MAX_PLAYERS; ++p) {
        const auto& ctrl = cb::input::GetController(p);
        bool connected = (ctrl.flags & cb::input::CB_CTRL_CONNECTED) != 0;
        std::printf("  Giocatore %d: %s (flags=0x%X)\n",
            p + 1,
            connected ? "CONNESSO" : "Non connesso (Slot disponibile per Bot/Remoto)",
            ctrl.flags);
    }

    // Esecuzione di un ciclo di prova (Tick deterministico + Snapshot Rollback + Render)
    std::printf("\n--- Test Pipeline Motore ---\n");
    cb::input::PollControllers();
    cb::core::TickGameLogic();

    cb::netcode::FrameStateSnapshot snap{};
    cb::netcode::SaveSnapshot(1, snap);
    std::printf("[Test] Snapshot frame #1 salvato con successo per Rollback Netcode.\n");

    cb::renderer::BeginFrame();
    cb::renderer::SubmitOrderingTable();
    cb::renderer::EndFrame();

    std::printf("\nShutdown dei sottosistemi...\n");
    cb::netcode::Shutdown();
    cb::audio::Shutdown();
    cb::input::Shutdown();
    cb::renderer::Shutdown();
    cb::core::Shutdown();

    return 0;
}

