// world.h - the open city: streets, blocks, enterable buildings, interiors.
//
// One contiguous 320m square grid at 2m resolution. There are no loading
// screens between areas: buildings are carved out of the same grid the street
// lives in, so walking through a doorway is just walking onto different cells.
#pragma once
#include "hmath.h"
#include "mesh.h"
#include "matids.h"
#include <vector>

namespace hm {

enum CellType : uint8_t {
    CELL_SOLID = 0,     // building mass or a wall; blocks movement and sight
    CELL_ROAD,
    CELL_SIDEWALK,
    CELL_LOT,           // yards, car parks, rubble
    CELL_INTERIOR,      // inside a building; has a ceiling
    CELL_DOORWAY,       // threshold: walkable, counts as interior for lighting
    CELL_TYPE_COUNT
};

inline bool cellWalkable(uint8_t c) { return c != CELL_SOLID; }
inline bool cellIndoor(uint8_t c) { return c == CELL_INTERIOR || c == CELL_DOORWAY; }

struct Building {
    int x0, z0, x1, z1;     // inclusive cell bounds of the footprint
    float height;
    bool enterable;
    vec3 doorPos;
    int doorCX, doorCZ;
    int floorMat;
    int wallMat;
};

struct LightSrc {
    vec3 pos;
    vec3 color;
    float radius;
    float flicker;
    float phase;
    bool indoor;
    bool alive = true;
};

struct Prop {
    vec3 pos;
    float yaw;
    int kind;
};

// A block of the world, meshed and culled as a unit.
struct Chunk {
    Mesh mesh;
    vec3 mn, mx;
    int cx, cz;
};

struct World {
    int W = 0, H = 0;
    float cellSize = 2.0f;
    float interiorHeight = 3.3f;

    std::vector<uint8_t> cells;
    std::vector<uint8_t> groundMat;
    std::vector<uint16_t> buildingOf;   // 0xFFFF = none
    std::vector<Building> buildings;
    std::vector<LightSrc> lights;
    std::vector<Prop> props;

    // Flow field used by the AI, and a BFS scratch buffer.
    std::vector<uint16_t> flow;
    static constexpr uint16_t FLOW_INF = 0xFFFF;

    vec3 playerStart{0, 0, 0};

    void generate(uint32_t seed, int gridW, int gridH);
    void buildChunks(std::vector<Chunk>& out, int chunkCells) const;

    bool inBounds(int x, int z) const { return x >= 0 && z >= 0 && x < W && z < H; }
    uint8_t at(int x, int z) const { return inBounds(x, z) ? cells[z * W + x] : CELL_SOLID; }
    bool solid(int x, int z) const { return at(x, z) == CELL_SOLID; }
    bool indoorAt(const vec3& p) const {
        int x, z;
        worldToCell(p, x, z);
        return cellIndoor(at(x, z));
    }

    vec3 cellCenter(int x, int z) const {
        return {(x + 0.5f) * cellSize, 0.0f, (z + 0.5f) * cellSize};
    }
    void worldToCell(const vec3& p, int& x, int& z) const {
        x = (int)std::floor(p.x / cellSize);
        z = (int)std::floor(p.z / cellSize);
    }
    float extentX() const { return W * cellSize; }
    float extentZ() const { return H * cellSize; }

    vec3 resolveCollision(const vec3& desired, float radius) const;
    bool lineOfSight(const vec3& a, const vec3& b) const;

    void computeFlow(int targetX, int targetZ);
    bool flowStep(int x, int z, int& outX, int& outZ) const;

    // Finds a walkable cell near a world position; used to place things
    // without having to know the layout.
    bool findOpenNear(const vec3& near, float maxRadius, vec3& out, Rng& rng) const;

private:
    void layStreets(Rng& rng);
    void placeBuildings(Rng& rng);
    void carveInterior(Building& b, Rng& rng);
    void placeLightsAndProps(Rng& rng);
};

} // namespace hm
