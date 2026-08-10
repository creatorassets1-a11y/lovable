// world.h - procedural level: grid maze, collision, line-of-sight, nav field.
#pragma once
#include "hmath.h"
#include "mesh.h"
#include <vector>

namespace hm {

enum CellType : uint8_t {
    CELL_SOLID = 0,
    CELL_FLOOR = 1,
    CELL_DOOR  = 2,   // a doorway: walkable, but blocks light and sight less
};

struct LightSrc {
    vec3 pos;
    vec3 color;
    float radius;
    float flicker;    // 0 = steady, 1 = badly failing
    float phase;
    bool  alive = true;
};

struct Pickup {
    vec3 pos;
    bool taken = false;
    float bob = 0.0f;
};

struct Prop {
    vec3 pos;
    float yaw;
    int kind;         // 0 crate, 1 barrel, 2 locker, 3 pipe, 4 gurney
};

// Layout knobs that differ per chapter. Tuned so each episode feels distinct
// without needing a separate level format.
struct ChapterSpec {
    const char* title;
    const char* subtitle;
    const char* objectiveNoun;   // "FUSE", "TAPE", ...
    uint32_t seed;
    int gridW, gridH;
    int objectiveCount;
    int roomAttempts;
    float lightDensity;          // 0..1 fraction of junctions that get a lamp
    float stalkerSpeed;
    float stalkerHearing;        // metres
    float aggressionRate;        // how fast it stops being patient, per second
    float fogDensity;
    vec3 fogColor;
    vec3 wallTint;
};

const int CHAPTER_COUNT = 5;
const ChapterSpec& chapterSpec(int index);

struct World {
    int W = 0, H = 0;
    float cellSize = 3.0f;
    float wallHeight = 3.2f;
    std::vector<uint8_t> cells;

    std::vector<LightSrc> lights;
    std::vector<Pickup> pickups;
    std::vector<Prop> props;

    vec3 playerStart{0, 0, 0};
    vec3 exitPos{0, 0, 0};
    int  exitCX = 0, exitCZ = 0;

    // Flow field: BFS distance (in cells) to whatever the stalker is hunting.
    std::vector<uint16_t> flow;
    // constexpr so it is implicitly inline: flow.assign() takes it by
    // reference, which would otherwise need an out-of-class definition.
    static constexpr uint16_t FLOW_INF = 0xFFFF;

    void generate(const ChapterSpec& spec);
    void buildMesh(Mesh& out) const;

    bool inBounds(int x, int z) const { return x >= 0 && z >= 0 && x < W && z < H; }
    uint8_t at(int x, int z) const { return inBounds(x, z) ? cells[z * W + x] : CELL_SOLID; }
    bool solid(int x, int z) const { return at(x, z) == CELL_SOLID; }

    vec3 cellCenter(int x, int z) const {
        return {(x + 0.5f) * cellSize, 0.0f, (z + 0.5f) * cellSize};
    }
    void worldToCell(const vec3& p, int& x, int& z) const {
        x = (int)std::floor(p.x / cellSize);
        z = (int)std::floor(p.z / cellSize);
    }

    // Slides a circle of `radius` out of any solid cells it overlaps.
    vec3 resolveCollision(const vec3& desired, float radius) const;

    bool lineOfSight(const vec3& a, const vec3& b) const;

    void computeFlow(int targetX, int targetZ);
    // Best neighbouring cell to move to from (x,z) to descend the flow field.
    bool flowStep(int x, int z, int& outX, int& outZ) const;

private:
    void carveMaze(Rng& rng);
    void carveRooms(Rng& rng, int attempts);
    void openLoops(Rng& rng, int count);
    void placeContent(Rng& rng, const ChapterSpec& spec);
};

} // namespace hm
