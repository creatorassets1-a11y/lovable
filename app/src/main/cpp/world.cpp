#include "world.h"
#include "noise.h"
#include <queue>

namespace hm {

// Block period: 18 cells of block, 4 cells of road, at 2m per cell. That gives
// 36m blocks and 8m streets, which reads as a real city at walking pace.
static const int BLOCK = 18;
static const int ROAD = 4;
static const int PERIOD = BLOCK + ROAD;

// ---------------------------------------------------------------- generation

void World::layStreets(Rng& rng) {
    // Everything starts as building mass and the streets are cut out of it.
    cells.assign(W * H, CELL_SOLID);
    groundMat.assign(W * H, MAT_CONCRETE);
    buildingOf.assign(W * H, 0xFFFF);

    for (int z = 0; z < H; z++) {
        for (int x = 0; x < W; x++) {
            int bx = x % PERIOD, bz = z % PERIOD;
            bool roadX = bx >= BLOCK;
            bool roadZ = bz >= BLOCK;
            if (roadX || roadZ) {
                cells[z * W + x] = CELL_ROAD;
                // Centre line only down the middle lane of a straight run.
                bool centreX = roadX && (bx == BLOCK + ROAD / 2) && !roadZ;
                bool centreZ = roadZ && (bz == BLOCK + ROAD / 2) && !roadX;
                groundMat[z * W + x] = (centreX || centreZ) ? MAT_ROADLINE : MAT_ASPHALT;
            }
        }
    }

    // Pavement ring inside each block edge.
    for (int z = 0; z < H; z++) {
        for (int x = 0; x < W; x++) {
            if (cells[z * W + x] != CELL_SOLID) continue;
            int bx = x % PERIOD, bz = z % PERIOD;
            if (bx == 0 || bx == BLOCK - 1 || bz == 0 || bz == BLOCK - 1) {
                cells[z * W + x] = CELL_SIDEWALK;
                groundMat[z * W + x] = MAT_SIDEWALK;
            }
        }
    }
    // Seal the district border now rather than after the buildings go in.
    // Doing it last used to let a building on the edge of the map put its only
    // door on row 0, which was then walled off - sealing the whole interior
    // behind a door that opened into solid rock.
    for (int x = 0; x < W; x++) {
        cells[x] = CELL_SOLID;
        cells[(H - 1) * W + x] = CELL_SOLID;
    }
    for (int z = 0; z < H; z++) {
        cells[z * W] = CELL_SOLID;
        cells[z * W + W - 1] = CELL_SOLID;
    }
    (void)rng;
}

void World::placeBuildings(Rng& rng) {
    int blocksX = (W + PERIOD - 1) / PERIOD;
    int blocksZ = (H + PERIOD - 1) / PERIOD;

    for (int bz = 0; bz < blocksZ; bz++) {
        for (int bx = 0; bx < blocksX; bx++) {
            int x0 = bx * PERIOD + 1;
            int z0 = bz * PERIOD + 1;
            int x1 = std::min(x0 + BLOCK - 3, W - 2);
            int z1 = std::min(z0 + BLOCK - 3, H - 2);
            if (x1 - x0 < 4 || z1 - z0 < 4) continue;

            float roll = rng.f01();
            if (roll < 0.16f) {
                // Empty lot: gravel, weeds, and a fence line implied by props.
                for (int z = z0; z <= z1; z++)
                    for (int x = x0; x <= x1; x++) {
                        cells[z * W + x] = CELL_LOT;
                        groundMat[z * W + x] = (rng.f01() < 0.45f) ? MAT_WEEDS : MAT_GRAVEL;
                    }
                continue;
            }

            // Otherwise one or two buildings sharing the block.
            int splits = (roll < 0.55f) ? 1 : 2;
            for (int s = 0; s < splits; s++) {
                int sx0 = x0, sx1 = x1, sz0 = z0, sz1 = z1;
                if (splits == 2) {
                    int mid = (x0 + x1) / 2;
                    if (s == 0) sx1 = mid - 1;
                    else sx0 = mid + 1;
                }
                if (sx1 - sx0 < 4 || sz1 - sz0 < 4) continue;

                Building b;
                b.x0 = sx0; b.z0 = sz0; b.x1 = sx1; b.z1 = sz1;
                b.height = rng.range(1.0f, 6.0f) * 3.6f + 0.8f;
                b.enterable = rng.f01() < 0.42f;
                b.doorCX = b.doorCZ = -1;
                float wr = rng.f01();
                b.wallMat = (wr < 0.45f) ? MAT_BRICK
                          : (wr < 0.75f) ? MAT_CONCRETE
                          : (wr < 0.90f) ? MAT_CORRUGATED : MAT_PLASTER;
                b.floorMat = (rng.f01() < 0.5f) ? MAT_TILE : MAT_DEBRIS;

                uint16_t id = (uint16_t)buildings.size();
                for (int z = sz0; z <= sz1; z++)
                    for (int x = sx0; x <= sx1; x++) {
                        cells[z * W + x] = CELL_SOLID;
                        buildingOf[z * W + x] = id;
                    }
                buildings.push_back(b);
                if (buildings.back().enterable) carveInterior(buildings.back(), rng);
            }
        }
    }
}

// Binary space partition of the footprint into rooms, then knock doorways
// between neighbours so the interior is fully connected, then one street door.
void World::carveInterior(Building& b, Rng& rng) {
    int ix0 = b.x0 + 1, iz0 = b.z0 + 1, ix1 = b.x1 - 1, iz1 = b.z1 - 1;
    // Too small to hold rooms. Nothing has been carved yet, so it simply stays
    // a solid block.
    if (ix1 - ix0 < 3 || iz1 - iz0 < 3) { b.enterable = false; return; }

    struct Room { int x0, z0, x1, z1; };
    std::vector<Room> rooms;
    std::vector<Room> queue;
    queue.push_back({ix0, iz0, ix1, iz1});

    while (!queue.empty()) {
        Room r = queue.back();
        queue.pop_back();
        int w = r.x1 - r.x0 + 1, h = r.z1 - r.z0 + 1;
        // Stop splitting when a room is small enough to feel like a room.
        if ((w <= 7 && h <= 7) || rooms.size() > 10 || w < 4 || h < 4) {
            rooms.push_back(r);
            continue;
        }
        bool splitX = (w > h) ? true : (h > w ? false : rng.f01() < 0.5f);
        if (splitX) {
            int cut = r.x0 + 2 + rng.rangei(1, std::max(2, w - 4));
            if (cut <= r.x0 + 1 || cut >= r.x1 - 1) { rooms.push_back(r); continue; }
            queue.push_back({r.x0, r.z0, cut - 1, r.z1});
            queue.push_back({cut + 1, r.z0, r.x1, r.z1});
            for (int z = r.z0; z <= r.z1; z++) cells[z * W + cut] = CELL_SOLID;
        } else {
            int cut = r.z0 + 2 + rng.rangei(1, std::max(2, h - 4));
            if (cut <= r.z0 + 1 || cut >= r.z1 - 1) { rooms.push_back(r); continue; }
            queue.push_back({r.x0, r.z0, r.x1, cut - 1});
            queue.push_back({r.x0, cut + 1, r.x1, r.z1});
            for (int x = r.x0; x <= r.x1; x++) cells[cut * W + x] = CELL_SOLID;
        }
    }

    for (const Room& r : rooms) {
        for (int z = r.z0; z <= r.z1; z++)
            for (int x = r.x0; x <= r.x1; x++) {
                if (!inBounds(x, z)) continue;
                cells[z * W + x] = CELL_INTERIOR;
                groundMat[z * W + x] = b.floorMat;
            }
    }

    // Connect the rooms: for every interior wall cell with interior on both
    // sides, open some of them. Then verify connectivity and force more open
    // if the flood fill does not reach everything.
    for (int pass = 0; pass < 6; pass++) {
        int opened = 0;
        for (int z = iz0; z <= iz1; z++) {
            for (int x = ix0; x <= ix1; x++) {
                if (cells[z * W + x] != CELL_SOLID) continue;
                bool horiz = at(x - 1, z) == CELL_INTERIOR && at(x + 1, z) == CELL_INTERIOR;
                bool vert = at(x, z - 1) == CELL_INTERIOR && at(x, z + 1) == CELL_INTERIOR;
                if (horiz == vert) continue;
                if (rng.f01() > (pass == 0 ? 0.30f : 0.55f)) continue;
                cells[z * W + x] = CELL_INTERIOR;
                groundMat[z * W + x] = b.floorMat;
                opened++;
            }
        }
        if (pass > 0 && opened == 0) break;
    }

    // Street door: pick a footprint edge cell that has open ground outside it.
    struct Cand { int x, z, ox, oz; };
    std::vector<Cand> cands;
    for (int x = b.x0; x <= b.x1; x++) {
        if (at(x, b.z0 + 1) == CELL_INTERIOR && cellWalkable(at(x, b.z0 - 1)))
            cands.push_back({x, b.z0, x, b.z0 - 1});
        if (at(x, b.z1 - 1) == CELL_INTERIOR && cellWalkable(at(x, b.z1 + 1)))
            cands.push_back({x, b.z1, x, b.z1 + 1});
    }
    for (int z = b.z0; z <= b.z1; z++) {
        if (at(b.x0 + 1, z) == CELL_INTERIOR && cellWalkable(at(b.x0 - 1, z)))
            cands.push_back({b.x0, z, b.x0 - 1, z});
        if (at(b.x1 - 1, z) == CELL_INTERIOR && cellWalkable(at(b.x1 + 1, z)))
            cands.push_back({b.x1, z, b.x1 + 1, z});
    }
    if (cands.empty()) {
        // No wall of this footprint faces open ground, so there is nowhere to
        // put a door. Fill the rooms back in rather than leaving a sealed void:
        // carved-but-unreachable interior is invisible from outside and shows
        // up only as cells the player can never stand on.
        b.enterable = false;
        uint16_t id = (uint16_t)(&b - buildings.data());
        for (int z = b.z0; z <= b.z1; z++)
            for (int x = b.x0; x <= b.x1; x++) {
                if (!inBounds(x, z)) continue;
                cells[z * W + x] = CELL_SOLID;
                buildingOf[z * W + x] = id;
            }
        return;
    }
    const Cand& c = cands[rng.rangei(0, (int)cands.size())];
    cells[c.z * W + c.x] = CELL_DOORWAY;
    groundMat[c.z * W + c.x] = b.floorMat;
    b.doorCX = c.x;
    b.doorCZ = c.z;
    b.doorPos = cellCenter(c.x, c.z);

    // Guarantee the whole interior is reachable from the door. Anything the
    // flood fill misses gets walled back off, so there are no rooms the player
    // can see into and never enter.
    std::vector<uint8_t> seen((size_t)(b.x1 - b.x0 + 1) * (b.z1 - b.z0 + 1), 0);
    auto idx = [&](int x, int z) { return (z - b.z0) * (b.x1 - b.x0 + 1) + (x - b.x0); };
    std::queue<int> q;
    q.push(c.z * W + c.x);
    seen[idx(c.x, c.z)] = 1;
    while (!q.empty()) {
        int cur = q.front(); q.pop();
        int cx = cur % W, cz = cur / W;
        const int dx[4] = {1, -1, 0, 0}, dz[4] = {0, 0, 1, -1};
        for (int d = 0; d < 4; d++) {
            int nx = cx + dx[d], nz = cz + dz[d];
            if (nx < b.x0 || nx > b.x1 || nz < b.z0 || nz > b.z1) continue;
            if (!cellIndoor(at(nx, nz))) continue;
            if (seen[idx(nx, nz)]) continue;
            seen[idx(nx, nz)] = 1;
            q.push(nz * W + nx);
        }
    }
    for (int z = b.z0; z <= b.z1; z++)
        for (int x = b.x0; x <= b.x1; x++)
            if (cellIndoor(at(x, z)) && !seen[idx(x, z)]) {
                cells[z * W + x] = CELL_SOLID;
                buildingOf[z * W + x] = (uint16_t)(&b - buildings.data());
            }
}

void World::placeLightsAndProps(Rng& rng) {
    // Street lights on the pavement at regular intervals: most are dead, which
    // is the point - the ones still working become islands you move between.
    for (int z = 0; z < H; z++) {
        for (int x = 0; x < W; x++) {
            if (cells[z * W + x] != CELL_SIDEWALK) continue;
            int bx = x % PERIOD, bz = z % PERIOD;
            bool corner = (bx == 0 || bx == BLOCK - 1) && (bz == 0 || bz == BLOCK - 1);
            if (corner) continue;
            if ((x % 9 != 0) && (z % 9 != 0)) continue;
            if (rng.f01() > 0.55f) continue;

            Prop p;
            p.pos = cellCenter(x, z);
            p.yaw = 0.0f;
            p.kind = 0;   // lamp post
            props.push_back(p);

            if (rng.f01() < 0.42f) {
                LightSrc L;
                L.pos = cellCenter(x, z) + vec3(0, 5.2f, 0);
                L.color = vec3(0.98f, 0.76f, 0.44f);
                L.radius = rng.range(9.0f, 15.0f);
                L.flicker = rng.f01() < 0.45f ? rng.range(0.3f, 1.0f) : 0.0f;
                L.phase = rng.range(0.0f, TAU);
                L.indoor = false;
                lights.push_back(L);
            }
        }
    }

    // Interior lights: one per few interior cells, sicklier than the street.
    for (int z = 1; z < H - 1; z++) {
        for (int x = 1; x < W - 1; x++) {
            if (cells[z * W + x] != CELL_INTERIOR) continue;
            if (rng.f01() > 0.045f) continue;
            LightSrc L;
            L.pos = cellCenter(x, z) + vec3(0, interiorHeight - 0.25f, 0);
            bool bad = rng.f01() < 0.35f;
            L.color = bad ? vec3(0.40f, 0.58f, 0.44f) : vec3(0.92f, 0.80f, 0.58f);
            L.radius = rng.range(5.0f, 8.5f);
            L.flicker = rng.f01() < 0.5f ? rng.range(0.35f, 1.0f) : 0.0f;
            L.phase = rng.range(0.0f, TAU);
            L.indoor = true;
            lights.push_back(L);
        }
    }

    // Street clutter and interior furniture.
    for (int z = 1; z < H - 1; z++) {
        for (int x = 1; x < W - 1; x++) {
            uint8_t c = cells[z * W + x];
            if (!cellWalkable(c)) continue;

            float chance = (c == CELL_ROAD) ? 0.020f
                         : (c == CELL_SIDEWALK) ? 0.055f
                         : (c == CELL_LOT) ? 0.10f
                         : (c == CELL_INTERIOR) ? 0.10f : 0.0f;
            if (rng.f01() > chance) continue;

            // Keep the middle of the road clear so it stays readable as a route.
            bool nextToWall = solid(x + 1, z) || solid(x - 1, z) ||
                              solid(x, z + 1) || solid(x, z - 1);
            if (c == CELL_ROAD && !nextToWall && rng.f01() < 0.7f) continue;

            Prop p;
            p.pos = cellCenter(x, z);
            p.yaw = rng.range(0.0f, TAU);
            if (c == CELL_ROAD) {
                p.kind = 1;                                  // wrecked car
            } else if (c == CELL_SIDEWALK) {
                p.kind = rng.f01() < 0.5f ? 2 : 3;           // bin, barrier
            } else if (c == CELL_LOT) {
                p.kind = rng.f01() < 0.4f ? 4 : (rng.f01() < 0.5f ? 5 : 2);  // pallet, rubble
            } else {
                p.kind = 6 + rng.rangei(0, 4);               // interior furniture
            }
            props.push_back(p);
        }
    }
}

void World::generate(uint32_t seed, int gridW, int gridH) {
    W = gridW;
    H = gridH;
    buildings.clear();
    lights.clear();
    props.clear();

    Rng rng(seed ? seed : 1u);
    layStreets(rng);
    placeBuildings(rng);
    placeLightsAndProps(rng);

    // Spawn on a road near the middle.
    Rng srng(seed * 7919u + 13u);
    vec3 mid((W * 0.5f) * cellSize, 0, (H * 0.5f) * cellSize);
    if (!findOpenNear(mid, 40.0f, playerStart, srng)) {
        for (int z = 1; z < H - 1 && playerStart.x == 0.0f; z++)
            for (int x = 1; x < W - 1; x++)
                if (cells[z * W + x] == CELL_ROAD) { playerStart = cellCenter(x, z); break; }
    }
    flow.assign((size_t)W * H, FLOW_INF);
}

bool World::findOpenNear(const vec3& near, float maxRadius, vec3& out, Rng& rng) const {
    int cx, cz;
    worldToCell(near, cx, cz);
    int maxCells = (int)(maxRadius / cellSize);
    for (int tries = 0; tries < 400; tries++) {
        int r = rng.rangei(0, std::max(1, maxCells));
        float a = rng.range(0.0f, TAU);
        int x = cx + (int)(std::cos(a) * r);
        int z = cz + (int)(std::sin(a) * r);
        if (!inBounds(x, z) || solid(x, z)) continue;
        out = cellCenter(x, z);
        return true;
    }
    // Deterministic fallback: spiral outward.
    for (int r = 0; r < maxCells; r++) {
        for (int dz = -r; dz <= r; dz++) {
            for (int dx = -r; dx <= r; dx++) {
                if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
                int x = cx + dx, z = cz + dz;
                if (!inBounds(x, z) || solid(x, z)) continue;
                out = cellCenter(x, z);
                return true;
            }
        }
    }
    return false;
}

// ------------------------------------------------------------------- meshing

// Cheap ambient occlusion: a vertex sitting in a corner where several
// neighbouring cells are solid gets darkened. It costs nothing at runtime and
// does more for the sense of solidity than another light would.
static float cornerAO(const World& w, int x, int z, int dx, int dz) {
    int a = w.solid(x + dx, z) ? 1 : 0;
    int b = w.solid(x, z + dz) ? 1 : 0;
    int c = w.solid(x + dx, z + dz) ? 1 : 0;
    int n = a + b + ((a && b) ? 1 : c);
    return 1.0f - n * 0.16f;
}

void World::buildChunks(std::vector<Chunk>& out, int chunkCells) const {
    out.clear();
    int nx = (W + chunkCells - 1) / chunkCells;
    int nz = (H + chunkCells - 1) / chunkCells;
    const float cs = cellSize;
    const float uvScale = 0.42f;

    for (int cz = 0; cz < nz; cz++) {
        for (int cx = 0; cx < nx; cx++) {
            Chunk chunk;
            chunk.cx = cx;
            chunk.cz = cz;
            int x0 = cx * chunkCells, x1 = std::min(W, x0 + chunkCells);
            int z0 = cz * chunkCells, z1 = std::min(H, z0 + chunkCells);
            float maxY = 0.5f;

            for (int z = z0; z < z1; z++) {
                for (int x = x0; x < x1; x++) {
                    uint8_t c = cells[z * W + x];
                    if (!cellWalkable(c)) continue;
                    float fx0 = x * cs, fx1 = fx0 + cs;
                    float fz0 = z * cs, fz1 = fz0 + cs;
                    float uv = cs * uvScale;
                    float gm = (float)groundMat[z * W + x];

                    float a00 = cornerAO(*this, x, z, -1, -1);
                    float a10 = cornerAO(*this, x, z, 1, -1);
                    float a11 = cornerAO(*this, x, z, 1, 1);
                    float a01 = cornerAO(*this, x, z, -1, 1);

                    // Floor.
                    chunk.mesh.quad({fx0, 0, fz1}, {fx1, 0, fz1}, {fx1, 0, fz0}, {fx0, 0, fz0},
                                    gm, uv, uv, a01, a11, a10, a00);

                    // Ceiling, indoors only.
                    if (cellIndoor(c)) {
                        float h = interiorHeight;
                        chunk.mesh.quad({fx0, h, fz0}, {fx1, h, fz0}, {fx1, h, fz1}, {fx0, h, fz1},
                                        (float)MAT_CEILING, uv, uv);
                        maxY = std::max(maxY, h);
                    }

                    // Walls: emitted from the walkable side, so no hidden faces.
                    const int dxs[4] = {1, -1, 0, 0};
                    const int dzs[4] = {0, 0, 1, -1};
                    for (int d = 0; d < 4; d++) {
                        int wx = x + dxs[d], wz = z + dzs[d];
                        if (!solid(wx, wz)) continue;

                        // How tall this face is: indoors it stops at the
                        // ceiling, outdoors it runs to the top of the building.
                        float wallH = interiorHeight;
                        int wallMat = MAT_PLASTER;
                        if (!cellIndoor(c)) {
                            uint16_t bid = inBounds(wx, wz) ? buildingOf[wz * W + wx] : 0xFFFF;
                            if (bid != 0xFFFF && bid < buildings.size()) {
                                wallH = buildings[bid].height;
                                wallMat = buildings[bid].wallMat;
                            } else {
                                wallH = 4.5f;
                                wallMat = MAT_CONCRETE;
                            }
                        } else {
                            uint16_t bid = inBounds(wx, wz) ? buildingOf[wz * W + wx] : 0xFFFF;
                            if (bid != 0xFFFF && bid < buildings.size())
                                wallMat = buildings[bid].wallMat == MAT_CORRUGATED
                                        ? MAT_CORRUGATED : MAT_PLASTER;
                        }
                        maxY = std::max(maxY, wallH);
                        float uvh = wallH * uvScale;

                        if (d == 0)
                            chunk.mesh.quad({fx1, 0, fz0}, {fx1, 0, fz1}, {fx1, wallH, fz1}, {fx1, wallH, fz0},
                                            (float)wallMat, uv, uvh, 0.72f, 0.72f, 1.0f, 1.0f);
                        else if (d == 1)
                            chunk.mesh.quad({fx0, 0, fz1}, {fx0, 0, fz0}, {fx0, wallH, fz0}, {fx0, wallH, fz1},
                                            (float)wallMat, uv, uvh, 0.72f, 0.72f, 1.0f, 1.0f);
                        else if (d == 2)
                            chunk.mesh.quad({fx1, 0, fz1}, {fx0, 0, fz1}, {fx0, wallH, fz1}, {fx1, wallH, fz1},
                                            (float)wallMat, uv, uvh, 0.72f, 0.72f, 1.0f, 1.0f);
                        else
                            chunk.mesh.quad({fx0, 0, fz0}, {fx1, 0, fz0}, {fx1, wallH, fz0}, {fx0, wallH, fz0},
                                            (float)wallMat, uv, uvh, 0.72f, 0.72f, 1.0f, 1.0f);

                        // Windows: a band of glass on the upper floors of the
                        // outward faces, so the skyline is not blank masonry.
                        if (!cellIndoor(c) && wallH > 6.0f) {
                            for (int storey = 1; storey * 3.6f + 2.6f < wallH; storey++) {
                                float wy0 = storey * 3.6f + 0.9f;
                                float wy1 = wy0 + 1.5f;
                                float eps = 0.02f;
                                if (d == 0)
                                    chunk.mesh.quad({fx1 + eps, wy0, fz0 + 0.3f}, {fx1 + eps, wy0, fz1 - 0.3f},
                                                    {fx1 + eps, wy1, fz1 - 0.3f}, {fx1 + eps, wy1, fz0 + 0.3f},
                                                    (float)MAT_WINDOW, 1.0f, 1.0f);
                                else if (d == 1)
                                    chunk.mesh.quad({fx0 - eps, wy0, fz1 - 0.3f}, {fx0 - eps, wy0, fz0 + 0.3f},
                                                    {fx0 - eps, wy1, fz0 + 0.3f}, {fx0 - eps, wy1, fz1 - 0.3f},
                                                    (float)MAT_WINDOW, 1.0f, 1.0f);
                                else if (d == 2)
                                    chunk.mesh.quad({fx1 - 0.3f, wy0, fz1 + eps}, {fx0 + 0.3f, wy0, fz1 + eps},
                                                    {fx0 + 0.3f, wy1, fz1 + eps}, {fx1 - 0.3f, wy1, fz1 + eps},
                                                    (float)MAT_WINDOW, 1.0f, 1.0f);
                                else
                                    chunk.mesh.quad({fx0 + 0.3f, wy0, fz0 - eps}, {fx1 - 0.3f, wy0, fz0 - eps},
                                                    {fx1 - 0.3f, wy1, fz0 - eps}, {fx0 + 0.3f, wy1, fz0 - eps},
                                                    (float)MAT_WINDOW, 1.0f, 1.0f);
                            }
                        }
                    }
                }
            }

            // Roofs: one quad per building cell whose neighbour is lower or
            // open, which caps the silhouette without tiling the whole roof.
            for (int z = z0; z < z1; z++) {
                for (int x = x0; x < x1; x++) {
                    if (cells[z * W + x] != CELL_SOLID) continue;
                    uint16_t bid = buildingOf[z * W + x];
                    if (bid == 0xFFFF || bid >= buildings.size()) continue;
                    float h = buildings[bid].height;
                    float fx0 = x * cs, fx1 = fx0 + cs;
                    float fz0 = z * cs, fz1 = fz0 + cs;
                    chunk.mesh.quad({fx0, h, fz1}, {fx1, h, fz1}, {fx1, h, fz0}, {fx0, h, fz0},
                                    (float)MAT_RUSTMETAL, cs * uvScale, cs * uvScale);
                    maxY = std::max(maxY, h);
                }
            }

            if (chunk.mesh.empty()) continue;
            chunk.mn = vec3(x0 * cs, 0.0f, z0 * cs);
            chunk.mx = vec3(x1 * cs, maxY, z1 * cs);
            out.push_back(std::move(chunk));
        }
    }
}

// ------------------------------------------------------------------ collision

vec3 World::resolveCollision(const vec3& desired, float radius) const {
    vec3 p = desired;
    int cx, cz;
    for (int pass = 0; pass < 4; pass++) {
        worldToCell(p, cx, cz);
        for (int dz = -1; dz <= 1; dz++) {
            for (int dx = -1; dx <= 1; dx++) {
                int x = cx + dx, z = cz + dz;
                if (!solid(x, z)) continue;
                float minX = x * cellSize, maxX = minX + cellSize;
                float minZ = z * cellSize, maxZ = minZ + cellSize;
                float qx = clampf(p.x, minX, maxX);
                float qz = clampf(p.z, minZ, maxZ);
                float ddx = p.x - qx, ddz = p.z - qz;
                float d2 = ddx * ddx + ddz * ddz;
                if (d2 >= radius * radius) continue;
                if (d2 > 1e-6f) {
                    float d = std::sqrt(d2);
                    float push = radius - d;
                    p.x += (ddx / d) * push;
                    p.z += (ddz / d) * push;
                } else {
                    float toL = p.x - minX, toR = maxX - p.x;
                    float toB = p.z - minZ, toT = maxZ - p.z;
                    float m = std::min(std::min(toL, toR), std::min(toB, toT));
                    if (m == toL) p.x = minX - radius;
                    else if (m == toR) p.x = maxX + radius;
                    else if (m == toB) p.z = minZ - radius;
                    else p.z = maxZ + radius;
                }
            }
        }
    }
    worldToCell(p, cx, cz);
    if (solid(cx, cz)) {
        float bestD = 1e30f;
        vec3 best = p;
        bool found = false;
        for (int r = 1; r <= 3 && !found; r++) {
            for (int dz = -r; dz <= r; dz++) {
                for (int dx = -r; dx <= r; dx++) {
                    if (std::max(std::abs(dx), std::abs(dz)) != r) continue;
                    int x = cx + dx, z = cz + dz;
                    if (!inBounds(x, z) || solid(x, z)) continue;
                    vec3 c = cellCenter(x, z);
                    float d = lengthSq(vec3(c.x - p.x, 0, c.z - p.z));
                    if (d < bestD) { bestD = d; best = c; found = true; }
                }
            }
        }
        if (found) { p.x = best.x; p.z = best.z; }
    }
    return p;
}

bool World::lineOfSight(const vec3& a, const vec3& b) const {
    float x = a.x / cellSize, z = a.z / cellSize;
    float ex = b.x / cellSize, ez = b.z / cellSize;
    int cx = (int)std::floor(x), cz = (int)std::floor(z);
    int endX = (int)std::floor(ex), endZ = (int)std::floor(ez);

    float dx = ex - x, dz = ez - z;
    float dist = std::sqrt(dx * dx + dz * dz);
    if (dist < 1e-5f) return !solid(cx, cz);
    dx /= dist; dz /= dist;

    int stepX = dx > 0 ? 1 : -1;
    int stepZ = dz > 0 ? 1 : -1;
    float tMaxX = std::fabs(dx) < 1e-8f ? 1e30f
        : ((dx > 0 ? (cx + 1 - x) : (x - cx)) / std::fabs(dx));
    float tMaxZ = std::fabs(dz) < 1e-8f ? 1e30f
        : ((dz > 0 ? (cz + 1 - z) : (z - cz)) / std::fabs(dz));
    float tDeltaX = std::fabs(dx) < 1e-8f ? 1e30f : 1.0f / std::fabs(dx);
    float tDeltaZ = std::fabs(dz) < 1e-8f ? 1e30f : 1.0f / std::fabs(dz);

    int guard = 0;
    while (guard++ < 4096) {
        if (cx == endX && cz == endZ) return true;
        float tEnter = std::min(tMaxX, tMaxZ);
        if (tEnter > dist) return true;

        if (std::fabs(tMaxX - tMaxZ) < 1e-4f * std::max(1.0f, tEnter)) {
            // Exact corner crossing. Stepping X-then-Z gives a different answer
            // from Z-then-X, so sight would become direction-dependent: the
            // creature could see you through a corner you cannot see through.
            // Blocking when either cell touching the corner is solid is both
            // symmetric and stops sight slipping diagonally between two walls.
            if (solid(cx + stepX, cz) || solid(cx, cz + stepZ)) return false;
            cx += stepX;
            cz += stepZ;
            tMaxX += tDeltaX;
            tMaxZ += tDeltaZ;
        } else if (tMaxX < tMaxZ) {
            cx += stepX;
            tMaxX += tDeltaX;
        } else {
            cz += stepZ;
            tMaxZ += tDeltaZ;
        }
        if (solid(cx, cz)) return false;
    }
    return false;
}

// ----------------------------------------------------------------- navigation

void World::computeFlow(int targetX, int targetZ) {
    flow.assign((size_t)W * H, FLOW_INF);
    if (!inBounds(targetX, targetZ) || solid(targetX, targetZ)) return;

    std::queue<int> q;
    flow[targetZ * W + targetX] = 0;
    q.push(targetZ * W + targetX);
    const int dx[4] = {1, -1, 0, 0}, dz[4] = {0, 0, 1, -1};
    while (!q.empty()) {
        int cur = q.front(); q.pop();
        int cx = cur % W, cz = cur / W;
        uint16_t nd = (uint16_t)(flow[cur] + 1);
        if (nd == FLOW_INF) continue;
        for (int d = 0; d < 4; d++) {
            int nx = cx + dx[d], nz = cz + dz[d];
            if (!inBounds(nx, nz) || solid(nx, nz)) continue;
            if (flow[nz * W + nx] <= nd) continue;
            flow[nz * W + nx] = nd;
            q.push(nz * W + nx);
        }
    }
}

bool World::flowStep(int x, int z, int& outX, int& outZ) const {
    if (!inBounds(x, z)) return false;
    uint16_t best = flow[z * W + x];
    if (best == FLOW_INF) return false;
    bool found = false;
    const int dx[4] = {1, -1, 0, 0}, dz[4] = {0, 0, 1, -1};
    for (int d = 0; d < 4; d++) {
        int nx = x + dx[d], nz = z + dz[d];
        if (!inBounds(nx, nz) || solid(nx, nz)) continue;
        uint16_t f = flow[nz * W + nx];
        if (f < best) { best = f; outX = nx; outZ = nz; found = true; }
    }
    return found;
}

} // namespace hm
