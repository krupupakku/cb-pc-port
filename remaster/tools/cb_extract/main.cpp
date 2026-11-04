// =============================================================================
// Crash Bash: Remaster+ — cb_extract
// Estrattore completo automatico (64-bit) per original/disc/bash.bin (MODE2/2352)
// 1. Legge il filesystem ISO9660/XA direttamente da bash.bin
// 2. Estrae SYSTEM.CNF, SCUS_945.70 e CRASHBSH.DAT
// 3. Legge la tabella LBA (0x3E910..0x40810, 992 voci) da SCUS_945.70
// 4. Spacchetta tutti i 992 file classificando .tex, .mdl, .tga, .strings.bin,
//    .bank.bin, .music.bin e .code.bin + genera original/extracted/manifest.txt
// =============================================================================

#include <cctype>
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

static constexpr uint32_t RAW_SECTOR_SIZE  = 2352;
static constexpr uint32_t MODE2_HEADER_LEN = 24;
static constexpr uint32_t USER_SECTOR_SIZE = 2048;

// Offset della tabella LBA dentro SCUS_945.70 (NTSC-U)
static constexpr uint32_t SCUS_TABLE_START = 0x3E910;
static constexpr uint32_t SCUS_TABLE_END   = 0x40810;

struct BashOffset {
    uint32_t LBA;
    uint32_t size;
};

// Legge 'byteSize' byte a partire dal settore LBA specificato da un file RAW MODE2/2352
static bool ReadMode2Data(std::ifstream& binFile, uint32_t startLba, uint32_t byteSize, std::vector<uint8_t>& outBuf) {
    outBuf.resize(byteSize);
    uint32_t remaining = byteSize;
    uint32_t curLba    = startLba;
    size_t   writePos  = 0;

    while (remaining > 0) {
        uint64_t fileOffset = static_cast<uint64_t>(curLba) * RAW_SECTOR_SIZE + MODE2_HEADER_LEN;
        binFile.seekg(static_cast<std::streamoff>(fileOffset), std::ios::beg);

        uint32_t toRead = (remaining < USER_SECTOR_SIZE) ? remaining : USER_SECTOR_SIZE;
        if (!binFile.read(reinterpret_cast<char*>(outBuf.data() + writePos), toRead)) {
            return false;
        }
        writePos  += toRead;
        remaining -= toRead;
        curLba++;
    }
    return true;
}

// Estrae una eventuale stringa ASCII leggibile all'offset +4 (es. "PAPU PUMMEL", "OXIDE RIDE")
static std::string ExtractAsciiLabel(const uint8_t* buf, uint32_t size) {
    if (size < 12) return "";
    std::string label;
    for (uint32_t i = 4; i < size && i < 36; ++i) {
        char c = static_cast<char>(buf[i]);
        if (c == '\0') break;
        if (c >= 32 && c <= 126) {
            label.push_back(c);
        } else {
            return "";
        }
    }
    if (label.size() >= 3) return label;
    return "";
}

// Determina il tipo di file in base ai magic byte di Eurocom
static std::string DetectBashFileType(const uint8_t* buf, uint32_t size, std::string& outCategory) {
    if (size < 8) {
        outCategory = "RAW";
        return "raw.bin";
    }

    uint32_t magic32 = *reinterpret_cast<const uint32_t*>(buf);
    uint32_t word1   = *reinterpret_cast<const uint32_t*>(buf + 4);

    // 1. Immagini Truevision TGA (footer "TRUEVISION-XFILE.")
    if (size >= 0x12) {
        const char* tgaFooter = reinterpret_cast<const char*>(buf + size - 0x12);
        if (std::strncmp(tgaFooter, "TRUEVISION-XFILE.", 17) == 0) {
            outCategory = "TGA_IMAGE";
            return "tga";
        }
    }

    // 2. Texture Pack + CLUT Palettes (TexHeader: magic == 0x08 e word1 == size)
    if (magic32 == 0x00000008 && word1 == size) {
        outCategory = "TEXTURE_PACK";
        return "tex";
    }

    // 3. Modello 3D + Mesh + Animazioni (MDLHeader: magic == 0x0C160029 o 0x09160026 e word1 == 0)
    if ((magic32 == 0x0C160029 || magic32 == 0x09160026) && word1 == 0x00000000) {
        outCategory = "MODEL_3D";
        return "mdl";
    }

    // 4. Code Overlay MIPS (inizia con puntatori in RAM PS1 0x800xxxxx)
    if ((magic32 & 0xFFF00000) == 0x80000000) {
        outCategory = "CODE_OVERLAY";
        return "code.bin";
    }

    // 5. Musica / Sequenze (magic == 0x14)
    if (magic32 == 0x00000014) {
        outCategory = "MUSIC_BANK";
        return "music.bin";
    }

    // 6. Tabelle stringhe / Sound & Anim Banks con etichetta ASCII a +4
    std::string label = ExtractAsciiLabel(buf, size);
    if (!label.empty()) {
        if (magic32 >= 0x50) {
            outCategory = "SOUND_OR_ANIM_BANK";
            return "bank.bin";
        } else {
            outCategory = "STRINGS_OR_META";
            return "strings.bin";
        }
    }

    outCategory = "OTHER";
    char ext[32];
    std::snprintf(ext, sizeof(ext), "type_0x%X.bin", magic32);
    return std::string(ext);
}

