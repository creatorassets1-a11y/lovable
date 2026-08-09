#include "level.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>

#include "../core/asset.h"

namespace hl {

namespace {

/** Push one quad (four corners, CCW) into a 12-float-per-vertex stream. */
void quad(std::vector<float>& v, std::vector<uint32_t>& idx,
          v3 a, v3 b, v3 c, v3 d, v3 n, v3 tan, v2 uv0, v2 uv1) {
    uint32_t base = (uint32_t)(v.size() / 12);
    const v3 corners[4] = {a, b, c, d};
    const v2 uvs[4] = {
        {uv0.x, uv0.y}, {uv1.x, uv0.y}, {uv1.x, uv1.y}, {uv0.x, uv1.y},
    };
    for (int i = 0; i < 4; i++) {
        v.push_back(corners[i].x); v.push_back(corners[i].y); v.push_back(corners[i].z);
        v.push_back(n.x); v.push_back(n.y); v.push_back(n.z);
        v.push_back(tan.x); v.push_back(tan.y); v.push_back(tan.z);
        v.push_back(1.0f);
        v.push_back(uvs[i].x); v.push_back(uvs[i].y);
    }
    // Corners are listed clockwise when viewed from the front face, so the
    // triangles are emitted reversed to come out counter-clockwise (GL_CCW).
    // Getting this backwards culls every surface and renders a black frame.
    idx.push_back(base); idx.push_back(base + 2); idx.push_back(base + 1);
    idx.push_back(base); idx.push_back(base + 3); idx.push_back(base + 2);
}

}  // namespace

bool Level::load(const std::string& path) {
    std::string text = asset_read_text(path);
    if (text.empty()) {
        loge("level missing: %s", path.c_str());
        return false;
    }

    std::vector<std::string> lines;
    std::string cur;
    for (char c : text) {
        if (c == '\n') { lines.push_back(cur); cur.clear(); }
        else if (c != '\r') { cur.push_back(c); }
    }
    if (!cur.empty()) lines.push_back(cur);

    // Ignore comment and blank lines so maps can be annotated.
    std::vector<std::string> rows;
    for (auto& l : lines) {
        if (l.empty() || l[0] == ';') continue;
        rows.push_back(l);
    }
    if (rows.empty()) { loge("level empty: %s", path.c_str()); return false; }

    d_ = (int)rows.size();
    w_ = 0;
    for (auto& r : rows) w_ = std::max(w_, (int)r.size());
    cells_.assign((size_t)w_ * d_, Cell::Void);
    spawns_.clear();

    for (int z = 0; z < d_; z++) {
        const std::string& r = rows[z];
        for (int x = 0; x < w_; x++) {
            char c = x < (int)r.size() ? r[x] : ' ';
            Cell cell = Cell::Void;
            switch (c) {
                case '#': cell = Cell::WallTile; break;
                case '%': cell = Cell::WallPlaster; break;
                case '=': cell = Cell::WallConcrete; break;
                case '.': cell = Cell::Floor; break;
                case 'D': cell = Cell::Door; break;
                case ' ': cell = Cell::Void; break;
                default:
                    // Anything else is a tagged spawn standing on open floor.
                    cell = Cell::Floor;
                    spawns_.push_back({c, x, z});
                    break;
            }
            cells_[(size_t)z * w_ + x] = cell;
        }
    }

    build_geometry();
    logi("level %s: %dx%d cells, %d spawns, %d wall tris", path.c_str(), w_, d_,
         (int)spawns_.size(), walls_.triangles());
    return true;
}

void Level::build_geometry() {
    std::vector<float> wv, fv, cv, tv;
    std::vector<uint32_t> wi, fi, ci, ti;

    const float S = CELL;
    const float H = WALL_H;

    for (int z = 0; z < d_; z++) {
        for (int x = 0; x < w_; x++) {
            if (!walkable(x, z)) continue;

            float x0 = x * S, x1 = x0 + S;
            float z0 = z * S, z1 = z0 + S;

            // Floor. UVs in world units so tiling is continuous across cells.
            quad(fv, fi,
                 {x0, 0, z0}, {x1, 0, z0}, {x1, 0, z1}, {x0, 0, z1},
                 {0, 1, 0}, {1, 0, 0},
                 {x0 * 0.5f, z0 * 0.5f}, {x1 * 0.5f, z1 * 0.5f});

            // Ceiling, wound the other way so it faces down.
            quad(cv, ci,
                 {x0, H, z1}, {x1, H, z1}, {x1, H, z0}, {x0, H, z0},
                 {0, -1, 0}, {1, 0, 0},
                 {x0 * 0.5f, z1 * 0.5f}, {x1 * 0.5f, z0 * 0.5f});

            // Walls: one face per neighbouring solid cell.
            struct Side { int dx, dz; };
            const Side sides[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (const Side& s : sides) {
                if (walkable(x + s.dx, z + s.dz)) continue;

                v3 a, b, c, d, n, tan;
                float u0, u1;
                if (s.dx == 1) {
                    a = {x1, 0, z1}; b = {x1, 0, z0}; c = {x1, H, z0}; d = {x1, H, z1};
                    n = {-1, 0, 0}; tan = {0, 0, -1}; u0 = z1 * 0.5f; u1 = z0 * 0.5f;
                } else if (s.dx == -1) {
                    a = {x0, 0, z0}; b = {x0, 0, z1}; c = {x0, H, z1}; d = {x0, H, z0};
                    n = {1, 0, 0}; tan = {0, 0, 1}; u0 = z0 * 0.5f; u1 = z1 * 0.5f;
                } else if (s.dz == 1) {
                    a = {x0, 0, z1}; b = {x1, 0, z1}; c = {x1, H, z1}; d = {x0, H, z1};
                    n = {0, 0, -1}; tan = {1, 0, 0}; u0 = x0 * 0.5f; u1 = x1 * 0.5f;
                } else {
                    a = {x1, 0, z0}; b = {x0, 0, z0}; c = {x0, H, z0}; d = {x1, H, z0};
                    n = {0, 0, 1}; tan = {-1, 0, 0}; u0 = x1 * 0.5f; u1 = x0 * 0.5f;
                }
                quad(wv, wi, a, b, c, d, n, tan, {u0, 0.0f}, {u1, H * 0.5f});

                // Skirting board: a strip of trim at the base, which is what
                // stops a corridor reading as a cardboard box.
                const float TH = 0.14f;
                v3 ta = a, tb = b, tc = c, td = d;
                tc = b + v3(0, TH, 0);
                td = a + v3(0, TH, 0);
                v3 push = n * 0.02f;
                quad(tv, ti, a + push, b + push, tc + push, td + push, n, tan,
                     {u0, 0.0f}, {u1, TH * 2.0f});
            }
        }
    }

    walls_.build(wv, wi);
    floor_.build(fv, fi);
    ceiling_.build(cv, ci);
    trim_.build(tv, ti);
}

bool Level::find_spawn(char tag, v3* out) const {
    for (const auto& s : spawns_) {
        if (s.tag == tag) { *out = cell_center(s.x, s.z); return true; }
    }
    return false;
}

std::vector<v3> Level::all_spawns(char tag) const {
    std::vector<v3> out;
    for (const auto& s : spawns_) {
        if (s.tag == tag) out.push_back(cell_center(s.x, s.z));
    }
    return out;
}

bool Level::line_of_sight(v3 a, v3 b) const {
    v3 d = b - a;
    d.y = 0;
    float dist = length(d);
    if (dist < 0.001f) return true;
    int steps = (int)(dist / (CELL * 0.35f)) + 2;
    for (int i = 1; i < steps; i++) {
        float t = (float)i / steps;
        v3 p = a + d * t;
        if (solid_at(p.x, p.z)) return false;
    }
    return true;
}

void Level::build_flow(int tx, int tz, std::vector<int16_t>* out) const {
    out->assign((size_t)w_ * d_, -1);
    if (!walkable(tx, tz)) {
        // Snap to the nearest walkable cell so a target inside geometry (or a
        // player clipped into a wall) still produces a usable field.
        int best = -1, bx = tx, bz = tz;
        for (int z = 0; z < d_; z++) {
            for (int x = 0; x < w_; x++) {
                if (!walkable(x, z)) continue;
                int dd = (x - tx) * (x - tx) + (z - tz) * (z - tz);
                if (best < 0 || dd < best) { best = dd; bx = x; bz = z; }
            }
        }
        if (best < 0) return;
        tx = bx; tz = bz;
    }

    std::deque<int> q;
    (*out)[(size_t)tz * w_ + tx] = 0;
    q.push_back(tz * w_ + tx);
    const int dx[4] = {1, -1, 0, 0};
    const int dz[4] = {0, 0, 1, -1};
    while (!q.empty()) {
        int cur = q.front();
        q.pop_front();
        int cx = cur % w_, cz = cur / w_;
        int16_t nd = (int16_t)((*out)[cur] + 1);
        for (int i = 0; i < 4; i++) {
            int nx = cx + dx[i], nz = cz + dz[i];
            if (nx < 0 || nz < 0 || nx >= w_ || nz >= d_) continue;
            if (!walkable(nx, nz)) continue;
            size_t k = (size_t)nz * w_ + nx;
            if ((*out)[k] >= 0) continue;
            (*out)[k] = nd;
            q.push_back((int)k);
        }
    }
}

bool Level::flow_step(const std::vector<int16_t>& flow, int x, int z,
                      int* nx, int* nz) const {
    if (x < 0 || z < 0 || x >= w_ || z >= d_) return false;
    int16_t here = flow[(size_t)z * w_ + x];
    if (here <= 0) return false;
    int16_t best = here;
    int bx = x, bz = z;
    const int dx[4] = {1, -1, 0, 0};
    const int dz[4] = {0, 0, 1, -1};
    for (int i = 0; i < 4; i++) {
        int cx = x + dx[i], cz = z + dz[i];
        if (cx < 0 || cz < 0 || cx >= w_ || cz >= d_) continue;
        int16_t v = flow[(size_t)cz * w_ + cx];
        if (v >= 0 && v < best) { best = v; bx = cx; bz = cz; }
    }
    if (bx == x && bz == z) return false;
    *nx = bx;
    *nz = bz;
    return true;
}

}  // namespace hl
