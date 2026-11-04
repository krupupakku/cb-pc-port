#include "audio/audio_manager.h"
#include <cstdio>

namespace cb::audio {

void Initialize() {
    std::printf("[Audio] Sottosistema SPU / Audio PC inizializzato.\n");
}

void Shutdown() {
    std::printf("[Audio] Sottosistema audio chiuso.\n");
}

} // namespace cb::audio