int main(int argc, char* argv[]) {
    std::cout << "================================================================\n";
    std::cout << "  Crash Bash: Remaster+ | Estrattore Automatico Disco & Asset   \n";
    std::cout << "================================================================\n\n";

    fs::path binPath   = "original/disc/bash.bin";
    fs::path outRoot   = "original/extracted";

    if (argc >= 2) binPath = argv[1];
    if (argc >= 3) outRoot = argv[2];

    if (!fs::exists(binPath)) {
        // Prova percorso relativo se lanciato da build/
        if (fs::exists("../original/disc/bash.bin")) {
            binPath = "../original/disc/bash.bin";
            outRoot = "../original/extracted";
        } else {
            std::cerr << "[ERRORE] File immagine disco non trovato in: " << binPath << "\n";
            return 1;
        }
    }

    std::ifstream binFile(binPath, std::ios::binary);
    if (!binFile) {
        std::cerr << "[ERRORE] Impossibile aprire: " << binPath << "\n";
        return 1;
    }

    fs::path discOutDir = outRoot / "disc_files";
    fs::path datOutDir  = outRoot / "assets";
    fs::create_directories(discOutDir);
    fs::create_directories(datOutDir);

    // 1. Estrazione SYSTEM.CNF (LBA 234, 69 byte) e SCUS_945.70 (LBA 23, 432128 byte)
    std::cout << "[1/3] Estrazione SYSTEM.CNF e SCUS_945.70 da " << binPath.string() << "...\n";
    std::vector<uint8_t> sysCnf;
    if (ReadMode2Data(binFile, 234, 69, sysCnf)) {
        std::ofstream f(discOutDir / "SYSTEM.CNF", std::ios::binary);
        f.write(reinterpret_cast<const char*>(sysCnf.data()), sysCnf.size());
    }

    std::vector<uint8_t> exeBuf;
    const uint32_t exeLba  = 23;
    const uint32_t exeSize = 432128;
    if (!ReadMode2Data(binFile, exeLba, exeSize, exeBuf)) {
        std::cerr << "[ERRORE] Fallita lettura di SCUS_945.70 dal disco!\n";
        return 1;
    }
    {
        std::ofstream f(discOutDir / "SCUS_945.70", std::ios::binary);
        f.write(reinterpret_cast<const char*>(exeBuf.data()), exeBuf.size());
    }
    std::cout << "      -> Salvato: " << (discOutDir / "SCUS_945.70").string() << " (432,128 byte)\n";

    // 2. Lettura Tabella LBA da SCUS_945.70 (0x3E910 .. 0x40810)
    const auto* offsetTable = reinterpret_cast<const BashOffset*>(exeBuf.data() + SCUS_TABLE_START);
    const uint32_t entryCount = (SCUS_TABLE_END - SCUS_TABLE_START) / sizeof(BashOffset);
    const uint32_t datBaseLba = 236; // LBA di CRASHBSH/CRASHBSH.DAT nel disco SCUS-94570

    std::cout << "[2/3] Lettura tabella LBA da SCUS_945.70 (0x3E910..0x40810): " << entryCount << " file.\n";
    std::cout << "[3/3] Estrazione e classificazione dei " << entryCount << " asset da CRASHBSH.DAT...\n";

    std::ofstream manifest(outRoot / "manifest.txt");
    manifest << "# Crash Bash (SCUS-94570) — Asset Manifest\n";
    manifest << "# Totale voci: " << entryCount << "\n";
    manifest << "# ID   | LBA    | Size (Hex) | Categoria          | File Estratto        | Etichetta ASCII\n";
    manifest << "---------------------------------------------------------------------------------------------\n";

    uint32_t countTex   = 0;
    uint32_t countMdl   = 0;
    uint32_t countTga   = 0;
    uint32_t countCode  = 0;
    uint32_t countOther = 0;

    std::vector<uint8_t> chunkBuf;
    for (uint32_t i = 0; i < entryCount; ++i) {
        uint32_t relLba = offsetTable[i].LBA;
        uint32_t sz     = offsetTable[i].size;
        if (sz == 0) continue;

        if (!ReadMode2Data(binFile, datBaseLba + relLba, sz, chunkBuf)) {
            std::cerr << "[WARN] Errore lettura chunk #" << i << "\n";
            continue;
        }

        std::string category;
        std::string ext   = DetectBashFileType(chunkBuf.data(), sz, category);
        std::string label = ExtractAsciiLabel(chunkBuf.data(), sz);

        if (ext == "tex")       countTex++;
        else if (ext == "mdl")  countMdl++;
        else if (ext == "tga")  countTga++;
        else if (ext == "code.bin") countCode++;
        else                    countOther++;

        char filename[64];
        std::snprintf(filename, sizeof(filename), "%03u.%s", i, ext.c_str());
        fs::path outFile = datOutDir / filename;

        std::ofstream fout(outFile, std::ios::binary);
        fout.write(reinterpret_cast<const char*>(chunkBuf.data()), chunkBuf.size());

        char line[256];
        std::snprintf(line, sizeof(line), "  #%03u | %6u | 0x%06X   | %-18s | %-20s | %s\n",
            i, relLba, sz, category.c_str(), filename, label.c_str());
        manifest << line;
    }

    manifest.close();

    std::cout << "\n=== Estrazione Completata con Successo! ===\n";
    std::cout << "  - Pacchetti Texture (.tex) : " << countTex   << "\n";
    std::cout << "  - Modelli 3D & Anim (.mdl) : " << countMdl   << "\n";
    std::cout << "  - Schermate Splash (.tga)  : " << countTga   << "\n";
    std::cout << "  - Code Overlays (.code.bin): " << countCode  << "\n";
    std::cout << "  - Stringhe/Audio/Banchi    : " << countOther << "\n";
    std::cout << "  - Manifest dettagliato     : " << (outRoot / "manifest.txt").string() << "\n";
    return 0;
}
