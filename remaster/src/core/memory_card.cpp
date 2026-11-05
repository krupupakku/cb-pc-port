#include "core/memory_card.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace cb {

MemoryCard::MemoryCard() {
    data.resize(TOTAL_SIZE, 0);
}

MemoryCard::~MemoryCard() {
    if (dirty) {
        flush();
    }
}

uint8_t MemoryCard::calc_xor_checksum(const uint8_t* buf, size_t len) {
    uint8_t chk = 0;
    for (size_t i = 0; i < len; ++i) {
        chk ^= buf[i];
    }
    return chk;
}

void MemoryCard::format_internal() {
    std::memset(data.data(), 0, data.size());

    // Settore 0: Header standard PS1 Memory Card
    data[0] = 'M';
    data[1] = 'C';
    data[127] = calc_xor_checksum(data.data(), 127); // 0x4D ^ 0x43 = 0x0E

    // Settori 1..15: Directory entries per i 15 blocchi liberi (8 KB ciascuno)
    for (int block = 1; block <= 15; ++block) {
        uint8_t* entry = &data[block * SECTOR_SIZE];
        // 0xA0 = Blocco libero / non allocato
        entry[0] = 0xA0;
        entry[1] = 0x00;
        entry[2] = 0x00;
        entry[3] = 0x00;

        // Dimensione iniziale = 0
        entry[4] = 0x00;
        entry[5] = 0x00;
        entry[6] = 0x00;
        entry[7] = 0x00;

        // Prossimo blocco: 0xFFFF (nessun blocco concatenato)
        entry[8] = 0xFF;
        entry[9] = 0xFF;

        // Checksum dei primi 127 byte
        entry[127] = calc_xor_checksum(entry, 127);
    }

    dirty = true;
    cardLoaded = true;
}

bool MemoryCard::init(const std::string& path) {
    filePath = path;

    namespace fs = std::filesystem;
    try {
        fs::path p(filePath);
        if (p.has_parent_path()) {
            fs::create_directories(p.parent_path());
        }
    } catch (...) {}

    FILE* f = std::fopen(filePath.c_str(), "rb");
    if (f) {
        size_t readBytes = std::fread(data.data(), 1, TOTAL_SIZE, f);
        std::fclose(f);
        if (readBytes == TOTAL_SIZE && data[0] == 'M' && data[1] == 'C') {
            std::printf("[MemoryCard] Caricata Memory Card da '%s' (128 KB standard PS1).\n", filePath.c_str());
            cardLoaded = true;
            dirty = false;
            return true;
        }
    }

    std::printf("[MemoryCard] Creazione nuova Memory Card virtuale formattata in '%s'...\n", filePath.c_str());
    format_internal();
    flush();
    return true;
}

bool MemoryCard::flush() {
    if (filePath.empty()) return false;
    FILE* f = std::fopen(filePath.c_str(), "wb");
    if (!f) {
        std::printf("[MemoryCard] ERRORE: Impossibile salvare la Memory Card in '%s'\n", filePath.c_str());
        return false;
    }
    std::fwrite(data.data(), 1, TOTAL_SIZE, f);
    std::fclose(f);
    dirty = false;
    return true;
}

bool MemoryCard::read_sector(int port, int sector, uint8_t* dst) {
    // Port 0 = Slot 1, Port 1 = Slot 2 (non presente)
    if (port != 0 || sector < 0 || (size_t)sector >= TOTAL_SECTORS || !dst) {
        return false;
    }
    std::memcpy(dst, &data[sector * SECTOR_SIZE], SECTOR_SIZE);
    return true;
}

bool MemoryCard::write_sector(int port, int sector, const uint8_t* src) {
    if (port != 0 || sector < 0 || (size_t)sector >= TOTAL_SECTORS || !src) {
        return false;
    }
    std::memcpy(&data[sector * SECTOR_SIZE], src, SECTOR_SIZE);
    dirty = true;
    flush();
    return true;
}

bool MemoryCard::format(int port) {
    if (port != 0) return false;
    format_internal();
    flush();
    std::printf("[MemoryCard] Memory Card formattata con successo.\n");
    return true;
}

int MemoryCard::get_info(int port) {
    if (port != 0) {
        return 2; // Nessun dispositivo presente nello Slot 2
    }
    // Port 0: 0x00 = card presente e pronta
    return 0;
}

int MemoryCard::load(int port) {
    if (port != 0) return 0;
    cardLoaded = true;
    return 1; // 1 = Successo
}

} // namespace cb
