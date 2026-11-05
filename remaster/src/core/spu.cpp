#include "core/spu.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>

namespace cb {

// Coefficienti di predizione Sony ADPCM standard (formato VAG)
static const int f0_table[5] = { 0, 60, 115, 98, 122 };
static const int f1_table[5] = { 0,  0, -52, -55, -60 };

Spu::Spu() {
    ram.resize(512 * 1024, 0);
    reset();
}

void Spu::reset() {
    std::fill(ram.begin(), ram.end(), 0);
    for (auto& v : voices) {
        v = SpuVoice{};
    }
    main_vol_l = 0x3FFF;
    main_vol_r = 0x3FFF;
    transfer_addr = 0;
    spucnt = 0;
    spustat = 0;
}

uint16_t Spu::read(uint32_t addr) {
    uint32_t reg = addr & 0x3FF;

    // Registri voci 0..23 (0x1F801C00..0x1F801D7F)
    if (reg < 0x180) {
        int v_idx = reg / 16;
        int v_reg = reg % 16;
        const auto& v = voices[v_idx];
        switch (v_reg) {
        case 0x0: return (uint16_t)v.vol_l;
        case 0x2: return (uint16_t)v.vol_r;
        case 0x4: return v.pitch;
        case 0x6: return (uint16_t)(v.start_addr >> 3);
        case 0x8: return v.adsr_lo;
        case 0xA: return v.adsr_hi;
        case 0xC: return (uint16_t)v.env_level;
        case 0xE: return (uint16_t)(v.loop_addr >> 3);
        }
        return 0;
    }

    // Registri di controllo (0x1F801D80..0x1F801DFF)
    switch (reg) {
    case 0x180: return (uint16_t)main_vol_l;
    case 0x182: return (uint16_t)main_vol_r;
    case 0x1A6: return (uint16_t)(transfer_addr >> 3);        // 0x1F801DA6: Sound RAM Transfer Address
    case 0x1A8: {                                             // 0x1F801DA8: Sound RAM Transfer FIFO Data
        // Lettura SPU RAM FIFO
        uint16_t val = 0;
        if (transfer_addr < ram.size() - 1) {
            val = (uint16_t)ram[transfer_addr] | ((uint16_t)ram[transfer_addr + 1] << 8);
            transfer_addr = (transfer_addr + 2) & 0x7FFFE;
        }
        return val;
    }
    case 0x1AA: return spucnt;                                // 0x1F801DAA: SPU Control (SPUCNT)
    case 0x1AE: return (spustat & ~0x3F) | (spucnt & 0x3F);    // 0x1F801DAE: SPU Status (SPUSTAT)
    }

    return 0;
}

void Spu::write(uint32_t addr, uint16_t val) {
    uint32_t reg = addr & 0x3FF;

    // Registri voci 0..23 (0x1F801C00..0x1F801D7F)
    if (reg < 0x180) {
        int v_idx = reg / 16;
        int v_reg = reg % 16;
        auto& v = voices[v_idx];
        switch (v_reg) {
        case 0x0: v.vol_l = (int16_t)val; break;
        case 0x2: v.vol_r = (int16_t)val; break;
        case 0x4: v.pitch = val; break;
        case 0x6: v.start_addr = (static_cast<uint32_t>(val) << 3) & 0x7FFFF; break;
        case 0x8: v.adsr_lo = val; break;
        case 0xA: v.adsr_hi = val; break;
        case 0xC: v.env_level = val & 0x7FFF; break;
        case 0xE: v.loop_addr = (static_cast<uint32_t>(val) << 3) & 0x7FFFF; break;
        }
        return;
    }

    // Registri globali
    switch (reg) {
    case 0x180: main_vol_l = (val != 0) ? (int16_t)val : 0x3FFF; break;
    case 0x182: main_vol_r = (val != 0) ? (int16_t)val : 0x3FFF; break;
    case 0x188: key_on(val); break;                        // Key ON voci 0..15
    case 0x18A: key_on(static_cast<uint32_t>(val) << 16); break; // Key ON voci 16..23
    case 0x18C: key_off(val); break;                       // Key OFF voci 0..15
    case 0x18E: key_off(static_cast<uint32_t>(val) << 16); break;// Key OFF voci 16..23
    case 0x1A6: transfer_addr = (static_cast<uint32_t>(val) << 3) & 0x7FFFF; break; // 0x1F801DA6
    case 0x1A8: {                                                                  // 0x1F801DA8
        // Scrittura SPU RAM FIFO
        if (transfer_addr < ram.size() - 1) {
            ram[transfer_addr]     = static_cast<uint8_t>(val & 0xFF);
            ram[transfer_addr + 1] = static_cast<uint8_t>(val >> 8);
            transfer_addr = (transfer_addr + 2) & 0x7FFFE;
        }
        break;
    }
    case 0x1AA:                                                                    // 0x1F801DAA
        spucnt = val;
        spustat = (spustat & ~0x3F) | (spucnt & 0x3F);
        break;
    }
}

void Spu::dma_write(const uint8_t* src, uint32_t byte_count) {
    if (!src || byte_count == 0) return;
    for (uint32_t i = 0; i < byte_count; i++) {
        ram[transfer_addr] = src[i];
        transfer_addr = (transfer_addr + 1) & 0x7FFFF;
    }
    transfer_addr &= ~1; // Allineamento word
}

void Spu::key_on(uint32_t voice_mask) {
    for (int i = 0; i < 24; i++) {
        if (voice_mask & (1u << i)) {
            auto& v = voices[i];
            v.curr_addr = v.start_addr;
            v.loop_addr = v.start_addr;
            v.sample_idx = 28;
            v.sample_pos = 0.0;
            v.prev1 = 0;
            v.prev2 = 0;
            v.env_level = 0;
            v.phase = SpuVoice::PHASE_ATTACK;
            v.active = true;
            v.looping = false;
        }
    }
}

void Spu::key_off(uint32_t voice_mask) {
    for (int i = 0; i < 24; i++) {
        if (voice_mask & (1u << i)) {
            auto& v = voices[i];
            if (v.active) {
                v.phase = SpuVoice::PHASE_RELEASE;
            }
        }
    }
}

void Spu::decode_block(SpuVoice& v) {
    if (v.curr_addr + 16 > ram.size()) {
        v.active = false;
        v.phase = SpuVoice::PHASE_OFF;
        return;
    }

    const uint8_t* block = &ram[v.curr_addr];
    uint8_t shift_filter = block[0];
    uint8_t flags        = block[1];

    int shift  = shift_filter & 0x0F;
    int filter = (shift_filter >> 4) & 0x07;
    if (filter > 4) filter = 4;

    int f0 = f0_table[filter];
    int f1 = f1_table[filter];

    // Decodifica 28 nibble (14 byte)
    int out_idx = 0;
    for (int i = 0; i < 14; i++) {
        uint8_t byte = block[2 + i];
        int nibbles[2] = {
            static_cast<int>(static_cast<int8_t>(byte << 4) >> 4), // Low nibble sign-extended
            static_cast<int>(static_cast<int8_t>(byte & 0xF0) >> 4) // High nibble sign-extended
        };

        for (int n = 0; n < 2; n++) {
            int s = (nibbles[n] << 12) >> shift;
            int sample = s + (v.prev1 * f0 + v.prev2 * f1 + 32) / 64;
            sample = std::clamp(sample, -32768, 32767);
            v.prev2 = v.prev1;
            v.prev1 = sample;
            v.samples[out_idx++] = static_cast<int16_t>(sample);
        }
    }

    // Gestione flag di looping
    if (flags & 0x04) {
        v.loop_addr = v.curr_addr;
        v.looping = true;
    }

    if (flags & 0x01) { // End flag
        if (flags & 0x02) { // Repeat
            v.curr_addr = v.loop_addr;
        } else {
            v.active = false;
            v.phase = SpuVoice::PHASE_OFF;
            return;
        }
    } else {
        v.curr_addr += 16;
    }
}

void Spu::update_adsr(SpuVoice& v) {
    switch (v.phase) {
    case SpuVoice::PHASE_ATTACK:
        v.env_level += 64; // Attack dolce (~11 ms) per evitare pop
        if (v.env_level >= 0x7FFF) {
            v.env_level = 0x7FFF;
            v.phase = SpuVoice::PHASE_DECAY;
        }
        break;
    case SpuVoice::PHASE_DECAY:
        v.env_level -= 8;  // Decay graduale verso il sustain
        if (v.env_level <= 0x7000) {
            v.env_level = 0x7000;
            v.phase = SpuVoice::PHASE_SUSTAIN;
        }
        break;
    case SpuVoice::PHASE_SUSTAIN:
        // Mantieni livello di sustain stabile
        break;
    case SpuVoice::PHASE_RELEASE:
        v.env_level -= 16; // Release dolce (~46 ms) anti-click
        if (v.env_level <= 0) {
            v.env_level = 0;
            v.phase = SpuVoice::PHASE_OFF;
            v.active = false;
        }
        break;
    case SpuVoice::PHASE_OFF:
        v.env_level = 0;
        v.active = false;
        break;
    }
}

void Spu::render(int16_t* out_stereo, int num_samples) {
    if (!out_stereo || num_samples <= 0) return;
    std::fill(out_stereo, out_stereo + num_samples * 2, 0);

    // Buffer di accumulo statico riutilizzato per evitare allocazioni heap a ogni frame
    static std::vector<int32_t> mix_buf;
    if (mix_buf.size() < static_cast<size_t>(num_samples * 2)) {
        mix_buf.resize(num_samples * 2);
    }
    std::fill(mix_buf.begin(), mix_buf.begin() + num_samples * 2, 0);

    for (int v_idx = 0; v_idx < 24; v_idx++) {
        auto& v = voices[v_idx];
        if (!v.active && v.phase == SpuVoice::PHASE_OFF) continue;

        // Frequenza di riproduzione: pitch 0x1000 = 44100 Hz = 1.0 campione per step
        double pitch_step = static_cast<double>(v.pitch ? v.pitch : 0x1000) / 4096.0;

        for (int i = 0; i < num_samples; i++) {
            if (v.sample_idx >= 28) {
                decode_block(v);
                v.sample_idx = 0;
                if (!v.active) break;
            }

            int idx0 = v.sample_idx;
            int idx1 = (idx0 + 1 < 28) ? idx0 + 1 : idx0;
            double frac = v.sample_pos - std::floor(v.sample_pos);

            // Interpolazione lineare tra campioni successivi
            double s = v.samples[idx0] * (1.0 - frac) + v.samples[idx1] * frac;

            // ADSR
            update_adsr(v);
            double env = static_cast<double>(v.env_level) / 32767.0;
            s *= env;

            int32_t smp = static_cast<int32_t>(s);
            mix_buf[i * 2 + 0] += (smp * v.vol_l) >> 14;
            mix_buf[i * 2 + 1] += (smp * v.vol_r) >> 14;

            v.sample_pos += pitch_step;
            while (v.sample_pos >= 1.0) {
                v.sample_pos -= 1.0;
                v.sample_idx++;
                if (v.sample_idx >= 28) {
                    decode_block(v);
                    v.sample_idx = 0;
                    if (!v.active) break;
                }
            }
        }
    }

    // Mixdown finale con Master Volume e saturazione a 16-bit
    for (int i = 0; i < num_samples; i++) {
        int32_t left  = (mix_buf[i * 2 + 0] * main_vol_l) >> 14;
        int32_t right = (mix_buf[i * 2 + 1] * main_vol_r) >> 14;

        out_stereo[i * 2 + 0] = static_cast<int16_t>(std::clamp(left, -32768, 32767));
        out_stereo[i * 2 + 1] = static_cast<int16_t>(std::clamp(right, -32768, 32767));
    }
}

} // namespace cb
