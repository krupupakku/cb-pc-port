// cb_boot: avvia SCUS_945.70 headless con l'interprete MIPS e logga cosa succede.
// Serve per scoprire quali funzioni Psy-Q vanno intercettate (HLE).
#include "core/mips_cpu.h"
#include "core/gpu.h"
#include "core/spu.h"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#include <cstring>

using namespace cb;

static void dump_bmp(const char* path, const std::vector<uint16_t>& vram, int x0, int y0, int w, int h) {
    FILE* f = std::fopen(path, "wb"); if (!f) return;
    int row = (w * 3 + 3) & ~3; uint32_t size = 54 + row * h;
    uint8_t hd[54] = {'B', 'M'}; std::memcpy(hd + 2, &size, 4); uint32_t off = 54, dib = 40; std::memcpy(hd + 10, &off, 4); std::memcpy(hd + 14, &dib, 4);
    int32_t ww = w, hh = h; std::memcpy(hd + 18, &ww, 4); std::memcpy(hd + 22, &hh, 4); hd[26] = 1; hd[28] = 24;
    std::fwrite(hd, 1, 54, f);
    std::vector<uint8_t> line(row);
    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            uint16_t p = vram[((y0 + y) & 511) * 1024 + ((x0 + x) & 1023)];
            line[x * 3] = ((p >> 10) & 31) << 3; line[x * 3 + 1] = ((p >> 5) & 31) << 3; line[x * 3 + 2] = (p & 31) << 3;
        }
        std::fwrite(line.data(), 1, row, f);
    }
    std::fclose(f);
}

static void dump_wav(const char* path, const std::vector<int16_t>& pcm, int sample_rate) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return;
    uint32_t data_bytes = (uint32_t)(pcm.size() * sizeof(int16_t));
    uint32_t riff_size = 36 + data_bytes;
    uint16_t channels = 2;
    uint16_t bits = 16;
    uint32_t byte_rate = sample_rate * channels * (bits / 8);
    uint16_t block_align = channels * (bits / 8);

    std::fwrite("RIFF", 1, 4, f);
    std::fwrite(&riff_size, 4, 1, f);
    std::fwrite("WAVEfmt ", 1, 8, f);
    uint32_t sub1_size = 16;
    uint16_t pcm_fmt = 1;
    std::fwrite(&sub1_size, 4, 1, f);
    std::fwrite(&pcm_fmt, 2, 1, f);
    std::fwrite(&channels, 2, 1, f);
    std::fwrite(&sample_rate, 4, 1, f);
    std::fwrite(&byte_rate, 4, 1, f);
    std::fwrite(&block_align, 2, 1, f);
    std::fwrite(&bits, 2, 1, f);
    std::fwrite("data", 1, 4, f);
    std::fwrite(&data_bytes, 4, 1, f);
    std::fwrite(pcm.data(), sizeof(int16_t), pcm.size(), f);
    std::fclose(f);
    std::printf("[Audio] Salvato WAV (%zu campioni stereo) in %s\n", pcm.size() / 2, path);
}

