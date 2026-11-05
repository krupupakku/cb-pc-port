#include "core/gpu.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace cb {

static inline int sx11(uint32_t v) { return ((int)(v << 21)) >> 21; }
static inline uint32_t rd32(uint8_t* ram, uint32_t mask, uint32_t a) { uint32_t v; std::memcpy(&v, ram + (a & mask & ~3u), 4); return v; }

void Gpu::set_scale(int s) {
    int target = (s == 2 || s == 4) ? s : 1;
    if (target == scale && (target == 1 || !hires_vram.empty())) return;
    scale = target;
    if (scale > 1) {
        hires_vram.assign((size_t)(1024 * scale) * (512 * scale), 0xFF000000u);
        int maxW = 1024 * scale;
        for (int y = 0; y < 512; y++) {
            for (int x = 0; x < 1024; x++) {
                uint16_t pix = vram[y * 1024 + x];
                uint32_t r = ((pix & 31) << 3) | ((pix & 31) >> 2);
                uint32_t g = (((pix >> 5) & 31) << 3) | (((pix >> 5) >> 2) & 7);
                uint32_t b = (((pix >> 10) & 31) << 3) | (((pix >> 10) >> 2) & 7);
                uint32_t rgba = 0xFF000000u | (b << 16) | (g << 8) | r;
                for (int dy = 0; dy < scale; dy++) {
                    for (int dx = 0; dx < scale; dx++) {
                        hires_vram[(y * scale + dy) * maxW + (x * scale + dx)] = rgba;
                    }
                }
            }
        }
    } else {
        hires_vram.clear();
    }
}

uint32_t Gpu::gpustat() const {
    static uint32_t tog = 0; tog ^= 0x80000000u;
    uint32_t s = (uint32_t)(tpx & 15) | ((tpy & 1) << 4) | ((semi_mode & 3) << 5) | ((tex_depth & 3) << 7);
    s |= (dither ? 1u << 9 : 0) | (draw_to_display ? 1u << 10 : 0) | (set_mask ? 1u << 11 : 0) | (check_mask ? 1u << 12 : 0);
    s |= 1u << 13;
    int hr = hres == 256 ? 0 : hres == 320 ? 1 : hres == 512 ? 2 : hres == 640 ? 3 : 1;
    s |= (uint32_t)hr << 17;
    if (vres == 480) s |= 1u << 19;
    if (rgb24) s |= 1u << 21;
    if (!disp_enabled) s |= 1u << 23;
    s |= 0x1C000000u;
    return s | tog;
}

uint32_t Gpu::gpuread() {
    if (vram2cpu) {
        uint32_t out = 0;
        for (int i = 0; i < 2; i++) {
            int px = (rx + rpos % rw) & 1023, py = (ry + rpos / rw) & 511;
            out |= (uint32_t)vram[py * 1024 + px] << (16 * i);
            rpos++;
        }
        if (rpos >= rw * rh) vram2cpu = false;
        return out;
    }
    return 0;
}

void Gpu::gp1(uint32_t w) {
    uint32_t cmd = w >> 24;
    switch (cmd) {
    case 0x00: fifo.clear(); need = 0; cpu2vram = vram2cpu = false; disp_enabled = false;
               tpx = tpy = semi_mode = tex_depth = 0; clip_x0 = clip_y0 = 0; clip_x1 = 1023; clip_y1 = 511; off_x = off_y = 0; break;
    case 0x01: fifo.clear(); need = 0; break;
    case 0x03: disp_enabled = !(w & 1); break;
    case 0x05: disp_x = w & 0x3FF; disp_y = (w >> 10) & 0x1FF; break;
    case 0x08: {
        int h = w & 3; hres = h == 0 ? 256 : h == 1 ? 320 : h == 2 ? 512 : 640;
        if (w & 0x40) hres = 368;
        vres = (w & 4) ? 480 : 240; rgb24 = (w >> 4) & 1; break; }
    default: break;
    }
}

