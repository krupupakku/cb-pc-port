// =============================================================================
// Crash Bash: Remaster+ — cb_extract
// Estrattore avanzato 64-bit per CRASHBSH.DAT basato su CrashBashEx (xan1242)
// Corregge i bug di puntatori a 32-bit dell'originale e classifica automaticamente
// modelli 3D (.mdl), pacchetti texture (.tex), overlay di codice (.code.bin) e TGA.
// =============================================================================

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct BashOffset {
    uint32_t LBA;
    uint32_t size;
};

// Determina l'estensione del file analizzando l'header di Eurocom in CRASHBSH.DAT
static std::string DetectBashFileType(const uint8_t* buf, uint32_t size) {
    if (size < 4) {
        return "raw.bin";
    }

    uint16_t type = *reinterpret_cast<const uint16_t*>(buf);

    // Controlla se è un'immagine Truevision TGA (presente per schermate/debug)
    if (type == 0 && size >= 0x12) {
        const char* tgaFooter = reinterpret_cast<const char*>(buf + size - 0x12);
        if (std::strncmp(tgaFooter, "TRUEVISION-XFILE.", 17) == 0) {
            return "image.tga";
        }
    }

    // Se i bit alti sono impostati, è un code overlay MIPS (logica di un minigioco)
    if (type & 0xFF00) {
        return "code.bin";
    }

    // Classificazione basata sui magic byte Eurocom (cfr. tools/model/primitives.h)
    switch (type) {
        case 0x0001:
            return "mdl"; // Modello 3D + Animazioni (compatibile con cb_model2obj)
        case 0x0007:
            return "tex"; // Texture Pack + Palette CLUT (compatibile con cb_model2obj)
        case 0x0014:
            return "music.bin"; // Dati musicali / sequenze
        default: {
            char outType[32];
            std::snprintf(outType, sizeof(outType), "type_0x%02X.bin", type);
            return std::string(outType);
        }
    }
}

int main(int argc, char* argv[]) {
    std::cout << "=== Crash Bash: Remaster+ | Asset & Overlay Extractor (cb_extract) ===\n";

    if (argc < 6) {
        std::cout << "Utilizzo:\n"
                  << "  " << argv[0] << " <SCUS_945.70> <CRASHBSH.DAT> <OutFolder> <TableStartHex> <TableEndHex>\n\n"
                  << "Nota: Estrae tutti gli asset e gli overlay dal file CRASHBSH.DAT\n"
                  << "      rinominando automaticamente .mdl, .tex, .tga e .code.bin.\n";
        return 1;
    }

    fs::path exePath   = argv[1];
    fs::path datPath   = argv[2];
    fs::path outFolder = argv[3];
    uint32_t tableStart = static_cast<uint32_t>(std::stoul(argv[4], nullptr, 16));
    uint32_t tableEnd   = static_cast<uint32_t>(std::stoul(argv[5], nullptr, 16));

    if (tableEnd <= tableStart || (tableEnd - tableStart) % sizeof(BashOffset) != 0) {
        std::cerr << "[ERRORE] Range tabella LBA non valido!\n";
        return 1;
    }

    std::ifstream fexe(exePath, std::ios::binary | std::ios::ate);
    if (!fexe) {
        std::cerr << "[ERRORE] Impossibile aprire l'eseguibile PS1: " << exePath << "\n";
        return 1;
    }
    std::vector<uint8_t> exeBuf(static_cast<size_t>(fexe.tellg()));
    fexe.seekg(0);
    fexe.read(reinterpret_cast<char*>(exeBuf.data()), exeBuf.size());
    fexe.close();

    if (tableEnd > exeBuf.size()) {
        std::cerr << "[ERRORE] TableEnd supera la dimensione dell'eseguibile!\n";
        return 1;
    }

    std::ifstream fdat(datPath, std::ios::binary);
    if (!fdat) {
        std::cerr << "[ERRORE] Impossibile aprire l'archivio dati: " << datPath << "\n";
        return 1;
    }

    fs::create_directories(outFolder);

    const auto* offsetTable = reinterpret_cast<const BashOffset*>(exeBuf.data() + tableStart);
    uint32_t count = (tableEnd - tableStart) / sizeof(BashOffset);

    std::cout << "Trovate " << count << " voci nella tabella LBA. Estrazione in corso...\n";

    uint32_t extractedCount = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (offsetTable[i].size == 0) continue;

        std::vector<uint8_t> buf(offsetTable[i].size);
        uint64_t byteOffset = static_cast<uint64_t>(offsetTable[i].LBA) * 0x800ULL;

        fdat.seekg(static_cast<std::streamoff>(byteOffset), std::ios::beg);
        if (!fdat.read(reinterpret_cast<char*>(buf.data()), buf.size())) {
            std::cerr << "[WARN] Lettura fallita all'indice " << i << "\n";
            continue;
        }

        std::string ext = DetectBashFileType(buf.data(), offsetTable[i].size);
        fs::path outFile = outFolder / (std::to_string(i) + "." + ext);

        std::ofstream fout(outFile, std::ios::binary);
        fout.write(reinterpret_cast<const char*>(buf.data()), buf.size());
        extractedCount++;
    }

    std::cout << "[OK] Estratti con successo " << extractedCount << " file in: " << outFolder << "\n";
    return 0;
}

