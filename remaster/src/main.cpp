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
#include <timeapi.h>
#endif

#include "core/console.h"
#include "renderer/renderer.h"
#include "renderer/prompt_overlay.h"
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
    float aspect = 0.0f; // 0.0f = usa la modalità dinamica di default (4:3 o 16:9 con F5)
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
    if (aspect > 0.0f) cb::renderer::SetAspectRatio(aspect);

    cb::input::Initialize();
    cb::audio::Initialize();
    cb::netcode::Initialize();

    static cb::Console console;   // statico: contiene RAM/VRAM
    console.verbose = verbose;
    console.on_vibration = cb::input::SetVibration;
    if (!console.init(exe, disc)) { Fatal("Inizializzazione console fallita."); return 1; }

    std::printf("\n[Info] Controlli di gioco (P1): Frecce/WASD, Z=X(croce), X=quadrato, C=cerchio, V=triangolo, Invio=Start, ESC=esci.\n");
    std::printf("[Info] Tasti Grafica: F5=Aspect Ratio (4:3 / 16:9 Widescreen 3D Hack), F6=Filtro GLSL (Sharp 4K / CRT / Smooth), F7=Risoluzione Interna (1x / 2x / 4x), F8=Filtro Texture 3D, F11 o Alt+Invio=Schermo Intero.\n\n");

#ifdef _WIN32
    timeBeginPeriod(1);
    LARGE_INTEGER freq, last, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&last);
#endif
    const double frameTime = 1.0 / 59.94;
    bool halted = false;
    double fpsAcc = 0, cpuAcc = 0; int fpsCnt = 0, videoFrames = 0;

    while (cb::renderer::ProcessWindowEvents()) {
        cb::input::PollControllers();
        for (int p = 0; p < 4; p++) {
            const auto& c = cb::input::GetController(p);
            console.set_pad(p, (c.flags & cb::input::CB_CTRL_CONNECTED) != 0 || p == 0, (uint16_t)c.buttonsHeld,
                            c.lx, c.ly, c.rx, c.ry);
        }

        // Sincronizza Widescreen Hack 3D e Risoluzione Interna con la configurazione del renderer
        console.cpu.widescreen_hack = cb::renderer::IsWidescreenHackEnabled();
        console.gpu.set_scale(cb::renderer::GetInternalResolutionScale());
        console.gpu.bilinear_tex = cb::renderer::IsBilinearTexturesEnabled();

        int consumed = 0;
        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);
        consumed = halted ? 0 : console.run_frame();
        QueryPerformanceCounter(&t1);
        double emuSec = (double)(t1.QuadPart - t0.QuadPart) / (double)freq.QuadPart;
        if (!halted && consumed == 0) halted = true;

        // Rendering e mixdown audio SPU (44100 Hz stereo: 735 campioni per frame video a 60Hz)
        if (consumed > 0) {
            int num_audio_samples = 735 * consumed;
            static std::vector<int16_t> audio_buf;
            audio_buf.resize(num_audio_samples * 2);
            console.spu.render(audio_buf.data(), num_audio_samples);
            cb::audio::SubmitSamples(audio_buf.data(), num_audio_samples);
        }

        console.gpu.flush_hires();   // rasterizzazione HD parallela a bande
        const auto& g = console.gpu;
        cb::renderer::PresentPsxFrame(g.vram.data(), g.disp_x, g.disp_y, g.hres, g.vres, g.disp_enabled, 0.0f,
                                     g.scale > 1 ? g.hires_vram.data() : nullptr, g.scale);

#ifdef _WIN32
        // Frame Pacing nativo a 59.94 Hz (30 fps per la logica di gioco quando consumed == 2)
        double target = frameTime * (consumed > 0 ? consumed : 1);
        for (;;) {
            QueryPerformanceCounter(&now);
            double dt = (double)(now.QuadPart - last.QuadPart) / (double)freq.QuadPart;
            if (dt >= target) {
                fpsAcc += dt;
                fpsCnt++;
                videoFrames += (consumed > 0 ? consumed : 1);
                cpuAcc += emuSec / target;
                last = now;
                break;
            }
            double rem = target - dt;
            if (rem > 0.0025) {
                Sleep(1);
            } else if (rem > 0.0005) {
                Sleep(0);
            }
        }
        if (fpsAcc >= 1.0) {
            char t[256];
            std::snprintf(t, sizeof t, "Crash Bash: Remaster+ | %s | %s | %s | %dx%d | Prompt: %s | video %.1f Hz | gioco %.1f fps | CPU %.0f%%%s%s",
                          cb::renderer::GetAspectRatioName(),
                          cb::renderer::GetInternalResolutionName(),
                          cb::renderer::GetVideoFilterName(),
                          g.hres * g.scale, g.vres * g.scale,
                          cb::ui::GetPromptDeviceName(),
                          videoFrames / fpsAcc, fpsCnt / fpsAcc,
                          100.0 * cpuAcc / (fpsCnt ? fpsCnt : 1),
                          cb::renderer::IsFullscreen() ? " | FS" : "",
                          halted ? " | CPU FERMA" : "");
            cb::renderer::SetWindowTitle(t);
            fpsAcc = 0; fpsCnt = 0; videoFrames = 0; cpuAcc = 0;
        }
#endif
    }

#ifdef _WIN32
    timeEndPeriod(1);
#endif

    std::printf("\nShutdown dei sottosistemi...\n");
    cb::netcode::Shutdown();
    cb::audio::Shutdown();
    cb::input::Shutdown();
    cb::renderer::Shutdown();
    return 0;
}
