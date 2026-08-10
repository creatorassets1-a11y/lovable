// materials.cpp - procedural material authoring, run at bake time.
//
// Each material produces an albedo map and a height field. The height field is
// converted to a tangent-space normal map with roughness packed into alpha.
// Doing this offline rather than at runtime is what lets them be 512x512 with
// real structure in them instead of 256x256 noise.
#include "materials.h"
#include "../app/src/main/cpp/noise.h"
#include <cmath>
#include <algorithm>

using namespace hm;

namespace bake {

// ------------------------------------------------------------------- helpers

static inline float saturate(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

void Material::alloc(int s) {
    size = s;
    albedo.assign((size_t)s * s * 4, 255);
    height.assign((size_t)s * s, 0.5f);
    rough.assign((size_t)s * s, 0.7f);
}

void Material::setAlbedo(int x, int y, float r, float g, float b, float a) {
    size_t i = ((size_t)y * size + x) * 4;
    albedo[i + 0] = (uint8_t)(saturate(r) * 255.0f);
    albedo[i + 1] = (uint8_t)(saturate(g) * 255.0f);
    albedo[i + 2] = (uint8_t)(saturate(b) * 255.0f);
    albedo[i + 3] = (uint8_t)(saturate(a) * 255.0f);
}

// Sobel over the (tiling) height field. bumpStrength is in texels, so it stays
// consistent as the resolution changes.
std::vector<uint8_t> Material::buildNormalRoughness(float bumpStrength) const {
    std::vector<uint8_t> out((size_t)size * size * 4);
    auto H = [&](int x, int y) {
        x = ((x % size) + size) % size;
        y = ((y % size) + size) % size;
        return height[(size_t)y * size + x];
    };
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            float gx = (H(x + 1, y - 1) + 2.0f * H(x + 1, y) + H(x + 1, y + 1))
                     - (H(x - 1, y - 1) + 2.0f * H(x - 1, y) + H(x - 1, y + 1));
            float gy = (H(x - 1, y + 1) + 2.0f * H(x, y + 1) + H(x + 1, y + 1))
                     - (H(x - 1, y - 1) + 2.0f * H(x, y - 1) + H(x + 1, y - 1));
            vec3 n = normalize(vec3(-gx * bumpStrength, -gy * bumpStrength, 1.0f));
            size_t i = ((size_t)y * size + x) * 4;
            out[i + 0] = (uint8_t)((n.x * 0.5f + 0.5f) * 255.0f);
            out[i + 1] = (uint8_t)((n.y * 0.5f + 0.5f) * 255.0f);
            out[i + 2] = (uint8_t)((n.z * 0.5f + 0.5f) * 255.0f);
            out[i + 3] = (uint8_t)(saturate(rough[(size_t)y * size + x]) * 255.0f);
        }
    }
    return out;
}

// Streaks that run downward from a mask - the single most useful weathering
// primitive there is. Rain does this to every vertical surface on earth.
static float runoff(float u, float v, float scale, int period) {
    float s = fbm(u * scale, v * scale * 0.14f, 4, period);
    return s;
}

// ------------------------------------------------------------------ materials

static void genAsphalt(Material& m) {
    const int S = m.size;
    const float inv = 8.0f / S;
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            float u = x * inv, v = y * inv;
            // Aggregate: worley cells are the stones, fbm is the binder.
            float agg = worley(u * 6.0f, v * 6.0f, 48);
            float stone = smoothstepf(0.28f, 0.05f, agg);
            float base = 0.085f + fbm(u * 2.0f, v * 2.0f, 4, 16) * 0.055f;
            base += stone * 0.055f;

            // Tar patches: darker, smoother, with hard-ish edges.
            float patchMask = fbm(u * 0.8f, v * 0.8f, 3, 6);
            float patch = smoothstepf(0.56f, 0.64f, patchMask);
            base = lerpf(base, 0.055f, patch * 0.85f);

            // Cracks: thin dark filaments where an fbm crosses a narrow band.
            float cn = fbm(u * 1.6f, v * 1.6f, 5, 12);
            float crack = 1.0f - smoothstepf(0.0f, 0.030f, std::fabs(cn - 0.5f));
            crack *= smoothstepf(0.35f, 0.55f, fbm(u * 0.5f, v * 0.5f, 2, 4));
            base -= crack * 0.055f;

            float grain = (hash2(x * 7, y * 13) - 0.5f) * 0.020f;
            m.setAlbedo(x, y, base + grain, (base + grain) * 0.99f, (base + grain) * 1.02f);
            m.height[(size_t)y * S + x] = stone * 0.55f + base * 1.6f - crack * 0.8f + patch * 0.1f;
            m.rough[(size_t)y * S + x] = 0.92f - patch * 0.30f - stone * 0.08f;
        }
    }
}

