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

    // Settori 16..35: Tabella bad sectors (0xFFFFFFFF = nessun settore danneggiato)
    for (int sec = 16; sec <= 35; ++sec) {
        uint8_t* entry = &data[sec * SECTOR_SIZE];
        entry[0] = 0xFF;
        entry[1] = 0xFF;
        entry[2] = 0xFF;
        entry[3] = 0xFF;
        entry[8] = 0xFF;
        entry[9] = 0xFF;
        entry[127] = calc_xor_checksum(entry, 127);
    }

    // Settore 63: Write test sector (copia del settore 0)
    std::memcpy(&data[63 * SECTOR_SIZE], data.data(), SECTOR_SIZE);

    dirty = true;
    cardLoaded = true;
}

bool MemoryCard::is_formatted() const {
    return data.size() >= TOTAL_SIZE && data[0] == 'M' && data[1] == 'C';
}

bool MemoryCard::match_wildcard(const char* pat, const char* str) {
    if (!pat || pat[0] == '\0' || (pat[0] == '*' && pat[1] == '\0')) return true;
    while (*pat && *str) {
        if (*pat == '*') {
            ++pat;
            if (!*pat) return true;
            while (*str) {
                if (match_wildcard(pat, str)) return true;
                ++str;
            }
            return false;
        }
        if (*pat != '?' && *pat != *str) return false;
        ++pat;
        ++str;
    }
    while (*pat == '*') ++pat;
    return *pat == '\0' && *str == '\0';
}

