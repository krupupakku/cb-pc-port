// GTE (Geometry Transformation Engine, COP2) della PS1.
// Implementazione basata sulla documentazione di nocash (psx-spx).
// NOTA: i bit di FLAG per overflow/saturazione e la divisione UNR esatta
// sono semplificati: da rifinire quando serviranno (determinismo netcode).
#include "core/mips_cpu.h"
#include <algorithm>
#include <cstdio>
#include <map>

namespace cb {

static inline int32_t clampi(int64_t v, int64_t lo, int64_t hi) { return (int32_t)(v < lo ? lo : (v > hi ? hi : v)); }
static inline int16_t lo16(int32_t v) { return (int16_t)(v & 0xFFFF); }
static inline int16_t hi16(int32_t v) { return (int16_t)((uint32_t)v >> 16); }

uint32_t MipsCpu::gte_read(int n) {
    switch (n) {
    case 1: case 3: case 5: case 8: case 9: case 10: case 11:
    case 16: case 17: case 18: case 19:
        return (uint32_t)(int32_t)(n >= 16 ? (uint16_t)gte_d[n] : (int16_t)gte_d[n]) ;
    case 7:  return (uint16_t)gte_d[7];
    case 15: return (uint32_t)gte_d[14];                       // SXYP legge SXY2
    case 28: case 29: {                                        // IRGB/ORGB
        int r = clampi(gte_d[9] >> 7, 0, 31), g = clampi(gte_d[10] >> 7, 0, 31), b = clampi(gte_d[11] >> 7, 0, 31);
        return r | (g << 5) | (b << 10);
    }
    default: return (uint32_t)gte_d[n];
    }
}

void MipsCpu::gte_write(int n, uint32_t v) {
    switch (n) {
    case 15:  // SXYP: push FIFO
        gte_d[12] = gte_d[13]; gte_d[13] = gte_d[14]; gte_d[14] = (int32_t)v; break;
    case 28:  // IRGB -> IR1..3
        gte_d[28] = v & 0x7FFF;
        gte_d[9] = (v & 31) * 0x80; gte_d[10] = ((v >> 5) & 31) * 0x80; gte_d[11] = ((v >> 10) & 31) * 0x80; break;
    case 29: break;  // ORGB read-only
    case 30: {       // LZCS -> LZCR
        gte_d[30] = (int32_t)v;
        int c = 0; uint32_t sign = v >> 31;
        for (int i = 31; i >= 0; i--) { if (((v >> i) & 1) == sign) c++; else break; }
        gte_d[31] = c; break; }
    case 31: break;  // LZCR read-only
    case 1: case 3: case 5: case 8: case 9: case 10: case 11:
        gte_d[n] = (int16_t)v; break;
    case 7: case 16: case 17: case 18: case 19:
        gte_d[n] = (uint16_t)v; break;
    default: gte_d[n] = (int32_t)v; break;
    }
}

uint32_t MipsCpu::gte_ctl_read(int n) {
    switch (n) {
    case 4: case 12: case 20: case 26: case 27: case 29: case 30: return (uint32_t)(int32_t)(int16_t)gte_c[n];
    default: return (uint32_t)gte_c[n];
    }
}

void MipsCpu::gte_ctl_write(int n, uint32_t v) {
    if (n == 31) { gte_c[31] = (int32_t)(v & 0x7FFFF000); return; }
    gte_c[n] = (int32_t)v;
}

// ---------------------------------------------------------------------------
namespace {
struct G {
    MipsCpu& c; int sf, lm;
    int64_t sh(int64_t v) const { return v >> (sf * 12); }
    int16_t mat(int base, int i, int j) const {
        int idx = i * 3 + j; int32_t r = c.gte_c[base + idx / 2];
        return (idx & 1) ? hi16(r) : lo16(r);
    }
    void vec(int v, int32_t out[3]) const {
        if (v == 3) { out[0] = c.gte_d[9]; out[1] = c.gte_d[10]; out[2] = c.gte_d[11]; return; }
        out[0] = lo16(c.gte_d[v * 2]); out[1] = hi16(c.gte_d[v * 2]); out[2] = (int16_t)c.gte_d[v * 2 + 1];
    }
    void setIR(int i, int64_t v) { c.gte_d[8 + i] = clampi(v, lm ? 0 : -32768, 32767); }
    void setMAC(int i, int64_t v) { c.gte_d[24 + i] = (int32_t)v; }
    void mvmva(int mx, int v, int cv) {
        int32_t V[3]; vec(v, V);
        int base = mx == 0 ? 0 : mx == 1 ? 8 : 16;
        int64_t T[3] = {0, 0, 0};
        if (cv == 0) { T[0] = c.gte_c[5]; T[1] = c.gte_c[6]; T[2] = c.gte_c[7]; }
        else if (cv == 1) { T[0] = c.gte_c[13]; T[1] = c.gte_c[14]; T[2] = c.gte_c[15]; }
        else if (cv == 2) { T[0] = c.gte_c[21]; T[1] = c.gte_c[22]; T[2] = c.gte_c[23]; }
        for (int i = 0; i < 3; i++) {
            int64_t s = (T[i] << 12) + (int64_t)mat(base, i, 0) * V[0] + (int64_t)mat(base, i, 1) * V[1] + (int64_t)mat(base, i, 2) * V[2];
            setMAC(1 + i, sh(s)); setIR(1 + i, c.gte_d[25 + i]);
        }
    }
    void pushColor() {
        c.gte_d[20] = c.gte_d[21]; c.gte_d[21] = c.gte_d[22];
        uint32_t code = (uint32_t)c.gte_d[6] >> 24;
        uint32_t r = clampi(c.gte_d[25] >> 4, 0, 255), g = clampi(c.gte_d[26] >> 4, 0, 255), b = clampi(c.gte_d[27] >> 4, 0, 255);
        c.gte_d[22] = (int32_t)(r | (g << 8) | (b << 16) | (code << 24));
    }
    void rgbc(int32_t out[3]) { uint32_t v = (uint32_t)c.gte_d[6]; out[0] = v & 255; out[1] = (v >> 8) & 255; out[2] = (v >> 16) & 255; }
    void light(int v) { mvmva(1, v, 3); }                       // LLM * V
    void color() { mvmva(2, 3, 1); }                            // BK + LCM * IR
    void rtp(int v, bool last) {
        int32_t V[3]; vec(v, V);
        int64_t m[3];
        for (int i = 0; i < 3; i++)
            m[i] = ((int64_t)c.gte_c[5 + i] << 12) + (int64_t)mat(0, i, 0) * V[0] + (int64_t)mat(0, i, 1) * V[1] + (int64_t)mat(0, i, 2) * V[2];
        setMAC(1, sh(m[0])); setMAC(2, sh(m[1])); setMAC(3, sh(m[2]));
        setIR(1, c.gte_d[25]); setIR(2, c.gte_d[26]);
        { int64_t z = c.gte_d[27]; c.gte_d[11] = clampi(z, lm ? 0 : -32768, 32767); }
        int64_t zs = (sf ? c.gte_d[27] : (int64_t)(m[2] >> 12));
        c.gte_d[16] = c.gte_d[17]; c.gte_d[17] = c.gte_d[18]; c.gte_d[18] = c.gte_d[19];
        c.gte_d[19] = clampi(zs, 0, 65535);
        uint32_t H = (uint16_t)c.gte_c[26]; uint32_t sz3 = (uint16_t)c.gte_d[19];
        int64_t div = 0x1FFFF;
        if (sz3 > 0 && sz3 * 2 > H) {
            div = (((int64_t)H * 0x20000 / sz3) + 1) / 2;
            if (div > 0x1FFFF) div = 0x1FFFF;
        }
        int64_t mx = div * c.gte_d[9] + c.gte_c[24];
        int64_t my = div * c.gte_d[10] + c.gte_c[25];
        c.gte_d[12] = c.gte_d[13]; c.gte_d[13] = c.gte_d[14];
        int32_t sx = clampi(mx >> 16, -1024, 1023), sy = clampi(my >> 16, -1024, 1023);
        c.gte_d[14] = (int32_t)(((uint32_t)(uint16_t)sy << 16) | (uint16_t)sx);
        if (last) {
            int64_t m0 = div * (int16_t)c.gte_c[27] + c.gte_c[28];
            c.gte_d[24] = (int32_t)m0; c.gte_d[8] = clampi(m0 >> 12, 0, 65535);
        }
    }
};
} // namespace

void MipsCpu::gte_op(uint32_t in) {
    G g{*this, (int)((in >> 19) & 1), (int)((in >> 10) & 1)};
    int cmd = in & 0x3F;
    int mx = (in >> 17) & 3, v = (in >> 15) & 3, cv = (in >> 13) & 3;
    gte_c[31] = 0;
    switch (cmd) {
    case 0x01: g.rtp(0, true); break;                                         // RTPS
    case 0x30: g.rtp(0, false); g.rtp(1, false); g.rtp(2, true); break;       // RTPT
    case 0x06: {                                                              // NCLIP
        int32_t x0 = lo16(gte_d[12]), y0 = hi16(gte_d[12]), x1 = lo16(gte_d[13]), y1 = hi16(gte_d[13]), x2 = lo16(gte_d[14]), y2 = hi16(gte_d[14]);
        gte_d[24] = (int32_t)((int64_t)x0 * y1 + (int64_t)x1 * y2 + (int64_t)x2 * y0 - (int64_t)x0 * y2 - (int64_t)x1 * y0 - (int64_t)x2 * y1);
        break; }
    case 0x2D: { int64_t m = (int64_t)(int16_t)gte_c[29] * ((uint16_t)gte_d[17] + (uint16_t)gte_d[18] + (uint16_t)gte_d[19]);
                 gte_d[24] = (int32_t)m; gte_d[7] = clampi(m >> 12, 0, 65535); break; }                       // AVSZ3
    case 0x2E: { int64_t m = (int64_t)(int16_t)gte_c[30] * ((uint16_t)gte_d[16] + (uint16_t)gte_d[17] + (uint16_t)gte_d[18] + (uint16_t)gte_d[19]);
                 gte_d[24] = (int32_t)m; gte_d[7] = clampi(m >> 12, 0, 65535); break; }                       // AVSZ4
    case 0x12: g.mvmva(mx, v, cv); break;                                     // MVMVA
    case 0x28: for (int i = 0; i < 3; i++) { int64_t x = gte_d[9 + i]; g.setMAC(1 + i, g.sh(x * x)); g.setIR(1 + i, gte_d[25 + i]); } break; // SQR
    case 0x0C: {                                                              // OP
        int64_t d1 = lo16(gte_c[0]), d2 = lo16(gte_c[2]), d3 = lo16(gte_c[4]);
        int64_t i1 = gte_d[9], i2 = gte_d[10], i3 = gte_d[11];
        g.setMAC(1, g.sh(i3 * d2 - i2 * d3)); g.setMAC(2, g.sh(i1 * d3 - i3 * d1)); g.setMAC(3, g.sh(i2 * d1 - i1 * d2));
        for (int i = 0; i < 3; i++) g.setIR(1 + i, gte_d[25 + i]);
        break; }
    case 0x3D: for (int i = 0; i < 3; i++) { g.setMAC(1 + i, g.sh((int64_t)gte_d[9 + i] * gte_d[8])); g.setIR(1 + i, gte_d[25 + i]); } g.pushColor(); break; // GPF
    case 0x3E: for (int i = 0; i < 3; i++) { g.setMAC(1 + i, g.sh(((int64_t)gte_d[25 + i] << (g.sf * 12)) + (int64_t)gte_d[9 + i] * gte_d[8])); g.setIR(1 + i, gte_d[25 + i]); } g.pushColor(); break; // GPL
    case 0x1E: g.light(0); g.color(); g.pushColor(); break;                                   // NCS
    case 0x20: for (int k = 0; k < 3; k++) { g.light(k); g.color(); g.pushColor(); } break;   // NCT
    case 0x1B: case 0x3F: case 0x1C: case 0x14: case 0x16: case 0x13: case 0x2A: case 0x29: case 0x10: case 0x11: case 0x07: case 0x08: case 0x09: {
        // famiglia colore: NCCS(1B) NCCT(3F) CC(1C) CDP(14) NCDS(13) NCDT(16) DCPL(29) DPCS(10) DPCT(2A) INTPL(11)
        int32_t rgb[3];
        auto mulcol = [&]() { g.rgbc(rgb); for (int i = 0; i < 3; i++) { g.setMAC(1 + i, g.sh((int64_t)rgb[i] * gte_d[9 + i] << 4)); g.setIR(1 + i, gte_d[25 + i]); } };
        auto interp = [&]() {       // MAC = MAC + (FC - MAC) * IR0
            for (int i = 0; i < 3; i++) {
                int64_t fc = (int64_t)gte_c[21 + i] << 12;
                int64_t t = g.sh(fc - ((int64_t)gte_d[25 + i] << 12)); g.setIR(1 + i, t);
                int64_t m = ((int64_t)gte_d[9 + i] * gte_d[8]) + ((int64_t)gte_d[25 + i] << 12);
                g.setMAC(1 + i, g.sh(m)); g.setIR(1 + i, gte_d[25 + i]);
            } };
        auto dpc = [&](int32_t base[3]) {   // depth cue: MAC = base<<16 + (FC - base<<4?) * IR0
            for (int i = 0; i < 3; i++) {
                int64_t fc = (int64_t)gte_c[21 + i] << 12;
                int64_t b = (int64_t)base[i] << 16;
                int64_t t = g.sh(fc - b); g.setIR(1 + i, t);
                int64_t m = b + (int64_t)gte_d[9 + i] * gte_d[8];
                g.setMAC(1 + i, g.sh(m)); g.setIR(1 + i, gte_d[25 + i]);
            } };
        switch (cmd) {
        case 0x1B: g.light(0); g.color(); mulcol(); g.pushColor(); break;
        case 0x3F: for (int k = 0; k < 3; k++) { g.light(k); g.color(); mulcol(); g.pushColor(); } break;
        case 0x1C: g.color(); mulcol(); g.pushColor(); break;
        case 0x13: g.light(0); g.color(); mulcol(); interp(); g.pushColor(); break;
        case 0x16: for (int k = 0; k < 3; k++) { g.light(k); g.color(); mulcol(); interp(); g.pushColor(); } break;
        case 0x14: g.rgbc(rgb); mulcol(); interp(); g.pushColor(); break;
        case 0x29: mulcol(); interp(); g.pushColor(); break;
        case 0x10: g.rgbc(rgb); dpc(rgb); g.pushColor(); break;
        case 0x2A: for (int k = 0; k < 3; k++) { uint32_t cc = (uint32_t)gte_d[20]; int32_t b[3] = {(int32_t)(cc & 255), (int32_t)((cc >> 8) & 255), (int32_t)((cc >> 16) & 255)}; dpc(b); g.pushColor(); } break;
        case 0x11: for (int i = 0; i < 3; i++) { g.setMAC(1 + i, g.sh(((int64_t)gte_d[9 + i] << 12))); } interp(); g.pushColor(); break;
        default: break;
        }
        break; }
    default: {
        static std::map<int, int> unh;
        if (unh[cmd]++ < 3) std::printf("[GTE-IGNORATO] cmd=%02X op=%08X @%08X\n", cmd, in, pc - 4);
        break;
    }
    }
}

} // namespace cb