static void genSidewalk(Material& m) {
    const int S = m.size;
    const float inv = 8.0f / S;
    const int slab = S / 2;         // two slabs across the tile
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            float u = x * inv, v = y * inv;
            float base = 0.30f + fbm(u * 2.4f, v * 2.4f, 4, 20) * 0.10f;

            float jx = std::fabs(std::fmod((float)x, (float)slab) - slab * 0.5f) / (slab * 0.5f);
            float jy = std::fabs(std::fmod((float)y, (float)slab) - slab * 0.5f) / (slab * 0.5f);
            float joint = std::max(smoothstepf(0.93f, 1.0f, jx), smoothstepf(0.93f, 1.0f, jy));

            // Chipped edges: the joint line is not clean, it is broken up.
            float chip = fbm(u * 12.0f, v * 12.0f, 3, 96);
            joint *= 0.55f + chip * 0.9f;
            base -= joint * 0.14f;

            float stain = smoothstepf(0.55f, 0.85f, fbm(u * 1.1f, v * 1.1f, 3, 9));
            base *= 1.0f - stain * 0.28f;

            float pits = smoothstepf(0.20f, 0.0f, worley(u * 9.0f, v * 9.0f, 72));
            base -= pits * 0.06f;

            float g = (hash2(x * 3, y * 11) - 0.5f) * 0.022f;
            m.setAlbedo(x, y, (base + g) * 1.00f, (base + g) * 0.995f, (base + g) * 0.955f);
            m.height[(size_t)y * S + x] = base * 1.2f - joint * 0.75f - pits * 0.4f;
            m.rough[(size_t)y * S + x] = 0.86f + stain * 0.08f;
        }
    }
}

static void genBrick(Material& m) {
    const int S = m.size;
    const float inv = 8.0f / S;
    const int bw = S / 4, bh = S / 12;     // 4 bricks across, 12 courses
    const int mortar = std::max(2, S / 128);

    for (int y = 0; y < S; y++) {
        int row = y / bh;
        int yin = y % bh;
        // Running bond: every other course is offset by half a brick.
        int xoff = (row & 1) ? bw / 2 : 0;
        for (int x = 0; x < S; x++) {
            float u = x * inv, v = y * inv;
            int xs = ((x + xoff) % S + S) % S;
            int col = xs / bw;
            int xin = xs % bw;

            bool isMortar = (yin < mortar) || (xin < mortar);

            float r, g, b, h, rg;
            if (isMortar) {
                float mv = 0.33f + fbm(u * 14.0f, v * 14.0f, 3, 112) * 0.13f;
                r = mv; g = mv * 0.985f; b = mv * 0.94f;
                h = 0.18f + mv * 0.20f;      // recessed
                rg = 0.94f;
            } else {
                // Per-brick colour variation, then within-brick mottling.
                float pick = hash2(col + row * 31, row * 17 + 7);
                float pick2 = hash3(col, row, 5);
                float baseR = lerpf(0.30f, 0.46f, pick);
                float baseG = baseR * lerpf(0.44f, 0.56f, pick2);
                float baseB = baseR * lerpf(0.34f, 0.44f, pick2);
                float mot = fbm(u * 10.0f, v * 10.0f, 4, 80);
                float k = 0.80f + mot * 0.42f;
                r = baseR * k; g = baseG * k; b = baseB * k;

                // Edge darkening: brick faces are chamfered by weather.
                float ex = std::min((float)(xin - mortar), (float)(bw - 1 - xin)) / (bw * 0.14f);
                float ey = std::min((float)(yin - mortar), (float)(bh - 1 - yin)) / (bh * 0.22f);
                float edge = 1.0f - saturate(std::min(ex, ey));
                r *= 1.0f - edge * 0.30f;
                g *= 1.0f - edge * 0.30f;
                b *= 1.0f - edge * 0.30f;

                h = 0.55f + mot * 0.16f - edge * 0.14f;
                rg = 0.80f + mot * 0.14f;
            }

            // Efflorescence and damp running down over everything.
            float run = runoff(u, v, 4.0f, 32);
            float damp = smoothstepf(0.56f, 0.86f, run);
            r *= 1.0f - damp * 0.34f;
            g *= 1.0f - damp * 0.32f;
            b *= 1.0f - damp * 0.26f;
            rg += damp * 0.05f;

            float salt = smoothstepf(0.72f, 0.92f, fbm(u * 3.0f, v * 2.0f, 3, 24));
            r = lerpf(r, 0.62f, salt * 0.35f);
            g = lerpf(g, 0.62f, salt * 0.35f);
            b = lerpf(b, 0.60f, salt * 0.35f);

            m.setAlbedo(x, y, r, g, b);
            m.height[(size_t)y * S + x] = h;
            m.rough[(size_t)y * S + x] = rg;
        }
    }
}

