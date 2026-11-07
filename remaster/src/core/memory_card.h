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
    static constexpr size_t BLOCK_SIZE = 8192; // 64 settori da 128 byte
    static constexpr size_t TOTAL_BLOCKS = 15;
    static constexpr size_t TOTAL_SIZE = SECTOR_SIZE * TOTAL_SECTORS; // 131072 byte (128 KB)

    struct DirEntry {
        std::string name;
        uint32_t attr = 0x51;
        uint32_t size = 0;
        int first_block = 0;
    };

    MemoryCard();
    ~MemoryCard();

    // Inizializza o carica la Memory Card dal percorso indicato (default "saves/crashbash_slot1.mcr")
    bool init(const std::string& path = "saves/crashbash_slot1.mcr");

    // Salva i dati della card su file
    bool flush();

    // Verifica se la card presenta un header PS1 valido ('M','C')
    bool is_formatted() const;

    // Operazioni sui settori (128 byte per settore)
    bool read_sector(int port, int sector, uint8_t* dst);
    bool write_sector(int port, int sector, const uint8_t* src);
    bool format(int port);

    // Operazioni ad alto livello su file PS1 (compatibili con Psy-Q libmcrd)
    // Codici di ritorno Psy-Q: 0=McErrNone, 1=McErrCardNotExist, 2=McErrCardInvalid,
    // 3=McErrNewCard/FileNotExist, 4=McErrNotFormat, 5=McErrAlreadyExist, 6=McErrBlockFull
    int get_direntries(int port, const char* pattern, std::vector<DirEntry>& out) const;
    int create_file(int port, const char* filename, int blocks);
    int read_file(int port, const char* filename, uint8_t* dst, uint32_t offset, uint32_t bytes) const;
    int write_file(int port, const char* filename, const uint8_t* src, uint32_t offset, uint32_t bytes);

    // Stato per le chiamate BIOS
    int get_info(int port);
    int load(int port);

private:
    std::string filePath;
    std::vector<uint8_t> data;
    bool cardLoaded = false;
    bool dirty = false;

    void format_internal();
    std::vector<int> get_file_block_chain(const char* filename) const;
    static bool match_wildcard(const char* pat, const char* str);
    static uint8_t calc_xor_checksum(const uint8_t* buf, size_t len);
};

} // namespace cb
