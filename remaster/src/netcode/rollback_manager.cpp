#include "netcode/rollback_manager.h"
#include <cstdio>
#include <cstring>

namespace cb::netcode {

void Initialize() {
    std::printf("[Netcode] Rollback State Serializer pronto (dimensione snapshot: %zu byte).\n",
        sizeof(FrameStateSnapshot));
}

void Shutdown() {
    std::printf("[Netcode] Sottosistema netcode chiuso.\n");
}

void SaveSnapshot(uint32_t frameNumber, FrameStateSnapshot& outSnapshot) {
    outSnapshot.frameNumber   = frameNumber;
    outSnapshot.prngSeed      = 0x12345678;
    outSnapshot.prngIncrement = 1;
    for (int i = 0; i < cb::input::MAX_PLAYERS; ++i) {
        outSnapshot.controllers[i] = cb::input::GetController(i);
    }
}

void LoadSnapshot(const FrameStateSnapshot& snapshot) {
    (void)snapshot;
}

} // namespace cb::netcode

