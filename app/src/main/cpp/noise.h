// noise.h - value noise / fbm, used to generate every texture at runtime.
// Keeping textures procedural is what lets this ship with zero art files.
#pragma once
#include "hmath.h"

namespace hm {

inline uint32_t hashu(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

inline float hash2(int x, int y) {
    uint32_t h = hashu((uint32_t)x * 374761393u + (uint32_t)y * 668265263u);
    return (h >> 8) * (1.0f / 16777216.0f);
}

inline float hash3(int x, int y, int z) {
    uint32_t h = hashu((uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)z * 2147483647u);
    return (h >> 8) * (1.0f / 16777216.0f);
}

// Tiling value noise: period lets textures wrap seamlessly.
inline float valueNoise(float x, float y, int period) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float xf = x - xi, yf = y - yi;
    float u = xf * xf * (3.0f - 2.0f * xf);
    float v = yf * yf * (3.0f - 2.0f * yf);
    auto wrap = [period](int a) { return ((a % period) + period) % period; };
    float a = hash2(wrap(xi), wrap(yi));
    float b = hash2(wrap(xi + 1), wrap(yi));
    float c = hash2(wrap(xi), wrap(yi + 1));
    float d = hash2(wrap(xi + 1), wrap(yi + 1));
    return lerpf(lerpf(a, b, u), lerpf(c, d, u), v);
}

inline float fbm(float x, float y, int octaves, int period) {
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    int p = period;
    for (int i = 0; i < octaves; i++) {
        sum += valueNoise(x, y, p) * amp;
        norm += amp;
        x *= 2.0f; y *= 2.0f; p *= 2; amp *= 0.5f;
    }
    return sum / norm;
}

// Worley/cellular - gives concrete its pitting and flesh its cell structure.
inline float worley(float x, float y, int period) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float best = 1e9f;
    auto wrap = [period](int a) { return ((a % period) + period) % period; };
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int cx = xi + dx, cy = yi + dy;
            float px = cx + hash2(wrap(cx), wrap(cy));
            float py = cy + hash3(wrap(cx), wrap(cy), 7);
            float ddx = px - x, ddy = py - y;
            float d = ddx * ddx + ddy * ddy;
            if (d < best) best = d;
        }
    }
    return std::sqrt(best);
}

} // namespace hm
