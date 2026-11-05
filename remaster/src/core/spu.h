#pragma once
// =============================================================================
// Crash Bash: Remaster+ — SPU (Sound Processing Unit) Core
// Emulazione hardware SPU PS1:
// - 512 KB SPU RAM
// - 24 Canali Voci con decoding Sony ADPCM (VAG)
// - Resampling Pitch (0x1000 = 44100 Hz)
// - ADSR envelope generator & Key ON / OFF
// - DMA4 & SPU Data Port FIFO Transfer
// - Mixer Stereo 44100 Hz 16-bit
// =============================================================================

#include <cstdint>
#include <vector>
#include <array>

namespace cb {

struct SpuVoice {
    int16_t  vol_l = 0;
    int16_t  vol_r = 0;
    uint16_t pitch = 0x1000;      // 0x1000 = 44100 Hz (1.0x playback)
    uint32_t start_addr = 0;     // offset in SPU RAM
    uint32_t curr_addr = 0;
    uint32_t loop_addr = 0;

    // ADSR
    uint16_t adsr_lo = 0;
    uint16_t adsr_hi = 0;
    int32_t  env_level = 0;      // 0..0x7FFF
    enum AdsrPhase { PHASE_OFF, PHASE_ATTACK, PHASE_DECAY, PHASE_SUSTAIN, PHASE_RELEASE } phase = PHASE_OFF;

    // Decoding ADPCM buffer (28 campioni a 16-bit)
    int16_t  samples[28] = {};
    int      sample_idx = 28;     // 28 = necessita nuovo blocco
    double   sample_pos = 0.0;    // posizione frazionaria per interpolazione pitch
    int32_t  prev1 = 0, prev2 = 0;
    bool     active = false;
    bool     looping = false;
};

class Spu {
public:
    std::vector<uint8_t> ram;     // 512 KB SPU RAM
    std::array<SpuVoice, 24> voices;

    int16_t  main_vol_l = 0x3FFF;
    int16_t  main_vol_r = 0x3FFF;
    uint32_t transfer_addr = 0;   // Indirizzo SPU RAM corrente per DMA/FIFO
    uint16_t spucnt = 0;
    uint16_t spustat = 0;

    Spu();

    void reset();

    // Lettura/scrittura registri SPU hardware (0x1F801C00..0x1F801DFF)
    uint16_t read(uint32_t addr);
    void     write(uint32_t addr, uint16_t val);

    // Trasferimento DMA canale 4
    void dma_write(const uint8_t* src, uint32_t byte_count);

    // Generazione campioni audio stereo a 44100 Hz
    void render(int16_t* out_stereo, int num_samples);

private:
    void key_on(uint32_t voice_mask);
    void key_off(uint32_t voice_mask);
    void decode_block(SpuVoice& v);
    void update_adsr(SpuVoice& v);
};

} // namespace cb

