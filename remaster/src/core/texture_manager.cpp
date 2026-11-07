#include "texture_manager.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <fstream>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace fs = std::filesystem;

namespace cb {

// Tabella CRC32 (polinomio standard IEEE 802.3 0xEDB88320)
static uint32_t s_crcTable[256];
static bool s_crcInit = false;

static void InitCrcTable() {
    if (s_crcInit) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) {
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        s_crcTable[i] = c;
    }
    s_crcInit = true;
}

uint32_t TextureManager::ComputeCRC32(const void* data, size_t length) {
    InitCrcTable();
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < length; i++) {
        crc = s_crcTable[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

TextureManager& TextureManager::Instance() {
    static TextureManager s_instance;
    return s_instance;
}

bool TextureManager::Initialize(const std::string& indexFilePath, const std::string& remasteredDirPath) {
    m_indexPath = indexFilePath;
    m_remasteredDir = remasteredDirPath;

    InitCrcTable();
    m_crcMap.clear();

    // 1. Carica l'indice binario generato (textures_index.dat)
    std::ifstream idxFile(indexFilePath, std::ios::binary);
    if (idxFile.is_open()) {
        char magic[4] = {0};
        idxFile.read(magic, 4);
        if (std::memcmp(magic, "CBTX", 4) == 0) {
            uint32_t count = 0;
            idxFile.read(reinterpret_cast<char*>(&count), sizeof(count));
            for (uint32_t i = 0; i < count; i++) {
                uint32_t crc = 0;
                uint16_t pack = 0, img = 0, w = 0, h = 0;
                uint8_t bpp = 0, pad = 0;
                idxFile.read(reinterpret_cast<char*>(&crc), 4);
                idxFile.read(reinterpret_cast<char*>(&pack), 2);
                idxFile.read(reinterpret_cast<char*>(&img), 2);
                idxFile.read(reinterpret_cast<char*>(&w), 2);
                idxFile.read(reinterpret_cast<char*>(&h), 2);
                idxFile.read(reinterpret_cast<char*>(&bpp), 1);
                idxFile.read(reinterpret_cast<char*>(&pad), 1);

                TextureMeta meta;
                meta.crc32 = crc;
                meta.pack_id = pack;
                meta.img_idx = img;
                meta.orig_w = w;
                meta.orig_h = h;
                meta.bpp = bpp;
                m_crcMap[crc] = meta;
            }
            std::printf("[TextureManager] Caricati %zu metadati da %s\n", m_crcMap.size(), indexFilePath.c_str());
        }
        idxFile.close();
    } else {
        std::printf("[TextureManager] Indice non trovato: %s (sostituzione basata solo su hash VRAM)\n", indexFilePath.c_str());
    }

    // 2. Carica le texture rimasterizzate
    Reload();
    return true;
}

void TextureManager::Reload() {
    m_remasteredTextures.clear();
    for (int x = 0; x < 16; x++) {
        for (int y = 0; y < 2; y++) {
            m_pageBindings[x][y].clear();
        }
    }

    if (m_remasteredDir.empty() || !fs::exists(m_remasteredDir)) {
        return;
    }

    size_t loadedCount = 0;
    try {
        for (const auto& entry : fs::recursive_directory_iterator(m_remasteredDir)) {
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
                fs::path rel = fs::relative(entry.path(), m_remasteredDir);
                std::string key = rel.string();
                std::replace(key.begin(), key.end(), '\\', '/');
                if (key.length() > ext.length()) {
                    key = key.substr(0, key.length() - ext.length());
                }

                int w = 0, h = 0, comp = 0;
                stbi_uc* data = stbi_load(entry.path().string().c_str(), &w, &h, &comp, 4);
                if (data && w > 0 && h > 0) {
                    RemasteredTexture rt;
                    rt.width = w;
                    rt.height = h;
                    rt.rgba.resize(w * h);
                    std::memcpy(rt.rgba.data(), data, w * h * 4);
                    stbi_image_free(data);

                    m_remasteredTextures[key] = std::move(rt);
                    loadedCount++;
                }
            }
        }
    } catch (const std::exception& e) {
        std::printf("[TextureManager] Errore scansione %s: %s\n", m_remasteredDir.c_str(), e.what());
    }

    if (loadedCount > 0) {
        std::printf("[TextureManager] HD Pack Attivo: caricate %zu texture rimasterizzate da %s\n",
                    loadedCount, m_remasteredDir.c_str());
    }
}

void TextureManager::OnVramUpload(int cx, int cy, int cw, int ch, const uint16_t* vram, int scale, std::vector<uint32_t>& hires_vram) {
    if (!m_enabled || m_remasteredTextures.empty() || cw <= 0 || ch <= 0 || !vram) {
        return;
    }

    // Estrai i byte trasferiti in VRAM per calcolare il CRC32
    std::vector<uint8_t> bytes(cw * ch * 2);
    for (int y = 0; y < ch; y++) {
        int vy = (cy + y) & 511;
        const uint16_t* row = &vram[vy * 1024];
        for (int x = 0; x < cw; x++) {
            int vx = (cx + x) & 1023;
            uint16_t pix = row[vx];
            bytes[(y * cw + x) * 2 + 0] = static_cast<uint8_t>(pix & 0xFF);
            bytes[(y * cw + x) * 2 + 1] = static_cast<uint8_t>(pix >> 8);
        }
    }
    uint32_t crc = ComputeCRC32(bytes.data(), bytes.size());
    if (crc == 0x05FE99DD) {
        FILE* f = std::fopen("build/upload_138.bin", "wb");
        if (f) { std::fwrite(bytes.data(), 1, bytes.size(), f); std::fclose(f); }
    }

    // Cerca se corrisponde ad un metadato noto
    auto itMeta = m_crcMap.find(crc);
    if (itMeta == m_crcMap.end()) {
        // Controlla se l'utente ha inserito direttamente un file con nome <CRC32>.png
        char hexBuf[16];
        std::snprintf(hexBuf, sizeof(hexBuf), "%08X", crc);
        auto itCrcTex = m_remasteredTextures.find(hexBuf);
        if (itCrcTex != m_remasteredTextures.end()) {
            int tpx = (cx / 64) & 15;
            int tpy = (cy / 256) & 1;
            int u0 = (cx % 64) * 4;
            int v0 = cy % 256;
            int u1 = u0 + cw * 4;
            int v1 = v0 + ch;
            m_pageBindings[tpx][tpy].push_back(VramBinding{u0, v0, u1, v1, &itCrcTex->second});
        }
        return;
    }

    const TextureMeta& meta = itMeta->second;

    // Caso 1: Splash Screen / Schermata pre-renderizzata a pieno schermo (pack_id == 0xFFFF)
    if (meta.pack_id == 0xFFFF) {
        char splashKey[64];
        std::snprintf(splashKey, sizeof(splashKey), "splashes/%d", meta.img_idx);
        auto itSplash = m_remasteredTextures.find(splashKey);
        if (itSplash != m_remasteredTextures.end()) {
            const RemasteredTexture& st = itSplash->second;
            // 1. Inietta nel buffer ad alta risoluzione (hires_vram) se attivo
            if (scale > 1 && !hires_vram.empty()) {
                int maxW = 1024 * scale;
                int targetX = cx * scale;
                int targetY = cy * scale;
                int targetW = cw * scale;
                int targetH = ch * scale;

                for (int dy = 0; dy < targetH; dy++) {
                    int vy = (targetY + dy);
                    if (vy < 0 || vy >= 512 * scale) continue;
                    float nv = static_cast<float>(dy) / static_cast<float>(targetH);
                    for (int dx = 0; dx < targetW; dx++) {
                        int vx = (targetX + dx);
                        if (vx < 0 || vx >= maxW) continue;
                        float nu = static_cast<float>(dx) / static_cast<float>(targetW);
                        uint8_t r, g, b, a;
                        if (Sample(&st, nu, nv, true, r, g, b, a)) {
                            hires_vram[vy * maxW + vx] = 0xFF000000u | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(r);
                        }
                    }
                }
            }
            // 2. Aggiorna anche vram a 16-bit per garantire la visibilita' a qualunque risoluzione
            uint16_t* mutableVram = const_cast<uint16_t*>(vram);
            for (int dy = 0; dy < ch; dy++) {
                int vy = (cy + dy) & 511;
                float nv = static_cast<float>(dy) / static_cast<float>(ch);
                for (int dx = 0; dx < cw; dx++) {
                    int vx = (cx + dx) & 1023;
                    float nu = static_cast<float>(dx) / static_cast<float>(cw);
                    uint8_t r, g, b, a;
                    if (Sample(&st, nu, nv, true, r, g, b, a)) {
                        uint16_t pix1555 = ((r >> 3) & 31) | (((g >> 3) & 31) << 5) | (((b >> 3) & 31) << 10);
                        mutableVram[vy * 1024 + vx] = pix1555;
                    }
                }
            }
            std::printf("[TextureManager] Sostituito splash screen %s con versione HD (%dx%d) in hires_vram e VRAM!\n",
                        splashKey, st.width, st.height);
        }
        return;
    }

    // Caso 2: Texture di modello 3D o mondo di gioco
    char key[64];
    std::snprintf(key, sizeof(key), "%03d_tex/tex_%d", meta.pack_id, meta.img_idx);
    auto itTex = m_remasteredTextures.find(key);
    if (itTex == m_remasteredTextures.end()) {
        return;
    }

    int tpx = (cx / 64) & 15;
    int tpy = (cy / 256) & 1;
    int mult = (meta.bpp == 8) ? 2 : (meta.bpp == 16) ? 1 : 4;
    int u0 = (cx % 64) * mult;
    int v0 = cy % 256;
    int u1 = u0 + meta.orig_w;
    int v1 = v0 + meta.orig_h;

    // Rimuovi eventuali vecchi binding obsoleti nella stessa area
    auto& list = m_pageBindings[tpx][tpy];
    list.erase(std::remove_if(list.begin(), list.end(), [u0, v0, u1, v1](const VramBinding& b) {
        return (b.u0 >= u0 && b.u1 <= u1 && b.v0 >= v0 && b.v1 <= v1);
    }), list.end());

    list.push_back(VramBinding{u0, v0, u1, v1, &itTex->second});
    // std::printf("[TextureManager] Attivato HD binding: %s -> tpage(%d,%d) UV[%d,%d - %d,%d] (HD %dx%d)\n",
    //             key, tpx, tpy, u0, v0, u1, v1, itTex->second.width, itTex->second.height);
}

const VramBinding* TextureManager::FindBinding(int tpx, int tpy, int u0, int v0, int u1, int v1, int u2, int v2) const {
    if (!m_enabled) return nullptr;
    tpx &= 15;
    tpy &= 1;
    const auto& list = m_pageBindings[tpx][tpy];
    if (list.empty()) return nullptr;

    int min_u = std::min({u0, u1, u2});
    int max_u = std::max({u0, u1, u2});
    int min_v = std::min({v0, v1, v2});
    int max_v = std::max({v0, v1, v2});

    for (const auto& b : list) {
        if (min_u >= b.u0 && max_u <= b.u1 && min_v >= b.v0 && max_v <= b.v1) {
            return &b;
        }
    }

    int mid_u = (u0 + u1 + u2) / 3;
    int mid_v = (v0 + v1 + v2) / 3;
    for (const auto& b : list) {
        if (mid_u >= b.u0 && mid_u <= b.u1 && mid_v >= b.v0 && mid_v <= b.v1) {
            return &b;
        }
    }

    return nullptr;
}

bool TextureManager::Sample(const RemasteredTexture* tex, float nu, float nv, bool bilinear,
                            uint8_t& outR, uint8_t& outG, uint8_t& outB, uint8_t& outA) const {
    if (!tex || tex->rgba.empty() || tex->width <= 0 || tex->height <= 0) return false;
    nu = std::clamp(nu, 0.0f, 1.0f);
    nv = std::clamp(nv, 0.0f, 1.0f);
    int w = tex->width, h = tex->height;

    if (bilinear) {
        float fx = nu * static_cast<float>(w - 1);
        float fy = nv * static_cast<float>(h - 1);
        int x0 = static_cast<int>(fx);
        int y0 = static_cast<int>(fy);
        int x1 = std::min(x0 + 1, w - 1);
        int y1 = std::min(y0 + 1, h - 1);
        float wx1 = fx - static_cast<float>(x0);
        float wx0 = 1.0f - wx1;
        float wy1 = fy - static_cast<float>(y0);
        float wy0 = 1.0f - wy1;

        uint32_t c00 = tex->rgba[y0 * w + x0];
        uint32_t c10 = tex->rgba[y0 * w + x1];
        uint32_t c01 = tex->rgba[y1 * w + x0];
        uint32_t c11 = tex->rgba[y1 * w + x1];

        float w00 = wx0 * wy0, w10 = wx1 * wy0, w01 = wx0 * wy1, w11 = wx1 * wy1;
        outR = static_cast<uint8_t>((c00 & 0xFF) * w00 + (c10 & 0xFF) * w10 + (c01 & 0xFF) * w01 + (c11 & 0xFF) * w11 + 0.5f);
        outG = static_cast<uint8_t>(((c00 >> 8) & 0xFF) * w00 + (((c10 >> 8) & 0xFF)) * w10 + (((c01 >> 8) & 0xFF)) * w01 + (((c11 >> 8) & 0xFF)) * w11 + 0.5f);
        outB = static_cast<uint8_t>(((c00 >> 16) & 0xFF) * w00 + (((c10 >> 16) & 0xFF)) * w10 + (((c01 >> 16) & 0xFF)) * w01 + (((c11 >> 16) & 0xFF)) * w11 + 0.5f);
        outA = static_cast<uint8_t>(((c00 >> 24) & 0xFF) * w00 + (((c10 >> 24) & 0xFF)) * w10 + (((c01 >> 24) & 0xFF)) * w01 + (((c11 >> 24) & 0xFF)) * w11 + 0.5f);
    } else {
        int x = std::clamp(static_cast<int>(nu * static_cast<float>(w)), 0, w - 1);
        int y = std::clamp(static_cast<int>(nv * static_cast<float>(h)), 0, h - 1);
        uint32_t c = tex->rgba[y * w + x];
        outR = static_cast<uint8_t>(c & 0xFF);
        outG = static_cast<uint8_t>((c >> 8) & 0xFF);
        outB = static_cast<uint8_t>((c >> 16) & 0xFF);
        outA = static_cast<uint8_t>((c >> 24) & 0xFF);
    }
    return true;
}

} // namespace cb