static void genConcreteWall(Material& m) {
    const int S = m.size;
    const float inv = 8.0f / S;
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            float u = x * inv, v = y * inv;
            float base = 0.34f + fbm(u * 2.2f, v * 2.2f, 5, 18) * 0.16f;

            // Form-board marks: the horizontal lines left by shuttering.
            float form = std::fabs(std::fmod((float)y, S / 6.0f) - S / 12.0f) / (S / 12.0f);
            float formLine = smoothstepf(0.90f, 1.0f, form);
            base -= formLine * 0.045f;

            // Tie holes on a regular grid.
            float tie = 0.0f;
            {
                float gx = std::fmod((float)x + S / 12.0f, S / 3.0f) - S / 6.0f;
                float gy = std::fmod((float)y + S / 12.0f, S / 3.0f) - S / 6.0f;
                float d = std::sqrt(gx * gx + gy * gy) / (S * 0.016f);
                tie = 1.0f - smoothstepf(0.6f, 1.0f, d);
            }
            base -= tie * 0.20f;

            float pits = smoothstepf(0.24f, 0.02f, worley(u * 7.0f, v * 7.0f, 56));
            base -= pits * 0.10f;

            float run = runoff(u, v, 5.0f, 40);
            float streak = smoothstepf(0.54f, 0.84f, run);
            base *= 1.0f - streak * 0.32f;

            float crack = 1.0f - smoothstepf(0.0f, 0.022f,
                std::fabs(fbm(u * 1.3f, v * 1.6f, 5, 10) - 0.5f));
            crack *= smoothstepf(0.4f, 0.6f, fbm(u * 0.4f, v * 0.4f, 2, 3));
            base -= crack * 0.14f;

            float g = (hash2(x * 5, y * 3) - 0.5f) * 0.022f;
            m.setAlbedo(x, y, (base + g) * 1.0f, (base + g) * 0.99f, (base + g) * 0.95f);
            m.height[(size_t)y * S + x] = base * 1.35f - tie * 0.9f - pits * 0.5f
                                        - crack * 0.7f - formLine * 0.25f;
            m.rough[(size_t)y * S + x] = 0.88f + streak * 0.06f;
        }
    }
}

static void genCorrugated(Material& m) {
    const int S = m.size;
    const float inv = 8.0f / S;
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            float u = x * inv, v = y * inv;
            // Ribs: eight per tile, sinusoidal so the normal map curves.
            float rib = std::sin((float)x / S * TAU * 8.0f);
            float ribN = rib * 0.5f + 0.5f;

            float base = 0.30f + ribN * 0.16f;
            float scratch = fbm(u * 20.0f, v * 1.2f, 3, 160);
            base += (scratch - 0.5f) * 0.06f;

            // Rust blooms from the low points of the ribs where water sits.
            float rustMask = fbm(u * 1.4f, v * 1.4f, 4, 11) * (1.0f - ribN * 0.45f);
            float rust = smoothstepf(0.42f, 0.72f, rustMask);
            float rr = lerpf(base, 0.32f, rust);
            float gg = lerpf(base * 1.01f, 0.15f, rust);
            float bb = lerpf(base * 1.06f, 0.075f, rust);

            float run = smoothstepf(0.58f, 0.88f, runoff(u, v, 6.0f, 48));
            rr = lerpf(rr, 0.26f, run * 0.5f);
            gg = lerpf(gg, 0.13f, run * 0.5f);
            bb = lerpf(bb, 0.07f, run * 0.5f);

            m.setAlbedo(x, y, rr, gg, bb);
            m.height[(size_t)y * S + x] = ribN * 1.6f + rust * 0.12f;
            m.rough[(size_t)y * S + x] = lerpf(0.42f, 0.95f, rust);
        }
    }
}

static void genRustMetal(Material& m) {
    const int S = m.size;
    const float inv = 8.0f / S;
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            float u = x * inv, v = y * inv;
            float pit = worley(u * 10.0f, v * 10.0f, 80);
            float deep = smoothstepf(0.30f, 0.02f, pit);
            float scale = fbm(u * 3.0f, v * 3.0f, 5, 24);
            float rust = saturate(scale * 1.15f + deep * 0.4f);

            float r = lerpf(0.24f, 0.46f, rust);
            float g = lerpf(0.20f, 0.20f, rust);
            float b = lerpf(0.19f, 0.10f, rust);
            // Patches where the paint has not gone yet.
            float paint = smoothstepf(0.70f, 0.80f, fbm(u * 0.9f, v * 0.9f, 3, 7));
            r = lerpf(r, 0.16f, paint);
            g = lerpf(g, 0.22f, paint);
            b = lerpf(b, 0.24f, paint);

            m.setAlbedo(x, y, r, g, b);
            m.height[(size_t)y * S + x] = 0.6f + scale * 0.5f - deep * 0.9f;
            m.rough[(size_t)y * S + x] = lerpf(0.95f, 0.55f, paint);
        }
    }
}

