#include "core/texture_manager.h"
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

int main() {
    cb::TextureManager& tm = cb::TextureManager::Instance();
    tm.Initialize("textures/textures_index.dat", "textures/remastered");
    std::printf("Indexed count: %zu\n", tm.GetIndexedCount());
    std::printf("Loaded remastered count: %zu\n", tm.GetLoadedCount());

    // Print all files in textures/remastered
    for (const auto& entry : fs::recursive_directory_iterator("textures/remastered")) {
        if (entry.is_regular_file() && entry.path().extension() == ".png") {
            fs::path rel = fs::relative(entry.path(), "textures/remastered");
            std::string key = rel.string();
            for (char& c : key) if (c == '\\') c = '/';
            if (key.length() > 4 && key.substr(key.length() - 4) == ".png") key = key.substr(0, key.length() - 4);
            std::printf("Found PNG file: %s -> key: '%s'\n", entry.path().string().c_str(), key.c_str());
        }
    }
    return 0;
}