void Gpu::gp0(uint32_t w) {
    if (cpu2vram) {
        for (int i = 0; i < 2 && cpos < cw * ch; i++, cpos++) {
            int px = (cx + cpos % cw) & 1023, py = (cy + cpos / cw) & 511;
            uint16_t pix = (uint16_t)(w >> (16 * i));
            vram[py * 1024 + px] = pix;
            if (scale > 1 && !hires_vram.empty()) {
                uint32_t r = ((pix & 31) << 3) | ((pix & 31) >> 2);
                uint32_t g = (((pix >> 5) & 31) << 3) | (((pix >> 5) >> 2) & 7);
                uint32_t b = (((pix >> 10) & 31) << 3) | (((pix >> 10) >> 2) & 7);
                uint32_t rgba = 0xFF000000u | (b << 16) | (g << 8) | r;
                int maxW = 1024 * scale;
                for (int dy = 0; dy < scale; dy++) {
                    for (int dx = 0; dx < scale; dx++) {
                        hires_vram[(py * scale + dy) * maxW + (px * scale + dx)] = rgba;
                    }
                }
            }
        }
        if (cpos >= cw * ch) cpu2vram = false;
        return;
    }
    if (need == 0) {
        fifo.clear(); fifo.push_back(w);
        uint32_t cmd = w >> 24; polyline = false;
        if (cmd >= 0x20 && cmd < 0x40) {
            bool quad = cmd & 8, tex = cmd & 4, gour = cmd & 0x10; int n = quad ? 4 : 3;
            need = 1; for (int i = 0; i < n; i++) { need += 1 + (tex ? 1 : 0) + ((gour && i > 0) ? 1 : 0); }
        } else if (cmd >= 0x40 && cmd < 0x60) {
            bool gour = cmd & 0x10; polyline = cmd & 8; need = gour ? 4 : 3;
        } else if (cmd >= 0x60 && cmd < 0x80) {
            need = 2 + ((cmd & 4) ? 1 : 0) + (((cmd >> 3) & 3) == 0 ? 1 : 0);
        } else if (cmd == 0x02) need = 3;
        else if (cmd == 0x80) need = 4;
        else if (cmd == 0xA0 || cmd == 0xC0) need = 3;
        else need = 1;
        if (need == 1) { exec_command(); need = 0; fifo.clear(); }
        return;
    }
    fifo.push_back(w);
    if (polyline) {
        bool gour = (fifo[0] >> 28) & 1;
        bool term = (w & 0xF000F000u) == 0x50005000u && fifo.size() >= (size_t)(gour ? 4 : 3);
        if (term) { fifo.pop_back(); exec_command(); need = 0; fifo.clear(); polyline = false; }
        return;
    }
    if ((int)fifo.size() >= need) { exec_command(); need = 0; fifo.clear(); }
}

void Gpu::exec_command() {
    uint32_t cmd = fifo[0] >> 24;
    prim_count++;
    if (cmd >= 0x20 && cmd < 0x40) draw_poly(cmd & 8, cmd & 0x10, cmd & 4, cmd & 2, cmd & 1);
    else if (cmd >= 0x40 && cmd < 0x60) draw_line(cmd & 0x10, cmd & 2);
    else if (cmd >= 0x60 && cmd < 0x80) draw_rect((cmd >> 3) & 3, cmd & 4, cmd & 2, cmd & 1);
    else switch (cmd) {
    case 0x02: fill_rect(); break;
    case 0x80: copy_rect(); break;
    case 0xA0: {
        cx = fifo[1] & 0x3FF; cy = (fifo[1] >> 16) & 0x1FF;
        cw = fifo[2] & 0x3FF; ch = (fifo[2] >> 16) & 0x1FF;
        if (!cw) cw = 1024;
        if (!ch) ch = 512;
        cpos = 0; cpu2vram = true; break; }
    case 0xC0: {
        rx = fifo[1] & 0x3FF; ry = (fifo[1] >> 16) & 0x1FF;
        rw = fifo[2] & 0x3FF; rh = (fifo[2] >> 16) & 0x1FF;
        if (!rw) rw = 1024;
        if (!rh) rh = 512;
        rpos = 0; vram2cpu = true; break; }
    case 0xE1: { uint32_t w = fifo[0]; tpx = w & 15; tpy = (w >> 4) & 1; semi_mode = (w >> 5) & 3; tex_depth = (w >> 7) & 3;
                 dither = (w >> 9) & 1; draw_to_display = (w >> 10) & 1; break; }
    case 0xE2: break;
    case 0xE3: clip_x0 = fifo[0] & 0x3FF; clip_y0 = (fifo[0] >> 10) & 0x3FF; break;
    case 0xE4: clip_x1 = fifo[0] & 0x3FF; clip_y1 = (fifo[0] >> 10) & 0x3FF; break;
    case 0xE5: off_x = sx11(fifo[0] & 0x7FF); off_y = sx11((fifo[0] >> 11) & 0x7FF); break;
    case 0xE6: set_mask = fifo[0] & 1; check_mask = (fifo[0] >> 1) & 1; break;
    default: break;
    }
}

