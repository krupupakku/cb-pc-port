#include "core/mips_cpu.h"
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
        uint32_t v = 0;
        std::memcpy(&v, p, size);
        return v;
    }
    uint32_t phys = vaddr & 0x1FFFFFFF;
    if (phys >= 0x1F800000 && phys < 0x1F800400) {  // scratchpad: usa fine della RAM virtuale
        uint32_t v = 0;
        std::memcpy(&v, &g_scratch[phys & 0x3FF], size);
        return v;
    }
    if (io_read) return io_read(phys, size);
    return 0;
}

void MipsCpu::write(uint32_t vaddr, uint32_t val, int size) {
    uint8_t* p = ram_ptr(vaddr);
    if (p) { std::memcpy(p, &val, size); return; }
    uint32_t phys = vaddr & 0x1FFFFFFF;
    if (phys >= 0x1F800000 && phys < 0x1F800400) {
        std::memcpy(&g_scratch[phys & 0x3FF], &val, size);
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
    // Hook HLE per indirizzo
    auto it = hooks.find(pc);
    if (it != hooks.end()) {
        if (it->second(*this)) {          // gestito: ritorna al chiamante
            pc = r[31]; next_pc = pc + 4;
            instr_count++;
            return;
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

} // namespace cb
