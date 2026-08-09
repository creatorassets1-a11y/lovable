#include "stb_write_shim.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../android/app/src/main/cpp/third_party/stb_image_write.h"

void write_png_flipped(const char* path, int w, int h, const uint8_t* rgba) {
    // GL's origin is bottom-left; PNG's is top-left.
    std::vector<uint8_t> flip((size_t)w * h * 4);
    for (int y = 0; y < h; y++) {
        std::memcpy(&flip[(size_t)y * w * 4], &rgba[(size_t)(h - 1 - y) * w * 4],
                    (size_t)w * 4);
    }
    stbi_write_png(path, w, h, 4, flip.data(), w * 4);
}
