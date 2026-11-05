// MIPS R3000A interpreter core per il port di Crash Bash.
// Esegue il vero codice di SCUS_945.70; le funzioni Psy-Q (CD, GPU, Pad, SPU...)
// vengono intercettate per indirizzo tramite hook HLE.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace cb {

struct MipsCpu;

// Hook HLE: ritorna true se ha gestito la funzione (la CPU esegue poi un "jr ra").
using HleHook = std::function<bool(MipsCpu&)>;

struct MipsCpu {
    uint32_t r[32] = {};
    uint32_t pc = 0, next_pc = 4;
    uint32_t hi = 0, lo = 0;
    uint32_t cop0[32] = {};
    int32_t  gte_d[32] = {};   // dati GTE (COP2 data)
    int32_t  gte_c[32] = {};   // controllo GTE
    uint64_t instr_count = 0;
    bool     halted = false;
    std::string halt_reason;
    bool     widescreen_hack = false; // DuckStation-style 3D Widescreen FOV Expansion (16:9)

    std::vector<uint8_t> ram;  // 2 MB
    std::unordered_map<uint32_t, HleHook> hooks;

    // I/O hardware (0x1F80xxxx): callback opzionali
    std::function<uint32_t(uint32_t addr, int size)> io_read;
    std::function<void(uint32_t addr, uint32_t val, int size)> io_write;

    MipsCpu();

    bool load_psx_exe(const std::string& path);
    void reset_to(uint32_t entry, uint32_t sp, uint32_t gp);

    uint32_t read(uint32_t vaddr, int size);
    void     write(uint32_t vaddr, uint32_t val, int size);
    uint8_t* ram_ptr(uint32_t vaddr);          // nullptr se fuori RAM
    uint32_t read32(uint32_t a) { return read(a, 4); }

    void step();
    void run(uint64_t max_instr);
    void call_subroutine(uint32_t target_addr, uint32_t a0 = 0);
    void halt(const std::string& why) { halted = true; halt_reason = why; }

    // COP2 (GTE): implementato in gte.cpp
    void gte_op(uint32_t instr);
    uint32_t gte_read(int n);
    void     gte_write(int n, uint32_t v);
    uint32_t gte_ctl_read(int n);
    void     gte_ctl_write(int n, uint32_t v);
};

} // namespace cb

