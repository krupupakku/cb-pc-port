// cb_boot: avvia SCUS_945.70 headless con l'interprete MIPS e logga cosa succede.
// Serve per scoprire quali funzioni Psy-Q vanno intercettate (HLE).
#include "core/mips_cpu.h"
#include "core/gpu.h"
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
    cpu.io_read = [&](uint32_t a, int size) -> uint32_t {
        if (a < 0x1F801000 || a >= 0x1F803000) { if (io_log[a]++ < 2) std::printf("[io-r?] %08X\n", a); return 0; }
        if (a == 0x1F801814) return gpu.gpustat();
        if (a == 0x1F801810) return gpu.gpuread();
        if (a == 0x1F801DAE) { uint16_t c; std::memcpy(&c, hw + off(0x1F801DAA), 2); return c & 0x3F; } // SPUSTAT
        uint32_t v = 0; std::memcpy(&v, hw + off(a), size);
        return v;
    };
    cpu.io_write = [&](uint32_t a, uint32_t v, int size) {
        if (a < 0x1F801000 || a >= 0x1F803000) return;
        if (a == 0x1F801810) { gpu.gp0(v); return; }
        if (a == 0x1F801814) { gpu.gp1(v); return; }
        if (a >= 0x1F801080 && a < 0x1F8010F0 && (a & 0xF) == 8) {   // DMA CHCR
            uint32_t ch = (a - 0x1F801080) >> 4;
            uint32_t madr, bcr; std::memcpy(&madr, hw + off(a - 8), 4); std::memcpy(&bcr, hw + off(a - 4), 4);
            if (v & 0x01000000) {
                uint32_t sync = (v >> 9) & 3;
                if (ch == 2) {
                    if (sync == 2) gpu.dma_linked_list(cpu.ram.data(), 0x1FFFFC, madr);
                    else if (v & 1) gpu.dma_block(cpu.ram.data(), 0x1FFFFC, madr, (bcr & 0xFFFF) * (bcr >> 16 ? bcr >> 16 : 1));
                    else { uint32_t words = (bcr & 0xFFFF) * (bcr >> 16 ? bcr >> 16 : 1);
                           for (uint32_t i = 0; i < words; i++) { uint32_t w = gpu.gpuread(); std::memcpy(&cpu.ram[(madr + i * 4) & 0x1FFFFC], &w, 4); } }
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
    static uint32_t vframes = 0;
    cpu.hooks[0x800320EC] = [](MipsCpu& c) {
        int32_t mode = (int32_t)c.r[4];
        if (mode >= 0 && mode != 1) vframes += (mode ? mode : 1);
        c.r[2] = (mode == 1) ? vframes * 263 : vframes;
        return true;
    };

    // ---- HLE libcd (Psy-Q) con lettura diretta da bash.bin (MODE2/2352) ----
    static FILE* disc = std::fopen("original/disc/bash.bin", "rb");
    if (!disc) { std::printf("bash.bin non trovato\n"); return 1; }
    static uint32_t cd_lba = 0;                      // LBA corrente (senza offset 150)
    auto bcd = [](uint32_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); };
    auto unbcd = [](uint8_t v) { return (uint32_t)((v >> 4) * 10 + (v & 15)); };

    cpu.hooks[0x80034AFC] = [](MipsCpu& c) { c.r[2] = 1; return true; };          // CdInit
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
    while (!cpu.halted && cpu.instr_count < maxi) {
        hist[cpu.pc]++;
        cpu.step();
    }
    dump_bmp("build/vram_full.bmp", gpu.vram, 0, 0, 1024, 512);
    dump_bmp("build/display.bmp", gpu.vram, gpu.disp_x, gpu.disp_y, gpu.hres, gpu.vres);
    std::printf("GPU: %llu primitive, display %d,%d %dx%d en=%d\n", (unsigned long long)gpu.prim_count, gpu.disp_x, gpu.disp_y, gpu.hres, gpu.vres, gpu.disp_enabled);
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
