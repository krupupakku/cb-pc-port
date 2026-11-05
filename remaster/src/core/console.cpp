#include "core/console.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

namespace cb {

static inline uint32_t hwoff(uint32_t a) { return a - 0x1F801000; }
static inline uint8_t  bcd(uint32_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static inline uint32_t unbcd(uint8_t v) { return (uint32_t)((v >> 4) * 10 + (v & 15)); }

bool Console::init(const std::string& exePath, const std::string& discBinPath) {
    if (!cpu.load_psx_exe(exePath)) { std::printf("[console] impossibile caricare %s\n", exePath.c_str()); return false; }
    disc = std::fopen(discBinPath.c_str(), "rb");
    if (!disc) { std::printf("[console] disco non trovato: %s\n", discBinPath.c_str()); return false; }
    setup_hw();
    setup_bios();
    setup_cd();
    return true;
}

void Console::dma_write(uint32_t a, uint32_t v) {
    uint32_t ch = (a - 0x1F801080) >> 4;
    uint32_t madr, bcr;
    std::memcpy(&madr, hw + hwoff(a - 8), 4); std::memcpy(&bcr, hw + hwoff(a - 4), 4);
    if (v & 0x01000000) {
        uint32_t sync = (v >> 9) & 3;
        uint32_t words = (bcr & 0xFFFF) * ((bcr >> 16) ? (bcr >> 16) : 1);
        if (ch == 2) {
            if (sync == 2) gpu.dma_linked_list(cpu.ram.data(), 0x1FFFFC, madr);
            else if (v & 1) gpu.dma_block(cpu.ram.data(), 0x1FFFFC, madr, words);
            else for (uint32_t i = 0; i < words; i++) { uint32_t w = gpu.gpuread(); std::memcpy(&cpu.ram[(madr + i * 4) & 0x1FFFFC], &w, 4); }
        } else if (ch == 4) {   // SPU DMA transfer
            uint32_t bytes = words * 4;
            spu.dma_write(cpu.ram.data() + (madr & 0x1FFFFC), bytes);
        } else if (ch == 6) {   // ClearOTagR
            uint32_t n = bcr & 0xFFFF, ad = madr;
            for (uint32_t i = 0; i < n; i++, ad -= 4) {
                uint32_t e = (i == n - 1) ? 0x00FFFFFFu : ((ad - 4) & 0xFFFFFF);
                std::memcpy(&cpu.ram[ad & 0x1FFFFC], &e, 4);
            }
        }
    }
}

void Console::setup_hw() {
    cpu.io_read = [this](uint32_t a, int size) -> uint32_t {
        if (a < 0x1F801000 || a >= 0x1F803000) return 0;
        if (a == 0x1F801120) {
            // Root Counter 2 (SysClock / 8 = 4.2336 MHz).
            // A 59.94 Hz = esattamente 70631 tick per frame video nominale.
            uint64_t frame_instr = (cpu.instr_count >= frame_start_instr) ? (cpu.instr_count - frame_start_instr) : 0;
            uint64_t ticks = (uint64_t)vframes * 70631ULL + frame_instr;
            return (uint32_t)(ticks & 0xFFFF);
        }
        if (a == 0x1F801814) return gpu.gpustat();
        if (a == 0x1F801810) return gpu.gpuread();
        if (a >= 0x1F801C00 && a < 0x1F801E00) {
            uint32_t lo = spu.read(a);
            if (size == 4) {
                uint32_t hi = spu.read(a + 2);
                return lo | (hi << 16);
            }
            return lo;
        }
        uint32_t v = 0; std::memcpy(&v, hw + hwoff(a), size);
        return v;
    };
    cpu.io_write = [this](uint32_t a, uint32_t v, int size) {
        if (a < 0x1F801000 || a >= 0x1F803000) return;
        if (a == 0x1F801810) { gpu.gp0(v); return; }
        if (a == 0x1F801814) { gpu.gp1(v); return; }
        if (a >= 0x1F801C00 && a < 0x1F801E00) {
            spu.write(a, (uint16_t)v);
            if (size == 4) spu.write(a + 2, (uint16_t)(v >> 16));
            return;
        }
        if (a >= 0x1F801080 && a < 0x1F8010F0 && (a & 0xF) == 8) { dma_write(a, v); v &= ~0x01000000u; }
        if (a == 0x1F801070) { uint32_t cur; std::memcpy(&cur, hw + hwoff(a), 4); v = cur & v; }
        std::memcpy(hw + hwoff(a), &v, size);
    };
}

void Console::setup_bios() {
    for (uint32_t base : {0xA0u, 0xB0u, 0xC0u}) {
        cpu.hooks[base] = [base](MipsCpu& c) {
            // Eventi BIOS: OpenEvent=B0(08), CloseEvent=B0(09), WaitEvent=B0(0A), TestEvent=B0(0B), EnableEvent=B0(0C)
            static uint32_t evh = 0xF1000000;
            uint32_t fn = c.r[9] & 0xFF;
            c.r[2] = 0;
            if (base == 0xB0) {
                if (fn == 0x08) c.r[2] = evh++;
                else if (fn >= 0x09 && fn <= 0x0C) c.r[2] = 1;
            }
            return true;
        };
    }

    // VSync(mode) @0x800320EC: -1 = leggi contatore; 0/n = attendi n frame -> fine frame emulato
    cpu.hooks[0x800320EC] = [this](MipsCpu& c) {
        int32_t mode = (int32_t)c.r[4];
        if (mode == 1) { c.r[2] = vframes * 263; return true; }
        if (mode >= 0) {
            int n = mode ? mode : 1;
            vframes += n; vsync_frames = n; vsync_hit = true;

            // Ticking del sottosistema audio ad ogni frame video (VSync callback PS1)
            uint8_t* cb_ptr = c.ram_ptr(0x8006D86C);
            uint32_t sound_callback = 0;
            if (cb_ptr) std::memcpy(&sound_callback, cb_ptr, 4);
            if (sound_callback >= 0x80010000 && sound_callback < 0x80080000) {
                c.call_subroutine(sound_callback);
            }
        }
        c.r[2] = vframes;
        return true;
    };
}

void Console::setup_cd() {
    cpu.hooks[0x80034AFC] = [this](MipsCpu& c) {
        spu.main_vol_l = 0x3FFF;
        spu.main_vol_r = 0x3FFF;
        c.r[2] = 1;
        return true;
    };   // CdInit
    cpu.hooks[0x800349AC] = [](MipsCpu& c) { c.r[2] = 2; return true; };   // CdSync-like: complete
    cpu.hooks[0x800348A8] = [](MipsCpu& c) { c.r[2] = 0; return true; };   // CdSync(mode,res)
    cpu.hooks[0x80034C4C] = [](MipsCpu& c) { c.r[2] = 0; return true; };
    cpu.hooks[0x80034C6C] = [](MipsCpu& c) {                               // CdSearchFile
        uint32_t out = c.r[4]; std::string n;
        for (uint8_t* p = c.ram_ptr(c.r[5]); p && *p; ++p) n += (char)*p;
        if (n.find("CRASHBSH.DAT") == std::string::npos) { c.r[2] = 0; return true; }
        uint32_t lba = 236 + 150; uint8_t* o = c.ram_ptr(out);
        o[0] = bcd(lba / 75 / 60); o[1] = bcd(lba / 75 % 60); o[2] = bcd(lba % 75); o[3] = 0;
        uint32_t sz = 73220096; std::memcpy(o + 4, &sz, 4);
        std::memset(o + 8, 0, 16); std::memcpy(o + 8, "CRASHBSH.DAT;1", 14);
        c.r[2] = out; return true;
    };
    cpu.hooks[0x80035AAC] = [](MipsCpu& c) {                               // CdPosToInt
        uint8_t* p = c.ram_ptr(c.r[4]);
        c.r[2] = (unbcd(p[0]) * 60 + unbcd(p[1])) * 75 + unbcd(p[2]) - 150; return true;
    };
    cpu.hooks[0x8003599C] = [](MipsCpu& c) {                               // CdIntToPos
        uint32_t l = c.r[4] + 150; uint8_t* o = c.ram_ptr(c.r[5]);
        o[0] = bcd(l / 75 / 60); o[1] = bcd(l / 75 % 60); o[2] = bcd(l % 75); o[3] = 0;
        c.r[2] = c.r[5]; return true;
    };
    cpu.hooks[0x800355DC] = [this](MipsCpu& c) {                           // CdControl
        if ((c.r[4] & 0xFF) == 2 && c.r[5]) {
            uint8_t* p = c.ram_ptr(c.r[5]);
            cd_lba = (unbcd(p[0]) * 60 + unbcd(p[1])) * 75 + unbcd(p[2]) - 150;
        }
        c.r[2] = 1; return true;
    };
    cpu.hooks[0x8003470C] = [this](MipsCpu& c) {                           // CdRead(sectors, buf, mode)
        uint32_t n = c.r[4], dst = c.r[5];
        static std::vector<uint8_t> sec(2352);
        for (uint32_t i = 0; i < n; i++) {
            _fseeki64(disc, (int64_t)(cd_lba + i) * 2352, SEEK_SET);
            if (std::fread(sec.data(), 1, 2352, disc) != 2352) break;
            std::memcpy(c.ram_ptr(dst + i * 2048), sec.data() + 24, 2048);
        }
        if (verbose) std::printf("[cd] read lba=%u n=%u -> %08X\n", cd_lba, n, dst);
        c.r[2] = 1; return true;
    };
}

void Console::set_pad(int slot, bool connected, uint16_t held) {
    pad_connected[slot & 3] = connected; pad_held[slot & 3] = held;
}

// Scrive i buffer pad "grezzi" come farebbe il BIOS: multitap sulla porta 1 (4 giocatori).
// Formato per slot: [status][id][btnLo][btnHi][rx][ry][lx][ly], pulsanti active-low.
void Console::write_pads() {
    uint8_t* p1 = cpu.ram_ptr(0x80063A90);
    uint8_t* p2 = cpu.ram_ptr(0x80063AC4);
    p1[0] = 0x00; p1[1] = 0x80;                       // multitap collegato
    for (int k = 0; k < 4; k++) {
        uint8_t* s = p1 + 2 + k * 8;
        if (!pad_connected[k]) { std::memset(s, 0xFF, 8); continue; }
        uint16_t raw = (uint16_t)~pad_held[k];
        s[0] = 0x00; s[1] = 0x41;                      // pad digitale
        s[2] = raw & 0xFF; s[3] = raw >> 8;
        s[4] = s[5] = s[6] = s[7] = 0x80;
    }
    p2[0] = 0xFF;                                      // porta 2: nessun pad
}

int Console::run_frame(uint64_t max_instr) {
    if (cpu.halted) return 0;
    frame_start_instr = cpu.instr_count;
    write_pads();
    vsync_hit = false;
    uint64_t end = cpu.instr_count + max_instr;
    while (!cpu.halted && !vsync_hit && cpu.instr_count < end) cpu.step();
    if (cpu.halted) { std::printf("[console] HALT: %s\n", cpu.halt_reason.c_str()); return 0; }
    return vsync_hit ? vsync_frames : 1;
}

} // namespace cb

