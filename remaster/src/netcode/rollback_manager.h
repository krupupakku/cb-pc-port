#pragma once
// =============================================================================
// Crash Bash: Remaster+ — Rollback Netcode & State Serialization
// Cattura e ripristina lo stato deterministico di un minigioco (PRNG, 4 Player,
// 4 Controller e oggetti arena) per il multiplayer online P2P fino a 4 giocatori.
// =============================================================================

#include <cstdint>
#include "input/input_manager.h"

namespace cb::netcode {

// Snapshot deterministico leggerissimo per il Rollback Netcode
struct FrameStateSnapshot {
    uint32_t frameNumber;
    uint32_t prngSeed;      // Da include/math.h (_prng @ 0x800517b8)
    uint32_t prngIncrement;
    cb::input::BashControllerState controllers[cb::input::MAX_PLAYERS];
    uint8_t  arenaStateBlob[16384]; // Buffer per _playerMetadata e oggetti arena
};

void Initialize();
void Shutdown();

void SaveSnapshot(uint32_t frameNumber, FrameStateSnapshot& outSnapshot);
void LoadSnapshot(const FrameStateSnapshot& snapshot);

} // namespace cb::netcode