static void genWoodPlank(Material& m) {
    const int S = m.size;
    const float inv = 8.0f / S;
    const int pw = S / 4;
    const int gap = std::max(1, S / 170);
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            float u = x * inv, v = y * inv;
            int plank = x / pw;
            int xin = x % pw;
            bool isGap = xin < gap;

            float tone = lerpf(0.20f, 0.34f, hash2(plank * 13 + 3, 1));
            // Grain runs along the plank: stretched noise plus ring lines.
            float grain = fbm(u * 2.0f + plank * 3.7f, v * 26.0f, 4, 200);
            float rings = std::sin((grain * 9.0f + v * 3.0f) * TAU);
            float g2 = 0.5f + 0.5f * rings;
            float base = tone * (0.78f + g2 * 0.40f);

            // Knots.
            float knot = 0.0f;
            {
                float kx = std::fmod(u * 2.0f + plank * 1.7f, 2.0f) - 1.0f;
                float ky = std::fmod(v * 1.3f + plank * 0.9f, 2.0f) - 1.0f;
                float d = std::sqrt(kx * kx * 4.0f + ky * ky) / 0.35f;
                knot = 1.0f - smoothstepf(0.5f, 1.0f, d);
            }
            base *= 1.0f - knot * 0.45f;

            float r = base * 1.00f, gg = base * 0.76f, b = base * 0.52f;
            float h = 0.55f + g2 * 0.10f - knot * 0.12f;
            float rg = 0.82f + g2 * 0.10f;

            if (isGap) {
                r *= 0.18f; gg *= 0.18f; b *= 0.18f;
                h = 0.05f;
                rg = 0.98f;
            }

            float rot = smoothstepf(0.62f, 0.88f, runoff(u, v, 5.0f, 40));
            r = lerpf(r, 0.10f, rot * 0.6f);
            gg = lerpf(gg, 0.10f, rot * 0.6f);
            b = lerpf(b, 0.09f, rot * 0.6f);

            m.setAlbedo(x, y, r, gg, b);
            m.height[(size_t)y * S + x] = h;
            m.rough[(size_t)y * S + x] = rg;
        }
    }
}

static void genPlaster(Material& m) {
    const int S = m.size;
    const float inv = 8.0f / S;
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            float u = x * inv, v = y * inv;
            float base = 0.46f + fbm(u * 3.0f, v * 3.0f, 4, 24) * 0.10f;

            // Trowel swirls.
            float swirl = fbm(u * 1.2f + std::sin(v * 2.0f) * 0.3f, v * 1.2f, 3, 10);
            base += (swirl - 0.5f) * 0.07f;

            // Damp bloom from the bottom, which is how real walls fail.
            float rise = 1.0f - smoothstepf(0.0f, 0.55f, (float)y / S);
            float damp = rise * smoothstepf(0.35f, 0.75f, fbm(u * 2.5f, v * 1.2f, 4, 20));
            base *= 1.0f - damp * 0.45f;

            // Blown plaster: patches flaked off to the darker substrate.
            float flakeMask = fbm(u * 2.2f, v * 2.2f, 5, 18);
            float flake = smoothstepf(0.62f, 0.68f, flakeMask + damp * 0.15f);
            float r = lerpf(base, base * 0.52f, flake);
            float g = lerpf(base * 0.985f, base * 0.46f, flake);
            float b = lerpf(base * 0.93f, base * 0.40f, flake);

            float crack = 1.0f - smoothstepf(0.0f, 0.018f,
                std::fabs(fbm(u * 2.4f, v * 2.6f, 5, 20) - 0.5f));
            r -= crack * 0.16f; g -= crack * 0.16f; b -= crack * 0.15f;

            m.setAlbedo(x, y, r, g, b);
            m.height[(size_t)y * S + x] = base * 1.1f - flake * 0.55f - crack * 0.7f;
            m.rough[(size_t)y * S + x] = 0.90f + flake * 0.06f;
        }
    }
}

static void genTileFloor(Material& m) {
    const int S = m.size;
    const float inv = 8.0f / S;
    const int t = S / 4;
    const int grout = std::max(2, S / 100);
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            float u = x * inv, v = y * inv;
            int xin = x % t, yin = y % t;
            int tx = x / t, ty = y / t;
            bool isGrout = xin < grout || yin < grout;

            float r, g, b, h, rg;
            if (isGrout) {
                float gv = 0.16f + fbm(u * 16.0f, v * 16.0f, 3, 128) * 0.08f;
                // Grout collects everything. It is always the dirtiest part.
                float filth = smoothstepf(0.35f, 0.75f, fbm(u * 4.0f, v * 4.0f, 3, 32));
                gv *= 1.0f - filth * 0.45f;
                r = gv; g = gv * 0.97f; b = gv * 0.90f;
                h = 0.16f;
                rg = 0.96f;
            } else {
                float pick = hash2(tx * 7 + 1, ty * 11 + 3);
                float base = lerpf(0.34f, 0.44f, pick);
                float mot = fbm(u * 8.0f, v * 8.0f, 3, 64);
                base *= 0.90f + mot * 0.22f;
                r = base; g = base * 0.99f; b = base * 0.94f;
                h = 0.72f;
                rg = 0.34f + mot * 0.16f;      // glazed: much smoother
            }

            // Wear paths and stains over the top of everything.
            float wear = smoothstepf(0.50f, 0.80f, fbm(u * 1.1f, v * 1.1f, 4, 9));
            r *= 1.0f - wear * 0.30f;
            g *= 1.0f - wear * 0.32f;
            b *= 1.0f - wear * 0.34f;
            rg = lerpf(rg, 0.92f, wear * 0.7f);

            float crackT = 1.0f - smoothstepf(0.0f, 0.014f,
                std::fabs(fbm(u * 3.0f, v * 3.0f, 4, 24) - 0.5f));
            crackT *= smoothstepf(0.55f, 0.7f, fbm(u * 0.7f, v * 0.7f, 2, 6));
            r -= crackT * 0.18f; g -= crackT * 0.18f; b -= crackT * 0.18f;

            m.setAlbedo(x, y, r, g, b);
            m.height[(size_t)y * S + x] = h - crackT * 0.5f;
            m.rough[(size_t)y * S + x] = rg;
        }
    }
}

