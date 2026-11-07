#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <filesystem>
#include <chrono>
#include <future>
#include <mutex>
#include "tex.h"

#include "third_party/stb_image.h"
#include "third_party/stb_image_write.h"

namespace fs = std::filesystem;

void process_tga(const fs::path& tga_path, const fs::path& out_path) {
    int w = 0, h = 0, comp = 0;
    stbi_set_unpremultiply_on_load(1);
    stbi_convert_iphone_png_to_rgb(1);
    unsigned char* data = stbi_load(tga_path.string().c_str(), &w, &h, &comp, 4);
    if (!data) {
        std::cerr << "Failed to load TGA: " << tga_path << std::endl;
        return;
    }
    fs::create_directories(out_path.parent_path());
    stbi_write_png(out_path.string().c_str(), w, h, 4, data, w * 4);
    stbi_image_free(data);
}

int main(int argc, char* argv[]) {
    fs::path assetsDir = "original/extracted/assets";
    fs::path outDir = "textures/extracted";
    if (argc >= 2) assetsDir = argv[1];
    if (argc >= 3) outDir = argv[2];

    fs::path absOutDir = fs::absolute(outDir);
    fs::path absAssetsDir = fs::absolute(assetsDir);
    fs::create_directories(absOutDir);

    std::cout << "Assets dir: " << absAssetsDir.string() << std::endl;
    std::cout << "Output dir: " << absOutDir.string() << std::endl;

    // Collect all .tex files
    std::vector<fs::path> texFiles;
    for (const auto& entry : fs::directory_iterator(absAssetsDir)) {
        if (entry.path().extension() == ".tex") {
            texFiles.push_back(entry.path());
        }
    }
    std::sort(texFiles.begin(), texFiles.end());
    std::cout << "Found " << texFiles.size() << " .tex files." << std::endl;

    auto t0 = std::chrono::high_resolution_clock::now();

    // Remember original current path
    fs::path prevPath = fs::current_path();
    fs::current_path(absOutDir);

    int totalImages = 0;
    int processedFiles = 0;
    for (const auto& texPath : texFiles) {
        try {
            std::string stem = texPath.stem().string();
            std::string mtlPath = stem + "_tex/" + stem + ".mtl";
            Tex tex(texPath, mtlPath);
            tex.Export();
            totalImages += tex.GetImageListSize();
            processedFiles++;
            if (processedFiles % 50 == 0 || processedFiles == (int)texFiles.size()) {
                std::cout << "Processed " << processedFiles << "/" << texFiles.size() 
                          << " files (" << totalImages << " textures extracted)..." << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error processing " << texPath.filename().string() << ": " << e.what() << std::endl;
        }
    }

    // Process TGA splash screens
    fs::path splashesDir = absOutDir / "splashes";
    fs::create_directories(splashesDir);
    int splashesCount = 0;
    for (const auto& entry : fs::directory_iterator(absAssetsDir)) {
        if (entry.path().extension() == ".tga") {
            fs::path dest = splashesDir / (entry.path().stem().string() + ".png");
            process_tga(entry.path(), dest);
            splashesCount++;
        }
    }
    std::cout << "Processed " << splashesCount << " splash screens to " << splashesDir.string() << std::endl;

    fs::current_path(prevPath);

    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(t1 - t0).count();
    std::cout << "\nSUCCESS: Extracted " << totalImages << " textures + " << splashesCount 
              << " splashes in " << elapsed << " seconds!" << std::endl;

    return 0;
}
