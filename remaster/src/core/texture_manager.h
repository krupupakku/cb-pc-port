#pragma once
// =============================================================================
// Crash Bash: Remaster+ — High-Resolution Texture Replacement Manager
// Gestione real-time e hot-reload delle texture rimasterizzate (Approccio A)
// =============================================================================

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace cb {

struct TextureMeta {
    uint32_t crc32 = 0;
    uint16_t pack_id = 0; // 0..999, o 0xFFFF per splash screens
    uint16_t img_idx = 0; // indice texture nel pack, o ID splash
    uint16_t orig_w = 0;
    uint16_t orig_h = 0;
    uint8_t  bpp = 4;
};

struct RemasteredTexture {
    int width = 0;
    int height = 0;
    std::vector<uint32_t> rgba; // 32-bit RGBA (0xAABBGGRR in little-endian uint32)
};

struct VramBinding {
    int u0 = 0, v0 = 0;
    int u1 = 0, v1 = 0;
    const RemasteredTexture* tex = nullptr;
};

class TextureManager {
public:
    static TextureManager& Instance();

    // Inizializza leggendo l'indice precalcolato e scansionando textures/remastered/
    bool Initialize(const std::string& indexFilePath, const std::string& remasteredDirPath);

    // Ricarica tutte le texture rimasterizzate da disco (hot-reload con F7)
    void Reload();

    // Chiamato da Gpu::gp0 al termine di un trasferimento CPU->VRAM (GP0 0xA0)
    void OnVramUpload(int cx, int cy, int cw, int ch, const uint16_t* vram, int scale, std::vector<uint32_t>& hires_vram);

    // Cerca un binding attivo per una texture page e coordinate UV
    const VramBinding* FindBinding(int tpx, int tpy, int u0, int v0, int u1, int v1, int u2, int v2) const;

    // Campiona una texture rimasterizzata con coordinate normalizzate 0.0 .. 1.0
    bool Sample(const RemasteredTexture* tex, float nu, float nv, bool bilinear,
                uint8_t& outR, uint8_t& outG, uint8_t& outB, uint8_t& outA) const;

    // Statistiche e stato
    bool IsEnabled() const { return m_enabled; }
    void SetEnabled(bool en) { m_enabled = en; }
    size_t GetLoadedCount() const { return m_remasteredTextures.size(); }
    size_t GetIndexedCount() const { return m_crcMap.size(); }
    const std::string& GetRemasteredDir() const { return m_remasteredDir; }

    static uint32_t ComputeCRC32(const void* data, size_t length);

private:
    TextureManager() = default;

    bool m_enabled = true;
    std::string m_indexPath;
    std::string m_remasteredDir;

    // CRC32 -> Metadati originali (15.165 voci)
    std::unordered_map<uint32_t, TextureMeta> m_crcMap;

    // Chiave univoca ("000_tex/tex_0", "splashes/136", "<crc32_hex>") -> Immagine caricata
    std::unordered_map<std::string, RemasteredTexture> m_remasteredTextures;

    // Binding attivi per ciascuna delle 32 texture page (16 orizzontali x 2 verticali)
    std::vector<VramBinding> m_pageBindings[16][2];
};

} // namespace cb