static void genGravel(Material& m) {
    const int S = m.size;
    const float inv = 8.0f / S;
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            float u = x * inv, v = y * inv;
            float w1 = worley(u * 12.0f, v * 12.0f, 96);
            float w2 = worley(u * 24.0f, v * 24.0f, 192);
            float stone = smoothstepf(0.34f, 0.02f, w1);
            float small = smoothstepf(0.22f, 0.02f, w2);
            float base = 0.16f + stone * 0.16f + small * 0.09f;
            float tint = fbm(u * 6.0f, v * 6.0f, 3, 48);
            m.setAlbedo(x, y, base * (0.9f + tint * 0.3f),
                              base * (0.88f + tint * 0.28f),
                              base * (0.82f + tint * 0.26f));
            m.height[(size_t)y * S + x] = stone * 1.1f + small * 0.5f + tint * 0.15f;
            m.rough[(size_t)y * S + x] = 0.95f;
        }
    }
}

static void genWeeds(Material& m) {
    const int S = m.size;
    const float inv = 8.0f / S;
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            float u = x * inv, v = y * inv;
            float soil = 0.10f + fbm(u * 5.0f, v * 5.0f, 4, 40) * 0.07f;
            // Blades: stretched high-frequency noise, thresholded.
            float blade = fbm(u * 30.0f, v * 4.0f, 3, 240);
            float blade2 = fbm(u * 4.0f, v * 30.0f, 3, 240);
            float cover = smoothstepf(0.52f, 0.78f, std::max(blade, blade2))
                        * smoothstepf(0.35f, 0.65f, fbm(u * 1.5f, v * 1.5f, 3, 12));
            float r = lerpf(soil, 0.115f, cover);
            float g = lerpf(soil * 0.95f, 0.150f, cover);
            float b = lerpf(soil * 0.82f, 0.072f, cover);
            m.setAlbedo(x, y, r, g, b);
            m.height[(size_t)y * S + x] = soil * 1.5f + cover * 0.7f;
            m.rough[(size_t)y * S + x] = 0.93f - cover * 0.10f;
        }
    }
}

static void genFlesh(Material& m) {
    const int S = m.size;
    const float inv = 8.0f / S;
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            float u = x * inv, v = y * inv;
            float cells = worley(u * 5.0f, v * 5.0f, 40);
            float skin = 0.36f + smoothstepf(0.0f, 0.45f, cells) * 0.20f;

            // Veins: a narrow band of an fbm, which reads as a branching network.
            float vn = fbm(u * 2.6f, v * 2.6f, 5, 20);
            float vein = 1.0f - smoothstepf(0.0f, 0.040f, std::fabs(vn - 0.47f));
            skin -= vein * 0.16f;

            float mottle = fbm(u * 9.0f, v * 9.0f, 4, 72);
            skin *= 0.86f + mottle * 0.30f;

            // Wet patches: low roughness is what makes it look like it is not dry.
            float wet = smoothstepf(0.55f, 0.80f, fbm(u * 1.8f, v * 1.8f, 3, 14));

            float r = skin * 0.88f + vein * 0.045f;
            float g = skin * 0.79f;
            float b = skin * 0.77f;
            m.setAlbedo(x, y, r, g, b);
            m.height[(size_t)y * S + x] = 0.5f + smoothstepf(0.0f, 0.5f, cells) * 0.5f
                                        - vein * 0.35f + mottle * 0.1f;
            m.rough[(size_t)y * S + x] = lerpf(0.72f, 0.22f, wet);
        }
    }
}

static void genCloth(Material& m) {
    const int S = m.size;
    const float inv = 8.0f / S;
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            float u = x * inv, v = y * inv;
            // Weave: two perpendicular square waves, softened.
            float wx = 0.5f + 0.5f * std::sin((float)x / S * TAU * 96.0f);
            float wy = 0.5f + 0.5f * std::sin((float)y / S * TAU * 96.0f);
            float weave = std::max(wx, wy) * 0.5f + (wx * wy) * 0.5f;

            float base = 0.16f + fbm(u * 2.0f, v * 2.0f, 3, 16) * 0.06f;
            base *= 0.82f + weave * 0.34f;

            float dirt = smoothstepf(0.45f, 0.80f, fbm(u * 1.3f, v * 1.3f, 4, 10));
            float r = lerpf(base * 1.0f, base * 0.68f, dirt);
            float g = lerpf(base * 0.96f, base * 0.62f, dirt);
            float b = lerpf(base * 0.90f, base * 0.55f, dirt);

            m.setAlbedo(x, y, r, g, b);
            m.height[(size_t)y * S + x] = 0.5f + weave * 0.35f;
            m.rough[(size_t)y * S + x] = 0.95f;
        }
    }
}

