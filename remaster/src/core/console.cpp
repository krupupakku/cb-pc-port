#include "core/console.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

namespace cb {

static inline uint32_t hwoff(uint32_t a) { return a - 0x1F801000; }
static inline uint8_t  bcd(uint32_t v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static inline uint32_t unbcd(uint8_t v) { return (uint32_t)((v >> 4) * 10 + (v & 15)); }

bool Console::init(const std::string& exePath, const std::string& discBinPath, const std::string& memcardPath) {
    if (!cpu.load_psx_exe(exePath)) { std::printf("[console] impossibile caricare %s\n", exePath.c_str()); return false; }
    disc = std::fopen(discBinPath.c_str(), "rb");
    if (!disc) { std::printf("[console] disco non trovato: %s\n", discBinPath.c_str()); return false; }
    memcard.init(memcardPath);
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
            if (sync == 2) {
                gpu.dma_linked_list(cpu.ram.data(), 0x1FFFFC, madr);
                uint32_t end_madr = 0x00FFFFFFu;
                std::memcpy(hw + hwoff(a - 8), &end_madr, 4);
            }
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

void Console::trigger_bios_event(MipsCpu& c, uint32_t ev_class, uint32_t ev_spec) {
    (void)c;
    for (auto& [id, ev] : bios_events) {
        if (ev.ev_class == ev_class && ev.ev_spec == ev_spec && ev.enabled) {
            ev.triggered = true;
        }
    }
}

void Console::setup_bios() {
    for (uint32_t base : {0xA0u, 0xB0u, 0xC0u}) {
        cpu.hooks[base] = [this, base](MipsCpu& c) {
            static uint32_t evh = 0xF1000000;
            uint32_t fn = c.r[9] & 0xFF;
            c.r[2] = 0;

            if (base == 0xB0) {
                // Eventi BIOS: OpenEvent=B0(08), CloseEvent=B0(09), WaitEvent=B0(0A), TestEvent=B0(0B), EnableEvent=B0(0C)
                if (fn == 0x08) {
                    uint32_t id = evh++;
                    bios_events[id] = { c.r[4], c.r[5], c.r[6], c.r[7], false, false };
                    c.r[2] = id;
                    return true;
                }
                if (fn == 0x09) {
                    bios_events.erase(c.r[4]);
                    c.r[2] = 1;
                    return true;
                }
                if (fn == 0x0A) {
                    auto it = bios_events.find(c.r[4]);
                    if (it != bios_events.end()) it->second.triggered = false;
                    c.r[2] = 1;
                    return true;
                }
                if (fn == 0x0B) {
                    // TestEvent: ritorna 1 per evitare lo stallo nel loop di attesa eventi (es. 0x80037F9C)
                    c.r[2] = 1;
                    return true;
                }
                if (fn == 0x0C) {
                    auto it = bios_events.find(c.r[4]);
                    if (it != bios_events.end()) it->second.enabled = true;
                    c.r[2] = 1;
                    return true;
                }
                if (fn == 0x0D) {
                    auto it = bios_events.find(c.r[4]);
                    if (it != bios_events.end()) it->second.enabled = false;
                    c.r[2] = 1;
                    return true;
                }

                // Emulazione Memory Card (BIOS B0 / libmcrd)
                // Event flags in RAM (SCUS_945.70 @0x80078800..0x8007881C):
                // - SwCARD (0xF4000001): 0x80078800=IOE, 0x80078804=ERROR, 0x80078808=TIMEOUT, 0x8007880C=NEW
                // - HwCARD (0xF0000011): 0x80078810=IOE, 0x80078814=ERROR, 0x80078818=TIMEOUT, 0x8007881C=NEW
                if (fn == 0x4A) { // _card_info(port)
                    uint32_t port = c.r[4] & 0x10;
                    if (port == 0) { // Slot 1: Presente e pronta
                        trigger_bios_event(c, 0xF4000001, 0x0004);
                        c.write(0x80078800, 1, 4);
                        c.write(0x80078808, 0, 4);
                        c.r[2] = 1;
                    } else { // Slot 2: Nessun dispositivo (Timeout)
                        trigger_bios_event(c, 0xF4000001, 0x0100);
                        c.write(0x80078808, 1, 4);
                        c.r[2] = 1;
                    }
                    return true;
                }
                if (fn == 0x4B) { // _card_load(port)
                    uint32_t port = c.r[4] & 0x10;
                    if (port == 0) {
                        memcard.load(0);
                        trigger_bios_event(c, 0xF4000001, 0x0004);
                        c.write(0x80078800, 1, 4);
                        c.write(0x80078810, 1, 4);
                        c.r[2] = 1;
                    } else {
                        trigger_bios_event(c, 0xF4000001, 0x0100);
                        c.write(0x80078808, 1, 4);
                        c.r[2] = 0;
                    }
                    return true;
                }
                if (fn == 0x4C || fn == 0x4D || fn == 0x50 || fn == 0x53) { // _card_auto / _new_card / _card_status
                    c.r[2] = 1;
                    return true;
                }
                if (fn == 0x4E || fn == 0x51) { // B0(0x4E) = _card_write(port, sector, buf)
                    uint32_t port = c.r[4] & 0x10;
                    uint32_t sec = c.r[5];
                    uint8_t* buf = c.ram_ptr(c.r[6]);
                    if (port == 0 && buf && memcard.write_sector(0, sec, buf)) {
                        trigger_bios_event(c, 0xF0000011, 0x0004);
                        trigger_bios_event(c, 0xF4000001, 0x0004);
                        c.write(0x80078810, 1, 4);
                        c.write(0x80078818, 0, 4);
                        c.write(0x80078800, 1, 4);
                        c.r[2] = 1;
                    } else {
                        trigger_bios_event(c, 0xF0000011, 0x0100);
                        c.write(0x80078818, 1, 4);
                        c.r[2] = 0;
                    }
                    return true;
                }
                if (fn == 0x4F || fn == 0x52) { // B0(0x4F) = _card_read(port, sector, buf)
                    uint32_t port = c.r[4] & 0x10;
                    uint32_t sec = c.r[5];
                    uint8_t* buf = c.ram_ptr(c.r[6]);
                    if (port == 0 && buf && memcard.read_sector(0, sec, buf)) {
                        trigger_bios_event(c, 0xF0000011, 0x0004);
                        trigger_bios_event(c, 0xF4000001, 0x0004);
                        c.write(0x80078810, 1, 4);
                        c.write(0x80078818, 0, 4);
                        c.write(0x80078800, 1, 4);
                        c.r[2] = 1;
                    } else {
                        trigger_bios_event(c, 0xF0000011, 0x0100);
                        c.write(0x80078818, 1, 4);
                        c.r[2] = 0;
                    }
                    return true;
                }
                if (fn == 0x56 || fn == 0x57 || fn == 0x5B) {
                    c.r[2] = 1;
                    return true;
                }
            } else if (base == 0xA0) {
                if (fn == 0xAB) { // A0(0xAB) = _card_info
                    uint32_t port = c.r[4] & 0x10;
                    if (port == 0) {
                        trigger_bios_event(c, 0xF4000001, 0x0004);
                        c.write(0x80078800, 1, 4);
                        c.write(0x80078808, 0, 4);
                        c.r[2] = 1;
                    } else {
                        trigger_bios_event(c, 0xF0000011, 0x0100);
                        c.write(0x80078818, 1, 4);
                        c.write(0x80078808, 1, 4);
                        c.r[2] = 1;
                    }
                    return true;
                }
                if (fn == 0xAC) { // A0(0xAC) = _card_load
                    uint32_t port = c.r[4] & 0x10;
                    if (port == 0) {
                        memcard.load(0);
                        trigger_bios_event(c, 0xF4000001, 0x0004);
                        c.write(0x80078800, 1, 4);
                        c.write(0x80078810, 1, 4);
                        c.r[2] = 1;
                    } else {
                        trigger_bios_event(c, 0xF0000011, 0x0100);
                        c.write(0x80078808, 1, 4);
                        c.r[2] = 0;
                    }
                    return true;
                }
                if (fn == 0x39 || fn == 0x49 || fn == 0x70 || fn == 0x71 || fn == 0x72) {
                    c.r[2] = 1;
                    return true;
                }
                if (fn == 0x44) {
                    c.r[2] = 0; // Nessun bad sector
                    return true;
                }
            } else if (base == 0xC0) {
                if (fn == 0x02 || fn == 0x03 || fn == 0x0A) {
                    c.r[2] = 1;
                    return true;
                }
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

            // Ticking dei sottosistemi periodici ad ogni frame video (VSync callback PS1 a 60 Hz):
            // 1. Callback audio registrata a 0x8006D86C
            // 2. Callback driver Memory Card Psy-Q registrata a 0x8003AAA4
            uint8_t* cb_ptr = c.ram_ptr(0x8006D86C);
            uint32_t sound_callback = 0;
            if (cb_ptr) std::memcpy(&sound_callback, cb_ptr, 4);
            if (sound_callback >= 0x80010000 && sound_callback < 0x80080000) {
                for (int i = 0; i < n; i++) {
                    c.call_subroutine(sound_callback);
                }
            }
            c.call_subroutine(0x8003AAA4);
        }
        c.r[2] = vframes;
        return true;
    };

    // Psy-Q Pad Driver Hooks per abilitare DualShock e attuatori vibrazione nativi
    // PadGetState(port) @0x80035EAC: ritorna PadStateStable (6) se connesso, PadStateDiscon (0) se disconnesso
    cpu.hooks[0x80035EAC] = [this](MipsCpu& c) {
        int p = (int)(c.r[4] & 3);
        c.r[2] = pad_connected[p] ? 6 : 0;
        return true;
    };

    // PadInfoAct(port, act, func) @0x800360DC: segnala la presenza degli attuatori
    cpu.hooks[0x800360DC] = [](MipsCpu& c) { c.r[2] = 1; return true; };

    // PadInfoComb(port, type, no) @0x80036100
    cpu.hooks[0x80036100] = [](MipsCpu& c) { c.r[2] = 1; return true; };

    // PadSetActAlign(port, data, len) @0x8003624C
    cpu.hooks[0x8003624C] = [](MipsCpu& c) { c.r[2] = 1; return true; };

    // PadSetAct(port, act, len) @0x800363AC
    cpu.hooks[0x800363AC] = [this](MipsCpu& c) {
        uint32_t port = c.r[4];
        uint32_t act_ptr = c.r[5];
        if (act_ptr && on_vibration) {
            uint8_t* act = c.ram_ptr(act_ptr);
            if (act) {
                int p = (int)(port & 3);
                on_vibration(p, act[0], act[1]);
            }
        }
        c.r[2] = 1;
        return true;
    };

    // =========================================================================
    // Hook HLE Psy-Q libmcrd (SCUS_945.70 @0x80039338..0x8003ABAC)
    // Intercetta le primitive Memory Card di Crash Bash collegandole direttamente
    // all'immagine standard PS1 .mcr (saves/crashbash_slot1.mcr).
    // =========================================================================

    // MemCardInit(val) @0x8003ABAC
    cpu.hooks[0x8003ABAC] = [this](MipsCpu& c) {
        mc_cmd_pending = false;
        mc_pending_delay = 0;
        mc_last_cmd = 0;
        mc_last_rslt = 0;
        c.r[2] = 0;
        return true;
    };

    // MemCardStart() @0x80039338
    cpu.hooks[0x80039338] = [](MipsCpu& c) {
        c.r[2] = 0;
        return true;
    };

    // MemCardStop() @0x800393A8
    cpu.hooks[0x800393A8] = [this](MipsCpu& c) {
        memcard.flush();
        c.r[2] = 0;
        return true;
    };

    // MemCardCallback() @0x8003AAA4 (tick periodico driver libmcrd)
    cpu.hooks[0x8003AAA4] = [](MipsCpu& c) {
        c.r[2] = 0;
        return true;
    };

    // MemCardExist(port) @0x800393E8 — asincrona (McFuncExist = 1)
    cpu.hooks[0x800393E8] = [this](MipsCpu& c) {
        int port = (int)(c.r[4] & 0x10);
        mc_last_cmd = 1; // McFuncExist
        mc_last_rslt = (port == 0) ? 0u : 1u; // 0 = McErrNone, 1 = McErrCardNotExist
        mc_cmd_pending = true;
        mc_pending_delay = 0;
        c.r[2] = 1;
        return true;
    };

    // MemCardAccept(port) @0x8003976C — asincrona (McFuncAccept = 2)
    cpu.hooks[0x8003976C] = [this](MipsCpu& c) {
        int port = (int)(c.r[4] & 0x10);
        mc_last_cmd = 2; // McFuncAccept
        if (port != 0) {
            mc_last_rslt = 1; // McErrCardNotExist
        } else if (!memcard.is_formatted()) {
            mc_last_rslt = 4; // McErrNotFormat
        } else {
            mc_last_rslt = 0; // McErrNone
        }
        mc_cmd_pending = true;
        mc_pending_delay = 0;
        c.r[2] = 1;
        return true;
    };

    // MemCardGetDirentry(port, name, dir, files, ofs, max) @0x8003A554 — sincrona
    cpu.hooks[0x8003A554] = [this](MipsCpu& c) {
        int port = (int)(c.r[4] & 0x10);
        uint32_t name_ptr = c.r[5];
        uint32_t dir_ptr  = c.r[6];
        uint32_t files_ptr = c.r[7];
        int32_t ofs = (int32_t)c.read32(c.r[29] + 16);
        int32_t max_entries = (int32_t)c.read32(c.r[29] + 20);

        std::string pattern;
        if (uint8_t* p = c.ram_ptr(name_ptr)) {
            for (int i = 0; i < 32 && p[i]; ++i) pattern += (char)p[i];
        }

        std::vector<MemoryCard::DirEntry> entries;
        int res = memcard.get_direntries(port, pattern.c_str(), entries);
        if (res != 0) {
            if (files_ptr) c.write(files_ptr, 0, 4);
            c.r[2] = (uint32_t)res;
            return true;
        }

        int total = (int)entries.size();
        int written = 0;
        if (dir_ptr && max_entries > 0 && ofs >= 0) {
            for (int i = ofs; i < total && written < max_entries; ++i, ++written) {
                uint32_t ent_addr = dir_ptr + (uint32_t)(written * 40);
                uint8_t* dst = c.ram_ptr(ent_addr);
                if (!dst) break;
                std::memset(dst, 0, 40);
                std::strncpy(reinterpret_cast<char*>(dst), entries[i].name.c_str(), 20);
                std::memcpy(dst + 20, &entries[i].attr, 4);
                std::memcpy(dst + 24, &entries[i].size, 4);
                int32_t head = entries[i].first_block;
                std::memcpy(dst + 32, &head, 4);
            }
        }
        if (files_ptr) {
            c.write(files_ptr, (uint32_t)written, 4);
        }
        c.r[2] = 0; // McErrNone
        return true;
    };

    // MemCardReadFile(port, file, adr, ofs, bytes) @0x8003A114 — asincrona (McFuncReadFile = 5)
    cpu.hooks[0x8003A114] = [this](MipsCpu& c) {
        int port = (int)(c.r[4] & 0x10);
        uint32_t file_ptr = c.r[5];
        uint32_t adr_ptr  = c.r[6];
        uint32_t ofs      = c.r[7];
        uint32_t bytes    = c.read32(c.r[29] + 16);

        std::string filename;
        if (uint8_t* p = c.ram_ptr(file_ptr)) {
            for (int i = 0; i < 20 && p[i]; ++i) filename += (char)p[i];
        }

        uint8_t* dst = c.ram_ptr(adr_ptr);
        int res = memcard.read_file(port, filename.c_str(), dst, ofs, bytes);
        mc_last_cmd = 5; // McFuncReadFile
        mc_last_rslt = (uint32_t)res;
        mc_cmd_pending = true;
        mc_pending_delay = 0;
        c.r[2] = 1;
        return true;
    };

    // MemCardWriteFile(port, file, adr, ofs, bytes) @0x8003A334 — asincrona (McFuncWriteFile = 6)
    cpu.hooks[0x8003A334] = [this](MipsCpu& c) {
        int port = (int)(c.r[4] & 0x10);
        uint32_t file_ptr = c.r[5];
        uint32_t adr_ptr  = c.r[6];
        uint32_t ofs      = c.r[7];
        uint32_t bytes    = c.read32(c.r[29] + 16);

        std::string filename;
        if (uint8_t* p = c.ram_ptr(file_ptr)) {
            for (int i = 0; i < 20 && p[i]; ++i) filename += (char)p[i];
        }

        const uint8_t* src = c.ram_ptr(adr_ptr);
        int res = memcard.write_file(port, filename.c_str(), src, ofs, bytes);
        mc_last_cmd = 6; // McFuncWriteFile
        mc_last_rslt = (uint32_t)res;
        mc_cmd_pending = true;
        mc_pending_delay = 10; // Breve attesa visiva per mostrare la schermata "SAVING..."
        c.r[2] = 1;
        return true;
    };

    // MemCardCreateFile(port, file, blocks) @0x8003A8E0 — sincrona
    cpu.hooks[0x8003A8E0] = [this](MipsCpu& c) {
        int port = (int)(c.r[4] & 0x10);
        uint32_t file_ptr = c.r[5];
        int blocks = (int)c.r[6];

        std::string filename;
        if (uint8_t* p = c.ram_ptr(file_ptr)) {
            for (int i = 0; i < 20 && p[i]; ++i) filename += (char)p[i];
        }

        int res = memcard.create_file(port, filename.c_str(), blocks);
        c.r[2] = (uint32_t)res;
        return true;
    };

    // MemCardFormat(port) @0x8003A9DC — sincrona
    cpu.hooks[0x8003A9DC] = [this](MipsCpu& c) {
        int port = (int)(c.r[4] & 0x10);
        bool ok = memcard.format(port);
        c.r[2] = ok ? 0u : 2u; // 0 = McErrNone, 2 = McErrCardInvalid
        return true;
    };

    // MemCardSync(mode, cmds, rslt) @0x8003A7C4
    cpu.hooks[0x8003A7C4] = [this](MipsCpu& c) {
        int32_t mode = (int32_t)c.r[4];
        uint32_t cmds_ptr = c.r[5];
        uint32_t rslt_ptr = c.r[6];

        if (mc_cmd_pending) {
            if (mode != 0 && mc_pending_delay > 0) {
                --mc_pending_delay;
                c.r[2] = 0; // 0 = Operazione asincrona in corso
                return true;
            }
            mc_pending_delay = 0;
            mc_cmd_pending = false;
            if (cmds_ptr) c.write(cmds_ptr, mc_last_cmd, 4);
            if (rslt_ptr) c.write(rslt_ptr, mc_last_rslt, 4);
            c.write(0x80076AA8, mc_last_cmd, 4);
            c.write(0x80076AAC, mc_last_rslt, 4);
            c.r[2] = 1; // 1 = Operazione completata
            return true;
        }

        if (cmds_ptr) c.write(cmds_ptr, mc_last_cmd, 4);
        if (rslt_ptr) c.write(rslt_ptr, mc_last_rslt, 4);
        c.r[2] = 0xFFFFFFFFu; // -1 = Nessuna operazione in coda
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

void Console::set_pad(int slot, bool connected, uint16_t held, uint8_t lx, uint8_t ly, uint8_t rx, uint8_t ry) {
    int s = slot & 3;
    pad_connected[s] = connected;
    pad_held[s] = held;
    pad_lx[s] = lx;
    pad_ly[s] = ly;
    pad_rx[s] = rx;
    pad_ry[s] = ry;
}

// Scrive i buffer pad "grezzi" come farebbe il BIOS: multitap sulla porta 1 (4 giocatori).
// Formato per slot: [status][id][btnLo][btnHi][rx][ry][lx][ly], pulsanti active-low.
// ID 0x73 = DualShock Analogico (supporta motori di vibrazione + stick analogici PS1).
void Console::write_pads() {
    uint8_t* p1 = cpu.ram_ptr(0x80063A90);
    uint8_t* p2 = cpu.ram_ptr(0x80063AC4);
    p1[0] = 0x00; p1[1] = 0x80;                       // multitap collegato
    for (int k = 0; k < 4; k++) {
        uint8_t* s = p1 + 2 + k * 8;
        if (!pad_connected[k]) { std::memset(s, 0xFF, 8); continue; }
        uint16_t raw = (uint16_t)~pad_held[k];
        s[0] = 0x00; s[1] = 0x73;                      // pad DualShock analogico
        s[2] = raw & 0xFF; s[3] = raw >> 8;
        s[4] = pad_rx[k];
        s[5] = pad_ry[k];
        s[6] = pad_lx[k];
        s[7] = pad_ly[k];
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
    if (cpu.halted) {
        if (on_vibration) {
            for (int p = 0; p < 4; p++) on_vibration(p, 0, 0);
        }
        std::printf("[console] HALT: %s\n", cpu.halt_reason.c_str());
        return 0;
    }

    // Polling attuatori di vibrazione da strutture controller Crash Bash (_controllers @0x80051338, stride 0x58)
    if (on_vibration) {
        for (int p = 0; p < 4; p++) {
            if (!pad_connected[p]) {
                on_vibration(p, 0, 0);
                continue;
            }
            uint8_t* ctrl = cpu.ram_ptr(0x80051338 + p * 0x58);
            if (ctrl) {
                // Sblocca CONTROLLER_HAS_ANALOG (0x08) per consentire a SetBigMotor / SetSmallMotor di aggiornare i motori
                *(uint32_t*)ctrl |= 0x08;

                // Gli attuatori reali di Crash Bash risiedono a +0x20 e +0x21:
                // Offset +0x20: small motor (0 o 1 / alta frequenza)
                // Offset +0x21: big motor (0..255 / bassa frequenza, intensita urti/esplosioni)
                // Offset +0x04 era invece 'buttonsHeld' (motivo per cui vibrava alla pressione dei tasti!)
                on_vibration(p, ctrl[0x20], ctrl[0x21]);
            }
        }
    }

    return vsync_hit ? vsync_frames : 1;
}

} // namespace cb

