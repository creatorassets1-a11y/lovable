#pragma once
#include <vector>
#include <cstdint>

namespace bake {

struct Material {
    int size = 0;
    std::vector<uint8_t> albedo;   // RGBA8
    std::vector<float> height;     // 0..1
    std::vector<float> rough;      // 0..1

    void alloc(int s);
    void setAlbedo(int x, int y, float r, float g, float b, float a = 1.0f);
    // Sobel over the height field; roughness goes into alpha.
    std::vector<uint8_t> buildNormalRoughness(float bumpStrength) const;
};

struct MaterialDef {
    const char* name;
    void (*generate)(Material&);
    float bump;
};

int materialCount();
const MaterialDef& materialDef(int i);

} // namespace bake