static void genSkin(Material& m) {
    const int S = m.size;
    const float inv = 8.0f / S;
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            float u = x * inv, v = y * inv;
            float pores = worley(u * 26.0f, v * 26.0f, 208);
            float base = 0.50f + fbm(u * 3.0f, v * 3.0f, 4, 24) * 0.07f;
            base -= smoothstepf(0.30f, 0.0f, pores) * 0.045f;
            float blotch = fbm(u * 1.6f, v * 1.6f, 3, 12);
            float r = base * (1.02f + blotch * 0.10f);
            float g = base * (0.80f + blotch * 0.05f);
            float b = base * (0.72f + blotch * 0.04f);
            // Grime: nobody down here has washed recently.
            float grime = smoothstepf(0.55f, 0.85f, fbm(u * 2.2f, v * 2.2f, 4, 18));
            r = lerpf(r, r * 0.55f, grime);
            g = lerpf(g, g * 0.52f, grime);
            b = lerpf(b, b * 0.50f, grime);
            m.setAlbedo(x, y, r, g, b);
            m.height[(size_t)y * S + x] = 0.5f + (1.0f - smoothstepf(0.3f, 0.0f, pores)) * 0.12f;
            m.rough[(size_t)y * S + x] = 0.62f + grime * 0.22f;
        }
    }
}

static void genWindow(Material& m) {
    const int S = m.size;
    const float inv = 8.0f / S;
    const int pane = S / 2;
    const int frame = std::max(3, S / 64);
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            float u = x * inv, v = y * inv;
            int xin = x % pane, yin = y % pane;
            bool isFrame = xin < frame || yin < frame;

            float r, g, b, h, rg, a = 1.0f;
            if (isFrame) {
                float fv = 0.13f + fbm(u * 12.0f, v * 12.0f, 3, 96) * 0.06f;
                r = fv; g = fv * 0.98f; b = fv * 0.92f;
                h = 0.85f;
                rg = 0.80f;
            } else {
                // Glass: dark, grimy, with a hint of what little light gets in.
                float grime = fbm(u * 4.0f, v * 4.0f, 4, 32);
                float streak = smoothstepf(0.45f, 0.85f, runoff(u, v, 7.0f, 56));
                float gl = 0.045f + grime * 0.05f + streak * 0.05f;
                r = gl * 0.85f; g = gl * 0.95f; b = gl * 1.15f;
                h = 0.55f;
                rg = lerpf(0.10f, 0.55f, saturate(grime + streak * 0.6f));

                // Some panes are broken out entirely.
                float broken = fbm(u * 0.7f + 3.1f, v * 0.7f, 3, 5);
                if (broken > 0.62f) {
                    float crack = 1.0f - smoothstepf(0.0f, 0.04f,
                        std::fabs(fbm(u * 6.0f, v * 6.0f, 4, 48) - 0.5f));
                    a = crack > 0.35f ? 1.0f : 0.0f;
                    r = g = b = 0.02f;
                }
            }
            m.setAlbedo(x, y, r, g, b, a);
            m.height[(size_t)y * S + x] = h;
            m.rough[(size_t)y * S + x] = rg;
        }
    }
}

static void genCarPaint(Material& m) {
    const int S = m.size;
    const float inv = 8.0f / S;
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            float u = x * inv, v = y * inv;
            float base = 0.20f + fbm(u * 1.5f, v * 1.5f, 3, 12) * 0.05f;
            // Scratches: fine directional lines through to primer.
            float scr = fbm(u * 40.0f, v * 2.0f, 3, 320);
            float scratch = smoothstepf(0.72f, 0.80f, scr);
            // Rust creeping from the panel edges and wheel arches.
            float rust = smoothstepf(0.60f, 0.85f, fbm(u * 2.4f, v * 2.4f, 4, 19));
            float r = lerpf(base * 1.05f, 0.30f, rust);
            float g = lerpf(base * 1.00f, 0.14f, rust);
            float b = lerpf(base * 0.98f, 0.07f, rust);
            r = lerpf(r, 0.34f, scratch * 0.5f);
            g = lerpf(g, 0.34f, scratch * 0.5f);
            b = lerpf(b, 0.34f, scratch * 0.5f);
            float dust = smoothstepf(0.4f, 0.8f, fbm(u * 3.0f, v * 3.0f, 3, 24));
            m.setAlbedo(x, y, r * (1.0f - dust * 0.25f),
                              g * (1.0f - dust * 0.24f),
                              b * (1.0f - dust * 0.22f));
            m.height[(size_t)y * S + x] = 0.6f + rust * 0.25f - scratch * 0.08f;
            m.rough[(size_t)y * S + x] = lerpf(0.28f, 0.92f, saturate(rust + dust * 0.5f));
        }
    }
}