void Gpu::fill_rect() {
    uint32_t c = fifo[0]; int r = c & 255, g = (c >> 8) & 255, b = (c >> 16) & 255;
    uint16_t px = (uint16_t)((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10));
    int x = fifo[1] & 0x3F0, y = (fifo[1] >> 16) & 0x1FF;
    int w = (((fifo[2] & 0x3FF) + 0xF) & ~0xF), h = (fifo[2] >> 16) & 0x1FF;
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) vram[((y + j) & 511) * 1024 + ((x + i) & 1023)] = px;
    if (scale > 1 && !hires_vram.empty()) {
        uint32_t rgba = 0xFF000000u | (b << 16) | (g << 8) | r;
        int maxW = 1024 * scale, maxH = 512 * scale;
        int hx = x * scale, hy = y * scale, hw = w * scale, hh = h * scale;
        for (int j = 0; j < hh; j++) {
            int py = (hy + j) % maxH;
            for (int i = 0; i < hw; i++) {
                int px_pos = (hx + i) % maxW;
                hires_vram[py * maxW + px_pos] = rgba;
            }
        }
    }
}

void Gpu::copy_rect() {
    int sx = fifo[1] & 0x3FF, sy = (fifo[1] >> 16) & 0x1FF, dx = fifo[2] & 0x3FF, dy = (fifo[2] >> 16) & 0x1FF;
    int w = fifo[3] & 0x3FF, h = (fifo[3] >> 16) & 0x1FF; if (!w) w = 1024; if (!h) h = 512;
    std::vector<uint16_t> tmp((size_t)w * h);
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) tmp[j * w + i] = vram[((sy + j) & 511) * 1024 + ((sx + i) & 1023)];
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) vram[((dy + j) & 511) * 1024 + ((dx + i) & 1023)] = tmp[j * w + i];

    if (scale > 1 && !hires_vram.empty()) {
        int maxW = 1024 * scale, maxH = 512 * scale;
        int hsx = sx * scale, hsy = sy * scale, hdx = dx * scale, hdy = dy * scale;
        int hw = w * scale, hh = h * scale;
        std::vector<uint32_t> htmp((size_t)hw * hh);
        for (int j = 0; j < hh; j++) for (int i = 0; i < hw; i++)
            htmp[j * hw + i] = hires_vram[((hsy + j) % maxH) * maxW + ((hsx + i) % maxW)];
        for (int j = 0; j < hh; j++) for (int i = 0; i < hw; i++)
            hires_vram[((hdy + j) % maxH) * maxW + ((hdx + i) % maxW)] = htmp[j * hw + i];
    }
}

