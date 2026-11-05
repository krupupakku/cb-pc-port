// Console: la "macchina PS1" virtuale.
// Contiene CPU MIPS + GTE, GPU software, DMA, registri hardware, e gli hook HLE
// del BIOS / libcd (Psy-Q). Il codice del gioco (SCUS_945.70) gira invariato.
#pragma once
#include "core/gpu.h"
#include "core/spu.h"
#include "core/mips_cpu.h"
#include <cstdint>
#include <string>

namespace cb {

class Console {
public:
    MipsCpu cpu;
    Gpu     gpu;
    Spu     spu;
    bool    verbose = false;

    bool init(const std::string& exePath, const std::string& discBinPath);

    // Esegue il gioco fino al prossimo VSync. Ritorna i frame video consumati (0 = errore/halt).
    int run_frame(uint64_t max_instr = 50000000);

    // Stato pad per i 4 slot del multitap (maschera nel formato CB_BTN_*, bit=1 premuto)
    void set_pad(int slot, bool connected, uint16_t held);

private:
    uint8_t hw[0x2000] = {};
    FILE*   disc = nullptr;
    uint32_t cd_lba = 0;
    uint32_t vframes = 0;
    uint64_t frame_start_instr = 0;
    bool     vsync_hit = false;
    int      vsync_frames = 1;
    bool     pad_connected[4] = {};
    uint16_t pad_held[4] = {};

    void setup_hw();
    void setup_bios();
    void setup_cd();
    void write_pads();
    void dma_write(uint32_t chcr_addr, uint32_t v);
};

} // namespace cb

