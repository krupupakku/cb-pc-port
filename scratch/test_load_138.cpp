#include <cstdio>
#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb_image.h"

int main() {
    int w = 0, h = 0, comp = 0;
    stbi_uc* data = stbi_load("textures/remastered/splashes/138.png", &w, &h, &comp, 4);
    if (!data) {
        std::printf("stbi_load failed! reason: %s\n", stbi_failure_reason());
    } else {
        std::printf("stbi_load SUCCESS! w=%d h=%d comp=%d\n", w, h, comp);
        stbi_image_free(data);
    }
    return 0;
}