std::vector<int> MemoryCard::get_file_block_chain(const char* filename) const {
    std::vector<int> chain;
    if (!filename || !filename[0] || !is_formatted()) return chain;

    int start_block = 0;
    for (int block = 1; block <= 15; ++block) {
        const uint8_t* entry = &data[block * SECTOR_SIZE];
        if (entry[0] == 0x51) {
            char entry_name[21] = {};
            std::memcpy(entry_name, entry + 10, 20);
            if (std::strcmp(entry_name, filename) == 0) {
                start_block = block;
                break;
            }
        }
    }
    if (start_block == 0) return chain;

    int cur = start_block;
    for (int step = 0; step < 15 && cur >= 1 && cur <= 15; ++step) {
        chain.push_back(cur);
        const uint8_t* entry = &data[cur * SECTOR_SIZE];
        uint16_t next_idx = 0xFFFFu;
        std::memcpy(&next_idx, entry + 8, 2);
        if (next_idx >= 15) break;
        cur = static_cast<int>(next_idx) + 1;
    }
    return chain;
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

int MemoryCard::get_direntries(int port, const char* pattern, std::vector<DirEntry>& out) const {
    out.clear();
    if (port != 0) return 1; // McErrCardNotExist
    if (!is_formatted()) return 4; // McErrNotFormat

    for (int block = 1; block <= 15; ++block) {
        const uint8_t* entry = &data[block * SECTOR_SIZE];
        if (entry[0] == 0x51) {
            char name[21] = {};
            std::memcpy(name, entry + 10, 20);
            if (match_wildcard(pattern, name)) {
                uint32_t fsize = 0;
                std::memcpy(&fsize, entry + 4, 4);
                if (fsize == 0) fsize = BLOCK_SIZE;
                out.push_back(DirEntry{ name, 0x51u, fsize, block });
            }
        }
    }
    return 0; // McErrNone
}

int MemoryCard::create_file(int port, const char* filename, int blocks) {
    if (port != 0) return 1; // McErrCardNotExist
    if (!is_formatted()) return 4; // McErrNotFormat
    if (!filename || !filename[0] || blocks <= 0 || blocks > 15) return 2; // McErrCardInvalid

    // Se il file esiste gia' sulla Memory Card, ritorna McErrNone (0) per consentire la sovrascrittura
    auto existing = get_file_block_chain(filename);
    if (!existing.empty()) {
        return 0;
    }

    std::vector<int> free_blocks;
    for (int block = 1; block <= 15; ++block) {
        const uint8_t* entry = &data[block * SECTOR_SIZE];
        if ((entry[0] & 0xF0) == 0xA0) {
            free_blocks.push_back(block);
            if (static_cast<int>(free_blocks.size()) == blocks) break;
        }
    }

    if (static_cast<int>(free_blocks.size()) < blocks) {
        return 6; // McErrBlockFull
    }

    for (int i = 0; i < blocks; ++i) {
        int b = free_blocks[i];
        uint8_t* entry = &data[b * SECTOR_SIZE];
        std::memset(entry, 0, SECTOR_SIZE);

        if (i == 0) {
            entry[0] = 0x51; // Primo blocco (o blocco singolo)
            uint32_t byte_size = static_cast<uint32_t>(blocks) * BLOCK_SIZE;
            std::memcpy(entry + 4, &byte_size, 4);
            std::strncpy(reinterpret_cast<char*>(entry + 10), filename, 20);
        } else if (i + 1 < blocks) {
            entry[0] = 0x52; // Blocco intermedio
        } else {
            entry[0] = 0x53; // Ultimo blocco
        }

        uint16_t next_idx = (i + 1 < blocks) ? static_cast<uint16_t>(free_blocks[i + 1] - 1) : 0xFFFFu;
        std::memcpy(entry + 8, &next_idx, 2);
        entry[127] = calc_xor_checksum(entry, 127);
    }

    dirty = true;
    flush();
    std::printf("[MemoryCard] Creato file '%s' (%d blocchi, %u byte) nello Slot 1.\n",
                filename, blocks, static_cast<unsigned>(blocks * BLOCK_SIZE));
    return 0; // McErrNone
}

int MemoryCard::read_file(int port, const char* filename, uint8_t* dst, uint32_t offset, uint32_t bytes) const {
    if (port != 0) return 1; // McErrCardNotExist
    if (!is_formatted()) return 4; // McErrNotFormat
    if (!dst || bytes == 0) return 0;

    auto chain = get_file_block_chain(filename);
    if (chain.empty()) return 3; // McErrFileNotExist

    for (uint32_t i = 0; i < bytes; ++i) {
        uint32_t pos = offset + i;
        size_t blk_idx = pos / BLOCK_SIZE;
        size_t blk_off = pos % BLOCK_SIZE;
        if (blk_idx < chain.size()) {
            size_t data_off = static_cast<size_t>(chain[blk_idx]) * BLOCK_SIZE + blk_off;
            dst[i] = data[data_off];
        } else {
            dst[i] = 0;
        }
    }
    std::printf("[MemoryCard] Letto file '%s' (offset=%u, %u byte) dallo Slot 1.\n",
                filename, offset, bytes);
    return 0; // McErrNone
}

int MemoryCard::write_file(int port, const char* filename, const uint8_t* src, uint32_t offset, uint32_t bytes) {
    if (port != 0) return 1; // McErrCardNotExist
    if (!is_formatted()) return 4; // McErrNotFormat
    if (!src || bytes == 0) return 0;

    auto chain = get_file_block_chain(filename);
    if (chain.empty()) {
        int needed_blocks = static_cast<int>((offset + bytes + BLOCK_SIZE - 1) / BLOCK_SIZE);
        if (needed_blocks < 1) needed_blocks = 1;
        int cr = create_file(port, filename, needed_blocks);
        if (cr != 0) return cr;
        chain = get_file_block_chain(filename);
        if (chain.empty()) return 3;
    }

    for (uint32_t i = 0; i < bytes; ++i) {
        uint32_t pos = offset + i;
        size_t blk_idx = pos / BLOCK_SIZE;
        size_t blk_off = pos % BLOCK_SIZE;
        if (blk_idx < chain.size()) {
            size_t data_off = static_cast<size_t>(chain[blk_idx]) * BLOCK_SIZE + blk_off;
            data[data_off] = src[i];
        }
    }

    dirty = true;
    flush();
    std::printf("[MemoryCard] Salvato file '%s' (offset=%u, %u byte) su '%s'.\n",
                filename, offset, bytes, filePath.c_str());
    return 0; // McErrNone
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
