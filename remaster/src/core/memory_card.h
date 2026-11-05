#pragma once
// =============================================================================
// Crash Bash: Remaster+ — Virtual Memory Card (Slot 1 & Slot 2)
// Emulazione Memory Card PS1 standard (128 KB, 15 blocchi da 8 KB, 1024 settori).
// I dati vengono salvati su disco in formato standard .mcr compatibile.
// =============================================================================

#include <cstdint>
#include <string>
#include <vector>

namespace cb {

class MemoryCard {
public:
    static constexpr size_t SECTOR_SIZE = 128;
    static constexpr size_t TOTAL_SECTORS = 1024;
    static constexpr size_t TOTAL_SIZE = SECTOR_SIZE * TOTAL_SECTORS; // 131072 byte (128 KB)

    MemoryCard();
    ~MemoryCard();

    // Inizializza o carica la Memory Card dal percorso indicato (default "saves/crashbash_slot1.mcr")
    bool init(const std::string& path = "saves/crashbash_slot1.mcr");

    // Salva i dati della card su file
    bool flush();

    // Operazioni sui settori (128 byte per settore)
    bool read_sector(int port, int sector, uint8_t* dst);
    bool write_sector(int port, int sector, const uint8_t* src);
    bool format(int port);

    // Stato per le chiamate BIOS
    int get_info(int port);
    int load(int port);

private:
    std::string filePath;
    std::vector<uint8_t> data;
    bool cardLoaded = false;
    bool dirty = false;

    void format_internal();
    static uint8_t calc_xor_checksum(const uint8_t* buf, size_t len);
};

} // namespace cb
