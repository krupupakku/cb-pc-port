// =============================================================================
// Crash Bash: Remaster+ — Entry Point Principale PC
// =============================================================================

#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#endif

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
    if (!cb::renderer::Initialize(config.renderWidth, config.renderHeight, config.widescreen)) {
        return 1;
    }
    cb::input::Initialize();
    cb::audio::Initialize();
    cb::netcode::Initialize();

    std::printf("\n[Info] Finestra avviata! Usa un controller XInput oppure WASD/Frecce + Z/X/C/V.\n");
    std::printf("[Info] Premi ESC o chiudi la finestra per terminare.\n");

    uint64_t frameCount = 0;
    while (cb::renderer::ProcessWindowEvents()) {
        cb::input::PollControllers();

        // Tick deterministico della logica
        cb::core::TickGameLogic();

        // Rendering 60 FPS
        cb::renderer::BeginFrame(frameCount);
        cb::renderer::SubmitOrderingTable();
        cb::renderer::EndFrame();

        frameCount++;
#ifdef _WIN32
        Sleep(16); // ~60 FPS pacing
#endif
    }

    std::printf("\nShutdown dei sottosistemi...\n");
    cb::netcode::Shutdown();
    cb::audio::Shutdown();
    cb::input::Shutdown();
    cb::renderer::Shutdown();
    cb::core::Shutdown();

    return 0;
}
