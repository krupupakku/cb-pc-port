#include "core/mips_cpu.h"
#include "core/cb_memory_map.h"
#include "renderer/renderer.h"
#include "renderer/prompt_overlay.h"
#include <cstdio>
#include <cstring>

namespace cb {

static constexpr uint32_t RAM_SIZE = 2 * 1024 * 1024;
static uint8_t g_scratch[1024];

MipsCpu::MipsCpu() : ram(RAM_SIZE, 0) {}

uint8_t* MipsCpu::ram_ptr(uint32_t vaddr) {
    uint32_t p = vaddr & 0x1FFFFFFF;
    if (p < 0x00800000) return &ram[p & (RAM_SIZE - 1)];  // 2MB mirrorati fino a 8MB
    return nullptr;
}

uint32_t MipsCpu::read(uint32_t vaddr, int size) {
    uint8_t* p = ram_ptr(vaddr);
    if (p) {
        if (size == 4) return *reinterpret_cast<const uint32_t*>(p);
        if (size == 2) return *reinterpret_cast<const uint16_t*>(p);
        return *p;
    }
    uint32_t phys = vaddr & 0x1FFFFFFF;
    if (phys >= 0x1F800000 && phys < 0x1F800400) {  // scratchpad
        const uint8_t* sp = &g_scratch[phys & 0x3FF];
        if (size == 4) return *reinterpret_cast<const uint32_t*>(sp);
        if (size == 2) return *reinterpret_cast<const uint16_t*>(sp);
        return *sp;
    }
    if (io_read) return io_read(phys, size);
    return 0;
}

void MipsCpu::write(uint32_t vaddr, uint32_t val, int size) {
    uint8_t* p = ram_ptr(vaddr);
    if (p) {
        if (size == 4) *reinterpret_cast<uint32_t*>(p) = val;
        else if (size == 2) *reinterpret_cast<uint16_t*>(p) = static_cast<uint16_t>(val);
        else *p = static_cast<uint8_t>(val);
        return;
    }
    uint32_t phys = vaddr & 0x1FFFFFFF;
    if (phys >= 0x1F800000 && phys < 0x1F800400) {
        uint8_t* sp = &g_scratch[phys & 0x3FF];
        if (size == 4) *reinterpret_cast<uint32_t*>(sp) = val;
        else if (size == 2) *reinterpret_cast<uint16_t*>(sp) = static_cast<uint16_t>(val);
        else *sp = static_cast<uint8_t>(val);
        return;
    }
    if (io_write) io_write(phys, val, size);
}

void MipsCpu::reset_to(uint32_t entry, uint32_t sp, uint32_t gp) {
    std::memset(r, 0, sizeof(r));
    pc = entry; next_pc = entry + 4;
    r[29] = sp; r[28] = gp; r[30] = sp;
    halted = false;
}

bool MipsCpu::load_psx_exe(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    uint8_t hdr[0x800];
    if (std::fread(hdr, 1, 0x800, f) != 0x800 || std::memcmp(hdr, "PS-X EXE", 8) != 0) { std::fclose(f); return false; }
    auto u32 = [&](int o) { uint32_t v; std::memcpy(&v, hdr + o, 4); return v; };
    uint32_t pc0 = u32(0x10), gp0 = u32(0x14), taddr = u32(0x18), tsize = u32(0x1C), sp0 = u32(0x30);
    uint8_t* dst = ram_ptr(taddr);
    size_t got = std::fread(dst, 1, tsize, f);
    std::fclose(f);
    if (got == 0) return false;
    reset_to(pc0, sp0 ? sp0 : 0x801FFFF0, gp0);
    std::printf("[exe] pc=%08X gp=%08X load=%08X size=%X sp=%08X\n", pc0, gp0, taddr, tsize, r[29]);
    return true;
}

static inline int32_t sx16(uint32_t v) { return (int16_t)v; }

void MipsCpu::step() {
    // Crash Bash Pause Menu / 2D UI Double-Buffer Sync:
    // DrawTextQuad (0x8002992C) alloca i pacchetti testo da *(0x8005B68C), ma li linka
    // nella OT di Viewport 0 *(0x800569D8). Nei menu di pausa (Warp Room e minigiochi),
    // il rendering 3D e' sospeso e la viewport non viene aggiornata prima del testo,
    // lasciando 0x800569D8 sul buffer del frame precedente (che viene azzerato prima del DMA).
    // Sincronizzando *(0x800569D8) = *(0x8005B68C) all'ingresso di DrawTextQuad, i quad
    // vengono inseriti nella OT attiva e inviati correttamente al DMA GPU.
    if (pc == 0x8002992C) {
        uint32_t b68c;
        std::memcpy(&b68c, &ram[0x5B68C], 4);
        std::memcpy(&ram[0x569D8], &b68c, 4);
    }

    // Crash Bash Memory Card I/O Completion Sync:
    // Evita il blocco o attesa infinita degli eventi hardware della memory card:
    // Slot 1: 0x80047620 (wait loop IOE), 0x800477C4 (event check IOE)
    // Slot 2: 0x800476F8 (wait loop TIMEOUT), 0x80047800 (event check TIMEOUT)
    // MemCardSync(0) blocking wait loop: 0x8003A818 (avanza il ticker della card)
    if (pc == 0x80047620 || pc == 0x800477C4) {
        uint32_t one = 1;
        std::memcpy(&ram[0x78800], &one, 4); // Slot 1 IOE = 1 (Operazione completata con successo)
    }
    if (pc == 0x800476F8 || pc == 0x80047800) {
        uint32_t one = 1;
        std::memcpy(&ram[0x78818], &one, 4); // Slot 2 TIMEOUT = 1 (Nessuna card nello Slot 2)
    }
    if (pc == 0x8003A818) {
        call_subroutine(0x8003AAA4);
    }

    // Intercettazione nativa del sottomenu "EXTRA" nelle OPTIONS (Main Menu e Pause Menu)
    if (pc >= 0x8007FEBC && pc <= 0x800B4F60) {
        if (handle_extra_menu_hooks()) {
            instr_count++;
            return;
        }
    }

    // Hook HLE: controllo rapido dell'intervallo prima della lookup hash
    if (pc < 0x200 || (pc >= 0x80030000 && pc < 0x80040000)) {
        auto it = hooks.find(pc);
        if (it != hooks.end()) {
            if (it->second(*this)) {          // gestito: ritorna al chiamante
                pc = r[31]; next_pc = pc + 4;
                instr_count++;
                return;
            }
        }
    }

    uint32_t cur_pc = pc;
    uint32_t in = read(cur_pc, 4);
    uint32_t branch_target = next_pc + 4;  // default
    uint32_t this_next = next_pc;
    pc = next_pc;
    next_pc = branch_target;
    (void)this_next;

    uint32_t op = in >> 26, rs = (in >> 21) & 31, rt = (in >> 16) & 31, rd = (in >> 11) & 31;
    uint32_t sh = (in >> 6) & 31, fn = in & 63;
    uint32_t imm = in & 0xFFFF;
    int32_t  simm = sx16(imm);
    uint32_t tgt = (cur_pc & 0xF0000000) | ((in & 0x03FFFFFF) << 2);
    auto br = [&](bool c) { if (c) next_pc = cur_pc + 4 + (simm << 2); };

    switch (op) {
    case 0x00:
        switch (fn) {
        case 0x00: r[rd] = r[rt] << sh; break;
        case 0x02: r[rd] = r[rt] >> sh; break;
        case 0x03: r[rd] = (int32_t)r[rt] >> sh; break;
        case 0x04: r[rd] = r[rt] << (r[rs] & 31); break;
        case 0x06: r[rd] = r[rt] >> (r[rs] & 31); break;
        case 0x07: r[rd] = (int32_t)r[rt] >> (r[rs] & 31); break;
        case 0x08: next_pc = r[rs]; break;
        case 0x09: { uint32_t t = r[rs]; r[rd] = cur_pc + 8; next_pc = t; break; }
        case 0x0C: // SYSCALL HLE: 1=EnterCriticalSection, 2=ExitCriticalSection
            if (r[4] == 1) r[2] = 1;
            else if (r[4] != 2) { halt("SYSCALL sconosciuta " + std::to_string(r[4])); }
            break;
        case 0x0D: halt("BREAK"); break;
        case 0x10: r[rd] = hi; break;
        case 0x11: hi = r[rs]; break;
        case 0x12: r[rd] = lo; break;
        case 0x13: lo = r[rs]; break;
        case 0x18: { int64_t p = (int64_t)(int32_t)r[rs] * (int32_t)r[rt]; lo = (uint32_t)p; hi = (uint32_t)(p >> 32); break; }
        case 0x19: { uint64_t p = (uint64_t)r[rs] * r[rt]; lo = (uint32_t)p; hi = (uint32_t)(p >> 32); break; }
        case 0x1A: {
            int32_t a = r[rs], b = r[rt];
            if (b == 0) { lo = a < 0 ? 1 : 0xFFFFFFFF; hi = a; }
            else if (a == INT32_MIN && b == -1) { lo = 0x80000000; hi = 0; }
            else { lo = a / b; hi = a % b; }
            break; }
        case 0x1B: {
            uint32_t a = r[rs], b = r[rt];
            if (b == 0) { lo = 0xFFFFFFFF; hi = a; } else { lo = a / b; hi = a % b; }
            break; }
        case 0x20: case 0x21: r[rd] = r[rs] + r[rt]; break;
        case 0x22: case 0x23: r[rd] = r[rs] - r[rt]; break;
        case 0x24: r[rd] = r[rs] & r[rt]; break;
        case 0x25: r[rd] = r[rs] | r[rt]; break;
        case 0x26: r[rd] = r[rs] ^ r[rt]; break;
        case 0x27: r[rd] = ~(r[rs] | r[rt]); break;
        case 0x2A: r[rd] = (int32_t)r[rs] < (int32_t)r[rt]; break;
        case 0x2B: r[rd] = r[rs] < r[rt]; break;
        default: halt("opcode SPECIAL sconosciuto"); break;
        }
        break;
    case 0x01: {
        bool ge = (rt & 1);
        bool link = (rt & 0x1E) == 0x10;
        bool cond = ge ? ((int32_t)r[rs] >= 0) : ((int32_t)r[rs] < 0);
        if (link) r[31] = cur_pc + 8;
        br(cond);
        break; }
    case 0x02: next_pc = tgt; break;
    case 0x03: r[31] = cur_pc + 8; next_pc = tgt; break;
    case 0x04: br(r[rs] == r[rt]); break;
    case 0x05: br(r[rs] != r[rt]); break;
    case 0x06: br((int32_t)r[rs] <= 0); break;
    case 0x07: br((int32_t)r[rs] > 0); break;
    case 0x08: case 0x09: r[rt] = r[rs] + simm; break;
    case 0x0A: r[rt] = (int32_t)r[rs] < simm; break;
    case 0x0B: r[rt] = r[rs] < (uint32_t)simm; break;
    case 0x0C: r[rt] = r[rs] & imm; break;
    case 0x0D: r[rt] = r[rs] | imm; break;
    case 0x0E: r[rt] = r[rs] ^ imm; break;
    case 0x0F: r[rt] = imm << 16; break;
    case 0x10: // COP0
        if (rs == 0) r[rt] = cop0[rd];
        else if (rs == 4) cop0[rd] = r[rt];
        else if (rs == 0x10) { /* RFE */ cop0[12] = (cop0[12] & ~0xF) | ((cop0[12] >> 2) & 0xF); }
        break;
    case 0x12: // COP2 (GTE)
        if (rs == 0) r[rt] = gte_read(rd);
        else if (rs == 2) r[rt] = gte_ctl_read(rd);
        else if (rs == 4) gte_write(rd, r[rt]);
        else if (rs == 6) gte_ctl_write(rd, r[rt]);
        else if (in & 0x02000000) gte_op(in);
        break;
    case 0x20: { uint32_t v = read(r[rs] + simm, 1); r[rt] = (int8_t)v; break; }
    case 0x21: { uint32_t v = read(r[rs] + simm, 2); r[rt] = (int16_t)v; break; }
    case 0x22: { uint32_t a = r[rs] + simm; uint32_t w = read(a & ~3, 4); uint32_t s = (a & 3) * 8;
                 r[rt] = (r[rt] & (0x00FFFFFFu >> s)) | (w << (24 - s)); break; }
    case 0x23: r[rt] = read(r[rs] + simm, 4); break;
    case 0x24: r[rt] = read(r[rs] + simm, 1); break;
    case 0x25: r[rt] = read(r[rs] + simm, 2); break;
    case 0x26: { uint32_t a = r[rs] + simm; uint32_t w = read(a & ~3, 4); uint32_t s = (a & 3) * 8;
                 r[rt] = (r[rt] & (0xFFFFFF00u << (24 - s))) | (w >> s); break; }
    case 0x28: write(r[rs] + simm, r[rt], 1); break;
    case 0x29: write(r[rs] + simm, r[rt], 2); break;
    case 0x2A: { uint32_t a = r[rs] + simm; uint32_t w = read(a & ~3, 4); uint32_t s = (a & 3) * 8;
                 w = (w & (0xFFFFFF00u << s)) | (r[rt] >> (24 - s)); write(a & ~3, w, 4); break; }
    case 0x2B: write(r[rs] + simm, r[rt], 4); break;
    case 0x2E: { uint32_t a = r[rs] + simm; uint32_t w = read(a & ~3, 4); uint32_t s = (a & 3) * 8;
                 w = (w & (0x00FFFFFFu >> (24 - s))) | (r[rt] << s); write(a & ~3, w, 4); break; }
    case 0x32: gte_write(rt, read(r[rs] + simm, 4)); break;   // LWC2
    case 0x3A: write(r[rs] + simm, gte_read(rt), 4); break;   // SWC2
    default: {
        char b[96]; std::snprintf(b, sizeof b, "opcode sconosciuto %02X @%08X", op, cur_pc);
        halt(b); break; }
    }
    r[0] = 0;
    instr_count++;
}

void MipsCpu::run(uint64_t max_instr) {
    uint64_t end = instr_count + max_instr;
    while (!halted && instr_count < end) step();
}

void MipsCpu::call_subroutine(uint32_t target_addr, uint32_t a0) {
    if (target_addr == 0 || halted) return;
    uint32_t saved_r[32];
    std::memcpy(saved_r, r, sizeof(r));
    uint32_t saved_pc = pc;
    uint32_t saved_next_pc = next_pc;

    static constexpr uint32_t SENTINEL_RETURN = 0x00000008;
    r[31] = SENTINEL_RETURN;
    r[4]  = a0;
    pc = target_addr;
    next_pc = target_addr + 4;

    uint32_t steps = 0;
    while (!halted && pc != SENTINEL_RETURN && steps < 100000) {
        step();
        steps++;
    }

    std::memcpy(r, saved_r, sizeof(r));
    pc = saved_pc;
    next_pc = saved_next_pc;
}

static void apply_extra_setting_step(int item_idx, int dir) {
    switch (item_idx) {
    case 0: cb::renderer::StepAspectRatio(dir); break;
    case 1: cb::renderer::StepInternalResolution(dir); break;
    case 2: cb::renderer::StepVideoFilter(dir); break;
    case 3: cb::renderer::ToggleBilinearTextures(); break;
    case 4: cb::ui::StepPromptDevice(dir); break;
    case 5: cb::renderer::ToggleFullscreen(); break;
    default: break;
    }
}

void MipsCpu::sync_extra_menu_values() {
    const char* vals[6] = {
        cb::renderer::GetAspectRatioMenuLabel(),
        cb::renderer::GetInternalResolutionMenuLabel(),
        cb::renderer::GetVideoFilterMenuLabel(),
        cb::renderer::GetBilinearTexturesMenuLabel(),
        cb::ui::GetPromptDeviceMenuLabel(),
        cb::renderer::GetFullscreenMenuLabel()
    };
    for (int i = 0; i < 6; ++i) {
        uint8_t* dst = ram_ptr(cb::mem::REMASTER_STR_DYN_BASE + i * 32);
        if (dst) {
            std::memset(dst, 0, 32);
            std::snprintf(reinterpret_cast<char*>(dst), 32, "%s", vals[i]);
        }
    }
}

void MipsCpu::ensure_extra_menu_tables() {
    // Sincronizza sempre le stringhe dinamiche dei valori correnti
    if (read32(cb::mem::PAUSE_STR_TABLE_PTRS) == cb::mem::REMASTER_STR_TABLE_BASE &&
        read32(cb::mem::REMASTER_PAUSE_OPT_DESC) != 0) {
        sync_extra_menu_values();
        return;
    }

    // Configura Style 8 (0x8005689C + 8 * 28) nel Main Menu per il font compatto pulsante giallo
    uint32_t style8 = 0x8005689Cu + 8u * 28u;
    write(style8 + 0x00, 1, 4);          // font0 = 1 (Compact Font)
    write(style8 + 0x04, 0x0000D0FF, 4); // col1
    write(style8 + 0x08, 0x000000FF, 4); // col2
    write(style8 + 0x0C, 2, 4);          // mode = 2 (Pulsating Yellow Highlight)
    write(style8 + 0x10, 1, 4);          // font1 = 1 (Compact Font durante la pulsazione)

    // 1. Stringhe statiche in 0x8000C200 (slot da 32 byte ciascuno)
    static const char* kStaticStrings[9] = {
        "EXTRA",
        "ASPECT RATIO",
        "RESOLUTION",
        "VIDEO FILTER",
        "TEX FILTER",
        "BUTTON ICONS",
        "WINDOW MODE",
        "PRESS \xff BUTTON TO CHANGE",
        "PRESS \xfc BUTTON TO ACCEPT"
    };
    for (int i = 0; i < 9; ++i) {
        uint8_t* dst = ram_ptr(cb::mem::REMASTER_STR_STATIC_BASE + i * 32);
        if (dst) {
            std::memset(dst, 0, 32);
            std::memcpy(dst, kStaticStrings[i], std::strlen(kStaticStrings[i]) + 1);
        }
    }

    // 2. Popola i valori dinamici in 0x8000C380 (6 slot da 32 byte)
    sync_extra_menu_values();

    // 3. Tabella puntatori stringhe estesa in 0x8000C000
    for (int i = 0; i < 56; ++i) {
        write(cb::mem::REMASTER_STR_TABLE_BASE + i * 4,
              read32(cb::mem::PAUSE_STR_TABLE_EN + i * 4), 4);
    }
    write(cb::mem::REMASTER_STR_TABLE_BASE + 56 * 4, cb::mem::REMASTER_STR_STATIC_BASE + 0 * 32, 4); // "EXTRA"
    for (int i = 0; i < 6; ++i) {
        write(cb::mem::REMASTER_STR_TABLE_BASE + (57 + i) * 4,
              cb::mem::REMASTER_STR_STATIC_BASE + (1 + i) * 32, 4);
        write(cb::mem::REMASTER_STR_TABLE_BASE + (63 + i) * 4,
              cb::mem::REMASTER_STR_DYN_BASE + i * 32, 4);
    }
    write(cb::mem::REMASTER_STR_TABLE_BASE + 69 * 4, cb::mem::REMASTER_STR_STATIC_BASE + 7 * 32, 4);
    write(cb::mem::REMASTER_STR_TABLE_BASE + 70 * 4, cb::mem::REMASTER_STR_STATIC_BASE + 8 * 32, 4);

    // Redireziona tutte le 5 lingue della tabella globale del menu di pausa
    for (int lang = 0; lang < 5; ++lang) {
        write(cb::mem::PAUSE_STR_TABLE_PTRS + lang * 4, cb::mem::REMASTER_STR_TABLE_BASE, 4);
    }

    // 4. Descrittore Pause Menu OPTIONS (4 voci: SOUND, CONTROLLER, EXTRA, EXIT) in 0x8000C500
    auto write_pause_item = [&](uint32_t base, int idx, int16_t type, int16_t x, int16_t y, int16_t str_id,
                                uint32_t col1, uint32_t col2, uint32_t sel1, uint32_t sel2) {
        uint32_t addr = base + idx * 24;
        write(addr + 0,  (uint16_t)type, 2);
        write(addr + 2,  (uint16_t)x, 2);
        write(addr + 4,  (uint16_t)y, 2);
        write(addr + 6,  (uint16_t)str_id, 2);
        write(addr + 8,  col1, 4);
        write(addr + 12, col2, 4);
        write(addr + 16, sel1, 4);
        write(addr + 20, sel2, 4);
    };

    const uint32_t c_norm1 = 0x0000D0FF, c_norm2 = 0x000000FF;
    const uint32_t c_sel1  = 0x00FFFFFF, c_sel2  = 0x0000FFFF;
    const uint32_t c_dim1  = 0x00808080, c_dim2  = 0x00404040;

    write_pause_item(cb::mem::REMASTER_PAUSE_OPT_DESC, 0,  2,    -10, 210, 210, 0, 0, 0x8080, 0);
    write_pause_item(cb::mem::REMASTER_PAUSE_OPT_DESC, 1,  5, -32768,  10,   2, c_norm1, c_norm2, c_dim1, c_dim2); // OPTIONS
    write_pause_item(cb::mem::REMASTER_PAUSE_OPT_DESC, 2,  0, -32768,  58,  39, c_norm1, c_norm2, c_sel1, c_sel2); // SOUND
    write_pause_item(cb::mem::REMASTER_PAUSE_OPT_DESC, 3,  0, -32768,  92,  44, c_norm1, c_norm2, c_sel1, c_sel2); // CONTROLLER
    write_pause_item(cb::mem::REMASTER_PAUSE_OPT_DESC, 4,  0, -32768, 126,  56, c_norm1, c_norm2, c_sel1, c_sel2); // EXTRA
    write_pause_item(cb::mem::REMASTER_PAUSE_OPT_DESC, 5,  0, -32768, 160,  49, c_norm1, c_norm2, c_sel1, c_sel2); // EXIT
    write_pause_item(cb::mem::REMASTER_PAUSE_OPT_DESC, 6, -1,      0,   0,   0, 0, 0, 0, 0);

    // 5. Descrittore Pause Menu EXTRA in 0x8000C600 (19 record da 24 byte)
    write_pause_item(cb::mem::REMASTER_PAUSE_EXTRA_DESC, 0, 7,    -10, 216, 216, 0, 0, 0x8080, 0);
    write_pause_item(cb::mem::REMASTER_PAUSE_EXTRA_DESC, 1, 5, -32768,   6,  56, c_norm1, c_norm2, c_dim1, c_dim2); // EXTRA title
    write_pause_item(cb::mem::REMASTER_PAUSE_EXTRA_DESC, 2, 6,      1,   0,   0, 0, 0, 0, 0); // Compact Font
    for (int i = 0; i < 6; ++i) {
        int16_t y = static_cast<int16_t>(42 + i * 20);
        write_pause_item(cb::mem::REMASTER_PAUSE_EXTRA_DESC, 3 + 2 * i,     0,  90, y, static_cast<int16_t>(57 + i), c_norm1, c_norm2, c_sel1, c_sel2);
        write_pause_item(cb::mem::REMASTER_PAUSE_EXTRA_DESC, 4 + 2 * i,     5, 355, y, static_cast<int16_t>(63 + i), c_norm1, c_norm2, c_dim1, c_dim2);
    }
    write_pause_item(cb::mem::REMASTER_PAUSE_EXTRA_DESC, 15, 5, -32768, 170, 69, c_norm1, c_norm2, c_dim1, c_dim2);
    write_pause_item(cb::mem::REMASTER_PAUSE_EXTRA_DESC, 16, 5, -32768, 188, 70, c_norm1, c_norm2, c_dim1, c_dim2);
    write_pause_item(cb::mem::REMASTER_PAUSE_EXTRA_DESC, 17, 6,      0,   0,  0, 0, 0, 0, 0); // Restore Large Font
    write_pause_item(cb::mem::REMASTER_PAUSE_EXTRA_DESC, 18, -1,     0,   0,  0, 0, 0, 0, 0);

    // 6. Descrittore Main Menu OPTIONS (4 voci) in 0x8000C800 (8 widget da 36 byte)
    auto write_mm_widget = [&](uint32_t base, int idx, uint32_t w0, uint32_t w1, uint32_t w2,
                               uint32_t w3, uint32_t w4, uint32_t w5, uint32_t w6) {
        uint32_t addr = base + idx * 36;
        write(addr + 0,  w0, 4);
        write(addr + 4,  w1, 4);
        write(addr + 8,  w2, 4);
        write(addr + 12, w3, 4);
        write(addr + 16, w4, 4);
        write(addr + 20, w5, 4);
        write(addr + 24, w6, 4);
        write(addr + 28, 0,  4);
        write(addr + 32, 0,  4);
    };

    write_mm_widget(cb::mem::REMASTER_MM_OPT_DESC, 0, 0x0E, 0xFF748000, 0, 0, 0, 0x800B8EE8, 0); // OPTIONS
    write_mm_widget(cb::mem::REMASTER_MM_OPT_DESC, 1, 0x04, 0x015EFEA2, 0xFF9CFF6C, 1, 0, 0, 0); // Top banner box
    write_mm_widget(cb::mem::REMASTER_MM_OPT_DESC, 2, 0x04, 0x00BEFF42, 0x0072FFBA, 1, 0, 0, 0); // Center 4-item box
    write_mm_widget(cb::mem::REMASTER_MM_OPT_DESC, 3, 0x0E, 0xFFD48000, 0, 0, 0, 0x800B8EF0, 0); // SOUND (y=-44)
    write_mm_widget(cb::mem::REMASTER_MM_OPT_DESC, 4, 0x0E, 0xFFF68000, 0, 0, 0, 0x800B8F08, 0); // CONTROLLER (y=-10)
    write_mm_widget(cb::mem::REMASTER_MM_OPT_DESC, 5, 0x03, 0x00188000, 0, 0, 0, cb::mem::REMASTER_STR_STATIC_BASE + 0 * 32, 0); // EXTRA (y=+24)
    write_mm_widget(cb::mem::REMASTER_MM_OPT_DESC, 6, 0x0E, 0x003A8000, 0, 0, 0, 0x800B8EEC, 0); // EXIT (y=+58)
    write_mm_widget(cb::mem::REMASTER_MM_OPT_DESC, 7, 0x00, 0, 0, 0, 0, 0, 0);

    // 7. Descrittore Main Menu EXTRA in 0x8000CA00 (18 widget da 36 byte)
    write_mm_widget(cb::mem::REMASTER_MM_EXTRA_DESC, 0, 0x03, 0xFF748000, 0, 0, 0, cb::mem::REMASTER_STR_STATIC_BASE + 0 * 32, 0); // EXTRA banner
    write_mm_widget(cb::mem::REMASTER_MM_EXTRA_DESC, 1, 0x04, 0x015EFEA2, 0xFF9CFF6C, 1, 0, 0, 0); // Top banner box
    write_mm_widget(cb::mem::REMASTER_MM_EXTRA_DESC, 2, 0x04, 0x00FAFF06, 0x008CFFB4, 1, 0, 0, 0); // Center wide box (x=-250..250, y=-76..140)
    for (int i = 0; i < 6; ++i) {
        uint16_t y_u16 = static_cast<uint16_t>(static_cast<int16_t>(-58 + i * 20));
        uint32_t w1_lbl = (static_cast<uint32_t>(y_u16) << 16) | static_cast<uint16_t>(-220);
        uint32_t w1_val = (static_cast<uint32_t>(y_u16) << 16) | static_cast<uint16_t>(55);
        write_mm_widget(cb::mem::REMASTER_MM_EXTRA_DESC, 3 + 2 * i,     0x03, w1_lbl, 0, 0, 0, cb::mem::REMASTER_STR_STATIC_BASE + (1 + i) * 32, 4);
        write_mm_widget(cb::mem::REMASTER_MM_EXTRA_DESC, 4 + 2 * i,     0x03, w1_val, 0, 0, 0, cb::mem::REMASTER_STR_DYN_BASE + i * 32, 4);
    }
    write_mm_widget(cb::mem::REMASTER_MM_EXTRA_DESC, 15, 0x03, (74u << 16) | 0x8000u, 0, 0, 0, cb::mem::REMASTER_STR_STATIC_BASE + 7 * 32, 4);
    write_mm_widget(cb::mem::REMASTER_MM_EXTRA_DESC, 16, 0x03, (94u << 16) | 0x8000u, 0, 0, 0, cb::mem::REMASTER_STR_STATIC_BASE + 8 * 32, 4);
    write_mm_widget(cb::mem::REMASTER_MM_EXTRA_DESC, 17, 0x00, 0, 0, 0, 0, 0, 0);
}

bool MipsCpu::handle_extra_menu_hooks() {
    // -------------------------------------------------------------------------
    // 1. PAUSE MENU OPTIONS: Render Selector (0x80081284 in 991.code.bin)
    // -------------------------------------------------------------------------
    if (pc == 0x80081284) {
        ensure_extra_menu_tables();
        uint32_t sub_page = read32(cb::mem::PAUSE_OPT_SUBPAGE);
        if (sub_page == 0) {
            r[4] = cb::mem::REMASTER_PAUSE_OPT_DESC;
            r[5] = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(read(cb::mem::PAUSE_OPT_BOX_Y, 2))));
            pc = 0x80081450;
            next_pc = 0x80081454;
            return true;
        }
        if (sub_page == 3) {
            int32_t cursor = static_cast<int32_t>(read32(cb::mem::PAUSE_OPT_CURSOR));
            for (int i = 0; i < 6; ++i) {
                uint32_t addr = cb::mem::REMASTER_PAUSE_EXTRA_DESC + (4 + 2 * i) * 24;
                if (i == cursor) {
                    write(addr + 8,  0x00FFFFFF, 4);
                    write(addr + 12, 0x0000FFFF, 4);
                } else {
                    write(addr + 8,  0x0000D0FF, 4);
                    write(addr + 12, 0x000000FF, 4);
                }
            }
            r[4] = cb::mem::REMASTER_PAUSE_EXTRA_DESC;
            r[5] = 22;
            pc = 0x80081450;
            next_pc = 0x80081454;
            return true;
        }
        return false;
    }

    // -------------------------------------------------------------------------
    // 2. PAUSE MENU OPTIONS: Input Handler (0x8007FEBC in 991.code.bin)
    // -------------------------------------------------------------------------
    if (pc == 0x8007FEBC) {
        ensure_extra_menu_tables();
        // Estende il limite del menu OPTIONS di pausa da 3 a 4 voci (0=SOUND, 1=CONTROLLER, 2=EXTRA, 3=EXIT)
        // Nota: 0x800802DC contiene "jal 0x8007EFC0" (MenuHandleUpDownCross) e NON va sovrascritto!
        write(0x800802DC, 0x0C01FBF0, 4); // Ripristina jal 0x8007EFC0 (MenuHandleUpDownCross)
        write(0x800802E0, 0x24050003, 4); // addiu $a1, $zero, 3 (maxIdx = 3 nel delay slot)
        write(0x800802F0, 0x24020003, 4); // addiu $v0, $zero, 3 (EXIT index = 3)

        if (read32(cb::mem::PAUSE_OPT_SUBPAGE) == 3) {
            uint32_t port = read32(cb::mem::PAUSE_PLAYER_PORT) & 3u;
            uint32_t tapped = read32(cb::mem::PAD_STRUCT_BASE + port * cb::mem::PAD_STRUCT_STRIDE + cb::mem::PAD_TAPPED_OFFSET);
            int32_t cursor = static_cast<int32_t>(read32(cb::mem::PAUSE_OPT_CURSOR));

            if (tapped & cb::mem::PAD_BTN_UP) {
                if (cursor > 0) {
                    write(cb::mem::PAUSE_OPT_CURSOR, static_cast<uint32_t>(cursor - 1), 4);
                    r[4] = 400;
                    pc = 0x800801A0;
                    next_pc = 0x800801A4;
                    return true;
                }
            } else if (tapped & cb::mem::PAD_BTN_DOWN) {
                if (cursor < 5) {
                    write(cb::mem::PAUSE_OPT_CURSOR, static_cast<uint32_t>(cursor + 1), 4);
                    r[4] = 400;
                    pc = 0x800801A0;
                    next_pc = 0x800801A4;
                    return true;
                }
            } else if (tapped & (cb::mem::PAD_BTN_LEFT | cb::mem::PAD_BTN_RIGHT | cb::mem::PAD_BTN_SQUARE)) {
                int dir = (tapped & cb::mem::PAD_BTN_LEFT) ? -1 : 1;
                apply_extra_setting_step(cursor, dir);
                sync_extra_menu_values();
                r[4] = 400;
                pc = 0x800801A0;
                next_pc = 0x800801A4;
                return true;
            } else if (tapped & (cb::mem::PAD_BTN_CROSS | cb::mem::PAD_BTN_TRIANGLE)) {
                write(cb::mem::PAUSE_OPT_SUBPAGE, 0, 4);
                write(cb::mem::PAUSE_OPT_CURSOR,  2, 4); // Mantiene il cursore su EXTRA
                r[4] = 405;
                pc = 0x800801A0;
                next_pc = 0x800801A4;
                return true;
            }

            pc = 0x8007F704;
            next_pc = 0x8007F708;
            return true;
        }
        return false;
    }

    // Controlla se l'overlay 722.type_0x69.bin (Main Menu) e' attualmente caricato in RAM
    bool is_main_menu_ovl = (read32(cb::mem::OVERLAY_BASE) == 0x69) &&
                            ((read32(0x800B49F8) & 0xFFFFFFFCu) == 0x28620000u);
    if (!is_main_menu_ovl) return false;

    // -------------------------------------------------------------------------
    // 3. MAIN MENU: MainMenuSetWidgetPage (0x80095BEC)
    // -------------------------------------------------------------------------
    if (pc == 0x80095BEC) {
        ensure_extra_menu_tables();
        write(0x800B49F8, 0x28620003, 4); // slti $v0, $v1, 3 (consente 4 voci: 0..3)
        if (r[4] == 4) {
            if (read32(cb::mem::MAIN_MENU_OPT_SUBPAGE) == 3) {
                r[5] = cb::mem::REMASTER_MM_EXTRA_DESC;
            } else if (r[5] == cb::mem::MM_OPT_MAIN_WIDGETS) {
                r[5] = cb::mem::REMASTER_MM_OPT_DESC;
            }
        }
        return false;
    }

    // -------------------------------------------------------------------------
    // 4. MAIN MENU OPTIONS: Cross Button Branch (0x800B4AD0)
    // -------------------------------------------------------------------------
    if (pc == 0x800B4AD0) {
        ensure_extra_menu_tables();
        uint32_t cursor = read32(cb::mem::MAIN_MENU_OPT_CURSOR);
        if (cursor == 2) { // EXTRA
            write(cb::mem::MAIN_MENU_OPT_SUBPAGE, 3, 4);
            pc = 0x800B4B10;
            next_pc = 0x800B4B14;
            return true;
        }
        if (cursor == 3) { // EXIT
            pc = 0x800B4AD8;
            next_pc = 0x800B4ADC;
            return true;
        }
        return false;
    }

    // -------------------------------------------------------------------------
    // 5. MAIN MENU OPTIONS: Input Handler per sub_page == 3 (0x800B498C)
    // -------------------------------------------------------------------------
    if (pc == 0x800B498C) {
        ensure_extra_menu_tables();
        if (r[3] == 3) {
            uint32_t tapped = read32(cb::mem::PAD_P1_TAPPED);
            int32_t cursor = static_cast<int32_t>(read32(cb::mem::MAIN_MENU_OPT_CURSOR));

            auto play_mm_sfx_and_end = [&](uint32_t sfx_id) {
                r[4] = sfx_id;
                r[5] = 0;
                r[6] = 0;
                r[7] = 0x1000;
                write(r[29] + 16, 0x1E00, 4);
                write(r[29] + 20, 0, 4);
                r[31] = 0x800B4F00;
                pc = cb::mem::FN_PLAY_UI_SFX;
                next_pc = cb::mem::FN_PLAY_UI_SFX + 4;
            };

            if (tapped & cb::mem::PAD_BTN_UP) {
                if (cursor > 0) {
                    write(cb::mem::MAIN_MENU_OPT_CURSOR, static_cast<uint32_t>(cursor - 1), 4);
                    play_mm_sfx_and_end(400);
                    return true;
                }
            } else if (tapped & cb::mem::PAD_BTN_DOWN) {
                if (cursor < 5) {
                    write(cb::mem::MAIN_MENU_OPT_CURSOR, static_cast<uint32_t>(cursor + 1), 4);
                    play_mm_sfx_and_end(400);
                    return true;
                }
            } else if (tapped & (cb::mem::PAD_BTN_LEFT | cb::mem::PAD_BTN_RIGHT | cb::mem::PAD_BTN_SQUARE)) {
                int dir = (tapped & cb::mem::PAD_BTN_LEFT) ? -1 : 1;
                apply_extra_setting_step(cursor, dir);
                sync_extra_menu_values();
                play_mm_sfx_and_end(400);
                return true;
            } else if (tapped & (cb::mem::PAD_BTN_CROSS | cb::mem::PAD_BTN_TRIANGLE)) {
                extra_return_cursor_ = true;
                pc = 0x800B4EC0;
                next_pc = 0x800B4EC4;
                return true;
            }

            pc = 0x800B4F00;
            next_pc = 0x800B4F04;
            return true;
        }
        return false;
    }

    // -------------------------------------------------------------------------
    // 6. MAIN MENU OPTIONS: Ripristino cursore su EXTRA al ritorno (0x800B4F00)
    // -------------------------------------------------------------------------
    if (pc == 0x800B4F00) {
        if (extra_return_cursor_) {
            extra_return_cursor_ = false;
            write(cb::mem::MAIN_MENU_OPT_CURSOR, 2, 4);
        }
        return false;
    }

    // -------------------------------------------------------------------------
    // 7. MAIN MENU OPTIONS: Update Visual State per sub_page == 3 (0x800B4F60)
    // -------------------------------------------------------------------------
    if (pc == 0x800B4F60) {
        ensure_extra_menu_tables();
        if (r[3] == 3 && r[17] != 0) {
            int32_t cursor = static_cast<int32_t>(read32(cb::mem::MAIN_MENU_OPT_CURSOR));
            for (int i = 0; i < 6; ++i) {
                // Style 8 = Compact Font pulsante giallo; Style 4 = Compact Font normale arancio
                uint32_t style = (i == cursor) ? 8u : 4u;
                uint32_t w_lbl = r[17] + static_cast<uint32_t>(3 + 2 * i) * 168u;
                uint32_t w_val = r[17] + static_cast<uint32_t>(4 + 2 * i) * 168u;
                write(w_lbl + 124, style, 4);
                write(w_val + 124, style, 4);
            }
        }
        return false;
    }

    return false;
}

} // namespace cb