void Gpu::put_pixel(int x, int y, int r, int g, int b, bool semi, int smode, bool texsemi) {
    if (x < clip_x0 || x > clip_x1 || y < clip_y0 || y > clip_y1 || x < 0 || x > 1023 || y < 0 || y > 511) return;
    uint16_t& dst = vram[y * 1024 + x];
    if (check_mask && (dst & 0x8000)) return;
    if (semi && texsemi) {
        int br = (dst & 31) << 3, bg = ((dst >> 5) & 31) << 3, bb = ((dst >> 10) & 31) << 3;
        switch (smode) {
        case 0: r = (br + r) >> 1; g = (bg + g) >> 1; b = (bb + b) >> 1; break;
        case 1: r = br + r; g = bg + g; b = bb + b; break;
        case 2: r = br - r; g = bg - g; b = bb - b; break;
        default: r = br + (r >> 2); g = bg + (g >> 2); b = bb + (b >> 2); break;
        }
        r = std::clamp(r, 0, 255); g = std::clamp(g, 0, 255); b = std::clamp(b, 0, 255);
    }
    dst = (uint16_t)((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | (set_mask ? 0x8000 : 0));
}

uint16_t Gpu::fetch_tex(int u, int v, uint16_t clut, int px, int py, int depth) {
    int bx = px * 64, by = py * 256;
    u &= 255; v &= 255;
    if (depth == 0) {
        uint16_t t = vram[(by + v) * 1024 + ((bx + (u >> 2)) & 1023)];
        int idx = (t >> ((u & 3) * 4)) & 15;
        return vram[((clut >> 6) & 511) * 1024 + (((clut & 63) * 16 + idx) & 1023)];
    } else if (depth == 1) {
        uint16_t t = vram[(by + v) * 1024 + ((bx + (u >> 1)) & 1023)];
        int idx = (t >> ((u & 1) * 8)) & 255;
        return vram[((clut >> 6) & 511) * 1024 + (((clut & 63) * 16 + idx) & 1023)];
    }
    return vram[(by + v) * 1024 + ((bx + u) & 1023)];
}

void Gpu::put_pixel_hires(int x, int y, int r, int g, int b, bool semi, int smode, bool texsemi) {
    int maxW = 1024 * scale, maxH = 512 * scale;
    if (x < clip_x0 * scale || x > clip_x1 * scale || y < clip_y0 * scale || y > clip_y1 * scale ||
        x < 0 || x >= maxW || y < 0 || y >= maxH) return;

    uint32_t& dst = hires_vram[y * maxW + x];
    if (semi && texsemi) {
        int br = dst & 255, bg = (dst >> 8) & 255, bb = (dst >> 16) & 255;
        switch (smode) {
        case 0: r = (br + r) >> 1; g = (bg + g) >> 1; b = (bb + b) >> 1; break;
        case 1: r = br + r; g = bg + g; b = bb + b; break;
        case 2: r = br - r; g = bg - g; b = bb - b; break;
        default: r = br + (r >> 2); g = bg + (g >> 2); b = bb + (b >> 2); break;
        }
        r = std::clamp(r, 0, 255); g = std::clamp(g, 0, 255); b = std::clamp(b, 0, 255);
    }
    dst = 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

uint16_t Gpu::fetch_tex_bilinear(float uf, float vf, uint16_t clut, int px, int py, int depth) {
    int u0 = (int)uf, v0 = (int)vf;
    float fu = uf - (float)u0, fv = vf - (float)v0;
    int u1 = (u0 + 1) & 255, v1 = (v0 + 1) & 255;
    u0 &= 255; v0 &= 255;
    uint16_t t00 = fetch_tex(u0, v0, clut, px, py, depth);
    if (!bilinear_tex || t00 == 0) return t00;
    uint16_t t10 = fetch_tex(u1, v0, clut, px, py, depth);
    uint16_t t01 = fetch_tex(u0, v1, clut, px, py, depth);
    uint16_t t11 = fetch_tex(u1, v1, clut, px, py, depth);
    if (t10 == 0) t10 = t00;
    if (t01 == 0) t01 = t00;
    if (t11 == 0) t11 = t00;

    auto unpack = [](uint16_t t, int& r, int& g, int& b) {
        r = (t & 31) << 3; g = ((t >> 5) & 31) << 3; b = ((t >> 10) & 31) << 3;
    };
    int r00, g00, b00, r10, g10, b10, r01, g01, b01, r11, g11, b11;
    unpack(t00, r00, g00, b00); unpack(t10, r10, g10, b10);
    unpack(t01, r01, g01, b01); unpack(t11, r11, g11, b11);

    float w00 = (1.0f - fu) * (1.0f - fv);
    float w10 = fu * (1.0f - fv);
    float w01 = (1.0f - fu) * fv;
    float w11 = fu * fv;

    int r = (int)(r00 * w00 + r10 * w10 + r01 * w01 + r11 * w11);
    int g = (int)(g00 * w00 + g10 * w10 + g01 * w01 + g11 * w11);
    int b = (int)(b00 * w00 + b10 * w10 + b01 * w01 + b11 * w11);

    return (uint16_t)((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | (t00 & 0x8000));
}

void Gpu::raster_tri_hires(const V& a0, const V& b0, const V& c0, bool gouraud, bool textured, bool semi, bool raw,
                           uint16_t clut, int ctpx, int ctpy, int cdepth, int csemi) {
    int s = scale;
    V a = a0, b = b0, c = c0;
    a.x *= s; a.y *= s;
    b.x *= s; b.y *= s;
    c.x *= s; c.y *= s;

    auto edge = [](const V& p, const V& q, int x, int y) {
        return (long long)(q.x - p.x) * (y - p.y) - (long long)(q.y - p.y) * (x - p.x);
    };
    long long area = edge(a, b, c.x, c.y);
    if (area == 0) return;
    if (area < 0) { std::swap(b, c); area = -area; }

    int maxW = 1024 * s, maxH = 512 * s;
    int minx = std::max({std::min({a.x, b.x, c.x}), clip_x0 * s, 0});
    int maxx = std::min({std::max({a.x, b.x, c.x}), clip_x1 * s, maxW - 1});
    int miny = std::max({std::min({a.y, b.y, c.y}), clip_y0 * s, 0});
    int maxy = std::min({std::max({a.y, b.y, c.y}), clip_y1 * s, maxH - 1});
    if (std::max({a.x, b.x, c.x}) - std::min({a.x, b.x, c.x}) > maxW ||
        std::max({a.y, b.y, c.y}) - std::min({a.y, b.y, c.y}) > maxH) return;

    auto bias = [](const V& p, const V& q) {
        int dy = q.y - p.y, dx = q.x - p.x;
        return (dy > 0 || (dy == 0 && dx < 0)) ? 0 : -1;
    };
    int b0b = bias(a, b), b1b = bias(b, c), b2b = bias(c, a);

    float invArea = 1.0f / (float)area;

    for (int y = miny; y <= maxy; y++) {
        for (int x = minx; x <= maxx; x++) {
            long long w2 = edge(a, b, x, y), w0 = edge(b, c, x, y), w1 = edge(c, a, x, y);
            if (w2 + b0b < 0 || w0 + b1b < 0 || w1 + b2b < 0) continue;
            float fa = (float)w0 * invArea, fb = (float)w1 * invArea, fc = (float)w2 * invArea;
            int r, g, bl;
            if (gouraud) {
                r = (int)(a.r * fa + b.r * fb + c.r * fc);
                g = (int)(a.g * fa + b.g * fb + c.g * fc);
                bl = (int)(a.b * fa + b.b * fb + c.b * fc);
            } else {
                r = a.r; g = a.g; bl = a.b;
            }
            bool texsemi = true;
            if (textured) {
                float uf = a.u * fa + b.u * fb + c.u * fc;
                float vf = a.v * fa + b.v * fb + c.v * fc;
                uint16_t t = bilinear_tex ? fetch_tex_bilinear(uf, vf, clut, ctpx, ctpy, cdepth)
                                          : fetch_tex((int)(uf + 0.5f), (int)(vf + 0.5f), clut, ctpx, ctpy, cdepth);
                if (t == 0) continue;
                int tr = (t & 31) << 3, tg = ((t >> 5) & 31) << 3, tb = ((t >> 10) & 31) << 3;
                if (!raw) {
                    tr = std::min(255, (tr * r) >> 7);
                    tg = std::min(255, (tg * g) >> 7);
                    tb = std::min(255, (tb * bl) >> 7);
                }
                r = tr; g = tg; bl = tb;
                texsemi = (t & 0x8000) != 0;
            }
            put_pixel_hires(x, y, std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(bl, 0, 255), semi, csemi, textured ? texsemi : true);
        }
    }
}

void Gpu::raster_tri(const V& a0, const V& b0, const V& c0, bool gouraud, bool textured, bool semi, bool raw,
                     uint16_t clut, int ctpx, int ctpy, int cdepth, int csemi) {
    V a = a0, b = b0, c = c0;
    auto edge = [](const V& p, const V& q, int x, int y) { return (long long)(q.x - p.x) * (y - p.y) - (long long)(q.y - p.y) * (x - p.x); };
    long long area = edge(a, b, c.x, c.y);
    if (area == 0) return;
    if (area < 0) { std::swap(b, c); area = -area; }
    int minx = std::max({std::min({a.x, b.x, c.x}), clip_x0, 0}), maxx = std::min({std::max({a.x, b.x, c.x}), clip_x1, 1023});
    int miny = std::max({std::min({a.y, b.y, c.y}), clip_y0, 0}), maxy = std::min({std::max({a.y, b.y, c.y}), clip_y1, 511});
    if (std::max({a.x, b.x, c.x}) - std::min({a.x, b.x, c.x}) > 1023 || std::max({a.y, b.y, c.y}) - std::min({a.y, b.y, c.y}) > 511) return;
    auto bias = [](const V& p, const V& q) { int dy = q.y - p.y, dx = q.x - p.x; return (dy > 0 || (dy == 0 && dx < 0)) ? 0 : -1; };
    int b0b = bias(a, b), b1b = bias(b, c), b2b = bias(c, a);
    for (int y = miny; y <= maxy; y++) for (int x = minx; x <= maxx; x++) {
        long long w2 = edge(a, b, x, y), w0 = edge(b, c, x, y), w1 = edge(c, a, x, y);
        if (w2 + b0b < 0 || w0 + b1b < 0 || w1 + b2b < 0) continue;
        float fa = (float)w0 / area, fb = (float)w1 / area, fc = (float)w2 / area;
        int r, g, bl;
        if (gouraud) { r = (int)(a.r * fa + b.r * fb + c.r * fc); g = (int)(a.g * fa + b.g * fb + c.g * fc); bl = (int)(a.b * fa + b.b * fb + c.b * fc); }
        else { r = a.r; g = a.g; bl = a.b; }
        bool texsemi = true;
        if (textured) {
            int u = (int)(a.u * fa + b.u * fb + c.u * fc + 0.5f), v = (int)(a.v * fa + b.v * fb + c.v * fc + 0.5f);
            uint16_t t = fetch_tex(u, v, clut, ctpx, ctpy, cdepth);
            if (t == 0) continue;
            int tr = (t & 31) << 3, tg = ((t >> 5) & 31) << 3, tb = ((t >> 10) & 31) << 3;
            if (!raw) { tr = std::min(255, (tr * r) >> 7); tg = std::min(255, (tg * g) >> 7); tb = std::min(255, (tb * bl) >> 7); }
            r = tr; g = tg; bl = tb; texsemi = (t & 0x8000) != 0;
        }
        put_pixel(x, y, std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(bl, 0, 255), semi, csemi, textured ? texsemi : true);
    }
}

void Gpu::draw_poly(bool quad, bool gouraud, bool textured, bool semi, bool raw) {
    int n = quad ? 4 : 3; V v[4]; int idx = 0; uint16_t clut = 0; int ctpx = tpx, ctpy = tpy, cdepth = tex_depth, csemi = semi_mode;
    for (int i = 0; i < n; i++) {
        uint32_t col = (i == 0 || !gouraud) ? fifo[0] : fifo[idx++];
        if (i == 0) idx = 1;
        uint32_t vw = fifo[idx++];
        v[i].r = col & 255; v[i].g = (col >> 8) & 255; v[i].b = (col >> 16) & 255;
        if (!gouraud) { uint32_t c0 = fifo[0]; v[i].r = c0 & 255; v[i].g = (c0 >> 8) & 255; v[i].b = (c0 >> 16) & 255; }
        v[i].x = sx11(vw & 0x7FF) + off_x; v[i].y = sx11((vw >> 16) & 0x7FF) + off_y;
        if (textured) {
            uint32_t uv = fifo[idx++]; v[i].u = uv & 255; v[i].v = (uv >> 8) & 255;
            if (i == 0) clut = (uint16_t)(uv >> 16);
            if (i == 1) { uint32_t tp = uv >> 16; ctpx = tp & 15; ctpy = (tp >> 4) & 1; csemi = (tp >> 5) & 3; cdepth = (tp >> 7) & 3;
                          tpx = ctpx; tpy = ctpy; semi_mode = csemi; tex_depth = cdepth; }
        }
    }
    raster_tri(v[0], v[1], v[2], gouraud, textured, semi, raw, clut, ctpx, ctpy, cdepth, csemi);
    if (quad) raster_tri(v[1], v[2], v[3], gouraud, textured, semi, raw, clut, ctpx, ctpy, cdepth, csemi);
    if (scale > 1 && !hires_vram.empty()) {
        raster_tri_hires(v[0], v[1], v[2], gouraud, textured, semi, raw, clut, ctpx, ctpy, cdepth, csemi);
        if (quad) raster_tri_hires(v[1], v[2], v[3], gouraud, textured, semi, raw, clut, ctpx, ctpy, cdepth, csemi);
    }
}

void Gpu::draw_rect(int sizeMode, bool textured, bool semi, bool raw) {
    uint32_t col = fifo[0]; int r = col & 255, g = (col >> 8) & 255, b = (col >> 16) & 255;
    uint32_t vw = fifo[1]; int x = sx11(vw & 0x7FF) + off_x, y = sx11((vw >> 16) & 0x7FF) + off_y;
    int i = 2; int u0 = 0, v0 = 0; uint16_t clut = 0;
    if (textured) { uint32_t uv = fifo[i++]; u0 = uv & 255; v0 = (uv >> 8) & 255; clut = (uint16_t)(uv >> 16); }
    int w, h;
    if (sizeMode == 0) { uint32_t s = fifo[i++]; w = s & 0x3FF; h = (s >> 16) & 0x1FF; }
    else if (sizeMode == 1) w = h = 1; else if (sizeMode == 2) w = h = 8; else w = h = 16;
    for (int j = 0; j < h; j++) for (int k = 0; k < w; k++) {
        int pr = r, pg = g, pb = b; bool texsemi = true;
        if (textured) {
            uint16_t t = fetch_tex(u0 + k, v0 + j, clut, tpx, tpy, tex_depth);
            if (t == 0) continue;
            int tr = (t & 31) << 3, tg = ((t >> 5) & 31) << 3, tb = ((t >> 10) & 31) << 3;
            if (!raw) { tr = std::min(255, (tr * r) >> 7); tg = std::min(255, (tg * g) >> 7); tb = std::min(255, (tb * b) >> 7); }
            pr = tr; pg = tg; pb = tb; texsemi = (t & 0x8000) != 0;
        }
        put_pixel(x + k, y + j, pr, pg, pb, semi, semi_mode, texsemi);
        if (scale > 1 && !hires_vram.empty()) {
            for (int dy = 0; dy < scale; dy++) {
                for (int dx = 0; dx < scale; dx++) {
                    put_pixel_hires((x + k) * scale + dx, (y + j) * scale + dy, pr, pg, pb, semi, semi_mode, texsemi);
                }
            }
        }
    }
}

void Gpu::draw_line(bool gouraud, bool semi) {
    // Polyline: fifo = cmd, v0, [c1, v1]... oppure cmd, v0, v1, ...
    size_t i = 1; uint32_t c0 = fifo[0];
    auto vtx = [&](uint32_t w, int& x, int& y) { x = sx11(w & 0x7FF) + off_x; y = sx11((w >> 16) & 0x7FF) + off_y; };
    int x0, y0; vtx(fifo[i++], x0, y0); uint32_t col0 = c0;
    while (i < fifo.size()) {
        uint32_t col1 = col0; if (gouraud) col1 = fifo[i++];
        if (i >= fifo.size()) break;
        int x1, y1; vtx(fifo[i++], x1, y1);
        int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0), steps = std::max(dx, dy);
        for (int s = 0; s <= steps; s++) {
            float t = steps ? (float)s / steps : 0;
            int x = x0 + (int)((x1 - x0) * t + (x1 >= x0 ? 0.5f : -0.5f)), y = y0 + (int)((y1 - y0) * t + (y1 >= y0 ? 0.5f : -0.5f));
            int r = (int)((col0 & 255) * (1 - t) + (col1 & 255) * t), g = (int)(((col0 >> 8) & 255) * (1 - t) + ((col1 >> 8) & 255) * t), b = (int)(((col0 >> 16) & 255) * (1 - t) + ((col1 >> 16) & 255) * t);
            put_pixel(x, y, r, g, b, semi, semi_mode, true);
        }
        x0 = x1; y0 = y1; col0 = col1;
    }
}

void Gpu::dma_linked_list(uint8_t* ram, uint32_t mask, uint32_t addr) {
    uint32_t node = addr & mask & ~3u; int guard = 0;
    while (guard++ < 200000) {
        uint32_t hdr = rd32(ram, mask, node);
        uint32_t cnt = hdr >> 24;
        for (uint32_t i = 0; i < cnt; i++) gp0(rd32(ram, mask, node + 4 + i * 4));
        uint32_t next = hdr & 0xFFFFFF;
        if (next == 0xFFFFFF) break;
        node = next & mask & ~3u;
    }
}

void Gpu::dma_block(uint8_t* ram, uint32_t mask, uint32_t addr, uint32_t words) {
    for (uint32_t i = 0; i < words; i++) gp0(rd32(ram, mask, addr + i * 4));
}

} // namespace cb

