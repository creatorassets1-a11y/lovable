// Grid levels.
//
// A level is an ASCII map: cheap to author, trivially collidable, and it gives
// the AI a nav grid for free. Geometry is generated once into three batched
// meshes (walls, floor, ceiling) so the whole environment costs three draw calls
// rather than one per room.
#pragma once
#include <string>
#include <vector>

#include "../core/hmath.h"
#include "../gfx/renderer.h"

namespace hl {

using namespace hm;

constexpr float CELL = 2.0f;          // metres per grid cell
constexpr float WALL_H = 3.1f;        // floor to ceiling

enum class Cell : uint8_t {
    Void = 0,      // nothing; never rendered, always solid
    Floor,
    WallTile,      // ceramic-tiled ward wall
    WallPlaster,   // painted plaster
    WallConcrete,  // basement
    Door,          // open, with a frame
};

struct Spawn {
    char tag = 0;
    int x = 0, z = 0;
};

class Level {
public:
    /** Parse an ASCII map from assets and build its geometry. */
    bool load(const std::string& path);

    int width() const { return w_; }
    int depth() const { return d_; }
    Cell at(int x, int z) const {
        if (x < 0 || z < 0 || x >= w_ || z >= d_) return Cell::Void;
        return cells_[(size_t)z * w_ + x];
    }
    bool walkable(int x, int z) const {
        Cell c = at(x, z);
        return c == Cell::Floor || c == Cell::Door;
    }
    /** World-space solidity test used by movement. */
    bool solid_at(float wx, float wz) const {
        return !walkable((int)std::floor(wx / CELL), (int)std::floor(wz / CELL));
    }
    /** Straight-line visibility over the grid. */
    bool line_of_sight(v3 a, v3 b) const;

    v3 cell_center(int x, int z) const {
        return v3((x + 0.5f) * CELL, 0.0f, (z + 0.5f) * CELL);
    }

    const std::vector<Spawn>& spawns() const { return spawns_; }
    /** First spawn with the given tag, or false. */
    bool find_spawn(char tag, v3* out) const;
    std::vector<v3> all_spawns(char tag) const;

    const Mesh& walls() const { return walls_; }
    const Mesh& floor() const { return floor_; }
    const Mesh& ceiling() const { return ceiling_; }
    const Mesh& trim() const { return trim_; }

    /** Breadth-first flow field toward a target cell, for the Matron's pathing. */
    void build_flow(int tx, int tz, std::vector<int16_t>* out) const;
    /** Downhill step on a flow field. Returns false at the target or if stranded. */
    bool flow_step(const std::vector<int16_t>& flow, int x, int z, int* nx, int* nz) const;

private:
    void build_geometry();

    int w_ = 0, d_ = 0;
    std::vector<Cell> cells_;
    std::vector<Spawn> spawns_;
    Mesh walls_, floor_, ceiling_, trim_;
};

}  // namespace hl
