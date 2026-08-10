// matids.h - layer indices into the material texture array.
//
// This order MUST match the registry at the bottom of tools/materials.cpp.
// The bake writes layers in that order and the runtime uploads them in the
// order they appear in the pack, so a mismatch silently retextures the world.
// tests/host_test.cpp asserts the two lists agree.
#pragma once

namespace hm {

enum MatId {
    MAT_ASPHALT = 0,
    MAT_SIDEWALK,
    MAT_BRICK,
    MAT_CONCRETE,
    MAT_CORRUGATED,
    MAT_RUSTMETAL,
    MAT_WOOD,
    MAT_PLASTER,
    MAT_TILE,
    MAT_GRAVEL,
    MAT_WEEDS,
    MAT_FLESH,
    MAT_CLOTH,
    MAT_SKIN,
    MAT_WINDOW,
    MAT_CARPAINT,
    MAT_CHAINLINK,
    MAT_ROADLINE,
    MAT_CEILING,
    MAT_BLOODCONCRETE,
    MAT_DOOR,
    MAT_DEBRIS,
    MAT_COUNT
};

// Names in registry order, used to look entries up in the pack.
static const char* const kMatNames[MAT_COUNT] = {
    "asphalt", "sidewalk", "brick", "concrete", "corrugated", "rustmetal",
    "wood", "plaster", "tile", "gravel", "weeds", "flesh", "cloth", "skin",
    "window", "carpaint", "chainlink", "roadline", "ceiling", "bloodconcrete",
    "door", "debris"
};

} // namespace hm
