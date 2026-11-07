// Console: la "macchina PS1" virtuale.
// Contiene CPU MIPS + GTE, GPU software, DMA, registri hardware, e gli hook HLE
// del BIOS / libcd (Psy-Q). Il codice del gioco (SCUS_945.70) gira invariato.
#pragma once
#include "core/gpu.h"
#include "core/spu.h"
#include "core/mips_cpu.h"
#include "core/memory_card.h"
#include <cstdint>
#include <map>
#include <string>

namespace cb {

class Console {
public:
    MipsCpu cpu;
    Gpu     gpu;
    Spu     spu;
    MemoryCard memcard;
    bool    verbose = false;
    std::function<void(int slot, uint8_t smallMotor, uint8_t bigMotor)> on_vibration;

    bool init(const std::string& exePath, const std::string& discBinPath);

    // Esegue il gioco fino al prossimo VSync. Ritorna i frame video consumati (0 = errore/halt).
    int run_frame(uint64_t max_instr = 50000000);

    // Stato pad per i 4 slot del multitap (maschera nel formato CB_BTN_*, bit=1 premuto + stick analogici)
    void set_pad(int slot, bool connected, uint16_t held, uint8_t lx = 128, uint8_t ly = 128, uint8_t rx = 128, uint8_t ry = 128);

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
    uint8_t  pad_lx[4] = {128, 128, 128, 128};
    uint8_t  pad_ly[4] = {128, 128, 128, 128};
    uint8_t  pad_rx[4] = {128, 128, 128, 128};
    uint8_t  pad_ry[4] = {128, 128, 128, 128};

    void setup_hw();
    void setup_bios();
    void setup_cd();
    void write_pads();
    void dma_write(uint32_t chcr_addr, uint32_t v);

    struct BiosEvent {
        uint32_t ev_class = 0;
        uint32_t ev_spec  = 0;
        uint32_t ev_mode  = 0;
        uint32_t ev_func  = 0;
        bool     enabled  = false;
        bool     triggered = false;
    };
    std::map<uint32_t, BiosEvent> bios_events;
    void trigger_bios_event(MipsCpu& c, uint32_t ev_class, uint32_t ev_spec);

    // Stato coda comandi asincroni Psy-Q libmcrd
    bool     mc_cmd_pending = false;
    int      mc_pending_delay = 0;
    uint32_t mc_last_cmd = 0;
    uint32_t mc_last_rslt = 0;
};

} // namespace cb