static void genChainlink(Material& m) {
    const int S = m.size;
    const int cell = S / 16;
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            // Diamond mesh: two diagonal families of wires.
            float d1 = std::fabs(std::fmod((float)(x + y), (float)cell) - cell * 0.5f);
            float d2 = std::fabs(std::fmod((float)(x - y + S * 4), (float)cell) - cell * 0.5f);
            float wire = std::min(d1, d2);
            float thickness = std::max(1.2f, cell * 0.085f);
            float a = wire < thickness ? 1.0f : 0.0f;
            float shade = 1.0f - saturate(wire / thickness) * 0.45f;
            float u = x * 8.0f / S, v = y * 8.0f / S;
            float rust = smoothstepf(0.55f, 0.85f, fbm(u * 3.0f, v * 3.0f, 3, 24));
            float base = 0.26f * shade;
            m.setAlbedo(x, y, lerpf(base, 0.30f, rust),
                              lerpf(base * 1.02f, 0.15f, rust),
                              lerpf(base * 1.05f, 0.08f, rust), a);
            m.height[(size_t)y * S + x] = a > 0.5f ? 0.8f : 0.2f;
            m.rough[(size_t)y * S + x] = lerpf(0.55f, 0.95f, rust);
        }
    }
}

static void genRoadLine(Material& m) {
    const int S = m.size;
    const float inv = 8.0f / S;
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            float u = x * inv, v = y * inv;
            float agg = worley(u * 6.0f, v * 6.0f, 48);
            float stone = smoothstepf(0.28f, 0.05f, agg);
            float base = 0.085f + fbm(u * 2.0f, v * 2.0f, 4, 16) * 0.05f + stone * 0.05f;

            // A worn painted line down the centre of the tile.
            float band = 1.0f - smoothstepf(0.06f, 0.10f, std::fabs((float)x / S - 0.5f));
            float wear = smoothstepf(0.30f, 0.70f, fbm(u * 8.0f, v * 3.0f, 4, 64));
            float paint = band * wear;
            float r = lerpf(base, 0.52f, paint);
            float g = lerpf(base * 0.99f, 0.50f, paint);
            float b = lerpf(base * 1.02f, 0.40f, paint);
            m.setAlbedo(x, y, r, g, b);
            m.height[(size_t)y * S + x] = stone * 0.55f + base * 1.6f + paint * 0.12f;
            m.rough[(size_t)y * S + x] = lerpf(0.92f, 0.70f, paint);
        }
    }
}

static void genCeiling(Material& m) {
    const int S = m.size;
    const float inv = 8.0f / S;
    const int t = S / 2;
    const int rail = std::max(2, S / 128);
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            float u = x * inv, v = y * inv;
            int xin = x % t, yin = y % t;
            bool isRail = xin < rail || yin < rail;
            float base;
            if (isRail) {
                base = 0.20f;
            } else {
                // Mineral fibre tile: dense random perforations.
                float perf = worley(u * 30.0f, v * 30.0f, 240);
                base = 0.40f - smoothstepf(0.26f, 0.02f, perf) * 0.16f;
                base += fbm(u * 6.0f, v * 6.0f, 3, 48) * 0.05f;
            }
            // Water damage: brown rings spreading from leaks.
            float stainMask = fbm(u * 1.1f, v * 1.1f, 4, 9);
            float stain = smoothstepf(0.52f, 0.72f, stainMask);
            float ring = 1.0f - smoothstepf(0.0f, 0.05f, std::fabs(stainMask - 0.54f));
            float r = lerpf(base, base * 0.62f, stain) + ring * 0.05f;
            float g = lerpf(base * 0.99f, base * 0.48f, stain) + ring * 0.03f;
            float b = lerpf(base * 0.95f, base * 0.34f, stain);

            // A few tiles missing entirely, showing the void above.
            float missing = fbm(u * 0.6f + 7.3f, v * 0.6f, 2, 4);
            float a = 1.0f;
            if (!isRail && missing > 0.70f) { r = g = b = 0.012f; a = 1.0f; }

            m.setAlbedo(x, y, r, g, b, a);
            m.height[(size_t)y * S + x] = isRail ? 0.85f : 0.55f - stain * 0.08f;
            m.rough[(size_t)y * S + x] = 0.93f;
        }
    }
}

static void genBloodConcrete(Material& m) {
    genConcreteWall(m);
    const int S = m.size;
    const float inv = 8.0f / S;
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            float u = x * inv, v = y * inv;
            // Old, oxidised, and running downward. Bright red reads as a prop;
            // brown-black reads as something that happened a while ago.
            float mask = fbm(u * 2.0f, v * 0.8f, 5, 16);
            float spread = smoothstepf(0.54f, 0.74f, mask);
            float drip = smoothstepf(0.60f, 0.90f, runoff(u, v, 9.0f, 72)) * spread;
            float amt = saturate(spread * 0.8f + drip * 0.6f);
            size_t i = ((size_t)y * S + x) * 4;
            float r = m.albedo[i + 0] / 255.0f;
            float g = m.albedo[i + 1] / 255.0f;
            float b = m.albedo[i + 2] / 255.0f;
            r = lerpf(r, 0.115f, amt);
            g = lerpf(g, 0.030f, amt);
            b = lerpf(b, 0.022f, amt);
            m.setAlbedo(x, y, r, g, b);
            m.rough[(size_t)y * S + x] = lerpf(m.rough[(size_t)y * S + x], 0.45f, amt * 0.6f);
        }
    }
}

