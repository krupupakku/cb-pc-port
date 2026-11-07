#include "core/texture_manager.h"
#include <cstdio>
#include <fstream>
#include <cstring>

int main() {
    std::ifstream idxFile("textures/textures_index.dat", std::ios::binary);
    if (!idxFile.is_open()) {
        std::printf("Failed to open index!\n");
        return 1;
    }
    char magic[4];
    idxFile.read(magic, 4);
    uint32_t count = 0;
    idxFile.read(reinterpret_cast<char*>(&count), sizeof(count));
    std::printf("Index magic: %.4s, count: %u\n", magic, count);
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
        if (pack == 0xFFFF || img == 138) {
            std::printf("Entry %u: crc=%08X pack=%04X img=%u w=%u h=%u bpp=%u\n",
                        i, crc, pack, img, w, h, bpp);
        }
    }
    return 0;
}

