#include "world.h"
#include <queue>

namespace hm {

static const ChapterSpec kChapters[CHAPTER_COUNT] = {
    // Later episodes are bigger, which on its own makes them calmer - there is
    // more map to hide in. The aggression ramp is raised to compensate so the
    // finale is the tightest chapter rather than the most spacious one.
    // title              subtitle                        noun     seed   W   H  obj rooms lightD spd hear  aggr    fogD fogColor              wallTint
    {"THE BASEMENT",     "EPISODE ONE",                  "FUSE",  1337u, 25, 25, 4,  10, 0.55f, 2.05f, 11.0f, 0.006f, 0.085f, {0.02f,0.02f,0.03f}, {0.44f,0.42f,0.38f}},
    {"THE WARD",         "EPISODE TWO",                  "CHART", 2711u, 29, 29, 5,  14, 0.42f, 2.25f, 13.0f, 0.009f, 0.100f, {0.03f,0.03f,0.03f}, {0.52f,0.53f,0.48f}},
    {"SUBLEVEL C",       "EPISODE THREE",                "CELL",  9091u, 31, 31, 5,  12, 0.32f, 2.45f, 15.0f, 0.013f, 0.115f, {0.02f,0.02f,0.02f}, {0.36f,0.35f,0.34f}},
    {"THE TUNNELS",      "EPISODE FOUR",                 "VALVE", 4423u, 35, 35, 6,   8, 0.24f, 2.65f, 17.0f, 0.018f, 0.140f, {0.01f,0.02f,0.02f}, {0.30f,0.31f,0.29f}},
    {"THE BROADCAST",    "EPISODE FIVE - SIGNAL FOUND",  "RELAY", 8081u, 37, 37, 6,  16, 0.18f, 2.90f, 20.0f, 0.024f, 0.155f, {0.02f,0.01f,0.01f}, {0.34f,0.28f,0.27f}},
};

const ChapterSpec& chapterSpec(int index) {
    if (index < 0) index = 0;
    if (index >= CHAPTER_COUNT) index = CHAPTER_COUNT - 1;
    return kChapters[index];
}

// ---------------------------------------------------------------- generation

void World::carveMaze(Rng& rng) {
    // Recursive backtracker over odd coordinates.
    std::vector<int> stack;
    int sx = 1, sz = 1;
    cells[sz * W + sx] = CELL_FLOOR;
    stack.push_back(sz * W + sx);

    const int dx[4] = {2, -2, 0, 0};
    const int dz[4] = {0, 0, 2, -2};

    while (!stack.empty()) {
        int cur = stack.back();
        int cx = cur % W, cz = cur / W;

        int order[4] = {0, 1, 2, 3};
        for (int i = 3; i > 0; i--) {
            int j = rng.rangei(0, i + 1);
            std::swap(order[i], order[j]);
        }

        bool moved = false;
        for (int k = 0; k < 4; k++) {
            int d = order[k];
            int nx = cx + dx[d], nz = cz + dz[d];
            if (nx <= 0 || nz <= 0 || nx >= W - 1 || nz >= H - 1) continue;
            if (cells[nz * W + nx] != CELL_SOLID) continue;
            // Knock out the wall between.
            cells[(cz + dz[d] / 2) * W + (cx + dx[d] / 2)] = CELL_FLOOR;
            cells[nz * W + nx] = CELL_FLOOR;
            stack.push_back(nz * W + nx);
            moved = true;
            break;
        }
        if (!moved) stack.pop_back();
    }
}

void World::carveRooms(Rng& rng, int attempts) {
    for (int i = 0; i < attempts; i++) {
        int rw = rng.rangei(3, 8);
        int rh = rng.rangei(3, 8);
        int rx = rng.rangei(1, std::max(2, W - rw - 1));
        int rz = rng.rangei(1, std::max(2, H - rh - 1));
        for (int z = rz; z < rz + rh && z < H - 1; z++) {
            for (int x = rx; x < rx + rw && x < W - 1; x++) {
                cells[z * W + x] = CELL_FLOOR;
            }
        }
    }
}

void World::openLoops(Rng& rng, int count) {
    // A perfect maze is tedious, not frightening: you always know the way back.
    // Punching loops means the stalker can come from behind you.
    int made = 0, guard = 0;
    while (made < count && guard++ < count * 60) {
        int x = rng.rangei(1, W - 1);
        int z = rng.rangei(1, H - 1);
        if (cells[z * W + x] != CELL_SOLID) continue;
        bool horiz = at(x - 1, z) != CELL_SOLID && at(x + 1, z) != CELL_SOLID;
        bool vert  = at(x, z - 1) != CELL_SOLID && at(x, z + 1) != CELL_SOLID;
        if (horiz != vert) {  // exactly one axis is a corridor pair
            cells[z * W + x] = CELL_DOOR;
            made++;
        }
    }
}

void World::placeContent(Rng& rng, const ChapterSpec& spec) {
    // Distance field from the top-left region, used to place the exit far away
    // and to spread objectives across the whole map rather than one corner.
    std::vector<int> dist(W * H, -1);
    int startIdx = -1;
    for (int z = 1; z < H - 1 && startIdx < 0; z++)
        for (int x = 1; x < W - 1 && startIdx < 0; x++)
            if (cells[z * W + x] != CELL_SOLID) startIdx = z * W + x;
    if (startIdx < 0) return;

    std::queue<int> q;
    dist[startIdx] = 0;
    q.push(startIdx);
    int farIdx = startIdx;
    while (!q.empty()) {
        int cur = q.front(); q.pop();
        if (dist[cur] > dist[farIdx]) farIdx = cur;
        int cx = cur % W, cz = cur / W;
        const int dx[4] = {1, -1, 0, 0}, dz[4] = {0, 0, 1, -1};
        for (int d = 0; d < 4; d++) {
            int nx = cx + dx[d], nz = cz + dz[d];
            if (!inBounds(nx, nz) || cells[nz * W + nx] == CELL_SOLID) continue;
            if (dist[nz * W + nx] >= 0) continue;
            dist[nz * W + nx] = dist[cur] + 1;
            q.push(nz * W + nx);
        }
    }

    playerStart = cellCenter(startIdx % W, startIdx / W);
    exitCX = farIdx % W;
    exitCZ = farIdx / W;
    exitPos = cellCenter(exitCX, exitCZ);

    int maxD = dist[farIdx];
    if (maxD < 1) maxD = 1;

    // Objectives: one per distance band, so you are forced to cross the map.
    for (int i = 0; i < spec.objectiveCount; i++) {
        int loD = (int)(maxD * (0.25f + 0.65f * i / (float)spec.objectiveCount));
        int hiD = (int)(maxD * (0.25f + 0.65f * (i + 1) / (float)spec.objectiveCount));
        std::vector<int> band;
        for (int c = 0; c < W * H; c++)
            if (dist[c] >= loD && dist[c] <= hiD) band.push_back(c);
        if (band.empty()) {
            for (int c = 0; c < W * H; c++) if (dist[c] > 0) band.push_back(c);
        }
        if (band.empty()) continue;
        int pick = band[rng.rangei(0, (int)band.size())];
        Pickup p;
        p.pos = cellCenter(pick % W, pick / W);
        p.pos.y = 0.75f;
        p.bob = rng.range(0.0f, TAU);
        pickups.push_back(p);
    }

    // Lights at junctions and open rooms.
    for (int z = 1; z < H - 1; z++) {
        for (int x = 1; x < W - 1; x++) {
            if (cells[z * W + x] == CELL_SOLID) continue;
            int open = 0;
            for (int d = 0; d < 4; d++) {
                const int dx[4] = {1, -1, 0, 0}, dz[4] = {0, 0, 1, -1};
                if (at(x + dx[d], z + dz[d]) != CELL_SOLID) open++;
            }
            if (open < 3) continue;
            if (rng.f01() > spec.lightDensity) continue;
            LightSrc L;
            L.pos = cellCenter(x, z);
            L.pos.y = wallHeight - 0.35f;
            // Sodium-ish, sickly. Occasional dying green one.
            bool bad = rng.f01() < 0.25f;
            L.color = bad ? vec3(0.35f, 0.55f, 0.40f) : vec3(0.95f, 0.78f, 0.52f);
            L.radius = rng.range(5.0f, 8.5f);
            L.flicker = rng.f01() < 0.4f ? rng.range(0.3f, 1.0f) : 0.0f;
            L.phase = rng.range(0.0f, TAU);
            lights.push_back(L);
        }
    }

    // Props hugging walls, so corridors stay navigable but read as lived-in.
    for (int z = 1; z < H - 1; z++) {
        for (int x = 1; x < W - 1; x++) {
            if (cells[z * W + x] == CELL_SOLID) continue;
            if (rng.f01() > 0.16f) continue;
            int wallDir = -1;
            const int dx[4] = {1, -1, 0, 0}, dz[4] = {0, 0, 1, -1};
            for (int d = 0; d < 4; d++)
                if (at(x + dx[d], z + dz[d]) == CELL_SOLID) { wallDir = d; break; }
            if (wallDir < 0) continue;
            Prop pr;
            pr.pos = cellCenter(x, z) + vec3(dx[wallDir] * cellSize * 0.32f, 0.0f, dz[wallDir] * cellSize * 0.32f);
            pr.yaw = rng.range(0.0f, TAU);
            pr.kind = rng.rangei(0, 5);
            props.push_back(pr);
        }
    }
}

void World::generate(const ChapterSpec& spec) {
    W = spec.gridW | 1;   // maze algorithm needs odd dimensions
    H = spec.gridH | 1;
    cells.assign(W * H, CELL_SOLID);
    lights.clear();
    pickups.clear();
    props.clear();

    Rng rng(spec.seed);
    carveMaze(rng);
    carveRooms(rng, spec.roomAttempts);
    openLoops(rng, (W * H) / 40);

    // Seal the border no matter what the room carver did.
    for (int x = 0; x < W; x++) { cells[x] = CELL_SOLID; cells[(H - 1) * W + x] = CELL_SOLID; }
    for (int z = 0; z < H; z++) { cells[z * W] = CELL_SOLID; cells[z * W + W - 1] = CELL_SOLID; }

    placeContent(rng, spec);
    flow.assign(W * H, FLOW_INF);
}

// -------------------------------------------------------------------- meshing

void World::buildMesh(Mesh& out) const {
    out.clear();
    const float uvScale = 0.45f;   // texture repeats roughly every 2.2m
    const float cs = cellSize;

    for (int z = 0; z < H; z++) {
        for (int x = 0; x < W; x++) {
            if (cells[z * W + x] == CELL_SOLID) continue;
            float x0 = x * cs, x1 = x0 + cs;
            float z0 = z * cs, z1 = z0 + cs;
            float uv = cs * uvScale;

            // Floor (facing up) and ceiling (facing down).
            out.quad({x0, 0, z1}, {x1, 0, z1}, {x1, 0, z0}, {x0, 0, z0}, uv, uv);
            out.quad({x0, wallHeight, z0}, {x1, wallHeight, z0},
                     {x1, wallHeight, z1}, {x0, wallHeight, z1}, uv, uv);

            float h = wallHeight;
            float uvh = h * uvScale;
            // Only emit a wall where the neighbour is solid: no hidden faces.
            if (at(x + 1, z) == CELL_SOLID)
                out.quad({x1, 0, z0}, {x1, 0, z1}, {x1, h, z1}, {x1, h, z0}, uv, uvh);
            if (at(x - 1, z) == CELL_SOLID)
                out.quad({x0, 0, z1}, {x0, 0, z0}, {x0, h, z0}, {x0, h, z1}, uv, uvh);
            if (at(x, z + 1) == CELL_SOLID)
                out.quad({x1, 0, z1}, {x0, 0, z1}, {x0, h, z1}, {x1, h, z1}, uv, uvh);
            if (at(x, z - 1) == CELL_SOLID)
                out.quad({x0, 0, z0}, {x1, 0, z0}, {x1, h, z0}, {x0, h, z0}, uv, uvh);
        }
    }
}

// ------------------------------------------------------------------ collision

vec3 World::resolveCollision(const vec3& desired, float radius) const {
    vec3 p = desired;
    int cx, cz;
    worldToCell(p, cx, cz);
    // Several passes: escaping one wall can push you into its neighbour, and a
    // corner needs both faces resolved before the position settles.
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
                    // Dead centre of a wall cell: eject along the shallowest axis.
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

    // Safety net. Normal movement steps are far smaller than a cell so this
    // never fires in play, but if anything ever does end up buried in geometry
    // (a teleport, a regenerated level) we put it back on walkable ground
    // instead of letting it fall through the world.
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
    // Amanatides & Woo grid traversal.
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
    while (guard++ < 1024) {
        if (cx == endX && cz == endZ) return true;
        // Distance at which the ray crosses into the next cell. If that is
        // already past the endpoint we have arrived without hitting anything.
        // (Testing tMax *after* stepping compares against the far side of the
        // new cell instead, which lets sight leak through the last wall.)
        float tEnter = std::min(tMaxX, tMaxZ);
        if (tEnter > dist) return true;

        if (std::fabs(tMaxX - tMaxZ) < 1e-4f * std::max(1.0f, tEnter)) {
            // The ray passes exactly through a cell corner. Stepping X-then-Z
            // gives a different answer from Z-then-X, so sight becomes
            // direction-dependent: the stalker could see you through a corner
            // you cannot see through, which feels like cheating because it is.
            // Treat the corner as blocked if either cell touching it is solid.
            // That is symmetric, and it also stops sight slipping diagonally
            // between two wall blocks that visually meet.
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
    flow.assign(W * H, FLOW_INF);
    if (!inBounds(targetX, targetZ) || solid(targetX, targetZ)) return;

    std::queue<int> q;
    flow[targetZ * W + targetX] = 0;
    q.push(targetZ * W + targetX);
    const int dx[4] = {1, -1, 0, 0}, dz[4] = {0, 0, 1, -1};
    while (!q.empty()) {
        int cur = q.front(); q.pop();
        int cx = cur % W, cz = cur / W;
        uint16_t nd = (uint16_t)(flow[cur] + 1);
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