static void genDoor(Material& m) {
    const int S = m.size;
    const float inv = 8.0f / S;
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            float u = x * inv, v = y * inv;
            float fx = (float)x / S, fy = (float)y / S;
            // Two recessed panels, the standard interior door.
            float panel = 0.0f;
            auto rect = [&](float x0, float y0, float x1, float y1) {
                float ex = std::min(fx - x0, x1 - fx);
                float ey = std::min(fy - y0, y1 - fy);
                return smoothstepf(0.0f, 0.02f, std::min(ex, ey));
            };
            panel = std::max(rect(0.16f, 0.08f, 0.84f, 0.44f),
                             rect(0.16f, 0.54f, 0.84f, 0.92f));
            float grain = fbm(u * 2.0f, v * 22.0f, 4, 176);
            float base = lerpf(0.155f, 0.235f, grain);
            base *= 1.0f - panel * 0.16f;
            float scuff = smoothstepf(0.55f, 0.85f, fbm(u * 4.0f, v * 4.0f, 3, 32));
            base *= 1.0f - scuff * 0.25f;
            m.setAlbedo(x, y, base * 1.00f, base * 0.80f, base * 0.60f);
            m.height[(size_t)y * S + x] = 0.75f - panel * 0.30f + grain * 0.05f;
            m.rough[(size_t)y * S + x] = 0.78f + scuff * 0.14f;
        }
    }
}

static void genPaperDebris(Material& m) {
    const int S = m.size;
    const float inv = 8.0f / S;
    for (int y = 0; y < S; y++) {
        for (int x = 0; x < S; x++) {
            float u = x * inv, v = y * inv;
            float floorBase = 0.14f + fbm(u * 3.0f, v * 3.0f, 4, 24) * 0.06f;
            // Scattered sheets: overlapping bright rectangles at odd angles.
            float sheets = 0.0f;
            for (int k = 0; k < 5; k++) {
                float a = hash2(k * 13 + 1, 7) * TAU;
                float ox = hash2(k * 7 + 3, 11), oy = hash3(k, 5, 9);
                float rx = (std::fmod(u * 0.5f + ox, 1.0f) - 0.5f);
                float ry = (std::fmod(v * 0.5f + oy, 1.0f) - 0.5f);
                float cx = rx * std::cos(a) - ry * std::sin(a);
                float cy = rx * std::sin(a) + ry * std::cos(a);
                if (std::fabs(cx) < 0.10f && std::fabs(cy) < 0.14f) sheets = 1.0f;
            }
            float paper = 0.44f + fbm(u * 9.0f, v * 9.0f, 3, 72) * 0.10f;
            float r = lerpf(floorBase, paper, sheets);
            float g = lerpf(floorBase * 0.99f, paper * 0.98f, sheets);
            float b = lerpf(floorBase * 0.95f, paper * 0.90f, sheets);
            m.setAlbedo(x, y, r, g, b);
            m.height[(size_t)y * S + x] = floorBase * 1.2f + sheets * 0.18f;
            m.rough[(size_t)y * S + x] = 0.92f;
        }
    }
}

// ------------------------------------------------------------------- registry

static const MaterialDef kMaterials[] = {
    {"asphalt",      genAsphalt,       2.6f},
    {"sidewalk",     genSidewalk,      2.2f},
    {"brick",        genBrick,         3.4f},
    {"concrete",     genConcreteWall,  2.4f},
    {"corrugated",   genCorrugated,    4.0f},
    {"rustmetal",    genRustMetal,     2.8f},
    {"wood",         genWoodPlank,     2.4f},
    {"plaster",      genPlaster,       2.0f},
    {"tile",         genTileFloor,     2.6f},
    {"gravel",       genGravel,        3.2f},
    {"weeds",        genWeeds,         2.6f},
    {"flesh",        genFlesh,         2.2f},
    {"cloth",        genCloth,         1.8f},
    {"skin",         genSkin,          1.4f},
    {"window",       genWindow,        2.0f},
    {"carpaint",     genCarPaint,      1.6f},
    {"chainlink",    genChainlink,     2.4f},
    {"roadline",     genRoadLine,      2.6f},
    {"ceiling",      genCeiling,       2.0f},
    {"bloodconcrete",genBloodConcrete, 2.4f},
    {"door",         genDoor,          2.2f},
    {"debris",       genPaperDebris,   2.2f},
};

int materialCount() { return (int)(sizeof(kMaterials) / sizeof(kMaterials[0])); }
const MaterialDef& materialDef(int i) { return kMaterials[i]; }

} // namespace bake