int main(int argc, char** argv) {
    const char* exe = argc > 1 ? argv[1] : "original/extracted/disc_files/SCUS_945.70";
    uint64_t maxi = argc > 2 ? std::strtoull(argv[2], nullptr, 0) : 5000000;

    MipsCpu cpu;
    if (!cpu.load_psx_exe(exe)) { std::printf("impossibile caricare %s\n", exe); return 1; }

    std::map<uint32_t, int> io_log;
    // Registri hardware: file di registri generico con qualche comportamento speciale.
    static uint8_t hw[0x2000] = {};
    auto off = [](uint32_t a) { return a - 0x1F801000; };
    static Gpu gpu;
    static Spu spu;
    static uint32_t vframes = 0;
    cpu.io_read = [&](uint32_t a, int size) -> uint32_t {
        if (a < 0x1F801000 || a >= 0x1F803000) { if (io_log[a]++ < 2) std::printf("[io-r?] %08X\n", a); return 0; }
        if (a == 0x1F801120) {
            // Root Counter 2 (SysClock / 8 = 4.2336 MHz).
            // A 59.94 Hz = esattamente 70631 tick per frame video nominale.
            uint64_t ticks = (uint64_t)vframes * 70631ULL + (cpu.instr_count % 70000ULL);
            return (uint32_t)(ticks & 0xFFFF);
        }
        if (a == 0x1F801814) return gpu.gpustat();
        if (a == 0x1F801810) return gpu.gpuread();
        if (a >= 0x1F801C00 && a < 0x1F801E00) {
            uint32_t lo = spu.read(a);
            if (size == 4) return lo | ((uint32_t)spu.read(a + 2) << 16);
            return lo;
        }
        uint32_t v = 0; std::memcpy(&v, hw + off(a), size);
        return v;
    };
    cpu.io_write = [&](uint32_t a, uint32_t v, int size) {
        if (a < 0x1F801000 || a >= 0x1F803000) return;
        if (a == 0x1F801810) { gpu.gp0(v); return; }
        if (a == 0x1F801814) { gpu.gp1(v); return; }
        if (a >= 0x1F801C00 && a < 0x1F801E00) {
            spu.write(a, (uint16_t)v);
            if (size == 4) spu.write(a + 2, (uint16_t)(v >> 16));
            return;
        }
        if (a >= 0x1F801080 && a < 0x1F8010F0 && (a & 0xF) == 8) {   // DMA CHCR
            uint32_t ch = (a - 0x1F801080) >> 4;
            uint32_t madr, bcr; std::memcpy(&madr, hw + off(a - 8), 4); std::memcpy(&bcr, hw + off(a - 4), 4);
            if (v & 0x01000000) {
                uint32_t sync = (v >> 9) & 3;
                uint32_t words = (bcr & 0xFFFF) * (bcr >> 16 ? bcr >> 16 : 1);
                if (ch == 2) {
                    if (sync == 2) gpu.dma_linked_list(cpu.ram.data(), 0x1FFFFC, madr);
                    else if (v & 1) gpu.dma_block(cpu.ram.data(), 0x1FFFFC, madr, words);
                    else {
                        for (uint32_t i = 0; i < words; i++) { uint32_t w = gpu.gpuread(); std::memcpy(&cpu.ram[(madr + i * 4) & 0x1FFFFC], &w, 4); }
                    }
                } else if (ch == 4) {                                  // SPU DMA
                    uint32_t bytes = words * 4;
                    spu.dma_write(cpu.ram.data() + (madr & 0x1FFFFC), bytes);
                } else if (ch == 6) {                                  // OT clear (ClearOTagR)
                    uint32_t n = bcr & 0xFFFF, ad = madr;
                    for (uint32_t i = 0; i < n; i++, ad -= 4) {
                        uint32_t e = (i == n - 1) ? 0x00FFFFFFu : ((ad - 4) & 0xFFFFFF);
                        std::memcpy(&cpu.ram[ad & 0x1FFFFC], &e, 4);
                    }
                }
            }
            v &= ~0x01000000u;   // trasferimento istantaneo
        }
        if (a == 0x1F801070) { uint32_t cur; std::memcpy(&cur, hw + off(a), 4); v = cur & v; } // I_STAT: ack
        std::memcpy(hw + off(a), &v, size);
    };

    // BIOS A0/B0/C0: per ora solo log
    for (uint32_t base : {0xA0u, 0xB0u, 0xC0u}) {
        cpu.hooks[base] = [base](MipsCpu& c) {
            static std::map<uint32_t, int> seen;
            if (seen[base * 256 + (c.r[9] & 0xFF)]++ < 1)
                std::printf("[bios] %02X(%02X) ra=%08X a0=%08X\n", base, c.r[9], c.r[31], c.r[4]);
            // HLE eventi BIOS: OpenEvent=B0(08), CloseEvent=B0(09), WaitEvent=B0(0A), TestEvent=B0(0B), EnableEvent=B0(0C)
            static uint32_t evh = 0xF1000000;
            uint32_t fn = c.r[9] & 0xFF;
            c.r[2] = 0;
            if (base == 0xB0) {
                if (fn == 0x08) c.r[2] = evh++;
                else if (fn == 0x09 || fn == 0x0A || fn == 0x0B || fn == 0x0C) c.r[2] = 1;
            }
            return true;
        };
    }

    // HLE Psy-Q: VSync(mode) @0x800320EC. -1 = leggi contatore frame; 0/n = attendi (qui: avanza di 1)
    static std::vector<int16_t> recorded_audio;
    cpu.hooks[0x800320EC] = [&](MipsCpu& c) {
        int32_t mode = (int32_t)c.r[4];
        if (mode >= 0 && mode != 1) {
            int n = (mode ? mode : 1);
            vframes += n;

            // Tick del sottosistema audio ad ogni frame video
            uint8_t* cb_ptr = c.ram_ptr(0x8006D86C);
            uint32_t sound_callback = 0;
            if (cb_ptr) std::memcpy(&sound_callback, cb_ptr, 4);
            if (sound_callback >= 0x80010000 && sound_callback < 0x80080000) {
                c.call_subroutine(sound_callback);
            }

            // Render audio SPU (735 campioni per frame a 60 Hz)
            int samples = 735 * n;
            size_t prev_sz = recorded_audio.size();
            recorded_audio.resize(prev_sz + samples * 2);
            spu.render(recorded_audio.data() + prev_sz, samples);
        }
        c.r[2] = (mode == 1) ? vframes * 263 : vframes;

        // Iniezione input pad per navigare menu fino alla Warp Room
        uint16_t held = 0;
        // Premiamo Start / Croce nei momenti chiave
        if (vframes >= 60 && vframes < 90) held = 0x0008;         // Start
        else if (vframes >= 120 && vframes < 140) held = 0x4000;  // X (Select Game Type -> Adventure)
        else if (vframes >= 170 && vframes < 190) held = 0x4000;  // X (1 Player)
        else if (vframes >= 220 && vframes < 240) held = 0x4000;  // X (Nuova partita)
        else if (vframes >= 270 && vframes < 290) held = 0x4000;  // X (Scelta personaggio: Crash/Cortex)
        else if (vframes >= 330 && vframes < 360) held = 0x0008;  // Start (Salta filmato intro)
        else if (vframes >= 400 && vframes < 430) held = 0x0008;  // Start (Salta dialogo Aku/Uka)
        else if (vframes >= 500 && vframes < 1000) held = 0x1000;  // D-Pad UP: Cammina verso il portale!
        else if (vframes >= 1020 && vframes < 1040) held = 0x0008; // Start: PAUSA!

        uint8_t* p1 = cpu.ram_ptr(0x80063A90);
        uint8_t* p2 = cpu.ram_ptr(0x80063AC4);
        if (p1) {
            p1[0] = 0x00; p1[1] = 0x80;
            for (int k = 0; k < 4; k++) {
                uint8_t* s = p1 + 2 + k * 8;
                uint16_t raw = (k == 0) ? (uint16_t)~held : 0xFFFF;
                s[0] = 0x00; s[1] = 0x73; // DualShock controller ID (0x73) with analog sticks!
                s[2] = raw & 0xFF; s[3] = raw >> 8;
                s[4] = 128; // LX
                s[5] = (held & 0x1000) ? 0 : 128; // LY: 0 = full UP!
                s[6] = s[7] = 128; // RX, RY
            }
        }
        if (p2) p2[0] = 0xFF;

        return true;
    };

    // ---- HLE libcd (Psy-Q) con lettura diretta da bash.bin (MODE2/2352) ----
    static FILE* disc = std::fopen("original/disc/bash.bin", "rb");
    if (!disc) { std::printf("bash.bin non trovato\n"); return 1; }
    static uint32_t cd_lba = 0;                      // LBA corrente (senza offset 150)
    auto bcd = [](uint32_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); };
    auto unbcd = [](uint8_t v) { return (uint32_t)((v >> 4) * 10 + (v & 15)); };

    cpu.hooks[0x80034AFC] = [&](MipsCpu& c) {
        spu.main_vol_l = 0x3FFF;
        spu.main_vol_r = 0x3FFF;
        c.r[2] = 1;
        return true;
    };          // CdInit
    cpu.hooks[0x800349AC] = [](MipsCpu& c) { c.r[2] = 2; return true; };          // CdSync-like: complete
    cpu.hooks[0x800348A8] = [](MipsCpu& c) { c.r[2] = 0; return true; };          // CdSync(mode,res)
    cpu.hooks[0x80034C4C] = [](MipsCpu& c) { c.r[2] = 0; return true; };          // CdSetDebug-like
    cpu.hooks[0x80034C6C] = [&](MipsCpu& c) {                                      // CdSearchFile
        uint32_t out = c.r[4]; uint32_t name = c.r[5]; std::string n;
        for (uint8_t* p = c.ram_ptr(name); p && *p; ++p) n += (char)*p;
        if (n.find("CRASHBSH.DAT") == std::string::npos) { c.r[2] = 0; return true; }
        uint32_t lba = 236 + 150;
        uint8_t* o = c.ram_ptr(out);
        o[0] = bcd(lba / 75 / 60); o[1] = bcd(lba / 75 % 60); o[2] = bcd(lba % 75); o[3] = 0;
        uint32_t sz = 73220096; std::memcpy(o + 4, &sz, 4);
        std::memset(o + 8, 0, 16); std::memcpy(o + 8, "CRASHBSH.DAT;1", 14);
        c.r[2] = out; return true; };
    cpu.hooks[0x80035AAC] = [&](MipsCpu& c) {                                      // CdPosToInt
        uint8_t* p = c.ram_ptr(c.r[4]);
        c.r[2] = (unbcd(p[0]) * 60 + unbcd(p[1])) * 75 + unbcd(p[2]) - 150; return true; };
    cpu.hooks[0x8003599C] = [&](MipsCpu& c) {                                      // CdIntToPos(lba, pos*)
        uint32_t l = c.r[4] + 150; uint8_t* o = c.ram_ptr(c.r[5]);
        o[0] = bcd(l / 75 / 60); o[1] = bcd(l / 75 % 60); o[2] = bcd(l % 75); o[3] = 0;
        c.r[2] = c.r[5]; return true; };
    cpu.hooks[0x800355DC] = [&](MipsCpu& c) {                                      // CdControl(cmd, param, res)
        if ((c.r[4] & 0xFF) == 2 && c.r[5]) {                                      // CdlSetloc
            uint8_t* p = c.ram_ptr(c.r[5]);
            cd_lba = (unbcd(p[0]) * 60 + unbcd(p[1])) * 75 + unbcd(p[2]) - 150; }
        c.r[2] = 1; return true; };
    cpu.hooks[0x8003470C] = [&](MipsCpu& c) {                                      // CdRead(sectors, buf, mode)
        uint32_t n = c.r[4]; uint32_t dst = c.r[5];
        static std::vector<uint8_t> sec(2352);
        for (uint32_t i = 0; i < n; i++) {
            _fseeki64(disc, (int64_t)(cd_lba + i) * 2352, SEEK_SET);
            if (std::fread(sec.data(), 1, 2352, disc) != 2352) break;
            std::memcpy(c.ram_ptr(dst + i * 2048), sec.data() + 24, 2048);
        }
        std::printf("[cd] read lba=%u n=%u -> %08X\n", cd_lba, n, dst);
        c.r[2] = 1; return true; };

    // ring buffer degli ultimi PC
    std::map<uint32_t, uint64_t> hist;
    int font_poly_count = 0;
    while (!cpu.halted && cpu.instr_count < maxi) {
        hist[cpu.pc]++;
        cpu.step();
    }
    for (uint32_t a = 0; a < 0x200000 - 4; a += 4) {
        uint32_t val = *(const uint32_t*)(cpu.ram.data() + a);
        if (val == 0x800B34B8) {
            std::printf("[PTR TO 'PRESS...'] found at RAM %08X\n", 0x80000000 + a);
        }
    }
    const uint16_t* tbl = (const uint16_t*)(cpu.ram.data() + (0x8005AAB4 & 0x1FFFFF));
    for (int c = 0xF0; c <= 0xFF; c++) {
        std::printf("[GLYPH TABLE] 0x%02X -> 0x%04X (idx %d)\n", c, tbl[c], tbl[c] & 0xFFF);
    }
    uint32_t pFont = *(const uint32_t*)(cpu.ram.data() + (0x8005B63C & 0x1FFFFF));
    if (pFont >= 0x80000000 && pFont < 0x80200000) {
        uint32_t sprite_table = *(const uint32_t*)(cpu.ram.data() + ((pFont + 24) & 0x1FFFFF));
        std::printf("[FONT SPRITE TABLE] pFont=%08X sprite_table=%08X\n", pFont, sprite_table);
        if (sprite_table >= 0x80000000 && sprite_table < 0x80200000) {
            for (int idx : { 98, 100, 101, 102, 103, 104, 105 }) {
                uint32_t s_addr = sprite_table + idx * 56;
                const uint8_t* sp = cpu.ram.data() + (s_addr & 0x1FFFFF);
                uint16_t tp   = *(const uint16_t*)(sp + 36);
                uint16_t clut = *(const uint16_t*)(sp + 38);
                uint8_t u0 = sp[40], v0 = sp[41];
                uint8_t u1 = sp[42], v1 = sp[43];
                uint16_t w_px = *(const uint16_t*)(sp + 8);
                uint16_t h_px = *(const uint16_t*)(sp + 10);
                std::printf("Btn Sprite (idx %3d): w=%d h=%d clut=%04X tp=%04X (tpx=%d tpy=%d depth=%d) u=[%d,%d] v=[%d,%d]\n",
                    idx, w_px, h_px, clut, tp, tp & 15, (tp >> 4) & 1, (tp >> 7) & 3, u0, u1, v0, sp[45]);
            }
            for (char ch : { 'C', 'O', 'N', 'T', 'I', 'U', 'E' }) {
                uint16_t g = tbl[(uint8_t)ch];
                int idx = g & 0xFFF;
                uint32_t s_addr = sprite_table + idx * 56;
                const uint8_t* sp = cpu.ram.data() + (s_addr & 0x1FFFFF);
                uint16_t tp   = *(const uint16_t*)(sp + 36);
                uint16_t clut = *(const uint16_t*)(sp + 38);
                uint8_t u0 = sp[40], v0 = sp[41];
                uint8_t u1 = sp[42], v1 = sp[43];
                uint16_t w_px = *(const uint16_t*)(sp + 8);
                uint16_t h_px = *(const uint16_t*)(sp + 10);
                std::printf("Char '%c' (idx %3d): w=%d h=%d clut=%04X tp=%04X (tpx=%d tpy=%d depth=%d) u=[%d,%d] v=[%d,%d]\n",
                    ch, idx, w_px, h_px, clut, tp, tp & 15, (tp >> 4) & 1, (tp >> 7) & 3, u0, u1, v0, sp[45]);
            }
        }
    }
    uint16_t clut_test = 0x57BF;
    int cx = (clut_test & 63) * 16;
    int cy = (clut_test >> 6) & 511;
    std::printf("Raw CLUT 0x%04X at (%d, %d):\n", clut_test, cx, cy);
    for (int i = 0; i < 16; i++) {
        uint16_t val = gpu.vram[cy * 1024 + cx + i];
        std::printf("  [%2d]: 0x%04X (bit15=%d r=%d g=%d b=%d)\n",
            i, val, (val >> 15) & 1, val & 31, (val >> 5) & 31, (val >> 10) & 31);
    }
    dump_bmp("build/vram_full.bmp", gpu.vram, 0, 0, 1024, 512);
    dump_bmp("build/display.bmp", gpu.vram, gpu.disp_x, gpu.disp_y, gpu.hres, gpu.vres);
    dump_wav("build/audio_test.wav", recorded_audio, 44100);
    std::printf("GPU: %llu primitive, display %d,%d %dx%d en=%d, vframes=%u\n", (unsigned long long)gpu.prim_count, gpu.disp_x, gpu.disp_y, gpu.hres, gpu.vres, gpu.disp_enabled, vframes);
    std::printf("\nstop dopo %llu istruzioni, pc=%08X ra=%08X\n", (unsigned long long)cpu.instr_count, cpu.pc, cpu.r[31]);
    if (cpu.halted) std::printf("HALT: %s\n", cpu.halt_reason.c_str());

    std::printf("PC piu' frequenti:\n");
    for (int i = 0; i < 8 && !hist.empty(); i++) {
        auto best = hist.begin();
        for (auto it = hist.begin(); it != hist.end(); ++it) if (it->second > best->second) best = it;
        std::printf("  %08X x%llu\n", best->first, (unsigned long long)best->second);
        hist.erase(best);
    }
    return 0;
}
