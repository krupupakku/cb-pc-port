// =============================================================================
// Crash Bash: Remaster+ — Entry Point Principale PC
// Esegue il codice originale PS1 (SCUS_945.70) su CPU/GTE/GPU emulati e
// mostra il risultato in una finestra OpenGL con input XInput/tastiera.
// =============================================================================

#include <cstdio>
#include <filesystem>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif

#include "core/console.h"
#include "renderer/renderer.h"
#include "input/input_manager.h"
#include "audio/audio_manager.h"
#include "netcode/rollback_manager.h"

namespace fs = std::filesystem;

// Cerca un file risalendo dalle cartelle (permette di lanciare l'exe da build/ con doppio click)
static std::string FindFile(const std::string& rel) {
    fs::path base = fs::current_path();
    for (int i = 0; i < 4; i++) {
        fs::path p = base / rel;
        if (fs::exists(p)) return p.string();
        if (!base.has_parent_path() || base.parent_path() == base) break;
        base = base.parent_path();
    }
    return {};
}

static void Fatal(const std::string& msg) {
    std::printf("[ERRORE] %s\n", msg.c_str());
#ifdef _WIN32
    MessageBoxA(nullptr, msg.c_str(), "Crash Bash Remaster+", MB_OK | MB_ICONERROR);
#endif
}

int main(int argc, char* argv[]) {
    bool verbose = false;
    float aspect = 4.0f / 3.0f;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-v") verbose = true;
        else if (a == "--16x9") aspect = 16.0f / 9.0f;
    }

    std::printf("================================================================\n");
    std::printf("   CRASH BASH: REMASTER+ (PC Port - esecuzione codice originale) \n");
    std::printf("================================================================\n\n");

    std::string exe  = FindFile("original/extracted/disc_files/SCUS_945.70");
    std::string disc = FindFile("original/disc/bash.bin");
    if (exe.empty())  { Fatal("SCUS_945.70 non trovato. Esegui prima cb_extract su original/disc/bash.bin."); return 1; }
    if (disc.empty()) { Fatal("original/disc/bash.bin non trovato."); return 1; }

    if (!cb::renderer::Initialize(1920, 1080, true)) return 1;
    cb::input::Initialize();
    cb::audio::Initialize();
    cb::netcode::Initialize();

    static cb::Console console;   // statico: contiene RAM/VRAM
    console.verbose = verbose;
    if (!console.init(exe, disc)) { Fatal("Inizializzazione console fallita."); return 1; }

    std::printf("\n[Info] Controlli (P1): frecce/WASD, Z=X(croce), X=quadrato, C=cerchio, V=triangolo, Invio=Start, ESC=esci.\n");

#ifdef _WIN32
    LARGE_INTEGER freq, last, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&last);
#endif
    const double frameTime = 1.0 / 59.94;
    uint64_t frames = 0;
    bool halted = false;
    double fpsAcc = 0; int fpsCnt = 0;

    while (cb::renderer::ProcessWindowEvents()) {
        cb::input::PollControllers();
        for (int p = 0; p < 4; p++) {
            const auto& c = cb::input::GetController(p);
            console.set_pad(p, (c.flags & cb::input::CB_CTRL_CONNECTED) != 0 || p == 0, (uint16_t)c.buttonsHeld);
        }

        int consumed = halted ? 0 : console.run_frame();
        if (!halted && consumed == 0) halted = true;

        const auto& g = console.gpu;
        cb::renderer::PresentPsxFrame(g.vram.data(), g.disp_x, g.disp_y, g.hres, g.vres, g.disp_enabled, aspect);
        frames++;

#ifdef _WIN32
        // Pacing a ~59.94 Hz (per 'consumed' frame video)
        double target = frameTime * (consumed > 0 ? consumed : 1);
        for (;;) {
            QueryPerformanceCounter(&now);
            double dt = (double)(now.QuadPart - last.QuadPart) / (double)freq.QuadPart;
            if (dt >= target) { fpsAcc += dt; fpsCnt++; last = now; break; }
            if (target - dt > 0.002) Sleep(1);
        }
        if (fpsAcc >= 1.0) {
            char t[160];
            std::snprintf(t, sizeof t, "Crash Bash: Remaster+ | %.1f FPS%s", fpsCnt / fpsAcc, halted ? " | CPU FERMA" : "");
            cb::renderer::SetWindowTitle(t);
            fpsAcc = 0; fpsCnt = 0;
        }
#endif
    }

    std::printf("\nShutdown dei sottosistemi...\n");
    cb::netcode::Shutdown();
    cb::audio::Shutdown();
    cb::input::Shutdown();
    cb::renderer::Shutdown();
    return 0;
}
